#include "style.h"

#include "fontlib.h"
#include "unicode_tables.h"

#include <cinttypes>
#include <cstdio>
#include <limits>
#include <utility>

namespace ImTypeset::detail
{

namespace
{

constexpr int SF_UPPERCASE = 999;
constexpr int SF_SENTENCE_END = 3000;
constexpr int SF_TRANSPARENT = 0;
constexpr int EXTRA_SPACE_THRESHOLD = 2000;
// The largest space factor tex.web allows \spacefactor to be set to.
constexpr int MAX_SPACE_FACTOR = 32767;

void style_set(std::string* why, std::string text)
{
    if (why != nullptr)
        *why = std::move(text);
}

// A length from a font brought inside the layout core's invariant (units.h):
// rounded half to even, then held to +-MAX_DIMEN. The reference has no bound;
// a length past it needs an em of several thousand points.
Scaled rounded_dimen(double value)
{
    const Scaled rounded = round_to_scaled(value);
    return rounded > MAX_DIMEN ? MAX_DIMEN : (rounded < -MAX_DIMEN ? -MAX_DIMEN : rounded);
}

// A glue component held to +-MAX_DIMEN. Not clamp_dimen: that lets the RUNNING
// sentinel through, which is one below -MAX_DIMEN and means nothing in glue.
Scaled glue_dimen(Scaled value)
{
    return value > MAX_DIMEN ? MAX_DIMEN : (value < -MAX_DIMEN ? -MAX_DIMEN : value);
}

// The \sfcode of one character, in the four classes the reference distinguishes.
int sf_code(uint32_t codepoint)
{
    switch (codepoint) {
    case '.':
    case '?':
    case '!':
        return SF_SENTENCE_END;
    case ')':
    case ']':
    case '\'':
    case '"':
    case 0x201D: // right double quotation mark
    case 0x2019: // right single quotation mark
        return SF_TRANSPARENT;
    default:
        return py_isupper(codepoint) ? SF_UPPERCASE : NEUTRAL_SPACE_FACTOR;
    }
}

const FontMetrics* library_resolver(void* user, FontKey key)
{
    FontSpec spec;
    if (!parse_font_key(key, &spec))
        return nullptr;
    return static_cast<FontLibrary*>(user)->metrics(spec.face, static_cast<double>(spec.size));
}

CharDimens measure_thunk(void* user, FontKey font, uint32_t codepoint)
{
    return (*static_cast<CharMeasureCache*>(user))(font, codepoint);
}

bool is_ascii_space(char c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}

void push(Run& run, const Node& node, uint32_t offset)
{
    run.nodes.push_back(node);
    if (run.source_offsets != nullptr)
        run.source_offsets->push_back(run.source_base + offset);
}

// markdown.py `_Converter._append_text`, over decoded text. `offsets` is
// parallel to `text`.
bool append_codepoints(Run& run, const uint32_t* text, const uint32_t* offsets, size_t length,
                       FontSpec font, bool breakable, const Composition& composition,
                       MetricsResolver resolve, std::string* why)
{
    const FontKey key = font.key();
    const FontMetrics* metrics = resolve(key);
    if (metrics == nullptr) {
        style_set(why, "no metrics for " + font_key_text(font.face, font.size));
        return false;
    }
    const Scaled space_width = rounded_dimen(metrics->space_width());
    size_t index = 0;
    while (index < length) {
        const uint32_t codepoint = text[index];
        if (py_isspace(codepoint)) {
            const uint32_t offset = offsets[index];
            while (index < length && py_isspace(text[index]))
                ++index;
            // Leading space at the start of a block: a break here would be
            // illegal and the space itself would be discarded.
            if (run.nodes.empty())
                continue;
            if (!breakable)
                push(run, penalty(INF_PENALTY), offset);
            push(run,
                 glue(interword_glue(space_width, run.space_factor, composition.spacing,
                                     composition.rules)),
                 offset);
            continue;
        }
        const bool covered = metrics->has(codepoint);
        if (covered) {
            push(run, char_node(key, codepoint), offsets[index]);
        } else {
            const StandIn shown = stand_in(composition, resolve, key, codepoint);
            push(run,
                 shown.codepoint == codepoint
                     ? char_node(shown.font, codepoint)
                     : replaced_char(shown.font, shown.codepoint, codepoint),
                 offsets[index]);
        }
        run.space_factor = space_factor_after(codepoint, run.space_factor);
        if (covered && index + 1 < length && !py_isspace(text[index + 1])) {
            const Scaled width = rounded_dimen(metrics->kern(codepoint, text[index + 1]));
            if (width != 0)
                push(run, kern(width, KernKind::NORMAL), offsets[index]);
        }
        ++index;
    }
    return true;
}

// Python's str.strip() on [first, last): the bounds with white space removed
// from both ends.
void strip(const uint32_t* text, size_t& first, size_t& last)
{
    while (first < last && py_isspace(text[first]))
        ++first;
    while (last > first && py_isspace(text[last - 1]))
        --last;
}

} // namespace

// --- Font keys ----------------------------------------------------------------------

FontKey FontSpec::key() const
{
    return font_key(face, size);
}

FontKey font_key(Face face, Scaled size)
{
    return FontKey::make(static_cast<uint32_t>(face), size);
}

bool parse_font_key(FontKey key, FontSpec* out)
{
    if (key.face() >= static_cast<uint32_t>(FACE_COUNT))
        return false;
    if (out != nullptr) {
        out->face = static_cast<Face>(key.face());
        out->size = key.size();
    }
    return true;
}

std::string font_key_text(Face face, Scaled size)
{
    char digits[32];
    std::snprintf(digits, sizeof digits, "@%" PRId64, size);
    return std::string(face_value(face)) + digits;
}

bool parse_font_key_text(std::string_view text, FontSpec* out)
{
    const size_t at = text.rfind('@');
    if (at == std::string_view::npos || at == 0 || at + 1 == text.size())
        return false;
    Face face = Face::PROSE;
    if (!face_from_value(text.substr(0, at), &face))
        return false;

    std::string_view number = text.substr(at + 1);
    while (!number.empty() && is_ascii_space(number.front()))
        number.remove_prefix(1);
    while (!number.empty() && is_ascii_space(number.back()))
        number.remove_suffix(1);
    bool negative = false;
    if (!number.empty() && (number.front() == '+' || number.front() == '-')) {
        negative = number.front() == '-';
        number.remove_prefix(1);
    }
    if (number.empty())
        return false;
    const uint64_t limit = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    uint64_t magnitude = 0;
    bool after_digit = false;
    for (const char c : number) {
        if (c == '_') {
            // An underscore only between two digits.
            if (!after_digit)
                return false;
            after_digit = false;
            continue;
        }
        if (c < '0' || c > '9')
            return false;
        const uint64_t digit = static_cast<uint64_t>(c - '0');
        if (magnitude > (limit - digit) / 10)
            return false;
        magnitude = magnitude * 10 + digit;
        after_digit = true;
    }
    if (!after_digit)
        return false;
    if (out != nullptr) {
        out->face = face;
        out->size = negative ? -static_cast<int64_t>(magnitude) : static_cast<int64_t>(magnitude);
    }
    return true;
}

// --- Metrics --------------------------------------------------------------------------

MetricsResolver fontlib_metrics(FontLibrary& library)
{
    return MetricsResolver{&library_resolver, &library};
}

CharMeasureCache::CharMeasureCache(MetricsResolver resolve)
    : _resolve(resolve)
{
}

size_t CharMeasureCache::hash(uint64_t font, uint32_t codepoint)
{
    uint64_t h = (font + 0x9E3779B97F4A7C15ull) * 0xBF58476D1CE4E5B9ull;
    h ^= (static_cast<uint64_t>(codepoint) + 1) * 0x94D049BB133111EBull;
    h ^= h >> 29;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 32;
    return static_cast<size_t>(h);
}

void CharMeasureCache::grow()
{
    std::vector<Slot> old;
    old.swap(_slots);
    _slots.resize(old.empty() ? 1024 : old.size() * 2);
    const size_t mask = _slots.size() - 1;
    for (const Slot& slot : old) {
        if (!slot.used)
            continue;
        size_t i = hash(slot.font, slot.codepoint) & mask;
        while (_slots[i].used)
            i = (i + 1) & mask;
        _slots[i] = slot;
    }
}

CharDimens CharMeasureCache::operator()(FontKey font, uint32_t codepoint)
{
    // At most half full, so a probe always ends.
    if (_count * 2 >= _slots.size())
        grow();
    const size_t mask = _slots.size() - 1;
    size_t i = hash(font.bits, codepoint) & mask;
    while (_slots[i].used) {
        if (_slots[i].font == font.bits && _slots[i].codepoint == codepoint)
            return _slots[i].dimens;
        i = (i + 1) & mask;
    }
    if (font.bits != _last_font) {
        _last_font = font.bits;
        _last_metrics = _resolve(font);
    }
    CharDimens dimens;
    if (_last_metrics != nullptr) {
        const GlyphBBox box = _last_metrics->bbox(codepoint);
        dimens.width = rounded_dimen(_last_metrics->advance(codepoint));
        dimens.height = rounded_dimen(box.height());
        dimens.depth = rounded_dimen(box.depth());
    }
    Slot& slot = _slots[i];
    slot.font = font.bits;
    slot.codepoint = codepoint;
    slot.used = true;
    slot.dimens = dimens;
    ++_count;
    return dimens;
}

CharMeasure CharMeasureCache::measure()
{
    CharMeasure out;
    out.function = &measure_thunk;
    out.user = this;
    return out;
}

void CharMeasureCache::clear()
{
    _slots.clear();
    _count = 0;
    _last_font = ~uint64_t(0);
    _last_metrics = nullptr;
}

// --- Space factor and interword glue ------------------------------------------------

// tex.web's adjust_space_factor (section 1034), its four branches in its
// order.
int space_factor_after(uint32_t codepoint, int current)
{
    const int code = sf_code(codepoint);
    if (code == NEUTRAL_SPACE_FACTOR)
        return NEUTRAL_SPACE_FACTOR;
    if (code < NEUTRAL_SPACE_FACTOR)
        return code > SF_TRANSPARENT ? code : current;
    if (current < NEUTRAL_SPACE_FACTOR)
        return NEUTRAL_SPACE_FACTOR;
    return code;
}

// The glue of tex.web's app_space (section 1043): the font's space as section
// 1042 finds it, modified as section 1044 says. Two departures, both the
// reference's: the stretch, shrink and extra space are fractions of the space
// width and not \fontdimens, and section 1044's two xn_over_d calls, which
// truncate, are round() of the true quotient here, half to even, unless
// TexRules::sentence_space_truncates.
GlueSpec interword_glue(Scaled space_width, int space_factor, Spacing spacing, TexRules rules)
{
    if (!IMTYPESET_CONTRACT(space_factor >= 1 && space_factor <= MAX_SPACE_FACTOR,
                            "interword_glue: space factor out of range"))
        space_factor = NEUTRAL_SPACE_FACTOR;
    // The space width is held to MAX_DIMEN on the way in, which keeps the
    // products below exact, and the glue on the way out: a sentence space is up
    // to a third wider than the space it is made from, and its stretch up to
    // MAX_SPACE_FACTOR / NEUTRAL_SPACE_FACTOR times as much.
    space_width = glue_dimen(space_width);
    const double natural = static_cast<double>(space_width);
    Scaled stretch = round_to_scaled(natural * SPACE_STRETCH_FRACTION);
    Scaled shrink = round_to_scaled(natural * SPACE_SHRINK_FRACTION);
    Scaled width = space_width;
    if (spacing != Spacing::FRENCH && space_factor != NEUTRAL_SPACE_FACTOR) {
        if (space_factor >= EXTRA_SPACE_THRESHOLD)
            width += round_to_scaled(natural * EXTRA_SPACE_FRACTION);
        if (spacing == Spacing::NONFRENCH && rules.sentence_space_truncates) {
            // xn_over_d (section 107) works on the magnitude and puts the
            // sign back, which is what a division that truncates toward zero
            // does. The products are exact: see above.
            stretch = stretch * space_factor / NEUTRAL_SPACE_FACTOR;
            shrink = shrink * NEUTRAL_SPACE_FACTOR / space_factor;
        } else if (spacing == Spacing::NONFRENCH) {
            // The reference divides two integers, which rounds the exact
            // quotient once. So does this while the products fit a double's 53
            // bits, which a width within MAX_DIMEN keeps them inside.
            stretch = round_to_scaled(static_cast<double>(stretch * space_factor) /
                                      static_cast<double>(NEUTRAL_SPACE_FACTOR));
            shrink = round_to_scaled(static_cast<double>(shrink * NEUTRAL_SPACE_FACTOR) /
                                     static_cast<double>(space_factor));
        }
    }
    return GlueSpec{glue_dimen(width), glue_dimen(stretch), GlueOrder::NORMAL, glue_dimen(shrink),
                    GlueOrder::NORMAL};
}

// --- Strut ------------------------------------------------------------------------------

Strut Strut::filling(Scaled leading)
{
    Strut strut;
    strut.height = round_to_scaled(static_cast<double>(leading) * STRUT_HEIGHT_FRACTION);
    strut.depth = leading - strut.height;
    return strut;
}

void Strut::apply_to(Box& box) const
{
    if (exact) {
        box.height = height;
        box.depth = depth;
        return;
    }
    if (box.height < height)
        box.height = height;
    if (box.depth < depth)
        box.depth = depth;
}

// --- Text ---------------------------------------------------------------------------------

void decode_utf8(std::string_view text, std::vector<uint32_t>& out, std::vector<uint32_t>* offsets)
{
    const size_t length = text.size();
    size_t i = 0;
    while (i < length) {
        const size_t start = i;
        const uint8_t lead = static_cast<uint8_t>(text[i++]);
        uint32_t codepoint = lead;
        if (lead >= 0x80) {
            // How many continuation bytes follow this lead, and the range the
            // first of them must be in: narrower than 80..BF where the full
            // range would admit an overlong form, a surrogate or a value past
            // U+10FFFF (the Unicode Standard's table 3-7).
            int following = 0;
            uint8_t low = 0x80;
            uint8_t high = 0xBF;
            if (lead >= 0xC2 && lead <= 0xDF) {
                following = 1;
                codepoint = lead & 0x1Fu;
            } else if (lead >= 0xE0 && lead <= 0xEF) {
                following = 2;
                codepoint = lead & 0x0Fu;
                if (lead == 0xE0)
                    low = 0xA0;
                else if (lead == 0xED)
                    high = 0x9F;
            } else if (lead >= 0xF0 && lead <= 0xF4) {
                following = 3;
                codepoint = lead & 0x07u;
                if (lead == 0xF0)
                    low = 0x90;
                else if (lead == 0xF4)
                    high = 0x8F;
            } else {
                codepoint = REPLACEMENT_CHARACTER;
            }
            for (; following > 0; --following) {
                const uint8_t next = i < length ? static_cast<uint8_t>(text[i]) : 0;
                if (i >= length || next < low || next > high) {
                    // The byte that does not fit is not consumed: it starts
                    // the next character.
                    codepoint = REPLACEMENT_CHARACTER;
                    break;
                }
                codepoint = (codepoint << 6) | (next & 0x3Fu);
                low = 0x80;
                high = 0xBF;
                ++i;
            }
        }
        out.push_back(codepoint);
        if (offsets != nullptr) {
            // An offset past 32 bits saturates; nothing indexes with it.
            const size_t limit = std::numeric_limits<uint32_t>::max();
            offsets->push_back(static_cast<uint32_t>(start < limit ? start : limit));
        }
    }
}

StandIn stand_in(const Composition& composition, MetricsResolver resolve, FontKey key,
                 uint32_t codepoint)
{
    const StandIn unchanged{key, codepoint};
    if (py_isspace(codepoint) || unicode_in(UNICODE_CATEGORY_CC, codepoint) ||
        unicode_in(UNICODE_CATEGORY_CF, codepoint) || unicode_in(UNICODE_CATEGORY_MN, codepoint) ||
        unicode_in(UNICODE_CATEGORY_ME, codepoint))
        return unchanged;
    FontSpec bound;
    if (!parse_font_key(key, &bound))
        return unchanged;

    // The regular face, then math, then code, each once.
    const Face order[3] = {composition.regular, Face::MATH, composition.code};
    FontKey prose_key;
    const FontMetrics* prose = nullptr;
    for (int i = 0; i < 3; ++i) {
        const Face face = order[i];
        bool seen = false;
        for (int j = 0; j < i; ++j)
            seen = seen || order[j] == face;
        if (seen)
            continue;
        const FontKey other = face == bound.face ? key : font_key(face, bound.size);
        const FontMetrics* metrics = resolve(other);
        if (metrics == nullptr)
            continue;
        if (face == composition.regular) {
            prose_key = other;
            prose = metrics;
        }
        if (face != bound.face && metrics->has(codepoint))
            return StandIn{other, codepoint};
    }
    if (prose != nullptr) {
        for (const uint32_t stand : {REPLACEMENT_CHARACTER, uint32_t('?')}) {
            if (prose->has(stand))
                return StandIn{prose_key, stand};
        }
    }
    return unchanged;
}

bool append_text(Run& run, std::string_view text, FontSpec font, bool breakable,
                 const Composition& composition, MetricsResolver resolve, std::string* why)
{
    run.codepoints.clear();
    run.offsets.clear();
    decode_utf8(text, run.codepoints, &run.offsets);
    return append_codepoints(run, run.codepoints.data(), run.offsets.data(), run.codepoints.size(),
                             font, breakable, composition, resolve, why);
}

void fill_and_break(Align align, std::vector<Node>& nodes)
{
    if (align != Align::RIGHT && align != Align::CENTER) {
        nodes.push_back(penalty(INF_PENALTY));
        nodes.push_back(glue(PARFILLSKIP));
    }
    nodes.push_back(penalty(EJECT_PENALTY));
}

bool append_text_block(Run& run, std::string_view text, FontSpec font, bool hard_breaks,
                       Align align, const Composition& composition, MetricsResolver resolve,
                       std::string* why)
{
    // Checked before anything is appended, so a refusal appends nothing.
    if (resolve(font.key()) == nullptr) {
        style_set(why, "no metrics for " + font_key_text(font.face, font.size));
        return false;
    }
    run.codepoints.clear();
    run.offsets.clear();
    decode_utf8(text, run.codepoints, &run.offsets);

    // `\r\n` and a lone `\r` become `\n`, in place.
    size_t length = 0;
    for (size_t i = 0; i < run.codepoints.size(); ++i) {
        uint32_t codepoint = run.codepoints[i];
        const uint32_t offset = run.offsets[i];
        if (codepoint == '\r') {
            codepoint = '\n';
            if (i + 1 < run.codepoints.size() && run.codepoints[i + 1] == '\n')
                ++i;
        }
        run.codepoints[length] = codepoint;
        run.offsets[length] = offset;
        ++length;
    }

    const uint32_t* codepoints = run.codepoints.data();
    const uint32_t* offsets = run.offsets.data();
    size_t first = 0;
    size_t last = length;
    strip(codepoints, first, last);
    if (!hard_breaks) {
        return append_codepoints(run, codepoints + first, offsets + first, last - first, font, true,
                                 composition, resolve, why);
    }
    // Stripped first, so a trailing newline is not a forced break with nothing
    // after it: the line breaker appends its own terminator and the two
    // together would set an empty last line.
    size_t line_first = first;
    for (;;) {
        size_t line_end = line_first;
        while (line_end < last && codepoints[line_end] != '\n')
            ++line_end;
        size_t from = line_first;
        size_t to = line_end;
        strip(codepoints, from, to);
        if (!append_codepoints(run, codepoints + from, offsets + from, to - from, font, true,
                               composition, resolve, why))
            return false;
        if (line_end >= last)
            break;
        const size_t before = run.nodes.size();
        fill_and_break(align, run.nodes);
        if (run.source_offsets != nullptr) {
            run.source_offsets->insert(run.source_offsets->end(), run.nodes.size() - before,
                                       run.source_base + offsets[line_end]);
        }
        line_first = line_end + 1;
    }
    return true;
}

} // namespace ImTypeset::detail
