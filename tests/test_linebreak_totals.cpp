// The line breaker judges a candidate line either from running totals over the
// prepared list or by laying the candidate out and walking it (src/linebreak.h).
// The goldens hold whichever one a paragraph takes to the reference. This holds
// the two to each other, on lists no golden has: every paragraph here is broken
// both ways and the two results must be the same in every field.

#include "check.h"

#include "linebreak.h"
#include "nodes.h"
#include "pack.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace ImTypeset::detail;

namespace
{

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
    // In [0, bound).
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

// A horizontal list of the kinds a paragraph without discretionaries holds.
void list_of(Random& random, NodeArena& arena, std::vector<Node>& out)
{
    out.clear();
    const int64_t length = random.below(140);
    const FontKey fonts[2] = {FontKey::make(0, 10 * UNITY), FontKey::make(1, 12 * UNITY)};
    bool in_math = false;
    for (int64_t i = 0; i < length; ++i) {
        const int64_t pick = random.below(100);
        if (pick < 58) {
            out.push_back(
                char_node(fonts[random.below(2)], static_cast<uint32_t>(33 + random.below(90))));
        } else if (pick < 80) {
            out.push_back(glue(glue_of(random)));
        } else if (pick < 85) {
            out.push_back(kern(random.below(3 * UNITY) - UNITY,
                               random.one_in(2) ? KernKind::EXPLICIT : KernKind::NORMAL));
        } else if (pick < 91) {
            static const Scaled values[] = {0, 50, -50, 150, -150, 9999, 10000, -10000, -9999, 1};
            out.push_back(penalty(values[random.below(10)]));
        } else if (pick < 95) {
            out.push_back(in_math ? math_off(random.below(2 * UNITY))
                                  : math_on(random.below(2 * UNITY)));
            in_math = !in_math;
        } else if (pick < 98) {
            Box box;
            box.width = random.below(20 * UNITY);
            box.height = random.below(10 * UNITY);
            box.depth = random.below(4 * UNITY);
            box.shift_amount = random.below(4 * UNITY) - 2 * UNITY;
            out.push_back(arena.add_box(box));
        } else {
            out.push_back(
                rule(random.below(6 * UNITY), random.below(8 * UNITY), random.below(2 * UNITY)));
        }
    }
}

LineBreakParams params_of(Random& random)
{
    LineBreakParams params;
    if (random.one_in(3))
        params.first_fit = true;
    if (random.one_in(4))
        params.pretolerance = -1;
    if (random.one_in(4))
        params.tolerance = random.below(400);
    if (random.one_in(5))
        params.emergency_stretch = random.below(30 * UNITY);
    if (random.one_in(4))
        params.line_penalty = random.below(200);
    if (random.one_in(4))
        params.adj_demerits = random.below(20000);
    if (random.one_in(3))
        params.right_skip = GlueSpec{0, UNITY, GlueOrder::FIL, 0, GlueOrder::NORMAL};
    else if (random.one_in(4))
        params.right_skip = glue_of(random);
    if (random.one_in(5))
        params.left_skip = glue_of(random);
    if (random.one_in(8))
        params.par_fill_skip = glue_of(random);
    return params;
}

bool same_bits(double a, double b)
{
    return std::memcmp(&a, &b, sizeof a) == 0;
}

void compare(const NodeArena& arena_a, const Paragraph& a, const NodeArena& arena_b,
             const Paragraph& b)
{
    CHECK_EQ(static_cast<int>(a.error), static_cast<int>(b.error));
    CHECK_EQ(static_cast<int>(a.pack_error), static_cast<int>(b.pack_error));
    CHECK_EQ(static_cast<int>(a.pass_used), static_cast<int>(b.pass_used));
    CHECK_EQ(a.total_demerits, b.total_demerits);
    CHECK_EQ(a.prepared_length, b.prepared_length);
    REQUIRE_EQ(a.breaks.size(), b.breaks.size());
    REQUIRE_EQ(a.lines.size(), b.lines.size());
    for (size_t i = 0; i < a.breaks.size(); ++i) {
        CHECK_CONTEXT("break %zu", i);
        CHECK_EQ(a.breaks[i].position, b.breaks[i].position);
        CHECK_EQ(static_cast<int>(a.breaks[i].kind), static_cast<int>(b.breaks[i].kind));
    }
    for (size_t i = 0; i < a.lines.size(); ++i) {
        CHECK_CONTEXT("line %zu", i);
        const PackResult& p = a.lines[i].packed;
        const PackResult& q = b.lines[i].packed;
        CHECK_EQ(static_cast<int>(p.fitness), static_cast<int>(q.fitness));
        CHECK_EQ(static_cast<int>(p.error), static_cast<int>(q.error));
        CHECK_EQ(p.natural, q.natural);
        CHECK_EQ(p.excess, q.excess);
        CHECK_EQ(p.badness, q.badness);
        CHECK_EQ(p.overfull_by, q.overfull_by);
        CHECK_EQ(p.box.width, q.box.width);
        CHECK_EQ(p.box.height, q.box.height);
        CHECK_EQ(p.box.depth, q.box.depth);
        CHECK_EQ(p.box.shift_amount, q.box.shift_amount);
        CHECK_EQ(static_cast<int>(p.box.glue_sign), static_cast<int>(q.box.glue_sign));
        CHECK_EQ(static_cast<int>(p.box.glue_order), static_cast<int>(q.box.glue_order));
        CHECK(same_bits(p.box.glue_set, q.box.glue_set));
        CHECK(deep_equal(arena_a, a.lines[i].box, arena_b, b.lines[i].box));
    }
}

struct Tally {
    long paragraphs = 0;
    long lines = 0;
    long several = 0; // paragraphs of more than one line
    long first_fit = 0;
    long emergency = 0;
};

// Breaks the list of one seed both ways and compares.
void run_seed(uint64_t seed, LineBreaker& by_totals, LineBreaker& by_walking, Tally& tally)
{
    CHECK_CONTEXT("seed %llu", static_cast<unsigned long long>(seed));
    NodeArena arenas[2];
    Paragraph results[2];
    LineBreaker* breakers[2] = {&by_totals, &by_walking};
    for (int side = 0; side < 2; ++side) {
        // The same draws for both sides: everything comes from the seed.
        Random random(seed);
        std::vector<Node> nodes;
        list_of(random, arenas[side], nodes);
        const LineBreakParams params = params_of(random);
        LineSpec specs[3];
        for (LineSpec& spec : specs) {
            spec.width = (40 + random.below(360)) * UNITY;
            spec.indent = random.below(20 * UNITY);
        }
        ParShape shape;
        switch (random.below(3)) {
        case 0:
            shape = ParShape::uniform(specs[0].width, 0);
            break;
        case 1:
            shape = ParShape::hanging(specs[0].width, specs[0].indent, random.below(4));
            break;
        default:
            shape = ParShape::of(specs, static_cast<size_t>(1 + random.below(3)));
            break;
        }
        const CharMeasure measure{measure_of, nullptr};
        breakers[side]->break_paragraph(arenas[side], nodes, shape, params, measure, results[side]);
    }
    compare(arenas[0], results[0], arenas[1], results[1]);
    ++tally.paragraphs;
    tally.lines += static_cast<long>(results[0].lines.size());
    tally.several += results[0].lines.size() > 1 ? 1 : 0;
    tally.first_fit += results[0].pass_used == BreakPass::FIRST_FIT ? 1 : 0;
    tally.emergency += results[0].pass_used == BreakPass::EMERGENCY ? 1 : 0;
}

} // namespace

TEST(running_totals_and_walked_lines_break_alike)
{
    LineBreaker by_totals;
    LineBreaker by_walking;
    by_walking.use_running_totals(false);
    Tally tally;
    for (uint64_t seed = 1; seed <= 6000; ++seed)
        run_seed(seed, by_totals, by_walking, tally);
    std::printf("test_linebreak_totals: %ld paragraphs, %ld lines, %ld of more than one line, "
                "%ld first fit, %ld on the emergency pass\n",
                tally.paragraphs, tally.lines, tally.several, tally.first_fit, tally.emergency);
    // The comparison says nothing unless the lists really break.
    CHECK(tally.several > tally.paragraphs / 2);
    CHECK(tally.first_fit > 0);
    CHECK(tally.emergency > 0);
}

// Lists the totals cannot describe take the walked path whatever the switch
// says, so the two breakers agree on them too; what is checked is that they
// are refused the short cut and still break.
TEST(a_discretionary_or_a_running_rule_is_walked)
{
    const CharMeasure measure{measure_of, nullptr};
    const FontKey font = FontKey::make(0, 10 * UNITY);
    for (int which = 0; which < 3; ++which) {
        CHECK_CONTEXT("case %d", which);
        NodeArena arenas[2];
        Paragraph results[2];
        for (int side = 0; side < 2; ++side) {
            NodeArena& arena = arenas[side];
            std::vector<Node> nodes;
            for (int word = 0; word < 30; ++word) {
                for (int c = 0; c < 4; ++c)
                    nodes.push_back(char_node(font, static_cast<uint32_t>('a' + (word + c) % 26)));
                if (which == 0 && word % 3 == 0) {
                    const Node hyphen = char_node(font, '-');
                    nodes.push_back(arena.add_disc(NodeView(&hyphen, 1), NodeView()));
                }
                if (which == 1 && word == 7)
                    nodes.push_back(rule());
                for (int c = 0; c < 3; ++c)
                    nodes.push_back(char_node(font, static_cast<uint32_t>('k' + (word + c) % 10)));
                nodes.push_back(glue(
                    GlueSpec{3 * UNITY, 2 * UNITY, GlueOrder::NORMAL, UNITY, GlueOrder::NORMAL}));
            }
            LineBreaker breaker;
            breaker.use_running_totals(side == 0);
            // The third case has characters and no measure.
            breaker.break_paragraph(arena, nodes, ParShape::uniform(150 * UNITY, 0),
                                    LineBreakParams(), which == 2 ? CharMeasure() : measure,
                                    results[side]);
        }
        compare(arenas[0], results[0], arenas[1], results[1]);
        CHECK(!results[0].lines.empty());
        if (which == 1)
            CHECK_EQ(static_cast<int>(results[0].pack_error),
                     static_cast<int>(PackError::RUNNING_WIDTH_IN_HLIST));
        if (which == 2)
            CHECK_EQ(static_cast<int>(results[0].pack_error),
                     static_cast<int>(PackError::CHAR_WITHOUT_MEASURE));
    }
}
