// TexRules (src/tex_rules.h): the places where the product does what tex.web
// says and the reference does not. Section numbers are tex.web's.
//
// No TeX engine is installed on the machine these were written on, so nothing
// here compares against one. Each rule has a case small enough to work by hand
// from the section it cites; the working is in the comment above the test, and
// the same case is run under TexRules::reference() to show what the rule
// changes. Agreement with TeX is claimed for those cases and no further.
//
// Then three measurements, which are also pins: every paragraph of the core,
// para and blocks goldens broken under the reference's rules and under each
// product rule, counting the paragraphs whose breaks differ and the line boxes
// that differ; and every formula of the math goldens set both ways.
//
// With IMTYPESET_TEXFIX_SHOTS=<directory> the last test writes pictures of
// three formulas under both rule sets, to be looked at.

#include "blocks_golden.h"
#include "core_golden.h"
#include "math_golden.h"

#include "support/soft_raster.h"

#include "blocks.h"
#include "context.h"
#include "fontlib.h"
#include "imgui.h"
#include "imtypeset.h"
#include "linebreak.h"
#include "math_mlist.h"
#include "mathtext.h"
#include "paragraph.h"
#include "style.h"
#include "tex_rules.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace ImTypeset::detail;

namespace
{

constexpr Scaled PT = UNITY;

// Words as boxes of the given widths in points, with `space` between them.
std::vector<Node> words(NodeArena& arena, const std::vector<int>& widths, const GlueSpec& space)
{
    std::vector<Node> out;
    for (size_t i = 0; i < widths.size(); ++i) {
        if (i != 0)
            out.push_back(glue(space));
        Box word;
        word.width = widths[i] * PT;
        word.height = 7 * PT;
        word.depth = 2 * PT;
        out.push_back(arena.add_box(word));
    }
    return out;
}

// How many boxes of a paragraph's own material ended up on each line.
std::vector<int> boxes_per_line(const NodeArena& arena, const Paragraph& paragraph)
{
    std::vector<int> out;
    for (const BrokenLine& line : paragraph.lines) {
        int boxes = 0;
        for (const Node& node : arena.contents(line.box))
            boxes += node.is(NodeType::BOX) ? 1 : 0;
        out.push_back(boxes);
    }
    return out;
}

// One pass at a tolerance of 200, and it is the final one: no first pass, no
// emergency stretch.
LineBreakParams one_pass(TexRules rules)
{
    LineBreakParams params;
    params.pretolerance = -1;
    params.tolerance = 200;
    params.emergency_stretch = 0;
    params.rules = rules;
    return params;
}

// A face whose space is 15sp wide, whatever the key.
class FixedMetrics : public FontMetrics
{
public:
    Face face() const override { return Face::PROSE; }
    double size() const override { return 100.0; }
    double ascent() const override { return 70.0; }
    double descent() const override { return 20.0; }
    double x_height() const override { return 50.0; }
    double em() const override { return 100.0; }
    double space_width() const override { return 15.0; }
    bool has(uint32_t) const override { return true; }
    double advance(uint32_t) const override { return 10.0; }
    GlyphBBox bbox(uint32_t) const override { return GlyphBBox(); }
    double kern(uint32_t, uint32_t) const override { return 0.0; }
};

// The switches one at a time, for the measurements.
struct NamedRules {
    const char* name;
    TexRules rules;
};

std::vector<NamedRules> break_rules_one_at_a_time()
{
    std::vector<NamedRules> out;
    TexRules rules;
    rules.skips_made_shrink_finite = true;
    out.push_back({"B5 skips_made_shrink_finite", rules});
    rules = TexRules();
    rules.left_skip_omitted_when_zero = true;
    out.push_back({"B6 left_skip_omitted_when_zero", rules});
    rules = TexRules();
    rules.post_break_text_stops_pruning = true;
    out.push_back({"B7 post_break_text_stops_pruning", rules});
    rules = TexRules();
    rules.sentence_space_truncates = true;
    out.push_back({"Y1 sentence_space_truncates", rules});
    rules = TexRules();
    rules.easy_line = true;
    out.push_back({"B3a/B4 easy_line", rules});
    out.push_back({"all of product()", TexRules::product()});
    return out;
}

} // namespace

// --- The presets ----------------------------------------------------------------------

TEST(the_reference_keeps_every_departure_and_the_product_none_that_is_a_switch)
{
    const TexRules reference = TexRules::reference();
    const TexRules product = TexRules::product();
    const TexRules defaulted;
    for (const TexRules& rules : {reference, defaulted}) {
        CHECK(!rules.skips_made_shrink_finite);
        CHECK(!rules.left_skip_omitted_when_zero);
        CHECK(!rules.post_break_text_stops_pruning);
        CHECK(!rules.easy_line);
        CHECK(!rules.sentence_space_truncates);
        CHECK(!rules.italic_kern_only_without_subscript);
        CHECK(!rules.underline_clearance);
        CHECK(!rules.sublist_nucleus_packed);
        CHECK(!rules.mu_truncates);
    }
    CHECK(product.skips_made_shrink_finite);
    CHECK(product.left_skip_omitted_when_zero);
    CHECK(product.post_break_text_stops_pruning);
    CHECK(product.easy_line);
    CHECK(product.sentence_space_truncates);
    CHECK(product.italic_kern_only_without_subscript);
    CHECK(product.underline_clearance);
    CHECK(product.sublist_nucleus_packed);
    CHECK(product.mu_truncates);
    // What carries them defaults to the reference: a golden replay that says
    // nothing is the reference's.
    CHECK(!LineBreakParams().rules.easy_line);
    CHECK(!prose_breaking().rules.easy_line);
    CHECK(!Composition().rules.sentence_space_truncates);
    CHECK(!MathFonts().rules.italic_kern_only_without_subscript);
}

// --- B5, sections 827 and 868 ---------------------------------------------------------

// Four boxes of 10pt with a rigid 5pt between them, a measure of 26pt, and a
// right skip of 0pt plus 2pt minus 1fil.
//
// Section 827 makes the right skip's shrink finite: 1pt at the normal order.
// Then, by sections 851 to 853 and 859 with line_penalty 10:
//   two boxes   natural 25pt, shortfall 1pt against 2pt of stretch. Section
//               108: r = 65536 * 297 div 131072 = 148, and
//               (148^3 + 2^17) div 2^18 = 3372864 div 262144 = 12. Decent.
//   three       natural 40pt against 1pt of shrink: inf_bad + 1, never taken.
//   one         shortfall 16pt against 2pt: r = 2376 > 1290, so inf_bad;
//               (10 + 10000)^2 saturates at 100000000.
// So the lines are two and two: (10 + 12)^2 = 484 for the first, and for the
// last, whose par-fill glue is infinitely stretchable, (10 + 0)^2 = 100. 584.
//
// The reference leaves the skip its infinite shrink, and a line that holds it
// fits whatever its width: all four boxes on one line, 55pt in 26pt, at
// badness 0 and 100 demerits.
TEST(b5_a_skip_with_infinite_shrink_is_made_finite_as_section_827_does)
{
    NodeArena arena;
    const std::vector<Node> list = words(arena, {10, 10, 10, 10}, GlueSpec{5 * PT});
    const ParShape shape = ParShape::uniform(26 * PT);
    Paragraph paragraph;

    LineBreakParams params;
    params.pretolerance = -1;
    params.tolerance = 10000;
    params.right_skip = GlueSpec{0, 2 * PT, GlueOrder::NORMAL, 1 * PT, GlueOrder::FIL};

    params.rules = TexRules::reference();
    REQUIRE(break_paragraph(arena, list, shape, params, CharMeasure(), paragraph));
    CHECK((boxes_per_line(arena, paragraph) == std::vector<int>{4}));
    CHECK_EQ(paragraph.total_demerits, int64_t(100));
    CHECK(paragraph.lines[0].packed.fitness == PackFitness::INFINITE_GLUE);
    CHECK_EQ(paragraph.lines[0].packed.natural, 55 * PT);

    params.rules.skips_made_shrink_finite = true;
    REQUIRE(break_paragraph(arena, list, shape, params, CharMeasure(), paragraph));
    CHECK((boxes_per_line(arena, paragraph) == std::vector<int>{2, 2}));
    CHECK_EQ(paragraph.total_demerits, int64_t(584));
    CHECK_EQ(paragraph.lines[0].packed.badness, 12);
    CHECK_EQ(paragraph.lines[0].packed.excess, 1 * PT);
    // The skip that went into the lines is the finite one.
    for (const BrokenLine& line : paragraph.lines) {
        const NodeView contents = arena.contents(line.box);
        REQUIRE(contents.size() > 0);
        const Node& right_skip = contents[contents.size() - 1];
        REQUIRE(right_skip.is(NodeType::GLUE));
        CHECK(right_skip.spec() == (GlueSpec{0, 2 * PT, GlueOrder::NORMAL, 1 * PT}));
    }

    // check_shrinkage leaves a zero shrink alone, whatever its order, and the
    // left skip and the par-fill glue are treated as the right skip is.
    params.right_skip = GlueSpec{0, 2 * PT, GlueOrder::NORMAL, 0, GlueOrder::FILL};
    params.left_skip = GlueSpec{0, 0, GlueOrder::NORMAL, 3 * PT, GlueOrder::FILL};
    params.par_fill_skip = GlueSpec{0, 1, GlueOrder::FIL, 2 * PT, GlueOrder::FIL};
    REQUIRE(break_paragraph(arena, list, shape, params, CharMeasure(), paragraph));
    REQUIRE(!paragraph.lines.empty());
    const NodeView last = arena.contents(paragraph.lines.back().box);
    REQUIRE(last.size() >= 3);
    CHECK(last[0].spec() == (GlueSpec{0, 0, GlueOrder::NORMAL, 3 * PT, GlueOrder::NORMAL}));
    CHECK(last[last.size() - 1].spec() ==
          (GlueSpec{0, 2 * PT, GlueOrder::NORMAL, 0, GlueOrder::FILL}));
    CHECK(last[last.size() - 2].spec() ==
          (GlueSpec{0, 1, GlueOrder::FIL, 2 * PT, GlueOrder::NORMAL}));
}

// --- B6, section 887 ------------------------------------------------------------------

// A left skip of 0pt plus 0fil. Section 887 puts the left skip on a line
// unless it is zero_glue, and section 1229 makes a glue parameter zero_glue
// whenever its width, stretch and shrink are zero: no glue. The reference
// compares all five fields with the default spec, finds the order different,
// and starts every line with an empty glue.
//
// One line of two boxes. Its list, by sections 881 to 887: box, glue, box,
// then section 816's penalty and par-fill glue, then the right skip: six
// nodes. The reference's has the left skip in front: seven.
TEST(b6_a_left_skip_of_zero_amounts_is_left_off_whatever_its_orders)
{
    NodeArena arena;
    const std::vector<Node> list = words(arena, {10, 10}, GlueSpec{5 * PT});
    const ParShape shape = ParShape::uniform(100 * PT);
    Paragraph paragraph;
    LineBreakParams params;
    params.left_skip = GlueSpec{0, 0, GlueOrder::FIL, 0, GlueOrder::NORMAL};

    REQUIRE(break_paragraph(arena, list, shape, params, CharMeasure(), paragraph));
    REQUIRE_EQ(paragraph.lines.size(), size_t(1));
    {
        const NodeView line = arena.contents(paragraph.lines[0].box);
        REQUIRE_EQ(line.size(), size_t(7));
        CHECK(line[0].is(NodeType::GLUE));
        CHECK(line[0].spec() == params.left_skip);
    }

    params.rules.left_skip_omitted_when_zero = true;
    REQUIRE(break_paragraph(arena, list, shape, params, CharMeasure(), paragraph));
    REQUIRE_EQ(paragraph.lines.size(), size_t(1));
    {
        const NodeView line = arena.contents(paragraph.lines[0].box);
        REQUIRE_EQ(line.size(), size_t(6));
        CHECK(line[0].is(NodeType::BOX));
    }

    // A skip with any amount is glue on the line under either rule, and both
    // judge the line the same way with and without the running totals.
    params.left_skip = GlueSpec{0, 0, GlueOrder::NORMAL, 1, GlueOrder::NORMAL};
    for (const bool totals : {true, false}) {
        LineBreaker breaker;
        breaker.use_running_totals(totals);
        REQUIRE(breaker.break_paragraph(arena, list, shape, params, CharMeasure(), paragraph));
        CHECK_EQ(arena.contents(paragraph.lines[0].box).size(), size_t(7));
    }
    params.left_skip = GlueSpec{0, 0, GlueOrder::FILL, 0, GlueOrder::FIL};
    Paragraph walked;
    LineBreaker breaker;
    breaker.use_running_totals(false);
    REQUIRE(breaker.break_paragraph(arena, list, shape, params, CharMeasure(), walked));
    REQUIRE(break_paragraph(arena, list, shape, params, CharMeasure(), paragraph));
    CHECK_EQ(arena.contents(walked.lines[0].box).size(), size_t(6));
    CHECK_EQ(walked.lines[0].packed.natural, paragraph.lines[0].packed.natural);
}

// Whoever indexes a line's nodes needs to know whether the first is the left
// skip, and the breaker is the one place that says: under section 887's rule a
// skip can differ from the default spec and still be left off.
TEST(b6_the_breaker_says_whether_its_lines_begin_with_the_left_skip)
{
    NodeArena arena;
    const std::vector<Node> list = words(arena, {10, 10}, GlueSpec{5 * PT});
    const ParShape shape = ParShape::uniform(100 * PT);
    const GlueSpec skips[] = {
        GlueSpec(),
        GlueSpec{0, 0, GlueOrder::FIL, 0, GlueOrder::NORMAL},
        GlueSpec{0, 0, GlueOrder::NORMAL, 0, GlueOrder::FILL},
        GlueSpec{0, UNITY, GlueOrder::FIL, 0, GlueOrder::NORMAL},
        GlueSpec{0, 0, GlueOrder::NORMAL, 1, GlueOrder::NORMAL},
    };
    for (const bool when_zero : {false, true}) {
        for (size_t s = 0; s < sizeof skips / sizeof skips[0]; ++s) {
            CHECK_CONTEXT("left_skip_omitted_when_zero %d, skip %d", when_zero ? 1 : 0,
                          static_cast<int>(s));
            LineBreakParams params;
            params.left_skip = skips[s];
            params.rules.left_skip_omitted_when_zero = when_zero;
            LineBreaker breaker;
            Paragraph paragraph;
            REQUIRE(breaker.break_paragraph(arena, list, shape, params, CharMeasure(), paragraph));
            REQUIRE_EQ(paragraph.lines.size(), size_t(1));
            const NodeView line = arena.contents(paragraph.lines[0].box);
            CHECK_EQ(line.size(), size_t(breaker.left_skip_omitted() ? 6 : 7));
            CHECK_EQ(line[0].is(NodeType::GLUE), !breaker.left_skip_omitted());
            // The reference's test, which is the breaker's only without the
            // switch: skips 1 and 2 are where the two part.
            const bool by_equality = skips[s] == GlueSpec();
            CHECK_EQ(breaker.left_skip_omitted() != by_equality, when_zero && (s == 1 || s == 2));
        }
    }
}

// The composer's map from the material to shipped items counts the left skip
// of every line. Two empty boxes and the glue between them: the boxes ship as
// nothing, so the glue's index is the number of items before it on its line,
// which is the left skip or nothing.
TEST(b6_the_shipped_map_counts_the_left_skip_the_breaker_put_on_the_line)
{
    for (const bool product : {false, true}) {
        for (const Align align : {Align::JUSTIFY, Align::LEFT, Align::RIGHT, Align::CENTER}) {
            CHECK_CONTEXT("product %d, align %d", product ? 1 : 0, static_cast<int>(align));
            NodeArena arena;
            TextBlock block;
            block.run.nodes = words(arena, {10, 10}, GlueSpec{5 * PT});
            BlockStyle style;
            style.align = align;
            LineBreakParams params;
            params.rules = product ? TexRules::product() : TexRules::reference();
            // skips() replaces this; it is here so that a composer that stopped
            // replacing it would be held to the breaker's answer all the same.
            params.left_skip = GlueSpec{0, 0, GlueOrder::FIL, 0, GlueOrder::NORMAL};
            ParagraphComposer composer;
            ParagraphLayout layout;
            REQUIRE(
                composer.set_lines(arena, block, style, 100 * PT, params, CharMeasure(), layout));
            REQUIRE_EQ(layout.broken.lines.size(), size_t(1));
            const NodeView line = arena.contents(layout.broken.lines[0].box);
            const bool has_left = !composer.breaker().left_skip_omitted();
            CHECK_EQ(has_left, align == Align::RIGHT || align == Align::CENTER);
            REQUIRE_EQ(line.size(), size_t(has_left ? 7 : 6));
            CHECK_EQ(line[0].is(NodeType::GLUE), has_left);
            REQUIRE(layout.mapped);
            REQUIRE_EQ(layout.shipped.size(), size_t(3));
            CHECK_EQ(layout.shipped[0], NOT_SHIPPED);
            CHECK_EQ(layout.shipped[1], uint32_t(has_left ? 1 : 0));
            CHECK_EQ(layout.shipped[2], NOT_SHIPPED);
            // The left skip if any, the glue, the par-fill glue, the right skip.
            CHECK_EQ(layout.item_count, uint32_t(has_left ? 4 : 3));
        }
    }
}

// --- B7, sections 877 and 884 ---------------------------------------------------------

// A box a of 10pt; a discretionary with a pre-break box of 3pt, a post-break
// box x of 4pt and nothing replaced; a penalty of 10000; a rigid glue of 5pt;
// a box b of 10pt. The measure is 19pt and the right skip is 0pt plus 1fil.
//
// Unbroken the list is 25pt with no shrink: overfull, never feasible. The
// glue follows a penalty and is not a legal breakpoint (section 868), so the
// one break is at the discretionary, at hyphen_penalty 50:
//   line 1   a and the pre-break box, 13pt, badness 0 for the fil:
//            (10 + 0)^2 + 50^2 = 2600 (section 859).
//   line 2   Section 884 moves the post-break list to the head of the line
//            and sets post_disc_break, and section 877 then prunes nothing:
//            x, the penalty, the glue, b: 19pt. It costs nothing: the break
//            at the paragraph's end is forced, the node at the discretionary
//            is the only one left (the paragraph's start was deactivated,
//            25pt from the end), nothing feasible has been recorded and the
//            pass is the final one, which is section 854's case, taken for
//            any forced break (section 851) and not only an overfull line.
// 2600 in all. The reference scores the same breaks but prunes the penalty
// and the glue after x, and its second line is 14pt.
TEST(b7_nothing_is_pruned_after_a_discretionary_that_has_post_break_text)
{
    NodeArena arena;
    Box box;
    box.height = 7 * PT;
    box.width = 10 * PT;
    const Node a = arena.add_box(box);
    const Node b = arena.add_box(box);
    box.width = 3 * PT;
    const Node pre = arena.add_box(box);
    box.width = 4 * PT;
    const Node x = arena.add_box(box);
    const std::vector<Node> list = {a, arena.add_disc(NodeView(&pre, 1), NodeView(&x, 1), 0),
                                    penalty(INF_PENALTY), glue(GlueSpec{5 * PT}), b};
    const ParShape shape = ParShape::uniform(19 * PT);
    LineBreakParams params = one_pass(TexRules::reference());
    params.right_skip = GlueSpec{0, PT, GlueOrder::FIL};
    Paragraph paragraph;

    REQUIRE(break_paragraph(arena, list, shape, params, CharMeasure(), paragraph));
    REQUIRE_EQ(paragraph.lines.size(), size_t(2));
    CHECK(paragraph.breaks[0].kind == BreakKind::DISC);
    CHECK_EQ(paragraph.total_demerits, int64_t(2600));
    CHECK_EQ(paragraph.artificial_breaks, uint32_t(1));
    CHECK_EQ(paragraph.lines[0].packed.natural, 13 * PT);
    CHECK_EQ(paragraph.lines[1].packed.natural, 14 * PT);
    CHECK_EQ(arena.contents(paragraph.lines[1].box).size(), size_t(5));

    params.rules.post_break_text_stops_pruning = true;
    for (const bool first_fit : {false, true}) {
        CHECK_CONTEXT("first fit %d", first_fit ? 1 : 0);
        params.first_fit = first_fit;
        REQUIRE(break_paragraph(arena, list, shape, params, CharMeasure(), paragraph));
        REQUIRE_EQ(paragraph.lines.size(), size_t(2));
        CHECK(paragraph.breaks[0].kind == BreakKind::DISC);
        if (!first_fit)
            CHECK_EQ(paragraph.total_demerits, int64_t(2600));
        CHECK_EQ(paragraph.lines[0].packed.natural, 13 * PT);
        CHECK_EQ(paragraph.lines[1].packed.natural, 19 * PT);
        const NodeView line = arena.contents(paragraph.lines[1].box);
        REQUIRE_EQ(line.size(), size_t(7));
        CHECK(line[0] == x);
        CHECK(line[1].is(NodeType::PENALTY));
        CHECK(line[2].is(NodeType::GLUE));
        CHECK(line[3] == b);
    }
    params.first_fit = false;

    // A discretionary with no post-break text: pruned under both, as section
    // 877 has it.
    const std::vector<Node> bare = {a, arena.add_disc(NodeView(&pre, 1), NodeView(), 0),
                                    penalty(INF_PENALTY), glue(GlueSpec{5 * PT}), b};
    REQUIRE(
        break_paragraph(arena, bare, ParShape::uniform(15 * PT), params, CharMeasure(), paragraph));
    REQUIRE_EQ(paragraph.lines.size(), size_t(2));
    CHECK_EQ(paragraph.lines[1].packed.natural, 10 * PT);
}

// --- B3a and B4, sections 835, 848, 850 and 854: easy_line --------------------------

// Six boxes of 10pt and a seventh of 100pt, a glue of 5pt plus 30pt between
// them, a measure of 40pt, one pass at tolerance 200, which is the final one.
// g1 to g6 are the glues, the legal breakpoints.
//
// A line of three boxes is 40pt: badness 0, (10 + 0)^2 = 100. A line of two
// is 25pt, 15pt short against 30pt of stretch: r = 983040 * 297 div 1966080 =
// 148, badness 12 as above, decent, (10 + 12)^2 = 484. A line of one has no
// stretch: badness inf_bad, over the tolerance but not deactivating. Four are
// 55pt: inf_bad + 1.
//
// tex.web, one class for every line number (easy_line = 0), the active list
// in order of creation, one new node per fitness class at each breakpoint
// (sections 835 and 836), all of them decent here:
//   g2  from the start, two boxes: 484.                          N2
//   g3  from the start, three: 100. From N2, one box: infeasible. N3
//   g4  the start is 55pt away: deactivated. From N2: 484 + 484.  N4 = 968
//   g5  from N2, three: 584. From N3, two: 584, and `<=` takes
//       the later (section 855).                                  N5 = 584
//   g6  N2 deactivated. From N3, three: 200. From N4, two: 1452.  N6 = 200
//   end the list is N3, N4, N5, N6 and every line to the end
//       holds the 100pt box. N3, N4 and N5 are deactivated; N6 is
//       then the last node and nothing is ahead of it, so section
//       854 breaks there for free: 200.
// Lines of 3, 3 and 1 boxes, 200 demerits.
//
// The reference keeps a node for each line count: at g6 both N6 (in three
// lines, 200) and a node reached in four (1452). At the end the node that is
// alone when its turn comes is the last in line-number order, the four-line
// one, and the paragraph is set 2, 2, 2, 1 at 1452 demerits although a chain
// costing 200 reached the same breakpoint.
TEST(easy_line_the_last_resort_is_forced_from_the_one_active_node_left)
{
    NodeArena arena;
    const GlueSpec space{5 * PT, 30 * PT};
    const std::vector<Node> list = words(arena, {10, 10, 10, 10, 10, 10, 100}, space);
    const ParShape shape = ParShape::uniform(40 * PT);
    Paragraph paragraph;

    REQUIRE(break_paragraph(arena, list, shape, one_pass(TexRules::reference()), CharMeasure(),
                            paragraph));
    CHECK((boxes_per_line(arena, paragraph) == std::vector<int>{2, 2, 2, 1}));
    CHECK_EQ(paragraph.total_demerits, int64_t(1452));

    TexRules rules;
    rules.easy_line = true;
    for (const bool totals : {true, false}) {
        CHECK_CONTEXT("running totals %d", totals ? 1 : 0);
        LineBreaker breaker;
        breaker.use_running_totals(totals);
        REQUIRE(
            breaker.break_paragraph(arena, list, shape, one_pass(rules), CharMeasure(), paragraph));
        CHECK((boxes_per_line(arena, paragraph) == std::vector<int>{3, 3, 1}));
        CHECK_EQ(paragraph.total_demerits, int64_t(200));
        CHECK_EQ(paragraph.artificial_breaks, uint32_t(1));
        CHECK(paragraph.pass_used == BreakPass::SECOND);
        CHECK(paragraph.lines.back().packed.fitness == PackFitness::OVERFULL);
    }
}

// Four boxes of 10pt, a rigid 5pt between them, a measure of 40pt, a right
// skip of 0pt plus 1fil and line_penalty 0. Every line that is not overfull
// has badness 0 and (0 + 0)^2 = 0 demerits, so every breaking that fits costs
// nothing and the choice is all tie-breaking.
//
// tex.web: one class, the list in order of creation, and section 855's `<=`
// lets the last active node that reaches a breakpoint take it:
//   g1  from the start.                                 N1 (line 2)
//   g2  from the start, then from N1, which wins.       N2 (line 3)
//   g3  from the start, N1, N2: N2 wins.                N3 (line 4)
//   end the start is 55pt away, deactivated; N1, N2, N3: N3 wins.
// A box to a line: 1, 1, 1, 1.
//
// The reference keeps the line counts apart, lets the later node win within
// one, and at the end takes the first node with the fewest demerits, which is
// the one with the fewest lines: 3, 1.
TEST(easy_line_of_two_breakings_with_equal_demerits_the_later_node_wins)
{
    NodeArena arena;
    const std::vector<Node> list = words(arena, {10, 10, 10, 10}, GlueSpec{5 * PT});
    const ParShape shape = ParShape::uniform(40 * PT);
    LineBreakParams params = one_pass(TexRules::reference());
    params.line_penalty = 0;
    params.right_skip = GlueSpec{0, PT, GlueOrder::FIL};
    Paragraph paragraph;

    REQUIRE(break_paragraph(arena, list, shape, params, CharMeasure(), paragraph));
    CHECK((boxes_per_line(arena, paragraph) == std::vector<int>{3, 1}));
    CHECK_EQ(paragraph.total_demerits, int64_t(0));

    params.rules.easy_line = true;
    REQUIRE(break_paragraph(arena, list, shape, params, CharMeasure(), paragraph));
    CHECK((boxes_per_line(arena, paragraph) == std::vector<int>{1, 1, 1, 1}));
    CHECK_EQ(paragraph.total_demerits, int64_t(0));

    // With plain.tex's line_penalty every line costs 100 and there is no tie:
    // both take the fewest lines.
    params.line_penalty = 10;
    for (const bool easy : {false, true}) {
        params.rules.easy_line = easy;
        REQUIRE(break_paragraph(arena, list, shape, params, CharMeasure(), paragraph));
        CHECK_EQ(paragraph.lines.size(), size_t(2));
        CHECK_EQ(paragraph.total_demerits, int64_t(200));
    }
}

// A hanging indentation: the first line 40pt, every later one 25pt, so
// easy_line is 1 (section 848: last_special_line = hang_after). Five boxes of
// 10pt, 5pt plus 30pt between them, one pass at tolerance 200.
//
//   g2  from the start (the class of line 1), two boxes in 40pt:
//       badness 12, 484.                                     N2 (line 2)
//   g3  from the start, three boxes, 40pt: 100. The class of line 1 is
//       line easy_line and N2 follows, so section 835 makes no node yet.
//       From N2 (the class of every later line), one box in 25pt:
//       infeasible. At the end of the list the node is made, from the
//       start, its line number best_pl_line + 1 = 2.         N3 (line 2)
//   g4  the start is 55pt away from g4: deactivated. From N2, two boxes
//       in 25pt, exact: 484 + 100. From N3, one: infeasible.  N4 = 584
//   end from N2, three boxes, 40pt in 25pt: inf_bad + 1. From N3, two and
//       the par-fill glue: 100 + 100 = 200. From N4, one: 684.
// Lines of 3 and 2 boxes, 200 demerits, which is also the only breaking that
// cheap, so the reference finds it too.
TEST(easy_line_a_shaped_first_line_is_a_class_of_its_own)
{
    NodeArena arena;
    const std::vector<Node> list = words(arena, {10, 10, 10, 10, 10}, GlueSpec{5 * PT, 30 * PT});
    const ParShape hanging = ParShape::hanging(40 * PT, 15 * PT, 1);
    CHECK_EQ(hanging.last_special_line(), int64_t(1));
    CHECK_EQ(ParShape::uniform(40 * PT).last_special_line(), int64_t(0));
    CHECK_EQ(ParShape().last_special_line(), int64_t(0));
    const LineSpec specs[3] = {{40 * PT, 0}, {30 * PT, 0}, {25 * PT, 15 * PT}};
    CHECK_EQ(ParShape::of(specs, 3).last_special_line(), int64_t(2));

    Paragraph paragraph;
    for (const bool easy : {false, true}) {
        CHECK_CONTEXT("easy_line %d", easy ? 1 : 0);
        TexRules rules;
        rules.easy_line = easy;
        REQUIRE(break_paragraph(arena, list, hanging, one_pass(rules), CharMeasure(), paragraph));
        CHECK((boxes_per_line(arena, paragraph) == std::vector<int>{3, 2}));
        CHECK_EQ(paragraph.total_demerits, int64_t(200));
        REQUIRE_EQ(paragraph.lines.size(), size_t(2));
        CHECK_EQ(arena.box(paragraph.lines[0].box).shift_amount, Scaled(0));
        CHECK_EQ(arena.box(paragraph.lines[1].box).shift_amount, 15 * PT);
    }
}

// Whatever the order of the active list, total fit finds the fewest demerits:
// under easy_line the total is the least over every way of breaking the
// paragraph, found by forcing each in turn, for a uniform shape and for
// shapes with one and with two special lines.
TEST(easy_line_finds_the_fewest_demerits_of_every_possible_breaking)
{
    NodeArena arena;
    const std::vector<int> widths = {12, 19, 27, 14, 12, 13, 19, 15, 18, 11};
    const GlueSpec space{6 * PT, 4 * PT, GlueOrder::NORMAL, 2 * PT, GlueOrder::NORMAL};
    const std::vector<Node> plain = words(arena, widths, space);
    const LineSpec two[3] = {{80 * PT, 0}, {70 * PT, 0}, {100 * PT, 0}};
    const ParShape shapes[] = {ParShape::uniform(100 * PT), ParShape::hanging(100 * PT, 20 * PT, 1),
                               ParShape::of(two, 3)};
    LineBreakParams params;
    params.pretolerance = 10000;
    params.tolerance = 10000;
    params.rules.easy_line = true;
    const NodeArena::Mark mark = arena.mark();

    for (const ParShape& shape : shapes) {
        CHECK_CONTEXT("last special line %lld", static_cast<long long>(shape.last_special_line()));
        LineBreaker breaker;
        Paragraph chosen;
        REQUIRE(breaker.break_paragraph(arena, plain, shape, params, CharMeasure(), chosen));
        REQUIRE(chosen.pass_used == BreakPass::FIRST);

        const size_t gaps = widths.size() - 1;
        int64_t best = INT64_MAX;
        int scored = 0;
        Paragraph forced_result;
        std::vector<Node> forced;
        for (uint32_t mask = 0; mask < (1u << gaps); ++mask) {
            forced.clear();
            int breaks = 0;
            for (size_t i = 0; i < widths.size(); ++i) {
                if (i != 0) {
                    const bool take = ((mask >> (i - 1)) & 1u) != 0;
                    breaks += take ? 1 : 0;
                    forced.push_back(penalty(take ? EJECT_PENALTY : INF_PENALTY));
                    forced.push_back(glue(space));
                }
                forced.push_back(plain[2 * i]);
            }
            arena.rewind(mark);
            breaker.break_paragraph(arena, forced, shape, params, CharMeasure(), forced_result);
            if (forced_result.pass_used != BreakPass::FIRST ||
                forced_result.lines.size() != static_cast<size_t>(breaks) + 1)
                continue;
            best = std::min(best, forced_result.total_demerits);
            ++scored;
        }
        CHECK(scored > 1);
        CHECK_EQ(chosen.total_demerits, best);

        // And the reference's classes reach the same total.
        LineBreakParams reference = params;
        reference.rules = TexRules::reference();
        Paragraph by_line_count;
        arena.rewind(mark);
        REQUIRE(
            breaker.break_paragraph(arena, plain, shape, reference, CharMeasure(), by_line_count));
        CHECK_EQ(by_line_count.total_demerits, best);
    }
}

// The work budget still stops the optimiser under easy_line, and the
// paragraph is then first fit's.
TEST(easy_line_keeps_the_work_budget)
{
    NodeArena arena;
    std::vector<int> widths(400, 10);
    const std::vector<Node> list = words(arena, widths, GlueSpec{5 * PT, 30 * PT});
    LineBreakParams params = one_pass(TexRules::product());
    LineBreaker breaker;
    Paragraph whole;
    REQUIRE(breaker.break_paragraph(arena, list, ParShape::uniform(200 * PT), params, CharMeasure(),
                                    whole));
    CHECK(!whole.budget_exhausted);
    const uint64_t cost = breaker.work_done();
    CHECK(cost > 400);

    breaker.set_work_budget(cost / 2);
    Paragraph cut;
    REQUIRE(breaker.break_paragraph(arena, list, ParShape::uniform(200 * PT), params, CharMeasure(),
                                    cut));
    CHECK(cut.budget_exhausted);
    CHECK(cut.pass_used == BreakPass::FIRST_FIT);
    CHECK(!cut.lines.empty());
    CHECK(breaker.work_done() <= cost);
}

// --- Y1, section 1044 -----------------------------------------------------------------

// A space of 15sp: stretch round(7.5) = 8 (half to even) and shrink 5, as the
// reference makes them from the width.
//
// After a full stop the space factor is 3000. Section 1044: the width gains
// the extra space, 5; the stretch is xn_over_d(8, 3000, 1000) = 24; the
// shrink is xn_over_d(5, 1000, 3000) = 5000 div 3000 = 1, remainder 2000.
// The reference rounds 1.67 to 2.
//
// After a capital the space factor is 999. The stretch is
// xn_over_d(8, 999, 1000) = 7992 div 1000 = 7, where the reference rounds to
// 8; the shrink is 5000 div 999 = 5 either way.
TEST(y1_a_sentence_space_is_scaled_with_xn_over_d_which_truncates)
{
    TexRules tex;
    tex.sentence_space_truncates = true;

    CHECK(interword_glue(15, 3000, Spacing::NONFRENCH) == (GlueSpec{20, 24, GlueOrder::NORMAL, 2}));
    CHECK(interword_glue(15, 3000, Spacing::NONFRENCH, tex) ==
          (GlueSpec{20, 24, GlueOrder::NORMAL, 1}));
    CHECK(interword_glue(15, 999, Spacing::NONFRENCH) == (GlueSpec{15, 8, GlueOrder::NORMAL, 5}));
    CHECK(interword_glue(15, 999, Spacing::NONFRENCH, tex) ==
          (GlueSpec{15, 7, GlueOrder::NORMAL, 5}));

    // At the neutral factor, and in the two spacings that do not scale, the
    // rule has nothing to act on.
    for (const Scaled width : {Scaled(15), Scaled(218235), Scaled(1), Scaled(0)}) {
        CHECK(interword_glue(width, 1000, Spacing::NONFRENCH, tex) ==
              interword_glue(width, 1000, Spacing::NONFRENCH));
        for (const int factor : {999, 1250, 3000}) {
            CHECK(interword_glue(width, factor, Spacing::FRENCH, tex) ==
                  interword_glue(width, factor, Spacing::FRENCH));
            CHECK(interword_glue(width, factor, Spacing::EXTRA_SPACE_ONLY, tex) ==
                  interword_glue(width, factor, Spacing::EXTRA_SPACE_ONLY));
        }
    }

    // Never more than one scaled point apart, and never above the reference.
    long differing = 0;
    for (Scaled width = 0; width < 4000; ++width) {
        for (const int factor : {999, 1250, 3000}) {
            const GlueSpec a = interword_glue(width, factor, Spacing::NONFRENCH);
            const GlueSpec b = interword_glue(width, factor, Spacing::NONFRENCH, tex);
            CHECK_EQ(a.width, b.width);
            CHECK(b.stretch == a.stretch || b.stretch == a.stretch - 1);
            CHECK(b.shrink == a.shrink || b.shrink == a.shrink - 1);
            differing += a == b ? 0 : 1;
        }
    }
    CHECK(differing > 1000);

    // Composition carries the rule to the glue it makes.
    static const FixedMetrics fixed;
    const MetricsResolver resolve{[](void*, FontKey) -> const FontMetrics* { return &fixed; },
                                  nullptr};
    for (const bool truncates : {false, true}) {
        Composition composition;
        composition.spacing = Spacing::NONFRENCH;
        composition.rules.sentence_space_truncates = truncates;
        Run run;
        std::string why;
        REQUIRE(append_text(run, "a. b", FontSpec{Face::PROSE, 10 * PT}, true, composition, resolve,
                            &why));
        REQUIRE_EQ(run.nodes.size(), size_t(4));
        REQUIRE(run.nodes[2].is(NodeType::GLUE));
        CHECK_EQ(run.nodes[2].spec().shrink, Scaled(truncates ? 1 : 2));
    }
}

// --- How much the golden paragraphs move ------------------------------------------------

namespace
{

// What a broken paragraph is compared by, kept apart from the arena so the
// arena can be rewound between two breakings of the same list.
struct Snapshot {
    std::vector<Break> breaks;
    std::vector<std::vector<std::string>> lines;
    int64_t total_demerits = 0;
    BreakPass pass = BreakPass::FIRST;
    bool overfull = false;
    bool budget_exhausted = false;
    uint32_t artificial_breaks = 0;
};

Snapshot snapshot(const core_golden::Context& context, const Paragraph& paragraph)
{
    Snapshot out;
    out.breaks = paragraph.breaks;
    out.total_demerits = paragraph.total_demerits;
    out.pass = paragraph.pass_used;
    out.budget_exhausted = paragraph.budget_exhausted;
    out.artificial_breaks = paragraph.artificial_breaks;
    for (const BrokenLine& line : paragraph.lines) {
        std::vector<std::string> text;
        core_golden::write_packed(
            context, core_golden::PACK_FITNESS_TOKENS[static_cast<int>(line.packed.fitness)],
            line.packed.natural, line.packed.excess, line.packed.badness, line.packed.overfull_by,
            line.box, text);
        out.lines.push_back(std::move(text));
        out.overfull = out.overfull || line.packed.fitness == PackFitness::OVERFULL;
    }
    return out;
}

bool same_breaks(const Snapshot& a, const Snapshot& b)
{
    if (a.breaks.size() != b.breaks.size())
        return false;
    for (size_t i = 0; i < a.breaks.size(); ++i) {
        if (a.breaks[i].position != b.breaks[i].position || a.breaks[i].kind != b.breaks[i].kind)
            return false;
    }
    return true;
}

// The paragraphs one rule set moved, against the reference's.
struct Counts {
    long paragraphs = 0;
    long total_fit = 0;
    long breaks_differ = 0;
    long lines = 0;
    long lines_differ = 0;
    // Of the paragraphs whose breaks differ: in one of the two breakings the
    // pass took section 854's free break (the last resort); the two cost the
    // same and came from the same pass (a tie resolved the other way);
    // neither.
    long ties = 0;
    long last_resort = 0;
    long other = 0;
    // Of the last-resort ones: an overfull line in either; the changed
    // breaking has fewer lines, or more; it costs more than the reference's.
    long overfull = 0;
    long fewer_lines = 0;
    long more_lines = 0;
    long costs_more = 0;
    // Total-fit paragraphs whose total demerits differ although no free break
    // was taken in either: there should be none.
    long totals_differ_without_last_resort = 0;
    std::vector<std::string> examples;

    void add(const Snapshot& reference, const Snapshot& changed, bool was_total_fit,
             const std::string& where)
    {
        ++paragraphs;
        total_fit += was_total_fit ? 1 : 0;
        lines += static_cast<long>(reference.lines.size());
        const size_t most = std::max(reference.lines.size(), changed.lines.size());
        for (size_t i = 0; i < most; ++i) {
            if (i >= reference.lines.size() || i >= changed.lines.size() ||
                reference.lines[i] != changed.lines[i])
                ++lines_differ;
        }
        const bool free_break = reference.artificial_breaks > 0 || changed.artificial_breaks > 0;
        if (was_total_fit && !free_break && reference.total_demerits != changed.total_demerits)
            ++totals_differ_without_last_resort;
        if (same_breaks(reference, changed))
            return;
        ++breaks_differ;
        if (free_break) {
            ++last_resort;
            overfull += reference.overfull || changed.overfull ? 1 : 0;
            fewer_lines += changed.lines.size() < reference.lines.size() ? 1 : 0;
            more_lines += changed.lines.size() > reference.lines.size() ? 1 : 0;
            costs_more += changed.total_demerits > reference.total_demerits ? 1 : 0;
        } else if (reference.total_demerits == changed.total_demerits &&
                   reference.pass == changed.pass) {
            ++ties;
        } else {
            ++other;
        }
        if (examples.size() < 4)
            examples.push_back(check::format(
                "%s: %zu lines at %lld -> %zu at %lld%s%s", where.c_str(), reference.lines.size(),
                static_cast<long long>(reference.total_demerits), changed.lines.size(),
                static_cast<long long>(changed.total_demerits), free_break ? ", section 854" : "",
                reference.overfull || changed.overfull ? ", overfull" : ""));
    }

    void print(const char* corpus, const char* rule) const
    {
        std::printf("    tex-rules %s, %s: %ld paragraphs (%ld total fit), breaks differ in %ld "
                    "(%ld ties, %ld last resort, %ld other), line boxes differ %ld of %ld\n",
                    corpus, rule, paragraphs, total_fit, breaks_differ, ties, last_resort, other,
                    lines_differ, lines);
        if (last_resort > 0)
            std::printf("        of the last-resort ones: %ld with an overfull line, %ld with "
                        "fewer lines, %ld with more, %ld costing more\n",
                        overfull, fewer_lines, more_lines, costs_more);
        for (const std::string& example : examples)
            std::printf("        %s\n", example.c_str());
    }
};

LineBreakParams core_params(const core_golden::Params& p)
{
    LineBreakParams params;
    params.pretolerance = p.pretolerance;
    params.tolerance = p.tolerance;
    params.line_penalty = p.line_penalty;
    params.adj_demerits = p.adj_demerits;
    params.double_hyphen_demerits = p.double_hyphen_demerits;
    params.final_hyphen_demerits = p.final_hyphen_demerits;
    params.hyphen_penalty = p.hyphen_penalty;
    params.ex_hyphen_penalty = p.ex_hyphen_penalty;
    params.emergency_stretch = p.emergency_stretch;
    params.left_skip = p.left_skip;
    params.right_skip = p.right_skip;
    params.par_fill_skip = p.par_fill_skip;
    params.first_fit = p.first_fit;
    return params;
}

struct CoreMeasure {
    const core_golden::MeasureTable* table = nullptr;

    static CharDimens answer(void* user, FontKey font, uint32_t codepoint)
    {
        const CoreMeasure& self = *static_cast<CoreMeasure*>(user);
        const core_golden::MeasureTable::Entry* entry = self.table->find(font, codepoint);
        return entry == nullptr ? CharDimens{}
                                : CharDimens{entry->width, entry->height, entry->depth};
    }

    CharMeasure measure()
    {
        if (table == nullptr || !table->present)
            return CharMeasure();
        return CharMeasure{&CoreMeasure::answer, this};
    }
};

bool register_fonts(FontLibrary& library)
{
    return math_golden::register_reference_fonts(library);
}

// The widths a para or blocks case was composed at: field 1 of every record
// with `tag` from where the reader stands.
std::vector<Scaled> widths_of(const golden::File& file, const core_golden::Reader& reader,
                              std::string_view tag)
{
    std::vector<Scaled> out;
    for (size_t i = reader.position(); i < reader.end(); ++i) {
        const golden::Record& record = file.records()[i];
        if (record.is(tag))
            out.push_back(record.i64(1));
    }
    return out;
}

} // namespace

// tests/golden/core: the node lists the reference broke, with its parameters
// and shapes. Y1 does not apply (the lists are already made).
TEST(measured_how_many_core_golden_paragraphs_each_rule_moves)
{
    const std::vector<NamedRules> rules = break_rules_one_at_a_time();
    std::vector<Counts> counts(rules.size());
    LineBreaker breaker;
    Paragraph paragraph;
    for (const char* name : {"core/linebreak.golden", "core/paragraphs.golden"}) {
        golden::File file;
        REQUIRE(file.load(golden::path(name)));
        for (const core_golden::Case& c : core_golden::cases(file)) {
            if (c.kind != "linebreak")
                continue;
            core_golden::Context context;
            core_golden::Reader reader(file, c);
            while (reader.next_is("source") || reader.next_is("theme"))
                reader.take();
            const core_golden::MeasureTable table = core_golden::read_measure(reader, context);
            CoreMeasure measure{&table};
            std::vector<std::string> list_names;
            std::vector<std::vector<Node>> lists;
            while (!reader.done()) {
                if (reader.next_is("nodes")) {
                    std::string list_name;
                    lists.push_back(core_golden::read_nodes(reader, context, &list_name));
                    list_names.push_back(list_name);
                    continue;
                }
                const golden::Record& open = reader.take("break");
                const std::vector<Node>* nodes = nullptr;
                for (size_t i = 0; i < lists.size(); ++i) {
                    if (list_names[i] == open.raw(1))
                        nodes = &lists[i];
                }
                REQUIRE(nodes != nullptr);
                std::vector<LineSpec> specs;
                for (const core_golden::Shape::Line& line : core_golden::read_shape(reader).lines)
                    specs.push_back(LineSpec{line.width, line.indent});
                LineBreakParams params = core_params(core_golden::read_params(reader));
                const ParShape shape = ParShape::of(specs.data(), specs.size());
                while (!reader.done() && !reader.next_is("endbreak"))
                    reader.take();
                reader.take("endbreak");

                const NodeArena::Mark mark = context.arena.mark();
                params.rules = TexRules::reference();
                breaker.break_paragraph(context.arena, *nodes, shape, params, measure.measure(),
                                        paragraph);
                const Snapshot reference = snapshot(context, paragraph);
                context.arena.rewind(mark);
                const std::string where = c.name + "/" + open.str(0);
                for (size_t r = 0; r < rules.size(); ++r) {
                    params.rules = rules[r].rules;
                    breaker.break_paragraph(context.arena, *nodes, shape, params, measure.measure(),
                                            paragraph);
                    counts[r].add(reference, snapshot(context, paragraph), !params.first_fit,
                                  where);
                    context.arena.rewind(mark);
                }
            }
        }
    }
    for (size_t r = 0; r < rules.size(); ++r)
        counts[r].print("core", rules[r].name);
    // What was measured, held: 500 breakings, 336 of them by total fit.
    CHECK_EQ(counts[0].paragraphs, 500L);
    CHECK_EQ(counts[0].total_fit, 336L);
    // B5: the four cases built with an infinitely shrinkable skip.
    CHECK_EQ(counts[0].breaks_differ, 4L);
    CHECK_EQ(counts[0].lines_differ, 22L);
    // B6 and B7: no breaks; two and three line boxes of the cases built for
    // them (a zero left skip of an infinite order; a discretionary with
    // post-break text before glue).
    CHECK_EQ(counts[1].breaks_differ, 0L);
    CHECK_EQ(counts[1].lines_differ, 2L);
    CHECK_EQ(counts[2].breaks_differ, 0L);
    CHECK_EQ(counts[2].lines_differ, 3L);
    // easy_line: seven ties (the cases with line_penalty 0, built to tie) and
    // four last resorts, each with fewer lines; nothing else.
    CHECK_EQ(counts[4].breaks_differ, 11L);
    CHECK_EQ(counts[4].ties, 7L);
    CHECK_EQ(counts[4].last_resort, 4L);
    CHECK_EQ(counts[4].fewer_lines, 4L);
    CHECK_EQ(counts[4].other, 0L);
    CHECK_EQ(counts[4].totals_differ_without_last_resort, 0L);
    CHECK_EQ(counts[4].lines_differ, 146L);
    CHECK_EQ(counts[5].breaks_differ, 15L);
}

namespace
{

struct ParaInput {
    Composition composition;
    BlockStyle style;
    std::vector<blocks_golden::Event> events;
    bool has_marker = false;
    FontSpec marker_font;
    std::string marker_text;
};

// The INPUT records of a `para` case, as tests/test_paragraph.cpp reads them.
bool read_para_input(core_golden::Reader& reader, ParaInput& in)
{
    reader.take("source");
    golden::Cursor block(reader.take("block"));
    block.f64();
    if (!blocks_golden::spacing_of(block.raw(), &in.composition.spacing) ||
        !blocks_golden::align_of(block.raw(), &in.style.align))
        return false;
    block.raw();
    block.str();
    in.style.left_indent = block.i64();
    in.style.first_line_indent = block.i64();
    in.style.marker_gap = block.i64();
    golden::Cursor faces(reader.take("faces"));
    in.composition.regular = blocks_golden::face_of(faces.str());
    faces.str();
    in.composition.code = blocks_golden::face_of(faces.str());
    const core_golden::Skip skip = core_golden::read_skip(reader);
    in.style.interline.baselineskip = skip.baselineskip;
    in.style.interline.lineskip = skip.lineskip;
    in.style.interline.lineskiplimit = skip.lineskiplimit;
    golden::Cursor strut(reader.take("strut"));
    in.style.strut.height = strut.i64();
    in.style.strut.depth = strut.i64();
    reader.take("runs");
    while (!reader.next_is("endruns") && !reader.done()) {
        blocks_golden::Event event;
        if (reader.next_is("hardbreak")) {
            reader.take();
            event.hard_break = true;
        } else {
            golden::Cursor run(reader.take("run"));
            event.font = blocks_golden::spec_of(run.str());
            event.breakable = run.boolean();
            event.text = run.str();
        }
        in.events.push_back(event);
    }
    reader.take("endruns");
    if (reader.next_is("marker")) {
        golden::Cursor marker(reader.take());
        in.marker_font = blocks_golden::spec_of(marker.str());
        in.marker_text = marker.str();
        in.has_marker = true;
    }
    return true;
}

bool build_text(const ParaInput& in, TexRules rules, MetricsResolver resolve, TextBlock& block,
                const Spacing* spacing = nullptr)
{
    Composition composition = in.composition;
    composition.rules = rules;
    if (spacing != nullptr)
        composition.spacing = *spacing;
    std::string why;
    for (const blocks_golden::Event& event : in.events) {
        if (event.hard_break)
            hard_break(block.run, in.style.align);
        else if (!append_text(block.run, event.text, event.font, event.breakable, composition,
                              resolve, &why))
            return false;
    }
    return !in.has_marker ||
           set_marker(block, in.marker_text, in.marker_font, composition, resolve, &why);
}

} // namespace

// tests/golden/para: every block the reference composed, at every width it
// composed it at, through the font reader. Each is counted twice over: as the
// reference breaks it (justified text by total fit, anything else by first
// fit with its shrink taken away), and with ragged text broken by total fit
// too, which the public API offers (ImTypesetLineBreak_TotalFit).
//
// Y1 acts only on text set with Spacing::NONFRENCH, which two of the cases
// are; the reference's theme, and the public API's default, is
// EXTRA_SPACE_ONLY. So Y1 is measured a second time with every case set
// NONFRENCH (ImTypesetSpacing_TeX), against the reference's rules on the same.
TEST(measured_how_many_para_golden_paragraphs_each_rule_moves)
{
    FontLibrary library;
    REQUIRE(register_fonts(library));
    const MetricsResolver resolve = fontlib_metrics(library);
    CharMeasureCache cache(resolve);
    const std::vector<NamedRules> rules = break_rules_one_at_a_time();
    std::vector<Counts> as_composed(rules.size());
    std::vector<Counts> ragged_total_fit(rules.size());
    Counts y1_nonfrench;
    long y1_glue_differs = 0;
    long y1_glue = 0;
    TexRules y1;
    y1.sentence_space_truncates = true;
    long nonfrench = 0;
    ParagraphComposer composer;
    ParagraphLayout layout;
    for (const char* name : {"para/edge.golden", "para/whitespace.golden", "para/standin.golden",
                             "para/runs.golden", "para/prose.golden", "para/sample.golden"}) {
        golden::File file;
        REQUIRE(file.load(golden::path(name)));
        for (const core_golden::Case& c : core_golden::cases(file)) {
            if (c.kind != "para")
                continue;
            CHECK_CONTEXT("%s: case %s", name, c.name.c_str());
            core_golden::Context context;
            core_golden::Reader reader(file, c);
            ParaInput in;
            REQUIRE(read_para_input(reader, in));
            nonfrench += in.composition.spacing == Spacing::NONFRENCH ? 1 : 0;
            const std::vector<Scaled> widths = widths_of(file, reader, "assemble");
            REQUIRE(!widths.empty());

            TextBlock reference_block;
            REQUIRE(build_text(in, TexRules::reference(), resolve, reference_block));
            std::vector<TextBlock> blocks(rules.size());
            for (size_t r = 0; r < rules.size(); ++r)
                REQUIRE(build_text(in, rules[r].rules, resolve, blocks[r]));

            {
                const Spacing tex = Spacing::NONFRENCH;
                TextBlock rounded;
                TextBlock truncated;
                REQUIRE(build_text(in, TexRules::reference(), resolve, rounded, &tex));
                REQUIRE(build_text(in, y1, resolve, truncated, &tex));
                REQUIRE_EQ(rounded.run.nodes.size(), truncated.run.nodes.size());
                for (size_t i = 0; i < rounded.run.nodes.size(); ++i) {
                    if (!rounded.run.nodes[i].is(NodeType::GLUE))
                        continue;
                    ++y1_glue;
                    y1_glue_differs += rounded.run.nodes[i] == truncated.run.nodes[i] ? 0 : 1;
                }
                for (const Scaled width : widths) {
                    const NodeArena::Mark mark = context.arena.mark();
                    LineBreakParams params = prose_breaking();
                    composer.compose(context.arena, rounded, in.style, width, params,
                                     cache.measure(), layout);
                    const Snapshot reference = snapshot(context, layout.broken);
                    context.arena.rewind(mark);
                    params.rules = y1;
                    composer.compose(context.arena, truncated, in.style, width, params,
                                     cache.measure(), layout);
                    y1_nonfrench.add(
                        reference, snapshot(context, layout.broken),
                        in.style.align == Align::JUSTIFY,
                        check::format("%s@%lld", c.name.c_str(), static_cast<long long>(width)));
                    context.arena.rewind(mark);
                }
            }

            for (const bool ragged : {false, true}) {
                if (ragged && in.style.align == Align::JUSTIFY)
                    continue;
                BlockStyle style = in.style;
                style.ragged_total_fit = ragged;
                std::vector<Counts>& counts = ragged ? ragged_total_fit : as_composed;
                const bool total_fit = ragged || in.style.align == Align::JUSTIFY;
                for (const Scaled width : widths) {
                    const NodeArena::Mark mark = context.arena.mark();
                    LineBreakParams params = prose_breaking();
                    composer.compose(context.arena, reference_block, style, width, params,
                                     cache.measure(), layout);
                    const Snapshot reference = snapshot(context, layout.broken);
                    context.arena.rewind(mark);
                    const std::string where =
                        check::format("%s@%lld", c.name.c_str(), static_cast<long long>(width));
                    for (size_t r = 0; r < rules.size(); ++r) {
                        params.rules = rules[r].rules;
                        composer.compose(context.arena, blocks[r], style, width, params,
                                         cache.measure(), layout);
                        counts[r].add(reference, snapshot(context, layout.broken), total_fit,
                                      where);
                        context.arena.rewind(mark);
                    }
                }
            }
        }
    }
    std::printf("    tex-rules para: %ld of the cases are set with Spacing::NONFRENCH\n",
                nonfrench);
    for (size_t r = 0; r < rules.size(); ++r)
        as_composed[r].print("para as the reference composes it", rules[r].name);
    for (size_t r = 0; r < rules.size(); ++r)
        ragged_total_fit[r].print("para, ragged blocks by total fit", rules[r].name);
    y1_nonfrench.print("para, every case set NONFRENCH", "Y1 sentence_space_truncates");
    std::printf("        %ld of %ld interword glues differ, each by one scaled point\n",
                y1_glue_differs, y1_glue);
    // What was measured, held. 710 compositions, 250 of them justified and
    // so by total fit.
    CHECK_EQ(as_composed[0].paragraphs, 710L);
    CHECK_EQ(as_composed[0].total_fit, 250L);
    // B5, B6, B7 and Y1 move nothing the reference composes.
    for (size_t r = 0; r < 4; ++r) {
        CHECK_EQ(as_composed[r].breaks_differ, 0L);
        CHECK_EQ(as_composed[r].lines_differ, 0L);
        CHECK_EQ(ragged_total_fit[r].breaks_differ, 0L);
        CHECK_EQ(ragged_total_fit[r].lines_differ, 0L);
    }
    // easy_line moves a paragraph only where section 854's free break was
    // taken, and then to fewer lines; never by a tie, and never the total
    // where every line was feasible.
    CHECK_EQ(as_composed[4].breaks_differ, 17L);
    CHECK_EQ(as_composed[4].last_resort, 17L);
    CHECK_EQ(as_composed[4].fewer_lines, 17L);
    CHECK_EQ(as_composed[4].costs_more, 0L);
    CHECK_EQ(as_composed[4].lines_differ, 669L);
    CHECK_EQ(as_composed[4].totals_differ_without_last_resort, 0L);
    CHECK_EQ(ragged_total_fit[4].paragraphs, 460L);
    CHECK_EQ(ragged_total_fit[4].breaks_differ, 57L);
    CHECK_EQ(ragged_total_fit[4].last_resort, 57L);
    CHECK_EQ(ragged_total_fit[4].fewer_lines, 57L);
    CHECK_EQ(ragged_total_fit[4].totals_differ_without_last_resort, 0L);
    CHECK_EQ(as_composed[5].breaks_differ, 17L);
    // Y1 with every case NONFRENCH: one scaled point in 21 glues, no break.
    CHECK_EQ(y1_nonfrench.breaks_differ, 0L);
    CHECK_EQ(y1_nonfrench.lines_differ, 55L);
    CHECK_EQ(y1_glue_differs, 21L);
    CHECK_EQ(y1_glue, 4171L);
}

// tests/golden/blocks: every text block of every document the reference
// composed, at every width, through the font reader, at the indent the
// document layer gives it.
TEST(measured_how_many_blocks_golden_paragraphs_each_rule_moves)
{
    FontLibrary library;
    REQUIRE(register_fonts(library));
    const MetricsResolver resolve = fontlib_metrics(library);
    CharMeasureCache cache(resolve);
    const std::vector<NamedRules> rules = break_rules_one_at_a_time();
    std::vector<Counts> counts(rules.size());
    BlockComposer composer;
    BlockBoxes boxes;
    const DocumentRules document_rules;
    for (const char* name : {"blocks/paragraphs.golden", "blocks/headings.golden",
                             "blocks/lists.golden", "blocks/quotes.golden", "blocks/code.golden",
                             "blocks/rules.golden", "blocks/edges.golden", "blocks/demo.golden"}) {
        golden::File file;
        REQUIRE(file.load(golden::path(name)));
        for (const core_golden::Case& c : core_golden::cases(file)) {
            CHECK_CONTEXT("%s: case %s", name, c.name.c_str());
            core_golden::Context context;
            core_golden::Reader reader(file, c);
            reader.take("source");
            const blocks_golden::ThemeIn theme = blocks_golden::read_theme(reader);
            blocks_golden::Recorded recorded;
            recorded.read(reader, context);
            (void)core_golden::read_measure(reader, context);
            const std::vector<blocks_golden::BlockIn> inputs =
                blocks_golden::read_blocks(reader, context);
            const std::vector<Scaled> widths = widths_of(file, reader, "compose");
            REQUIRE(!widths.empty());

            // Each block built under the reference's rules and under each rule
            // set: only the glue between words can differ (Y1).
            const auto build = [&](TexRules tex, std::vector<Block>& out) {
                out.assign(inputs.size(), Block());
                Composition composition = theme.composition;
                composition.rules = tex;
                std::string why;
                for (size_t i = 0; i < inputs.size(); ++i) {
                    const blocks_golden::BlockIn& in = inputs[i];
                    Block& block = out[i];
                    block.type = in.type;
                    block.style.align = in.align;
                    block.style.left_indent = in.left_indent;
                    block.style.first_line_indent = in.first_line_indent;
                    block.style.marker_gap = theme.marker_gap;
                    block.style.interline = in.interline;
                    block.style.strut = in.strut;
                    block.space_above = in.space_above;
                    block.space_below = in.space_below;
                    block.quote_depth = in.quote_depth;
                    block.thickness = in.thickness;
                    if (in.type != BlockType::TEXT)
                        continue;
                    for (const blocks_golden::Event& event : in.events) {
                        if (event.hard_break)
                            hard_break(block.text.run, in.align);
                        else if (!append_text(block.text.run, event.text, event.font,
                                              event.breakable, composition, resolve, &why))
                            return false;
                    }
                    if (in.has_marker && !set_marker(block.text, in.marker_text, in.marker_font,
                                                     composition, resolve, &why))
                        return false;
                }
                return true;
            };
            std::vector<Block> reference_blocks;
            REQUIRE(build(TexRules::reference(), reference_blocks));
            std::vector<std::vector<Block>> blocks(rules.size());
            for (size_t r = 0; r < rules.size(); ++r)
                REQUIRE(build(rules[r].rules, blocks[r]));

            Derived derived;
            derive(reference_blocks.data(), reference_blocks.size(), nullptr, Structure(),
                   document_rules, derived);
            for (const Scaled width : widths) {
                for (size_t i = 0; i < inputs.size(); ++i) {
                    if (inputs[i].type != BlockType::TEXT)
                        continue;
                    const Scaled indent =
                        effective_indent(reference_blocks[i], derived, document_rules, width);
                    const NodeArena::Mark mark = context.arena.mark();
                    LineBreakParams params = prose_breaking();
                    composer.block_boxes(context.arena, reference_blocks[i], indent, width, params,
                                         cache.measure(), document_rules, boxes);
                    const Snapshot reference = snapshot(context, boxes.para.broken);
                    context.arena.rewind(mark);
                    const std::string where = check::format("%s block %zu@%lld", c.name.c_str(), i,
                                                            static_cast<long long>(width));
                    for (size_t r = 0; r < rules.size(); ++r) {
                        params.rules = rules[r].rules;
                        composer.block_boxes(context.arena, blocks[r][i], indent, width, params,
                                             cache.measure(), document_rules, boxes);
                        counts[r].add(reference, snapshot(context, boxes.para.broken),
                                      inputs[i].align == Align::JUSTIFY, where);
                        context.arena.rewind(mark);
                    }
                }
            }
        }
    }
    for (size_t r = 0; r < rules.size(); ++r)
        counts[r].print("blocks", rules[r].name);
    // What was measured, held.
    CHECK_EQ(counts[0].paragraphs, 705L);
    CHECK_EQ(counts[0].total_fit, 465L);
    for (size_t r = 0; r < 4; ++r) {
        CHECK_EQ(counts[r].breaks_differ, 0L);
        CHECK_EQ(counts[r].lines_differ, 0L);
    }
    CHECK_EQ(counts[4].breaks_differ, 14L);
    CHECK_EQ(counts[4].last_resort, 14L);
    CHECK_EQ(counts[4].fewer_lines, 14L);
    CHECK_EQ(counts[4].costs_more, 0L);
    CHECK_EQ(counts[4].lines_differ, 120L);
    CHECK_EQ(counts[4].totals_differ_without_last_resort, 0L);
    CHECK_EQ(counts[5].breaks_differ, 14L);
}

// --- Math: M9, M10, M13, M14 ----------------------------------------------------------

namespace
{

// Latin Modern Math at ten points, through the font reader, and one formula
// set under a rule set.
struct MathBench {
    FontLibrary library;
    math_golden::LibraryFonts source;
    MathFonts fonts;
    NodeArena arena;
    bool ready = false;

    MathBench()
        : source(library)
    {
        ready = register_fonts(library) && fonts.init(10 * PT, source.math(), source.text());
    }

    // The formula's box, or a box of no kind at all if it was refused.
    Node set(const char* formula, TexRules rules, bool display = false)
    {
        fonts.rules = rules;
        Formula parsed;
        Refusal refusal;
        Node box;
        if (!typeset(formula, display, fonts, arena, parsed, box, refusal)) {
            FAIL("%s was refused: %s", formula, refusal.reason.c_str());
            return Node();
        }
        return box;
    }
};

TexRules only(bool TexRules::*rule)
{
    TexRules rules;
    rules.*rule = true;
    return rules;
}

} // namespace

// The math italic f of Latin Modern Math has an italic correction of 0.09 em:
// 58982 sp at ten points (round(0.09 * 655360) = round(58982.4)).
//
// Section 755, for a character with italic correction delta:
//   f^2     no subscript: the kern is appended and delta is cleared. The
//           list is f, kern 58982, the superscript's box.
//   f_i     a subscript: no kern, and make_scripts is handed delta, which a
//           lone subscript does not use (section 757). The list is f and the
//           subscript's box, which starts where f's advance ends.
//   f_i^2   no kern; section 759 shifts the superscript delta to the right
//           inside the vbox it makes of the two. The list is f and that vbox.
// So by hand f_i is one delta narrower than the reference's f, kern, box, and
// f_i^2 one delta narrower than its f, kern, vbox; f^2 is the same list under
// both; and f_i^2 is exactly as wide as f^2, because the vbox is as wide as
// its superscript shifted by delta, which is wider than the subscript.
// x has no italic correction in this font and x_i does not change.
TEST(m9_the_italic_correction_is_a_kern_only_when_there_is_no_subscript)
{
    MathBench bench;
    REQUIRE(bench.ready);
    const Scaled delta = 58982;
    CHECK_EQ(round_to_scaled(bench.fonts.face(Style::TEXT).italic_correction(0x1D453)), delta);
    const TexRules reference = TexRules::reference();
    const TexRules tex = only(&TexRules::italic_kern_only_without_subscript);

    // f^2: the same list.
    {
        const Node a = bench.set("f^2", reference);
        const Node b = bench.set("f^2", tex);
        const NodeView la = bench.arena.contents(a);
        const NodeView lb = bench.arena.contents(b);
        REQUIRE_EQ(la.size(), size_t(3));
        REQUIRE_EQ(lb.size(), size_t(3));
        CHECK(lb[0].is(NodeType::CHAR));
        CHECK(lb[1].is(NodeType::KERN));
        CHECK_EQ(lb[1].a, delta);
        CHECK(lb[2].is(NodeType::BOX));
        CHECK_EQ(bench.arena.box(a).width, bench.arena.box(b).width);
        CHECK_EQ(bench.arena.box(b).width, Scaled(642252));
    }
    // f_i.
    {
        const Node a = bench.set("f_i", reference);
        const Node b = bench.set("f_i", tex);
        const NodeView la = bench.arena.contents(a);
        const NodeView lb = bench.arena.contents(b);
        REQUIRE_EQ(la.size(), size_t(3));
        CHECK(la[1].is(NodeType::KERN));
        CHECK_EQ(la[1].a, delta);
        REQUIRE_EQ(lb.size(), size_t(2));
        CHECK(lb[0].is(NodeType::CHAR));
        CHECK(lb[1].is(NodeType::BOX));
        CHECK_EQ(bench.arena.box(a).width, Scaled(571145));
        CHECK_EQ(bench.arena.box(b).width, Scaled(571145) - delta);
        // The subscript's own box is the same box, moved.
        CHECK_EQ(bench.arena.box(la[2]).width, bench.arena.box(lb[1]).width);
        CHECK_EQ(bench.arena.box(la[2]).shift_amount, bench.arena.box(lb[1]).shift_amount);
        CHECK_EQ(bench.arena.box(a).height, bench.arena.box(b).height);
        CHECK_EQ(bench.arena.box(a).depth, bench.arena.box(b).depth);
    }
    // f_i^2.
    {
        const Node a = bench.set("f_i^2", reference);
        const Node b = bench.set("f_i^2", tex);
        const NodeView la = bench.arena.contents(a);
        const NodeView lb = bench.arena.contents(b);
        REQUIRE_EQ(la.size(), size_t(3));
        REQUIRE_EQ(lb.size(), size_t(2));
        CHECK_EQ(bench.arena.box(a).width, Scaled(701234));
        CHECK_EQ(bench.arena.box(b).width, Scaled(701234) - delta);
        CHECK_EQ(bench.arena.box(b).width, Scaled(642252));
        // The pair: superscript, kern, subscript, the superscript delta to
        // the right under both.
        const Box& pair = bench.arena.box(lb[1]);
        CHECK(pair.kind == BoxKind::VLIST);
        const NodeView scripts = bench.arena.view(pair.contents);
        REQUIRE_EQ(scripts.size(), size_t(3));
        CHECK_EQ(bench.arena.box(scripts[0]).shift_amount, delta);
        CHECK_EQ(bench.arena.box(scripts[2]).shift_amount, Scaled(0));
        CHECK_EQ(bench.arena.box(bench.arena.view(bench.arena.box(la[2]).contents)[0]).shift_amount,
                 delta);
    }
    // x_i: nothing to correct.
    {
        const Node a = bench.set("x_i", reference);
        const Node b = bench.set("x_i", tex);
        CHECK_EQ(bench.arena.contents(a).size(), size_t(2));
        CHECK_EQ(bench.arena.contents(b).size(), size_t(2));
        CHECK_EQ(bench.arena.box(a).width, bench.arena.box(b).width);
    }
    // A big operator goes through make_op (section 749), which both rule sets
    // have as tex.web does.
    for (const char* formula : {"\\int_a^b", "\\sum_i^n", "\\int_a"}) {
        CHECK_CONTEXT("%s", formula);
        const Node a = bench.set(formula, reference);
        const Node b = bench.set(formula, tex);
        CHECK_EQ(bench.arena.box(a).width, bench.arena.box(b).width);
        CHECK_EQ(bench.arena.contents(a).size(), bench.arena.contents(b).size());
    }
}

// \underline{x} at ten points, where default_rule_thickness is 26214 sp
// (0.04 em: round(0.04 * 655360) = round(26214.4)).
//
// Section 735: y is the vpack of x, a kern of three thicknesses and a rule of
// one, so height(y) + depth(y) = height(x) + depth(x) + 4 * 26214. delta adds
// one thickness more; height(y) becomes height(x) and depth(y) is
// delta - height(x) = depth(x) + 5 * 26214. The reference leaves the last
// thickness out: depth(x) + 4 * 26214.
//
// x's own depth in this font at this size is 7209 sp, so the two depths are
// 7209 + 104856 = 112065 and 7209 + 131070 = 138279. Nothing inside moves.
TEST(m10_an_underline_has_a_rule_thickness_of_clearance_below_it)
{
    MathBench bench;
    REQUIRE(bench.ready);
    const Scaled thickness = 26214;
    CHECK_EQ(round_to_scaled(bench.fonts.params(Style::TEXT).default_rule_thickness), thickness);
    const Node a = bench.set("\\underline{x}", TexRules::reference());
    const Node b = bench.set("\\underline{x}", only(&TexRules::underline_clearance));
    const Box& reference = bench.arena.box(a);
    const Box& tex = bench.arena.box(b);
    CHECK_EQ(reference.depth, Scaled(112065));
    CHECK_EQ(tex.depth, Scaled(138279));
    CHECK_EQ(tex.depth - reference.depth, thickness);
    CHECK_EQ(reference.height, tex.height);
    CHECK_EQ(reference.width, tex.width);
    // The same three things in the vlist.
    const NodeView la = bench.arena.contents(a);
    const NodeView lb = bench.arena.contents(b);
    REQUIRE_EQ(la.size(), size_t(1));
    REQUIRE_EQ(lb.size(), size_t(1));
    const NodeView va = bench.arena.contents(la[0]);
    const NodeView vb = bench.arena.contents(lb[0]);
    REQUIRE_EQ(va.size(), size_t(3));
    REQUIRE_EQ(vb.size(), size_t(3));
    CHECK(va[1] == vb[1]);
    CHECK(va[2] == vb[2]);
    CHECK(vb[2].is(NodeType::RULE));
}

// Section 754 packs a nucleus that is a list into one box, and section 720's
// clean_box does not pack again what is already one unshifted box.
//
//   {a+x}^2   tex.web: the hbox of a, glue, +, glue, x, then the superscript.
//             The reference splices the five nodes into the outer list.
//             (Neither letter has an italic correction in this font.)
//   \frac{\frac{aa}{b}}{c}
//             The numerator's field is a list of one fraction. clean_box
//             converts it to one box, unshifted (the hbox section 754 made
//             of the inner group, which holds the inner fraction's vlist):
//             tex.web uses that box as the numerator; the reference, which
//             made no box of the group, wraps the vlist in an hbox of its
//             own. One box around the vlist either way, for different
//             reasons; what section 720 saves is a second one.
// The boxes measure the same either way.
TEST(m13_a_group_is_one_box_and_a_clean_box_is_not_boxed_again)
{
    MathBench bench;
    REQUIRE(bench.ready);
    const TexRules tex = only(&TexRules::sublist_nucleus_packed);
    {
        const Node a = bench.set("{a+x}^2", TexRules::reference());
        const Node b = bench.set("{a+x}^2", tex);
        const NodeView la = bench.arena.contents(a);
        const NodeView lb = bench.arena.contents(b);
        REQUIRE_EQ(la.size(), size_t(6));
        REQUIRE_EQ(lb.size(), size_t(2));
        REQUIRE(lb[0].is(NodeType::BOX));
        const Box& group = bench.arena.box(lb[0]);
        CHECK(group.kind == BoxKind::HLIST);
        CHECK_EQ(group.shift_amount, Scaled(0));
        const NodeView inside = bench.arena.view(group.contents);
        REQUIRE_EQ(inside.size(), size_t(5));
        for (size_t i = 0; i < 5; ++i)
            CHECK(inside[i] == la[i]);
        // The superscript is the same box, placed the same.
        CHECK_EQ(bench.arena.box(lb[1]).shift_amount, bench.arena.box(la[5]).shift_amount);
        CHECK_EQ(bench.arena.box(a).width, bench.arena.box(b).width);
        CHECK_EQ(bench.arena.box(a).height, bench.arena.box(b).height);
        CHECK_EQ(bench.arena.box(a).depth, bench.arena.box(b).depth);
    }
    {
        const Node a = bench.set("\\frac{\\frac{aa}{b}}{c}", TexRules::reference());
        const Node b = bench.set("\\frac{\\frac{aa}{b}}{c}", tex);
        const NodeView la = bench.arena.contents(a);
        const NodeView lb = bench.arena.contents(b);
        REQUIRE_EQ(la.size(), size_t(1));
        REQUIRE_EQ(lb.size(), size_t(1));
        // \frac{..}{..} is a group holding a fraction, as {.. \over ..} is:
        // under section 754 the fraction's vlist is inside the group's hbox.
        const NodeView fa = bench.arena.contents(la[0]);
        const NodeView group = bench.arena.contents(lb[0]);
        CHECK(bench.arena.box(lb[0]).kind == BoxKind::HLIST);
        REQUIRE_EQ(group.size(), size_t(1));
        const NodeView fb = bench.arena.contents(group[0]);
        REQUIRE(fa.size() == 5 && fb.size() == 5);
        const Box& wrapped = bench.arena.box(fa[0]);
        const Box& clean = bench.arena.box(fb[0]);
        for (const Box* numerator : {&wrapped, &clean}) {
            CHECK(numerator->kind == BoxKind::HLIST);
            REQUIRE_EQ(numerator->contents.count, uint32_t(1));
            CHECK(bench.arena.box(bench.arena.view(numerator->contents)[0]).kind == BoxKind::VLIST);
        }
        CHECK_EQ(clean.shift_amount, Scaled(0));
        CHECK_EQ(wrapped.width, clean.width);
        CHECK_EQ(wrapped.height, clean.height);
        CHECK_EQ(wrapped.depth, clean.depth);
        CHECK_EQ(bench.arena.box(a).width, bench.arena.box(b).width);
        CHECK_EQ(bench.arena.box(a).height, bench.arena.box(b).height);
        CHECK_EQ(bench.arena.box(a).depth, bench.arena.box(b).depth);
    }
    // Section 721 on a box used as it stands: {f}, a group of one character
    // with an italic correction, is the hbox of f and its kern; as a
    // numerator that box is clean already, and its kern is dropped while its
    // width keeps the correction.
    {
        const Node b = bench.set("\\frac{{f}}{xxx}", tex);
        const NodeView fraction =
            bench.arena.contents(bench.arena.contents(bench.arena.contents(b)[0])[0]);
        REQUIRE(fraction.size() == 5);
        // Narrower than the denominator, so reboxed: hss, f, kern, hss.
        const Box& numerator = bench.arena.box(fraction[0]);
        const Box& denominator = bench.arena.box(fraction[4]);
        CHECK_EQ(numerator.width, denominator.width);
        const Node a = bench.set("\\frac{{f}}{xxx}", TexRules::reference());
        CHECK_EQ(bench.arena.box(a).width, bench.arena.box(b).width);
        CHECK_EQ(bench.arena.box(a).height, bench.arena.box(b).height);
        CHECK_EQ(bench.arena.box(a).depth, bench.arena.box(b).depth);
    }
}

// Section 703: cur_mu = x_over_n(math_quad, 18). At ten points the quad is
// 655360 sp and 655360 = 18 * 36408 + 16, so cur_mu is 36408; the reference
// rounds 36408.89 to 36409. plain.tex's thick space is 5mu plus 5mu, so each
// side of a relation is 182040 sp where the reference has 182045, and a = b is
// ten scaled points narrower.
TEST(m14_the_math_unit_is_the_quad_over_eighteen_truncated)
{
    MathBench bench;
    REQUIRE(bench.ready);
    CHECK_EQ(round_to_scaled(bench.fonts.params(Style::TEXT).math_quad), Scaled(655360));
    bench.fonts.rules = TexRules::reference();
    CHECK_EQ(bench.fonts.mu(Style::TEXT), Scaled(36409));
    bench.fonts.rules = only(&TexRules::mu_truncates);
    CHECK_EQ(bench.fonts.mu(Style::TEXT), Scaled(36408));

    const Node a = bench.set("a=b", TexRules::reference());
    const Node b = bench.set("a=b", only(&TexRules::mu_truncates));
    const NodeView la = bench.arena.contents(a);
    const NodeView lb = bench.arena.contents(b);
    REQUIRE_EQ(la.size(), lb.size());
    int glues = 0;
    for (size_t i = 0; i < la.size(); ++i) {
        if (!la[i].is(NodeType::GLUE))
            continue;
        ++glues;
        CHECK_EQ(la[i].spec().width, Scaled(182045));
        CHECK_EQ(lb[i].spec().width, Scaled(182040));
        CHECK_EQ(lb[i].spec().stretch, Scaled(182040));
    }
    CHECK_EQ(glues, 2);
    CHECK_EQ(bench.arena.box(a).width - bench.arena.box(b).width, Scaled(10));
}

// Every source of tests/golden/math/formulas.golden, set at its own size and
// style under the reference's rules and under each math rule: how many boxes
// change their dimensions, and by how much at most.
TEST(measured_how_many_golden_formulas_each_math_rule_moves)
{
    golden::File file;
    REQUIRE(file.load(golden::path("math/formulas.golden")));
    FontLibrary library;
    REQUIRE(register_fonts(library));
    math_golden::LibraryFonts source(library);
    const struct {
        const char* name;
        TexRules rules;
    } rules[] = {
        {"M9 italic_kern_only_without_subscript",
         only(&TexRules::italic_kern_only_without_subscript)},
        {"M10 underline_clearance", only(&TexRules::underline_clearance)},
        {"M13 sublist_nucleus_packed", only(&TexRules::sublist_nucleus_packed)},
        {"M14 mu_truncates", only(&TexRules::mu_truncates)},
        {"all of product()", TexRules::product()},
    };
    struct Moved {
        long set = 0;
        long width = 0;
        long height_or_depth = 0;
        long refused_differently = 0;
        Scaled most_width = 0;
        Scaled most_vertical = 0;
    } moved[5];
    long formulas = 0;
    for (const core_golden::Case& c : core_golden::cases(file)) {
        core_golden::Reader reader(file, c);
        if (c.kind == "table" || !reader.next_is("source"))
            continue;
        const golden::Record& source_record = reader.take("source");
        const std::string text = source_record.str(0);
        const bool display = source_record.boolean(1);
        const Scaled base_size = reader.take("fonts").i64(2);
        const Scaled shortfall = reader.take("delimiter_shortfall").i64(0);
        MathFonts fonts;
        REQUIRE(fonts.init(base_size, source.math(), source.text()));
        fonts.delimiter_shortfall = shortfall;
        NodeArena arena;
        Formula formula;
        Refusal refusal;
        Node box;
        fonts.rules = TexRules::reference();
        const bool ok = typeset(text, display, fonts, arena, formula, box, refusal);
        const Box reference = ok ? arena.box(box) : Box();
        ++formulas;
        for (size_t r = 0; r < 5; ++r) {
            fonts.rules = rules[r].rules;
            const bool ok_too = typeset(text, display, fonts, arena, formula, box, refusal);
            if (ok != ok_too) {
                ++moved[r].refused_differently;
                continue;
            }
            if (!ok)
                continue;
            ++moved[r].set;
            const Box& changed = arena.box(box);
            const Scaled dw = changed.width - reference.width;
            const Scaled dv = std::max(std::abs(changed.height - reference.height),
                                       std::abs(changed.depth - reference.depth));
            moved[r].width += dw != 0 ? 1 : 0;
            moved[r].height_or_depth += dv != 0 ? 1 : 0;
            moved[r].most_width = std::max(moved[r].most_width, std::abs(dw));
            moved[r].most_vertical = std::max(moved[r].most_vertical, dv);
        }
    }
    for (size_t r = 0; r < 5; ++r)
        std::printf("    tex-rules math, %s: of %ld formulas set, the width differs in %ld (by at "
                    "most %lld sp), the height or depth in %ld (by at most %lld sp); %ld set "
                    "under one rule set and refused under the other\n",
                    rules[r].name, moved[r].set, moved[r].width,
                    static_cast<long long>(moved[r].most_width), moved[r].height_or_depth,
                    static_cast<long long>(moved[r].most_vertical), moved[r].refused_differently);
    CHECK(formulas > 1500);
    // What was measured, held.
    CHECK_EQ(moved[0].set, 1950L);
    CHECK_EQ(moved[0].width, 22L);
    CHECK_EQ(moved[0].height_or_depth, 0L);
    CHECK_EQ(moved[1].width, 0L);
    CHECK_EQ(moved[1].height_or_depth, 14L);
    CHECK_EQ(moved[3].width, 181L);
    CHECK(moved[3].most_width <= 40);
    CHECK_EQ(moved[3].height_or_depth, 0L);
    CHECK_EQ(moved[4].width, 194L);
    CHECK_EQ(moved[4].height_or_depth, 14L);
    // Packing a group changes no dimension, and no rule changes what is set.
    CHECK_EQ(moved[2].width, 0L);
    CHECK_EQ(moved[2].height_or_depth, 0L);
    for (const Moved& m : moved)
        CHECK_EQ(m.refused_differently, 0L);
}

// --- Pictures ---------------------------------------------------------------------------

// Not a check: pixels to look at. With IMTYPESET_TEXFIX_SHOTS=<directory> it
// writes, for each of a few formulas, one picture: the formula through the
// public API under the reference's rules above, under the product's below,
// enlarged, on a light band that is the paragraph's own box. Both halves are
// drawn under the product's device-space rules (DrawRules::product()), so the
// only thing that differs between them is where layout put things. It also
// prints where each glyph and rule was drawn, in pixels.
TEST(pictures_of_the_math_rules_for_whoever_sets_IMTYPESET_TEXFIX_SHOTS)
{
    const char* const directory = std::getenv("IMTYPESET_TEXFIX_SHOTS");
    if (directory == nullptr || directory[0] == '\0') {
        std::printf("    SKIPPED: IMTYPESET_TEXFIX_SHOTS is not set\n");
        return;
    }
    static const struct {
        ImTypesetFace face;
        const char* file;
    } faces[] = {
        {ImTypesetFace_Regular, "Roboto-Regular.ttf"},
        {ImTypesetFace_Bold, "Roboto-Bold.ttf"},
        {ImTypesetFace_Italic, "Roboto-RegularItalic.ttf"},
        {ImTypesetFace_BoldItalic, "Roboto-BoldItalic.ttf"},
        {ImTypesetFace_Code, "Inconsolata-Medium.ttf"},
        {ImTypesetFace_Math, "latinmodern-math.otf"},
    };
    const int page_width = 640;
    const int page_height = 160;
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(static_cast<float>(page_width), static_cast<float>(page_height));
    io.DeltaTime = 1.0f / 60.0f;
    soft_raster::Renderer renderer;
    renderer.init();
    REQUIRE(ImTypeset::CreateContext() != nullptr);
    for (const auto& entry : faces) {
        const std::string path = golden::data_path(std::string("fonts/") + entry.file);
        REQUIRE(ImTypeset::AddFontFromFileTTF(entry.face, path.c_str()) != nullptr);
    }
    ImTypesetContext* const ctx = ImTypeset::GetCurrentContext();
    ctx->draw_rules = DrawRules::product();
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    const struct {
        const char* name;
        float em;
        int zoom;
        const char* text;
    } shots[] = {
        {"f_i", 32.0f, 4, "$f_i$"},
        {"f_i-sup", 32.0f, 4, "$f_i^2$"},
        {"underline", 32.0f, 4, "$\\underline{x}$"},
        {"underline-in-fraction", 32.0f, 4, "$\\frac{\\underline{x}}{y}$"},
        {"letters", 16.0f, 3,
         "$f_i + N_k^2 + U_i + C_n + \\Gamma_i + F_n^2 + x_i = \\underline{x+y}$"},
    };
    for (const auto& shot : shots) {
        soft_raster::Image halves[2];
        ImVec2 sizes[2];
        // The right-most thing drawn: the paragraph's own width is the
        // measure's, which is not what to crop to.
        float drawn_right = 0.0f;
        for (int half = 0; half < 2; ++half) {
            ctx->tex_rules = half == 0 ? TexRules::reference() : TexRules::product();
            ImTypesetStyle style = ImTypeset::GetStyle();
            style.EmSize = shot.em;
            style.Color = IM_COL32(0, 0, 0, 255);
            // Set afresh each time: the rules are read when the material is
            // built, and the same text would be kept.
            ImTypeset::SetParagraphText(p, "", nullptr, nullptr, 0, ImTypesetTextFlags_Math);
            ImTypeset::SetParagraphText(p, shot.text, nullptr, nullptr, 0, ImTypesetTextFlags_Math);
            ImTypeset::LayoutParagraph(p, 100000.0f, &style);
            CHECK(ImTypeset::GetParagraphError(p)->Code == ImTypesetError_None);
            sizes[half] = ImTypeset::GetParagraphSize(p);
            const ImTypesetLine line = ImTypeset::GetParagraphLine(p, 0);
            ImGui::NewFrame();
            ImDrawList* list = ImGui::GetBackgroundDrawList();
            const ImVec2 at(8.0f, 8.0f);
            list->AddRectFilled(at, ImVec2(static_cast<float>(page_width), at.y + sizes[half].y),
                                IM_COL32(232, 238, 246, 255));
            const int first_vertex = list->VtxBuffer.Size;
            CHECK(ImTypeset::DrawParagraph(p, list, at, nullptr));
            std::printf("    %s, %s rules: paragraph %.0f x %.0f px, baseline at %.0f\n", shot.text,
                        half == 0 ? "reference" : "product", static_cast<double>(sizes[half].x),
                        static_cast<double>(sizes[half].y), static_cast<double>(line.Baseline));
            for (int v = first_vertex; v + 3 < list->VtxBuffer.Size; v += 4) {
                const ImDrawVert& a = list->VtxBuffer[v];
                const ImDrawVert& c = list->VtxBuffer[v + 2];
                const bool rule = a.uv.x == c.uv.x && a.uv.y == c.uv.y;
                drawn_right = std::max(drawn_right, c.pos.x);
                std::printf(
                    "        %s x %.0f..%.0f  y %.0f..%.0f\n", rule ? "rule " : "glyph",
                    static_cast<double>(a.pos.x - at.x), static_cast<double>(c.pos.x - at.x),
                    static_cast<double>(a.pos.y - at.y), static_cast<double>(c.pos.y - at.y));
            }
            ImGui::Render();
            halves[half] =
                soft_raster::Image(page_width, page_height, IM_COL32(255, 255, 255, 255));
            renderer.render(ImGui::GetDrawData(), halves[half]);
        }
        const int crop_width = std::min(page_width, static_cast<int>(drawn_right) + 12);
        const int crop_height =
            std::min(page_height, static_cast<int>(std::max(sizes[0].y, sizes[1].y)) + 16);
        const int gap = 2;
        soft_raster::Image out(crop_width * shot.zoom, (2 * crop_height + gap) * shot.zoom,
                               IM_COL32(120, 120, 120, 255));
        for (int half = 0; half < 2; ++half) {
            const int top = half * (crop_height + gap) * shot.zoom;
            for (int y = 0; y < crop_height * shot.zoom; ++y) {
                for (int x = 0; x < crop_width * shot.zoom; ++x) {
                    if (halves[half].contains(x / shot.zoom, y / shot.zoom))
                        std::memcpy(out.pixel(x, top + y),
                                    halves[half].pixel(x / shot.zoom, y / shot.zoom), 4);
                }
            }
        }
        const std::string path = check::format("%s/texfix-%s.png", directory, shot.name);
        CHECK(soft_raster::write_png(path.c_str(), out));
        std::printf("    wrote %s (%d x %d)\n", path.c_str(), out.width, out.height);
    }
    ImTypeset::DestroyParagraph(p);
    ImTypeset::DestroyContext();
    renderer.shutdown();
    ImGui::DestroyContext();
}

// --- The public API ---------------------------------------------------------------------

// A context lays out under TexRules::product() unless it is told otherwise,
// and what it is told reaches the three places that read a rule: the math
// fonts, the composition of text, and the breaker's parameters.
TEST(the_public_api_lays_out_under_the_product_rules)
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(800.0f, 600.0f);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImTypesetContext* const ctx = ImTypeset::CreateContext();
    REQUIRE(ctx != nullptr);
    static const struct {
        ImTypesetFace face;
        const char* file;
    } faces[] = {
        {ImTypesetFace_Regular, "Roboto-Regular.ttf"},
        {ImTypesetFace_Math, "latinmodern-math.otf"},
    };
    for (const auto& entry : faces) {
        const std::string path = golden::data_path(std::string("fonts/") + entry.file);
        REQUIRE(ImTypeset::AddFontFromFileTTF(entry.face, path.c_str()) != nullptr);
    }
    const TexRules product = TexRules::product();
    CHECK(ctx->tex_rules.skips_made_shrink_finite == product.skips_made_shrink_finite);
    CHECK(ctx->tex_rules.left_skip_omitted_when_zero == product.left_skip_omitted_when_zero);
    CHECK(ctx->tex_rules.post_break_text_stops_pruning == product.post_break_text_stops_pruning);
    CHECK(ctx->tex_rules.easy_line == product.easy_line);
    CHECK(ctx->tex_rules.sentence_space_truncates == product.sentence_space_truncates);
    CHECK(ctx->tex_rules.italic_kern_only_without_subscript ==
          product.italic_kern_only_without_subscript);
    CHECK(ctx->tex_rules.underline_clearance == product.underline_clearance);
    CHECK(ctx->tex_rules.sublist_nucleus_packed == product.sublist_nucleus_packed);
    CHECK(ctx->tex_rules.mu_truncates == product.mu_truncates);

    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    ImTypesetStyle style = ImTypeset::GetStyle();
    style.EmSize = 16.0f;
    style.Align = ImTypesetAlign_Justify;
    style.Spacing = ImTypesetSpacing_TeX;

    // The list the math italic f is in, wherever the formula's boxes put it,
    // and the box of the formula itself: the first box of the material.
    struct Found {
        const Box* formula = nullptr;
        NodeView list;
        size_t at = 0;
    };
    const auto find_f = [&]() {
        Found found;
        std::vector<NodeView> pending;
        for (const Node& node : p->block.run.nodes) {
            if (node.is(NodeType::BOX) && found.formula == nullptr) {
                found.formula = &p->arena.box(node);
                pending.push_back(p->arena.view(found.formula->contents));
            }
        }
        while (!pending.empty()) {
            const NodeView list = pending.back();
            pending.pop_back();
            for (size_t i = 0; i < list.size(); ++i) {
                if (list[i].is(NodeType::CHAR) && list[i].codepoint() == 0x1D453) {
                    found.list = list;
                    found.at = i;
                    return found;
                }
                if (list[i].is(NodeType::BOX))
                    pending.push_back(p->arena.contents(list[i]));
            }
        }
        return found;
    };
    Scaled widths[2] = {0, 0};
    Scaled kern_width = 0;
    for (int pass = 0; pass < 2; ++pass) {
        CHECK_CONTEXT("%s rules", pass == 0 ? "product" : "reference");
        if (pass == 1)
            ctx->tex_rules = TexRules::reference();
        // Set afresh: the rules are read when the material is built.
        ImTypeset::SetParagraphText(p, "", nullptr, nullptr, 0, ImTypesetTextFlags_Math);
        ImTypeset::SetParagraphText(p, "A. b $f_i$", nullptr, nullptr, 0, ImTypesetTextFlags_Math);
        ImTypeset::LayoutParagraph(p, 400.0f, &style);
        CHECK(ImTypeset::GetParagraphError(p)->Code == ImTypesetError_None);
        const Found found = find_f();
        REQUIRE(found.formula != nullptr);
        REQUIRE(found.list.size() > found.at + 1);
        widths[pass] = found.formula->width;
        // M9: f and its subscript, with the kern between them only under the
        // reference's rules.
        const Node& after_f = found.list[found.at + 1];
        if (pass == 0) {
            CHECK(after_f.is(NodeType::BOX));
        } else {
            REQUIRE(after_f.is(NodeType::KERN));
            kern_width = after_f.a;
        }
        // The breaker was handed the context's rules.
        CHECK_EQ(ctx->composer.line_params().rules.easy_line, pass == 0);
        CHECK_EQ(ctx->composer.line_params().rules.skips_made_shrink_finite, pass == 0);
    }
    CHECK(kern_width > 0);
    CHECK_EQ(widths[1] - widths[0], kern_width);

    // Y1: the glue after a full stop under ImTypesetSpacing_TeX is the one
    // interword_glue makes under the context's rules, at sizes enough that
    // the two rule sets give different glue at some of them. The sizes are
    // not whole numbers of pixels: at 13 to 20 px to the em this face's space
    // divides exactly and the two give the same glue.
    int told_apart = 0;
    for (const float em : {13.1f, 14.3f, 15.7f, 16.9f, 17.3f, 18.1f, 19.7f, 20.9f}) {
        style.EmSize = em;
        GlueSpec made[2];
        for (int pass = 0; pass < 2; ++pass) {
            CHECK_CONTEXT("em %.1f, %s rules", static_cast<double>(em),
                          pass == 0 ? "product" : "reference");
            ctx->tex_rules = pass == 0 ? TexRules::product() : TexRules::reference();
            ImTypeset::SetParagraphText(p, "", nullptr, nullptr, 0, 0);
            ImTypeset::SetParagraphText(p, "a. b", nullptr, nullptr, 0, 0);
            ImTypeset::LayoutParagraph(p, 400.0f, &style);
            const std::vector<Node>& nodes = p->block.run.nodes;
            REQUIRE(nodes.size() >= 4);
            REQUIRE(nodes[0].is(NodeType::CHAR));
            REQUIRE(nodes[2].is(NodeType::GLUE));
            const FontMetrics* metrics = ctx->resolve(nodes[0].font());
            REQUIRE(metrics != nullptr);
            const Scaled space = clamp_dimen(round_to_scaled(metrics->space_width()));
            made[pass] = nodes[2].spec();
            CHECK(made[pass] == interword_glue(space, 3000, Spacing::NONFRENCH, ctx->tex_rules));
        }
        told_apart += made[0] == made[1] ? 0 : 1;
    }
    CHECK(told_apart > 0);

    ImTypeset::DestroyParagraph(p);
    ImTypeset::DestroyContext();
    ImGui::DestroyContext();
}
