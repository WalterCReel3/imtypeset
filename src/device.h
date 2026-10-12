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
//
// EVERY ADVANCE ON A DEVICE IS A WHOLE NUMBER OF FRAMEBUFFER PIXELS, whatever
// the character: one the atlas cannot be asked about (its face has no font
// in the atlas, the codepoint does not fit this build's ImWchar) is measured
// from the font file and rounded the same way, and one the face has no glyph
// for and nothing stands in for is given no advance at all if it is
// `unspaced` (src/style.h) and the rounded advance of the face's
// missing-glyph glyph otherwise.
//
// THE GRID. A face is FIXED-PITCH if every character from U+0020 to U+007E
// that its font maps has one advance in the font file (its hmtx table; the
// post table's isFixedPitch is not read: it is a declaration, and fonts in
// the wild declare it wrongly both ways). Its CELL on a device is the largest
// device advance among those characters, which under every loader measured
// is the advance of each of them. Text in such a face on a device is on a
// grid, and the promise is unconditional: every character takes a whole
// number of cells.
//   - A character of the face takes the cells its own advance needs: one for
//     nearly all of them, none for a mark the font gives no advance.
//   - An `unspaced` character the face has no ink for takes none, whether the
//     font lacks it or maps it to an empty glyph with an advance (U+200B in
//     many monospace fonts).
//   - A character borrowed from another face (`stand_in`) takes as many cells
//     as hold its device advance in that face, at least one: an ideograph
//     takes two, as in a terminal. Its glyph is at the left of its cells.
//   - U+FFFD and `?` standing in for a character no face has take one cell.
//   - A character that draws nothing and is not `unspaced` (a space the face
//     lacks) takes one cell.
// A guest's cells are the host's, not its own face's, so its key cannot be
// its face on the host's device: it is its face on a GRID DEVICE, the host's
// device with the host's cell (Device::cell), which the context numbers like
// any other.
//
// WHAT IS REMEMBERED IS CHECKED. An advance read from an atlas entry is true
// while the entry is rasterised as it was. A host can change that under the
// same ImFont: loader flags edited in place and the output rebuilt (Dear
// ImGui's own Fonts debug window does it), another loader, a source's extra
// advance. So each face on a device keeps what it read, and `device_number`,
// which every layout and every question to a document passes, has each of
// them that was used in this frame or the one before ask again once a frame:
// one character's advance, and all of them when that character's glyph has
// left its entry, which is what a rebuild does.
// A face that could not ask (the atlas would not make an entry of its size)
// is checked for whether it now can. On a difference everything measured is
// forgotten and the devices' epoch moves, which makes every layout that was
// resolved before stale exactly as a change of display does.

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
    // Zero, or for a grid device the width of a cell in framebuffer pixels:
    // every advance on it is a whole number of these.
    double cell = 0.0;
};

// A face at a size on a device: its metrics, and whether what it remembers
// of the atlas is still what the atlas answers.
class DeviceFace : public FontMetrics
{
public:
    // False if an advance this read is no longer the entry's, or if it could
    // not ask the atlas and now can. `thorough` asks again for everything it
    // read and not only for its witness.
    virtual bool current(bool thorough) = 0;
    // ImGui's frame at which the context's resolver last handed this out: a
    // layout measuring with it, or a draw finding its entry. A face nothing
    // has used since the frame before is not asked, so that sizes a host has
    // left behind cost nothing and their atlas entries are not kept in use;
    // it is asked at the first check after it is used again.
    int touched = -1;
};

// The devices a context has met and the metrics made for them.
struct DeviceState {
    // Device `n` is table[n - 1].
    std::vector<Device> table;
    // By font key.
    std::unordered_map<uint64_t, std::unique_ptr<DeviceFace>> metrics;
    // Changes when what was resolved before is stale: the table was emptied
    // to start again, which it is when it would pass MAX_DEVICES and a number
    // then means another device; or the atlas no longer answers the advances
    // that were read from it.
    uint32_t epoch = 0;
    // ImGui's frame at which the faces were last asked whether they are
    // current, and whether something since has given reason to ask before
    // the next: a face found its witness gone from its entry, or could not
    // ask the atlas, or the table had no number left for a grid device.
    int checked_frame = -1;
    bool suspect = false;
    bool full = false;
};

// The number of the device the context would draw on now at `px_per_inch`:
// the display's density as ImGui has it at this moment. Zero if there is none
// to ask: no ImGui context, another one than the context was made in, or a
// backend that cannot bake a glyph on demand.
//
// This is where what is remembered is checked (see the top): it can forget
// every measurement and move the epoch, so it is called between layouts and
// never within one.
uint16_t device_number(ImTypesetContext& ctx, double px_per_inch);

// The number of `base` with a grid of `cell` framebuffer pixels: found, or
// added. With no number left it answers zero and the table starts again at
// the next `device_number`; the caller sets its guest without a grid for the
// one layout that is then already stale.
uint16_t grid_device(ImTypesetContext& ctx, const Device& base, double cell);

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
// rounded to a whole one: the entry's own advance, before any grid. False if
// the atlas cannot be asked: the context is not bound, the face has no font
// in the atlas, the atlas has no glyph for the codepoint, or ImGui has no
// entry of that size to give.
bool device_advance_px(ImTypesetContext& ctx, const FontMetrics& font, Scaled size,
                       const Device& device, uint32_t codepoint, double* out);

} // namespace ImTypeset::detail
