#include "device.h"

#include "context.h"
#include "draw.h"
#include "font_loader.h"

#include "imgui.h"

namespace ImTypeset::detail
{

namespace
{

// One face at one size on one device: the font's own metrics with the
// advances the atlas has. See src/device.h.
class DeviceMetrics final : public FontMetrics
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

    // A character the atlas cannot be asked about is measured from the font
    // file: the layout is then the reference's for it, and drawn by the
    // reference's rule.
    double advance(uint32_t codepoint) const override
    {
        double pixels = 0.0;
        if (!device_advance_px(_ctx, _font, _size, _device, codepoint, &pixels))
            return _font.advance(codepoint);
        return pixels * _sp_per_px;
    }

    // ImGui's text has no pair kerns, and a kern is a fraction of a pixel.
    double kern(uint32_t, uint32_t) const override { return 0.0; }

private:
    ImTypesetContext& _ctx;
    const FontMetrics& _font;
    Scaled _size;
    Device _device;
    double _sp_per_px;
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
    const auto found = ctx.devices.metrics.find(key.bits);
    if (found != ctx.devices.metrics.end())
        return found->second.get();
    FontMetrics* made =
        new DeviceMetrics(ctx, *font, spec.size, ctx.devices.table[spec.device - 1u]);
    ctx.devices.metrics.emplace(key.bits, std::unique_ptr<FontMetrics>(made));
    return made;
}

} // namespace

bool device_advance_px(ImTypesetContext& ctx, const FontMetrics& font, Scaled size,
                       const Device& device, uint32_t codepoint, double* out)
{
    const int role = static_cast<int>(font.face());
    if (role < 0 || role >= FACE_COUNT || !font.has(codepoint))
        return false;
    // Nothing of ImGui's is touched unless the atlas is the one the face's
    // font was added to and still holds it: a layout can be asked for with
    // the ImGui context gone, or after the host took the font away.
    if (!core::bound(ctx) || !core::backend_has_textures() || !core::role_font_in_atlas(ctx, role))
        return false;
    const FaceFont& bound = ctx.fonts.faces[role];
    uint32_t asked = codepoint;
    if (bound.data != nullptr) {
        asked = atlas_codepoint(*bound.data, codepoint);
        if (asked == 0 && codepoint != 0)
            return false;
    }
    if (asked > IM_UNICODE_CODEPOINT_MAX)
        return false;
    ImFontBaked* baked = face_bake(bound.font, &font, size, device.px_per_inch, device.density);
    if (baked == nullptr)
        return false;
    const ImFontGlyph* glyph = baked->FindGlyph(static_cast<ImWchar>(asked));
    if (glyph == nullptr)
        return false;
    // What ImFontConfig::PixelSnapH makes of an advance, and for a negative
    // one (no font has one) the floor and not ImGui's truncation.
    const double advance = static_cast<double>(glyph->AdvanceX);
    if (!(advance >= 0.0 && advance <= static_cast<double>(Drawer::MAX_BAKED_SIZE_PX) * 16.0))
        return false;
    *out = snap_px(advance);
    return true;
}

uint16_t device_number(ImTypesetContext& ctx, double px_per_inch)
{
    if (!core::bound(ctx) || !core::backend_has_textures())
        return 0;
    DrawArgs args;
    args.rules = ctx.draw_rules;
    const double density = Drawer::density_of(args);
    std::vector<Device>& table = ctx.devices.table;
    for (size_t i = 0; i < table.size(); ++i) {
        if (table[i].px_per_inch == px_per_inch && table[i].density == density)
            return static_cast<uint16_t>(i + 1);
    }
    if (table.size() >= MAX_DEVICES) {
        // The numbers have run out: a host has laid text out at that many
        // sizes of unit or densities. They start again, and nothing resolved
        // under the old ones is current.
        table.clear();
        forget_measurements(ctx);
        ++ctx.devices.epoch;
    }
    Device device;
    device.px_per_inch = px_per_inch;
    device.density = density;
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
