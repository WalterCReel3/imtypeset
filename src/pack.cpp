// See pack.h for the map of tex.web's part 33 onto this file. Section numbers
// are tex.web's.

#include "pack.h"

#include "badness.h"

#include <algorithm>

namespace ImTypeset::detail
{

namespace
{

// ARITHMETIC OVER A LIST NOBODY VOUCHED FOR
//
// None of this is tex.web's, whose hpack and vpack add 32-bit dimensions
// unchecked (sections 651 to 656, 669 to 671). Guarded and Checked are this
// port's own (decision D33).
//
// The sums below are of dimensions out of a list that may have been built by
// hand, so none of them may be a plain signed addition (src/units.h). They are
// formed twice over, by two policies with one interface, and the walk is
// written once against it.
//
// Guarded is for the list that honours the invariant, which is every list the
// library itself builds, and costs it nothing to speak of: each dimension that
// enters a sum is first folded into a guard word, and the sums wrap. If no
// dimension was outside +-2^30 -- MAX_DIMEN, and the RUNNING sentinel beside
// it -- and the list is no longer than MAX_GUARDED_NODES, no sum can have left
// the range of a Scaled, the wrapping never happened, and the answer stands.
//
// Otherwise the answer is discarded and the walk is made again with Checked,
// which tests every addition and stops at the end of the range.

// Two terms a node at +-2^30 make a sum below 2^63 for this many nodes.
constexpr size_t MAX_GUARDED_NODES = size_t(1) << 30;

struct Guarded {
    uint64_t guard = 0;

    // A dimension on its way into a sum. Inside [-2^30, 2^30) it leaves the
    // bits from 31 up clear; outside, it sets one of them.
    Scaled term(Scaled value)
    {
        guard |= static_cast<uint64_t>(value) + (uint64_t(1) << 30);
        return value;
    }
    static Scaled plus(Scaled a, Scaled b)
    {
        return static_cast<Scaled>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
    }
    static Scaled minus(Scaled a, Scaled b)
    {
        return static_cast<Scaled>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
    }
    // Whether the answer cannot be vouched for.
    bool doubtful() const { return (guard >> 31) != 0; }
};

struct Checked {
    bool overflowed = false;

    static Scaled term(Scaled value) { return value; }
    Scaled plus(Scaled a, Scaled b) { return add_checked(a, b, overflowed); }
    Scaled minus(Scaled a, Scaled b) { return sub_checked(a, b, overflowed); }
};

// A glue node's stretch and shrink into the totals: the two assignments to
// total_stretch[o] and total_shrink[o] that sections 656 and 671 share. The
// orders are bytes of the node and are masked into range before they index.
template<class Arithmetic>
void add_glue(GlueTotal& stretch, GlueTotal& shrink, const Node& node, Arithmetic& arithmetic)
{
    Scaled& stretched = stretch.amounts[static_cast<unsigned>(node.stretch_order) & 3u];
    stretched = arithmetic.plus(stretched, arithmetic.term(node.b));
    Scaled& shrunk = shrink.amounts[static_cast<unsigned>(node.shrink_order) & 3u];
    shrunk = arithmetic.plus(shrunk, arithmetic.term(node.c));
}

// tex.web sections 657 to 667, "Determine the value of width(r) and the
// appropriate glue setting": decide the glue ratio, sign and order, and judge
// the result. One copy for both directions, because sections 672 to 678 are
// these with `height` for `width`. `excess` is what section 657 leaves in x.
//
// Departures, each the reference's:
//
//   - Sections 660, 666 and 667 report only when hbadness or hfuzz says to, and
//     so set last_badness only then for nothing to read. Here the badness and
//     the fitness are always answered.
//   - Section 664 leaves glue_sign at normal for an overfull box that has no
//     shrink at all, having found nothing to shrink, and then stores a ratio of
//     one. Here such a box is SHRINKING with a ratio of one. The two set the
//     same, there being no shrink for the ratio to multiply.
//   - The rule that section 666 appends to an overfull hbox (overfull_rule) is
//     not appended.
template<class Arithmetic>
void determine_glue_setting(Scaled natural, Scaled target, const GlueTotal& stretch,
                            const GlueTotal& shrink, bool is_empty, PackResult& result,
                            Arithmetic& arithmetic)
{
    Box& box = result.box;
    const Scaled excess = arithmetic.minus(target, natural);
    result.natural = natural;
    result.excess = excess;
    result.badness = 0;
    result.overfull_by = 0;
    box.glue_set = 0.0;
    box.glue_sign = GlueSign::NORMAL;
    box.glue_order = GlueOrder::NORMAL;

    if (excess == 0) {
        result.fitness = PackFitness::EXACT;
        return;
    }

    if (excess > 0) {
        const GlueOrder order = stretch.dominant_order();
        const Scaled available = stretch[order];
        box.glue_order = order;
        // Sections 658 and 659. A zero total is only reachable at NORMAL
        // order: nothing to stretch (section 658 then leaves the sign normal
        // and the ratio zero), and the contents simply do not fill the box.
        if (available != 0) {
            box.glue_sign = GlueSign::STRETCHING;
            box.glue_set = static_cast<double>(excess) / static_cast<double>(available);
        }
        if (is_empty) {
            result.fitness = PackFitness::EMPTY;
            return;
        }
        if (order != GlueOrder::NORMAL) {
            result.fitness = PackFitness::INFINITE_GLUE;
            return;
        }
        // Section 660, whose message says "Underfull" above 100 and "Loose"
        // otherwise.
        result.badness = badness(excess, stretch[GlueOrder::NORMAL]);
        result.fitness = result.badness > 100 ? PackFitness::UNDERFULL : PackFitness::LOOSE;
        return;
    }

    // Sections 664 and 665. `wanted` is section 664's -x.
    const Scaled wanted = arithmetic.minus(0, excess);
    const GlueOrder order = shrink.dominant_order();
    const Scaled available = shrink[order];
    box.glue_order = order;
    if (available != 0) {
        box.glue_sign = GlueSign::SHRINKING;
        box.glue_set = static_cast<double>(wanted) / static_cast<double>(available);
    }
    if (is_empty) {
        result.fitness = PackFitness::EMPTY;
        return;
    }
    if (order != GlueOrder::NORMAL) {
        result.fitness = PackFitness::INFINITE_GLUE;
        return;
    }
    if (available < wanted) {
        // Section 664's overfull case: the overflow is not distributed. The
        // glue shrinks as far as it can (a ratio of one) and the box sticks
        // out by the difference, which is the length section 666 reports.
        box.glue_set = 1.0;
        box.glue_sign = GlueSign::SHRINKING;
        result.fitness = PackFitness::OVERFULL;
        result.badness = OVERFULL_BADNESS;
        result.overfull_by = arithmetic.minus(wanted, available);
        return;
    }
    // Section 667.
    result.fitness = PackFitness::TIGHT;
    result.badness = badness(wanted, available);
}

void note(PackResult& result, PackError error)
{
    if (result.error == PackError::NONE)
        result.error = error;
}

// The walks fill a result the caller owns, so that whichever policy's answer
// stands is returned without being copied.

// tex.web sections 649 to 656, hpack's walk. `width`, `height` and `depth` are
// its x, h and d; `target` and `mode` its w and m.
//
// Departures: a character's dimensions come from `measure` and not from the
// font's char_info (section 654); there are no ligature, insert, mark, adjust,
// whatsit or unset nodes and no leaders, so sections 652 and 655 and the
// leader half of 656 have nothing to do; a rule of running width and a
// character with no measure are reported and left out, where section 653
// would add null_flag to the width.
template<class Arithmetic>
void hpack_with(const NodeArena& arena, NodeView nodes, Scaled target, PackMode mode,
                CharMeasure measure, Arithmetic& arithmetic, PackResult& result)
{
    // Plain locals, so that the three sizes stay in registers through the loop.
    Scaled width = 0;
    Scaled height = 0;
    Scaled depth = 0;
    GlueTotal stretch;
    GlueTotal shrink;
    target = arithmetic.term(target);

    for (const Node& node : nodes) {
        switch (node.type) {
        case NodeType::CHAR: {
            // Section 654.
            if (!measure) {
                note(result, PackError::CHAR_WITHOUT_MEASURE);
                break;
            }
            const CharDimens dimens = measure(node.font(), node.codepoint());
            width = arithmetic.plus(width, arithmetic.term(dimens.width));
            height = std::max(height, dimens.height);
            depth = std::max(depth, dimens.depth);
            break;
        }
        case NodeType::BOX: {
            const Box* const box = arena.find_box(node);
            if (box == nullptr) {
                note(result, PackError::NODE_NOT_IN_ARENA);
                break;
            }
            // Section 653: a box shifted down by s reaches height - s above
            // the baseline and depth + s below it.
            const Scaled shift = arithmetic.term(box->shift_amount);
            width = arithmetic.plus(width, arithmetic.term(box->width));
            height = std::max(height, arithmetic.minus(arithmetic.term(box->height), shift));
            depth = std::max(depth, arithmetic.plus(arithmetic.term(box->depth), shift));
            break;
        }
        case NodeType::RULE:
            if (node.a == RUNNING) {
                note(result, PackError::RUNNING_WIDTH_IN_HLIST);
                break;
            }
            width = arithmetic.plus(width, arithmetic.term(node.a));
            // Section 653 again, with no shift for a rule. A running height
            // or depth loses the maximum on its own, being hugely negative,
            // which is how that section handles it.
            height = std::max(height, node.b);
            depth = std::max(depth, node.c);
            break;
        case NodeType::GLUE:
            // Section 656.
            width = arithmetic.plus(width, arithmetic.term(node.a));
            add_glue(stretch, shrink, node, arithmetic);
            break;
        case NodeType::KERN:
        case NodeType::MATH_ON:
        case NodeType::MATH_OFF:
            // Section 651: a kern or a math node adds its width.
            width = arithmetic.plus(width, arithmetic.term(node.a));
            break;
        case NodeType::PENALTY:
        case NodeType::DISC:
            // Section 651's othercases: a discretionary contributes nothing;
            // the nodes it would replace follow it in the list and are
            // measured themselves.
            break;
        }
    }

    // Section 657: an additional target is added to the natural width.
    const Scaled final_width = mode == PackMode::EXACTLY ? target : arithmetic.plus(width, target);
    result.box.kind = BoxKind::HLIST;
    result.box.width = final_width;
    result.box.height = height;
    result.box.depth = depth;
    determine_glue_setting(width, final_width, stretch, shrink, nodes.empty(), result, arithmetic);
}

// tex.web sections 668 to 671, vpackage's walk. `width`, `height` and `depth`
// are its w, x and d; `target`, `mode` and `max_depth` its h, m and l.
//
// Departures: a character, and a rule of running height or depth, are reported
// and left out, where section 669 stops with confusion("vpack") for the first
// and section 670 would add null_flag to the height for the second; no
// whatsits and no leaders.
template<class Arithmetic>
void vpack_with(const NodeArena& arena, NodeView nodes, Scaled target, PackMode mode,
                Scaled max_depth, Arithmetic& arithmetic, PackResult& result)
{
    // Plain locals, so that the three sizes stay in registers through the loop.
    Scaled width = 0;
    Scaled height = 0;
    Scaled depth = 0;
    GlueTotal stretch;
    GlueTotal shrink;
    target = arithmetic.term(target);
    max_depth = arithmetic.term(max_depth);

    // Each item moves the height by the depth before it and by its own
    // height: two terms a node.
    for (const Node& node : nodes) {
        switch (node.type) {
        case NodeType::CHAR:
            note(result, PackError::CHAR_IN_VLIST);
            break;
        case NodeType::BOX: {
            const Box* const box = arena.find_box(node);
            if (box == nullptr) {
                note(result, PackError::NODE_NOT_IN_ARENA);
                break;
            }
            // Section 670.
            height = arithmetic.plus(height, arithmetic.plus(depth, arithmetic.term(box->height)));
            depth = arithmetic.term(box->depth);
            width = std::max(width, arithmetic.plus(arithmetic.term(box->width),
                                                    arithmetic.term(box->shift_amount)));
            break;
        }
        case NodeType::RULE:
            if (node.b == RUNNING || node.c == RUNNING) {
                note(result, PackError::RUNNING_HEIGHT_OR_DEPTH_IN_VLIST);
                break;
            }
            height = arithmetic.plus(height, arithmetic.plus(depth, arithmetic.term(node.b)));
            depth = arithmetic.term(node.c);
            // A running width loses the maximum on its own (section 670).
            width = std::max(width, node.a);
            break;
        case NodeType::GLUE:
            // Section 671.
            height = arithmetic.plus(height, arithmetic.plus(depth, arithmetic.term(node.a)));
            depth = 0;
            add_glue(stretch, shrink, node, arithmetic);
            break;
        case NodeType::KERN:
            // Section 669.
            height = arithmetic.plus(height, arithmetic.plus(depth, arithmetic.term(node.a)));
            depth = 0;
            break;
        case NodeType::PENALTY:
        case NodeType::DISC:
        case NodeType::MATH_ON:
        case NodeType::MATH_OFF:
            // Section 669's othercases: these do not occur in a proper
            // vertical list, and where they do they occupy no space.
            break;
        }
    }

    // Section 668: depth past the limit moves the reference point down.
    if (depth > max_depth) {
        height = arithmetic.plus(height, arithmetic.minus(depth, max_depth));
        depth = max_depth;
    }

    // Section 672.
    const Scaled final_height =
        mode == PackMode::EXACTLY ? target : arithmetic.plus(height, target);
    result.box.kind = BoxKind::VLIST;
    result.box.width = width;
    result.box.height = final_height;
    result.box.depth = depth;
    determine_glue_setting(height, final_height, stretch, shrink, nodes.empty(), result,
                           arithmetic);
}

} // namespace

PackResult hpack(const NodeArena& arena, NodeView nodes, Scaled target, PackMode mode,
                 CharMeasure measure)
{
    PackResult result;
    if (nodes.size() <= MAX_GUARDED_NODES) {
        Guarded guarded;
        hpack_with(arena, nodes, target, mode, measure, guarded, result);
        if (!guarded.doubtful())
            return result;
        result = PackResult();
    }
    Checked checked;
    hpack_with(arena, nodes, target, mode, measure, checked, result);
    if (checked.overflowed)
        note(result, PackError::DIMENSION_OVERFLOW);
    return result;
}

PackResult vpack(const NodeArena& arena, NodeView nodes, Scaled target, PackMode mode,
                 Scaled max_depth)
{
    PackResult result;
    if (nodes.size() <= MAX_GUARDED_NODES) {
        Guarded guarded;
        vpack_with(arena, nodes, target, mode, max_depth, guarded, result);
        if (!guarded.doubtful())
            return result;
        result = PackResult();
    }
    Checked checked;
    vpack_with(arena, nodes, target, mode, max_depth, checked, result);
    if (checked.overflowed)
        note(result, PackError::DIMENSION_OVERFLOW);
    return result;
}

// The glue half of tex.web's append_to_vlist (section 679). `deficiency` is
// its d: what the baselineskip lacks once the depth above and the height below
// are taken out of it.
bool interline_glue(Scaled prev_depth, const Box& box, const InterlineSkip& skip, Node& glue_out)
{
    if (prev_depth <= IGNORE_DEPTH)
        return false;
    // Saturating: there is no status to report through, and the vpack of the
    // list this glue goes into reports a sum that leaves the range. tex.web
    // subtracts unchecked.
    const Scaled deficiency =
        sub_saturating(sub_saturating(skip.baselineskip.width, prev_depth), box.height);
    if (deficiency < skip.lineskiplimit) {
        glue_out = glue(skip.lineskip);
        return true;
    }
    GlueSpec spec = skip.baselineskip;
    spec.width = deficiency;
    glue_out = glue(spec);
    return true;
}

// tex.web's append_to_vlist (section 679) for a box, and section 1056 for a
// rule. prev_depth is a field of the semantic nest there (section 212) and an
// argument and a result here.
Scaled append_to_vlist(const NodeArena& arena, std::vector<Node>& vlist, const Node& item,
                       Scaled prev_depth, const InterlineSkip& skip)
{
    if (item.type == NodeType::RULE) {
        vlist.push_back(item);
        return IGNORE_DEPTH;
    }
    if (!IMTYPESET_CONTRACT(item.type == NodeType::BOX,
                            "append_to_vlist: the item is neither a box nor a rule")) {
        vlist.push_back(item);
        return prev_depth;
    }
    // A box the arena does not hold goes in as it is, for vpack to report.
    const Box* const box = arena.find_box(item);
    if (box == nullptr) {
        vlist.push_back(item);
        return prev_depth;
    }
    Node above;
    if (interline_glue(prev_depth, *box, skip, above))
        vlist.push_back(above);
    vlist.push_back(item);
    return box->depth;
}

} // namespace ImTypeset::detail
