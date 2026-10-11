// Tables: a column's width from its entries, and each row set to the columns.
// Mirrors the table layout of advtext/ui/__init__.py (`_natural_widths`,
// `_longest_word`, `_column_widths`, `_apportioned`, `_squeezed`, `_shared`,
// `_table_boxes`, `_table_row`, `_wrapped_row`, `_cell_lines`, `_cell_params`,
// `_aligned`, `_WrappedRow`, `prev_depth_after`, and `_stacked` for one table).
//
// WHAT OF tex.web THIS IS. Part 37, "Alignment" (sections 768 to 812), as far
// as an \halign of fixed templates goes:
//
//   section 796        an entry is packaged at its natural   natural_width,
//                      width and the width recorded in its   fin_col
//                      column's alignrecord
//   section 799        fin_row: the finished row goes into   fin_row
//                      the vertical list by append_to_vlist
//   section 800        fin_align                             Aligner::fin_align
//   section 801        the final column widths w_j           column_widths
//   sections 804-810   the prototype row is packaged and     Aligner::set_row
//                      every unset row becomes an hlist of
//                      boxes as wide as their columns
//   section 806        a rule between rows runs to the       Aligner::head_rule
//                      alignment's edges
//
// and, from part 47, section 1087's \vtop (a wrapped cell).
//
// The order of work is not tex.web's. There a row is read, each entry is
// packaged unset as it ends (fin_col), the row is appended unset (fin_row),
// and fin_align sets every row at the end. Here nothing is unset: a cell is a
// list the caller keeps, its natural width is asked for when wanted, and a row
// is built once, at the widths decided, so fin_row comes after set_row.
//
// WHERE THIS IS NOT tex.web. Each of these is the reference's, and
// docs/divergences.md lists them; what is this port's alone is under ERRORS.
//
//   - The widths fill the measure. Section 801 leaves every w_j at the widest
//     entry of its column and section 804 packages the prototype row to the
//     \halign's `to` or `spread`, which stretches or shrinks the tabskip glue
//     between the columns and nothing else. Here a table narrower than the
//     measure has its columns scaled up in proportion (`apportioned`), and one
//     wider is squeezed: each column is given its longest unbreakable piece
//     and the rest of the measure is shared in proportion to what each column
//     could still use (`squeezed`). A caller may give the widths, or shares of
//     the measure (`shared`), instead.
//   - An entry wider than its column is broken. Section 810 sets an entry's
//     glue as if to the column's width and lets an overfull one stand; an
//     entry is restricted horizontal material and never a paragraph. Here a
//     row with such an entry has every cell broken first fit at its column's
//     width and reboxed as a \vtop (section 1087), so the cells of the row
//     hang from one first baseline.
//   - Padding is a kern inside each cell where tex.web has tabskip glue
//     between the columns (sections 778, 795, 809), and the table's indent is
//     a kern at the head of each row where section 807 has a shift_amount.
//   - No preamble and no templates (sections 777 to 784, 789, 793, 794), no
//     \noalign (section 785), no \omit (sections 788, 790), no \span
//     (sections 797, 798, 803, 808) and no \valign (section 811): a cell is
//     a list and has one column. A row may end early; the columns it lacks
//     are set empty, where section 807 leaves the row's list as long as it
//     was written.
//   - A column's width starts at zero, not at section 801's null_flag: a
//     column whose every entry is narrower than nothing is zero wide.
//   - The depth the next row is spaced from. After a row of \vtops section 679
//     reads the row's own depth, which is every line below the first. Here it
//     reads the depth of the row's lowest line (SetRow::prev_depth), which is
//     what a document that assigned \prevdepth after the row would do.
//   - A row is floored at the table's strut, once, on the row.
//
// THE STEPS, each of which a caller takes alone, because the layer above keeps
// what it learned of a cell and of a row and sets again only what changed:
//
//   a cell     natural_width; Aligner::longest_word, which is only asked when
//              the table does not fit
//   a column   fin_col over the cells' widths, twice: widest and narrowest
//   the solve  column_widths: the answer, which regime it was, and what it
//              went through
//   a row      Aligner::set_row: the row's box, whether it wrapped, and the
//              prev_depth to go on from
//   the rule   Aligner::head_rule
//   a table    Aligner::fin_align: all of the above and the vertical list
//
// ERRORS. Everything here is data. A row has as many cells as it has; a
// column a row lacks is empty; lengths are clamped to MAX_DIMEN; sums go
// through add_checked and a product through a 128-bit intermediate; a policy
// that is not one the reference would construct is refused and the default
// taken, and the solve says so. What a packer or the breaker found wrong is on
// the result. Nothing asserts on what a table holds.
//
// ALLOCATION. An Aligner keeps its buffers, and so do an Aligned and a
// ColumnSolve, so setting table after table with the same objects settles to
// no allocation apart from what is added to the arena.
//
// Not here: cell borders and fills (the reference has none), a table cut
// between rows, selection inside a table, and markdown.

#pragma once

#include "linebreak.h"
#include "nodes.h"
#include "pack.h"
#include "style.h"
#include "units.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ImTypeset::detail
{

// Where a column's entries sit in it: the reference's ColumnAlign, in its
// order. What tex.web's templates say with \hfil on one side or both.
enum class ColumnAlign : uint8_t { LEFT = 0, CENTER, RIGHT };

// TeX's \hss, the reference's `HSS`: what places an entry in a column wider
// than it, and lets one wider than its column overhang instead of overfull.
constexpr GlueSpec HSS = {0, UNITY, GlueOrder::FIL, UNITY, GlueOrder::FIL};

// The most columns a table has. fin_col refuses a column past it; a table is
// indexed by 32 bits and a row's box holds one node a column.
constexpr size_t MAX_ALIGN_COLUMNS = size_t(1) << 20;

// What setting a table reads of the reference's TableBlock, its BlockStyle and
// its Theme. Lengths are clamped to MAX_DIMEN where they are used.
struct TableStyle {
    // The table's distance from the column's left edge: a kern at the head of
    // every row.
    Scaled left_indent = 0;
    // A kern on each side of every cell. Column widths include both.
    Scaled cell_padding = 0;
    // The height of the rule under a header; zero for none.
    Scaled rule_thickness = 0;
    // The glue between rows, and between the lines of a wrapped cell (tex.web
    // section 679).
    InterlineSkip interline;
    // What no row, and no line of a wrapped cell, may fall below.
    Strut strut;
};

// --- A cell -----------------------------------------------------------------------

// A cell set on one line: the natural width of `material` plus the padding on
// both sides (the reference's `_natural_widths`, for one cell). The packaging
// of tex.web section 796, whose w is the same hpack's width without padding.
// `arena` is where the material's BOX nodes live. `error` receives the first
// pack error if it is given and holds NONE.
Scaled natural_width(const NodeArena& arena, NodeView material, Scaled cell_padding,
                     CharMeasure measure, PackError* error = nullptr);

// Records an entry's width in its column: `widths[column]` becomes the larger
// of itself and `w`, the vector growing with zeros to hold the column. The
// second half of tex.web section 796 (`if w > width(cur_align)`), for a span
// of one. A column starts at zero where section 801 starts it at null_flag,
// so a negative `w` leaves it at zero. False, and nothing recorded, for a
// column at or past MAX_ALIGN_COLUMNS.
bool fin_col(std::vector<Scaled>& widths, size_t column, Scaled w);

// --- The column widths --------------------------------------------------------------

// Who decides the widths.
enum class WidthPolicy : uint8_t {
    // The table's contents and the measure.
    APPORTIONED = 0,
    // The caller, in scaled points, padding included; the table is as wide as
    // they sum to whatever the measure.
    WIDTHS,
    // The caller, as shares of the measure.
    FRACTIONS,
};

// A table's width policy. The arrays are the caller's and are only read.
struct ColumnPolicy {
    WidthPolicy kind = WidthPolicy::APPORTIONED;
    // WIDTHS: one per column, each at least 1.
    const Scaled* widths = nullptr;
    // FRACTIONS: one per column, each finite and positive, and still positive
    // as a share of their sum. Not normalised.
    const double* fractions = nullptr;
    // How many entries the array in use has. Zero is APPORTIONED.
    size_t count = 0;
};

// How the widths were reached: the reference's seven ways out of
// `_column_widths`.
enum class SolveRegime : uint8_t {
    // The caller's widths.
    GIVEN = 0,
    // The caller's shares of the measure.
    FRACTIONS,
    // The table fits: its columns scaled up to fill the measure.
    APPORTIONED,
    // The same, when the widest cells sum to nothing: the widths as they are.
    UNSCALED,
    // The table does not fit and its longest words do: floors, then the slack.
    SQUEEZED,
    // Not even the longest words fit: the table is wider than the measure.
    FLOORS,
    // The words fit and no column has anything to gain: the floors.
    RIGID,
};

// Why a caller's policy was not used. The reference refuses each of these
// when the table is constructed; here the default policy is taken.
enum class PolicyRefusal : uint8_t {
    NONE = 0,
    // Not one entry per column.
    WRONG_COUNT,
    // A width below 1.
    WIDTH_BELOW_ONE,
    // A share that is not finite and positive, a sum that is not finite, a
    // share that is nothing as a part of the sum, or one whose width is not a
    // finite number.
    NOT_A_SHARE,
    // The array is null.
    NO_ARRAY,
};

// The widths, and what they were reached through. Which of the intermediate
// values mean anything follows the regime:
//
//   GIVEN        nothing
//   FRACTIONS    share_total; floored (each share rounded); last; borrowed
//   APPORTIONED  total (the widest cells' sum); floored
//   UNSCALED     total
//   FLOORS       slack
//   RIGID        slack; give; total (give's sum)
//   SQUEEZED     slack; give; total; floored
struct ColumnSolve {
    SolveRegime regime = SolveRegime::UNSCALED;
    // NONE unless a WIDTHS or FRACTIONS policy was refused, in which case the
    // regime is one of the default policy's.
    PolicyRefusal refused = PolicyRefusal::NONE;
    // A sum or a product left the range of a Scaled, which no table whose
    // lengths honour MAX_DIMEN can do. The widths are defined and wrong.
    bool overflowed = false;
    // max(width - left_indent, one point): what the columns share.
    Scaled available = 0;
    Scaled total = 0;
    double share_total = 0.0;
    Scaled slack = 0;
    // FRACTIONS: the last column once it has taken the rounding remainder,
    // before anything is borrowed for it.
    Scaled last = 0;
    // FRACTIONS: the column the last one borrowed from, or -1.
    int64_t borrowed = -1;
    std::vector<Scaled> floored;
    std::vector<Scaled> give;
    // The answer: one width per column, padding included.
    std::vector<Scaled> columns;
};

// The final column widths: tex.web section 801's step of fin_align, and the
// reference's `_column_widths`. `widest` is each column's widest cell
// (fin_col over natural_width) and `narrowest` its longest unbreakable piece
// plus padding (fin_col over longest_word + 2 * cell_padding), `columns` of
// each; `width` is the measure and `left_indent` what the table gives up of
// it.
//
// `narrowest` may be null, since measuring it costs a line break a cell and
// only a table that does not fit needs it. The answer is then false, with no
// widths in `out`, exactly when it is needed: call again with it. True
// otherwise.
//
// Not section 801's formula, which has no measure in it: there w_j is the
// widest entry and stays so, and the \halign's `to` is met by the tabskip
// glue (section 804). Every division floors, as the reference's do.
bool column_widths(const ColumnPolicy& policy, const Scaled* widest, const Scaled* narrowest,
                   size_t columns, Scaled width, Scaled left_indent, ColumnSolve& out);

// The three rules `column_widths` chooses among, as the reference has them.
// Each writes `out.columns`, its regime and its intermediate values, and reads
// `out.available`.

// `widest` scaled to fill `out.available`, in proportion, the last column
// taking the rounding remainder: `_apportioned`. APPORTIONED, or UNSCALED.
void apportioned(const Scaled* widest, size_t columns, ColumnSolve& out);

// Floors first, then the slack in proportion to `widest - narrowest`, the
// last column taking the remainder: `_squeezed`. SQUEEZED, FLOORS or RIGID.
void squeezed(const Scaled* widest, const Scaled* narrowest, size_t columns, ColumnSolve& out);

// `out.available` divided in the proportions of `fractions`, each column at
// least one scaled point: `_shared`. The shares are rounded half to even, the
// last column takes the remainder, and what that would take from it below one
// is taken from the first of the widest columns instead. False, with `refused`
// set and no widths in `out`, when the fractions are not shares.
bool shared(const double* fractions, size_t columns, ColumnSolve& out);

// --- A row ----------------------------------------------------------------------------

// One row to set. The arrays are the caller's. A cell's material is a list in
// storage the caller owns: not a view of the arena, which grows as the row is
// built.
struct RowInput {
    // The cells the row has, which may be fewer than the columns. A cell past
    // `column_count` is not set.
    const NodeView* cells = nullptr;
    size_t cell_count = 0;
    // natural_width of each cell, `cell_count` of them. Null to have them
    // measured here.
    const Scaled* natural = nullptr;
    // The widths to set to, one per column.
    const Scaled* columns = nullptr;
    size_t column_count = 0;
    // The columns' alignments. A column past `align_count` is LEFT.
    const ColumnAlign* aligns = nullptr;
    size_t align_count = 0;
};

// A row, set.
struct SetRow {
    // The row as a BOX node of the arena, on the table's strut.
    Node box;
    // The row's box as it was packed, before the strut: the same contents.
    Box unstrutted;
    // Whether a cell was wider than its column, and every cell of the row is
    // therefore a stack of lines.
    bool wrapped = false;
    // How far the row's lowest baseline is below its reference point: zero
    // unless a cell wrapped to a second line. The reference's
    // `_WrappedRow.last_baseline`.
    Scaled lowest = 0;
    // The prev_depth a vertical list goes on from after this row (the
    // reference's `prev_depth_after`): the box's depth, or after a wrapped
    // row its depth less `lowest`, which is the depth of its lowest line.
    Scaled prev_depth = 0;
    // How many reading-order indices ship-out gives the row (src/shipout: one
    // for each character, each glue and each atom box; none for a kern, a
    // penalty or a rule), and for each COLUMN, a cell the row lacks included,
    // one past the last index of its cell, counted from the row's first. The
    // cells tile the row's indices in column order, and every cell owns at
    // least one: the glue that places it.
    uint32_t item_count = 0;
    std::vector<uint32_t> cell_index_end;
    // For each node of each cell the row has, cell after cell: the index it
    // ships as, from the row's first, or NOT_SHIPPED (src/paragraph.h) for a
    // kern, a penalty and a node the breaker discarded. What
    // ParagraphLayout::shipped is for a paragraph. A wrapped cell broken at a
    // discretionary holds nodes that are not its material's: its entries are
    // all NOT_SHIPPED and `mapped` is false.
    std::vector<uint32_t> shipped;
    bool mapped = true;
    PackError pack_error = PackError::NONE;
    LineBreakError break_error = LineBreakError::NONE;

    bool ok() const { return pack_error == PackError::NONE && break_error == LineBreakError::NONE; }
};

// What setting a wrapped row went through, cell by cell, for a test to hold
// against the reference. Filling it allocates.
struct CellTrace {
    Scaled column_width = 0;
    // What the cell's lines are broken to: the column less its padding, at
    // least a point.
    Scaled inner = 0;
    LineBreakParams params;
    // The list that was broken: the cell's material with its shrink removed.
    std::vector<Node> broken;
    Paragraph paragraph;
    struct Line {
        Scaled prev_depth_in = 0;
        Scaled prev_depth_out = 0;
        bool has_glue = false;
        Node glue;
        // The line as it went into the cell's list: shifted by the padding
        // and on the strut.
        Box box;
    };
    std::vector<Line> lines;
    // The cell's lines packed, its reference point on the last baseline, and
    // the same as a \vtop as wide as the column.
    Box stacked;
    Box vtop;
    // How far the cell's last baseline is below its first.
    Scaled below = 0;
};

struct RowTrace {
    std::vector<CellTrace> cells;
};

// `params` as a wrapped cell of a column aligned `align` is broken with (the
// reference's `_cell_params`): first fit, the fil of ragged setting on the
// side the alignment leaves free, and no \parfillskip unless the column is
// LEFT.
LineBreakParams cell_params(ColumnAlign align, const LineBreakParams& params);

// Appends `material` to `out` with the \hss that places it in its column (the
// reference's `_aligned`): after it for LEFT, before it for RIGHT, both for
// CENTER. What a template's \hfil does in tex.web.
void aligned(NodeView material, ColumnAlign align, std::vector<Node>& out);

// Appends a set row, or the head rule's box, to a vertical list with its
// interline glue, and answers what append_to_vlist answered: the box's depth.
// The half of tex.web's fin_row (section 799) that puts the row in the list.
// The prev_depth to hand the NEXT call is not that answer but the row's
// SetRow::prev_depth, which differs after a wrapped row.
Scaled fin_row(const NodeArena& arena, std::vector<Node>& vlist, const Node& row, Scaled prev_depth,
               const InterlineSkip& interline);

// --- A table ----------------------------------------------------------------------------

// A whole table to set. The arrays are the caller's.
struct TableInput {
    // Every cell, row after row. Material in storage the caller owns.
    const NodeView* cells = nullptr;
    // Row r is cells[row_first[r]] to cells[row_first[r + 1]]: `rows + 1`
    // offsets, not decreasing.
    const uint32_t* row_first = nullptr;
    size_t rows = 0;
    // Row 0 is a header: the rule goes under it.
    bool has_header = false;
    const ColumnAlign* aligns = nullptr;
    size_t align_count = 0;
    ColumnPolicy policy;
    TableStyle style;
};

// One box of a set table, in the order they are stacked.
struct TableBox {
    // A row's box on its strut, or the head rule's box.
    Node box;
    // The row, or -1 for the head rule.
    int64_t row = -1;
    bool wrapped = false;
    // What append_to_vlist was handed and what it answered, and what the next
    // append is handed: `prev_depth_out`, except after a wrapped row.
    Scaled prev_depth_in = 0;
    Scaled prev_depth_out = 0;
    Scaled prev_depth_carried = 0;
    // Whether interline glue went above the box; it is the node before the
    // box in Aligned::vlist.
    bool has_glue = false;
    // Where the box is in Aligned::vlist.
    uint32_t vlist_index = 0;
};

// A set table. Nodes are the arena's it was set in.
struct Aligned {
    // natural_width of every cell, in TableInput::cells' order.
    std::vector<Scaled> natural;
    // Each column's widest cell, and its longest unbreakable piece with
    // padding. `narrowest` is empty unless the solve needed it.
    std::vector<Scaled> widest;
    std::vector<Scaled> narrowest;
    ColumnSolve solve;
    // The rows, and the head rule after row 0 when there is one.
    std::vector<TableBox> boxes;
    // `boxes` with interline glue between them, started from IGNORE_DEPTH,
    // and that list packed at its natural size. `box` is the vbox as a node;
    // its reference point is on the last row's baseline.
    std::vector<Node> vlist;
    PackResult packed;
    Node box;
    // How tall the list is: what its items occupy, top to bottom.
    Scaled extent = 0;
    PackError pack_error = PackError::NONE;
    LineBreakError break_error = LineBreakError::NONE;

    bool ok() const { return pack_error == PackError::NONE && break_error == LineBreakError::NONE; }
};

// Sets tables, and owns the buffers that takes. Reuse one across cells, rows
// and tables. Not thread-safe; one belongs to one thread at a time.
class Aligner
{
public:
    // The width of the widest stretch of a cell that no line break can divide,
    // without padding (the reference's `_longest_word`): `material`, as it is
    // and not with its shrink removed, broken first fit at a measure of one
    // point with no left or right skip, each line then measured at its natural
    // width. Zero for a cell with no material. `arena` is where the material's
    // BOX and DISC nodes live; the lines are dropped from it again before this
    // returns unless `keep_lines` is set, which is for a caller that wants to
    // look at them.
    //
    // No part of tex.web: an alignment entry there is never broken.
    Scaled longest_word(NodeArena& arena, NodeView material, const LineBreakParams& params,
                        CharMeasure measure, bool keep_lines = false);

    // What the last longest_word measured each line at; empty for a cell with
    // no material. And the paragraph the last longest_word or the last cell
    // of the last wrapped row was broken into, whose line boxes are in the
    // arena only if the lines were kept.
    const std::vector<Scaled>& word_lines() const { return _word_lines; }
    const Paragraph& broken() const { return _paragraph; }

    // Sets one row to `row.columns` and adds its box to `arena`: the step of
    // tex.web sections 807 to 810, and the reference's `_table_row` or
    // `_wrapped_row` with the strut `_table_boxes` puts on either.
    //
    // The row fits when no cell it has is wider, at its natural width with
    // padding, than its column. It is then one hbox: a kern of the indent,
    // and for each column an hbox packed to exactly the column's width of
    // kern, the material with its \hss, kern (section 810, where the padding
    // is tabskip glue outside the box).
    //
    // Otherwise every cell of the row, the ones that fit included, is broken
    // first fit at the column's width less its padding, from the material
    // with its shrink removed; each line is shifted right by the padding and
    // set on the strut; the lines are stacked under the table's interline
    // glue from IGNORE_DEPTH and packed; and that box is given the column's
    // width and its first line's height, what was height below that becoming
    // depth: a \vtop (section 1087, whose height is the first item's too).
    // The row is an hbox of the indent and those. tex.web has no counterpart:
    // section 810 would leave the entry overfull.
    //
    // A column the row has no cell for is set as an empty cell and takes no
    // part in whether the row fits.
    //
    // Returns out.ok().
    bool set_row(NodeArena& arena, const RowInput& row, const TableStyle& style,
                 const LineBreakParams& params, CharMeasure measure, SetRow& out,
                 RowTrace* trace = nullptr);

    // The rule under a header: an hbox of a kern of the indent and a rule as
    // wide as the columns sum to and `style.rule_thickness` high, not on the
    // strut. tex.web's rule between rows has running width, which section 806
    // extends to the alignment's edges; here the width is written. `error`
    // receives a pack error or an overflowing sum if it is given and holds
    // NONE.
    Node head_rule(NodeArena& arena, const Scaled* columns, size_t column_count,
                   const TableStyle& style, PackError* error = nullptr);

    // A whole table at a measure of `width`: tex.web's fin_align (section
    // 800), and the reference's `_table_boxes` followed by `_stacked` for the
    // one block. Every cell is measured, the widths solved (the longest words
    // measured only if the solve asks), every row set, the head rule put
    // after row 0 when the table has a header and a rule thickness, and the
    // boxes stacked from IGNORE_DEPTH with fin_row, each handed the prev_depth
    // the box before it carried. A table with no rows is an empty list and an
    // empty box.
    //
    // `out` is overwritten. Returns out.ok().
    bool fin_align(NodeArena& arena, const TableInput& table, Scaled width,
                   const LineBreakParams& params, CharMeasure measure, Aligned& out);

    // The breaker every call uses.
    LineBreaker& breaker() { return _breaker; }

private:
    void cell_lines(NodeArena& arena, NodeView material, ColumnAlign align, Scaled width,
                    const TableStyle& style, const LineBreakParams& params, CharMeasure measure,
                    SetRow& out, Node& cell, Scaled& below, uint32_t& index, bool has_cell,
                    CellTrace* trace);

    LineBreaker _breaker;
    Paragraph _paragraph;
    std::vector<Scaled> _word_lines;
    std::vector<Scaled> _row_natural;
    // One cell's list; one row's list; one wrapped cell's lines.
    std::vector<Node> _cell;
    std::vector<Node> _row;
    std::vector<Node> _lines;
};

} // namespace ImTypeset::detail
