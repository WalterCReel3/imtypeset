#include "columns.h"

#include <algorithm>

namespace ImTypeset::detail
{

namespace
{

// Past every height a document has: the limit of a column nothing follows,
// and the top of one nothing was left for.
constexpr Scaled NO_LIMIT = Scaled(1) << 62;

// A working list is given up to its present column's unit once that many
// nodes lie before it.
constexpr size_t KEEP_BEFORE = 4096;

Scaled extent_of(const Node& node)
{
    if (node.is(NodeType::RULE))
        return add_saturating(node.height(), node.depth());
    if (node.is(NodeType::GLUE) || node.is(NodeType::KERN))
        return node.width();
    return 0;
}

} // namespace

void DocumentColumns::set_rules(const ColumnRules& rules)
{
    ColumnRules checked = rules;
    if (checked.height < 1)
        checked.height = 1;
    checked.height = clamp_dimen(checked.height);
    if (checked.balance > MAX_BALANCED_COLUMNS)
        checked.balance = MAX_BALANCED_COLUMNS;
    checked.split_top_skip.width = clamp_dimen(checked.split_top_skip.width);
    if (checked == _rules)
        return;
    _rules = checked;
    drop_from(0);
    _known_count = 0;
    _known_reach = 0;
}

void DocumentColumns::drop_from(size_t k)
{
    if (_finished) {
        _known_count = _columns.size();
        _known_reach = _reach;
    }
    if (k < _columns.size())
        _columns.resize(k);
    _finished = false;
    _open = false;
    _list.clear();
    _tops.clear();
    _units.clear();
    _unit_first.clear();
    _start = NONE;
    _balanced_at = 0;
    _height = _rules.balance != 0 ? 0 : _rules.height;
    _reach = 0;
    for (const DocumentColumn& column : _columns) {
        if (column.overfull)
            _reach = std::max(_reach, column.used);
    }
}

void DocumentColumns::bring_up_to_date(Document& document, DocumentComposer& composer)
{
    document.prepare_if_needed(composer);
    const size_t changed = document.take_changed();
    if (changed == Document::UNCHANGED)
        return;
    if (_rules.balance != 0) {
        drop_from(0);
        return;
    }
    // The columns whose scans read the unit that changed, or any after it.
    const auto first =
        std::partition_point(_columns.begin(), _columns.end(), [&](const DocumentColumn& column) {
            return column.read_end <= changed;
        });
    drop_from(static_cast<size_t>(first - _columns.begin()));
}

size_t DocumentColumns::unit_first(uint32_t unit) const
{
    const auto found = std::lower_bound(
        _unit_first.begin(), _unit_first.end(), unit,
        [](const UnitFirst& entry, uint32_t wanted) { return entry.unit < wanted; });
    return found != _unit_first.end() && found->unit == unit ? found->first : _list.size();
}

// Reads the next unit's part of the list onto the working copy, laying the
// unit out first: its dimensions are then the ones it is shown with.
void DocumentColumns::load_unit(Document& document, DocumentComposer& composer)
{
    const uint32_t u = _next_unit;
    document.layout_unit(composer, u);
    _unit_nodes.clear();
    document.unit_vlist(u, _unit_nodes);
    _unit_first.push_back(UnitFirst{u, _list.size()});
    Scaled pen = sub_saturating(document.unit_top(u), document.unit(u).gap);
    for (const Node& node : _unit_nodes) {
        _list.push_back(node);
        _tops.push_back(pen);
        _units.push_back(u);
        pen = add_saturating(pen, extent_of(node));
    }
    ++_next_unit;
    ++units_read;
}

// The first box at or after `from` in the working copy, reading further units
// until there is one; NONE when the document ends first. This is
// prune_page_top's walk (section 968): what it passes over is glue and kerns.
size_t DocumentColumns::next_box(Document& document, DocumentComposer& composer, size_t from)
{
    size_t i = from;
    for (;;) {
        for (; i < _list.size(); ++i) {
            if (_list[i].is(NodeType::RULE))
                return i;
        }
        if (_next_unit >= document.unit_count())
            return NONE;
        load_unit(document, composer);
    }
}

// Section 969's glue for the box at `box` of the working copy.
Node DocumentColumns::head_glue(size_t box) const
{
    const Node one[1] = {_list[box]};
    return prune_page_top(_no_boxes, NodeView(one, 1), _rules.split_top_skip).glue;
}

// Makes the working copy hold the list from where the next column starts.
void DocumentColumns::open(Document& document, DocumentComposer& composer)
{
    _list.clear();
    _tops.clear();
    _units.clear();
    _unit_first.clear();
    _open = true;
    if (_columns.empty()) {
        _next_unit = 0;
        _list_first_node = 0;
        _start = next_box(document, composer, 0);
        return;
    }
    const DocumentColumn& last = _columns.back();
    _start = NONE;
    if (last.next_unit >= document.unit_count())
        return;
    _next_unit = last.next_unit;
    _list_first_node = last.next_unit_node;
    load_unit(document, composer);
    // The box the column before found next: the first at or below its limit.
    for (size_t i = 0; i < _list.size(); ++i) {
        if (_list[i].is(NodeType::RULE) && _tops[i] >= last.limit) {
            _start = i;
            return;
        }
    }
    _start = next_box(document, composer, _list.size());
}

// One vsplit (section 977) off the front of what is left: the next column, at
// `height`. False when nothing is left.
bool DocumentColumns::cut_next(Document& document, DocumentComposer& composer, Scaled height)
{
    if (!_open)
        open(document, composer);
    if (_start == NONE) {
        _finished = true;
        return false;
    }
    const Node head = head_glue(_start);
    _scan.begin(height, _rules.split_max_depth);
    _scan.head(head);
    for (;;) {
        const bool complete = _next_unit >= document.unit_count();
        if (_scan.scan(_no_boxes, NodeView(_list.data() + _start, _list.size() - _start), complete))
            break;
        load_unit(document, composer);
    }

    // best_place counts the glue as node 0: the cut falls at this node of the
    // working copy, or at its end.
    const size_t place = std::max<size_t>(_scan.best_place, 1);
    const size_t cut = std::min(_start + place - 1, _list.size());
    DocumentColumn column;
    column.first_unit = _units[_start];
    column.first_node = _list_first_node + _start;
    column.place = _scan.best_place;
    column.top = _tops[_start];
    column.bottom = column.top;
    for (size_t i = cut; i-- > _start;) {
        if (_list[i].is(NodeType::RULE)) {
            column.bottom = add_saturating(_tops[i], extent_of(_list[i]));
            break;
        }
    }
    column.head = head.width();
    column.used = _scan.best_height_plus_depth;
    column.least_cost = _scan.least_cost;
    column.stop = _scan.stop;
    column.overfull = _scan.least_cost == AWFUL_BAD;

    const size_t next = next_box(document, composer, cut);
    if (next != NONE) {
        column.limit = _tops[next];
        column.next_unit = _units[next];
        column.next_unit_node = _list_first_node + unit_first(_units[next]);
    } else {
        column.limit = NO_LIMIT;
        column.next_unit = static_cast<uint32_t>(document.unit_count());
        column.next_unit_node = _list_first_node + _list.size();
    }
    // A scan that came to the end of the document read what is not there
    // yet, too: a unit added after the last one changes it.
    column.read_end = _next_unit + (_next_unit >= document.unit_count() ? 1u : 0u);
    _columns.push_back(column);
    if (column.overfull)
        _reach = std::max(_reach, column.used);
    ++columns_cut;
    _start = next;
    if (next == NONE) {
        _finished = true;
        return true;
    }

    // What lies before the unit the next column starts in is not read again.
    if (_rules.balance == 0) {
        const size_t keep = unit_first(_units[_start]);
        if (keep > KEEP_BEFORE && keep <= _start) {
            const auto drop = static_cast<std::ptrdiff_t>(keep);
            _list.erase(_list.begin(), _list.begin() + drop);
            _tops.erase(_tops.begin(), _tops.begin() + drop);
            _units.erase(_units.begin(), _units.begin() + drop);
            const auto kept =
                std::partition_point(_unit_first.begin(), _unit_first.end(),
                                     [&](const UnitFirst& entry) { return entry.first < keep; });
            _unit_first.erase(_unit_first.begin(), kept);
            for (UnitFirst& entry : _unit_first)
                entry.first -= keep;
            _list_first_node += keep;
            _start -= keep;
        }
    }
    return true;
}

// Whether `count` columns of `height` hold the working copy from the box at
// `first` on: the cuts of cut_next, made and not kept.
bool DocumentColumns::holds(size_t first, Scaled height, uint32_t count)
{
    size_t start = first;
    VertBreak scan;
    for (uint32_t c = 0; c < count; ++c) {
        if (start >= _list.size())
            return true;
        scan.begin(height, _rules.split_max_depth);
        scan.head(head_glue(start));
        scan.scan(_no_boxes, NodeView(_list.data() + start, _list.size() - start), true);
        size_t next = start + std::max<size_t>(scan.best_place, 1) - 1;
        while (next < _list.size() && !_list[next].is(NodeType::RULE))
            ++next;
        start = next;
    }
    return start >= _list.size();
}

// BALANCE: the least height at which the rule's count of columns holds the
// document, found by bisection, and the columns cut at it. More height never
// holds less of a list that cannot stretch or shrink -- each column takes the
// last breakpoint that fits, which only moves later -- so the heights that
// hold the document are all those from some least one up.
void DocumentColumns::balance(Document& document, DocumentComposer& composer)
{
    drop_from(0);
    open(document, composer);
    while (_next_unit < document.unit_count())
        load_unit(document, composer);
    const uint32_t count = std::max<uint32_t>(_rules.balance, 1);
    const size_t first = _start;
    if (first != NONE) {
        // Everything in one column: its glue, and the list from its first box.
        Scaled all = _rules.split_top_skip.width > 0 ? _rules.split_top_skip.width : 0;
        for (size_t i = first; i < _list.size(); ++i)
            all = add_saturating(all, extent_of(_list[i]));
        Scaled low = 1;
        Scaled high = std::max<Scaled>(all, 1);
        while (low < high) {
            const Scaled middle = low + (high - low) / 2;
            ++heights_tried;
            if (holds(first, middle, count))
                high = middle;
            else
                low = middle + 1;
        }
        _balanced_at = low;
        // Past `count` only if the list was one this cannot balance (stretch
        // in it): nothing is left out for that.
        while (!_finished && cut_next(document, composer, low)) {
        }
    }
    // The page is as tall as its tallest column, so no column holds anything
    // taller than a column is. The height the cuts were made at says nothing
    // of that: where the document has no more pieces than columns it is one
    // scaled point, at which every column's one legal cut is past its height.
    for (DocumentColumn& column : _columns) {
        _height = std::max(_height, column.used);
        column.overfull = false;
    }
    _reach = 0;
    while (_columns.size() < count) {
        DocumentColumn none;
        none.empty = true;
        none.first_unit = static_cast<uint32_t>(document.unit_count());
        none.next_unit = none.first_unit;
        none.read_end = none.first_unit + 1;
        none.top = NO_LIMIT;
        none.bottom = NO_LIMIT;
        none.limit = NO_LIMIT;
        none.least_cost = EJECT_PENALTY;
        _columns.push_back(none);
    }
    _finished = true;
}

bool DocumentColumns::cut_through(Document& document, DocumentComposer& composer, size_t k)
{
    bring_up_to_date(document, composer);
    if (_rules.balance != 0) {
        if (!_finished)
            balance(document, composer);
        return k < _columns.size();
    }
    while (_columns.size() <= k && !_finished) {
        if (!cut_next(document, composer, _rules.height))
            break;
    }
    return k < _columns.size();
}

void DocumentColumns::cut_past_unit(Document& document, DocumentComposer& composer, size_t unit)
{
    bring_up_to_date(document, composer);
    if (_rules.balance != 0) {
        if (!_finished)
            balance(document, composer);
        return;
    }
    while (!_finished && (_columns.empty() || _columns.back().next_unit <= unit)) {
        if (!cut_next(document, composer, _rules.height))
            break;
    }
}

void DocumentColumns::cut_all(Document& document, DocumentComposer& composer)
{
    bring_up_to_date(document, composer);
    if (_rules.balance != 0) {
        if (!_finished)
            balance(document, composer);
        return;
    }
    while (!_finished) {
        if (!cut_next(document, composer, _rules.height))
            break;
    }
}

size_t DocumentColumns::column_at(Document& document, DocumentComposer& composer, Scaled y)
{
    cut_through(document, composer, 0);
    while (!_finished && !_columns.empty() && y >= _columns.back().limit) {
        if (!cut_through(document, composer, _columns.size()))
            break;
    }
    const auto after =
        std::partition_point(_columns.begin(), _columns.end(),
                             [&](const DocumentColumn& column) { return column.top <= y; });
    return after == _columns.begin() ? 0 : static_cast<size_t>(after - _columns.begin()) - 1;
}

size_t DocumentColumns::estimated_count(const Document& document) const
{
    if (_finished)
        return _columns.size();
    if (_rules.balance == 0 && _known_count > _columns.size())
        return _known_count;
    const Scaled cut_to = _columns.empty() ? 0 : _columns.back().limit;
    const Scaled below = sub_saturating(document.height(), cut_to);
    const Scaled height = std::max<Scaled>(_rules.height, 1);
    const Scaled more = below > 0 ? ceil_div(below, height) : 1;
    const size_t bound = MAX_DOCUMENT_BLOCKS * 4;
    return _columns.size() + static_cast<size_t>(std::min<Scaled>(std::max<Scaled>(more, 1),
                                                                  static_cast<Scaled>(bound)));
}

const DocumentColumn& DocumentColumns::column(size_t k) const
{
    static const DocumentColumn none;
    if (!IMTYPESET_CONTRACT(k < _columns.size(), "DocumentColumns::column: no such column"))
        return none;
    return _columns[k];
}

bool DocumentColumns::flatten(const Document& document, size_t k, std::vector<DrawCmd>& out) const
{
    if (k >= _columns.size())
        return false;
    const DocumentColumn& column = _columns[k];
    if (column.empty)
        return true;
    const size_t base = out.size();
    // From the uncut document to the column: its first box's top goes to the
    // foot of the glue.
    const Scaled shift = sub_saturating(column.head, column.top);

    // The quotes open round the block being written, as Document::flatten
    // keeps them. A quote is one box and is never cut.
    struct Open {
        size_t slot = 0;
        size_t end_block = 0;
    };
    Open open_quotes[MAX_QUOTE_DEPTH + 1];
    size_t depth = 0;
    uint64_t index = 0;
    std::vector<Scaled> tops;
    for (size_t u = column.first_unit; u < document.unit_count(); ++u) {
        const DocumentUnit& unit = document.unit(u);
        const Scaled top = document.unit_top(u);
        if (top >= column.limit)
            break;
        size_t bar = unit.first_bar;
        const size_t bar_end = bar + unit.bar_count;
        const size_t first = unit.first_block;
        const size_t end = first + unit.block_count;
        for (size_t b = first; b <= end; ++b) {
            while (depth > 0 && open_quotes[depth - 1].end_block <= b) {
                out[open_quotes[depth - 1].slot].end = static_cast<uint32_t>(index);
                --depth;
            }
            if (b == end)
                break;
            for (; bar < bar_end && document.bar(bar).first_block == b && depth <= MAX_QUOTE_DEPTH;
                 ++bar) {
                const Bar& found = document.bar(bar);
                open_quotes[depth].slot = out.size();
                open_quotes[depth].end_block =
                    static_cast<size_t>(found.first_block) + found.block_count;
                ++depth;
                const Scaled box_top = add_saturating(add_saturating(top, found.box_top), shift);
                out.push_back(DrawCmd::line(
                    0, add_saturating(box_top, found.box_height), found.box_width, found.box_height,
                    found.box_depth, static_cast<uint32_t>(index), static_cast<uint32_t>(index)));
                if (found.extent > 0 && found.thickness > 0)
                    out.push_back(DrawCmd::rule_rect(
                        found.x, add_saturating(add_saturating(top, found.top), shift),
                        found.thickness, found.extent));
            }

            const BlockLayout& layout = document.layout(b);
            const Scaled block_top = add_saturating(top, document.top_in_unit(b));
            size_t first_cmd = 0;
            size_t end_cmd = layout.cmds.size();
            uint32_t first_index = 0;
            uint32_t end_index = layout.item_count;
            const bool whole =
                unit.barred || (block_top >= column.top &&
                                add_saturating(block_top, layout.extent) <= column.limit);
            if (!whole) {
                // The block's boxes that are this column's, found among its
                // commands: each box is a LINE, at the top the list gives it
                // and of its height and depth, and the boxes come in order.
                document.block_line_tops(b, tops);
                bool any = false;
                size_t at = 0;
                for (size_t line = 0; line < tops.size() && line < layout.lines.size(); ++line) {
                    const LineDims& dims = layout.lines[line];
                    while (at < layout.cmds.size()) {
                        const DrawCmd& cmd = layout.cmds[at];
                        if (cmd.is(CmdType::LINE) && cmd.y - cmd.height == tops[line] &&
                            cmd.height == dims.height && cmd.depth == dims.depth)
                            break;
                        ++at;
                    }
                    if (at >= layout.cmds.size()) {
                        out.resize(base);
                        return false;
                    }
                    const Scaled line_top = add_saturating(block_top, tops[line]);
                    if (line_top >= column.limit) {
                        end_cmd = at;
                        end_index = layout.cmds[at].start();
                        break;
                    }
                    if (line_top >= column.top && !any) {
                        any = true;
                        first_cmd = at;
                        first_index = layout.cmds[at].start();
                    }
                    ++at;
                }
                if (!any)
                    continue;
            }
            for (size_t c = first_cmd; c < end_cmd; ++c) {
                DrawCmd cmd = layout.cmds[c];
                cmd.y = add_saturating(add_saturating(cmd.y, block_top), shift);
                if (!cmd.is(CmdType::RULE_RECT)) {
                    cmd.index = cmd.index - first_index + static_cast<uint32_t>(index);
                    if (cmd.is(CmdType::LINE))
                        cmd.end = cmd.end - first_index + static_cast<uint32_t>(index);
                }
                out.push_back(cmd);
            }
            index += end_index - first_index;
            if (index > 0xFFFFFFFFull) {
                out.resize(base);
                return false;
            }
        }
    }
    return true;
}

} // namespace ImTypeset::detail
