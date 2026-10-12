// Advances as the rasteriser bakes them: the second way a character can be
// measured, beside the font file's own (src/fontlib).
//
// This is not the reference's. The reference lays text out from the font's
// advances, which are fractions of a pixel, and its draw loop then puts every
// glyph on a whole pixel by rounding the gap from the glyph before it
// (src/draw.h): the two agree to a pixel or so and no better. Dear ImGui
// draws its own text from the advances its rasteriser baked into the atlas,
// which under a hinting loader are whole pixels. So a row of monospace set by
// the reference's rule does not stand on the columns of the row ImGui draws
// beside it from the same atlas entry.
//
// A DEVICE is what a baked advance depends on beyond the face and its size:
// how many of ImGui's units there are to the inch, which turns an em in
// scaled points into a size in units, and how many framebuffer pixels to the
// unit (DrawRules::device_pixels), which is the size the rasteriser is asked
// for. A context numbers the devices it has met, and a font key carries the
// number (FontSpec::device), so that a character measured for one device is
// never taken for the same character measured for another: the memo of
// measurements, the material of a paragraph and a block's layout are all
// keyed by the key.
//
// THE ADVANCE of a codepoint on a device is the AdvanceX of its glyph in the
// atlas entry the draw loop draws that face from at that size on that device
// (`face_bake`, src/draw.h: one function for both), rounded to a whole
// framebuffer pixel, half up. Under FreeType with hinting, which is ImGui's
// default when it is built with FreeType, the entry's advance is a whole
// pixel already and the rounding changes nothing. Under stb_truetype, and
// under FreeType told not to hint, the entry's advance is the font's own,
// scaled, and the rounding is what ImFontConfig::PixelSnapH does to it. There
// are no pair kerns: ImGui's text has none.
//
// What that buys, by construction: the gap between two glyphs of a rigid span
// is a whole number of framebuffer pixels in the layout, the draw loop's
// rounding of it is the identity, and nothing gathers along a word. A glyph
// is drawn where it was laid out against the first glyph of its span, and
// that glyph is on the pixel nearest its own place.
//
// A LAYOUT MADE THIS WAY IS FOR ONE DEVICE. At another density, or another
// number of units to the inch, the same text has other advances and breaks
// its lines elsewhere: a style resolved for a device is not the same style as
// one resolved for another (ResolvedStyle::device), and whatever was laid out
// under the first is laid out again.
//
// Math is not measured this way: a formula's glyphs are placed by the font's
// own advances and its MATH table, and are drawn from the edge of the
// formula's box (DrawRules::formula_placed_whole).

#pragma once

#include "fontmetrics.h"
#include "nodes.h"
#include "style.h"
#include "units.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

struct ImTypesetContext;

namespace ImTypeset::detail
{

struct Device {
    // Units of ImGui's coordinates to the inch: what the layout is done at.
    double px_per_inch = DEFAULT_PX_PER_INCH;
    // Framebuffer pixels to the unit, as the draw loop means it
    // (Drawer::density_of): 1 unless the context draws on the device's pixels.
    double density = 1.0;
};

// The devices a context has met and the metrics made for them.
struct DeviceState {
    // Device `n` is table[n - 1].
    std::vector<Device> table;
    // By font key.
    std::unordered_map<uint64_t, std::unique_ptr<FontMetrics>> metrics;
    // Changes when the table is emptied to start again, which it is when it
    // would pass MAX_DEVICES: a number then means another device, and what
    // was resolved before is stale.
    uint32_t epoch = 0;
};

// The number of the device the context would draw on now at `px_per_inch`:
// the display's density as ImGui has it at this moment. Zero if there is none
// to ask: no ImGui context, another one than the context was made in, or a
// backend that cannot bake a glyph on demand.
uint16_t device_number(ImTypesetContext& ctx, double px_per_inch);

// Which faces a style measures on its device, a bit a Face: those the style's
// ImTypesetAdvances_ names that have a font in the atlas now. `advances` is
// validated; `device` zero answers zero.
uint8_t device_faces(ImTypesetContext& ctx, int advances, uint16_t device);

// A context's resolver: a key without a device from the font library, as
// `fontlib_metrics` answers it, and a key with one from the atlas. A key
// whose device the context does not know is answered from the font file.
MetricsResolver context_metrics(ImTypesetContext& ctx);

// Drops every memoised measurement and every device's metrics: before the
// font library frees metrics they point at, and when the fonts change.
void forget_measurements(ImTypesetContext& ctx);

// How far the pen moves after `codepoint` in the atlas entry `face` is drawn
// from at an em of `size` scaled points on `device`, in framebuffer pixels,
// rounded to a whole one. False if the atlas cannot be asked: the context is
// not bound, the face has no font in the atlas, or the atlas has no glyph for
// the codepoint.
bool device_advance_px(ImTypesetContext& ctx, const FontMetrics& font, Scaled size,
                       const Device& device, uint32_t codepoint, double* out);

} // namespace ImTypeset::detail
