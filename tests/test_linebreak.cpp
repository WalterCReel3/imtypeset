// src/linebreak against the reference: every run of tests/golden/core/
// linebreak.golden (hand-built paragraphs) and paragraphs.golden (real text),
// compared as text -- line count, total demerits, pass, break positions and
// kinds, and every line's pack result and contents. Then the claims of the
// reference's tests/test_linebreak.py and tests/test_firstfit.py that are
// properties and not single answers, and the things only the port has: reuse
// without allocation, and defined behaviour on input the reference hangs or
// raises on.

#include "core_golden.h"

#include "linebreak.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace ImTypeset::detail;

// Counts every allocation the test executable makes, so that a test can show
// that a warmed-up breaker makes none. Not inlined: GCC, seeing through an
// inlined replacement to malloc and free, reports the pair as mismatched.
#if defined(__GNUC__)
#define TEST_LINEBREAK_NOINLINE __attribute__((noinline))
#else
#define TEST_LINEBREAK_NOINLINE
#endif

static size_t g_allocations = 0;

TEST_LINEBREAK_NOINLINE void* operator new(size_t size)
{
    ++g_allocations;
    void* const memory = std::malloc(size != 0 ? size : 1);
    if (memory == nullptr)
        std::abort();
    return memory;
}

TEST_LINEBREAK_NOINLINE void operator delete(void* memory) noexcept
{
    std::free(memory);
}

TEST_LINEBREAK_NOINLINE void operator delete(void* memory, size_t) noexcept
{
    std::free(memory);
}

// Every other form the library or the standard library reaches for
// (std::stable_sort's buffer is a nothrow new), so that no block is allocated
// by one family and freed by another.
TEST_LINEBREAK_NOINLINE void* operator new(size_t size, const std::nothrow_t&) noexcept
{
    ++g_allocations;
    return std::malloc(size != 0 ? size : 1);
}

TEST_LINEBREAK_NOINLINE void* operator new[](size_t size)
{
    return operator new(size);
}

TEST_LINEBREAK_NOINLINE void* operator new[](size_t size, const std::nothrow_t& tag) noexcept
{
    return operator new(size, tag);
}

TEST_LINEBREAK_NOINLINE void operator delete(void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}

TEST_LINEBREAK_NOINLINE void operator delete[](void* memory) noexcept
{
    std::free(memory);
}

TEST_LINEBREAK_NOINLINE void operator delete[](void* memory, size_t) noexcept
{
    std::free(memory);
}

TEST_LINEBREAK_NOINLINE void operator delete[](void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}

namespace
{

constexpr Scaled PT = UNITY;
constexpr Scaled LINE = 100 * PT;
const GlueSpec SPACE{6 * PT, 4 * PT, GlueOrder::NORMAL, 2 * PT, GlueOrder::NORMAL};
const GlueSpec STIFF{6 * PT};
const GlueSpec FIL{0, PT, GlueOrder::FIL};
const int SHOWCASE[] = {12, 19, 27, 14, 12, 13, 19, 15, 18};

int g_violations = 0;

void count_violation(const char*)
{
    ++g_violations;
}

struct ViolationScope {
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

struct TableMeasure {
    const core_golden::MeasureTable* table = nullptr;
    int misses = 0;

    static CharDimens answer(void* user, FontKey font, uint32_t codepoint)
    {
        TableMeasure& self = *static_cast<TableMeasure*>(user);
        const core_golden::MeasureTable::Entry* entry = self.table->find(font, codepoint);
        if (entry == nullptr) {
            ++self.misses;
            return CharDimens{};
        }
        return CharDimens{entry->width, entry->height, entry->depth};
    }

    CharMeasure measure()
    {
        if (table == nullptr || !table->present)
            return CharMeasure();
        return CharMeasure{&TableMeasure::answer, this};
    }
};

LineBreakParams line_break_params(const core_golden::Params& p)
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

std::vector<LineSpec> line_specs(const core_golden::Shape& shape)
{
    std::vector<LineSpec> specs;
    for (const core_golden::Shape::Line& line : shape.lines)
        specs.push_back(LineSpec{line.width, line.indent});
    return specs;
}

const char* token(BreakPass pass)
{
    return core_golden::BREAK_PASS_TOKENS[static_cast<int>(pass)];
}

const char* token(BreakKind kind)
{
    return core_golden::BREAK_KIND_TOKENS[static_cast<int>(kind)];
}

const char* token(PackFitness fitness)
{
    return core_golden::PACK_FITNESS_TOKENS[static_cast<int>(fitness)];
}

// A paragraph as the oracle writes one: the `paragraph` and `breaks` records
// and each line's pack result.
std::vector<std::string> written(const core_golden::Context& context, const Paragraph& paragraph)
{
    std::vector<std::string> out;
    out.push_back(check::format("paragraph %zu %lld %s %u", paragraph.lines.size(),
                                static_cast<long long>(paragraph.total_demerits),
                                token(paragraph.pass_used), paragraph.prepared_length));
    std::string breaks = check::format("breaks %zu", paragraph.breaks.size());
    for (const Break& b : paragraph.breaks)
        breaks += check::format(" %u %s", b.position, token(b.kind));
    out.push_back(breaks);
    for (const BrokenLine& line : paragraph.lines) {
        core_golden::write_packed(context, token(line.packed.fitness), line.packed.natural,
                                  line.packed.excess, line.packed.badness, line.packed.overfull_by,
                                  line.box, out);
    }
    return out;
}

bool same_breaking(const Paragraph& a, const Paragraph& b)
{
    if (a.lines.size() != b.lines.size() || a.total_demerits != b.total_demerits ||
        a.pass_used != b.pass_used || a.breaks.size() != b.breaks.size())
        return false;
    for (size_t i = 0; i < a.breaks.size(); ++i) {
        if (a.breaks[i].position != b.breaks[i].position || a.breaks[i].kind != b.breaks[i].kind)
            return false;
    }
    return true;
}

struct Tally {
    int cases = 0;
    int runs = 0;
    int lines = 0;
    int passes[4] = {0, 0, 0, 0};
    int kinds[6] = {0, 0, 0, 0, 0, 0};
    int closed_forms = 0;
    std::set<std::string> names;
};

// Replays every run of one `linebreak` case. `breaker` and `paragraph` are
// shared by every case of every file, which is the reuse the port offers.
void replay_case(const golden::File& file, const core_golden::Case& c, LineBreaker& breaker,
                 Paragraph& paragraph, Tally& tally)
{
    core_golden::Context context;
    core_golden::Reader reader(file, c);
    while (reader.next_is("source") || reader.next_is("theme"))
        reader.take();
    const core_golden::MeasureTable table = core_golden::read_measure(reader, context);
    TableMeasure measure{&table};

    std::vector<std::string> list_names;
    std::vector<std::vector<Node>> lists;
    while (!reader.done()) {
        if (reader.next_is("nodes")) {
            std::string name;
            lists.push_back(core_golden::read_nodes(reader, context, &name));
            list_names.push_back(name);
            continue;
        }
        const golden::Record& open = reader.take("break");
        const std::string run = open.str(0);
        CHECK_CONTEXT("run %s (line %d)", run.c_str(), open.line);
        const std::vector<Node>* nodes = nullptr;
        for (size_t i = 0; i < lists.size(); ++i) {
            if (list_names[i] == open.raw(1))
                nodes = &lists[i];
        }
        REQUIRE(nodes != nullptr);

        const std::vector<LineSpec> specs = line_specs(core_golden::read_shape(reader));
        const LineBreakParams params = line_break_params(core_golden::read_params(reader));
        const ParShape shape = ParShape::of(specs.data(), specs.size());

        const size_t first = reader.position();
        const core_golden::ParagraphResult expected = core_golden::read_paragraph(reader);
        for (int64_t i = 0; i < expected.line_count && !reader.done(); ++i)
            core_golden::read_packed(reader, context);
        const std::vector<std::string> reference = reader.lines(first, reader.position());

        const bool ok = breaker.break_paragraph(context.arena, *nodes, shape, params,
                                                measure.measure(), paragraph);
        CHECK(ok);
        CHECK(paragraph.error == LineBreakError::NONE);
        if (CHECK_LINES_EQ(reference, written(context, paragraph), "the broken paragraph")) {
            ++tally.passes[static_cast<int>(paragraph.pass_used)];
            for (const Break& b : paragraph.breaks)
                ++tally.kinds[static_cast<int>(b.kind)];
            tally.lines += static_cast<int>(paragraph.lines.size());
        }

        // A breaker of its own, and a shape in closed form where one says the
        // same thing, must choose the same breaks.
        Paragraph fresh;
        break_paragraph(context.arena, *nodes, shape, params, measure.measure(), fresh);
        CHECK(same_breaking(paragraph, fresh));
        if (specs.size() == 1) {
            Paragraph closed;
            break_paragraph(context.arena, *nodes,
                            ParShape::uniform(specs[0].width, specs[0].indent), params,
                            measure.measure(), closed);
            CHECK(same_breaking(paragraph, closed));
            ++tally.closed_forms;
        }

        if (reader.next_is("spec")) {
            core_golden::read_skip(reader);
            reader.take("vbox");
            reader.take("origin");
            core_golden::read_cmds(reader, context);
        }
        reader.take("endbreak");
        ++tally.runs;
    }
    CHECK_EQ(measure.misses, 0);
    tally.names.insert(c.name);
    ++tally.cases;
}

std::vector<Node> para(NodeArena& arena, const std::vector<int>& widths, const GlueSpec& space)
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

std::vector<int> showcase()
{
    return std::vector<int>(std::begin(SHOWCASE), std::end(SHOWCASE));
}

// How many word boxes ended up on each line.
std::vector<int> words_per_line(const NodeArena& arena, const Paragraph& paragraph)
{
    std::vector<int> out;
    for (const BrokenLine& line : paragraph.lines) {
        int words = 0;
        for (const Node& node : arena.contents(line.box))
            words += node.is(NodeType::BOX) ? 1 : 0;
        out.push_back(words);
    }
    return out;
}

LineBreakParams permissive()
{
    LineBreakParams params;
    params.pretolerance = 10000;
    params.tolerance = 10000;
    return params;
}

} // namespace

// --- The goldens ------------------------------------------------------------------

TEST(line_breaking_matches_the_reference)
{
    LineBreaker breaker;
    Paragraph paragraph;
    Tally tally;
    for (const char* name : {"core/linebreak.golden", "core/paragraphs.golden"}) {
        CHECK_CONTEXT("%s", name);
        golden::File file;
        if (!CHECK(file.load(golden::path(name))))
            continue;
        for (const core_golden::Case& c : core_golden::cases(file)) {
            if (c.kind != "linebreak")
                continue;
            CHECK_CONTEXT("case %s (line %d)", c.name.c_str(), c.line);
            replay_case(file, c, breaker, paragraph, tally);
        }
    }
    // Floors a little under what the goldens hold.
    CHECK(tally.cases >= 142);
    CHECK(tally.runs >= 500);
    CHECK(tally.lines > 2800);
    CHECK(tally.closed_forms > 300);
    for (const int count : tally.passes)
        CHECK(count > 20);
    for (const int count : tally.kinds)
        CHECK(count > 40);
    // The cases that exist to pin a tie-break or an overflow were replayed.
    for (const char* needed :
         {"ties-survivor-against-created-and-across-line-counts", "ties-showcase-every-line-free",
          "ties-12-equal-words-at-58pt", "ties-symmetric-words", "demerits-past-awful-bad",
          "adjacent-fitness-classes", "fitness-threshold-stretch-100",
          "fitness-threshold-shrink-13", "glue-penalty-glue", "disc-replacing-2",
          "sample-md-largest-paragraph", "inline-math", "stand-in-characters"}) {
        CHECK_CONTEXT("%s", needed);
        CHECK(tally.names.count(needed) == 1);
    }
}

TEST(parshape_matches_the_reference)
{
    golden::File file;
    REQUIRE(file.load(golden::path("core/linebreak.golden")));
    int shapes = 0;
    for (const core_golden::Case& c : core_golden::cases(file)) {
        if (c.kind != "parshape")
            continue;
        CHECK_CONTEXT("case %s", c.name.c_str());
        core_golden::Reader reader(file, c);
        const std::vector<LineSpec> specs = line_specs(core_golden::read_shape(reader));
        const ParShape listed = ParShape::of(specs.data(), specs.size());
        if (reader.next_is("error")) {
            CHECK_EQ(std::string(reader.take().raw(0)), std::string("empty-parshape"));
            CHECK(listed.empty());
            NodeArena arena;
            Paragraph paragraph;
            CHECK(!break_paragraph(arena, NodeView(), listed, LineBreakParams(), CharMeasure(),
                                   paragraph));
            CHECK(paragraph.error == LineBreakError::EMPTY_PARSHAPE);
            CHECK(paragraph.lines.empty());
            ++shapes;
            continue;
        }
        CHECK(!listed.empty());

        // The same shape in closed form, where the case is one of those.
        ParShape closed;
        if (c.name == "parshape-uniform")
            closed = ParShape::uniform(LINE, 3 * PT);
        else if (c.name == "parshape-hanging-after-0")
            closed = ParShape::hanging(LINE, 12 * PT, 0);
        else if (c.name == "parshape-hanging-after-1")
            closed = ParShape::hanging(LINE, 12 * PT);
        else if (c.name == "parshape-hanging-after-3")
            closed = ParShape::hanging(LINE, 12 * PT, 3);
        else if (c.name == "parshape-hanging-negative")
            closed = ParShape::hanging(LINE, -12 * PT, 2);

        while (reader.next_is("for_line")) {
            const golden::Record& record = reader.take();
            const LineSpec expected{record.i64(1), record.i64(2)};
            CHECK(listed.for_line(record.i64(0)) == expected);
            if (!closed.empty())
                CHECK(closed.for_line(record.i64(0)) == expected);
        }
        CHECK(reader.done());
        ++shapes;
    }
    CHECK_EQ(shapes, 7);

    CHECK(ParShape().empty());
    CHECK(ParShape::of(nullptr, 3).empty());
    CHECK(ParShape().for_line(1) == LineSpec{});
    CHECK(ParShape::uniform(LINE).for_line(0) == LineSpec{});
    CHECK(ParShape::uniform(LINE).for_line(std::numeric_limits<int64_t>::max()) ==
          (LineSpec{LINE, 0}));
}

TEST(line_break_constants_are_the_references)
{
    golden::File file;
    REQUIRE(file.load(golden::path("core/nodes.golden")));
    bool found = false;
    for (const golden::Record& record : file) {
        if (record.is("const") && record.raw(0) == "AWFUL_BAD") {
            CHECK_EQ(record.i64(2), AWFUL_BAD);
            found = true;
        }
    }
    CHECK(found);
    // The formula subtracts fitness classes, so their numbers are semantics.
    CHECK_EQ(static_cast<int>(Fitness::VERY_LOOSE), 0);
    CHECK_EQ(static_cast<int>(Fitness::LOOSE), 1);
    CHECK_EQ(static_cast<int>(Fitness::DECENT), 2);
    CHECK_EQ(static_cast<int>(Fitness::TIGHT), 3);
    const LineBreakParams defaults;
    CHECK_EQ(defaults.pretolerance, int64_t(100));
    CHECK_EQ(defaults.tolerance, int64_t(200));
    CHECK_EQ(defaults.line_penalty, int64_t(10));
    CHECK_EQ(defaults.adj_demerits, int64_t(10000));
    CHECK((defaults.par_fill_skip == GlueSpec{0, 1, GlueOrder::FIL}));
    CHECK(!defaults.first_fit);
}

// --- Total fit, as a property -------------------------------------------------------

// The chosen breaking has the fewest total demerits of every possible breaking,
// found by forcing each of the 2^8 subsets of interword breaks in turn.
TEST(the_chosen_breaking_is_the_best_of_every_possible_breaking)
{
    NodeArena arena;
    const std::vector<int> widths = showcase();
    const std::vector<Node> plain = para(arena, widths, SPACE);
    const ParShape shape = ParShape::uniform(LINE);
    LineBreaker breaker;
    Paragraph chosen;
    REQUIRE(breaker.break_paragraph(arena, plain, shape, permissive(), CharMeasure(), chosen));
    REQUIRE(chosen.pass_used == BreakPass::FIRST);

    const size_t gaps = widths.size() - 1;
    int64_t best = std::numeric_limits<int64_t>::max();
    int scored = 0;
    Paragraph forced_result;
    for (uint32_t mask = 0; mask < (1u << gaps); ++mask) {
        std::vector<Node> forced;
        int breaks = 0;
        for (size_t i = 0; i < widths.size(); ++i) {
            if (i != 0) {
                const bool take = ((mask >> (i - 1)) & 1u) != 0;
                breaks += take ? 1 : 0;
                forced.push_back(penalty(take ? EJECT_PENALTY : INF_PENALTY));
                forced.push_back(glue(SPACE));
            }
            forced.push_back(plain[2 * i]);
        }
        breaker.break_paragraph(arena, forced, shape, permissive(), CharMeasure(), forced_result);
        // Not realisable within the first pass: some line was infeasible.
        if (forced_result.pass_used != BreakPass::FIRST ||
            forced_result.lines.size() != static_cast<size_t>(breaks) + 1)
            continue;
        best = std::min(best, forced_result.total_demerits);
        ++scored;
    }
    CHECK(scored > 0);
    CHECK_EQ(chosen.total_demerits, best);
    // And it is not what a greedy filler does: greedy puts five words on the
    // first line, using every scrap of its shrink.
    CHECK((words_per_line(arena, chosen) == std::vector<int>{4, 5}));
    LineBreakParams greedy = permissive();
    greedy.first_fit = true;
    Paragraph filled;
    breaker.break_paragraph(arena, plain, shape, greedy, CharMeasure(), filled);
    CHECK((words_per_line(arena, filled) == std::vector<int>{5, 4}));
    CHECK(filled.pass_used == BreakPass::FIRST_FIT);
    CHECK_EQ(filled.total_demerits, int64_t(0));
}

// The fitness classes are thresholds against exact badness values: stretch is
// very loose above 99 and loose above 12, shrink is tight above 12 (§817). The
// `fitness-threshold-*` cases of linebreak.golden land a line exactly on 12,
// 13, 99 and 100 where the class changes the total: a glue of 297 sp of
// stretch and shrink makes the badness's ratio the excess itself, forced
// breaks pin the lines, and adj_demerits is 7777 so that a class two away from
// its neighbour's shows. tools/oracle/dump_core.py (`fitness_threshold_cases`)
// has the construction.
//
// The table is what those cases are for. It is compared with the golden, so a
// regenerated golden that no longer lands on a threshold fails here and not
// only in the oracle script; the port is compared with the golden in full.
TEST(fitness_class_thresholds_fall_where_the_reference_puts_them)
{
    struct Expected {
        const char* name; // the case, after "fitness-threshold-"
        const char* run;
        int64_t lines;
        size_t under_test; // which line sits on the threshold
        int64_t badness;
        const char* fitness; // as hpack reports it: the side, not the class
        int64_t total_demerits;
        const char* pass;
        bool seen;
    };
    Expected expected[] = {
        {"stretch-99", "permissive", 2, 0, 99, "loose", 11981, "first", false},   // loose
        {"stretch-100", "permissive", 2, 0, 100, "loose", 27754, "first", false}, // very loose
        {"stretch-12", "permissive", 3, 0, 12, "loose", 2265, "first", false},    // decent
        {"stretch-13", "permissive", 3, 0, 13, "loose", 10087, "first", false},   // loose
        {"shrink-12", "permissive", 3, 1, 12, "tight", 2265, "first", false},     // decent
        {"shrink-13", "permissive", 3, 1, 13, "tight", 10087, "first", false},    // tight
        // Feasibility is `badness > threshold`: a line of badness b passes at b.
        {"stretch-100", "pretolerance-100", 2, 0, 100, "loose", 27754, "first", false},
        {"stretch-100", "pretolerance-99", 2, 0, 100, "loose", 27754, "second", false},
        {"stretch-99", "pretolerance-99", 2, 0, 99, "loose", 11981, "first", false},
        {"stretch-99", "pretolerance-98", 2, 0, 99, "loose", 11981, "second", false},
        // On the final pass a forced break from the only active node is free.
        {"stretch-100", "final-pass-tolerance-99", 2, 0, 100, "loose", 0, "second", false},
        {"stretch-100", "final-pass-tolerance-100", 2, 0, 100, "loose", 0, "second", false},
    };
    const std::string prefix = "fitness-threshold-";

    golden::File file;
    REQUIRE(file.load(golden::path("core/linebreak.golden")));
    LineBreaker breaker;
    Paragraph paragraph;
    Tally tally;
    int runs_read = 0;
    for (const core_golden::Case& c : core_golden::cases(file)) {
        if (c.kind != "linebreak" || c.name.compare(0, prefix.size(), prefix) != 0)
            continue;
        CHECK_CONTEXT("case %s (line %d)", c.name.c_str(), c.line);

        // What the reference answered, against the table.
        core_golden::Context context;
        core_golden::Reader reader(file, c);
        core_golden::read_measure(reader, context);
        core_golden::read_nodes(reader, context);
        while (!reader.done()) {
            const std::string run = reader.take("break").str(0);
            CHECK_CONTEXT("run %s", run.c_str());
            core_golden::read_shape(reader);
            const core_golden::Params params = core_golden::read_params(reader);
            CHECK_EQ(params.adj_demerits, int64_t(7777));
            const core_golden::ParagraphResult result = core_golden::read_paragraph(reader);
            std::vector<core_golden::Packed> lines;
            for (int64_t i = 0; i < result.line_count && !reader.done(); ++i)
                lines.push_back(core_golden::read_packed(reader, context));
            while (!reader.done() && !reader.next_is("endbreak"))
                reader.take();
            reader.take("endbreak");
            ++runs_read;

            Expected* row = nullptr;
            for (Expected& e : expected) {
                if (c.name == prefix + e.name && run == e.run)
                    row = &e;
            }
            if (!CHECK(row != nullptr))
                continue;
            row->seen = true;
            CHECK_EQ(result.line_count, row->lines);
            CHECK_EQ(result.total_demerits, row->total_demerits);
            CHECK_EQ(result.pass, std::string(row->pass));
            if (CHECK(row->under_test < lines.size())) {
                CHECK_EQ(lines[row->under_test].badness, row->badness);
                CHECK_EQ(lines[row->under_test].fitness, std::string(row->fitness));
            }
        }

        // What the port answers, against the reference.
        replay_case(file, c, breaker, paragraph, tally);
    }
    for (const Expected& e : expected) {
        CHECK_CONTEXT("%s / %s", e.name, e.run);
        CHECK(e.seen);
    }
    CHECK_EQ(runs_read, 12);
    CHECK_EQ(tally.runs, 12);
    CHECK_EQ(tally.cases, 6);
}

// --- First fit, as a property -------------------------------------------------------

// No settled line changes as the paragraph arrives a word at a time: every
// word-prefix of the reference's 635-node paragraph is broken first-fit, and
// every line but the last of each prefix must reappear unchanged in the next.
TEST(first_fit_never_moves_a_finished_line_as_words_arrive)
{
    golden::File file;
    REQUIRE(file.load(golden::path("core/paragraphs.golden")));
    const core_golden::Case* wanted = nullptr;
    const std::vector<core_golden::Case> cases = core_golden::cases(file);
    for (const core_golden::Case& c : cases) {
        if (c.name == "sample-md-largest-paragraph")
            wanted = &c;
    }
    REQUIRE(wanted != nullptr);
    core_golden::Context context;
    core_golden::Reader reader(file, *wanted);
    while (reader.next_is("source") || reader.next_is("theme"))
        reader.take();
    const core_golden::MeasureTable table = core_golden::read_measure(reader, context);
    TableMeasure measure{&table};
    const std::vector<Node> material = core_golden::read_nodes(reader, context);
    REQUIRE_EQ(material.size(), size_t(635));

    std::vector<size_t> cuts;
    for (size_t i = 0; i < material.size(); ++i) {
        if (material[i].is(NodeType::GLUE))
            cuts.push_back(i);
    }
    cuts.push_back(material.size());
    REQUIRE_EQ(cuts.size(), size_t(102));

    LineBreakParams ragged;
    ragged.tolerance = 1000;
    ragged.emergency_stretch = 20 * PT;
    ragged.right_skip = GlueSpec{0, PT, GlueOrder::FIL};
    ragged.first_fit = true;

    LineBreaker breaker;
    const NodeView all(material);
    for (const double width_pt : {150.0, 200.0, 270.0, 420.0}) {
        CHECK_CONTEXT("width %g pt", width_pt);
        const ParShape shape = ParShape::uniform(sp_from_pt(width_pt));
        std::vector<std::vector<std::string>> before;
        size_t most = 0;
        int flips = 0;
        for (const size_t cut : cuts) {
            const NodeArena::Mark mark = context.arena.mark();
            Paragraph paragraph;
            breaker.break_paragraph(context.arena, all.sub(0, cut), shape, ragged,
                                    measure.measure(), paragraph);
            std::vector<std::vector<std::string>> after;
            for (const BrokenLine& line : paragraph.lines)
                after.push_back(core_golden::node_lines(context, line.box));
            most = std::max(most, after.size());
            for (size_t i = 0; i + 1 < before.size(); ++i) {
                if (i >= after.size() || before[i] != after[i])
                    ++flips;
            }
            before = std::move(after);
            context.arena.rewind(mark);
        }
        CHECK(most >= 8);
        CHECK_EQ(flips, 0);
    }
    CHECK_EQ(measure.misses, 0);
}

// --- What only the port has ---------------------------------------------------------

TEST(a_warmed_up_breaker_allocates_nothing)
{
    NodeArena arena;
    std::vector<int> widths;
    for (int i = 0; i < 240; ++i)
        widths.push_back(8 + (i * 7) % 23);
    const std::vector<Node> nodes = para(arena, widths, SPACE);
    const NodeArena::Mark mark = arena.mark();
    LineBreaker breaker;
    Paragraph paragraph;

    LineBreakParams total_fit;
    total_fit.tolerance = 1000;
    total_fit.emergency_stretch = 20 * PT;
    LineBreakParams first_fit = total_fit;
    first_fit.first_fit = true;
    const LineSpec listed[] = {LineSpec{120 * PT, 0}, LineSpec{90 * PT, 10 * PT}};
    const ParShape shapes[] = {ParShape::uniform(LINE), ParShape::hanging(LINE, 12 * PT),
                               ParShape::of(listed, 2), ParShape::uniform(40 * PT)};

    // Twice round to grow every buffer to its largest, then count.
    for (int round = 0; round < 2; ++round) {
        for (const ParShape& shape : shapes) {
            for (const LineBreakParams* params : {&total_fit, &first_fit}) {
                arena.rewind(mark);
                breaker.break_paragraph(arena, nodes, shape, *params, CharMeasure(), paragraph);
            }
        }
    }
    size_t lines = 0;
    const size_t before = g_allocations;
    for (int round = 0; round < 3; ++round) {
        for (const ParShape& shape : shapes) {
            for (const LineBreakParams* params : {&total_fit, &first_fit}) {
                arena.rewind(mark);
                breaker.break_paragraph(arena, nodes, shape, *params, CharMeasure(), paragraph);
                lines += paragraph.lines.size();
            }
        }
    }
    const size_t allocations = g_allocations - before;
    CHECK_EQ(allocations, size_t(0));
    CHECK(lines > 500);
}

TEST(the_input_is_not_modified_and_may_live_in_the_arena)
{
    NodeArena arena;
    const std::vector<Node> nodes = para(arena, showcase(), SPACE);
    const NodeSpan stored = arena.add_nodes(nodes);
    Paragraph from_vector;
    Paragraph from_arena;
    LineBreaker breaker;
    breaker.break_paragraph(arena, nodes, ParShape::uniform(LINE), LineBreakParams(), CharMeasure(),
                            from_vector);
    // The view is of storage the line boxes are about to be added to.
    breaker.break_paragraph(arena, arena.view(stored), ParShape::uniform(LINE), LineBreakParams(),
                            CharMeasure(), from_arena);
    CHECK(same_breaking(from_vector, from_arena));
    REQUIRE_EQ(from_vector.lines.size(), from_arena.lines.size());
    for (size_t i = 0; i < from_vector.lines.size(); ++i)
        CHECK(deep_equal(arena, from_vector.lines[i].box, arena, from_arena.lines[i].box));
    CHECK(deep_equal(arena, arena.view(stored), arena, NodeView(nodes)));
    // Each line is a box of the arena carrying its own packing.
    for (const BrokenLine& line : from_arena.lines) {
        CHECK(line.box.is(NodeType::BOX));
        CHECK_EQ(arena.box(line.box).width, LINE);
        CHECK(arena.box(line.box).contents == line.packed.box.contents);
        CHECK_EQ(arena.box(line.box).glue_set, line.packed.box.glue_set);
    }
}

TEST(a_malformed_list_still_breaks_and_says_so)
{
    NodeArena arena;
    std::vector<Node> nodes = para(arena, {30, 30, 30, 30, 30}, SPACE);
    nodes.insert(nodes.begin() + 2, char_node(FontKey::make(0, 10 * PT), 'a'));
    Paragraph paragraph;
    CHECK(!break_paragraph(arena, nodes, ParShape::uniform(LINE), LineBreakParams(), CharMeasure(),
                           paragraph));
    CHECK(paragraph.error == LineBreakError::MALFORMED_LIST);
    CHECK(paragraph.pack_error == PackError::CHAR_WITHOUT_MEASURE);
    CHECK(paragraph.lines.size() >= 2);

    nodes = para(arena, {30, 30, 30}, SPACE);
    nodes.push_back(rule(RUNNING, PT, 0));
    CHECK(!break_paragraph(arena, nodes, ParShape::uniform(LINE), LineBreakParams(), CharMeasure(),
                           paragraph));
    CHECK(paragraph.pack_error == PackError::RUNNING_WIDTH_IN_HLIST);
    CHECK(!paragraph.lines.empty());
}

// The reference trusts a discretionary's replace_count: one that overruns the
// list reads past nothing in Python and would read past the end here, and a
// negative one never terminates there. Both are bounded.
TEST(a_discretionary_that_replaces_more_than_exists_or_less_than_nothing_is_bounded)
{
    NodeArena arena;
    const std::vector<Node> words = para(arena, {40, 30, 30, 40}, SPACE);
    const std::vector<Node> hyphen = {words[0]};
    LineBreaker breaker;
    Paragraph paragraph;
    LineBreakParams free_hyphens;
    free_hyphens.hyphen_penalty = 0;

    for (const int32_t count : {1000000, std::numeric_limits<int32_t>::max(), -1, -1000,
                                std::numeric_limits<int32_t>::min()}) {
        CHECK_CONTEXT("replace_count %d", count);
        std::vector<Node> nodes = {words[0], words[1], words[2]};
        nodes.push_back(arena.add_disc(hyphen, hyphen, count));
        nodes.insert(nodes.end(), words.begin() + 3, words.end());
        for (const bool greedy : {false, true}) {
            free_hyphens.first_fit = greedy;
            CHECK(breaker.break_paragraph(arena, nodes, ParShape::uniform(60 * PT), free_hyphens,
                                          CharMeasure(), paragraph));
            CHECK(!paragraph.lines.empty());
            CHECK(paragraph.breaks.back().kind == BreakKind::PARAGRAPH_END);
            CHECK_EQ(paragraph.breaks.back().position, paragraph.prepared_length);
        }
    }

    // A negative count behaves as zero.
    std::vector<Node> zero = {words[0], words[1], words[2], arena.add_disc(hyphen, hyphen, 0),
                              words[4], words[5], words[6]};
    std::vector<Node> negative = zero;
    negative[3] = arena.add_disc(hyphen, hyphen, -7);
    Paragraph a;
    Paragraph b;
    free_hyphens.first_fit = false;
    breaker.break_paragraph(arena, zero, ParShape::uniform(80 * PT), free_hyphens, CharMeasure(),
                            a);
    breaker.break_paragraph(arena, negative, ParShape::uniform(80 * PT), free_hyphens,
                            CharMeasure(), b);
    CHECK(same_breaking(a, b));
}

TEST(extreme_parameters_and_penalties_are_clamped_and_not_undefined)
{
    NodeArena arena;
    const int64_t lowest = std::numeric_limits<int64_t>::min();
    const int64_t highest = std::numeric_limits<int64_t>::max();
    std::vector<Node> nodes = para(arena, showcase(), SPACE);
    nodes.insert(nodes.begin() + 3, penalty(lowest));
    nodes.insert(nodes.begin() + 9, penalty(highest));
    nodes.insert(nodes.begin() + 12, penalty(-9999));
    const std::vector<Node> hyphen = {nodes[0]};
    nodes.insert(nodes.begin() + 15, arena.add_disc(hyphen, {}, 0));

    LineBreaker breaker;
    Paragraph paragraph;
    for (const int64_t extreme : {lowest, lowest + 1, int64_t(-1), highest - 1, highest}) {
        CHECK_CONTEXT("extreme %lld", static_cast<long long>(extreme));
        for (int field = 0; field < 9; ++field) {
            LineBreakParams params;
            int64_t* const fields[] = {
                &params.pretolerance,           &params.tolerance,
                &params.line_penalty,           &params.adj_demerits,
                &params.double_hyphen_demerits, &params.final_hyphen_demerits,
                &params.hyphen_penalty,         &params.ex_hyphen_penalty,
                &params.emergency_stretch};
            *fields[field] = extreme;
            breaker.break_paragraph(arena, nodes, ParShape::uniform(LINE), params, CharMeasure(),
                                    paragraph);
            if (!CHECK(!paragraph.lines.empty()))
                return;
        }
    }
    // The forced break was taken where it stands, the forbidden one never.
    breaker.break_paragraph(arena, nodes, ParShape::uniform(LINE), LineBreakParams(), CharMeasure(),
                            paragraph);
    bool at_forced = false;
    bool at_forbidden = false;
    for (const Break& b : paragraph.breaks) {
        at_forced = at_forced || (b.kind == BreakKind::PENALTY && b.position == 3);
        at_forbidden = at_forbidden || (b.kind == BreakKind::PENALTY && b.position == 9);
    }
    CHECK(at_forced);
    CHECK(!at_forbidden);
}

TEST(a_negative_hang_after_is_a_contract_violation_taken_as_zero)
{
    const ViolationScope scope;
    const ParShape shape = ParShape::hanging(LINE, 12 * PT, -3);
    CHECK_EQ(g_violations, 1);
    CHECK(shape.for_line(1) == (LineSpec{LINE - 12 * PT, 12 * PT}));
}

// The counting allocator above has to replace every form of operator new and
// delete, or a block comes from one family and goes back to another:
// std::stable_sort takes its buffer with the nothrow form, and the font
// reader sorts that way. Fails only under ASan (alloc-dealloc-mismatch).
TEST(the_counting_allocator_replaces_every_form)
{
    std::vector<std::pair<int, int>> pairs;
    for (int i = 0; i < 200; ++i)
        pairs.emplace_back((i * 37) % 11, i);
    std::stable_sort(pairs.begin(), pairs.end(),
                     [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                         return a.first < b.first;
                     });
    CHECK(std::is_sorted(pairs.begin(), pairs.end()));
    int* const plain = new (std::nothrow) int(7);
    int* const array = new (std::nothrow) int[4]();
    REQUIRE(plain != nullptr && array != nullptr);
    CHECK_EQ(*plain + array[3], 7);
    delete plain;
    delete[] array;
}
