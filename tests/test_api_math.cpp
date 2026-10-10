// Formulas through the public API: `$...$` and `$$...$$` in a paragraph's text,
// found by the native scanner, set by src/mathtext and src/math_mlist, and
// broken, shipped, drawn, selected and copied with the words around them.
//
// What is held against the reference here is tests/golden/math/inline.golden:
// for each of its sources, where the formulas are (the segments), what became
// of each (set, or refused with which reason and where), the paragraph's
// material node for node, and at every recorded width the breaks, the line
// boxes and the shipped commands. The golden's vertical assembly has no strut
// (tools/oracle/FORMAT.md, section 6) and the public API's lines have one, so
// of a shipped command the vertical placement is compared relative to its
// line's baseline, and the height and depth of a line and of its items, which
// are the line's, are only required not to be less; everything else is
// compared exactly.
//
// The rest is what no golden holds: the rules the port decides for itself
// (`\$`, backquotes, runs), copy, the performance contract with math on, and
// pixels.

#include "core_golden.h"
#include "golden.h"

#include "imtypeset.h"

#include "context.h"
#include "font_loader.h"
#include "support/soft_raster.h"
#include "unicode_tables.h"

#include <cinttypes>
#include <cstdlib>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

using namespace ImTypeset::detail;

// --- Counting allocations ---------------------------------------------------------
// Every form of operator new and ImGui's own allocator, as tests/test_api.cpp
// counts them. Not inlined: GCC, seeing through an inlined replacement to
// malloc and free, reports the pair as mismatched.
#if defined(__GNUC__)
#define TEST_API_NOINLINE __attribute__((noinline))
#else
#define TEST_API_NOINLINE
#endif

static size_t g_allocations = 0;

TEST_API_NOINLINE void* operator new(size_t size)
{
    ++g_allocations;
    void* const memory = std::malloc(size != 0 ? size : 1);
    if (memory == nullptr)
        std::abort();
    return memory;
}
TEST_API_NOINLINE void* operator new(size_t size, const std::nothrow_t&) noexcept
{
    ++g_allocations;
    return std::malloc(size != 0 ? size : 1);
}
TEST_API_NOINLINE void* operator new[](size_t size)
{
    return operator new(size);
}
TEST_API_NOINLINE void* operator new[](size_t size, const std::nothrow_t& tag) noexcept
{
    return operator new(size, tag);
}
TEST_API_NOINLINE void operator delete(void* memory) noexcept
{
    std::free(memory);
}
TEST_API_NOINLINE void operator delete(void* memory, size_t) noexcept
{
    std::free(memory);
}
TEST_API_NOINLINE void operator delete(void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}
TEST_API_NOINLINE void operator delete[](void* memory) noexcept
{
    std::free(memory);
}
TEST_API_NOINLINE void operator delete[](void* memory, size_t) noexcept
{
    std::free(memory);
}
TEST_API_NOINLINE void operator delete[](void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}

namespace
{

void* counted_alloc(size_t size, void*)
{
    ++g_allocations;
    return std::malloc(size != 0 ? size : 1);
}

void counted_free(void* memory, void*)
{
    std::free(memory);
}

const struct {
    ImTypesetFace face;
    const char* file;
} DEFAULT_FONTS[] = {
    {ImTypesetFace_Regular, "Roboto-Regular.ttf"},
    {ImTypesetFace_Bold, "Roboto-Bold.ttf"},
    {ImTypesetFace_Italic, "Roboto-RegularItalic.ttf"},
    {ImTypesetFace_BoldItalic, "Roboto-BoldItalic.ttf"},
    {ImTypesetFace_Code, "Inconsolata-Medium.ttf"},
    {ImTypesetFace_Math, "latinmodern-math.otf"},
};

std::string font_path(const char* file)
{
    return golden::data_path(std::string("fonts/") + file);
}

// A headless ImGui context with an ImTypeset context in it, the reference's
// default fonts, the math face among them unless told otherwise.
struct Stage {
    explicit Stage(bool with_math_face = true)
    {
        ImGui::SetAllocatorFunctions(&counted_alloc, &counted_free, nullptr);
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(800.0f, 600.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        if (!IMTYPESET_CHECKVERSION())
            FAIL("IMTYPESET_CHECKVERSION: %s", ImTypeset::GetLastError()->Message);
        ctx = ImTypeset::CreateContext();
        if (ctx == nullptr) {
            FAIL("CreateContext: %s", ImTypeset::GetLastError()->Message);
            return;
        }
        for (const auto& entry : DEFAULT_FONTS) {
            if (entry.face == ImTypesetFace_Math && !with_math_face)
                continue;
            if (ImTypeset::AddFontFromFileTTF(entry.face, font_path(entry.file).c_str()) == nullptr)
                FAIL("%s: %s", entry.file, ImTypeset::GetLastError()->Message);
        }
        // The reference's default theme: a 16 px em.
        ImTypeset::GetStyle().EmSize = 16.0f;
        ImTypeset::GetStyle().Color = IM_COL32(0, 0, 0, 255);
    }
    ~Stage()
    {
        ImTypeset::DestroyContext();
        ImGui::DestroyContext();
    }
    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;

    ImTypesetContext* ctx = nullptr;
};

struct Frame {
    Frame() { ImGui::NewFrame(); }
    ~Frame() { ImGui::Render(); }
};

const ImGuiWindowFlags PLAIN =
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;

void begin_window(const char* name, float width, float height = 500.0f)
{
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin(name, nullptr, PLAIN);
}

struct Cost {
    ImTypesetStats stats;
    size_t allocations;

    static Cost now() { return Cost{ImTypeset::GetStats(), g_allocations}; }
};

// The text of bytes [start, end) of what the paragraph was given.
std::string copied(const ImTypesetParagraph* paragraph, int start, int end)
{
    ImGuiTextBuffer buffer;
    const ImTypesetTextRange range = {start, end};
    ImTypeset::AppendParagraphRangeText(paragraph, range, &buffer);
    return std::string(buffer.c_str());
}

std::string copied_all(const ImTypesetParagraph* paragraph)
{
    return copied(paragraph, 0, 1 << 30);
}

// The shipped items, which the public API no longer speaks in and these
// tests still reason about: how many there are, the item at a byte of the
// text, the byte an item came from, and the text of items [first, end).
int item_count(const ImTypesetParagraph* paragraph)
{
    return static_cast<int>(paragraph->layout.item_count);
}

int index_of(const ImTypesetParagraph* paragraph, int byte)
{
    const ImTypesetTextRange range = {byte, 1 << 30};
    return byte >= static_cast<int>(paragraph->text.size())
               ? item_count(paragraph)
               : static_cast<int>(selection_of(*paragraph, range).start);
}

int byte_of(const ImTypesetParagraph* paragraph, int index)
{
    if (paragraph->index_bytes.empty())
        return 0;
    const size_t last = paragraph->index_bytes.size() - 1;
    return static_cast<int>(
        paragraph->index_bytes[std::min(static_cast<size_t>(std::max(index, 0)), last)]);
}

std::string copied_items(const ImTypesetParagraph* paragraph, int first, int end)
{
    return copied(paragraph, byte_of(paragraph, first), byte_of(paragraph, end));
}

void set_math(ImTypesetParagraph* p, const std::string& text, ImTypesetTextFlags more = 0)
{
    ImTypeset::SetParagraphText(p, text.data(), text.data() + text.size(), nullptr, 0,
                                ImTypesetTextFlags_Math | more);
}

int count_of(const ImTypesetParagraph* p, CmdType type)
{
    int n = 0;
    for (const DrawCmd& cmd : shipped_commands(*p))
        n += cmd.is(type) ? 1 : 0;
    return n;
}

ImTypesetFormula formula_of(const ImTypesetParagraph* p, int index)
{
    CHECK(index >= 0 && index < ImTypeset::GetParagraphFormulaCount(p));
    return ImTypeset::GetParagraphFormula(p, index);
}

// --- Nodes as the oracle writes them -----------------------------------------------

std::string font_text(FontKey key)
{
    FontSpec spec;
    if (!parse_font_key(key, &spec))
        return "?";
    return golden::percent_encode(font_key_text(spec.face, spec.size));
}

void write_node(const NodeArena& arena, const Node& node, std::vector<std::string>& out)
{
    using core_golden::token;
    switch (node.type) {
    case NodeType::CHAR:
        if (node.is_replaced())
            out.push_back("rchar " + font_text(node.font()) +
                          check::format(" %u %u", node.codepoint(), node.original()));
        else
            out.push_back("char " + font_text(node.font()) +
                          check::format(" %u", node.codepoint()));
        break;
    case NodeType::BOX: {
        const Box& box = arena.box(node);
        const NodeView contents = arena.view(box.contents);
        out.push_back(check::format("box %s %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64
                                    " %s %s %s %zu",
                                    token(box.kind), box.width, box.height, box.depth,
                                    box.shift_amount, core_golden::hex_float(box.glue_set).c_str(),
                                    token(box.glue_sign), token(box.glue_order), contents.size()));
        for (const Node& child : contents)
            write_node(arena, child, out);
        out.push_back("endbox");
        break;
    }
    case NodeType::RULE:
        out.push_back(check::format("rule %" PRId64 " %" PRId64 " %" PRId64, node.width(),
                                    node.height(), node.depth()));
        break;
    case NodeType::DISC:
        out.push_back("disc");
        break;
    case NodeType::MATH_ON:
        out.push_back(check::format("mathon %" PRId64, node.width()));
        break;
    case NodeType::MATH_OFF:
        out.push_back(check::format("mathoff %" PRId64, node.width()));
        break;
    case NodeType::GLUE:
        out.push_back("glue " + core_golden::spec_fields(node.spec()));
        break;
    case NodeType::KERN:
        out.push_back(check::format("kern %" PRId64 " %s", node.width(), token(node.kern_kind())));
        break;
    case NodeType::PENALTY:
        out.push_back(check::format("penalty %" PRId64, node.penalty()));
        break;
    }
}

std::string record_text(const golden::Record& record)
{
    std::string out(record.tag);
    for (const std::string_view field : record.fields) {
        out.push_back(' ');
        out.append(field);
    }
    return out;
}

void check_lines(const char* what, const std::vector<std::string>& expected,
                 const std::vector<std::string>& actual)
{
    CHECK_CONTEXT("%s", what);
    CHECK_EQ(actual.size(), expected.size());
    for (size_t i = 0; i < expected.size() && i < actual.size(); ++i) {
        if (expected[i] != actual[i]) {
            FAIL("record %zu: the reference has `%s`, the port `%s`", i, expected[i].c_str(),
                 actual[i].c_str());
            return;
        }
    }
}

// --- The golden ------------------------------------------------------------------

struct Segment {
    bool formula = false;
    bool display = false;
    std::string text;
};

struct Span {
    std::string source;
    bool display = false;
    bool unclosed = false;
    uint32_t start = 0;
    uint32_t end = 0;
    bool refused = false;
    uint32_t position = 0;
    uint32_t byte_position = 0;
    std::string reason;
};

struct InlineCase {
    std::string source;
    bool block = false;
    bool closed = false;
    std::vector<Segment> segments;
    std::vector<Span> spans;
};

InlineCase read_inline(const golden::File& file, const core_golden::Case& c)
{
    InlineCase out;
    for (size_t at = c.first; at < c.end; ++at) {
        const golden::Record& r = file.records()[at];
        if (r.is("source")) {
            out.source = r.str(0);
        } else if (r.is("text")) {
            out.segments.push_back(Segment{false, false, r.str(0)});
        } else if (r.is("formula")) {
            out.segments.push_back(Segment{true, r.boolean(0), r.str(1)});
        } else if (r.is("block")) {
            out.block = true;
            out.closed = r.boolean(0);
        } else if (r.is("span")) {
            Span span;
            span.source = r.str(0);
            span.display = r.boolean(1);
            span.unclosed = r.boolean(2);
            span.start = r.u32(3);
            span.end = r.u32(4);
            span.refused = r.boolean(5);
            span.position = r.u32(6);
            span.byte_position = r.u32(7);
            span.reason = r.str(8);
            out.spans.push_back(span);
        }
    }
    return out;
}

// What the native scanner makes of a source, as the golden's segments.
std::vector<Segment> scanned_segments(const std::string& source,
                                      std::vector<FoundFormula>* found_out,
                                      std::vector<uint32_t>* dropped_out = nullptr)
{
    std::vector<uint32_t> cps;
    std::vector<uint32_t> offs;
    decode_utf8(source, cps, &offs);
    offs.push_back(static_cast<uint32_t>(source.size()));
    size_t begin = 0;
    size_t end = cps.size();
    while (begin < end && py_isspace(cps[begin]))
        ++begin;
    while (end > begin && py_isspace(cps[end - 1]))
        --end;
    std::vector<FoundFormula> found;
    std::vector<uint32_t> dropped;
    scan_math(cps.data(), offs.data(), begin, end, found, dropped);
    std::vector<Segment> out;
    uint32_t at = offs[begin];
    for (const FoundFormula& f : found) {
        if (f.begin > at)
            out.push_back(Segment{false, false, source.substr(at, f.begin - at)});
        out.push_back(Segment{true, f.marker == 2,
                              source.substr(f.begin + f.marker, f.end - f.begin - 2 * f.marker)});
        at = f.end;
    }
    if (offs[end] > at)
        out.push_back(Segment{false, false, source.substr(at, offs[end] - at)});
    if (found_out != nullptr)
        *found_out = found;
    if (dropped_out != nullptr)
        *dropped_out = dropped;
    return out;
}

std::string segments_text(const std::vector<Segment>& segments)
{
    std::string out;
    for (const Segment& s : segments) {
        out += s.formula ? (s.display ? "[display:" : "[formula:") : "[text:";
        out += golden::percent_encode(s.text);
        out += "]";
    }
    return out;
}

} // namespace

// --- The scanner -------------------------------------------------------------------

TEST(the_scanner_segments_every_reference_paragraph_as_the_reference_does)
{
    golden::File file;
    REQUIRE(file.load(golden::path("math/inline.golden")));
    int paragraphs = 0;
    int formulas = 0;
    for (const core_golden::Case& c : core_golden::cases(file)) {
        if (c.kind != "inline")
            continue;
        CHECK_CONTEXT("case %s (line %d)", c.name.c_str(), c.line);
        const InlineCase expected = read_inline(file, c);
        if (expected.block)
            continue;
        ++paragraphs;
        std::vector<FoundFormula> found;
        const std::vector<Segment> actual = scanned_segments(expected.source, &found);
        CHECK_EQ(segments_text(actual), segments_text(expected.segments));
        formulas += static_cast<int>(found.size());
    }
    // A golden that quietly shrank must not read as a pass.
    CHECK_EQ(paragraphs, 58);
    CHECK(formulas >= 40);
    std::printf("api-math: %d paragraphs segmented as the reference's, %d formulas\n", paragraphs,
                formulas);
}

// What the reference leaves to markdown and the port decides: see
// ImTypesetTextFlags_Math in the header and docs/divergences.md, `## api-math`.
TEST(the_scanner_decides_what_markdown_decided_for_the_reference)
{
    const auto scanned = [](const char* source) {
        return segments_text(scanned_segments(source, nullptr));
    };
    // A backslash takes the dollar after it out of consideration, as an
    // opener and as a closer.
    CHECK_EQ(scanned("\\$x$"), std::string("[text:\\$x$]"));
    CHECK_EQ(scanned("costs \\$5, or $x$"), std::string("[text:costs%20\\$5,%20or%20][formula:x]"));
    CHECK_EQ(scanned("$a\\$b$"), std::string("[formula:a\\$b]"));
    CHECK_EQ(scanned("$a\\$ b$"), std::string("[formula:a\\$%20b]"));
    // Two backslashes are a backslash and leave the dollar alone.
    CHECK_EQ(scanned("\\\\$x$"), std::string("[text:\\\\][formula:x]"));
    CHECK_EQ(scanned("\\\\\\$x$"), std::string("[text:\\\\\\$x$]"));
    CHECK_EQ(scanned("$x\\\\$"), std::string("[formula:x\\\\]"));
    // An escaped closer does not close, and the search goes on past it.
    CHECK_EQ(scanned("$x\\$y$ z"), std::string("[formula:x\\$y][text:%20z]"));
    // The first unescaped candidate decides, as in the reference: here it has
    // white space before it, so the opener opens nothing.
    CHECK_EQ(scanned("$x\\$ and $y$"), std::string("[text:$x\\$%20and%20][formula:y]"));
    // A backslash escapes whatever follows it, so it cannot be escaped by a
    // letter before it.
    CHECK_EQ(scanned("a\\b$x$"), std::string("[text:a\\b$x$]"));
    CHECK_EQ(scanned("a\\ $x$"), std::string("[text:a\\%20][formula:x]"));
    // Backquotes and asterisks are characters like any other.
    CHECK_EQ(scanned("`$x$`"), std::string("[text:`][formula:x][text:`]"));
    CHECK_EQ(scanned("*$x$*"), std::string("[text:*][formula:x][text:*]"));
    CHECK_EQ(scanned("`a $b` c$"), std::string("[text:`a%20][formula:b`%20c]"));
    // A formula may run across a newline.
    CHECK_EQ(scanned("$a\n+b$"), std::string("[formula:a%0A+b]"));
    // White space at the ends of the text is not the text.
    CHECK_EQ(scanned("  $x$  "), std::string("[formula:x]"));
    CHECK_EQ(scanned(""), std::string(""));
    CHECK_EQ(scanned("\\"), std::string("[text:\\]"));
    CHECK_EQ(scanned("$"), std::string("[text:$]"));
    CHECK_EQ(scanned("$$"), std::string("[text:$$]"));
    CHECK_EQ(scanned("$$$"), std::string("[text:$$$]"));
    CHECK_EQ(scanned("$$$$"), std::string("[text:$$$$]"));
    // Malformed UTF-8 is U+FFFD, which is neither a letter nor a space.
    CHECK_EQ(scanned("\xFF$x$\xFF"), std::string("[text:%FF][formula:x][text:%FF]"));

    std::vector<FoundFormula> found;
    std::vector<uint32_t> dropped;
    scanned_segments("a \\$1, \\\\ and \\$2 $x\\$y$ \\q", &found, &dropped);
    REQUIRE_EQ(dropped.size(), size_t{2});
    CHECK_EQ(dropped[0], 2u);
    CHECK_EQ(dropped[1], 14u);
    CHECK_EQ(found.size(), size_t{1});
}

// --- Against the reference ----------------------------------------------------------

namespace
{

struct Tally {
    int paragraphs = 0;
    int widths = 0;
    int set = 0;
    int refused = 0;
    int glyphs = 0;
    int atoms = 0;
};

// One golden paragraph: the `inline` case's spans and the `linebreak` case's
// material, breaks, lines and commands, against the public API's.
void replay(Stage& stage, const golden::File& file, const InlineCase& expected,
            const core_golden::Case& c, ImTypesetParagraph* p, Tally& tally)
{
    const std::vector<golden::Record>& records = file.records();
    size_t at = c.first;
    const auto find = [&](const char* tag) -> const golden::Record* {
        while (at < c.end && !records[at].is(tag))
            ++at;
        return at < c.end ? &records[at++] : nullptr;
    };

    const golden::Record* theme = find("theme");
    REQUIRE(theme != nullptr);
    ImTypesetStyle style;
    style.PixelsPerInch = static_cast<float>(theme->f64(0));
    REQUIRE_EQ(std::string(theme->raw(1)), std::string("extra_space_only"));
    const std::string align(theme->raw(3));
    FontSpec block_font;
    REQUIRE(parse_font_key_text(theme->str(4), &block_font));
    REQUIRE_EQ(theme->i64(5), int64_t{0});
    REQUIRE_EQ(theme->i64(6), int64_t{0});
    const double ppi = static_cast<double>(style.PixelsPerInch);
    style.EmSize = static_cast<float>(px_from_sp(block_font.size, ppi));
    // The style asks for a justified paragraph with an indent whatever the
    // golden says: a display block centres itself and takes no indent.
    style.Align = ImTypesetAlign_Justify;
    style.FirstLineIndent = expected.block ? 40.0f : 0.0f;
    const ResolvedStyle resolved = resolve_style(*stage.ctx, style);
    REQUIRE_EQ(resolved.em, block_font.size);
    CHECK_EQ(align, std::string(expected.block ? "center" : "justify"));

    set_math(p, expected.source);
    ImTypeset::LayoutParagraph(p, 300.0f, &style);

    // --- Material.
    const golden::Record* open = find("nodes");
    REQUIRE(open != nullptr);
    REQUIRE_EQ(std::string(open->raw(0)), std::string("material"));
    std::vector<std::string> expected_nodes;
    expected_nodes.push_back(record_text(*open));
    while (at < c.end) {
        expected_nodes.push_back(record_text(records[at]));
        if (records[at++].is("endnodes"))
            break;
    }
    std::vector<std::string> actual_nodes;
    const std::vector<Node>& material = p->block.run.nodes;
    actual_nodes.push_back(check::format("nodes material %zu", material.size()));
    for (const Node& node : material)
        write_node(p->arena, node, actual_nodes);
    actual_nodes.push_back("endnodes");
    check_lines("material", expected_nodes, actual_nodes);

    // --- What became of each formula.
    CHECK_EQ(p->display_block, expected.block);
    REQUIRE_EQ(ImTypeset::GetParagraphFormulaCount(p), static_cast<int>(expected.spans.size()));
    std::vector<uint32_t> cps;
    std::vector<uint32_t> offs;
    decode_utf8(expected.source, cps, &offs);
    offs.push_back(static_cast<uint32_t>(expected.source.size()));
    size_t atom = 0;
    bool any_refused = false;
    for (size_t n = 0; n < expected.spans.size(); ++n) {
        const Span& span = expected.spans[n];
        CHECK_CONTEXT("formula %zu, `%s`", n, span.source.c_str());
        const ImTypesetFormula formula = formula_of(p, static_cast<int>(n));
        CHECK_EQ(formula.Display, span.display);
        CHECK_EQ(formula.Refused, span.refused);
        CHECK_EQ(std::string(formula.Reason), span.reason);
        const std::string written =
            expected.source.substr(static_cast<size_t>(formula.TextStart),
                                   static_cast<size_t>(formula.TextEnd - formula.TextStart));
        if (!expected.block) {
            const std::string marker = span.display ? "$$" : "$";
            CHECK_EQ(written, marker + span.source + marker);
        } else {
            CHECK_EQ(written, expected.source);
        }
        if (!span.refused) {
            ++tally.set;
            CHECK_EQ(formula.BytePos, -1);
            CHECK_EQ(formula.CodepointPos, -1);
            // One node of the material, an atom box, where the reference has it.
            CHECK_EQ(span.end, span.start + 1);
            REQUIRE(atom < p->atoms.size());
            CHECK_EQ(p->atoms[atom].node, span.start);
            ++atom;
            continue;
        }
        ++tally.refused;
        if (!any_refused) {
            // The paragraph's error is the first refusal.
            const ImTypesetErrorInfo* error = ImTypeset::GetParagraphError(p);
            CHECK_EQ(error->Code, static_cast<int>(ImTypesetError_MathRefused));
            CHECK_EQ(std::string(error->Message), span.reason);
            CHECK_EQ(error->BytePos, formula.BytePos);
            CHECK_EQ(error->CodepointPos, formula.CodepointPos);
        }
        any_refused = true;
        // The reference's position is an offset into the source between the
        // dollars; the API's is a place in the text.
        size_t expected_byte = static_cast<size_t>(formula.TextStart);
        if (!span.unclosed) {
            expected_byte += span.display ? 2 : 1;
            if (expected.block) {
                // No block here is refused anywhere but at its first character.
                REQUIRE_EQ(span.byte_position, 0u);
                while (expected_byte < expected.source.size() &&
                       (expected.source[expected_byte] == ' ' ||
                        expected.source[expected_byte] == '\n'))
                    ++expected_byte;
            }
            expected_byte += span.byte_position;
        }
        CHECK_EQ(formula.BytePos, static_cast<int>(expected_byte));
        REQUIRE(formula.CodepointPos >= 0 &&
                static_cast<size_t>(formula.CodepointPos) < offs.size());
        CHECK_EQ(offs[static_cast<size_t>(formula.CodepointPos)],
                 static_cast<uint32_t>(expected_byte));
    }
    if (!any_refused)
        CHECK_EQ(ImTypeset::GetParagraphError(p)->Code, static_cast<int>(ImTypesetError_None));
    CHECK_EQ(atom, p->atoms.size());

    // --- Each width the reference broke it at.
    while (const golden::Record* run = find("break")) {
        const std::string name = run->str(0);
        CHECK_CONTEXT("%s", name.c_str());
        // Only what the reference's views hand the breaker is the API's to match.
        if (name.compare(0, 3, "ui-") != 0)
            continue;
        const float width_px = static_cast<float>(std::atof(name.c_str() + 3));
        const golden::Record* shape = find("shape");
        REQUIRE(shape != nullptr);
        REQUIRE_EQ(resolve_width(width_px, resolved), shape->i64(1));
        ImTypeset::LayoutParagraph(p, width_px, &style);

        const golden::Record* breaks = find("breaks");
        REQUIRE(breaks != nullptr);
        const Paragraph& broken = p->layout.broken;
        const size_t lines = breaks->u32(0);
        REQUIRE_EQ(broken.breaks.size(), lines);
        REQUIRE_EQ(broken.lines.size(), lines);
        for (size_t line = 0; line < lines; ++line) {
            CHECK_EQ(broken.breaks[line].position, breaks->u32(1 + 2 * line));
            CHECK_EQ(
                std::string(
                    core_golden::BREAK_KIND_TOKENS[static_cast<int>(broken.breaks[line].kind)]),
                std::string(breaks->raw(2 + 2 * line)));
        }
        for (size_t line = 0; line < lines; ++line) {
            REQUIRE(find("packed") != nullptr);
            std::vector<std::string> expected_box;
            int depth = 0;
            while (at < c.end) {
                const golden::Record& r = records[at++];
                expected_box.push_back(record_text(r));
                depth += r.is("box") ? 1 : (r.is("endbox") ? -1 : 0);
                if (depth == 0)
                    break;
            }
            std::vector<std::string> actual_box;
            write_node(p->arena, broken.lines[line].box, actual_box);
            check_lines(check::format("line %zu", line).c_str(), expected_box, actual_box);
        }

        const golden::Record* head = find("cmds");
        REQUIRE(head != nullptr);
        const std::vector<DrawCmd>& actual = shipped_commands(*p);
        REQUIRE_EQ(actual.size(), static_cast<size_t>(head->u32(0)));
        Scaled expected_baseline = 0;
        Scaled actual_baseline = 0;
        for (size_t n = 0; n < actual.size() && at < c.end; ++n) {
            const golden::Record& r = records[at++];
            const DrawCmd& cmd = actual[n];
            CHECK_CONTEXT("command %zu, `%s`", n, record_text(r).c_str());
            if (r.is("line")) {
                REQUIRE(cmd.is(CmdType::LINE));
                expected_baseline = r.i64(1);
                actual_baseline = cmd.y;
                CHECK_EQ(cmd.x, r.i64(0));
                CHECK_EQ(cmd.width, r.i64(2));
                // The strut only ever adds.
                CHECK(cmd.height >= r.i64(3));
                CHECK(cmd.depth >= r.i64(4));
                CHECK_EQ(cmd.index, r.u32(5));
                CHECK_EQ(cmd.end, r.u32(6));
            } else if (r.is("glyph")) {
                REQUIRE(cmd.is(CmdType::GLYPH));
                ++tally.glyphs;
                CHECK_EQ(font_text(cmd.font()), std::string(r.raw(0)));
                CHECK_EQ(cmd.codepoint, r.u32(1));
                CHECK_EQ(cmd.x, r.i64(2));
                CHECK_EQ(cmd.y - actual_baseline, r.i64(3) - expected_baseline);
                CHECK_EQ(cmd.index, r.u32(4));
            } else if (r.is("rulerect")) {
                REQUIRE(cmd.is(CmdType::RULE_RECT));
                CHECK_EQ(cmd.x, r.i64(0));
                CHECK_EQ(cmd.y - actual_baseline, r.i64(1) - expected_baseline);
                CHECK_EQ(cmd.width, r.i64(2));
                CHECK_EQ(cmd.height, r.i64(3));
            } else if (r.is("setglue") || r.is("atom")) {
                const bool is_atom = r.is("atom");
                REQUIRE(cmd.is(is_atom ? CmdType::ATOM : CmdType::SET_GLUE));
                tally.atoms += is_atom ? 1 : 0;
                CHECK_EQ(cmd.x, r.i64(0));
                CHECK_EQ(cmd.y - actual_baseline, r.i64(1) - expected_baseline);
                CHECK_EQ(cmd.width, r.i64(2));
                CHECK_EQ(cmd.a, r.i64(3));
                // An item's height and depth are its line's, so the strut's.
                CHECK(cmd.height >= r.i64(4));
                CHECK(cmd.depth >= r.i64(5));
                CHECK_EQ(cmd.index, r.u32(6));
            } else {
                FAIL("line %d: not a draw command", r.line);
            }
        }
        ++tally.widths;

        // A formula copies as it was written, whatever the width.
        if (!expected.block)
            CHECK_EQ(copied_all(p), expected.source);
    }
    ++tally.paragraphs;
}

} // namespace

TEST(formulas_in_running_text_give_the_reference_material_lines_and_commands)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    // The goldens are the reference's, and the public API lays out under
    // TexRules::product() unless told otherwise (src/tex_rules.h).
    stage.ctx->tex_rules = ImTypeset::detail::TexRules::reference();
    golden::File file;
    REQUIRE(file.load(golden::path("math/inline.golden")));
    const std::vector<core_golden::Case> cases = core_golden::cases(file);
    Tally tally;
    int blocks = 0;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    for (size_t n = 0; n + 1 < cases.size(); ++n) {
        if (cases[n].kind != "inline")
            continue;
        CHECK_CONTEXT("case %s (line %d)", cases[n].name.c_str(), cases[n].line);
        REQUIRE_EQ(cases[n + 1].kind, std::string("linebreak"));
        REQUIRE_EQ(cases[n + 1].name, cases[n].name + "/paragraph");
        const InlineCase expected = read_inline(file, cases[n]);
        blocks += expected.block ? 1 : 0;
        replay(stage, file, expected, cases[n + 1], p, tally);
    }
    std::printf("api-math: %d reference paragraphs (%d display blocks) through the public API at "
                "%d widths: %d formulas set, %d refused, %d atoms and %d glyphs shipped\n",
                tally.paragraphs, blocks, tally.widths, tally.set, tally.refused, tally.atoms,
                tally.glyphs);
    CHECK_EQ(tally.paragraphs, 71);
    CHECK_EQ(blocks, 13);
    CHECK(tally.widths >= 100);
    CHECK(tally.set >= 40);
    CHECK(tally.refused >= 6);
    CHECK(tally.glyphs > 2000);
}

// --- Off by default ------------------------------------------------------------------

TEST(math_is_off_unless_the_text_asks_for_it_and_the_flag_is_the_whole_switch)
{
    Stage stage;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    const std::string text = "Area is $\\pi r^2$ here.";

    // Without the flag a dollar is a dollar, math face or no.
    ImTypeset::SetParagraphText(p, text.c_str());
    CHECK(ImTypeset::LayoutParagraph(p, 400.0f));
    CHECK_EQ(count_of(p, CmdType::ATOM), 0);
    CHECK_EQ(ImTypeset::GetParagraphFormulaCount(p), 0);
    CHECK_EQ(copied_all(p), text);
    const int plain = item_count(p);

    // With it: one atom where nine characters were. Nothing else to switch.
    set_math(p, text);
    const ImU64 layouts = ImTypeset::GetStats().Layouts;
    CHECK(ImTypeset::LayoutParagraph(p, 400.0f));
    CHECK_EQ(count_of(p, CmdType::ATOM), 1);
    CHECK_EQ(item_count(p), plain - 9 + 1);
    CHECK_EQ(ImTypeset::GetParagraphFormulaCount(p), 1);
    CHECK_EQ(copied_all(p), text);
    CHECK_EQ(ImTypeset::GetStats().Layouts, layouts + 1);
    CHECK_EQ(ImTypeset::GetParagraphStatus(p), static_cast<int>(ImTypesetStatusFlags_LaidOut));

    // And without it again.
    ImTypeset::SetParagraphText(p, text.c_str());
    CHECK(ImTypeset::LayoutParagraph(p, 400.0f));
    CHECK_EQ(count_of(p, CmdType::ATOM), 0);
    CHECK_EQ(ImTypeset::GetParagraphFormulaCount(p), 0);
    CHECK_EQ(item_count(p), plain);

    // Nothing here trusts its arguments: an index that is not one answers an
    // empty formula.
    for (const ImTypesetFormula none :
         {ImTypeset::GetParagraphFormula(p, 0), ImTypeset::GetParagraphFormula(nullptr, 0),
          ImTypeset::GetParagraphFormula(p, -1), ImTypeset::GetParagraphFormula(p, 1 << 30)}) {
        CHECK_EQ(none.TextStart, 0);
        CHECK_EQ(none.TextEnd, 0);
        CHECK(!none.Refused);
        REQUIRE(none.Reason != nullptr);
        CHECK_EQ(none.Reason[0], '\0');
    }
    CHECK_EQ(ImTypeset::GetParagraphFormulaCount(nullptr), 0);

    CHECK(std::is_standard_layout<ImTypesetFormula>::value);
    CHECK(std::is_trivial<ImTypesetFormula>::value);
}

// --- Set in the paragraph -------------------------------------------------------------

TEST(a_formula_is_a_box_on_the_baseline_among_the_words_in_the_math_face)
{
    Stage stage;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    const std::string text = "so $x^2 + y^2 = r^2$ holds";
    set_math(p, text);
    ImTypeset::LayoutParagraph(p, 600.0f);
    REQUIRE_EQ(ImTypeset::GetParagraphLineCount(p), 1);
    const std::vector<DrawCmd>& cmds = shipped_commands(*p);
    const DrawCmd* line = nullptr;
    const DrawCmd* atom = nullptr;
    int math_glyphs = 0;
    int script_glyphs = 0;
    Scaled before = 0;
    Scaled after = 0;
    for (const DrawCmd& cmd : cmds) {
        if (cmd.is(CmdType::LINE))
            line = &cmd;
        if (cmd.is(CmdType::ATOM))
            atom = &cmd;
        if (!cmd.is(CmdType::GLYPH))
            continue;
        FontSpec font;
        REQUIRE(parse_font_key(cmd.font(), &font));
        if (font.face != Face::MATH) {
            // Words: on the line's baseline, before the formula or after it.
            REQUIRE(line != nullptr);
            CHECK_EQ(cmd.y, line->y);
            if (atom == nullptr)
                before = cmd.x;
            else
                after = after == 0 ? cmd.x : after;
            continue;
        }
        REQUIRE(atom != nullptr);
        // The formula's glyphs carry its index and lie inside its box.
        CHECK_EQ(cmd.index, atom->index);
        CHECK(cmd.x >= atom->x && cmd.x < atom->x + atom->width);
        CHECK(cmd.y <= atom->y + atom->depth && cmd.y >= atom->y - atom->height);
        ++math_glyphs;
        if (font.size < resolve_style(*stage.ctx, stage.ctx->style).em) {
            // A superscript: smaller, and raised.
            ++script_glyphs;
            CHECK(cmd.y < atom->y);
        }
    }
    REQUIRE(atom != nullptr);
    REQUIRE(line != nullptr);
    CHECK_EQ(atom->y, line->y);
    CHECK_EQ(math_glyphs, 8);
    CHECK_EQ(script_glyphs, 3);
    CHECK(before < atom->x);
    CHECK(after >= atom->x + atom->width);

    // It is one item: a point inside it selects all of it, and all of it is
    // one rectangle.
    const double ppi = 96.0;
    const ImVec2 inside(static_cast<float>(px_from_sp(atom->x + atom->width / 3, ppi)),
                        static_cast<float>(px_from_sp(atom->y, ppi)) - 2.0f);
    const ImTypesetTextRange word =
        ImTypeset::SelectParagraphRange(p, inside, inside, ImTypesetGranularity_Word);
    CHECK_EQ(copied(p, word.Start, word.End), std::string("$x^2 + y^2 = r^2$"));
    // In bytes: from its opening dollar to the character after its closing one.
    CHECK_EQ(word.Start, 3);
    CHECK_EQ(word.End, 20);
    const ImTypesetHit hit = ImTypeset::HitTestParagraph(p, inside);
    CHECK_EQ(hit.TextOffset, 3);
    CHECK(hit.Inside);
    const int at = index_of(p, 3);
    CHECK_EQ(at, static_cast<int>(atom->index));
    CHECK_EQ(byte_of(p, at), 3);
    CHECK_EQ(byte_of(p, at + 1), 20);
    ImVector<ImVec4> rects;
    const ImTypesetTextRange one = {3, 20};
    CHECK_EQ(ImTypeset::GetParagraphRangeRects(p, one, &rects), 1);
    // A range that only reaches into it does not hold it.
    const ImTypesetTextRange inside_it = {5, 12};
    CHECK_EQ(ImTypeset::GetParagraphRangeRects(p, inside_it, &rects), 0);

    // The line breaker moves it whole: at no width is it cut.
    for (float width = 30.0f; width < 260.0f; width += 7.0f) {
        CHECK_CONTEXT("width %g", static_cast<double>(width));
        ImTypeset::LayoutParagraph(p, width);
        CHECK_EQ(count_of(p, CmdType::ATOM), 1);
        CHECK_EQ(copied_all(p), text);
        int glyphs = 0;
        for (const DrawCmd& cmd : shipped_commands(*p))
            glyphs += cmd.is(CmdType::GLYPH) ? 1 : 0;
        CHECK_EQ(glyphs, 8 + 7);
    }
}

TEST(a_formula_takes_the_size_and_colour_of_the_run_it_starts_in)
{
    Stage stage;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    const std::string text = "big $x$ and small $y$";
    ImTypesetRun run;
    run.Start = 0;
    run.End = 7;
    run.Scale = 2.0f;
    run.Color = IM_COL32(200, 0, 0, 255);
    ImTypeset::SetParagraphText(p, text.c_str(), nullptr, &run, 1, ImTypesetTextFlags_Math);
    ImTypeset::LayoutParagraph(p, 600.0f);
    std::vector<Scaled> sizes;
    for (const DrawCmd& cmd : shipped_commands(*p)) {
        FontSpec font;
        if (cmd.is(CmdType::GLYPH) && parse_font_key(cmd.font(), &font) && font.face == Face::MATH)
            sizes.push_back(font.size);
    }
    REQUIRE_EQ(sizes.size(), size_t{2});
    CHECK_EQ(sizes[0], 2 * sizes[1]);
    CHECK_EQ(sizes[1], resolve_style(*stage.ctx, stage.ctx->style).em);

    Frame frame;
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    REQUIRE(ImTypeset::DrawParagraphToBuffers(
        p, ImVec2(0.0f, 0.0f), ImVec4(-1e4f, -1e4f, 1e4f, 1e4f), &vertices, &indices));
    int red = 0;
    for (const ImDrawVert& v : vertices)
        red += v.col == IM_COL32(200, 0, 0, 255) ? 1 : 0;
    CHECK_EQ(red, 4 * 4); // b, i, g and the x of the formula
}

TEST(the_space_after_a_formula_is_an_ordinary_one)
{
    Stage stage;
    ImTypeset::GetStyle().Spacing = ImTypesetSpacing_TeX;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    // After "NASA" a period does not end a sentence; after a formula the
    // space factor is neutral again, so the same period does (tex.web section
    // 1076: a box resets it).
    set_math(p, "NASA. x");
    ImTypeset::LayoutParagraph(p, 600.0f);
    REQUIRE(p->block.run.nodes.size() >= 2);
    REQUIRE(p->block.run.nodes[p->block.run.nodes.size() - 2].is(NodeType::GLUE));
    const GlueSpec plain = p->block.run.nodes[p->block.run.nodes.size() - 2].spec();
    set_math(p, "NASA $y$. x");
    ImTypeset::LayoutParagraph(p, 600.0f);
    const std::vector<Node>& nodes = p->block.run.nodes;
    REQUIRE(nodes.back().is(NodeType::CHAR));
    REQUIRE(nodes[nodes.size() - 2].is(NodeType::GLUE));
    const GlueSpec ended = nodes[nodes.size() - 2].spec();
    CHECK(ended.width > plain.width);
    CHECK(ended.stretch > plain.stretch);
}

// --- Refusals ---------------------------------------------------------------------

TEST(a_refused_formula_is_its_source_in_the_code_face_and_says_why_and_where)
{
    Stage stage;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    // The first formula sets; the second holds a command outside the subset
    // after two characters that are not ASCII; the third is an accent.
    const std::string text = "caf\xC3\xA9 $a+b$ puis $\xCE\xB1\xCE\xB2 \\nosuch$ et $\\hat{x}$.";
    set_math(p, text);
    ImTypeset::LayoutParagraph(p, 800.0f);

    const ImTypesetErrorInfo* error = ImTypeset::GetParagraphError(p);
    CHECK_EQ(error->Code, static_cast<int>(ImTypesetError_MathRefused));
    const size_t byte = text.find("\\nosuch");
    CHECK_EQ(error->BytePos, static_cast<int>(byte));
    // One codepoint fewer than bytes for each of the three two-byte characters.
    CHECK_EQ(error->CodepointPos, static_cast<int>(byte) - 3);
    // The reference's sentence, from its parser.
    Formula formula;
    Refusal refusal;
    CHECK(!parse("\xCE\xB1\xCE\xB2 \\nosuch", false, formula, refusal));
    CHECK_EQ(std::string(error->Message), refusal.reason);
    CHECK_EQ(refusal.byte_position, size_t{5});
    CHECK_EQ(refusal.position, size_t{3});
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_MathRefused));

    // Every formula is answered for, not only the first refused.
    REQUIRE_EQ(ImTypeset::GetParagraphFormulaCount(p), 3);
    const ImTypesetFormula first = formula_of(p, 0);
    const ImTypesetFormula second = formula_of(p, 1);
    const ImTypesetFormula third = formula_of(p, 2);
    CHECK(!first.Refused);
    CHECK_EQ(std::string(first.Reason), std::string(""));
    CHECK(second.Refused);
    CHECK_EQ(second.BytePos, error->BytePos);
    CHECK_EQ(second.CodepointPos, error->CodepointPos);
    CHECK_EQ(std::string(second.Reason), std::string(error->Message));
    CHECK(third.Refused);
    CHECK_EQ(std::string(third.Reason),
             std::string("\\hat is an accent, which the math engine does not build"));
    CHECK_EQ(third.BytePos, static_cast<int>(text.find("\\hat")));
    CHECK_EQ(third.CodepointPos, third.BytePos - 3);
    CHECK_EQ(text.substr(static_cast<size_t>(third.TextStart),
                         static_cast<size_t>(third.TextEnd - third.TextStart)),
             std::string("$\\hat{x}$"));

    // One atom; the refused sources are glyphs of the code face, dollars
    // included, at the size of the text, and there is no break inside one.
    CHECK_EQ(count_of(p, CmdType::ATOM), 1);
    std::string code;
    for (const DrawCmd& cmd : shipped_commands(*p)) {
        FontSpec font;
        if (cmd.is(CmdType::GLYPH) && parse_font_key(cmd.font(), &font) &&
            font.face == Face::CODE) {
            CHECK_EQ(font.size, resolve_style(*stage.ctx, stage.ctx->style).em);
            code.push_back(cmd.codepoint < 0x80 ? static_cast<char>(cmd.codepoint) : '?');
        }
    }
    // The two Greek letters are not in the code face and are stood in for
    // from another, as any character a face lacks is.
    CHECK_EQ(code, std::string("$\\nosuch$$\\hat{x}$"));
    CHECK_EQ(copied_all(p), text);
    const int space = index_of(p, static_cast<int>(text.find(" \\nosuch")));
    for (float width = 40.0f; width < 400.0f; width += 9.0f) {
        CHECK_CONTEXT("width %g", static_cast<double>(width));
        ImTypeset::LayoutParagraph(p, width);
        CHECK_EQ(copied_all(p), text);
        // The space inside the refused source never ends a line.
        for (const DrawCmd& cmd : shipped_commands(*p)) {
            if (cmd.is(CmdType::LINE))
                CHECK(cmd.end != static_cast<uint32_t>(space) + 1 || cmd.end == 0);
        }
        CHECK_EQ(index_of(p, static_cast<int>(text.find(" \\nosuch"))), space);
    }

    // New text, no refusal: the error goes with the text it was about.
    set_math(p, "fine $x$");
    ImTypeset::LayoutParagraph(p, 400.0f);
    CHECK_EQ(ImTypeset::GetParagraphError(p)->Code, static_cast<int>(ImTypesetError_None));
    CHECK_EQ(ImTypeset::GetParagraphFormulaCount(p), 1);
}

TEST(a_formula_the_engine_refuses_reports_the_references_sentence)
{
    Stage stage;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    // A radicand taller than the tallest radical sign the math face has:
    // refused by the layout engine, not by the parser (inline.golden #55).
    set_math(p, "Too tall: $\\sqrt{\\sqrt{\\sqrt{\\sqrt{\\sqrt{\\sqrt{x}}}}}}$.");
    ImTypeset::LayoutParagraph(p, 600.0f);
    const ImTypesetErrorInfo* error = ImTypeset::GetParagraphError(p);
    CHECK_EQ(error->Code, static_cast<int>(ImTypesetError_MathRefused));
    CHECK_EQ(std::string(error->Message),
             std::string("what is under the root needs a radical sign 1.04 times as tall as the "
                         "largest the math face has, and one is not assembled from pieces here"));
    CHECK_EQ(error->BytePos, 11);
    CHECK_EQ(error->CodepointPos, 11);
    CHECK_EQ(count_of(p, CmdType::ATOM), 0);
    // Nothing of the attempt is left in the paragraph's arena for a later
    // layout to trip on: the same text lays out the same way again.
    const std::vector<DrawCmd> once = shipped_commands(*p);
    ImTypeset::LayoutParagraph(p, 300.0f);
    ImTypeset::LayoutParagraph(p, 600.0f);
    REQUIRE_EQ(shipped_commands(*p).size(), once.size());
    for (size_t n = 0; n < once.size(); ++n) {
        CHECK_EQ(shipped_commands(*p)[n].x, once[n].x);
        CHECK_EQ(shipped_commands(*p)[n].y, once[n].y);
    }
}

TEST(without_a_math_face_every_formula_is_refused_and_the_text_still_sets)
{
    Stage stage(false);
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    const std::string text = "No face for $x + y$ yet.";
    set_math(p, text);
    ImTypeset::LayoutParagraph(p, 400.0f);
    const ImTypesetErrorInfo* error = ImTypeset::GetParagraphError(p);
    CHECK_EQ(error->Code, static_cast<int>(ImTypesetError_MathRefused));
    CHECK_EQ(std::string(error->Message),
             std::string("the math face could not set it: MissingFontError: math has no bundled "
                         "file and none is registered"));
    CHECK_EQ(error->BytePos, 13);
    CHECK_EQ(count_of(p, CmdType::ATOM), 0);
    CHECK_EQ(copied_all(p), text);
    CHECK(item_count(p) > 20);

    // The face arrives: every layout is stale, and the formula sets.
    REQUIRE(ImTypeset::AddFontFromFileTTF(ImTypesetFace_Math,
                                          font_path("latinmodern-math.otf").c_str()) != nullptr);
    ImTypeset::LayoutParagraph(p, 400.0f);
    CHECK_EQ(ImTypeset::GetParagraphError(p)->Code, static_cast<int>(ImTypesetError_None));
    CHECK_EQ(count_of(p, CmdType::ATOM), 1);
    CHECK_EQ(copied_all(p), text);
}

TEST(nesting_past_the_parsers_bound_is_refused_and_nothing_deeper_reaches_the_engine)
{
    // The parser's bound keeps what it emits inside the engine's, and the
    // engine's keeps its boxes inside the arena's.
    CHECK(2 * MAX_DEPTH + 1 <= MAX_MATH_NESTING);
    CHECK(3 * MAX_MATH_NESTING + 8 < static_cast<int>(NodeArena::MAX_NESTING));

    Stage stage;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    int set = 0;
    int refused = 0;
    int deepest_set = 0;
    for (int depth = 1; depth <= 60; ++depth) {
        CHECK_CONTEXT("depth %d", depth);
        for (int shape = 0; shape < 3; ++shape) {
            std::string source;
            for (int n = 0; n < depth; ++n)
                source += shape == 0 ? "x^{" : (shape == 1 ? "\\frac{" : "{");
            source += "y";
            for (int n = 0; n < depth; ++n)
                source += shape == 1 ? "}{z}" : "}";
            set_math(p, "a $" + source + "$ b");
            ImTypeset::LayoutParagraph(p, 500.0f);
            const ImTypesetErrorInfo* error = ImTypeset::GetParagraphError(p);
            // Set, or refused with a sentence: never a layout that failed.
            if (error->Code == ImTypesetError_None) {
                ++set;
                deepest_set = depth > deepest_set ? depth : deepest_set;
                CHECK_EQ(count_of(p, CmdType::ATOM), 1);
            } else {
                ++refused;
                CHECK_EQ(error->Code, static_cast<int>(ImTypesetError_MathRefused));
                CHECK(error->Message[0] != '\0');
                CHECK_EQ(count_of(p, CmdType::ATOM), 0);
            }
            CHECK_EQ(copied_all(p), "a $" + source + "$ b");
        }
    }
    CHECK(set >= 30);
    CHECK(refused >= 100);
    CHECK(deepest_set <= MAX_DEPTH);
    std::printf("api-math: nesting: %d set (deepest %d levels), %d refused\n", set, deepest_set,
                refused);
}

// --- What the port decides -----------------------------------------------------------

TEST(an_escaped_dollar_is_a_dollar_and_its_backslash_is_not_set)
{
    Stage stage;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    ImTypesetParagraph* plain = ImTypeset::CreateParagraph();
    const auto lines_of = [](ImTypesetParagraph* q) {
        std::vector<std::string> out;
        for (const Node& node : q->block.run.nodes)
            write_node(q->arena, node, out);
        return out;
    };

    // The material is that of the same text written without the backslashes
    // and with math off: the dollar kerns with its neighbours as any other.
    const std::string text = "It costs US\\$5 or \\$6, not $x$.";
    set_math(p, text);
    ImTypeset::LayoutParagraph(p, 600.0f);
    CHECK_EQ(ImTypeset::GetParagraphError(p)->Code, static_cast<int>(ImTypesetError_None));
    ImTypeset::SetParagraphText(plain, "It costs US$5 or $6, not .");
    ImTypeset::LayoutParagraph(plain, 600.0f);
    std::vector<std::string> expected = lines_of(plain);
    std::vector<std::string> actual = lines_of(p);
    REQUIRE_EQ(actual.size(), expected.size() + 7); // the formula's box, written out
    // Up to the formula the two are the same list.
    size_t same = 0;
    while (same < expected.size() && expected[same] == actual[same])
        ++same;
    CHECK(actual[same].compare(0, 4, "box ") == 0);
    CHECK_EQ(actual.back(), expected.back());
    CHECK_EQ(count_of(p, CmdType::ATOM), 1);

    // It copies without the backslashes, and an index still knows its byte.
    CHECK_EQ(copied_all(p), std::string("It costs US$5 or $6, not $x$."));
    for (const char* needle : {"$5", "5 or", "$6", "6, not", "$x$", "."}) {
        CHECK_CONTEXT("%s", needle);
        const int offset = static_cast<int>(text.find(needle));
        const int index = index_of(p, offset);
        CHECK_EQ(byte_of(p, index), offset);
        CHECK_EQ(copied_items(p, index, index + 1), std::string(needle, needle[1] == 'x' ? 3 : 1));
    }
    // The escaping backslash has no item: its offset answers the dollar's.
    const int backslash = static_cast<int>(text.find("\\$6"));
    CHECK_EQ(index_of(p, backslash), index_of(p, backslash + 1));

    // A run boundary between the backslash and its dollar changes nothing
    // but the dollar's face.
    ImTypesetRun run;
    run.Start = static_cast<int>(text.find("$5"));
    run.End = run.Start + 2;
    run.Face = ImTypesetFace_Bold;
    ImTypeset::SetParagraphText(p, text.c_str(), nullptr, &run, 1, ImTypesetTextFlags_Math);
    ImTypeset::LayoutParagraph(p, 600.0f);
    CHECK_EQ(copied_all(p), std::string("It costs US$5 or $6, not $x$."));

    // Any other backslash is set as written, and without the flag all are.
    set_math(p, "a\\b \\\\$x$ \\");
    ImTypeset::LayoutParagraph(p, 600.0f);
    CHECK_EQ(copied_all(p), std::string("a\\b \\\\$x$ \\"));
    CHECK_EQ(count_of(p, CmdType::ATOM), 1);
    ImTypeset::SetParagraphText(p, text.c_str());
    ImTypeset::LayoutParagraph(p, 600.0f);
    CHECK_EQ(copied_all(p), text);
}

TEST(markup_around_a_formula_is_text_and_a_run_does_not_hide_a_dollar)
{
    Stage stage;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    // No markdown here: backquotes and asterisks are set, and what is
    // between the dollars is a formula all the same.
    const std::string text = "code `$x$` and *$y$* stay";
    ImTypesetRun run;
    run.Start = static_cast<int>(text.find('`'));
    run.End = run.Start + 5;
    run.Face = ImTypesetFace_Code;
    run.Flags = ImTypesetRunFlags_NoBreak;
    ImTypeset::SetParagraphText(p, text.c_str(), nullptr, &run, 1, ImTypesetTextFlags_Math);
    ImTypeset::LayoutParagraph(p, 600.0f);
    CHECK_EQ(count_of(p, CmdType::ATOM), 2);
    CHECK_EQ(copied_all(p), text);
    int backquotes = 0;
    int asterisks = 0;
    for (const DrawCmd& cmd : shipped_commands(*p)) {
        backquotes += cmd.is(CmdType::GLYPH) && cmd.codepoint == '`' ? 1 : 0;
        asterisks += cmd.is(CmdType::GLYPH) && cmd.codepoint == '*' ? 1 : 0;
    }
    CHECK_EQ(backquotes, 2);
    CHECK_EQ(asterisks, 2);

    // The host that means the dollars literally escapes them.
    const std::string escaped = "code `\\$x\\$` stays";
    set_math(p, escaped);
    ImTypeset::LayoutParagraph(p, 600.0f);
    CHECK_EQ(count_of(p, CmdType::ATOM), 0);
    CHECK_EQ(copied_all(p), std::string("code `$x$` stays"));

    // With hard breaks a newline inside a formula belongs to the formula.
    set_math(p, "one $a\n+b$ two\nthree", ImTypesetTextFlags_HardBreaks);
    ImTypeset::LayoutParagraph(p, 600.0f);
    CHECK_EQ(count_of(p, CmdType::ATOM), 1);
    CHECK_EQ(ImTypeset::GetParagraphLineCount(p), 2);
    CHECK_EQ(copied_all(p), std::string("one $a\n+b$ two\nthree"));
}

// --- Display blocks ------------------------------------------------------------------

TEST(a_text_that_is_a_display_block_is_centred_in_display_style)
{
    Stage stage;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    ImTypesetStyle style = ImTypeset::GetStyle();
    style.Align = ImTypesetAlign_Left;
    style.FirstLineIndent = 50.0f;
    const auto atom_of = [&]() -> DrawCmd {
        for (const DrawCmd& cmd : shipped_commands(*p)) {
            if (cmd.is(CmdType::ATOM))
                return cmd;
        }
        return DrawCmd();
    };

    // The same sum between single dollars, for its size in text style.
    set_math(p, "$\\sum_{i=1}^{n} i$");
    ImTypeset::LayoutParagraph(p, 400.0f, &style);
    const DrawCmd inline_atom = atom_of();
    REQUIRE(inline_atom.is(CmdType::ATOM));

    for (const char* text :
         {"$$\\sum_{i=1}^{n} i$$", "$$ \\sum_{i=1}^{n}   i $$", "$$\n\\sum_{i=1}^{n}\n   i\n$$",
          "  $$\r\n\\sum_{i=1}^{n} i\r\n$$  \n", "$$\n\n\\sum_{i=1}^{n} i\n\n  $$"}) {
        CHECK_CONTEXT("%s", golden::percent_encode(text).c_str());
        set_math(p, text);
        ImTypeset::LayoutParagraph(p, 400.0f, &style);
        CHECK_EQ(ImTypeset::GetParagraphError(p)->Code, static_cast<int>(ImTypesetError_None));
        REQUIRE_EQ(ImTypeset::GetParagraphFormulaCount(p), 1);
        const ImTypesetFormula formula = formula_of(p, 0);
        CHECK(formula.Display);
        CHECK(!formula.Refused);
        const DrawCmd atom = atom_of();
        REQUIRE(atom.is(CmdType::ATOM));
        // Display style: limits above and below, so narrower and taller.
        CHECK(atom.height + atom.depth > inline_atom.height + inline_atom.depth);
        CHECK(atom.width < inline_atom.width);
        // Centred, whatever the style's alignment and indent: the slack is
        // split to the scaled point.
        const Scaled width = resolve_width(400.0f, resolve_style(*stage.ctx, style));
        const Scaled right = width - (atom.x + atom.width);
        CHECK(atom.x - right >= 0 && atom.x - right <= 1);
        // It copies as its source with white space collapsed.
        CHECK_EQ(copied_all(p), std::string("$$\\sum_{i=1}^{n} i$$"));
    }

    // Wider than the measure it overhangs; it is not broken.
    set_math(p, "$$a+b+c+d+e+f+g+h+i+j+k+l+m+n+o+p+q+r+s+t+u+v+w+x+y+z$$");
    ImTypeset::LayoutParagraph(p, 60.0f, &style);
    CHECK_EQ(ImTypeset::GetParagraphLineCount(p), 1);
    CHECK_EQ(count_of(p, CmdType::ATOM), 1);

    // Not a block: something follows the closing line, or the one line has a
    // `$$` inside or nothing between. The inline rule then reads the text.
    for (const char* text : {"$$\nx\n$$\nmore", "$$x$$ more", "$$ $$", "$$a$$b$$", "$$x$"}) {
        CHECK_CONTEXT("%s", golden::percent_encode(text).c_str());
        set_math(p, text);
        ImTypeset::LayoutParagraph(p, 400.0f, &style);
        CHECK(!p->display_block);
        CHECK_EQ(copied_all(p), std::string(text[2] == '\n' ? "$$ x $$ more" : text));
    }
}

TEST(a_display_block_that_is_refused_or_unclosed_is_its_source_and_says_so)
{
    Stage stage;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();

    // Refused: the position is a place in the text as it was given, although
    // the typesetter was handed the source with its white space collapsed.
    const std::string refused = "$$\n  a  +\n\xCE\xB1 \\nosuch{b}\n$$";
    set_math(p, refused);
    ImTypeset::LayoutParagraph(p, 400.0f);
    const ImTypesetErrorInfo* error = ImTypeset::GetParagraphError(p);
    CHECK_EQ(error->Code, static_cast<int>(ImTypesetError_MathRefused));
    CHECK_EQ(error->BytePos, static_cast<int>(refused.find("\\nosuch")));
    CHECK_EQ(error->CodepointPos, error->BytePos - 1);
    Formula formula;
    Refusal refusal;
    CHECK(!parse("a + \xCE\xB1 \\nosuch{b}", true, formula, refusal));
    CHECK_EQ(std::string(error->Message), refusal.reason);
    CHECK(p->display_block);
    CHECK_EQ(count_of(p, CmdType::ATOM), 0);
    CHECK_EQ(copied_all(p), std::string("$$a + \xCE\xB1 \\nosuch{b}$$"));
    // Its glyphs know where in the text they came from.
    const int at = index_of(p, static_cast<int>(refused.find("\\nosuch")));
    CHECK_EQ(copied_items(p, at, at + 7), std::string("\\nosuch"));
    CHECK_EQ(byte_of(p, at), static_cast<int>(refused.find("\\nosuch")));
    // Long, and a block of its own: it may break.
    ImTypeset::LayoutParagraph(p, 80.0f);
    CHECK(ImTypeset::GetParagraphLineCount(p) > 1);

    // Unclosed: never offered to the typesetter, though what there is of it
    // would set.
    for (const char* text : {"$$", "$$\nx", "$$\n\\sum_{k=0}"}) {
        CHECK_CONTEXT("%s", golden::percent_encode(text).c_str());
        set_math(p, text);
        ImTypeset::LayoutParagraph(p, 400.0f);
        error = ImTypeset::GetParagraphError(p);
        CHECK_EQ(error->Code, static_cast<int>(ImTypesetError_MathRefused));
        CHECK_EQ(std::string(error->Message),
                 std::string("the display block has not been closed yet"));
        CHECK_EQ(error->BytePos, 0);
        CHECK_EQ(error->CodepointPos, 0);
        CHECK_EQ(count_of(p, CmdType::ATOM), 0);
        REQUIRE_EQ(ImTypeset::GetParagraphFormulaCount(p), 1);
        CHECK(formula_of(p, 0).Refused);
        CHECK(formula_of(p, 0).Display);
    }
}

// --- Copy -------------------------------------------------------------------------

// The ruling's end-to-end case: select everything in two paragraphs, one of
// which holds a formula, and copy.
TEST(select_all_over_two_paragraphs_copies_the_formula_as_its_source)
{
    Stage stage;
    ImTypesetParagraph* first = ImTypeset::CreateParagraph();
    ImTypesetParagraph* second = ImTypeset::CreateParagraph();
    const std::string a = "The area of a circle is $\\pi r^2$, and no more.";
    const std::string b = "A second paragraph follows it.";
    set_math(first, a);
    set_math(second, b);
    // Narrow enough that both wrap: a line break made by layout is a space.
    ImTypeset::LayoutParagraph(first, 120.0f);
    ImTypeset::LayoutParagraph(second, 120.0f);
    REQUIRE(ImTypeset::GetParagraphLineCount(first) > 2);
    REQUIRE(ImTypeset::GetParagraphLineCount(second) > 1);
    CHECK_EQ(count_of(first, CmdType::ATOM), 1);

    // Select all as a host does it: a drag from the top-left corner of each
    // paragraph to its bottom-right.
    ImGuiTextBuffer buffer;
    for (const ImTypesetParagraph* p : {first, second}) {
        const ImVec2 size = ImTypeset::GetParagraphSize(p);
        const ImTypesetTextRange all =
            ImTypeset::SelectParagraphRange(p, ImVec2(0.0f, 0.0f), ImVec2(size.x, size.y + 50.0f));
        CHECK_EQ(all.Start, 0);
        CHECK_EQ(all.End, static_cast<int>(p->text.size()));
        ImTypeset::AppendParagraphRangeText(p, all, &buffer);
    }
    CHECK_EQ(std::string(buffer.c_str()),
             std::string("The area of a circle is $\\pi r^2$, and no more.\n\n"
                         "A second paragraph follows it."));

    // A selection that takes in only the formula is only its source, and one
    // that stops short of it leaves it out.
    const int at = index_of(first, static_cast<int>(a.find('$')));
    CHECK_EQ(copied_items(first, at, at + 1), std::string("$\\pi r^2$"));
    CHECK_EQ(copied_items(first, 0, at), std::string("The area of a circle is "));
    CHECK_EQ(copied_items(first, at + 1, item_count(first)), std::string(", and no more."));

    // Several formulas, in order, at a width where each is on its own line.
    const std::string many = "$a$ then $b^2$ then $$c_3$$ then $\\frac{d}{4}$ end";
    set_math(first, many);
    for (float width = 20.0f; width < 400.0f; width += 13.0f) {
        CHECK_CONTEXT("width %g", static_cast<double>(width));
        ImTypeset::LayoutParagraph(first, width);
        CHECK_EQ(count_of(first, CmdType::ATOM), 4);
        CHECK_EQ(copied_all(first), many);
    }
}

// --- The performance contract, with math on -----------------------------------------

TEST(a_steady_frame_with_formulas_lays_nothing_out_and_allocates_nothing)
{
    Stage stage;
    const std::string text =
        "Inline, a sum keeps its limits beside it, as in $\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2}$, "
        "and the roots are $x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}$, while $\\hat{x}$ is "
        "refused and \\$5 is five dollars. Typesetting is the composition of text by arranging "
        "physical type or its digital equivalents, and a formula is a box among the words.";
    const std::string block = "$$\n\\left( \\sum_{k=0}^{\\infty} \\frac{x^k}{k!} \\right)^2\n$$";
    Cost before, after;
    const auto frame = [&](float width) {
        Frame f;
        begin_window("math", width);
        before = Cost::now();
        // The first holds a formula outside the subset: shown, and reported.
        CHECK(!ImTypeset::TextUnformatted(text.c_str(), nullptr, ImTypesetTextFlags_Math));
        CHECK(ImTypeset::TextUnformatted(block.c_str(), nullptr, ImTypesetTextFlags_Math));
        after = Cost::now();
        ImGui::End();
    };
    frame(420.0f);
    CHECK_EQ(after.stats.TextBuilds - before.stats.TextBuilds, 2);
    CHECK_EQ(after.stats.Layouts - before.stats.Layouts, 2);

    // The second frame: no text is built, nothing is broken, no formula is
    // parsed or set, and nothing is allocated; both paragraphs are drawn.
    frame(420.0f);
    CHECK_EQ(after.stats.TextBuilds - before.stats.TextBuilds, 0);
    CHECK_EQ(after.stats.Layouts - before.stats.Layouts, 0);
    CHECK_EQ(after.stats.DrawCalls - before.stats.DrawCalls, 2);
    CHECK(after.stats.VerticesEmitted - before.stats.VerticesEmitted > 4 * 250);
    CHECK_EQ(after.allocations - before.allocations, size_t{0});
    CHECK_EQ(after.stats.CachedParagraphs, 2);

    // A new width reflows from the same material: the formulas are not set again.
    frame(300.0f);
    CHECK_EQ(after.stats.TextBuilds - before.stats.TextBuilds, 0);
    CHECK_EQ(after.stats.Layouts - before.stats.Layouts, 2);
    frame(300.0f);
    CHECK_EQ(after.stats.Layouts - before.stats.Layouts, 0);
    CHECK_EQ(after.allocations - before.allocations, size_t{0});

    // The same holds for a paragraph a host keeps itself: laid out, drawn
    // with a selection, hit-tested and asked for its rectangles. (Copying is
    // not in the loop: src/selection's text() allocates, with or without a
    // formula, and a copy is a keystroke and not a frame.)
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    set_math(p, text);
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    ImVector<ImVec4> rects;
    const auto kept = [&]() {
        Frame f;
        vertices.resize(0);
        indices.resize(0);
        rects.resize(0);
        before = Cost::now();
        ImTypeset::LayoutParagraph(p, 380.0f);
        const ImTypesetTextRange all = {0, 1 << 30};
        ImTypeset::DrawParagraphToBuffers(p, ImVec2(10.0f, 10.0f), ImVec4(0, 0, 800, 600),
                                          &vertices, &indices, &all);
        ImTypeset::HitTestParagraph(p, ImVec2(100.0f, 30.0f));
        ImTypeset::GetParagraphRangeRects(p, all, &rects);
        ImTypeset::GetParagraphFormula(p, 2);
        ImTypeset::GetParagraphLine(p, 1);
        after = Cost::now();
    };
    kept();
    kept();
    kept();
    CHECK_EQ(after.stats.TextBuilds - before.stats.TextBuilds, 0);
    CHECK_EQ(after.stats.Layouts - before.stats.Layouts, 0);
    CHECK_EQ(after.allocations - before.allocations, size_t{0});
    CHECK(vertices.Size > 4 * 200);
}

TEST(a_formula_refused_in_a_text_widget_is_that_calls_answer_on_every_frame)
{
    Stage stage;
    const char* text = "caf\xC3\xA9: $\\hat{x}$ is not set.";
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_None));
    for (int n = 0; n < 3; ++n) {
        CHECK_CONTEXT("frame %d", n);
        Frame f;
        begin_window("refused", 400.0f);
        CHECK(ImTypeset::TextUnformatted("Nothing to refuse here."));
        CHECK(!ImTypeset::TextUnformatted(text, nullptr, ImTypesetTextFlags_Math));
        const ImTypesetErrorInfo* error = ImTypeset::GetLastError();
        CHECK_EQ(error->Code, static_cast<int>(ImTypesetError_MathRefused));
        CHECK_EQ(std::string(error->Message),
                 std::string("\\hat is an accent, which the math engine does not build"));
        CHECK_EQ(error->BytePos, 8);
        CHECK_EQ(error->CodepointPos, 7);
        // The same words without the flag are words.
        CHECK(ImTypeset::TextUnformatted(text));
        CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_None));
        ImGui::End();
    }
    // The flag is part of what the text is: three layouts, not two.
    CHECK_EQ(ImTypeset::GetStats().Layouts, 3);
}

// --- Pixels ---------------------------------------------------------------------------

namespace
{

// The synthetic codepoints the reference mints for glyphs a math face reaches
// only through its variant chains: no cmap of the font file maps them.
bool is_minted(uint32_t codepoint)
{
    return codepoint >= 0xFFF1Bu && codepoint <= 0xFFFFDu;
}

struct Ink {
    long inside = 0;
    long outside = 0;
};

// Pixels that are not the background, inside a rectangle and in the rest of
// the image.
Ink ink_in(const soft_raster::Image& image, ImU32 background, double x0, double y0, double x1,
           double y1)
{
    Ink out;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            if (image.get(x, y) == background)
                continue;
            const bool in = x >= x0 && x < x1 && y >= y0 && y < y1;
            ++(in ? out.inside : out.outside);
        }
    }
    return out;
}

// Draws one paragraph alone on a white page and returns the image.
soft_raster::Image rendered(soft_raster::Renderer& renderer, ImTypesetParagraph* p, ImVec2 pos)
{
    const ImU32 white = IM_COL32(255, 255, 255, 255);
    soft_raster::Image image(480, 240, white);
    // The atlas bakes a glyph when it is first asked for; the texture reaches
    // the renderer with that frame's draw data.
    for (int n = 0; n < 3; ++n) {
        ImGui::NewFrame();
        ImGui::PushStyleColor(ImGuiCol_WindowBg, white);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        begin_window("page", 480.0f, 240.0f);
        ImTypeset::DrawParagraph(p, ImGui::GetWindowDrawList(), pos);
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::Render();
        image.clear(white);
        renderer.render(ImGui::GetDrawData(), image);
    }
    return image;
}

} // namespace

TEST(a_minted_variant_glyph_reaches_the_atlas_and_inks_pixels_inside_the_formulas_box)
{
    Stage stage;
    soft_raster::Renderer renderer;
    renderer.init();
    const ImU32 white = IM_COL32(255, 255, 255, 255);
    ImTypesetStyle style = ImTypeset::GetStyle();
    style.EmSize = 28.0f;
    const double ppi = 96.0;
    const ImVec2 pos(20.0f, 20.0f);
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    const FaceData* math_face = stage.ctx->fonts.faces[static_cast<int>(Face::MATH)].data;
    REQUIRE(math_face != nullptr);

    // A display-size summation, alone: one glyph, the next of the font's
    // variant chain, which the font file's cmap does not map.
    set_math(p, "$$\\sum$$");
    ImTypeset::LayoutParagraph(p, 440.0f, &style);
    REQUIRE_EQ(ImTypeset::GetParagraphError(p)->Code, static_cast<int>(ImTypesetError_None));
    const DrawCmd* atom = nullptr;
    const DrawCmd* glyph = nullptr;
    int glyphs = 0;
    for (const DrawCmd& cmd : shipped_commands(*p)) {
        if (cmd.is(CmdType::ATOM))
            atom = &cmd;
        if (cmd.is(CmdType::GLYPH)) {
            glyph = &cmd;
            ++glyphs;
        }
    }
    REQUIRE(atom != nullptr);
    REQUIRE_EQ(glyphs, 1);
    CHECK(is_minted(glyph->codepoint));
    // What the atlas is asked for: the codepoint itself where ImWchar can
    // hold it, and a private-use stand-in in the BMP where it cannot.
    const uint32_t asked = atlas_codepoint(*math_face, glyph->codepoint);
    if (sizeof(ImWchar) == 2) {
        CHECK(asked >= 0xE000u && asked <= 0xF8FFu);
    } else {
        CHECK_EQ(asked, glyph->codepoint);
    }
    // The summation in text style is the font's own U+2211, and smaller.
    {
        ImTypesetParagraph* small = ImTypeset::CreateParagraph();
        set_math(small, "$\\sum$");
        ImTypeset::LayoutParagraph(small, 440.0f, &style);
        for (const DrawCmd& cmd : shipped_commands(*small)) {
            if (cmd.is(CmdType::GLYPH))
                CHECK_EQ(cmd.codepoint, 0x2211u);
            if (cmd.is(CmdType::ATOM))
                CHECK(cmd.height + cmd.depth < atom->height + atom->depth);
        }
    }

    soft_raster::Image image = rendered(renderer, p, pos);
    // The formula's box, in pixels, with the pixel a snapped glyph's
    // antialiased edge can reach on each side.
    const auto px = [&](Scaled sp) { return px_from_sp(sp, ppi); };
    Ink ink =
        ink_in(image, white, pos.x + px(atom->x) - 1.0, pos.y + px(atom->y - atom->height) - 1.0,
               pos.x + px(atom->x + atom->width) + 1.0, pos.y + px(atom->y + atom->depth) + 1.0);
    const double area = px(atom->width) * px(atom->height + atom->depth);
    std::printf("api-math: display summation U+%X (asked of the atlas as U+%X): %ld inked pixels "
                "in a box of %.0f, %ld outside it; %s, %d-bit ImWchar\n",
                glyph->codepoint, asked, ink.inside, area, ink.outside,
                ImGui::GetIO().Fonts->FontLoaderName, static_cast<int>(sizeof(ImWchar) * 8));
    // A summation sign is strokes around white: a fifth of its box at least,
    // and never all of it, which is what a missing-glyph rectangle would be.
    CHECK(static_cast<double>(ink.inside) > 0.15 * area);
    CHECK(static_cast<double>(ink.inside) < 0.75 * area);
    CHECK_EQ(ink.outside, 0L);

    // Tall delimiters around a fraction of fractions: each a minted variant.
    // The ink of each is looked for in its own box, where nothing else is.
    set_math(p, "so $\\left( \\frac{\\frac{a}{b}}{\\frac{c}{d}} \\right)$ is");
    ImTypeset::LayoutParagraph(p, 440.0f, &style);
    REQUIRE_EQ(ImTypeset::GetParagraphError(p)->Code, static_cast<int>(ImTypesetError_None));
    image = rendered(renderer, p, pos);
    int delimiters = 0;
    atom = nullptr;
    for (const DrawCmd& cmd : shipped_commands(*p)) {
        if (cmd.is(CmdType::ATOM))
            atom = &cmd;
        if (!cmd.is(CmdType::GLYPH) || !is_minted(cmd.codepoint))
            continue;
        REQUIRE(atom != nullptr);
        CHECK_EQ(cmd.index, atom->index);
        ++delimiters;
        const CharDimens dimens = stage.ctx->measure()(cmd.font(), cmd.codepoint);
        // Taller than the text around it: that is what the variant is for.
        CHECK(dimens.height + dimens.depth > resolve_style(*stage.ctx, style).em);
        // Inside the formula's box.
        CHECK(cmd.x >= atom->x && cmd.x + dimens.width <= atom->x + atom->width);
        CHECK(cmd.y - dimens.height >= atom->y - atom->height);
        CHECK(cmd.y + dimens.depth <= atom->y + atom->depth);
        ink = ink_in(image, white, pos.x + px(cmd.x), pos.y + px(cmd.y - dimens.height),
                     pos.x + px(cmd.x + dimens.width), pos.y + px(cmd.y + dimens.depth));
        const double box = px(dimens.width) * px(dimens.height + dimens.depth);
        CHECK(static_cast<double>(ink.inside) > 0.08 * box);
        CHECK(static_cast<double>(ink.inside) < 0.60 * box);
        std::printf("api-math: delimiter U+%X: %ld inked pixels in its box of %.0f\n",
                    cmd.codepoint, ink.inside, box);
    }
    CHECK_EQ(delimiters, 2);
    renderer.shutdown();
}
