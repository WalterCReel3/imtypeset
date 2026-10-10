// See shipout.h for the map of tex.web's part 32 onto this file. Section
// numbers are tex.web's.

#include "shipout.h"

#include <cstddef>

namespace ImTypeset::detail
{

namespace
{

// Section 625: the set width of one glue node, and the running state it leaves
// behind. Section 634 is the same with heights for widths. tex.web has no
// name for this; the value returned is what both sections leave in rule_wd
// (rule_ht) before they move past the glue.
//
// `cur_glue` is the stretch (or, negated, the shrink) of the box's own order
// seen so far in this box, and `cur_g` is that total times the glue ratio,
// rounded. The node's width is taken against `cur_g` before the total moves
// and has the new `cur_g` added after, so the node gets the difference of two
// rounded totals and not its own rounded share. glue_set is positive for
// shrinking as well; the sign is in glue_sign alone (section 664 stores the
// ratio of two positive amounts).
//
// Glue whose order is not the box's contributes nothing and is set at its
// natural width.
//
// cur_g is within a billion either way (vet_glue); the node's width is whatever
// the list holds, so the two steps are checked, which tex.web's are not.
Scaled set_glue_width(const Node& node, const Box& box, double& cur_glue, Scaled& cur_g,
                      bool& overflowed)
{
    const Scaled width = sub_checked(node.width(), cur_g, overflowed);
    if (box.glue_sign == GlueSign::STRETCHING) {
        if (node.stretch_order == box.glue_order) {
            cur_glue += static_cast<double>(node.stretch());
            cur_g = pascal_round(vet_glue(box.glue_set * cur_glue));
        }
    } else if (box.glue_sign == GlueSign::SHRINKING) {
        if (node.shrink_order == box.glue_order) {
            cur_glue -= static_cast<double>(node.shrink());
            cur_g = pascal_round(vet_glue(box.glue_set * cur_glue));
        }
    }
    return add_checked(width, cur_g, overflowed);
}

// The recursive walk. `out` and the reading index are shared by the whole
// recursion; cur_glue and cur_g are locals of each list's walk, as tex.web
// declares them inside hlist_out and vlist_out (sections 619 and 629).
class Shipper
{
public:
    Shipper(const NodeArena& arena, CharMeasure measure, std::vector<DrawCmd>& out,
            uint32_t max_items)
        : _arena(arena)
        , _measure(measure)
        , _out(out)
        , _max_items(max_items)
    {
    }

    ShipError error() const { return _error; }

    // Recurses into a box. `as_line` says whether an hbox here is a line.
    void descend(const Box& box, NodeView contents, Scaled x, Scaled y, bool as_line)
    {
        // The arena refuses to nest deeper than this; a tree that does anyway
        // was not made by adding to one.
        if (_depth >= NodeArena::MAX_NESTING) {
            fail(ShipError::TOO_DEEP);
            return;
        }
        ++_depth;
        if (box.kind == BoxKind::VLIST)
            vlist_out(box, contents, x, y);
        else if (as_line)
            line_out(box, contents, x, y);
        else
            hlist_out(box, contents, x, y);
        --_depth;
    }

private:
    bool fail(ShipError error)
    {
        if (_error == ShipError::NONE)
            _error = error;
        return false;
    }

    // Positions are sums over a tree somebody else may have built. One that
    // leaves the range of a Scaled stops there and fails the whole ship.
    Scaled plus(Scaled a, Scaled b)
    {
        bool overflowed = false;
        const Scaled sum = add_checked(a, b, overflowed);
        if (overflowed)
            fail(ShipError::DIMENSION_OVERFLOW);
        return sum;
    }
    Scaled minus(Scaled a, Scaled b)
    {
        bool overflowed = false;
        const Scaled difference = sub_checked(a, b, overflowed);
        if (overflowed)
            fail(ShipError::DIMENSION_OVERFLOW);
        return difference;
    }
    Scaled glue_width(const Node& node, const Box& box, double& cur_glue, Scaled& cur_g)
    {
        bool overflowed = false;
        const Scaled width = set_glue_width(node, box, cur_glue, cur_g, overflowed);
        if (overflowed)
            fail(ShipError::DIMENSION_OVERFLOW);
        return width;
    }

    // The box a BOX node names, or null with the ship failed.
    const Box* box_of(const Node& node)
    {
        const Box* const box = _arena.find_box(node);
        if (box == nullptr)
            fail(ShipError::NODE_NOT_IN_ARENA);
        return box;
    }

    // Counts one node of the walk. False once there have been more than the
    // caller allowed.
    bool visit()
    {
        if (_visited == _max_items)
            return fail(ShipError::TOO_MANY_ITEMS);
        ++_visited;
        return true;
    }

    // Ships an hbox as a line: its LINE command first, so that one forward pass
    // over the buffer always knows which line it is in, with the end of its
    // index range filled in once the contents have been walked. Not tex.web's,
    // which has no lines after the paragraph is built.
    void line_out(const Box& box, NodeView contents, Scaled left_edge, Scaled base_line)
    {
        const size_t slot = _out.size();
        _out.push_back(
            DrawCmd::line(left_edge, base_line, box.width, box.height, box.depth, _index, _index));
        hlist_out(box, contents, left_edge, base_line);
        // By index, not by reference: the buffer has grown since.
        _out[slot].end = _index;
    }

    // tex.web's hlist_out (sections 619 to 625): a horizontal list, left to
    // right, with cur_v held at base_line. `box` is its this_box.
    void hlist_out(const Box& box, NodeView contents, Scaled left_edge, Scaled base_line)
    {
        Scaled cur_h = left_edge;
        double cur_glue = 0.0;
        Scaled cur_g = 0;

        for (const Node& node : contents) {
            if (_error != ShipError::NONE || !visit())
                return;
            switch (node.type) {
            case NodeType::CHAR: {
                // Section 620, which writes the character to the DVI file and
                // then advances by its width. The glyph is placed before its
                // advance is looked up, as in the reference; an error discards
                // it with the rest.
                _out.push_back(
                    DrawCmd::glyph(node.font(), node.codepoint(), cur_h, base_line, _index));
                if (!_in_atom)
                    ++_index;
                if (!_measure) {
                    fail(ShipError::CHAR_WITHOUT_MEASURE);
                    return;
                }
                cur_h = plus(cur_h, _measure(node.font(), node.codepoint()).width);
                break;
            }
            case NodeType::BOX: {
                const Box* const found = box_of(node);
                if (found == nullptr)
                    return;
                const Box& inner = *found;
                const NodeView inner_contents = _arena.view(inner.contents);
                const Scaled inner_base = plus(base_line, inner.shift_amount);
                if (!_in_atom && is_atom(_arena, inner)) {
                    // Not tex.web's. One item, then its ink. The index moves
                    // on only after the inside has been walked, so every glyph
                    // in there carries the atom's. y, height and depth are the
                    // enclosing box's.
                    _out.push_back(DrawCmd::atom(cur_h, base_line, inner.width, inner.width,
                                                 box.height, box.depth, _index));
                    _in_atom = true;
                    descend(inner, inner_contents, cur_h, inner_base, false);
                    _in_atom = false;
                    ++_index;
                } else {
                    // Section 623: the sub-box is shifted down by shift_amount.
                    // An hbox inside an hbox is not a line.
                    descend(inner, inner_contents, cur_h, inner_base, false);
                }
                // Section 623 again: the edge plus the box's own width, never
                // wherever the recursion happened to leave it.
                cur_h = plus(cur_h, inner.width);
                break;
            }
            case NodeType::RULE: {
                // Section 624: a running height or depth takes the enclosing
                // box's, the two together are the thickness, and a rule with
                // no thickness or no width is not output.
                if (node.width() == RUNNING) {
                    fail(ShipError::RUNNING_WIDTH_IN_HLIST);
                    return;
                }
                const Scaled height = node.height() == RUNNING ? box.height : node.height();
                const Scaled depth = node.depth() == RUNNING ? box.depth : node.depth();
                const Scaled thickness = plus(height, depth);
                if (thickness > 0 && node.width() > 0)
                    _out.push_back(DrawCmd::rule_rect(cur_h, minus(base_line, height), node.width(),
                                                      thickness));
                cur_h = plus(cur_h, node.width());
                break;
            }
            case NodeType::GLUE: {
                // Section 625. Inside an atom the glue still moves cur_h and is
                // neither a command nor an index.
                const Scaled set_width = glue_width(node, box, cur_glue, cur_g);
                if (!_in_atom) {
                    _out.push_back(DrawCmd::set_glue(cur_h, base_line, set_width, node.width(),
                                                     box.height, box.depth, _index));
                    ++_index;
                }
                cur_h = plus(cur_h, set_width);
                break;
            }
            case NodeType::KERN:
            case NodeType::MATH_ON:
            case NodeType::MATH_OFF:
                // Section 622: a kern or a math node moves cur_h by its width.
                cur_h = plus(cur_h, node.width());
                break;
            case NodeType::PENALTY:
            case NodeType::DISC:
                // Section 622's othercases. A penalty has no extent, and a
                // discretionary that survived line breaking was not taken.
                break;
            }
        }
    }

    // tex.web's vlist_out (sections 629 to 634): a vertical list, downward,
    // with cur_h held at left_edge. Entered at the box's reference point and
    // stepping at once to its top edge (section 629 takes the box's height off
    // cur_v); each item then advances cur_v by its height to reach its
    // baseline and by its depth afterwards.
    void vlist_out(const Box& box, NodeView contents, Scaled left_edge, Scaled reference)
    {
        Scaled cur_v = minus(reference, box.height);
        double cur_glue = 0.0;
        Scaled cur_g = 0;

        for (const Node& node : contents) {
            if (_error != ShipError::NONE || !visit())
                return;
            switch (node.type) {
            case NodeType::CHAR:
                // Section 630, where tex.web stops with confusion.
                fail(ShipError::CHAR_IN_VLIST);
                return;
            case NodeType::BOX: {
                // Section 632: down to the item's baseline, recurse, then down
                // by its depth. shift_amount moves the box right here. An hbox
                // that is an item of a vertical list is a line, unless all of
                // this is inside an atom.
                const Box* const found = box_of(node);
                if (found == nullptr)
                    return;
                const Box& inner = *found;
                cur_v = plus(cur_v, inner.height);
                descend(inner, _arena.view(inner.contents), plus(left_edge, inner.shift_amount),
                        cur_v, !_in_atom);
                cur_v = plus(cur_v, inner.depth);
                break;
            }
            case NodeType::RULE: {
                // Section 633: a running width takes the enclosing box's, and
                // cur_v moves past the rule before it is drawn, so the rule
                // hangs above the new cur_v.
                if (node.height() == RUNNING || node.depth() == RUNNING) {
                    fail(ShipError::RUNNING_HEIGHT_OR_DEPTH_IN_VLIST);
                    return;
                }
                const Scaled width = node.width() == RUNNING ? box.width : node.width();
                const Scaled thickness = plus(node.height(), node.depth());
                const Scaled top = cur_v;
                cur_v = plus(cur_v, thickness);
                if (thickness > 0 && width > 0)
                    _out.push_back(DrawCmd::rule_rect(left_edge, top, width, thickness));
                break;
            }
            case NodeType::GLUE:
                // Section 634. Vertical glue is not pointable: no command and
                // no index, which is all tex.web ever does for glue.
                cur_v = plus(cur_v, glue_width(node, box, cur_glue, cur_g));
                break;
            case NodeType::KERN:
                // Section 631: a kern moves cur_v by its width.
                cur_v = plus(cur_v, node.width());
                break;
            case NodeType::PENALTY:
            case NodeType::DISC:
            case NodeType::MATH_ON:
            case NodeType::MATH_OFF:
                // Section 631's othercases: nothing with an extent.
                break;
            }
        }
    }

    const NodeArena& _arena;
    CharMeasure _measure;
    std::vector<DrawCmd>& _out;
    uint32_t _max_items;
    uint32_t _index = 0;
    uint32_t _visited = 0;
    int _depth = 0;
    ShipError _error = ShipError::NONE;
    // True while walking the inside of an atom box: nothing in there takes an
    // index of its own or is a line.
    bool _in_atom = false;
};

} // namespace

bool is_atom(const NodeArena& arena, const Box& box)
{
    if (box.kind != BoxKind::HLIST)
        return false;
    const NodeView contents = arena.view(box.contents);
    return contents.size() >= 2 && contents[0].is(NodeType::MATH_ON) &&
           contents[contents.size() - 1].is(NodeType::MATH_OFF);
}

Node atom_box(NodeArena& arena, const Node& box)
{
    // Copied out: adding to the arena invalidates references into it.
    const Box inner = arena.box(box);
    const Scaled shift = inner.shift_amount;
    Box wrapper;
    wrapper.kind = BoxKind::HLIST;
    wrapper.width = inner.width;
    wrapper.height = shift < 0 ? sub_saturating(inner.height, shift) : inner.height;
    wrapper.depth = shift > 0 ? add_saturating(inner.depth, shift) : inner.depth;
    const Node contents[3] = {math_on(), box, math_off()};
    return arena.add_box(wrapper, NodeView(contents, 3));
}

// What is left of tex.web's ship_out (sections 638 and 640) once the DVI file
// is gone: start the walk. Section 640 starts it at the page's height below
// the top-left corner, plus the offsets; here the caller says where.
ShipError ship(const NodeArena& arena, const Box& box, NodeView contents, Point origin,
               CharMeasure measure, std::vector<DrawCmd>& out, uint32_t max_items)
{
    if (max_items == MAX_SHIP_ITEMS) {
        const uint64_t every = static_cast<uint64_t>(arena.node_count()) + contents.size();
        const uint64_t allowed = every * DEFAULT_SHIP_VISITS_PER_NODE + DEFAULT_SHIP_VISITS_FLOOR;
        max_items = allowed < MAX_SHIP_ITEMS ? static_cast<uint32_t>(allowed) : MAX_SHIP_ITEMS - 1;
    }
    const size_t size_on_entry = out.size();
    Shipper shipper(arena, measure, out, max_items);
    shipper.descend(box, contents, origin.x, origin.y, true);
    if (shipper.error() != ShipError::NONE)
        out.resize(size_on_entry);
    return shipper.error();
}

ShipError ship(const NodeArena& arena, const Node& box, Point origin, CharMeasure measure,
               std::vector<DrawCmd>& out, uint32_t max_items)
{
    if (!box.is(NodeType::BOX))
        return ShipError::ROOT_NOT_A_BOX;
    const Box* const root = arena.find_box(box);
    if (root == nullptr)
        return ShipError::NODE_NOT_IN_ARENA;
    return ship(arena, *root, arena.view(root->contents), origin, measure, out, max_items);
}

} // namespace ImTypeset::detail
