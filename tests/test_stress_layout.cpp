// Stress and hardening tests of the layout core: nodes, pack, linebreak,
// shipout, driven through the internal API with lists nobody vetted.
//
// The first half holds the layout-core review's findings (items A to F and K
// of .concerns/review-layout-core--reviewer-3--findings.md), each as the input
// that broke the code before it was fixed. docs/hardening.md has one row per
// item. Several of them can only fail under ASan or UBSan (a stack overflow, a
// signed overflow); each still asserts what is assertable -- the status that
// comes back, the size of what was built, a bound on the time.
//
// The second half is a deterministic fuzz: random well-formed lists checked
// against invariants, and random hostile lists (any byte in any field) that
// only have to come back.
//
// IMTYPESET_STRESS_ITERS multiplies the iteration counts (default 1).

#include "check.h"

#include "core_golden.h"
#include "linebreak.h"
#include "nodes.h"
#include "pack.h"
#include "shipout.h"
#include "style.h"
#include "units.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace ImTypeset::detail;

namespace
{

constexpr Scaled PT = UNITY;
constexpr Scaled BIG = std::numeric_limits<int64_t>::max();
constexpr Scaled SMALL = std::numeric_limits<int64_t>::min();

int g_violations = 0;
void count_violation(const char*)
{
    ++g_violations;
}

// Contract violations are counted for the life of the scope and not asserted:
// hostile input is allowed to trip them, and must be defined afterwards.
class ViolationScope
{
public:
    ViolationScope()
        : _before(contract_handler())
    {
        contract_handler() = &count_violation;
        g_violations = 0;
    }
    ~ViolationScope() { contract_handler() = _before; }
    ViolationScope(const ViolationScope&) = delete;
    ViolationScope& operator=(const ViolationScope&) = delete;

private:
    ContractHandler _before;
};

double seconds_now()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

int64_t scale()
{
    const char* const text = std::getenv("IMTYPESET_STRESS_ITERS");
    if (text == nullptr)
        return 1;
    const long long value = std::atoll(text);
    return value < 1 ? 1 : (value > 100000 ? 100000 : value);
}

struct Random {
    uint64_t state;

    explicit Random(uint64_t seed)
        : state(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull)
    {
    }
    uint64_t next()
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }
    int64_t below(int64_t bound)
    {
        return static_cast<int64_t>(next() % static_cast<uint64_t>(bound));
    }
    bool one_in(int64_t n) { return below(n) == 0; }
};

// A width for every character, between nothing and 12 pt, from its own bits.
CharDimens measure_of(void*, FontKey font, uint32_t codepoint)
{
    uint64_t h = font.bits * 0x9E3779B97F4A7C15ull ^ (codepoint * 0xC2B2AE3D27D4EB4Full);
    h ^= h >> 29;
    CharDimens dimens;
    dimens.width = static_cast<Scaled>(h % static_cast<uint64_t>(12 * UNITY));
    dimens.height = static_cast<Scaled>((h >> 20) % static_cast<uint64_t>(9 * UNITY));
    dimens.depth = static_cast<Scaled>((h >> 40) % static_cast<uint64_t>(3 * UNITY));
    return dimens;
}
const CharMeasure MEASURE{&measure_of, nullptr};

// A measure that answers whatever bits it is asked with: a font that lies.
// `user` is a number and not an address: what it answers must be the same in
// every run, or the checks that follow an error are not the same checks.
CharDimens hostile_measure(void* user, FontKey font, uint32_t codepoint)
{
    Random random(font.bits ^ codepoint ^ static_cast<uint64_t>(reinterpret_cast<uintptr_t>(user)));
    CharDimens dimens;
    dimens.width = static_cast<Scaled>(random.next());
    dimens.height = static_cast<Scaled>(random.next());
    dimens.depth = static_cast<Scaled>(random.next());
    return dimens;
}

// `levels` boxes, each holding the one before it twice: 2^levels paths.
Node shared_tree(NodeArena& arena, int levels)
{
    const std::vector<Node> leaf = {glue(GlueSpec{PT}), glue(GlueSpec{PT})};
    Node box = arena.add_box(Box{}, leaf);
    for (int i = 0; i < levels; ++i) {
        const std::vector<Node> pair = {box, box};
        box = arena.add_box(Box{}, pair);
    }
    return box;
}

Node box_node(uint32_t index)
{
    Node node;
    node.type = NodeType::BOX;
    node.u = index;
    return node;
}

Node disc_node(uint32_t index)
{
    Node node;
    node.type = NodeType::DISC;
    node.u = index;
    return node;
}

} // namespace

// --- Review item A: a box can contain itself ----------------------------------------

// The reviewer's snippet. add_box did not look at the indices its contents
// name, so a BOX node naming the box about to be made went in, and ship
// recursed until the stack ended (ASan: stack-overflow in NodeArena::holds).
TEST(item_a_a_box_cannot_be_made_to_contain_itself)
{
    const ViolationScope scope;
    NodeArena arena;
    const std::vector<Node> inside = {box_node(static_cast<uint32_t>(arena.box_count()))};
    const Node box = arena.add_box(Box{}, inside);
    CHECK_EQ(g_violations, 1);
    CHECK(arena.contents(box).empty());
    std::vector<DrawCmd> out;
    CHECK(ship(arena, box, Point{}, CharMeasure(), out) == ShipError::NONE);

    // The same hole through a discretionary, on either side.
    const std::vector<Node> own = {disc_node(static_cast<uint32_t>(arena.disc_count()))};
    const Node pre = arena.add_disc(own, {});
    CHECK_EQ(g_violations, 2);
    CHECK_EQ(arena.disc(pre).pre_break.count, 0u);
    const std::vector<Node> own_again = {disc_node(static_cast<uint32_t>(arena.disc_count()))};
    const Node post = arena.add_disc({}, own_again);
    CHECK_EQ(g_violations, 3);
    CHECK_EQ(arena.disc(post).post_break.count, 0u);

    // A box naming one that is further ahead still.
    const std::vector<Node> ahead = {kern(PT), box_node(1000)};
    CHECK(arena.contents(arena.add_box(Box{}, ahead)).empty());
    CHECK_EQ(g_violations, 4);
}

// A node kept across a rewind names an index the arena no longer holds; the
// next box made takes that index, and the stale node inside it would be the
// box itself.
TEST(item_a_a_node_kept_across_a_rewind_cannot_close_a_cycle)
{
    const ViolationScope scope;
    NodeArena arena;
    const NodeArena::Mark mark = arena.mark();
    const std::vector<Node> leaf = {kern(PT)};
    const Node stale = arena.add_box(Box{}, leaf);
    arena.rewind(mark);
    const std::vector<Node> inside = {stale};
    const Node box = arena.add_box(Box{}, inside);
    CHECK_EQ(box.u, stale.u);
    CHECK_EQ(g_violations, 1);
    CHECK(arena.contents(box).empty());
}

// A Mark put together by hand keeps a box and drops the nodes it names; what
// is added next lands inside it. The arena cannot see that, so the walks count
// their own depth: ship answers TOO_DEEP, import and deep_equal come back.
TEST(item_a_a_cycle_made_around_the_arena_is_walked_to_a_bound)
{
    const ViolationScope scope;
    NodeArena arena;
    const std::vector<Node> leaf = {kern(PT)};
    const Node box = arena.add_box(Box{}, leaf);
    NodeArena::Mark forged;
    forged.nodes = 0;
    forged.boxes = 1;
    arena.rewind(forged);
    const std::vector<Node> self = {box};
    arena.add_nodes(self);
    REQUIRE_EQ(arena.contents(box).size(), size_t(1));
    REQUIRE(arena.contents(box)[0] == box);

    std::vector<DrawCmd> out;
    out.push_back(DrawCmd::rule_rect(1, 2, 3, 4));
    CHECK(ship(arena, box, Point{}, CharMeasure(), out) == ShipError::TOO_DEEP);
    CHECK_EQ(out.size(), size_t(1));
    CHECK(!deep_equal(arena, box, arena, box));
    NodeArena other;
    const Node imported = other.import(arena, box);
    CHECK(imported.is(NodeType::BOX));
    CHECK(other.box_count() <= size_t(NodeArena::MAX_NESTING) + 2);

    // hpack and vpack do not recurse.
    CHECK(hpack(arena, self, 0, PackMode::ADDITIONAL, CharMeasure()).error == PackError::NONE);
    CHECK(vpack(arena, self).error == PackError::NONE);
}

// --- Review item B: ship's default bound ---------------------------------------------

// 32 levels of a box named twice: 8.6 thousand million visits from 66 nodes.
// With max_items defaulting to "as many as an index can number" ship pushed
// commands until memory ran out (measured at 20 levels: 2,097,153 commands,
// 134 MB, from 42 nodes).
TEST(item_b_the_default_bound_of_ship_follows_the_size_of_the_tree)
{
    NodeArena arena;
    const Node box = shared_tree(arena, 32);
    std::vector<DrawCmd> out;
    out.push_back(DrawCmd::rule_rect(1, 2, 3, 4));
    const double start = seconds_now();
    CHECK(ship(arena, box, Point{}, CharMeasure(), out) == ShipError::TOO_MANY_ITEMS);
    CHECK(seconds_now() - start < 5.0);
    CHECK_EQ(out.size(), size_t(1));
    CHECK(out.capacity() < size_t(1) << 18);

    // Sharing within the allowance still ships: a box set three times over.
    NodeArena small;
    const std::vector<Node> leaf = {glue(GlueSpec{PT}), kern(PT)};
    const Node word = small.add_box(Box{}, leaf);
    const std::vector<Node> line = {word, word, word};
    out.clear();
    CHECK(ship(small, Box{}, line, Point{}, CharMeasure(), out) == ShipError::NONE);
    CHECK_EQ(out.size(), size_t(4)); // the LINE and three glue

    // A caller that means it says how many visits it expects.
    NodeArena deep;
    const Node ten = shared_tree(deep, 10);
    out.clear();
    CHECK(ship(deep, ten, Point{}, CharMeasure(), out, 1u << 20) == ShipError::NONE);
    CHECK_EQ(out.size(), size_t(1) + (size_t(1) << 11));
}

// --- Review item C: import and deep_equal on a shared tree -----------------------------

// Both walked every path. Measured at 20 levels: import turned 42 nodes into
// 4,194,302 nodes and 2,097,151 boxes in 4.6 s; deep_equal took 0.6 s. Forty
// levels would not finish.
TEST(item_c_import_and_deep_equal_visit_a_shared_box_once)
{
    NodeArena arena;
    const Node box = shared_tree(arena, 40);
    const double start = seconds_now();
    NodeArena other;
    const Node imported = other.import(arena, box);
    CHECK_EQ(other.box_count(), arena.box_count());
    CHECK_EQ(other.node_count(), arena.node_count());
    CHECK(deep_equal(arena, box, other, imported));
    CHECK(deep_equal(arena, box, arena, box));

    // Into the arena it came from, and a tree that differs at the bottom.
    const Node again = arena.import(arena, box);
    CHECK(deep_equal(arena, box, arena, again));
    NodeArena different;
    const std::vector<Node> leaf = {glue(GlueSpec{PT}), glue(GlueSpec{2 * PT})};
    Node unlike = different.add_box(Box{}, leaf);
    for (int i = 0; i < 40; ++i) {
        const std::vector<Node> pair = {unlike, unlike};
        unlike = different.add_box(Box{}, pair);
    }
    CHECK(!deep_equal(arena, box, different, unlike));
    CHECK(seconds_now() - start < 5.0);

    // Sharing through discretionaries.
    NodeArena discs;
    const std::vector<Node> word = {kern(PT)};
    Node disc = discs.add_disc(word, word, 1);
    for (int i = 0; i < 40; ++i) {
        const std::vector<Node> one = {disc};
        disc = discs.add_disc(one, one, i);
    }
    NodeArena copy;
    const Node copied = copy.import(discs, disc);
    CHECK_EQ(copy.disc_count(), discs.disc_count());
    CHECK(deep_equal(discs, disc, copy, copied));
    CHECK(seconds_now() - start < 5.0);
}

// What import and deep_equal answer for a node its arena does not hold: data,
// so an empty box and no contract.
TEST(item_c_import_and_deep_equal_take_a_stray_index_as_empty)
{
    const ViolationScope scope;
    NodeArena arena;
    NodeArena other;
    const Node imported = other.import(arena, box_node(9));
    CHECK(other.contents(imported).empty());
    const Node disc = other.import(arena, disc_node(9));
    CHECK_EQ(other.disc(disc).pre_break.count, 0u);
    CHECK(deep_equal(arena, box_node(9), other, imported));
    CHECK_EQ(g_violations, 0);
}

// --- Review item D: dimensions outside MAX_DIMEN ---------------------------------------

// Every sum below was a signed overflow (UBSan: pack.cpp width, height and
// excess; GlueTotal::add; shipout's cur_h, cur_v, rule thickness and glue
// width; ParShape::hanging). They are checked now: the sum stops at the end of
// the range and the packing or the ship says so.
TEST(item_d_sums_that_leave_the_range_are_reported_and_defined)
{
    NodeArena arena;
    const GlueSpec huge{BIG, BIG, GlueOrder::NORMAL, BIG, GlueOrder::NORMAL};
    const std::vector<Node> list = {kern(BIG), kern(BIG), glue(huge), glue(huge),
                                    rule(BIG, BIG, BIG)};
    const PackResult h = hpack(arena, list, -BIG, PackMode::EXACTLY, CharMeasure());
    CHECK(h.error == PackError::DIMENSION_OVERFLOW);
    CHECK_EQ(h.natural, SCALED_MAX);
    const PackResult v = vpack(arena, list, BIG, PackMode::ADDITIONAL);
    CHECK(v.error == PackError::DIMENSION_OVERFLOW);
    CHECK_EQ(v.box.height, SCALED_MAX);

    std::vector<DrawCmd> out;
    CHECK(ship(arena, h.box, list, Point{BIG, BIG}, CharMeasure(), out) ==
          ShipError::DIMENSION_OVERFLOW);
    Box tall = v.box;
    tall.height = SMALL;
    CHECK(ship(arena, tall, list, Point{BIG, BIG}, CharMeasure(), out) ==
          ShipError::DIMENSION_OVERFLOW);
    CHECK(out.empty());

    // The most negative value has no negation.
    const std::vector<Node> negative = {
        kern(SMALL), glue(GlueSpec{SMALL, SMALL, GlueOrder::FIL, SMALL, GlueOrder::FIL})};
    CHECK(hpack(arena, negative, BIG, PackMode::EXACTLY, CharMeasure()).error ==
          PackError::DIMENSION_OVERFLOW);
    CHECK(vpack(arena, negative, SMALL, PackMode::ADDITIONAL, SMALL).error ==
          PackError::DIMENSION_OVERFLOW);
    const std::vector<Node> lone = {kern(SMALL)};
    const PackResult least = hpack(arena, lone, BIG, PackMode::EXACTLY, CharMeasure());
    CHECK(least.error == PackError::DIMENSION_OVERFLOW);
    CHECK_EQ(least.excess, SCALED_MAX);

    // A box with such dimensions inside a list, and shifted.
    Box wide;
    wide.width = BIG;
    wide.height = SMALL;
    wide.depth = BIG;
    wide.shift_amount = BIG;
    const std::vector<Node> boxes = {arena.add_box(wide), arena.add_box(wide)};
    CHECK(hpack(arena, boxes, 0, PackMode::ADDITIONAL, CharMeasure()).error ==
          PackError::DIMENSION_OVERFLOW);
    CHECK(vpack(arena, boxes).error == PackError::DIMENSION_OVERFLOW);
    const Node wrapped = atom_box(arena, boxes[0]);
    CHECK_EQ(arena.box(wrapped).depth, SCALED_MAX);

    // interline_glue has no status; its answer is defined.
    InterlineSkip skip;
    skip.baselineskip.width = SMALL;
    Node above;
    CHECK(interline_glue(BIG, wide, skip, above));

    const ParShape shape = ParShape::hanging(-BIG, SMALL, 1);
    CHECK_EQ(shape.for_line(5).width, -SCALED_MAX);

    // What a font answers is a dimension too.
    const std::vector<Node> chars = {char_node(FontKey::make(0, 10 * PT), 'a'),
                                     char_node(FontKey::make(0, 10 * PT), 'b'),
                                     char_node(FontKey::make(0, 10 * PT), 'c')};
    const CharMeasure liar{&hostile_measure, nullptr};
    const PackResult lied = hpack(arena, chars, 0, PackMode::ADDITIONAL, liar);
    out.clear();
    (void)ship(arena, lied.box, chars, Point{}, liar, out);

    // The breaker on the same material, by both algorithms and both ways of
    // judging a line.
    for (int totals = 0; totals < 2; ++totals) {
        for (int first = 0; first < 2; ++first) {
            LineBreaker breaker;
            breaker.use_running_totals(totals != 0);
            LineBreakParams params;
            params.first_fit = first != 0;
            params.left_skip = huge;
            params.right_skip = huge;
            params.emergency_stretch = BIG;
            params.line_penalty = SMALL;
            params.adj_demerits = SMALL;
            Paragraph paragraph;
            std::vector<Node> words = list;
            words.push_back(penalty(SMALL));
            words.push_back(glue(huge));
            words.push_back(kern(SMALL));
            words.push_back(penalty(-50));
            words.push_back(glue(huge));
            breaker.break_paragraph(arena, words, ParShape::uniform(first != 0 ? -BIG : BIG, BIG),
                                    params, CharMeasure(), paragraph);
            CHECK(!paragraph.lines.empty());
            breaker.break_paragraph(arena, words, ParShape::hanging(BIG, SMALL, 2), params,
                                    CharMeasure(), paragraph);
            CHECK(!paragraph.lines.empty());
        }
    }
}

// A glue order is a byte of a node somebody built. It indexed GlueTotal's four
// amounts as it stood (ASan: stack-buffer-overflow in GlueTotal::add for order
// 4, the first past the end); the running totals of the breaker already masked it.
TEST(item_d_a_glue_order_outside_the_enum_is_masked)
{
    NodeArena arena;
    Node corrupt = glue(GlueSpec{PT, PT, GlueOrder::NORMAL, PT, GlueOrder::NORMAL});
    corrupt.stretch_order = static_cast<GlueOrder>(4);
    corrupt.shrink_order = static_cast<GlueOrder>(255);
    const std::vector<Node> list = {corrupt, corrupt};
    const PackResult wide = hpack(arena, list, 10 * PT, PackMode::EXACTLY, CharMeasure());
    CHECK(wide.error == PackError::NONE);
    CHECK_EQ(wide.natural, 2 * PT);
    const PackResult narrow = vpack(arena, list, PT, PackMode::EXACTLY);
    CHECK(narrow.error == PackError::NONE);
    std::vector<DrawCmd> out;
    CHECK(ship(arena, wide.box, list, Point{}, CharMeasure(), out) == ShipError::NONE);
    GlueTotal total;
    CHECK(total.add(PT, static_cast<GlueOrder>(7)));
    CHECK_EQ(total[static_cast<GlueOrder>(7)], PT);
    CHECK(!total.add(BIG, GlueOrder::FILLL) || !total.add(BIG, GlueOrder::FILLL));
}

// --- Review item E: the work budget of total fit ---------------------------------------

namespace
{

// `words` words of five characters with stretchable spaces between them.
void prose(std::vector<Node>& out, int words, uint64_t seed)
{
    Random random(seed);
    const FontKey font = FontKey::make(0, 10 * PT);
    const GlueSpec space{3 * PT, 2 * PT, GlueOrder::NORMAL, PT, GlueOrder::NORMAL};
    out.clear();
    for (int w = 0; w < words; ++w) {
        if (w != 0)
            out.push_back(glue(space));
        const int64_t letters = 2 + random.below(7);
        for (int64_t c = 0; c < letters; ++c)
            out.push_back(char_node(font, static_cast<uint32_t>('a' + random.below(26))));
    }
}

bool same_lines(const NodeArena& arena_a, const Paragraph& a, const NodeArena& arena_b,
                const Paragraph& b)
{
    if (a.lines.size() != b.lines.size() || a.breaks.size() != b.breaks.size())
        return false;
    for (size_t i = 0; i < a.breaks.size(); ++i) {
        if (a.breaks[i].position != b.breaks[i].position || a.breaks[i].kind != b.breaks[i].kind)
            return false;
    }
    for (size_t i = 0; i < a.lines.size(); ++i) {
        if (!deep_equal(arena_a, a.lines[i].box, arena_b, b.lines[i].box))
            return false;
    }
    return true;
}

LineBreakParams ragged_total_fit()
{
    LineBreakParams params;
    params.right_skip = GlueSpec{0, 1, GlueOrder::FIL, 0, GlueOrder::NORMAL};
    params.tolerance = 10000;
    return params;
}

} // namespace

// Ragged right by total fit: every line is feasible, so every breakpoint stays
// active and the work is quadratic in the paragraph (measured through the
// public API: 5.9 ms, 106 ms, 1.76 s, 29.7 s for 1, 4, 16 and 64 copies of a
// 635-node paragraph). Past the budget the paragraph is first fit's, and says
// so; this one is 20,000 words.
TEST(item_e_total_fit_stops_at_its_work_budget_and_first_fit_takes_over)
{
    std::vector<Node> words;
    prose(words, 20000, 1);
    for (int totals = 0; totals < 2; ++totals) {
        CHECK_CONTEXT("running totals %d", totals);
        NodeArena arena;
        LineBreaker breaker;
        breaker.use_running_totals(totals != 0);
        Paragraph optimal;
        const double start = seconds_now();
        CHECK(breaker.break_paragraph(arena, words, ParShape::uniform(300 * PT), ragged_total_fit(),
                                      MEASURE, optimal));
        const double took = seconds_now() - start;
        CHECK(optimal.budget_exhausted);
        CHECK(optimal.pass_used == BreakPass::FIRST_FIT);
        CHECK_EQ(optimal.total_demerits, int64_t(0));
        CHECK(optimal.lines.size() > 500);
        // The budget is about 0.1 s of work in an optimised build and a few
        // seconds under the sanitisers; without it this is minutes.
        CHECK(took < 60.0);
        std::printf("    20,000 words ragged, totals %d: %.2f s, %zu lines, work %llu\n", totals,
                    took, optimal.lines.size(),
                    static_cast<unsigned long long>(breaker.work_done()));

        // The lines are exactly what asking for first fit gives.
        NodeArena arena_first;
        LineBreakParams first = ragged_total_fit();
        first.first_fit = true;
        Paragraph greedy;
        LineBreaker second;
        second.use_running_totals(totals != 0);
        CHECK(second.break_paragraph(arena_first, words, ParShape::uniform(300 * PT), first,
                                     MEASURE, greedy));
        CHECK(!greedy.budget_exhausted);
        CHECK(same_lines(arena, optimal, arena_first, greedy));
    }
}

// Below the budget nothing changes: the same paragraph broken with the default
// budget and with the largest one there is. And the budget is a property of
// the paragraph, not of the breaker's history: a breaker that has just given
// up breaks the next paragraph optimally.
TEST(item_e_below_the_budget_the_breaking_is_the_optimisers)
{
    const int64_t rounds = 20 * scale();
    LineBreaker defaulted;
    LineBreaker unbounded;
    unbounded.set_work_budget(std::numeric_limits<uint64_t>::max());
    CHECK_EQ(unbounded.work_budget(), MAX_LINE_BREAK_WORK_BUDGET);
    CHECK_EQ(defaulted.work_budget(), DEFAULT_LINE_BREAK_WORK_BUDGET);
    uint64_t most = 0;
    for (int64_t round = 0; round < rounds; ++round) {
        CHECK_CONTEXT("round %lld", static_cast<long long>(round));
        std::vector<Node> words;
        prose(words, 20 + static_cast<int>(round % 20) * 10, static_cast<uint64_t>(round) + 7);
        LineBreakParams params = round % 2 == 0 ? LineBreakParams() : ragged_total_fit();
        if (round % 3 == 0)
            params.emergency_stretch = 20 * PT;
        NodeArena arena_a;
        NodeArena arena_b;
        Paragraph a;
        Paragraph b;
        defaulted.break_paragraph(arena_a, words, ParShape::uniform(250 * PT), params, MEASURE, a);
        unbounded.break_paragraph(arena_b, words, ParShape::uniform(250 * PT), params, MEASURE, b);
        CHECK(!a.budget_exhausted);
        CHECK(!b.budget_exhausted);
        CHECK(a.pass_used == b.pass_used);
        CHECK_EQ(a.total_demerits, b.total_demerits);
        CHECK(same_lines(arena_a, a, arena_b, b));
        CHECK_EQ(defaulted.work_done(), unbounded.work_done());
        most = std::max(most, defaulted.work_done());
    }
    CHECK(most < DEFAULT_LINE_BREAK_WORK_BUDGET);
    std::printf("    largest paragraph below the budget cost %llu of %llu\n",
                static_cast<unsigned long long>(most),
                static_cast<unsigned long long>(DEFAULT_LINE_BREAK_WORK_BUDGET));

    // A small budget on a small paragraph: the fallback at every possible
    // point of giving up is first fit's breaking, never a partial one.
    std::vector<Node> words;
    prose(words, 60, 99);
    NodeArena arena_first;
    LineBreakParams first;
    first.first_fit = true;
    Paragraph greedy;
    LineBreaker().break_paragraph(arena_first, words, ParShape::uniform(200 * PT), first, MEASURE,
                                  greedy);
    NodeArena arena_full;
    Paragraph full;
    LineBreaker measure_work;
    measure_work.break_paragraph(arena_full, words, ParShape::uniform(200 * PT), LineBreakParams(),
                                 MEASURE, full);
    const uint64_t needed = measure_work.work_done();
    REQUIRE(needed > 10);
    for (uint64_t budget = 0; budget <= needed + 2; ++budget) {
        CHECK_CONTEXT("budget %llu of %llu", static_cast<unsigned long long>(budget),
                      static_cast<unsigned long long>(needed));
        LineBreaker limited;
        limited.set_work_budget(budget);
        NodeArena arena;
        Paragraph out;
        limited.break_paragraph(arena, words, ParShape::uniform(200 * PT), LineBreakParams(),
                                MEASURE, out);
        if (out.budget_exhausted) {
            CHECK(budget <= needed);
            CHECK(out.pass_used == BreakPass::FIRST_FIT);
            CHECK(same_lines(arena, out, arena_first, greedy));
        } else {
            CHECK(budget > needed);
            CHECK(same_lines(arena, out, arena_full, full));
        }
        // The same breaker, the budget lifted: optimal again.
        limited.set_work_budget(DEFAULT_LINE_BREAK_WORK_BUDGET);
        NodeArena arena_again;
        Paragraph again;
        limited.break_paragraph(arena_again, words, ParShape::uniform(200 * PT), LineBreakParams(),
                                MEASURE, again);
        CHECK(!again.budget_exhausted);
        CHECK(same_lines(arena_again, again, arena_full, full));
    }
}

// A candidate that has to be laid out (here: a paragraph with a discretionary)
// costs its length, so the budget bounds the nodes walked and not only the
// candidates counted: one long line of words with nowhere it must break.
TEST(item_e_the_budget_counts_the_length_of_a_candidate_that_is_walked)
{
    std::vector<Node> words;
    prose(words, 6000, 5);
    NodeArena arena;
    words.push_back(arena.add_disc({}, {}));
    LineBreaker breaker;
    Paragraph out;
    const double start = seconds_now();
    breaker.break_paragraph(arena, words, ParShape::uniform(MAX_DIMEN), ragged_total_fit(), MEASURE,
                            out);
    CHECK(out.budget_exhausted);
    CHECK(seconds_now() - start < 60.0);
    CHECK(!out.lines.empty());
}

// --- Review item F: an index the arena does not hold -----------------------------------

// A list built by hand is data. A BOX or DISC node naming nothing went through
// IMTYPESET_CONTRACT (an assertion in a debug build) and, where assertions are
// off, packed and shipped as an empty box with no error at all.
TEST(item_f_an_index_the_arena_does_not_hold_is_a_status_and_not_an_assertion)
{
    const ViolationScope scope;
    NodeArena arena;
    const std::vector<Node> list = {box_node(77), kern(PT)};
    const PackResult h = hpack(arena, list, 0, PackMode::ADDITIONAL, CharMeasure());
    CHECK(h.error == PackError::NODE_NOT_IN_ARENA);
    CHECK_EQ(h.box.width, PT);
    CHECK(vpack(arena, list).error == PackError::NODE_NOT_IN_ARENA);

    std::vector<DrawCmd> out;
    CHECK(ship(arena, h.box, list, Point{}, CharMeasure(), out) == ShipError::NODE_NOT_IN_ARENA);
    Box page;
    page.kind = BoxKind::VLIST;
    CHECK(ship(arena, page, list, Point{}, CharMeasure(), out) == ShipError::NODE_NOT_IN_ARENA);
    CHECK(ship(arena, box_node(77), Point{}, CharMeasure(), out) == ShipError::NODE_NOT_IN_ARENA);
    CHECK(ship(arena, kern(PT), Point{}, CharMeasure(), out) == ShipError::ROOT_NOT_A_BOX);
    CHECK(out.empty());

    std::vector<Node> vlist;
    CHECK_EQ(append_to_vlist(arena, vlist, box_node(77), 5 * PT, InterlineSkip()), 5 * PT);
    CHECK_EQ(vlist.size(), size_t(1));

    // The breaker: a stray box among the words, and a stray discretionary,
    // with every sign of replace count.
    for (int totals = 0; totals < 2; ++totals) {
        for (int first = 0; first < 2; ++first) {
            LineBreaker breaker;
            breaker.use_running_totals(totals != 0);
            LineBreakParams params;
            params.first_fit = first != 0;
            Paragraph paragraph;
            std::vector<Node> words;
            prose(words, 40, 3);
            words.insert(words.begin() + 20, box_node(4000000000u));
            CHECK(!breaker.break_paragraph(arena, words, ParShape::uniform(100 * PT), params,
                                           MEASURE, paragraph));
            CHECK(paragraph.error == LineBreakError::MALFORMED_LIST);
            CHECK(paragraph.pack_error == PackError::NODE_NOT_IN_ARENA);
            CHECK(paragraph.lines.size() > 1);

            prose(words, 40, 3);
            words.insert(words.begin() + 20, disc_node(12));
            CHECK(!breaker.break_paragraph(arena, words, ParShape::uniform(100 * PT), params,
                                           MEASURE, paragraph));
            CHECK(paragraph.error == LineBreakError::MALFORMED_LIST);
            CHECK(paragraph.pack_error == PackError::NODE_NOT_IN_ARENA);
            CHECK(paragraph.lines.size() > 1);
        }
    }
    CHECK_EQ(g_violations, 0);
}

// --- The public-api report's surprise: a sentence space past MAX_DIMEN ------------------

// src/style holds what a font answers to MAX_DIMEN, and then made a sentence
// space out of it that was not: from a space of MAX_DIMEN, a width of 1.33 and
// a stretch of 1.5 times MAX_DIMEN, and for a space width nobody clamped a
// signed overflow in `stretch * space_factor` (UBSan). The glue is held to
// MAX_DIMEN where it is made; inside the bound nothing changes.
TEST(interword_glue_stays_within_max_dimen)
{
    const ViolationScope scope;
    static const Scaled WIDTHS[] = {
        0,       1,   UNITY, MAX_DIMEN / 2, MAX_DIMEN - 1, MAX_DIMEN, MAX_DIMEN + 1,
        BIG / 2, BIG, SMALL, -MAX_DIMEN,    -UNITY};
    for (const Scaled width : WIDTHS) {
        for (const int factor : {1, 999, 1000, 1250, 2000, 3000, 32767}) {
            for (const Spacing spacing :
                 {Spacing::NONFRENCH, Spacing::FRENCH, Spacing::EXTRA_SPACE_ONLY}) {
                CHECK_CONTEXT("width %lld, factor %d", static_cast<long long>(width), factor);
                const GlueSpec spec = interword_glue(width, factor, spacing);
                CHECK(spec.width >= -MAX_DIMEN && spec.width <= MAX_DIMEN);
                CHECK(spec.stretch >= -MAX_DIMEN && spec.stretch <= MAX_DIMEN);
                CHECK(spec.shrink >= -MAX_DIMEN && spec.shrink <= MAX_DIMEN);
            }
        }
    }
    // Inside the bound the glue is the reference's: these are its numbers for
    // a 3.33 pt space after a full stop.
    const GlueSpec ordinary = interword_glue(218235, 3000, Spacing::NONFRENCH);
    CHECK_EQ(ordinary.width, Scaled(290980));
    CHECK_EQ(ordinary.stretch, Scaled(327354));
    CHECK_EQ(ordinary.shrink, Scaled(24248));
}

// --- Review item K: an enum outside its range in the golden writer ---------------------

// core_golden::token indexed its tables with `% 3` and `& 3`, so a corrupt
// enum printed as a legal token and compared equal to a golden.
TEST(item_k_a_corrupt_enum_has_no_legal_token)
{
    using core_golden::token;
    CHECK_EQ(std::string(token(GlueSign::SHRINKING)), std::string("shrinking"));
    CHECK_EQ(std::string(token(KernKind::ACCENT)), std::string("accent"));
    CHECK_EQ(std::string(token(GlueOrder::FILLL)), std::string("filll"));
    const std::string none = token(static_cast<GlueSign>(3));
    CHECK_NE(none, std::string("normal"));
    CHECK_EQ(std::string(token(static_cast<GlueSign>(7))), none);
    CHECK_EQ(std::string(token(static_cast<KernKind>(4))), none);
    CHECK_EQ(std::string(token(static_cast<GlueOrder>(4))), none);
    CHECK_EQ(std::string(token(static_cast<GlueOrder>(255))), none);
    CHECK_EQ(std::string(token(static_cast<BoxKind>(2))), none);
}

// --- Fuzz: well-formed lists against invariants ----------------------------------------

namespace
{

GlueOrder order_of(Random& random)
{
    return random.one_in(12) ? static_cast<GlueOrder>(1 + random.below(3)) : GlueOrder::NORMAL;
}

GlueSpec glue_of(Random& random)
{
    GlueSpec spec;
    spec.width = random.below(8 * UNITY);
    if (random.one_in(15))
        spec.width = -random.below(4 * UNITY);
    spec.stretch = random.one_in(6) ? 0 : random.below(5 * UNITY);
    spec.stretch_order = order_of(random);
    spec.shrink = random.one_in(4) ? 0 : random.below(3 * UNITY);
    spec.shrink_order = order_of(random);
    return spec;
}

// A box of characters, kerns and rules, with boxes of the same inside it down
// to `depth`. No glue: the invariants below add up set glue at one level.
Node rigid_box(Random& random, NodeArena& arena, uint32_t& next_id, int depth)
{
    std::vector<Node> inside;
    const int64_t count = random.below(5);
    const FontKey font = FontKey::make(1, 12 * UNITY);
    for (int64_t i = 0; i < count; ++i) {
        switch (random.below(depth > 0 ? 4 : 3)) {
        case 0:
            inside.push_back(char_node(font, next_id++));
            break;
        case 1:
            inside.push_back(kern(random.below(2 * UNITY) - UNITY / 2));
            break;
        case 2:
            inside.push_back(rule(random.below(3 * UNITY), random.one_in(2) ? RUNNING : UNITY,
                                  random.one_in(2) ? RUNNING : UNITY / 4));
            break;
        default:
            inside.push_back(rigid_box(random, arena, next_id, depth - 1));
            break;
        }
    }
    PackResult packed = hpack(arena, inside, 0, PackMode::ADDITIONAL, MEASURE);
    if (random.one_in(4))
        packed.box.shift_amount = random.below(4 * UNITY) - 2 * UNITY;
    return arena.add_box(packed.box, inside);
}

// A paragraph's worth of well-formed material. Every character has its own
// codepoint, counted from `next_id`, so that it can be found again.
void material(Random& random, NodeArena& arena, std::vector<Node>& out, uint32_t& next_id,
              bool with_discs)
{
    out.clear();
    const int64_t length = 1 + random.below(220);
    const FontKey fonts[2] = {FontKey::make(0, 10 * UNITY), FontKey::make(1, 12 * UNITY)};
    bool in_math = false;
    for (int64_t i = 0; i < length; ++i) {
        const int64_t pick = random.below(100);
        if (pick < 55) {
            out.push_back(char_node(fonts[random.below(2)], next_id++));
        } else if (pick < 78) {
            out.push_back(glue(glue_of(random)));
        } else if (pick < 83) {
            out.push_back(
                kern(random.below(2 * UNITY) - UNITY / 2, static_cast<KernKind>(random.below(3))));
        } else if (pick < 88) {
            static const Scaled values[] = {0,    50,    -50,         200,
                                            9999, -9999, INF_PENALTY, EJECT_PENALTY};
            out.push_back(penalty(values[random.below(8)]));
        } else if (pick < 91) {
            out.push_back(in_math ? math_off(random.below(UNITY)) : math_on(random.below(UNITY)));
            in_math = !in_math;
        } else if (pick < 94) {
            out.push_back(rule(random.below(3 * UNITY), random.one_in(2) ? RUNNING : UNITY,
                               random.one_in(2) ? RUNNING : UNITY / 4));
        } else if (pick < 98 || !with_discs) {
            out.push_back(rigid_box(random, arena, next_id, 3));
        } else {
            std::vector<Node> pre;
            std::vector<Node> post;
            if (!random.one_in(3))
                pre.push_back(char_node(fonts[0], '-'));
            if (random.one_in(3))
                post.push_back(char_node(fonts[0], '='));
            out.push_back(arena.add_disc(pre, post, static_cast<int32_t>(random.below(3))));
        }
    }
    if (in_math)
        out.push_back(math_off());
}

// Every CHAR, BOX and RULE of a list, in order, as (type, identity).
void rigid_items(NodeView nodes, std::vector<uint64_t>& out)
{
    for (const Node& node : nodes) {
        if (node.type == NodeType::CHAR)
            out.push_back((uint64_t(1) << 40) | node.u);
        else if (node.type == NodeType::BOX)
            out.push_back((uint64_t(2) << 40) | node.u);
        else if (node.type == NodeType::RULE)
            out.push_back((uint64_t(3) << 40) | static_cast<uint64_t>(node.a & 0xFFFFFFFF));
    }
}

// The natural width of an hlist, summed here and not by hpack.
Scaled natural_width(const NodeArena& arena, NodeView nodes)
{
    Scaled width = 0;
    for (const Node& node : nodes) {
        switch (node.type) {
        case NodeType::CHAR:
            width += MEASURE(node.font(), node.codepoint()).width;
            break;
        case NodeType::BOX:
            width += arena.box(node).width;
            break;
        case NodeType::RULE:
        case NodeType::GLUE:
        case NodeType::KERN:
        case NodeType::MATH_ON:
        case NodeType::MATH_OFF:
            width += node.a;
            break;
        case NodeType::PENALTY:
        case NodeType::DISC:
            break;
        }
    }
    return width;
}

struct Census {
    int64_t paragraphs = 0;
    int64_t lines = 0;
    int64_t commands = 0;
    int64_t hostile = 0;
    int64_t hostile_errors = 0;
};

// One line of a broken paragraph: the packing against a sum made here, and the
// shipped line against the packing.
void check_line(const NodeArena& arena, const BrokenLine& line, const LineSpec& spec,
                std::vector<DrawCmd>& cmds, Census& census)
{
    const Box& box = arena.box(line.box);
    const NodeView contents = arena.view(box.contents);
    CHECK_EQ(box.width, spec.width);
    CHECK_EQ(box.shift_amount, spec.indent);
    CHECK_EQ(line.packed.natural, natural_width(arena, contents));
    CHECK_EQ(line.packed.excess, spec.width - line.packed.natural);

    cmds.clear();
    REQUIRE(ship(arena, line.box, Point{0, 0}, MEASURE, cmds) == ShipError::NONE);
    census.commands += static_cast<int64_t>(cmds.size());
    REQUIRE(!cmds.empty());
    REQUIRE(cmds[0].is(CmdType::LINE));

    // The glue of the line as it was set, and everything else at its natural
    // width, come to the line's width when the glue could be set: stretched
    // or shrunk within what there was. The ratio is a double and each total
    // is rounded once, so the sum is within a scaled point.
    Scaled set = 0;
    Scaled natural = 0;
    uint32_t next_index = cmds[0].start();
    size_t top_level_glue = 0;
    for (const Node& node : contents)
        top_level_glue += node.type == NodeType::GLUE ? 1 : 0;
    size_t set_glue = 0;
    for (size_t i = 1; i < cmds.size(); ++i) {
        const DrawCmd& cmd = cmds[i];
        if (cmd.is(CmdType::GLYPH) || cmd.is(CmdType::SET_GLUE) || cmd.is(CmdType::ATOM)) {
            CHECK_EQ(cmd.index, next_index);
            ++next_index;
        }
        if (cmd.is(CmdType::SET_GLUE)) {
            set += cmd.width;
            natural += cmd.natural();
            ++set_glue;
        }
    }
    CHECK_EQ(cmds[0].end, next_index);
    CHECK_EQ(set_glue, top_level_glue);
    const bool settable =
        box.glue_sign != GlueSign::NORMAL && line.packed.fitness != PackFitness::OVERFULL;
    if (settable) {
        const Scaled off = (line.packed.natural - natural + set) - spec.width;
        CHECK(off >= -1 && off <= 1);
    } else if (box.glue_sign == GlueSign::NORMAL) {
        CHECK_EQ(set, natural);
    }
}

} // namespace

TEST(fuzz_well_formed_paragraphs_keep_their_invariants)
{
    const int64_t rounds = 800 * scale();
    Census census;
    LineBreaker breaker;
    Paragraph paragraph;
    std::vector<Node> nodes;
    std::vector<DrawCmd> cmds;
    std::vector<uint64_t> before;
    std::vector<uint64_t> after;
    double slowest = 0.0;
    for (int64_t round = 0; round < rounds; ++round) {
        CHECK_CONTEXT("round %lld", static_cast<long long>(round));
        Random random(static_cast<uint64_t>(round) + 1);
        NodeArena arena;
        uint32_t next_id = 1000;
        const bool with_discs = round % 4 == 0;
        material(random, arena, nodes, next_id, with_discs);

        LineBreakParams params;
        params.first_fit = round % 3 == 0;
        if (round % 5 == 0)
            params.right_skip = GlueSpec{0, 1, GlueOrder::FIL, 0, GlueOrder::NORMAL};
        if (round % 7 == 0)
            params.emergency_stretch = random.below(30 * UNITY);
        if (round % 11 == 0)
            params.left_skip = GlueSpec{random.below(4 * UNITY)};
        const Scaled width = (20 + random.below(300)) * UNITY;
        const ParShape shape = round % 6 == 0 ? ParShape::hanging(width, 12 * UNITY, 2)
                                              : ParShape::uniform(width, random.below(3) * UNITY);
        breaker.use_running_totals(round % 2 == 0);

        const double start = seconds_now();
        const bool ok = breaker.break_paragraph(arena, nodes, shape, params, MEASURE, paragraph);
        CHECK(ok);
        CHECK(paragraph.error == LineBreakError::NONE);
        CHECK(!paragraph.budget_exhausted);
        REQUIRE(!paragraph.lines.empty());
        REQUIRE_EQ(paragraph.lines.size(), paragraph.breaks.size());
        CHECK(paragraph.breaks.back().kind == BreakKind::PARAGRAPH_END);
        CHECK_EQ(paragraph.breaks.back().position, paragraph.prepared_length);
        for (size_t i = 1; i < paragraph.breaks.size(); ++i)
            CHECK(paragraph.breaks[i].position > paragraph.breaks[i - 1].position);

        // Nothing that is not discardable is lost or set twice. A taken
        // discretionary replaces nodes on purpose, so those lists are held to
        // "no more than there were".
        before.clear();
        after.clear();
        rigid_items(nodes, before);
        for (const BrokenLine& line : paragraph.lines)
            rigid_items(arena.contents(line.box), after);
        if (!with_discs) {
            CHECK_EQ(after.size(), before.size());
            CHECK(after == before);
        } else {
            // Pre- and post-break text adds '-' and '=' characters.
            size_t kept = 0;
            for (const uint64_t item : after)
                kept += (item & 0xFFFFFFFF) >= 1000 || (item >> 40) != 1 ? 1 : 0;
            CHECK(kept <= before.size());
        }

        std::vector<Node> page;
        for (size_t i = 0; i < paragraph.lines.size(); ++i) {
            CHECK_CONTEXT("line %zu", i);
            check_line(arena, paragraph.lines[i], shape.for_line(static_cast<int64_t>(i) + 1), cmds,
                       census);
            page.push_back(paragraph.lines[i].box);
        }

        // The page of lines: one LINE per line, in order, indices continuing.
        const PackResult packed = vpack(arena, page);
        CHECK(packed.error == PackError::NONE);
        cmds.clear();
        REQUIRE(ship(arena, packed.box, page, Point{0, 0}, MEASURE, cmds) == ShipError::NONE);
        size_t line_cmds = 0;
        uint32_t expected_start = 0;
        for (const DrawCmd& cmd : cmds) {
            if (!cmd.is(CmdType::LINE))
                continue;
            ++line_cmds;
            CHECK_EQ(cmd.start(), expected_start);
            expected_start = cmd.end;
        }
        CHECK_EQ(line_cmds, paragraph.lines.size());

        const double took = seconds_now() - start;
        slowest = std::max(slowest, took);
        // Each paragraph is a few hundred nodes; a second is a loop.
        CHECK(took < 10.0);
        ++census.paragraphs;
        census.lines += static_cast<int64_t>(paragraph.lines.size());
    }
    std::printf("    well-formed: %lld paragraphs, %lld lines, %lld commands, slowest %.3f s\n",
                static_cast<long long>(census.paragraphs), static_cast<long long>(census.lines),
                static_cast<long long>(census.commands), slowest);
    CHECK(census.lines > census.paragraphs);
}

// --- Fuzz: hostile lists only have to come back ----------------------------------------

namespace
{

Scaled hostile_dimen(Random& random)
{
    switch (random.below(10)) {
    case 0:
        return BIG;
    case 1:
        return SMALL;
    case 2:
        return RUNNING;
    case 3:
        return MAX_DIMEN + random.below(3) - 1;
    case 4:
        return -MAX_DIMEN + random.below(3) - 1;
    case 5:
        return static_cast<Scaled>(random.next());
    case 6:
        return 0;
    default:
        return random.below(40 * UNITY) - 10 * UNITY;
    }
}

double hostile_ratio(Random& random)
{
    switch (random.below(8)) {
    case 0:
        return std::numeric_limits<double>::quiet_NaN();
    case 1:
        return std::numeric_limits<double>::infinity();
    case 2:
        return -std::numeric_limits<double>::infinity();
    case 3:
        return 1.0e300;
    case 4:
        return -1.0e300;
    default:
        return static_cast<double>(random.below(4000)) / 1000.0 - 1.0;
    }
}

// A node with any byte in any field. Types run past the enum on purpose.
Node hostile_node(Random& random, const NodeArena& arena)
{
    Node node;
    node.type = static_cast<NodeType>(random.one_in(20) ? random.below(256) : random.below(9));
    node.kind = static_cast<uint8_t>(random.one_in(6) ? random.below(256) : random.below(3));
    node.stretch_order =
        static_cast<GlueOrder>(random.one_in(8) ? random.below(256) : random.below(4));
    node.shrink_order =
        static_cast<GlueOrder>(random.one_in(8) ? random.below(256) : random.below(4));
    node.a = hostile_dimen(random);
    node.b = hostile_dimen(random);
    node.c = hostile_dimen(random);
    const size_t held = node.type == NodeType::DISC ? arena.disc_count() : arena.box_count();
    if (node.type == NodeType::BOX || node.type == NodeType::DISC)
        node.u = random.one_in(5) || held == 0
                     ? static_cast<uint32_t>(random.next())
                     : static_cast<uint32_t>(random.below(static_cast<int64_t>(held) + 1));
    else
        node.u = static_cast<uint32_t>(random.next());
    return node;
}

Box hostile_box(Random& random)
{
    Box box;
    box.kind = static_cast<BoxKind>(random.one_in(10) ? random.below(256) : random.below(2));
    box.glue_sign = static_cast<GlueSign>(random.one_in(10) ? random.below(256) : random.below(3));
    box.glue_order =
        static_cast<GlueOrder>(random.one_in(10) ? random.below(256) : random.below(4));
    box.width = hostile_dimen(random);
    box.height = hostile_dimen(random);
    box.depth = hostile_dimen(random);
    box.shift_amount = hostile_dimen(random);
    box.glue_set = hostile_ratio(random);
    return box;
}

} // namespace

// Nothing here is checked but that it comes back, with ASan and UBSan
// watching, inside a time box, and that an error leaves `out` alone. The
// accessors of Node assert the kind they are for; the walks only use them
// under the matching case, which a type outside the enum never reaches.
TEST(fuzz_hostile_lists_come_back)
{
    const ViolationScope scope;
    const int64_t rounds = 1500 * scale();
    Census census;
    LineBreaker breaker;
    Paragraph paragraph;
    std::vector<Node> nodes;
    std::vector<Node> inside;
    std::vector<DrawCmd> cmds;
    for (int64_t round = 0; round < rounds; ++round) {
        Random random(static_cast<uint64_t>(round) * 2654435761u + 17);
        NodeArena arena;
        const CharMeasure measure =
            round % 3 == 0
                ? CharMeasure()
                : (round % 3 == 1
                       ? MEASURE
                       : CharMeasure{&hostile_measure,
                                     reinterpret_cast<void*>(static_cast<uintptr_t>(round + 1))});
        const double start = seconds_now();

        // Boxes and discretionaries of hostile nodes, naming one another,
        // themselves and nothing; then spans the arena does not hold, marks
        // made by hand, and a rewind in the middle.
        const int64_t containers = random.below(40);
        for (int64_t i = 0; i < containers; ++i) {
            inside.clear();
            const int64_t count = random.below(6);
            for (int64_t k = 0; k < count; ++k)
                inside.push_back(hostile_node(random, arena));
            if (random.one_in(4)) {
                arena.add_disc(inside, inside, static_cast<int32_t>(random.next()));
            } else if (random.one_in(9)) {
                Box header = hostile_box(random);
                header.contents.first = static_cast<uint32_t>(random.next());
                header.contents.count = static_cast<uint32_t>(random.next());
                arena.add_box(header);
            } else {
                arena.add_box(hostile_box(random), inside);
            }
            if (random.one_in(30)) {
                NodeArena::Mark mark;
                mark.nodes = static_cast<uint32_t>(
                    random.below(static_cast<int64_t>(arena.node_count()) + 2));
                mark.boxes = static_cast<uint32_t>(
                    random.below(static_cast<int64_t>(arena.box_count()) + 2));
                mark.discs = static_cast<uint32_t>(
                    random.below(static_cast<int64_t>(arena.disc_count()) + 2));
                arena.rewind(mark);
            }
        }

        nodes.clear();
        const int64_t length = random.below(120);
        for (int64_t i = 0; i < length; ++i)
            nodes.push_back(hostile_node(random, arena));

        const Scaled target = hostile_dimen(random);
        const PackMode mode = random.one_in(2) ? PackMode::EXACTLY : PackMode::ADDITIONAL;
        const PackResult h = hpack(arena, nodes, target, mode, measure);
        const PackResult v = vpack(arena, nodes, target, mode, hostile_dimen(random));
        census.hostile_errors += h.error != PackError::NONE ? 1 : 0;

        cmds.clear();
        cmds.push_back(DrawCmd::rule_rect(1, 2, 3, 4));
        const Point origin{hostile_dimen(random), hostile_dimen(random)};
        for (int which = 0; which < 3; ++which) {
            const Box header = which == 0 ? h.box : (which == 1 ? v.box : hostile_box(random));
            const size_t size_before = cmds.size();
            const ShipError shipped = ship(arena, header, nodes, origin, measure, cmds);
            if (shipped != ShipError::NONE) {
                CHECK_EQ(cmds.size(), size_before);
                ++census.hostile_errors;
            }
            // A bound of the caller's own, including none at all.
            const ShipError bounded = ship(arena, header, nodes, origin, measure, cmds,
                                           static_cast<uint32_t>(random.below(50)));
            (void)bounded;
        }
        for (uint32_t b = 0; b < arena.box_count() && b < 8; ++b) {
            const size_t size_before = cmds.size();
            if (ship(arena, box_node(b), origin, measure, cmds) != ShipError::NONE)
                CHECK_EQ(cmds.size(), size_before);
            NodeArena other;
            const Node imported = other.import(arena, box_node(b));
            (void)deep_equal(arena, box_node(b), other, imported);
            (void)atom_box(other, imported);
        }
        if (!nodes.empty()) {
            NodeArena other;
            const Node imported = other.import(arena, nodes[0]);
            (void)deep_equal(arena, nodes[0], other, imported);
            (void)deep_equal(arena, nodes, arena, nodes);
        }

        std::vector<Node> vlist;
        Scaled prev_depth = hostile_dimen(random);
        InterlineSkip skip;
        skip.baselineskip = GlueSpec{hostile_dimen(random)};
        skip.lineskiplimit = hostile_dimen(random);
        for (const Node& node : nodes) {
            if (node.type == NodeType::BOX || node.type == NodeType::RULE)
                prev_depth = append_to_vlist(arena, vlist, node, prev_depth, skip);
        }
        (void)vpack(arena, vlist);

        LineBreakParams params;
        // Every other list under the product's rules: tex.web's classes of
        // line numbers are a second walk of the active list.
        if (round % 2 == 1)
            params.rules = TexRules::product();
        params.first_fit = random.one_in(2);
        params.pretolerance = static_cast<int64_t>(hostile_dimen(random));
        params.tolerance = static_cast<int64_t>(hostile_dimen(random));
        params.line_penalty = static_cast<int64_t>(hostile_dimen(random));
        params.adj_demerits = static_cast<int64_t>(hostile_dimen(random));
        params.double_hyphen_demerits = static_cast<int64_t>(hostile_dimen(random));
        params.final_hyphen_demerits = static_cast<int64_t>(hostile_dimen(random));
        params.hyphen_penalty = static_cast<int64_t>(hostile_dimen(random));
        params.ex_hyphen_penalty = static_cast<int64_t>(hostile_dimen(random));
        params.emergency_stretch = hostile_dimen(random);
        if (random.one_in(2)) {
            params.left_skip = hostile_node(random, arena).type == NodeType::GLUE
                                   ? GlueSpec{hostile_dimen(random)}
                                   : GlueSpec();
            params.right_skip =
                GlueSpec{hostile_dimen(random), hostile_dimen(random),
                         static_cast<GlueOrder>(random.below(256)), hostile_dimen(random),
                         static_cast<GlueOrder>(random.below(256))};
            params.par_fill_skip = GlueSpec{hostile_dimen(random), hostile_dimen(random),
                                            static_cast<GlueOrder>(random.below(4))};
        }
        LineSpec specs[3] = {{hostile_dimen(random), hostile_dimen(random)},
                             {hostile_dimen(random), hostile_dimen(random)},
                             {100 * UNITY, 0}};
        const ParShape shape =
            round % 4 == 0 ? ParShape::of(specs, static_cast<size_t>(1 + random.below(3)))
            : round % 4 == 1
                ? ParShape::hanging(hostile_dimen(random), hostile_dimen(random), random.below(5))
            : round % 4 == 2 ? ParShape::uniform(hostile_dimen(random), hostile_dimen(random))
                             : ParShape();
        breaker.use_running_totals(random.one_in(2));
        breaker.set_work_budget(random.one_in(4) ? static_cast<uint64_t>(random.below(200))
                                                 : DEFAULT_LINE_BREAK_WORK_BUDGET);
        breaker.break_paragraph(arena, nodes, shape, params, measure, paragraph);
        CHECK_EQ(paragraph.lines.size(), paragraph.breaks.size());
        if (!shape.empty())
            CHECK(!paragraph.lines.empty());
        for (const BrokenLine& line : paragraph.lines) {
            const size_t size_before = cmds.size();
            if (ship(arena, line.box, origin, measure, cmds) != ShipError::NONE)
                CHECK_EQ(cmds.size(), size_before);
        }

        CHECK(seconds_now() - start < 10.0);
        ++census.hostile;
    }
    std::printf("    hostile: %lld lists, %lld reported an error, %d contract violations\n",
                static_cast<long long>(census.hostile),
                static_cast<long long>(census.hostile_errors), g_violations);
    CHECK(census.hostile_errors > 0);
}
