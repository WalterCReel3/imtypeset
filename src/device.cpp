#include "device.h"

#include "context.h"
#include "draw.h"
#include "font_loader.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace ImTypeset::detail
{

namespace
{

enum class Ask {
    ANSWERED,
    // The atlas has no glyph to answer with: the font lacks the character, or
    // this build's ImWchar cannot name it. The font file answers, for good.
    NO_GLYPH,
    // The atlas could not be asked now and may be later: it would not make
    // an entry of the size, or the context is not bound to it.
    NO_ATLAS,
};

// The atlas entry `font`'s face is drawn from at `size` on `device`, held to
// its size: ImGui rounds the size asked for to a whole one and no further,
// and an entry further from it than that is another size's (src/draw.h).
ImFontBaked* entry_of(ImFont* atlas_font, const FontMetrics& font, Scaled size,
                      const Device& device)
{
    const double requested = face_bake_size(&font, size, device.px_per_inch, device.density);
    if (requested == 0.0)
        return nullptr;
    ImFontBaked* baked = face_bake(atlas_font, &font, size, device.px_per_inch, device.density);
    if (baked == nullptr)
        return nullptr;
    if (!(std::fabs(static_cast<double>(baked->Size) - requested) <= 0.5 + 1.0e-3))
        return nullptr;
    return baked;
}

// Whether the context can ask the atlas about a role's font at all, and the
// font if so.
ImFont* atlas_font_of(ImTypesetContext& ctx, int role)
{
    if (role < 0 || role >= FACE_COUNT)
        return nullptr;
    // Nothing of ImGui's is touched unless the atlas is the one the face's
    // font was added to and still holds it: a layout can be asked for with
    // the ImGui context gone, or after the host took the font away.
    if (!core::bound(ctx) || !core::backend_has_textures() || !core::role_font_in_atlas(ctx, role))
        return nullptr;
    const ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    // A locked atlas loads nothing and answers its fallback's advance for a
    // glyph it has not loaded.
    if (atlas == nullptr || atlas->Locked)
        return nullptr;
    return ctx.fonts.faces[role].font;
}

// The entry's own advance for a character, as ImGui holds it: a float in
// units of ImGui's coordinates at the entry's rasteriser density, which is 1
// (src/draw.h), so in pixels of the device. `atlas_codepoint` is what the
// atlas was asked for, and `entry` the entry asked.
//
// ImFontBaked::GetCharAdvance and not FindGlyph()->AdvanceX: the same number
// (tests/test_device_grid.cpp holds the two together), read by ImGui's own
// code and not through the layout of ImFontGlyph, and at 128 px and above,
// where ImGui loads advances without rasterising, without a bitmap.
Ask ask(ImTypesetContext& ctx, const FontMetrics& font, Scaled size, const Device& device,
        uint32_t codepoint, float* raw, uint32_t* atlas_codepoint_out, ImFontBaked** entry)
{
    const int role = static_cast<int>(font.face());
    if (role < 0 || role >= FACE_COUNT || !font.has(codepoint))
        return Ask::NO_GLYPH;
    ImFont* const atlas_font = atlas_font_of(ctx, role);
    if (atlas_font == nullptr)
        return Ask::NO_ATLAS;
    const FaceFont& bound = ctx.fonts.faces[role];
    uint32_t asked = codepoint;
    if (bound.data != nullptr) {
        asked = atlas_codepoint(*bound.data, codepoint);
        if (asked == 0 && codepoint != 0)
            return Ask::NO_GLYPH;
    }
    if (asked > IM_UNICODE_CODEPOINT_MAX)
        return Ask::NO_GLYPH;
    // GetCharAdvance answers the fallback glyph's advance for a character
    // the font has no glyph for.
    if (!atlas_font->IsGlyphInFont(static_cast<ImWchar>(asked)))
        return Ask::NO_GLYPH;
    ImFontBaked* const baked = entry_of(atlas_font, font, size, device);
    if (baked == nullptr)
        return Ask::NO_ATLAS;
    const float advance = baked->GetCharAdvance(static_cast<ImWchar>(asked));
    if (!(advance >= 0.0f && advance <= Drawer::MAX_BAKED_SIZE_PX * 16.0f))
        return Ask::NO_GLYPH;
    *raw = advance;
    if (atlas_codepoint_out != nullptr)
        *atlas_codepoint_out = asked;
    if (entry != nullptr)
        *entry = baked;
    return Ask::ANSWERED;
}

// One face at one size on one device: the font's own metrics with the
// advances the atlas has, on the device's grid if it has one or the face's
// own if it is fixed-pitch. See src/device.h.
class DeviceMetrics final : public DeviceFace
{
public:
    DeviceMetrics(ImTypesetContext& ctx, const FontMetrics& font, Scaled size, Device device)
        : _ctx(ctx)
        , _font(font)
        , _size(size)
        , _device(device)
        // sp_from_px's factor, with the density divided out first: a length
        // in framebuffer pixels as scaled points.
        , _sp_per_px(PT_PER_INCH / (device.px_per_inch * device.density) *
                     static_cast<double>(UNITY))
    {
    }

    Face face() const override { return _font.face(); }
    double size() const override { return _font.size(); }
    double ascent() const override { return _font.ascent(); }
    double descent() const override { return _font.descent(); }
    double x_height() const override { return _font.x_height(); }
    double em() const override { return _font.em(); }
    double space_width() const override { return advance(0x20); }
    bool has(uint32_t codepoint) const override { return _font.has(codepoint); }
    GlyphBBox bbox(uint32_t codepoint) const override { return _font.bbox(codepoint); }

    double advance(uint32_t codepoint) const override { return scaled(pixels(codepoint)); }

    // ImGui's text has no pair kerns, and a kern is a fraction of a pixel.
    double kern(uint32_t, uint32_t) const override { return 0.0; }

    double cell() const override { return scaled(_device.cell > 0.0 ? _device.cell : own_cell()); }

    uint16_t guest_device(uint16_t own) const override
    {
        if (_device.cell > 0.0)
            return own;
        const double cell = own_cell();
        if (!(cell > 0.0))
            return own;
        const uint16_t grid = grid_device(_ctx, _device, cell);
        return grid != 0 ? grid : own;
    }

    bool current(bool thorough) override
    {
        const int role = static_cast<int>(_font.face());
        ImFont* const atlas_font = atlas_font_of(_ctx, role);
        // Nothing to ask: what is held stands until there is.
        if (atlas_font == nullptr)
            return true;
        ImFontBaked* const baked = entry_of(atlas_font, _font, _size, _device);
        if (baked == nullptr)
            return true;
        // It could not ask, and measured from the font file; now it can.
        if (_unanswered)
            return false;
        if (_read.empty())
            return true;
        const ImWchar witness = static_cast<ImWchar>(_read[0].asked);
        // A glyph leaves its entry when the entry is destroyed, which is what
        // rebuilding a font's output does. An entry that never held it (at
        // 128 px and above ImGui loads advances alone) cannot tell that way.
        const bool rebuilt = !_witness_loaded || !baked->IsGlyphLoaded(witness);
        const size_t count = thorough || rebuilt ? _read.size() : 1;
        for (size_t i = 0; i < count; ++i) {
            if (baked->GetCharAdvance(static_cast<ImWchar>(_read[i].asked)) != _read[i].raw)
                return false;
        }
        _witness_loaded = baked->IsGlyphLoaded(witness);
        return true;
    }

private:
    struct Read {
        uint32_t asked = 0; // of the atlas: `atlas_codepoint` of the character
        float raw = 0.0f;   // the entry's advance for it
    };

    // A length in framebuffer pixels as scaled points, rounded up to a whole
    // one. Up, and not to the nearest: a whole pixel is not a whole number of
    // scaled points, so a position along a line is a whole number of pixels
    // and a little, and with the little never negative a line that starts at
    // exactly half a pixel has every word of it rounded the same way.
    double scaled(double px) const { return px > 0.0 ? std::ceil(px * _sp_per_px) : 0.0; }

    // The font file's advance, in framebuffer pixels rounded to a whole one.
    double from_font(uint32_t codepoint) const
    {
        const double px = _font.advance(codepoint) / _sp_per_px;
        return px > 0.0 ? snap_px(px) : 0.0;
    }

    // The entry's advance rounded to a whole framebuffer pixel, which is
    // what ImFontConfig::PixelSnapH makes of one; the font file's where the
    // atlas has no answer.
    double from_atlas(uint32_t codepoint) const
    {
        if (codepoint < ASCII) {
            if (_ascii_known[codepoint])
                return _ascii[codepoint];
        } else {
            const auto found = _others.find(codepoint);
            if (found != _others.end())
                return found->second;
        }
        const double px = read(codepoint);
        if (codepoint < ASCII) {
            _ascii[codepoint] = px;
            _ascii_known[codepoint] = true;
        } else {
            _others.emplace(codepoint, px);
        }
        return px;
    }

    double read(uint32_t codepoint) const
    {
        float raw = 0.0f;
        uint32_t asked = 0;
        ImFontBaked* baked = nullptr;
        switch (ask(_ctx, _font, _size, _device, codepoint, &raw, &asked, &baked)) {
        case Ask::ANSWERED:
            break;
        case Ask::NO_ATLAS:
            _unanswered = true;
            return from_font(codepoint);
        case Ask::NO_GLYPH:
            return from_font(codepoint);
        }
        if (_read.empty()) {
            _witness_loaded = baked->IsGlyphLoaded(static_cast<ImWchar>(asked));
        } else if (_witness_loaded && !baked->IsGlyphLoaded(static_cast<ImWchar>(_read[0].asked))) {
            // The entry was made again since the first advance was read from
            // it: what was read before may be another rasterisation's. This
            // is inside a layout; the next `device_number` looks.
            _ctx.devices.suspect = true;
        }
        Read read;
        read.asked = asked;
        read.raw = raw;
        _read.push_back(read);
        return snap_px(static_cast<double>(raw));
    }

    // This face's cell in framebuffer pixels if it is fixed-pitch, or zero.
    double own_cell() const
    {
        if (_pitch == Pitch::UNKNOWN) {
            _pitch = Pitch::VARIES;
            double first = -1.0;
            int mapped = 0;
            bool one = true;
            for (uint32_t c = 0x20; c <= 0x7E && one; ++c) {
                if (!_font.has(c))
                    continue;
                const double advance = _font.advance(c);
                if (first < 0.0)
                    first = advance;
                one = advance == first;
                ++mapped;
            }
            if (one && mapped >= 2 && first > 0.0) {
                double cell = 0.0;
                for (uint32_t c = 0x20; c <= 0x7E; ++c) {
                    if (_font.has(c))
                        cell = std::max(cell, from_atlas(c));
                }
                if (cell >= 1.0) {
                    _own_cell = cell;
                    _pitch = Pitch::FIXED;
                }
            }
        }
        return _pitch == Pitch::FIXED ? _own_cell : 0.0;
    }

    bool inkless(uint32_t codepoint) const
    {
        const GlyphBBox box = _font.bbox(codepoint);
        return !(box.x_max > box.x_min) || !(box.y_max > box.y_min);
    }

    // The advance in framebuffer pixels, a whole number of them, and of
    // cells where there is a grid: see THE GRID in src/device.h.
    double pixels(uint32_t codepoint) const
    {
        const bool guest = _device.cell > 0.0;
        const double grid = guest ? _device.cell : own_cell();
        if (!_font.has(codepoint)) {
            if (unspaced(codepoint))
                return 0.0;
            return grid > 0.0 ? grid : from_font(codepoint);
        }
        const double px = from_atlas(codepoint);
        if (!(grid > 0.0))
            return px;
        if (guest) {
            if (codepoint == REPLACEMENT_CHARACTER || codepoint == '?')
                return grid;
            return std::max(1.0, std::ceil(px / grid)) * grid;
        }
        if (unspaced(codepoint) && inkless(codepoint))
            return 0.0;
        return std::ceil(px / grid) * grid;
    }

    enum class Pitch : uint8_t { UNKNOWN, FIXED, VARIES };

    ImTypesetContext& _ctx;
    const FontMetrics& _font;
    Scaled _size;
    Device _device;
    double _sp_per_px;
    // What was read from the atlas, in the order it was read: the first is
    // the witness (src/device.h). A character is here once: `from_atlas`
    // remembers its answers, the font file's among them.
    mutable std::vector<Read> _read;
    static constexpr uint32_t ASCII = 128;
    mutable double _ascii[ASCII] = {};
    mutable bool _ascii_known[ASCII] = {};
    mutable std::unordered_map<uint32_t, double> _others;
    mutable bool _witness_loaded = false;
    mutable bool _unanswered = false;
    mutable Pitch _pitch = Pitch::UNKNOWN;
    mutable double _own_cell = 0.0;
};

const FontMetrics* resolve_in_context(void* user, FontKey key)
{
    ImTypesetContext& ctx = *static_cast<ImTypesetContext*>(user);
    FontSpec spec;
    if (!parse_font_key(key, &spec))
        return nullptr;
    const FontMetrics* font = ctx.library.metrics(spec.face, static_cast<double>(spec.size));
    if (font == nullptr || spec.device == 0 || spec.device > ctx.devices.table.size())
        return font;
    const int frame = core::bound(ctx) ? ImGui::GetFrameCount() : -1;
    const auto found = ctx.devices.metrics.find(key.bits);
    if (found != ctx.devices.metrics.end()) {
        if (frame >= 0)
            found->second->touched = frame;
        return found->second.get();
    }
    DeviceFace* made =
        new DeviceMetrics(ctx, *font, spec.size, ctx.devices.table[spec.device - 1u]);
    made->touched = frame;
    ctx.devices.metrics.emplace(key.bits, std::unique_ptr<DeviceFace>(made));
    return made;
}

// Asks every face on a device whether what it remembers is still the
// atlas's, once a frame and whenever something gave reason to; forgets
// everything and moves the epoch if one says no. With the context bound.
void check_remembered(ImTypesetContext& ctx)
{
    DeviceState& state = ctx.devices;
    const int frame = ImGui::GetFrameCount();
    if (frame == state.checked_frame && !state.suspect)
        return;
    const bool thorough = state.suspect;
    state.checked_frame = frame;
    state.suspect = false;
    for (const auto& entry : state.metrics) {
        if (entry.second->touched < frame - 1)
            continue;
        if (!entry.second->current(thorough)) {
            forget_measurements(ctx);
            ++state.epoch;
            return;
        }
    }
}

} // namespace

bool device_advance_px(ImTypesetContext& ctx, const FontMetrics& font, Scaled size,
                       const Device& device, uint32_t codepoint, double* out)
{
    float raw = 0.0f;
    if (ask(ctx, font, size, device, codepoint, &raw, nullptr, nullptr) != Ask::ANSWERED)
        return false;
    // What ImFontConfig::PixelSnapH makes of an advance.
    *out = snap_px(static_cast<double>(raw));
    return true;
}

uint16_t device_number(ImTypesetContext& ctx, double px_per_inch)
{
    if (!core::bound(ctx) || !core::backend_has_textures())
        return 0;
    check_remembered(ctx);
    DrawArgs args;
    args.rules = ctx.draw_rules;
    const double density = Drawer::density_of(args);
    std::vector<Device>& table = ctx.devices.table;
    if (!ctx.devices.full) {
        for (size_t i = 0; i < table.size(); ++i) {
            if (table[i].px_per_inch == px_per_inch && table[i].density == density &&
                table[i].cell == 0.0)
                return static_cast<uint16_t>(i + 1);
        }
    }
    if (table.size() >= MAX_DEVICES || ctx.devices.full) {
        // The numbers have run out: a host has laid text out at that many
        // sizes of unit, densities or cells. They start again, and nothing
        // resolved under the old ones is current.
        table.clear();
        forget_measurements(ctx);
        ++ctx.devices.epoch;
        ctx.devices.full = false;
    }
    Device device;
    device.px_per_inch = px_per_inch;
    device.density = density;
    table.push_back(device);
    return static_cast<uint16_t>(table.size());
}

uint16_t grid_device(ImTypesetContext& ctx, const Device& base, double cell)
{
    std::vector<Device>& table = ctx.devices.table;
    for (size_t i = 0; i < table.size(); ++i) {
        if (table[i].px_per_inch == base.px_per_inch && table[i].density == base.density &&
            table[i].cell == cell)
            return static_cast<uint16_t>(i + 1);
    }
    if (table.size() >= MAX_DEVICES) {
        // Not emptied here: this is asked from inside a layout, by metrics
        // the emptying would destroy.
        ctx.devices.full = true;
        return 0;
    }
    Device device = base;
    device.cell = cell;
    table.push_back(device);
    return static_cast<uint16_t>(table.size());
}

uint8_t device_faces(ImTypesetContext& ctx, int advances, uint16_t device)
{
    if (device == 0 || advances == ImTypesetAdvances_Font)
        return 0;
    if (advances == ImTypesetAdvances_Default && !ctx.tex_rules.code_on_device)
        return 0;
    unsigned faces = 0;
    for (int role = 0; role < FACE_COUNT; ++role) {
        const bool code =
            role == static_cast<int>(Face::CODE) || role == static_cast<int>(Face::CODE_BOLD);
        if ((advances == ImTypesetAdvances_Device || code) && ctx.fonts.faces[role].font != nullptr)
            faces |= 1u << role;
    }
    return static_cast<uint8_t>(faces);
}

MetricsResolver context_metrics(ImTypesetContext& ctx)
{
    return MetricsResolver{&resolve_in_context, &ctx};
}

void forget_measurements(ImTypesetContext& ctx)
{
    ctx.cache.clear();
    ctx.devices.metrics.clear();
}

} // namespace ImTypeset::detail
