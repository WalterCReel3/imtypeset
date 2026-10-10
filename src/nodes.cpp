// The arena behind src/nodes.h. Nothing here transcribes tex.web: its nodes
// live in one array, mem, managed by get_node and free_node (sections 115 to
// 130), and none of that is ported.

#include "nodes.h"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace ImTypeset::detail
{

namespace
{

// What nesting_of answers for a span that names a box or discretionary the
// arena does not hold.
constexpr int NAMES_NOTHING = -1;

const Box EMPTY_BOX{};
const Disc NODES_EMPTY_DISC{};

// The arena indexes with 32 bits. Past that nothing more is stored.
bool has_room(size_t have, size_t more)
{
    return more <= static_cast<size_t>(std::numeric_limits<uint32_t>::max()) - have;
}

} // namespace

bool NodeArena::holds(NodeSpan span) const
{
    return span.first <= _nodes.size() && span.count <= _nodes.size() - span.first;
}

int NodeArena::nesting(const Node& node) const
{
    if (node.type == NodeType::BOX)
        return node.u < _box_nesting.size() ? _box_nesting[node.u] : 0;
    if (node.type == NodeType::DISC)
        return node.u < _disc_nesting.size() ? _disc_nesting[node.u] : 0;
    return 0;
}

// The deepest nesting among a span's nodes, or NAMES_NOTHING if one of them
// is a BOX or DISC node whose index the arena does not hold: one pass for
// both, because every box made goes through here.
int NodeArena::nesting_of(NodeSpan span) const
{
    int deepest = 0;
    const Node* const nodes = _nodes.data() + span.first;
    for (uint32_t i = 0; i < span.count; ++i) {
        const Node& node = nodes[i];
        if (node.type == NodeType::BOX) {
            if (node.u >= _box_nesting.size())
                return NAMES_NOTHING;
            deepest = std::max<int>(deepest, _box_nesting[node.u]);
        } else if (node.type == NodeType::DISC) {
            if (node.u >= _disc_nesting.size())
                return NAMES_NOTHING;
            deepest = std::max<int>(deepest, _disc_nesting[node.u]);
        }
    }
    return deepest;
}

NodeSpan NodeArena::add_nodes(NodeView nodes)
{
    NodeSpan span;
    span.first = static_cast<uint32_t>(_nodes.size());
    if (nodes.empty())
        return span;
    if (!IMTYPESET_CONTRACT(has_room(_nodes.size(), nodes.size()), "NodeArena: too many nodes"))
        return span;

    // The view may be of this arena's own storage, which growing would move.
    const Node* const own_begin = _nodes.data();
    const bool aliases = own_begin != nullptr && nodes.data() >= own_begin &&
                         nodes.data() < own_begin + _nodes.size();
    if (aliases) {
        const size_t offset = static_cast<size_t>(nodes.data() - own_begin);
        _nodes.resize(_nodes.size() + nodes.size());
        std::copy_n(_nodes.data() + offset, nodes.size(), _nodes.data() + span.first);
    } else {
        _nodes.insert(_nodes.end(), nodes.begin(), nodes.end());
    }
    span.count = static_cast<uint32_t>(nodes.size());
    return span;
}

Node NodeArena::add_box(const Box& box)
{
    Box stored = box;
    if (!IMTYPESET_CONTRACT(holds(stored.contents), "NodeArena: box contents outside the arena"))
        stored.contents = NodeSpan{};
    int nesting_inside = nesting_of(stored.contents);
    // The box about to be made is not among its contents, nor is any made
    // after it: that is what keeps the tree a tree.
    if (!IMTYPESET_CONTRACT(nesting_inside != NAMES_NOTHING,
                            "NodeArena: box contents name a box that does not exist yet")) {
        stored.contents = NodeSpan{};
        nesting_inside = 0;
    }
    int nesting_here = nesting_inside + 1;
    if (!IMTYPESET_CONTRACT(nesting_here <= MAX_NESTING, "NodeArena: boxes nested too deeply")) {
        stored.contents = NodeSpan{};
        nesting_here = 1;
    }

    Node node;
    node.type = NodeType::BOX;
    if (!IMTYPESET_CONTRACT(has_room(_boxes.size(), 1), "NodeArena: too many boxes")) {
        // Index 0xFFFFFFFF names no box; box() answers an empty one for it.
        node.u = std::numeric_limits<uint32_t>::max();
        return node;
    }
    node.u = static_cast<uint32_t>(_boxes.size());
    _boxes.push_back(stored);
    _box_nesting.push_back(static_cast<uint16_t>(nesting_here));
    return node;
}

Node NodeArena::add_box(const Box& header, NodeView contents)
{
    Box box = header;
    box.contents = add_nodes(contents);
    return add_box(box);
}

Node NodeArena::add_disc(NodeView pre_break, NodeView post_break, int32_t replace_count)
{
    Disc disc;
    disc.pre_break = add_nodes(pre_break);
    disc.post_break = add_nodes(post_break);
    disc.replace_count = replace_count;
    return push_disc(disc);
}

Node NodeArena::push_disc(Disc disc)
{
    int nesting_pre = nesting_of(disc.pre_break);
    int nesting_post = nesting_of(disc.post_break);
    if (!IMTYPESET_CONTRACT(nesting_pre != NAMES_NOTHING && nesting_post != NAMES_NOTHING,
                            "NodeArena: a discretionary names a node that does not exist yet")) {
        disc.pre_break = NodeSpan{};
        disc.post_break = NodeSpan{};
        nesting_pre = 0;
        nesting_post = 0;
    }
    int nesting_here = std::max(nesting_pre, nesting_post) + 1;
    if (!IMTYPESET_CONTRACT(nesting_here <= MAX_NESTING, "NodeArena: nodes nested too deeply")) {
        disc.pre_break = NodeSpan{};
        disc.post_break = NodeSpan{};
        nesting_here = 1;
    }

    Node node;
    node.type = NodeType::DISC;
    if (!IMTYPESET_CONTRACT(has_room(_discs.size(), 1), "NodeArena: too many discretionaries")) {
        node.u = std::numeric_limits<uint32_t>::max();
        return node;
    }
    node.u = static_cast<uint32_t>(_discs.size());
    _discs.push_back(disc);
    _disc_nesting.push_back(static_cast<uint16_t>(nesting_here));
    return node;
}

const Box& NodeArena::box(const Node& node) const
{
    if (!IMTYPESET_CONTRACT(node.type == NodeType::BOX && node.u < _boxes.size(),
                            "NodeArena: not a box of this arena"))
        return EMPTY_BOX;
    return _boxes[node.u];
}

const Disc& NodeArena::disc(const Node& node) const
{
    if (!IMTYPESET_CONTRACT(node.type == NodeType::DISC && node.u < _discs.size(),
                            "NodeArena: not a discretionary of this arena"))
        return NODES_EMPTY_DISC;
    return _discs[node.u];
}

NodeView NodeArena::view(NodeSpan span) const
{
    if (!IMTYPESET_CONTRACT(holds(span), "NodeArena: span outside the arena"))
        return NodeView();
    return NodeView(_nodes.data() + span.first, span.count);
}

// What one import has already copied: source index to the node made for it.
struct NodeArena::ImportMemo {
    std::unordered_map<uint32_t, Node> boxes;
    std::unordered_map<uint32_t, Node> discs;
};

NodeSpan NodeArena::import_span(const NodeArena& source, NodeSpan span, ImportMemo& memo, int depth)
{
    // Children first, into a scratch list, so that this span's own nodes end up
    // contiguous however much the children add to the arena. `source` may be
    // this arena, so nothing of it is held by reference across an add.
    std::vector<Node> scratch;
    if (!source.holds(span))
        return add_nodes(scratch);
    scratch.reserve(span.count);
    for (uint32_t i = 0; i < span.count; ++i) {
        const Node child = source._nodes[span.first + i];
        scratch.push_back(import_node(source, child, memo, depth));
    }
    return add_nodes(scratch);
}

Node NodeArena::import_node(const NodeArena& source, const Node& node, ImportMemo& memo, int depth)
{
    if (node.type == NodeType::BOX) {
        const auto seen = memo.boxes.find(node.u);
        if (seen != memo.boxes.end())
            return seen->second;
        const Box* const original = source.find_box(node);
        Box copy = original != nullptr ? *original : EMPTY_BOX;
        copy.contents =
            depth < MAX_NESTING ? import_span(source, copy.contents, memo, depth + 1) : NodeSpan{};
        const Node made = add_box(copy);
        memo.boxes.emplace(node.u, made);
        return made;
    }
    if (node.type == NodeType::DISC) {
        const auto seen = memo.discs.find(node.u);
        if (seen != memo.discs.end())
            return seen->second;
        const Disc* const found = source.find_disc(node);
        const Disc original = found != nullptr ? *found : NODES_EMPTY_DISC;
        Disc copy;
        if (depth < MAX_NESTING) {
            copy.pre_break = import_span(source, original.pre_break, memo, depth + 1);
            copy.post_break = import_span(source, original.post_break, memo, depth + 1);
        }
        copy.replace_count = original.replace_count;
        const Node made = push_disc(copy);
        memo.discs.emplace(node.u, made);
        return made;
    }
    return node;
}

Node NodeArena::import(const NodeArena& source, const Node& node)
{
    if (node.type != NodeType::BOX && node.type != NodeType::DISC)
        return node;
    ImportMemo memo;
    return import_node(source, node, memo, 0);
}

NodeArena::Mark NodeArena::mark() const
{
    Mark m;
    m.nodes = static_cast<uint32_t>(_nodes.size());
    m.boxes = static_cast<uint32_t>(_boxes.size());
    m.discs = static_cast<uint32_t>(_discs.size());
    return m;
}

void NodeArena::rewind(Mark m)
{
    if (!IMTYPESET_CONTRACT(m.nodes <= _nodes.size() && m.boxes <= _boxes.size() &&
                                m.discs <= _discs.size(),
                            "NodeArena: rewind to a mark that is ahead of the arena"))
        return;
    _nodes.resize(m.nodes);
    _boxes.resize(m.boxes);
    _box_nesting.resize(m.boxes);
    _discs.resize(m.discs);
    _disc_nesting.resize(m.discs);
}

void NodeArena::clear()
{
    _nodes.clear();
    _boxes.clear();
    _box_nesting.clear();
    _discs.clear();
    _disc_nesting.clear();
}

namespace
{

// One comparison of two trees: which pairs of boxes and of discretionaries are
// already known equal, and how deep the walk is.
class Comparison
{
public:
    Comparison(const NodeArena& arena_a, const NodeArena& arena_b)
        : _a(arena_a)
        , _b(arena_b)
    {
    }

    bool lists(NodeView a, NodeView b, int depth)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (!nodes(a[i], b[i], depth))
                return false;
        }
        return true;
    }

    bool nodes(const Node& a, const Node& b, int depth)
    {
        if (a.type != b.type)
            return false;
        if (a.type == NodeType::BOX) {
            const Box* const found_x = _a.find_box(a);
            const Box* const found_y = _b.find_box(b);
            const Box& x = found_x != nullptr ? *found_x : EMPTY_BOX;
            const Box& y = found_y != nullptr ? *found_y : EMPTY_BOX;
            // glue_set compares as the reference's float does: by value, so a
            // NaN is unequal to itself and -0.0 equals 0.0.
            if (!(x.kind == y.kind && x.width == y.width && x.height == y.height &&
                  x.depth == y.depth && x.shift_amount == y.shift_amount &&
                  x.glue_set == y.glue_set && x.glue_sign == y.glue_sign &&
                  x.glue_order == y.glue_order))
                return false;
            if (x.contents.count == 0 && y.contents.count == 0)
                return true;
            const uint64_t pair = (static_cast<uint64_t>(a.u) << 32) | b.u;
            if (_equal_boxes.count(pair) != 0)
                return true;
            if (depth >= NodeArena::MAX_NESTING ||
                !lists(view(_a, x.contents), view(_b, y.contents), depth + 1))
                return false;
            _equal_boxes.insert(pair);
            return true;
        }
        if (a.type == NodeType::DISC) {
            const Disc* const found_x = _a.find_disc(a);
            const Disc* const found_y = _b.find_disc(b);
            const Disc& x = found_x != nullptr ? *found_x : NODES_EMPTY_DISC;
            const Disc& y = found_y != nullptr ? *found_y : NODES_EMPTY_DISC;
            if (x.replace_count != y.replace_count)
                return false;
            const uint64_t pair = (static_cast<uint64_t>(a.u) << 32) | b.u;
            if (_equal_discs.count(pair) != 0)
                return true;
            if (depth >= NodeArena::MAX_NESTING ||
                !lists(view(_a, x.pre_break), view(_b, y.pre_break), depth + 1) ||
                !lists(view(_a, x.post_break), view(_b, y.post_break), depth + 1))
                return false;
            _equal_discs.insert(pair);
            return true;
        }
        return a == b;
    }

private:
    // A span its arena does not hold reads as empty, without the contract
    // NodeArena::view has: the tree may be somebody else's.
    static NodeView view(const NodeArena& arena, NodeSpan span)
    {
        const size_t count = arena.node_count();
        if (span.first > count || span.count > count - span.first)
            return NodeView();
        return arena.view(span);
    }

    const NodeArena& _a;
    const NodeArena& _b;
    std::unordered_set<uint64_t> _equal_boxes;
    std::unordered_set<uint64_t> _equal_discs;
};

} // namespace

bool deep_equal(const NodeArena& arena_a, const Node& a, const NodeArena& arena_b, const Node& b)
{
    Comparison comparison(arena_a, arena_b);
    return comparison.nodes(a, b, 0);
}

bool deep_equal(const NodeArena& arena_a, NodeView a, const NodeArena& arena_b, NodeView b)
{
    Comparison comparison(arena_a, arena_b);
    return comparison.lists(a, b, 0);
}

} // namespace ImTypeset::detail
