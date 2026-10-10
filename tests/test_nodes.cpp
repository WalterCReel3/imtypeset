// src/nodes against tests/golden/core/nodes.golden; the node arena; and
// tests/core_golden.h itself, by reading every case of every core golden with
// it and writing the records back out.

#include "core_golden.h"

#include "nodes.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

using namespace ImTypeset::detail;

namespace
{

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

constexpr Scaled PT = UNITY;

const FontKey FONT = FontKey::make(0, 10 * PT);

// What walking the goldens saw, so a test can refuse a vacuous pass.
struct Seen {
    int files = 0;
    int cases = 0;
    int top_level_nodes = 0;
    int boxes = 0;
    int discs = 0;
    int replaced = 0;
    int command_lists = 0;
    int commands = 0;
    int packed = 0;
    int runs = 0;
    int measures = 0;
    int deepest = 0;
};

void count_node(const core_golden::Context& context, const Node& node, Seen& seen, int depth)
{
    if (depth > seen.deepest)
        seen.deepest = depth;
    if (node.is_replaced())
        ++seen.replaced;
    if (node.is(NodeType::BOX)) {
        ++seen.boxes;
        for (const Node& child : context.arena.contents(node))
            count_node(context, child, seen, depth + 1);
    } else if (node.is(NodeType::DISC)) {
        ++seen.discs;
        const Disc& disc = context.arena.disc(node);
        for (const Node& child : context.arena.view(disc.pre_break))
            count_node(context, child, seen, depth + 1);
        for (const Node& child : context.arena.view(disc.post_break))
            count_node(context, child, seen, depth + 1);
    }
}

// Reads one node and checks that writing it gives back the file's own lines,
// that a second reading is deep_equal to the first, and that importing it into
// another arena preserves both.
Node round_trip_node(core_golden::Reader& reader, core_golden::Context& context, Seen& seen)
{
    const size_t first = reader.position();
    const Node node = core_golden::read_node(reader, context);
    const std::vector<std::string> expected = reader.lines(first, reader.position());
    CHECK_LINES_EQ(expected, core_golden::node_lines(context, node), "a node written back");
    count_node(context, node, seen, 0);
    ++seen.top_level_nodes;

    // Only the boxes and discretionaries have anything an arena could get
    // wrong; the rest are values.
    if (node.is(NodeType::BOX) || node.is(NodeType::DISC)) {
        core_golden::Context other;
        other.faces = context.faces;
        const Node imported = other.arena.import(context.arena, node);
        CHECK(deep_equal(context.arena, node, other.arena, imported));
        CHECK_LINES_EQ(expected, core_golden::node_lines(other, imported), "an imported node");
        CHECK_EQ(other.arena.nesting(imported), context.arena.nesting(node));
    }
    return node;
}

void round_trip_nodes(core_golden::Reader& reader, core_golden::Context& context, Seen& seen)
{
    const size_t first = reader.position();
    std::string name;
    const std::vector<Node> nodes = core_golden::read_nodes(reader, context, &name);
    std::vector<std::string> written;
    core_golden::write_nodes(context, name, nodes, written);
    CHECK_LINES_EQ(reader.lines(first, reader.position()), written, "a node list written back");
    for (const Node& node : nodes)
        count_node(context, node, seen, 0);
    seen.top_level_nodes += static_cast<int>(nodes.size());
}

void round_trip_packed(core_golden::Reader& reader, core_golden::Context& context, Seen& seen)
{
    const size_t first = reader.position();
    const core_golden::Packed packed = core_golden::read_packed(reader, context);
    std::vector<std::string> written;
    core_golden::write_packed(context, packed.fitness, packed.natural, packed.excess,
                              packed.badness, packed.overfull_by, packed.box, written);
    CHECK_LINES_EQ(reader.lines(first, reader.position()), written, "a pack result written back");
    core_golden::token_index(packed.fitness, core_golden::PACK_FITNESS_TOKENS, "PackFitness");
    count_node(context, packed.box, seen, 0);
    ++seen.packed;
}

void round_trip_cmds(core_golden::Reader& reader, core_golden::Context& context, Seen& seen)
{
    const size_t first = reader.position();
    const std::vector<core_golden::Cmd> cmds = core_golden::read_cmds(reader, context);
    std::vector<std::string> written;
    core_golden::write_cmds(context, cmds, written);
    CHECK_LINES_EQ(reader.lines(first, reader.position()), written, "commands written back");
    ++seen.command_lists;
    seen.commands += static_cast<int>(cmds.size());
}

void round_trip_skip(core_golden::Reader& reader)
{
    const size_t first = reader.position();
    const core_golden::Skip skip = core_golden::read_skip(reader);
    std::vector<std::string> written;
    written.push_back(core_golden::write_spec("baselineskip", skip.baselineskip));
    written.push_back(core_golden::write_spec("lineskip", skip.lineskip));
    written.push_back("lineskiplimit " + core_golden::i64_text(skip.lineskiplimit));
    CHECK_LINES_EQ(reader.lines(first, reader.position()), written, "a skip written back");
}

// Consumes one case by the grammar of tools/oracle/FORMAT.md section 6, using
// nothing but core_golden.h. Returns false for a case kind it does not know.
bool walk_case(const golden::File& file, const core_golden::Case& c, Seen& seen)
{
    core_golden::Context context;
    core_golden::Reader reader(file, c);

    if (c.kind == "table" || c.kind == "gluetotal") {
        while (!reader.done())
            reader.take();
    } else if (c.kind == "predicates") {
        while (!reader.done()) {
            round_trip_node(reader, context, seen);
            const golden::Record& verdict = reader.take();
            CHECK(verdict.is("pred") || verdict.is("penalty_pred") || verdict.is("rule_pred"));
        }
    } else if (c.kind == "hpack" || c.kind == "vpack") {
        const core_golden::MeasureTable measure = core_golden::read_measure(reader, context);
        seen.measures += static_cast<int>(measure.entries.size());
        round_trip_nodes(reader, context, seen);
        const golden::Record& call = reader.take(c.kind);
        core_golden::token_index(call.raw(1), core_golden::PACK_MODE_TOKENS, "PackMode");
        if (reader.next_is("error"))
            reader.take();
        else
            round_trip_packed(reader, context, seen);
    } else if (c.kind == "interline") {
        round_trip_skip(reader);
        reader.take("prev_depth");
        round_trip_node(reader, context, seen);
        if (reader.next_is("none"))
            reader.take();
        else
            CHECK(round_trip_node(reader, context, seen).is(NodeType::GLUE));
    } else if (c.kind == "append") {
        round_trip_skip(reader);
        reader.take("prev_depth");
        round_trip_nodes(reader, context, seen);
        reader.take("prev_depths");
        round_trip_nodes(reader, context, seen);
    } else if (c.kind == "atom") {
        round_trip_node(reader, context, seen);
        reader.take("is_atom");
        round_trip_node(reader, context, seen);
        reader.take("is_atom");
    } else if (c.kind == "ship") {
        const core_golden::MeasureTable measure = core_golden::read_measure(reader, context);
        seen.measures += static_cast<int>(measure.entries.size());
        CHECK(round_trip_node(reader, context, seen).is(NodeType::BOX));
        reader.take("origin");
        if (reader.next_is("error"))
            reader.take();
        else
            round_trip_cmds(reader, context, seen);
    } else if (c.kind == "parshape") {
        const core_golden::Shape shape = core_golden::read_shape(reader);
        if (reader.next_is("error")) {
            reader.take();
            CHECK(shape.lines.empty());
        }
        while (reader.next_is("for_line"))
            reader.take();
    } else if (c.kind == "linebreak") {
        while (reader.next_is("source") || reader.next_is("theme"))
            reader.take();
        const core_golden::MeasureTable measure = core_golden::read_measure(reader, context);
        seen.measures += static_cast<int>(measure.entries.size());
        while (!reader.done()) {
            if (reader.next_is("nodes")) {
                round_trip_nodes(reader, context, seen);
                continue;
            }
            reader.take("break");
            const core_golden::Shape shape = core_golden::read_shape(reader);
            CHECK(!shape.lines.empty());
            core_golden::read_params(reader);
            const core_golden::ParagraphResult result = core_golden::read_paragraph(reader);
            CHECK_EQ(result.line_count, static_cast<int64_t>(result.breaks.size()));
            core_golden::token_index(result.pass, core_golden::BREAK_PASS_TOKENS, "BreakPass");
            if (!result.breaks.empty()) {
                CHECK_EQ(result.breaks.back().kind, std::string("paragraph_end"));
                CHECK_EQ(result.breaks.back().position, result.prepared_length);
            }
            for (const core_golden::ParagraphResult::Break& b : result.breaks)
                core_golden::token_index(b.kind, core_golden::BREAK_KIND_TOKENS, "break kind");
            for (int64_t i = 0; i < result.line_count && !reader.done(); ++i)
                round_trip_packed(reader, context, seen);
            if (reader.next_is("spec")) {
                round_trip_skip(reader);
                reader.take("vbox");
                reader.take("origin");
                round_trip_cmds(reader, context, seen);
            }
            reader.take("endbreak");
            ++seen.runs;
        }
    } else {
        return false;
    }
    CHECK(reader.done());
    return true;
}

} // namespace

// --- The golden: constants, GlueTotal, the predicates ----------------------------

TEST(nodes_match_the_reference)
{
    golden::File file;
    REQUIRE(file.load(golden::path("core/nodes.golden")));
    const std::vector<core_golden::Case> cases = core_golden::cases(file, "core/nodes");
    REQUIRE(!cases.empty());

    int totals = 0;
    int verdicts = 0;
    for (const core_golden::Case& c : cases) {
        CHECK_CONTEXT("case %s", c.name.c_str());
        core_golden::Context context;
        core_golden::Reader reader(file, c);

        if (c.kind == "table") {
            while (!reader.done()) {
                const golden::Record& record = reader.take("const");
                const std::string name(record.raw(0));
                if (name == "INF_PENALTY")
                    CHECK_EQ(record.i64(2), INF_PENALTY);
                else if (name == "EJECT_PENALTY")
                    CHECK_EQ(record.i64(2), EJECT_PENALTY);
                else if (name == "INF_BAD")
                    CHECK_EQ(record.i64(2), int64_t(INF_BAD));
                // The remaining constants (AWFUL_BAD, IGNORE_DEPTH,
                // OVERFULL_BADNESS, BILLION) are pack's, linebreak's and
                // shipout's, and are checked where they are defined.
            }
        } else if (c.kind == "gluetotal") {
            GlueTotal total;
            GlueTotal summed;
            while (reader.next_is("contrib")) {
                const golden::Record& record = reader.take();
                const GlueOrder order = core_golden::glue_order(record.raw(1));
                total.add(record.i64(0), order);
                GlueTotal one;
                one.add(record.i64(0), order);
                summed += one;
            }
            const golden::Record& record = reader.take("total");
            for (int i = 0; i < 4; ++i)
                CHECK_EQ(total.amounts[i], record.i64(static_cast<size_t>(i)));
            CHECK(total.dominant_order() == core_golden::glue_order(record.raw(4)));
            CHECK_EQ(total.dominant(), record.i64(5));
            CHECK(summed == total);
            ++totals;
        } else if (c.kind == "predicates") {
            while (!reader.done()) {
                const Node node = core_golden::read_node(reader, context);
                const golden::Record& record = reader.take();
                CHECK_CONTEXT("%s:%d", record.file, record.line);
                if (record.is("pred")) {
                    CHECK_EQ(is_discardable(node), record.boolean(0));
                    CHECK_EQ(permits_break_at_following_glue(node), record.boolean(1));
                } else if (record.is("penalty_pred")) {
                    REQUIRE(node.is(NodeType::PENALTY));
                    CHECK_EQ(forbids_break(node.penalty()), record.boolean(0));
                    CHECK_EQ(forces_break(node.penalty()), record.boolean(1));
                } else if (record.is("rule_pred")) {
                    REQUIRE(node.is(NodeType::RULE));
                    CHECK_EQ(is_running(node), record.boolean(0));
                } else {
                    FAIL("nodes.golden holds a verdict this test does not know");
                }
                ++verdicts;
            }
        } else {
            FAIL("nodes.golden holds a case kind this test does not know: %s", c.kind.c_str());
        }
    }
    CHECK(totals >= 30);
    CHECK(verdicts >= 40);
}

// --- core_golden.h reads and writes every core golden -----------------------------

TEST(every_core_golden_reads_and_writes_back_identically)
{
    const std::vector<std::string> files = golden::list("core");
    REQUIRE(!files.empty());
    Seen seen;
    for (const std::string& path : files) {
        CHECK_CONTEXT("%s", path.c_str());
        golden::File file;
        if (!file.load(path))
            continue;
        ++seen.files;
        for (const core_golden::Case& c : core_golden::cases(file)) {
            CHECK_CONTEXT("case %s (line %d)", c.name.c_str(), c.line);
            if (!walk_case(file, c, seen))
                FAIL("a case kind FORMAT.md does not list: %s", c.kind.c_str());
            ++seen.cases;
        }
    }
    // Floors a little under what the goldens hold, so that a file that lost
    // its cases, or a walk that skipped them, does not read as a pass.
    CHECK_EQ(seen.files, 7);
    CHECK(seen.cases >= 590);
    CHECK(seen.top_level_nodes > 14000);
    CHECK(seen.boxes > 10000);
    CHECK(seen.discs > 500);
    CHECK(seen.replaced >= 12);
    CHECK(seen.deepest >= 5);
    CHECK(seen.command_lists > 600);
    CHECK(seen.commands > 38000);
    CHECK(seen.packed > 3000);
    CHECK(seen.runs > 480);
    CHECK(seen.measures > 2400);
}

TEST(hex_float_is_pythons_float_hex)
{
    CHECK_EQ(core_golden::hex_float(0.0), std::string("0x0.0p+0"));
    CHECK_EQ(core_golden::hex_float(-0.0), std::string("-0x0.0p+0"));
    CHECK_EQ(core_golden::hex_float(1.0), std::string("0x1.0000000000000p+0"));
    CHECK_EQ(core_golden::hex_float(3.0), std::string("0x1.8000000000000p+1"));
    CHECK_EQ(core_golden::hex_float(-0.1), std::string("-0x1.999999999999ap-4"));
    CHECK_EQ(core_golden::hex_float(5e-324), std::string("0x0.0000000000001p-1022"));
    CHECK_EQ(core_golden::hex_float(std::numeric_limits<double>::max()),
             std::string("0x1.fffffffffffffp+1023"));
    CHECK_EQ(core_golden::hex_float(std::numeric_limits<double>::infinity()), std::string("inf"));
    CHECK_EQ(core_golden::hex_float(-std::numeric_limits<double>::infinity()), std::string("-inf"));
    CHECK_EQ(core_golden::hex_float(std::numeric_limits<double>::quiet_NaN()), std::string("nan"));

    // Every float token of every core golden reads and writes back as itself.
    int floats = 0;
    for (const std::string& path : golden::list("core")) {
        golden::File file;
        if (!file.load(path))
            continue;
        for (const golden::Record& record : file) {
            for (size_t i = 0; i < record.size(); ++i) {
                const std::string_view field = record.fields[i];
                if (field.substr(0, 2) != "0x" && field.substr(0, 3) != "-0x")
                    continue;
                if (!CHECK_EQ(core_golden::hex_float(record.f64(i)), std::string(field)))
                    return;
                ++floats;
            }
        }
    }
    CHECK(floats > 10000);
}

TEST(a_face_table_gives_the_references_faces_their_enum_order)
{
    core_golden::FaceTable faces;
    const FontKey prose = faces.key("prose@789381");
    CHECK_EQ(prose.face(), 0u);
    CHECK_EQ(prose.size(), Scaled(789381));
    CHECK_EQ(faces.key("prose-bold-italic@1").face(), 3u);
    CHECK_EQ(faces.key("math@552567").face(), 6u);
    const FontKey fake = faces.key("fake@10");
    CHECK_EQ(fake.face(), 7u);
    CHECK_EQ(faces.key("big@20").face(), 8u);
    CHECK(faces.key("fake@10") == fake);
    CHECK_EQ(faces.text(fake), std::string("fake@10"));
    CHECK_EQ(faces.text(prose), std::string("prose@789381"));
    // The reference splits at the last '@'.
    const FontKey odd = faces.key("a@b@12");
    CHECK_EQ(faces.text(odd), std::string("a@b@12"));
}

// --- The node value ---------------------------------------------------------------

TEST(a_node_is_a_trivially_copyable_32_byte_value)
{
    CHECK_EQ(sizeof(Node), size_t(32));
    CHECK(std::is_trivially_copyable<Node>::value);
    CHECK(std::is_trivially_copyable<Box>::value);
    CHECK(std::is_trivially_copyable<Disc>::value);
    CHECK(std::is_trivially_copyable<GlueSpec>::value);
    CHECK(std::is_trivially_copyable<FontKey>::value);
    CHECK_EQ(sizeof(FontKey), size_t(8));
}

TEST(a_font_key_holds_a_face_and_a_size_and_compares_as_the_string_would)
{
    const FontKey a = FontKey::make(3, 789381);
    CHECK_EQ(a.face(), 3u);
    CHECK_EQ(a.size(), Scaled(789381));
    CHECK(a == FontKey::make(3, 789381));
    CHECK(a != FontKey::make(3, 789382));
    CHECK(a != FontKey::make(4, 789381));
    const FontKey edge = FontKey::make(FontKey::MAX_FACE, FontKey::MAX_SIZE);
    CHECK_EQ(edge.face(), FontKey::MAX_FACE);
    CHECK_EQ(edge.size(), FontKey::MAX_SIZE);
    // Bit 63 stays clear, so the key survives the trip through a node's Scaled.
    CHECK(static_cast<int64_t>(edge.bits) > 0);
    CHECK(char_node(edge, 'x').font() == edge);
    CHECK(FontKey::make(0, 0) == FontKey{});
    CHECK(MAX_DIMEN < FontKey::MAX_SIZE);
}

TEST(a_font_key_out_of_range_is_a_contract_violation_and_is_clamped)
{
    const ViolationScope scope;
    CHECK_EQ(FontKey::make(FontKey::MAX_FACE + 1, 5).face(), FontKey::MAX_FACE);
    CHECK_EQ(FontKey::make(1, -5).size(), Scaled(0));
    CHECK_EQ(FontKey::make(1, FontKey::MAX_SIZE + 1).size(), FontKey::MAX_SIZE);
    CHECK_EQ(g_violations, 3);
}

TEST(each_constructor_makes_its_kind_and_nothing_else)
{
    const Node c = char_node(FONT, 0x1F600);
    CHECK(c.is(NodeType::CHAR));
    CHECK(c.font() == FONT);
    CHECK_EQ(c.codepoint(), 0x1F600u);
    CHECK_EQ(c.original(), 0x1F600u);
    CHECK(!c.is_replaced());

    const Node r = replaced_char(FONT, 0xFFFD, 0x6F22);
    CHECK(r.is(NodeType::CHAR));
    CHECK_EQ(r.codepoint(), 0xFFFDu);
    CHECK_EQ(r.original(), 0x6F22u);
    CHECK(r.is_replaced());
    // As the reference's ReplacedChar is not equal to its CharNode, even when
    // the two codepoints agree.
    CHECK(r != char_node(FONT, 0xFFFD));
    CHECK(replaced_char(FONT, 'a', 'a') != char_node(FONT, 'a'));

    const GlueSpec spec{6 * PT, 4 * PT, GlueOrder::FIL, 2 * PT, GlueOrder::FILLL};
    const Node g = glue(spec);
    CHECK(g.is(NodeType::GLUE));
    CHECK(g.spec() == spec);
    CHECK_EQ(g.width(), 6 * PT);
    CHECK_EQ(g.stretch(), 4 * PT);
    CHECK_EQ(g.shrink(), 2 * PT);
    CHECK(g.stretch_order == GlueOrder::FIL);
    CHECK(g.shrink_order == GlueOrder::FILLL);
    // A spec is a value: two glue nodes of the same spec are the same node.
    CHECK(glue(spec) == g);
    CHECK(glue(GlueSpec{}) != g);
    CHECK((GlueSpec{} == GlueSpec{0, 0, GlueOrder::NORMAL, 0, GlueOrder::NORMAL}));
    CHECK((GlueSpec{0, 1} != GlueSpec{}));
    CHECK((GlueSpec{0, 0, GlueOrder::FIL} != GlueSpec{}));

    const Node k = kern(-3, KernKind::EXPLICIT);
    CHECK(k.is(NodeType::KERN));
    CHECK_EQ(k.width(), Scaled(-3));
    CHECK(k.kern_kind() == KernKind::EXPLICIT);
    CHECK(kern().kern_kind() == KernKind::NORMAL);
    CHECK(kern(-3, KernKind::NORMAL) != k);

    CHECK_EQ(penalty(-150).penalty(), Scaled(-150));
    CHECK(penalty().is(NodeType::PENALTY));

    const Node plain = rule();
    CHECK(plain.is(NodeType::RULE));
    CHECK_EQ(plain.width(), RUNNING);
    CHECK_EQ(plain.height(), RUNNING);
    CHECK_EQ(plain.depth(), RUNNING);
    CHECK(is_running(plain));
    CHECK(!is_running(rule(PT, 2 * PT, 3 * PT)));
    CHECK_EQ(rule(PT, 2 * PT, 3 * PT).height(), 2 * PT);
    CHECK_EQ(rule(PT, 2 * PT, 3 * PT).depth(), 3 * PT);

    CHECK(math_on(PT).is(NodeType::MATH_ON));
    CHECK(math_off(PT).is(NodeType::MATH_OFF));
    CHECK_EQ(math_on(PT).width(), PT);
    CHECK(math_on(PT) != math_off(PT));

    // Kinds never compare equal across types, whatever their fields.
    CHECK(kern(0) != penalty(0));
    CHECK(math_on(0) != kern(0));
    CHECK(Node{} == penalty(0));
}

TEST(a_new_box_is_unset_with_natural_glue)
{
    const Box box;
    CHECK(box.kind == BoxKind::HLIST);
    CHECK_EQ(box.width, Scaled(0));
    CHECK_EQ(box.shift_amount, Scaled(0));
    CHECK_EQ(box.glue_set, 0.0);
    CHECK(box.glue_sign == GlueSign::NORMAL);
    CHECK(box.glue_order == GlueOrder::NORMAL);
    CHECK_EQ(box.contents.count, 0u);
}

TEST(the_type_order_is_the_discardable_partition_of_section_148)
{
    NodeArena arena;
    const Node box = arena.add_box(Box{});
    const Node disc = arena.add_disc({}, {});
    for (const Node& keep : {char_node(FONT, 'a'), box, rule(), disc})
        CHECK(!is_discardable(keep));
    for (const Node& drop : {math_on(), math_off(), glue(GlueSpec{}), kern(), penalty()})
        CHECK(is_discardable(drop));
    // A font kern is discardable and still permits the break after it.
    CHECK(permits_break_at_following_glue(kern(PT, KernKind::NORMAL)));
    CHECK(permits_break_at_following_glue(kern(PT, KernKind::ACCENT)));
    CHECK(!permits_break_at_following_glue(kern(PT, KernKind::EXPLICIT)));
    CHECK(!permits_break_at_following_glue(glue(GlueSpec{})));
    CHECK(!permits_break_at_following_glue(penalty()));
    CHECK(!permits_break_at_following_glue(math_off()));
}

TEST(penalty_sentinels_share_inf_bads_magnitude)
{
    CHECK_EQ(INF_PENALTY, Scaled(INF_BAD));
    CHECK_EQ(EJECT_PENALTY, -INF_PENALTY);
    CHECK(forbids_break(INF_PENALTY));
    CHECK(!forbids_break(INF_PENALTY - 1));
    CHECK(forces_break(EJECT_PENALTY));
    CHECK(!forces_break(EJECT_PENALTY + 1));
    CHECK(!forbids_break(0) && !forces_break(0));
}

TEST(glue_totals_keep_the_orders_apart)
{
    // Section 646's example: 3pt, 8fill, 5fil, 6pt, -3fil, -8fill is 2fil.
    GlueTotal total;
    total.add(3 * PT, GlueOrder::NORMAL);
    total.add(8 * PT, GlueOrder::FILL);
    total.add(5 * PT, GlueOrder::FIL);
    total.add(6 * PT, GlueOrder::NORMAL);
    total.add(-3 * PT, GlueOrder::FIL);
    total.add(-8 * PT, GlueOrder::FILL);
    CHECK(total.dominant_order() == GlueOrder::FIL);
    CHECK_EQ(total.dominant(), 2 * PT);
    CHECK_EQ(total[GlueOrder::NORMAL], 9 * PT);
    CHECK_EQ(total[GlueOrder::FILL], Scaled(0));

    const GlueTotal empty;
    CHECK(empty.dominant_order() == GlueOrder::NORMAL);
    CHECK_EQ(empty.dominant(), Scaled(0));
    GlueTotal sum = total;
    sum += empty;
    CHECK(sum == total);
}

// --- The arena --------------------------------------------------------------------

TEST(an_arena_keeps_a_boxs_contents_contiguous_and_in_order)
{
    NodeArena arena;
    std::vector<Node> inner = {char_node(FONT, 'a'), kern(PT), char_node(FONT, 'b')};
    Box header;
    header.width = 30 * PT;
    header.height = 7 * PT;
    header.glue_set = 0.5;
    header.glue_sign = GlueSign::STRETCHING;
    const Node inner_box = arena.add_box(header, inner);
    REQUIRE(inner_box.is(NodeType::BOX));
    CHECK_EQ(arena.box(inner_box).width, 30 * PT);
    CHECK_EQ(arena.box(inner_box).glue_set, 0.5);
    const NodeView contents = arena.contents(inner_box);
    REQUIRE_EQ(contents.size(), size_t(3));
    CHECK(contents[0] == inner[0]);
    CHECK(contents[2] == inner[2]);
    CHECK_EQ(arena.nesting(inner_box), 1);

    // The scratch list is the caller's and can be reused at once.
    inner.clear();
    inner.push_back(glue(GlueSpec{PT}));
    inner.push_back(inner_box);
    Box outer;
    outer.kind = BoxKind::VLIST;
    const Node outer_box = arena.add_box(outer, inner);
    CHECK_EQ(arena.nesting(outer_box), 2);
    REQUIRE_EQ(arena.contents(outer_box).size(), size_t(2));
    CHECK(arena.contents(outer_box)[1] == inner_box);
    // The inner box is unchanged by what was added after it.
    CHECK_EQ(arena.contents(inner_box).size(), size_t(3));
    CHECK_EQ(arena.box_count(), size_t(2));
    CHECK_EQ(arena.node_count(), size_t(5));
    CHECK_EQ(arena.nesting(kern()), 0);
}

TEST(the_same_box_shifted_shares_its_contents)
{
    NodeArena arena;
    const std::vector<Node> inner = {char_node(FONT, 'a')};
    const Node original = arena.add_box(Box{}, inner);
    Box shifted = arena.box(original);
    shifted.shift_amount = 3 * PT;
    const Node moved = arena.add_box(shifted);
    CHECK(moved != original);
    CHECK(arena.box(moved).contents == arena.box(original).contents);
    CHECK_EQ(arena.box(original).shift_amount, Scaled(0));
    CHECK_EQ(arena.node_count(), size_t(1));
    CHECK(!deep_equal(arena, original, arena, moved));
}

TEST(a_discretionary_keeps_both_lists_and_its_count)
{
    NodeArena arena;
    const std::vector<Node> pre = {char_node(FONT, '-')};
    const std::vector<Node> post = {char_node(FONT, 'k'), kern(PT)};
    const Node node = arena.add_disc(pre, post, 1);
    REQUIRE(node.is(NodeType::DISC));
    const Disc& disc = arena.disc(node);
    CHECK_EQ(disc.replace_count, 1);
    CHECK_EQ(arena.view(disc.pre_break).size(), size_t(1));
    CHECK_EQ(arena.view(disc.post_break).size(), size_t(2));
    CHECK(arena.view(disc.post_break)[1] == kern(PT));
    CHECK_EQ(arena.nesting(node), 1);
    const Node empty = arena.add_disc({}, {});
    CHECK(arena.view(arena.disc(empty).pre_break).empty());
    CHECK_EQ(arena.disc(empty).replace_count, 0);
}

TEST(adding_a_view_of_the_arenas_own_nodes_survives_the_growth_it_causes)
{
    NodeArena arena;
    std::vector<Node> seed;
    for (uint32_t i = 0; i < 5; ++i)
        seed.push_back(char_node(FONT, 'a' + i));
    NodeSpan span = arena.add_nodes(seed);
    // Doubling repeatedly forces reallocation while the source is the arena.
    for (int round = 0; round < 12; ++round)
        span = arena.add_nodes(arena.view(NodeSpan{0, static_cast<uint32_t>(arena.node_count())}));
    CHECK_EQ(arena.node_count(), size_t(5) << 12);
    const NodeView all = arena.view(NodeSpan{0, static_cast<uint32_t>(arena.node_count())});
    bool intact = true;
    for (size_t i = 0; i < all.size(); ++i)
        intact = intact && all[i] == seed[i % 5];
    CHECK(intact);
    CHECK_EQ(span.first + span.count, static_cast<uint32_t>(arena.node_count()));
}

TEST(rewinding_drops_what_was_added_after_the_mark_and_clear_drops_all)
{
    NodeArena arena;
    const std::vector<Node> inner = {char_node(FONT, 'a')};
    const Node kept = arena.add_box(Box{}, inner);
    const NodeArena::Mark mark = arena.mark();
    arena.add_box(Box{}, inner);
    arena.add_disc(inner, inner, 0);
    CHECK_EQ(arena.box_count(), size_t(2));
    CHECK_EQ(arena.disc_count(), size_t(1));
    arena.rewind(mark);
    CHECK_EQ(arena.box_count(), size_t(1));
    CHECK_EQ(arena.disc_count(), size_t(0));
    CHECK_EQ(arena.node_count(), size_t(1));
    CHECK_EQ(arena.contents(kept).size(), size_t(1));
    // What is added next takes the indices the dropped items had.
    const Node again = arena.add_box(Box{}, inner);
    CHECK_EQ(again.index(), 1u);
    CHECK_EQ(arena.nesting(again), 1);
    arena.clear();
    CHECK_EQ(arena.node_count() + arena.box_count() + arena.disc_count(), size_t(0));
}

TEST(a_view_clips_its_sub_ranges)
{
    const std::vector<Node> nodes = {penalty(1), penalty(2), penalty(3), penalty(4)};
    const NodeView view(nodes);
    CHECK_EQ(view.size(), size_t(4));
    CHECK_EQ(view.sub(1, 2).size(), size_t(2));
    CHECK(view.sub(1, 2)[0] == penalty(2));
    CHECK_EQ(view.sub(3, 10).size(), size_t(1));
    CHECK(view.sub(9, 1).empty());
    CHECK(NodeView().empty());
    int total = 0;
    for (const Node& node : view.sub(0, 3))
        total += static_cast<int>(node.penalty());
    CHECK_EQ(total, 6);
}

TEST(deep_equal_is_the_references_equality)
{
    NodeArena a;
    NodeArena b;
    // Pad one arena so that equal trees have different indices in the two.
    b.add_box(Box{});
    b.add_disc({}, {});
    const std::vector<Node> leaves = {char_node(FONT, 'x'), glue(GlueSpec{PT, 1})};

    auto build = [&leaves](NodeArena& arena, double glue_set, int32_t replace_count) {
        Box inner;
        inner.width = PT;
        inner.glue_set = glue_set;
        std::vector<Node> outer = leaves;
        outer.push_back(arena.add_box(inner, leaves));
        outer.push_back(arena.add_disc(leaves, {}, replace_count));
        return arena.add_box(Box{}, outer);
    };
    const Node in_a = build(a, 0.25, 1);
    const Node in_b = build(b, 0.25, 1);
    CHECK(in_a != in_b); // different indices: `==` on nodes is not tree equality
    CHECK(deep_equal(a, in_a, b, in_b));
    CHECK(!deep_equal(a, in_a, b, build(b, 0.5, 1)));
    CHECK(!deep_equal(a, in_a, b, build(b, 0.25, 2)));
    CHECK(!deep_equal(a, in_a, b, kern()));
    CHECK(deep_equal(a, kern(PT), b, kern(PT)));
    CHECK(!deep_equal(a, NodeView(leaves), b, NodeView(leaves).sub(0, 1)));
    // A zero ratio of either sign is the same ratio, as it is to Python.
    CHECK(deep_equal(a, build(a, 0.0, 0), b, build(b, -0.0, 0)));
}

TEST(importing_within_one_arena_copies_the_tree)
{
    NodeArena arena;
    const std::vector<Node> leaves = {char_node(FONT, 'x'), kern(PT)};
    std::vector<Node> outer = leaves;
    outer.push_back(arena.add_box(Box{}, leaves));
    outer.push_back(arena.add_disc(leaves, leaves, 2));
    const Node tree = arena.add_box(Box{}, outer);
    const size_t boxes_before = arena.box_count();
    const Node copy = arena.import(arena, tree);
    CHECK(copy != tree);
    CHECK(deep_equal(arena, tree, arena, copy));
    CHECK_EQ(arena.box_count(), boxes_before + 2);
    CHECK_EQ(arena.nesting(copy), arena.nesting(tree));
    // Values are imported as themselves.
    CHECK(arena.import(arena, kern(PT)) == kern(PT));
}

TEST(nesting_is_refused_past_the_limit_and_never_cycles)
{
    const ViolationScope scope;
    NodeArena arena;
    std::vector<Node> one = {char_node(FONT, 'a')};
    Node box = arena.add_box(Box{}, one);
    for (int depth = 2; depth <= NodeArena::MAX_NESTING; ++depth) {
        one[0] = box;
        box = arena.add_box(Box{}, one);
    }
    CHECK_EQ(arena.nesting(box), NodeArena::MAX_NESTING);
    CHECK_EQ(g_violations, 0);

    // One deeper is made with no contents, as a box and as a discretionary.
    one[0] = box;
    const Node too_deep = arena.add_box(Box{}, one);
    CHECK_EQ(g_violations, 1);
    CHECK(arena.contents(too_deep).empty());
    CHECK_EQ(arena.nesting(too_deep), 1);
    const Node deep_disc = arena.add_disc(one, {});
    CHECK_EQ(g_violations, 2);
    CHECK(arena.view(arena.disc(deep_disc).pre_break).empty());

    // The deepest legal tree still walks: equality and import recurse through it.
    NodeArena other;
    const Node imported = other.import(arena, box);
    CHECK(deep_equal(arena, box, other, imported));
    CHECK_EQ(other.nesting(imported), NodeArena::MAX_NESTING);
    CHECK_EQ(g_violations, 2);

    // And never cycles: a box cannot name itself, the only way a tree whose
    // boxes name earlier ones could close a loop. The node for the box about
    // to be made is refused with the rest of its contents.
    Node self;
    self.type = NodeType::BOX;
    self.u = static_cast<uint32_t>(arena.box_count());
    one[0] = self;
    const Node cyclic = arena.add_box(Box{}, one);
    CHECK_EQ(cyclic.u, self.u);
    CHECK_EQ(g_violations, 3);
    CHECK(arena.contents(cyclic).empty());
    CHECK_EQ(arena.nesting(cyclic), 1);
}

TEST(what_an_arena_does_not_hold_is_a_contract_violation_with_an_empty_answer)
{
    const ViolationScope scope;
    NodeArena arena;
    const Node box = arena.add_box(Box{});

    CHECK_EQ(arena.box(kern()).width, Scaled(0));
    CHECK_EQ(g_violations, 1);
    Node stranger = box;
    stranger.u = 99;
    CHECK(arena.contents(stranger).empty());
    CHECK_EQ(g_violations, 2);
    CHECK_EQ(arena.disc(box).replace_count, 0);
    CHECK_EQ(g_violations, 3);
    CHECK(arena.view(NodeSpan{0, 1}).empty());
    CHECK_EQ(g_violations, 4);
    CHECK(arena.view(NodeSpan{5, 0}).empty());
    CHECK_EQ(g_violations, 5);

    // A box naming contents the arena does not have is stored empty.
    Box wild;
    wild.contents = NodeSpan{3, 100};
    const Node stored = arena.add_box(wild);
    CHECK_EQ(g_violations, 6);
    CHECK(arena.contents(stored).empty());

    NodeArena::Mark ahead = arena.mark();
    ahead.boxes += 5;
    arena.rewind(ahead);
    CHECK_EQ(g_violations, 7);
    CHECK_EQ(arena.box_count(), size_t(2));

    // An empty span anywhere inside or at the end of the arena is fine.
    CHECK(arena.view(NodeSpan{0, 0}).empty());
    CHECK_EQ(g_violations, 7);
}
