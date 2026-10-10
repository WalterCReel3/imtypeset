// src/pack against the reference: every hpack, vpack, interline and append case
// of tests/golden/core/pack.golden; every line the reference's line breaker
// packed, re-packed from its contents (linebreak.golden, paragraphs.golden); and
// the vertical assembly of those lines. Then the claims of the reference's
// tests/test_pack.py that are about this port's shape and not in a golden.

#include "core_golden.h"

#include "pack.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace ImTypeset::detail;

namespace
{

constexpr Scaled PT = UNITY;

int g_violations = 0;

void count_violation(const char*)
{
    ++g_violations;
}

// A CharMeasure answering from a case's `measure` records. A character the
// golden has no answer for is counted: the port measured something the
// reference did not.
struct TableMeasure {
    const core_golden::MeasureTable* table = nullptr;
    int calls = 0;
    int misses = 0;

    static CharDimens answer(void* user, FontKey font, uint32_t codepoint)
    {
        TableMeasure& self = *static_cast<TableMeasure*>(user);
        ++self.calls;
        const core_golden::MeasureTable::Entry* entry = self.table->find(font, codepoint);
        if (entry == nullptr) {
            ++self.misses;
            return CharDimens{};
        }
        return CharDimens{entry->width, entry->height, entry->depth};
    }

    // Empty when the case has no `measure` record: it ran with no measure.
    CharMeasure measure()
    {
        if (table == nullptr || !table->present)
            return CharMeasure();
        return CharMeasure{&TableMeasure::answer, this};
    }
};

const char* slug(PackError error)
{
    switch (error) {
    case PackError::NONE:
        return "none";
    case PackError::CHAR_WITHOUT_MEASURE:
        return "char-without-measure";
    case PackError::RUNNING_WIDTH_IN_HLIST:
        return "running-width-in-hlist";
    case PackError::CHAR_IN_VLIST:
        return "char-in-vlist";
    case PackError::RUNNING_HEIGHT_OR_DEPTH_IN_VLIST:
        return "running-height-or-depth-in-vlist";
    case PackError::NODE_NOT_IN_ARENA:
        return "node-not-in-arena";
    case PackError::DIMENSION_OVERFLOW:
        return "dimension-overflow";
    }
    return "?";
}

PackMode pack_mode(std::string_view token)
{
    return static_cast<PackMode>(
        core_golden::token_index(token, core_golden::PACK_MODE_TOKENS, "PackMode"));
}

const char* token(PackFitness fitness)
{
    return core_golden::PACK_FITNESS_TOKENS[static_cast<int>(fitness)];
}

// The port's result as the oracle would have written it, the box made from
// `contents`.
std::vector<std::string> written(core_golden::Context& context, const PackResult& result,
                                 NodeView contents)
{
    std::vector<std::string> out;
    const Node box = context.arena.add_box(result.box, contents);
    core_golden::write_packed(context, token(result.fitness), result.natural, result.excess,
                              result.badness, result.overfull_by, box, out);
    return out;
}

InterlineSkip interline_skip(const core_golden::Skip& skip)
{
    return InterlineSkip{skip.baselineskip, skip.lineskip, skip.lineskiplimit};
}

struct Tally {
    int hpack = 0;
    int vpack = 0;
    int errors = 0;
    int interline = 0;
    int append = 0;
    int fitness[7] = {0, 0, 0, 0, 0, 0, 0};
    int orders[4] = {0, 0, 0, 0};
    int measured = 0;
};

void replay_pack(const golden::File& file, const core_golden::Case& c, Tally& tally)
{
    core_golden::Context context;
    core_golden::Reader reader(file, c);
    const bool horizontal = c.kind == "hpack";
    const core_golden::MeasureTable table = core_golden::read_measure(reader, context);
    const std::vector<Node> nodes = core_golden::read_nodes(reader, context);
    const golden::Record& call = reader.take(c.kind);
    const Scaled target = call.i64(0);
    const PackMode mode = pack_mode(call.raw(1));

    TableMeasure measure{&table};
    const PackResult result = horizontal
                                  ? hpack(context.arena, nodes, target, mode, measure.measure())
                                  : vpack(context.arena, nodes, target, mode, call.i64(2));
    CHECK_EQ(measure.misses, 0);
    tally.measured += measure.calls;

    if (reader.next_is("error")) {
        CHECK_EQ(std::string(slug(result.error)), std::string(reader.take().raw(0)));
        ++tally.errors;
    } else {
        CHECK_EQ(std::string(slug(result.error)), std::string("none"));
        const size_t first = reader.position();
        core_golden::read_packed(reader, context);
        CHECK_LINES_EQ(reader.lines(first, reader.position()), written(context, result, nodes),
                       "the pack result");
        ++tally.fitness[static_cast<int>(result.fitness)];
        ++tally.orders[static_cast<int>(result.box.glue_order)];
    }
    CHECK(reader.done());
    ++(horizontal ? tally.hpack : tally.vpack);
}

void replay_interline(const golden::File& file, const core_golden::Case& c, Tally& tally)
{
    core_golden::Context context;
    core_golden::Reader reader(file, c);
    const InterlineSkip skip = interline_skip(core_golden::read_skip(reader));
    const Scaled prev_depth = reader.take("prev_depth").i64(0);
    const Node box = core_golden::read_node(reader, context);
    REQUIRE(box.is(NodeType::BOX));

    Node glue_node;
    const bool any = interline_glue(prev_depth, context.arena.box(box), skip, glue_node);
    if (reader.next_is("none")) {
        reader.take();
        CHECK(!any);
    } else {
        CHECK(any);
        const size_t first = reader.position();
        core_golden::read_node(reader, context);
        CHECK_LINES_EQ(reader.lines(first, reader.position()),
                       core_golden::node_lines(context, glue_node), "the interline glue");
    }
    CHECK(reader.done());
    ++tally.interline;
}

void replay_append(const golden::File& file, const core_golden::Case& c, Tally& tally)
{
    core_golden::Context context;
    core_golden::Reader reader(file, c);
    const InterlineSkip skip = interline_skip(core_golden::read_skip(reader));
    Scaled prev_depth = reader.take("prev_depth").i64(0);
    const std::vector<Node> items = core_golden::read_nodes(reader, context);
    const golden::Record& depths = reader.take("prev_depths");
    REQUIRE_EQ(depths.i64(0), static_cast<int64_t>(items.size()));

    std::vector<Node> vlist;
    for (size_t i = 0; i < items.size(); ++i) {
        prev_depth = append_to_vlist(context.arena, vlist, items[i], prev_depth, skip);
        CHECK_EQ(prev_depth, depths.i64(i + 1));
    }
    const size_t first = reader.position();
    core_golden::read_nodes(reader, context);
    std::vector<std::string> lines;
    core_golden::write_nodes(context, "vlist", vlist, lines);
    CHECK_LINES_EQ(reader.lines(first, reader.position()), lines, "the vertical list");
    CHECK(reader.done());
    ++tally.append;
}

} // namespace

TEST(pack_matches_the_reference)
{
    golden::File file;
    REQUIRE(file.load(golden::path("core/pack.golden")));
    const std::vector<core_golden::Case> cases = core_golden::cases(file, "core/pack");
    REQUIRE(!cases.empty());

    Tally tally;
    for (const core_golden::Case& c : cases) {
        CHECK_CONTEXT("case %s (line %d)", c.name.c_str(), c.line);
        if (c.kind == "hpack" || c.kind == "vpack")
            replay_pack(file, c, tally);
        else if (c.kind == "interline")
            replay_interline(file, c, tally);
        else if (c.kind == "append")
            replay_append(file, c, tally);
        else
            FAIL("pack.golden holds a case kind this test does not replay: %s", c.kind.c_str());
    }
    // Floors a little under what the golden holds; every fitness and every glue
    // order has to have been met for the replay to mean anything.
    CHECK(tally.hpack >= 130);
    CHECK(tally.vpack >= 85);
    CHECK_EQ(tally.errors, 5);
    CHECK_EQ(tally.interline, 15);
    CHECK(tally.append >= 20);
    for (const int count : tally.fitness)
        CHECK(count > 0);
    for (const int count : tally.orders)
        CHECK(count > 0);
    CHECK(tally.measured >= 120);
}

// Every line the reference's breaker produced is an hpack of that line's
// contents to the line's width. Re-packing the contents has to give the same
// result, which checks hpack on a few thousand lines of real text with real
// font metrics. The line's indent is its shift_amount, set after packing.
TEST(every_broken_line_of_the_reference_repacks_identically)
{
    int lines = 0;
    int assemblies = 0;
    int measured = 0;
    for (const char* name : {"core/linebreak.golden", "core/paragraphs.golden"}) {
        CHECK_CONTEXT("%s", name);
        golden::File file;
        if (!CHECK(file.load(golden::path(name))))
            continue;
        for (const core_golden::Case& c : core_golden::cases(file)) {
            if (c.kind != "linebreak")
                continue;
            CHECK_CONTEXT("case %s (line %d)", c.name.c_str(), c.line);
            core_golden::Context context;
            core_golden::Reader reader(file, c);
            while (reader.next_is("source") || reader.next_is("theme"))
                reader.take();
            const core_golden::MeasureTable table = core_golden::read_measure(reader, context);
            TableMeasure measure{&table};

            while (!reader.done()) {
                if (reader.next_is("nodes")) {
                    core_golden::read_nodes(reader, context);
                    continue;
                }
                const std::string run = reader.take("break").str(0);
                CHECK_CONTEXT("run %s", run.c_str());
                core_golden::read_shape(reader);
                core_golden::read_params(reader);
                const core_golden::ParagraphResult paragraph = core_golden::read_paragraph(reader);

                std::vector<Node> line_boxes;
                for (int64_t i = 0; i < paragraph.line_count && !reader.done(); ++i) {
                    const size_t first = reader.position();
                    const core_golden::Packed expected = core_golden::read_packed(reader, context);
                    const Box reference = context.arena.box(expected.box);
                    const std::vector<Node> contents(context.arena.view(reference.contents).begin(),
                                                     context.arena.view(reference.contents).end());

                    PackResult result = hpack(context.arena, contents, reference.width,
                                              PackMode::EXACTLY, measure.measure());
                    CHECK_EQ(std::string(slug(result.error)), std::string("none"));
                    result.box.shift_amount = reference.shift_amount;
                    CHECK_LINES_EQ(reader.lines(first, reader.position()),
                                   written(context, result, contents), "the re-packed line");
                    line_boxes.push_back(expected.box);
                    ++lines;
                }

                // The ship section's vbox: the lines appended with interline
                // glue from IGNORE_DEPTH and packed at their natural height.
                if (reader.next_is("spec")) {
                    const InterlineSkip skip = interline_skip(core_golden::read_skip(reader));
                    std::vector<Node> vlist;
                    Scaled prev_depth = IGNORE_DEPTH;
                    for (const Node& line : line_boxes)
                        prev_depth = append_to_vlist(context.arena, vlist, line, prev_depth, skip);
                    const PackResult page = vpack(context.arena, vlist);
                    const golden::Record& vbox = reader.take("vbox");
                    CHECK_EQ(page.box.width, vbox.i64(0));
                    CHECK_EQ(page.box.height, vbox.i64(1));
                    CHECK_EQ(page.box.depth, vbox.i64(2));
                    CHECK(page.fitness == PackFitness::EXACT);
                    const golden::Record& origin = reader.take("origin");
                    CHECK_EQ(origin.i64(0), int64_t(0));
                    CHECK_EQ(origin.i64(1), page.box.height);
                    core_golden::read_cmds(reader, context);
                    ++assemblies;
                }
                reader.take("endbreak");
            }
            CHECK_EQ(measure.misses, 0);
            measured += measure.calls;
        }
    }
    CHECK(lines > 2800);
    CHECK(assemblies > 470);
    CHECK(measured > 24000);
}

TEST(pack_constants_are_the_references)
{
    golden::File file;
    REQUIRE(file.load(golden::path("core/nodes.golden")));
    int found = 0;
    for (const golden::Record& record : file) {
        if (!record.is("const"))
            continue;
        if (record.raw(0) == "IGNORE_DEPTH") {
            CHECK_EQ(record.i64(2), IGNORE_DEPTH);
            ++found;
        } else if (record.raw(0) == "OVERFULL_BADNESS") {
            CHECK_EQ(record.i64(2), int64_t(OVERFULL_BADNESS));
            ++found;
        }
    }
    CHECK_EQ(found, 2);
    // Exactly -1000 pt, and below any real depth.
    CHECK_EQ(IGNORE_DEPTH, -1000 * PT);
}

// --- The shape of the port --------------------------------------------------------

TEST(packing_reads_a_range_in_place_and_writes_nothing)
{
    NodeArena arena;
    Box word;
    word.width = 10 * PT;
    word.height = 7 * PT;
    word.depth = 2 * PT;
    const GlueSpec space{6 * PT, 4 * PT, GlueOrder::NORMAL, 2 * PT, GlueOrder::NORMAL};
    std::vector<Node> paragraph;
    for (int i = 0; i < 6; ++i) {
        if (i != 0)
            paragraph.push_back(glue(space));
        paragraph.push_back(arena.add_box(word));
    }
    const std::vector<Node> before = paragraph;
    const size_t boxes_before = arena.box_count();
    const size_t nodes_before = arena.node_count();

    // Words 2..4 with the two spaces between them: 30pt + 12pt, stretched by 4pt.
    const NodeView all(paragraph);
    const PackResult middle = hpack(arena, all.sub(2, 5), 46 * PT, PackMode::EXACTLY);
    CHECK_EQ(middle.natural, 42 * PT);
    CHECK_EQ(middle.excess, 4 * PT);
    CHECK_EQ(middle.box.glue_set, 0.5);
    CHECK(middle.box.glue_sign == GlueSign::STRETCHING);
    CHECK(middle.fitness == PackFitness::LOOSE);
    CHECK_EQ(middle.badness, 12);
    CHECK_EQ(middle.box.contents.count, 0u);

    // The same list packed to another width: the first packing left no trace.
    const PackResult tight = hpack(arena, all.sub(2, 5), 40 * PT, PackMode::EXACTLY);
    CHECK(tight.box.glue_sign == GlueSign::SHRINKING);
    CHECK_EQ(tight.box.glue_set, 0.5);
    CHECK(tight.fitness == PackFitness::TIGHT);

    CHECK(paragraph == before);
    CHECK_EQ(arena.box_count(), boxes_before);
    CHECK_EQ(arena.node_count(), nodes_before);

    // Making the box is the caller's step, and may name the arena's own nodes.
    const Node line = arena.add_box(middle.box, all.sub(2, 5));
    CHECK_EQ(arena.contents(line).size(), size_t(5));
    CHECK_EQ(arena.box(line).width, 46 * PT);
    const PackResult again = hpack(arena, arena.contents(line), 46 * PT, PackMode::EXACTLY);
    CHECK_EQ(again.box.glue_set, middle.box.glue_set);
    CHECK_EQ(again.badness, middle.badness);
}

TEST(a_character_is_measured_once_per_occurrence_through_the_callable)
{
    struct Counting {
        int calls = 0;
        static CharDimens answer(void* user, FontKey font, uint32_t codepoint)
        {
            ++static_cast<Counting*>(user)->calls;
            const Scaled scale = font.size();
            return CharDimens{scale * 10, scale * (codepoint == 'g' ? 5 : 7),
                              codepoint == 'g' ? scale * 2 : 0};
        }
    };
    Counting counting;
    const CharMeasure measure{&Counting::answer, &counting};
    CHECK(static_cast<bool>(measure));
    CHECK(!static_cast<bool>(CharMeasure()));

    NodeArena arena;
    const FontKey font = FontKey::make(0, PT);
    const std::vector<Node> nodes = {char_node(font, 'a'), char_node(font, 'g'),
                                     replaced_char(font, 0xFFFD, 0x6F22), char_node(font, 'a')};
    const PackResult result = hpack(arena, nodes, 0, PackMode::ADDITIONAL, measure);
    CHECK_EQ(counting.calls, 4);
    CHECK_EQ(result.box.width, 40 * PT);
    CHECK_EQ(result.box.height, 7 * PT);
    CHECK_EQ(result.box.depth, 2 * PT);
    CHECK(result.fitness == PackFitness::EXACT);
    CHECK(result.error == PackError::NONE);
}

TEST(a_malformed_list_is_reported_and_the_rest_of_it_is_still_packed)
{
    NodeArena arena;
    Box word;
    word.width = 10 * PT;
    word.height = 7 * PT;
    const Node w = arena.add_box(word);
    const FontKey font = FontKey::make(0, PT);

    // No measure: the character contributes nothing and the boxes still count.
    const std::vector<Node> with_char = {w, char_node(font, 'a'), w};
    const PackResult h = hpack(arena, with_char);
    CHECK(h.error == PackError::CHAR_WITHOUT_MEASURE);
    CHECK_EQ(h.box.width, 20 * PT);
    CHECK_EQ(h.box.height, 7 * PT);

    // The first error is the one reported.
    const std::vector<Node> two = {rule(RUNNING, PT, 0), char_node(font, 'a'), w};
    const PackResult first = hpack(arena, two);
    CHECK(first.error == PackError::RUNNING_WIDTH_IN_HLIST);
    CHECK_EQ(first.box.width, 10 * PT);
    // A width of RUNNING + 1 is a length, however absurd, and is added.
    const std::vector<Node> nearly = {rule(RUNNING + 1, PT, 0)};
    CHECK(hpack(arena, nearly).error == PackError::NONE);

    const PackResult v = vpack(arena, with_char);
    CHECK(v.error == PackError::CHAR_IN_VLIST);
    CHECK_EQ(v.box.height, 14 * PT);
    const std::vector<Node> running = {w, rule(PT, RUNNING, 0), rule(PT, PT, RUNNING), w};
    const PackResult r = vpack(arena, running);
    CHECK(r.error == PackError::RUNNING_HEIGHT_OR_DEPTH_IN_VLIST);
    CHECK_EQ(r.box.height, 14 * PT);
}

TEST(a_vertical_list_built_with_interline_glue_has_evenly_spaced_baselines)
{
    const InterlineSkip skip{GlueSpec{12 * PT, 2 * PT, GlueOrder::NORMAL, PT, GlueOrder::NORMAL},
                             GlueSpec{PT, PT / 2}, 0};
    NodeArena arena;
    std::vector<Node> vlist;
    Scaled prev_depth = IGNORE_DEPTH;
    const Scaled dimensions[3][2] = {{7 * PT, 2 * PT}, {5 * PT, 0}, {9 * PT, PT}};
    for (const Scaled* d : dimensions) {
        Box box;
        box.width = PT;
        box.height = d[0];
        box.depth = d[1];
        prev_depth = append_to_vlist(arena, vlist, arena.add_box(box), prev_depth, skip);
    }
    REQUIRE_EQ(vlist.size(), size_t(5));

    std::vector<Scaled> baselines;
    Scaled pen = 0;
    for (const Node& node : vlist) {
        if (node.is(NodeType::GLUE)) {
            pen += node.width();
            // The baselineskip copy keeps its stretch and shrink.
            CHECK_EQ(node.stretch(), 2 * PT);
            CHECK_EQ(node.shrink(), PT);
        } else {
            const Box& box = arena.box(node);
            pen += box.height;
            baselines.push_back(pen);
            pen += box.depth;
        }
    }
    REQUIRE_EQ(baselines.size(), size_t(3));
    CHECK_EQ(baselines[1] - baselines[0], 12 * PT);
    CHECK_EQ(baselines[2] - baselines[1], 12 * PT);
    CHECK_EQ(vpack(arena, vlist).box.height, pen - PT);
    CHECK_EQ(vpack(arena, vlist).box.depth, PT);
}

TEST(appending_something_that_is_neither_a_box_nor_a_rule_is_a_contract_violation)
{
    const ContractHandler before = contract_handler();
    contract_handler() = &count_violation;
    g_violations = 0;

    NodeArena arena;
    std::vector<Node> vlist;
    const InterlineSkip skip{GlueSpec{12 * PT}, GlueSpec{PT}, 0};
    CHECK_EQ(append_to_vlist(arena, vlist, kern(PT), 3 * PT, skip), 3 * PT);
    CHECK_EQ(g_violations, 1);
    REQUIRE_EQ(vlist.size(), size_t(1));
    CHECK(vlist[0] == kern(PT));

    contract_handler() = before;
}
