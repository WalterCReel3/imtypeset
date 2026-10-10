#include "paragraph.h"

#include <utility>

namespace ImTypeset::detail
{

namespace
{

void note(PackError* error, PackError found)
{
    if (error != nullptr && *error == PackError::NONE)
        *error = found;
}

// How many indices ship-out's hlist_out gives the nodes of a horizontal list:
// one for a character, one for a glue, one for an atom box, and for any other
// box whatever its contents take. `budget` bounds the walk, since a box may be
// named by more than one node; false once it is spent.
bool pointable_in_hlist(const NodeArena& arena, NodeView nodes, uint64_t& budget, uint32_t& count);

// The same for a vertical list, where glue is not pointable.
bool pointable_in_vlist(const NodeArena& arena, NodeView nodes, uint64_t& budget, uint32_t& count)
{
    for (const Node& node : nodes) {
        if (budget == 0)
            return false;
        --budget;
        if (node.type != NodeType::BOX)
            continue;
        const Box& inner = arena.box(node);
        const NodeView contents = arena.view(inner.contents);
        const bool walked = inner.kind == BoxKind::VLIST
                                ? pointable_in_vlist(arena, contents, budget, count)
                                : pointable_in_hlist(arena, contents, budget, count);
        if (!walked)
            return false;
    }
    return true;
}

bool pointable_in_box(const NodeArena& arena, const Node& box, uint64_t& budget, uint32_t& count)
{
    const Box& inner = arena.box(box);
    if (is_atom(arena, inner)) {
        ++count;
        return true;
    }
    const NodeView contents = arena.view(inner.contents);
    return inner.kind == BoxKind::VLIST ? pointable_in_vlist(arena, contents, budget, count)
                                        : pointable_in_hlist(arena, contents, budget, count);
}

bool pointable_in_hlist(const NodeArena& arena, NodeView nodes, uint64_t& budget, uint32_t& count)
{
    for (const Node& node : nodes) {
        if (budget == 0)
            return false;
        --budget;
        switch (node.type) {
        case NodeType::CHAR:
        case NodeType::GLUE:
            ++count;
            break;
        case NodeType::BOX:
            if (!pointable_in_box(arena, node, budget, count))
                return false;
            break;
        default:
            break;
        }
    }
    return true;
}

} // namespace

LineBreakParams prose_breaking()
{
    LineBreakParams params;
    params.tolerance = 1000;
    params.emergency_stretch = 20 * UNITY;
    return params;
}

LineBreakParams skips(Align align, const LineBreakParams& params)
{
    LineBreakParams out = params;
    switch (align) {
    case Align::JUSTIFY:
        out.left_skip = GlueSpec();
        out.right_skip = GlueSpec();
        break;
    case Align::LEFT:
        out.left_skip = GlueSpec();
        out.right_skip = RAGGED_RIGHT;
        break;
    case Align::RIGHT:
        out.left_skip = RAGGED_RIGHT;
        out.right_skip = GlueSpec();
        out.par_fill_skip = GlueSpec();
        break;
    case Align::CENTER:
        out.left_skip = RAGGED_RIGHT;
        out.right_skip = RAGGED_RIGHT;
        out.par_fill_skip = GlueSpec();
        break;
    }
    return out;
}

BlockShape shape(Scaled width, Scaled left_indent, Scaled first_line_indent)
{
    // Held to the core's invariant, so that the differences below cannot
    // overflow whatever a caller hands in.
    width = clamp_dimen(width);
    left_indent = clamp_dimen(left_indent);
    first_line_indent = clamp_dimen(first_line_indent);

    BlockShape out;
    const Scaled available = width - left_indent > UNITY ? width - left_indent : UNITY;
    if (first_line_indent == 0) {
        out.lines[0] = LineSpec{available, left_indent};
        out.count = 1;
        return out;
    }
    const Scaled first = available - first_line_indent;
    out.lines[0] = LineSpec{first > UNITY ? first : UNITY, left_indent + first_line_indent};
    out.lines[1] = LineSpec{available, left_indent};
    out.count = 2;
    return out;
}

void unshrinkable(NodeView material, std::vector<Node>& out)
{
    out.reserve(out.size() + material.size());
    for (Node node : material) {
        if (node.type == NodeType::GLUE && node.c != 0) {
            node.c = 0;
            node.shrink_order = GlueOrder::NORMAL;
        }
        out.push_back(node);
    }
}

void unskip(Run& run)
{
    std::vector<Node>& nodes = run.nodes;
    while (!nodes.empty() && nodes.back().type == NodeType::GLUE) {
        nodes.pop_back();
        if (!nodes.empty() && nodes.back().type == NodeType::PENALTY &&
            forbids_break(nodes.back().penalty()))
            nodes.pop_back();
    }
    // One offset per node, while the caller has kept the two in step.
    if (run.source_offsets != nullptr && run.source_offsets->size() > nodes.size())
        run.source_offsets->resize(nodes.size());
}

void hard_break(Run& run, Align align, uint32_t source_offset)
{
    unskip(run);
    const size_t before = run.nodes.size();
    fill_and_break(align, run.nodes);
    if (run.source_offsets != nullptr) {
        run.source_offsets->insert(run.source_offsets->end(), run.nodes.size() - before,
                                   run.source_base + source_offset);
    }
}

void TextBlock::clear()
{
    run.nodes.clear();
    run.space_factor = NEUTRAL_SPACE_FACTOR;
    marker.nodes.clear();
    marker.space_factor = NEUTRAL_SPACE_FACTOR;
}

bool set_marker(TextBlock& block, std::string_view text, FontSpec font,
                const Composition& composition, MetricsResolver resolve, std::string* why)
{
    block.marker.nodes.clear();
    block.marker.space_factor = NEUTRAL_SPACE_FACTOR;
    return append_text(block.marker, text, font, false, composition, resolve, why);
}

Node hang_marker(NodeArena& arena, const Node& line, NodeView marker, Scaled indent,
                 Scaled marker_gap, CharMeasure measure, std::vector<Node>& scratch,
                 PackError* error)
{
    scratch.clear();
    scratch.reserve(marker.size() + 2);
    scratch.push_back(glue(RAGGED_RIGHT));
    scratch.insert(scratch.end(), marker.begin(), marker.end());
    scratch.push_back(kern(marker_gap));
    const PackResult gutter = hpack(arena, scratch, indent, PackMode::EXACTLY, measure);
    note(error, gutter.error);
    const Node marker_box = arena.add_box(gutter.box, scratch);

    // The marker box now supplies the offset the line's shift_amount did. The
    // copy shares the line's contents.
    Box unshifted = arena.box(line);
    unshifted.shift_amount = 0;
    const Node pair[2] = {marker_box, arena.add_box(unshifted)};
    const PackResult both = hpack(arena, NodeView(pair, 2), 0, PackMode::ADDITIONAL, measure);
    note(error, both.error);
    return arena.add_box(both.box, NodeView(pair, 2));
}

bool ParagraphComposer::set_lines(NodeArena& arena, const TextBlock& block, const BlockStyle& style,
                                  Scaled width, const LineBreakParams& params, CharMeasure measure,
                                  ParagraphLayout& out)
{
    out.hung = Node();
    out.lines.clear();
    out.vlist.clear();
    out.packed = PackResult();
    out.box = Node();
    out.shipped.clear();
    out.mapped = false;
    out.item_count = 0;
    out.marker_items = 0;
    out.originals.clear();
    out.hard_breaks.clear();
    out.break_error = LineBreakError::NONE;
    out.pack_error = PackError::NONE;

    // The reference's `_text_boxes`.
    _line_params = skips(style.align, params);
    const bool first_fit = style.align != Align::JUSTIFY && !style.ragged_total_fit;
    if (first_fit)
        _line_params.first_fit = true;

    // tex.web section 1091: an indented paragraph begins with an empty box as
    // wide as the indent.
    const size_t offset = style.first_line_indent != 0 ? 1 : 0;
    if (offset != 0 || first_fit) {
        _material.clear();
        _material.reserve(block.run.nodes.size() + offset);
        if (offset != 0) {
            Box indent;
            indent.kind = BoxKind::HLIST;
            indent.width = style.first_line_indent;
            _material.push_back(arena.add_box(indent));
        }
        if (first_fit)
            unshrinkable(block.run.nodes, _material);
        else
            _material.insert(_material.end(), block.run.nodes.begin(), block.run.nodes.end());
        _broken = _material;
    } else {
        _broken = block.run.nodes;
    }

    const BlockShape block_shape =
        shape(width, style.left_indent,
              style.first_line == FirstLineIndent::BOX_AND_SHAPE ? style.first_line_indent : 0);
    _breaker.break_paragraph(arena, _broken, block_shape.par_shape(), _line_params, measure,
                             out.broken);
    out.break_error = out.broken.error;
    out.pack_error = out.broken.pack_error;

    // The reference's `_block_boxes`: the marker on the first line, then every
    // line on the strut. A line the strut does not change is the breaker's own
    // box; one it does is a second header over the same contents.
    out.lines.reserve(out.broken.lines.size());
    for (size_t i = 0; i < out.broken.lines.size(); ++i) {
        Node line = out.broken.lines[i].box;
        if (i == 0 && !block.marker.nodes.empty()) {
            line = hang_marker(arena, line, block.marker.nodes, style.left_indent, style.marker_gap,
                               measure, _scratch, &out.pack_error);
            out.hung = line;
        }
        Box strutted = arena.box(line);
        if (strutted.height < style.strut.height || strutted.depth < style.strut.depth) {
            style.strut.apply_to(strutted);
            line = arena.add_box(strutted);
        }
        out.lines.push_back(line);
    }

    map_shipped(arena, block, offset, out);
    return out.ok();
}

bool ParagraphComposer::compose(NodeArena& arena, const TextBlock& block, const BlockStyle& style,
                                Scaled width, const LineBreakParams& params, CharMeasure measure,
                                ParagraphLayout& out)
{
    set_lines(arena, block, style, width, params, measure, out);

    // The reference's `_stacked` for one block, and `compose_blocks`' vpack.
    Scaled prev_depth = IGNORE_DEPTH;
    for (const Node& line : out.lines)
        prev_depth = append_to_vlist(arena, out.vlist, line, prev_depth, style.interline);
    out.packed = vpack(arena, out.vlist);
    note(&out.pack_error, out.packed.error);
    out.box = arena.add_box(out.packed.box, out.vlist);
    out.packed.box.contents = arena.box(out.box).contents;
    return out.ok();
}

// A line the breaker built is, in order: the left skip unless the breaker
// left it out (LineBreaker::left_skip_omitted, which is asked and not restated
// here: the test depends on the rule set);
// a contiguous range of the prepared list ending just before the break; the
// node at the break if that is a kern, a math node or a penalty; the right
// skip. (A break at a discretionary puts its pre-break text there and its
// post-break text on the next line, and then the line is not a range of the
// list: no map is made.) The prepared list is the broken list with a penalty
// and the par-fill glue after it, so a position in it below the material's
// length is that node of the material.
void ParagraphComposer::map_shipped(const NodeArena& arena, const TextBlock& block, size_t offset,
                                    ParagraphLayout& out)
{
    const std::vector<Node>& material = block.run.nodes;
    out.shipped.assign(material.size(), NOT_SHIPPED);
    const size_t line_count = out.broken.lines.size();
    if (out.broken.breaks.size() != line_count)
        return;
    for (const Break& taken : out.broken.breaks) {
        if (taken.kind == BreakKind::DISC)
            return;
    }

    // Twice what a tree with no shared box could cost.
    uint64_t budget = 2 * static_cast<uint64_t>(arena.node_count()) + 1024;
    const size_t left = _breaker.left_skip_omitted() ? 0 : 1;
    uint32_t index = 0;
    bool walked = true;
    if (out.hung.is(NodeType::BOX)) {
        // The hung line is the marker's box and the line's; the marker's
        // indices come first.
        const NodeView pair = arena.contents(out.hung);
        if (pair.size() == 2 && pair[0].is(NodeType::BOX))
            walked = pointable_in_box(arena, pair[0], budget, index);
        out.marker_items = index;
    }
    for (size_t i = 0; walked && i < line_count; ++i) {
        const NodeView contents = arena.contents(out.broken.lines[i].box);
        const Break taken = out.broken.breaks[i];
        const size_t tail = taken.kind == BreakKind::KERN || taken.kind == BreakKind::MATH ||
                                    taken.kind == BreakKind::PENALTY
                                ? 1
                                : 0;
        if (contents.size() < left + tail + 1) {
            walked = false;
            break;
        }
        const size_t body = contents.size() - left - 1;
        const size_t from_list = body - tail;
        if (from_list > taken.position) {
            walked = false;
            break;
        }
        const size_t start = taken.position - from_list;
        index += static_cast<uint32_t>(left);
        for (size_t j = 0; walked && j < body; ++j) {
            const Node& node = contents[left + j];
            uint32_t items = 0;
            if (node.type == NodeType::CHAR || node.type == NodeType::GLUE)
                items = 1;
            else if (node.type == NodeType::BOX)
                walked = pointable_in_box(arena, node, budget, items);
            const size_t prepared = start + j;
            if (items != 0 && prepared >= offset && prepared - offset < material.size())
                out.shipped[prepared - offset] = index;
            index += items;
        }
        index += 1; // the right skip
    }
    if (!walked) {
        out.shipped.assign(material.size(), NOT_SHIPPED);
        out.marker_items = 0;
        return;
    }
    out.mapped = true;
    out.item_count = index;

    for (size_t m = 0; m < material.size(); ++m) {
        if (material[m].is_replaced() && out.shipped[m] != NOT_SHIPPED)
            out.originals.push_back(Original{out.shipped[m], material[m].original()});
    }

    // The reference's `_after_hard_breaks`: for each forced break, the first
    // node of the material after it that shipped. One that ends the material
    // has none, which is the reference's "not the last node". Walked
    // backwards, so the answers arrive descending.
    uint32_t next = NOT_SHIPPED;
    for (size_t m = material.size(); m-- > 0;) {
        const Node& node = material[m];
        if (node.type == NodeType::PENALTY && forces_break(node.penalty()) && next != NOT_SHIPPED &&
            (out.hard_breaks.empty() || out.hard_breaks.back() != next))
            out.hard_breaks.push_back(next);
        if (out.shipped[m] != NOT_SHIPPED)
            next = out.shipped[m];
    }
    for (size_t a = 0, b = out.hard_breaks.size(); a + 1 < b; ++a, --b)
        std::swap(out.hard_breaks[a], out.hard_breaks[b - 1]);
}

ShipError ship(const NodeArena& arena, const ParagraphLayout& layout, Point top_left,
               CharMeasure measure, std::vector<DrawCmd>& out, uint32_t max_items)
{
    if (!IMTYPESET_CONTRACT(layout.box.is(NodeType::BOX), "ship: the layout has no box"))
        return ShipError::NONE;
    const Point origin{top_left.x, top_left.y + layout.packed.box.height};
    return ship(arena, layout.box, origin, measure, out, max_items);
}

} // namespace ImTypeset::detail
