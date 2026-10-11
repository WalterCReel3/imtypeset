// Tables: see align.h for what this is of tex.web's part 37 and what is not.

#include "align.h"

#include "paragraph.h"
#include "shipout.h"

#include <cmath>

namespace ImTypeset::detail
{

namespace
{

// How many levels of box a table puts round a cell's material: a line, the
// cell, the row and the table's own box, and room for what a caller wraps the
// table in. Material nested deeper than MAX_NESTING less this is not set.
constexpr int ALIGN_NESTING_RESERVE = 8;

// A length somebody else chose, inside the range a node may hold.
constexpr Scaled align_length(Scaled value)
{
    return value > MAX_DIMEN ? MAX_DIMEN : (value < -MAX_DIMEN ? -MAX_DIMEN : value);
}

void align_note(PackError& first, PackError error)
{
    if (first == PackError::NONE)
        first = error;
}

void align_note(LineBreakError& first, LineBreakError error)
{
    if (first == LineBreakError::NONE)
        first = error;
}

// How many reading-order indices ship-out gives `node`: one for a character
// and for a glue, one for an atom box, and for any other box what its
// contents come to. False if `budget` nodes were visited before the end,
// which a tree that shares boxes can make happen.
bool align_items_of(const NodeArena& arena, const Node& node, uint64_t& budget, uint32_t& count)
{
    if (budget == 0)
        return false;
    --budget;
    if (node.type == NodeType::CHAR || node.type == NodeType::GLUE) {
        ++count;
        return true;
    }
    if (node.type != NodeType::BOX)
        return true;
    const Box* const box = arena.find_box(node);
    if (box == nullptr)
        return true;
    if (is_atom(arena, *box)) {
        ++count;
        return true;
    }
    for (const Node& inner : arena.view(box->contents)) {
        if (!align_items_of(arena, inner, budget, count))
            return false;
    }
    return true;
}

// Makes `nodes` safe to put in a box of `arena`: a BOX or DISC node the arena
// does not hold, or one nested so deep that the boxes a table adds round it
// would pass NodeArena::MAX_NESTING, becomes an empty rule. The reference
// cannot build either. False if anything was replaced.
bool align_settle(const NodeArena& arena, std::vector<Node>& nodes, size_t first)
{
    bool clean = true;
    for (size_t i = first; i < nodes.size(); ++i) {
        Node& node = nodes[i];
        if (node.type != NodeType::BOX && node.type != NodeType::DISC)
            continue;
        const bool held = node.type == NodeType::BOX ? arena.find_box(node) != nullptr
                                                     : arena.find_disc(node) != nullptr;
        if (held && arena.nesting(node) <= NodeArena::MAX_NESTING - ALIGN_NESTING_RESERVE)
            continue;
        node = rule(0, 0, 0);
        clean = false;
    }
    return clean;
}

// floor(a * b / d) for a positive d, as Python computes `a * b // d` on
// integers of any size. A quotient outside a Scaled stops at the end of the range.
#if defined(__SIZEOF_INT128__)
__extension__ typedef __int128 AlignWide;

Scaled align_mul_floor_div(Scaled a, Scaled b, Scaled d, bool& overflowed)
{
    const AlignWide product = static_cast<AlignWide>(a) * static_cast<AlignWide>(b);
    const AlignWide divisor = static_cast<AlignWide>(d);
    AlignWide quotient = product / divisor;
    if (product % divisor != 0 && product < 0)
        --quotient;
    if (quotient > static_cast<AlignWide>(SCALED_MAX)) {
        overflowed = true;
        return SCALED_MAX;
    }
    if (quotient < -static_cast<AlignWide>(SCALED_MAX)) {
        overflowed = true;
        return -SCALED_MAX;
    }
    return static_cast<Scaled>(quotient);
}
#else
// Without a 128-bit integer the product is exact only while it fits 64 bits,
// which it does for every table whose lengths honour MAX_DIMEN; past that the
// quotient is a double's and `overflowed` says so.
Scaled align_mul_floor_div(Scaled a, Scaled b, Scaled d, bool& overflowed)
{
    const Scaled limit = SCALED_MAX;
    const Scaled magnitude_a = a < 0 ? (a == -limit - 1 ? limit : -a) : a;
    const Scaled magnitude_b = b < 0 ? (b == -limit - 1 ? limit : -b) : b;
    if (magnitude_a == 0 || magnitude_b <= limit / magnitude_a)
        return floor_div(a * b, d);
    overflowed = true;
    const double quotient =
        std::floor(static_cast<double>(a) * static_cast<double>(b) / static_cast<double>(d));
    const double end = static_cast<double>(SCALED_LIMIT);
    return quotient >= end ? SCALED_LIMIT
                           : (quotient <= -end ? -SCALED_LIMIT : static_cast<Scaled>(quotient));
}
#endif

Scaled align_sum(const Scaled* values, size_t count, bool& overflowed)
{
    Scaled total = 0;
    for (size_t i = 0; i < count; ++i)
        total = add_checked(total, values[i], overflowed);
    return total;
}

void align_clear(ColumnSolve& out)
{
    out.regime = SolveRegime::UNSCALED;
    out.refused = PolicyRefusal::NONE;
    out.overflowed = false;
    out.available = 0;
    out.total = 0;
    out.share_total = 0.0;
    out.slack = 0;
    out.last = 0;
    out.borrowed = -1;
    out.floored.clear();
    out.give.clear();
    out.columns.clear();
}

// The last column takes what the floors left over, so that the widths sum to
// `available` exactly.
void align_give_remainder_to_last(ColumnSolve& out)
{
    if (out.columns.empty())
        return;
    const Scaled used = align_sum(out.columns.data(), out.columns.size(), out.overflowed);
    const Scaled remainder = sub_checked(out.available, used, out.overflowed);
    out.columns.back() = add_checked(out.columns.back(), remainder, out.overflowed);
}

ColumnAlign align_column_of(const RowInput& row, size_t column)
{
    if (row.aligns == nullptr || column >= row.align_count)
        return ColumnAlign::LEFT;
    return row.aligns[column];
}

// What a list's items occupy from top to bottom at its natural size: the
// reference's `_extent`, summed.
Scaled align_extent_of(const NodeArena& arena, const std::vector<Node>& vlist, bool& overflowed)
{
    Scaled extent = 0;
    for (const Node& node : vlist) {
        if (node.type == NodeType::BOX) {
            if (const Box* const box = arena.find_box(node)) {
                extent = add_checked(extent, box->height, overflowed);
                extent = add_checked(extent, box->depth, overflowed);
            }
        } else if (node.type == NodeType::GLUE || node.type == NodeType::KERN) {
            extent = add_checked(extent, node.a, overflowed);
        }
    }
    return extent;
}

} // namespace

// --- A cell -----------------------------------------------------------------------

Scaled natural_width(const NodeArena& arena, NodeView material, Scaled cell_padding,
                     CharMeasure measure, PackError* error)
{
    // tex.web section 796: u := hpack(..., natural); w := width(u). The
    // padding on both sides is the reference's, and stands where the tabskip
    // glue of section 795 does.
    const PackResult packed = hpack(arena, material, 0, PackMode::ADDITIONAL, measure);
    if (error != nullptr)
        align_note(*error, packed.error);
    const Scaled padding = align_length(cell_padding);
    return add_saturating(packed.box.width, add_saturating(padding, padding));
}

bool fin_col(std::vector<Scaled>& widths, size_t column, Scaled w)
{
    if (column >= MAX_ALIGN_COLUMNS)
        return false;
    // Zero, where tex.web section 801 has null_flag for a column nothing has
    // been recorded in: the reference's `[0] * columns`.
    if (widths.size() <= column)
        widths.resize(column + 1, 0);
    // tex.web section 796: if w > width(cur_align) then width(cur_align) := w.
    if (w > widths[column])
        widths[column] = w;
    return true;
}

// --- The column widths --------------------------------------------------------------

void apportioned(const Scaled* widest, size_t columns, ColumnSolve& out)
{
    out.floored.clear();
    out.give.clear();
    out.total = align_sum(widest, columns, out.overflowed);
    out.columns.assign(widest, widest + columns);
    if (out.total <= 0 || columns == 0) {
        out.regime = SolveRegime::UNSCALED;
        return;
    }
    out.regime = SolveRegime::APPORTIONED;
    for (size_t c = 0; c < columns; ++c)
        out.columns[c] = align_mul_floor_div(widest[c], out.available, out.total, out.overflowed);
    out.floored = out.columns;
    align_give_remainder_to_last(out);
}

void squeezed(const Scaled* widest, const Scaled* narrowest, size_t columns, ColumnSolve& out)
{
    out.floored.clear();
    out.give.clear();
    out.total = 0;
    out.columns.assign(narrowest, narrowest + columns);
    out.slack =
        sub_checked(out.available, align_sum(narrowest, columns, out.overflowed), out.overflowed);
    if (out.slack <= 0) {
        out.regime = SolveRegime::FLOORS;
        return;
    }
    out.give.resize(columns);
    for (size_t c = 0; c < columns; ++c)
        out.give[c] = sub_checked(widest[c], narrowest[c], out.overflowed);
    out.total = align_sum(out.give.data(), columns, out.overflowed);
    if (out.total <= 0) {
        out.regime = SolveRegime::RIGID;
        return;
    }
    out.regime = SolveRegime::SQUEEZED;
    for (size_t c = 0; c < columns; ++c) {
        const Scaled share = align_mul_floor_div(out.slack, out.give[c], out.total, out.overflowed);
        out.columns[c] = add_checked(narrowest[c], share, out.overflowed);
    }
    out.floored = out.columns;
    align_give_remainder_to_last(out);
}

bool shared(const double* fractions, size_t columns, ColumnSolve& out)
{
    // What the reference's `_checked_fractions` refuses when the table is
    // made.
    double total = 0.0;
    for (size_t c = 0; c < columns; ++c) {
        if (!std::isfinite(fractions[c]) || !(fractions[c] > 0.0)) {
            out.refused = PolicyRefusal::NOT_A_SHARE;
            return false;
        }
        total += fractions[c];
    }
    if (!std::isfinite(total) || columns == 0) {
        out.refused = PolicyRefusal::NOT_A_SHARE;
        return false;
    }
    out.floored.clear();
    out.floored.reserve(columns);
    const double available = static_cast<double>(out.available);
    for (size_t c = 0; c < columns; ++c) {
        // In this order: the product first, then the quotient. Python's round
        // is half to even, and raises for a value that is not finite.
        const double width = available * fractions[c] / total;
        if (!(fractions[c] / total > 0.0) || !std::isfinite(width)) {
            out.floored.clear();
            out.refused = PolicyRefusal::NOT_A_SHARE;
            return false;
        }
        const Scaled rounded = round_to_scaled(width);
        out.floored.push_back(rounded < 1 ? 1 : rounded);
    }
    out.give.clear();
    out.total = 0;
    out.slack = 0;
    out.regime = SolveRegime::FRACTIONS;
    out.share_total = total;
    out.columns = out.floored;
    align_give_remainder_to_last(out);
    out.last = out.columns.back();
    out.borrowed = -1;
    if (out.last < 1) {
        // The first of the widest, as Python's max over the indices finds it.
        size_t widest = 0;
        for (size_t c = 1; c < columns; ++c) {
            if (out.columns[c] > out.columns[widest])
                widest = c;
        }
        out.columns[widest] = sub_checked(out.columns[widest],
                                          sub_checked(1, out.last, out.overflowed), out.overflowed);
        out.columns.back() = 1;
        out.borrowed = static_cast<int64_t>(widest);
    }
    return true;
}

bool column_widths(const ColumnPolicy& policy, const Scaled* widest, const Scaled* narrowest,
                   size_t columns, Scaled width, Scaled left_indent, ColumnSolve& out)
{
    align_clear(out);
    const Scaled room = sub_checked(width, align_length(left_indent), out.overflowed);
    out.available = room > UNITY ? room : UNITY;

    const bool widths = policy.kind == WidthPolicy::WIDTHS;
    if ((widths || policy.kind == WidthPolicy::FRACTIONS) && policy.count != 0) {
        if (widths ? policy.widths == nullptr : policy.fractions == nullptr) {
            out.refused = PolicyRefusal::NO_ARRAY;
        } else if (policy.count != columns) {
            out.refused = PolicyRefusal::WRONG_COUNT;
        } else if (widths) {
            for (size_t c = 0; c < columns; ++c) {
                if (policy.widths[c] < 1)
                    out.refused = PolicyRefusal::WIDTH_BELOW_ONE;
            }
            if (out.refused == PolicyRefusal::NONE) {
                out.regime = SolveRegime::GIVEN;
                out.columns.assign(policy.widths, policy.widths + columns);
                return true;
            }
        } else if (shared(policy.fractions, columns, out)) {
            return true;
        }
    }

    // Columns and no widths for them: a table of empty columns.
    if (widest == nullptr && columns != 0) {
        out.columns.assign(columns, 0);
        out.regime = SolveRegime::UNSCALED;
        return true;
    }
    bool overflowed = false;
    if (align_sum(widest, columns, overflowed) <= out.available && !overflowed) {
        apportioned(widest, columns, out);
        return true;
    }
    out.overflowed = out.overflowed || overflowed;
    if (narrowest == nullptr)
        return false;
    squeezed(widest, narrowest, columns, out);
    return true;
}

// --- A row ----------------------------------------------------------------------------

LineBreakParams cell_params(ColumnAlign align, const LineBreakParams& params)
{
    LineBreakParams out = params;
    out.first_fit = true;
    switch (align) {
    case ColumnAlign::RIGHT:
        out.left_skip = RAGGED_RIGHT;
        out.right_skip = GlueSpec();
        out.par_fill_skip = GlueSpec();
        break;
    case ColumnAlign::CENTER:
        out.left_skip = RAGGED_RIGHT;
        out.right_skip = RAGGED_RIGHT;
        out.par_fill_skip = GlueSpec();
        break;
    case ColumnAlign::LEFT:
    default:
        out.left_skip = GlueSpec();
        out.right_skip = RAGGED_RIGHT;
        break;
    }
    return out;
}

void aligned(NodeView material, ColumnAlign align, std::vector<Node>& out)
{
    if (align == ColumnAlign::RIGHT || align == ColumnAlign::CENTER)
        out.push_back(glue(HSS));
    out.insert(out.end(), material.begin(), material.end());
    if (align != ColumnAlign::RIGHT)
        out.push_back(glue(HSS));
}

Scaled fin_row(const NodeArena& arena, std::vector<Node>& vlist, const Node& row, Scaled prev_depth,
               const InterlineSkip& interline)
{
    // tex.web section 799: p := hpack(link(head), natural); append_to_vlist(p).
    // The packing is set_row's here, since the row is set before it is
    // appended.
    return append_to_vlist(arena, vlist, row, prev_depth, interline);
}

Scaled Aligner::longest_word(NodeArena& arena, NodeView material, const LineBreakParams& params,
                             CharMeasure measure, bool keep_lines)
{
    _word_lines.clear();
    if (material.empty())
        return 0;
    _cell.assign(material.begin(), material.end());
    align_settle(arena, _cell, 0);

    LineBreakParams word_params = params;
    word_params.first_fit = true;
    word_params.left_skip = GlueSpec();
    word_params.right_skip = GlueSpec();

    // A measure nothing fits in puts every unbreakable stretch on a line of
    // its own.
    const NodeArena::Mark mark = arena.mark();
    _breaker.break_paragraph(arena, _cell, ParShape::uniform(UNITY), word_params, measure,
                             _paragraph);
    Scaled longest = 0;
    for (const BrokenLine& line : _paragraph.lines) {
        // The line's contents at their natural width, not the point the line
        // was set to.
        const Scaled natural =
            hpack(arena, arena.contents(line.box), 0, PackMode::ADDITIONAL, measure).box.width;
        _word_lines.push_back(natural);
        if (natural > longest)
            longest = natural;
    }
    if (!keep_lines)
        arena.rewind(mark);
    return longest;
}

void Aligner::cell_lines(NodeArena& arena, NodeView material, ColumnAlign align, Scaled width,
                         const TableStyle& style, const LineBreakParams& params,
                         CharMeasure measure, SetRow& out, Node& cell, Scaled& below,
                         uint32_t& index, bool has_cell, CellTrace* trace)
{
    const Scaled padding = align_length(style.cell_padding);
    const Scaled room = sub_saturating(width, add_saturating(padding, padding));
    const Scaled inner = room > UNITY ? room : UNITY;

    // At natural spacing: with the shrink gone a line fits when it is no wider
    // than the cell, which is also the test set_row decides wrapping by.
    _cell.clear();
    unshrinkable(material, _cell);
    if (!align_settle(arena, _cell, 0))
        align_note(out.pack_error, PackError::NODE_NOT_IN_ARENA);
    const LineBreakParams line_params = cell_params(align, params);
    _breaker.break_paragraph(arena, _cell, ParShape::uniform(inner), line_params, measure,
                             _paragraph);
    align_note(out.break_error, _paragraph.error);
    align_note(out.pack_error, _paragraph.pack_error);

    // The indices of the cell's lines, and which of them each node of the
    // material became: a line is the left skip unless the breaker left it
    // out, a run of the list that was broken ending just before the break,
    // the node at the break if that is a kern, a math node or a penalty, and
    // the right skip (src/paragraph's map_shipped has the same walk).
    const size_t map_first = out.shipped.size();
    if (has_cell)
        out.shipped.resize(map_first + material.size(), NOT_SHIPPED);
    bool walked = _paragraph.breaks.size() == _paragraph.lines.size();
    for (const Break& taken : _paragraph.breaks)
        walked = walked && taken.kind != BreakKind::DISC;
    uint64_t budget = 2 * static_cast<uint64_t>(arena.node_count()) + 1024;
    const size_t left = _breaker.left_skip_omitted() ? 0 : 1;
    for (size_t i = 0; i < _paragraph.lines.size(); ++i) {
        const Box* const line = arena.find_box(_paragraph.lines[i].box);
        if (line == nullptr)
            continue;
        const NodeView contents = arena.view(line->contents);
        size_t start = 0;
        size_t body = 0;
        bool placed = false;
        if (walked) {
            const Break taken = _paragraph.breaks[i];
            const size_t tail = taken.kind == BreakKind::KERN || taken.kind == BreakKind::MATH ||
                                        taken.kind == BreakKind::PENALTY
                                    ? 1
                                    : 0;
            if (contents.size() >= left + tail + 1) {
                body = contents.size() - left - 1;
                if (body - tail <= taken.position) {
                    start = taken.position - (body - tail);
                    placed = true;
                }
            }
            walked = placed;
        }
        for (size_t j = 0; j < contents.size(); ++j) {
            uint32_t items = 0;
            if (!align_items_of(arena, contents[j], budget, items))
                walked = false;
            if (placed && has_cell && items != 0 && j >= left && j - left < body &&
                start + (j - left) < material.size())
                out.shipped[map_first + start + (j - left)] = index;
            index += items;
        }
    }
    if (!walked && has_cell) {
        std::fill(out.shipped.begin() + static_cast<std::ptrdiff_t>(map_first), out.shipped.end(),
                  NOT_SHIPPED);
        out.mapped = false;
    }

    if (trace != nullptr) {
        trace->column_width = width;
        trace->inner = inner;
        trace->params = line_params;
        trace->broken = _cell;
        trace->paragraph = _paragraph;
        trace->lines.clear();
    }

    // Every line on the strut and joined by the table's interline glue, so a
    // wrapped cell has the leading the table's rows have.
    _lines.clear();
    Scaled prev_depth = IGNORE_DEPTH;
    Scaled first = 0;
    for (size_t i = 0; i < _paragraph.lines.size(); ++i) {
        const Box* const broken = arena.find_box(_paragraph.lines[i].box);
        if (broken == nullptr)
            continue;
        Box line = *broken;
        line.shift_amount = padding;
        style.strut.apply_to(line);
        if (_lines.empty())
            first = line.height;
        const Node node = arena.add_box(line);
        const size_t before = _lines.size();
        const Scaled prev_depth_in = prev_depth;
        prev_depth = append_to_vlist(arena, _lines, node, prev_depth, style.interline);
        if (trace != nullptr) {
            CellTrace::Line entry;
            entry.prev_depth_in = prev_depth_in;
            entry.prev_depth_out = prev_depth;
            entry.has_glue = _lines.size() - before == 2;
            if (entry.has_glue)
                entry.glue = _lines[before];
            entry.box = line;
            trace->lines.push_back(entry);
        }
    }

    // vpack leaves the reference point on the last baseline. Moving what is
    // below the first line's height into the depth puts it on the first:
    // tex.web section 1087, whose h is the first item's height as here.
    const PackResult stacked = vpack(arena, _lines);
    align_note(out.pack_error, stacked.error);
    bool overflowed = false;
    below = sub_checked(stacked.box.height, first, overflowed);
    Box vtop = stacked.box;
    // The column's width whatever the lines are.
    vtop.width = width;
    vtop.height = first;
    vtop.depth = add_checked(below, stacked.box.depth, overflowed);
    if (overflowed)
        align_note(out.pack_error, PackError::DIMENSION_OVERFLOW);
    cell = arena.add_box(vtop, _lines);
    if (trace != nullptr) {
        trace->stacked = stacked.box;
        trace->vtop = arena.box(cell);
        trace->below = below;
    }
}

bool Aligner::set_row(NodeArena& arena, const RowInput& row, const TableStyle& style,
                      const LineBreakParams& params, CharMeasure measure, SetRow& out,
                      RowTrace* trace)
{
    // Field by field: the two lists keep their capacity from row to row.
    out.box = Node();
    out.unstrutted = Box();
    out.wrapped = false;
    out.lowest = 0;
    out.prev_depth = 0;
    out.item_count = 0;
    out.cell_index_end.clear();
    out.shipped.clear();
    out.mapped = true;
    out.pack_error = PackError::NONE;
    out.break_error = LineBreakError::NONE;
    const size_t columns = row.columns != nullptr ? row.column_count : 0;
    const size_t cells =
        row.cells != nullptr ? (row.cell_count < columns ? row.cell_count : columns) : 0;
    const Scaled padding = align_length(style.cell_padding);
    const Scaled indent = align_length(style.left_indent);

    // The row fits when no cell it has is wider than its column. A column the
    // row has no cell for takes no part.
    const Scaled* natural = row.natural;
    if (natural == nullptr) {
        _row_natural.clear();
        for (size_t c = 0; c < cells; ++c)
            _row_natural.push_back(
                natural_width(arena, row.cells[c], padding, measure, &out.pack_error));
        natural = _row_natural.data();
    }
    bool fits = true;
    for (size_t c = 0; c < cells && fits; ++c)
        fits = natural[c] <= row.columns[c];
    out.wrapped = !fits;

    if (trace != nullptr)
        trace->cells.resize(fits ? 0 : columns);

    _row.clear();
    _row.push_back(kern(indent));
    Scaled lowest = 0;
    uint32_t index = 0;
    uint64_t budget = 2 * static_cast<uint64_t>(arena.node_count()) + 1024;
    for (size_t c = 0; c < columns; ++c) {
        const NodeView material = c < cells ? row.cells[c] : NodeView();
        const ColumnAlign align = align_column_of(row, c);
        if (fits) {
            // tex.web section 810: the entry becomes an hlist as wide as its
            // column, its glue set to reach it. The \hss is what the
            // template's \hfil is there, and the padding is inside the box.
            _cell.clear();
            _cell.push_back(kern(padding));
            const size_t first = _cell.size();
            aligned(material, align, _cell);
            if (!align_settle(arena, _cell, first))
                align_note(out.pack_error, PackError::NODE_NOT_IN_ARENA);
            _cell.push_back(kern(padding));
            const PackResult packed =
                hpack(arena, _cell, row.columns[c], PackMode::EXACTLY, measure);
            align_note(out.pack_error, packed.error);
            _row.push_back(arena.add_box(packed.box, _cell));
            // The cell's indices: its \hss, and its material's in order.
            const size_t lead = align == ColumnAlign::LEFT ? 0 : 1;
            for (size_t k = first; k + 1 < _cell.size(); ++k) {
                uint32_t items = 0;
                if (!align_items_of(arena, _cell[k], budget, items))
                    out.mapped = false;
                const bool own = k >= first + lead && k - first - lead < material.size();
                if (own && c < cells)
                    out.shipped.push_back(items != 0 ? index : NOT_SHIPPED);
                index += items;
            }
        } else {
            Node cell;
            Scaled below = 0;
            cell_lines(arena, material, align, row.columns[c], style, params, measure, out, cell,
                       below, index, c < cells, trace != nullptr ? &trace->cells[c] : nullptr);
            _row.push_back(cell);
            if (below > lowest)
                lowest = below;
        }
        out.cell_index_end.push_back(index);
    }
    out.item_count = index;

    // tex.web section 807 makes the row an hlist as wide as the prototype
    // row; here it is the natural width of its cells, which are already as
    // wide as their columns.
    const PackResult packed = hpack(arena, _row, 0, PackMode::ADDITIONAL, measure);
    align_note(out.pack_error, packed.error);
    out.unstrutted = packed.box;
    out.unstrutted.contents = arena.add_nodes(_row);

    // One strut on the row and none on a cell: a cell is a box of the row's
    // list, so flooring the row floors them all.
    Box strutted = out.unstrutted;
    style.strut.apply_to(strutted);
    out.box = arena.add_box(strutted);
    out.lowest = lowest;
    // After a row of \vtops the interline glue of tex.web section 679 would
    // read a depth of every line below the first. It is handed the depth of
    // the lowest line instead: the row's depth as it stands, strut included,
    // less how far down that line's baseline is.
    out.prev_depth = fits ? strutted.depth : sub_saturating(strutted.depth, lowest);
    return out.ok();
}

Node Aligner::head_rule(NodeArena& arena, const Scaled* columns, size_t column_count,
                        const TableStyle& style, PackError* error)
{
    bool overflowed = false;
    const Scaled across = columns != nullptr ? align_sum(columns, column_count, overflowed) : 0;
    _row.clear();
    _row.push_back(kern(align_length(style.left_indent)));
    _row.push_back(rule(across, align_length(style.rule_thickness), 0));
    const PackResult packed = hpack(arena, _row, 0, PackMode::ADDITIONAL, CharMeasure());
    if (error != nullptr) {
        align_note(*error, packed.error);
        if (overflowed)
            align_note(*error, PackError::DIMENSION_OVERFLOW);
    }
    return arena.add_box(packed.box, _row);
}

// --- A table ----------------------------------------------------------------------------

bool Aligner::fin_align(NodeArena& arena, const TableInput& table, Scaled width,
                        const LineBreakParams& params, CharMeasure measure, Aligned& out)
{
    out.natural.clear();
    out.widest.clear();
    out.narrowest.clear();
    align_clear(out.solve);
    out.boxes.clear();
    out.vlist.clear();
    out.extent = 0;
    out.pack_error = PackError::NONE;
    out.break_error = LineBreakError::NONE;

    const size_t rows = table.row_first != nullptr ? table.rows : 0;
    const Scaled padding = align_length(table.style.cell_padding);
    const auto row_begin = [&](size_t r) { return static_cast<size_t>(table.row_first[r]); };
    // Offsets that run backwards are an empty row, as is any row of a table
    // with no cells; a row longer than a table may be wide is cut there.
    const auto row_length = [&](size_t r) {
        if (table.cells == nullptr)
            return size_t(0);
        const size_t first = row_begin(r);
        const size_t end = row_begin(r + 1);
        const size_t count = end > first ? end - first : 0;
        return count < MAX_ALIGN_COLUMNS ? count : MAX_ALIGN_COLUMNS;
    };

    if (rows != 0) {
        // Every entry's natural width, and the widest of each column: tex.web
        // section 796, entry by entry.
        size_t columns = 0;
        for (size_t r = 0; r < rows; ++r) {
            const size_t count = row_length(r);
            if (count > columns)
                columns = count;
        }
        out.widest.assign(columns, 0);
        size_t total_cells = 0;
        for (size_t r = 0; r < rows; ++r)
            total_cells += row_length(r);
        out.natural.reserve(total_cells);
        for (size_t r = 0; r < rows; ++r) {
            const NodeView* const cells = table.cells + row_begin(r);
            const size_t count = row_length(r);
            for (size_t c = 0; c < count; ++c) {
                const Scaled w = natural_width(arena, cells[c], padding, measure, &out.pack_error);
                out.natural.push_back(w);
                fin_col(out.widest, c, w);
            }
        }

        // The final widths: tex.web section 801's place in fin_align. The
        // longest words are measured only when the table does not fit.
        if (!column_widths(table.policy, out.widest.data(), nullptr, columns, width,
                           table.style.left_indent, out.solve)) {
            out.narrowest.assign(columns, 0);
            for (size_t r = 0; r < rows; ++r) {
                const NodeView* const cells = table.cells + row_begin(r);
                const size_t count = row_length(r);
                for (size_t c = 0; c < count; ++c) {
                    const Scaled word = longest_word(arena, cells[c], params, measure);
                    align_note(out.pack_error, _paragraph.pack_error);
                    fin_col(out.narrowest, c,
                            add_saturating(word, add_saturating(padding, padding)));
                }
            }
            column_widths(table.policy, out.widest.data(), out.narrowest.data(), columns, width,
                          table.style.left_indent, out.solve);
        }
        if (out.solve.overflowed)
            align_note(out.pack_error, PackError::DIMENSION_OVERFLOW);

        // Every row set to the widths (tex.web sections 805 to 810) and put in
        // the list (section 799), each append handed the prev_depth the box
        // before it carried.
        RowInput row;
        row.columns = out.solve.columns.data();
        row.column_count = out.solve.columns.size();
        row.aligns = table.aligns;
        row.align_count = table.aligns != nullptr ? table.align_count : 0;
        SetRow set;
        Scaled prev_depth = IGNORE_DEPTH;
        size_t natural_at = 0;
        // `carried` is null for a box that carries its own depth.
        const auto append = [&](const Node& box, int64_t number, bool wrapped,
                                const Scaled* carried) {
            TableBox entry;
            entry.box = box;
            entry.row = number;
            entry.wrapped = wrapped;
            entry.prev_depth_in = prev_depth;
            const size_t before = out.vlist.size();
            entry.prev_depth_out =
                fin_row(arena, out.vlist, box, prev_depth, table.style.interline);
            entry.has_glue = out.vlist.size() - before == 2;
            entry.vlist_index = static_cast<uint32_t>(out.vlist.size() - 1);
            entry.prev_depth_carried = carried != nullptr ? *carried : entry.prev_depth_out;
            prev_depth = entry.prev_depth_carried;
            out.boxes.push_back(entry);
        };
        out.boxes.reserve(rows + 1);
        for (size_t r = 0; r < rows; ++r) {
            row.cells = table.cells + row_begin(r);
            row.cell_count = row_length(r);
            row.natural = out.natural.data() + natural_at;
            natural_at += row.cell_count;
            set_row(arena, row, table.style, params, measure, set);
            align_note(out.pack_error, set.pack_error);
            align_note(out.break_error, set.break_error);
            append(set.box, static_cast<int64_t>(r), set.wrapped, &set.prev_depth);
            if (r == 0 && table.has_header && table.style.rule_thickness != 0) {
                const Node ruled =
                    head_rule(arena, row.columns, row.column_count, table.style, &out.pack_error);
                append(ruled, -1, false, nullptr);
            }
        }
    }

    out.packed = vpack(arena, out.vlist);
    align_note(out.pack_error, out.packed.error);
    out.box = arena.add_box(out.packed.box, out.vlist);
    bool overflowed = false;
    out.extent = align_extent_of(arena, out.vlist, overflowed);
    if (overflowed)
        align_note(out.pack_error, PackError::DIMENSION_OVERFLOW);
    return out.ok();
}

} // namespace ImTypeset::detail
