#include "fontlib.h"

#include "units.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ImTypeset::detail
{

namespace
{

// Used only when OS/2 gives no sxHeight (version 0 and 1 tables do not).
constexpr double X_HEIGHT_FALLBACK = 0.45;

// The BMP private-use block a 16-bit ImWchar build draws supplementary
// codepoints through.
constexpr uint32_t STAND_IN_FIRST = 0xE000;
constexpr uint32_t STAND_IN_LAST = 0xF8FF;

// Work a hostile cmap may cost while it is inverted: glyphs visited.
constexpr size_t CMAP_INVERSION_BUDGET = size_t(1) << 23;

// A role with no file of its own, and the role whose file stands in for it.
bool stand_in_for(Face face, Face* other)
{
    if (face != Face::CODE_BOLD)
        return false;
    *other = Face::PROSE_BOLD;
    return true;
}

bool valid(Face face)
{
    return static_cast<unsigned>(face) < static_cast<unsigned>(FACE_COUNT);
}

void set(std::string* why, const std::string& reason)
{
    if (why != nullptr)
        *why = reason;
}

std::string basename_of(const char* path)
{
    const char* slash = std::strrchr(path, '/');
    const char* backslash = std::strrchr(path, '\\');
    const char* base = path;
    if (slash != nullptr)
        base = slash + 1;
    if (backslash != nullptr && backslash + 1 > base)
        base = backslash + 1;
    return base;
}

template<typename Pairs>
auto find_first(const Pairs& sorted, uint32_t key)
{
    const auto it = std::lower_bound(
        sorted.begin(), sorted.end(), key,
        [](const std::pair<uint32_t, uint32_t>& entry, uint32_t k) { return entry.first < k; });
    return it != sorted.end() && it->first == key ? it : sorted.end();
}

} // namespace

// --- FaceData -------------------------------------------------------------------------

bool FaceData::init(std::vector<uint8_t> bytes, std::string name, const FontLoadOptions& options,
                    std::string* why)
{
    _bytes = std::move(bytes);
    _name = std::move(name);
    if (!_font.load(_bytes.data(), _bytes.size(), why, options))
        return false;
    const int raw = _font.os2_x_height();
    _x_height =
        raw != 0 ? raw : static_cast<int>(py_round(X_HEIGHT_FALLBACK * _font.units_per_em()));
    for (uint32_t cp = 0; cp < _latin.size(); ++cp) {
        uint32_t glyph = 0;
        _latin[cp] = _font.lookup(cp, &glyph) ? glyph + 1 : 0;
    }
    if (!mint(why))
        return false;
    build_stand_ins();
    return true;
}

// A codepoint for every glyph a vertical variant chain can reach, minting one
// where the font encodes none. The unencoded glyphs are taken in order of
// their names, which is what the reference sorts by, and assigned downward
// from SYNTHETIC_FIRST. The names are fontTools' glyph order: the CFF charset,
// or a format 1 or 2 post table. A font with neither is minted in glyph
// order, which is the order of the names fontTools makes up for glyphs the
// cmap does not reach ("glyphNNNNN").
bool FaceData::mint(std::string* why)
{
    if (!_font.has_math())
        return true;
    std::vector<uint32_t> reachable;
    for (const uint16_t base : _font.math_vertical_bases())
        for (const MathVariantRecord& record : _font.math_vertical_variants(base))
            reachable.push_back(record.glyph);
    std::sort(reachable.begin(), reachable.end());
    reachable.erase(std::unique(reachable.begin(), reachable.end()), reachable.end());

    // The lowest codepoint mapped to each reachable glyph. The cmap is sorted
    // by codepoint, so the first range to reach a glyph has it.
    constexpr uint32_t NONE = 0xFFFFFFFFu;
    std::vector<uint32_t> lowest(reachable.size(), NONE);
    size_t budget = CMAP_INVERSION_BUDGET;
    for (const CmapRange& range : _font.cmap()) {
        const uint32_t end = range.last_glyph();
        auto it = std::lower_bound(reachable.begin(), reachable.end(), range.glyph);
        for (; it != reachable.end() && *it <= end; ++it) {
            if (budget-- == 0) {
                set(why, "the font's cmap maps too many codepoints onto its math variants");
                return false;
            }
            uint32_t& slot = lowest[static_cast<size_t>(it - reachable.begin())];
            if (slot == NONE)
                slot = range.one_glyph ? range.first : range.first + (*it - range.glyph);
        }
    }

    std::vector<uint32_t> unencoded;
    for (size_t i = 0; i < reachable.size(); ++i)
        if (lowest[i] == NONE)
            unencoded.push_back(reachable[i]);
    std::vector<std::string> names;
    const bool named = !unencoded.empty() && _font.glyph_order_names(names) &&
                       std::all_of(unencoded.begin(), unencoded.end(), [&](uint32_t g) {
                           return g < names.size() && !names[g].empty();
                       });
    if (named) {
        // Bytewise (std::string compares bytes as unsigned), which is how
        // Python orders the Latin-1 strings fontTools makes of glyph names.
        // Stable, so equal names keep glyph order.
        std::stable_sort(unencoded.begin(), unencoded.end(),
                         [&](uint32_t a, uint32_t b) { return names[a] < names[b]; });
    }
    if (unencoded.size() > SYNTHETIC_FIRST - SYNTHETIC_LAST + 1) {
        set(why, "the font needs more synthetic codepoints than U+F0000..U+FFFFD holds");
        return false;
    }
    _minted.reserve(unencoded.size());
    for (size_t i = 0; i < unencoded.size(); ++i) {
        const uint32_t codepoint = SYNTHETIC_FIRST - static_cast<uint32_t>(i);
        // Minting over a codepoint the font uses would make a real glyph
        // unreachable, silently.
        if (_font.lookup(codepoint, nullptr)) {
            set(why, "the font already maps a codepoint in U+F0000..U+FFFFD that a math variant "
                     "needs");
            return false;
        }
        _minted.push_back({codepoint, unencoded[i]});
    }

    _variant_codepoints.reserve(reachable.size());
    for (size_t i = 0; i < reachable.size(); ++i)
        if (lowest[i] != NONE)
            _variant_codepoints.emplace_back(reachable[i], lowest[i]);
    for (const SyntheticCodepoint& minted : _minted)
        _variant_codepoints.emplace_back(minted.glyph, minted.codepoint);
    std::sort(_variant_codepoints.begin(), _variant_codepoints.end());
    return true;
}

void FaceData::build_stand_ins()
{
    // Most fonts have nothing above U+FFFF and need none.
    const std::vector<CmapRange>& cmap = _font.cmap();
    if (_minted.empty() && (cmap.empty() || cmap.back().last <= 0xFFFFu))
        return;
    // The private-use codepoints the font leaves free, ascending.
    std::vector<uint32_t> slots;
    for (uint32_t slot = STAND_IN_FIRST; slot <= STAND_IN_LAST; ++slot)
        if (!_font.lookup(slot, nullptr))
            slots.push_back(slot);

    // What gets one, while they last: the minted codepoints first, in the
    // order they were minted, because nothing else can draw those glyphs;
    // then the supplementary codepoints of the font's own cmap, ascending.
    std::vector<uint32_t> served;
    uint64_t asked = 0;
    for (const SyntheticCodepoint& minted : _minted) {
        ++asked;
        if (served.size() < slots.size())
            served.push_back(minted.codepoint);
    }
    for (const CmapRange& range : _font.cmap()) {
        if (range.last <= 0xFFFFu || range.first > 0x10FFFFu)
            continue;
        const uint32_t first = std::max<uint32_t>(range.first, 0x10000u);
        const uint32_t last = std::min<uint32_t>(range.last, 0x10FFFFu);
        asked += uint64_t(last) - first + 1;
        for (uint32_t cp = first; cp <= last && served.size() < slots.size(); ++cp)
            served.push_back(cp);
    }
    std::sort(served.begin(), served.end());
    served.erase(std::unique(served.begin(), served.end()), served.end());

    _stand_ins.reserve(served.size());
    _stood_in.reserve(served.size());
    for (size_t i = 0; i < served.size(); ++i) {
        _stand_ins.emplace_back(slots[i], served[i]);
        _stood_in.emplace_back(served[i], slots[i]);
    }
    _stand_ins_unserved = static_cast<size_t>(asked - served.size());
    if (_stand_ins_unserved != 0) {
        _warnings.push_back(
            std::to_string(_stand_ins_unserved) +
            " supplementary codepoints have no private-use stand-in and cannot be drawn "
            "through a 16-bit ImWchar");
    }
}

bool FaceData::lookup(uint32_t codepoint, uint32_t* glyph) const
{
    if (codepoint < _latin.size()) {
        if (_latin[codepoint] == 0)
            return false;
        if (glyph != nullptr)
            *glyph = _latin[codepoint] - 1;
        return true;
    }
    if (_font.lookup(codepoint, glyph))
        return true;
    if (codepoint < SYNTHETIC_LAST || codepoint > SYNTHETIC_FIRST)
        return false;
    // Assigned downward without gaps, so the codepoint is its own index.
    const size_t index = SYNTHETIC_FIRST - codepoint;
    if (index >= _minted.size())
        return false;
    if (glyph != nullptr)
        *glyph = _minted[index].glyph;
    return true;
}

uint32_t FaceData::variant_codepoint(uint32_t glyph) const
{
    const auto it = find_first(_variant_codepoints, glyph);
    return it != _variant_codepoints.end() ? it->second : 0;
}

uint32_t FaceData::atlas_codepoint(uint32_t codepoint, int wchar_bits) const
{
    if (wchar_bits >= 32)
        return codepoint;
    if (codepoint <= 0xFFFFu) {
        // A stand-in is a codepoint the face itself does not map. Asked for as
        // a layout codepoint it is still unmapped, whatever the atlas now
        // holds there.
        const bool taken = find_first(_stand_ins, codepoint) != _stand_ins.end();
        return taken ? 0 : codepoint;
    }
    const auto it = find_first(_stood_in, codepoint);
    return it != _stood_in.end() ? it->second : 0;
}

bool FaceData::glyph_for_atlas_codepoint(uint32_t atlas_codepoint, int wchar_bits,
                                         uint32_t* glyph) const
{
    if (wchar_bits < 32) {
        if (atlas_codepoint > 0xFFFFu)
            return false;
        const auto it = find_first(_stand_ins, atlas_codepoint);
        if (it != _stand_ins.end())
            return lookup(it->second, glyph);
    }
    return lookup(atlas_codepoint, glyph);
}

// --- Metrics --------------------------------------------------------------------------

// FontMetrics over a real font file: one role at one size. Every answer is a
// font-unit integer (or, for an ink box, a double) times `size / unitsPerEm`,
// with that ratio formed once; the reference does exactly that, and the
// goldens hold the doubles.
class FontLibrary::Metrics final : public MathFontMetrics
{
public:
    Metrics(Face face, const FaceData& data, double size, bool is_math)
        : _face(face)
        , _data(data)
        , _size(size)
        , _scale(size / data.units_per_em())
        , _is_math(is_math)
    {
        if (!is_math)
            return;
        const OpenTypeFont& font = data.font();
        const auto dimen = [&](MathConstant c) { return font.math_constant(c) * _scale; };
        MathParams& p = _params;
        p.math_x_height = x_height();
        p.math_quad = size;
        p.num1 = dimen(FractionNumeratorDisplayStyleShiftUp);
        p.num2 = dimen(FractionNumeratorShiftUp);
        p.num3 = dimen(StackTopShiftUp);
        p.denom1 = dimen(FractionDenominatorDisplayStyleShiftDown);
        p.denom2 = dimen(FractionDenominatorShiftDown);
        p.sup1 = dimen(SuperscriptShiftUp);
        p.sup2 = dimen(SuperscriptShiftUp);
        p.sup3 = dimen(SuperscriptShiftUpCramped);
        p.sub1 = dimen(SubscriptShiftDown);
        p.sub2 = dimen(SubscriptShiftDown);
        p.sup_drop = dimen(SuperscriptBaselineDropMax);
        p.sub_drop = dimen(SubscriptBaselineDropMin);
        p.delim1 = 2.4 * size;
        p.delim2 = 1.0 * size;
        p.axis_height = dimen(AxisHeight);
        p.default_rule_thickness = dimen(FractionRuleThickness);
        p.big_op_spacing1 = dimen(UpperLimitGapMin);
        p.big_op_spacing2 = dimen(LowerLimitGapMin);
        p.big_op_spacing3 = dimen(UpperLimitBaselineRiseMin);
        p.big_op_spacing4 = dimen(LowerLimitBaselineDropMin);
        p.big_op_spacing5 = 0.1 * size;
        p.radical_rule_thickness = dimen(RadicalRuleThickness);
        p.radical_vertical_gap = dimen(RadicalVerticalGap);
        p.radical_display_style_vertical_gap = dimen(RadicalDisplayStyleVerticalGap);
        p.script_scale = font.math_constant(ScriptPercentScaleDown) / 100.0;
        p.script_script_scale = font.math_constant(ScriptScriptPercentScaleDown) / 100.0;
        p.display_operator_min_height = dimen(DisplayOperatorMinHeight);

        // Every vertical chain at this size, laid end to end, so that asking
        // for one allocates nothing.
        const std::vector<uint16_t>& bases = font.math_vertical_bases();
        _chain_starts.reserve(bases.size() + 1);
        for (const uint16_t base : bases) {
            _chain_starts.push_back(_chains.size());
            for (const MathVariantRecord& record : font.math_vertical_variants(base))
                _chains.push_back(GlyphVariant{data.variant_codepoint(record.glyph), record.glyph,
                                               record.advance * _scale});
        }
        _chain_starts.push_back(_chains.size());
    }

    Face face() const override { return _face; }
    double size() const override { return _size; }
    double ascent() const override { return _data.ascent() * _scale; }
    double descent() const override { return _data.descent() * _scale; }
    double x_height() const override { return _data.x_height() * _scale; }
    double em() const override { return _size; }
    double space_width() const override { return advance(0x20); }

    bool has(uint32_t codepoint) const override { return _data.has(codepoint); }

    double advance(uint32_t codepoint) const override
    {
        return _data.font().advance(_data.glyph(codepoint)) * _scale;
    }

    GlyphBBox bbox(uint32_t codepoint) const override
    {
        const GlyphBounds b = _data.font().bounds(_data.glyph(codepoint));
        return GlyphBBox{b.x_min * _scale, b.y_min * _scale, b.x_max * _scale, b.y_max * _scale};
    }

    double kern(uint32_t codepoint, uint32_t following) const override
    {
        // Held to +-MAX_DIMEN, the layout core's invariant (units.h), here
        // where the font's answer becomes a length: a 'kern' table's
        // subtables add up, to 2^31 units for one pair, where every other
        // value this class answers is 16 bits a glyph. A value inside the
        // bound is returned as computed.
        const double value =
            _data.font().pair_kern(_data.glyph(codepoint), _data.glyph(following)) * _scale;
        const double limit = static_cast<double>(MAX_DIMEN);
        return value > limit ? limit : (value < -limit ? -limit : value);
    }

    const MathFontMetrics* as_math() const override { return _is_math ? this : nullptr; }
    const MathParams& math() const override { return _params; }

    double italic_correction(uint32_t codepoint) const override
    {
        return _data.font().math_italic_correction(_data.glyph(codepoint)) * _scale;
    }

    GlyphVariants vertical_variants(uint32_t codepoint) const override
    {
        if (!_is_math)
            return GlyphVariants{};
        const std::vector<uint16_t>& bases = _data.font().math_vertical_bases();
        const uint32_t glyph = _data.glyph(codepoint);
        const auto it = std::lower_bound(bases.begin(), bases.end(), glyph);
        if (it == bases.end() || *it != glyph)
            return GlyphVariants{};
        const size_t index = static_cast<size_t>(it - bases.begin());
        return GlyphVariants{_chains.data() + _chain_starts[index],
                             _chain_starts[index + 1] - _chain_starts[index]};
    }

private:
    Face _face;
    const FaceData& _data;
    double _size;
    double _scale;
    bool _is_math;
    MathParams _params;
    std::vector<GlyphVariant> _chains;
    std::vector<size_t> _chain_starts;
};

// --- FontLibrary ------------------------------------------------------------------------

FontLibrary::FontLibrary() = default;
FontLibrary::~FontLibrary() = default;

void FontLibrary::registry_changed()
{
    // Every size of every role: a stand-in role reads another role's file, so
    // what a change invalidates is not one role's metrics.
    for (MetricsCache& cache : _metrics) {
        for (auto* by_size : {&cache.recent, &cache.previous}) {
            for (auto& entry : *by_size)
                _retired_metrics.push_back(std::move(entry.second));
            by_size->clear();
        }
    }
    ++_generation;
}

void FontLibrary::release_expired_metrics()
{
    _expired_metrics.clear();
}

size_t FontLibrary::metrics_cached(Face face) const
{
    if (!valid(face))
        return 0;
    const MetricsCache& cache = _metrics[static_cast<size_t>(face)];
    return cache.recent.size() + cache.previous.size();
}

bool FontLibrary::register_font(Face face, const void* data, size_t size, const char* name,
                                std::string* why, int face_index)
{
    if (!valid(face)) {
        set(why, "not a font role");
        return false;
    }
    if (data == nullptr || size == 0) {
        set(why, "the font is empty");
        return false;
    }
    if (face_index < 0) {
        set(why, "face index " + std::to_string(face_index) + " is negative");
        return false;
    }
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    std::unique_ptr<FaceData> parsed(new FaceData());
    FontLoadOptions options;
    options.face_index = static_cast<uint32_t>(face_index);
    options.legacy_kern = _legacy_kern;
    if (!parsed->init(std::vector<uint8_t>(bytes, bytes + size), name != nullptr ? name : "",
                      options, why))
        return false;
    std::unique_ptr<FaceData>& slot = _registered[static_cast<size_t>(face)];
    if (slot)
        _retired_data.push_back(std::move(slot));
    slot = std::move(parsed);
    registry_changed();
    return true;
}

bool FontLibrary::register_font(Face face, const char* path, std::string* why, int face_index)
{
    if (path == nullptr || path[0] == '\0') {
        set(why, "no font file was named");
        return false;
    }
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        set(why, std::string(face_value(face)) + " was registered as " + path +
                     ", which cannot be opened");
        return false;
    }
    std::vector<uint8_t> bytes;
    uint8_t buffer[65536];
    for (;;) {
        const size_t n = std::fread(buffer, 1, sizeof buffer, file);
        bytes.insert(bytes.end(), buffer, buffer + n);
        if (n < sizeof buffer)
            break;
    }
    const bool read_ok = std::ferror(file) == 0;
    std::fclose(file);
    if (!read_ok) {
        set(why, std::string("reading ") + path + " failed");
        return false;
    }
    const std::string name = basename_of(path);
    return register_font(face, bytes.data(), bytes.size(), name.c_str(), why, face_index);
}

void FontLibrary::unregister_font(Face face)
{
    if (!valid(face))
        return;
    std::unique_ptr<FaceData>& slot = _registered[static_cast<size_t>(face)];
    if (!slot)
        return;
    _retired_data.push_back(std::move(slot));
    registry_changed();
}

bool FontLibrary::is_registered(Face face) const
{
    return valid(face) && _registered[static_cast<size_t>(face)] != nullptr;
}

Face FontLibrary::backing_face(Face face) const
{
    Face other = face;
    if (!is_registered(face) && stand_in_for(face, &other))
        return other;
    return face;
}

const FaceData* FontLibrary::face_data(Face face, std::string* why) const
{
    if (!valid(face)) {
        set(why, "not a font role");
        return nullptr;
    }
    const FaceData* data = _registered[static_cast<size_t>(backing_face(face))].get();
    if (data == nullptr)
        set(why, std::string(face_value(face)) + " has no font registered");
    return data;
}

const FontMetrics* FontLibrary::metrics(Face face, double size, std::string* why)
{
    const FaceData* data = face_data(face, why);
    if (data == nullptr)
        return nullptr;
    uint64_t key = 0;
    std::memcpy(&key, &size, sizeof key);
    MetricsCache& cache = _metrics[static_cast<size_t>(face)];
    const auto found = cache.recent.find(key);
    if (found != cache.recent.end())
        return found->second.get();
    const bool is_math = face == Face::MATH;
    if (is_math && !data->font().has_math()) {
        set(why, std::string(face_value(face)) + " maps to " + data->name() +
                     ", which has no MATH table");
        return nullptr;
    }
    // Two generations. A size asked for goes into the recent one; when that
    // is full it becomes the previous one, and what was the previous one has
    // then gone METRICS_SIZES_KEPT admissions without being asked for and
    // expires.
    std::unique_ptr<Metrics> object;
    const auto older = cache.previous.find(key);
    if (older != cache.previous.end()) {
        object = std::move(older->second);
        cache.previous.erase(older);
    } else {
        object.reset(new Metrics(face, *data, size, is_math));
    }
    if (cache.recent.size() >= METRICS_SIZES_KEPT) {
        for (auto& entry : cache.previous)
            _expired_metrics.push_back(std::move(entry.second));
        cache.previous = std::move(cache.recent);
        cache.recent.clear();
    }
    Metrics* made = object.get();
    cache.recent.emplace(key, std::move(object));
    return made;
}

const MathFontMetrics* FontLibrary::math_metrics(double size, std::string* why)
{
    const FontMetrics* found = metrics(Face::MATH, size, why);
    return found != nullptr ? found->as_math() : nullptr;
}

double FontLibrary::em_size_for_pixel_height(Face face, double pixel_height) const
{
    const FaceData* data = face_data(face);
    if (data == nullptr)
        return pixel_height;
    const int extent = data->ascent() + data->descent();
    if (extent <= 0)
        return pixel_height;
    return pixel_height * data->units_per_em() / extent;
}

} // namespace ImTypeset::detail
