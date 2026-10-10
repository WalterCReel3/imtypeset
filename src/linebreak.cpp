// See linebreak.h for the map of tex.web's parts 38 and 39 onto this file and
// for where it departs from them. Section numbers are tex.web's.

#include "linebreak.h"

#include "badness.h"

#include <algorithm>
#include <limits>

namespace ImTypeset::detail
{

namespace
{

// What (line_penalty + badness) squared becomes when the sum reaches 10000 in
// magnitude (tex.web section 859, which writes the number as a literal).
// Replaced, not clamped: the section assigns this value in place of the square
// and does not cap the square at it.
constexpr int64_t SATURATED_DEMERITS = 100000000;

int64_t clamp_param(int64_t value)
{
    return std::max(-LINE_BREAK_PARAM_LIMIT, std::min(LINE_BREAK_PARAM_LIMIT, value));
}

// |value| for a value already inside +-LINE_BREAK_PARAM_LIMIT or a dimension:
// tex.web's abs, defined for the one 64-bit value whose negation is not.
int64_t magnitude(int64_t value)
{
    if (value == std::numeric_limits<int64_t>::min())
        return std::numeric_limits<int64_t>::max();
    return value < 0 ? -value : value;
}

// Whether a node is discarded from the start of a line after a break (tex.web
// section 879's loop, which has no name there): glue, penalties, math nodes
// and explicit kerns. A kern that is not explicit is kept, as sections 837
// and 879 both have it, which is why this is not is_discardable.
bool is_prunable(const Node& node)
{
    switch (node.type) {
    case NodeType::KERN:
        return static_cast<KernKind>(node.kind) == KernKind::EXPLICIT;
    case NodeType::GLUE:
    case NodeType::PENALTY:
    case NodeType::MATH_ON:
    case NodeType::MATH_OFF:
        return true;
    default:
        return false;
    }
}

// How many nodes after a discretionary its break replaces: its replace_count
// (section 145), bounded. The count comes from whoever built the list; a
// negative one (on which the reference never terminates, and which tex.web's
// quarterword cannot hold) is taken as zero. One past the end of the list is
// harmless: every use below is bounded by a position in the list.
uint64_t replaced_after(const Disc& disc)
{
    return disc.replace_count > 0 ? static_cast<uint64_t>(disc.replace_count) : 0;
}

const Disc EMPTY_DISC{};

// One unit of the total-fit work budget is one candidate line judged from the
// running totals. A candidate that has to be laid out and walked costs in
// proportion to its length: this many of its nodes to the unit. The budget is
// this port's own (decision D33); tex.web has none.
constexpr uint64_t NODES_PER_WORK_UNIT = 16;

// tex.web's b and fit_class (section 851) for one candidate line.
struct Verdict {
    int64_t how_bad = 0;
    Fitness fit = Fitness::DECENT;
};

// A packed candidate line as tex.web sections 851 to 853 judge it. Infinite
// stretch is badness 0 and decent whatever the numbers say; finite stretch is
// very loose above 99 and loose above 12 (section 852); shrink is tight above
// 12, and a line needing more shrink than exists scores inf_bad + 1, one past
// the largest feasible badness, which is what gets its active node deactivated
// (section 853).
//
// The sections compute these from cur_active_width; here they are read off
// what hpack answered for the candidate, which calls badness with the same two
// arguments. Where the two differ:
//
//   - Section 852 tests for a large shortfall against little stretch before it
//     calls badness. The test is not made here: badness answers inf_bad for
//     exactly those arguments, so it changes no result.
//   - A line that fits exactly is decent with badness 0. Section 851 sends a
//     shortfall of zero to section 853, which answers the same unless the
//     line's total shrink is negative, when it answers inf_bad + 1.
//   - A line whose shrink is infinite is decent with badness 0. Section 853
//     never meets one, sections 827 and 868 having made all shrink finite;
//     here a skip can carry it in (see linebreak.h).
Verdict classify(const PackResult& result)
{
    Verdict verdict;
    if (result.excess > 0) {
        if (result.box.glue_order != GlueOrder::NORMAL)
            return verdict;
        verdict.how_bad = result.badness;
        if (verdict.how_bad > 99)
            verdict.fit = Fitness::VERY_LOOSE;
        else if (verdict.how_bad > 12)
            verdict.fit = Fitness::LOOSE;
        return verdict;
    }
    if (result.fitness == PackFitness::OVERFULL) {
        verdict.how_bad = INF_BAD + 1;
        verdict.fit = Fitness::TIGHT;
        return verdict;
    }
    verdict.how_bad = result.badness;
    verdict.fit = verdict.how_bad > 12 ? Fitness::TIGHT : Fitness::DECENT;
    return verdict;
}

} // namespace

// --- ParShape ---------------------------------------------------------------------

ParShape ParShape::uniform(Scaled width, Scaled indent)
{
    ParShape shape;
    shape._closed_form = true;
    shape._head_lines = 0;
    shape._tail = LineSpec{width, indent};
    return shape;
}

ParShape ParShape::hanging(Scaled width, Scaled indent, int64_t after)
{
    if (!IMTYPESET_CONTRACT(after >= 0, "ParShape::hanging: negative hang_after"))
        after = 0;
    ParShape shape;
    shape._closed_form = true;
    shape._head_lines = after;
    shape._head = LineSpec{width, 0};
    shape._tail = LineSpec{sub_saturating(width, magnitude(indent)), indent > 0 ? indent : 0};
    return shape;
}

ParShape ParShape::of(const LineSpec* lines, size_t count)
{
    ParShape shape;
    if (lines != nullptr && count != 0) {
        shape._list = lines;
        shape._count = count;
    }
    return shape;
}

LineSpec ParShape::for_line(int64_t line) const
{
    if (line < 1)
        return LineSpec{};
    if (_list != nullptr) {
        const uint64_t wanted = static_cast<uint64_t>(line);
        const size_t index = wanted < _count ? static_cast<size_t>(wanted) : _count;
        return _list[index - 1];
    }
    if (_closed_form)
        return line <= _head_lines ? _head : _tail;
    return LineSpec{};
}

// Section 848: one less than the number of pairs of a \parshape, and
// hang_after for a hanging indentation.
int64_t ParShape::last_special_line() const
{
    if (_list != nullptr)
        return static_cast<int64_t>(_count - 1);
    return _closed_form ? _head_lines : 0;
}

// --- The prepared list and its breakpoints ----------------------------------------

// tex.web section 816: a trailing glue node is replaced by an infinite
// penalty, not kept -- it would be a legal breakpoint that makes an empty last
// line -- and anything else gets the penalty appended. Then the par-fill glue
// goes on the end, where the penalty before it stops it being a breakpoint.
//
// Glue in the paragraph is made shrink-finite here, all at once, which is
// sections 825 and 826's finite_shrink applied where section 868 applies it
// node by node as the list is scanned. Three departures, the reference's: the
// order is reset whether or not the shrink is zero (check_shrinkage leaves a
// zero shrink alone); nothing is reported (section 826 issues an error, once
// a paragraph); and the par-fill glue appended below, and the left and right
// skips section 827 treats, are not touched here. Under
// TexRules::skips_made_shrink_finite break_paragraph has made those three
// finite before this runs.
//
// A BOX or DISC node naming nothing `arena` holds is not carried into the
// lines, which could not be stored with it: an empty rule stands where it was,
// so positions keep their meaning, and the paragraph is reported as malformed.
void LineBreaker::prepare(const NodeArena& arena, NodeView nodes)
{
    size_t count = nodes.size();
    const size_t room = static_cast<size_t>(std::numeric_limits<uint32_t>::max()) - 2;
    if (!IMTYPESET_CONTRACT(count <= room, "break_paragraph: the list is too long"))
        count = room;

    _body.clear();
    _body.reserve(count + 2);
    for (size_t i = 0; i < count; ++i) {
        Node node = nodes[i];
        if (node.type == NodeType::GLUE) {
            node.shrink_order = GlueOrder::NORMAL;
        } else if ((node.type == NodeType::BOX && arena.find_box(node) == nullptr) ||
                   (node.type == NodeType::DISC && arena.find_disc(node) == nullptr)) {
            node = rule(0, 0, 0);
            if (_pack_error == PackError::NONE)
                _pack_error = PackError::NODE_NOT_IN_ARENA;
        }
        _body.push_back(node);
    }
    if (!_body.empty() && _body.back().type == NodeType::GLUE)
        _body.back() = penalty(INF_PENALTY);
    else
        _body.push_back(penalty(INF_PENALTY));
    _body.push_back(glue(_params.par_fill_skip));
}

// Every legal breakpoint of the prepared list, in order, ending with the
// paragraph's own end (section 873). Shared by both breakers, so "where may a
// line end" has one answer.
//
// This is the main loop of tex.web's line_break (section 866) with the calls
// of try_break collected and not made: glue is section 868, an explicit kern
// and a math node are section 866's kern_break, a discretionary is section
// 869. `previous_index` is its prev_p. What that loop also does as it goes,
// adding each node's width to active_width (sections 866 to 871), has no
// counterpart: a candidate is measured when it is tried.
//
// auto_breaking goes off at a MATH_ON and back on at a MATH_OFF, so glue inside
// a formula is never a breakpoint. A penalty is listed whatever its value;
// inhibiting and forcing belong to whoever takes the break (section 831).
void LineBreaker::find_breakpoints(const NodeArena& arena)
{
    _breakpoints.clear();
    const size_t count = _body.size();
    bool auto_breaking = true;
    // Section 863: glue in the very first position sees itself as its
    // predecessor and is not a breakpoint.
    size_t previous_index = 0;
    size_t index = 0;
    while (index < count) {
        const Node& node = _body[index];
        const bool followed_by_glue = index + 1 < count && _body[index + 1].type == NodeType::GLUE;
        Breakpoint here;
        here.position = static_cast<uint32_t>(index);
        switch (node.type) {
        case NodeType::GLUE:
            if (auto_breaking && permits_break_at_following_glue(_body[previous_index])) {
                here.kind = BreakKind::GLUE;
                _breakpoints.push_back(here);
            }
            break;
        case NodeType::KERN:
            if (static_cast<KernKind>(node.kind) == KernKind::EXPLICIT && auto_breaking &&
                followed_by_glue) {
                here.kind = BreakKind::KERN;
                _breakpoints.push_back(here);
            }
            break;
        case NodeType::MATH_ON:
        case NodeType::MATH_OFF:
            auto_breaking = node.type == NodeType::MATH_OFF;
            if (auto_breaking && followed_by_glue) {
                here.kind = BreakKind::MATH;
                _breakpoints.push_back(here);
            }
            break;
        case NodeType::PENALTY:
            here.penalty = node.a;
            here.kind = BreakKind::PENALTY;
            _breakpoints.push_back(here);
            break;
        case NodeType::DISC: {
            const Disc& disc = disc_of(arena, node);
            here.penalty =
                disc.pre_break.count != 0 ? _params.hyphen_penalty : _params.ex_hyphen_penalty;
            here.hyphenated = true;
            here.kind = BreakKind::DISC;
            _breakpoints.push_back(here);
            // Section 869: empty pre-break material is an explicit hyphen's
            // penalty, anything else the ordinary one. The nodes the break
            // would replace are stepped over unseen.
            previous_index = index;
            const uint64_t skip = 1 + replaced_after(disc);
            index = skip < count - index ? index + static_cast<size_t>(skip) : count;
            continue;
        }
        case NodeType::CHAR:
        case NodeType::BOX:
        case NodeType::RULE:
            break;
        }
        previous_index = index;
        ++index;
    }

    // Section 873: the end of a paragraph is a forced break, and counts as
    // hyphenated (section 829 says so) so that final_hyphen_demerits can be
    // charged.
    Breakpoint end;
    end.penalty = EJECT_PENALTY;
    end.hyphenated = true;
    end.position = static_cast<uint32_t>(count);
    end.kind = BreakKind::PARAGRAPH_END;
    _breakpoints.push_back(end);
}

// The discretionary a DISC node of the prepared list names. One the arena does
// not hold is an empty one, and the paragraph is reported as malformed.
const Disc& LineBreaker::disc_of(const NodeArena& arena, const Node& node)
{
    const Disc* const disc = arena.find_disc(node);
    if (disc != nullptr)
        return *disc;
    if (_pack_error == PackError::NONE)
        _pack_error = PackError::NODE_NOT_IN_ARENA;
    return EMPTY_DISC;
}

// The horizontal list for the line running from one breakpoint to the next,
// into _line. This is the part of tex.web's post_line_break that makes one
// line's list (sections 879 to 887), written so that a candidate can be built
// the same way as a chosen line.
//
// At the start: a discretionary's post-break text is prepended and the nodes
// it replaces are skipped (sections 882 to 884), then leading discardables are
// pruned (section 879), stopping at the next breakpoint so that a line is
// never pruned out of existence. Then the left skip, unless it is the all-zero
// spec (section 887).
//
// At the end (section 881): glue at the break becomes the right skip and is
// not kept before it; a discretionary contributes its pre-break text (section
// 885); a kern or math node keeps its place with its width zeroed; a penalty
// stays and occupies nothing. The right skip follows (section 886).
//
// Three departures, the reference's, two of them switches (src/tex_rules.h):
// the pruning is done after a break at a discretionary too, where section 877
// skips it if the discretionary had post-break material
// (post_break_text_stops_pruning); the DISC node is not carried into the line,
// where section 882 keeps it, emptied; and the left skip is left out on
// equality with the default spec in every field, where section 887 leaves it
// out when its width, stretch and shrink are zero
// (left_skip_omitted_when_zero).
void LineBreaker::build_line(const NodeArena& arena, bool has_previous, uint32_t previous_position,
                             BreakKind previous_kind, uint32_t position, BreakKind kind)
{
    _line.clear();
    if (!left_skip_omitted())
        _line.push_back(glue(_params.left_skip));

    uint64_t start = 0;
    if (has_previous) {
        start = static_cast<uint64_t>(previous_position) + 1;
        // Section 884's post_disc_break.
        bool post_disc_break = false;
        if (previous_kind == BreakKind::DISC) {
            const Disc& disc = disc_of(arena, _body[previous_position]);
            const NodeView post_break = arena.view(disc.post_break);
            _line.insert(_line.end(), post_break.begin(), post_break.end());
            start += replaced_after(disc);
            post_disc_break = _params.rules.post_break_text_stops_pruning && !post_break.empty();
        }
        while (!post_disc_break && start < position &&
               is_prunable(_body[static_cast<size_t>(start)]))
            ++start;
    }
    if (start < position)
        _line.insert(_line.end(), _body.begin() + static_cast<std::ptrdiff_t>(start),
                     _body.begin() + static_cast<std::ptrdiff_t>(position));

    switch (kind) {
    case BreakKind::GLUE:
    case BreakKind::PARAGRAPH_END:
        break;
    case BreakKind::DISC: {
        const NodeView pre_break = arena.view(disc_of(arena, _body[position]).pre_break);
        _line.insert(_line.end(), pre_break.begin(), pre_break.end());
        break;
    }
    case BreakKind::KERN:
    case BreakKind::MATH: {
        Node broken = _body[position];
        broken.a = 0;
        _line.push_back(broken);
        break;
    }
    case BreakKind::PENALTY:
        _line.push_back(_body[position]);
        break;
    }
    _line.push_back(glue(_params.right_skip));
}

// The running totals a candidate line is judged from, and whether the
// prepared list allows it. It does not when a candidate's totals are not a
// range of the list's: a discretionary puts its own material at both ends of
// a line and takes the nodes it replaces out. Nor when hpack would report an
// error, which it does per line: a character with no measure, a rule of
// running width.
//
// Each node contributes what hpack adds for it (tex.web sections 651 to 656),
// characters as the measure answers. Height and depth have no part in judging
// a line and are not kept.
//
// tabulate and trial have no names in tex.web. They stand where it keeps
// active_width up to date as the list is scanned (sections 866 to 871) and
// carries the differences between active nodes in delta nodes (sections 822 to
// 824, 843, 844): the same sums, as totals from the start of the list. _resume
// is what section 837's look-ahead computes at each break, the first node
// after it that a break does not discard, except that it is cut off at the
// candidate's own end.
bool LineBreaker::tabulate(const NodeArena& arena)
{
    const size_t count = _body.size();
    _totals.resize(count + 1);
    _resume.resize(count + 1);
    Totals running;
    _totals[0] = running;
    for (size_t i = 0; i < count; ++i) {
        const Node& node = _body[i];
        switch (node.type) {
        case NodeType::CHAR:
            if (!_measure)
                return false;
            running.width += static_cast<uint64_t>(_measure(node.font(), node.codepoint()).width);
            break;
        case NodeType::BOX: {
            // One the arena does not hold adds nothing, as in hpack, which
            // reports it when the lines are packed.
            const Box* const box = arena.find_box(node);
            if (box != nullptr)
                running.width += static_cast<uint64_t>(box->width);
            break;
        }
        case NodeType::RULE:
            if (node.a == RUNNING)
                return false;
            running.width += static_cast<uint64_t>(node.a);
            break;
        case NodeType::GLUE:
            running.width += static_cast<uint64_t>(node.a);
            running.stretch[static_cast<unsigned>(node.stretch_order) & 3u] +=
                static_cast<uint64_t>(node.b);
            running.shrink[static_cast<unsigned>(node.shrink_order) & 3u] +=
                static_cast<uint64_t>(node.c);
            break;
        case NodeType::KERN:
        case NodeType::MATH_ON:
        case NodeType::MATH_OFF:
            running.width += static_cast<uint64_t>(node.a);
            break;
        case NodeType::PENALTY:
            break;
        case NodeType::DISC:
            return false;
        }
        _totals[i + 1] = running;
    }
    _resume[count] = static_cast<uint32_t>(count);
    for (size_t i = count; i-- > 0;)
        _resume[i] = is_prunable(_body[i]) ? _resume[i + 1] : static_cast<uint32_t>(i);
    return true;
}

// hpack's verdict on the candidate line build_line would make, without making
// it. The line's nodes are the range of the prepared list left when leading
// discardables are pruned, between the skips; a kern or math node at the break
// is there with no width and a penalty occupies nothing, so the break itself
// adds nothing. What hpack is given instead is a line of four glue nodes, one
// per order, that sum to the same natural width, stretch and shrink. The box
// it reports has no height or depth, which no caller of this reads.
PackResult LineBreaker::trial(const NodeArena& arena, bool has_previous, uint32_t previous_position,
                              BreakKind previous_kind, uint32_t position, BreakKind kind,
                              Scaled extra_stretch, Scaled target)
{
    if (!_tabulated) {
        build_line(arena, has_previous, previous_position, previous_kind, position, kind);
        if (extra_stretch > 0)
            _line.push_back(glue(GlueSpec{0, extra_stretch}));
        _work += 1 + _line.size() / NODES_PER_WORK_UNIT;
        return hpack(arena, _line, target, PackMode::EXACTLY, _measure);
    }
    ++_work;

    size_t start = 0;
    if (has_previous) {
        start = static_cast<size_t>(previous_position) + 1;
        if (start < position)
            start = std::min<size_t>(_resume[start], position);
    }
    Totals sum;
    if (start < position) {
        const Totals& from = _totals[start];
        const Totals& to = _totals[position];
        sum.width = to.width - from.width;
        for (int order = 0; order < 4; ++order) {
            sum.stretch[order] = to.stretch[order] - from.stretch[order];
            sum.shrink[order] = to.shrink[order] - from.shrink[order];
        }
    }
    const auto add = [&sum](const GlueSpec& spec) {
        sum.width += static_cast<uint64_t>(spec.width);
        sum.stretch[static_cast<unsigned>(spec.stretch_order) & 3u] +=
            static_cast<uint64_t>(spec.stretch);
        sum.shrink[static_cast<unsigned>(spec.shrink_order) & 3u] +=
            static_cast<uint64_t>(spec.shrink);
    };
    if (!left_skip_omitted())
        add(_params.left_skip);
    add(_params.right_skip);
    if (extra_stretch > 0)
        sum.stretch[0] += static_cast<uint64_t>(extra_stretch);

    _line.clear();
    for (int order = 0; order < 4; ++order) {
        GlueSpec spec;
        spec.width = order == 0 ? static_cast<Scaled>(sum.width) : 0;
        spec.stretch = static_cast<Scaled>(sum.stretch[order]);
        spec.stretch_order = static_cast<GlueOrder>(order);
        spec.shrink = static_cast<Scaled>(sum.shrink[order]);
        spec.shrink_order = static_cast<GlueOrder>(order);
        _line.push_back(glue(spec));
    }
    return hpack(arena, _line, target, PackMode::EXACTLY, _measure);
}

// Whether a line gets no left skip. Section 887 asks whether left_skip is
// zero_glue, which section 1229 makes of any glue parameter whose width,
// stretch and shrink are zero; the reference asks whether it equals the
// default spec, orders and all.
bool LineBreaker::left_skip_omitted() const
{
    const GlueSpec& skip = _params.left_skip;
    if (_params.rules.left_skip_omitted_when_zero)
        return skip.width == 0 && skip.stretch == 0 && skip.shrink == 0;
    return skip == GlueSpec();
}

void LineBreaker::note(const PackResult& result)
{
    if (_pack_error == PackError::NONE)
        _pack_error = result.error;
}

// --- Total fit --------------------------------------------------------------------

// Packs the candidate line from a passive node to a breakpoint to its target
// width (tex.web's line_width, section 850). emergency_stretch is added here,
// where it can change which breaks look feasible, and not when the chosen lines
// are packed for real; section 863 adds it to the background, which comes to
// the same.
PackResult LineBreaker::evaluate(const NodeArena& arena, int32_t previous_passive,
                                 uint32_t position, BreakKind kind, int64_t line, const Pass& pass)
{
    const Scaled target = _shape.for_line(line).width;
    PackResult result;
    if (previous_passive >= 0) {
        const Passive& previous = _passive[static_cast<size_t>(previous_passive)];
        result = trial(arena, true, previous.cur_break, previous.kind, position, kind,
                       pass.extra_stretch, target);
    } else {
        result = trial(arena, false, 0, BreakKind::PARAGRAPH_END, position, kind,
                       pass.extra_stretch, target);
    }
    note(result);
    return result;
}

// tex.web section 859, with its names: d, and pi for the penalty at the
// break. A positive penalty adds its square; a negative one subtracts it,
// which is how a break is rewarded; exactly EJECT_PENALTY contributes nothing,
// because a forced break is not a choice. `how_bad` is b, `fit` is fit_class,
// `hyphenated` is break_type and `at_end` is the section's cur_p = null.
int64_t LineBreaker::demerits(int64_t how_bad, int64_t pi, bool hyphenated, bool at_end,
                              Fitness fit, const Active& previous) const
{
    int64_t d = _params.line_penalty + how_bad;
    d = magnitude(d) >= 10000 ? SATURATED_DEMERITS : d * d;
    if (pi > 0)
        d += pi * pi;
    else if (pi > EJECT_PENALTY && pi < 0)
        d -= pi * pi;
    if (hyphenated && previous.hyphenated)
        d += at_end ? _params.final_hyphen_demerits : _params.double_hyphen_demerits;
    const int distance = static_cast<int>(fit) - static_cast<int>(previous.fitness);
    if (distance > 1 || distance < -1)
        d += _params.adj_demerits;
    return d;
}

// tex.web's try_break (sections 829 to 861): scores a breakpoint against every
// active node and replaces the active list. pi, r, d, minimal_demerits,
// minimum_demerits, best_place, artificial_demerits and node_r_stays_active
// are its names; `line` is its l.
//
// Active nodes are processed in groups sharing a line number, because the node
// created from a group inherits line_number + 1 and that decides which LineSpec
// the next line is measured against. A group is tex.web's line-number class
// (section 835) without easy_line: every line number is a class of its own,
// where section 850 makes one class of all the lines past last_special_line.
// And the order in which the nodes are met is the reference's and not
// tex.web's (linebreak.h, "Ties").
void LineBreaker::try_break(const NodeArena& arena, const Breakpoint& breakpoint, const Pass& pass)
{
    // Section 831: an infinite penalty inhibits the break, and anything at or
    // below eject_penalty is normalised so that "forced" is an equality.
    int64_t pi = breakpoint.penalty;
    if (pi >= INF_PENALTY)
        return;
    if (pi <= EJECT_PENALTY)
        pi = EJECT_PENALTY;
    const bool forced = pi == EJECT_PENALTY;
    const bool at_end = breakpoint.kind == BreakKind::PARAGRAPH_END;

    // The reference's stable sort by line number of survivors + created: both
    // runs are already in line-number order, so it is their merge, survivors
    // first among equals. tex.web's list needs no sorting, each new node
    // having been linked in at the end of its class (section 845).
    _ordered.clear();
    {
        size_t s = 0;
        size_t c = 0;
        while (s < _survivors.size() || c < _created.size()) {
            const bool take_survivor =
                c == _created.size() ||
                (s < _survivors.size() && _survivors[s].line_number <= _created[c].line_number);
            _ordered.push_back(take_survivor ? _survivors[s++] : _created[c++]);
        }
    }
    _survivors.clear();
    _created.clear();

    size_t index = 0;
    while (index < _ordered.size()) {
        const int64_t line = _ordered[index].line_number;
        size_t group_end = index;
        while (group_end < _ordered.size() && _ordered[group_end].line_number == line)
            ++group_end;

        int64_t minimal_demerits[4] = {AWFUL_BAD, AWFUL_BAD, AWFUL_BAD, AWFUL_BAD};
        int32_t best_place[4] = {-1, -1, -1, -1};
        int64_t minimum_demerits = AWFUL_BAD;

        for (size_t i = index; i < group_end; ++i) {
            // Out of budget: the pass is abandoned where it stands and run()
            // reports it. Nothing of the active list is read again. Not
            // tex.web's.
            if (_work >= _work_budget)
                return;
            const Active r = _ordered[i];
            const Verdict verdict = classify(
                evaluate(arena, r.break_node, breakpoint.position, breakpoint.kind, line, pass));
            const int64_t how_bad = verdict.how_bad;
            const int fit = static_cast<int>(verdict.fit);

            bool artificial_demerits = false;
            bool node_r_stays_active = false;
            if (how_bad > INF_BAD || forced) {
                // Section 851 sends an overfull line and a forced break here
                // alike. Section 854, the last resort: on the final pass, if
                // this is the only active node left and nothing feasible has
                // been found, break here for free. Overfull output beats none.
                // `alone` is that section's two tests on the links of r.
                const bool alone =
                    _survivors.empty() && _created.empty() && i == _ordered.size() - 1;
                if (pass.final_pass && minimum_demerits == AWFUL_BAD && alone) {
                    artificial_demerits = true;
                    ++_artificial_breaks;
                } else if (how_bad > pass.threshold) {
                    continue;
                }
            } else {
                if (how_bad > pass.threshold) {
                    _survivors.push_back(r);
                    continue;
                }
                node_r_stays_active = true;
            }

            int64_t d = artificial_demerits
                            ? 0
                            : demerits(how_bad, pi, breakpoint.hyphenated, at_end, verdict.fit, r);
            // Section 855. A total can only run away downward, through
            // negative penalties and a negative adj_demerits over very many
            // lines; tex.web adds unchecked.
            d = add_saturating(d, r.total_demerits);
            // The section's comparison is `<=`, so a later break with equal
            // demerits wins.
            if (d <= minimal_demerits[fit]) {
                minimal_demerits[fit] = d;
                best_place[fit] = r.break_node;
                minimum_demerits = std::min(minimum_demerits, d);
            }
            if (node_r_stays_active)
                _survivors.push_back(r);
        }

        if (minimum_demerits < AWFUL_BAD) {
            // Section 836: a new active node whose demerits already exceed the
            // best found plus the worst adjacency charge can never win. The
            // nodes are made in fitness order, and each with its passive node
            // (section 845).
            const int64_t adjacency = magnitude(_params.adj_demerits);
            const int64_t limit = adjacency >= AWFUL_BAD - minimum_demerits
                                      ? AWFUL_BAD - 1
                                      : minimum_demerits + adjacency;
            for (int fit = 0; fit < 4; ++fit) {
                if (minimal_demerits[fit] > limit)
                    continue;
                Passive passive;
                passive.cur_break = breakpoint.position;
                passive.kind = breakpoint.kind;
                passive.prev_break = best_place[fit];
                Active active;
                active.break_node = static_cast<int32_t>(_passive.size());
                active.line_number = line + 1;
                active.fitness = static_cast<Fitness>(fit);
                active.hyphenated = breakpoint.hyphenated;
                active.total_demerits = minimal_demerits[fit];
                _passive.push_back(passive);
                _created.push_back(active);
            }
        }
        index = group_end;
    }
}

// tex.web's try_break (sections 829 to 861) with its line-number classes and
// its active list: what TexRules::easy_line asks for. The names are the
// section's: r, l, old_l, d, minimal_demerits, minimum_demerits, best_place,
// best_pl_line, artificial_demerits, node_r_stays_active.
//
// The active list is one sequence, in _survivors between calls. It is walked
// once (section 829's loop over r), and the list that replaces it is written
// as the walk goes: a node that stays active is copied across, and the nodes
// a class creates go in where the walk stands, which is ahead of the first
// node of the next class (section 845's link(prev_r) := q).
//
// Section 835: when r is the first node of a new class -- its line number is
// past old_l -- the class just finished gets its new active nodes, unless
// that class is line easy_line and there are nodes after it. Those nodes are
// the first of the class of every later line, the nodes made from line
// easy_line would join that class, and so the best breaks found so far are
// carried into it and one set of nodes is made for both at the end. Section
// 850 then makes the class of r: a line number of its own up to easy_line,
// and everything past it together, measured against the width every later
// line has. The end of the list is section 835's last_active, whose line
// number is past any class.
//
// A new node's line number is one more than that of the node its best break
// came from (best_pl_line, section 855), not one more than the class's.
//
// Section 854's last resort is taken when r is the last node of the list and
// nothing is ahead of it in the list being written (link(r) = last_active and
// prev_r = active).
void LineBreaker::try_break_by_class(const NodeArena& arena, const Breakpoint& breakpoint,
                                     const Pass& pass)
{
    // Section 831.
    int64_t pi = breakpoint.penalty;
    if (pi >= INF_PENALTY)
        return;
    if (pi <= EJECT_PENALTY)
        pi = EJECT_PENALTY;
    const bool forced = pi == EJECT_PENALTY;
    const bool at_end = breakpoint.kind == BreakKind::PARAGRAPH_END;

    _ordered.clear();
    _ordered.swap(_survivors);

    int64_t minimal_demerits[4] = {AWFUL_BAD, AWFUL_BAD, AWFUL_BAD, AWFUL_BAD};
    int32_t best_place[4] = {-1, -1, -1, -1};
    int64_t best_pl_line[4] = {0, 0, 0, 0};
    int64_t minimum_demerits = AWFUL_BAD;

    constexpr int64_t LAST_ACTIVE = std::numeric_limits<int64_t>::max();
    int64_t old_l = 0;
    // The line whose width the class of r is measured against: its first
    // node's. Past easy_line every line has the one width.
    int64_t width_line = 1;

    for (size_t i = 0;; ++i) {
        const bool last_active = i == _ordered.size();
        const int64_t l = last_active ? LAST_ACTIVE : _ordered[i].line_number;
        if (l > old_l) {
            if (minimum_demerits < AWFUL_BAD && (old_l != _easy_line || last_active)) {
                // Section 836, as in try_break.
                const int64_t adjacency = magnitude(_params.adj_demerits);
                const int64_t limit = adjacency >= AWFUL_BAD - minimum_demerits
                                          ? AWFUL_BAD - 1
                                          : minimum_demerits + adjacency;
                for (int fit = 0; fit < 4; ++fit) {
                    if (minimal_demerits[fit] <= limit) {
                        Passive passive;
                        passive.cur_break = breakpoint.position;
                        passive.kind = breakpoint.kind;
                        passive.prev_break = best_place[fit];
                        Active active;
                        active.break_node = static_cast<int32_t>(_passive.size());
                        active.line_number = best_pl_line[fit] + 1;
                        active.fitness = static_cast<Fitness>(fit);
                        active.hyphenated = breakpoint.hyphenated;
                        active.total_demerits = minimal_demerits[fit];
                        _passive.push_back(passive);
                        _survivors.push_back(active);
                    }
                    minimal_demerits[fit] = AWFUL_BAD;
                }
                minimum_demerits = AWFUL_BAD;
            }
            if (last_active)
                return;
            // Section 850.
            width_line = l;
            old_l = l > _easy_line ? LAST_ACTIVE - 1 : l;
        }

        // Out of budget, as in try_break. The nodes not yet met are dropped
        // with the pass.
        if (_work >= _work_budget)
            return;
        const Active r = _ordered[i];
        const Verdict verdict = classify(
            evaluate(arena, r.break_node, breakpoint.position, breakpoint.kind, width_line, pass));
        const int64_t how_bad = verdict.how_bad;
        const int fit = static_cast<int>(verdict.fit);

        bool artificial_demerits = false;
        bool node_r_stays_active = false;
        if (how_bad > INF_BAD || forced) {
            // Section 854.
            const bool alone = _survivors.empty() && i == _ordered.size() - 1;
            if (pass.final_pass && minimum_demerits == AWFUL_BAD && alone) {
                artificial_demerits = true;
                ++_artificial_breaks;
            } else if (how_bad > pass.threshold) {
                continue;
            }
        } else {
            if (how_bad > pass.threshold) {
                _survivors.push_back(r);
                continue;
            }
            node_r_stays_active = true;
        }

        // Section 855.
        int64_t d = artificial_demerits
                        ? 0
                        : demerits(how_bad, pi, breakpoint.hyphenated, at_end, verdict.fit, r);
        d = add_saturating(d, r.total_demerits);
        if (d <= minimal_demerits[fit]) {
            minimal_demerits[fit] = d;
            best_place[fit] = r.break_node;
            best_pl_line[fit] = l;
            minimum_demerits = std::min(minimum_demerits, d);
        }
        if (node_r_stays_active)
            _survivors.push_back(r);
    }
}

// One pass: the body of section 863's loop. It starts from one active node
// for the paragraph's beginning (section 864: unhyphenated, decent, no passive
// node, line 1), tries every breakpoint while any node is active, and at the
// end picks the active node with the fewest demerits (section 874). False
// means it failed: the active list emptied before the end, so no chain of
// breakpoints within the threshold reaches the paragraph's end -- or the work
// budget ran out, which the caller tells apart by looking at it.
bool LineBreaker::run(const NodeArena& arena, const Pass& pass, Active& winner)
{
    _passive.clear();
    _survivors.clear();
    _created.clear();
    _survivors.push_back(Active());
    _artificial_breaks = 0;

    for (const Breakpoint& breakpoint : _breakpoints) {
        if (_survivors.empty() && _created.empty())
            return false;
        if (_params.rules.easy_line)
            try_break_by_class(arena, breakpoint, pass);
        else
            try_break(arena, breakpoint, pass);
        if (_work >= _work_budget)
            return false;
    }
    if (_survivors.empty() && _created.empty())
        return false;

    // Section 874: the first of the fewest, in the active list's own order
    // (`best` is its best_bet). The order is the reference's, or under
    // TexRules::easy_line tex.web's, the whole list being in _survivors.
    const Active* best = nullptr;
    for (const Active& active : _survivors) {
        if (best == nullptr || active.total_demerits < best->total_demerits)
            best = &active;
    }
    for (const Active& active : _created) {
        if (best == nullptr || active.total_demerits < best->total_demerits)
            best = &active;
    }
    winner = *best;
    return true;
}

// A winning active node's passive chain, from the paragraph's start: section
// 878, which reverses the prev_break links in place where this copies the
// breaks out in order.
void LineBreaker::chain(const Active& winner)
{
    _chosen.clear();
    for (int32_t at = winner.break_node; at >= 0;
         at = _passive[static_cast<size_t>(at)].prev_break) {
        const Passive& passive = _passive[static_cast<size_t>(at)];
        _chosen.push_back(Break{passive.cur_break, passive.kind});
    }
    std::reverse(_chosen.begin(), _chosen.end());
}

// --- First fit --------------------------------------------------------------------
//
// Nothing below, to the next rule, is tex.web's: it has one line breaker.

// Walks the legal breakpoints in order, remembering the latest one at which the
// line so far fits. At each breakpoint: if the line to here fits, remember it,
// and take it if the break is forced; if it does not fit and one is remembered,
// take the remembered one and look at this breakpoint again from the new start;
// if it does not fit and nothing is remembered, take it anyway -- a word wider
// than its line, set alone and overfull.
//
// "Fits" is hpack's verdict and not a width comparison: the line is packed to
// its width and fits unless the result is OVERFULL, so whatever shrink the
// material carries is used.
//
// A penalty strictly between the two infinities is a legal breakpoint whose
// value is ignored: there is no cost to add it to.
void LineBreaker::first_fit(const NodeArena& arena)
{
    _chosen.clear();
    bool has_start = false;
    Break start;
    bool has_fit = false;
    Break fit;
    int64_t line = 1;

    const auto fits = [&](const Break& candidate) {
        const PackResult packed =
            trial(arena, has_start, start.position, start.kind, candidate.position, candidate.kind,
                  0, _shape.for_line(line).width);
        note(packed);
        return packed.fitness != PackFitness::OVERFULL;
    };

    for (const Breakpoint& breakpoint : _breakpoints) {
        if (breakpoint.penalty >= INF_PENALTY)
            continue;
        const bool forced = breakpoint.penalty <= EJECT_PENALTY;
        const Break candidate{breakpoint.position, breakpoint.kind};
        bool room = fits(candidate);
        if (!room && has_fit) {
            _chosen.push_back(fit);
            start = fit;
            has_start = true;
            has_fit = false;
            ++line;
            room = fits(candidate);
        }
        if (forced || !room) {
            _chosen.push_back(candidate);
            start = candidate;
            has_start = true;
            has_fit = false;
            ++line;
        } else {
            fit = candidate;
            has_fit = true;
        }
    }
}

// --- Assembly ---------------------------------------------------------------------

// tex.web's post_line_break (section 877) as far as this port takes it: each
// chosen line is built (build_line), packed to its own width and shifted to
// its own indent (section 889), without emergency_stretch. `line.box` is its
// just_box. The lines are returned and not appended to a vertical list, so
// sections 888 and 890 -- append_to_vlist, the material moved out of the line,
// and the penalties between lines -- are not here.
void LineBreaker::post_line_break(NodeArena& arena, Paragraph& out)
{
    bool has_previous = false;
    Break previous;
    int64_t line_number = 1;
    for (const Break& current : _chosen) {
        const LineSpec spec = _shape.for_line(line_number);
        build_line(arena, has_previous, previous.position, previous.kind, current.position,
                   current.kind);
        BrokenLine line;
        line.packed = hpack(arena, _line, spec.width, PackMode::EXACTLY, _measure);
        note(line.packed);
        line.packed.box.shift_amount = spec.indent;
        line.box = arena.add_box(line.packed.box, _line);
        line.packed.box.contents = arena.box(line.box).contents;
        out.lines.push_back(line);
        previous = current;
        has_previous = true;
        ++line_number;
    }
    out.breaks.assign(_chosen.begin(), _chosen.end());
}

bool LineBreaker::break_paragraph(NodeArena& arena, NodeView nodes, const ParShape& shape,
                                  const LineBreakParams& params, CharMeasure measure,
                                  Paragraph& out)
{
    out.lines.clear();
    out.breaks.clear();
    out.total_demerits = 0;
    out.pass_used = BreakPass::FIRST;
    out.prepared_length = 0;
    out.error = LineBreakError::NONE;
    out.pack_error = PackError::NONE;
    out.budget_exhausted = false;
    out.artificial_breaks = 0;

    if (shape.empty()) {
        out.error = LineBreakError::EMPTY_PARSHAPE;
        return false;
    }

    _params = params;
    _params.pretolerance = clamp_param(params.pretolerance);
    _params.tolerance = clamp_param(params.tolerance);
    _params.line_penalty = clamp_param(params.line_penalty);
    _params.adj_demerits = clamp_param(params.adj_demerits);
    _params.double_hyphen_demerits = clamp_param(params.double_hyphen_demerits);
    _params.final_hyphen_demerits = clamp_param(params.final_hyphen_demerits);
    _params.hyphen_penalty = clamp_param(params.hyphen_penalty);
    _params.ex_hyphen_penalty = clamp_param(params.ex_hyphen_penalty);
    _params.emergency_stretch = clamp_param(params.emergency_stretch);
    if (_params.rules.skips_made_shrink_finite) {
        // Section 827's two check_shrinkage calls, and section 868's for the
        // par-fill glue, which is glue of the list there. Section 826 reports
        // the first of them as an error; nothing is reported here.
        for (GlueSpec* skip : {&_params.left_skip, &_params.right_skip, &_params.par_fill_skip}) {
            if (skip->shrink_order != GlueOrder::NORMAL && skip->shrink != 0)
                skip->shrink_order = GlueOrder::NORMAL;
        }
    }
    _shape = shape;
    _easy_line = shape.last_special_line();
    _measure = measure;
    _pack_error = PackError::NONE;
    _work = 0;

    // `nodes` may view the arena, which post_line_break() adds to; the prepared copy
    // is what is read from here on.
    prepare(arena, nodes);
    find_breakpoints(arena);
    out.prepared_length = static_cast<uint32_t>(_body.size());
    _tabulated = _use_totals && tabulate(arena);

    if (_params.first_fit) {
        first_fit(arena);
        out.pass_used = BreakPass::FIRST_FIT;
    } else {
        // Section 863: the passes, each with its threshold capped at inf_bad.
        // The second is the last unless there is emergency stretch to try.
        const int64_t tolerance = std::min<int64_t>(_params.tolerance, INF_BAD);
        Pass passes[3];
        int pass_count = 0;
        if (_params.pretolerance >= 0) {
            passes[pass_count++] =
                Pass{std::min<int64_t>(_params.pretolerance, INF_BAD), 0, false, BreakPass::FIRST};
        }
        passes[pass_count++] =
            Pass{tolerance, 0, _params.emergency_stretch <= 0, BreakPass::SECOND};
        if (_params.emergency_stretch > 0) {
            passes[pass_count++] =
                Pass{tolerance, _params.emergency_stretch, true, BreakPass::EMERGENCY};
        }

        bool broken = false;
        for (int i = 0; i < pass_count && !broken && _work < _work_budget; ++i) {
            Active winner;
            if (!run(arena, passes[i], winner))
                continue;
            chain(winner);
            out.total_demerits = winner.total_demerits;
            out.pass_used = passes[i].which;
            out.artificial_breaks = _artificial_breaks;
            broken = true;
        }
        if (!broken && _work >= _work_budget) {
            // Not tex.web's. The optimiser was costing more than it is allowed
            // to. The paragraph is broken by first fit, whose cost is linear,
            // and says so. The work counted is all of the passes together, so
            // a paragraph is never broken by total fit at more than the budget.
            first_fit(arena);
            out.pass_used = BreakPass::FIRST_FIT;
            out.budget_exhausted = true;
        } else if (!IMTYPESET_CONTRACT(broken, "break_paragraph: no pass produced a breaking")) {
            // Section 854's escape makes the last pass always succeed, so this
            // is not reachable; one line to the paragraph's end if it ever is.
            _chosen.clear();
            _chosen.push_back(Break{static_cast<uint32_t>(_body.size()), BreakKind::PARAGRAPH_END});
            out.pass_used = passes[pass_count - 1].which;
        }
    }

    post_line_break(arena, out);
    out.pack_error = _pack_error;
    if (_pack_error != PackError::NONE)
        out.error = LineBreakError::MALFORMED_LIST;
    return out.error == LineBreakError::NONE;
}

bool break_paragraph(NodeArena& arena, NodeView nodes, const ParShape& shape,
                     const LineBreakParams& params, CharMeasure measure, Paragraph& out)
{
    LineBreaker breaker;
    return breaker.break_paragraph(arena, nodes, shape, params, measure, out);
}

} // namespace ImTypeset::detail
