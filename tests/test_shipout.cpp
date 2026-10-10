// src/shipout against the reference: every case of
// tests/golden/core/shipout.golden; the lines of every paragraph the reference
// broke, assembled and shipped (linebreak.golden, paragraphs.golden, and the
// paragraphs with formulas in math/inline.golden); and every formula box of
// math/mlist.golden and math/formulas.golden. Then what is this port's own
// shape and in no golden: the command as a value, the buffer, the two entry
// points, the error contract and the bound on the walk.

#include "core_golden.h"

#include "pack.h"
#include "shipout.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
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
    long calls = 0;
    long misses = 0;

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

const char* slug(ShipError error)
{
    switch (error) {
    case ShipError::NONE:
        return "none";
    case ShipError::CHAR_WITHOUT_MEASURE:
        return "char-without-measure";
    case ShipError::RUNNING_WIDTH_IN_HLIST:
        return "running-width-in-hlist";
    case ShipError::CHAR_IN_VLIST:
        return "char-in-vlist";
    case ShipError::RUNNING_HEIGHT_OR_DEPTH_IN_VLIST:
        return "running-height-or-depth-in-vlist";
    case ShipError::TOO_MANY_ITEMS:
        return "too-many-items";
    case ShipError::NODE_NOT_IN_ARENA:
        return "node-not-in-arena";
    case ShipError::ROOT_NOT_A_BOX:
        return "root-not-a-box";
    case ShipError::TOO_DEEP:
        return "too-deep";
    case ShipError::DIMENSION_OVERFLOW:
        return "dimension-overflow";
    }
    return "?";
}

// A command as the golden reader holds one, so the port's output can be
// written as the oracle writes it.
core_golden::Cmd as_golden(const DrawCmd& cmd)
{
    core_golden::Cmd out;
    out.x = cmd.x;
    out.y = cmd.y;
    switch (cmd.type) {
    case CmdType::GLYPH:
        out.kind = core_golden::Cmd::GLYPH;
        out.font = cmd.font();
        out.codepoint = cmd.codepoint;
        out.index = cmd.index;
        break;
    case CmdType::RULE_RECT:
        out.kind = core_golden::Cmd::RULERECT;
        out.width = cmd.width;
        out.height = cmd.height;
        break;
    case CmdType::SET_GLUE:
    case CmdType::ATOM:
        out.kind = cmd.type == CmdType::ATOM ? core_golden::Cmd::ATOM : core_golden::Cmd::SETGLUE;
        out.width = cmd.width;
        out.natural = cmd.natural();
        out.height = cmd.height;
        out.depth = cmd.depth;
        out.index = cmd.index;
        break;
    case CmdType::LINE:
        out.kind = core_golden::Cmd::LINE;
        out.width = cmd.width;
        out.height = cmd.height;
        out.depth = cmd.depth;
        out.start = cmd.start();
        out.end = cmd.end;
        break;
    }
    return out;
}

std::vector<std::string> written(const core_golden::Context& context,
                                 const std::vector<DrawCmd>& cmds)
{
    std::vector<core_golden::Cmd> converted;
    converted.reserve(cmds.size());
    for (const DrawCmd& cmd : cmds)
        converted.push_back(as_golden(cmd));
    std::vector<std::string> out;
    core_golden::write_cmds(context, converted, out);
    return out;
}

Point read_origin(core_golden::Reader& reader)
{
    const golden::Record& record = reader.take("origin");
    return Point{record.i64(0), record.i64(1)};
}

// The `cmds` ... `endcmds` at the reader's position, as text, consuming them.
std::vector<std::string> take_cmds_text(core_golden::Reader& reader)
{
    const size_t first = reader.position();
    const uint32_t count = reader.take("cmds").u32(0);
    for (uint32_t i = 0; i < count && !reader.done(); ++i)
        reader.take();
    reader.take("endcmds");
    return reader.lines(first, reader.position());
}

struct Tally {
    long cases = 0;
    long commands = 0;
    long by_type[5] = {0, 0, 0, 0, 0};
    long errors = 0;
    long measured = 0;

    void count(const std::vector<DrawCmd>& cmds)
    {
        commands += static_cast<long>(cmds.size());
        for (const DrawCmd& cmd : cmds)
            ++by_type[static_cast<int>(cmd.type)];
    }
};

// Ships `box` (a BOX node) at the origin the reader holds next and compares
// with the `cmds` or `error` that follows.
void ship_and_compare(core_golden::Reader& reader, core_golden::Context& context, const Node& box,
                      TableMeasure& measure, Tally& tally)
{
    const Point origin = read_origin(reader);
    std::vector<DrawCmd> cmds;
    const ShipError error = ship(context.arena, box, origin, measure.measure(), cmds);
    if (reader.next_is("error")) {
        CHECK_EQ(std::string(slug(error)), std::string(reader.take().raw(0)));
        CHECK(cmds.empty());
        ++tally.errors;
    } else {
        CHECK_EQ(std::string(slug(error)), std::string("none"));
        CHECK_LINES_EQ(take_cmds_text(reader), written(context, cmds), "the shipped commands");
        tally.count(cmds);
    }
    ++tally.cases;
}

} // namespace

// --- The reference's own cases ------------------------------------------------------

TEST(ship_matches_the_reference)
{
    golden::File file;
    REQUIRE(file.load(golden::path("core/shipout.golden")));
    const std::vector<core_golden::Case> cases = core_golden::cases(file, "core/shipout");
    REQUIRE(!cases.empty());

    Tally tally;
    long rounded = 0;
    long atoms = 0;
    for (const core_golden::Case& c : cases) {
        CHECK_CONTEXT("case %s (line %d)", c.name.c_str(), c.line);
        core_golden::Context context;
        core_golden::Reader reader(file, c);
        if (c.kind == "table") {
            while (!reader.done()) {
                const golden::Record& record = reader.take("pascal_round");
                CHECK_CONTEXT("pascal_round %s", std::string(record.raw(0)).c_str());
                CHECK_EQ(pascal_round(record.f64(0)), record.i64(1));
                ++rounded;
            }
        } else if (c.kind == "atom") {
            const Node box = core_golden::read_node(reader, context);
            REQUIRE(box.is(NodeType::BOX));
            CHECK_EQ(is_atom(context.arena, context.arena.box(box)),
                     reader.take("is_atom").boolean(0));
            const size_t first = reader.position();
            core_golden::read_node(reader, context);
            const std::vector<std::string> expected = reader.lines(first, reader.position());
            const Node wrapped = atom_box(context.arena, box);
            CHECK_LINES_EQ(expected, core_golden::node_lines(context, wrapped), "atom_box");
            CHECK_EQ(is_atom(context.arena, context.arena.box(wrapped)),
                     reader.take("is_atom").boolean(0));
            ++atoms;
        } else if (c.kind == "ship") {
            const core_golden::MeasureTable table = core_golden::read_measure(reader, context);
            TableMeasure measure{&table};
            const Node box = core_golden::read_node(reader, context);
            REQUIRE(box.is(NodeType::BOX));
            ship_and_compare(reader, context, box, measure, tally);
            CHECK_EQ(measure.misses, 0);
            tally.measured += measure.calls;
        } else {
            FAIL("a case kind this test does not know: %s", c.kind.c_str());
        }
        CHECK(reader.done());
    }
    CHECK(rounded >= 100);
    CHECK(atoms >= 11);
    CHECK(tally.cases >= 126);
    // One case for each of the reference's four ValueErrors.
    CHECK_EQ(tally.errors, 4);
    // Every kind of command is in there to be compared.
    for (const long n : tally.by_type)
        CHECK(n > 10);
    CHECK(tally.measured > 100);
}

// Every paragraph the reference broke: its lines appended with interline glue
// from IGNORE_DEPTH, packed at their natural height, and shipped at
// (0, height), as FORMAT.md's ship section lays out. This is ship-out on real
// justified text, where the glue of each line has to add up to the scaled
// point.
TEST(every_assembled_paragraph_ships_as_the_reference_shipped_it)
{
    Tally tally;
    long files = 0;
    for (const char* name :
         {"core/linebreak.golden", "core/paragraphs.golden", "math/inline.golden"}) {
        CHECK_CONTEXT("%s", name);
        golden::File file;
        if (!CHECK(file.load(golden::path(name))))
            continue;
        ++files;
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
                std::vector<Node> lines;
                for (int64_t i = 0; i < paragraph.line_count && !reader.done(); ++i)
                    lines.push_back(core_golden::read_packed(reader, context).box);

                if (reader.next_is("spec")) {
                    const core_golden::Skip read = core_golden::read_skip(reader);
                    const InterlineSkip skip{read.baselineskip, read.lineskip, read.lineskiplimit};
                    std::vector<Node> vlist;
                    Scaled prev_depth = IGNORE_DEPTH;
                    for (const Node& line : lines)
                        prev_depth = append_to_vlist(context.arena, vlist, line, prev_depth, skip);
                    const PackResult page = vpack(context.arena, vlist);
                    const golden::Record& vbox = reader.take("vbox");
                    REQUIRE_EQ(page.box.height, vbox.i64(1));

                    // Shipped as packed: the PackResult's box and the list it
                    // was packed from, with nothing added to the arena.
                    const Point origin = read_origin(reader);
                    std::vector<DrawCmd> cmds;
                    const ShipError error =
                        ship(context.arena, page.box, vlist, origin, measure.measure(), cmds);
                    CHECK_EQ(std::string(slug(error)), std::string("none"));
                    CHECK_LINES_EQ(take_cmds_text(reader), written(context, cmds),
                                   "the shipped paragraph");
                    tally.count(cmds);
                    ++tally.cases;
                }
                reader.take("endbreak");
            }
            CHECK_EQ(measure.misses, 0);
            tally.measured += measure.calls;
        }
    }
    CHECK_EQ(files, 3);
    CHECK(tally.cases > 500);
    CHECK(tally.by_type[static_cast<int>(CmdType::GLYPH)] > 25000);
    CHECK(tally.by_type[static_cast<int>(CmdType::SET_GLUE)] > 10000);
    CHECK(tally.by_type[static_cast<int>(CmdType::LINE)] > 2800);
    CHECK(tally.by_type[static_cast<int>(CmdType::ATOM)] > 50);
    // Fraction bars and radical rules, inside the formulas of the paragraphs.
    CHECK(tally.by_type[static_cast<int>(CmdType::RULE_RECT)] >= 40);
}

// The formulas of the math goldens: each `mlist` case that set holds the box
// the reference packed, the origin and the commands it shipped. Nested boxes
// with shifts, rules for fraction bars and radicals, kerns, and glue inside
// boxes set at their natural width.
TEST(every_formula_box_ships_as_the_reference_shipped_it)
{
    Tally tally;
    long refused = 0;
    for (const char* name : {"math/mlist.golden", "math/formulas.golden"}) {
        CHECK_CONTEXT("%s", name);
        golden::File file;
        if (!CHECK(file.load(golden::path(name))))
            continue;
        for (const core_golden::Case& c : core_golden::cases(file)) {
            if (c.kind != "mlist")
                continue;
            CHECK_CONTEXT("case %s (line %d)", c.name.c_str(), c.line);
            core_golden::Context context;
            core_golden::Reader reader(file, c);
            // Everything before the result is the math list and what the fonts
            // were asked; of it, ship-out needs the `measure` records only.
            // A `subbox` field of the list is followed by a box record of its
            // own, which is skipped by reading it.
            core_golden::MeasureTable table;
            bool set = false;
            while (!reader.done()) {
                if (reader.next_is("measure")) {
                    table = core_golden::read_measure(reader, context);
                } else if (reader.next_is("subbox")) {
                    reader.take();
                    core_golden::read_node(reader, context);
                } else if (reader.next_is("nodes")) {
                    core_golden::read_nodes(reader, context);
                } else if (reader.next_is("packed")) {
                    set = true;
                    break;
                } else {
                    reader.take();
                }
            }
            if (!set) {
                ++refused;
                continue;
            }
            TableMeasure measure{&table};
            const core_golden::Packed packed = core_golden::read_packed(reader, context);
            ship_and_compare(reader, context, packed.box, measure, tally);
            CHECK_EQ(measure.misses, 0);
            tally.measured += measure.calls;
            CHECK(reader.done());
        }
    }
    CHECK(tally.cases > 4000);
    CHECK(refused > 100);
    CHECK_EQ(tally.errors, 0);
    CHECK(tally.by_type[static_cast<int>(CmdType::GLYPH)] > 15000);
    CHECK(tally.by_type[static_cast<int>(CmdType::RULE_RECT)] > 1000);
    CHECK(tally.by_type[static_cast<int>(CmdType::LINE)] > 4000);
}

// --- This port's shape ---------------------------------------------------------------

TEST(a_command_is_a_fixed_size_value_with_its_unused_fields_zero)
{
    CHECK_EQ(sizeof(DrawCmd), 64u);
    CHECK(std::is_trivially_copyable<DrawCmd>::value);
    CHECK(std::is_standard_layout<DrawCmd>::value);

    const FontKey font = FontKey::make(3, 12 * PT);
    const DrawCmd glyph = DrawCmd::glyph(font, 0x1D44E, -5, 7, 9);
    CHECK(glyph.is(CmdType::GLYPH));
    CHECK(glyph.font() == font);
    CHECK_EQ(glyph.codepoint, 0x1D44Eu);
    CHECK_EQ(glyph.x, -5);
    CHECK_EQ(glyph.y, 7);
    CHECK_EQ(glyph.index, 9u);
    CHECK_EQ(glyph.width, 0);
    CHECK_EQ(glyph.end, 0u);
    CHECK(!glyph.is_set_glue());

    const DrawCmd rule = DrawCmd::rule_rect(1, 2, 3, 4);
    CHECK(rule.is(CmdType::RULE_RECT));
    CHECK_EQ(rule.width, 3);
    CHECK_EQ(rule.height, 4);
    CHECK_EQ(rule.depth, 0);
    CHECK_EQ(rule.index, 0u);

    const DrawCmd space = DrawCmd::set_glue(1, 2, 30, 20, 7, 3, 5);
    const DrawCmd formula = DrawCmd::atom(1, 2, 30, 20, 7, 3, 5);
    CHECK(space.is(CmdType::SET_GLUE) && formula.is(CmdType::ATOM));
    CHECK_EQ(space.natural(), 20);
    CHECK_EQ(formula.natural(), 20);
    // The reference's isinstance(cmd, SetGlue) is true of an Atom too.
    CHECK(space.is_set_glue() && formula.is_set_glue());
    // The same seven fields, and still two different commands.
    CHECK(space != formula);
    DrawCmd retagged = formula;
    retagged.type = CmdType::SET_GLUE;
    CHECK(retagged == space);

    const DrawCmd line = DrawCmd::line(1, 2, 100, 7, 3, 4, 11);
    CHECK(line.is(CmdType::LINE));
    CHECK_EQ(line.start(), 4u);
    CHECK_EQ(line.end, 11u);

    // Bytes are the value: a buffer of commands can be copied as memory.
    DrawCmd copy;
    std::memcpy(&copy, &line, sizeof copy);
    CHECK(copy == line);

    const Rect rect{1, 2, 3, 4};
    const Rect taller{1, 2, 3, 5};
    CHECK(rect == rect);
    CHECK(rect != taller);
}

TEST(pascal_round_and_vet_glue_at_their_edges)
{
    CHECK_EQ(pascal_round(2.5), 3);
    CHECK_EQ(pascal_round(-2.5), -3);
    CHECK_EQ(pascal_round(0.5), 1);
    CHECK_EQ(pascal_round(-0.5), -1);
    CHECK_EQ(pascal_round(0.0), 0);
    CHECK_EQ(pascal_round(-0.0), 0);
    CHECK_EQ(pascal_round(0.4), 0);
    CHECK_EQ(pascal_round(-0.4), 0);
    // floor(v + 0.5) where v + 0.5 rounds up to 1.0 in double arithmetic: the
    // reference's formula, and so its answer.
    CHECK_EQ(pascal_round(0.49999999999999994), 1);
    CHECK_EQ(pascal_round(-0.49999999999999994), -1);
    // Where the reference raises: defined here, and stated in the header.
    CHECK_EQ(pascal_round(std::nan("")), 0);
    CHECK_EQ(pascal_round(std::numeric_limits<double>::infinity()), SCALED_LIMIT);
    CHECK_EQ(pascal_round(-std::numeric_limits<double>::infinity()), -SCALED_LIMIT);
    CHECK_EQ(pascal_round(1e300), SCALED_LIMIT);
    CHECK_EQ(pascal_round(-1e300), -SCALED_LIMIT);

    CHECK_EQ(vet_glue(12.5), 12.5);
    CHECK_EQ(vet_glue(BILLION), BILLION);
    CHECK_EQ(vet_glue(BILLION + 1.0), BILLION);
    CHECK_EQ(vet_glue(-BILLION - 1.0), -BILLION);
    CHECK_EQ(vet_glue(std::numeric_limits<double>::infinity()), BILLION);
    CHECK_EQ(vet_glue(-std::numeric_limits<double>::infinity()), -BILLION);
    // Python's max(-B, min(B, nan)) is +B: min keeps its first argument when
    // the comparison with NaN is false.
    CHECK_EQ(vet_glue(std::nan("")), BILLION);
}

namespace
{

CharDimens ten_points(void*, FontKey, uint32_t)
{
    return CharDimens{10 * PT, 7 * PT, 2 * PT};
}

// "ab cd" with a stretchable space, packed to `width`.
struct Words {
    NodeArena arena;
    std::vector<Node> nodes;
    PackResult packed;
    CharMeasure measure{&ten_points, nullptr};

    explicit Words(Scaled width)
    {
        const FontKey font = FontKey::make(0, 10 * PT);
        nodes = {char_node(font, 'a'), char_node(font, 'b'),
                 glue(GlueSpec{4 * PT, 6 * PT, GlueOrder::NORMAL, 2 * PT, GlueOrder::NORMAL}),
                 char_node(font, 'c'), char_node(font, 'd')};
        packed = hpack(arena, nodes, width, PackMode::EXACTLY, measure);
    }
};

} // namespace

TEST(the_two_entry_points_ship_the_same_commands)
{
    Words words(47 * PT);
    std::vector<DrawCmd> from_parts;
    CHECK(ship(words.arena, words.packed.box, words.nodes, Point{3 * PT, 20 * PT}, words.measure,
               from_parts) == ShipError::NONE);
    const size_t arena_nodes = words.arena.node_count();
    CHECK_EQ(arena_nodes, 0u); // shipping a packed list in place adds nothing

    const Node box = words.arena.add_box(words.packed.box, words.nodes);
    std::vector<DrawCmd> from_node;
    CHECK(ship(words.arena, box, Point{3 * PT, 20 * PT}, words.measure, from_node) ==
          ShipError::NONE);
    CHECK(from_parts == from_node);

    // LINE, a, b, the space, c, d.
    REQUIRE_EQ(from_parts.size(), 6u);
    CHECK(from_parts[0] == DrawCmd::line(3 * PT, 20 * PT, 47 * PT, 7 * PT, 2 * PT, 0, 5));
    CHECK(from_parts[3] == DrawCmd::set_glue(23 * PT, 20 * PT, 7 * PT, 4 * PT, 7 * PT, 2 * PT, 2));
    CHECK_EQ(from_parts[5].x, 40 * PT);
    CHECK_EQ(from_parts[5].index, 4u);
}

TEST(ship_appends_and_numbers_from_zero_each_time)
{
    Words words(44 * PT);
    std::vector<DrawCmd> out;
    out.push_back(DrawCmd::rule_rect(1, 2, 3, 4)); // something already there
    REQUIRE(ship(words.arena, words.packed.box, words.nodes, Point{0, 0}, words.measure, out) ==
            ShipError::NONE);
    REQUIRE_EQ(out.size(), 7u);
    CHECK(out[0] == DrawCmd::rule_rect(1, 2, 3, 4));
    CHECK(out[1].is(CmdType::LINE));
    CHECK_EQ(out[1].start(), 0u);
    CHECK_EQ(out[1].end, 5u);

    // A second paragraph into the same buffer: its LINE is patched in its own
    // slot, not the first one's, and its indices start again.
    REQUIRE(ship(words.arena, words.packed.box, words.nodes, Point{0, 30 * PT}, words.measure,
                 out) == ShipError::NONE);
    REQUIRE_EQ(out.size(), 13u);
    CHECK(out[1] == DrawCmd::line(0, 0, 44 * PT, 7 * PT, 2 * PT, 0, 5));
    CHECK(out[7] == DrawCmd::line(0, 30 * PT, 44 * PT, 7 * PT, 2 * PT, 0, 5));
    CHECK_EQ(out[8].index, 0u);
    CHECK_EQ(out[12].index, 4u);
}

TEST(an_error_leaves_the_buffer_as_it_was)
{
    Words words(44 * PT);
    std::vector<DrawCmd> out;
    out.push_back(DrawCmd::rule_rect(1, 2, 3, 4));
    const std::vector<DrawCmd> before = out;

    // No measure: the first character is already in the buffer when its
    // advance is asked for, and is taken back out.
    CHECK(ship(words.arena, words.packed.box, words.nodes, Point{0, 0}, CharMeasure(), out) ==
          ShipError::CHAR_WITHOUT_MEASURE);
    CHECK(out == before);

    // The error is deep inside: a line that ships, then a vbox holding a
    // character.
    const FontKey font = FontKey::make(0, 10 * PT);
    const Node good = words.arena.add_box(words.packed.box, words.nodes);
    const std::vector<Node> stray = {char_node(font, 'x')};
    Box bad_header;
    bad_header.kind = BoxKind::VLIST;
    const Node bad = words.arena.add_box(bad_header, stray);
    const std::vector<Node> page = {good, good, bad, good};
    const PackResult packed = vpack(words.arena, page);
    CHECK(ship(words.arena, packed.box, page, Point{0, 0}, words.measure, out) ==
          ShipError::CHAR_IN_VLIST);
    CHECK(out == before);

    // And the same buffer is usable afterwards.
    CHECK(ship(words.arena, good, Point{0, 0}, words.measure, out) == ShipError::NONE);
    CHECK_EQ(out.size(), before.size() + 6u);
}

// A box may be named by more than one node, and then it is shipped once for
// each. Twenty levels of a box holding its child twice is a million visits from
// forty nodes, which is what `max_items` is for.
TEST(the_walk_is_bounded_by_max_items)
{
    NodeArena arena;
    Box header;
    const std::vector<Node> leaf = {kern(PT), rule(PT, PT, 0)};
    Node box = arena.add_box(header, leaf);
    for (int level = 0; level < 20; ++level) {
        const std::vector<Node> twice = {box, box};
        box = arena.add_box(header, twice);
    }
    CHECK(arena.node_count() <= 42u);

    std::vector<DrawCmd> out;
    out.push_back(DrawCmd::rule_rect(1, 2, 3, 4));
    CHECK(ship(arena, box, Point{0, 0}, CharMeasure(), out, 1000) == ShipError::TOO_MANY_ITEMS);
    CHECK_EQ(out.size(), 1u);

    // Enough room, and it is all there: 2^20 leaves, each with one rule, under
    // one LINE; 2^21 - 2 box nodes and 2^21 leaf nodes visited.
    const uint32_t visits = (uint32_t(1) << 21) - 2 + (uint32_t(1) << 21);
    CHECK(ship(arena, box, Point{0, 0}, CharMeasure(), out, visits) == ShipError::NONE);
    CHECK_EQ(out.size(), 1u + 1u + (size_t(1) << 20));
    out.resize(1);
    CHECK(ship(arena, box, Point{0, 0}, CharMeasure(), out, visits - 1) ==
          ShipError::TOO_MANY_ITEMS);
    CHECK_EQ(out.size(), 1u);
}

TEST(atom_box_does_not_disturb_the_box_it_wraps)
{
    NodeArena arena;
    const FontKey font = FontKey::make(6, 10 * PT);
    const std::vector<Node> inside = {char_node(font, 'x'), kern(PT), char_node(font, 'y')};
    Box header;
    header.width = 21 * PT;
    header.height = 7 * PT;
    header.depth = 2 * PT;
    header.shift_amount = -3 * PT;
    const Node formula = arena.add_box(header, inside);
    CHECK(!is_atom(arena, arena.box(formula)));

    const Node wrapped = atom_box(arena, formula);
    CHECK(is_atom(arena, arena.box(wrapped)));
    const Box wrapper = arena.box(wrapped);
    CHECK_EQ(wrapper.width, 21 * PT);
    CHECK_EQ(wrapper.height, 10 * PT); // raised by 3pt, so 3pt taller
    CHECK_EQ(wrapper.depth, 2 * PT);
    CHECK_EQ(wrapper.shift_amount, 0);
    // The box inside is the very one it was given, as it was.
    const NodeView contents = arena.view(wrapper.contents);
    REQUIRE_EQ(contents.size(), 3u);
    CHECK(contents[1] == formula);
    CHECK_EQ(arena.box(formula).shift_amount, -3 * PT);

    // In a line: one ATOM, its two glyphs carrying its index, then the next
    // character with the next one.
    const std::vector<Node> line = {char_node(font, 'a'), wrapped, char_node(font, 'b')};
    const CharMeasure measure{&ten_points, nullptr};
    const PackResult packed = hpack(arena, line, 0, PackMode::ADDITIONAL, measure);
    std::vector<DrawCmd> cmds;
    REQUIRE(ship(arena, packed.box, line, Point{0, 0}, measure, cmds) == ShipError::NONE);
    REQUIRE_EQ(cmds.size(), 6u);
    CHECK(cmds[2] ==
          DrawCmd::atom(10 * PT, 0, 21 * PT, 21 * PT, packed.box.height, packed.box.depth, 1));
    CHECK_EQ(cmds[3].index, 1u);
    CHECK_EQ(cmds[4].index, 1u);
    CHECK_EQ(cmds[3].y, -3 * PT); // the formula's own shift, upward
    CHECK_EQ(cmds[5].index, 2u);
    CHECK_EQ(cmds[5].x, 31 * PT);
    CHECK_EQ(cmds[0].end, 3u);
}

// A root that is not a box, or names no box of the arena, is data and not the
// caller's mistake: a status, and no contract fires.
TEST(shipping_something_that_is_not_a_box_of_the_arena_is_an_error)
{
    NodeArena arena;
    std::vector<DrawCmd> out;
    const ContractHandler saved = contract_handler();
    contract_handler() = &count_violation;
    g_violations = 0;
    const ShipError not_a_box = ship(arena, kern(PT), Point{0, 0}, CharMeasure(), out);
    Node stray;
    stray.type = NodeType::BOX;
    stray.u = 7;
    const ShipError not_held = ship(arena, stray, Point{0, 0}, CharMeasure(), out);
    contract_handler() = saved;
    CHECK_EQ(g_violations, 0);
    CHECK(not_a_box == ShipError::ROOT_NOT_A_BOX);
    CHECK(not_held == ShipError::NODE_NOT_IN_ARENA);
    CHECK(out.empty());
}
