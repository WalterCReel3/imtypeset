// A Document cut into columns: src/columns against the reference and against
// src/vsplit.
//
// ON A GOLDEN: the `columns` cases of tests/golden/tables/columns.golden. Each
// case's blocks go into a Document under the reference's rules; its vertical
// list, made a unit at a time, is the golden's `vlist`; and every column of
// every split is where the reference cut it (start, glue, place, cost, why the
// scan stopped) and ships the reference's commands, exactly.
//
// ON A PROPERTY, for documents no golden holds: under the reference's rules a
// Document's columns are the columns split_into_columns cuts from the same
// blocks composed at once, shipped (random documents with quotes, lists, code
// and rules, random heights), however the columns were asked for and whatever
// was edited in between.
//
// ON A DECISION (docs/decisions.md D58), with no oracle: balanced columns, a
// break between two blocks, a heading kept with what follows it, and what is
// laid out when (the work counters at ten thousand blocks).

#include "blocks_golden.h"
#include "core_golden.h"
#include "golden.h"

#include "blocks.h"
#include "columns.h"
#include "shipout.h"
#include "vsplit.h"

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace ImTypeset::detail;

namespace
{

constexpr Scaled PT = UNITY;

// --- The golden -----------------------------------------------------------------------------

// tests/test_blocks.cpp's.
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

const char* const STOP_TOKENS[] = {"end", "eject", "overfull"};

// A node of a Document's list as the golden's `v` record, less what the list
// does not carry: a box's width and shift.
std::string dims_of(const Node& node)
{
    switch (node.type) {
    case NodeType::RULE:
        return check::format("box %" PRId64 " %" PRId64, node.height(), node.depth());
    case NodeType::GLUE:
        return "glue " + core_golden::spec_fields(node.spec());
    case NodeType::KERN:
        return check::format("kern %" PRId64 " %s", node.width(),
                             core_golden::token(node.kern_kind()));
    default:
        return "<unexpected>";
    }
}

// The same of a `v` record.
std::string dims_of(const golden::Record& record)
{
    golden::Cursor cursor(record);
    const std::string kind(cursor.raw());
    if (kind == "box") {
        cursor.raw();
        cursor.i64();
        const Scaled height = cursor.i64();
        const Scaled depth = cursor.i64();
        return check::format("box %" PRId64 " %" PRId64, height, depth);
    }
    std::string out = kind;
    while (!cursor.done())
        out += " " + std::string(cursor.raw());
    return out;
}

struct Tally {
    int cases = 0;
    int splits = 0;
    int columns = 0;
    int overfull = 0;
    int empty = 0;
    int cut_blocks = 0;
    size_t cmds = 0;
};

void replay_case(const golden::File& file, const core_golden::Case& c, Tally& tally)
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

    const std::vector<blocks_golden::BlockIn> inputs = blocks_golden::read_blocks(reader, context);
    Document document;
    document.set_theme(theme.theme);
    for (size_t i = 0; i < inputs.size(); ++i) {
        CHECK_CONTEXT("block %zu (line %d)", i, inputs[i].line);
        REQUIRE_EQ(document.add_block(), i);
        if (!build_block(inputs[i], theme, resolve, document.edit(i)))
            return;
    }
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
    composer.measure = table_measure.measure();
    document.set_width(width);

    // The list, a unit at a time, is the reference's.
    std::vector<std::string> expected_list;
    {
        const uint32_t count = reader.take("vlist").u32(0);
        for (uint32_t i = 0; i < count && !reader.done(); ++i)
            expected_list.push_back(dims_of(reader.take("v")));
        reader.take("endvlist");
    }
    {
        document.layout_all(composer);
        std::vector<Node> list;
        for (size_t u = 0; u < document.unit_count(); ++u)
            document.unit_vlist(u, list);
        std::vector<std::string> actual;
        for (const Node& node : list)
            actual.push_back(dims_of(node));
        CHECK_LINES_EQ(expected_list, actual, "the document's vertical list");
    }

    DocumentColumns columns;
    std::vector<DrawCmd> cmds;
    while (reader.next_is("split")) {
        const size_t first = reader.position();
        const size_t end = blocks_golden::group_end(file, first, reader.end(), "split", "endsplit");
        golden::Cursor open(reader.take("split"));
        const std::string name = open.str();
        ColumnRules rules;
        rules.height = open.i64();
        const size_t count = static_cast<size_t>(open.u32());
        rules.split_max_depth = open.i64();
        rules.split_top_skip = core_golden::read_spec(reader, "split_top_skip");
        CHECK_CONTEXT("split %s (line %d)", name.c_str(), file.records()[first].line);
        columns.set_rules(rules);

        for (size_t k = 0; k < count; ++k) {
            CHECK_CONTEXT("column %zu", k);
            golden::Cursor header(reader.take("column"));
            REQUIRE_EQ(static_cast<size_t>(header.u32()), k);
            const bool head = header.boolean();
            const uint64_t start = header.u32();
            const size_t place = header.u32();
            Scaled glue_width = 0;
            if (head)
                glue_width = reader.take("glue").i64(0);
            const uint32_t candidates = reader.take("scan").u32(0);
            for (uint32_t i = 0; i < candidates; ++i)
                reader.take("cand");
            golden::Cursor placed(reader.take("place"));
            placed.u32();
            const int64_t least_cost = placed.i64();
            const std::string stop(placed.raw());
            golden::Cursor packed(reader.take("packed"));
            const std::string fitness(packed.raw());
            const Scaled natural = packed.i64();
            golden::Cursor colbox(reader.take("colbox"));
            colbox.i64();
            colbox.i64();
            const Scaled depth = colbox.i64();
            reader.take("origin");
            const size_t cmds_first = reader.position();
            core_golden::read_cmds(reader, context);
            const std::vector<std::string> expected_cmds =
                reader.lines(cmds_first, reader.position());
            reader.take("endcolumn");

            // The reference always answers `count` columns; a document's are
            // the ones something is left for.
            const bool exists = columns.cut_through(document, composer, k);
            CHECK_EQ(exists, head);
            if (!head) {
                CHECK(columns.finished());
                CHECK(expected_cmds.size() == 2);
                ++tally.empty;
                continue;
            }
            if (!exists)
                continue;
            const DocumentColumn& column = columns.column(k);
            CHECK_EQ(column.first_node, start);
            CHECK_EQ(column.place, place);
            CHECK_EQ(column.head, glue_width);
            CHECK_EQ(column.least_cost, least_cost);
            CHECK_EQ(std::string(STOP_TOKENS[static_cast<int>(column.stop)]), stop);
            CHECK_EQ(column.overfull, fitness == "overfull");
            // The natural height of what was split off, and its last depth.
            CHECK_EQ(column.used, natural + depth);
            CHECK_EQ(column.bottom - column.top + column.head, column.used);
            cmds.clear();
            REQUIRE(columns.flatten(document, k, cmds));
            CHECK_LINES_EQ(expected_cmds, blocks_golden::written(context, cmds),
                           "the column, flattened");
            tally.cmds += cmds.size();
            tally.overfull += column.overfull ? 1 : 0;
            ++tally.columns;
        }
        golden::Cursor overflow(reader.take("overflow"));
        const bool left = overflow.boolean();
        const uint64_t left_start = overflow.u32();
        // What `count` columns did not hold is the columns after them.
        CHECK_EQ(columns.cut_through(document, composer, count), left);
        if (left && columns.count() > count)
            CHECK_EQ(columns.column(count).first_node, left_start);
        while (reader.position() < end)
            reader.take();
        ++tally.splits;
    }
    CHECK(reader.done());
    ++tally.cases;
}

} // namespace

TEST(a_documents_columns_are_the_references_under_its_rules)
{
    golden::File file;
    REQUIRE(file.load(golden::path("tables/columns.golden")));
    const std::vector<core_golden::Case> cases = core_golden::cases(file);
    Tally tally;
    for (const core_golden::Case& c : cases) {
        CHECK_CONTEXT("case %s (line %d)", c.name.c_str(), c.line);
        REQUIRE_EQ(c.kind, std::string("columns"));
        replay_case(file, c, tally);
    }
    std::printf("    columns.golden through a Document: %d cases, %d splits, %d columns "
                "(%d overfull, %d the reference left empty), %zu commands\n",
                tally.cases, tally.splits, tally.columns, tally.overfull, tally.empty, tally.cmds);
    CHECK_EQ(tally.cases, 4);
    CHECK_EQ(tally.splits, 8);
    CHECK_EQ(tally.columns + tally.empty, 21);
    CHECK(tally.overfull > 0);
    CHECK(tally.cmds > 2000);
}

// --- A face with round numbers --------------------------------------------------------------

namespace
{

// Every character is half an em wide, 0.7 em tall and 0.2 em deep; no pair
// kerns. At EM that is 5 pt a character, and with a leading of 12 pt every
// line is exactly 12 pt: tests/test_blocks.cpp's.
constexpr Scaled EM = 10 * PT;
constexpr Scaled LEADING = 12 * PT;

class FakeMetrics : public FontMetrics
{
public:
    explicit FakeMetrics(FontKey key)
        : _key(key)
    {
    }
    Face face() const override { return static_cast<Face>(_key.face()); }
    double size() const override { return static_cast<double>(_key.size()); }
    double ascent() const override { return size() * 0.7; }
    double descent() const override { return size() * 0.2; }
    double x_height() const override { return size() * 0.5; }
    double em() const override { return size(); }
    double space_width() const override { return size() * 0.5; }
    bool has(uint32_t) const override { return true; }
    double advance(uint32_t) const override { return size() * 0.5; }
    GlyphBBox bbox(uint32_t) const override
    {
        GlyphBBox box;
        box.x_max = size() * 0.5;
        box.y_min = -size() * 0.2;
        box.y_max = size() * 0.7;
        return box;
    }
    double kern(uint32_t, uint32_t) const override { return 0.0; }

private:
    FontKey _key;
};

struct Fake {
    std::map<uint64_t, std::unique_ptr<FakeMetrics>> faces;

    static const FontMetrics* resolve(void* user, FontKey key)
    {
        Fake& self = *static_cast<Fake*>(user);
        std::unique_ptr<FakeMetrics>& slot = self.faces[key.bits];
        if (!slot)
            slot.reset(new FakeMetrics(key));
        return slot.get();
    }
    static CharDimens dimens(void*, FontKey font, uint32_t)
    {
        const Scaled size = font.size();
        return CharDimens{size / 2, size * 7 / 10, size / 5};
    }
    MetricsResolver resolver() { return MetricsResolver{&resolve, this}; }
    CharMeasure measure() { return CharMeasure{&dimens, this}; }
};

struct Random {
    uint64_t state;
    explicit Random(uint64_t seed)
        : state(seed * 0x9E3779B97F4A7C15ull + 1)
    {
    }
    uint32_t next()
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<uint32_t>(state >> 16);
    }
    uint32_t below(uint32_t n) { return n == 0 ? 0 : next() % n; }
};

BlockStyle body_style(Scaled indent = 0)
{
    BlockStyle out;
    out.align = Align::LEFT;
    out.left_indent = indent;
    out.marker_gap = 2 * PT;
    out.interline.baselineskip.width = LEADING;
    out.interline.lineskip.width = PT;
    out.strut = Strut::filling(LEADING);
    return out;
}

std::string words(Random& random, uint32_t count)
{
    static const char* const WORDS[] = {"stored", "letters", "and",     "other", "symbols",
                                        "are",    "set",     "in",      "a",     "column",
                                        "by",     "the",     "measure", "of",    "it"};
    std::string out;
    for (uint32_t i = 0; i < count; ++i) {
        if (i != 0)
            out += ' ';
        out += WORDS[random.below(15)];
    }
    return out;
}

// A document described by hand, set two ways that must agree.
struct Harness {
    Fake fake;
    std::vector<Block> blocks;
    DocumentTheme theme;
    DocumentComposer composer;
    Document document;
    DocumentColumns columns;

    Harness()
    {
        theme.quote_indent = 20 * PT;
        theme.quote_rule_thickness = 2 * PT;
        composer.measure = fake.measure();
    }

    Block& text(const std::string& body, Scaled indent = 0)
    {
        blocks.emplace_back();
        Block& block = blocks.back();
        block.type = BlockType::TEXT;
        block.style = body_style(indent);
        CHECK(append_text(block.text.run, body, FontSpec{Face::PROSE, EM}, true, Composition(),
                          fake.resolver()));
        return block;
    }
    Block& code(const std::string& source)
    {
        blocks.emplace_back();
        Block& block = blocks.back();
        block.type = BlockType::CODE;
        block.style = body_style();
        CHECK(
            set_code(block.code, source, FontSpec{Face::CODE, EM}, Composition(), fake.resolver()));
        return block;
    }
    Block& rule(Scaled thickness)
    {
        blocks.emplace_back();
        Block& block = blocks.back();
        block.type = BlockType::RULE;
        block.style = body_style();
        block.thickness = thickness;
        return block;
    }

    void fill(const DocumentRules& rules, Scaled width)
    {
        document.clear();
        document.set_rules(rules);
        document.set_theme(theme);
        for (const Block& block : blocks)
            document.edit(document.add_block()) = block;
        document.set_width(width);
    }

    // The reference's way: everything composed at once, the list cut by
    // split_into_columns, each column shipped.
    void at_once(const DocumentRules& rules, Scaled width, const ColumnRules& how, size_t count,
                 NodeArena& arena, ColumnList& list, ColumnSplit& split,
                 std::vector<std::vector<DrawCmd>>& shipped)
    {
        Composed composed;
        DocumentComposer whole;
        whole.measure = fake.measure();
        whole.compose_blocks(arena, blocks.data(), blocks.size(), Structure(), theme, rules, width,
                             composed);
        list.assign(composed.stacked.vlist);
        REQUIRE(split_into_columns(arena, list, how.height, count, split, how.split_top_skip,
                                   how.split_max_depth));
        shipped.clear();
        for (const SplitColumn& column : split.columns) {
            shipped.emplace_back();
            if (!column.head)
                continue;
            const Node box = arena.add_box(
                column.packed.box, NodeView(list.nodes.data() + column.first, column.count));
            CHECK(ship(arena, box, Point{0, column.packed.box.height}, whole.measure,
                       shipped.back()) == ShipError::NONE);
        }
    }
};

void random_blocks(Random& random, Harness& h, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        const uint32_t kind = random.below(10);
        if (kind < 6) {
            Block& block = h.text(words(random, 1 + random.below(40)));
            block.space_above = static_cast<Scaled>(random.below(3)) * 3 * PT;
            block.space_below = static_cast<Scaled>(random.below(3)) * 4 * PT;
            if (random.below(5) == 0)
                block.quote_depth = 1 + random.below(2);
        } else if (kind < 8) {
            std::string source;
            const uint32_t lines = random.below(6);
            for (uint32_t l = 0; l < lines; ++l)
                source += "line " + std::to_string(l) + "\n";
            Block& block = h.code(source);
            block.space_above = 6 * PT;
            block.space_below = 6 * PT;
        } else {
            Block& block = h.rule(static_cast<Scaled>(1 + random.below(30)) * PT);
            block.space_above = static_cast<Scaled>(random.below(2)) * 5 * PT;
            block.space_below = 5 * PT;
        }
    }
}

} // namespace

TEST(a_documents_columns_are_split_into_columns_of_the_same_blocks_composed_at_once)
{
    int compared = 0;
    int cut_blocks = 0;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        CHECK_CONTEXT("seed %llu", static_cast<unsigned long long>(seed));
        Random random(seed);
        Harness h;
        random_blocks(random, h, 5 + random.below(60));
        const DocumentRules rules; // the reference's
        const Scaled width = static_cast<Scaled>(60 + random.below(200)) * PT;
        h.fill(rules, width);
        for (int round = 0; round < 3; ++round) {
            ColumnRules how;
            how.height = static_cast<Scaled>(1 + random.below(400)) * PT + random.below(65536);
            if (random.below(4) == 0)
                how.split_top_skip.width = static_cast<Scaled>(random.below(20)) * PT;
            if (random.below(6) == 0)
                how.split_max_depth = static_cast<Scaled>(random.below(4)) * PT;
            CHECK_CONTEXT("height %lld", static_cast<long long>(how.height));
            h.columns.set_rules(how);
            // Asked for out of order: a late column first, then all of them.
            h.columns.cut_through(h.document, h.composer, 2 + random.below(5));
            h.columns.cut_all(h.document, h.composer);
            REQUIRE(h.columns.finished());

            NodeArena arena;
            ColumnList list;
            ColumnSplit split;
            std::vector<std::vector<DrawCmd>> shipped;
            const size_t count = h.columns.count() + 2;
            h.at_once(rules, width, how, count, arena, list, split, shipped);
            REQUIRE_EQ(split.columns.size(), count);
            CHECK(split.fits());
            std::vector<DrawCmd> cmds;
            for (size_t k = 0; k < count; ++k) {
                CHECK_CONTEXT("column %zu", k);
                const SplitColumn& expected = split.columns[k];
                CHECK_EQ(expected.head, k < h.columns.count());
                if (!expected.head || k >= h.columns.count())
                    continue;
                const DocumentColumn& column = h.columns.column(k);
                CHECK_EQ(column.first_node, static_cast<uint64_t>(expected.start));
                CHECK_EQ(column.place, expected.cut.best_place);
                CHECK_EQ(column.least_cost, expected.cut.least_cost);
                CHECK(column.stop == expected.cut.stop);
                CHECK_EQ(column.used, expected.cut.best_height_plus_depth);
                CHECK_EQ(column.head, expected.glue.width());
                cmds.clear();
                REQUIRE(h.columns.flatten(h.document, k, cmds));
                CHECK(cmds == shipped[k]);
                ++compared;
                const Scaled first_top = h.document.unit_top(column.first_unit);
                cut_blocks += column.top != first_top ? 1 : 0;
            }
        }
    }
    std::printf("    %d columns of random documents compared with the same blocks composed at "
                "once; %d start inside a block\n",
                compared, cut_blocks);
    CHECK(compared > 300);
    CHECK(cut_blocks > 50);
}

TEST(columns_cut_before_an_edit_stand_and_the_ones_after_it_are_cut_again)
{
    Random random(7);
    Harness h;
    for (int i = 0; i < 400; ++i) {
        Block& block = h.text(words(random, 5 + random.below(30)));
        block.space_below = 6 * PT;
    }
    const DocumentRules rules;
    const Scaled width = 150 * PT;
    h.fill(rules, width);
    ColumnRules how;
    how.height = 300 * PT;
    h.columns.set_rules(how);
    h.columns.cut_all(h.document, h.composer);
    const size_t before = h.columns.count();
    REQUIRE(before > 20);
    std::vector<DocumentColumn> kept;
    for (size_t k = 0; k < before; ++k)
        kept.push_back(h.columns.column(k));

    // Text appended to the last block: only the columns that read it.
    {
        const uint64_t cut = h.columns.columns_cut;
        const uint64_t composed = h.composer.blocks_composed;
        Block& last = h.document.edit_text(h.blocks.size() - 1);
        CHECK(append_text(last.text.run, " and more words that make the block longer than it was",
                          FontSpec{Face::PROSE, EM}, true, Composition(), h.fake.resolver()));
        h.columns.cut_all(h.document, h.composer);
        CHECK_EQ(h.composer.blocks_composed - composed, uint64_t(1));
        CHECK(h.columns.columns_cut - cut <= 2);
        CHECK(h.columns.count() >= before);
        for (size_t k = 0; k + 2 < before; ++k) {
            CHECK_EQ(h.columns.column(k).top, kept[k].top);
            CHECK_EQ(h.columns.column(k).limit, kept[k].limit);
        }
    }
    // A block in the middle changed: the columns before it stand.
    {
        const size_t middle = h.blocks.size() / 2;
        const size_t unit = h.document.unit_of(middle);
        const Scaled y = h.document.unit_top(unit);
        const size_t holder = h.columns.column_at(h.document, h.composer, y);
        const uint64_t cut = h.columns.columns_cut;
        Block& block = h.document.edit_text(middle);
        CHECK(append_text(block.text.run, words(random, 60), FontSpec{Face::PROSE, EM}, true,
                          Composition(), h.fake.resolver()));
        // Asking for a column before the edit cuts nothing.
        REQUIRE(holder > 3);
        CHECK(h.columns.cut_through(h.document, h.composer, holder - 2));
        CHECK_EQ(h.columns.columns_cut, cut);
        for (size_t k = 0; k + 1 < holder; ++k)
            CHECK_EQ(h.columns.column(k).limit, kept[k].limit);
        h.columns.cut_all(h.document, h.composer);
        CHECK(h.columns.columns_cut - cut >= before - holder - 1);
        CHECK(h.columns.columns_cut - cut <= before - holder + 3);

        // And all of it is what cutting afresh gives.
        DocumentColumns fresh;
        fresh.set_rules(how);
        fresh.cut_all(h.document, h.composer);
        REQUIRE_EQ(fresh.count(), h.columns.count());
        for (size_t k = 0; k < fresh.count(); ++k) {
            CHECK_EQ(fresh.column(k).top, h.columns.column(k).top);
            CHECK_EQ(fresh.column(k).first_node, h.columns.column(k).first_node);
            CHECK_EQ(fresh.column(k).place, h.columns.column(k).place);
        }
    }
    // A block added at the end: the last column is cut again, the rest stand.
    {
        const uint64_t cut = h.columns.columns_cut;
        const size_t count = h.columns.count();
        const size_t added = h.document.add_block();
        Block& block = h.document.edit(added);
        block.type = BlockType::TEXT;
        block.style = body_style();
        CHECK(append_text(block.text.run, words(random, 12), FontSpec{Face::PROSE, EM}, true,
                          Composition(), h.fake.resolver()));
        h.columns.cut_all(h.document, h.composer);
        CHECK(h.columns.count() >= count);
        CHECK(h.columns.columns_cut - cut <= 3);
    }
}

TEST(ten_thousand_blocks_in_order_are_laid_out_as_far_as_the_columns_asked_for)
{
    Random random(11);
    Harness h;
    h.blocks.reserve(10000);
    for (int i = 0; i < 10000; ++i) {
        Block& block = h.text(words(random, 20 + random.below(20)));
        block.space_below = 6 * PT;
    }
    const DocumentRules rules = DocumentRules::product(EM);
    h.fill(rules, 200 * PT);
    ColumnRules how;
    how.height = 480 * PT;
    h.columns.set_rules(how);

    h.document.prepare(h.composer);
    CHECK_EQ(h.composer.blocks_composed, uint64_t(0));
    // Three columns: what they hold and the block the last scan stopped in.
    REQUIRE(h.columns.cut_through(h.document, h.composer, 2));
    const uint64_t three = h.composer.blocks_composed;
    const size_t held = h.columns.column(2).next_unit;
    std::printf("    in order, 10,000 blocks: 3 columns laid out %llu blocks (they hold %zu), "
                "read %llu units\n",
                static_cast<unsigned long long>(three), held,
                static_cast<unsigned long long>(h.columns.units_read));
    CHECK(three >= held);
    CHECK(three <= held + 2);
    CHECK(three < 100);
    CHECK(!h.columns.finished());
    // The same three again cost nothing.
    REQUIRE(h.columns.cut_through(h.document, h.composer, 2));
    CHECK_EQ(h.composer.blocks_composed, three);
    CHECK_EQ(h.columns.columns_cut, uint64_t(3));
    // An estimate of how many there will be, from the heights not yet known.
    const size_t estimate = h.columns.estimated_count(h.document);
    CHECK(estimate > 100);

    // The last column costs all of it.
    h.columns.cut_all(h.document, h.composer);
    CHECK_EQ(h.composer.blocks_composed, uint64_t(10000));
    CHECK(h.columns.finished());
    // And with every block laid out, cutting it all again is arithmetic.
    {
        DocumentColumns again;
        again.set_rules(how);
        const auto start = std::chrono::steady_clock::now();
        again.cut_all(h.document, h.composer);
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        CHECK_EQ(h.composer.blocks_composed, uint64_t(10000));
        CHECK_EQ(again.count(), h.columns.count());
        std::printf("    cutting 10,000 laid-out blocks into %zu columns again: %.2f ms\n",
                    again.count(), ms);
    }
    std::printf("    all of it: %zu columns (estimated %zu after three), %llu blocks laid out\n",
                h.columns.count(), estimate,
                static_cast<unsigned long long>(h.composer.blocks_composed));
    CHECK_EQ(h.columns.estimated_count(h.document), h.columns.count());
    // Every column is full to within what cannot be cut: a line and a gap.
    for (size_t k = 0; k + 1 < h.columns.count(); ++k) {
        const DocumentColumn& column = h.columns.column(k);
        CHECK(!column.overfull);
        CHECK(column.used <= how.height + LEADING);
        CHECK(column.used > how.height - 2 * LEADING - 6 * PT);
    }
}

TEST(ten_thousand_blocks_balanced_are_all_laid_out_and_the_columns_are_level)
{
    Random random(13);
    Harness h;
    h.blocks.reserve(10000);
    for (int i = 0; i < 10000; ++i) {
        Block& block = h.text(words(random, 20 + random.below(20)));
        block.space_below = 6 * PT;
    }
    h.fill(DocumentRules::product(EM), 200 * PT);
    ColumnRules how;
    how.balance = 5;
    h.columns.set_rules(how);
    REQUIRE(h.columns.cut_through(h.document, h.composer, 0));
    CHECK_EQ(h.composer.blocks_composed, uint64_t(10000));
    CHECK(h.columns.finished());
    REQUIRE_EQ(h.columns.count(), size_t(5));
    {
        // The search alone, every block already laid out.
        DocumentColumns again;
        again.set_rules(how);
        const auto start = std::chrono::steady_clock::now();
        again.cut_all(h.document, h.composer);
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        CHECK_EQ(again.balanced_at(), h.columns.balanced_at());
        std::printf("    balancing 10,000 laid-out blocks again (%zu nodes, %llu heights): "
                    "%.2f ms\n",
                    again.working_list().size(),
                    static_cast<unsigned long long>(again.heights_tried), ms);
    }
    std::printf("    balanced, 10,000 blocks into 5: %llu blocks laid out, %llu heights tried, "
                "cut at %.2f pt, columns %.2f pt tall\n",
                static_cast<unsigned long long>(h.composer.blocks_composed),
                static_cast<unsigned long long>(h.columns.heights_tried),
                static_cast<double>(h.columns.balanced_at()) / UNITY,
                static_cast<double>(h.columns.height()) / UNITY);
    Scaled tallest = 0;
    Scaled shortest = h.columns.column(0).used;
    for (size_t k = 0; k < 5; ++k) {
        const DocumentColumn& column = h.columns.column(k);
        CHECK(!column.empty);
        CHECK(!column.overfull);
        tallest = std::max(tallest, column.used);
        if (k + 1 < 5)
            shortest = std::min(shortest, column.used);
    }
    CHECK_EQ(h.columns.height(), tallest);
    // All but the last are within what cannot be cut of one another.
    CHECK(tallest - shortest <= 2 * LEADING + 6 * PT);
    // The height is the least: one scaled point less and five do not hold it.
    {
        std::vector<Node> list;
        for (size_t u = 0; u < h.document.unit_count(); ++u)
            h.document.unit_vlist(u, list);
        NodeArena arena;
        ColumnList columns;
        ColumnSplit split;
        columns.assign(list);
        REQUIRE(split_into_columns(arena, columns, h.columns.balanced_at(), 5, split));
        CHECK(split.fits());
        columns.assign(list);
        REQUIRE(split_into_columns(arena, columns, h.columns.balanced_at() - 1, 5, split));
        CHECK(!split.fits());
    }
    // Asking again costs nothing; an edit makes the search again and lays out
    // the one block.
    const uint64_t tried = h.columns.heights_tried;
    REQUIRE(h.columns.cut_through(h.document, h.composer, 4));
    CHECK_EQ(h.columns.heights_tried, tried);
    Block& last = h.document.edit_text(h.blocks.size() - 1);
    CHECK(append_text(last.text.run, " more", FontSpec{Face::PROSE, EM}, true, Composition(),
                      h.fake.resolver()));
    REQUIRE(h.columns.cut_through(h.document, h.composer, 4));
    CHECK(h.columns.heights_tried > tried);
    CHECK_EQ(h.composer.blocks_composed, uint64_t(10001));
}

TEST(balanced_columns_of_a_short_document_and_of_none)
{
    Harness h;
    h.fill(DocumentRules::product(EM), 200 * PT);
    ColumnRules how;
    how.balance = 3;
    how.split_top_skip.width = 0;
    h.columns.set_rules(how);
    CHECK(h.columns.cut_through(h.document, h.composer, 2));
    CHECK_EQ(h.columns.count(), size_t(3));
    CHECK_EQ(h.columns.height(), Scaled(0));
    for (size_t k = 0; k < 3; ++k)
        CHECK(h.columns.column(k).empty);
    CHECK_EQ(h.columns.column_at(h.document, h.composer, 0), size_t(0));

    // One line: one column holds it and two hold nothing.
    h.text("one line");
    h.fill(DocumentRules::product(EM), 200 * PT);
    CHECK(h.columns.cut_through(h.document, h.composer, 2));
    REQUIRE_EQ(h.columns.count(), size_t(3));
    CHECK(!h.columns.column(0).empty);
    CHECK(h.columns.column(1).empty);
    CHECK(h.columns.column(2).empty);
    CHECK_EQ(h.columns.height(), LEADING);
    std::vector<DrawCmd> cmds;
    CHECK(h.columns.flatten(h.document, 1, cmds));
    CHECK(cmds.empty());

    // Something that cannot be cut: the tallest column is as tall as it is,
    // and the rest are balanced among themselves.
    h.blocks.clear();
    h.rule(200 * PT).space_below = 6 * PT;
    for (int i = 0; i < 12; ++i)
        h.text("a line");
    h.fill(DocumentRules::product(EM), 200 * PT);
    how.balance = 3;
    h.columns.set_rules(how);
    CHECK(h.columns.cut_through(h.document, h.composer, 2));
    REQUIRE_EQ(h.columns.count(), size_t(3));
    // No column is overfull: the columns are as tall as the tallest, which
    // is the rule's, and nothing reaches past that.
    for (size_t k = 0; k < 3; ++k)
        CHECK(!h.columns.column(k).overfull);
    CHECK_EQ(h.columns.column(0).used, 200 * PT);
    CHECK_EQ(h.columns.height(), 200 * PT);
    CHECK_EQ(h.columns.reach(), 200 * PT);
    CHECK_EQ(h.columns.column(1).used, 6 * LEADING);
    CHECK_EQ(h.columns.column(2).used, 6 * LEADING);
}

TEST(under_the_products_rules_a_column_can_end_between_two_blocks_but_not_after_a_heading)
{
    // Twenty one-line blocks with no space between them: nothing in the
    // reference's list is a breakpoint, and it is one column however tall.
    Harness h;
    for (int i = 0; i < 20; ++i)
        h.text("one line");
    ColumnRules how;
    how.height = 5 * LEADING;
    how.split_top_skip.width = 0;
    {
        h.fill(DocumentRules(), 200 * PT);
        h.columns.set_rules(how);
        h.columns.cut_all(h.document, h.composer);
        CHECK_EQ(h.columns.count(), size_t(1));
        CHECK(h.columns.column(0).overfull);
    }
    {
        DocumentRules product = DocumentRules::product(EM);
        h.fill(product, 200 * PT);
        DocumentColumns columns;
        columns.set_rules(how);
        columns.cut_all(h.document, h.composer);
        REQUIRE_EQ(columns.count(), size_t(4));
        for (size_t k = 0; k < 4; ++k) {
            CHECK(!columns.column(k).overfull);
            CHECK_EQ(columns.column(k).used, 5 * LEADING);
            CHECK_EQ(columns.column(k).first_unit, static_cast<uint32_t>(5 * k));
        }
    }
    // The fifth block is a heading: the column that would end after it ends
    // before it, and it heads the next one with the block it introduces.
    {
        h.blocks[4].keep_with_next = true;
        h.fill(DocumentRules::product(EM), 200 * PT);
        DocumentColumns columns;
        columns.set_rules(how);
        columns.cut_all(h.document, h.composer);
        REQUIRE(columns.count() >= 4);
        CHECK_EQ(columns.column(0).used, 4 * LEADING);
        CHECK_EQ(columns.column(1).first_unit, uint32_t(4));
        CHECK_EQ(columns.column(1).used, 5 * LEADING);
        CHECK_EQ(columns.column(2).first_unit, uint32_t(9));
    }
    // The space between two blocks is dropped where a cut falls in it, and is
    // there where none does.
    {
        h.blocks[4].keep_with_next = false;
        for (Block& block : h.blocks)
            block.space_below = 6 * PT;
        h.fill(DocumentRules::product(EM), 200 * PT);
        DocumentColumns columns;
        how.height = 5 * LEADING + 4 * 6 * PT;
        columns.set_rules(how);
        columns.cut_all(h.document, h.composer);
        REQUIRE_EQ(columns.count(), size_t(4));
        for (size_t k = 0; k < 4; ++k) {
            CHECK_EQ(columns.column(k).used, how.height);
            CHECK_EQ(columns.column(k).head, Scaled(0));
        }
        CHECK_EQ(columns.column(1).top - columns.column(0).bottom, 6 * PT);
    }
}

TEST(the_column_a_height_of_the_uncut_document_is_shown_in)
{
    Random random(3);
    Harness h;
    for (int i = 0; i < 200; ++i)
        h.text(words(random, 5 + random.below(40))).space_below = 6 * PT;
    h.fill(DocumentRules::product(EM), 180 * PT);
    ColumnRules how;
    how.height = 240 * PT;
    h.columns.set_rules(how);
    // Asked from the far end first: everything before it is cut on the way.
    h.document.layout_all(h.composer);
    const Scaled height = h.document.height();
    const size_t last = h.columns.column_at(h.document, h.composer, height - 1);
    CHECK(h.columns.finished());
    CHECK_EQ(last + 1, h.columns.count());
    for (size_t k = 0; k < h.columns.count(); ++k) {
        const DocumentColumn& column = h.columns.column(k);
        CHECK_EQ(h.columns.column_at(h.document, h.composer, column.top), k);
        CHECK_EQ(h.columns.column_at(h.document, h.composer, column.bottom - 1), k);
        if (k + 1 < h.columns.count()) {
            CHECK_EQ(h.columns.column_at(h.document, h.composer, column.limit - 1), k);
            CHECK_EQ(column.limit, h.columns.column(k + 1).top);
            CHECK(column.bottom <= column.limit);
        }
    }
    CHECK_EQ(h.columns.column_at(h.document, h.composer, -5 * PT), size_t(0));
    CHECK_EQ(h.columns.column_at(h.document, h.composer, height + 100 * PT), last);
}

TEST(rules_nobody_vetted_are_cut_to_what_can_be_set)
{
    Random random(5);
    Harness h;
    for (int i = 0; i < 30; ++i)
        h.text(words(random, 5 + random.below(40)));
    h.fill(DocumentRules::product(EM), 180 * PT);
    for (const Scaled height : {Scaled(0), Scaled(-5), Scaled(1), MAX_DIMEN, Scaled(1) << 60}) {
        ColumnRules how;
        how.height = height;
        how.split_top_skip.width = Scaled(1) << 61;
        how.split_max_depth = -(Scaled(1) << 61);
        h.columns.set_rules(how);
        h.columns.cut_all(h.document, h.composer);
        CHECK(h.columns.finished());
        CHECK(h.columns.count() >= 1);
        std::vector<DrawCmd> cmds;
        for (size_t k = 0; k < h.columns.count(); ++k)
            CHECK(h.columns.flatten(h.document, k, cmds));
    }
    ColumnRules many;
    many.balance = 0xFFFFFFFFu;
    h.columns.set_rules(many);
    h.columns.cut_all(h.document, h.composer);
    CHECK_EQ(h.columns.count(), size_t(MAX_BALANCED_COLUMNS));
}

// What is kept of the columns across an edit is kept only where the document
// is the same document. A block of another shape that is exactly as tall as
// the one whose place it takes is not the unit that stood there: here a rule
// two lines thick takes the place of a two-line paragraph that a column was
// cut inside, and a rule cannot be cut.
TEST(a_unit_of_the_same_height_but_another_shape_is_not_the_unit_that_stood_there)
{
    Harness h;
    const std::string two_lines = "stored letters and other symbols are set in a column";
    h.text(two_lines);
    h.text(two_lines);
    h.rule(2 * LEADING);
    // And more of the same height after it, so that the document's end, where
    // the number of units says something changed, is far from the cut.
    for (int i = 0; i < 4; ++i)
        h.text(two_lines);
    // The product's rules: a column may end between two blocks.
    const DocumentRules rules = DocumentRules::product(EM);
    const Scaled width = 150 * PT;
    h.fill(rules, width);
    ColumnRules how;
    how.height = LEADING + LEADING / 2;
    h.columns.set_rules(how);
    h.columns.cut_all(h.document, h.composer);
    // The premise: both paragraphs are two lines, cut between them, and the
    // rule is as tall as either.
    REQUIRE_EQ(h.document.layout(0).lines.size(), size_t(2));
    REQUIRE_EQ(h.document.layout(1).lines.size(), size_t(2));
    REQUIRE_EQ(h.document.layout(0).extent, 2 * LEADING);
    REQUIRE_EQ(h.document.layout(2).extent, h.document.layout(1).extent);
    REQUIRE_EQ(h.columns.count(), size_t(13));

    // The first paragraph goes: the second takes its place, and the rule the
    // second's.
    REQUIRE(h.document.splice(0, 1, 0));
    h.columns.cut_all(h.document, h.composer);
    DocumentColumns fresh;
    fresh.set_rules(how);
    fresh.cut_all(h.document, h.composer);
    REQUIRE_EQ(fresh.count(), size_t(11));
    REQUIRE_EQ(h.columns.count(), fresh.count());
    for (size_t k = 0; k < fresh.count(); ++k) {
        CHECK_CONTEXT("column %d", static_cast<int>(k));
        CHECK_EQ(h.columns.column(k).top, fresh.column(k).top);
        CHECK_EQ(h.columns.column(k).limit, fresh.column(k).limit);
        CHECK_EQ(h.columns.column(k).first_unit, fresh.column(k).first_unit);
        CHECK(h.columns.column(k).overfull == fresh.column(k).overfull);
    }
    // The rule is alone in the third column and overfull there.
    CHECK(fresh.column(2).overfull);
    CHECK_EQ(fresh.column(2).first_unit, uint32_t(1));
    CHECK_EQ(fresh.column(3).first_unit, uint32_t(2));
}
