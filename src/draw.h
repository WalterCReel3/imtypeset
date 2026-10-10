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
//     rigid span, a word is held to its laid-out width, and a word after a
//     space is placed from the word before it: see DrawRules.
//   - Past 64k vertices with 16-bit indices, a draw command of the list ends
//     up to 1,024 quads earlier than the reference's does: space is reserved
//     for a run of quads at a time. The vertices and triangles are the same.
//   - Optional culling by line (`LineIndex`), which the reference leaves to its
//     views.
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

    void clear()
    {
        spans.clear();
        sorted = true;
    }
};

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
    // box is put on the nearest pixel, and every glyph and both ends of every
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
    // from its place.
    //
    // The reference rounds every gap to the nearest pixel and holds nothing.
    // The roundings of a word add up, the next word is anchored where layout
    // put it, and the whole sum comes out of the space between them: measured
    // with the bundled faces at 13 to 20 px, words up to 5.6 px wider than
    // laid out, one justified space in seventy drawn at under 60 % of its
    // width, and some at no width at all (tests/test_word_gaps.cpp).
    bool word_width_held = false;

    // A word that follows a space is placed from the word before it: by the
    // distance from that word's last glyph, rounded once, as a glyph inside a
    // word is placed from the glyph before. It is held to the two pixels
    // either side of where layout put it, so nothing accumulates along a line
    // and the right-hand margin is kept; a word that starts a line, or
    // follows a formula, a rule or anything else that is not a word and
    // spaces, is put on the pixel nearest its own place. So a space is the
    // width layout gave it to half a pixel wherever that hold does not come
    // into it, and with word_width_held it is less than a pixel and a half
    // from it always: a word's first glyph is less than a pixel from its
    // place and its last less than a pixel and a half. What it costs: where
    // a word after a space stands depends, by a pixel, on the words before it
    // on the line, so two lines whose words start at one x in the layout can
    // start them a pixel apart.
    //
    // The reference puts every span's first glyph on the pixel nearest its
    // own place, so a space carries the rounding of both words' anchors on
    // top of what the word before it gathered.
    bool word_spaces_led = false;

    static constexpr DrawRules reference() { return DrawRules(); }
    static constexpr DrawRules product()
    {
        DrawRules rules;
        rules.formula_placed_whole = true;
        rules.word_width_held = true;
        rules.word_spaces_led = true;
        return rules;
    }
};

// The most a word's drawn width differs from its laid-out width under
// DrawRules::word_width_held.
constexpr double WORD_WIDTH_BOUND_PX = 0.5;

struct DrawArgs {
    // Where the commands' (0, 0) goes, in screen pixels.
    ImVec2 origin = ImVec2(0.0f, 0.0f);
    double px_per_inch = DEFAULT_PX_PER_INCH;
    DrawRules rules;
    Palette palette;
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
    // as the rasteriser baked them. Asked when the key first has a glyph to
    // draw. False if the key cannot be drawn at all, which makes its glyphs
    // missing.
    virtual bool bake(uint32_t key_slot, FontKey key, float* size, float* ascent) = 0;
};

// One call the reference would have made on the draw list.
enum class OpType : uint8_t {
    // A glyph: pen position (x0, y0), the glyph slot, the colour. Under
    // DrawRules::word_width_held, x1 is how far the gap from the glyph before
    // it is rounded up, in pixels (down, if negative); zero otherwise.
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
    // args.cull_lines tests against; null disables culling.
    DrawStats place(const DrawRun* runs, size_t run_count, const DrawArgs& args, FontBinder& binder,
                    const ImVec4* cull_clip = nullptr);

    struct Key {
        FontKey key;
        FontBinder::Binding binding = FontBinder::Binding::UNKNOWN;
        // Whether bake() has been asked, and whether it answered.
        bool asked = false;
        bool drawable = false;
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

private:
    class ImGuiBinder;
    struct Span;

    uint32_t key_slot(FontKey key, FontBinder& binder);
    uint32_t glyph_slot(uint32_t key, uint32_t codepoint, FontBinder& binder);
    void place_commands(const DrawCmd* cmds, size_t count, ImU32 ink, const DrawArgs& args,
                        FontBinder& binder, DrawStats& stats);
    void push_area(OpType type, Scaled x, Scaled y, Scaled width, Scaled height, ImU32 colour,
                   const DrawArgs& args);
    double settle_word(size_t word_op, double drift);

    template<class Sink>
    void emit(Sink& sink, const ImVec4& clip);

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
    // settle_word's: the ops of the gaps of one word that can give.
    std::vector<size_t> _gaps;
    ImGuiBinder* _binder;
};

} // namespace ImTypeset::detail
