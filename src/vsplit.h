// vsplit: cutting a vertical list at the best place for a given height, and
// doing it again and again to set one list as columns. Mirrors
// advtext/columns.py (`vert_break`, `_break_badness`, `prune_top`, `vsplit`,
// `split_into_columns`).
//
// WHAT OF tex.web THIS IS. The first half of part 44, "Breaking vertical lists
// into pages" (sections 967 to 979), which is \vsplit and the two subroutines
// it is made of:
//
//   sections 968, 969  prune_page_top: glue, kerns and     prune_page_top
//                      penalties before the first box
//                      deleted; the \splittopskip glue
//   section 970        vert_break: the scan and its        VertBreak::begin,
//                      variables; active_height            VertBreak::scan
//   section 971        best_height_plus_depth              VertBreak::
//                                                          best_height_plus_depth
//   section 972        the end of the list as a forced     VertBreak::scan
//                      break; the depth limit applied
//                      after every node
//   section 973        which nodes are legal breakpoints   VertBreak::scan
//   section 974        deplorable; the cost of a break,    DEPLORABLE,
//                      the champion, when to stop          VertBreak::scan
//   section 975        the badness of the page so far      break_badness
//   section 976        glue and kerns added to the         VertBreak::scan
//                      heights
//   section 977        vsplit                              vsplit
//
// awful_bad is section 833's, and is src/linebreak.h's AWFUL_BAD: section 970
// borrows the line breaker's six distances under another name
// (active_height), and its sentinel with them.
//
// WHERE THIS IS NOT tex.web, because the reference is not (docs/divergences.md,
// "columns-core", has the list):
//
//   - There are no marks, insertions or whatsits, so section 979 (the marks of
//     the part split off) and the arms of sections 968 and 973 for those nodes
//     have nothing to do. A node that cannot be in a vertical list (a
//     character, a discretionary, a math bracket), where tex.web stops with
//     confusion("vertbreak") or confusion("pruning") and the reference raises,
//     is left out of the sums and named in an error: such a list reaches here
//     from data.
//   - vsplit takes a list and not a box register, so section 978's void and
//     hbox cases do not arise; what is left is answered pruned and is NOT
//     packed again (section 977 ends with vpack(q, natural) into the register).
//   - Infinite shrink in the list (section 976) is an error message there, and
//     the glue is made finite. Here it is made finite and nothing is said:
//     its shrink is added whatever its order.
//   - tex.web has no columns: \vsplit is a primitive (section 1082 is where
//     the command reaches it) and columns are what a macro package builds from
//     it. split_into_columns is the reference's loop: the list pruned once
//     before the first column -- which is section 1001's computation, the page
//     builder's \topskip, made with \splittopskip -- then vsplit `count`
//     times, and what is left over answered. There is no balancing in it.
//
// A list is cut where it is. A column's list is one new glue node followed by
// what is left of the material, and the caller keeps a spare node in front of
// the material for it (ColumnList): nothing is copied to make the next
// column's list, and a column's box holds a span of the caller's storage.
//
// VertBreak is the scan of section 970 with its variables kept between calls,
// so that it can be fed a list that is still being made: a document's vertical
// list is made as far as a column needs and no further (src/blocks.h,
// "COLUMNS").
//
// Not here: the page builder (sections 980 to 1028), \topskip as its own
// parameter, \vsize, insertions, output routines; balancing, which is
// src/blocks.h's and is a search over this.

#pragma once

#include "badness.h"
#include "linebreak.h"
#include "nodes.h"
#include "pack.h"
#include "units.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ImTypeset::detail
{

// tex.web's deplorable (section 974): the cost of a break whose badness is
// already infinite. More than INF_BAD and less than AWFUL_BAD, so that a break
// which is merely hopeless still beats one that cannot be taken, and a list
// with no good break in it is cut somewhere.
constexpr int64_t DEPLORABLE = 100000;

// The reference's \splittopskip: 10 pt, plain.tex's value, and rigid.
constexpr GlueSpec SPLIT_TOP_SKIP = {10 * UNITY, 0, GlueOrder::NORMAL, 0, GlueOrder::NORMAL};

enum class VsplitError : uint8_t {
    NONE = 0,
    // A character, a discretionary or a math bracket in the list. It takes no
    // part in the heights and is not a breakpoint.
    NOT_VERTICAL,
    // A rule whose height or depth is RUNNING: tex.web section 138 says which
    // dimensions may run in which list. It adds nothing to the heights.
    RUNNING_RULE,
    // A BOX node names nothing the arena holds. It adds nothing.
    NODE_NOT_IN_ARENA,
};

// Why a scan ended.
enum class ScanStop : uint8_t {
    // At the end of the list, which is a forced break (section 972).
    END = 0,
    // At a penalty of EJECT_PENALTY or less.
    EJECT,
    // At a breakpoint the page was already too full for (section 974's
    // b = awful_bad).
    OVERFULL,
};

// One legal breakpoint a scan reached, for a reader that wants to know why the
// cut is where it is (the goldens' `cand`). The totals are those before the
// node at `index` is added.
struct BreakCandidate {
    size_t index = 0;
    Scaled penalty = 0;
    Scaled cur_height = 0;
    Scaled prev_dp = 0;
    GlueTotal stretch;
    Scaled shrink = 0;
    int64_t badness = 0;
    int64_t cost = 0;
    bool taken = false;
};

// Section 975: how bad it is to end a page of height `h` when the list so far
// is `cur_height` tall with `stretch` and `shrink` in it. Zero for a short
// page with any infinite stretch; AWFUL_BAD for one too tall for its shrink.
int64_t break_badness(Scaled cur_height, Scaled h, const GlueTotal& stretch, Scaled shrink);

// What prune_page_top makes of a list.
struct Pruned {
    // The first box or rule: everything before it is deleted. The list's
    // length if there is none, and then the list prunes to nothing.
    size_t start = 0;
    // Whether there is a box or rule, and so a glue node to go before it.
    bool head = false;
    // That glue: the \splittopskip with its width reduced by the height of
    // the box at `start`, and never below zero.
    Node glue;
    VsplitError error = VsplitError::NONE;
};

// tex.web's prune_page_top (sections 968, 969) as a statement about `nodes`:
// where what survives begins, and the glue that goes in front of it. The glue
// puts the first baseline \splittopskip below the top, "whenever this is
// possible without backspacing". Nothing is written.
Pruned prune_page_top(const NodeArena& arena, NodeView nodes,
                      const GlueSpec& split_top_skip = SPLIT_TOP_SKIP);

// tex.web's vert_break (sections 970 to 976): the best place to cut a vertical
// list for a page of height h with depth limit d.
//
// `begin`, then `scan` until it answers true. Each call goes on from where the
// one before stopped, over the same list grown longer: a list handed over with
// `complete` false is scanned as far as can be decided without knowing what
// follows it (a kern at its very end waits, since whether it is a breakpoint is
// the next node's type), and `scan` answers false for "more is needed".
class VertBreak
{
public:
    // Section 970's initialisation. `h` and `d` are tex.web's.
    void begin(Scaled h, Scaled d = MAX_DIMEN);
    // Gives the list's first node apart from the rest, straight after
    // `begin`: the glue prune_page_top made, for a caller that cannot write
    // it in front of the material. `scan` is then handed the list from its
    // second node on, and every index it answers still counts the glue as
    // node 0.
    void head(const Node& glue_node);

    // True when the scan is over: `best_place` is where to cut. `trace`, if
    // given, has one entry appended per legal breakpoint reached.
    bool scan(const NodeArena& arena, NodeView nodes, bool complete,
              std::vector<BreakCandidate>* trace = nullptr);

    bool done() const { return _done; }
    // How many nodes have been gone over: the next call starts there.
    size_t position() const { return _next; }

    // Section 974's best_place, as an index: the node the cut falls AT, which
    // is glue, a kern or a penalty that then heads what is left; or the
    // list's length, tex.web's null, for "all of it".
    size_t best_place = 0;
    int64_t least_cost = AWFUL_BAD;
    // Section 971: the natural size of the part before best_place, its last
    // box's depth included.
    Scaled best_height_plus_depth = 0;
    ScanStop stop = ScanStop::END;
    VsplitError error = VsplitError::NONE;

private:
    Scaled _h = 0;
    Scaled _d = MAX_DIMEN;
    // active_height[1], [2..5] and [6].
    Scaled _cur_height = 0;
    GlueTotal _stretch;
    Scaled _shrink = 0;
    Scaled _prev_dp = 0;
    size_t _next = 0;
    // 1 when node 0 was given through `head`, and `scan` sees node 1 first.
    size_t _apart = 0;
    bool _done = false;
};

// One scan over a whole list.
struct VertBreakResult {
    size_t best_place = 0;
    int64_t least_cost = AWFUL_BAD;
    Scaled best_height_plus_depth = 0;
    ScanStop stop = ScanStop::END;
    VsplitError error = VsplitError::NONE;
};
VertBreakResult vert_break(const NodeArena& arena, NodeView nodes, Scaled h, Scaled d = MAX_DIMEN,
                           std::vector<BreakCandidate>* trace = nullptr);

// What vsplit answers.
struct Vsplit {
    VertBreakResult cut;
    // The part before the cut packed `exactly` to the height (section 977's
    // vpackage(p, h, exactly, split_max_depth)). `packed.box.contents` is not
    // set: the contents are nodes[0, cut.best_place).
    PackResult packed;
    // What is left: nodes[cut.best_place..] pruned, with `rest.start` an
    // index into `nodes`.
    Pruned rest;
};

// tex.web's vsplit (section 977) over a list: one page of height `h` off the
// front of `nodes`, and what is left.
Vsplit vsplit(const NodeArena& arena, NodeView nodes, Scaled h,
              const GlueSpec& split_top_skip = SPLIT_TOP_SKIP, Scaled split_max_depth = MAX_DIMEN,
              std::vector<BreakCandidate>* trace = nullptr);

// A vertical list with room in front of it: nodes[0] is a spare, and the
// material is nodes[1..]. A column's list is the glue prune_page_top made,
// written into the node before the column's first box, followed by the rest
// of the material; that node is the spare for the first column and, for every
// other, a node the cut before it deleted.
struct ColumnList {
    std::vector<Node> nodes;

    void clear()
    {
        nodes.clear();
        nodes.push_back(penalty(0));
    }
    // Replaces the material.
    void assign(NodeView material)
    {
        clear();
        nodes.insert(nodes.end(), material.begin(), material.end());
    }
    size_t material_size() const { return nodes.empty() ? 0 : nodes.size() - 1; }
};

// One column of a split.
struct SplitColumn {
    // Whether the column was handed anything: a glue node and then the
    // material from `start` (an index into the material). With nothing left,
    // `start` is the material's length.
    bool head = false;
    size_t start = 0;
    Node glue;
    // The cut, counted in the list the column was handed (the glue is 0).
    VertBreakResult cut;
    PackResult packed;
    // The column's contents in ColumnList::nodes: `count` nodes from `first`.
    size_t first = 0;
    size_t count = 0;
    // The legal breakpoints of its scan: ColumnSplit::candidates
    // [candidate_first, candidate_end).
    size_t candidate_first = 0;
    size_t candidate_end = 0;
};

// What split_into_columns produced.
struct ColumnSplit {
    std::vector<SplitColumn> columns;
    // What the columns did not hold, pruned like a column's list: whether
    // there is anything, where in the material it starts, its glue, and how
    // many nodes it is with the glue.
    bool overflow_head = false;
    size_t overflow_start = 0;
    Node overflow_glue;
    size_t overflow_count = 0;
    // Filled only when `tracing` is set before the call.
    bool tracing = false;
    std::vector<BreakCandidate> candidates;
    VsplitError error = VsplitError::NONE;

    bool fits() const { return !overflow_head; }
};

// The reference's `split_into_columns`: `list`'s material flowed into `count`
// columns of `height`, in reading order, filled one after another; the last is
// as short as it turns out, and columns past the end of the material are
// empty and as tall as the rest. The first column's head is pruned too.
// `list.nodes` is written (the glue nodes) and is what the columns' contents
// are spans of. False, with nothing done, for no columns or a height that is
// not positive.
bool split_into_columns(const NodeArena& arena, ColumnList& list, Scaled height, size_t count,
                        ColumnSplit& out, const GlueSpec& split_top_skip = SPLIT_TOP_SKIP,
                        Scaled split_max_depth = MAX_DIMEN);

} // namespace ImTypeset::detail
