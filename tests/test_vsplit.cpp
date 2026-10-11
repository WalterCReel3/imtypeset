// Columns at the core: src/vsplit against the reference's advtext/columns.py.
//
// The goldens of tests/golden/tables that hold `vsplit` and `columns` cases
// (vertbreak, documents, columns; tools/oracle/FORMAT-tables.md sections 4 and
// 5) are replayed record by record: every column's head glue and where it
// starts, every legal breakpoint its scan reached with the totals and the cost
// there, the place taken and why the scan stopped, the column packed, its box
// and what it ships; and what the columns did not hold. Integers equal,
// doubles to the last bit. For a `columns` case the vertical list is the
// port's own: the case's blocks composed by DocumentComposer::compose_blocks,
// held to the golden's `vlist` before it is cut.
//
// Below that, lists the reference cannot construct or raises on, the scan fed
// a list a piece at a time, and lists long enough that anything quadratic
// would show.

#include "blocks_golden.h"
#include "core_golden.h"
#include "golden.h"

#include "blocks.h"
#include "shipout.h"
#include "vsplit.h"

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <string>
#include <vector>

using namespace ImTypeset::detail;

namespace
{

struct Tally {
    int cases = 0;
    int splits = 0;
    int columns = 0;
    int empty_columns = 0;
    int candidates = 0;
    int stops[3] = {0, 0, 0};
    int overfull_columns = 0;
    int overflows = 0;
    int set_glue = 0;
    size_t nodes = 0;
    size_t cmds = 0;
};

const char* const STOP_TOKENS[] = {"end", "eject", "overfull"};

// A `v` record of a `vlist` group as a node. A box is an empty one of its size.
Node read_v(core_golden::Reader& reader, core_golden::Context& context)
{
    golden::Cursor cursor(reader.take("v"));
    const std::string kind(cursor.raw());
    if (kind == "box") {
        Box box;
        box.kind = static_cast<BoxKind>(
            core_golden::token_index(cursor.raw(), core_golden::BOX_KIND_TOKENS, "BoxKind"));
        box.width = cursor.i64();
        box.height = cursor.i64();
        box.depth = cursor.i64();
        box.shift_amount = cursor.i64();
        cursor.expect_done();
        return context.arena.add_box(box);
    }
    if (kind == "rule") {
        const Scaled width = cursor.i64();
        const Scaled height = cursor.i64();
        const Scaled depth = cursor.i64();
        cursor.expect_done();
        return rule(width, height, depth);
    }
    if (kind == "glue") {
        const GlueSpec spec = core_golden::read_spec_fields(cursor);
        cursor.expect_done();
        return glue(spec);
    }
    if (kind == "kern") {
        const Scaled width = cursor.i64();
        const KernKind kern_kind = static_cast<KernKind>(
            core_golden::token_index(cursor.raw(), core_golden::KERN_KIND_TOKENS, "KernKind"));
        cursor.expect_done();
        return kern(width, kern_kind);
    }
    if (kind == "penalty") {
        const Scaled value = cursor.i64();
        cursor.expect_done();
        return penalty(value);
    }
    FAIL("a `v` record of no known kind: %s", kind.c_str());
    return penalty(0);
}

// A node as its `v` record: its dimensions and nothing of what it holds.
std::string write_v(const core_golden::Context& context, const Node& node)
{
    switch (node.type) {
    case NodeType::BOX: {
        const Box& box = context.arena.box(node);
        return check::format("v box %s %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64,
                             core_golden::token(box.kind), box.width, box.height, box.depth,
                             box.shift_amount);
    }
    case NodeType::RULE:
        return check::format("v rule %" PRId64 " %" PRId64 " %" PRId64, node.width(), node.height(),
                             node.depth());
    case NodeType::GLUE:
        return "v glue " + core_golden::spec_fields(node.spec());
    case NodeType::KERN:
        return check::format("v kern %" PRId64 " %s", node.width(),
                             core_golden::token(node.kern_kind()));
    case NodeType::PENALTY:
        return check::format("v penalty %" PRId64, node.penalty());
    default:
        return "v <not-vertical>";
    }
}

std::string glue_line(const Node& node)
{
    return "glue " + core_golden::spec_fields(node.spec());
}

// One `split` group as the port produces it, written as the oracle writes it.
std::vector<std::string> written(core_golden::Context& context, const std::string& name,
                                 Scaled height, size_t count, Scaled split_max_depth,
                                 const GlueSpec& split_top_skip, const ColumnList& list,
                                 const ColumnSplit& split, CharMeasure measure, Tally& tally)
{
    std::vector<std::string> out;
    out.push_back(check::format("split %s %" PRId64 " %zu %" PRId64,
                                golden::percent_encode(name).c_str(), height, count,
                                split_max_depth));
    out.push_back(core_golden::write_spec("split_top_skip", split_top_skip));
    std::vector<DrawCmd> cmds;
    for (size_t k = 0; k < split.columns.size(); ++k) {
        const SplitColumn& column = split.columns[k];
        out.push_back(check::format("column %zu %d %zu %zu", k, column.head ? 1 : 0, column.start,
                                    column.cut.best_place));
        if (column.head)
            out.push_back(glue_line(column.glue));
        out.push_back(check::format("scan %zu", column.candidate_end - column.candidate_first));
        for (size_t c = column.candidate_first; c < column.candidate_end; ++c) {
            const BreakCandidate& cand = split.candidates[c];
            out.push_back(
                check::format("cand %zu %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64
                              " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %d",
                              cand.index, cand.penalty, cand.cur_height, cand.prev_dp,
                              cand.stretch[GlueOrder::NORMAL], cand.stretch[GlueOrder::FIL],
                              cand.stretch[GlueOrder::FILL], cand.stretch[GlueOrder::FILLL],
                              cand.shrink, cand.badness, cand.cost, cand.taken ? 1 : 0));
            ++tally.candidates;
        }
        out.push_back(check::format("place %zu %" PRId64 " %s", column.cut.best_place,
                                    column.cut.least_cost,
                                    STOP_TOKENS[static_cast<int>(column.cut.stop)]));
        ++tally.stops[static_cast<int>(column.cut.stop)];
        const PackResult& packed = column.packed;
        CHECK(packed.error == PackError::NONE);
        out.push_back(check::format("packed %s %" PRId64 " %" PRId64 " %d %" PRId64,
                                    blocks_golden::token(packed.fitness), packed.natural,
                                    packed.excess, packed.badness, packed.overfull_by));
        if (packed.fitness == PackFitness::OVERFULL)
            ++tally.overfull_columns;
        if (column.count == 0)
            ++tally.empty_columns;
        out.push_back(check::format(
            "colbox %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %s %s %s", packed.box.width,
            packed.box.height, packed.box.depth, packed.box.shift_amount,
            core_golden::hex_float(packed.box.glue_set).c_str(),
            core_golden::token(packed.box.glue_sign), core_golden::token(packed.box.glue_order)));
        if (packed.box.glue_sign != GlueSign::NORMAL)
            ++tally.set_glue;
        out.push_back(check::format("origin 0 %" PRId64, packed.box.height));
        const NodeView contents(list.nodes.data() + (column.count != 0 ? column.first : 0),
                                column.count);
        const Node box = context.arena.add_box(packed.box, contents);
        cmds.clear();
        CHECK(ship(context.arena, box, Point{0, packed.box.height}, measure, cmds) ==
              ShipError::NONE);
        tally.cmds += cmds.size();
        const std::vector<std::string> shipped = blocks_golden::written(context, cmds);
        out.insert(out.end(), shipped.begin(), shipped.end());
        out.push_back("endcolumn");
        ++tally.columns;
    }
    out.push_back(check::format("overflow %d %zu %zu", split.overflow_head ? 1 : 0,
                                split.overflow_start, split.overflow_count));
    if (split.overflow_head) {
        out.push_back(glue_line(split.overflow_glue));
        ++tally.overflows;
    }
    out.push_back("endsplit");
    return out;
}

// The `split` groups of a case, each cut from `material` afresh.
void replay_splits(const golden::File& file, core_golden::Reader& reader,
                   core_golden::Context& context, const std::vector<Node>& material,
                   CharMeasure measure, Tally& tally)
{
    ColumnList list;
    ColumnSplit split;
    int groups = 0;
    while (reader.next_is("split")) {
        const size_t first = reader.position();
        const size_t end = blocks_golden::group_end(file, first, reader.end(), "split", "endsplit");
        golden::Cursor open(reader.take("split"));
        const std::string name = open.str();
        const Scaled height = open.i64();
        const size_t count = static_cast<size_t>(open.u32());
        const Scaled split_max_depth = open.i64();
        open.expect_done();
        CHECK_CONTEXT("split %s (line %d)", name.c_str(), file.records()[first].line);
        const GlueSpec split_top_skip = core_golden::read_spec(reader, "split_top_skip");

        list.assign(material);
        split.tracing = true;
        REQUIRE(split_into_columns(context.arena, list, height, count, split, split_top_skip,
                                   split_max_depth));
        CHECK(split.error == VsplitError::NONE);
        CHECK_EQ(split.columns.size(), count);
        CHECK_LINES_EQ(reader.lines(first, end),
                       written(context, name, height, count, split_max_depth, split_top_skip, list,
                               split, measure, tally),
                       "the split");

        // What the cut is, whatever the records say: every node of the
        // material is in exactly one column, deleted at a cut, or left over;
        // and one vsplit after another gives the same columns.
        size_t at = 0;
        for (const SplitColumn& column : split.columns) {
            if (!column.head)
                continue;
            CHECK(column.start >= at);
            for (size_t i = at; i < column.start; ++i)
                CHECK(is_discardable(material[i]));
            CHECK(!is_discardable(material[column.start]));
            // The contents after the glue are the material itself.
            for (size_t i = 1; i < column.count; ++i)
                CHECK(list.nodes[column.first + i] == material[column.start + i - 1]);
            at = column.start + (column.count != 0 ? column.count - 1 : 0);
        }
        if (split.overflow_head) {
            for (size_t i = at; i < split.overflow_start; ++i)
                CHECK(is_discardable(material[i]));
            CHECK_EQ(split.overflow_count, material.size() - split.overflow_start + 1);
        } else {
            for (size_t i = at; i < material.size(); ++i)
                CHECK(is_discardable(material[i]));
        }

        while (reader.position() < end)
            reader.take();
        ++tally.splits;
        ++groups;
    }
    CHECK(groups > 0);
}

void replay_vsplit_case(const golden::File& file, const core_golden::Case& c, Tally& tally)
{
    core_golden::Context context;
    core_golden::Reader reader(file, c);
    reader.take("source");
    const uint32_t count = reader.take("vlist").u32(0);
    std::vector<Node> material;
    for (uint32_t i = 0; i < count && !reader.done(); ++i)
        material.push_back(read_v(reader, context));
    reader.take("endvlist");
    tally.nodes += material.size();
    replay_splits(file, reader, context, material, CharMeasure(), tally);
    CHECK(reader.done());
    ++tally.cases;
}

// The port's Block from one block group: tests/test_blocks.cpp's.
bool build_block(const blocks_golden::BlockIn& in, const blocks_golden::ThemeIn& theme,
                 MetricsResolver resolve, Block& block)
{
    block.type = in.type;
    block.style = BlockStyle();
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
    block.clear_content();
    std::string why;
    switch (in.type) {
    case BlockType::TEXT:
        for (const blocks_golden::Event& event : in.events) {
            if (event.hard_break) {
                hard_break(block.text.run, in.align);
            } else if (!append_text(block.text.run, event.text, event.font, event.breakable,
                                    theme.composition, resolve, &why)) {
                FAIL("refused: %s", why.c_str());
                return false;
            }
        }
        if (in.has_marker && !set_marker(block.text, in.marker_text, in.marker_font,
                                         theme.composition, resolve, &why)) {
            FAIL("marker refused: %s", why.c_str());
            return false;
        }
        break;
    case BlockType::CODE:
        if (!set_code(block.code, in.source, blocks_golden::spec_of(in.font), theme.composition,
                      resolve, &why)) {
            FAIL("code refused: %s", why.c_str());
            return false;
        }
        break;
    case BlockType::RULE:
        break;
    case BlockType::TABLE:
        FAIL("a table in a columns case: the goldens hold none");
        return false;
    }
    return true;
}

void replay_columns_case(const golden::File& file, const core_golden::Case& c, Tally& tally)
{
    core_golden::Context context;
    core_golden::Reader reader(file, c);
    reader.take("source");
    const blocks_golden::ThemeIn theme = blocks_golden::read_theme(reader);
    blocks_golden::Recorded recorded;
    recorded.read(reader, context);
    const core_golden::MeasureTable table = core_golden::read_measure(reader, context);
    blocks_golden::TableMeasure table_measure(table);
    const MetricsResolver resolve = recorded.resolver();
    const CharMeasure measure = table_measure.measure();

    const std::vector<blocks_golden::BlockIn> inputs = blocks_golden::read_blocks(reader, context);
    std::vector<Block> blocks(inputs.size());
    for (size_t i = 0; i < inputs.size(); ++i) {
        CHECK_CONTEXT("block %zu (line %d)", i, inputs[i].line);
        if (!build_block(inputs[i], theme, resolve, blocks[i]))
            return;
    }

    // The composition is tests/test_blocks.cpp's to hold stage by stage; here
    // it is made, and held by the list it comes to.
    REQUIRE(reader.next_is("compose"));
    const Scaled width = reader.peek().i64(1);
    {
        const size_t first = reader.position();
        const size_t end =
            blocks_golden::group_end(file, first, reader.end(), "compose", "endcompose");
        while (reader.position() < end)
            reader.take();
    }
    DocumentComposer composer;
    composer.measure = measure;
    Composed composed;
    REQUIRE(composer.compose_blocks(context.arena, blocks.data(), blocks.size(), Structure(),
                                    theme.theme, DocumentRules(), width, composed));

    const std::vector<Node>& material = composed.stacked.vlist;
    {
        const size_t first = reader.position();
        const uint32_t count = reader.take("vlist").u32(0);
        for (uint32_t i = 0; i < count && !reader.done(); ++i)
            reader.take("v");
        reader.take("endvlist");
        std::vector<std::string> actual;
        actual.push_back(check::format("vlist %zu", material.size()));
        for (const Node& node : material)
            actual.push_back(write_v(context, node));
        actual.push_back("endvlist");
        CHECK_LINES_EQ(reader.lines(first, reader.position()), actual, "the vertical list");
    }
    tally.nodes += material.size();
    replay_splits(file, reader, context, material, measure, tally);
    CHECK(reader.done());
    CHECK_EQ(recorded.misses, 0);
    CHECK_EQ(table_measure.misses, 0);
    ++tally.cases;
}

Tally replay_file(const char* name, const char* kind)
{
    Tally tally;
    CHECK_CONTEXT("%s", name);
    golden::File file;
    if (!file.load(golden::path(name))) {
        FAIL("cannot load %s", name);
        return tally;
    }
    const std::vector<core_golden::Case> cases = core_golden::cases(file);
    CHECK(!cases.empty());
    for (const core_golden::Case& c : cases) {
        CHECK_CONTEXT("case %s (line %d)", c.name.c_str(), c.line);
        if (!CHECK_EQ(c.kind, std::string(kind)))
            continue;
        if (c.kind == "vsplit")
            replay_vsplit_case(file, c, tally);
        else
            replay_columns_case(file, c, tally);
    }
    std::printf("    %s: %d cases, %zu nodes, %d splits, %d columns (%d empty, %d overfull, "
                "%d with glue set), %d candidates, stops %d end / %d eject / %d overfull, "
                "%d overflows, %zu commands\n",
                name, tally.cases, tally.nodes, tally.splits, tally.columns, tally.empty_columns,
                tally.overfull_columns, tally.set_glue, tally.candidates, tally.stops[0],
                tally.stops[1], tally.stops[2], tally.overflows, tally.cmds);
    return tally;
}

} // namespace

TEST(hand_built_lists_are_cut_where_the_reference_cuts_them)
{
    const Tally tally = replay_file("tables/vertbreak.golden", "vsplit");
    // A golden that went missing or a case that was skipped must not read as
    // a pass.
    CHECK_EQ(tally.cases, 19);
    CHECK(tally.stops[0] > 0 && tally.stops[1] > 0 && tally.stops[2] > 0);
    CHECK(tally.set_glue > 0);
    CHECK(tally.overfull_columns > 0);
    CHECK(tally.empty_columns > 0);
}

TEST(composed_documents_given_by_dimension_are_cut_where_the_reference_cuts_them)
{
    const Tally tally = replay_file("tables/documents.golden", "vsplit");
    CHECK_EQ(tally.cases, 5);
    CHECK(tally.overflows > 0);
}

TEST(documents_composed_here_are_cut_and_shipped_column_by_column_as_the_reference_does)
{
    const Tally tally = replay_file("tables/columns.golden", "columns");
    CHECK_EQ(tally.cases, 4);
    CHECK(tally.cmds > 1000);
    CHECK(tally.overfull_columns > 0);
}

// --- What the reference is never asked ------------------------------------------------------

namespace
{

Node empty_box(NodeArena& arena, Scaled height, Scaled depth = 0)
{
    Box box;
    box.width = 100 * UNITY;
    box.height = height;
    box.depth = depth;
    return arena.add_box(box);
}

// `lines` boxes of 10 pt with 2 pt of glue between.
std::vector<Node> uniform(NodeArena& arena, size_t lines)
{
    std::vector<Node> out;
    for (size_t i = 0; i < lines; ++i) {
        if (i != 0)
            out.push_back(glue(GlueSpec{2 * UNITY, 0, GlueOrder::NORMAL, 0, GlueOrder::NORMAL}));
        out.push_back(empty_box(arena, 10 * UNITY));
    }
    return out;
}

} // namespace

TEST(a_scan_fed_a_list_in_pieces_ends_where_a_scan_of_the_whole_list_does)
{
    NodeArena arena;
    // Boxes, glue, kerns before glue and before boxes, penalties: every arm
    // of section 973, with a kern at each place a piece can end.
    std::vector<Node> list;
    for (int i = 0; i < 40; ++i) {
        list.push_back(empty_box(arena, (8 + i % 3) * UNITY, (i % 4) * UNITY));
        switch (i % 5) {
        case 0:
            list.push_back(
                glue(GlueSpec{UNITY, UNITY / 2, GlueOrder::NORMAL, UNITY / 4, GlueOrder::NORMAL}));
            break;
        case 1:
            list.push_back(kern(UNITY));
            list.push_back(glue(GlueSpec{UNITY, 0, GlueOrder::NORMAL, 0, GlueOrder::NORMAL}));
            break;
        case 2:
            list.push_back(kern(2 * UNITY));
            break;
        case 3:
            list.push_back(penalty(i * 10));
            list.push_back(glue(GlueSpec{UNITY, 0, GlueOrder::NORMAL, 0, GlueOrder::NORMAL}));
            break;
        default:
            list.push_back(kern(UNITY));
            list.push_back(penalty(50));
            break;
        }
    }
    for (const Scaled h : {Scaled(1), 30 * UNITY, 97 * UNITY, 200 * UNITY, 10000 * UNITY}) {
        CHECK_CONTEXT("height %lld", static_cast<long long>(h));
        std::vector<BreakCandidate> whole_trace;
        const VertBreakResult whole = vert_break(arena, list, h, 2 * UNITY, &whole_trace);
        for (size_t piece = 1; piece <= 7; ++piece) {
            CHECK_CONTEXT("pieces of %zu", piece);
            VertBreak scan;
            scan.begin(h, 2 * UNITY);
            std::vector<BreakCandidate> trace;
            size_t given = 0;
            bool done = false;
            int calls = 0;
            while (!done) {
                given = std::min(list.size(), given + piece);
                done = scan.scan(arena, NodeView(list.data(), given), given == list.size(), &trace);
                CHECK(scan.position() <= given);
                REQUIRE(++calls < 1000);
            }
            CHECK_EQ(scan.best_place, whole.best_place);
            CHECK_EQ(scan.least_cost, whole.least_cost);
            CHECK_EQ(scan.best_height_plus_depth, whole.best_height_plus_depth);
            CHECK(scan.stop == whole.stop);
            REQUIRE_EQ(trace.size(), whole_trace.size());
            for (size_t i = 0; i < trace.size(); ++i) {
                CHECK_EQ(trace[i].index, whole_trace[i].index);
                CHECK_EQ(trace[i].cost, whole_trace[i].cost);
                CHECK_EQ(trace[i].cur_height, whole_trace[i].cur_height);
            }
        }
    }
}

TEST(the_size_of_the_part_split_off_is_its_boxes_and_glue_with_the_last_depth)
{
    NodeArena arena;
    std::vector<Node> list;
    list.push_back(empty_box(arena, 10 * UNITY, 3 * UNITY));
    list.push_back(glue(GlueSpec{2 * UNITY, 0, GlueOrder::NORMAL, 0, GlueOrder::NORMAL}));
    list.push_back(empty_box(arena, 10 * UNITY, 4 * UNITY));
    list.push_back(glue(GlueSpec{2 * UNITY, 0, GlueOrder::NORMAL, 0, GlueOrder::NORMAL}));
    list.push_back(empty_box(arena, 10 * UNITY, 5 * UNITY));
    // Two lines fit 27 pt: 10 + 3 + 2 + 10 = 25, and the third would make 41.
    const VertBreakResult cut = vert_break(arena, list, 27 * UNITY);
    CHECK_EQ(cut.best_place, size_t(3));
    CHECK_EQ(cut.best_height_plus_depth, 29 * UNITY);
    CHECK(cut.stop == ScanStop::OVERFULL);
    // With a depth limit the excess depth is height (section 972): the two
    // lines are 10 + 2 + (1 + 2) + 10 + 3 = 28 before the second one's depth.
    const VertBreakResult limited = vert_break(arena, list, 27 * UNITY, 1 * UNITY);
    CHECK_EQ(limited.best_place, size_t(1));
    CHECK_EQ(limited.best_height_plus_depth, 13 * UNITY);
}

TEST(a_node_that_cannot_be_in_a_vertical_list_is_left_out_and_named)
{
    NodeArena arena;
    std::vector<Node> list = uniform(arena, 4);
    list.insert(list.begin() + 2, char_node(FontKey::make(0, 10 * UNITY), 'x'));
    list.insert(list.begin() + 4, math_on(0));
    std::vector<BreakCandidate> trace;
    const VertBreakResult cut = vert_break(arena, list, 1000 * UNITY, MAX_DIMEN, &trace);
    CHECK(cut.error == VsplitError::NOT_VERTICAL);
    CHECK_EQ(cut.best_place, list.size());
    CHECK_EQ(cut.best_height_plus_depth, 46 * UNITY);

    // The reference raises on these too.
    std::vector<Node> running = uniform(arena, 2);
    running.push_back(rule(10 * UNITY, RUNNING, 0));
    CHECK(vert_break(arena, running, 1000 * UNITY).error == VsplitError::RUNNING_RULE);
    const Node head[2] = {rule(10 * UNITY, RUNNING, 0), running[0]};
    const Pruned pruned = prune_page_top(arena, NodeView(head, 2));
    CHECK(pruned.error == VsplitError::RUNNING_RULE);
    CHECK(pruned.head);
    CHECK_EQ(pruned.glue.width(), SPLIT_TOP_SKIP.width);

    // A box of another arena.
    NodeArena other;
    empty_box(other, UNITY);
    empty_box(other, UNITY);
    const Node stranger = empty_box(other, UNITY);
    const Node strange[1] = {stranger};
    CHECK(
        vert_break(arena, NodeView(strange, 1), UNITY).error ==
        (arena.find_box(stranger) != nullptr ? VsplitError::NONE : VsplitError::NODE_NOT_IN_ARENA));

    const Node chars[3] = {penalty(0), char_node(FontKey::make(0, 10 * UNITY), 'x'), list[0]};
    const Pruned through = prune_page_top(arena, NodeView(chars, 3));
    CHECK(through.error == VsplitError::NOT_VERTICAL);
    CHECK(through.head);
    CHECK_EQ(through.start, size_t(2));
}

TEST(no_columns_or_no_height_is_refused_and_an_empty_list_is_empty_columns)
{
    NodeArena arena;
    ColumnList list;
    list.assign(uniform(arena, 3));
    ColumnSplit split;
    CHECK(!split_into_columns(arena, list, 100 * UNITY, 0, split));
    CHECK(!split_into_columns(arena, list, 0, 2, split));
    CHECK(!split_into_columns(arena, list, -UNITY, 2, split));
    CHECK(split.columns.empty());

    ColumnList nothing;
    REQUIRE(split_into_columns(arena, nothing, 100 * UNITY, 3, split));
    REQUIRE_EQ(split.columns.size(), size_t(3));
    for (const SplitColumn& column : split.columns) {
        CHECK(!column.head);
        CHECK_EQ(column.count, size_t(0));
        CHECK_EQ(column.packed.box.height, 100 * UNITY);
    }
    CHECK(split.fits());
}

TEST(lengths_at_the_ends_of_a_scaled_do_not_overflow_the_sums)
{
    NodeArena arena;
    const Scaled huge = (Scaled(1) << 62);
    std::vector<Node> list;
    for (int i = 0; i < 8; ++i) {
        list.push_back(empty_box(arena, huge, huge));
        list.push_back(glue(GlueSpec{huge, huge, GlueOrder::NORMAL, huge, GlueOrder::FILL}));
        list.push_back(kern(-huge));
        list.push_back(penalty(i % 2 == 0 ? huge : -huge + 1));
    }
    for (const Scaled h : {Scaled(1), huge, -huge}) {
        for (const Scaled d : {MAX_DIMEN, -huge, huge}) {
            const VertBreakResult cut = vert_break(arena, list, h, d);
            CHECK(cut.best_place <= list.size());
        }
    }
    ColumnList columns;
    columns.assign(list);
    ColumnSplit split;
    CHECK(split_into_columns(arena, columns, huge, 4, split));
}

TEST(a_long_list_is_cut_into_many_columns_in_time_proportional_to_its_length)
{
    NodeArena arena;
    const size_t lines = 200000;
    ColumnList list;
    list.assign(uniform(arena, lines));
    ColumnSplit split;
    // Ten lines a column: 20,000 columns. A cut that copied what is left, or
    // scanned from the start of the list, would be 2 * 10^9 steps here.
    const auto start = std::chrono::steady_clock::now();
    REQUIRE(split_into_columns(arena, list, 120 * UNITY, lines / 10, split));
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(split.fits());
    REQUIRE_EQ(split.columns.size(), lines / 10);
    for (size_t k = 0; k < split.columns.size(); k += 997) {
        CHECK_EQ(split.columns[k].start, k * 20);
        CHECK_EQ(split.columns[k].count, size_t(20));
    }
    std::printf("    %zu lines into %zu columns: %.3f s\n", lines, split.columns.size(), seconds);
    CHECK(seconds < 20.0);
}

TEST(a_head_glue_given_apart_is_the_same_scan_as_one_written_in_front)
{
    NodeArena arena;
    std::vector<Node> material;
    for (int i = 0; i < 30; ++i) {
        material.push_back(empty_box(arena, (9 + i % 4) * UNITY, (i % 3) * UNITY));
        if (i % 4 == 3)
            material.push_back(kern(3 * UNITY));
        else
            material.push_back(
                glue(GlueSpec{2 * UNITY, UNITY, GlueOrder::NORMAL, UNITY / 2, GlueOrder::NORMAL}));
    }
    const Node head =
        glue(GlueSpec{4 * UNITY, UNITY / 3, GlueOrder::NORMAL, UNITY / 5, GlueOrder::NORMAL});
    std::vector<Node> whole;
    whole.push_back(head);
    whole.insert(whole.end(), material.begin(), material.end());
    for (const Scaled h : {Scaled(1), 4 * UNITY, 50 * UNITY, 123 * UNITY, 5000 * UNITY}) {
        CHECK_CONTEXT("height %lld", static_cast<long long>(h));
        std::vector<BreakCandidate> expected_trace;
        const VertBreakResult expected = vert_break(arena, whole, h, UNITY, &expected_trace);
        VertBreak scan;
        scan.begin(h, UNITY);
        scan.head(head);
        std::vector<BreakCandidate> trace;
        CHECK(scan.scan(arena, material, true, &trace));
        CHECK_EQ(scan.best_place, expected.best_place);
        CHECK_EQ(scan.least_cost, expected.least_cost);
        CHECK_EQ(scan.best_height_plus_depth, expected.best_height_plus_depth);
        CHECK(scan.stop == expected.stop);
        REQUIRE_EQ(trace.size(), expected_trace.size());
        for (size_t i = 0; i < trace.size(); ++i) {
            CHECK_EQ(trace[i].index, expected_trace[i].index);
            CHECK_EQ(trace[i].cost, expected_trace[i].cost);
            CHECK_EQ(trace[i].cur_height, expected_trace[i].cur_height);
            CHECK(trace[i].stretch == expected_trace[i].stretch);
            CHECK_EQ(trace[i].shrink, expected_trace[i].shrink);
        }
    }
}
