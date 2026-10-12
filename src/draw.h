// Shipped commands to vertices: the per-frame loop. The port of the reference's
// advtext/backends/imgui_draw.py (`draw_runs`), with its device-space rules
// carried across as they stand. ../imgui-adv-text/docs/subpixel.md and
// docs/advance-rule.md are the write-ups; what this file needs of them:
//
//   - LAYOUT SPACE IS NOT TOUCHED. Commands arrive in scaled points and every
//     pixel decision is taken here, on device coordinates.
//   - THE SIZE ASKED OF IMGUI IS THE SIZE IT BAKES. A key's em is corrected to
//     ImGui's pixel height (`baked_size_px`), ImGui rounds that to a whole
//     pixel, and glyphs are drawn at the rounded size, so a quad is as many
//     pixels across as its bitmap is texels across. Nothing is resampled.
//   - THE BASELINE IS IMGUI'S. A glyph command's y is on the baseline; a quad
//     hangs from the top of ImGui's cell, so the baked ascent is subtracted,
//     and it is ImGui's baked ascent and not the font's own.
//   - A RIGID SPAN IS THE UNIT OF SNAPPING. A span opens at a glyph that
//     follows anything but a glyph and ends at the next set glue, atom, line
//     or rule. Its first glyph is put on the nearest pixel (`snap_px`,
//     floor(v + 0.5) — not ImGui's IM_ROUND, which truncates and is wrong
//     below zero). Every later glyph is placed by adding the gap from the
//     glyph before it, rounded to a whole pixel, to where that glyph went:
//     the gaps are honest to half a pixel, every glyph is on the grid, and
//     the drift is confined to the span. The vertical offset from the anchor
//     is snapped absolutely and does not accumulate.
//   - A GAP IS TAKEN IN SCALED POINTS AND CONVERTED ONCE, so the same span is
//     set with the same pixel offsets wherever the line was broken and
//     however far it was scrolled.
//   - AN AREA KEEPS ITS INK. Both edges of a rule, fill or highlight are
//     snapped (`snap_span`), and one with real extent that would close up is
//     widened to a pixel.
//   - A COORDINATE PASSES THROUGH FLOAT32 WHERE THE REFERENCE BUILDS AN ImVec2
//     and nowhere else: `device_coordinate`. Everything around it is double.
//
// WHAT IS DIFFERENT FROM THE REFERENCE, on purpose:
//   - It makes one ImDrawList::AddText call per glyph. This writes the quads
//     itself: a call is placed first, into a list of `DrawOp`s (the calls the
//     reference would have made, with their arguments), every glyph it needs
//     is then loaded into the atlas, and the quads are written into space
//     reserved in runs. The vertices are the ones those AddText calls produce,
//     including which quads ImGui drops (see `Drawer::draw_runs`).
//   - The draw list's flags are not touched. The reference sets
//     ImDrawListFlags_TextNoPixelSnap so that ImGui does not re-round a pen;
//     nothing here goes through the code that would.
//   - Block roles are not known here: a run comes with its ink (decision D19).
//   - A second output, vertices and indices in buffers the caller owns, for a
//     host that cannot reach an ImDrawList.
//   - The debug overlay's outlines are rectangles of its own and not ImGui's
//     AddRect and AddLine (see Drawer::draw_runs).
//   - Under DrawRules::product(), a formula is placed whole and not as a
//     rigid span, a word is held to its laid-out width and each of its
//     glyphs to its place in it, and words and formulas along a line are
//     each placed from the one before: see DrawRules, which states the
//     bounds those keep between them.
//   - Past 64k vertices with 16-bit indices, a draw command of the list ends
//     up to 1,023 quads earlier than the reference's does: space is reserved
//     for a run of up to 1,024 quads at a time, so ImGui starts a new command
//     at 65,536 - 4,096 vertices at the earliest, where a quad at a time goes
//     on to 65,532. The vertices and triangles are the same.
//   - Optional culling by line (`LineIndex`), which the reference leaves to its
//     views.
//   - Under DrawRules::product(), "pixel" in every rule above is a pixel of
//     the framebuffer and not a unit of ImGui's coordinates, which on a dense
//     display (macOS: two framebuffer pixels to the unit) are not the same
//     thing: see DrawRules::device_pixels. The reference knows one kind.
//   - The per-call memo is kept as reusable scratch in a `Drawer`. It is still
//     rebuilt on every call and nothing of ImGui's is held between calls: an
//     ImFontBaked is only good for a frame.

#pragma once

#include "fontlib.h"
#include "shipout.h"
#include "style.h"
#include "units.h"

#include "imgui.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ImTypeset::detail
{

// --- The pure helpers ---------------------------------------------------------------

// A device coordinate on the pixel grid, rounding half up: floor(value + 0.5),
// in double. Not ImGui's IM_ROUND, `(float)(int)(v + 0.5f)`, which truncates
// and so differs for every negative non-integer.
double snap_px(double value);

// One axis of an area, snapped to pixel edges without losing its ink: both
// edges by snap_px, and a span with real extent (`to > from`) whose edges
// land together is widened to one pixel. A span of no extent stays empty.
void snap_span(double from, double to, double* lo, double* hi);

// The size to ask ImGui for so its baked glyphs match the em layout used:
// size_px * (ascent + descent) / em. The three metrics may be in any one
// unit. `size_px` unchanged when em <= 0.
double baked_size_px(double size_px, double ascent, double descent, double em);

// The atlas entry a face's glyphs are drawn from: `font` at an em of `size`
// scaled points, where there are `px_per_inch` of ImGui's units to the inch
// and `density` pixels of the grid placement is done on to the unit. The em
// is turned into those pixels and corrected to the pixel height ImGui sizes a
// font by (`baked_size_px`, from `ours`, the face's own metrics; uncorrected
// if null), and ImGui rounds that to a whole size itself. The size is in the
// pixels of the grid already, so the entry is the one at ImGui's rasteriser
// density 1, and that is said outright at every density: a font's own
// (ImFont::CurrentRasterizerDensity) is whatever the display's was when a
// host last made it the current font, and an entry that followed it would be
// one entry when a line is measured and another when it is drawn. Null for a
// size below half a pixel or above Drawer::MAX_BAKED_SIZE_PX, and if ImGui
// has no entry to give.
//
// One function for the draw loop and for whoever measures a character as the
// atlas has it (src/device), so that the two cannot name different entries.
// Good for the frame it is asked in, as any ImFontBaked is. ImGui can answer
// an entry of another size (a font whose sizes are locked answers the
// nearest it has): `face_bake_size` is what to hold the entry's Size to.
ImFontBaked* face_bake(ImFont* font, const FontMetrics* ours, Scaled size, double px_per_inch,
                       double density);

// The size `face_bake` asks ImGui for, in pixels of the grid, before ImGui
// rounds it to a whole one. Zero where `face_bake` asks for nothing.
double face_bake_size(const FontMetrics* ours, Scaled size, double px_per_inch, double density);

// One axis of a document coordinate as the screen position an ImVec2 holds:
// origin + px_from_sp(length), summed in double and rounded to float32.
float device_coordinate(float origin, Scaled length, double px_per_inch);

// A colour packed as IM_COL32 packs it: alpha in the high byte, red in the low.
constexpr ImU32 rgba(unsigned red, unsigned green, unsigned blue, unsigned alpha = 255)
{
    return (static_cast<ImU32>(alpha) << 24) | (static_cast<ImU32>(blue) << 16) |
           (static_cast<ImU32>(green) << 8) | static_cast<ImU32>(red);
}

// Every colour the reference's backend names, less its per-block roles and its
// diff colours, which belong to layers that are not ported. `draw_runs` reads
// `highlight`, `rule` and the four debug colours; `page`, `ink` and `link` are
// here so a host states its theme in one place. `ink` is dark because `page`
// is light: the pair is the unit.
struct Palette {
    ImU32 page = rgba(0xFA, 0xFA, 0xF8);
    ImU32 ink = rgba(0x11, 0x11, 0x11);
    ImU32 rule = rgba(0x11, 0x11, 0x11);
    ImU32 highlight = rgba(0x3B, 0x82, 0xF6, 0x4D);
    ImU32 debug_box = rgba(0x22, 0xC5, 0x5E, 0x80);
    ImU32 debug_baseline = rgba(0xEF, 0x44, 0x44, 0x80);
    ImU32 debug_stretched = rgba(0x3B, 0x82, 0xF6, 0x66);
    ImU32 debug_shrunk = rgba(0xF9, 0x73, 0x16, 0x66);
    ImU32 link = rgba(0x1D, 0x4E, 0xD8);
};

// Dark ink on a light page: what a caller gets by naming none.
constexpr Palette default_palette()
{
    return Palette();
}

// Light ink for a dark background.
constexpr Palette dark_palette()
{
    Palette p;
    p.page = rgba(0x1E, 0x1E, 0x1E);
    p.ink = rgba(0xE6, 0xE6, 0xE6);
    p.rule = rgba(0x8A, 0x8A, 0x8A);
    p.highlight = rgba(0x3B, 0x82, 0xF6, 0x66);
    p.link = rgba(0x8A, 0xB4, 0xF8);
    return p;
}

// What one call managed. `glyphs` counts every glyph command that was placed,
// whether or not a quad came of it (a space is placed and draws nothing);
// `missing` counts glyph commands the face has no raster for.
struct DrawStats {
    int glyphs = 0;
    int missing = 0;
    int rules = 0;
};

// --- Inputs ---------------------------------------------------------------------------

// How a typographic role becomes a loaded ImGui face. `font` null is a usable
// answer: the role's glyphs are drawn in ImGui's current font at its current
// size, with no check that the font has them. `data` is the FaceData the font
// was added from (src/font_loader), which is what maps a layout codepoint to
// the one the atlas knows it by; without it the codepoint is asked for as it
// is, and one above U+FFFF is missing in a build with a 16-bit ImWchar.
struct FaceFont {
    ImFont* font = nullptr;
    const FaceData* data = nullptr;
};

struct FaceFonts {
    FaceFont faces[FACE_COUNT];
};

// A colour for the glyphs whose reading index is in [start, end), in place of
// their run's ink. Ranges are sorted by `start` and do not overlap.
struct InkRange {
    uint32_t start = 0;
    uint32_t end = 0;
    ImU32 colour = 0;
};

// A filled rectangle with a colour of its own: the mark behind a search hit,
// the underline of a link.
struct Fill {
    Rect rect;
    ImU32 colour = 0;
};

// A stretch of a command list that is drawn or skipped as one: a LINE command
// and everything up to the next LINE that is not inside it, or the commands
// before the first LINE. [top, bottom] is the vertical extent, in the
// commands' scaled points, of the lines and rules in it. `bounded` is false
// for a stretch holding a glyph, set glue or atom that no line covers, which
// is never skipped.
struct LineSpan {
    uint32_t first = 0; // index of its first command
    uint32_t end = 0;   // one past its last
    Scaled top = 0;
    Scaled bottom = 0;
    bool bounded = false;
};

// The lines of one command list, for skipping those the clip rectangle does
// not reach without visiting their commands. Built once per shipped list, not
// per frame.
struct LineIndex {
    std::vector<LineSpan> spans;
    // Whether tops and bottoms both never decrease from one span to the next,
    // as they do down a page. Only then can the visible spans be found by
    // bisection; otherwise every span is tested.
    bool sorted = true;
    // How far, in the commands' scaled points, ink may stand outside the box
    // of the line it is on, above or below: zero for a list whose lines hold
    // what is in them, which is every list but one whose lines were set to an
    // exact height (Strut::exact). A span is skipped only when its box grown
    // by this, and by CULL_MARGIN_PX, misses the clip rectangle. Not set by
    // index_lines: whoever knows the list was set that way sets it
    // (ink_overhang).
    Scaled overhang = 0;

    void clear()
    {
        spans.clear();
        sorted = true;
        overhang = 0;
    }
};

// A bound on how far the ink of `cmds` reaches from the baseline of the line
// it is on, for LineIndex::overhang: a glyph's ink is taken to lie within two
// ems of its own baseline (the tallest glyphs of a math face, at their size,
// reach a little over one), and a formula's glyphs and rules to lie within
// the formula's box, whose height and depth its ATOM command carries.
Scaled ink_overhang(const DrawCmd* cmds, size_t count);

// Builds the index of `cmds`, replacing what `out` held. A LINE command opens
// a new span unless it lies inside the one that is open (a line of a box
// nested in a line, whose indices fall in the outer line's range); a nested
// line's extent is added to the span it is in.
void index_lines(const DrawCmd* cmds, size_t count, LineIndex& out);

// How far, in pixels, a line's box is grown before it is tested against the
// clip rectangle. A glyph's bitmap can reach a pixel past the outline box its
// line was measured from, and a span's anchor moves by up to half a pixel.
constexpr double CULL_MARGIN_PX = 2.0;

// Commands with the ink their glyphs are drawn in. `lines`, if given, is the
// index of exactly these commands.
struct DrawRun {
    const DrawCmd* cmds = nullptr;
    size_t count = 0;
    ImU32 ink = 0;
    const LineIndex* lines = nullptr;
};

// Where this loop can be told to leave the reference's device-space rules.
// reference() is the prototype's, exactly, and is what every replay of its
// goldens runs under; product() is what the public API draws with.
struct DrawRules {
    // A formula is set as one picture on the pixel grid. The left edge of its
    // box is put on a pixel — the nearest, or under word_spaces_led one of
    // the two beside its place — and every glyph and both ends of every
    // rule in it stand at that pixel plus their own distance from the edge in
    // scaled points, converted and rounded once: nothing in a formula is
    // placed from the glyph before it. So each glyph and each end of a rule
    // is within half a pixel of where layout put it against the formula's
    // edge, and any two of them within a pixel of where layout put them
    // against each other, whatever the order they are shipped in and however
    // many there are: a bar against the parts it lies between, a radical's
    // bar against its sign and what is under it, the narrower part of a
    // fraction against the middle of its bar. A rule's top and bottom are
    // taken from the baseline its formula's first glyph was put on, and it
    // does not end the span. A formula is the same picture wherever the page
    // is put. What it costs is the evenness of a row of letters inside a
    // formula: a gap between two glyphs can be up to a pixel from the gap in
    // the layout, where a gap rounded on its own is at most half.
    //
    // Text set straight after a formula, with no space, is placed from the
    // formula's rightmost part: whichever glyph, by its origin, or rule, by
    // its right-hand end, stands furthest right in the layout, by the gap
    // from it rounded once. That is the bar for a bracket closing a fraction
    // and the last letter for one closing a row of letters; it need not be
    // the glyph the formula shipped last, which for a fraction is in the
    // middle of its denominator. A rule that lies wholly to one side of the
    // formula's box is not the formula's, and ends it.
    //
    // The reference places a formula's glyphs as it places a word's, each by
    // the rounded gap from the one shipped before it, snaps every rule alone
    // by its own corners, and lets it end the span. A formula's parts are
    // shipped row by row, so the rounding of a whole numerator stands between
    // its first glyph and the denominator's, and none of it reaches a bar:
    // measured with the bundled math face, a bar's end up to 10 px from the
    // glyph it should end over and a denominator up to 6 px off the middle of
    // its bar.
    bool formula_placed_whole = false;

    // A word is drawn as wide as it was laid out, to WORD_WIDTH_BOUND_PX. The
    // word is the glyphs of a rigid span that are not a formula's. They are
    // placed by the rounded gaps as ever; when the span ends, the distance
    // from the word's first glyph to its last is compared with the distance
    // layout gave them, and while it is more than the bound too long, the gap
    // that was rounded up by the most is rounded down instead (the earliest
    // of equals), and the other way about for a word too short. The gap that
    // was nearest to rounding the other way is the one that does, so it ends
    // between a half and a whole pixel from its laid-out size and usually
    // just past the half; every other gap is rounded as before. Which gaps
    // these are depends on the word and its size alone (the roundings are
    // compared in single precision, as DrawOp::x1 keeps them), so a word is
    // the same picture wherever it stands. Only the word's two ends are
    // held: a glyph in the middle of a long word can still stand further
    // from its place. Text set straight after a formula is a word from its
    // first glyph, and text straight before one is a word that ends there.
    //
    // The reference rounds every gap to the nearest pixel and holds nothing.
    // The roundings of a word add up, the next word is anchored where layout
    // put it, and the whole sum comes out of the space between them: measured
    // with the bundled faces at 13 to 20 px, words up to 5.6 px wider than
    // laid out, one justified space in seventy drawn at under 60 % of its
    // width, and some at no width at all (tests/test_word_gaps.cpp).
    bool word_width_held = false;

    // Along a line each thing is placed from the thing before it, as a glyph
    // inside a word is placed from the glyph before. The thing placed is the
    // first glyph of a word that follows a space or follows a formula with
    // no space, or the left edge of a formula's box. What it is placed from,
    // the leader, is the last glyph of the word before it or, after a
    // formula, the formula's rightmost part (see formula_placed_whole;
    // without that rule a formula's glyphs are glyphs of a word like any
    // others, and an atom ends the leader). It goes to the leader's pixel
    // plus the laid-out distance between the two, rounded once, and is then
    // held to the two pixels either side of its own place, so that nothing
    // accumulates along a line and the right-hand margin is kept. Set glue
    // and the start of a formula keep the leader. A line, a rule that is no
    // formula's and anything else end it, and what comes next is put on the
    // pixel nearest its own place.
    //
    // THE BOUND, which is what DrawRules::product() promises and what
    // tests/test_word_gaps.cpp asserts. Take a word at its glyph nearest the
    // neighbour and a formula at the left edge of its box. Then two
    // neighbours on a line are drawn LESS THAN A PIXEL AND A HALF from their
    // laid-out distance apart: two words across a space; text and a formula
    // after it, with a space between them or with none; a formula and the
    // text after it likewise; two formulas. Half a pixel wherever the hold
    // to its own place does not move the thing that was led, which is nearly
    // everywhere. It follows from three facts: whatever is led stands less
    // than a pixel from its own place; a word's last glyph stands less than
    // a pixel and a half from its place, being half a pixel from its
    // laid-out distance to the first (word_width_held); and the hold only
    // ever moves a thing towards its own place, so by less than its leader
    // stands from the leader's.
    //
    // What the bound does not say. Inside a formula every glyph and rule end
    // is up to half a pixel from its laid-out distance to the box's edge
    // (formula_placed_whole), so the text before a formula can be up to two
    // pixels from its laid-out distance to a particular glyph of the
    // formula; text after a formula is less than a pixel and a half from the
    // formula's rightmost part and at most a pixel from its box's edge.
    // And a glyph in the middle of a long word is not held by these three
    // rules: word_glyphs_held is what holds it.
    //
    // What it costs: where a word or a formula stands depends, by a pixel,
    // on what is before it on the line, so two lines whose words start at
    // one x in the layout can start them a pixel apart, and a formula is at
    // one of the two pixels beside its place and not always the nearer.
    // Text straight after a formula is, where the hold moves it, a pixel
    // nearer the formula or further from it than one rounded gap would put
    // it.
    //
    // The reference puts every span's first glyph on the pixel nearest its
    // own place, so a space carries the rounding of both words' anchors on
    // top of what the word before it gathered; and it places text after a
    // formula one rounded gap from the glyph the formula shipped last,
    // wherever that glyph went.
    bool word_spaces_led = false;

    // Every glyph of a word is drawn within WORD_GLYPH_BOUND_PX, a pixel, of
    // its laid-out distance from the word's first glyph, and the word's last
    // within WORD_WIDTH_BOUND_PX of it as under word_width_held. The gaps
    // are rounded as ever, and as each glyph is placed the distance it has
    // gathered is compared with the one layout gave it. When it is more than
    // the bound too far along, one gap before it is rounded down instead of
    // up: of the gaps into the glyphs, placed since a gap last gave, that
    // have stood at or right of their places without a break up to this one,
    // the one that was rounded up by the most, the earliest of equals. The
    // glyphs from that gap on move a pixel left, none of them more than a
    // pixel past its place, and this one is back within half a pixel. The
    // other way about for a glyph too far back. At the
    // word's end the same choice is made once more if the last glyph is
    // more than half a pixel out. A step between two glyphs is still its
    // laid-out size rounded down or up, and which gaps give depends on the
    // word and its size alone, so a word is the same picture wherever it
    // stands. It takes the place of word_width_held's choice of gaps, which
    // looks at the word's two ends alone.
    //
    // With a word's first glyph less than a pixel from its own place
    // (word_spaces_led), EVERY GLYPH OF TEXT IS DRAWN LESS THAN TWO PIXELS
    // FROM WHERE LAYOUT PUT IT, however long the word: the bound a caret, a
    // selection's edge and a hit test have against the glyph they are for,
    // since those answer from layout. tests/test_device_advances.cpp asserts
    // it for a number of forty digits and for a row of code of seventy-four
    // characters, which is one word.
    //
    // What it costs: in a word whose roundings gather to more than a pixel
    // part of the way along and come back by its end, a gap gives that
    // word_width_held alone would have left; and the gap that gives is the
    // best of a run and not of the whole word. Measured over the bundled
    // faces at 13 to 20 px (tests/test_word_gaps.cpp prints the tallies).
    //
    // The reference, and word_width_held alone, hold nothing in the middle
    // of a word. In text set in one advance (a monospace face; a row of
    // digits) every gap rounds the same way and the earliest give, so the
    // middle of a row is out by the row's length times what the rounding
    // loses: measured, 16.9 px in a row of 74 characters and 9.8 px in a
    // number of 40 digits.
    bool word_glyphs_held = false;

    // A glyph's quad is emitted exactly when part of it is inside the clip
    // rectangle vertically: its top above the rectangle's bottom edge and its
    // bottom below the top edge. A clip rectangle covers the pixels from its
    // top edge to, but not including, its bottom edge, so a quad wholly at or
    // below the bottom edge shows nothing; with the rule, a document drawn in
    // slices one under another (columns: each under a clip that ends where
    // the next begins) emits a glyph in every slice it shows in and in no
    // other. It is a rule about the glyphs placed: which lines are placed at
    // all is DrawArgs::cull_lines.
    //
    // The reference draws through ImFont::RenderText, which looks at the
    // font's cell and not at the glyph's bitmap: it skips a glyph only when
    // its pen (the top of the cell) is BELOW the bottom edge or the whole
    // cell above the top edge. A glyph whose pen is exactly on the bottom
    // edge is emitted, every vertex of it outside the rectangle; and a glyph
    // taller than the cell (a box-drawing bar, an integral sign) is dropped
    // with its cell while a row or two of its bitmap is inside.
    bool quads_off_clip_dropped = false;

    // The pixel grid is the framebuffer's. With DENSITY framebuffer pixels to
    // a unit of ImGui's coordinates (DrawArgs::density; ImGui's
    // io.DisplayFramebufferScale, 2 on a macOS Retina display and 1 on most
    // others), the whole of placement is done in framebuffer pixels: the
    // origin and every length are multiplied by the density, each face is
    // baked at its size in framebuffer pixels (at ImGui's rasteriser density
    // 1, so that the rasteriser is asked for the same bitmap whichever of
    // ImGui's loaders it is and the size is rounded to a framebuffer pixel
    // and not to a unit), and every rule of this file is applied to those
    // numbers as it stands. What is written to the draw list is the result
    // divided by the density. So a glyph's bitmap meets the framebuffer texel
    // for pixel, a gap is rounded to a framebuffer pixel, a hairline is one
    // framebuffer pixel thick, and every bound stated in pixels — half a
    // pixel for a word's width, a pixel and a half between neighbours, half a
    // pixel for a part of a formula — is that many FRAMEBUFFER pixels, which
    // at density 2 is half as far on the glass. At density 1 nothing is
    // multiplied by anything but one and the output is the same to the bit.
    //
    // Layout does not change: lines break where they broke, and sizes, hit
    // tests and selection rectangles are in ImGui's units as before. What is
    // drawn differs from layout by no more than the bounds above, in
    // framebuffer pixels.
    //
    // A density that is not above 1, or is above MAX_DENSITY, or is not a
    // number, is 1. At a density that is not a power of two (1.5, 3) a
    // position on the framebuffer's grid is not a float32 in ImGui's units: a
    // vertex is the nearest float32 to it, within a few millionths of a
    // pixel at any coordinate a display has.
    //
    // A face with no font bound is drawn in ImGui's current font as ImGui
    // has it baked, whatever density that is; only its pen is on the
    // framebuffer's grid.
    //
    // The reference has one kind of pixel. Without this rule the rules above
    // are applied in ImGui's units, and a font this library added is baked
    // at density 1 for a size in units, because ImGui raises a font's
    // density to the display's only while that font is the current one
    // (ImGui::PushFont), which this loop never makes it: measured at density
    // 2, half a texel to the framebuffer pixel, where ImGui's own text in
    // the same window has one (tests/test_hidpi.cpp).
    bool device_pixels = false;

    static constexpr DrawRules reference() { return DrawRules(); }
    static constexpr DrawRules product()
    {
        DrawRules rules;
        rules.formula_placed_whole = true;
        rules.word_width_held = true;
        rules.word_glyphs_held = true;
        rules.word_spaces_led = true;
        rules.quads_off_clip_dropped = true;
        rules.device_pixels = true;
        return rules;
    }
};

// The most a word's drawn width differs from its laid-out width under
// DrawRules::word_width_held.
constexpr double WORD_WIDTH_BOUND_PX = 0.5;

// The most a glyph's drawn distance from the first glyph of its word differs
// from the laid-out one under DrawRules::word_glyphs_held.
constexpr double WORD_GLYPH_BOUND_PX = 1.0;

// The most framebuffer pixels to a unit of ImGui's coordinates that
// DrawRules::device_pixels follows.
constexpr double MAX_DENSITY = 16.0;

struct DrawArgs {
    // Where the commands' (0, 0) goes, in ImGui's coordinates.
    ImVec2 origin = ImVec2(0.0f, 0.0f);
    // Units of ImGui's coordinates to the inch: what layout was done at.
    double px_per_inch = DEFAULT_PX_PER_INCH;
    // Framebuffer pixels to a unit of ImGui's coordinates, read under
    // DrawRules::device_pixels alone. Zero is the display's as ImGui has it,
    // io.DisplayFramebufferScale.x of the current context, and 1 with no
    // context.
    double density = 0.0;
    DrawRules rules;
    Palette palette;
    // If not zero, the colour of a rule that is no formula's (the rule under
    // a table's header), where `palette.rule` is then the colour of a
    // formula's alone. Under DrawRules::formula_placed_whole; without it no
    // rule is known to be a formula's and this colours every one.
    ImU32 rule_between = 0;
    // Drawn first, under everything, each in its own colour.
    const Fill* fills = nullptr;
    size_t fill_count = 0;
    // Drawn next, in `palette.highlight`.
    const Rect* highlights = nullptr;
    size_t highlight_count = 0;
    const InkRange* inks = nullptr;
    size_t ink_count = 0;
    // Line boxes, baselines, and glue coloured by whether it was stretched or
    // squeezed, over the ink.
    bool debug = false;
    // Skip the lines of a run that has a LineIndex when their boxes, grown by
    // CULL_MARGIN_PX, miss the clip rectangle. What is then drawn is what
    // would have been visible, and the work is proportional to it; the stats
    // count only what was visited, and quads ImGui would have emitted wholly
    // outside the clip rectangle are not emitted.
    bool cull_lines = false;
};

// --- Placement ------------------------------------------------------------------------

// What answers for a font key while a list is placed: ImGui in a frame, or a
// test's records. Keys are numbered from 0 in the order a call first meets
// them, and (key, codepoint) pairs likewise; each question is asked once per
// call for a number.
class FontBinder
{
public:
    enum class Binding : uint8_t {
        // The key's face is a loaded font.
        BOUND,
        // No font for the face: drawn in ImGui's current one, unasked.
        UNBOUND,
        // The key names no face. Its glyphs are missing.
        UNKNOWN,
    };

    virtual ~FontBinder() = default;

    // Forgets everything: a new call is starting.
    virtual void begin() = 0;
    virtual Binding bind(uint32_t key_slot, FontKey key) = 0;
    // Whether the key's glyph for the codepoint can be drawn: for a BOUND key,
    // whether its face has a raster for it; for an UNBOUND one, whether the
    // codepoint can be asked of the atlas at all. Not asked for UNKNOWN keys.
    virtual bool has_glyph(uint32_t glyph_slot, uint32_t key_slot, uint32_t codepoint) = 0;
    // The size the key's glyphs are drawn at and the ascent they hang from,
    // as the rasteriser baked them, in the pixels placement is done in:
    // framebuffer pixels (Drawer::density_of). Asked when the key first has a
    // glyph to draw. False if the key cannot be drawn at all, which makes its
    // glyphs missing.
    virtual bool bake(uint32_t key_slot, FontKey key, float* size, float* ascent) = 0;
    // Whether the key's text is set on a grid of cells (FontMetrics::cell).
    // A word of such text is never placed from the word before it
    // (DrawRules::word_spaces_led): it goes on the pixel nearest its own
    // place, so that what layout has at one column in two lines, or in two
    // rows of a table, is drawn at one x in both. A binder that knows no
    // metrics knows no grid.
    virtual bool on_grid(uint32_t key_slot, FontKey key)
    {
        (void)key_slot;
        (void)key;
        return false;
    }
};

// One call the reference would have made on the draw list.
enum class OpType : uint8_t {
    // A glyph: pen position (x0, y0), the glyph slot, the colour. Under
    // DrawRules::word_width_held or word_glyphs_held, x1 is how far the gap
    // from the glyph before it is rounded up, in pixels (down, if negative);
    // zero otherwise. Under word_glyphs_held, y1 is how far the glyph stands
    // right of its laid-out distance from the first glyph of its word, in
    // pixels; zero otherwise, and for a word's first glyph.
    TEXT,
    // AddRectFilled between (x0, y0) and (x1, y1).
    RECT_FILLED,
    // Where the reference calls AddRect between the same corners: the debug
    // overlay's line box. Written as the frame of the rectangle one pixel
    // thick, in up to four rectangles of this library's own.
    RECT,
    // Where it calls AddLine from (x0, y0) to (x1, y0), one pixel thick: the
    // overlay's baseline. Written as the row of pixels below y0.
    LINE_SEGMENT,
};

struct DrawOp {
    OpType type = OpType::TEXT;
    // TEXT: the number of its (key, codepoint) pair; see Drawer::glyphs().
    uint32_t glyph = 0;
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    ImU32 colour = 0;
    // TEXT: the glyph command's reading index.
    uint32_t index = 0;
};

// Vertices and indices in memory the caller owns: what a host hands its own
// renderer. Indices are 32 bits and count from the buffer's first vertex.
// Glyph quads are textured from the font atlas and rectangles from its white
// pixel, as in an ImDrawList, so the whole buffer draws with the atlas bound.
struct DrawBuffers {
    ImVector<ImDrawVert> vertices;
    ImVector<unsigned int> indices;

    void clear()
    {
        vertices.resize(0);
        indices.resize(0);
    }
};

// The draw loop and its scratch. One per thread of drawing; reuse it across
// frames so that a frame allocates nothing once its buffers have grown.
class Drawer
{
public:
    Drawer();
    ~Drawer();
    Drawer(const Drawer&) = delete;
    Drawer& operator=(const Drawer&) = delete;

    // Issues the runs to a draw list, anchored at args.origin: fills first,
    // then highlights, then each run's commands in order — rules in
    // `palette.rule`, glyphs in the run's ink or an InkRange's colour — with
    // the debug overlay over each line when asked for. The overlay's line
    // boxes and baselines are rectangles written here, one pixel thick and
    // not anti-aliased, where the reference draws them with ImGui's AddRect
    // and AddLine: they cover the same pixels' worth of outline and are the
    // same under every version and every flag of ImGui.
    //
    // `metrics` must resolve the measurements the commands were packed with;
    // the em it answers is what a key's baked size is derived from. A key it
    // does not know is drawn at its nominal size.
    //
    // The vertices are those of one ImDrawList::AddText per glyph and one
    // AddRectFilled per rectangle. So no quad is written for a glyph whose
    // colour has no alpha; whose codepoint is 0, U+000A or U+000D; whose pen
    // is below the draw list's clip rectangle or whose cell is wholly above
    // it; whose bitmap is empty; or whose quad lies wholly to one side of the
    // clip rectangle. A quad that straddles the clip rectangle is written
    // whole. The font atlas must be the texture bound on the draw list, as
    // AddText requires. Must be called inside a frame.
    //
    // A key ImGui would be asked to bake below half a pixel or above
    // MAX_BAKED_SIZE_PX is not drawn and its glyphs count as missing.
    DrawStats draw_runs(ImDrawList* draw_list, const DrawRun* runs, size_t run_count,
                        const DrawArgs& args, const FaceFonts& fonts, MetricsResolver metrics);

    // The same into buffers, appended. `clip` is the rectangle quads are
    // dropped against, as (x0, y0, x1, y1). Rectangles take the white texel of
    // the current context's atlas. An unbound face is drawn in
    // ImGui::GetFont().
    DrawStats draw_runs(DrawBuffers& out, const ImVec4& clip, const DrawRun* runs, size_t run_count,
                        const DrawArgs& args, const FaceFonts& fonts, MetricsResolver metrics);

    // --- The two halves, for tests and for a host with its own rasteriser ---

    // Places the runs: fills ops() with the calls, in order, and glyphs() and
    // keys() with what they refer to. `cull_clip` is the rectangle
    // args.cull_lines tests against; null disables culling. The ops are in
    // ImGui's coordinates whatever the density; a key's size and ascent are
    // as the binder answered them.
    DrawStats place(const DrawRun* runs, size_t run_count, const DrawArgs& args, FontBinder& binder,
                    const ImVec4* cull_clip = nullptr);

    struct Key {
        FontKey key;
        FontBinder::Binding binding = FontBinder::Binding::UNKNOWN;
        // Whether bake() has been asked, and whether it answered.
        bool asked = false;
        bool drawable = false;
        // FontBinder::on_grid.
        bool grid = false;
        float size = 0.0f;
        float ascent = 0.0f;
    };

    struct Glyph {
        uint32_t key = 0; // a key slot
        uint32_t codepoint = 0;
        bool present = false;
    };

    const std::vector<DrawOp>& ops() const { return _ops; }
    const std::vector<Key>& keys() const { return _keys; }
    const std::vector<Glyph>& glyphs() const { return _glyphs; }

    static constexpr float MAX_BAKED_SIZE_PX = 2048.0f;

    // The density a call with these arguments places at: 1 without
    // DrawRules::device_pixels, and for a density that rule does not follow.
    static double density_of(const DrawArgs& args);

private:
    class ImGuiBinder;
    struct Span;

    uint32_t key_slot(FontKey key, FontBinder& binder);
    uint32_t glyph_slot(uint32_t key, uint32_t codepoint, FontBinder& binder);
    void place_commands(const DrawCmd* cmds, size_t count, ImU32 ink, const DrawArgs& args,
                        FontBinder& binder, DrawStats& stats);
    void push_area(OpType type, Scaled x, Scaled y, Scaled width, Scaled height, ImU32 colour,
                   const DrawArgs& args);
    void ops_to_units();
    double settle_word(size_t word_op, double drift);
    double give_in_run(size_t word_op, size_t& floor_op, bool right);

    template<class Sink>
    void emit(Sink& sink, const ImVec4& clip, const DrawRules& rules);

    std::vector<DrawOp> _ops;
    std::vector<Key> _keys;
    std::vector<Glyph> _glyphs;
    // Per key, the glyph slot + 1 of each codepoint below 256; 0 is "not seen".
    std::vector<uint32_t> _latin;
    // The rest: open addressing on (key slot, codepoint), glyph slot + 1.
    struct Other {
        uint64_t id = 0;
        uint32_t slot = 0;
    };
    std::vector<Other> _others;
    size_t _other_count = 0;
    uint32_t _last_key = 0;
    // The density of the call being placed or emitted.
    double _density = 1.0;
    ImGuiBinder* _binder;
};

} // namespace ImTypeset::detail
