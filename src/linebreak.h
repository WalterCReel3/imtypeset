// Breaking a horizontal list into lines: the total-fit optimiser of tex.web
// sections 813-890, and a first-fit breaker. Mirrors advtext/linebreak.py.
//
// WHAT OF tex.web THIS IS. Parts 38 and 39, "Breaking paragraphs into lines"
// and its continuation (sections 813 to 890):
//
//   section 815        line_break                     LineBreaker::break_paragraph
//   section 816        the end of the list: penalty   prepare
//                      and par_fill_skip
//   section 817        very_loose_fit .. tight_fit    Fitness
//   section 819        the active node                Active
//   section 821        the passive node               Passive
//   sections 825-827   check_shrinkage, finite_shrink prepare (in part: below)
//   sections 829-861   try_break                      try_break
//   section 833        awful_bad, minimal_demerits,   AWFUL_BAD and try_break's
//                      minimum_demerits, best_place   locals of those names
//   sections 847-850   the paragraph's shape          ParShape
//   sections 851-853   the badness and fitness of a   classify (on what hpack
//                      line from r to cur_p           answered)
//   sections 854, 855  the last resort; recording a   try_break
//                      feasible break
//   section 859        the demerits of a line         demerits
//   section 863        the passes                     break_paragraph, run
//   sections 866-869   which nodes are legal          find_breakpoints
//                      breakpoints
//   sections 873, 874  the break at the end; the      find_breakpoints, run
//                      active node with fewest
//                      demerits
//   sections 877-889   post_line_break                post_line_break,
//                                                     build_line
//
// Parts 40 to 43 (sections 891 to 966, hyphenation) are not ported.
//
// Names that differ from tex.web's: break_paragraph is line_break (the
// reference's name, kept because other modules call it); a Breakpoint's
// `penalty` and `hyphenated` are try_break's two arguments, pi and break_type;
// `how_bad` is b. prepare, find_breakpoints,
// build_line, tabulate, trial, evaluate, run, chain and first_fit name things
// tex.web does in line or not at all.
//
// Total fit scores every legal breakpoint, keeps the breaks that could still
// lead somewhere active, and chooses the chain with the fewest total demerits.
// Demerits (section 859) are (line_penalty + badness) squared, plus the square
// of a penalty at the break, plus adj_demerits when adjacent lines differ by
// more than one fitness class.
//
// Every candidate line is evaluated by hpack, not by a private copy of the glue
// arithmetic, so the line the breaker scored and the line it emits cannot
// disagree. What hpack is handed is one of two things with the same totals.
// Where the paragraph allows it -- no discretionary, no rule of running width,
// a measure for its characters -- it is the candidate's natural width, stretch
// and shrink as differences of running totals over the prepared list, which
// costs the same however long the line is. Otherwise it is the candidate's
// nodes laid out in a scratch list exactly as the line would be built.
//
// WHERE THIS IS NOT tex.web. The reference's departures first; docs/
// divergences.md classifies each. Four of them are switches: under
// LineBreakParams::rules (src/tex_rules.h) the breaker does what the section
// says, and each entry below that has one names it. What is described is the
// reference's behaviour, which is the default.
//
//   - No active_width, no delta nodes, no break_width (sections 822 to 824,
//     832, 837 to 844, 860, 861). tex.web keeps the width from each active
//     node to the current position as running differences; here a candidate
//     line is measured when it is tried. One consequence is visible: section
//     837 discounts every discardable node after a break without knowing where
//     the line will end, so a line that ends inside that run of discardables
//     is judged there at a width it will not have (section 879 calls the case
//     anomalous); here it is judged as it will be built.
//   - No easy_line (sections 835, 848, 850). tex.web treats every line number
//     past the last specially shaped line as one class, and so keeps at most
//     one active node per fitness class for each breakpoint; here each line
//     number is its own class and a breakpoint reached in different numbers of
//     lines keeps a node for each. The fewest total demerits are the same.
//     What differs: which of several breakings with equal demerits is chosen
//     (below); and section 854's last resort, which is taken from the last
//     active node standing, here the one with the highest line number and not
//     necessarily the one with the fewest demerits. TexRules::easy_line gives
//     tex.web's classes and, with them, its order of the active list.
//   - Ties. tex.web links the active nodes a breakpoint creates in front of
//     the first node of the next class (section 845), so within a class the
//     newest come first, and with easy_line the unshaped lines are one class
//     in order of creation. Here active nodes are scored in order of line
//     number, and among equal line numbers in the order they became active
//     (survivors of earlier breakpoints before nodes the previous breakpoint
//     created). In both, a later candidate with demerits equal to the best so
//     far for its fitness class replaces it (section 855's `<=`), new active
//     nodes are created in fitness order, very loose first (section 836), and
//     the winner is the first active node with the fewest total demerits
//     (section 874) -- here that is fewest lines, then lowest fitness class.
//     So two breakings with exactly equal demerits can be resolved one way
//     there and the other way here. Under TexRules::easy_line the list is
//     tex.web's.
//   - No hyphenation. Section 863's second pass tries to hyphenate every word
//     (sections 891 on); here it is the first pass at a looser tolerance.
//   - Shrink. Section 827 makes left_skip and right_skip shrink-finite and
//     section 868 every glue of the list as it is passed, the par-fill glue
//     among them, reporting an error once (section 826). Here the glue of the
//     list handed in is made finite, silently, and left_skip, right_skip and
//     par_fill_skip go into each line as given. A line that holds infinite
//     shrink through a skip always fits. TexRules::skips_made_shrink_finite
//     makes the three finite, still silently.
//   - left_skip is put on a line unless it equals an all-zero spec in all five
//     fields. Section 887 leaves it out when it is zero_glue, which a glue
//     parameter is whenever its width, stretch and shrink are zero, whatever
//     their orders (section 1229). right_skip is always there, as in section
//     886. TexRules::left_skip_omitted_when_zero is section 887's test.
//   - After a break at a discretionary, discardable nodes at the start of the
//     next line are dropped. tex.web drops them only if the discretionary's
//     post-break list is empty (post_disc_break, sections 877 and 884; section
//     840 measures accordingly). TexRules::post_break_text_stops_pruning is
//     section 877's test. The DISC node itself is not carried into the line,
//     where section 882 leaves it there, emptied.
//   - No looseness (section 875), no club, widow, broken or interline
//     penalties (section 890), no adjustments or insertions moved out of the
//     lines (section 888), and nothing is appended to a vertical list: this
//     returns lines. Lines count from 1; tex.web's from prev_graf + 1 (section
//     864).
//   - ParShape::hanging is section 849 for a non-negative hang_after only.
//   - emergency_stretch is extra finite stretch on every candidate line of the
//     last pass and is absent when the chosen lines are packed. That is what
//     section 863 does by adding it to the background; it is listed because
//     the form differs (a glue node on the candidate) and not the result.
//   - The first-fit breaker has no counterpart in tex.web at all.
//
// This port's own, none of it in the reference either (decision D33): the
// work budget and the fall back to first fit (below); parameters clamped to
// LINE_BREAK_PARAM_LIMIT and demerits held in 64 bits and saturating, where
// tex.web's are 32-bit integers; a discretionary's replace_count bounded
// against the list; a node naming nothing in the arena replaced by an empty
// rule.
//
// The first-fit breaker fills each line until the next legal breakpoint would
// overfull it and never looks back. A line that is finished never changes when
// material is appended, which is what a paragraph that is still arriving needs,
// and its cost is linear where the optimiser's grows with its active list
// (ragged-right text, where every line that fits costs the same, is the bad
// case for the optimiser). It shares the breakpoint rules, the line-building
// and hpack with the optimiser.
//
// A paragraph always produces lines. A word wider than its line comes back as
// an overfull line, and the PackResult says so.
//
// The optimiser's work is bounded. It counts the candidate lines it judges, all
// passes together, and at LineBreaker's work budget it stops and the paragraph
// is broken by first fit instead: Paragraph::budget_exhausted is set and
// pass_used is FIRST_FIT. tex.web has no such bound (its three passes of
// section 863 run to the end). Without easy_line the active list grows with
// the number of line counts a breakpoint can be reached in, and the cost with
// it: a caller that is drawing a frame cannot wait out a quadratic. Under
// TexRules::easy_line the list is tex.web's and the cost is close to linear
// in the paragraph's length (docs/performance.md has both curves); the budget
// stays as the bound on what a list built to be expensive can cost. Below the
// budget nothing changes.
//
// Allocation. A LineBreaker owns every buffer the algorithm uses and keeps
// their capacity between calls, and a Paragraph keeps its vectors, so breaking
// paragraph after paragraph with the same two objects settles to no allocation
// at all apart from the line boxes added to the arena -- and none there either
// if the caller rewinds the arena to a mark.

#pragma once

#include "nodes.h"
#include "pack.h"
#include "tex_rules.h"
#include "units.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ImTypeset::detail
{

// tex.web's awful_bad (section 833), 2^30 - 1: the initial value of every
// demerit accumulator. A candidate whose total is past it is never recorded.
constexpr int64_t AWFUL_BAD = (int64_t(1) << 30) - 1;

// Parameters are clamped to this magnitude before use, so that no demerit sum
// can overflow 64 bits. Far above any value that changes an outcome: demerits
// past AWFUL_BAD already lose every comparison. Not tex.web's, whose integer
// parameters are whatever 32 bits hold.
constexpr int64_t LINE_BREAK_PARAM_LIMIT = int64_t(1) << 40;

// How a line was set, in the four classes of tex.web section 817:
// very_loose_fit, loose_fit, decent_fit and tight_fit, with its values. The
// values are arithmetic: section 859 charges adj_demerits when two adjacent
// lines' classes differ by more than one.
enum class Fitness : uint8_t { VERY_LOOSE = 0, LOOSE = 1, DECENT = 2, TIGHT = 3 };

// Which pass produced the paragraph: section 863's three, which tex.web tells
// apart by second_pass and final_pass. FIRST_FIT is none of them: the
// optimiser did not run, or ran out of budget (Paragraph::budget_exhausted).
enum class BreakPass : uint8_t { FIRST = 0, SECOND, EMERGENCY, FIRST_FIT };

// What sort of node a break happened at, which decides how the line ends
// (section 881): glue at the break becomes the right skip, a discretionary
// contributes its pre-break text, a kern or math node stays with its width
// zeroed, a penalty stays as it is. tex.web reads the type of the node at
// cur_break; PARAGRAPH_END is its cur_p = null.
enum class BreakKind : uint8_t { GLUE = 0, PENALTY, KERN, MATH, DISC, PARAGRAPH_END };

// The width available to one line and how far it is indented from the left:
// one pair of a \parshape (section 847), or tex.web's first_width and
// first_indent, second_width and second_indent. The indent becomes the line
// box's shift_amount (section 889); it is not glue.
struct LineSpec {
    Scaled width = 0;
    Scaled indent = 0;

    friend constexpr bool operator==(const LineSpec& a, const LineSpec& b)
    {
        return a.width == b.width && a.indent == b.indent;
    }
};

// Per-line widths (tex.web sections 847 to 850): a spec for each of the first
// n lines, the last of which applies to every line after it. tex.web holds
// the same in par_shape_ptr or in hang_indent and hang_after, and
// last_special_line is the n - 1 past which nothing changes.
//
// A value type that never allocates. The two common shapes are held in closed
// form; an explicit list is viewed, not copied, and must outlive the shape.
class ParShape
{
public:
    // A shape with no lines. Breaking against it is refused.
    ParShape() = default;

    // The same width for every line.
    static ParShape uniform(Scaled width, Scaled indent = 0);

    // Full width for the first `after` lines, then narrower by |indent| and
    // moved right by indent if it is positive: section 849 for a non-negative
    // hang_after. Its other half, a negative hang_after indenting the first
    // lines, is not offered: a negative `after` is a contract violation and is
    // taken as 0.
    static ParShape hanging(Scaled width, Scaled indent, int64_t after = 1);

    // An explicit list of `count` specs, which the caller keeps alive.
    static ParShape of(const LineSpec* lines, size_t count);

    // Whether the shape has no lines at all.
    bool empty() const { return _list == nullptr && !_closed_form; }

    // tex.web's last_special_line (section 848): the last line that is not
    // set to the width every later line has. Zero for a uniform shape and for
    // an empty one.
    int64_t last_special_line() const;

    // The spec for a line, counting from 1; past the end the last one repeats.
    // An empty shape, or a line below 1, answers a zero spec.
    LineSpec for_line(int64_t line) const;

private:
    const LineSpec* _list = nullptr;
    size_t _count = 0;
    bool _closed_form = false;
    int64_t _head_lines = 0;
    LineSpec _head;
    LineSpec _tail;
};

// The knobs of tex.web sections 236 (integer parameters), 247 (dimensions)
// and 224 (glue), under TeX's names, with plain.tex's values as defaults.
// tex.web itself gives them no values (section 240). A negative pretolerance
// skips the first pass (section 863).
//
// first_fit is not TeX's and switches the algorithm: with it set every field
// above left_skip is unread, except that a penalty or discretionary charge of
// INF_PENALTY or more still inhibits its break and EJECT_PENALTY or less still
// forces one.
struct LineBreakParams {
    int64_t pretolerance = 100;
    int64_t tolerance = 200;
    int64_t line_penalty = 10;
    int64_t adj_demerits = 10000;
    int64_t double_hyphen_demerits = 10000;
    int64_t final_hyphen_demerits = 5000;
    int64_t hyphen_penalty = 50;
    int64_t ex_hyphen_penalty = 50;
    Scaled emergency_stretch = 0;
    GlueSpec left_skip;
    GlueSpec right_skip;
    GlueSpec par_fill_skip = GlueSpec{0, 1, GlueOrder::FIL, 0, GlueOrder::NORMAL};
    bool first_fit = false;
    // Which of the reference's departures from tex.web are kept: all of them
    // unless said otherwise (src/tex_rules.h).
    TexRules rules;
};

// Why a paragraph could not be broken as asked.
enum class LineBreakError : uint8_t {
    NONE = 0,
    // The shape has no lines. No lines are produced.
    EMPTY_PARSHAPE,
    // hpack reported the list as malformed, or a BOX or DISC node names nothing
    // the arena holds (Paragraph::pack_error says how). Lines are still
    // produced; the offending nodes contribute nothing, and a node of the
    // second kind is an empty rule in its line.
    MALFORMED_LIST,
};

// One chosen breakpoint. `position` indexes the prepared list: the input with
// its glue made shrink-finite, a trailing glue node replaced by an infinite
// penalty (or one appended), and the par-fill glue appended. The last break of
// a paragraph is PARAGRAPH_END at the prepared list's length. What a passive
// node's cur_break points at (section 821).
struct Break {
    uint32_t position = 0;
    BreakKind kind = BreakKind::PARAGRAPH_END;
};

// One line: how packing it went, and the line as a BOX node of the arena, its
// shift_amount set to the line's indent. `packed.box` is that box's header
// (`contents` included).
//
// packed.fitness is the packer's diagnostic, not the breaker's Fitness class:
// TIGHT there means "shrank at all", Fitness::TIGHT means "shrank badly enough
// to score above 12". The box is tex.web's just_box (section 889).
struct BrokenLine {
    PackResult packed;
    Node box;
};

// A broken paragraph. total_demerits is the score the optimiser minimised
// (tex.web's fewest_demerits, section 874); a first-fit paragraph minimised
// nothing and reports 0.
struct Paragraph {
    std::vector<BrokenLine> lines;
    std::vector<Break> breaks; // one per line
    int64_t total_demerits = 0;
    BreakPass pass_used = BreakPass::FIRST;
    uint32_t prepared_length = 0;
    LineBreakError error = LineBreakError::NONE;
    PackError pack_error = PackError::NONE;
    // Total fit was asked for and gave up at the work budget; the lines are
    // first fit's. Not an error: the paragraph is whole.
    bool budget_exhausted = false;
    // How many times the pass that produced the lines took section 854's last
    // resort: a break made for free from the only active node left, no
    // feasible one having been found. Zero means every line the pass chose
    // between was feasible. Counted over the whole pass, so a free break need
    // not be on the chain that won. Zero for first fit.
    uint32_t artificial_breaks = 0;
};

// The default work budget of total fit, in candidate lines judged (a candidate
// that has to be laid out node by node costs more than one; see
// src/linebreak.cpp). docs/hardening.md has the measurements it was chosen
// from.
constexpr uint64_t DEFAULT_LINE_BREAK_WORK_BUDGET = uint64_t(1) << 20;

// The most a budget can be set to. Each unit of work makes at most four
// passive nodes, and they are indexed by a signed 32-bit number.
constexpr uint64_t MAX_LINE_BREAK_WORK_BUDGET = uint64_t(1) << 28;

// The line breaker and the buffers it works in. Reuse one across paragraphs.
// Not thread-safe; one breaker belongs to one thread at a time.
class LineBreaker
{
public:
    // Breaks `nodes` into lines against `shape`, writing the result to `out`
    // (whose previous contents are discarded) and the line boxes to `arena`,
    // which is also where the list's own BOX and DISC nodes live. `nodes` is
    // not modified and may view the arena. `measure` is needed only if the
    // list contains CHAR nodes.
    //
    // tex.web's line_break (section 815). Up to three passes (section 863),
    // stopping at the first that succeeds: pretolerance (skipped when
    // negative); tolerance; and, only if emergency_stretch is positive,
    // tolerance again with that much extra stretch on every line. On the last
    // pass a break is taken for free rather than leaving the paragraph
    // unbreakable (section 854).
    //
    // Returns whether out.error is NONE.
    bool break_paragraph(NodeArena& arena, NodeView nodes, const ParShape& shape,
                         const LineBreakParams& params, CharMeasure measure, Paragraph& out);

    // Whether candidate lines may be judged from running totals. On unless
    // turned off; off, every candidate is laid out and walked. The two give
    // the same paragraph, and a test turns it off to hold them to that.
    void use_running_totals(bool on) { _use_totals = on; }

    // How much work total fit may do on one paragraph before first fit takes
    // over. Zero means first fit always.
    // Capped at MAX_LINE_BREAK_WORK_BUDGET.
    void set_work_budget(uint64_t units)
    {
        _work_budget = units < MAX_LINE_BREAK_WORK_BUDGET ? units : MAX_LINE_BREAK_WORK_BUDGET;
    }
    uint64_t work_budget() const { return _work_budget; }
    // What the last paragraph cost, in the same units.
    uint64_t work_done() const { return _work; }

    // Whether the lines of the last paragraph broken begin without a left
    // skip node: the one statement of TexRules::left_skip_omitted_when_zero
    // (section 887), for the breaker and for whoever indexes its lines.
    bool left_skip_omitted() const;

private:
    // tex.web's passive node (section 821), under its field names: a chosen
    // breakpoint and the one before it. `prev_break` indexes _passive; -1 is
    // the paragraph's beginning, tex.web's null. `kind` is what tex.web reads
    // off the node at cur_break. Its `serial` is for tracing and is not kept.
    struct Passive {
        uint32_t cur_break = 0;
        BreakKind kind = BreakKind::PARAGRAPH_END;
        int32_t prev_break = -1;
    };

    // tex.web's active node (section 819), under its field names: a breakpoint
    // that could still begin a line. line_number is the line that starts here;
    // fitness and hyphenated describe the line that ended here (`hyphenated`
    // is the node's type there, hyphenated or unhyphenated). break_node
    // indexes _passive. There is no link: the active list is two vectors.
    struct Active {
        int32_t break_node = -1;
        int64_t line_number = 1;
        Fitness fitness = Fitness::DECENT;
        bool hyphenated = false;
        int64_t total_demerits = 0;
    };

    // A legal breakpoint: the arguments of try_break (section 829), which
    // calls them pi and break_type, and cur_p, which is global there.
    struct Breakpoint {
        int64_t penalty = 0;
        bool hyphenated = false;
        uint32_t position = 0;
        BreakKind kind = BreakKind::PARAGRAPH_END;
    };

    // One pass's settings: tex.web's globals threshold and final_pass
    // (section 828), and the emergency stretch section 863 adds to the
    // background for the last pass.
    struct Pass {
        int64_t threshold = 0;
        Scaled extra_stretch = 0;
        bool final_pass = false;
        BreakPass which = BreakPass::FIRST;
    };

    // What hpack sums over nodes [0, i) of the prepared list. Unsigned, so that
    // a difference of two entries is the sum of the range between them
    // whenever that sum is itself representable, whatever the entries did.
    //
    // This stands where tex.web has active_width, cur_active_width and the
    // delta nodes (sections 822 to 824): the same six quantities, one width and
    // the stretch of each order -- and the shrink of each order where tex.web
    // needs only one, its shrink being finite by then -- held as totals from
    // the start of the list and not as differences between active nodes.
    struct Totals {
        uint64_t width = 0;
        uint64_t stretch[4] = {0, 0, 0, 0};
        uint64_t shrink[4] = {0, 0, 0, 0};
    };

    void prepare(const NodeArena& arena, NodeView nodes);
    void find_breakpoints(const NodeArena& arena);
    bool tabulate(const NodeArena& arena);
    PackResult trial(const NodeArena& arena, bool has_previous, uint32_t previous_position,
                     BreakKind previous_kind, uint32_t position, BreakKind kind,
                     Scaled extra_stretch, Scaled target);
    void build_line(const NodeArena& arena, bool has_previous, uint32_t previous_position,
                    BreakKind previous_kind, uint32_t position, BreakKind kind);
    PackResult evaluate(const NodeArena& arena, int32_t previous_passive, uint32_t position,
                        BreakKind kind, int64_t line, const Pass& pass);
    int64_t demerits(int64_t how_bad, int64_t pi, bool hyphenated, bool at_end, Fitness fit,
                     const Active& previous) const;
    void try_break(const NodeArena& arena, const Breakpoint& breakpoint, const Pass& pass);
    void try_break_by_class(const NodeArena& arena, const Breakpoint& breakpoint, const Pass& pass);
    bool run(const NodeArena& arena, const Pass& pass, Active& winner);
    void first_fit(const NodeArena& arena);
    void chain(const Active& winner);
    void post_line_break(NodeArena& arena, Paragraph& out);
    void note(const PackResult& result);
    const Disc& disc_of(const NodeArena& arena, const Node& node);

    // The paragraph being broken. Valid for the duration of one call.
    LineBreakParams _params;
    ParShape _shape;
    CharMeasure _measure;
    PackError _pack_error = PackError::NONE;

    std::vector<Node> _body;              // the prepared list
    std::vector<Breakpoint> _breakpoints; // every legal breakpoint, in order
    std::vector<Node> _line;              // one candidate line
    // The running totals, one entry more than the prepared list, and for each
    // position the first one at or after it that a break does not discard.
    // Meaningful while _tabulated.
    std::vector<Totals> _totals;
    std::vector<uint32_t> _resume;
    bool _use_totals = true;
    bool _tabulated = false;
    uint64_t _work = 0;
    uint64_t _work_budget = DEFAULT_LINE_BREAK_WORK_BUDGET;
    uint32_t _artificial_breaks = 0;
    std::vector<Passive> _passive;
    // The active list is kept as its two runs, each in line-number order: what
    // survived the previous breakpoint and what it created. The reference's
    // list is their concatenation, and its stable sort by line number is their
    // merge. tex.web's is one linked list (section 819) with new nodes linked
    // in ahead of the next class (section 845), which is a different order
    // among equal line numbers: see the top of this file. Under
    // TexRules::easy_line the list is tex.web's, whole and in its order, in
    // _survivors, and _created stays empty.
    int64_t _easy_line = 0;
    std::vector<Active> _survivors;
    std::vector<Active> _created;
    std::vector<Active> _ordered;
    std::vector<Break> _chosen;
};

// Breaks one paragraph with a breaker of its own. For a caller that breaks many,
// keep a LineBreaker and a Paragraph and call the member.
bool break_paragraph(NodeArena& arena, NodeView nodes, const ParShape& shape,
                     const LineBreakParams& params, CharMeasure measure, Paragraph& out);

} // namespace ImTypeset::detail
