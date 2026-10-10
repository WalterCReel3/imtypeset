// src/paragraph against the reference: every case of tests/golden/para replayed
// stage by stage -- material, marker, skips and shape, the list handed to the
// breaker, the broken lines, the hung marker, the strutted lines, the vertical
// list, the vbox and the shipped commands -- twice: once answering metrics
// from the case's own records, where a question the reference did not ask is a
// failure, and once with the real font reader behind it. Then what only the
// port has: the map from material to shipped indices, the first-line indent as
// tex.web has it, reuse without allocation, and what happens without a measure.

#include "core_golden.h"

#include "fontlib.h"
#include "linebreak.h"
#include "paragraph.h"
#include "shipout.h"
#include "style.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

using namespace ImTypeset::detail;

// Counts every allocation the test executable makes, so that a test can show
// that warmed-up composition makes none. Not inlined: GCC, seeing through an
// inlined replacement to malloc and free, reports the pair as mismatched.
#if defined(__GNUC__)
#define TEST_PARAGRAPH_NOINLINE __attribute__((noinline))
#else
#define TEST_PARAGRAPH_NOINLINE
#endif

static size_t g_allocations = 0;

TEST_PARAGRAPH_NOINLINE void* operator new(size_t size)
{
    ++g_allocations;
    void* const memory = std::malloc(size != 0 ? size : 1);
    if (memory == nullptr)
        std::abort();
    return memory;
}

TEST_PARAGRAPH_NOINLINE void operator delete(void* memory) noexcept
{
    std::free(memory);
}

TEST_PARAGRAPH_NOINLINE void operator delete(void* memory, size_t) noexcept
{
    std::free(memory);
}

// Every other form the library or the standard library reaches for
// (std::stable_sort's buffer is a nothrow new), so that no block is allocated
// by one family and freed by another.
TEST_PARAGRAPH_NOINLINE void* operator new(size_t size, const std::nothrow_t&) noexcept
{
    ++g_allocations;
    return std::malloc(size != 0 ? size : 1);
}

TEST_PARAGRAPH_NOINLINE void* operator new[](size_t size)
{
    return operator new(size);
}

TEST_PARAGRAPH_NOINLINE void* operator new[](size_t size, const std::nothrow_t& tag) noexcept
{
    return operator new(size, tag);
}

TEST_PARAGRAPH_NOINLINE void operator delete(void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}

TEST_PARAGRAPH_NOINLINE void operator delete[](void* memory) noexcept
{
    std::free(memory);
}

TEST_PARAGRAPH_NOINLINE void operator delete[](void* memory, size_t) noexcept
{
    std::free(memory);
}

TEST_PARAGRAPH_NOINLINE void operator delete[](void* memory, const std::nothrow_t&) noexcept
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

bool spacing_of(std::string_view token, Spacing* out)
{
    if (token == "nonfrench")
        *out = Spacing::NONFRENCH;
    else if (token == "french")
        *out = Spacing::FRENCH;
    else if (token == "extra_space_only")
        *out = Spacing::EXTRA_SPACE_ONLY;
    else
        return false;
    return true;
}

bool align_of(std::string_view token, Align* out)
{
    if (token == "justify")
        *out = Align::JUSTIFY;
    else if (token == "left")
        *out = Align::LEFT;
    else if (token == "center")
        *out = Align::CENTER;
    else if (token == "right")
        *out = Align::RIGHT;
    else
        return false;
    return true;
}

// A font key as the goldens spell it, which must be one the port can hold.
FontSpec spec_of(const std::string& key)
{
    FontSpec spec;
    if (!parse_font_key_text(key, &spec))
        FAIL("'%s' is not a font key", key.c_str());
    return spec;
}

Face face_of(const std::string& name)
{
    return spec_of(name + "@0").face;
}

// --- Metrics answered from a case's records ----------------------------------------

struct Recorded;

// One face at one size, answering only what the reference was asked.
class RecordedMetrics : public FontMetrics
{
public:
    RecordedMetrics(Recorded& owner, FontKey key)
        : _owner(&owner)
        , _key(key)
    {
    }
    Face face() const override;
    double size() const override { return unasked("size"); }
    double ascent() const override { return unasked("ascent"); }
    double descent() const override { return unasked("descent"); }
    double x_height() const override { return unasked("x_height"); }
    double em() const override { return unasked("em"); }
    double space_width() const override;
    bool has(uint32_t codepoint) const override;
    double advance(uint32_t) const override { return unasked("advance"); }
    GlyphBBox bbox(uint32_t) const override
    {
        unasked("bbox");
        return GlyphBBox();
    }
    double kern(uint32_t codepoint, uint32_t following) const override;

private:
    double unasked(const char* what) const;

    Recorded* _owner;
    FontKey _key;
};

// The `space_width`, `has` and `kern` records of a case. `misses` counts the
// questions it has no record for: the port asking what the reference did not.
struct Recorded {
    std::map<uint64_t, double> space_widths;
    std::map<std::pair<uint64_t, uint32_t>, bool> has;
    std::map<std::tuple<uint64_t, uint32_t, uint32_t>, double> kerns;
    std::map<uint64_t, std::unique_ptr<RecordedMetrics>> faces;
    int misses = 0;
    int asked = 0;

    void miss(const char* what, FontKey key, uint32_t a = 0, uint32_t b = 0)
    {
        ++misses;
        FAIL("the port asked %s of %s (U+%04X, U+%04X), which the reference did not", what,
             font_key_text(static_cast<Face>(key.face()), key.size()).c_str(), a, b);
    }

    static const FontMetrics* resolve(void* user, FontKey key)
    {
        Recorded& self = *static_cast<Recorded*>(user);
        FontSpec spec;
        if (!parse_font_key(key, &spec))
            return nullptr;
        std::unique_ptr<RecordedMetrics>& slot = self.faces[key.bits];
        if (!slot)
            slot.reset(new RecordedMetrics(self, key));
        return slot.get();
    }

    MetricsResolver resolver() { return MetricsResolver{&resolve, this}; }

    // Reads the run of `space_width`, `has` and `kern` records at the reader.
    void read(core_golden::Reader& reader, core_golden::Context& context)
    {
        for (;;) {
            if (reader.next_is("space_width")) {
                golden::Cursor cursor(reader.take());
                const FontKey key = context.faces.key(cursor.str());
                space_widths[key.bits] = cursor.f64();
                cursor.expect_done();
            } else if (reader.next_is("has")) {
                golden::Cursor cursor(reader.take());
                const FontKey key = context.faces.key(cursor.str());
                const uint32_t codepoint = cursor.u32();
                has[{key.bits, codepoint}] = cursor.boolean();
                cursor.expect_done();
            } else if (reader.next_is("kern")) {
                golden::Cursor cursor(reader.take());
                const FontKey key = context.faces.key(cursor.str());
                const uint32_t codepoint = cursor.u32();
                const uint32_t following = cursor.u32();
                kerns[std::make_tuple(key.bits, codepoint, following)] = cursor.f64();
                cursor.expect_done();
            } else {
                return;
            }
        }
    }
};

Face RecordedMetrics::face() const
{
    return static_cast<Face>(_key.face());
}

double RecordedMetrics::unasked(const char* what) const
{
    _owner->miss(what, _key);
    return 0.0;
}

double RecordedMetrics::space_width() const
{
    ++_owner->asked;
    const auto found = _owner->space_widths.find(_key.bits);
    if (found == _owner->space_widths.end()) {
        _owner->miss("space_width", _key);
        return 0.0;
    }
    return found->second;
}

bool RecordedMetrics::has(uint32_t codepoint) const
{
    ++_owner->asked;
    const auto found = _owner->has.find({_key.bits, codepoint});
    if (found == _owner->has.end()) {
        _owner->miss("has", _key, codepoint);
        return false;
    }
    return found->second;
}

double RecordedMetrics::kern(uint32_t codepoint, uint32_t following) const
{
    ++_owner->asked;
    const auto found = _owner->kerns.find(std::make_tuple(_key.bits, codepoint, following));
    if (found == _owner->kerns.end()) {
        _owner->miss("kern", _key, codepoint, following);
        return 0.0;
    }
    return found->second;
}

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

    CharMeasure measure() { return CharMeasure{&TableMeasure::answer, this}; }
};

// --- Writing what the port produced as the oracle writes it ------------------------

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

bool same_params(const core_golden::Params& p, const LineBreakParams& q)
{
    return p.pretolerance == q.pretolerance && p.tolerance == q.tolerance &&
           p.line_penalty == q.line_penalty && p.adj_demerits == q.adj_demerits &&
           p.double_hyphen_demerits == q.double_hyphen_demerits &&
           p.final_hyphen_demerits == q.final_hyphen_demerits &&
           p.hyphen_penalty == q.hyphen_penalty && p.ex_hyphen_penalty == q.ex_hyphen_penalty &&
           p.emergency_stretch == q.emergency_stretch && p.first_fit == q.first_fit &&
           p.left_skip == q.left_skip && p.right_skip == q.right_skip &&
           p.par_fill_skip == q.par_fill_skip;
}

bool same_shape(const core_golden::Shape& expected, const BlockShape& actual)
{
    if (expected.lines.size() != actual.count)
        return false;
    for (size_t i = 0; i < actual.count; ++i) {
        if (expected.lines[i].width != actual.lines[i].width ||
            expected.lines[i].indent != actual.lines[i].indent)
            return false;
    }
    return true;
}

// --- The map from material to shipped indices ----------------------------------------

// What `shipped`, `originals` and `hard_breaks` promise, held against the
// commands that were actually shipped. Returns how many nodes were mapped.
size_t check_map(const TextBlock& block, const ParagraphLayout& layout,
                 const std::vector<DrawCmd>& cmds)
{
    const std::vector<Node>& material = block.run.nodes;
    CHECK(layout.mapped);
    if (!CHECK(layout.shipped.size() == material.size()))
        return 0;

    // The item command at each index: the first that is not a LINE, which for
    // an atom is the ATOM and not the glyphs inside it.
    std::vector<const DrawCmd*> items;
    uint32_t end = 0;
    for (const DrawCmd& cmd : cmds) {
        if (cmd.is(CmdType::LINE)) {
            if (cmd.end > end)
                end = cmd.end;
            continue;
        }
        if (cmd.is(CmdType::RULE_RECT))
            continue;
        if (cmd.index >= items.size())
            items.resize(cmd.index + 1, nullptr);
        if (items[cmd.index] == nullptr)
            items[cmd.index] = &cmd;
    }
    CHECK_EQ(layout.item_count, end);
    CHECK_EQ(items.size(), static_cast<size_t>(end));

    size_t mapped = 0;
    size_t replaced = 0;
    bool increasing = true;
    uint32_t previous = 0;
    for (size_t m = 0; m < material.size(); ++m) {
        const Node& node = material[m];
        const uint32_t index = layout.shipped[m];
        if (index == NOT_SHIPPED) {
            // A character is never discarded; a kern and a penalty never ship.
            CHECK(!node.is(NodeType::CHAR));
            continue;
        }
        CHECK_CONTEXT("material node %zu, index %u", m, index);
        if (mapped != 0 && index <= previous)
            increasing = false;
        previous = index;
        ++mapped;
        if (index < layout.marker_items || index >= items.size() || items[index] == nullptr) {
            FAIL("no item was shipped at this index");
            continue;
        }
        const DrawCmd& cmd = *items[index];
        if (node.is(NodeType::CHAR)) {
            CHECK(cmd.is(CmdType::GLYPH));
            if (cmd.is(CmdType::GLYPH)) {
                CHECK(cmd.font() == node.font());
                CHECK_EQ(cmd.codepoint, node.codepoint());
            }
            if (node.is_replaced())
                ++replaced;
        } else if (node.is(NodeType::GLUE)) {
            CHECK(cmd.is(CmdType::SET_GLUE));
            if (cmd.is(CmdType::SET_GLUE))
                CHECK_EQ(cmd.natural(), node.width());
        } else {
            CHECK(node.is(NodeType::BOX));
        }
    }
    CHECK(increasing);

    CHECK_EQ(layout.originals.size(), replaced);
    size_t at = 0;
    for (size_t m = 0; m < material.size() && at < layout.originals.size(); ++m) {
        if (!material[m].is_replaced())
            continue;
        CHECK_EQ(layout.originals[at].index, layout.shipped[m]);
        CHECK_EQ(layout.originals[at].codepoint, material[m].original());
        ++at;
    }

    // Every forced break but one that ends the material names an item, and
    // what it names is the first mapped node after the penalty.
    size_t forced = 0;
    std::set<uint32_t> after;
    for (size_t m = 0; m + 1 < material.size(); ++m) {
        if (!material[m].is(NodeType::PENALTY) || !forces_break(material[m].penalty()))
            continue;
        ++forced;
        for (size_t later = m + 1; later < material.size(); ++later) {
            if (layout.shipped[later] != NOT_SHIPPED) {
                after.insert(layout.shipped[later]);
                break;
            }
        }
    }
    CHECK(forced == 0 || !after.empty() || layout.hard_breaks.empty());
    CHECK(std::vector<uint32_t>(after.begin(), after.end()) == layout.hard_breaks);
    return mapped;
}

// --- Replaying a case -------------------------------------------------------------------

struct Event {
    bool hard_break = false;
    FontSpec font;
    bool breakable = true;
    std::string text;
};

struct Tally {
    int cases = 0;
    int runs = 0;
    int lines = 0;
    int hung = 0;
    int first_fit = 0;
    int total_fit = 0;
    int indented = 0;
    int hard_breaks = 0;
    int originals = 0;
    int from_text = 0;
    size_t mapped = 0;
    size_t glyphs = 0;
    int asked = 0;
    int nonzero_interline = 0;
    std::set<std::string> names;
};

// Replays one `para` case. With `library` null the metrics are the case's own
// records; otherwise the real font reader answers.
void replay_case(const golden::File& file, const core_golden::Case& c, FontLibrary* library,
                 ParagraphComposer& composer, ParagraphLayout& layout, Tally& tally)
{
    core_golden::Context context;
    core_golden::Reader reader(file, c);

    // --- INPUT
    const golden::Record& source = reader.take("source");
    const std::string source_kind(source.raw(0));
    const std::string source_text = source.str(1);

    Composition composition;
    BlockStyle style;
    {
        golden::Cursor block(reader.take("block"));
        block.f64(); // px per inch: already in the widths
        REQUIRE(spacing_of(block.raw(), &composition.spacing));
        REQUIRE(align_of(block.raw(), &style.align));
        block.raw(); // block kind: which style the reference looked up
        block.str(); // the style's own font: the runs carry theirs
        style.left_indent = block.i64();
        style.first_line_indent = block.i64();
        style.marker_gap = block.i64();
        block.expect_done();
    }
    {
        golden::Cursor faces(reader.take("faces"));
        composition.regular = face_of(faces.str());
        CHECK(face_of(faces.str()) == Face::MATH);
        composition.code = face_of(faces.str());
        faces.expect_done();
    }
    const core_golden::Skip skip = core_golden::read_skip(reader);
    style.interline.baselineskip = skip.baselineskip;
    style.interline.lineskip = skip.lineskip;
    style.interline.lineskiplimit = skip.lineskiplimit;
    {
        golden::Cursor strut(reader.take("strut"));
        style.strut.height = strut.i64();
        style.strut.depth = strut.i64();
        strut.expect_done();
        // The reference's strut is always the one that fills the leading.
        const Strut filling = Strut::filling(style.interline.baselineskip.width);
        CHECK_EQ(filling.height, style.strut.height);
        CHECK_EQ(filling.depth, style.strut.depth);
    }

    std::vector<Event> events;
    {
        const uint32_t count = reader.take("runs").u32(0);
        while (!reader.next_is("endruns") && !reader.done()) {
            Event event;
            if (reader.next_is("hardbreak")) {
                reader.take();
                event.hard_break = true;
            } else {
                golden::Cursor run(reader.take("run"));
                event.font = spec_of(run.str());
                event.breakable = run.boolean();
                event.text = run.str();
                run.expect_done();
            }
            events.push_back(event);
        }
        reader.take("endruns");
        // The count is of records, hard breaks included.
        CHECK_EQ(events.size(), static_cast<size_t>(count));
    }
    bool has_marker = false;
    FontSpec marker_font;
    std::string marker_text;
    if (reader.next_is("marker")) {
        golden::Cursor marker(reader.take());
        marker_font = spec_of(marker.str());
        marker_text = marker.str();
        marker.expect_done();
        has_marker = true;
    }

    Recorded recorded;
    recorded.read(reader, context);
    const core_golden::MeasureTable table = core_golden::read_measure(reader, context);
    TableMeasure table_measure{&table};

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

    // --- Material
    TextBlock block;
    std::string why;
    for (const Event& event : events) {
        if (event.hard_break) {
            hard_break(block.run, style.align);
        } else if (!append_text(block.run, event.text, event.font, event.breakable, composition,
                                resolve, &why)) {
            FAIL("refused: %s", why.c_str());
            return;
        }
    }
    if (has_marker && !set_marker(block, marker_text, marker_font, composition, resolve, &why)) {
        FAIL("marker refused: %s", why.c_str());
        return;
    }
    // A block given as plain text is the same material built from the text.
    // Only with the font reader: the recorded answers hold what one building
    // of the block asked.
    if (library != nullptr && (source_kind == "text" || source_kind == "text-hard-breaks")) {
        REQUIRE(!events.empty());
        Run from_text;
        REQUIRE(append_text_block(from_text, source_text, events[0].font,
                                  source_kind == "text-hard-breaks", style.align, composition,
                                  resolve, &why));
        CHECK(from_text.nodes == block.run.nodes);
        ++tally.from_text;
    }
    {
        std::string name;
        const size_t first = reader.position();
        core_golden::read_nodes(reader, context, &name);
        REQUIRE_EQ(name, std::string("material"));
        std::vector<Node> material;
        if (style.first_line_indent != 0) {
            Box indent;
            indent.width = style.first_line_indent;
            material.push_back(context.arena.add_box(indent));
            ++tally.indented;
        }
        material.insert(material.end(), block.run.nodes.begin(), block.run.nodes.end());
        std::vector<std::string> actual;
        core_golden::write_nodes(context, "material", material, actual);
        if (!CHECK_LINES_EQ(reader.lines(first, reader.position()), actual, "material"))
            return;
    }
    if (has_marker) {
        std::string name;
        const size_t first = reader.position();
        core_golden::read_nodes(reader, context, &name);
        REQUIRE_EQ(name, std::string("marker"));
        std::vector<std::string> actual;
        core_golden::write_nodes(context, "marker", block.marker.nodes, actual);
        if (!CHECK_LINES_EQ(reader.lines(first, reader.position()), actual, "marker"))
            return;
    }

    // --- One composition per width
    std::vector<std::string> unshrinkable_lines;
    bool unshrinkable_pending = false;
    std::vector<DrawCmd> cmds;
    while (!reader.done()) {
        if (reader.next_is("nodes")) {
            std::string name;
            const size_t first = reader.position();
            core_golden::read_nodes(reader, context, &name);
            REQUIRE_EQ(name, std::string("unshrinkable"));
            unshrinkable_lines = reader.lines(first, reader.position());
            unshrinkable_pending = true;
            continue;
        }
        const golden::Record& open = reader.take("break");
        const std::string run = open.str(0);
        const std::string list(open.raw(1));
        CHECK_CONTEXT("run %s (line %d)", run.c_str(), open.line);

        // The width is on the `assemble` record that follows the group.
        Scaled width = 0;
        bool found = false;
        for (size_t i = reader.position(); i < reader.end() && !found; ++i) {
            const golden::Record& record = file.records()[i];
            if (record.is("assemble")) {
                REQUIRE_EQ(record.str(0), run);
                width = record.i64(1);
                found = true;
            }
        }
        REQUIRE(found);

        const bool ok =
            composer.compose(context.arena, block, style, width, prose_breaking(), measure, layout);
        CHECK(ok);
        CHECK(layout.ok());

        // Skips, shape, and the list that was broken.
        CHECK(same_shape(core_golden::read_shape(reader),
                         shape(width, style.left_indent, style.first_line_indent)));
        const core_golden::Params params = core_golden::read_params(reader);
        CHECK(same_params(params, composer.line_params()));
        CHECK(same_params(params, skips(style.align, [&] {
                              LineBreakParams p = prose_breaking();
                              p.first_fit = style.align != Align::JUSTIFY;
                              return p;
                          }())));
        CHECK_EQ(list == "unshrinkable", composer.line_params().first_fit);
        CHECK_EQ(list == "unshrinkable", style.align != Align::JUSTIFY);
        if (unshrinkable_pending) {
            std::vector<std::string> actual;
            core_golden::write_nodes(context, "unshrinkable", composer.broken_material(), actual);
            CHECK_LINES_EQ(unshrinkable_lines, actual, "unshrinkable");
            unshrinkable_pending = false;
        }
        ++(composer.line_params().first_fit ? tally.first_fit : tally.total_fit);

        // The broken lines.
        {
            const size_t first = reader.position();
            const core_golden::ParagraphResult expected = core_golden::read_paragraph(reader);
            for (int64_t i = 0; i < expected.line_count && !reader.done(); ++i)
                core_golden::read_packed(reader, context);
            CHECK_LINES_EQ(reader.lines(first, reader.position()), written(context, layout.broken),
                           "the broken paragraph");
        }
        reader.take("endbreak");

        // Marker, strut, stack, ship.
        reader.take("assemble");
        if (reader.next_is("hung")) {
            reader.take();
            const size_t first = reader.position();
            core_golden::read_node(reader, context);
            CHECK(has_marker);
            CHECK_LINES_EQ(reader.lines(first, reader.position()),
                           core_golden::node_lines(context, layout.hung), "the hung line");
            ++tally.hung;
        } else {
            CHECK(!has_marker);
            CHECK(!layout.hung.is(NodeType::BOX));
        }
        {
            const uint32_t count = reader.take("strutted").u32(0);
            CHECK_EQ(layout.lines.size(), static_cast<size_t>(count));
            CHECK_EQ(layout.lines.size(), layout.broken.lines.size());
            for (uint32_t i = 0; i < count; ++i) {
                golden::Cursor dims(reader.take("dims"));
                if (i >= layout.lines.size())
                    continue;
                CHECK_CONTEXT("line %u", i);
                const Box& box = context.arena.box(layout.lines[i]);
                CHECK_EQ(box.width, dims.i64());
                CHECK_EQ(box.height, dims.i64());
                CHECK_EQ(box.depth, dims.i64());
                CHECK_EQ(box.shift_amount, dims.i64());
                dims.expect_done();
                // The strut changes two numbers and nothing else.
                const Node& from = i == 0 && has_marker ? layout.hung : layout.broken.lines[i].box;
                const Box& before = context.arena.box(from);
                CHECK(box.contents == before.contents);
                CHECK(box.kind == before.kind && box.glue_sign == before.glue_sign &&
                      box.glue_order == before.glue_order && box.glue_set == before.glue_set);
            }
        }
        {
            const uint32_t count = reader.take("vlist").u32(0);
            CHECK_EQ(layout.vlist.size(), static_cast<size_t>(count));
            for (uint32_t i = 0; i < count && !reader.next_is("endvlist"); ++i) {
                CHECK_CONTEXT("vlist item %u", i);
                if (reader.next_is("ref")) {
                    const uint32_t line = reader.take().u32(0);
                    if (i < layout.vlist.size() && line < layout.lines.size())
                        CHECK(layout.vlist[i] == layout.lines[line]);
                    continue;
                }
                const size_t first = reader.position();
                const Node expected = core_golden::read_node(reader, context);
                if (expected.is(NodeType::GLUE) && expected.spec() != GlueSpec())
                    ++tally.nonzero_interline;
                if (i < layout.vlist.size())
                    CHECK_LINES_EQ(reader.lines(first, reader.position()),
                                   core_golden::node_lines(context, layout.vlist[i]),
                                   "a vlist item");
            }
            reader.take("endvlist");
        }
        {
            golden::Cursor vbox(reader.take("vbox"));
            const Box& box = context.arena.box(layout.box);
            CHECK(box.kind == BoxKind::VLIST);
            CHECK_EQ(box.width, vbox.i64());
            CHECK_EQ(box.height, vbox.i64());
            CHECK_EQ(box.depth, vbox.i64());
            CHECK_EQ(layout.packed.box.height, box.height);
            golden::Cursor origin(reader.take("origin"));
            CHECK_EQ(origin.i64(), 0);
            CHECK_EQ(origin.i64(), box.height);
        }
        {
            const size_t first = reader.position();
            core_golden::read_cmds(reader, context);
            cmds.clear();
            CHECK(ship(context.arena, layout, Point{0, 0}, measure, cmds) == ShipError::NONE);
            CHECK_LINES_EQ(reader.lines(first, reader.position()), written(context, cmds),
                           "the shipped commands");
        }
        reader.take("endassemble");

        tally.mapped += check_map(block, layout, cmds);
        for (const DrawCmd& cmd : cmds)
            tally.glyphs += cmd.is(CmdType::GLYPH) ? 1 : 0;
        tally.hard_breaks += static_cast<int>(layout.hard_breaks.size());
        tally.originals += static_cast<int>(layout.originals.size());
        tally.lines += static_cast<int>(layout.lines.size());
        ++tally.runs;
    }
    if (library == nullptr) {
        CHECK_EQ(recorded.misses, 0);
        CHECK_EQ(table_measure.misses, 0);
        tally.asked += recorded.asked;
    }
    tally.names.insert(c.name);
    ++tally.cases;
}

const char* const PARA_FILES[] = {"para/edge.golden",    "para/whitespace.golden",
                                  "para/standin.golden", "para/runs.golden",
                                  "para/prose.golden",   "para/sample.golden"};

void replay_all(FontLibrary* library, Tally& tally)
{
    ParagraphComposer composer;
    ParagraphLayout layout;
    for (const char* name : PARA_FILES) {
        CHECK_CONTEXT("%s", name);
        golden::File file;
        if (!CHECK(file.load(golden::path(name))))
            continue;
        for (const core_golden::Case& c : core_golden::cases(file)) {
            CHECK_CONTEXT("case %s (line %d)", c.name.c_str(), c.line);
            if (!CHECK(c.kind == "para"))
                continue;
            replay_case(file, c, library, composer, layout, tally);
        }
    }
}

void check_tally(const Tally& tally)
{
    // What the goldens hold; a case that stops being replayed must say so.
    CHECK_EQ(tally.cases, 243);
    CHECK_EQ(tally.runs, 710);
    CHECK_EQ(tally.lines, 4902);
    CHECK_EQ(tally.hung, 16);
    CHECK_EQ(tally.indented, 6);
    CHECK_EQ(tally.total_fit, 250);
    CHECK_EQ(tally.first_fit, 460);
    std::printf("    para replay: %d cases, %d widths, %d lines, %zu glyphs, %zu nodes mapped, "
                "%d hard breaks, %d originals, %d non-zero interline glues, %d metric answers\n",
                tally.cases, tally.runs, tally.lines, tally.glyphs, tally.mapped, tally.hard_breaks,
                tally.originals, tally.nonzero_interline, tally.asked);
    CHECK(tally.hard_breaks > 0);
    CHECK(tally.originals > 0);
    CHECK(tally.nonzero_interline > 0);
    CHECK(tally.mapped > 100000);
    for (const char* needed :
         {"marker-on-empty-text", "ordered-item-marker-wider-than-the-indent",
          "first-line-indent-justify", "first-line-indent-wider-than-the-column",
          "line-taller-than-the-leading-justify", "hard-breaks-after-space-center",
          "space-at-run-boundaries", "no-runs", "code-run-longer-than-the-line"}) {
        CHECK_CONTEXT("%s", needed);
        CHECK(tally.names.count(needed) == 1);
    }
}

std::string utf8(uint32_t codepoint)
{
    std::string out;
    if (codepoint < 0x80) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    return out;
}

// A block style at 10 pt on 14 pt, as a host would make one.
BlockStyle plain_style(Align align)
{
    BlockStyle style;
    style.align = align;
    style.interline.baselineskip = GlueSpec{14 * PT};
    style.interline.lineskip = GlueSpec{1 * PT};
    style.strut = Strut::filling(14 * PT);
    return style;
}

const FontSpec PROSE_10{Face::PROSE, 10 * PT};

// What the hand-written tests share: the real fonts, a block, and the buffers.
struct Bench {
    FontLibrary library;
    MetricsResolver resolve;
    CharMeasureCache cache;
    Composition composition;
    NodeArena arena;
    TextBlock block;
    ParagraphComposer composer;
    ParagraphLayout layout;
    std::vector<DrawCmd> cmds;

    Bench()
        : resolve((register_defaults(library), fontlib_metrics(library)))
        , cache(resolve)
    {
    }

    bool text(std::string_view words, FontSpec font = PROSE_10, bool breakable = true)
    {
        return append_text(block.run, words, font, breakable, composition, resolve);
    }

    bool compose(const BlockStyle& style, Scaled width)
    {
        const bool ok =
            composer.compose(arena, block, style, width, prose_breaking(), cache.measure(), layout);
        cmds.clear();
        CHECK(ship(arena, layout, Point{0, 0}, cache.measure(), cmds) == ShipError::NONE);
        return ok;
    }

    // The text the glyphs of each line spell, lines joined by '|'.
    std::string spelled() const
    {
        std::string out;
        bool first = true;
        for (const DrawCmd& cmd : cmds) {
            if (cmd.is(CmdType::LINE)) {
                if (!first)
                    out.push_back('|');
                first = false;
            } else if (cmd.is(CmdType::GLYPH)) {
                out += utf8(cmd.codepoint);
            } else if (cmd.is(CmdType::SET_GLUE) && cmd.natural() != 0) {
                out.push_back(' ');
            }
        }
        return out;
    }
};

} // namespace

// --- Against the reference -------------------------------------------------------------

TEST(every_stage_matches_the_reference_from_recorded_metrics)
{
    Tally tally;
    replay_all(nullptr, tally);
    check_tally(tally);
    CHECK(tally.asked > 10000);
}

TEST(every_stage_matches_the_reference_with_the_font_reader)
{
    FontLibrary library;
    register_defaults(library);
    Tally tally;
    replay_all(&library, tally);
    check_tally(tally);
    CHECK_EQ(tally.from_text, 172);
}

TEST(the_small_pieces_match_the_reference)
{
    golden::File file;
    REQUIRE(file.load(golden::path("para/tables.golden")));
    int checked[6] = {0, 0, 0, 0, 0, 0};
    for (const core_golden::Case& c : core_golden::cases(file, "para/tables")) {
        CHECK_CONTEXT("case %s (line %d)", c.name.c_str(), c.line);
        core_golden::Context context;
        core_golden::Reader reader(file, c);
        if (c.name == "constants") {
            while (reader.next_is("const")) {
                const golden::Record& r = reader.take();
                const std::string name = r.str(0);
                if (name == "inf_penalty")
                    CHECK_EQ(r.i64(2), INF_PENALTY);
                else if (name == "eject_penalty")
                    CHECK_EQ(r.i64(2), EJECT_PENALTY);
                else if (name == "unity")
                    CHECK_EQ(r.i64(2), UNITY);
                else if (name == "strut_height_fraction")
                    CHECK_EQ(r.f64(2), STRUT_HEIGHT_FRACTION);
                else if (name == "replacement_character")
                    CHECK_EQ(r.i64(2), static_cast<int64_t>(REPLACEMENT_CHARACTER));
            }
            CHECK(core_golden::read_spec(reader, "ragged_right") == RAGGED_RIGHT);
            CHECK(core_golden::read_spec(reader, "hard_break_fill") == PARFILLSKIP);
            // PROSE_BREAKING alone: the record, without the skips that follow
            // it in a `break` group.
            const LineBreakParams prose = prose_breaking();
            golden::Cursor params(reader.take("params"));
            CHECK_EQ(params.i64(), prose.pretolerance);
            CHECK_EQ(params.i64(), prose.tolerance);
            CHECK_EQ(params.i64(), prose.line_penalty);
            CHECK_EQ(params.i64(), prose.adj_demerits);
            CHECK_EQ(params.i64(), prose.double_hyphen_demerits);
            CHECK_EQ(params.i64(), prose.final_hyphen_demerits);
            CHECK_EQ(params.i64(), prose.hyphen_penalty);
            CHECK_EQ(params.i64(), prose.ex_hyphen_penalty);
            CHECK_EQ(params.i64(), prose.emergency_stretch);
            CHECK_EQ(params.boolean(), prose.first_fit);
            params.expect_done();
            CHECK(reader.done());
            ++checked[0];
        } else if (c.name == "skips") {
            while (reader.next_is("skips")) {
                const golden::Record& r = reader.take();
                Align align = Align::JUSTIFY;
                REQUIRE(align_of(r.raw(0), &align));
                CHECK_EQ(r.boolean(1), align != Align::JUSTIFY);
                const LineBreakParams out = skips(align, prose_breaking());
                CHECK(core_golden::read_spec(reader, "left_skip") == out.left_skip);
                CHECK(core_golden::read_spec(reader, "right_skip") == out.right_skip);
                CHECK(core_golden::read_spec(reader, "par_fill_skip") == out.par_fill_skip);
                CHECK_EQ(out.tolerance, 1000);
                CHECK_EQ(out.emergency_stretch, 20 * PT);
                ++checked[1];
            }
        } else if (c.name == "fill-and-break") {
            while (reader.next_is("fill_and_break")) {
                Align align = Align::JUSTIFY;
                REQUIRE(align_of(reader.take().raw(0), &align));
                const size_t first = reader.position();
                core_golden::read_nodes(reader, context);
                // On a run that ends in a guarded space, which the break drops.
                Run run;
                run.nodes = {char_node(PROSE_10.key(), 'a'), penalty(INF_PENALTY),
                             glue(GlueSpec{5, 3, GlueOrder::NORMAL, 2}), glue(GlueSpec{4})};
                hard_break(run, align);
                REQUIRE(run.nodes.size() > 1);
                CHECK(run.nodes[0] == char_node(PROSE_10.key(), 'a'));
                std::vector<std::string> actual;
                core_golden::write_nodes(context, "fill",
                                         NodeView(run.nodes).sub(1, run.nodes.size()), actual);
                CHECK_LINES_EQ(reader.lines(first, reader.position()), actual, "fill");
                ++checked[2];
            }
        } else if (c.name == "shape") {
            while (reader.next_is("shape_for")) {
                const golden::Record& r = reader.take();
                CHECK_CONTEXT("line %d", r.line);
                CHECK(same_shape(core_golden::read_shape(reader),
                                 shape(r.i64(0), r.i64(1), r.i64(2))));
                ++checked[3];
            }
        } else if (c.name == "unshrinkable") {
            std::string name;
            const std::vector<Node> in = core_golden::read_nodes(reader, context, &name);
            CHECK_EQ(name, std::string("in"));
            const size_t first = reader.position();
            core_golden::read_nodes(reader, context);
            std::vector<Node> out;
            unshrinkable(in, out);
            std::vector<std::string> actual;
            core_golden::write_nodes(context, "out", out, actual);
            CHECK_LINES_EQ(reader.lines(first, reader.position()), actual, "out");
            ++checked[4];
        } else if (c.name == "strut") {
            while (!reader.done()) {
                const golden::Record& r = reader.take();
                CHECK_CONTEXT("line %d", r.line);
                if (r.is("strut_filling")) {
                    const Strut strut = Strut::filling(r.i64(0));
                    CHECK_EQ(strut.height, r.i64(1));
                    CHECK_EQ(strut.depth, r.i64(2));
                } else if (r.is("strut_applied")) {
                    Strut strut;
                    strut.height = r.i64(0);
                    strut.depth = r.i64(1);
                    Box box;
                    box.height = r.i64(2);
                    box.depth = r.i64(3);
                    strut.apply_to(box);
                    CHECK_EQ(box.height, r.i64(4));
                    CHECK_EQ(box.depth, r.i64(5));
                }
                ++checked[5];
            }
        }
    }
    CHECK_EQ(checked[0], 1);
    CHECK_EQ(checked[1], 4);
    CHECK_EQ(checked[2], 4);
    CHECK(checked[3] > 40);
    CHECK_EQ(checked[4], 1);
    CHECK(checked[5] > 50);
}

TEST(stand_ins_match_the_reference_and_ask_only_what_it_asked)
{
    golden::File file;
    REQUIRE(file.load(golden::path("para/tables.golden")));
    int cases = 0;
    int checked = 0;
    int replaced = 0;
    for (const core_golden::Case& c : core_golden::cases(file, "para/tables")) {
        if (c.kind != "standin")
            continue;
        CHECK_CONTEXT("case %s (line %d)", c.name.c_str(), c.line);
        core_golden::Context context;
        core_golden::Reader reader(file, c);
        Composition composition;
        golden::Cursor faces(reader.take("faces"));
        composition.regular = face_of(faces.str());
        CHECK(face_of(faces.str()) == Face::MATH);
        composition.code = face_of(faces.str());
        Recorded recorded;
        recorded.read(reader, context);
        while (reader.next_is("stand_in")) {
            const golden::Record& r = reader.take();
            CHECK_CONTEXT("line %d", r.line);
            const FontKey font = context.faces.key(r.str(0));
            const uint32_t codepoint = r.u32(1);
            const StandIn shown = stand_in(composition, recorded.resolver(), font, codepoint);
            CHECK(shown.font == context.faces.key(r.str(2)));
            CHECK_EQ(shown.codepoint, r.u32(3));
            replaced += shown.codepoint != codepoint ? 1 : 0;
            ++checked;
        }
        CHECK(reader.done());
        CHECK_EQ(recorded.misses, 0);
        ++cases;
    }
    CHECK_EQ(cases, 3);
    CHECK_EQ(checked, 828);
    CHECK(replaced > 0);
}

TEST(material_maps_to_shipped_indices_as_the_reference_finds_them)
{
    // The reference recovers the map by matching the material against the
    // commands it shipped (advtext.spans); the port derives it from the
    // breaks. tools/oracle/dump_para_map.py wrote what the reference found.
    FontLibrary library;
    register_defaults(library);
    const MetricsResolver resolve = fontlib_metrics(library);
    CharMeasureCache cache(resolve);
    const Composition composition;
    ParagraphComposer composer;
    ParagraphLayout layout;
    std::vector<DrawCmd> cmds;

    golden::File file;
    REQUIRE(file.load(golden::path("para-map/map.golden")));
    int cases = 0;
    int widths = 0;
    int hard_breaks = 0;
    int originals = 0;
    int discarded = 0;
    int with_marker = 0;
    for (const core_golden::Case& c : core_golden::cases(file, "para-map/map")) {
        CHECK_CONTEXT("case %s (line %d)", c.name.c_str(), c.line);
        REQUIRE(c.kind == "map");
        core_golden::Context context;
        core_golden::Reader reader(file, c);
        const golden::Record& text = reader.take("text");
        BlockStyle style;
        golden::Cursor block_record(reader.take("block"));
        REQUIRE(align_of(block_record.raw(), &style.align));
        const FontSpec font = spec_of(block_record.str());
        style.left_indent = block_record.i64();
        style.first_line_indent = block_record.i64();
        style.marker_gap = block_record.i64();
        block_record.expect_done();
        const core_golden::Skip skip = core_golden::read_skip(reader);
        style.interline.baselineskip = skip.baselineskip;
        style.interline.lineskip = skip.lineskip;
        style.interline.lineskiplimit = skip.lineskiplimit;
        const golden::Record& strut = reader.take("strut");
        style.strut.height = strut.i64(0);
        style.strut.depth = strut.i64(1);

        TextBlock block;
        REQUIRE(append_text_block(block.run, text.str(1), font, text.boolean(0), style.align,
                                  composition, resolve));
        if (reader.next_is("marker")) {
            const golden::Record& marker = reader.take();
            REQUIRE(set_marker(block, marker.str(1), spec_of(marker.str(0)), composition, resolve));
            ++with_marker;
        }
        // No block here has a first-line indent, so the reference's material
        // is the run's nodes and nothing before them.
        REQUIRE_EQ(style.first_line_indent, 0);
        REQUIRE_EQ(reader.take("material").u32(0), static_cast<uint32_t>(block.run.nodes.size()));

        while (reader.next_is("width")) {
            const Scaled width = reader.take().i64(0);
            CHECK_CONTEXT("width %lld", static_cast<long long>(width));
            REQUIRE(composer.compose(context.arena, block, style, width, prose_breaking(),
                                     cache.measure(), layout));
            cmds.clear();
            REQUIRE(ship(context.arena, layout, Point{0, 0}, cache.measure(), cmds) ==
                    ShipError::NONE);
            check_map(block, layout, cmds);

            golden::Cursor shipped(reader.take("shipped"));
            REQUIRE_EQ(shipped.u32(), static_cast<uint32_t>(layout.shipped.size()));
            for (size_t m = 0; m < layout.shipped.size(); ++m) {
                const int64_t expected = shipped.i64();
                const int64_t actual =
                    layout.shipped[m] == NOT_SHIPPED ? -1 : static_cast<int64_t>(layout.shipped[m]);
                if (expected != actual)
                    FAIL("material node %zu: the reference ships it at %lld, the port at %lld", m,
                         static_cast<long long>(expected), static_cast<long long>(actual));
                if (expected < 0 && block.run.nodes[m].is(NodeType::GLUE))
                    ++discarded;
            }
            shipped.expect_done();

            const golden::Record& count = reader.take("count");
            CHECK_EQ(count.u32(0), 0u);
            CHECK_EQ(count.u32(1), layout.item_count);

            // The reference answers once per forced break; `text` wants each
            // boundary once.
            golden::Cursor after(reader.take("after_hard_breaks"));
            std::set<uint32_t> expected_breaks;
            for (uint32_t n = after.u32(); n > 0; --n)
                expected_breaks.insert(after.u32());
            after.expect_done();
            CHECK(std::vector<uint32_t>(expected_breaks.begin(), expected_breaks.end()) ==
                  layout.hard_breaks);
            hard_breaks += static_cast<int>(layout.hard_breaks.size());

            golden::Cursor written_as(reader.take("originals"));
            const uint32_t n = written_as.u32();
            CHECK_EQ(static_cast<size_t>(n), layout.originals.size());
            for (uint32_t i = 0; i < n; ++i) {
                const uint32_t index = written_as.u32();
                const uint32_t codepoint = written_as.u32();
                if (i < layout.originals.size()) {
                    CHECK_EQ(layout.originals[i].index, index);
                    CHECK_EQ(layout.originals[i].codepoint, codepoint);
                }
            }
            written_as.expect_done();
            originals += static_cast<int>(n);
            ++widths;
        }
        CHECK(reader.done());
        ++cases;
    }
    CHECK_EQ(cases, 36);
    CHECK_EQ(widths, 108);
    CHECK_EQ(with_marker, 8);
    CHECK(hard_breaks > 100);
    CHECK(originals > 100);
    CHECK(discarded > 100);
}

// --- What only the port has ---------------------------------------------------------

TEST(prose_breaking_is_not_plain_tex)
{
    const LineBreakParams prose = prose_breaking();
    const LineBreakParams plain;
    CHECK_EQ(prose.tolerance, 1000);
    CHECK_EQ(prose.emergency_stretch, 20 * PT);
    CHECK_EQ(plain.tolerance, 200);
    CHECK_EQ(plain.emergency_stretch, 0);
    CHECK_EQ(prose.pretolerance, plain.pretolerance);
    CHECK(!prose.first_fit);
    CHECK(prose.par_fill_skip == plain.par_fill_skip);
}

TEST(a_shape_holds_absurd_lengths_inside_the_core_invariant)
{
    const Scaled huge = Scaled(1) << 62;
    for (const Scaled width : {huge, -huge, Scaled(0)}) {
        for (const Scaled indent : {huge, -huge}) {
            for (const Scaled first : {huge, -huge, Scaled(0)}) {
                const BlockShape s = shape(width, indent, first);
                for (size_t i = 0; i < s.count; ++i) {
                    CHECK(s.lines[i].width >= UNITY);
                    CHECK(s.lines[i].width <= 4 * MAX_DIMEN);
                    CHECK(s.lines[i].indent >= -2 * MAX_DIMEN);
                    CHECK(s.lines[i].indent <= 2 * MAX_DIMEN);
                }
            }
        }
    }
}

TEST(unskip_drops_trailing_glue_with_its_guard_and_its_offsets)
{
    const FontKey font = PROSE_10.key();
    std::vector<uint32_t> offsets;
    Run run;
    run.source_offsets = &offsets;
    run.nodes = {char_node(font, 'a'), glue(GlueSpec{3}),    char_node(font, 'b'),
                 penalty(INF_PENALTY), glue(GlueSpec{3}),    penalty(50),
                 glue(GlueSpec{3}),    penalty(INF_PENALTY), glue(GlueSpec{3}),
                 glue(GlueSpec{3})};
    offsets = {0, 1, 2, 3, 3, 4, 5, 6, 6, 7};
    unskip(run);
    // Every glue at the end goes, and the guard before the two that had one:
    // the unskip repeats until something that is not glue ends the run. The
    // finite penalty is not a guard and stays.
    REQUIRE_EQ(run.nodes.size(), size_t(6));
    CHECK(run.nodes.back() == penalty(50));
    CHECK(run.nodes[4] == glue(GlueSpec{3}));
    CHECK_EQ(offsets.size(), size_t(6));
    unskip(run);
    CHECK_EQ(run.nodes.size(), size_t(6));

    run.source_base = 100;
    hard_break(run, Align::JUSTIFY, 9);
    REQUIRE_EQ(run.nodes.size(), size_t(9));
    CHECK(run.nodes[6] == penalty(INF_PENALTY));
    CHECK(run.nodes[7] == glue(PARFILLSKIP));
    CHECK(run.nodes[8] == penalty(EJECT_PENALTY));
    REQUIRE_EQ(offsets.size(), size_t(9));
    CHECK_EQ(offsets[6], 109u);
    CHECK_EQ(offsets[8], 109u);

    Run empty;
    unskip(empty);
    CHECK(empty.nodes.empty());
    hard_break(empty, Align::CENTER);
    REQUIRE_EQ(empty.nodes.size(), size_t(1));
    CHECK(empty.nodes[0] == penalty(EJECT_PENALTY));
}

TEST(a_space_before_a_hard_break_does_not_make_an_empty_line)
{
    Bench bench;
    REQUIRE(bench.text("aaa bbb "));
    hard_break(bench.block.run, Align::LEFT);
    REQUIRE(bench.text("ccc"));
    // Find the widths at which "aaa bbb" fits and "aaa bbb " would not.
    const BlockStyle style = plain_style(Align::LEFT);
    REQUIRE(bench.compose(style, 400 * PT));
    CHECK_EQ(bench.spelled(), std::string("aaa bbb|ccc"));
    const Scaled natural = bench.layout.broken.lines[0].packed.natural;
    for (const Scaled extra : {Scaled(0), Scaled(1), PT, 2 * PT}) {
        CHECK_CONTEXT("extra %lld", static_cast<long long>(extra));
        REQUIRE(bench.compose(style, natural + extra));
        CHECK_EQ(bench.layout.lines.size(), size_t(2));
        CHECK_EQ(bench.spelled(), std::string("aaa bbb|ccc"));
    }
}

TEST(alignment_is_where_the_slack_goes)
{
    Bench bench;
    REQUIRE(bench.text("one two three four five six seven eight nine ten eleven twelve"));
    const Scaled width = 120 * PT;
    struct Expect {
        Align align;
        bool flush_left;
        bool flush_right;
    };
    for (const Expect& expect :
         {Expect{Align::JUSTIFY, true, true}, Expect{Align::LEFT, true, false},
          Expect{Align::RIGHT, false, true}, Expect{Align::CENTER, false, false}}) {
        CHECK_CONTEXT("align %d", static_cast<int>(expect.align));
        REQUIRE(bench.compose(plain_style(expect.align), width));
        REQUIRE(bench.layout.lines.size() > 2);
        CHECK_EQ(bench.composer.line_params().first_fit, expect.align != Align::JUSTIFY);
        // The first line's first and last glyphs.
        Scaled first_x = -1;
        Scaled last_right = -1;
        int line = 0;
        for (const DrawCmd& cmd : bench.cmds) {
            if (cmd.is(CmdType::LINE)) {
                ++line;
                CHECK_EQ(cmd.width, width);
                CHECK_EQ(cmd.height,
                         bench.layout.lines.empty() ? 0 : Strut::filling(14 * PT).height);
            } else if (line == 1 && cmd.is(CmdType::GLYPH)) {
                if (first_x < 0)
                    first_x = cmd.x;
                last_right = cmd.x + bench.cache(cmd.font(), cmd.codepoint).width;
            }
        }
        CHECK_EQ(first_x == 0, expect.flush_left);
        CHECK_EQ(last_right == width, expect.flush_right);
        if (expect.align == Align::CENTER)
            CHECK(first_x - (width - last_right) >= -1 && first_x - (width - last_right) <= 1);
        // Strutted lines 14 pt apart with zero glue between them.
        const Box& box = bench.arena.box(bench.layout.box);
        CHECK_EQ(box.height + box.depth, static_cast<Scaled>(bench.layout.lines.size()) * 14 * PT);
        CHECK_EQ(box.width, width);
    }
}

TEST(ragged_text_is_broken_first_fit_without_shrink_unless_asked)
{
    Bench bench;
    REQUIRE(bench.text("one two three four five six seven eight nine ten eleven twelve"));
    BlockStyle style = plain_style(Align::LEFT);
    REQUIRE(bench.compose(style, 120 * PT));
    CHECK(bench.composer.line_params().first_fit);
    CHECK(bench.layout.broken.pass_used == BreakPass::FIRST_FIT);
    bool shrink = false;
    for (const Node& node : bench.composer.broken_material())
        shrink = shrink || (node.is(NodeType::GLUE) && node.shrink() != 0);
    CHECK(!shrink);
    // No line is set tighter than its natural width.
    for (const BrokenLine& line : bench.layout.broken.lines)
        CHECK(line.packed.box.glue_sign != GlueSign::SHRINKING);
    // The block's own material keeps its shrink.
    shrink = false;
    for (const Node& node : bench.block.run.nodes)
        shrink = shrink || (node.is(NodeType::GLUE) && node.shrink() != 0);
    CHECK(shrink);

    style.ragged_total_fit = true;
    REQUIRE(bench.compose(style, 120 * PT));
    CHECK(!bench.composer.line_params().first_fit);
    CHECK(bench.layout.broken.pass_used != BreakPass::FIRST_FIT);
    CHECK(bench.composer.broken_material().data() == bench.block.run.nodes.data());
    CHECK(bench.composer.line_params().right_skip == RAGGED_RIGHT);
}

TEST(the_first_line_indent_is_applied_twice_or_once)
{
    Bench bench;
    REQUIRE(bench.text("one two three four five six seven eight nine ten eleven twelve"));
    BlockStyle style = plain_style(Align::JUSTIFY);
    style.left_indent = 7 * PT;
    style.first_line_indent = 12 * PT;
    const Scaled width = 150 * PT;

    // As the reference: the box, and a first line narrower and shifted.
    REQUIRE(bench.compose(style, width));
    REQUIRE(bench.layout.lines.size() > 2);
    {
        const Box& first = bench.arena.box(bench.layout.lines[0]);
        const Box& second = bench.arena.box(bench.layout.lines[1]);
        CHECK_EQ(first.width, width - 7 * PT - 12 * PT);
        CHECK_EQ(first.shift_amount, 7 * PT + 12 * PT);
        CHECK_EQ(second.width, width - 7 * PT);
        CHECK_EQ(second.shift_amount, 7 * PT);
        REQUIRE(bench.cmds.size() > 1 && bench.cmds[1].is(CmdType::GLYPH));
        CHECK_EQ(bench.cmds[1].x, 7 * PT + 2 * 12 * PT);
    }
    const std::vector<uint32_t> shipped_twice = bench.layout.shipped;

    // As tex.web section 1091: the box alone.
    style.first_line = FirstLineIndent::BOX_ONLY;
    REQUIRE(bench.compose(style, width));
    {
        const Box& first = bench.arena.box(bench.layout.lines[0]);
        CHECK_EQ(first.width, width - 7 * PT);
        CHECK_EQ(first.shift_amount, 7 * PT);
        REQUIRE(bench.cmds.size() > 1 && bench.cmds[1].is(CmdType::GLYPH));
        CHECK_EQ(bench.cmds[1].x, 7 * PT + 12 * PT);
        // The right edge of every line but the last is the column's.
        const DrawCmd& line = bench.cmds[0];
        CHECK_EQ(line.x + line.width, width);
    }
    // The box is not a node of the block's material and takes no index.
    CHECK_EQ(bench.layout.shipped[0], 0u);
    CHECK_EQ(shipped_twice[0], 0u);
    check_map(bench.block, bench.layout, bench.cmds);
    CHECK_EQ(bench.composer.broken_material().size(), bench.block.run.nodes.size() + 1);
}

TEST(indices_map_material_to_what_was_shipped)
{
    Bench bench;
    // U+4E00 is in none of the faces: set as U+FFFD, remembered.
    REQUIRE(bench.text("ab \xE4\xB8\x80 cd"));
    hard_break(bench.block.run, Align::RIGHT);
    hard_break(bench.block.run, Align::RIGHT);
    REQUIRE(bench.text("ef"));
    REQUIRE(set_marker(bench.block, "1.", PROSE_10, bench.composition, bench.resolve));
    BlockStyle style = plain_style(Align::RIGHT);
    style.left_indent = 20 * PT;
    style.marker_gap = 4 * PT;
    REQUIRE(bench.compose(style, 200 * PT));
    CHECK_EQ(bench.layout.lines.size(), size_t(3));
    check_map(bench.block, bench.layout, bench.cmds);

    // The marker: its fil glue and two glyphs. Then each line opens with the
    // left skip and closes with the right skip.
    CHECK_EQ(bench.layout.marker_items, 3u);
    const std::vector<uint32_t>& shipped = bench.layout.shipped;
    const std::vector<Node>& material = bench.block.run.nodes;
    // a b glue U+FFFD glue c d penalty penalty e f, with kerns wherever the
    // face has them: find the nodes by kind.
    std::vector<uint32_t> of_chars;
    for (size_t m = 0; m < material.size(); ++m) {
        if (material[m].is(NodeType::CHAR))
            of_chars.push_back(shipped[m]);
    }
    //                marker 0 1 2 | left 3 | a b _ ? _ c d | right 11
    //                             | left 12 | right 13
    //                             | left 14 | e f par-fill 17 | right 18
    // (the par-fill glue is all zero in a block set ragged left, and is still
    // a glue).
    CHECK(of_chars == (std::vector<uint32_t>{4, 5, 7, 9, 10, 15, 16}));
    CHECK_EQ(bench.layout.item_count, 19u);
    REQUIRE_EQ(bench.layout.originals.size(), size_t(1));
    CHECK_EQ(bench.layout.originals[0].index, 7u);
    CHECK_EQ(bench.layout.originals[0].codepoint, 0x4E00u);
    // Both forced breaks are followed, in the material, by `e`.
    CHECK(bench.layout.hard_breaks == std::vector<uint32_t>{15});

    // Justified, a break is three nodes, and the penalty and fill glue of the
    // second follow the first's forced break, where the breaker discards them:
    // the blank line holds its right skip and nothing else.
    bench.block.clear();
    REQUIRE(bench.text("ab"));
    hard_break(bench.block.run, Align::JUSTIFY);
    hard_break(bench.block.run, Align::JUSTIFY);
    REQUIRE(bench.text("ef"));
    REQUIRE(bench.compose(plain_style(Align::JUSTIFY), 200 * PT));
    CHECK_EQ(bench.layout.lines.size(), size_t(3));
    check_map(bench.block, bench.layout, bench.cmds);
    //   a 0 b 1 fill 2 right 3 | right 4 | e 5 f 6 par-fill 7 right 8
    CHECK(bench.layout.hard_breaks == std::vector<uint32_t>{5});
    CHECK_EQ(bench.layout.item_count, 9u);
    CHECK_EQ(bench.layout.marker_items, 0u);

    // A forced break that ends the material is the paragraph's own end.
    bench.block.clear();
    REQUIRE(bench.text("ab"));
    hard_break(bench.block.run, Align::CENTER);
    REQUIRE(bench.compose(plain_style(Align::CENTER), 200 * PT));
    CHECK(bench.layout.hard_breaks.empty());
    check_map(bench.block, bench.layout, bench.cmds);
}

TEST(an_inline_box_is_one_item_when_it_is_an_atom)
{
    Bench bench;
    REQUIRE(bench.text("x "));
    const FontKey font = PROSE_10.key();
    const Node inside[2] = {char_node(font, 'y'), char_node(font, 'z')};
    const PackResult packed =
        hpack(bench.arena, NodeView(inside, 2), 0, PackMode::ADDITIONAL, bench.cache.measure());
    const Node plain = bench.arena.add_box(packed.box, NodeView(inside, 2));
    bench.block.run.nodes.push_back(atom_box(bench.arena, plain));
    REQUIRE(bench.text(" w "));
    bench.block.run.nodes.push_back(plain);
    REQUIRE(bench.text(" v"));
    REQUIRE(bench.compose(plain_style(Align::JUSTIFY), 300 * PT));
    check_map(bench.block, bench.layout, bench.cmds);
    const std::vector<uint32_t>& shipped = bench.layout.shipped;
    // x _ [yz] _ w _ (y z) _ v parfill right: the atom is one index, the plain
    // box two, and it is named by its first.
    std::vector<uint32_t> expected = {0, 1, 2, 3, 4, 5, 6, 8, 9};
    std::vector<uint32_t> actual;
    for (const uint32_t index : shipped) {
        if (index != NOT_SHIPPED)
            actual.push_back(index);
    }
    CHECK(actual == expected);
    CHECK_EQ(bench.layout.item_count, 12u);
    int atoms = 0;
    for (const DrawCmd& cmd : bench.cmds)
        atoms += cmd.is(CmdType::ATOM) ? 1 : 0;
    CHECK_EQ(atoms, 1);
}

TEST(a_break_at_a_discretionary_leaves_the_material_unmapped)
{
    Bench bench;
    const FontKey font = PROSE_10.key();
    REQUIRE(bench.text("aaaaaaaaaaaaaaaa"));
    const Node hyphen[1] = {char_node(font, '-')};
    bench.block.run.nodes.push_back(bench.arena.add_disc(NodeView(hyphen, 1), NodeView(), 0));
    REQUIRE(bench.text("bbbbbbbbbbbbbbbb"));
    REQUIRE(bench.compose(plain_style(Align::JUSTIFY), 100 * PT));
    REQUIRE_EQ(bench.layout.lines.size(), size_t(2));
    CHECK(bench.layout.broken.breaks[0].kind == BreakKind::DISC);
    CHECK(!bench.layout.mapped);
    CHECK_EQ(bench.layout.item_count, 0u);
    CHECK(bench.layout.originals.empty());
    for (const uint32_t index : bench.layout.shipped)
        CHECK_EQ(index, NOT_SHIPPED);
    // Not taken, it is a node like any other.
    REQUIRE(bench.compose(plain_style(Align::JUSTIFY), 400 * PT));
    CHECK_EQ(bench.layout.lines.size(), size_t(1));
    CHECK(bench.layout.mapped);
    check_map(bench.block, bench.layout, bench.cmds);
}

TEST(malformed_utf8_draws_replacement_characters_and_the_bytes_are_recoverable)
{
    Bench bench;
    const std::string source = std::string("ab\xFF\xFE ") + "\xE4\xB8" + "cd";
    std::vector<uint32_t> offsets;
    bench.block.run.source_offsets = &offsets;
    REQUIRE(bench.text(source));
    REQUIRE_EQ(offsets.size(), bench.block.run.nodes.size());
    REQUIRE(bench.compose(plain_style(Align::LEFT), 300 * PT));
    check_map(bench.block, bench.layout, bench.cmds);
    CHECK_EQ(bench.spelled(), std::string("ab\xEF\xBF\xBD\xEF\xBF\xBD \xEF\xBF\xBD"
                                          "cd"));
    // From a shipped index back to the bytes that were written there.
    std::vector<std::string> written_as(bench.layout.item_count);
    const std::vector<Node>& material = bench.block.run.nodes;
    for (size_t m = 0; m < material.size(); ++m) {
        if (!material[m].is(NodeType::CHAR))
            continue;
        size_t end = source.size();
        for (size_t later = m + 1; later < material.size(); ++later) {
            if (offsets[later] > offsets[m]) {
                end = offsets[later];
                break;
            }
        }
        written_as[bench.layout.shipped[m]] = source.substr(offsets[m], end - offsets[m]);
    }
    std::string recovered;
    for (const DrawCmd& cmd : bench.cmds) {
        if (cmd.is(CmdType::GLYPH))
            recovered += written_as[cmd.index];
        else if (cmd.is(CmdType::SET_GLUE) && cmd.natural() != 0)
            recovered += ' ';
    }
    CHECK_EQ(recovered, source);
}

TEST(an_empty_block_is_one_line_on_the_strut)
{
    Bench bench;
    for (const Align align : {Align::JUSTIFY, Align::LEFT, Align::RIGHT, Align::CENTER}) {
        REQUIRE(bench.compose(plain_style(align), 100 * PT));
        REQUIRE_EQ(bench.layout.lines.size(), size_t(1));
        const Box& box = bench.arena.box(bench.layout.box);
        CHECK_EQ(box.height, Strut::filling(14 * PT).height);
        CHECK_EQ(box.depth, Strut::filling(14 * PT).depth);
        CHECK(bench.layout.shipped.empty());
        CHECK(bench.layout.mapped);
        CHECK(bench.layout.item_count >= 1u);
        check_map(bench.block, bench.layout, bench.cmds);
    }
}

TEST(without_a_measure_composition_says_so_and_still_makes_lines)
{
    Bench bench;
    REQUIRE(bench.text("one two three"));
    REQUIRE(set_marker(bench.block, "-", PROSE_10, bench.composition, bench.resolve));
    const bool ok = bench.composer.compose(bench.arena, bench.block, plain_style(Align::LEFT),
                                           100 * PT, prose_breaking(), CharMeasure(), bench.layout);
    CHECK(!ok);
    CHECK(!bench.layout.ok());
    CHECK(bench.layout.pack_error == PackError::CHAR_WITHOUT_MEASURE);
    CHECK(!bench.layout.lines.empty());
    CHECK(bench.layout.box.is(NodeType::BOX));
    std::vector<DrawCmd> cmds;
    cmds.push_back(DrawCmd::rule_rect(1, 2, 3, 4));
    CHECK(ship(bench.arena, bench.layout, Point{0, 0}, CharMeasure(), cmds) ==
          ShipError::CHAR_WITHOUT_MEASURE);
    CHECK_EQ(cmds.size(), size_t(1));

    // A marker the resolver cannot set leaves the block without one.
    FontLibrary empty;
    std::string why;
    CHECK(!set_marker(bench.block, "-", PROSE_10, bench.composition, fontlib_metrics(empty), &why));
    CHECK(!why.empty());
    CHECK(bench.block.marker.nodes.empty());
}

TEST(set_lines_is_compose_without_the_stack)
{
    Bench bench;
    REQUIRE(bench.text("one two three four five six seven eight nine ten eleven twelve"));
    REQUIRE(set_marker(bench.block, "12.", PROSE_10, bench.composition, bench.resolve));
    BlockStyle style = plain_style(Align::LEFT);
    style.left_indent = 18 * PT;
    style.marker_gap = 3 * PT;
    REQUIRE(bench.compose(style, 110 * PT));
    const std::vector<uint32_t> shipped = bench.layout.shipped;
    const size_t lines = bench.layout.lines.size();
    std::vector<Box> boxes;
    for (const Node& line : bench.layout.lines)
        boxes.push_back(bench.arena.box(line));

    ParagraphLayout alone;
    REQUIRE(bench.composer.set_lines(bench.arena, bench.block, style, 110 * PT, prose_breaking(),
                                     bench.cache.measure(), alone));
    CHECK(alone.vlist.empty());
    CHECK(!alone.box.is(NodeType::BOX));
    REQUIRE_EQ(alone.lines.size(), lines);
    CHECK(alone.shipped == shipped);
    for (size_t i = 0; i < lines; ++i) {
        const Box& box = bench.arena.box(alone.lines[i]);
        CHECK(box.width == boxes[i].width && box.height == boxes[i].height &&
              box.depth == boxes[i].depth && box.shift_amount == boxes[i].shift_amount);
        CHECK(deep_equal(bench.arena, alone.lines[i], bench.arena, bench.layout.lines[i]));
    }
    // The marker's box is exactly the indent wide and the line lost its shift.
    const NodeView pair = bench.arena.contents(alone.hung);
    REQUIRE_EQ(pair.size(), size_t(2));
    CHECK_EQ(bench.arena.box(pair[0]).width, 18 * PT);
    CHECK_EQ(bench.arena.box(pair[1]).shift_amount, 0);
    CHECK_EQ(bench.arena.box(alone.lines[0]).shift_amount, 0);
    CHECK_EQ(bench.arena.box(alone.lines[1]).shift_amount, 18 * PT);
}

TEST(warmed_up_composition_allocates_nothing)
{
    Bench bench;
    std::string words;
    for (int i = 0; i < 60; ++i)
        words += "word" + std::to_string(i * 37 % 101) + (i % 7 == 0 ? ". " : " ");
    const std::string list_marker = "\xE2\x80\xA2";
    BlockStyle styles[4] = {plain_style(Align::JUSTIFY), plain_style(Align::LEFT),
                            plain_style(Align::RIGHT), plain_style(Align::CENTER)};
    styles[1].left_indent = 15 * PT;
    styles[1].marker_gap = 4 * PT;
    styles[2].first_line_indent = 10 * PT;
    const Scaled widths[3] = {90 * PT, 180 * PT, 333 * PT};
    const NodeArena::Mark mark = bench.arena.mark();
    const CharMeasure measure = bench.cache.measure();

    size_t lines = 0;
    size_t counted_from = 0;
    for (int round = 0; round < 5; ++round) {
        // Twice round to grow every buffer to its largest, then count.
        if (round == 2) {
            counted_from = g_allocations;
            lines = 0;
        }
        for (int s = 0; s < 4; ++s) {
            for (const Scaled width : widths) {
                bench.arena.rewind(mark);
                bench.block.clear();
                append_text(bench.block.run, words, PROSE_10, true, bench.composition,
                            bench.resolve);
                hard_break(bench.block.run, styles[s].align);
                append_text(bench.block.run, words, FontSpec{Face::PROSE_ITALIC, 10 * PT}, true,
                            bench.composition, bench.resolve);
                if (s == 1)
                    set_marker(bench.block, list_marker, PROSE_10, bench.composition,
                               bench.resolve);
                bench.composer.compose(bench.arena, bench.block, styles[s], width, prose_breaking(),
                                       measure, bench.layout);
                bench.cmds.clear();
                ship(bench.arena, bench.layout, Point{0, 0}, measure, bench.cmds);
                lines += bench.layout.lines.size();
            }
        }
    }
    CHECK_EQ(g_allocations - counted_from, size_t(0));
    CHECK(lines > 200);
}
