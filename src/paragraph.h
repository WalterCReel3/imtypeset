// Paragraph assembly: one block of styled text, a column width and an alignment
// in; the block's lines, set on its strut and stacked, out. This is the layer
// between the text composition of src/style and the line breaker, the packer
// and ship-out. Mirrors the part of advtext/ui/__init__.py that is not a view
// (`RAGGED_RIGHT`, `PROSE_BREAKING`, `_skips`, `_shape`, `_text_boxes`,
// `_unshrinkable`, `_hang_marker`, `_block_boxes`, and `_stacked` and
// `compose_blocks` for one block) and `_unskip` of advtext/markdown.py.
//
// The steps, each of which a caller can also take alone:
//
//   material   src/style's append_text into TextBlock::run, `hard_break`
//              between runs, `set_marker` for a list marker
//   skips      the alignment as a left skip, a right skip and a par-fill skip
//   shape      the column, the block's indent and its first-line indent as
//              line widths
//   break      total fit for justified text; first fit on material with its
//              shrink removed for every other alignment
//   marker     hung in the margin of the first line
//   strut      every line floored at the block's strut
//   stack      the lines as a vertical list with interline glue, and its vbox
//   ship       src/shipout, on that vbox
//
// ALIGNMENT IS GLUE. There is no alignment branch below `skips`: ragged right
// is a right skip of one fil, ragged left the same on the left, centred both.
// Justified is the absence of either.
//
// NOT JUSTIFIED MEANS FIRST FIT. tex.web has no first-fit breaker: ragged
// text there is the optimiser of sections 813 to 890 run under a fil right
// skip, and this is the reference's departure from it (docs/divergences.md).
// Under a fil right skip every line that fits costs the optimiser the same,
// its active list grows with the paragraph, and the reference measured
// seconds where first fit takes milliseconds. The
// material's shrink is removed first, so that "fits" means "is no wider than
// the measure at its natural spacing" and no ragged line is set tighter than
// its neighbours. `ragged_total_fit` asks for the optimiser anyway.
//
// THE BREAKING PARAMETERS ARE `prose_breaking()`, not LineBreakParams(): a
// tolerance of 1000 and 20 pt of emergency stretch, which is what unhyphenated
// prose needs to avoid overfull lines. A caller that wants plain TeX's passes
// LineBreakParams() and should expect them.
//
// THE FIRST-LINE INDENT. tex.web section 1091 starts a paragraph with one empty
// box as wide as the indent, and that is all. The reference puts that box in
// the material and then also narrows and shifts the first line by the same
// amount in the shape, so the indent is applied twice. FirstLineIndent chooses;
// the default is the reference's, which the goldens hold, and BOX_ONLY is
// tex.web's.
//
// WHAT COPY NEEDS. Ship-out numbers what it emits in reading order, and those
// indices are not positions in the material: each line adds a right skip, some
// add a left skip, the marker comes first, and what a break discards is not
// shipped. `ParagraphLayout::shipped` is the map from a node of the material
// to its index, and from it `originals` (what a character set as a stand-in
// was written as) and `hard_breaks` (where a line begins that the source
// began) are derived: the data src/selection's `text` takes. With
// Run::source_offsets beside it, the same map takes an index back to a byte
// of the source text.
//
// ALLOCATION. A TextBlock, a ParagraphComposer and a ParagraphLayout keep
// their capacity, so composing block after block with the same three settles
// to no allocation apart from what is added to the arena, and none there if
// the caller rewinds it to a mark.
//
// Not here: more than one block (the space above and below a block, and
// prev_depth being reset between them, are the caller's: stack `lines`
// yourself), quote bars, code blocks, rules, tables, formulas (an inline
// formula is a box the caller appends to the material; see `shipped`), and
// markdown.

#pragma once

#include "linebreak.h"
#include "nodes.h"
#include "pack.h"
#include "shipout.h"
#include "style.h"
#include "units.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ImTypeset::detail
{

// The \rightskip that turns justified setting into ragged-right setting: one
// fil of stretch at the end of every line, so that the breaker stops charging
// badness for a short one.
constexpr GlueSpec RAGGED_RIGHT = {0, UNITY, GlueOrder::FIL, 0, GlueOrder::NORMAL};

// Breaking parameters for prose with no hyphenator: the reference's
// PROSE_BREAKING. Everything but the tolerance (1000) and the emergency
// stretch (20 pt) is LineBreakParams's default.
LineBreakParams prose_breaking();

// `params` with the \leftskip, \rightskip pair that produces one alignment
// (tex.web sections 886 and 887 are where a line receives them). Ragged left
// and centred also take the \parfillskip away: it is fil glue at the right-hand
// end of the last line, and it would share that line's slack with the left
// skip.
LineBreakParams skips(Align align, const LineBreakParams& params);

// Where a block's first-line indent is applied.
enum class FirstLineIndent : uint8_t {
    // As the reference does: an empty box at the head of the material and a
    // first line that is narrower by the indent and shifted right by it. The
    // first line's text starts left_indent + 2 * first_line_indent from the
    // column's edge.
    BOX_AND_SHAPE = 0,
    // As tex.web section 1091 does: the box, and a first line as wide as the
    // others.
    BOX_ONLY,
};

// Per-line widths for one block (tex.web section 847): one entry, or two when
// the first line differs. Owns the specs a ParShape only views.
struct BlockShape {
    LineSpec lines[2];
    size_t count = 1;

    // Valid while this object is alive and not moved.
    ParShape par_shape() const { return ParShape::of(lines, count); }
};

// The shape of a block in a column `width` wide: every line is the column less
// `left_indent` and is shifted right by it, and is never narrower than one
// point. A non-zero `first_line_indent` makes the first line narrower by it and
// shifts it further (the reference's `_shape`); pass 0 for BOX_ONLY.
BlockShape shape(Scaled width, Scaled left_indent, Scaled first_line_indent);

// Appends `material` to `out` with every top-level glue's shrink removed. Glue
// inside a box is not reached.
void unshrinkable(NodeView material, std::vector<Node>& out);

// TeX's \unskip, which is tex.web's delete_last asked for a glue node
// (section 1105), repeated: drops the glue `run` ends with, and with each glue
// an INF_PENALTY immediately before it (the guard of a space inside an
// unbreakable run). delete_last removes one node and no penalty.
// Run::source_offsets shrinks with it.
void unskip(Run& run);

// A forced line end inside a paragraph, as the reference's inline walk makes
// one: `unskip`, then src/style's `fill_and_break`. Without the unskip a space
// typed before the break is a legal breakpoint, and a line that fits without
// it and not with it is followed by an empty one. `source_offset` is what
// Run::source_offsets records for the nodes added.
void hard_break(Run& run, Align align, uint32_t source_offset = 0);

// One block of text before it meets a width: the reference's TextBlock, as far
// as composition reads it.
struct TextBlock {
    // The material: what src/style's append_text and `hard_break` built. A
    // caller may append nodes of its own (an inline formula's box, which must
    // live in the arena the block is composed in).
    Run run;
    // A list marker's nodes, composed apart from the text with a space factor
    // of their own. Empty for a block with no marker.
    Run marker;

    // Empties both, keeping their capacity. Run::source_offsets is left as set.
    void clear();
};

// Sets `block.marker` to `text`: one unbreakable run in `font`. False, with a
// reason and an empty marker, if the resolver does not know the font's face.
bool set_marker(TextBlock& block, std::string_view text, FontSpec font,
                const Composition& composition, MetricsResolver resolve,
                std::string* why = nullptr);

// What composition reads of the reference's BlockStyle, TextBlock and Theme.
// Every length is in scaled points and is expected within MAX_DIMEN.
struct BlockStyle {
    Align align = Align::JUSTIFY;
    // What encloses the block plus the style's own indent, already added.
    Scaled left_indent = 0;
    Scaled first_line_indent = 0;
    FirstLineIndent first_line = FirstLineIndent::BOX_AND_SHAPE;
    // The space between a hung marker and the text. Read only with a marker.
    Scaled marker_gap = 0;
    // The glue between the block's lines (tex.web section 679).
    InterlineSkip interline;
    // What no line may fall below. Strut::filling(interline.baselineskip.width)
    // makes the interline glue between two ordinary lines exactly zero.
    Strut strut;
    // Break ragged text with the optimiser and its shrink left in.
    bool ragged_total_fit = false;
};

// A character of the material that was set as something else: `index` is the
// shipped GLYPH's, `codepoint` what was written.
struct Original {
    uint32_t index = 0;
    uint32_t codepoint = 0;
};

// `ParagraphLayout::shipped` for a node that ships as no item of its own.
constexpr uint32_t NOT_SHIPPED = 0xFFFFFFFFu;

// A composed block. Nodes are the arena's it was composed in.
struct ParagraphLayout {
    // The breaker's answer: the lines as it packed them, before the marker and
    // the strut. Break positions index the list that was broken, which is the
    // material after the indent box when there is one.
    Paragraph broken;
    // The first line with the marker hung on it, before the strut. A PENALTY
    // node (the default Node) when the block has no marker.
    Node hung;
    // The line boxes, top to bottom, each set on the strut.
    std::vector<Node> lines;
    // `lines` with interline glue between them, and that list packed at its
    // natural size. `box` is the vbox as a node; its reference point is on the
    // first line's baseline.
    std::vector<Node> vlist;
    PackResult packed;
    Node box;

    // For each node of TextBlock::run, the index ship-out gives it, or
    // NOT_SHIPPED: a character's GLYPH, a glue's SET_GLUE, an atom box's ATOM,
    // and for a box that is not an atom the first index inside it. A node the
    // breaker discarded, and every kern and penalty, is NOT_SHIPPED. Indices
    // are those of shipping `box` (or the lines in order) on its own; a caller
    // that ships several blocks into one sequence adds its base.
    //
    // All NOT_SHIPPED, with `mapped` false, when a break was taken at a
    // discretionary: the line then holds nodes that are not the material's.
    std::vector<uint32_t> shipped;
    bool mapped = false;
    // How many indices the block ships: one past its last.
    uint32_t item_count = 0;
    // How many of them are the marker's, at the start: its fil glue and its
    // characters. Zero with no marker. With one, this is the index at which
    // the reference tells `text` to put a space, between the marker and the
    // line it hangs on.
    uint32_t marker_items = 0;
    // What replaced characters were written as, ascending by index. A marker's
    // are not included.
    std::vector<Original> originals;
    // The index of the first item of the material shipped after each forced
    // break (a penalty of EJECT_PENALTY or less that is not the material's
    // last node), ascending, none twice.
    //
    // For src/selection's `text` these are separators of "\n", which is how
    // the reference passes them, and not TextData::hard_breaks: that array is
    // for boundaries at which a line begins, and in a block set ragged left or
    // centred the line begins one index earlier, at its left skip.
    std::vector<uint32_t> hard_breaks;

    // NONE unless the shape was empty, which `shape` never makes.
    LineBreakError break_error = LineBreakError::NONE;
    // The first thing a packer found wrong with a list: a character with no
    // measure, in practice. Lines are produced all the same.
    PackError pack_error = PackError::NONE;

    bool ok() const { return break_error == LineBreakError::NONE && pack_error == PackError::NONE; }
};

// Composes blocks, and owns the buffers that takes. Reuse one across blocks.
// Not thread-safe; one composer belongs to one thread at a time.
class ParagraphComposer
{
public:
    // Breaks `block` into lines for a column `width` wide, hangs its marker,
    // sets each line on the strut, stacks them and packs the stack. `out` is
    // overwritten; the boxes are added to `arena`, which is also where any BOX
    // or DISC node of the material lives. `measure` must be the one the
    // material's kerns were computed against.
    //
    // A block with no material is one line: the breaker's terminator, on the
    // strut.
    //
    // Returns out.ok().
    bool compose(NodeArena& arena, const TextBlock& block, const BlockStyle& style, Scaled width,
                 const LineBreakParams& params, CharMeasure measure, ParagraphLayout& out);

    // The first half of `compose`, for a caller that stacks several blocks
    // itself: everything but `vlist`, `packed` and `box`, which are left empty.
    bool set_lines(NodeArena& arena, const TextBlock& block, const BlockStyle& style, Scaled width,
                   const LineBreakParams& params, CharMeasure measure, ParagraphLayout& out);

    // The list the last call handed to the breaker: the material, after the
    // indent box if there is one, with its shrink removed if it was broken
    // first fit. Valid until the next call, and only while the block's material
    // is unchanged.
    NodeView broken_material() const { return _broken; }

    // The parameters the last call handed to the breaker.
    const LineBreakParams& line_params() const { return _line_params; }

    // The breaker every call uses: its work budget, and what the last
    // paragraph cost.
    LineBreaker& breaker() { return _breaker; }

private:
    void map_shipped(const NodeArena& arena, const TextBlock& block, size_t offset,
                     ParagraphLayout& out);

    LineBreaker _breaker;
    LineBreakParams _line_params;
    NodeView _broken;
    std::vector<Node> _material;
    std::vector<Node> _scratch;
};

// The first line with a list marker put in the margin it gave up (the
// reference's `_hang_marker`): the marker in a box exactly `indent` wide,
// pushed right by one fil of stretch and held off the text by `marker_gap`,
// then that box and the line, its shift_amount dropped, side by side. The glue
// has stretch and no shrink, so a marker wider than the indent overfulls its
// box and eats into the gap, where shrink would hang it off the left edge.
// `scratch` is a buffer to build in. `error` receives the first pack error, if
// it is not null and holds NONE.
Node hang_marker(NodeArena& arena, const Node& line, NodeView marker, Scaled indent,
                 Scaled marker_gap, CharMeasure measure, std::vector<Node>& scratch,
                 PackError* error = nullptr);

// Ships a composed block with its top-left corner at `top_left`: `ship` of
// `layout.box` from an origin on its first baseline. `layout` must have come
// from `compose`.
ShipError ship(const NodeArena& arena, const ParagraphLayout& layout, Point top_left,
               CharMeasure measure, std::vector<DrawCmd>& out, uint32_t max_items = MAX_SHIP_ITEMS);

} // namespace ImTypeset::detail
