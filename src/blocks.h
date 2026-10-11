// Blocks: several paragraphs, headings, list items with their markers, code
// blocks, quotes with their bar and thematic rules, stacked into one document.
// This is the layer above src/paragraph, which sets one block. Mirrors the part
// of advtext/ui/__init__.py that is not a view (`_block_boxes`, `_rule_box`,
// `_runs`, `_quote_depth`, `_stacked`, `_barred`, `compose_blocks`), and
// `_code_lines` and `_newlines` of advtext/markdown.py.
//
// The steps, each of which a caller can also take alone:
//
//   describe   a Block: its type, the style values composition reads, its
//              material (src/style's append_text, `set_marker`, `set_code`),
//              and which quote and list it is in
//   boxes      BlockComposer::block_boxes: one block as the boxes it gives the
//              stack; lines for text and code, one box for a rule
//   stack      Stacker::stack: the vertical list, with the space between
//              blocks and a barred box round each quote
//   document   DocumentComposer::compose_blocks: all of it at once, as the
//              reference does it; or Document, which keeps each block's layout
//              and lays out only what is asked for
//
// WHAT OF tex.web THIS IS, AND WHAT IS NOT. Boxes enter a vertical list through
// append_to_vlist (section 679), a quote's bar is a rule beside a vbox in an
// hbox (hpack, section 649; vpack, section 668), and a block's own lines are
// spaced by its \baselineskip, \lineskip and \lineskiplimit as section 679 has
// them. THE SPACE BETWEEN BLOCKS IS NOT tex.web's. There a paragraph is
// preceded by \parskip glue (section 1091, par_skip_code of section 224) and
// its first line still receives interline glue against the box above, so the
// distance between two paragraphs is \parskip plus \baselineskip's remainder.
// Here, as in the reference, each block names a space above and a space below;
// the gap between two blocks is one KERN of the upper block's space below plus
// the lower block's space above (a kern as section 1061's append_kern makes,
// not glue: it neither stretches nor is a legal break), and prev_depth is put
// back to ignore_depth (section 212) after it, as section 1056 does after a
// rule, so that no interline glue is added on top. A block's strut makes that
// exact: every line is as tall as its leading, so a zero gap sets two blocks
// as close as two lines of one. There is no page builder (sections 980 to
// 1028): nothing is broken into pages, no \topskip, no \vsize, no insertions,
// and the document box is as tall as its contents.
//
// THE REFERENCE'S RULES AND THE PRODUCT'S. The reference is a prototype and its
// block stacking has shortcuts (docs/divergences.md, `## blocks`, lists
// sixteen). Each is a switch in DocumentRules. A default DocumentRules is the
// reference, which tests/golden/blocks holds stage by stage;
// DocumentRules::product() is what a document API should use.
//
// LAZY LAYOUT. `Document` keeps one layout per block -- the block shipped
// alone, with its top-left corner at the origin and its indices from zero --
// and where each sits. A block's layout is made when a window that shows it is
// asked for, and is stale when the width or the block changes. Blocks stack
// into units (a block, or a quote's run of blocks); the units' heights are in
// a prefix-sum tree, exact where a unit has been laid out and an estimate
// elsewhere, so finding the units a window shows is O(log units) and laying a
// window out is O(what it shows). Document::prepare is the one pass over every
// block: arithmetic on what the blocks say, no line is broken there.
//
// ONE BLOCK, ONE INDEX SPACE. A block's commands carry indices local to it, so
// which block an item belongs to is where the item is stored, and a position
// in a document is (block, index) -- or, through ParagraphLayout's map, (block,
// source byte). Document::flatten renumbers into one sequence, which is what
// the reference ships and what the goldens hold.
//
// ERRORS. Everything a Block holds is data: lengths are clamped to MAX_DIMEN,
// nesting to MAX_QUOTE_DEPTH and MAX_LIST_DEPTH, a container id that names
// nothing is no container, and sums go through add_checked. What a packer or
// ship-out found wrong is on the layout (BlockLayout::ok). Nothing here
// asserts on what a block says.
//
// ALLOCATION. A BlockComposer, a Stacker, a DocumentComposer and a Document
// keep their capacity. Laying a document out again at a width it has had makes
// no allocation.
//
// TABLES. A table is one block (BlockType::TABLE) whose boxes are its rows and
// the rule under its header: src/align sets them, and they are stacked like
// any block's lines, except that the prev_depth carried past a row whose
// cells wrapped is the depth of its lowest line (LineDims::resumed). In a
// Document a table's layout keeps each row as it was shipped (TableLayout),
// and a row is set again only when its cells, the column widths or anything
// else it was set from has changed: a row appended to a table costs that row.
//
// COLUMNS. A document is laid out as one column whatever it is shown in.
// What cutting it into columns needs of this layer is the vertical list by
// dimension, a unit at a time (Document::unit_vlist), and to be told from
// which unit on the document has changed (Document::take_changed): the cuts
// and what is kept of them are src/columns.
//
// Not here: hosted blocks, display formulas (an inline formula is a box
// in a text block's material, in Document::arena), links, markdown, drawing,
// and selection across blocks.

#pragma once

#include "align.h"
#include "linebreak.h"
#include "nodes.h"
#include "pack.h"
#include "paragraph.h"
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

// Block::quote and Block::list for a block in none, and a parent for a
// container at the top.
constexpr int32_t NO_CONTAINER = -1;

// The deepest a quote nests. A deeper one is stacked as this deep. Each level
// is two boxes round its contents, and NodeArena::MAX_NESTING bounds those.
constexpr uint32_t MAX_QUOTE_DEPTH = 64;

// The deepest a list nests. A list declared inside one this deep is treated as
// having no parent.
constexpr uint32_t MAX_LIST_DEPTH = 255;

// The most blocks a Document holds, and the most one block or unit may add to
// a document's height. Together they keep every sum of heights inside a Scaled.
constexpr size_t MAX_DOCUMENT_BLOCKS = size_t(1) << 22;
constexpr Scaled MAX_BLOCK_EXTENT = Scaled(1) << 38;

// The most columns and the most cells a table holds. A table is laid out
// whole, so these bound what one layout can cost.
constexpr size_t MAX_TABLE_COLUMNS = 1024;
constexpr size_t MAX_TABLE_CELLS = size_t(1) << 18;

// Columns between tab stops in a code block: the reference's TAB_WIDTH.
constexpr uint32_t TAB_WIDTH = 4;

enum class BlockType : uint8_t {
    // Styled text, broken into lines: a paragraph, a heading, a list item, a
    // quoted paragraph.
    TEXT = 0,
    // Source text, one line per source line.
    CODE,
    // A thematic break: a horizontal rule across the column.
    RULE,
    // Rows of cells in columns.
    TABLE,
};

// What becomes of a code line longer than the measure.
enum class CodeOverflow : uint8_t {
    // The line is set at its natural width and the document box grows to hold
    // it: the reference.
    GROW = 0,
    // The line's box is as wide as the measure and its characters run past
    // it: BlockLayout::clipped says a draw should clip the block to its box.
    CLIP,
    // The line continues on further lines, each indented by
    // DocumentRules::code_continuation_indent.
    WRAP,
};

// The rules of stacking that the reference and the product disagree on. A
// default one is the reference's behaviour throughout. Each switch is one item
// of docs/divergences.md, `## blocks`, whose number is given.
struct DocumentRules {
    // 2 (and 1). A block that sets no box takes no part in the gaps: the
    // blocks either side are spaced as if it were absent. Off, it contributes
    // its space above and its space below once the list holds something, and
    // swallows both when the list is still empty.
    bool skip_empty_blocks = false;
    // 3. A code block or a rule can be inside a quote, and the quote's bar runs
    // past it. Off, only a text block has a quote depth and anything else ends
    // the bar.
    bool quote_any_block = false;
    // 4. A bar stands at its own quote's edge: Quote::bar_x where the block
    // names a quote, and otherwise the run's least indent less one quote
    // indent for each level the block is quoted at or below the bar's. Off,
    // every bar is one quote indent left of the run's least indent, whatever
    // its level.
    bool bar_by_depth = false;
    // 5. Blocks that name different quotes are different runs, each with its
    // own bar and the blocks' gap between the two. Off, consecutive quoted
    // blocks are one run with one bar.
    bool separate_quotes = false;
    // 6. A rule is at least zero wide and its box exactly as wide as the
    // column. Off, a rule in a column narrower than its indent has a negative
    // width.
    bool floor_rule_width = false;
    // 7. See CodeOverflow.
    CodeOverflow code_overflow = CodeOverflow::GROW;
    // 7. How far a continuation line of a wrapped code line is indented (the
    // hanging indentation of tex.web section 849, with \hangafter 1).
    Scaled code_continuation_indent = 0;
    // 8. No block is indented so far that its lines are narrower than this:
    // the indent is cut back to the column's width less this, and to nothing
    // in a column narrower than this. Zero is the reference: indents are
    // never cut and a line in a column narrower than its indent is one point
    // wide (src/paragraph's `shape`).
    Scaled min_measure = 0;
    // 9. The spacing of 10 also holds for a list inside a quote. Off, blocks
    // inside a quote are spaced by their own two spaces alone.
    bool list_spacing_in_quotes = false;
    // 10. Blocks of one list are spaced by what the list says: nothing between
    // the items of a tight list, the blocks' own spaces between the items of a
    // loose one and between two blocks of one item. Off, every gap is the two
    // spaces whatever the blocks are.
    bool structured_list_spacing = false;
    // 11. A code block's marker is hung on its first line, and a code block
    // with a marker and no lines is one empty line. Off, only a text block's
    // marker is set.
    bool marker_on_any_block = false;
    // 12. A list whose widest marker, with its gap, does not fit List::indent
    // moves its blocks (and whatever is nested in it) right by the excess. Off,
    // the marker overfulls its box and runs into the text.
    bool widen_marker_gutter = false;
    // 13. A rule takes what 8 and 12 do to its container's indent. Off, a
    // rule's indent is used as given.
    bool rule_in_container = false;
    // 16. The space between two blocks is a place a column may end. In the
    // list a document's columns are cut from (Document::unit_vlist) it is
    // glue, where a cut is legal after a box (tex.web section 973), and it is
    // there between any two blocks, of no width where they have no space;
    // after a block that says keep_with_next it is the kern it is in the
    // stack. Off, it is that kern everywhere, which is no legal break unless
    // glue follows it, and none does: a column never ends between two blocks,
    // and a run of one-line blocks cannot be cut at all. The uncut document
    // does not read this.
    bool break_between_blocks = false;
    // 17. A table's header row is kept with the rule under it and with the
    // row after: in the list a document's columns are cut from, the interline
    // glue above the rule and above the first row under the header is a kern
    // of its width, which is no legal break before a box (tex.web section
    // 973). Off, they are the glue they are in the stack, and a column may
    // end between a header and its rule. The uncut document does not read
    // this.
    bool keep_table_header = false;

    // What a document API should use: every switch on, code lines wrapped. `em`
    // is the body size; the least measure is eight of it and a continuation
    // line hangs by two.
    static DocumentRules product(Scaled em = 12 * UNITY);
};

// The two values of the reference's Theme that stacking reads. A block's own
// values are on the block.
struct DocumentTheme {
    // How far a quote indents what is in it. Read for the place of a bar.
    Scaled quote_indent = 0;
    // The width of a quote's bar.
    Scaled quote_rule_thickness = 0;
};

// The lines of a code block: characters, no glue and no kerns.
struct CodeLines {
    std::vector<Node> nodes;
    // One past the last node of each line.
    std::vector<uint32_t> ends;

    size_t count() const { return ends.size(); }
    NodeView line(size_t i) const;
    void clear();

    // Scratch, kept so that setting code again does not allocate.
    std::vector<uint32_t> codepoints;
    std::vector<uint32_t> offsets;
};

// Sets `out` to the lines of `source`, as the reference's `code_block` does:
// `\r\n` and a lone `\r` are newlines; a tab advances to the next multiple of
// TAB_WIDTH columns, counted in characters from the start of its line; every
// other character, a space included, is one CHAR in `font`, or what
// src/style's `stand_in` gives for a character the face lacks; blank lines at
// the end are dropped and others kept. False, with a reason and no lines, if
// the resolver does not know the font's face.
//
// `node_bytes`, if given, is set to one entry per node: the byte of `source`
// its character starts at, as src/style's `decode_utf8` finds characters. The
// several spaces of a tab all have the tab's byte.
bool set_code(CodeLines& out, std::string_view source, FontSpec font,
              const Composition& composition, MetricsResolver resolve, std::string* why = nullptr,
              std::vector<uint32_t>* node_bytes = nullptr);

// How much of a source `set_code` sets, found without a font and by the same
// walk: for a block whose lines are made late (Block::length_hint).
struct CodeExtent {
    // How many lines there are once blank ones at the end are dropped.
    uint32_t lines = 0;
    // One past the last byte of the last character that is set: the line end
    // after it and whatever follows are no part of what is set.
    uint32_t end = 0;
};
CodeExtent code_extent(std::string_view source);

// A table's cells and what setting them reads beside the block's style
// (left_indent, interline, strut): the reference's TableBlock and the two
// values of its Theme a table has.
struct TableCells {
    // Every cell's material, row after row, in one list: cell i is the nodes
    // from cell_end[i - 1] to cell_end[i].
    std::vector<Node> nodes;
    std::vector<uint32_t> cell_end;
    // natural_width of each cell (src/align), padding included.
    std::vector<Scaled> natural;
    // Row r is cells row_first[r] to row_first[r + 1]: one entry more than
    // there are rows, or empty for a table with none.
    std::vector<uint32_t> row_first;
    // A number per row that changes whenever the row's cells do: what a kept
    // row is checked against (TableLayout). From table_stamp().
    std::vector<uint64_t> row_stamp;
    // A number that changes when the cells are emptied (`clear`). Between two
    // changes of it the cells only grow at their end: a cell joins the last
    // row, the last cell is made again, a row is started. So every row but
    // the last is what it was when the next was started, and what was worked
    // out over those rows under one epoch stands: a table that gains a row
    // costs that row and not a pass over the rows before it.
    uint64_t epoch = 0;
    // The most cells any row but the last has.
    uint32_t longest_closed = 0;
    // A number that changes whenever anything else a row is set from does
    // (the alignments, the padding, the fonts): whoever changes one of those
    // takes a new table_stamp().
    uint64_t config = 0;

    std::vector<ColumnAlign> aligns;
    // Who decides the widths. With `held`, `widths` are used while they fit
    // the measure and the default policy otherwise.
    WidthPolicy policy = WidthPolicy::APPORTIONED;
    bool held = false;
    std::vector<Scaled> widths;
    std::vector<double> fractions;
    bool has_header = false;
    Scaled cell_padding = 0;
    Scaled rule_thickness = 0;

    size_t rows() const { return row_first.empty() ? 0 : row_first.size() - 1; }
    size_t cells() const { return cell_end.size(); }
    NodeView cell(size_t i) const;
    // The longest row's cells. Not a pass over the rows.
    size_t columns() const;
    // Ends the row being filled and starts another, which the cells added
    // next belong to. The first cell added starts a row by itself.
    void start_row();
    // Adds a cell, whose material is nodes[cell_end.back()..] as they stand,
    // to the last row.
    void end_cell(Scaled natural_width);
    // Drops the last cell's material, keeping the cell: its row is stamped
    // anew. For a cell whose text grew.
    void reopen_last_cell();
    // Empties the cells, keeping capacity and what is said about columns.
    void clear();
};

// A number no table's row or configuration has had: never zero, never twice
// in a process. A kept row whose number is still its row's was set from what
// the row holds now, whatever was done to the table in between.
uint64_t table_stamp();

// One block before it meets a width.
struct Block {
    BlockType type = BlockType::TEXT;
    // TEXT: everything src/paragraph reads. CODE: left_indent, interline,
    // strut, and marker_gap with a marker. RULE: left_indent. `left_indent` is
    // absolute: what encloses the block plus its own.
    BlockStyle style;
    // The space this block asks for above and below itself. Neither is applied
    // at the edge of a document or of a quote.
    Scaled space_above = 0;
    Scaled space_below = 0;
    // How many quotes enclose the block, for one that names none.
    uint32_t quote_depth = 0;
    // The innermost quote and list the block is in: indices of the tables a
    // Structure views. A block naming a quote takes its depth from it.
    int32_t quote = NO_CONTAINER;
    int32_t list = NO_CONTAINER;
    // Which item of `list` the block belongs to. Blocks of one list with the
    // same number are one item.
    uint32_t item = 0;
    // RULE: how thick.
    Scaled thickness = 0;
    // The colour of what the block draws, for whatever draws it. Not read here.
    uint32_t ink = 0;
    // TEXT: the material, and a list marker. CODE: the marker alone.
    TextBlock text;
    // CODE: the lines.
    CodeLines code;
    // TABLE: the cells.
    TableCells table;
    // A column does not end between this block and the next (a heading and
    // what it heads): read under DocumentRules::break_between_blocks, by
    // Document::unit_vlist alone. tex.web's way to it is a \nobreak, a
    // penalty of 10000, after the heading's box; here the gap after the block
    // stays a kern, which section 973 does not break at.
    bool keep_with_next = false;
    // How much the block will hold, for one whose material or lines are made
    // only when it is first laid out (DocumentComposer::materialize): TEXT,
    // how many nodes; CODE, how many lines, which must then be exact: whether
    // a code block sets a box is decided by it. Otherwise read for the height
    // a block nobody has laid out is taken to have, and only while the block
    // holds nothing.
    uint32_t length_hint = 0;

    // Empties the material, the marker and the lines, keeping capacity, and
    // leaves the rest as it is.
    void clear_content();
};

// One quote: something a run of blocks is in, with a bar down its left.
struct Quote {
    // The quote this one is inside. Must have been declared before it.
    int32_t parent = NO_CONTAINER;
    // The innermost list the quote is inside, whose widened gutter moves it.
    int32_t list = NO_CONTAINER;
    // Where the bar stands: the indent in force where the quote opens.
    Scaled bar_x = 0;
    // The bar's colour. Not read here.
    uint32_t ink = 0;
};

// One list.
struct List {
    // The list this one is inside. Must have been declared before it.
    int32_t parent = NO_CONTAINER;
    // The gutter the list gives its markers: how much it adds to the indent of
    // what is in it.
    Scaled indent = 0;
    // Items follow one another as lines do, with no space between.
    bool tight = false;
};

// The containers a run of blocks refers to, by view.
struct Structure {
    const Quote* quotes = nullptr;
    size_t quote_count = 0;
    const List* lists = nullptr;
    size_t list_count = 0;
};

// What the containers come to: computed once per change of content, read by
// everything below. It belongs to the rules it was derived under.
struct Derived {
    // Per quote: how deep, from 1, capped at MAX_QUOTE_DEPTH.
    std::vector<uint16_t> quote_depth;
    // Per list: how deep, from 1, and how far its blocks move right for
    // widened gutters, its own and those of the lists it is in.
    std::vector<uint16_t> list_depth;
    std::vector<Scaled> list_shift;
    // Scratch: the widest marker need of each list.
    std::vector<Scaled> list_need;
};

// The natural width of a block's marker plus its gap, or zero with no marker:
// what the list's gutter has to hold.
Scaled marker_need(const NodeArena& arena, const Block& block, CharMeasure measure);

// Fills `out` from the containers and from `needs`, one marker_need per block.
void derive(const Block* blocks, size_t count, const Scaled* needs, const Structure& structure,
            const DocumentRules& rules, Derived& out);

// The indent a block is set at in a column `width` wide: its own, moved by its
// list's widened gutter (12) and cut back to leave the least measure (8).
Scaled effective_indent(const Block& block, const Derived& derived, const DocumentRules& rules,
                        Scaled width);

// How many quotes a block is stacked inside: 0 for anything but text unless
// quote_any_block; otherwise its quote's depth, or `quote_depth`.
uint32_t quote_level(const Block& block, const Structure& structure, const Derived& derived,
                     const DocumentRules& rules);

// Whether a block gives the stack at least one box. Decided by what the block
// says, not by a width: a text block always does (with no material it is one
// empty line), a rule does, a code block does when it has a line or, under
// marker_on_any_block, a marker.
bool sets_box(const Block& block, const DocumentRules& rules);

// The space between two blocks that follow one another at one level of
// stacking; `level` is how many quotes that level is inside. The sum of the
// upper block's space below and the lower block's space above, or what 9 and
// 10 make of it.
Scaled block_gap(const Block& above, const Block& below, uint32_t level, const Structure& structure,
                 const Derived& derived, const DocumentRules& rules);

// One past the last block of the run that starts at blocks[first], among
// blocks[first, end), stacking at `level`: the reference's `_runs`. A block
// quoted no deeper than `level` is a run alone; consecutive blocks quoted
// deeper are one run, cut where the quote changes under separate_quotes.
size_t run_end(const Block* blocks, size_t first, size_t end, uint32_t level,
               const Structure& structure, const Derived& derived, const DocumentRules& rules);

// One block as the boxes it gives the stack.
struct BlockBoxes {
    // TEXT: everything ParagraphComposer::set_lines leaves, the index map
    // included. Otherwise empty.
    ParagraphLayout para;
    // The boxes, top to bottom: text and code lines on the block's strut, or a
    // rule's one box. BOX nodes of the arena.
    std::vector<Node> lines;
    // CODE: for each box, whether it continues the source line the box above
    // began (a wrapped line). Empty unless a line was wrapped.
    std::vector<uint8_t> continues;
    // CODE: a line was packed to the measure with its characters running past.
    bool clipped = false;
    // TABLE: for each box, the prev_depth the list goes on from after it
    // (SetRow::prev_depth). Empty for any other block: a box's own depth.
    std::vector<Scaled> resumed;
    // The first line with the marker hung on it, before the strut, for a code
    // block with one; `para.hung` for a text block.
    Node hung;
    PackError pack_error = PackError::NONE;
    LineBreakError break_error = LineBreakError::NONE;
    // Total fit gave up at its budget and the lines are first fit's.
    bool budget_exhausted = false;

    bool ok() const { return pack_error == PackError::NONE && break_error == LineBreakError::NONE; }
};

// Makes blocks into boxes, and owns the buffers that takes. Not thread-safe.
class BlockComposer
{
public:
    // The reference's `_block_boxes`: `block` as its boxes in a column `width`
    // wide, at `indent` (effective_indent; the block's own left_indent is not
    // read). The boxes are added to `arena`, which is also where any BOX or
    // DISC node of the material lives. Returns out.ok().
    //
    //   TEXT  src/paragraph: broken, the marker hung, each line on the strut.
    //   CODE  each line an hbox at its natural width, shifted right by the
    //         indent, on the strut; or clipped or wrapped (CodeOverflow).
    //   RULE  one hbox of a kern as wide as the indent and a rule across the
    //         rest of the column. Not on the strut. A bare rule in the list
    //         would switch interline glue off either side of itself (tex.web
    //         section 1056), which the gap kern has already done.
    bool block_boxes(NodeArena& arena, const Block& block, Scaled indent, Scaled width,
                     const LineBreakParams& params, CharMeasure measure, const DocumentRules& rules,
                     BlockBoxes& out);

    ParagraphComposer& paragraph() { return _paragraph; }
    Aligner& aligner() { return _aligner; }

private:
    void code_boxes(NodeArena& arena, const Block& block, Scaled indent, Scaled width,
                    CharMeasure measure, const DocumentRules& rules, BlockBoxes& out);
    void table_boxes(NodeArena& arena, const Block& block, Scaled indent, Scaled width,
                     const LineBreakParams& params, CharMeasure measure, BlockBoxes& out);
    void rule_box(NodeArena& arena, const Block& block, Scaled indent, Scaled width,
                  CharMeasure measure, const DocumentRules& rules, BlockBoxes& out);

    ParagraphComposer _paragraph;
    std::vector<Node> _scratch;
    Aligner _aligner;
    Aligned _aligned;
    std::vector<NodeView> _cells;
    std::vector<Scaled> _scratch_widths;
};

// Where a block landed: its top and bottom edges, down from the top of what
// was stacked. A block that set no box has the two equal.
struct Extent {
    Scaled top = 0;
    Scaled bottom = 0;
};

// One quote's bar, and the box it is in.
struct Bar {
    // The run it stands beside: block numbers, as the stacking was given them.
    uint32_t first_block = 0;
    uint32_t block_count = 0;
    // How many quotes deep the bar is: 1 for an outermost one.
    uint32_t level = 0;
    // The quote the run names, or NO_CONTAINER.
    int32_t quote = NO_CONTAINER;
    // The bar's rectangle: its left edge, its top (down from the top of what
    // was stacked), its width and its height.
    Scaled x = 0;
    Scaled top = 0;
    Scaled thickness = 0;
    Scaled extent = 0;
    // The barred box: an hbox of the bar and the run's own vbox. Its top edge,
    // and its dimensions.
    Scaled box_top = 0;
    Scaled box_width = 0;
    Scaled box_height = 0;
    Scaled box_depth = 0;
};

// One step of a stacking, for a reader that wants to know why each item of the
// list is there (the goldens' `stack` group).
struct StackEvent {
    enum Kind : uint8_t {
        STACK,  // a: level, b: runs
        RUN,    // a: first block, b: blocks, c: barred
        GAP,    // a: space below, b: space above, c: amount, d: 1 when the list was empty
        KERN,   // node: the gap's kern
        APPEND, // a: prev_depth handed over, b: prev_depth answered
        GLUE,   // node: the interline glue
        REF,    // a: block, b: line
        BARRED, // node: the barred box
        END_BARRED,
        END_RUN,
        END_STACK,
    };
    Kind kind = STACK;
    Scaled a = 0;
    Scaled b = 0;
    Scaled c = 0;
    Scaled d = 0;
    Node node;
};

// What Stacker::stack is given. Blocks are numbered from `first_number`, which
// is what Bar and StackEvent report.
struct StackInput {
    const Block* blocks = nullptr;
    size_t count = 0;
    uint32_t first_number = 0;
    // The boxes of block i are lines[line_first[i]] to lines[line_first[i + 1]]:
    // `count + 1` offsets. BOX nodes of the arena; only a box's dimensions and
    // shift are read, so a box with no contents stands for a line as well as
    // the line does.
    const uint32_t* line_first = nullptr;
    const Node* lines = nullptr;
    // If not null, one per box of `lines`: the prev_depth the list goes on
    // from after it, where that is not the box's depth (a table row whose
    // cells wrapped).
    const Scaled* resumed = nullptr;
    // effective_indent of each block.
    const Scaled* indents = nullptr;
    Structure structure;
    const Derived* derived = nullptr;
    DocumentTheme theme;
    DocumentRules rules;
    CharMeasure measure;
    Scaled width = 0;
};

// A stacking.
struct Stacked {
    // The vertical list: boxes, the gap kerns, interline glue.
    std::vector<Node> vlist;
    // One per block.
    std::vector<Extent> extents;
    // The bars, outermost first and then in document order: the order ship-out
    // meets them in.
    std::vector<Bar> bars;
    // How tall the list is: the sum of what its items occupy.
    Scaled extent = 0;
    // Filled only when `tracing` is set before the call.
    bool tracing = false;
    std::vector<StackEvent> trace;
    PackError pack_error = PackError::NONE;
};

// Stacks blocks, and owns the buffers that takes. Not thread-safe.
class Stacker
{
public:
    Stacker();

    // The reference's `_stacked` at depth 0, with `_barred` for each run that
    // is quoted: `in.blocks` as a vertical list in `out.vlist`. The barred
    // boxes are added to `arena`.
    //
    // Between two runs goes one kern of block_gap, left out when that is zero,
    // and prev_depth starts again at IGNORE_DEPTH, so that the kern is the
    // whole of the space (see the top of this file for what tex.web does
    // instead). Nothing goes above the first run or below the last. Within a
    // run of one block the boxes go in through append_to_vlist (tex.web
    // section 679) under the block's own interline skip. A quoted run is one
    // box: an hbox of a kern to the bar, a rule as tall as the run, a kern
    // back to the left edge, and the vbox of the run stacked one level deeper.
    void stack(NodeArena& arena, const StackInput& in, Stacked& out);

private:
    void stacked(NodeArena& arena, const StackInput& in, size_t first, size_t end, uint32_t level,
                 Scaled base, std::vector<Node>& vlist, Stacked& out);
    Node barred(NodeArena& arena, const StackInput& in, size_t first, size_t end, uint32_t level,
                Scaled top, Stacked& out);

    // One list per level of nesting; the size is fixed, so a reference to one
    // survives the recursion.
    std::vector<std::vector<Node>> _levels;
};

// A whole document composed at once.
struct Composed {
    // The document's vbox, at its natural height, and the list it was packed
    // from. Its reference point is on the last baseline, as any vbox's:
    // ship it from Point{0, packed.box.height} to put its top-left at zero.
    Node box;
    PackResult packed;
    Stacked stacked;
    // The first error any block's boxes or the packing met.
    PackError pack_error = PackError::NONE;
    LineBreakError break_error = LineBreakError::NONE;

    bool ok() const { return pack_error == PackError::NONE && break_error == LineBreakError::NONE; }
};

struct BlockLayout;
struct TableLayout;
class Document;

// Composes documents, and owns the buffers that takes: reuse one across
// documents and frames. Not thread-safe.
class DocumentComposer
{
public:
    // What every block is measured and broken with. `measure` must be the one
    // the blocks' kerns were computed against.
    CharMeasure measure;
    LineBreakParams params = prose_breaking();

    // Work done since this was made: blocks made into boxes (each a paragraph
    // broken or a code block packed), and quoted units stacked.
    uint64_t blocks_composed = 0;
    uint64_t units_assembled = 0;
    // How many blocks Document::prepare has gone over.
    uint64_t blocks_prepared = 0;
    // Table rows set (boxed to their columns and shipped), and longest words
    // measured.
    uint64_t table_rows_set = 0;
    uint64_t table_words_measured = 0;
    // Table rows a layout went over for any reason: to take a row's widths
    // into its columns', to compare what is kept of it, to set it, to stack
    // it. A table that gains a row goes over that row and the one before it,
    // not the table.
    uint64_t table_rows_visited = 0;
    // Changed by whoever changes `params` or `measure`: a table's kept rows
    // were set under the number they remember.
    uint64_t params_serial = 1;

    // If set, called by Document for a block it is about to lay out, before
    // anything of the block is read: a caller that makes material late makes
    // it here, into `block`, with any BOX or DISC node in the document's
    // arena. The marker is not part of that: it is read by `prepare`.
    void (*materialize)(void* user, size_t number, Block& block) = nullptr;
    void* materialize_user = nullptr;

    // The reference's `compose_blocks`: every block made into boxes, stacked
    // and packed at natural height. Everything is added to `arena`. Costs the
    // whole document every time; Document is the one that does not.
    bool compose_blocks(NodeArena& arena, const Block* blocks, size_t count,
                        const Structure& structure, const DocumentTheme& theme,
                        const DocumentRules& rules, Scaled width, Composed& out);

private:
    friend class Document;

    void layout_block(NodeArena& arena, const Block& block, Scaled indent, Scaled width,
                      const DocumentRules& rules, BlockLayout& out);
    void layout_table(NodeArena& arena, const Block& block, Scaled indent, Scaled width,
                      BlockLayout& out);
    void table_narrowest(NodeArena& arena, const Block& block, TableLayout& kept);

    BlockComposer _block;
    Stacker _stacker;
    BlockBoxes _boxes;
    Stacked _stacked;
    Derived _derived;
    std::vector<Scaled> _needs;
    std::vector<Scaled> _indents;
    std::vector<uint32_t> _line_first;
    std::vector<Node> _lines;
    std::vector<Node> _vlist;
    std::vector<Scaled> _resumed;
    std::vector<NodeView> _cells;
    std::vector<Scaled> _widths;
    SetRow _row;
    uint64_t _serial = 0;
};

// One line box's dimensions.
struct LineDims {
    Scaled width = 0;
    Scaled height = 0;
    Scaled depth = 0;
    Scaled shift = 0;
    // The prev_depth the list goes on from after the box: its depth, except
    // after a table row whose cells wrapped.
    Scaled resumed = 0;
};

// One row of a table as it was last set, kept so that it is not set again
// while nothing it was set from changes.
struct TableRowLayout {
    // TableCells::row_stamp of the row when it was set.
    uint64_t stamp = 0;
    bool wrapped = false;
    bool mapped = true;
    LineDims dims;
    // How many reading-order indices the row ships.
    uint32_t item_count = 0;
    // One past the row's last entry in TableLayout::cmds, ::shipped and
    // ::cell_items.
    uint32_t cmd_end = 0;
    uint32_t shipped_end = 0;
    uint32_t cell_end = 0;
    // As the row was last stacked: the list's height, prev_depth and index
    // count after it (and after the head rule, for row 0), and how much of
    // BlockLayout::cmds and ::lines there was then.
    Scaled pen_after = 0;
    Scaled depth_after = 0;
    uint32_t index_after = 0;
    uint32_t out_cmd_end = 0;
    uint32_t out_line_end = 0;
};

// A table laid out: what BlockLayout holds beside the commands.
struct TableLayout {
    // What the kept rows were set from: the widths, and the rest by number.
    std::vector<Scaled> columns;
    uint64_t config = 0;
    uint64_t params_serial = 0;
    Scaled indent = 0;
    bool valid = false;
    // BlockLayout's commands, lines and index map are the kept rows', stacked.
    bool assembled = false;

    std::vector<TableRowLayout> rows;
    // Each row shipped alone, its top-left corner at the origin and its
    // indices from zero, one after another.
    std::vector<DrawCmd> cmds;
    // Per node of each row's cells, SetRow::shipped; and per column of each
    // row, one past the last index of its cell, from the row's first.
    std::vector<uint32_t> shipped;
    std::vector<uint32_t> cell_items;
    // The longest unbreakable piece of each cell, without padding, for the
    // rows whose stamp is in `word_stamp`; measured only when a solve needs it.
    std::vector<Scaled> word;
    std::vector<uint64_t> word_stamp;
    uint64_t word_serial = 0;

    // --- Over the rows that can no longer change (TableCells::epoch): every
    // row but the last, as far as each was taken. fin_col over the cells'
    // natural widths of rows [0, widest_rows) under `widest_epoch`, and over
    // their longest words with `narrowest_padding` on both sides of rows
    // [0, narrowest_rows) under `narrowest_epoch` and `word_serial`.
    std::vector<Scaled> widest_closed;
    size_t widest_rows = 0;
    uint64_t widest_epoch = 0;
    std::vector<Scaled> narrowest_closed;
    size_t narrowest_rows = 0;
    uint64_t narrowest_epoch = 0;
    Scaled narrowest_padding = 0;
    // The epoch of the cells the kept rows were set from.
    uint64_t epoch = 0;
    // If this layout went on from the rows of the one before it: that
    // layout's serial, and how many rows stand as it left them, each with its
    // commands, lines and indices where they were. Zero and zero otherwise.
    uint64_t resumed_from = 0;
    uint32_t resumed_rows = 0;

    // --- As assembled by the last layout
    ColumnSolve solve;
    std::vector<Scaled> widest;
    std::vector<Scaled> narrowest;
    // The widths were the caller's or the held ones, not fitted to the measure.
    bool stated = false;
    // Per row: its top, down from the block's top, and its first index. One
    // more entry than rows: the table's bottom and its item count.
    std::vector<Scaled> row_top;
    std::vector<uint32_t> row_index;
    // The first index of each cell, a row's missing cells included: `columns`
    // entries a row, then one more for the end.
    std::vector<uint32_t> cell_index;
    // How many rows this layout set, and how many it kept.
    uint32_t rows_set = 0;
    uint32_t rows_kept = 0;

    void clear();
};

// One block laid out at one width.
struct BlockLayout {
    // The block shipped alone: its top-left corner at (0, 0), indices from 0.
    // Translate by the block's top to place it.
    std::vector<DrawCmd> cmds;
    // Its boxes, top to bottom.
    std::vector<LineDims> lines;
    // How tall the block is: exact when it is laid out at the current width,
    // otherwise the last exact value or an estimate.
    Scaled extent = 0;
    // The widest of its boxes, with the box's shift.
    Scaled box_width = 0;
    // How many indices the block ships: one past its last.
    uint32_t item_count = 0;
    // A number no two layouts made by one DocumentComposer share: what tells
    // a caller that keeps something derived from a layout that it was redone.
    uint64_t serial = 0;

    // TEXT: ParagraphLayout's data for copy and for positions in the source,
    // with the same meaning (indices local to the block).
    std::vector<uint32_t> shipped;
    bool mapped = false;
    uint32_t marker_items = 0;
    std::vector<Original> originals;
    std::vector<uint32_t> hard_breaks;
    // CODE: BlockBoxes::continues and BlockBoxes::clipped.
    std::vector<uint8_t> continues;
    bool clipped = false;
    // TABLE. `shipped` is then per node of TableCells::nodes.
    TableLayout table;

    PackError pack_error = PackError::NONE;
    LineBreakError break_error = LineBreakError::NONE;
    ShipError ship_error = ShipError::NONE;
    bool budget_exhausted = false;

    bool ok() const
    {
        return pack_error == PackError::NONE && break_error == LineBreakError::NONE &&
               ship_error == ShipError::NONE;
    }
};

// A unit: what is laid out and placed as one. A block, or a quote's run.
struct DocumentUnit {
    uint32_t first_block = 0;
    uint32_t block_count = 0;
    // A quoted run: one barred box.
    bool barred = false;
    // Every block of it is laid out at the current width and it is stacked:
    // its extent is exact.
    bool exact = false;
    // The space above it (zero for the first, and where the rules give none).
    Scaled gap = 0;
    // How tall it is, exact or estimated.
    Scaled extent = 0;
    // For a unit of one block, exact when the units were last made: the
    // serial of the block's layout then. Zero otherwise.
    uint64_t serial = 0;
    // Its bars: Document::bar(first_bar) onwards. Tops are from the unit's top.
    uint32_t first_bar = 0;
    uint32_t bar_count = 0;
};

// A range of units.
struct UnitRange {
    size_t first = 0;
    size_t end = 0;
};

// Where one block's commands went in a flattened document.
struct BlockSpan {
    uint32_t cmd_start = 0;
    uint32_t cmd_end = 0;
    uint32_t index_start = 0;
    uint32_t index_end = 0;
    Scaled top = 0;
};

// A document: blocks, the containers they name, and a layout of each block
// that is made when it is needed and kept until the block or the width
// changes.
class Document
{
public:
    Document();

    // --- Content ---------------------------------------------------------------

    // Drops every block and container, keeping capacity: the blocks added next
    // reuse the buffers of the ones dropped.
    void clear();

    // Where a block's material keeps its BOX and DISC nodes (an inline
    // formula). Layout works in the same arena and takes back what it added;
    // what is added here stays until `clear`.
    NodeArena& arena() { return _arena; }

    // Appends a block and answers its number, or `size()` unchanged at
    // MAX_DOCUMENT_BLOCKS.
    size_t add_block();
    size_t size() const { return _count; }
    const Block& block(size_t i) const;
    // A block to change. Its layout is stale from now on. The reference is
    // good until the next add_block.
    Block& edit(size_t i);

    // A block whose text or lines are about to change and nothing else of it:
    // not its type, its marker, its spaces, its indent or what it is in. Its
    // layout is stale, as after `edit`, but the units stand and no `prepare`
    // follows: the block is laid out when a window next shows it.
    Block& edit_text(size_t i);
    // The block as it is, to fill in what DocumentComposer::materialize makes
    // late. Changes nothing about what is laid out.
    Block& material(size_t i);
    // Takes `removed` blocks out at `first` and puts `inserted` new ones
    // there, each as `add_block` makes one; the blocks after them keep their
    // layouts under their new numbers. False, with nothing changed, if the
    // range is not the document's or the result would pass
    // MAX_DOCUMENT_BLOCKS.
    bool splice(size_t first, size_t removed, size_t inserted);

    // Declares a container and answers its id. A parent that is not an
    // earlier one is no parent.
    int32_t add_quote(const Quote& quote);
    int32_t add_list(const List& list);
    Structure structure() const;

    void set_rules(const DocumentRules& rules);
    void set_theme(const DocumentTheme& theme);
    const DocumentRules& rules() const { return _rules; }
    const DocumentTheme& theme() const { return _theme; }

    // --- Layout ------------------------------------------------------------------

    // The column. Every layout is stale after a change; none is redone until a
    // window asks for it, and the heights already known stand as estimates.
    void set_width(Scaled width);
    Scaled width() const { return _width; }

    // Brings the units, the gaps and the height estimates up to date with the
    // content: one pass over the blocks, in which no block is laid out.
    // Everything below calls it when the content has changed.
    void prepare(DocumentComposer& composer);
    // The same, if anything has changed since the last one.
    void prepare_if_needed(DocumentComposer& composer)
    {
        if (!_prepared)
            prepare(composer);
    }
    // What the containers came to at the last `prepare`.
    const Derived& derived() const { return _derived; }

    // Lays out the units a window shows: those between `top` and `bottom`,
    // down from the top of the document. Answers the units it found, which
    // are exact on return; units above them are not touched, so nothing above
    // the window moves. O(log units) to find the first and then one step a
    // unit: a caller placing them does the same, from unit_top(first) adding
    // each unit's extent and the next one's gap.
    UnitRange layout_window(DocumentComposer& composer, Scaled top, Scaled bottom);
    // Lays out every unit.
    void layout_all(DocumentComposer& composer);
    // Lays out one unit, whatever is above it: its height is exact on return,
    // its top only as exact as the units above it are.
    void layout_unit(DocumentComposer& composer, size_t u);

    // What follows answers for the document as it was last prepared: a change
    // of content or width shows in it after the next `prepare`,
    // `layout_window` or `layout_all`.

    // True when every unit is exact and nothing has changed since: `height`
    // is then the document's.
    bool settled() const { return _prepared && _inexact == 0; }
    // The document's height: the sum of its units and gaps, estimates included.
    Scaled height() const;
    // The widest box of any block laid out at the current width.
    Scaled content_width() const;

    size_t unit_count() const { return _units.size(); }
    const DocumentUnit& unit(size_t u) const;
    const Bar& bar(size_t i) const;
    // The top of a unit, O(log units).
    Scaled unit_top(size_t u) const;
    // The unit at `y`: the last whose top, less its gap, is at or above it.
    // `unit_count()` below the last. O(log units), plus one step for each
    // unit a negative gap pulls over the one found.
    size_t unit_at(Scaled y) const;
    // The unit a block is in, once prepared.
    size_t unit_of(size_t block) const;

    // Whether a block is laid out at the current width.
    bool laid_out(size_t block) const;
    const BlockLayout& layout(size_t block) const;
    // The top of a block: its unit's, and where it is within the unit, which
    // is known once the unit is exact.
    Scaled block_top(size_t block) const;
    // Where a block is below the top of its unit: zero for a block that is a
    // unit alone.
    Scaled top_in_unit(size_t block) const;
    // A laid-out table's column: its longest unbreakable piece with padding,
    // measured now if the table's widths did not need it. Zero for a block
    // that is not a laid-out table.
    Scaled table_floor(DocumentComposer& composer, size_t block, size_t column);

    // --- The vertical list, for cutting into columns (src/columns.h) ------------

    // What take_changed answers when nothing has changed.
    static constexpr size_t UNCHANGED = static_cast<size_t>(-1);
    // The first unit that is not what it was when this was last asked: laid
    // out again or due to be, another block's, in another place among the
    // units, or new. Every unit before it stands at the height it stood at
    // with the lines it had. UNCHANGED if there is none. Asking forgets the
    // answer; ask after `prepare`.
    size_t take_changed();
    // Appends unit `u`'s part of the document's vertical list by dimension:
    // the gap above it and, for each of its boxes, the interline glue and the
    // box, as Stacker::stack puts them in the list at level 0. A box is a RULE
    // node of its width, height and depth: what a scan for a place to cut
    // reads of a box, with no arena behind it. A quoted run is one box. The
    // gap is a kern, or glue under DocumentRules::break_between_blocks. The
    // unit must be laid out (layout_unit).
    void unit_vlist(size_t u, std::vector<Node>& out) const;
    // The top of each box of a laid-out block, down from the block's top:
    // where unit_vlist's boxes stand.
    void block_line_tops(size_t block, std::vector<Scaled>& tops) const;

    // The whole document as one command sequence, as shipping the reference's
    // document box from its top-left corner gives it: every block's commands
    // moved to its place and renumbered, with each quote's LINE and its bar's
    // RULE_RECT before what the quote holds. False, with nothing appended,
    // unless the document is settled. `spans`, if given, receives one entry per
    // block.
    bool flatten(std::vector<DrawCmd>& out, std::vector<BlockSpan>* spans = nullptr) const;

private:
    struct Slot {
        bool valid = false;
        Scaled width = 0;
        Scaled indent = 0;
        uint32_t epoch = 0;
        // Where the block is within its unit.
        Scaled top = 0;
        // marker_need, and whether it is the block's as it stands.
        Scaled need = 0;
        bool need_known = false;
        // The block has been laid out at some width: its layout's extent is a
        // height it once had.
        bool ever = false;
        // On the first block of a quoted run: the run's height when it was
        // last stacked, and how many blocks it then had.
        Scaled run_extent = 0;
        uint32_t run_count = 0;
    };

    bool slot_valid(size_t block) const;
    Scaled estimate(size_t block) const;
    void ensure_unit(DocumentComposer& composer, size_t u);
    void assemble(DocumentComposer& composer, size_t u);
    void set_extent(size_t u, Scaled extent);
    void reset_block(size_t index);
    Scaled prefix(size_t u) const;
    void tree_add(size_t u, Scaled delta);
    void note_changed(size_t u)
    {
        if (u < _changed)
            _changed = u;
    }

    NodeArena _arena;
    // The first `_count` are the document's; the rest are kept for their
    // buffers.
    size_t _count = 0;
    std::vector<Block> _blocks;
    std::vector<BlockLayout> _layouts;
    std::vector<Slot> _slots;
    std::vector<Quote> _quotes;
    std::vector<List> _lists;
    DocumentRules _rules;
    DocumentTheme _theme;
    Derived _derived;

    Scaled _width = 0;
    uint32_t _epoch = 1;
    bool _prepared = false;

    std::vector<DocumentUnit> _units;
    // The units as they were before the last `prepare`, to find the first
    // that differs; and that, since take_changed was last asked.
    std::vector<DocumentUnit> _units_before;
    size_t _changed = 0;
    std::vector<uint32_t> _unit_of;
    std::vector<Bar> _bars;
    // A Fenwick tree over each unit's gap plus extent.
    std::vector<Scaled> _tree;
    size_t _inexact = 0;

    // What an accessor answers for a number the document does not have.
    Block _spare_block;
    BlockLayout _spare_layout;
    DocumentUnit _spare_unit;
    Bar _spare_bar;
};

} // namespace ImTypeset::detail
