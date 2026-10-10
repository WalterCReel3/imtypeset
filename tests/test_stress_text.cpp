// Hostile and oversized text through the public API, and nothing but the
// public API: include/imtypeset.h and imgui.h. What a host can pass, a host
// will pass -- bytes that are not UTF-8, NULs inside a length, a megabyte with
// no space in it, a hundred thousand words, a width that is not a number.
//
// Every test asserts what the header promises of an answer (a size that is a
// finite number of pixels, an index inside the paragraph, an offset inside the
// text) and a bound on the time where the input is large. Most of what these
// inputs can break shows only under ASan and UBSan, which is where this is
// meant to be run; each test says so where that is all it can fail by.
//
// IMTYPESET_STRESS_ITERS multiplies the iteration counts (default 1).

#include "check.h"
#include "golden.h"

#include "imgui.h"
#include "imtypeset.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <vector>

// Blocks handed out and not yet returned, by every form of operator new and by
// ImGui's allocator: what "allocation count steady" is measured in. Not
// inlined: GCC, seeing through an inlined replacement to malloc and free,
// reports the pair as mismatched.
#if defined(__GNUC__)
#define TEST_STRESS_NOINLINE __attribute__((noinline))
#else
#define TEST_STRESS_NOINLINE
#endif

static long long g_live = 0;
static long long g_allocations = 0;

TEST_STRESS_NOINLINE void* operator new(size_t size)
{
    ++g_live;
    ++g_allocations;
    void* const memory = std::malloc(size != 0 ? size : 1);
    if (memory == nullptr)
        std::abort();
    return memory;
}
TEST_STRESS_NOINLINE void* operator new(size_t size, const std::nothrow_t&) noexcept
{
    void* const memory = std::malloc(size != 0 ? size : 1);
    if (memory != nullptr) {
        ++g_live;
        ++g_allocations;
    }
    return memory;
}
TEST_STRESS_NOINLINE void* operator new[](size_t size)
{
    return operator new(size);
}
TEST_STRESS_NOINLINE void* operator new[](size_t size, const std::nothrow_t& tag) noexcept
{
    return operator new(size, tag);
}
TEST_STRESS_NOINLINE void operator delete(void* memory) noexcept
{
    if (memory != nullptr)
        --g_live;
    std::free(memory);
}
TEST_STRESS_NOINLINE void operator delete(void* memory, size_t) noexcept
{
    operator delete(memory);
}
TEST_STRESS_NOINLINE void operator delete(void* memory, const std::nothrow_t&) noexcept
{
    operator delete(memory);
}
TEST_STRESS_NOINLINE void operator delete[](void* memory) noexcept
{
    operator delete(memory);
}
TEST_STRESS_NOINLINE void operator delete[](void* memory, size_t) noexcept
{
    operator delete(memory);
}
TEST_STRESS_NOINLINE void operator delete[](void* memory, const std::nothrow_t&) noexcept
{
    operator delete(memory);
}

namespace
{

void* counted_alloc(size_t size, void*)
{
    ++g_live;
    ++g_allocations;
    return std::malloc(size != 0 ? size : 1);
}

void counted_free(void* memory, void*)
{
    if (memory != nullptr)
        --g_live;
    std::free(memory);
}

double seconds_now()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

int scale()
{
    const char* const text = std::getenv("IMTYPESET_STRESS_ITERS");
    if (text == nullptr)
        return 1;
    const int value = std::atoi(text);
    return value < 1 ? 1 : (value > 10000 ? 10000 : value);
}

struct Random {
    uint64_t state;

    explicit Random(uint64_t seed)
        : state(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull)
    {
    }
    uint64_t next()
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }
    int below(int bound) { return static_cast<int>(next() % static_cast<uint64_t>(bound)); }
    bool one_in(int n) { return below(n) == 0; }
};

const struct {
    ImTypesetFace face;
    const char* file;
} FONTS[] = {
    {ImTypesetFace_Regular, "Roboto-Regular.ttf"},
    {ImTypesetFace_Bold, "Roboto-Bold.ttf"},
    {ImTypesetFace_Italic, "Roboto-RegularItalic.ttf"},
    {ImTypesetFace_BoldItalic, "Roboto-BoldItalic.ttf"},
    {ImTypesetFace_Code, "Inconsolata-Medium.ttf"},
    {ImTypesetFace_Math, "latinmodern-math.otf"},
};

// A headless ImGui context with an ImTypeset context in it and the bundled
// fonts added through the public API.
struct Stage {
    explicit Stage(bool math = false)
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
        if (ImTypeset::CreateContext() == nullptr) {
            FAIL("CreateContext: %s", ImTypeset::GetLastError()->Message);
            return;
        }
        for (const auto& entry : FONTS) {
            const std::string path = golden::data_path(std::string("fonts/") + entry.file);
            if (ImTypeset::AddFontFromFileTTF(entry.face, path.c_str()) == nullptr)
                FAIL("%s: %s", entry.file, ImTypeset::GetLastError()->Message);
        }
        ImTypeset::GetStyle().EmSize = 16.0f;
        // Formulas are the text's to ask for, with ImTypesetTextFlags_Math.
        (void)math;
    }
    ~Stage()
    {
        ImTypeset::DestroyContext();
        ImGui::DestroyContext();
    }
    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;
};

struct Frame {
    Frame() { ImGui::NewFrame(); }
    ~Frame() { ImGui::Render(); }
};

const ImGuiWindowFlags PLAIN =
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;

void begin_window(float width = 600.0f, float height = 500.0f)
{
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin("stress", nullptr, PLAIN);
}

bool finite(float value)
{
    return value == value && value >= -1.0e30f && value <= 1.0e30f;
}

// Whether a style asks for type so large that drawing a text of many different
// characters in it would not fit ImGui's font atlas. That is ImGui's limit and
// ImGui's assertion ("Out of texture memory", in its font loader), met by any
// ImGui text at such a size; docs/hardening.md records it. Layout does not
// rasterise, so such a paragraph is laid out and asked everything else, and
// large type is drawn by the test that draws two letters of it.
bool large_type(const ImTypesetStyle& style)
{
    const float size = style.EmSize > 0.0f ? style.EmSize : style.FontSize;
    return !(size <= 40.0f);
}

// Everything the header lets a host ask of a paragraph that has been laid
// out, with what it promises of each answer. `text_size` is the size of the
// text the paragraph was given.
//
// `draw` is false for a paragraph laid out at a size its glyphs should not be
// rasterised at: see large_type() below.
// A layout and the size it gave.
ImVec2 laid_out(ImTypesetParagraph* p, float width, const ImTypesetStyle* style = nullptr)
{
    ImTypeset::LayoutParagraph(p, width, style);
    return ImTypeset::GetParagraphSize(p);
}

void interrogate(ImTypesetParagraph* p, int text_size, Random& random,
                 ImVector<ImDrawVert>& vertices, ImVector<ImDrawIdx>& indices, bool draw = true)
{
    const ImVec2 size = ImTypeset::GetParagraphSize(p);
    CHECK(finite(size.x) && finite(size.y));
    CHECK(size.x >= 0.0f && size.y >= 0.0f);
    const int lines = ImTypeset::GetParagraphLineCount(p);
    // Positions are bytes of the text, whatever is asked and however.
    const int length = text_size;
    CHECK(lines >= 0);
    const ImTypesetErrorInfo* const error = ImTypeset::GetParagraphError(p);
    REQUIRE(error != nullptr);
    REQUIRE(error->Message != nullptr);
    CHECK(error->BytePos >= -1 && error->BytePos <= text_size);
    (void)ImTypeset::GetParagraphStatus(p);

    const float xs[] = {-1.0e30f,
                        -5.0f,
                        0.0f,
                        size.x * 0.5f,
                        size.x + 3.0f,
                        1.0e30f,
                        std::numeric_limits<float>::quiet_NaN(),
                        std::numeric_limits<float>::infinity()};
    for (int k = 0; k < 6; ++k) {
        const ImVec2 at(xs[random.below(8)], xs[random.below(8)]);
        const ImTypesetHit hit = ImTypeset::HitTestParagraph(p, at);
        CHECK(hit.TextOffset >= 0 && hit.TextOffset <= length);
        CHECK(hit.Line >= -1 && hit.Line < std::max(lines, 1));
        if (hit.Line >= 0) {
            const ImTypesetLine line = ImTypeset::GetParagraphLine(p, hit.Line);
            CHECK(finite(line.Min.x) && finite(line.Min.y) && finite(line.Max.x) &&
                  finite(line.Max.y) && finite(line.Baseline));
            CHECK(line.TextStart >= 0 && line.TextStart <= line.TextEnd && line.TextEnd <= length);
        }
        const ImVec2 to(static_cast<float>(random.below(700)),
                        static_cast<float>(random.below(400)));
        const ImTypesetTextRange chosen = ImTypeset::SelectParagraphRange(
            p, at, to, static_cast<ImTypesetGranularity>(random.below(3)));
        CHECK(chosen.Start >= 0 && chosen.End <= length);
    }

    const int probes[] = {
        std::numeric_limits<int>::min(), -1, 0, 1, length / 2, length - 1, length, length + 1,
        std::numeric_limits<int>::max()};
    ImVector<ImVec4> rects;
    ImGuiTextBuffer copy;
    for (int k = 0; k < 6; ++k) {
        const ImTypesetTextRange selection = {probes[random.below(9)], probes[random.below(9)]};
        const int count = ImTypeset::GetParagraphRangeRects(p, selection, &rects);
        CHECK(count >= 0 && count <= rects.Size);
        for (const ImVec4& rect : rects)
            CHECK(finite(rect.x) && finite(rect.y) && finite(rect.z) && finite(rect.w));
        const int appended = ImTypeset::AppendParagraphRangeText(p, selection, &copy);
        CHECK(appended >= 0 && appended <= copy.size());
        const ImVec4 caret = ImTypeset::GetParagraphCaretRect(p, selection.Start);
        CHECK(finite(caret.x) && finite(caret.y) && finite(caret.z) && finite(caret.w));
        // Bytes to codepoints and back never leave the text.
        const int codepoint = ImTypeset::ParagraphByteToCodepointOffset(p, selection.End);
        CHECK(codepoint >= 0 && codepoint <= length);
        const int offset = ImTypeset::ParagraphCodepointToByteOffset(p, codepoint);
        CHECK(offset >= 0 && offset <= length);
    }
    for (const int line : {-1, 0, lines - 1, lines, std::numeric_limits<int>::max()}) {
        const ImTypesetLine got = ImTypeset::GetParagraphLine(p, line);
        CHECK(got.TextStart >= 0 && got.TextEnd <= length);
    }
    // Whatever was copied is a C string no longer than a few times the text:
    // six copies, each at most the text and its separators.
    CHECK(copy.size() <= 6 * (4 * text_size + 64));

    if (!draw)
        return;
    const ImVec4 clips[] = {ImVec4(0.0f, 0.0f, 800.0f, 600.0f), ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
                            ImVec4(-1.0e30f, -1.0e30f, 1.0e30f, 1.0e30f),
                            ImVec4(50.0f, 20.0f, 10.0f, 5.0f)};
    const ImTypesetTextRange all = {0, length};
    vertices.resize(0);
    indices.resize(0);
    const bool drawn =
        ImTypeset::DrawParagraphToBuffers(p, ImVec2(3.0f, 4.0f), clips[random.below(4)], &vertices,
                                          &indices, random.one_in(2) ? &all : nullptr);
    if (!drawn)
        CHECK(vertices.Size == 0 && indices.Size == 0);
    for (const ImDrawIdx index : indices) {
        if (static_cast<int>(index) >= vertices.Size) {
            FAIL("an index past the vertices: %d of %d", static_cast<int>(index), vertices.Size);
            break;
        }
    }
    ImTypeset::DrawParagraph(p, ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(),
                             random.one_in(2) ? &all : nullptr);
}

} // namespace

// --- Bytes that are not text ---------------------------------------------------------

// Every way a byte string fails to be UTF-8, as fixed cases: a decoder that
// reads a continuation byte without looking can run past the end on each of
// the truncated ones. Fails only under ASan, apart from the range checks.
TEST(invalid_utf8_is_set_without_reading_past_the_end)
{
    Stage stage;
    static const struct {
        const char* bytes;
        int size;
    } CASES[] = {
        {"\xC3", 1},                     // a lead byte and nothing
        {"\xE2\x82", 2},                 // two of three
        {"\xF0\x9F\x98", 3},             // three of four
        {"\xF0", 1},                     //
        {"a\xF0\x9F", 3},                //
        {"\x80\x80\x80", 3},             // continuation bytes alone
        {"\xC0\xAF", 2},                 // overlong
        {"\xE0\x80\xAF", 3},             // overlong
        {"\xF0\x80\x80\xAF", 4},         // overlong
        {"\xED\xA0\x80", 3},             // a surrogate
        {"\xED\xBF\xBF", 3},             //
        {"\xF4\x90\x80\x80", 4},         // past U+10FFFF
        {"\xF8\x88\x80\x80\x80", 5},     // five-byte form
        {"\xFC\x84\x80\x80\x80\x80", 6}, // six-byte form
        {"\xFE\xFF", 2},                 // never valid
        {"\xEF\xBB\xBFtext", 7},         // a byte order mark
        {"\xEF\xBF\xBE\xEF\xBF\xBF", 6}, // noncharacters
        {"a\0b\0\0c", 6},                // NULs inside the length
        {"\0", 1},                       //
        {"\0\0\0\0", 4},                 //
        {"$\0$", 3},                     //
        {"ab \xC3\x28 cd \xA0\xA1 ef", 14},
        {"\xF3\xB0\x80\x80 \xF4\x8F\xBF\xBD", 9},         // plane 15 and 16: private use
        {"\xE2\x80\xA8line\xE2\x80\xA9para\x0B\x0C", 16}, // Unicode line and paragraph ends
        {"\r\n\r\r\n\n", 6},
        {"", 0},
    };
    Random random(1);
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    REQUIRE(p != nullptr);
    Frame frame;
    begin_window();
    for (const auto& entry : CASES) {
        for (int flags = 0; flags < 8; ++flags) {
            CHECK_CONTEXT("%d bytes starting %02X, flags %d", entry.size,
                          entry.size > 0 ? static_cast<unsigned char>(entry.bytes[0]) : 0, flags);
            // An exact-size heap copy, so that one byte too far is ASan's.
            std::vector<char> exact(entry.bytes, entry.bytes + entry.size);
            ImTypeset::SetParagraphText(p, exact.data(), exact.data() + exact.size(), nullptr, 0,
                                        flags);
            ImTypeset::LayoutParagraph(p, 200.0f);
            const ImVec2 size = ImTypeset::GetParagraphSize(p);
            CHECK(finite(size.x) && finite(size.y));
            interrogate(p, entry.size, random, vertices, indices);
            // The same bytes measured, and counted, with no paragraph.
            const ImVec2 measured =
                ImTypeset::CalcTextSize(exact.data(), exact.data() + exact.size(), 200.0f, flags);
            CHECK_EQ(measured.y, size.y);
            const int codepoints = ImTypeset::TextByteToCodepointOffset(
                exact.data(), entry.size, exact.data() + exact.size());
            CHECK(codepoints >= 0 && codepoints <= entry.size);
            CHECK_EQ(ImTypeset::TextCodepointToByteOffset(exact.data(), codepoints,
                                                          exact.data() + exact.size()),
                     entry.size);
        }
    }
    // A null end means a C string, and a null text none at all.
    ImTypeset::SetParagraphText(p, "plain\0hidden");
    ImTypeset::LayoutParagraph(p, 200.0f);
    // Five letters, and not the eleven.
    CHECK_EQ(ImTypeset::GetParagraphLine(p, 0).TextEnd, 5);
    CHECK_EQ(ImTypeset::HitTestParagraph(p, ImVec2(1.0e6f, 5.0f)).TextOffset, 5);
    ImTypeset::SetParagraphText(p, nullptr, nullptr);
    ImTypeset::LayoutParagraph(p, 200.0f);
    CHECK_EQ(ImTypeset::GetParagraphLine(p, 0).TextEnd, 0);
    CHECK_EQ(ImTypeset::HitTestParagraph(p, ImVec2(1.0e6f, 5.0f)).TextOffset, 0);
    const char* const backwards = "backwards";
    ImTypeset::SetParagraphText(p, backwards + 4, backwards);
    ImTypeset::LayoutParagraph(p, 200.0f);
    ImTypeset::DestroyParagraph(p);
    ImGui::End();
}

namespace
{

// A buffer of `size` bytes in one of several hostile mixtures.
void hostile_text(Random& random, int size, std::vector<char>& out)
{
    static const char* const PIECES[] = {
        " ",
        "  ",
        "\n",
        "\t",
        "word",
        "$",
        "$$",
        "\\",
        "{",
        "}",
        "^",
        "_",
        "\\frac",
        "x",
        "é",
        "漢",
        "\xF0\x9F\x98\x80",
        "\xCC\x81",
        "\xE2\x80\x8B",
        "\xC2\xA0",
        ".",
        "!",
        "-",
        "\\left(",
        "\\right)",
        "\\sqrt",
        "\\text{",
        "\\$",
        "\xEF\xBF\xBD",
        "A.",
        "a  b",
    };
    out.clear();
    const int mode = random.below(4);
    while (static_cast<int>(out.size()) < size) {
        if (mode == 0 || (mode == 3 && random.one_in(4))) {
            out.push_back(static_cast<char>(random.below(256)));
        } else if (mode == 1) {
            // Bytes weighted towards the UTF-8 lead and continuation ranges.
            static const unsigned char RANGES[][2] = {{0x00, 0x00}, {0x20, 0x7E}, {0x80, 0xBF},
                                                      {0xC0, 0xDF}, {0xE0, 0xEF}, {0xF0, 0xFF}};
            const unsigned char* const range = RANGES[random.below(6)];
            out.push_back(static_cast<char>(range[0] + random.below(range[1] - range[0] + 1)));
        } else {
            const char* const piece = PIECES[random.below(IM_ARRAYSIZE(PIECES))];
            out.insert(out.end(), piece, piece + std::strlen(piece));
        }
    }
    out.resize(static_cast<size_t>(size));
}

ImTypesetRun hostile_run(Random& random, int text_size)
{
    static const int EDGES[] = {std::numeric_limits<int>::min(), -1, 0, 1,
                                std::numeric_limits<int>::max()};
    static const float SCALES[] = {1.0f,
                                   0.0f,
                                   -1.0f,
                                   1.0e-30f,
                                   0.25f,
                                   3.0f,
                                   1.0e30f,
                                   std::numeric_limits<float>::quiet_NaN(),
                                   std::numeric_limits<float>::infinity()};
    ImTypesetRun run;
    run.Start = random.one_in(5) ? EDGES[random.below(5)] : random.below(text_size + 2);
    run.End = random.one_in(5) ? EDGES[random.below(5)] : random.below(text_size + 2);
    run.Face = random.one_in(6) ? random.below(4000) - 2000 : random.below(9);
    run.Scale = SCALES[random.below(random.one_in(3) ? 9 : 1)];
    run.Color = static_cast<ImU32>(random.next());
    run.Flags = random.one_in(4) ? static_cast<int>(random.next()) : random.below(2);
    return run;
}

ImTypesetStyle hostile_style(Random& random)
{
    static const float VALUES[] = {std::numeric_limits<float>::quiet_NaN(),
                                   std::numeric_limits<float>::infinity(),
                                   -std::numeric_limits<float>::infinity(),
                                   -1.0f,
                                   0.0f,
                                   1.0e-30f,
                                   0.5f,
                                   7.0f,
                                   16.0f,
                                   300.0f,
                                   1.0e9f,
                                   1.0e30f,
                                   std::numeric_limits<float>::max(),
                                   -std::numeric_limits<float>::max()};
    const auto value = [&]() { return VALUES[random.below(IM_ARRAYSIZE(VALUES))]; };
    ImTypesetStyle style;
    style.EmSize = 16.0f;
    if (random.one_in(2))
        style.Face = random.one_in(4) ? random.below(4000) - 2000 : random.below(9);
    if (random.one_in(3))
        style.FontSize = value();
    if (random.one_in(3))
        style.EmSize = value();
    if (random.one_in(3))
        style.LineHeight = value();
    if (random.one_in(2))
        style.Align = random.one_in(6) ? static_cast<int>(random.next()) : random.below(4);
    if (random.one_in(3))
        style.LeftIndent = value();
    if (random.one_in(3))
        style.FirstLineIndent = value();
    if (random.one_in(3))
        style.Spacing = random.one_in(4) ? static_cast<int>(random.next()) : random.below(3);
    if (random.one_in(2))
        style.LineBreak = random.one_in(6) ? static_cast<int>(random.next()) : random.below(3);
    if (random.one_in(4))
        style.PixelsPerInch = value();
    return style;
}

float hostile_width(Random& random)
{
    static const float VALUES[] = {std::numeric_limits<float>::quiet_NaN(),
                                   std::numeric_limits<float>::infinity(),
                                   -std::numeric_limits<float>::infinity(),
                                   -300.0f,
                                   0.0f,
                                   1.0e-30f,
                                   0.4f,
                                   1.0f,
                                   1.0e9f,
                                   1.0e30f,
                                   std::numeric_limits<float>::max()};
    return random.one_in(3) ? VALUES[random.below(IM_ARRAYSIZE(VALUES))]
                            : static_cast<float>(random.below(900)) * 0.75f;
}

} // namespace

// Random bytes, random runs, random styles and random widths, each paragraph
// then asked everything. Sizes run from nothing to a few kilobytes.
TEST(fuzz_random_bytes_runs_styles_and_widths)
{
    Stage stage(true);
    const int rounds = 450 * scale();
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    std::vector<char> text;
    std::vector<ImTypesetRun> runs;
    ImTypesetParagraph* pool[4];
    for (ImTypesetParagraph*& p : pool)
        p = ImTypeset::CreateParagraph();
    double slowest = 0.0;
    int refused = 0;
    for (int round = 0; round < rounds; ++round) {
        CHECK_CONTEXT("round %d", round);
        Random random(static_cast<uint64_t>(round) + 100);
        const int size = random.one_in(20) ? 2000 + random.below(4000) : random.below(300);
        hostile_text(random, size, text);
        runs.clear();
        const int run_count = random.one_in(3) ? random.below(12) : 0;
        for (int i = 0; i < run_count; ++i)
            runs.push_back(hostile_run(random, size));
        const int flags = random.one_in(8) ? static_cast<int>(random.next()) : random.below(8);

        const double start = seconds_now();
        Frame frame;
        begin_window();
        ImTypesetParagraph* const p = pool[round % 4];
        // An exact-size heap copy; a zero-size one has no bytes to point at.
        std::vector<char> exact(text);
        const char* const begin = exact.empty() ? "" : exact.data();
        const bool set = ImTypeset::SetParagraphText(
            p, begin, begin + exact.size(), runs.empty() ? nullptr : runs.data(),
            random.one_in(30) ? -5 : static_cast<int>(runs.size()), flags);
        if (!set) {
            CHECK(ImTypeset::GetParagraphError(p)->Code != ImTypesetError_None);
            ++refused;
        }
        for (int attempt = 0; attempt < 3; ++attempt) {
            const ImTypesetStyle style = hostile_style(random);
            const bool defaulted = random.one_in(4);
            // The status is the paragraph's error, no more and no less.
            const bool ok =
                ImTypeset::LayoutParagraph(p, hostile_width(random), defaulted ? nullptr : &style);
            CHECK_EQ(ok, ImTypeset::GetParagraphError(p)->Code == ImTypesetError_None);
            CHECK_EQ(ImTypeset::GetLastError()->Code, ImTypeset::GetParagraphError(p)->Code);
            const ImVec2 laid = ImTypeset::GetParagraphSize(p);
            CHECK(finite(laid.x) && finite(laid.y));
            CHECK(laid.x >= 0.0f && laid.y >= 0.0f);
            const int formulas = ImTypeset::GetParagraphFormulaCount(p);
            CHECK(formulas >= 0);
            for (int f = -1; f <= formulas && f < 4; ++f) {
                const ImTypesetFormula formula = ImTypeset::GetParagraphFormula(p, f);
                REQUIRE(formula.Reason != nullptr);
                if (f < 0 || f >= formulas) {
                    CHECK(formula.TextStart == 0 && formula.TextEnd == 0);
                    continue;
                }
                CHECK(formula.TextStart >= 0 && formula.TextStart < formula.TextEnd);
                CHECK(formula.TextEnd <= size);
            }
            interrogate(p, size, random, vertices, indices, defaulted || !large_type(style));
        }
        // The widget entries on the same bytes.
        // Every flag bit there is and is not: the entry keeps the ones it knows.
        ImTypeset::TextUnformatted(begin, begin + exact.size(), flags);
        const ImTypesetStyle widget_style = hostile_style(random);
        if (!large_type(widget_style))
            ImTypeset::TextUnformatted(begin, begin + exact.size(), flags, hostile_width(random),
                                       &widget_style);
        ImTypeset::TextWithID("##fuzz", begin, begin + exact.size(), flags);
        ImTypesetTextRange selected = {random.below(64) - 8, random.below(64) - 8};
        ImTypeset::SelectableText("##selectable", begin, begin + exact.size(), flags, 0.0f, nullptr,
                                  &selected);
        CHECK(selected.Start >= -8 && selected.End <= 64);
        ImGui::End();
        const double took = seconds_now() - start;
        slowest = took > slowest ? took : slowest;
        CHECK(took < 20.0);
    }
    for (ImTypesetParagraph* p : pool)
        ImTypeset::DestroyParagraph(p);
    std::printf("    %d hostile paragraphs, %d reported by SetParagraphText, slowest %.3f s\n",
                rounds, refused, slowest);
}

// --- Size ----------------------------------------------------------------------------

// One megabyte with no space in it: one word, no legal breakpoint, one line.
// It must come back, and promptly: nothing is quadratic in the length of a
// line. Drawing it to buffers either fits ImDrawIdx or is refused whole.
TEST(a_megabyte_with_no_space_in_it_is_one_overfull_line)
{
    Stage stage;
    std::string word(1 << 20, 'm');
    for (size_t i = 0; i < word.size(); i += 7)
        word[i] = static_cast<char>('a' + i % 26);
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    const double start = seconds_now();
    CHECK(ImTypeset::SetParagraphText(p, word.data(), word.data() + word.size()));
    const int ways = scale() > 1 ? 4 : 1;
    for (int align = 0; align < ways; ++align) {
        ImTypesetStyle style;
        style.EmSize = 16.0f;
        style.Align = align;
        style.LineBreak = align == 1 ? ImTypesetLineBreak_TotalFit : ImTypesetLineBreak_Auto;
        ImTypeset::LayoutParagraph(p, 300.0f, &style);
        const ImVec2 size = ImTypeset::GetParagraphSize(p);
        CHECK(finite(size.x) && finite(size.y));
        CHECK_EQ(ImTypeset::GetParagraphLineCount(p), 1);
        CHECK_EQ(ImTypeset::GetParagraphLine(p, 0).TextEnd, 1 << 20);
    }
    const double laid = seconds_now() - start;
    CHECK(laid < 60.0);

    Frame frame;
    begin_window();
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    Random random(3);
    interrogate(p, static_cast<int>(word.size()), random, vertices, indices);
    // Clipped to a window, the megabyte costs the window.
    vertices.resize(0);
    indices.resize(0);
    CHECK(ImTypeset::DrawParagraphToBuffers(
        p, ImVec2(0.0f, 0.0f), ImVec4(0.0f, 0.0f, 400.0f, 300.0f), &vertices, &indices));
    CHECK(vertices.Size > 0 && vertices.Size < 4 * 400);
    ImGui::End();
    std::printf("    1 MiB word: set and laid out %d ways in %.2f s, all of it in %.2f s\n", ways,
                laid, seconds_now() - start);
    CHECK(seconds_now() - start < 90.0);
    ImTypeset::DestroyParagraph(p);
}

// A hundred thousand words in one paragraph. Justified text is broken by total
// fit, whose work grows faster than the paragraph (docs/performance.md): this
// is where its work budget has to hold. The paragraph is whole, first fit's,
// and says so in its status: ImTypesetStatusFlags_FirstFitFallback, which is
// not an error.
TEST(a_hundred_thousand_words_are_broken_within_the_work_budget)
{
    Stage stage;
    Random random(11);
    std::string text;
    for (int w = 0; w < 100000; ++w) {
        if (w != 0)
            text += ' ';
        const int letters = 1 + random.below(9);
        for (int c = 0; c < letters; ++c)
            text += static_cast<char>('a' + random.below(26));
        if (random.one_in(12))
            text += '.';
    }
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    CHECK(ImTypeset::SetParagraphText(p, text.data(), text.data() + text.size()));

    struct {
        const char* what;
        ImTypesetAlign align;
        ImTypesetLineBreak line_break;
        bool falls_back;
    } const MODES[] = {
        {"justified, total fit", ImTypesetAlign_Justify, ImTypesetLineBreak_Auto, true},
        {"ragged right, total fit", ImTypesetAlign_Left, ImTypesetLineBreak_TotalFit, true},
        {"centred, total fit", ImTypesetAlign_Center, ImTypesetLineBreak_TotalFit, true},
        {"justified, first fit", ImTypesetAlign_Justify, ImTypesetLineBreak_FirstFit, false},
        {"ragged right, first fit", ImTypesetAlign_Left, ImTypesetLineBreak_Auto, false},
    };
    // Scaled up, every mode; by default justified text both ways: the case a
    // host meets without asking for anything, and the one that never meets
    // the optimiser. Ragged total fit, the optimiser's worst case, is held at
    // 20,000 words by tests/test_stress_layout.cpp.
    const bool every = scale() > 1;
    for (const auto& mode : MODES) {
        CHECK_CONTEXT("%s", mode.what);
        if (!every && mode.align != ImTypesetAlign_Justify)
            continue;
        ImTypesetStyle style;
        style.EmSize = 16.0f;
        style.Align = mode.align;
        style.LineBreak = mode.line_break;
        const double start = seconds_now();
        // Falling back is not failing: the call succeeds.
        CHECK(ImTypeset::LayoutParagraph(p, 420.0f, &style));
        const double first = seconds_now() - start;
        const ImVec2 size = ImTypeset::GetParagraphSize(p);
        const int lines = ImTypeset::GetParagraphLineCount(p);
        CHECK(finite(size.x) && finite(size.y) && size.y > 0.0f);
        CHECK(lines > 5000);
        CHECK_EQ(ImTypeset::GetParagraphLine(p, lines - 1).TextEnd, static_cast<int>(text.size()));
        const ImTypesetErrorInfo* const error = ImTypeset::GetParagraphError(p);
        CHECK_EQ(error->Code, static_cast<ImTypesetError>(ImTypesetError_None));
        CHECK_EQ(error->Message[0], '\0');
        const auto fell_back = [&] {
            return (ImTypeset::GetParagraphStatus(p) & ImTypesetStatusFlags_FirstFitFallback) != 0;
        };
        CHECK((ImTypeset::GetParagraphStatus(p) & ImTypesetStatusFlags_LaidOut) != 0);
        CHECK_EQ(fell_back(), mode.falls_back);
        // The budget is about a tenth of a second of the optimiser in an
        // optimised build; the rest is building and shipping 600,000 items.
        // Without the budget the justified case alone is minutes and the
        // ragged one hours. The bound is generous for a loaded machine under
        // the sanitisers.
        CHECK(first < 120.0);

        // A resize: the paragraph that ran out of budget does not pay for
        // the optimiser again at each new width, and still says what it is.
        const double again = seconds_now();
        CHECK(ImTypeset::LayoutParagraph(p, 419.0f, &style));
        const double second = seconds_now() - again;
        CHECK_EQ(fell_back(), mode.falls_back);
        CHECK(second < 120.0);
        std::printf("    100,000 words, %s: %d lines, %.2f s, then %.2f s at another width\n",
                    mode.what, lines, first, second);
    }

    // A short paragraph in the same object afterwards is the optimiser's
    // again, and the flag is gone.
    ImTypeset::SetParagraphText(p, "A short paragraph, set after the long one, breaks optimally.");
    CHECK(ImTypeset::LayoutParagraph(p, 200.0f));
    CHECK_EQ(ImTypeset::GetParagraphStatus(p), static_cast<int>(ImTypesetStatusFlags_LaidOut));

    if (every) {
        Frame frame;
        begin_window();
        ImTypeset::SetParagraphText(p, text.data(), text.data() + text.size());
        ImTypeset::LayoutParagraph(p, 420.0f);
        ImVector<ImDrawVert> vertices;
        ImVector<ImDrawIdx> indices;
        Random probe(5);
        interrogate(p, static_cast<int>(text.size()), probe, vertices, indices);
        ImTypeset::TextUnformatted(text.data(), text.data() + text.size());
        ImGui::End();
    }
    ImTypeset::DestroyParagraph(p);
}

// --- Lengths that are not lengths ------------------------------------------------------

// Every float the API takes, as zero, negative, NaN, infinite and enormous, in
// every field at once and one at a time, on text with every kind of item in
// it. The answers are finite sizes; the rest is UBSan's (a float that does not
// fit the integer it is converted to).
TEST(widths_and_sizes_that_are_not_numbers_give_finite_answers)
{
    Stage stage(true);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const float big = std::numeric_limits<float>::max();
    const float values[] = {nan,      inf,  -inf, -big,  -1.0e9f, -1.0f,  -0.0f,   0.0f,
                            1.0e-38f, 0.5f, 1.0f, 96.0f, 1.0e4f,  1.0e9f, 1.0e30f, big};
    const char* const text =
        "Some words to set, with a formula $\\frac{a}{b}+\\sqrt{x^2}$ and a display of "
        "nothing. A second sentence follows! Tabs\tand\nnewlines \xE2\x80\x94 too.";
    const int text_size = static_cast<int>(std::strlen(text));
    ImTypesetRun runs[2];
    runs[0].Start = 5;
    runs[0].End = 20;
    runs[0].Face = ImTypesetFace_Bold;
    runs[1].Start = 40;
    runs[1].End = 60;
    runs[1].Face = ImTypesetFace_Code;
    runs[1].Flags = ImTypesetRunFlags_NoBreak;

    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    Random random(9);
    Frame frame;
    begin_window();
    float ImTypesetStyle::*const fields[] = {
        &ImTypesetStyle::FontSize,        &ImTypesetStyle::EmSize,
        &ImTypesetStyle::LineHeight,      &ImTypesetStyle::LeftIndent,
        &ImTypesetStyle::FirstLineIndent, &ImTypesetStyle::PixelsPerInch};
    for (const float value : values) {
        CHECK_CONTEXT("value %g", static_cast<double>(value));
        runs[0].Scale = value;
        ImTypeset::SetParagraphText(p, text, nullptr, runs, 2,
                                    ImTypesetTextFlags_Math | ImTypesetTextFlags_HardBreaks);
        ImVec2 size = laid_out(p, value);
        CHECK(finite(size.x) && finite(size.y) && size.x >= 0.0f && size.y >= 0.0f);
        interrogate(p, text_size, random, vertices, indices);
        for (float ImTypesetStyle::*const field : fields) {
            ImTypesetStyle style;
            style.EmSize = 16.0f;
            style.*field = value;
            for (int align = 0; align < 4; ++align) {
                style.Align = align;
                size = laid_out(p, 300.0f, &style);
                CHECK(finite(size.x) && finite(size.y) && size.x >= 0.0f && size.y >= 0.0f);
            }
            interrogate(p, text_size, random, vertices, indices, !large_type(style));
        }
        ImTypesetStyle all;
        for (float ImTypesetStyle::*const field : fields)
            all.*field = value;
        size = laid_out(p, value, &all);
        CHECK(finite(size.x) && finite(size.y) && size.x >= 0.0f && size.y >= 0.0f);
        interrogate(p, text_size, random, vertices, indices, !large_type(all));

        // The context's own style, which the widget reads, and type as large
        // as the library sets it, drawn: two letters of it.
        const ImTypesetStyle saved = ImTypeset::GetStyle();
        ImTypeset::GetStyle() = all;
        ImTypeset::TextUnformatted(large_type(all) ? "Ag" : text, nullptr, ImTypesetTextFlags_Math);
        ImTypeset::GetStyle() = saved;
        // The same style handed to the call, with a width that is not one,
        // and to the measuring entry.
        ImTypeset::TextUnformatted(large_type(all) ? "Ag" : text, nullptr, ImTypesetTextFlags_Math,
                                   value, &all);
        const ImVec2 measured =
            ImTypeset::CalcTextSize(text, nullptr, value, ImTypesetTextFlags_Math, &all);
        CHECK(finite(measured.x) && finite(measured.y) && measured.x >= 0.0f && measured.y >= 0.0f);

        // Drawn at a position that is not one.
        ImTypeset::LayoutParagraph(p, 300.0f);
        vertices.resize(0);
        indices.resize(0);
        ImTypeset::DrawParagraphToBuffers(p, ImVec2(value, value),
                                          ImVec4(value, -value, value, value), &vertices, &indices);
        ImTypeset::DrawParagraph(p, ImGui::GetWindowDrawList(), ImVec2(value, -value));
        (void)ImTypeset::HitTestParagraph(p, ImVec2(value, value));
    }
    ImGui::End();
    ImTypeset::DestroyParagraph(p);
}

// --- Formulas --------------------------------------------------------------------------

// Formula sources built to be as deep, as long and as wrong as a string can
// be. Each is set or refused with a reason; none may recurse to the end of the
// stack (ASan: stack-overflow) or take time that grows faster than its length.
TEST(pathological_formulas_are_set_or_refused_and_come_back)
{
    Stage stage(true);
    const auto repeat = [](const char* piece, int count) {
        std::string out;
        for (int i = 0; i < count; ++i)
            out += piece;
        return out;
    };
    std::vector<std::string> sources;
    // Past any bound the parser has (MAX_MATH_NESTING is 64), by a wide margin.
    const int deep = 2000 * (scale() > 1 ? 10 : 1);
    const int many = 2500 * (scale() > 1 ? 8 : 1);
    // Thousands of very short words are a paragraph the line breaker's work
    // budget ends (about 1.7 s each under the sanitisers), so by default
    // these stay below it; scaled up they go through it.
    const int words = scale() > 1 ? many : 400;
    sources.push_back("$" + repeat("{", deep) + "x" + repeat("}", deep) + "$");
    sources.push_back("$" + repeat("{", deep) + "$");
    sources.push_back("$" + repeat("}", deep) + "$");
    sources.push_back("$" + repeat("\\frac{", deep) + "x" + repeat("}{y}", deep) + "$");
    sources.push_back("$" + repeat("\\sqrt{", deep) + "x" + repeat("}", deep) + "$");
    sources.push_back("$" + repeat("\\left(", deep) + "x" + repeat("\\right)", deep) + "$");
    sources.push_back("$" + repeat("\\left(", deep) + "$");
    sources.push_back("$x" + repeat("^{x", deep) + repeat("}", deep) + "$");
    sources.push_back("$x" + repeat("_x^x", deep) + "$");
    sources.push_back("$x" + repeat("^x", deep) + "$");
    sources.push_back("$x" + repeat("'", deep) + "$");
    sources.push_back("$" + repeat("\\text{", deep) + repeat("}", deep) + "$");
    sources.push_back("$" + repeat("\\mathbf{\\mathrm{", deep / 2) + "x" + repeat("}}", deep / 2) +
                      "$");
    sources.push_back("$" + repeat("\\sum_{i=0}^{n}", deep / 4) + "$");
    sources.push_back("$" + repeat("\\binom{a}{", deep) + "b" + repeat("}", deep) + "$");
    sources.push_back("$" + repeat("\\overline{\\underline{", deep / 2) + "x" +
                      repeat("}}", deep / 2) + "$");
    sources.push_back("$" + repeat("a+b=c ", 2 * many) + "$");
    sources.push_back("$" + repeat("\\", deep) + "$");
    sources.push_back("$" + repeat("\\\\", deep) + "$");
    sources.push_back("$" + repeat("\\undefinedcommand", 5000) + "$");
    sources.push_back("$" + repeat("\\alpha", many) + "$");
    sources.push_back(repeat("$", 8 * many + 1));
    sources.push_back(repeat("$$", 4 * many + 1));
    sources.push_back(repeat("$x$ ", words));
    sources.push_back(repeat("$x$", many));
    sources.push_back(repeat("\\$", 4 * many));
    sources.push_back(repeat("$ x $ ", words));
    sources.push_back(repeat("$\\frac{a}{b}$ and ", words));
    sources.push_back("$$" + repeat("{", deep) + "$$");
    sources.push_back("$$\n" + repeat("\\frac{", 3000) + "x" + repeat("}{y}", 3000) + "\n$$");
    sources.push_back("$$\n" + repeat("x ", 4 * many));
    sources.push_back("$\\frac{" + repeat("x", 8 * many) + "}{y}$");
    sources.push_back(std::string("$a\0b$", 5));
    sources.push_back("$\xC3$");
    sources.push_back("$\\text{\xF0\x9F\x98}$");
    sources.push_back("$\\" + std::string(1, '\0') + "$");
    sources.push_back("$\\left$");
    sources.push_back("$\\right)$");
    sources.push_back("$\\frac$");
    sources.push_back("$^$");
    sources.push_back("$_$");
    sources.push_back("$x^^x$");
    sources.push_back("$x__x$");
    sources.push_back("$\\sqrt[$");
    sources.push_back("$\\sqrt[" + repeat("[", 5000) + "]{x}$");

    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    Random random(21);
    Frame frame;
    begin_window();
    double slowest = 0.0;
    int set = 0;
    int refused = 0;
    for (size_t i = 0; i < sources.size(); ++i) {
        const std::string& source = sources[i];
        CHECK_CONTEXT("source %zu, %zu bytes, starting %.24s", i, source.size(), source.c_str());
        const double start = seconds_now();
        ImTypeset::SetParagraphText(p, source.data(), source.data() + source.size(), nullptr, 0,
                                    ImTypesetTextFlags_Math);
        const ImVec2 size = laid_out(p, 400.0f);
        CHECK(finite(size.x) && finite(size.y));
        const ImTypesetErrorInfo* const error = ImTypeset::GetParagraphError(p);
        if (error->Code == ImTypesetError_MathRefused) {
            ++refused;
            CHECK(std::strlen(error->Message) > 0);
            CHECK(error->BytePos >= 0 && error->BytePos <= static_cast<int>(source.size()));
        } else {
            ++set;
        }
        const int formulas = ImTypeset::GetParagraphFormulaCount(p);
        for (int f = 0; f < formulas && f < 3; ++f) {
            const ImTypesetFormula formula = ImTypeset::GetParagraphFormula(p, f);
            CHECK(formula.TextEnd > formula.TextStart);
            CHECK(formula.TextEnd <= static_cast<int>(source.size()));
        }
        interrogate(p, static_cast<int>(source.size()), random, vertices, indices);
        const double took = seconds_now() - start;
        slowest = took > slowest ? took : slowest;
        if (std::getenv("IMTYPESET_STRESS_VERBOSE") != nullptr)
            std::printf("    source %zu, %zu bytes, %.24s: %.3f s\n", i, source.size(),
                        source.c_str(), took);
        // Each source is at most a few hundred kilobytes.
        CHECK(took < 30.0);
    }
    ImGui::End();
    ImTypeset::DestroyParagraph(p);
    std::printf("    %zu pathological sources: %d set, %d refused, slowest %.2f s\n",
                sources.size(), set, refused, slowest);
    CHECK(refused > 10);
}

// --- Long runs -------------------------------------------------------------------------

namespace
{

const char* const SAMPLE =
    "Typesetting is the composition of text by arranging physical type or its digital "
    "equivalents. Stored letters and other symbols are retrieved and ordered according to a "
    "language's orthography for visual display, as in $a^2+b^2=c^2$ and "
    "\xE2\x80\x9Cquotes\xE2\x80\x9D.";

// One paragraph's whole life.
void one_life(int cycle, ImVector<ImDrawVert>& vertices, ImVector<ImDrawIdx>& indices)
{
    ImTypesetParagraph* const p = ImTypeset::CreateParagraph();
    const int size = static_cast<int>(std::strlen(SAMPLE)) - cycle % 40;
    ImTypesetRun run;
    run.Start = cycle % 30;
    run.End = run.Start + 25;
    run.Face = ImTypesetFace_Italic;
    ImTypeset::SetParagraphText(p, SAMPLE, SAMPLE + size, &run, 1, ImTypesetTextFlags_Math);
    ImTypesetStyle style;
    style.EmSize = 16.0f;
    style.Align = cycle % 4;
    ImTypeset::LayoutParagraph(p, 200.0f + static_cast<float>(cycle % 7) * 30.0f, &style);
    vertices.resize(0);
    indices.resize(0);
    const ImTypesetTextRange selection = {2, 30};
    ImTypeset::DrawParagraphToBuffers(p, ImVec2(0.0f, 0.0f), ImVec4(0.0f, 0.0f, 800.0f, 600.0f),
                                      &vertices, &indices, &selection);
    ImTypeset::DrawParagraph(p, ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(),
                             &selection);
    ImGuiTextBuffer copy;
    ImTypeset::AppendParagraphRangeText(p, selection, &copy);
    ImTypeset::DestroyParagraph(p);
}

} // namespace

// Thousands of paragraphs made, set, laid out, drawn, copied from and
// destroyed. The number of live blocks after each batch is the number after
// the first: nothing is kept per paragraph that has gone.
TEST(create_layout_draw_destroy_cycles_leave_nothing_behind)
{
    Stage stage(true);
    const int batches = 6;
    const int per_batch = 200 * scale();
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    long long live_after[batches];
    for (int batch = 0; batch < batches; ++batch) {
        // A frame for every few lives: a window's draw list holds 65,535
        // vertices when ImDrawIdx is 16 bits.
        for (int i = 0; i < per_batch; i += 10) {
            Frame frame;
            begin_window();
            for (int k = i; k < i + 10 && k < per_batch; ++k)
                one_life(batch * per_batch + k, vertices, indices);
            ImGui::End();
        }
        live_after[batch] = g_live;
    }
    // The first two batches warm the buffers (ImGui's draw lists, the glyph
    // atlas, the context's scratch space). After that the count is level.
    for (int batch = 3; batch < batches; ++batch) {
        CHECK_CONTEXT("batch %d of %d paragraphs", batch, per_batch);
        CHECK_EQ(live_after[batch], live_after[2]);
    }
    std::printf(
        "    %d paragraph lives; live blocks after each batch: %lld %lld %lld %lld %lld %lld\n",
        batches * per_batch, live_after[0], live_after[1], live_after[2], live_after[3],
        live_after[4], live_after[5]);
}

// One paragraph laid out at a thousand widths and styles, and its text set
// again and again: its buffers reach their size and stay there.
TEST(relayout_and_retext_cycles_reach_a_steady_allocation_count)
{
    Stage stage(true);
    const int cycles = 1000 * scale();
    ImTypesetParagraph* const p = ImTypeset::CreateParagraph();
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    long long live_at_third = 0;
    long long live_at_two_thirds = 0;
    for (int cycle = 0; cycle < cycles; ++cycle) {
        Frame frame;
        begin_window();
        if (cycle % 5 == 0) {
            const int size = static_cast<int>(std::strlen(SAMPLE)) - cycle % 60;
            ImTypeset::SetParagraphText(p, SAMPLE, SAMPLE + size, nullptr, 0,
                                        cycle % 2 == 0 ? ImTypesetTextFlags_Math : 0);
        }
        ImTypesetStyle style;
        style.EmSize = 12.0f + static_cast<float>(cycle % 3) * 2.0f;
        style.Align = cycle % 4;
        ImTypeset::LayoutParagraph(p, 150.0f + static_cast<float>(cycle % 97) * 4.0f, &style);
        vertices.resize(0);
        indices.resize(0);
        ImTypeset::DrawParagraphToBuffers(p, ImVec2(0.0f, 0.0f), ImVec4(0.0f, 0.0f, 800.0f, 600.0f),
                                          &vertices, &indices);
        ImTypeset::DrawParagraph(p, ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos());
        ImGui::End();
        if (cycle == cycles / 3)
            live_at_third = g_live;
        if (cycle == 2 * cycles / 3)
            live_at_two_thirds = g_live;
    }
    // Three sizes of type are three sets of metrics and of baked glyphs, all
    // met in the first few cycles. From a third of the way in, nothing new.
    CHECK_EQ(live_at_two_thirds, live_at_third);
    CHECK_EQ(g_live, live_at_third);
    std::printf("    %d relayouts: live blocks %lld, %lld, %lld\n", cycles, live_at_third,
                live_at_two_thirds, g_live);
    ImTypeset::DestroyParagraph(p);
}

// A zoom animated through thousands of sizes that never repeat: every size is
// a set of font metrics, and for a formula three more of the math face. The
// registry keeps a bounded number of them (review item S9) and the context
// lets go of the rest between texts, so the count of live blocks stops
// growing; and nothing that was let go is read again, which is ASan's to see.
TEST(an_animated_zoom_keeps_a_bounded_number_of_metrics)
{
    Stage stage(true);
    const int steps = 900 * scale();
    ImTypesetParagraph* const p = ImTypeset::CreateParagraph();
    ImTypesetRun run;
    run.Start = 0;
    run.End = 11;
    run.Face = ImTypesetFace_Bold;
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    long long live_at_half = 0;
    long long live_at_three_quarters = 0;
    for (int step = 0; step < steps; ++step) {
        Frame frame;
        begin_window();
        ImTypesetStyle style;
        style.EmSize = 9.0f + static_cast<float>(step) * 0.004f;
        // The text is set again as a host's would be when its content is
        // rebuilt; every fourth step only the style moves.
        if (step % 4 != 0)
            ImTypeset::SetParagraphText(p, SAMPLE, nullptr, &run, 1, ImTypesetTextFlags_Math);
        const ImVec2 size = laid_out(p, 300.0f, &style);
        CHECK(finite(size.x) && size.y > 0.0f);
        // Drawn now and then: each size drawn is also a set of baked glyphs
        // in ImGui's atlas, which is not what this measures.
        if (step % 200 == 0) {
            vertices.resize(0);
            indices.resize(0);
            ImTypeset::DrawParagraphToBuffers(
                p, ImVec2(0.0f, 0.0f), ImVec4(0.0f, 0.0f, 800.0f, 600.0f), &vertices, &indices);
        }
        ImGui::End();
        if (step == steps / 2)
            live_at_half = g_live;
        if (step == 3 * steps / 4)
            live_at_three_quarters = g_live;
    }
    // Level, give or take what the sizes kept at one moment hold. Kept without
    // a bound, the second half of the run would hold as much again as the
    // first.
    const long long slack = live_at_half / 4 + 500;
    CHECK(live_at_three_quarters < live_at_half + slack);
    CHECK(g_live < live_at_half + slack);
    std::printf("    %d sizes: live blocks %lld, %lld, %lld\n", steps, live_at_half,
                live_at_three_quarters, g_live);
    ImTypeset::DestroyParagraph(p);
}

// The widget, given a different text every frame for thousands of frames: what
// it keeps is bounded by the cache's limits, which is what leaves when a text
// is found by its content and the content never comes back.
TEST(the_widget_cache_is_bounded_under_text_that_changes_every_frame)
{
    Stage stage;
    const int frames = 1500 * scale();
    const int limit = 64;
    ImTypeset::SetTextCacheLimits(limit, 0);
    ImU64 most_cached = 0;
    long long live_at_half = 0;
    std::string text;
    for (int frame_number = 0; frame_number < frames; ++frame_number) {
        Frame frame;
        begin_window();
        // A fresh buffer each frame, as a host formatting into a temporary.
        text = "Frame " + std::to_string(frame_number) +
               " of a counter that never repeats, set as text.";
        ImTypeset::TextUnformatted(text.data(), text.data() + text.size());
        ImGui::PushID(frame_number);
        ImTypeset::TextUnformatted(text.data(), text.data() + text.size());
        ImGui::PopID();
        // And as a host that knows it changes says it: one entry, for ever.
        ImTypeset::TextWithID("##counter", text.data(), text.data() + text.size());
        ImTypeset::Text("Frame %d, formatted.", frame_number);
        ImGui::End();
        const ImU64 cached = ImTypeset::GetStats().CachedParagraphs;
        most_cached = cached > most_cached ? cached : most_cached;
        if (frame_number == frames / 2)
            live_at_half = g_live;
    }
    // The limit, and what two frames submit: three new entries a frame.
    CHECK(most_cached <= static_cast<ImU64>(limit) + 2 * 4);
    CHECK(ImTypeset::GetStats().CacheEvictions > static_cast<ImU64>(frames) * 2);
    // Not exactly level -- which paragraphs are held moves with the frame --
    // but not growing with the number of frames either.
    CHECK(g_live < live_at_half + live_at_half / 4 + 1000);
    std::printf("    %d frames of changing text: at most %llu cached, live blocks %lld then %lld\n",
                frames, static_cast<unsigned long long>(most_cached), live_at_half, g_live);
}
