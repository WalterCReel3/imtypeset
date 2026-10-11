#include "vsplit.h"

#include <algorithm>

namespace ImTypeset::detail
{

namespace
{

void note(VsplitError& error, VsplitError found)
{
    if (error == VsplitError::NONE)
        error = found;
}

// The height and depth a box or rule adds to a vertical list, or false, with
// the reason, for one that adds nothing.
bool vertical_dimens(const NodeArena& arena, const Node& node, Scaled& height, Scaled& depth,
                     VsplitError& error)
{
    if (node.is(NodeType::BOX)) {
        const Box* const box = arena.find_box(node);
        if (box == nullptr) {
            note(error, VsplitError::NODE_NOT_IN_ARENA);
            return false;
        }
        height = box->height;
        depth = box->depth;
        return true;
    }
    if (node.height() == RUNNING || node.depth() == RUNNING) {
        note(error, VsplitError::RUNNING_RULE);
        return false;
    }
    height = node.height();
    depth = node.depth();
    return true;
}

} // namespace

int64_t break_badness(Scaled cur_height, Scaled h, const GlueTotal& stretch, Scaled shrink)
{
    if (cur_height < h) {
        // active_height[3..5]: any infinite stretch reaches the height exactly.
        if (stretch.dominant_order() != GlueOrder::NORMAL)
            return 0;
        return badness(sub_saturating(h, cur_height), stretch[GlueOrder::NORMAL]);
    }
    if (sub_saturating(cur_height, h) > shrink)
        return AWFUL_BAD;
    return badness(sub_saturating(cur_height, h), shrink);
}

Pruned prune_page_top(const NodeArena& arena, NodeView nodes, const GlueSpec& split_top_skip)
{
    Pruned out;
    out.start = nodes.size();
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Node& node = nodes[i];
        switch (node.type) {
        case NodeType::BOX:
        case NodeType::RULE: {
            // Section 969. A box the arena does not hold, or a rule whose
            // height runs, has no height to take off: the glue is the skip
            // whole.
            Scaled height = 0;
            if (node.is(NodeType::BOX)) {
                const Box* const box = arena.find_box(node);
                if (box != nullptr)
                    height = box->height;
                else
                    note(out.error, VsplitError::NODE_NOT_IN_ARENA);
            } else if (node.height() == RUNNING) {
                note(out.error, VsplitError::RUNNING_RULE);
            } else {
                height = node.height();
            }
            GlueSpec spec = split_top_skip;
            spec.width = spec.width > height ? sub_saturating(spec.width, height) : 0;
            out.start = i;
            out.head = true;
            out.glue = glue(spec);
            return out;
        }
        case NodeType::GLUE:
        case NodeType::KERN:
        case NodeType::PENALTY:
            break;
        case NodeType::CHAR:
        case NodeType::DISC:
        case NodeType::MATH_ON:
        case NodeType::MATH_OFF:
            // confusion("pruning"). It is deleted with the glue round it.
            note(out.error, VsplitError::NOT_VERTICAL);
            break;
        }
    }
    return out;
}

void VertBreak::begin(Scaled h, Scaled d)
{
    _h = h;
    _d = d;
    _cur_height = 0;
    _stretch = GlueTotal();
    _shrink = 0;
    _prev_dp = 0;
    _next = 0;
    _apart = 0;
    _done = false;
    best_place = 0;
    least_cost = AWFUL_BAD;
    best_height_plus_depth = 0;
    stop = ScanStop::END;
    error = VsplitError::NONE;
}

void VertBreak::head(const Node& glue_node)
{
    if (!IMTYPESET_CONTRACT(_next == 0 && _apart == 0 && glue_node.is(NodeType::GLUE),
                            "VertBreak::head: glue, once, before the scan"))
        return;
    // Glue at the head of the list is not a breakpoint (section 970); what is
    // left of the loop for it is section 976 and section 972's depth limit.
    _stretch.add(glue_node.stretch(), glue_node.stretch_order);
    _shrink = add_saturating(_shrink, glue_node.shrink());
    _cur_height = add_saturating(_cur_height, add_saturating(_prev_dp, glue_node.width()));
    _prev_dp = 0;
    if (_prev_dp > _d) {
        _cur_height = add_saturating(_cur_height, sub_saturating(_prev_dp, _d));
        _prev_dp = _d;
    }
    _next = 1;
    _apart = 1;
}

bool VertBreak::scan(const NodeArena& arena, NodeView nodes, bool complete,
                     std::vector<BreakCandidate>* trace)
{
    while (!_done) {
        const size_t p = _next;
        // Where node p is in `nodes`.
        const size_t at = p - _apart;
        const bool at_end = at >= nodes.size();
        // Section 973: `legal` is "pi was set"; without it the node is not a
        // breakpoint and section 974 is passed over.
        bool legal = false;
        Scaled pi = 0;
        if (at_end) {
            if (!complete)
                return false;
            // Section 972: the list is followed by a forced break.
            pi = EJECT_PENALTY;
            legal = true;
        } else {
            const Node& node = nodes[at];
            switch (node.type) {
            case NodeType::BOX:
            case NodeType::RULE: {
                Scaled height = 0;
                Scaled depth = 0;
                if (vertical_dimens(arena, node, height, depth, error)) {
                    _cur_height = add_saturating(_cur_height, add_saturating(_prev_dp, height));
                    _prev_dp = depth;
                }
                break;
            }
            case NodeType::GLUE:
                // Section 970: prev_p starts at p, so glue at the head of the
                // list is tested against itself and is never a breakpoint.
                // Section 148's precedes_break is type(p) < math_node.
                if (at > 0 && !is_discardable(nodes[at - 1])) {
                    pi = 0;
                    legal = true;
                }
                break;
            case NodeType::KERN:
                // Any kern, whatever its kind, when glue follows it. A kern
                // that ends the list is followed by the forced break, which
                // is a penalty and not glue.
                if (at + 1 < nodes.size()) {
                    if (nodes[at + 1].is(NodeType::GLUE)) {
                        pi = 0;
                        legal = true;
                    }
                } else if (!complete) {
                    return false;
                }
                break;
            case NodeType::PENALTY:
                pi = node.penalty();
                legal = true;
                break;
            case NodeType::CHAR:
            case NodeType::DISC:
            case NodeType::MATH_ON:
            case NodeType::MATH_OFF:
                // confusion("vertbreak").
                note(error, VsplitError::NOT_VERTICAL);
                break;
            }
        }

        // Section 974.
        if (legal && pi < INF_PENALTY) {
            const int64_t b = break_badness(_cur_height, _h, _stretch, _shrink);
            int64_t cost = b;
            if (cost < AWFUL_BAD) {
                if (pi <= EJECT_PENALTY)
                    cost = pi;
                else if (cost < INF_BAD)
                    cost = cost + pi;
                else
                    cost = DEPLORABLE;
            }
            // <= and not <: of two breaks that cost the same the later one is
            // kept. A list with no stretch in it makes every short page cost
            // DEPLORABLE, and the last of those is the fullest page that fits.
            const bool taken = cost <= least_cost;
            if (taken) {
                best_place = p;
                least_cost = cost;
                best_height_plus_depth = add_saturating(_cur_height, _prev_dp);
            }
            if (trace != nullptr) {
                BreakCandidate candidate;
                candidate.index = p;
                candidate.penalty = pi;
                candidate.cur_height = _cur_height;
                candidate.prev_dp = _prev_dp;
                candidate.stretch = _stretch;
                candidate.shrink = _shrink;
                candidate.badness = b;
                candidate.cost = cost;
                candidate.taken = taken;
                trace->push_back(candidate);
            }
            if (cost == AWFUL_BAD || pi <= EJECT_PENALTY) {
                stop = cost == AWFUL_BAD ? ScanStop::OVERFULL
                                         : (at_end ? ScanStop::END : ScanStop::EJECT);
                _done = true;
                return true;
            }
        }

        // Section 976, reached by glue and kerns whether or not they were
        // breakpoints. Glue's shrink is added whatever its order: infinite
        // shrink is made finite, as section 976 makes it after its message.
        if (!at_end && (nodes[at].is(NodeType::GLUE) || nodes[at].is(NodeType::KERN))) {
            const Node& node = nodes[at];
            if (node.is(NodeType::GLUE)) {
                _stretch.add(node.stretch(), node.stretch_order);
                _shrink = add_saturating(_shrink, node.shrink());
            }
            _cur_height = add_saturating(_cur_height, add_saturating(_prev_dp, node.width()));
            _prev_dp = 0;
        }
        // Section 972's not_found: d may be negative, so this follows glue
        // and kerns as well as boxes.
        if (_prev_dp > _d) {
            _cur_height = add_saturating(_cur_height, sub_saturating(_prev_dp, _d));
            _prev_dp = _d;
        }
        ++_next;
    }
    return true;
}

VertBreakResult vert_break(const NodeArena& arena, NodeView nodes, Scaled h, Scaled d,
                           std::vector<BreakCandidate>* trace)
{
    VertBreak scan;
    scan.begin(h, d);
    scan.scan(arena, nodes, true, trace);
    VertBreakResult out;
    out.best_place = scan.best_place;
    out.least_cost = scan.least_cost;
    out.best_height_plus_depth = scan.best_height_plus_depth;
    out.stop = scan.stop;
    out.error = scan.error;
    return out;
}

Vsplit vsplit(const NodeArena& arena, NodeView nodes, Scaled h, const GlueSpec& split_top_skip,
              Scaled split_max_depth, std::vector<BreakCandidate>* trace)
{
    Vsplit out;
    out.cut = vert_break(arena, nodes, h, split_max_depth, trace);
    const size_t place = std::min(out.cut.best_place, nodes.size());
    out.packed = vpack(arena, nodes.sub(0, place), h, PackMode::EXACTLY, split_max_depth);
    out.rest = prune_page_top(arena, nodes.sub(place, nodes.size() - place), split_top_skip);
    out.rest.start += place;
    return out;
}

bool split_into_columns(const NodeArena& arena, ColumnList& list, Scaled height, size_t count,
                        ColumnSplit& out, const GlueSpec& split_top_skip, Scaled split_max_depth)
{
    out.columns.clear();
    out.candidates.clear();
    out.overflow_head = false;
    out.overflow_start = 0;
    out.overflow_glue = Node();
    out.overflow_count = 0;
    out.error = VsplitError::NONE;
    if (count < 1 || height <= 0)
        return false;
    if (list.nodes.empty())
        list.clear();
    const size_t size = list.material_size();
    const auto material = [&](size_t from) {
        return NodeView(list.nodes.data() + 1 + from, size - from);
    };

    // What the next column is handed: `rest.start` is an index into the
    // material, and the node before it is free for the glue.
    Pruned rest = prune_page_top(arena, material(0), split_top_skip);
    if (out.error == VsplitError::NONE)
        out.error = rest.error;
    out.columns.reserve(count);
    for (size_t k = 0; k < count; ++k) {
        SplitColumn column;
        column.head = rest.head;
        column.start = rest.start;
        column.glue = rest.glue;
        column.candidate_first = out.candidates.size();
        NodeView handed;
        if (rest.head) {
            list.nodes[rest.start] = rest.glue;
            handed = NodeView(list.nodes.data() + rest.start, size - rest.start + 1);
        }
        const Vsplit cut = vsplit(arena, handed, height, split_top_skip, split_max_depth,
                                  out.tracing ? &out.candidates : nullptr);
        column.candidate_end = out.candidates.size();
        column.cut = cut.cut;
        column.packed = cut.packed;
        column.first = rest.head ? rest.start : list.nodes.size();
        column.count = std::min(cut.cut.best_place, handed.size());
        if (out.error == VsplitError::NONE)
            out.error = cut.cut.error != VsplitError::NONE ? cut.cut.error : cut.rest.error;
        out.columns.push_back(column);

        // cut.rest.start counts in the list handed over, whose node 0 stands
        // in the material at rest.start - 1.
        Pruned next = cut.rest;
        next.start = rest.head ? rest.start + cut.rest.start - 1 : size;
        rest = next;
    }
    out.overflow_head = rest.head;
    out.overflow_start = rest.start;
    out.overflow_glue = rest.glue;
    out.overflow_count = rest.head ? size - rest.start + 1 : 0;
    return true;
}

} // namespace ImTypeset::detail
