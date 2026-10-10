// src/blocks against the reference: every case of tests/golden/blocks replayed
// stage by stage -- each block's material, each block alone as its boxes, the
// stacking item by item, the extents, the document box, the shipped commands
// and the units -- twice: once answering metrics from the case's own records,
// where a question the reference did not ask is a failure, and once with the
// real font reader behind it. Each stage is checked three ways: the pieces
// called one by one, DocumentComposer::compose_blocks, and Document, which
// lays each block out alone and places it. Then what only the port has: each
// of DocumentRules' switches, laziness counted in blocks composed, and reuse
// without allocation.

#include "blocks_golden.h"

#include "fontlib.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace ImTypeset::detail;

// Counts every allocation the test executable makes, so that a test can show
// that a warmed-up document makes none. Not inlined: GCC, seeing through an
// inlined replacement to malloc and free, reports the pair as mismatched.
#if defined(__GNUC__)
#define TEST_BLOCKS_NOINLINE __attribute__((noinline))
#else
#define TEST_BLOCKS_NOINLINE
#endif

static size_t g_allocations = 0;

TEST_BLOCKS_NOINLINE void* operator new(size_t size)
{
    ++g_allocations;
    void* const memory = std::malloc(size != 0 ? size : 1);
    if (memory == nullptr)
        std::abort();
    return memory;
}

TEST_BLOCKS_NOINLINE void operator delete(void* memory) noexcept
{
    std::free(memory);
}

TEST_BLOCKS_NOINLINE void operator delete(void* memory, size_t) noexcept
{
    std::free(memory);
}

// Every other form the library or the standard library reaches for, so that no
// block is allocated by one family and freed by another.
TEST_BLOCKS_NOINLINE void* operator new(size_t size, const std::nothrow_t&) noexcept
{
    ++g_allocations;
    return std::malloc(size != 0 ? size : 1);
}

TEST_BLOCKS_NOINLINE void* operator new[](size_t size)
{
    return operator new(size);
}

TEST_BLOCKS_NOINLINE void* operator new[](size_t size, const std::nothrow_t& tag) noexcept
{
    return operator new(size, tag);
}

TEST_BLOCKS_NOINLINE void operator delete(void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}

TEST_BLOCKS_NOINLINE void operator delete[](void* memory) noexcept
{
    std::free(memory);
}

TEST_BLOCKS_NOINLINE void operator delete[](void* memory, size_t) noexcept
{
    std::free(memory);
}

TEST_BLOCKS_NOINLINE void operator delete[](void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}

namespace
{

constexpr Scaled PT = UNITY;

// The reference's default registry: the files imgui-bundle ships, by role.
// CODE_BOLD is left to its stand-in, as the reference leaves it.
void register_defaults(FontLibrary& library)
{
    static const struct {
        Face face;
        const char* file;
    } defaults[] = {
        {Face::PROSE, "Roboto-Regular.ttf"},
        {Face::PROSE_BOLD, "Roboto-Bold.ttf"},
        {Face::PROSE_ITALIC, "Roboto-RegularItalic.ttf"},
        {Face::PROSE_BOLD_ITALIC, "Roboto-BoldItalic.ttf"},
        {Face::CODE, "Inconsolata-Medium.ttf"},
        {Face::MATH, "latinmodern-math.otf"},
    };
    for (const auto& entry : defaults) {
        std::string why;
        if (!library.register_font(
                entry.face, golden::data_path(std::string("fonts/") + entry.file).c_str(), &why))
            FAIL("%s: %s", entry.file, why.c_str());
    }
}

// --- Replaying a case -------------------------------------------------------------------

struct Tally {
    int cases = 0;
    int composes = 0;
    int blocks = 0;
    int text = 0;
    int code = 0;
    int rules = 0;
    int lines = 0;
    int hung = 0;
    int bars = 0;
    int gaps = 0;
    int empty_gaps = 0;
    int negative_gaps = 0;
    int units = 0;
    int spans = 0;
    size_t cmds = 0;
    int asked = 0;
    std::set<std::string> names;
};

// Builds the port's Block from one block group of a case. False if the port
// refused something the reference set.
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
    }
    return true;
}

// The INPUT stage: the block's derived node lists are the reference's.
void check_material(core_golden::Context& context, const blocks_golden::BlockIn& in,
                    const Block& block)
{
    switch (in.type) {
    case BlockType::TEXT: {
        // The reference's material holds the indent box; the port adds it when
        // the block is composed.
        std::vector<Node> material;
        if (in.first_line_indent != 0) {
            Box indent;
            indent.width = in.first_line_indent;
            material.push_back(context.arena.add_box(indent));
        }
        material.insert(material.end(), block.text.run.nodes.begin(), block.text.run.nodes.end());
        std::vector<std::string> actual;
        core_golden::write_nodes(context, "material", material, actual);
        CHECK_LINES_EQ(in.material, actual, "material");
        if (in.has_marker) {
            actual.clear();
            core_golden::write_nodes(context, "marker", block.text.marker.nodes, actual);
            CHECK_LINES_EQ(in.marker, actual, "marker");
        }
        break;
    }
    case BlockType::CODE: {
        CHECK_EQ(block.code.count(), static_cast<size_t>(in.line_count));
        for (size_t l = 0; l < block.code.count() && l < in.code_lines.size(); ++l) {
            CHECK_CONTEXT("code line %zu", l);
            std::vector<std::string> actual;
            core_golden::write_nodes(context, "line", block.code.line(l), actual);
            CHECK_LINES_EQ(in.code_lines[l], actual, "a code line");
        }
        break;
    }
    case BlockType::RULE:
        break;
    }
}

// The `lines` stage for one block: `boxes` is what block_boxes gave.
void check_lines(const golden::File& file, core_golden::Reader& reader,
                 core_golden::Context& context, const blocks_golden::BlockIn& in,
                 const Block& block, Scaled width, BlockComposer& composer, const BlockBoxes& boxes,
                 size_t number, Tally& tally)
{
    (void)file;
    golden::Cursor open(reader.take("lines"));
    REQUIRE_EQ(open.u32(), static_cast<uint32_t>(number));
    const std::string type(open.raw());
    const uint32_t count = open.u32();
    open.expect_done();
    CHECK_EQ(boxes.lines.size(), static_cast<size_t>(count));
    CHECK(boxes.ok());
    tally.lines += static_cast<int>(count);

    if (in.type == BlockType::TEXT) {
        REQUIRE_EQ(type, std::string("text"));
        const golden::Record& group = reader.take("break");
        const std::string list(group.raw(1));
        const ParagraphComposer& paragraph = composer.paragraph();
        CHECK(blocks_golden::same_shape(core_golden::read_shape(reader),
                                        shape(width, in.left_indent, in.first_line_indent)));
        const core_golden::Params params = core_golden::read_params(reader);
        CHECK(blocks_golden::same_params(params, paragraph.line_params()));
        CHECK_EQ(list == "unshrinkable", paragraph.line_params().first_fit);
        CHECK_EQ(list == "unshrinkable", in.align != Align::JUSTIFY);
        if (in.align != Align::JUSTIFY) {
            std::vector<std::string> actual;
            core_golden::write_nodes(context, "unshrinkable", paragraph.broken_material(), actual);
            CHECK_LINES_EQ(in.unshrinkable, actual, "unshrinkable");
        }
        {
            const size_t first = reader.position();
            const core_golden::ParagraphResult expected = core_golden::read_paragraph(reader);
            for (int64_t i = 0; i < expected.line_count && !reader.done(); ++i)
                core_golden::read_packed(reader, context);
            CHECK_LINES_EQ(reader.lines(first, reader.position()),
                           blocks_golden::written(context, boxes.para.broken),
                           "the broken paragraph");
        }
        reader.take("endbreak");
        if (reader.next_is("hung")) {
            reader.take();
            const size_t first = reader.position();
            core_golden::read_node(reader, context);
            CHECK(in.has_marker);
            CHECK_LINES_EQ(reader.lines(first, reader.position()),
                           core_golden::node_lines(context, boxes.hung), "the hung line");
            ++tally.hung;
        } else {
            CHECK(!in.has_marker);
            CHECK(!boxes.hung.is(NodeType::BOX));
        }
        const uint32_t strutted = reader.take("strutted").u32(0);
        CHECK_EQ(strutted, count);
        for (uint32_t i = 0; i < strutted; ++i) {
            golden::Cursor dims(reader.take("dims"));
            if (i >= boxes.lines.size())
                continue;
            CHECK_CONTEXT("line %u", i);
            const Box& box = context.arena.box(boxes.lines[i]);
            CHECK_EQ(box.width, dims.i64());
            CHECK_EQ(box.height, dims.i64());
            CHECK_EQ(box.depth, dims.i64());
            CHECK_EQ(box.shift_amount, dims.i64());
            dims.expect_done();
            // The strut changes two numbers and nothing else.
            const Node& from =
                i == 0 && in.has_marker ? boxes.hung : boxes.para.broken.lines[i].box;
            const Box& before = context.arena.box(from);
            CHECK(box.contents == before.contents);
            CHECK(box.glue_set == before.glue_set && box.glue_sign == before.glue_sign);
        }
    } else {
        REQUIRE_EQ(type, std::string(in.type == BlockType::CODE ? "code" : "rule"));
        const size_t first = reader.position();
        for (uint32_t i = 0; i < count && !reader.done(); ++i)
            core_golden::read_node(reader, context);
        std::vector<std::string> actual;
        for (const Node& line : boxes.lines)
            core_golden::write_node(context, line, actual);
        CHECK_LINES_EQ(reader.lines(first, reader.position()), actual,
                       in.type == BlockType::CODE ? "the code lines" : "the rule's box");
        CHECK(boxes.continues.empty());
        CHECK(!boxes.clipped);
    }
    (void)block;
    reader.take("endlines");
}

// Replays one `blocks` case. With `library` null the metrics are the case's
// own records; otherwise the real font reader answers.
void replay_case(const golden::File& file, const core_golden::Case& c, FontLibrary* library,
                 Tally& tally)
{
    core_golden::Context context;
    core_golden::Reader reader(file, c);
    reader.take("source");
    const blocks_golden::ThemeIn theme = blocks_golden::read_theme(reader);

    blocks_golden::Recorded recorded;
    recorded.read(reader, context);
    const core_golden::MeasureTable table = core_golden::read_measure(reader, context);
    blocks_golden::TableMeasure table_measure(table);

    MetricsResolver resolve = recorded.resolver();
    CharMeasure measure = table_measure.measure();
    std::unique_ptr<CharMeasureCache> cache;
    if (library != nullptr) {
        resolve = fontlib_metrics(*library);
        cache.reset(new CharMeasureCache(resolve));
        measure = cache->measure();
        // The font reader measures every character as the reference did.
        for (const core_golden::MeasureTable::Entry& entry : table.entries) {
            const CharDimens dimens = (*cache)(entry.font, entry.codepoint);
            CHECK(dimens.width == entry.width && dimens.height == entry.height &&
                  dimens.depth == entry.depth);
        }
    }

    // --- INPUT: the blocks, twice over: a plain array for the pieces and for
    // compose_blocks, and a Document.
    const std::vector<blocks_golden::BlockIn> inputs = blocks_golden::read_blocks(reader, context);
    std::vector<Block> blocks(inputs.size());
    Document document;
    document.set_theme(theme.theme);
    for (size_t i = 0; i < inputs.size(); ++i) {
        CHECK_CONTEXT("block %zu (line %d)", i, inputs[i].line);
        if (!build_block(inputs[i], theme, resolve, blocks[i]))
            return;
        check_material(context, inputs[i], blocks[i]);
        REQUIRE_EQ(document.add_block(), i);
        Block& copy = document.edit(i);
        copy = blocks[i];
        ++(inputs[i].type == BlockType::TEXT   ? tally.text
           : inputs[i].type == BlockType::CODE ? tally.code
                                               : tally.rules);
    }
    tally.blocks += static_cast<int>(inputs.size());

    const DocumentRules rules; // the reference's
    BlockComposer block_composer;
    BlockBoxes boxes;
    Stacker stacker;
    Stacked stacked;
    DocumentComposer composer;
    composer.measure = measure;
    Composed composed;
    Derived derived;
    std::vector<Scaled> indents;
    std::vector<uint32_t> line_first;
    std::vector<Node> lines;
    std::vector<DrawCmd> cmds;
    std::vector<DrawCmd> flat;
    std::vector<BlockSpan> spans;

    // --- EXPECTED: one composition per width
    while (reader.next_is("compose")) {
        const golden::Record& open = reader.take("compose");
        const std::string run = open.str(0);
        const Scaled width = open.i64(1);
        CHECK_CONTEXT("compose %s (line %d)", run.c_str(), open.line);

        // Each block alone.
        derive(blocks.data(), blocks.size(), nullptr, Structure(), rules, derived);
        indents.clear();
        line_first.clear();
        lines.clear();
        for (size_t i = 0; i < blocks.size(); ++i) {
            CHECK_CONTEXT("block %zu", i);
            const Scaled indent = effective_indent(blocks[i], derived, rules, width);
            CHECK_EQ(indent, inputs[i].left_indent);
            indents.push_back(indent);
            line_first.push_back(static_cast<uint32_t>(lines.size()));
            block_composer.block_boxes(context.arena, blocks[i], indent, width, prose_breaking(),
                                       measure, rules, boxes);
            check_lines(file, reader, context, inputs[i], blocks[i], width, block_composer, boxes,
                        i, tally);
            lines.insert(lines.end(), boxes.lines.begin(), boxes.lines.end());
        }
        line_first.push_back(static_cast<uint32_t>(lines.size()));

        // The stacking.
        StackInput in;
        in.blocks = blocks.data();
        in.count = blocks.size();
        in.line_first = line_first.data();
        in.lines = lines.data();
        in.indents = indents.data();
        in.derived = &derived;
        in.theme = theme.theme;
        in.rules = rules;
        in.measure = measure;
        in.width = width;
        stacked.tracing = true;
        stacker.stack(context.arena, in, stacked);
        CHECK(stacked.pack_error == PackError::NONE);
        {
            const size_t first = reader.position();
            REQUIRE(reader.next_is("stack"));
            const size_t end =
                blocks_golden::group_end(file, first, reader.end(), "stack", "endstack");
            CHECK_LINES_EQ(reader.lines(first, end), blocks_golden::written(context, stacked.trace),
                           "the stacking");
            while (reader.position() < end)
                reader.take();
        }
        for (const StackEvent& event : stacked.trace) {
            if (event.kind == StackEvent::GAP) {
                ++tally.gaps;
                tally.empty_gaps += event.d != 0 ? 1 : 0;
                tally.negative_gaps += event.c < 0 ? 1 : 0;
            }
        }
        tally.bars += static_cast<int>(stacked.bars.size());

        std::vector<Extent> extents;
        {
            golden::Cursor record(reader.take("extents"));
            const uint32_t count = record.u32();
            REQUIRE_EQ(static_cast<size_t>(count), blocks.size());
            REQUIRE_EQ(stacked.extents.size(), blocks.size());
            for (uint32_t i = 0; i < count; ++i) {
                CHECK_CONTEXT("extent of block %u", i);
                Extent extent;
                extent.top = record.i64();
                extent.bottom = record.i64();
                extents.push_back(extent);
                CHECK_EQ(stacked.extents[i].top, extent.top);
                CHECK_EQ(stacked.extents[i].bottom, extent.bottom);
            }
            record.expect_done();
        }

        // The document box and what it ships.
        const PackResult packed = vpack(context.arena, stacked.vlist);
        CHECK(packed.error == PackError::NONE);
        const Node box = context.arena.add_box(packed.box, stacked.vlist);
        Scaled vbox_width = 0;
        Scaled vbox_height = 0;
        Scaled vbox_depth = 0;
        {
            golden::Cursor vbox(reader.take("vbox"));
            vbox_width = vbox.i64();
            vbox_height = vbox.i64();
            vbox_depth = vbox.i64();
            CHECK(packed.box.kind == BoxKind::VLIST);
            CHECK_EQ(packed.box.width, vbox_width);
            CHECK_EQ(packed.box.height, vbox_height);
            CHECK_EQ(packed.box.depth, vbox_depth);
            CHECK_EQ(stacked.extent, vbox_height + vbox_depth);
            golden::Cursor origin(reader.take("origin"));
            CHECK_EQ(origin.i64(), 0);
            CHECK_EQ(origin.i64(), packed.box.height);
        }
        std::vector<std::string> expected_cmds;
        {
            const size_t first = reader.position();
            core_golden::read_cmds(reader, context);
            expected_cmds = reader.lines(first, reader.position());
            cmds.clear();
            CHECK(ship(context.arena, box, Point{0, packed.box.height}, measure, cmds) ==
                  ShipError::NONE);
            CHECK_LINES_EQ(expected_cmds, blocks_golden::written(context, cmds),
                           "the shipped commands");
            tally.cmds += cmds.size();
        }

        // The same from compose_blocks, in one call.
        {
            composed.stacked.tracing = false;
            CHECK(composer.compose_blocks(context.arena, blocks.data(), blocks.size(), Structure(),
                                          theme.theme, rules, width, composed));
            CHECK_EQ(composed.packed.box.width, vbox_width);
            CHECK_EQ(composed.packed.box.height, vbox_height);
            CHECK_EQ(composed.packed.box.depth, vbox_depth);
            CHECK(composed.stacked.vlist.size() == stacked.vlist.size());
            std::vector<DrawCmd> again;
            CHECK(ship(context.arena, composed.box, Point{0, composed.packed.box.height}, measure,
                       again) == ShipError::NONE);
            CHECK(again == cmds);
        }

        // The same from Document: each block laid out alone, placed, and the
        // whole flattened.
        const std::vector<blocks_golden::UnitRecord> units = blocks_golden::read_units(reader);
        {
            document.set_width(width);
            const uint64_t before = composer.blocks_composed;
            document.prepare(composer);
            CHECK_EQ(composer.blocks_composed, before);
            document.layout_all(composer);
            CHECK(document.settled());
            CHECK_EQ(document.height(), vbox_height + vbox_depth);
            CHECK_EQ(document.content_width(), vbox_width);
            flat.clear();
            REQUIRE(document.flatten(flat, &spans));
            CHECK_LINES_EQ(expected_cmds, blocks_golden::written(context, flat),
                           "the document, flattened");
            for (size_t i = 0; i < blocks.size(); ++i) {
                CHECK_CONTEXT("block %zu", i);
                CHECK(document.laid_out(i));
                CHECK(document.layout(i).ok());
                CHECK_EQ(document.block_top(i), extents[i].top);
                CHECK_EQ(document.block_top(i) + document.layout(i).extent, extents[i].bottom);
                CHECK_EQ(spans[i].top, extents[i].top);
            }
            // The reference's units are the document's, and each block's
            // commands and indices are where the reference found them.
            REQUIRE_EQ(document.unit_count(), units.size());
            uint32_t cmd_at = 0;
            for (size_t u = 0; u < units.size(); ++u) {
                CHECK_CONTEXT("unit %zu", u);
                const DocumentUnit& unit = document.unit(u);
                const blocks_golden::UnitRecord& expected = units[u];
                CHECK_EQ(unit.first_block, expected.first_block);
                CHECK_EQ(unit.block_count, expected.block_count);
                CHECK_EQ(document.unit_top(u), expected.top);
                CHECK_EQ(unit.barred,
                         expected.block_count > 1 || inputs[expected.first_block].quote_depth > 0);
                if (unit.block_count == 0)
                    continue;
                const uint32_t last = unit.first_block + unit.block_count - 1;
                CHECK_EQ(cmd_at, expected.cmd_start);
                CHECK_EQ(spans[last].cmd_end, expected.cmd_end);
                cmd_at = spans[last].cmd_end;
                CHECK_EQ(spans[unit.first_block].index_start, expected.index_start);
                CHECK_EQ(spans[last].index_end, expected.index_end);
                for (const blocks_golden::UnitRecord::Span& span : expected.spans) {
                    CHECK_CONTEXT("span of block %u", span.block);
                    REQUIRE(span.block < spans.size());
                    CHECK_EQ(spans[span.block].index_start, span.index_start);
                    CHECK_EQ(spans[span.block].index_end, span.index_end);
                    ++tally.spans;
                }
            }
            tally.units += static_cast<int>(units.size());
        }
        reader.take("endcompose");
        ++tally.composes;
    }
    CHECK(reader.done());
    if (library == nullptr) {
        CHECK_EQ(recorded.misses, 0);
        CHECK_EQ(table_measure.misses, 0);
        tally.asked += recorded.asked;
    }
    tally.names.insert(c.name);
    ++tally.cases;
}

const char* const BLOCK_FILES[] = {"blocks/paragraphs.golden", "blocks/headings.golden",
                                   "blocks/lists.golden",      "blocks/quotes.golden",
                                   "blocks/code.golden",       "blocks/rules.golden",
                                   "blocks/edges.golden",      "blocks/demo.golden"};

void replay_all(FontLibrary* library, Tally& tally)
{
    for (const char* name : BLOCK_FILES) {
        CHECK_CONTEXT("%s", name);
        golden::File file;
        if (!file.load(golden::path(name))) {
            FAIL("cannot load %s", name);
            continue;
        }
        const std::vector<core_golden::Case> cases = core_golden::cases(file);
        CHECK(!cases.empty());
        for (const core_golden::Case& c : cases) {
            CHECK_CONTEXT("case %s (line %d)", c.name.c_str(), c.line);
            REQUIRE_EQ(c.kind, std::string("blocks"));
            replay_case(file, c, library, tally);
        }
    }
}

void report(const char* what, const Tally& tally)
{
    std::printf("    %s: %d cases, %d compositions, %d blocks (%d text, %d code, %d rule), "
                "%d lines, %d hung, %d bars, %d gaps (%d list-empty, %d negative), %d units, "
                "%d spans, %zu commands\n",
                what, tally.cases, tally.composes, tally.blocks, tally.text, tally.code,
                tally.rules, tally.lines, tally.hung, tally.bars, tally.gaps, tally.empty_gaps,
                tally.negative_gaps, tally.units, tally.spans, tally.cmds);
}

} // namespace

TEST(the_goldens_replay_stage_by_stage_from_recorded_metrics)
{
    Tally tally;
    replay_all(nullptr, tally);
    report("recorded metrics", tally);
    // Counts the oracle's report gives: a golden that went missing or a case
    // that was skipped must not read as a pass.
    CHECK_EQ(tally.cases, 69);
    CHECK_EQ(tally.blocks, 260);
    CHECK_EQ(tally.composes, 215);
    CHECK(tally.bars > 0);
    CHECK(tally.hung > 0);
    CHECK(tally.empty_gaps > 0);
    CHECK(tally.negative_gaps > 0);
    CHECK(tally.asked > 0);
}

TEST(the_goldens_replay_stage_by_stage_from_the_font_reader)
{
    FontLibrary library;
    register_defaults(library);
    Tally tally;
    replay_all(&library, tally);
    report("font reader", tally);
    CHECK_EQ(tally.cases, 69);
    CHECK_EQ(tally.composes, 215);
}

// --- A face with round numbers ------------------------------------------------------------

namespace
{

// Every character is half an em wide, 0.7 em tall and 0.2 em deep, in every
// face; no pair kerns. At EM that is 5 pt a character, and with a leading of
// 12 pt every line is exactly 12 pt.
constexpr Scaled EM = 10 * PT;
constexpr Scaled CHAR = EM / 2;
constexpr Scaled LEADING = 12 * PT;

struct Fake;

class FakeMetrics : public FontMetrics
{
public:
    FakeMetrics(const Fake& owner, FontKey key)
        : _owner(&owner)
        , _key(key)
    {
    }
    Face face() const override { return static_cast<Face>(_key.face()); }
    double size() const override { return static_cast<double>(_key.size()); }
    double ascent() const override { return size() * 0.7; }
    double descent() const override { return size() * 0.2; }
    double x_height() const override { return size() * 0.5; }
    double em() const override { return size(); }
    double space_width() const override { return size() * 0.5; }
    bool has(uint32_t codepoint) const override;
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
    const Fake* _owner;
    FontKey _key;
};

struct Fake {
    // Characters the code face lacks, and characters no face has.
    std::set<uint32_t> code_lacks;
    std::set<uint32_t> nobody_has;
    std::map<uint64_t, std::unique_ptr<FakeMetrics>> faces;

    static const FontMetrics* resolve(void* user, FontKey key)
    {
        Fake& self = *static_cast<Fake*>(user);
        std::unique_ptr<FakeMetrics>& slot = self.faces[key.bits];
        if (!slot)
            slot.reset(new FakeMetrics(self, key));
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

bool FakeMetrics::has(uint32_t codepoint) const
{
    if (_owner->nobody_has.count(codepoint) != 0)
        return false;
    return !(face() == Face::CODE && _owner->code_lacks.count(codepoint) != 0);
}

size_t count_of(const std::vector<DrawCmd>& cmds, CmdType type)
{
    size_t count = 0;
    for (const DrawCmd& cmd : cmds)
        count += cmd.is(type) ? 1 : 0;
    return count;
}

size_t kerns_in(NodeView list)
{
    size_t count = 0;
    for (const Node& node : list)
        count += node.is(NodeType::KERN) ? 1 : 0;
    return count;
}

// A document described by hand, composed two ways that must agree: all at
// once (compose_blocks, shipped) and through a Document.
struct Harness {
    Fake fake;
    std::vector<Block> blocks;
    std::vector<Quote> quotes;
    std::vector<List> lists;
    DocumentTheme theme;

    NodeArena arena;
    DocumentComposer composer;
    Composed composed;
    std::vector<DrawCmd> cmds;
    Document document;
    std::vector<DrawCmd> flat;
    std::vector<BlockSpan> spans;

    Harness()
    {
        blocks.reserve(512);
        theme.quote_indent = 20 * PT;
        theme.quote_rule_thickness = 2 * PT;
        composer.measure = fake.measure();
    }

    static BlockStyle style(Scaled indent)
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

    Block& text(const char* words, Scaled indent = 0)
    {
        blocks.emplace_back();
        Block& block = blocks.back();
        block.type = BlockType::TEXT;
        block.style = style(indent);
        CHECK(append_text(block.text.run, words, FontSpec{Face::PROSE, EM}, true, Composition(),
                          fake.resolver()));
        return block;
    }

    Block& code(const char* source, Scaled indent = 0)
    {
        blocks.emplace_back();
        Block& block = blocks.back();
        block.type = BlockType::CODE;
        block.style = style(indent);
        CHECK(
            set_code(block.code, source, FontSpec{Face::CODE, EM}, Composition(), fake.resolver()));
        return block;
    }

    Block& rule(Scaled thickness, Scaled indent = 0)
    {
        blocks.emplace_back();
        Block& block = blocks.back();
        block.type = BlockType::RULE;
        block.style = style(indent);
        block.thickness = thickness;
        return block;
    }

    void mark(Block& block, const char* marker)
    {
        CHECK(set_marker(block.text, marker, FontSpec{Face::PROSE, EM}, Composition(),
                         fake.resolver()));
    }

    Structure structure() const
    {
        Structure out;
        out.quotes = quotes.data();
        out.quote_count = quotes.size();
        out.lists = lists.data();
        out.list_count = lists.size();
        return out;
    }

    // Composes at once and through a Document, and holds the two together.
    void compose(const DocumentRules& rules, Scaled width)
    {
        arena.clear();
        composer.compose_blocks(arena, blocks.data(), blocks.size(), structure(), theme, rules,
                                width, composed);
        cmds.clear();
        CHECK(ship(arena, composed.box, Point{0, composed.packed.box.height}, composer.measure,
                   cmds) == ShipError::NONE);

        document.clear();
        document.set_rules(rules);
        document.set_theme(theme);
        for (const Quote& quote : quotes)
            document.add_quote(quote);
        for (const List& list : lists)
            document.add_list(list);
        for (const Block& block : blocks)
            document.edit(document.add_block()) = block;
        document.set_width(width);
        // A window in the middle first, so that the units are not laid out in
        // order.
        document.prepare(composer);
        document.layout_window(composer, document.height() / 2, document.height() / 2 + LEADING);
        document.layout_all(composer);
        CHECK(document.settled());
        flat.clear();
        CHECK(document.flatten(flat, &spans));
        CHECK(flat == cmds);
        CHECK_EQ(document.height(), composed.stacked.extent);
        CHECK_EQ(document.height(), composed.packed.box.height + composed.packed.box.depth);
        CHECK_EQ(document.content_width(), composed.packed.box.width);
        for (size_t i = 0; i < blocks.size(); ++i) {
            CHECK_CONTEXT("block %zu", i);
            CHECK_EQ(document.block_top(i), top(i));
            CHECK_EQ(document.block_top(i) + document.layout(i).extent, bottom(i));
        }
    }

    Scaled top(size_t i) const { return composed.stacked.extents[i].top; }
    Scaled bottom(size_t i) const { return composed.stacked.extents[i].bottom; }
    const std::vector<Bar>& bars() const { return composed.stacked.bars; }

    // The x of the first glyph in block `i`'s own commands at or after `skip`.
    Scaled glyph_x(size_t i, size_t skip = 0) const
    {
        for (const DrawCmd& cmd : document.layout(i).cmds) {
            if (cmd.is(CmdType::GLYPH) && skip-- == 0)
                return cmd.x;
        }
        return -1;
    }
};

DocumentRules with(void (*set)(DocumentRules&))
{
    DocumentRules rules;
    set(rules);
    return rules;
}

} // namespace

// --- DocumentRules, one switch at a time ---------------------------------------------------

TEST(product_rules_turn_every_switch_on)
{
    const DocumentRules reference;
    CHECK(!reference.skip_empty_blocks && !reference.quote_any_block && !reference.bar_by_depth);
    CHECK(!reference.separate_quotes && !reference.floor_rule_width);
    CHECK(reference.code_overflow == CodeOverflow::GROW);
    CHECK_EQ(reference.min_measure, Scaled(0));
    CHECK(!reference.list_spacing_in_quotes && !reference.structured_list_spacing);
    CHECK(!reference.marker_on_any_block && !reference.widen_marker_gutter);
    CHECK(!reference.rule_in_container);

    const DocumentRules product = DocumentRules::product(EM);
    CHECK(product.skip_empty_blocks && product.quote_any_block && product.bar_by_depth);
    CHECK(product.separate_quotes && product.floor_rule_width);
    CHECK(product.code_overflow == CodeOverflow::WRAP);
    CHECK_EQ(product.code_continuation_indent, 2 * EM);
    CHECK_EQ(product.min_measure, 8 * EM);
    CHECK(product.list_spacing_in_quotes && product.structured_list_spacing);
    CHECK(product.marker_on_any_block && product.widen_marker_gutter);
    CHECK(product.rule_in_container);
}

// Items 1 and 2.
TEST(an_empty_block_contributes_no_gap)
{
    const DocumentRules skip = with([](DocumentRules& r) { r.skip_empty_blocks = true; });
    {
        Harness h;
        h.text("aa").space_below = 6 * PT;
        Block& empty = h.code("");
        empty.space_above = 4 * PT;
        empty.space_below = 4 * PT;
        h.text("bb").space_above = 2 * PT;

        // The reference: both of the empty block's spaces, as two kerns.
        h.compose(DocumentRules(), 200 * PT);
        CHECK_EQ(kerns_in(h.composed.stacked.vlist), size_t(2));
        CHECK_EQ(h.top(2), LEADING + 10 * PT + 6 * PT);

        // The product: the paragraphs spaced as if it were not there.
        h.compose(skip, 200 * PT);
        CHECK_EQ(kerns_in(h.composed.stacked.vlist), size_t(1));
        CHECK_EQ(h.top(2), LEADING + 8 * PT);
        CHECK_EQ(h.top(1), h.bottom(1));
        CHECK_EQ(h.document.unit(1).gap, Scaled(0));
        CHECK_EQ(h.document.unit(1).extent, Scaled(0));
    }
    {
        // First: nothing above the paragraph under either rule, which is what
        // the reference's compose_blocks does and its layout_units does not.
        Harness h;
        h.code("").space_below = 4 * PT;
        h.text("bb").space_above = 2 * PT;
        h.compose(DocumentRules(), 200 * PT);
        CHECK_EQ(h.top(1), Scaled(0));
        CHECK_EQ(kerns_in(h.composed.stacked.vlist), size_t(0));
        h.compose(skip, 200 * PT);
        CHECK_EQ(h.top(1), Scaled(0));
        CHECK_EQ(h.composed.stacked.extent, LEADING);
    }
    {
        // Last: the reference leaves a kern at the foot of the document.
        Harness h;
        h.text("aa").space_below = 6 * PT;
        h.code("").space_above = 4 * PT;
        h.compose(DocumentRules(), 200 * PT);
        CHECK_EQ(h.composed.stacked.extent, LEADING + 10 * PT);
        h.compose(skip, 200 * PT);
        CHECK_EQ(h.composed.stacked.extent, LEADING);
    }
    {
        // Two empty blocks between two paragraphs, and a document of nothing
        // else.
        Harness h;
        h.text("aa").space_below = 6 * PT;
        h.code("").space_above = 4 * PT;
        h.code("").space_below = 4 * PT;
        h.text("bb").space_above = 2 * PT;
        h.compose(skip, 200 * PT);
        CHECK_EQ(h.top(3), LEADING + 8 * PT);
        Harness none;
        none.code("");
        none.code("");
        none.compose(skip, 200 * PT);
        CHECK_EQ(none.composed.stacked.extent, Scaled(0));
        CHECK(none.cmds.empty());
    }
}

// Item 3.
TEST(code_and_rules_inside_a_quote_keep_its_bar_unbroken)
{
    Harness h;
    h.text("quoted", 20 * PT).quote_depth = 1;
    h.code("x = 1", 20 * PT).quote_depth = 1;
    h.rule(PT, 20 * PT).quote_depth = 1;
    h.text("quoted", 20 * PT).quote_depth = 1;
    for (Block& block : h.blocks) {
        block.space_above = 3 * PT;
        block.space_below = 3 * PT;
    }

    // The reference: the code and the rule read as unquoted and cut the bar.
    h.compose(DocumentRules(), 200 * PT);
    REQUIRE_EQ(h.bars().size(), size_t(2));
    CHECK_EQ(h.bars()[0].block_count, 1u);
    CHECK_EQ(h.bars()[1].first_block, 3u);
    CHECK_EQ(h.bars()[0].extent, LEADING);
    CHECK_EQ(h.document.unit_count(), size_t(4));

    h.compose(with([](DocumentRules& r) { r.quote_any_block = true; }), 200 * PT);
    REQUIRE_EQ(h.bars().size(), size_t(1));
    CHECK_EQ(h.bars()[0].first_block, 0u);
    CHECK_EQ(h.bars()[0].block_count, 4u);
    CHECK_EQ(h.bars()[0].top, Scaled(0));
    CHECK_EQ(h.bars()[0].extent, h.composed.stacked.extent);
    CHECK_EQ(h.bars()[0].extent, 3 * LEADING + PT + 3 * 6 * PT);
    CHECK_EQ(h.document.unit_count(), size_t(1));
    // One bar in what is drawn, and the rule's own rectangle.
    CHECK_EQ(count_of(h.cmds, CmdType::RULE_RECT), size_t(2));

    // A quote holding nothing but an empty code block is still a box in the
    // list, so the paragraph after it gets its gap -- unless empty blocks are
    // skipped, and then the quote is too.
    Harness empty;
    Block& quoted = empty.code("", 20 * PT);
    quoted.quote_depth = 1;
    quoted.space_below = 5 * PT;
    empty.text("after").space_above = 2 * PT;
    empty.compose(with([](DocumentRules& r) { r.quote_any_block = true; }), 200 * PT);
    CHECK_EQ(empty.bars().size(), size_t(1));
    CHECK_EQ(empty.top(1), 7 * PT);
    empty.compose(with([](DocumentRules& r) {
                      r.quote_any_block = true;
                      r.skip_empty_blocks = true;
                  }),
                  200 * PT);
    CHECK_EQ(empty.bars().size(), size_t(0));
    CHECK_EQ(empty.top(1), Scaled(0));
}

// Item 4.
TEST(a_bar_stands_where_its_own_level_of_quoting_is)
{
    Harness h;
    h.text("twice quoted", 40 * PT).quote_depth = 2;
    h.text("twice quoted", 40 * PT).quote_depth = 2;

    // The reference: one quote indent left of the text for both bars.
    h.compose(DocumentRules(), 200 * PT);
    REQUIRE_EQ(h.bars().size(), size_t(2));
    CHECK_EQ(h.bars()[0].level, 1u);
    CHECK_EQ(h.bars()[1].level, 2u);
    CHECK_EQ(h.bars()[0].x, 20 * PT);
    CHECK_EQ(h.bars()[1].x, 20 * PT);

    // By depth: one quote indent for each level between the bar and the text.
    const DocumentRules by_depth = with([](DocumentRules& r) { r.bar_by_depth = true; });
    h.compose(by_depth, 200 * PT);
    REQUIRE_EQ(h.bars().size(), size_t(2));
    CHECK_EQ(h.bars()[0].x, Scaled(0));
    CHECK_EQ(h.bars()[1].x, 20 * PT);

    // A list item inside the inner quote does not move either bar when the
    // quotes say where they open.
    h.quotes.push_back(Quote{NO_CONTAINER, NO_CONTAINER, 3 * PT, 0});
    h.quotes.push_back(Quote{0, NO_CONTAINER, 23 * PT, 0});
    h.blocks[0].quote = 1;
    h.blocks[1].quote = 1;
    h.blocks[1].style.left_indent = 60 * PT;
    h.compose(by_depth, 200 * PT);
    REQUIRE_EQ(h.bars().size(), size_t(2));
    CHECK_EQ(h.bars()[0].x, 3 * PT);
    CHECK_EQ(h.bars()[0].quote, 0);
    CHECK_EQ(h.bars()[1].x, 23 * PT);
    CHECK_EQ(h.bars()[1].quote, 1);
    // What is drawn has the bars there.
    std::vector<Scaled> xs;
    for (const DrawCmd& cmd : h.cmds) {
        if (cmd.is(CmdType::RULE_RECT))
            xs.push_back(cmd.x);
    }
    REQUIRE_EQ(xs.size(), size_t(2));
    CHECK_EQ(xs[0], 3 * PT);
    CHECK_EQ(xs[1], 23 * PT);
}

// Item 5.
TEST(two_quotes_in_a_row_have_a_bar_each)
{
    Harness h;
    h.quotes.push_back(Quote{NO_CONTAINER, NO_CONTAINER, 0, 0});
    h.quotes.push_back(Quote{NO_CONTAINER, NO_CONTAINER, 0, 0});
    Block& first = h.text("the first quote", 20 * PT);
    first.quote = 0;
    first.space_below = 4 * PT;
    Block& second = h.text("the second quote", 20 * PT);
    second.quote = 1;
    second.space_above = 3 * PT;

    // The reference: one run, one bar through the gap.
    h.compose(DocumentRules(), 200 * PT);
    REQUIRE_EQ(h.bars().size(), size_t(1));
    CHECK_EQ(h.bars()[0].block_count, 2u);
    CHECK_EQ(h.bars()[0].extent, 2 * LEADING + 7 * PT);
    CHECK_EQ(h.document.unit_count(), size_t(1));

    h.compose(with([](DocumentRules& r) { r.separate_quotes = true; }), 200 * PT);
    REQUIRE_EQ(h.bars().size(), size_t(2));
    CHECK_EQ(h.bars()[0].extent, LEADING);
    CHECK_EQ(h.bars()[1].extent, LEADING);
    CHECK_EQ(h.bars()[1].top - (h.bars()[0].top + h.bars()[0].extent), 7 * PT);
    CHECK_EQ(h.document.unit_count(), size_t(2));
    CHECK_EQ(h.document.unit(1).gap, 7 * PT);

    // Two blocks of one quote are still one run, and a quote in a quote is
    // split only at its own level.
    h.blocks[1].quote = 0;
    h.compose(with([](DocumentRules& r) { r.separate_quotes = true; }), 200 * PT);
    CHECK_EQ(h.bars().size(), size_t(1));
    h.quotes.push_back(Quote{0, NO_CONTAINER, 20 * PT, 0});
    h.quotes.push_back(Quote{0, NO_CONTAINER, 20 * PT, 0});
    h.blocks[0].quote = 2;
    h.blocks[1].quote = 3;
    h.compose(with([](DocumentRules& r) { r.separate_quotes = true; }), 200 * PT);
    REQUIRE_EQ(h.bars().size(), size_t(3));
    CHECK_EQ(h.bars()[0].block_count, 2u);
    CHECK_EQ(h.bars()[1].block_count, 1u);
    CHECK_EQ(h.bars()[2].block_count, 1u);
    CHECK_EQ(h.bars()[1].level, 2u);
}

// Item 6.
TEST(a_rule_is_never_negative_and_its_box_is_the_column)
{
    Fake fake;
    NodeArena arena;
    BlockComposer composer;
    BlockBoxes boxes;
    Block block;
    block.type = BlockType::RULE;
    block.thickness = PT;
    const DocumentRules floored = with([](DocumentRules& r) { r.floor_rule_width = true; });
    const auto widths = [&](Scaled indent, Scaled width, const DocumentRules& rules, Scaled* before,
                            Scaled* across) -> std::pair<Scaled, size_t> {
        arena.clear();
        CHECK(composer.block_boxes(arena, block, indent, width, prose_breaking(), fake.measure(),
                                   rules, boxes));
        if (!CHECK(boxes.lines.size() == 1))
            return {0, 0};
        const NodeView contents = arena.contents(boxes.lines[0]);
        if (!CHECK(contents.size() == 2))
            return {0, 0};
        *before = contents[0].width();
        *across = contents[1].width();
        std::vector<DrawCmd> cmds;
        CHECK(ship(arena, boxes.lines[0], Point{0, 0}, fake.measure(), cmds) == ShipError::NONE);
        return std::make_pair(arena.box(boxes.lines[0]).width, count_of(cmds, CmdType::RULE_RECT));
    };
    Scaled before = 0;
    Scaled across = 0;

    // The reference, in a column narrower than the indent: a negative rule.
    auto got = widths(50 * PT, 30 * PT, DocumentRules(), &before, &across);
    CHECK_EQ(before, 50 * PT);
    CHECK_EQ(across, -20 * PT);
    CHECK_EQ(got.first, 30 * PT);
    CHECK_EQ(got.second, size_t(0));

    got = widths(50 * PT, 30 * PT, floored, &before, &across);
    CHECK_EQ(before, 30 * PT);
    CHECK_EQ(across, Scaled(0));
    CHECK_EQ(got.first, 30 * PT);

    // An indent that fits is the same under both.
    got = widths(10 * PT, 30 * PT, DocumentRules(), &before, &across);
    CHECK_EQ(across, 20 * PT);
    got = widths(10 * PT, 30 * PT, floored, &before, &across);
    CHECK_EQ(before, 10 * PT);
    CHECK_EQ(across, 20 * PT);
    CHECK_EQ(got.first, 30 * PT);
    CHECK_EQ(got.second, size_t(1));

    // A negative indent: the reference's rule is wider than the column.
    got = widths(-5 * PT, 30 * PT, DocumentRules(), &before, &across);
    CHECK_EQ(across, 35 * PT);
    got = widths(-5 * PT, 30 * PT, floored, &before, &across);
    CHECK_EQ(before, Scaled(0));
    CHECK_EQ(across, 30 * PT);
    CHECK_EQ(got.first, 30 * PT);
}

// Item 7.
TEST(a_code_line_longer_than_the_measure_grows_clips_or_wraps)
{
    const DocumentRules clip = with([](DocumentRules& r) { r.code_overflow = CodeOverflow::CLIP; });
    const DocumentRules wrap = with([](DocumentRules& r) {
        r.code_overflow = CodeOverflow::WRAP;
        r.code_continuation_indent = 10 * PT;
    });
    {
        // Twenty characters, 100 pt, in a 60 pt column.
        Harness h;
        h.code("abcdefghijklmnopqrst\nshort");

        h.compose(DocumentRules(), 60 * PT);
        CHECK_EQ(h.document.content_width(), 100 * PT);
        CHECK_EQ(h.document.layout(0).lines.size(), size_t(2));
        CHECK(!h.document.layout(0).clipped);

        h.compose(clip, 60 * PT);
        CHECK_EQ(h.document.content_width(), 60 * PT);
        CHECK(h.document.layout(0).clipped);
        CHECK_EQ(h.document.layout(0).lines.size(), size_t(2));
        CHECK_EQ(h.document.layout(0).lines[0].width, 60 * PT);
        CHECK_EQ(h.document.layout(0).lines[1].width, 25 * PT);
        // Every character is still there for a draw to clip and a copy to take.
        CHECK_EQ(count_of(h.cmds, CmdType::GLYPH), size_t(25));

        h.compose(wrap, 60 * PT);
        const BlockLayout& layout = h.document.layout(0);
        CHECK_EQ(h.document.content_width(), 60 * PT);
        REQUIRE_EQ(layout.lines.size(), size_t(3));
        CHECK_EQ(layout.lines[0].width, 60 * PT); // twelve characters
        CHECK_EQ(layout.lines[0].shift, Scaled(0));
        CHECK_EQ(layout.lines[1].width, 40 * PT); // the other eight
        CHECK_EQ(layout.lines[1].shift, 10 * PT);
        CHECK_EQ(layout.lines[2].width, 25 * PT);
        CHECK_EQ(layout.lines[2].shift, Scaled(0));
        REQUIRE_EQ(layout.continues.size(), size_t(3));
        CHECK(layout.continues[0] == 0 && layout.continues[1] == 1 && layout.continues[2] == 0);
        CHECK(!layout.clipped);
        CHECK_EQ(count_of(h.cmds, CmdType::GLYPH), size_t(25));
        CHECK_EQ(layout.extent, 3 * LEADING);

        // Wide enough, nothing wraps and nothing says it did.
        h.compose(wrap, 200 * PT);
        CHECK_EQ(h.document.layout(0).lines.size(), size_t(2));
        CHECK(h.document.layout(0).continues.empty());
    }
    {
        // A line is cut after a space when there is one past its indentation.
        Harness h;
        h.code("    aaaa bbbb cccc dddd");
        h.compose(wrap, 60 * PT);
        const BlockLayout& layout = h.document.layout(0);
        REQUIRE_EQ(layout.lines.size(), size_t(3));
        CHECK_EQ(layout.lines[0].width, 9 * CHAR);  // "    aaaa "
        CHECK_EQ(layout.lines[1].width, 10 * CHAR); // "bbbb cccc "
        CHECK_EQ(layout.lines[2].width, 4 * CHAR);  // "dddd"
        // Indentation alone is no place to cut.
        Harness deep;
        deep.code("        aaaaaaaaaaaa");
        deep.compose(wrap, 60 * PT);
        REQUIRE_EQ(deep.document.layout(0).lines.size(), size_t(2));
        CHECK_EQ(deep.document.layout(0).lines[0].width, 12 * CHAR);
    }
    {
        // A column narrower than one character: a character a line, each box
        // no wider than its room, and the walk ends.
        Harness h;
        h.code("abc");
        h.compose(wrap, 3 * PT);
        const BlockLayout& layout = h.document.layout(0);
        REQUIRE_EQ(layout.lines.size(), size_t(3));
        CHECK(layout.clipped);
        CHECK_EQ(layout.lines[0].width, 3 * PT);
        CHECK_EQ(h.document.content_width(), 3 * PT);
        CHECK_EQ(count_of(h.cmds, CmdType::GLYPH), size_t(3));
    }
}

// Item 8.
TEST(deep_nesting_in_a_narrow_column_keeps_a_stated_measure)
{
    Harness h;
    h.text("some words that have to be broken into lines", 100 * PT);
    const DocumentRules floor = with([](DocumentRules& r) { r.min_measure = 60 * PT; });

    // The reference: the indent stands, and the line is what is left, down to
    // one point.
    CHECK_EQ(effective_indent(h.blocks[0], Derived(), DocumentRules(), 120 * PT), 100 * PT);
    h.compose(DocumentRules(), 120 * PT);
    CHECK_EQ(h.document.layout(0).lines[0].width, 20 * PT);
    CHECK_EQ(h.document.layout(0).lines[0].shift, 100 * PT);
    h.compose(DocumentRules(), 40 * PT);
    CHECK_EQ(h.document.layout(0).lines[0].width, PT);
    CHECK(h.document.content_width() > 40 * PT);

    // With a least measure: the indent gives way.
    CHECK_EQ(effective_indent(h.blocks[0], Derived(), floor, 120 * PT), 60 * PT);
    h.compose(floor, 120 * PT);
    CHECK_EQ(h.document.layout(0).lines[0].width, 60 * PT);
    CHECK_EQ(h.document.layout(0).lines[0].shift, 60 * PT);
    // A column wide enough leaves the indent alone.
    CHECK_EQ(effective_indent(h.blocks[0], Derived(), floor, 400 * PT), 100 * PT);
    // A column narrower than the least measure: no indent at all.
    CHECK_EQ(effective_indent(h.blocks[0], Derived(), floor, 40 * PT), Scaled(0));
    h.compose(floor, 40 * PT);
    CHECK_EQ(h.document.layout(0).lines[0].width, 40 * PT);
    CHECK_EQ(h.document.layout(0).lines[0].shift, Scaled(0));
    CHECK_EQ(h.document.content_width(), 40 * PT);
    // A bar that names its place gives way with it.
    h.quotes.push_back(Quote{NO_CONTAINER, NO_CONTAINER, 80 * PT, 0});
    h.blocks[0].quote = 0;
    h.compose(with([](DocumentRules& r) {
                  r.min_measure = 60 * PT;
                  r.bar_by_depth = true;
              }),
              120 * PT);
    REQUIRE_EQ(h.bars().size(), size_t(1));
    CHECK_EQ(h.bars()[0].x, 60 * PT);
}

// Items 9 and 10.
TEST(a_list_is_spaced_by_what_it_says_in_a_quote_or_out_of_one)
{
    const DocumentRules structured =
        with([](DocumentRules& r) { r.structured_list_spacing = true; });
    const DocumentRules everywhere = with([](DocumentRules& r) {
        r.structured_list_spacing = true;
        r.list_spacing_in_quotes = true;
    });
    const auto build = [](Harness& h, bool tight, uint32_t quote_depth) {
        h.lists.push_back(List{NO_CONTAINER, 15 * PT, tight});
        h.lists.push_back(List{0, 15 * PT, tight});
        h.text("before").space_below = 6 * PT; // 0
        h.text("one", 15 * PT);                // 1: item 1
        h.text("one, again", 15 * PT);         // 2: item 1, a second paragraph
        h.text("nested", 30 * PT);             // 3: the nested list's item
        h.text("two", 15 * PT);                // 4: item 2
        h.text("three", 15 * PT);              // 5: item 3
        h.text("after").space_above = 6 * PT;  // 6
        const int32_t lists[] = {NO_CONTAINER, 0, 0, 1, 0, 0, NO_CONTAINER};
        const uint32_t items[] = {0, 1, 1, 1, 2, 3, 0};
        for (size_t i = 0; i < h.blocks.size(); ++i) {
            Block& block = h.blocks[i];
            block.list = lists[i];
            block.item = items[i];
            block.quote_depth = quote_depth;
            if (block.list != NO_CONTAINER) {
                block.space_above = 3 * PT;
                block.space_below = 3 * PT;
            }
            if (quote_depth != 0)
                block.style.left_indent += 20 * PT;
        }
    };
    const auto gaps = [](const Harness& h) {
        std::vector<Scaled> out;
        for (size_t i = 1; i < h.blocks.size(); ++i)
            out.push_back(h.top(i) - h.bottom(i - 1));
        return out;
    };
    const std::vector<Scaled> sums = {9 * PT, 6 * PT, 6 * PT, 6 * PT, 6 * PT, 9 * PT};
    const std::vector<Scaled> tight = {9 * PT, 6 * PT, 0, 0, 0, 9 * PT};
    {
        // The reference: every gap is the two spaces, tight or not.
        Harness h;
        build(h, true, 0);
        h.compose(DocumentRules(), 300 * PT);
        CHECK(gaps(h) == sums);
        // Structured, tight: nothing between items or round the nested list;
        // two paragraphs of one item are still paragraphs; the list's edges
        // are still the blocks' own spaces.
        h.compose(structured, 300 * PT);
        CHECK(gaps(h) == tight);
        // Structured, loose: the blocks' own spaces everywhere.
        for (List& list : h.lists)
            list.tight = false;
        h.compose(structured, 300 * PT);
        CHECK(gaps(h) == sums);
    }
    {
        // The same list in a quote. With the list's spacing switched on only
        // outside quotes, it is spaced by the blocks' own spaces there.
        Harness h;
        build(h, true, 1);
        h.compose(structured, 300 * PT);
        REQUIRE_EQ(h.bars().size(), size_t(1));
        CHECK(gaps(h) == sums);
        // Switched on in quotes too: exactly as outside one.
        h.compose(everywhere, 300 * PT);
        CHECK(gaps(h) == tight);
    }
}

// Item 11.
TEST(an_item_holding_only_code_or_nothing_keeps_its_marker)
{
    const DocumentRules marked = with([](DocumentRules& r) { r.marker_on_any_block = true; });
    Harness h;
    h.mark(h.code("x = 1\ny = 2", 30 * PT), "1.");
    h.mark(h.code("", 30 * PT), "2.");
    h.mark(h.text("", 30 * PT), "3.");

    // The reference: a code block has no marker, an empty one sets nothing.
    // An empty text block is already a line with its marker.
    h.compose(DocumentRules(), 200 * PT);
    CHECK_EQ(count_of(h.document.layout(0).cmds, CmdType::GLYPH), size_t(10));
    CHECK_EQ(h.document.layout(1).lines.size(), size_t(0));
    CHECK_EQ(count_of(h.document.layout(2).cmds, CmdType::GLYPH), size_t(2));
    CHECK_EQ(h.document.layout(2).extent, LEADING);

    h.compose(marked, 200 * PT);
    {
        // The marker is right-aligned in the indent, its gap before the code.
        const BlockLayout& layout = h.document.layout(0);
        CHECK_EQ(count_of(layout.cmds, CmdType::GLYPH), size_t(12));
        CHECK_EQ(h.glyph_x(0, 0), 30 * PT - 2 * PT - 2 * CHAR);
        CHECK_EQ(h.glyph_x(0, 2), 30 * PT);
        REQUIRE_EQ(layout.lines.size(), size_t(2));
        CHECK_EQ(layout.lines[0].shift, Scaled(0));
        CHECK_EQ(layout.lines[0].width, 30 * PT + 5 * CHAR);
        CHECK_EQ(layout.lines[1].shift, 30 * PT);
        CHECK_EQ(layout.extent, 2 * LEADING);
    }
    {
        // The empty item: one empty line, on the strut, with its marker.
        const BlockLayout& layout = h.document.layout(1);
        REQUIRE_EQ(layout.lines.size(), size_t(1));
        CHECK_EQ(layout.extent, LEADING);
        CHECK_EQ(count_of(layout.cmds, CmdType::GLYPH), size_t(2));
        CHECK_EQ(h.glyph_x(1, 0), 30 * PT - 2 * PT - 2 * CHAR);
    }
    CHECK(sets_box(h.blocks[1], marked));
    CHECK(!sets_box(h.blocks[1], DocumentRules()));
}

// Item 12.
TEST(the_marker_gutter_widens_to_the_widest_marker_of_its_list)
{
    const DocumentRules widen = with([](DocumentRules& r) { r.widen_marker_gutter = true; });
    Harness h;
    h.lists.push_back(List{NO_CONTAINER, 15 * PT, false});
    h.lists.push_back(List{0, 15 * PT, false});
    h.text("before");
    h.mark(h.text("one", 15 * PT), "1.");
    h.mark(h.text("a hundred", 15 * PT), "100.");
    h.mark(h.text("nested", 30 * PT), "-");
    h.text("after");
    h.blocks[1].list = 0;
    h.blocks[2].list = 0;
    h.blocks[3].list = 1;

    // The reference: the gutter is what the list gave it, and "100." (20 pt,
    // and a 2 pt gap) does not fit 15 pt: it starts at the left edge and its
    // last character stands where the text does.
    h.compose(DocumentRules(), 300 * PT);
    CHECK_EQ(h.glyph_x(1, 2), 15 * PT);
    CHECK_EQ(h.glyph_x(2, 0), Scaled(0));
    CHECK_EQ(h.glyph_x(2, 3), 15 * PT); // the marker's "."
    CHECK_EQ(h.glyph_x(2, 4), 15 * PT); // the text's "a"

    // Widened: every item of the list, and the list nested in it, moves right
    // by the 7 pt that were missing, and the marker ends its gap before the
    // text.
    h.compose(widen, 300 * PT);
    CHECK_EQ(h.glyph_x(1, 2), 22 * PT);
    CHECK_EQ(h.glyph_x(2, 0), Scaled(0));
    CHECK_EQ(h.glyph_x(2, 3), 15 * PT);
    CHECK_EQ(h.glyph_x(2, 4), 22 * PT);
    CHECK_EQ(h.glyph_x(1, 0), 22 * PT - 2 * PT - 2 * CHAR);
    CHECK_EQ(h.glyph_x(3, 1), 37 * PT);
    CHECK_EQ(h.glyph_x(0), Scaled(0));
    CHECK_EQ(h.glyph_x(4), Scaled(0));
    // The nested list's own marker fits its own gutter: it adds nothing.
    Derived derived;
    const Scaled needs[] = {0, 12 * PT, 22 * PT, 7 * PT, 0};
    derive(h.blocks.data(), h.blocks.size(), needs, h.structure(), widen, derived);
    CHECK_EQ(derived.list_shift[0], 7 * PT);
    CHECK_EQ(derived.list_shift[1], 7 * PT);
    CHECK_EQ(marker_need(h.arena, h.blocks[2], h.fake.measure()), 22 * PT);

    // Changing the marker is a change of content: the list's other items are
    // stale with it, and only they.
    h.document.prepare(h.composer);
    const uint64_t before = h.composer.blocks_composed;
    CHECK(set_marker(h.document.edit(2).text, "2.", FontSpec{Face::PROSE, EM}, Composition(),
                     h.fake.resolver()));
    h.document.layout_all(h.composer);
    CHECK_EQ(h.composer.blocks_composed - before, uint64_t(3));
    CHECK_EQ(h.glyph_x(1, 2), 15 * PT);
}

// Item 13.
TEST(a_rule_in_a_list_is_indented_with_its_list)
{
    Harness h;
    h.lists.push_back(List{NO_CONTAINER, 15 * PT, false});
    h.mark(h.text("a hundred", 15 * PT), "100.");
    h.rule(PT, 15 * PT);
    h.blocks[0].list = 0;
    h.blocks[1].list = 0;
    const auto rule_x = [&]() {
        for (const DrawCmd& cmd : h.document.layout(1).cmds) {
            if (cmd.is(CmdType::RULE_RECT))
                return cmd.x;
        }
        return Scaled(-1);
    };

    // Widened gutter, rule left where it was given: 7 pt left of the text.
    h.compose(with([](DocumentRules& r) { r.widen_marker_gutter = true; }), 300 * PT);
    CHECK_EQ(h.glyph_x(0, 4), 22 * PT);
    CHECK_EQ(rule_x(), 15 * PT);
    // In its container: under the text.
    h.compose(with([](DocumentRules& r) {
                  r.widen_marker_gutter = true;
                  r.rule_in_container = true;
              }),
              300 * PT);
    CHECK_EQ(rule_x(), 22 * PT);
    // And cut back with it in a narrow column.
    h.compose(with([](DocumentRules& r) {
                  r.min_measure = 40 * PT;
                  r.rule_in_container = true;
                  r.floor_rule_width = true;
              }),
              50 * PT);
    CHECK_EQ(rule_x(), 10 * PT);
    h.compose(with([](DocumentRules& r) {
                  r.min_measure = 40 * PT;
                  r.floor_rule_width = true;
              }),
              50 * PT);
    CHECK_EQ(rule_x(), 15 * PT);
}

// Item 14.
TEST(which_block_an_item_belongs_to_is_where_it_is_stored)
{
    // A quote whose middle paragraph is a line of no height: the case the
    // reference's search of baselines against block bottoms names as the one
    // it gets wrong.
    Harness h;
    h.text("first", 20 * PT).quote_depth = 1;
    Block& flat = h.text("x", 20 * PT);
    flat.quote_depth = 1;
    flat.style.strut = Strut();
    flat.text.run.nodes.clear();
    h.text("third", 20 * PT).quote_depth = 1;
    h.compose(DocumentRules(), 200 * PT);
    REQUIRE_EQ(h.document.unit_count(), size_t(1));
    CHECK_EQ(h.document.layout(1).extent, Scaled(0));

    uint32_t index = 0;
    for (size_t i = 0; i < h.blocks.size(); ++i) {
        CHECK_CONTEXT("block %zu", i);
        const BlockLayout& layout = h.document.layout(i);
        const BlockSpan& span = h.spans[i];
        // The ranges tile, each as long as its block shipped.
        CHECK_EQ(span.index_start, index);
        CHECK_EQ(span.index_end - span.index_start, layout.item_count);
        index = span.index_end;
        // And the block's commands in the document are its own, moved.
        REQUIRE_EQ(static_cast<size_t>(span.cmd_end - span.cmd_start), layout.cmds.size());
        for (size_t k = 0; k < layout.cmds.size(); ++k) {
            const DrawCmd& own = layout.cmds[k];
            const DrawCmd& placed = h.flat[span.cmd_start + k];
            CHECK(own.type == placed.type);
            CHECK_EQ(placed.y - own.y, span.top);
            CHECK_EQ(placed.x, own.x);
            if (!own.is(CmdType::RULE_RECT))
                CHECK_EQ(placed.index - own.index, span.index_start);
        }
    }
    // The empty paragraph ships its par-fill glue and its right skip.
    CHECK_EQ(h.spans[1].index_end - h.spans[1].index_start, 2u);
    // A block's layout is in its own indices whatever stands before it.
    CHECK_EQ(h.document.layout(2).cmds[0].start(), 0u);
}

// Item 15.
TEST(a_marker_is_whatever_text_the_block_says)
{
    for (const char* marker : {"(iv)", "a)", "\xE2\x86\x92", "\xE2\x80\xA2", "98765432."}) {
        CHECK_CONTEXT("marker %s", marker);
        Harness h;
        h.mark(h.text("item", 60 * PT), marker);
        h.compose(DocumentRules::product(EM), 300 * PT);
        std::vector<uint32_t> expected;
        decode_utf8(marker, expected);
        std::vector<uint32_t> shipped;
        for (const DrawCmd& cmd : h.document.layout(0).cmds) {
            if (cmd.is(CmdType::GLYPH) && shipped.size() < expected.size())
                shipped.push_back(cmd.codepoint);
        }
        CHECK(shipped == expected);
        // marker_items says where the item's own text starts: after the
        // marker's fil glue and its characters.
        CHECK_EQ(h.document.layout(0).marker_items, static_cast<uint32_t>(expected.size() + 1));
        CHECK_EQ(h.glyph_x(0, expected.size()), 60 * PT);
    }
}

// --- Code as material -----------------------------------------------------------------------

TEST(code_is_one_line_a_source_line_with_tabs_newlines_and_stand_ins)
{
    Fake fake;
    CodeLines lines;
    const FontSpec font{Face::CODE, EM};
    const auto text_of = [&](size_t l) {
        std::string out;
        for (const Node& node : lines.line(l))
            out += node.codepoint() < 0x80 ? static_cast<char>(node.codepoint()) : '?';
        return out;
    };
    // A tab goes to the next multiple of four columns, counted from the start
    // of its own line.
    REQUIRE(set_code(lines, "a\tb\n\tc\nabcd\te", font, Composition(), fake.resolver()));
    REQUIRE_EQ(lines.count(), size_t(3));
    CHECK_EQ(text_of(0), std::string("a   b"));
    CHECK_EQ(text_of(1), std::string("    c"));
    CHECK_EQ(text_of(2), std::string("abcd    e"));
    // CR LF and a lone CR are newlines; blank lines at the end go, others stay.
    REQUIRE(set_code(lines, "a\r\n\r\nb\rc\n\n\n", font, Composition(), fake.resolver()));
    REQUIRE_EQ(lines.count(), size_t(4));
    CHECK_EQ(text_of(0), std::string("a"));
    CHECK_EQ(text_of(1), std::string(""));
    CHECK_EQ(text_of(2), std::string("b"));
    CHECK_EQ(text_of(3), std::string("c"));
    // Nothing, and only blank lines, are no lines.
    REQUIRE(set_code(lines, "", font, Composition(), fake.resolver()));
    CHECK_EQ(lines.count(), size_t(0));
    REQUIRE(set_code(lines, "\n\n", font, Composition(), fake.resolver()));
    CHECK_EQ(lines.count(), size_t(0));
    CHECK(lines.nodes.empty());
    // A character the code face lacks comes from another face; one no face
    // has is U+FFFD and remembers what was written.
    fake.code_lacks.insert(0x3C0);
    fake.nobody_has.insert(0x2192);
    REQUIRE(set_code(lines, "\xCF\x80\xE2\x86\x92x", font, Composition(), fake.resolver()));
    REQUIRE_EQ(lines.count(), size_t(1));
    REQUIRE_EQ(lines.line(0).size(), size_t(3));
    CHECK(lines.line(0)[0].font() == font_key(Face::PROSE, EM));
    CHECK_EQ(lines.line(0)[0].codepoint(), 0x3C0u);
    CHECK(lines.line(0)[1].is_replaced());
    CHECK_EQ(lines.line(0)[1].codepoint(), REPLACEMENT_CHARACTER);
    CHECK_EQ(lines.line(0)[1].original(), 0x2192u);
    CHECK(lines.line(0)[2].font() == font.key());
    // A face nobody knows: refused, with the reason and no lines.
    std::string why;
    CHECK(!set_code(lines, "x", font, Composition(), MetricsResolver(), &why));
    CHECK(!why.empty());
    CHECK_EQ(lines.count(), size_t(0));
}

// --- The two ways of composing agree under any rules ----------------------------------------

namespace
{

struct Random {
    uint64_t state;
    uint32_t next()
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<uint32_t>(state >> 33);
    }
    uint32_t below(uint32_t n) { return n == 0 ? 0 : next() % n; }
    bool chance(uint32_t in) { return below(in) == 0; }
};

DocumentRules random_rules(Random& random)
{
    DocumentRules rules;
    rules.skip_empty_blocks = random.chance(2);
    rules.quote_any_block = random.chance(2);
    rules.bar_by_depth = random.chance(2);
    rules.separate_quotes = random.chance(2);
    rules.floor_rule_width = random.chance(2);
    rules.code_overflow = static_cast<CodeOverflow>(random.below(3));
    rules.code_continuation_indent = static_cast<Scaled>(random.below(4)) * 5 * PT;
    rules.min_measure = random.chance(2) ? 0 : static_cast<Scaled>(10 + random.below(60)) * PT;
    rules.list_spacing_in_quotes = random.chance(2);
    rules.structured_list_spacing = random.chance(2);
    rules.marker_on_any_block = random.chance(2);
    rules.widen_marker_gutter = random.chance(2);
    rules.rule_in_container = random.chance(2);
    return rules;
}

// A document of whatever: quotes in quotes, lists in lists and in quotes,
// markers of every width, empty blocks, spaces of either sign.
void random_document(Random& random, Harness& h, size_t count)
{
    static const char* const words[] = {"a", "be", "sea", "deed", "eerie", "effect", "gigabyte"};
    static const char* const markers[] = {"-", "1.", "10.", "100.", "(xviii)"};
    int32_t quote = NO_CONTAINER;
    int32_t list = NO_CONTAINER;
    uint32_t depth = 0;
    uint32_t item = 0;
    for (size_t i = 0; i < count; ++i) {
        switch (random.below(8)) {
        case 0:
            h.quotes.push_back(
                Quote{quote, list, static_cast<Scaled>(random.below(5)) * 10 * PT, 0});
            quote = static_cast<int32_t>(h.quotes.size() - 1);
            break;
        case 1:
            quote =
                quote == NO_CONTAINER ? NO_CONTAINER : h.quotes[static_cast<size_t>(quote)].parent;
            break;
        case 2:
            h.lists.push_back(
                List{list, static_cast<Scaled>(5 + random.below(20)) * PT, random.chance(2)});
            list = static_cast<int32_t>(h.lists.size() - 1);
            item = 0;
            break;
        case 3:
            list = list == NO_CONTAINER ? NO_CONTAINER : h.lists[static_cast<size_t>(list)].parent;
            break;
        case 4:
            depth = random.below(4);
            break;
        default:
            break;
        }
        const Scaled indent = static_cast<Scaled>(random.below(9)) * 10 * PT;
        Block* block = nullptr;
        switch (random.below(6)) {
        case 0: {
            std::string source;
            for (uint32_t l = random.below(4); l > 0; --l) {
                for (uint32_t w = random.below(6); w > 0; --w)
                    source += std::string(words[random.below(7)]) + (random.chance(3) ? "\t" : " ");
                source += "\n";
            }
            block = &h.code(source.c_str(), indent);
            break;
        }
        case 1:
            block = &h.rule(static_cast<Scaled>(random.below(3)) * PT, indent);
            break;
        default: {
            std::string words_of;
            for (uint32_t w = random.below(30); w > 0; --w)
                words_of += std::string(words[random.below(7)]) + " ";
            block = &h.text(words_of.c_str(), indent);
            block->style.align = static_cast<Align>(random.below(4));
            if (random.chance(5))
                block->style.first_line_indent = 8 * PT;
            break;
        }
        }
        if (random.chance(3))
            h.mark(*block, markers[random.below(5)]);
        block->space_above = (static_cast<Scaled>(random.below(8)) - 1) * 2 * PT;
        block->space_below = (static_cast<Scaled>(random.below(8)) - 1) * 2 * PT;
        block->quote = random.chance(4) ? NO_CONTAINER : quote;
        block->quote_depth = depth;
        block->list = list;
        item += random.chance(2) ? 1 : 0;
        block->item = item;
    }
}

} // namespace

TEST(a_document_laid_out_block_by_block_is_the_document_composed_at_once)
{
    Random random{20261009};
    size_t blocks = 0;
    size_t bars = 0;
    size_t commands = 0;
    for (int round = 0; round < 400; ++round) {
        CHECK_CONTEXT("round %d", round);
        Harness h;
        random_document(random, h, 1 + random.below(40));
        const DocumentRules rules = round % 4 == 0   ? DocumentRules()
                                    : round % 4 == 1 ? DocumentRules::product(EM)
                                                     : random_rules(random);
        const Scaled width = static_cast<Scaled>(5 + random.below(300)) * PT;
        h.compose(rules, width);
        blocks += h.blocks.size();
        bars += h.bars().size();
        commands += h.cmds.size();
        if (rules.code_overflow != CodeOverflow::GROW && rules.floor_rule_width &&
            rules.min_measure > 0 && rules.min_measure <= width && rules.rule_in_container) {
            // Nothing sets a box wider than the column, whatever is in it.
            CHECK(h.document.content_width() <= width);
        }
    }
    std::printf("    %zu blocks, %zu bars, %zu commands\n", blocks, bars, commands);
    CHECK(bars > 100);
}

// --- Laziness ------------------------------------------------------------------------------

TEST(ten_thousand_blocks_are_laid_out_only_where_a_window_shows_them)
{
    Fake fake;
    Document document;
    document.set_rules(DocumentRules::product(EM));
    DocumentComposer composer;
    composer.measure = fake.measure();
    const size_t count = 10000;
    const char* const sixty = "word word word word word word word word word word word word";
    for (size_t i = 0; i < count; ++i) {
        Block& block = document.edit(document.add_block());
        block.style = Harness::style(0);
        block.space_below = 6 * PT;
        REQUIRE(append_text(block.text.run, sixty, FontSpec{Face::PROSE, EM}, true, Composition(),
                            fake.resolver()));
    }
    document.set_width(200 * PT);

    // Preparing reads every block and lays out none.
    document.prepare(composer);
    CHECK_EQ(composer.blocks_composed, uint64_t(0));
    CHECK_EQ(document.unit_count(), count);
    CHECK(!document.settled());
    CHECK(document.height() > 0);

    // A window at the top: the blocks it shows and no more. Each is two lines
    // and a 6 pt gap, 30 pt, so 300 pt shows ten.
    UnitRange range = document.layout_window(composer, 0, 300 * PT);
    CHECK_EQ(range.first, size_t(0));
    CHECK_EQ(range.end, size_t(10));
    CHECK_EQ(composer.blocks_composed, uint64_t(10));
    CHECK(document.laid_out(9));
    CHECK(!document.laid_out(10));
    CHECK_EQ(document.unit_top(9), 9 * 30 * PT);
    CHECK_EQ(document.layout(9).extent, 2 * LEADING);

    // The same window again costs nothing.
    range = document.layout_window(composer, 0, 300 * PT);
    CHECK_EQ(composer.blocks_composed, uint64_t(10));

    // A window in the middle: found without laying out what is above it.
    const Scaled middle = document.height() / 2;
    range = document.layout_window(composer, middle, middle + 300 * PT);
    CHECK(range.first > 4000 && range.first < 6000);
    CHECK(range.end - range.first >= 10 && range.end - range.first <= 12);
    CHECK_EQ(composer.blocks_composed, uint64_t(10) + (range.end - range.first));
    CHECK(!document.laid_out(range.first - 1));
    // The first unit is the one whose gap or whose boxes the window's top is in.
    CHECK(document.unit_top(range.first) - document.unit(range.first).gap <= middle);
    CHECK(document.unit_top(range.first) + document.unit(range.first).extent > middle);
    CHECK(document.unit_top(range.end - 1) < middle + 300 * PT);
    CHECK_EQ(document.unit_at(document.unit_top(range.first)), range.first);
    const uint64_t at_first_width = composer.blocks_composed;

    // A new width makes every layout stale and lays out nothing.
    document.set_width(100 * PT);
    document.prepare(composer);
    CHECK_EQ(composer.blocks_composed, at_first_width);
    CHECK(!document.laid_out(0));
    CHECK(!document.laid_out(range.first));
    // Until a window asks: three lines and the gap now, 42 pt, so 300 pt
    // shows eight.
    range = document.layout_window(composer, 0, 300 * PT);
    CHECK_EQ(range.end, size_t(8));
    CHECK_EQ(composer.blocks_composed, at_first_width + 8);
    CHECK_EQ(document.layout(0).extent, 3 * LEADING);
    CHECK_EQ(document.unit_top(7), 7 * 42 * PT);

    // A change to one block the window shows lays out that block.
    uint64_t before = composer.blocks_composed;
    REQUIRE(append_text(document.edit(3).text.run, " and more words than before",
                        FontSpec{Face::PROSE, EM}, true, Composition(), fake.resolver()));
    document.prepare(composer);
    CHECK_EQ(composer.blocks_composed, before);
    CHECK(!document.laid_out(3));
    CHECK(document.laid_out(2));
    document.layout_window(composer, 0, 300 * PT);
    CHECK_EQ(composer.blocks_composed, before + 1);
    // A change to one it does not show lays out nothing.
    before = composer.blocks_composed;
    document.edit(9000).space_below = 20 * PT;
    document.layout_window(composer, 0, 300 * PT);
    CHECK_EQ(composer.blocks_composed, before);

    // Not settled, so there is no whole document to flatten.
    std::vector<DrawCmd> flat;
    CHECK(!document.flatten(flat));
    CHECK(flat.empty());

    // Laid out to the end, every block exactly once more at most, and the
    // height is the sum of what was measured.
    before = composer.blocks_composed;
    document.layout_all(composer);
    CHECK(document.settled());
    CHECK(composer.blocks_composed - before <= count);
    Scaled sum = 0;
    for (size_t u = 0; u < document.unit_count(); ++u)
        sum += document.unit(u).gap + document.unit(u).extent;
    CHECK_EQ(document.height(), sum);
    CHECK_EQ(document.unit_top(count - 1) + document.unit(count - 1).extent, sum);
    CHECK_EQ(document.unit_at(sum), count);
    CHECK_EQ(document.unit_at(sum - 1), count - 1);
    CHECK_EQ(document.unit_at(-5 * PT), size_t(0));
}

TEST(a_quote_is_one_unit_and_a_change_to_one_of_its_blocks_lays_out_that_block)
{
    Fake fake;
    Document document;
    document.set_theme(DocumentTheme{20 * PT, 2 * PT});
    DocumentComposer composer;
    composer.measure = fake.measure();
    for (size_t i = 0; i < 30; ++i) {
        Block& block = document.edit(document.add_block());
        block.style = Harness::style(i % 4 == 1 ? 20 * PT : 0);
        block.quote_depth = i % 4 == 1 ? 1 : 0;
        block.space_below = 4 * PT;
        REQUIRE(append_text(block.text.run, "some quoted words", FontSpec{Face::PROSE, EM}, true,
                            Composition(), fake.resolver()));
    }
    // Blocks 5 to 7 are one quote of three.
    document.edit(6).quote_depth = 1;
    document.edit(7).quote_depth = 1;
    document.set_width(200 * PT);
    document.layout_all(composer);
    CHECK_EQ(composer.blocks_composed, uint64_t(30));
    const size_t quote = document.unit_of(6);
    CHECK_EQ(document.unit(quote).block_count, 3u);
    CHECK(document.unit(quote).barred);
    CHECK_EQ(document.unit(quote).bar_count, 1u);
    const Bar bar = document.bar(document.unit(quote).first_bar);
    CHECK_EQ(bar.extent, document.unit(quote).extent);
    CHECK_EQ(bar.x, Scaled(0));

    // One block of the three changes: one block is laid out, and the unit is
    // stacked again from the three layouts.
    const uint64_t before = composer.blocks_composed;
    const uint64_t stacked = composer.units_assembled;
    REQUIRE(append_text(document.edit(6).text.run,
                        " and enough more of them to need a second line of text",
                        FontSpec{Face::PROSE, EM}, true, Composition(), fake.resolver()));
    document.layout_all(composer);
    CHECK_EQ(composer.blocks_composed, before + 1);
    CHECK(composer.units_assembled > stacked);
    CHECK_EQ(document.unit(quote).extent, bar.extent + LEADING);
    CHECK_EQ(document.bar(document.unit(quote).first_bar).extent, bar.extent + LEADING);
    CHECK_EQ(document.block_top(7) - document.block_top(6), 2 * LEADING + 4 * PT);
}

// --- Reuse ---------------------------------------------------------------------------------

namespace
{

void fill(Document& document, Fake& fake)
{
    document.clear();
    const int32_t quote = document.add_quote(Quote{NO_CONTAINER, NO_CONTAINER, 0, 0});
    const int32_t list = document.add_list(List{NO_CONTAINER, 15 * PT, true});
    for (size_t i = 0; i < 60; ++i) {
        Block& block = document.edit(document.add_block());
        block.style = Harness::style(i % 5 == 0 ? 20 * PT : 15 * PT);
        block.space_above = 3 * PT;
        block.space_below = 3 * PT;
        const FontSpec font{Face::PROSE, EM};
        switch (i % 6) {
        case 0:
            block.type = BlockType::CODE;
            set_code(block.code, "int main()\n{\n\treturn 0; // and a comment that is long\n}\n",
                     FontSpec{Face::CODE, EM}, Composition(), fake.resolver());
            break;
        case 1:
            block.type = BlockType::RULE;
            block.thickness = PT;
            break;
        case 2:
        case 3:
            block.quote = quote;
            append_text(block.text.run, "quoted words, several of them, enough for two lines", font,
                        true, Composition(), fake.resolver());
            break;
        default:
            block.list = list;
            block.item = static_cast<uint32_t>(i);
            append_text(block.text.run, "an item of a list with a marker hung on it", font, true,
                        Composition(), fake.resolver());
            hard_break(block.text.run, block.style.align);
            append_text(block.text.run, "and a second line", font, true, Composition(),
                        fake.resolver());
            set_marker(block.text, i % 2 == 0 ? "1." : "100.", font, Composition(),
                       fake.resolver());
            break;
        }
    }
}

} // namespace

TEST(laying_out_again_allocates_nothing)
{
    Fake fake;
    Document document;
    document.set_rules(DocumentRules::product(EM));
    document.set_theme(DocumentTheme{20 * PT, 2 * PT});
    DocumentComposer composer;
    composer.measure = fake.measure();
    std::vector<DrawCmd> flat;
    const auto cycle = [&](bool refill) {
        for (const Scaled width : {180 * PT, 90 * PT, 260 * PT}) {
            if (refill)
                fill(document, fake);
            document.set_width(width);
            document.layout_window(composer, 100 * PT, 400 * PT);
            document.layout_all(composer);
            flat.clear();
            CHECK(document.flatten(flat));
        }
    };
    fill(document, fake);
    cycle(false);
    cycle(true);
    cycle(false);
    const size_t commands = flat.size();
    CHECK(commands > 1000);

    // The same widths again, on the same content.
    size_t before = g_allocations;
    const uint64_t composed = composer.blocks_composed;
    cycle(false);
    CHECK_EQ(g_allocations - before, size_t(0));
    CHECK_EQ(composer.blocks_composed - composed, uint64_t(3 * 60));
    CHECK_EQ(flat.size(), commands);

    // And with the content described afresh each time.
    before = g_allocations;
    cycle(true);
    CHECK_EQ(g_allocations - before, size_t(0));
    CHECK_EQ(flat.size(), commands);
}

// --- What a block says is data -------------------------------------------------------------

TEST(blocks_that_say_anything_at_all_are_laid_out_without_a_contract_being_broken)
{
    static int violations = 0;
    violations = 0;
    const ContractHandler previous = contract_handler();
    contract_handler() = [](const char*) { ++violations; };

    const Scaled wild[] = {0,
                           -1,
                           1,
                           MAX_DIMEN,
                           -MAX_DIMEN,
                           RUNNING,
                           SCALED_MAX,
                           -SCALED_MAX,
                           std::numeric_limits<int64_t>::min(),
                           Scaled(1) << 40,
                           -(Scaled(1) << 40),
                           100 * PT};
    const size_t wilds = sizeof(wild) / sizeof(wild[0]);
    Random random{7};
    const auto any = [&]() { return wild[random.below(static_cast<uint32_t>(wilds))]; };
    for (int round = 0; round < 300; ++round) {
        CHECK_CONTEXT("round %d", round);
        Harness h;
        random_document(random, h, 1 + random.below(12));
        // Containers that name themselves, later ones, and nothing.
        for (Quote& quote : h.quotes) {
            if (random.chance(3))
                quote.parent = static_cast<int32_t>(random.next());
            quote.bar_x = random.chance(3) ? any() : quote.bar_x;
            quote.list = random.chance(4) ? static_cast<int32_t>(random.next()) : quote.list;
        }
        for (List& list : h.lists) {
            if (random.chance(3))
                list.parent = static_cast<int32_t>(random.next());
            list.indent = random.chance(3) ? any() : list.indent;
        }
        for (Block& block : h.blocks) {
            if (random.chance(2))
                block.style.left_indent = any();
            if (random.chance(3))
                block.style.first_line_indent = any();
            if (random.chance(3))
                block.style.marker_gap = any();
            if (random.chance(3))
                block.space_above = any();
            if (random.chance(3))
                block.space_below = any();
            if (random.chance(3))
                block.thickness = any();
            if (random.chance(3))
                block.quote_depth = random.next();
            if (random.chance(3))
                block.quote = static_cast<int32_t>(random.next());
            if (random.chance(3))
                block.list = static_cast<int32_t>(random.next());
            if (random.chance(4))
                block.style.strut = Strut{any(), any()};
            if (random.chance(4))
                block.style.interline.baselineskip.width = any();
            if (random.chance(6))
                block.type = static_cast<BlockType>(random.below(3));
        }
        h.theme.quote_indent = random.chance(2) ? any() : h.theme.quote_indent;
        h.theme.quote_rule_thickness = random.chance(2) ? any() : h.theme.quote_rule_thickness;
        DocumentRules rules = random_rules(random);
        if (random.chance(3))
            rules.min_measure = any();
        if (random.chance(3))
            rules.code_continuation_indent = any();
        const Scaled width = random.chance(2) ? any() : static_cast<Scaled>(random.below(300)) * PT;

        // Composed both ways. What comes out need not be layout, and with
        // dimensions outside MAX_DIMEN the two need not agree to the last
        // unit; it has to be defined, and to end.
        h.arena.clear();
        h.composer.compose_blocks(h.arena, h.blocks.data(), h.blocks.size(), h.structure(), h.theme,
                                  rules, width, h.composed);
        h.cmds.clear();
        ship(h.arena, h.composed.box, Point{0, h.composed.packed.box.height}, h.composer.measure,
             h.cmds);
        h.document.clear();
        h.document.set_rules(rules);
        h.document.set_theme(h.theme);
        for (const Quote& quote : h.quotes)
            h.document.add_quote(quote);
        for (const List& list : h.lists)
            h.document.add_list(list);
        for (const Block& block : h.blocks)
            h.document.edit(h.document.add_block()) = block;
        h.document.set_width(width);
        h.document.layout_window(h.composer, any(), any());
        h.document.layout_all(h.composer);
        CHECK(h.document.settled());
        h.flat.clear();
        h.document.flatten(h.flat, &h.spans);
        // Gaps may be negative, so a height may be; it is still the units' sum.
        Scaled sum = 0;
        for (size_t u = 0; u < h.document.unit_count(); ++u)
            sum += h.document.unit(u).gap + h.document.unit(u).extent;
        CHECK_EQ(h.document.height(), sum);
        h.document.unit_at(any());
    }
    CHECK_EQ(violations, 0);

    // Quotes nested past the limit are stacked at the limit.
    {
        Harness h;
        int32_t parent = NO_CONTAINER;
        for (uint32_t q = 0; q < 3 * MAX_QUOTE_DEPTH; ++q) {
            h.quotes.push_back(Quote{parent, NO_CONTAINER, static_cast<Scaled>(q) * PT, 0});
            parent = static_cast<int32_t>(q);
        }
        h.text("deep").quote = parent;
        h.text("deeper by number").quote_depth = 4000000000u;
        h.compose(DocumentRules::product(EM), 300 * PT);
        CHECK_EQ(h.bars().size(), size_t(2 * MAX_QUOTE_DEPTH));
        CHECK(h.composed.ok());
    }
    CHECK_EQ(violations, 0);
    contract_handler() = previous;
}

TEST(a_document_of_no_blocks_is_empty_and_settled)
{
    Fake fake;
    Document document;
    DocumentComposer composer;
    composer.measure = fake.measure();
    document.set_width(100 * PT);
    const UnitRange range = document.layout_window(composer, 0, 100 * PT);
    CHECK_EQ(range.first, size_t(0));
    CHECK_EQ(range.end, size_t(0));
    CHECK(document.settled());
    CHECK_EQ(document.height(), Scaled(0));
    std::vector<DrawCmd> flat;
    CHECK(document.flatten(flat));
    CHECK(flat.empty());

    NodeArena arena;
    Composed composed;
    CHECK(composer.compose_blocks(arena, nullptr, 0, Structure(), DocumentTheme(), DocumentRules(),
                                  100 * PT, composed));
    CHECK_EQ(composed.packed.box.height, Scaled(0));
    CHECK(composed.stacked.vlist.empty());
}
