// The public API, end to end, in headless ImGui frames: everything here goes
// in through include/imtypeset.h. src/context.h is included only to look at
// what came out (the shipped commands, the resolved lengths) and to install
// the stand-in formula layer the math seam is tested with.
//
// What "equal to the reference" means here. No golden records text ->
// vertices in one step, so the chain is checked link by link:
//   1. text -> shipped commands: every `para` golden the public API can
//      express is rebuilt through SetParagraphText/LayoutParagraph and its
//      commands compared, field for field, with the reference's. Cases it
//      cannot express (a list marker, the reference's doubled indent, white
//      space that the API collapses across runs on purpose, a length that is
//      not a float number of pixels) are counted and printed, not dropped.
//   2. commands -> vertices: the reference's commands, read from the same
//      golden, are drawn by src/draw (which tests/test_draw.cpp holds equal to
//      the reference's vertices) and compared, byte for byte, with what
//      DrawParagraphToBuffers produced for the same paragraph.

#include "core_golden.h"
#include "golden.h"

#include "imtypeset.h"

#include "context.h"
#include "support/soft_raster.h"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <new>
#include <string>
#include <vector>

using namespace ImTypeset::detail;

// --- Counting allocations ---------------------------------------------------------
// Every form of operator new, so that no block is allocated by one family and
// freed by another, and ImGui's own allocator. Not inlined: GCC, seeing
// through an inlined replacement to malloc and free, reports the pair as
// mismatched.
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

const char* const LOREM =
    "Typesetting is the composition of text by arranging physical type or its digital "
    "equivalents. Stored letters and other symbols are retrieved and ordered according to a "
    "language's orthography for visual display. Typesetting requires one or more fonts, which "
    "are widely but erroneously confused with and substituted for typefaces. One significant "
    "effect of typesetting was that authorship of works could be spotted more easily, making "
    "it difficult for copiers who have not gained permission.";

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

// A headless ImGui context with an ImTypeset context in it, and the
// reference's default fonts added through the public API.
struct Stage {
    explicit Stage(bool with_fonts = true)
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
        if (with_fonts) {
            for (const auto& entry : DEFAULT_FONTS) {
                if (ImTypeset::AddFontFromFileTTF(entry.face, font_path(entry.file).c_str()) ==
                    nullptr)
                    FAIL("%s: %s", entry.file, ImTypeset::GetLastError()->Message);
            }
        }
        // The reference's default theme: a 16 px em.
        ImTypeset::GetStyle().EmSize = 16.0f;
        ImTypeset::GetStyle().Color = IM_COL32(0x11, 0x22, 0x33, 0xFF);
    }
    ~Stage()
    {
        ImTypeset::DestroyContext();
        ImGui::DestroyContext();
    }
    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;

    bool freetype() const
    {
        const char* name = ImGui::GetIO().Fonts->FontLoaderName;
        return name != nullptr && std::strncmp(name, "FreeType", 8) == 0;
    }

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

// What one call cost, by the counters.
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

// How many items the paragraph shipped: an internal count, which the public
// API no longer speaks in.
int item_count(const ImTypesetParagraph* paragraph)
{
    return static_cast<int>(paragraph->layout.item_count);
}

// The size a layout gives, as the old return value was.
ImVec2 laid_out(ImTypesetParagraph* paragraph, float width, const ImTypesetStyle* style = nullptr)
{
    ImTypeset::LayoutParagraph(paragraph, width, style);
    return ImTypeset::GetParagraphSize(paragraph);
}

bool same_commands(const std::vector<DrawCmd>& a, const std::vector<DrawCmd>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const DrawCmd& x = a[i];
        const DrawCmd& y = b[i];
        if (x.type != y.type || x.codepoint != y.codepoint || x.x != y.x || x.y != y.y ||
            x.width != y.width || x.height != y.height || x.depth != y.depth || x.a != y.a ||
            x.index != y.index || x.end != y.end)
            return false;
    }
    return true;
}

} // namespace

// --- The header ---------------------------------------------------------------------

TEST(the_header_includes_imgui_and_names_no_standard_library_type)
{
    std::vector<uint8_t> bytes;
    REQUIRE(golden::read_file(IMTYPESET_SOURCE_DIR "/include/imtypeset.h", bytes));
    // The code, without its comments: a comment may say "no std:: type".
    std::string text;
    {
        const std::string raw(bytes.begin(), bytes.end());
        size_t from = 0;
        while (from < raw.size()) {
            size_t end = raw.find('\n', from);
            if (end == std::string::npos)
                end = raw.size();
            std::string line = raw.substr(from, end - from);
            const size_t comment = line.find("//");
            if (comment != std::string::npos)
                line.erase(comment);
            text += line + "\n";
            from = end + 1;
        }
    }
    REQUIRE(text.size() > 1000);
    int includes = 0;
    size_t at = 0;
    while ((at = text.find("#include", at)) != std::string::npos) {
        const size_t end = text.find('\n', at);
        std::string line = text.substr(at, end - at);
        while (!line.empty() && line.back() == ' ')
            line.pop_back();
        CHECK_EQ(line, std::string("#include \"imgui.h\""));
        ++includes;
        at = end;
    }
    CHECK_EQ(includes, 1);
    CHECK(text.find("std::") == std::string::npos);
    CHECK(text.find("imgui_internal") == std::string::npos);
    CHECK(text.find("template") == std::string::npos);
    CHECK(text.find("operator") == std::string::npos);
    // Nothing that publishes the address of a text as its identity.
    CHECK(text.find("NoTextCompare") == std::string::npos);
    CHECK(text.find("EnableMath") == std::string::npos);
    CHECK(text.find("namespace ImTypeset") != std::string::npos);
}

// --- Context ------------------------------------------------------------------------

// What IMTYPESET_CHECKVERSION() passes, with one size changed.
constexpr size_t CHECKED_SIZES = 11;
bool check_with(size_t which, size_t value)
{
    size_t sizes[CHECKED_SIZES] = {
        sizeof(ImWchar),        sizeof(ImDrawVert),        sizeof(ImDrawIdx),
        sizeof(ImTypesetStyle), sizeof(ImTypesetRun),      sizeof(ImTypesetTextRange),
        sizeof(ImTypesetLine),  sizeof(ImTypesetHit),      sizeof(ImTypesetFormula),
        sizeof(ImTypesetStats), sizeof(ImTypesetErrorInfo)};
    if (which < CHECKED_SIZES)
        sizes[which] = value;
    return ImTypeset::DebugCheckVersionAndDataLayout(
        IMTYPESET_VERSION, sizes[0], sizes[1], sizes[2], sizes[3], sizes[4], sizes[5], sizes[6],
        sizes[7], sizes[8], sizes[9], sizes[10]);
}

TEST(the_build_check_is_a_macro_and_a_failed_one_stops_create_context)
{
    ImGui::SetAllocatorFunctions(&counted_alloc, &counted_free, nullptr);
    CHECK(IMTYPESET_CHECKVERSION());
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_None));
    CHECK(ImTypeset::CreateContext() == nullptr);
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_NoContext));

    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;

    // Another version of the header.
    CHECK(!ImTypeset::DebugCheckVersionAndDataLayout(
        "0.0.0", sizeof(ImWchar), sizeof(ImDrawVert), sizeof(ImDrawIdx), sizeof(ImTypesetStyle),
        sizeof(ImTypesetRun), sizeof(ImTypesetTextRange), sizeof(ImTypesetLine),
        sizeof(ImTypesetHit), sizeof(ImTypesetFormula), sizeof(ImTypesetStats),
        sizeof(ImTypesetErrorInfo)));
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_VersionMismatch));
    // And the host that did not look at the result gets no context.
    CHECK(ImTypeset::CreateContext() == nullptr);
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_VersionMismatch));
    CHECK(std::strstr(ImTypeset::GetLastError()->Message, "0.0.0") != nullptr);

    // The other IMGUI_USE_WCHAR32 setting.
    CHECK(!check_with(0, sizeof(ImWchar) == 2 ? 4 : 2));
    CHECK(std::strstr(ImTypeset::GetLastError()->Message, "IMGUI_USE_WCHAR32") != nullptr);
    CHECK(ImTypeset::CreateContext() == nullptr);
    // Every struct that crosses the boundary is checked, the one a host
    // passes as an array among them.
    static const char* const NAMES[CHECKED_SIZES] = {"ImWchar",
                                                     "ImDraw",
                                                     "ImDraw",
                                                     "ImTypesetStyle",
                                                     "ImTypesetRun",
                                                     "ImTypesetTextRange",
                                                     "ImTypesetLine",
                                                     "ImTypesetHit",
                                                     "ImTypesetFormula",
                                                     "ImTypesetStats",
                                                     "ImTypesetErrorInfo"};
    for (size_t which = 0; which < CHECKED_SIZES; ++which) {
        CHECK_CONTEXT("size %zu (%s)", which, NAMES[which]);
        CHECK(!check_with(which, 1));
        CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_VersionMismatch));
        CHECK(std::strstr(ImTypeset::GetLastError()->Message, NAMES[which]) != nullptr);
        CHECK(ImTypeset::CreateContext() == nullptr);
    }
    CHECK(ImTypeset::GetCurrentContext() == nullptr);

    // A check that passes clears the refusal.
    CHECK(IMTYPESET_CHECKVERSION());
    ImTypesetContext* first = ImTypeset::CreateContext();
    REQUIRE(first != nullptr);
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_None));
    ImTypeset::DestroyContext(first);
    ImGui::DestroyContext();
}

TEST(create_context_makes_the_new_context_current_only_if_none_is)
{
    ImGui::SetAllocatorFunctions(&counted_alloc, &counted_free, nullptr);
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    REQUIRE(IMTYPESET_CHECKVERSION());
    ImTypesetContext* first = ImTypeset::CreateContext();
    REQUIRE(first != nullptr);
    CHECK(ImTypeset::GetCurrentContext() == first);
    // As ImGui::CreateContext(): a second one does not take the first's place.
    ImTypesetContext* second = ImTypeset::CreateContext();
    REQUIRE(second != nullptr);
    CHECK(ImTypeset::GetCurrentContext() == first);

    // A failure is reported whichever context is current.
    CHECK(!check_with(3, 1));
    CHECK(ImTypeset::CreateContext() == nullptr);
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_VersionMismatch));
    CHECK(IMTYPESET_CHECKVERSION());

    ImTypeset::SetCurrentContext(second);
    // A paragraph left alive dies with its context.
    CHECK(ImTypeset::CreateParagraph() != nullptr);
    ImTypeset::SetCurrentContext(first);
    ImTypeset::GetStyle().LeftIndent = 7.0f;
    ImTypeset::SetCurrentContext(second);
    CHECK_EQ(ImTypeset::GetStyle().LeftIndent, 0.0f);

    // A paragraph is its own context's, whichever is current: destroying one
    // of the first while the second is current frees it there.
    ImTypeset::SetCurrentContext(first);
    ImTypesetParagraph* of_first = ImTypeset::CreateParagraph();
    ImTypesetParagraph* other = ImTypeset::CreateParagraph();
    ImTypeset::SetCurrentContext(second);
    CHECK_EQ(first->paragraphs.size(), size_t{2});
    ImTypeset::DestroyParagraph(of_first);
    CHECK_EQ(first->paragraphs.size(), size_t{1});
    CHECK_EQ(second->paragraphs.size(), size_t{1});
    CHECK(first->paragraphs[0] == other);

    ImTypeset::DestroyContext(first);
    CHECK(ImTypeset::GetCurrentContext() == second);
    ImTypeset::DestroyContext();
    CHECK(ImTypeset::GetCurrentContext() == nullptr);
    ImGui::DestroyContext();
}

// A context's serial is what tells it from one made later at its address,
// which a pointer cannot: it is never zero, never another context's, and
// never used again by this process.
TEST(a_contexts_serial_is_its_own_and_is_never_used_again)
{
    CHECK_EQ(ImTypeset::GetContextSerial(), ImU64{0});
    ImGui::SetAllocatorFunctions(&counted_alloc, &counted_free, nullptr);
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    REQUIRE(IMTYPESET_CHECKVERSION());
    ImTypesetContext* first = ImTypeset::CreateContext();
    REQUIRE(first != nullptr);
    const ImU64 first_serial = ImTypeset::GetContextSerial();
    CHECK_NE(first_serial, ImU64{0});
    // It is the CURRENT context's: a second one, not current, changes nothing.
    ImTypesetContext* second = ImTypeset::CreateContext();
    REQUIRE(second != nullptr);
    CHECK_EQ(ImTypeset::GetContextSerial(), first_serial);
    ImTypeset::SetCurrentContext(second);
    const ImU64 second_serial = ImTypeset::GetContextSerial();
    CHECK_NE(second_serial, ImU64{0});
    CHECK_NE(second_serial, first_serial);
    ImTypeset::SetCurrentContext(nullptr);
    CHECK_EQ(ImTypeset::GetContextSerial(), ImU64{0});
    ImTypeset::SetCurrentContext(first);
    CHECK_EQ(ImTypeset::GetContextSerial(), first_serial);

    // Destroyed and made again, a context can come back at the address of one
    // that is gone. Its serial does not.
    ImTypeset::DestroyContext(second);
    std::map<ImU64, int> seen;
    seen[first_serial] = 1;
    seen[second_serial] = 1;
    int at_an_old_address = 0;
    std::vector<const void*> addresses = {second};
    for (int i = 0; i < 64; ++i) {
        ImTypesetContext* again = ImTypeset::CreateContext();
        REQUIRE(again != nullptr);
        bool reused = false;
        for (const void* old : addresses)
            reused = reused || old == again;
        at_an_old_address += reused ? 1 : 0;
        addresses.push_back(again);
        ImTypeset::SetCurrentContext(again);
        const ImU64 serial = ImTypeset::GetContextSerial();
        CHECK_NE(serial, ImU64{0});
        CHECK_EQ(++seen[serial], 1);
        ImTypeset::DestroyContext(again);
        CHECK_EQ(ImTypeset::GetContextSerial(), ImU64{0});
    }
    std::printf("api: 64 contexts made and destroyed in turn: %d at an address one had before, "
                "none with a serial one had before\n",
                at_an_old_address);
    ImTypeset::SetCurrentContext(first);
    ImTypeset::DestroyContext(first);
    ImGui::DestroyContext();
}

TEST(line_heights_used_alternately_are_each_converted_once)
{
    Stage stage;
    ImTypesetParagraph* heading = ImTypeset::CreateParagraph();
    ImTypesetParagraph* body = ImTypeset::CreateParagraph();
    ImTypeset::SetParagraphText(heading, "A heading");
    ImTypeset::SetParagraphText(body, LOREM);
    ImTypesetStyle one = ImTypeset::GetStyle();
    one.LineHeight = 1.2f;
    ImTypesetStyle two = ImTypeset::GetStyle();
    two.LineHeight = 1.5f;
    for (int n = 0; n < 1000; ++n) {
        ImTypeset::LayoutParagraph(heading, 300.0f, &one);
        ImTypeset::LayoutParagraph(body, 300.0f, &two);
    }
    // A float's decimal is found by printing and parsing it: twice in all,
    // not twice a frame.
    CHECK_EQ(stage.ctx->line_height_next, 2u);
    CHECK_EQ(ImTypeset::GetStats().Layouts, 2);
    // The default needs none.
    ImTypeset::LayoutParagraph(body, 300.0f);
    CHECK_EQ(stage.ctx->line_height_next, 2u);
}

TEST(no_counter_overflows_however_long_the_context_lives)
{
    Stage stage;
    // Where a 32-bit counter of vertices or bytes ends after some minutes at
    // sixty frames a second. UBSan is what would say if one still did.
    const ImU64 almost = 0x7FFFFFF0ull;
    stage.ctx->stats.VerticesEmitted = almost;
    stage.ctx->stats.BytesHashed = almost;
    stage.ctx->stats.DrawCalls = almost;
    stage.ctx->stats.Layouts = 0xFFFFFFF0ull;
    for (int n = 0; n < 2; ++n) {
        Frame frame;
        begin_window("counters", 400.0f);
        ImTypeset::TextUnformatted(LOREM);
        ImGui::End();
    }
    const ImTypesetStats stats = ImTypeset::GetStats();
    CHECK(stats.VerticesEmitted > almost + 2 * 4 * 300);
    CHECK(stats.VerticesEmitted > 0x80000000ull);
    CHECK(stats.BytesHashed > 0x80000000ull);
    CHECK_EQ(stats.DrawCalls, almost + 2);
    CHECK_EQ(stats.Layouts, 0xFFFFFFF1ull);
    CHECK(sizeof(stats.VerticesEmitted) == 8 && sizeof(stats.BytesHashed) == 8);
}

TEST(destroying_paragraphs_costs_the_same_however_many_there_are)
{
    Stage stage;
    // Oldest first, which a list searched from the front and closed up after
    // each removal makes quadratic. Counted, not timed: the slots are swapped.
    std::vector<ImTypesetParagraph*> all;
    for (int i = 0; i < 20000; ++i)
        all.push_back(ImTypeset::CreateParagraph());
    for (size_t i = 0; i < all.size(); ++i)
        CHECK_EQ(all[i]->slot, i);
    ImTypeset::DestroyParagraph(all[0]);
    // The last took its place: nothing else moved.
    CHECK_EQ(all.back()->slot, size_t{0});
    CHECK_EQ(all[1]->slot, size_t{1});
    CHECK_EQ(stage.ctx->paragraphs.size(), all.size() - 1);
    for (size_t i = 1; i < all.size(); ++i)
        ImTypeset::DestroyParagraph(all[i]);
    CHECK_EQ(stage.ctx->paragraphs.size(), size_t{0});
}

TEST(the_last_error_is_the_last_calls_and_a_success_clears_it)
{
    Stage stage;
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_None));
    CHECK(ImTypeset::AddFontFromFileTTF(ImTypesetFace_Bold, "no/such/font.ttf") == nullptr);
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_FileNotFound));
    CHECK(ImTypeset::GetLastError()->Message[0] != '\0');
    // Asking does not clear it; the next reporting call that succeeds does.
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_FileNotFound));
    CHECK(ImTypeset::AddFontFromFileTTF(ImTypesetFace_Bold, font_path("Roboto-Bold.ttf").c_str()) !=
          nullptr);
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_None));
    CHECK_EQ(ImTypeset::GetLastError()->Message[0], '\0');

    // A paragraph keeps its own, and a layout that finds nothing wrong leaves
    // nothing behind from the layout before it.
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    CHECK(ImTypeset::SetParagraphText(p, "Some words."));
    CHECK(ImTypeset::LayoutParagraph(p, 200.0f));
    p->error.set(ImTypesetError_LayoutFailed, "left over from a layout at another width");
    CHECK(ImTypeset::LayoutParagraph(p, 210.0f));
    CHECK_EQ(ImTypeset::GetParagraphError(p)->Code, static_cast<int>(ImTypesetError_None));
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_None));

    // What is wrong with the text stays wrong at every width, and is each
    // call's answer.
    ImTypesetRun backwards[2];
    backwards[0].Start = 5;
    backwards[0].End = 9;
    backwards[1].Start = 2;
    backwards[1].End = 4;
    // (A run out of order is left out and the text is set: a "done,
    // differently" code, not one that says nothing was done.)
    CHECK(!ImTypeset::SetParagraphText(p, "Some words.", nullptr, backwards, 2));
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_DescriptionCut));
    CHECK(!ImTypeset::LayoutParagraph(p, 200.0f));
    CHECK(!ImTypeset::LayoutParagraph(p, 220.0f));
    CHECK_EQ(ImTypeset::GetParagraphError(p)->Code,
             static_cast<int>(ImTypesetError_DescriptionCut));
    CHECK(ImTypeset::CreateParagraph() != nullptr);
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_None));
    // The paragraph's own is still there.
    CHECK_EQ(ImTypeset::GetParagraphError(p)->Code,
             static_cast<int>(ImTypesetError_DescriptionCut));
}

// --- Fonts --------------------------------------------------------------------------

TEST(a_face_is_added_to_the_atlas_once_and_a_bad_font_changes_nothing)
{
    Stage stage(false);
    REQUIRE(stage.ctx != nullptr);
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    const int before = atlas->Fonts.Size;

    ImFont* regular = ImTypeset::AddFontFromFileTTF(ImTypesetFace_Regular,
                                                    font_path("Roboto-Regular.ttf").c_str());
    REQUIRE(regular != nullptr);
    CHECK_EQ(atlas->Fonts.Size, before + 1);
    CHECK(ImTypeset::GetFont(ImTypesetFace_Regular) == regular);
    CHECK(ImTypeset::GetFont(ImTypesetFace_Bold) == nullptr);

    // A host's FontNo is not passed on: the rebuilt file has one face.
    ImFontConfig numbered;
    numbered.FontNo = 3;
    ImFont* bold = ImTypeset::AddFontFromFileTTF(
        ImTypesetFace_Bold, font_path("Roboto-Bold.ttf").c_str(), 0, &numbered);
    REQUIRE(bold != nullptr);
    CHECK_EQ(atlas->Fonts.Size, before + 2);
    // The bold code role reads the bold prose font until it has its own.
    CHECK(ImTypeset::GetFont(ImTypesetFace_CodeBold) == bold);

    const int settled = atlas->Fonts.Size;
    const uint32_t generation = stage.ctx->library.generation();
    const auto refused = [&](ImFont* font, ImTypesetError code) {
        CHECK(font == nullptr);
        CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(code));
        CHECK(ImTypeset::GetLastError()->Message[0] != '\0');
        CHECK_EQ(atlas->Fonts.Size, settled);
        CHECK_EQ(stage.ctx->library.generation(), generation);
        CHECK(ImTypeset::GetFont(ImTypesetFace_Regular) == regular);
    };
    ImFontConfig merging;
    merging.MergeMode = true;
    refused(ImTypeset::AddFontFromFileTTF(ImTypesetFace_Regular,
                                          font_path("Roboto-Bold.ttf").c_str(), 0, &merging),
            ImTypesetError_InvalidArgument);
    CHECK(std::strstr(ImTypeset::GetLastError()->Message, "MergeMode") != nullptr);
    refused(ImTypeset::AddFontFromFileTTF(ImTypesetFace_Regular,
                                          font_path("Roboto-Bold.ttf").c_str(), 1),
            ImTypesetError_FontRefused);
    refused(ImTypeset::AddFontFromFileTTF(ImTypesetFace_Regular, "no/such/font.ttf"),
            ImTypesetError_FileNotFound);
    refused(ImTypeset::AddFontFromFileTTF(ImTypesetFace_Regular, nullptr),
            ImTypesetError_InvalidArgument);
    refused(ImTypeset::AddFontFromFileTTF(99, font_path("Roboto-Bold.ttf").c_str()),
            ImTypesetError_InvalidArgument);
    refused(ImTypeset::AddFontFromFileTTF(-1, font_path("Roboto-Bold.ttf").c_str()),
            ImTypesetError_InvalidArgument);
    // Default is what a run says to keep the paragraph's face, not a role.
    refused(
        ImTypeset::AddFontFromFileTTF(ImTypesetFace_Default, font_path("Roboto-Bold.ttf").c_str()),
        ImTypesetError_InvalidArgument);
    CHECK(ImTypeset::GetFont(ImTypesetFace_Default) == regular);
    // Not a font at all, and a font cut short.
    std::vector<uint8_t> junk(4096, 0x5A);
    refused(ImTypeset::AddFontFromMemoryTTF(ImTypesetFace_Regular, junk.data(),
                                            static_cast<int>(junk.size())),
            ImTypesetError_FontRefused);
    std::vector<uint8_t> file;
    REQUIRE(golden::read_file(font_path("Roboto-Bold.ttf"), file));
    refused(ImTypeset::AddFontFromMemoryTTF(ImTypesetFace_Regular, file.data(),
                                            static_cast<int>(file.size() / 2)),
            ImTypesetError_FontRefused);
    refused(ImTypeset::AddFontFromMemoryTTF(ImTypesetFace_Regular, file.data(), -5),
            ImTypesetError_InvalidArgument);
    // A text face in the math role: it has no MATH table.
    refused(
        ImTypeset::AddFontFromFileTTF(ImTypesetFace_Math, font_path("Roboto-Regular.ttf").c_str()),
        ImTypesetError_FontRefused);
    CHECK(ImTypeset::GetFont(ImTypesetFace_Math) == nullptr);

    // From memory, the bytes are copied: the caller's buffer can go.
    {
        std::vector<uint8_t> copy = file;
        ImFont* again = ImTypeset::AddFontFromMemoryTTF(ImTypesetFace_BoldItalic, copy.data(),
                                                        static_cast<int>(copy.size()));
        CHECK(again != nullptr);
        std::fill(copy.begin(), copy.end(), uint8_t(0));
    }
    CHECK_EQ(atlas->Fonts.Size, settled + 1);

    // And the font draws.
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    ImTypesetRun run;
    run.Start = 0;
    run.End = 4;
    run.Face = ImTypesetFace_BoldItalic;
    CHECK(ImTypeset::SetParagraphText(p, "Bold and regular", nullptr, &run, 1));
    CHECK(ImTypeset::LayoutParagraph(p, 300.0f));
    CHECK_EQ(ImTypeset::GetParagraphError(p)->Code, static_cast<int>(ImTypesetError_None));
    {
        Frame frame;
        ImVector<ImDrawVert> vertices;
        ImVector<ImDrawIdx> indices;
        CHECK(ImTypeset::DrawParagraphToBuffers(
            p, ImVec2(0.0f, 0.0f), ImVec4(-1e4f, -1e4f, 1e4f, 1e4f), &vertices, &indices));
        // Fourteen letters and two spaces.
        CHECK_EQ(vertices.Size, 14 * 4);
        CHECK_EQ(indices.Size, 14 * 6);
    }
}

TEST(replacing_a_faces_font_remeasures_text_already_set)
{
    Stage stage;
    const char* text = "Waffle fjord: kerning, widths and all.";
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    ImTypesetParagraph* italic = ImTypeset::CreateParagraph();
    ImTypeset::SetParagraphText(p, text);
    ImTypesetRun whole;
    whole.Start = 0;
    whole.End = static_cast<int>(std::strlen(text));
    whole.Face = ImTypesetFace_Italic;
    ImTypeset::SetParagraphText(italic, text, nullptr, &whole, 1);
    ImTypeset::LayoutParagraph(p, 500.0f);
    ImTypeset::LayoutParagraph(italic, 500.0f);
    const auto same_positions = [&] {
        const std::vector<DrawCmd>& a = shipped_commands(*p);
        const std::vector<DrawCmd>& b = shipped_commands(*italic);
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (a[i].type != b[i].type || a[i].x != b[i].x || a[i].width != b[i].width)
                return false;
        }
        return true;
    };
    CHECK(!same_positions());
    // The Regular role is now played by the italic file. What was measured
    // from the old one must not be answered from a memo.
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    const int fonts = atlas->Fonts.Size;
    ImFont* const old = ImTypeset::GetFont(ImTypesetFace_Regular);
    ImFont* const replacement = ImTypeset::AddFontFromFileTTF(
        ImTypesetFace_Regular, font_path("Roboto-RegularItalic.ttf").c_str());
    REQUIRE(replacement != nullptr);
    // The font it replaces leaves the atlas with the role: a font picker does
    // not grow the atlas by one font a pick.
    CHECK_EQ(atlas->Fonts.Size, fonts);
    CHECK(!atlas->Fonts.contains(old) || old == replacement);
    CHECK(ImTypeset::GetFont(ImTypesetFace_Regular) == replacement);
    ImTypeset::LayoutParagraph(p, 500.0f);
    ImTypeset::LayoutParagraph(italic, 500.0f);
    CHECK(same_positions());
}

// --- Against the reference ----------------------------------------------------------

namespace
{

struct Replay {
    int cases = 0;
    int compositions = 0;
    int glyphs = 0;
    int vertex_compared = 0;
    int lines_checked = 0;
    std::map<std::string, int> skipped;
};

// GetParagraphLine against the commands: every outermost LINE, in order, with
// its baseline and height, its content inside its box, and text ranges that
// tile the paragraph's text. Returns how many lines it looked at.
int check_lines(const ImTypesetParagraph* paragraph, const std::vector<DrawCmd>& cmds)
{
    const double ppi = paragraph->layout_style.px_per_inch;
    const auto px = [&](Scaled v) { return static_cast<float>(px_from_sp(v, ppi)); };
    int line = 0;
    uint32_t inside_until = 0;
    int previous_end = -1;
    for (const DrawCmd& cmd : cmds) {
        if (!cmd.is(CmdType::LINE))
            continue;
        // A line of a box nested in a line has indices inside the outer one's.
        if (line > 0 && cmd.index < inside_until && cmd.end <= inside_until && cmd.index != cmd.end)
            continue;
        if (line > 0 && cmd.index == cmd.end && cmd.index < inside_until)
            continue;
        inside_until = std::max(cmd.end, cmd.index);
        if (!CHECK(line < ImTypeset::GetParagraphLineCount(paragraph)))
            return line;
        const ImTypesetLine got = ImTypeset::GetParagraphLine(paragraph, line);
        CHECK_CONTEXT("line %d", line);
        CHECK_EQ(got.Baseline, px(cmd.y));
        CHECK_EQ(got.Min.y, px(cmd.y - cmd.height));
        CHECK_EQ(got.Max.y, px(cmd.y + cmd.depth));
        // Not always inside the line's box: an overfull line's content runs
        // past its measure.
        CHECK(got.Min.x >= px(cmd.x));
        CHECK(got.Min.x <= got.Max.x);
        CHECK(got.TextStart <= got.TextEnd);
        if (previous_end >= 0)
            CHECK_EQ(got.TextStart, previous_end);
        previous_end = got.TextEnd;
        ++line;
    }
    CHECK_EQ(ImTypeset::GetParagraphLineCount(paragraph), line);
    CHECK_EQ(static_cast<size_t>(line), paragraph->layout.lines.size());
    if (line > 0)
        CHECK_EQ(previous_end, static_cast<int>(paragraph->text.size()));
    // A line that is not one is all zero, which no line is.
    for (const int not_one : {line, -1}) {
        const ImTypesetLine none = ImTypeset::GetParagraphLine(paragraph, not_one);
        CHECK(none.Baseline == 0.0f && none.Max.y == 0.0f && none.TextEnd == 0);
    }
    if (line > 0)
        CHECK(ImTypeset::GetParagraphLine(paragraph, 0).Baseline > 0.0f);
    return line;
}

bool whitespace(const std::string& text, bool at_end)
{
    if (text.empty())
        return false;
    std::vector<uint32_t> cps;
    decode_utf8(text, cps);
    return is_space(at_end ? cps.back() : cps.front());
}

bool all_whitespace(const std::string& text)
{
    std::vector<uint32_t> cps;
    decode_utf8(text, cps);
    for (const uint32_t c : cps) {
        if (!is_space(c))
            return false;
    }
    return true;
}

// One `para` case through the public API. Returns the reason it cannot be
// expressed, or null when it was replayed.
const char* replay_case(Stage& stage, const golden::File& file, const core_golden::Case& c,
                        ImTypesetParagraph* paragraph, Replay& tally)
{
    const std::vector<golden::Record>& records = file.records();
    size_t at = c.first;
    const auto next = [&](const char* tag) -> const golden::Record& {
        while (at < c.end && !records[at].is(tag))
            ++at;
        return records[at < c.end ? at++ : c.end - 1];
    };

    const golden::Record& source = next("source");
    const std::string source_kind(source.raw(0));
    const std::string source_text = source.str(1);
    const golden::Record& block = next("block");
    ImTypesetStyle style;
    style.PixelsPerInch = static_cast<float>(block.f64(0));
    if (static_cast<double>(style.PixelsPerInch) != block.f64(0))
        return "a resolution that is not a float";
    const std::string spacing(block.raw(1));
    style.Spacing = spacing == "french"      ? ImTypesetSpacing_French
                    : spacing == "nonfrench" ? ImTypesetSpacing_TeX
                                             : ImTypesetSpacing_Sentence;
    const std::string align(block.raw(2));
    style.Align = align == "left"     ? ImTypesetAlign_Left
                  : align == "center" ? ImTypesetAlign_Center
                  : align == "right"  ? ImTypesetAlign_Right
                                      : ImTypesetAlign_Justify;
    FontSpec block_font;
    if (!(parse_font_key_text(block.str(4), &block_font)))
        return "a bad block font";
    const Scaled left_indent = block.i64(5);
    if (block.i64(6) != 0)
        return "a first-line indent, which the reference applies twice";
    const golden::Record& faces = next("faces");
    if (faces.str(0) != "prose" || faces.str(1) != "math" || faces.str(2) != "code")
        return "a theme whose stand-in faces are not the defaults";
    const Scaled baselineskip = next("spec").i64(1);
    const Scaled lineskip = next("spec").i64(1);

    struct Event {
        bool hard_break = false;
        FontSpec font;
        bool breakable = true;
        std::string text;
    };
    std::vector<Event> events;
    next("runs");
    while (at < c.end && !records[at].is("endruns")) {
        const golden::Record& record = records[at++];
        Event event;
        if (record.is("hardbreak")) {
            event.hard_break = true;
        } else {
            if (!(parse_font_key_text(record.str(0), &event.font)))
                return "a bad run font";
            event.breakable = record.boolean(1);
            event.text = record.str(2);
        }
        events.push_back(event);
    }
    ++at;
    if (at < c.end && records[at].is("marker"))
        return "a list marker";

    // --- The style, which must resolve to exactly the reference's lengths.
    const double ppi = static_cast<double>(style.PixelsPerInch);
    style.Face = static_cast<ImTypesetFace>(block_font.face) + 1;
    style.EmSize = static_cast<float>(px_from_sp(block_font.size, ppi));
    style.LeftIndent = static_cast<float>(px_from_sp(left_indent, ppi));
    style.LineHeight = static_cast<float>(static_cast<double>(baselineskip) /
                                          static_cast<double>(block_font.size));
    style.Color = IM_COL32(0x11, 0x22, 0x33, 0xFF);
    const ResolvedStyle resolved = resolve_style(*stage.ctx, style);
    if (resolved.em != block_font.size)
        return "a size that is not a float number of pixels";
    if (resolved.left_indent != left_indent)
        return "an indent that is not a float number of pixels";
    if (resolved.baselineskip != baselineskip || resolved.lineskip != lineskip)
        return "a leading that is not a short decimal of the size";

    // --- The text and its runs.
    std::string text;
    std::vector<ImTypesetRun> runs;
    ImTypesetTextFlags flags = 0;
    if (source_kind == "text" || source_kind == "text-hard-breaks") {
        text = source_text;
        if (source_kind == "text-hard-breaks")
            flags |= ImTypesetTextFlags_HardBreaks;
        for (const Event& event : events) {
            if (!event.hard_break && !(event.font.key() == block_font.key()))
                return "a text block set in another font than its block's";
        }
    } else {
        bool any_hard = false;
        for (const Event& event : events)
            any_hard = any_hard || event.hard_break;
        if (any_hard)
            flags |= ImTypesetTextFlags_HardBreaks;
        // Where the API's white space rule and the reference's raw runs part.
        bool nothing_yet = true;
        bool after_space = false;
        bool line_start = false;
        for (size_t i = 0; i < events.size(); ++i) {
            const Event& event = events[i];
            if (event.hard_break) {
                text += "\n";
                line_start = true;
                after_space = false;
                continue;
            }
            if (any_hard && event.text.find_first_of("\r\n") != std::string::npos)
                return "a newline inside a run beside hard breaks";
            if (event.text.empty())
                continue;
            if (!nothing_yet && whitespace(event.text, false) && (after_space || line_start))
                return "white space the API collapses and the reference's raw runs do not";
            ImTypesetRun run;
            run.Start = static_cast<int>(text.size());
            text += event.text;
            run.End = static_cast<int>(text.size());
            run.Face = static_cast<ImTypesetFace>(event.font.face) + 1;
            if (event.font.size != block_font.size) {
                run.Scale = static_cast<float>(static_cast<double>(event.font.size) /
                                               static_cast<double>(block_font.size));
                char decimal[32];
                std::snprintf(decimal, sizeof decimal, "%.7g", static_cast<double>(run.Scale));
                if (round_to_scaled(static_cast<double>(block_font.size) *
                                    std::strtod(decimal, nullptr)) != event.font.size)
                    return "a run size that is not a short decimal of the block's";
            }
            if (!event.breakable)
                run.Flags |= ImTypesetRunFlags_NoBreak;
            runs.push_back(run);
            if (!all_whitespace(event.text)) {
                nothing_yet = false;
                line_start = false;
            }
            after_space = whitespace(event.text, true);
        }
        if (after_space && !nothing_yet)
            return "white space the API collapses and the reference's raw runs do not";
    }

    CHECK(ImTypeset::SetParagraphText(paragraph, text.data(), text.data() + text.size(),
                                      runs.data(), static_cast<int>(runs.size()), flags));

    // --- One layout per width.
    std::vector<DrawCmd> expected;
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    Drawer reference;
    DrawBuffers reference_buffers;
    bool any = false;
    while (at < c.end) {
        const golden::Record& assemble = next("assemble");
        if (!assemble.is("assemble"))
            break;
        const Scaled width = assemble.i64(1);
        CHECK_CONTEXT("width %s", assemble.str(0).c_str());
        const golden::Record& head = next("cmds");
        expected.clear();
        for (uint32_t n = head.u32(0); n > 0 && at < c.end; --n) {
            const golden::Record& r = records[at++];
            if (r.is("glyph")) {
                FontSpec font;
                if (!(parse_font_key_text(r.str(0), &font)))
                    return "a bad glyph font";
                expected.push_back(
                    DrawCmd::glyph(font.key(), r.u32(1), r.i64(2), r.i64(3), r.u32(4)));
            } else if (r.is("rulerect")) {
                expected.push_back(DrawCmd::rule_rect(r.i64(0), r.i64(1), r.i64(2), r.i64(3)));
            } else if (r.is("setglue")) {
                expected.push_back(DrawCmd::set_glue(r.i64(0), r.i64(1), r.i64(2), r.i64(3),
                                                     r.i64(4), r.i64(5), r.u32(6)));
            } else if (r.is("atom")) {
                expected.push_back(DrawCmd::atom(r.i64(0), r.i64(1), r.i64(2), r.i64(3), r.i64(4),
                                                 r.i64(5), r.u32(6)));
            } else if (r.is("line")) {
                expected.push_back(DrawCmd::line(r.i64(0), r.i64(1), r.i64(2), r.i64(3), r.i64(4),
                                                 r.u32(5), r.u32(6)));
            } else {
                FAIL("line %d: not a draw command", r.line);
            }
        }
        const float width_px = static_cast<float>(px_from_sp(width, ppi));
        if (resolve_width(width_px, resolved) != width) {
            ++tally.skipped["a width that is not a float number of pixels"];
            continue;
        }
        CHECK(ImTypeset::LayoutParagraph(paragraph, width_px, &style));
        CHECK_EQ(ImTypeset::GetParagraphError(paragraph)->Code,
                 static_cast<int>(ImTypesetError_None));
        const std::vector<DrawCmd>& actual = shipped_commands(*paragraph);
        // The lines the API reports are the reference's LINE commands that
        // are not inside another, one for one.
        tally.lines_checked += check_lines(paragraph, expected);
        if (!same_commands(expected, actual)) {
            FAIL("the shipped commands differ from the reference's (%zu expected, %zu shipped)",
                 expected.size(), actual.size());
            continue;
        }
        any = true;
        ++tally.compositions;
        for (const DrawCmd& cmd : expected)
            tally.glyphs += cmd.is(CmdType::GLYPH) ? 1 : 0;

        // The reference's commands through src/draw, against the public call.
        const ImVec2 origin(37.0f, 11.5f);
        const ImVec4 clip(-1.0e5f, -1.0e5f, 1.0e5f, 1.0e5f);
        DrawRun run;
        run.cmds = expected.data();
        run.count = expected.size();
        run.ink = style.Color;
        DrawArgs args;
        args.origin = origin;
        args.px_per_inch = ppi;
        args.palette.rule = style.Color;
        reference_buffers.clear();
        reference.draw_runs(reference_buffers, clip, &run, 1, args, stage.ctx->fonts,
                            stage.ctx->resolve);
        vertices.resize(0);
        indices.resize(0);
        CHECK(ImTypeset::DrawParagraphToBuffers(paragraph, origin, clip, &vertices, &indices));
        CHECK_EQ(vertices.Size, reference_buffers.vertices.Size);
        CHECK_EQ(indices.Size, reference_buffers.indices.Size);
        if (vertices.Size == reference_buffers.vertices.Size && vertices.Size > 0) {
            CHECK(std::memcmp(vertices.Data, reference_buffers.vertices.Data,
                              static_cast<size_t>(vertices.Size) * sizeof(ImDrawVert)) == 0);
            bool same = indices.Size == reference_buffers.indices.Size;
            for (int i = 0; same && i < indices.Size; ++i)
                same = indices[i] == reference_buffers.indices[i];
            CHECK(same);
            tally.vertex_compared += vertices.Size;
        }
    }
    if (any)
        ++tally.cases;
    return nullptr;
}

} // namespace

TEST(text_to_commands_to_vertices_equals_the_reference_chain)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    // The goldens are the reference's, and the public API lays out under
    // TexRules::product() unless told otherwise (src/tex_rules.h).
    stage.ctx->tex_rules = ImTypeset::detail::TexRules::reference();
    // And draws under DrawRules::product(), which places a word differently
    // (src/draw.h).
    stage.ctx->draw_rules = ImTypeset::detail::DrawRules::reference();
    static const char* const FILES[] = {"para/edge.golden",    "para/whitespace.golden",
                                        "para/standin.golden", "para/runs.golden",
                                        "para/prose.golden",   "para/sample.golden"};
    Replay tally;
    int total = 0;
    ImTypesetParagraph* paragraph = ImTypeset::CreateParagraph();
    {
        Frame frame;
        for (const char* name : FILES) {
            CHECK_CONTEXT("%s", name);
            golden::File file;
            if (!CHECK(file.load(golden::path(name))))
                continue;
            for (const core_golden::Case& c : core_golden::cases(file)) {
                if (c.kind != "para")
                    continue;
                CHECK_CONTEXT("case %s (line %d)", c.name.c_str(), c.line);
                ++total;
                if (const char* why = replay_case(stage, file, c, paragraph, tally))
                    ++tally.skipped[why];
            }
        }
    }
    std::printf("api: %d of %d reference paragraphs replayed through the public API: %d "
                "compositions, %d glyphs, %d vertices compared, %d lines\n",
                tally.cases, total, tally.compositions, tally.glyphs, tally.vertex_compared,
                tally.lines_checked);
    for (const auto& entry : tally.skipped)
        std::printf("api:   not expressible (%d): %s\n", entry.second, entry.first.c_str());
    // A replay that quietly shrank must not read as a pass.
    CHECK(total >= 240);
    CHECK(tally.cases >= 100);
    CHECK(tally.compositions >= 250);
    CHECK(tally.vertex_compared > 100000);
    CHECK(tally.lines_checked > 1000);
}

// --- The deliberate departures -------------------------------------------------------

TEST(the_first_line_indent_is_applied_once)
{
    Stage stage;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    ImTypeset::SetParagraphText(p, LOREM);
    ImTypesetStyle style = ImTypeset::GetStyle();
    style.LeftIndent = 10.0f;
    style.FirstLineIndent = 30.0f;
    ImTypeset::LayoutParagraph(p, 400.0f, &style);
    const std::vector<DrawCmd>& cmds = shipped_commands(*p);
    Scaled first_x = -1;
    Scaled second_line_x = -1;
    int lines = 0;
    for (const DrawCmd& cmd : cmds) {
        if (cmd.is(CmdType::LINE)) {
            ++lines;
        } else if (cmd.is(CmdType::GLYPH)) {
            if (lines == 1 && first_x < 0)
                first_x = cmd.x;
            if (lines == 2 && second_line_x < 0)
                second_line_x = cmd.x;
        }
    }
    // tex.web section 1091: one box as wide as the indent. The reference's
    // first glyph would be at 10 + 2 * 30 px.
    CHECK_EQ(first_x, sp_from_px(10.0) + sp_from_px(30.0));
    CHECK_EQ(second_line_x, sp_from_px(10.0));
    // And the first line is as wide as the others: its last glyph reaches as
    // far right as the second line's.
    CHECK(lines > 3);
}

TEST(white_space_collapses_across_run_boundaries)
{
    Stage stage;
    ImTypesetParagraph* plain = ImTypeset::CreateParagraph();
    ImTypesetParagraph* styled = ImTypeset::CreateParagraph();
    ImTypeset::SetParagraphText(plain, "alpha beta gamma");
    // The same words, with the white space between them split over three runs
    // and padded at both ends.
    const char* text = "  alpha  \t beta \n  gamma \n";
    ImTypesetRun runs[3];
    runs[0].Start = 0;
    runs[0].End = 9; // "  alpha  "
    runs[1].Start = 9;
    runs[1].End = 16; // "\t beta "
    runs[1].Color = IM_COL32(200, 0, 0, 255);
    runs[2].Start = 16;
    runs[2].End = 26;
    CHECK(ImTypeset::SetParagraphText(styled, text, nullptr, runs, 3));
    ImTypeset::LayoutParagraph(plain, 300.0f);
    ImTypeset::LayoutParagraph(styled, 300.0f);
    CHECK(same_commands(shipped_commands(*plain), shipped_commands(*styled)));
    CHECK_EQ(copied_all(styled), std::string("alpha beta gamma"));

    // The run's colour reaches its glyphs and no others.
    Frame frame;
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    REQUIRE(ImTypeset::DrawParagraphToBuffers(
        styled, ImVec2(0.0f, 0.0f), ImVec4(-1e4f, -1e4f, 1e4f, 1e4f), &vertices, &indices));
    int red = 0;
    int ink = 0;
    for (const ImDrawVert& v : vertices) {
        red += v.col == IM_COL32(200, 0, 0, 255) ? 1 : 0;
        ink += v.col == ImTypeset::GetStyle().Color ? 1 : 0;
    }
    CHECK_EQ(red, 4 * 4);  // beta
    CHECK_EQ(ink, 10 * 4); // alpha, gamma
    CHECK_EQ(vertices.Size, 14 * 4);
}

// --- The performance contract ---------------------------------------------------------

namespace
{

// Five different paragraphs, one with a hard break and one a different size
// of type, as one frame of a host might hold.
std::vector<std::string> several_texts()
{
    std::vector<std::string> texts;
    for (int i = 0; i < 5; ++i)
        texts.push_back("Paragraph " + std::to_string(i) + ". " + LOREM);
    texts[3] += "\nAnd a second line of its own.";
    return texts;
}

} // namespace

TEST(the_second_frame_lays_nothing_out_and_allocates_nothing)
{
    Stage stage;
    // Tall enough that all five are in view at both widths.
    ImGui::GetIO().DisplaySize = ImVec2(800.0f, 2000.0f);
    const std::vector<std::string> texts = several_texts();
    ImTypesetStyle larger = ImTypeset::GetStyle();
    larger.EmSize = 20.0f;
    larger.Align = ImTypesetAlign_Left;
    Cost before, after;
    size_t frame_allocations = 0;
    bool ok = true;
    const auto frame = [&](float width) {
        const size_t at_new_frame = g_allocations;
        {
            Frame f;
            begin_window("cache", width, 2000.0f);
            before = Cost::now();
            for (size_t i = 0; i < texts.size(); ++i) {
                ok = ImTypeset::TextUnformatted(texts[i].c_str(), nullptr,
                                                i == 3 ? ImTypesetTextFlags_HardBreaks : 0, 0.0f,
                                                i == 4 ? &larger : nullptr) &&
                     ok;
            }
            after = Cost::now();
            ImGui::End();
        }
        frame_allocations = g_allocations - at_new_frame;
    };
    size_t bytes = 0;
    for (const std::string& text : texts)
        bytes += text.size();

    frame(400.0f);
    CHECK(ok);
    CHECK_EQ(after.stats.TextBuilds - before.stats.TextBuilds, 5);
    CHECK_EQ(after.stats.Layouts - before.stats.Layouts, 5);

    // THE SECOND FRAME, all five, from the first call to the last.
    frame(400.0f);
    CHECK(ok);
    CHECK_EQ(after.stats.TextBuilds - before.stats.TextBuilds, 0);
    CHECK_EQ(after.stats.Layouts - before.stats.Layouts, 0);
    CHECK_EQ(after.stats.DrawCalls - before.stats.DrawCalls, 5);
    CHECK(after.stats.VerticesEmitted - before.stats.VerticesEmitted > 5 * 4 * 300);
    // Each text is read twice: once to find its layout, once to be sure of it.
    CHECK_EQ(after.stats.BytesHashed - before.stats.BytesHashed, 2 * bytes);
    CHECK_EQ(after.allocations - before.allocations, size_t{0});
    CHECK_EQ(after.stats.CachedParagraphs, 5);
    CHECK(after.stats.CachedBytes > bytes);
    // The whole frame, ImGui's own work included (NewFrame to Render), is
    // printed and not asserted: what ImGui allocates is ImGui's.
    std::printf("api: allocations in the whole second frame, ImGui's included: %zu\n",
                frame_allocations);
    frame(400.0f);
    CHECK_EQ(after.allocations - before.allocations, size_t{0});
    std::printf("api: allocations in the whole third frame, ImGui's included: %zu\n",
                frame_allocations);

    // A new width reflows, from the same material.
    frame(300.0f);
    CHECK_EQ(after.stats.TextBuilds - before.stats.TextBuilds, 0);
    CHECK_EQ(after.stats.Layouts - before.stats.Layouts, 5);
    frame(300.0f);
    CHECK_EQ(after.stats.Layouts - before.stats.Layouts, 0);
    CHECK_EQ(after.allocations - before.allocations, size_t{0});

    // A style change that moves glyphs reflows; a colour does not.
    ImTypeset::GetStyle().Color = IM_COL32(9, 9, 9, 255);
    frame(300.0f);
    CHECK_EQ(after.stats.Layouts - before.stats.Layouts, 0);
    ImTypeset::GetStyle().LineHeight = 1.5f;
    frame(300.0f);
    CHECK_EQ(after.stats.TextBuilds - before.stats.TextBuilds, 0);
    CHECK_EQ(after.stats.Layouts - before.stats.Layouts, 4); // the fifth has its own style
    ImTypeset::GetStyle().EmSize = 18.0f;
    frame(300.0f);
    CHECK_EQ(after.stats.TextBuilds - before.stats.TextBuilds, 4);

    // A font added makes every layout stale, once.
    REQUIRE(ImTypeset::AddFontFromFileTTF(ImTypesetFace_Regular,
                                          font_path("Roboto-RegularItalic.ttf").c_str()));
    frame(300.0f);
    CHECK_EQ(after.stats.TextBuilds - before.stats.TextBuilds, 5);
    CHECK_EQ(after.stats.Layouts - before.stats.Layouts, 5);
    frame(300.0f);
    CHECK_EQ(after.stats.Layouts - before.stats.Layouts, 0);
}

// --- Identity: the content, never the address ---------------------------------------------

TEST(the_same_words_in_a_new_buffer_each_frame_are_one_layout)
{
    Stage stage;
    std::vector<std::string> kept; // so that no two frames can share an address
    for (int n = 0; n < 40; ++n) {
        kept.emplace_back(LOREM);
        Frame f;
        begin_window("temporary", 400.0f);
        CHECK(ImTypeset::TextUnformatted(kept.back().c_str()));
        ImGui::End();
    }
    CHECK_EQ(ImTypeset::GetStats().Layouts, 1);
    CHECK_EQ(ImTypeset::GetStats().TextBuilds, 1);
    CHECK_EQ(ImTypeset::GetStats().CachedParagraphs, 1);
}

TEST(a_buffer_reused_down_a_list_lays_each_row_out_once_and_an_inserted_row_only_itself)
{
    Stage stage;
    std::vector<std::string> rows;
    for (int i = 0; i < 20; ++i)
        rows.push_back("Row number " + std::to_string(i) + " of the list, with some words.");
    const auto frame = [&] {
        Frame f;
        begin_window("rows", 400.0f, 2000.0f);
        char buffer[256]; // one address for every row, and no PushID
        for (const std::string& row : rows) {
            std::snprintf(buffer, sizeof buffer, "%s", row.c_str());
            ImTypeset::TextUnformatted(buffer);
        }
        ImGui::End();
    };
    for (int n = 0; n < 5; ++n)
        frame();
    CHECK_EQ(ImTypeset::GetStats().Layouts, 20);
    rows.insert(rows.begin(), "A new row at the top.");
    frame();
    CHECK_EQ(ImTypeset::GetStats().Layouts, 21);
    rows.erase(rows.begin() + 7);
    frame();
    CHECK_EQ(ImTypeset::GetStats().Layouts, 21);
    // And what a row shows is what it was given: every row's text is its own.
    CHECK_EQ(ImTypeset::GetStats().TextBuilds, 21);
}

TEST(text_that_changes_every_frame_is_one_layout_by_its_id_and_bounded_without_one)
{
    Stage stage;
    char buffer[64];
    // By ID: one paragraph, set again each frame.
    for (int n = 0; n < 50; ++n) {
        std::snprintf(buffer, sizeof buffer, "frame %d of the counter", n);
        Frame f;
        begin_window("counter", 400.0f);
        CHECK(ImTypeset::TextWithID("##counter", buffer));
        ImGui::End();
    }
    CHECK_EQ(ImTypeset::GetStats().CachedParagraphs, 1);
    CHECK_EQ(ImTypeset::GetStats().TextBuilds, 50);
    // The same text again costs nothing.
    const ImTypesetStats settled = ImTypeset::GetStats();
    for (int n = 0; n < 3; ++n) {
        Frame f;
        begin_window("counter", 400.0f);
        ImTypeset::TextWithID("##counter", buffer);
        ImGui::End();
    }
    CHECK_EQ(ImTypeset::GetStats().TextBuilds, settled.TextBuilds);
    CHECK_EQ(ImTypeset::GetStats().Layouts, settled.Layouts);

    // By content: each frame's text is a new entry, and the limit takes the
    // old ones. Never more than the limit and the two frames that are held.
    ImTypeset::SetTextCacheLimits(16, 0);
    ImU64 most = 0;
    for (int n = 0; n < 400; ++n) {
        std::snprintf(buffer, sizeof buffer, "frame %d of the counter", n);
        Frame f;
        begin_window("counter", 400.0f);
        ImTypeset::TextUnformatted(buffer);
        ImGui::End();
        most = std::max(most, ImTypeset::GetStats().CachedParagraphs);
    }
    CHECK(most <= 16 + 2);
    CHECK(ImTypeset::GetStats().CacheEvictions > 300);
}

TEST(a_steady_frame_larger_than_the_cache_limit_does_not_go_over_the_whole_cache)
{
    // Two hundred paragraphs a frame against a limit of eight: all of them are
    // in use, so there is nothing to drop, and finding that out must not cost
    // a pass over the two hundred every frame. What is observable is the order
    // of arrival the cache keeps: a steady frame sends the sixteen oldest
    // round to the front (its clock), and going over the whole list would
    // send all two hundred round, which leaves the order as it was.
    Stage stage;
    std::vector<std::string> texts;
    for (int i = 0; i < 200; ++i)
        texts.push_back("Entry " + std::to_string(i) + ". " + LOREM);
    const auto frame = [&](size_t count) {
        Frame f;
        begin_window("steady", 400.0f, 400.0f);
        for (size_t i = 0; i < count; ++i)
            ImTypeset::TextUnformatted(texts[i].c_str());
        ImGui::End();
    };
    const auto arrivals = [&] {
        std::vector<uint64_t> keys;
        for (const ImTypesetParagraph* p = stage.ctx->oldest; p != nullptr; p = p->newer)
            keys.push_back(p->key);
        return keys;
    };
    ImTypeset::SetTextCacheLimits(8, 0);
    for (int n = 0; n < 3; ++n)
        frame(200);
    REQUIRE_EQ(ImTypeset::GetStats().CachedParagraphs, 200);
    for (int n = 0; n < 5; ++n) {
        CHECK_CONTEXT("steady frame %d", n);
        const std::vector<uint64_t> before = arrivals();
        REQUIRE_EQ(before.size(), size_t(200));
        frame(200);
        std::vector<uint64_t> expected(before.begin() + 16, before.end());
        expected.insert(expected.end(), before.begin(), before.begin() + 16);
        CHECK(arrivals() == expected);
    }
    CHECK_EQ(ImTypeset::GetStats().Layouts, 200);
    CHECK_EQ(ImTypeset::GetStats().CacheEvictions, 0);

    // Fewer shown: in the first such frame the others were shown one frame
    // ago and are kept; in the next they go, all at once, and what is left is
    // what is shown, which is still more than the limit.
    frame(50);
    CHECK_EQ(ImTypeset::GetStats().CachedParagraphs, 200);
    frame(50);
    CHECK_EQ(ImTypeset::GetStats().CachedParagraphs, 50);
    CHECK_EQ(ImTypeset::GetStats().CacheEvictions, 150);
    // And shown again they are laid out again, once.
    frame(200);
    frame(200);
    CHECK_EQ(ImTypeset::GetStats().Layouts, 350);
    CHECK_EQ(ImTypeset::GetStats().CachedParagraphs, 200);
}

TEST(the_cache_limits_hold_and_a_frame_larger_than_the_limit_still_lays_out_once)
{
    Stage stage;
    std::vector<std::string> texts;
    for (int i = 0; i < 60; ++i)
        texts.push_back("Entry " + std::to_string(i) + ". " + LOREM);
    const auto frame = [&](size_t first, size_t count) {
        Frame f;
        begin_window("limits", 400.0f, 400.0f);
        for (size_t i = first; i < first + count; ++i)
            ImTypeset::TextUnformatted(texts[i].c_str());
        ImGui::End();
    };
    // More in one frame than the limit allows: all kept while they are shown.
    ImTypeset::SetTextCacheLimits(8, 0);
    for (int n = 0; n < 4; ++n)
        frame(0, 30);
    CHECK_EQ(ImTypeset::GetStats().Layouts, 30);
    CHECK_EQ(ImTypeset::GetStats().CachedParagraphs, 30);
    CHECK_EQ(ImTypeset::GetStats().CacheEvictions, 0);
    // Others shown instead: the first thirty go, down to the limit.
    for (int n = 0; n < 4; ++n)
        frame(30, 5);
    CHECK(ImTypeset::GetStats().CachedParagraphs <= 8);
    CHECK(ImTypeset::GetStats().CacheEvictions >= 27);

    // The byte limit, alone: far more entries allowed than fit in the bytes.
    ImTypeset::SetTextCacheLimits(100000, 0);
    for (int n = 0; n < 3; ++n)
        frame(0, 30);
    const ImU64 thirty = ImTypeset::GetStats().CachedBytes;
    CHECK(thirty > 30 * 1000);
    ImTypeset::SetTextCacheLimits(0, static_cast<int>(thirty / 6));
    for (int n = 0; n < 4; ++n)
        frame(40, 2);
    CHECK(ImTypeset::GetStats().CachedBytes <= thirty / 6 + thirty / 15);
    CHECK(ImTypeset::GetStats().CachedParagraphs < 12);
    // What was counted comes back to nothing with the entries.
    ImTypeset::SetTextCacheLimits(1, 1);
    for (int n = 0; n < 4; ++n) {
        Frame f;
    }
    {
        Frame f;
        begin_window("limits", 400.0f, 400.0f);
        ImTypeset::TextUnformatted("x");
        ImGui::End();
    }
    CHECK_EQ(ImTypeset::GetStats().CachedParagraphs, 1);
    REQUIRE(stage.ctx->newest != nullptr);
    CHECK_EQ(ImTypeset::GetStats().CachedBytes, stage.ctx->newest->footprint);
}

TEST(the_same_text_twice_in_a_frame_at_two_widths_keeps_two_layouts)
{
    Stage stage;
    Cost before, after;
    const auto frame = [&] {
        Frame f;
        begin_window("twice", 500.0f, 900.0f);
        before = Cost::now();
        ImTypeset::TextUnformatted(LOREM);
        ImGui::Indent(150.0f);
        ImTypeset::TextUnformatted(LOREM); // the same words, the same ID stack, a narrower column
        ImGui::Unindent(150.0f);
        // And by an explicit width.
        ImTypeset::TextUnformatted(LOREM, nullptr, 0, 200.0f);
        CHECK_EQ(ImGui::GetItemRectSize().x, 200.0f);
        after = Cost::now();
        ImGui::End();
    };
    frame();
    CHECK_EQ(after.stats.Layouts - before.stats.Layouts, 3);
    frame();
    frame();
    CHECK_EQ(after.stats.Layouts - before.stats.Layouts, 0);
    CHECK_EQ(after.stats.TextBuilds - before.stats.TextBuilds, 0);
    CHECK_EQ(after.stats.CachedParagraphs, 3);
    CHECK_EQ(after.allocations - before.allocations, size_t{0});
}

TEST(a_widget_scrolled_out_of_view_emits_nothing_lays_nothing_out_and_allocates_nothing)
{
    Stage stage;
    const int count = 12;
    std::vector<Cost> before(count), after(count);
    std::vector<int> vertices(count);
    std::vector<ImVec2> tops(count);
    float scroll = 0.0f;
    float scroll_max = 0.0f;
    const auto frame = [&] {
        Frame f;
        begin_window("outer", 420.0f, 400.0f);
        ImGui::BeginChild("clipped", ImVec2(0.0f, 150.0f));
        ImGui::SetScrollY(scroll);
        ImDrawList* list = ImGui::GetWindowDrawList();
        for (int i = 0; i < count; ++i) {
            ImGui::PushID(i);
            const int had = list->VtxBuffer.Size;
            before[static_cast<size_t>(i)] = Cost::now();
            ImTypeset::TextUnformatted(LOREM); // the default call, nothing promised
            after[static_cast<size_t>(i)] = Cost::now();
            vertices[static_cast<size_t>(i)] = list->VtxBuffer.Size - had;
            tops[static_cast<size_t>(i)] = ImGui::GetItemRectMin();
            ImGui::PopID();
        }
        scroll_max = ImGui::GetScrollMaxY();
        ImGui::EndChild();
        ImGui::End();
    };
    // Three frames: the child gets its scroll bar on the second, which is a
    // new width for what is in view.
    frame();
    frame();
    frame();
    // The first paragraph is in view; the last is far below the child's edge.
    CHECK(vertices[0] > 0);
    CHECK(scroll_max > 1000.0f);
    const size_t bytes = std::strlen(LOREM);
    for (int i = 0; i < count; ++i) {
        const size_t n = static_cast<size_t>(i);
        CHECK_CONTEXT("paragraph %d", i);
        const ImTypesetStats& a = before[n].stats;
        const ImTypesetStats& b = after[n].stats;
        CHECK_EQ(b.Layouts - a.Layouts, 0);
        CHECK_EQ(b.TextBuilds - a.TextBuilds, 0);
        CHECK_EQ(after[n].allocations - before[n].allocations, size_t{0});
        // What a clipped call does cost, on the default path: its bytes are
        // read, to find the layout and to be sure of it. Nothing else.
        CHECK_EQ(b.BytesHashed - a.BytesHashed, 2 * bytes);
        if (i >= 2) {
            CHECK_EQ(vertices[n], 0);
            CHECK_EQ(b.VerticesEmitted - a.VerticesEmitted, 0);
            CHECK_EQ(b.DrawCalls - a.DrawCalls, 0);
            CHECK_EQ(b.CulledCalls - a.CulledCalls, 1);
        }
        // Clipped or not, each still takes its place in the column.
        if (i > 0)
            CHECK(tops[n].y > tops[n - 1].y + 100.0f);
    }

    // Scrolled to the end, it is the first ones that cost nothing.
    scroll = scroll_max;
    frame();
    frame();
    CHECK_EQ(vertices[0], 0);
    CHECK_EQ(after[0].stats.CulledCalls - before[0].stats.CulledCalls, 1);
    CHECK(vertices[static_cast<size_t>(count - 1)] > 0);
}

TEST(a_width_change_lays_out_what_is_in_view_and_the_rest_when_it_is_seen)
{
    Stage stage;
    std::vector<std::string> texts;
    for (int i = 0; i < 200; ++i)
        texts.push_back(std::string(LOREM) + " " + std::to_string(i));
    float scroll = 0.0f;
    float content = 0.0f;
    std::vector<float> heights(texts.size());
    const auto frame = [&](float width) {
        Frame f;
        begin_window("document", width, 400.0f);
        ImGui::SetScrollY(scroll);
        for (size_t i = 0; i < texts.size(); ++i) {
            ImTypeset::TextUnformatted(texts[i].c_str());
            heights[i] = ImGui::GetItemRectSize().y;
        }
        content = ImGui::GetScrollMaxY();
        ImGui::End();
    };
    frame(400.0f);
    frame(400.0f);
    CHECK_EQ(ImTypeset::GetStats().Layouts, 200);
    const std::vector<float> at_400 = heights;

    // One step of a drag on the window's edge.
    const ImTypesetStats before = ImTypeset::GetStats();
    frame(300.0f);
    const ImTypesetStats after = ImTypeset::GetStats();
    const ImU64 relaid = after.Layouts - before.Layouts;
    std::printf("api: %llu of 200 paragraphs laid out again on a width change, %llu deferred\n",
                static_cast<unsigned long long>(relaid),
                static_cast<unsigned long long>(after.DeferredLayouts - before.DeferredLayouts));
    CHECK(relaid >= 1 && relaid <= 6);
    CHECK_EQ(relaid + (after.DeferredLayouts - before.DeferredLayouts), 200);
    CHECK_EQ(after.TextBuilds, before.TextBuilds);
    // What is in view is at the new width; what is not keeps its height.
    CHECK(heights[0] > at_400[0]);
    CHECK_EQ(heights[150], at_400[150]);
    const float estimated = content;

    // The same width again: the hidden ones are still not laid out.
    frame(300.0f);
    CHECK_EQ(ImTypeset::GetStats().Layouts, after.Layouts);

    // Scrolled to, they are, and only they.
    scroll = content * 0.5f;
    frame(300.0f);
    frame(300.0f);
    const ImU64 seen = ImTypeset::GetStats().Layouts - after.Layouts;
    CHECK(seen >= 1 && seen <= 8);
    // The scroll extent is the estimate until then: it grows as they are seen.
    CHECK(content > estimated);
    // Back at the first width, everything in view is laid out again and the
    // rest, which never changed, is as it was laid out for that width.
    scroll = 0.0f;
    const ImU64 layouts = ImTypeset::GetStats().Layouts;
    frame(400.0f); // the scroll position takes a frame to apply
    frame(400.0f);
    CHECK(ImTypeset::GetStats().Layouts - layouts <= 12);
    CHECK_EQ(heights[0], at_400[0]);
    CHECK_EQ(heights[199], at_400[199]);
}

TEST(drawing_a_long_paragraph_visits_only_the_lines_in_view_whatever_its_length)
{
    Stage stage;
    Frame frame;
    // The same window, 100 px tall, over a document of 1, 10 and 100 times
    // the size. What is bounded is the work: the commands placed and the
    // vertices made. That the lines are FOUND without a scan is src/draw's
    // bisection over the line index, which needs the index sorted; that is
    // asserted here, and the bisection itself is read, not counted.
    const ImVec4 window(0.0f, 0.0f, 380.0f, 100.0f);
    size_t placed[3] = {};
    int emitted[3] = {};
    int lines[3] = {};
    int copies = 3;
    for (int scale = 0; scale < 3; ++scale, copies *= 10) {
        CHECK_CONTEXT("%d copies", copies);
        std::string text;
        for (int i = 0; i < copies; ++i)
            text += std::string(LOREM) + " ";
        ImTypesetParagraph* p = ImTypeset::CreateParagraph();
        ImTypeset::SetParagraphText(p, text.c_str());
        ImTypesetStyle style = ImTypeset::GetStyle();
        // First fit, so that the longest is not also a test of the optimiser.
        style.LineBreak = ImTypesetLineBreak_FirstFit;
        CHECK(ImTypeset::LayoutParagraph(p, 380.0f, &style));
        const ImVec2 size = ImTypeset::GetParagraphSize(p);
        CHECK_EQ(size.x, 380.0f);
        lines[scale] = ImTypeset::GetParagraphLineCount(p);
        CHECK(p->lines.sorted);
        // A third of the way down, wherever that is.
        const float top = std::floor(size.y / 3.0f);
        ImVector<ImDrawVert> some;
        ImVector<ImDrawIdx> indices;
        REQUIRE(ImTypeset::DrawParagraphToBuffers(p, ImVec2(0.0f, -top), window, &some, &indices));
        placed[scale] = stage.ctx->drawer.ops().size();
        emitted[scale] = some.Size;
        for (const ImDrawVert& v : some)
            CHECK(v.pos.y > -30.0f && v.pos.y < 130.0f);

        // The same through a draw list with a clip rectangle pushed.
        if (scale == 1) {
            begin_window("long", 400.0f);
            ImDrawList* list = ImGui::GetWindowDrawList();
            list->PushClipRect(ImVec2(0.0f, 0.0f), ImVec2(400.0f, 100.0f));
            const int had = list->VtxBuffer.Size;
            const ImU64 counted = ImTypeset::GetStats().VerticesEmitted;
            CHECK(ImTypeset::DrawParagraph(p, list, ImVec2(8.0f, -top)));
            list->PopClipRect();
            CHECK_EQ(list->VtxBuffer.Size - had, ImTypeset::GetStats().VerticesEmitted - counted);
            CHECK(list->VtxBuffer.Size - had > 0);
            CHECK(list->VtxBuffer.Size - had <= emitted[scale] + 4 * 200);
            ImGui::End();
            if (sizeof(ImDrawIdx) == 2) {
                // More vertices than a 16-bit index reaches: refused whole.
                ImVector<ImDrawVert> full;
                full.resize(65000);
                indices.resize(0);
                CHECK(!ImTypeset::DrawParagraphToBuffers(p, ImVec2(0.0f, -top), window, &full,
                                                         &indices));
                CHECK_EQ(ImTypeset::GetLastError()->Code,
                         static_cast<int>(ImTypesetError_TooManyVertices));
                CHECK_EQ(full.Size, 65000);
                CHECK_EQ(indices.Size, 0);
            }
        }
        ImTypeset::DestroyParagraph(p);
    }
    std::printf("api: a 100 px window over %d, %d and %d lines: %zu, %zu and %zu commands "
                "placed, %d, %d and %d vertices\n",
                lines[0], lines[1], lines[2], placed[0], placed[1], placed[2], emitted[0],
                emitted[1], emitted[2]);
    CHECK(lines[0] > 10 && lines[1] > 8 * lines[0] && lines[2] > 80 * lines[0]);
    // 100 px is five lines of 21.6 px and the margin lets a neighbour in:
    // seven lines of some seventy characters at the most, at every length.
    for (int scale = 0; scale < 3; ++scale) {
        CHECK_CONTEXT("scale %d", scale);
        CHECK(emitted[scale] > 4 * 100);
        CHECK(placed[scale] <= 7 * 90);
        CHECK(emitted[scale] <= 4 * 7 * 90);
    }
}

TEST(the_paragraph_path_allocates_nothing_in_a_steady_frame)
{
    Stage stage;
    ImTypesetParagraph* heading = ImTypeset::CreateParagraph();
    ImTypesetParagraph* body = ImTypeset::CreateParagraph();
    ImTypeset::SetParagraphText(heading, "A heading over the body");
    ImTypeset::SetParagraphText(body, LOREM);
    // Two styles used alternately in a frame, their leadings different.
    ImTypesetStyle heading_style = ImTypeset::GetStyle();
    heading_style.EmSize = 24.0f;
    heading_style.LineHeight = 1.2f;
    heading_style.Align = ImTypesetAlign_Left;
    ImTypesetStyle body_style = ImTypeset::GetStyle();
    body_style.SelectionColor = IM_COL32(40, 80, 200, 120);
    const ImTypesetTextRange selection = {12, 140};
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    ImVector<ImVec4> rects;
    const ImVec4 clip(-1e4f, -1e4f, 1e4f, 1e4f);
    Cost before, after;
    // How many of the window's draw list's two buffers grew during the calls:
    // the host's memory, which grows once to what a frame draws into it.
    size_t list_grew = 0;
    const auto frame = [&](float width) {
        Frame f;
        begin_window("paragraphs", 500.0f, 600.0f);
        ImDrawList* list = ImGui::GetWindowDrawList();
        vertices.resize(0);
        indices.resize(0);
        rects.resize(0);
        const int vertex_room = list->VtxBuffer.Capacity;
        const int index_room = list->IdxBuffer.Capacity;
        before = Cost::now();
        CHECK(ImTypeset::LayoutParagraph(heading, width, &heading_style));
        CHECK(ImTypeset::LayoutParagraph(body, width, &body_style));
        CHECK(ImTypeset::DrawParagraph(heading, list, ImVec2(10.0f, 10.0f)));
        CHECK(ImTypeset::DrawParagraph(body, list, ImVec2(10.0f, 50.0f), &selection));
        CHECK(ImTypeset::DrawParagraphToBuffers(body, ImVec2(0.0f, 0.0f), clip, &vertices, &indices,
                                                &selection));
        ImTypeset::HitTestParagraph(body, ImVec2(50.0f, 30.0f));
        ImTypeset::SelectParagraphRange(body, ImVec2(5.0f, 5.0f), ImVec2(90.0f, 60.0f),
                                        ImTypesetGranularity_Word);
        ImTypeset::GetParagraphRangeRects(body, selection, &rects);
        ImTypeset::GetParagraphLine(body, 2);
        ImTypeset::GetParagraphCaretRect(body, 77);
        after = Cost::now();
        list_grew = (list->VtxBuffer.Capacity != vertex_room ? 1u : 0u) +
                    (list->IdxBuffer.Capacity != index_room ? 1u : 0u);
        ImGui::End();
    };
    frame(400.0f);
    CHECK_EQ(after.stats.Layouts - before.stats.Layouts, 2);
    // The second frame, and the third. In the second the library allocates
    // nothing; the window's draw list may still grow, because ImGui gives a
    // window's list its room as it is first drawn into.
    for (int n = 0; n < 2; ++n) {
        CHECK_CONTEXT("frame %d", n + 2);
        frame(400.0f);
        CHECK_EQ(after.stats.Layouts - before.stats.Layouts, 0);
        CHECK_EQ(after.stats.TextBuilds - before.stats.TextBuilds, 0);
        CHECK_EQ(after.stats.DrawCalls - before.stats.DrawCalls, 3);
        CHECK_EQ(after.allocations - before.allocations, list_grew);
        if (n == 0)
            std::printf("api: second frame of the paragraph path: %zu allocation(s), all of "
                        "them the draw list growing (%zu of its buffers)\n",
                        after.allocations - before.allocations, list_grew);
        else
            CHECK_EQ(after.allocations - before.allocations, size_t{0});
        CHECK(rects.Size >= 2);
    }
    // Across a resize: the frame that reflows may allocate (the lines are
    // new); the one after it does not.
    frame(310.0f);
    CHECK_EQ(after.stats.Layouts - before.stats.Layouts, 2);
    const size_t reflow = after.allocations - before.allocations;
    frame(310.0f);
    CHECK_EQ(after.allocations - before.allocations, size_t{0});
    // Back to a width it has been at: nothing grows a second time.
    frame(400.0f);
    frame(310.0f);
    CHECK_EQ(after.stats.Layouts - before.stats.Layouts, 2);
    std::printf("api: allocations in a frame that reflows two paragraphs: %zu the first time, "
                "%zu the second\n",
                reflow, after.allocations - before.allocations);
    CHECK(after.allocations - before.allocations <= reflow);
    frame(310.0f);
    CHECK_EQ(after.allocations - before.allocations, size_t{0});
}

// --- Pixels ---------------------------------------------------------------------------

TEST(rendered_ink_stays_in_the_item_and_justified_lines_reach_both_margins)
{
    Stage stage;
    soft_raster::Renderer renderer;
    renderer.init();
    const ImU32 white = IM_COL32(255, 255, 255, 255);
    ImTypeset::GetStyle().Color = IM_COL32(0, 0, 0, 255);
    ImVec2 item_min, item_max;
    soft_raster::Image image(400, 320, white);
    for (int n = 0; n < 3; ++n) {
        ImGui::NewFrame();
        ImGui::PushStyleColor(ImGuiCol_WindowBg, white);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 16.0f));
        begin_window("page", 400.0f, 320.0f);
        ImTypeset::TextUnformatted(LOREM);
        item_min = ImGui::GetItemRectMin();
        item_max = ImGui::GetItemRectMax();
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        ImGui::Render();
        image.clear(white);
        renderer.render(ImGui::GetDrawData(), image);
    }
    renderer.shutdown();

    CHECK_EQ(item_min.x, 24.0f);
    CHECK_EQ(item_max.x, 400.0f - 24.0f);
    // Baseline to baseline is 16 px * 1.35.
    const double leading = 21.6;
    const int lines = static_cast<int>(std::lround((item_max.y - item_min.y) / leading));
    REQUIRE(lines >= 5);
    std::vector<int> left(static_cast<size_t>(lines), image.width);
    std::vector<int> right(static_cast<size_t>(lines), -1);
    long inked = 0;
    long outside = 0;
    long overhang = 0;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            if (image.get(x, y) == white)
                continue;
            ++inked;
            const bool in_rows =
                y >= static_cast<int>(item_min.y) && y < static_cast<int>(item_max.y);
            // The one overhang there is: a glyph is put on a whole pixel, up to
            // half a pixel from where the line's arithmetic had it, and its
            // antialiased edge can then light the column after the margin.
            if (in_rows && x == static_cast<int>(item_max.x)) {
                ++overhang;
                continue;
            }
            if (!in_rows || x < static_cast<int>(item_min.x) || x > static_cast<int>(item_max.x)) {
                ++outside;
                continue;
            }
            const int line = std::min(
                lines - 1, static_cast<int>((y - static_cast<double>(item_min.y)) / leading));
            left[static_cast<size_t>(line)] = std::min(left[static_cast<size_t>(line)], x);
            right[static_cast<size_t>(line)] = std::max(right[static_cast<size_t>(line)], x);
        }
    }
    CHECK(inked > 2000);
    // Nothing the host did not make room for, bar that one column, and little
    // there.
    CHECK_EQ(outside, 0L);
    CHECK(overhang <= 2L * lines);
    std::printf("api: %ld inked pixels, %ld of them in the column after the right margin\n", inked,
                overhang);
    // Every line starts at the left margin, and every line but the last ends
    // at the right one: within a glyph's side bearing, which is what separates
    // an outline's edge from its advance.
    const int margin = 3;
    for (int line = 0; line < lines; ++line) {
        CHECK_CONTEXT("line %d of %d: ink from x=%d to x=%d", line, lines,
                      left[static_cast<size_t>(line)], right[static_cast<size_t>(line)]);
        CHECK(left[static_cast<size_t>(line)] - static_cast<int>(item_min.x) <= margin);
        if (line + 1 < lines)
            CHECK(static_cast<int>(item_max.x) - 1 - right[static_cast<size_t>(line)] <= margin);
    }
    // The last line is short: this is a paragraph, not a block of fill.
    CHECK(static_cast<int>(item_max.x) - right[static_cast<size_t>(lines - 1)] > 20);
    if (!stage.freetype())
        std::printf("api: the pixel test ran under stb_truetype\n");
}

// --- Selection and copy ---------------------------------------------------------------

TEST(positions_are_bytes_of_the_text_and_a_range_copies_what_it_covers)
{
    Stage stage;
    const char* first_text = "The quick brown fox jumps over the lazy dog, twice over.";
    const int size = static_cast<int>(std::strlen(first_text));
    ImTypesetParagraph* first = ImTypeset::CreateParagraph();
    ImTypeset::SetParagraphText(first, first_text);
    // Narrow enough to wrap into several lines.
    CHECK(ImTypeset::LayoutParagraph(first, 120.0f));
    CHECK(ImTypeset::GetParagraphLineCount(first) >= 4);
    // More items than characters: each line ends with glue the text never
    // had. None of that shows through the API.
    CHECK(item_count(first) > size);
    // A wrap is a space, never a newline.
    CHECK_EQ(copied_all(first), std::string(first_text));

    const int brown = static_cast<int>(std::strstr(first_text, "brown") - first_text);
    CHECK_EQ(copied(first, brown, brown + 5), std::string("brown"));
    // Backwards is empty; out of range is clamped.
    CHECK_EQ(copied(first, brown + 5, brown), std::string());
    CHECK_EQ(copied(first, -100, 1 << 30), std::string(first_text));

    // EVERY range of the text copies as exactly those bytes of it, on one
    // line or across the wraps, with one exception: a space a line was broken
    // at is not an item of the line, so a range that begins or ends with that
    // space copies without it.
    std::vector<bool> broken_at(static_cast<size_t>(size) + 1, false);
    for (int n = 0; n + 1 < ImTypeset::GetParagraphLineCount(first); ++n) {
        const ImTypesetLine wrapped = ImTypeset::GetParagraphLine(first, n);
        REQUIRE(wrapped.TextEnd > 0 && first_text[wrapped.TextEnd - 1] == ' ');
        broken_at[static_cast<size_t>(wrapped.TextEnd - 1)] = true;
    }
    int ranges = 0;
    for (int start = 0; start <= size; ++start) {
        for (int end = start; end <= size; ++end) {
            std::string expected(first_text + start, static_cast<size_t>(end - start));
            if (!expected.empty() && broken_at[static_cast<size_t>(start)])
                expected.erase(expected.begin());
            if (!expected.empty() && broken_at[static_cast<size_t>(end - 1)])
                expected.pop_back();
            const std::string got = copied(first, start, end);
            if (got != expected) {
                FAIL("bytes [%d, %d) copied as '%s', not '%s'", start, end, got.c_str(),
                     expected.c_str());
                return;
            }
            ++ranges;
        }
    }
    CHECK(ranges > 1500);

    // Geometry: the rectangle of the word, a hit inside it, a double click.
    ImVector<ImVec4> rects;
    const ImTypesetTextRange word = {brown, brown + 5};
    REQUIRE_EQ(ImTypeset::GetParagraphRangeRects(first, word, &rects), 1);
    const ImVec4 r = rects[0];
    CHECK(r.z > r.x && r.w > r.y);
    CHECK(r.w - r.y > 21.0f && r.w - r.y < 22.0f); // the full height of its line
    const ImVec2 inside(r.x + 0.5f, (r.y + r.w) * 0.5f);
    const ImTypesetHit on_word = ImTypeset::HitTestParagraph(first, inside);
    CHECK_EQ(on_word.TextOffset, brown);
    CHECK_EQ(on_word.Line, 0);
    CHECK(on_word.Inside);
    // "brown" ends its line at this width. The position after it is the
    // position before "fox": the space between them was the break, and a
    // position has no line of its own.
    const ImTypesetLine first_line = ImTypeset::GetParagraphLine(first, 0);
    REQUIRE_EQ(first_line.TextEnd, brown + 6);
    const ImTypesetHit after_word =
        ImTypeset::HitTestParagraph(first, ImVec2(r.z - 0.5f, inside.y));
    CHECK_EQ(after_word.TextOffset, brown + 6);
    // Which side of the break the point was on is the hit's line.
    CHECK_EQ(after_word.Line, 0);
    CHECK(after_word.Inside);
    // Inside a line, the position after a word is the space's.
    const int quick = static_cast<int>(std::strstr(first_text, "quick") - first_text);
    ImVector<ImVec4> quick_rects;
    const ImTypesetTextRange quick_range = {quick, quick + 5};
    REQUIRE_EQ(ImTypeset::GetParagraphRangeRects(first, quick_range, &quick_rects), 1);
    CHECK_EQ(
        ImTypeset::HitTestParagraph(first, ImVec2(quick_rects[0].z - 0.5f, inside.y)).TextOffset,
        quick + 5);
    const ImTypesetTextRange quick_clicked = ImTypeset::SelectParagraphRange(
        first, ImVec2(quick_rects[0].x + 2.0f, inside.y), ImVec2(quick_rects[0].x + 2.0f, inside.y),
        ImTypesetGranularity_Word);
    CHECK_EQ(quick_clicked.Start, quick);
    CHECK_EQ(quick_clicked.End, quick + 5);
    // The same line, past its ragged... its last word: a position, but not on text.
    const ImTypesetHit past = ImTypeset::HitTestParagraph(first, ImVec2(5000.0f, inside.y));
    CHECK(past.TextOffset > brown && past.TextOffset <= size);
    CHECK(!past.Inside);
    const ImTypesetHit above = ImTypeset::HitTestParagraph(first, ImVec2(inside.x, -40.0f));
    CHECK(!above.Inside);
    CHECK_EQ(above.Line, 0);
    const ImTypesetHit below = ImTypeset::HitTestParagraph(first, ImVec2(2.0f, 9000.0f));
    CHECK(!below.Inside);
    CHECK_EQ(below.Line, ImTypeset::GetParagraphLineCount(first) - 1);
    const ImTypesetTextRange clicked =
        ImTypeset::SelectParagraphRange(first, inside, inside, ImTypesetGranularity_Word);
    CHECK_EQ(clicked.Start, brown);
    CHECK_EQ(clicked.End, brown + 6);
    CHECK_EQ(copied(first, clicked.Start, clicked.End), std::string("brown"));
    const ImTypesetTextRange line =
        ImTypeset::SelectParagraphRange(first, inside, inside, ImTypesetGranularity_Line);
    CHECK(line.Start <= brown && line.End >= brown + 5 && line.End - line.Start < size);
    const ImTypesetTextRange none = ImTypeset::SelectParagraphRange(first, inside, inside);
    CHECK(none.Start >= none.End);
    // Hostile points land somewhere in the text.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (const ImVec2 point : {ImVec2(nan, nan), ImVec2(inf, -inf), ImVec2(-1e30f, 1e30f)}) {
        const ImTypesetHit hit = ImTypeset::HitTestParagraph(first, point);
        CHECK(hit.TextOffset >= 0 && hit.TextOffset <= size);
        CHECK(hit.Line >= 0 && hit.Line < ImTypeset::GetParagraphLineCount(first));
    }

    // A SELECTION SURVIVES A REFLOW, because it is bytes: dragged at one
    // width, it is the same words at every other, and in every alignment,
    // where the items it covers are numbered quite differently.
    const ImVec2 from(30.0f, 30.0f);
    const ImVec2 to(80.0f, 75.0f);
    const ImTypesetTextRange dragged = ImTypeset::SelectParagraphRange(first, from, to);
    REQUIRE(dragged.Start < dragged.End);
    const std::string words = copied(first, dragged.Start, dragged.End);
    CHECK(words.size() > 10);
    CHECK_EQ(words, std::string(first_text + dragged.Start,
                                static_cast<size_t>(dragged.End - dragged.Start)));
    const Selection items_at_120 = selection_of(*first, dragged);
    int numbered_differently = 0;
    for (const ImTypesetAlign align : {ImTypesetAlign_Justify, ImTypesetAlign_Left,
                                       ImTypesetAlign_Center, ImTypesetAlign_Right}) {
        for (const float width : {60.0f, 95.0f, 120.0f, 200.0f, 333.0f, 900.0f}) {
            CHECK_CONTEXT("align %d, width %g", align, static_cast<double>(width));
            ImTypesetStyle style = ImTypeset::GetStyle();
            style.Align = align;
            ImTypeset::LayoutParagraph(first, width, &style);
            CHECK_EQ(copied(first, dragged.Start, dragged.End), words);
            rects.resize(0);
            CHECK(ImTypeset::GetParagraphRangeRects(first, dragged, &rects) >= 1);
            numbered_differently += selection_of(*first, dragged) != items_at_120 ? 1 : 0;
        }
    }
    // The thing that did not survive before: the item numbers moved.
    std::printf("api: one selection kept as bytes over 24 layouts; its item numbers differ from "
                "the first layout's in %d of them\n",
                numbered_differently);
    CHECK(numbered_differently >= 6);
    ImTypeset::LayoutParagraph(first, 120.0f);

    // A second paragraph, centred, with hard breaks: a hard break copies as a
    // newline whatever the alignment, and a blank line separates paragraphs.
    const char* given = "Second paragraph \n with a hard break\r\nand another";
    ImTypesetParagraph* second = ImTypeset::CreateParagraph();
    ImTypeset::SetParagraphText(second, given, nullptr, nullptr, 0, ImTypesetTextFlags_HardBreaks);
    const int another = static_cast<int>(std::strstr(given, "and another") - given);
    for (const ImTypesetAlign align : {ImTypesetAlign_Center, ImTypesetAlign_Right,
                                       ImTypesetAlign_Left, ImTypesetAlign_Justify}) {
        CHECK_CONTEXT("align %d", align);
        ImTypesetStyle style = ImTypeset::GetStyle();
        style.Align = align;
        ImTypeset::LayoutParagraph(second, 400.0f, &style);
        CHECK_EQ(ImTypeset::GetParagraphLineCount(second), 3);
        CHECK_EQ(copied_all(second),
                 std::string("Second paragraph\nwith a hard break\nand another"));
        // Flush right every line begins with an item the text never had; the
        // byte offset of a word is the same all the same.
        CHECK_EQ(copied(second, another, another + 3), std::string("and"));
        const ImTypesetLine last = ImTypeset::GetParagraphLine(second, 2);
        CHECK_EQ(last.TextStart, another);
        CHECK_EQ(last.TextEnd, static_cast<int>(std::strlen(given)));
    }
    ImGuiTextBuffer document;
    const ImTypesetTextRange everything = {0, 1 << 30};
    const ImTypesetTextRange empty = {5, 5};
    CHECK_EQ(ImTypeset::AppendParagraphRangeText(first, everything, &document), size);
    // Adds nothing, and no separator.
    CHECK_EQ(ImTypeset::AppendParagraphRangeText(second, empty, &document), 0);
    CHECK(ImTypeset::AppendParagraphRangeText(second, everything, &document) > 2);
    CHECK_EQ(std::string(document.c_str()),
             std::string(first_text) + "\n\nSecond paragraph\nwith a hard break\nand another");

    // A character no face has is drawn as U+FFFD and copies as itself;
    // malformed UTF-8 is U+FFFD from then on.
    ImTypesetParagraph* odd = ImTypeset::CreateParagraph();
    const char* odd_text = "a \xE4\xB8\xAD b \xFF c";
    ImTypeset::SetParagraphText(odd, odd_text);
    ImTypeset::LayoutParagraph(odd, 400.0f);
    bool drew_replacement = false;
    for (const DrawCmd& cmd : shipped_commands(*odd))
        drew_replacement = drew_replacement || (cmd.is(CmdType::GLYPH) && cmd.codepoint == 0xFFFD);
    CHECK(drew_replacement);
    CHECK_EQ(copied_all(odd), std::string("a \xE4\xB8\xAD b \xEF\xBF\xBD c"));
    // Offsets are into what the host passed, malformed or not: 'c' is byte 10
    // and codepoint 8, and a click on it answers the byte.
    CHECK_EQ(copied(odd, 10, 11), std::string("c"));
    CHECK_EQ(ImTypeset::TextByteToCodepointOffset(odd_text, 10), 8);
    CHECK_EQ(ImTypeset::TextCodepointToByteOffset(odd_text, 8), 10);
    CHECK_EQ(ImTypeset::TextCodepointToByteOffset(odd_text, 3), 5);
    // An offset inside a character is the next character's: the range does
    // not hold the character it starts in the middle of.
    CHECK_EQ(copied(odd, 2, 7), std::string("\xE4\xB8\xAD b"));
    CHECK_EQ(copied(odd, 3, 7), std::string(" b"));
    CHECK_EQ(ImTypeset::TextByteToCodepointOffset(odd_text, 3), 3);
    ImVector<ImVec4> c_rect;
    const ImTypesetTextRange c_range = {10, 11};
    REQUIRE_EQ(ImTypeset::GetParagraphRangeRects(odd, c_range, &c_rect), 1);
    CHECK_EQ(ImTypeset::HitTestParagraph(odd, ImVec2(c_rect[0].x + 0.5f, 5.0f)).TextOffset, 10);
    // And for a binding that counts in codepoints, from the paragraph alone.
    CHECK_EQ(ImTypeset::ParagraphByteToCodepointOffset(odd, 10), 8);
    CHECK_EQ(ImTypeset::ParagraphCodepointToByteOffset(odd, 8), 10);
    CHECK_EQ(ImTypeset::ParagraphCodepointToByteOffset(odd, 99), 11);
    CHECK_EQ(ImTypeset::ParagraphByteToCodepointOffset(nullptr, 10), 0);

    // A selection highlights: more vertices, the first of them in its colour.
    Frame frame;
    ImTypeset::GetStyle().SelectionColor = IM_COL32(1, 2, 3, 200);
    ImTypeset::LayoutParagraph(first, 120.0f);
    ImVector<ImDrawVert> plain, marked;
    ImVector<ImDrawIdx> indices;
    const ImVec4 clip(-1e4f, -1e4f, 1e4f, 1e4f);
    CHECK(ImTypeset::DrawParagraphToBuffers(first, ImVec2(0.0f, 0.0f), clip, &plain, &indices));
    CHECK(ImTypeset::DrawParagraphToBuffers(first, ImVec2(0.0f, 0.0f), clip, &marked, &indices,
                                            &word));
    CHECK_EQ(marked.Size, plain.Size + 4);
    if (marked.Size > 0)
        CHECK_EQ(marked[0].col, IM_COL32(1, 2, 3, 200));
}

TEST(lines_and_carets_are_where_the_glyphs_are)
{
    Stage stage;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    ImTypeset::SetParagraphText(p, LOREM);
    const int size = static_cast<int>(std::strlen(LOREM));
    for (const ImTypesetAlign align : {ImTypesetAlign_Justify, ImTypesetAlign_Left,
                                       ImTypesetAlign_Center, ImTypesetAlign_Right}) {
        CHECK_CONTEXT("align %d", align);
        ImTypesetStyle style = ImTypeset::GetStyle();
        style.Align = align;
        style.LeftIndent = 12.0f;
        REQUIRE(ImTypeset::LayoutParagraph(p, 300.0f, &style));
        const int lines = ImTypeset::GetParagraphLineCount(p);
        REQUIRE(lines > 5);
        const ImVec2 block = ImTypeset::GetParagraphSize(p);
        float widest = 0.0f;
        for (int n = 0; n < lines; ++n) {
            CHECK_CONTEXT("line %d", n);
            const ImTypesetLine line = ImTypeset::GetParagraphLine(p, n);
            widest = std::max(widest, line.Max.x);
            // The baseline is 16 px * 1.35 below the one before, and the box
            // is the line's own: a strut of that height.
            CHECK(line.Baseline > line.Min.y && line.Baseline < line.Max.y);
            if (n > 0) {
                const ImTypesetLine above = ImTypeset::GetParagraphLine(p, n - 1);
                CHECK_NEAR(line.Baseline - above.Baseline, 21.6f, 0.01f);
                CHECK_EQ(line.TextStart, above.TextEnd);
            }
            // The text on the line is the words that are on it.
            const std::string on_line = copied(p, line.TextStart, line.TextEnd);
            CHECK(!on_line.empty());
            CHECK(on_line.find('\n') == std::string::npos);
            // Its content box is where its glyphs are: every glyph of the
            // line's commands between Min.x and Max.x.
            const LineBox& box = p->line_boxes[static_cast<size_t>(n)];
            int glyphs = 0;
            for (uint32_t at = box.first; at < box.end; ++at) {
                const DrawCmd& cmd = p->cmds[at];
                if (!cmd.is(CmdType::GLYPH))
                    continue;
                ++glyphs;
                const float x = static_cast<float>(px_from_sp(cmd.x));
                CHECK(x >= line.Min.x - 0.001f && x < line.Max.x);
            }
            CHECK(glyphs > 10);
            // Where the alignment puts it.
            const bool last = n + 1 == lines;
            if (align == ImTypesetAlign_Justify || align == ImTypesetAlign_Left)
                CHECK_NEAR(line.Min.x, 12.0f, 0.01f);
            // A justified line reaches the margin when its glue could be set
            // to; one whose shrink ran out overhangs by what was left.
            if (align == ImTypesetAlign_Justify && !last)
                CHECK_NEAR(line.Max.x, 300.0f, 1.5f);
            if (align == ImTypesetAlign_Right)
                CHECK_NEAR(line.Max.x, 300.0f, 0.01f);
            if (align == ImTypesetAlign_Left || align == ImTypesetAlign_Center)
                CHECK(line.Max.x < 300.0f);
            if (align == ImTypesetAlign_Center)
                CHECK_NEAR(line.Min.x - 12.0f, 300.0f - line.Max.x, 0.01f);

            // A caret at the line's first byte stands at its left edge, and
            // one a character on is to the right of it, on the same line.
            const ImVec4 caret = ImTypeset::GetParagraphCaretRect(p, line.TextStart);
            CHECK_EQ(caret.x, caret.z);
            CHECK_NEAR(caret.x, line.Min.x, 0.001f);
            CHECK_EQ(caret.y, line.Min.y);
            CHECK_EQ(caret.w, line.Max.y);
            const ImVec4 next = ImTypeset::GetParagraphCaretRect(p, line.TextStart + 1);
            CHECK(next.x > caret.x);
            CHECK_EQ(next.y, caret.y);
            // And a click there answers the byte, and the line.
            const ImTypesetHit hit =
                ImTypeset::HitTestParagraph(p, ImVec2(caret.x + 0.25f, line.Baseline));
            CHECK_EQ(hit.TextOffset, line.TextStart);
            CHECK_EQ(hit.Line, n);
        }
        // Past the end: after the last character of the last line.
        const ImTypesetLine last = ImTypeset::GetParagraphLine(p, lines - 1);
        ImVec4 end = ImTypeset::GetParagraphCaretRect(p, size);
        CHECK_NEAR(end.x, last.Max.x, 0.001f);
        CHECK_EQ(end.y, last.Min.y);
        end = ImTypeset::GetParagraphCaretRect(p, 1 << 30);
        CHECK_NEAR(end.x, last.Max.x, 0.001f);
        const ImVec4 start = ImTypeset::GetParagraphCaretRect(p, -5);
        CHECK_EQ(start.y, 0.0f);
        CHECK(start.w > 20.0f);

        // THE BLOCK is as wide as its text when it is ragged right, and the
        // measure otherwise.
        if (align == ImTypesetAlign_Left) {
            CHECK_EQ(block.x, std::ceil(widest - 1.0e-4f));
            CHECK(block.x < 300.0f);
        } else {
            CHECK_EQ(block.x, 300.0f);
        }
    }
}

TEST(a_text_is_measured_without_a_handle_a_window_or_a_frame)
{
    Stage stage;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    ImTypeset::SetParagraphText(p, LOREM);
    ImTypeset::LayoutParagraph(p, 300.0f);
    const ImVec2 wrapped = ImTypeset::CalcTextSize(LOREM, nullptr, 300.0f);
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_None));
    CHECK_EQ(wrapped.x, ImTypeset::GetParagraphSize(p).x);
    CHECK_EQ(wrapped.y, ImTypeset::GetParagraphSize(p).y);
    CHECK(wrapped.y > 150.0f);
    // The same question again is not laid out again.
    const ImU64 layouts = ImTypeset::GetStats().Layouts;
    ImTypeset::CalcTextSize(LOREM, nullptr, 300.0f);
    CHECK_EQ(ImTypeset::GetStats().Layouts, layouts);
    // Nothing is kept for the widgets, and no paragraph is the host's.
    CHECK_EQ(ImTypeset::GetStats().CachedParagraphs, 0);
    CHECK_EQ(stage.ctx->paragraphs.size(), size_t{1});

    // Unwrapped: one line, as wide as it comes.
    const ImVec2 one = ImTypeset::CalcTextSize("Three short words");
    CHECK(one.x > 80.0f && one.x < 160.0f);
    CHECK_EQ(one.y, 22.0f); // 21.6, in whole pixels
    const ImVec2 longer = ImTypeset::CalcTextSize(LOREM);
    CHECK(longer.x > 2000.0f);
    CHECK_EQ(longer.y, one.y);
    // One per newline with hard breaks.
    const ImVec2 two = ImTypeset::CalcTextSize("Three short words\nand four more words", nullptr,
                                               -1.0f, ImTypesetTextFlags_HardBreaks);
    CHECK_EQ(two.y, 44.0f); // 43.2
    CHECK(two.x > one.x);
    // A style of its own, and a width it wraps at whatever the alignment.
    ImTypesetStyle big = ImTypeset::GetStyle();
    big.EmSize = 32.0f;
    big.Align = ImTypesetAlign_Left;
    const ImVec2 large = ImTypeset::CalcTextSize("Three short words", nullptr, 0.0f, 0, &big);
    CHECK_NEAR(large.x, 2.0f * one.x, 2.0f);
    const ImVec2 ragged = ImTypeset::CalcTextSize(LOREM, nullptr, 300.0f, 0, &big);
    CHECK(ragged.x <= 300.0f && ragged.x > 200.0f);
    CHECK(ragged.y > 10 * 43.0f);
    // Text end, nothing, and no font.
    const ImVec2 cut = ImTypeset::CalcTextSize(LOREM, LOREM + 11);
    CHECK(cut.x > 40.0f && cut.x < 120.0f);
    CHECK_EQ(ImTypeset::CalcTextSize(nullptr).x, 0.0f);
    ImTypesetStyle unbacked = ImTypeset::GetStyle();
    unbacked.Face = ImTypesetFace_CodeBold;
    REQUIRE(ImTypeset::RemoveFont(ImTypesetFace_Bold));
    const ImVec2 none = ImTypeset::CalcTextSize(LOREM, nullptr, 300.0f, 0, &unbacked);
    CHECK_EQ(none.x, 0.0f);
    CHECK_EQ(none.y, 0.0f);
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_NoFont));
}

// --- Hostile input --------------------------------------------------------------------

TEST(no_length_a_host_passes_reaches_the_layout_core_unclamped)
{
    Stage stage;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const float big = std::numeric_limits<float>::max();
    const float values[] = {nan, inf, -inf, -1.0f, 0.0f, 1e-30f, 0.5f, 1e9f, 1e30f, big, -big};

    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    ImTypeset::SetParagraphText(p, "Some words to set, hostile lengths or not.");
    const auto sane = [&](const ImVec2 size) {
        CHECK(size.x == size.x && size.y == size.y);
        CHECK(size.x >= 0.0f && size.x <= 1.0e7f);
        CHECK(size.y >= 0.0f && size.y <= 1.0e7f);
        for (const DrawCmd& cmd : shipped_commands(*p)) {
            if (cmd.is(CmdType::LINE))
                CHECK(cmd.width >= 0 && cmd.width <= MAX_DIMEN);
        }
    };
    for (const float value : values) {
        CHECK_CONTEXT("value %g", static_cast<double>(value));
        sane(laid_out(p, value));
        ImTypesetStyle style;
        style.EmSize = 16.0f;
        style.LeftIndent = value;
        sane(laid_out(p, 300.0f, &style));
        style = ImTypesetStyle();
        style.EmSize = 16.0f;
        style.FirstLineIndent = value;
        sane(laid_out(p, 300.0f, &style));
        style = ImTypesetStyle();
        style.EmSize = 16.0f;
        style.LineHeight = value;
        sane(laid_out(p, 300.0f, &style));
        style = ImTypesetStyle();
        style.EmSize = 16.0f;
        style.PixelsPerInch = value;
        sane(laid_out(p, 300.0f, &style));
        style = ImTypesetStyle();
        style.EmSize = value;
        style.FontSize = value;
        sane(laid_out(p, 300.0f, &style));
        // The resolved style is in range whatever came in.
        const ResolvedStyle resolved = resolve_style(*stage.ctx, style);
        CHECK(resolved.em >= 1 && resolved.em <= MAX_EM);
        CHECK(resolved.baselineskip >= 1 && resolved.baselineskip <= MAX_DIMEN);
        style = ImTypesetStyle();
        style.EmSize = 16.0f;
        style.LeftIndent = value;
        style.FirstLineIndent = value;
        style.LineHeight = value;
        style.Face = static_cast<ImTypesetFace>(static_cast<int>(value == value ? 77 : -77));
        style.Align = 1234;
        style.Spacing = -9;
        style.LineBreak = ~0;
        sane(laid_out(p, value, &style));
    }

    // Runs that are out of range, out of order, overlapping, inside a
    // character; scales and faces that are not.
    const char* text = "na\xC3\xAFve caf\xC3\xA9 r\xC3\xA9sum\xC3\xA9";
    ImTypesetRun runs[6];
    runs[0].Start = -50;
    runs[0].End = 3; // ends inside the two-byte character
    runs[0].Scale = nan;
    runs[1].Start = 2; // overlaps
    runs[1].End = 5;
    runs[1].Face = 400;
    runs[2].Start = 9;
    runs[2].End = 7; // backwards
    runs[3].Start = 6;
    runs[3].End = 1 << 30;
    runs[3].Scale = 1e30f;
    runs[4].Start = 3; // out of order
    runs[4].End = 4;
    runs[5].Start = 1 << 30;
    runs[5].End = -1;
    runs[5].Scale = -inf;
    CHECK(!ImTypeset::SetParagraphText(p, text, nullptr, runs, 6));
    CHECK_EQ(ImTypeset::GetParagraphError(p)->Code,
             static_cast<int>(ImTypesetError_DescriptionCut));
    ImTypeset::GetStyle().EmSize = 16.0f;
    sane(laid_out(p, 300.0f));
    CHECK_EQ(copied_all(p), std::string(text));
    CHECK(!ImTypeset::SetParagraphText(p, text, nullptr, runs, -4) ||
          ImTypeset::GetParagraphError(p)->Code == ImTypesetError_None);

    // Text that is null, empty, backwards, all white space, or holds a NUL.
    CHECK(ImTypeset::SetParagraphText(p, nullptr));
    sane(laid_out(p, 300.0f));
    CHECK_EQ(ImTypeset::GetParagraphLineCount(p), 1);
    CHECK_EQ(copied_all(p), std::string());
    CHECK(ImTypeset::SetParagraphText(p, text + 4, text));
    sane(laid_out(p, 300.0f));
    CHECK(ImTypeset::SetParagraphText(p, " \t\n\r \xC2\xA0 "));
    sane(laid_out(p, 300.0f));
    CHECK_EQ(ImTypeset::GetParagraphLineCount(p), 1);
    const char with_nul[] = "a\0b";
    CHECK(ImTypeset::SetParagraphText(p, with_nul, with_nul + 3));
    sane(laid_out(p, 300.0f));
    CHECK_EQ(item_count(p) >= 2, true);

    // ALL ZERO IS THE DEFAULT, for the two structs a host fills: what zeroed
    // memory from C or a binding lays out is what the documented defaults do.
    {
        ImTypeset::SetParagraphText(p, LOREM);
        ImTypesetStyle zero;
        std::memset(static_cast<void*>(&zero), 0, sizeof zero);
        zero.EmSize = 16.0f;
        ImTypesetStyle spelled;
        spelled.Face = ImTypesetFace_Regular;
        spelled.EmSize = 16.0f;
        spelled.LineHeight = 1.35f;
        spelled.Align = ImTypesetAlign_Justify;
        spelled.Spacing = ImTypesetSpacing_Sentence;
        spelled.LineBreak = ImTypesetLineBreak_Auto;
        spelled.PixelsPerInch = 96.0f;
        const ImVec2 a = laid_out(p, 300.0f, &zero);
        const std::vector<DrawCmd> first = shipped_commands(*p);
        const ImVec2 b = laid_out(p, 300.0f, &spelled);
        CHECK_EQ(a.x, b.x);
        CHECK_EQ(a.y, b.y);
        CHECK(same_commands(first, shipped_commands(*p)));
        ImTypesetRun zeroed[1];
        std::memset(static_cast<void*>(zeroed), 0, sizeof zeroed);
        zeroed[0].Start = 5;
        zeroed[0].End = 50;
        CHECK(ImTypeset::SetParagraphText(p, LOREM, nullptr, zeroed, 1));
        laid_out(p, 300.0f, &spelled);
        CHECK(same_commands(first, shipped_commands(*p)));
    }

    // Drawing at hostile positions and through hostile clip rectangles.
    ImTypeset::SetParagraphText(p, LOREM);
    ImTypeset::LayoutParagraph(p, 300.0f);
    Frame frame;
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    for (const float value : values) {
        CHECK(ImTypeset::DrawParagraphToBuffers(
            p, ImVec2(value, value), ImVec4(value, -value, value, value), &vertices, &indices));
        CHECK(ImTypeset::DrawParagraph(p, ImGui::GetBackgroundDrawList(), ImVec2(value, -value)));
    }
    CHECK(!ImTypeset::DrawParagraphToBuffers(p, ImVec2(0.0f, 0.0f), ImVec4(0, 0, 1, 1), nullptr,
                                             &indices));
    CHECK(!ImTypeset::DrawParagraph(p, nullptr, ImVec2(0.0f, 0.0f)));
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_InvalidArgument));
    ImTypeset::DestroyParagraph(nullptr);
    ImTypeset::DestroyParagraph(p);
}

TEST(what_a_font_answers_is_clamped_on_the_way_into_the_layout_core)
{
    Stage stage(false);
    // Roboto with every advance in its hmtx set to 65535 units: 32 ems.
    std::vector<uint8_t> font;
    REQUIRE(golden::read_file(font_path("Roboto-Regular.ttf"), font));
    const auto be16 = [&](size_t at) { return (font[at] << 8) | font[at + 1]; };
    const auto be32 = [&](size_t at) {
        return (static_cast<uint32_t>(be16(at)) << 16) | static_cast<uint32_t>(be16(at + 2));
    };
    size_t hmtx = 0, hmtx_size = 0, metrics = 0;
    for (int i = 0; i < be16(4); ++i) {
        const size_t record = 12 + 16 * static_cast<size_t>(i);
        if (std::memcmp(&font[record], "hmtx", 4) == 0) {
            hmtx = be32(record + 8);
            hmtx_size = be32(record + 12);
        }
        if (std::memcmp(&font[record], "hhea", 4) == 0)
            metrics = static_cast<size_t>(be16(be32(record + 8) + 34));
    }
    REQUIRE(hmtx != 0 && metrics != 0 && metrics * 4 <= hmtx_size);
    for (size_t i = 0; i < metrics; ++i) {
        font[hmtx + 4 * i] = 0xFF;
        font[hmtx + 4 * i + 1] = 0xFF;
    }
    if (ImTypeset::AddFontFromMemoryTTF(ImTypesetFace_Regular, font.data(),
                                        static_cast<int>(font.size())) == nullptr) {
        // Refusing the font outright is as good an answer.
        std::printf("api: the font with 32 em advances was refused: %s\n",
                    ImTypeset::GetLastError()->Message);
        return;
    }
    // The largest em the entry lets through.
    ImTypesetStyle style;
    style.EmSize = 1.0e9f;
    style.PixelsPerInch = 24.0f;
    const ResolvedStyle resolved = resolve_style(*stage.ctx, style);
    CHECK_EQ(resolved.em, MAX_EM);
    // The font says 32 ems, which is twice MAX_DIMEN; the measure says MAX_DIMEN.
    const FontKey key = font_key(Face::PROSE, resolved.em);
    CHECK_EQ(stage.ctx->measure()(key, 'A').width, MAX_DIMEN);

    // A sentence space is the font's space and a third: past the bound again,
    // unless the material is clamped as it is built.
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    ImTypeset::SetParagraphText(p, "a. a. a");
    const ImVec2 size = laid_out(p, 300.0f, &style);
    CHECK(size.x >= 0.0f && size.y >= 0.0f);
    int glue = 0;
    for (const Node& node : p->block.run.nodes) {
        if (!node.is(NodeType::GLUE))
            continue;
        ++glue;
        CHECK(node.a <= MAX_DIMEN && node.b <= MAX_DIMEN && node.c <= MAX_DIMEN);
        CHECK_EQ(node.a, MAX_DIMEN);
    }
    CHECK_EQ(glue, 2);
}

TEST(a_run_in_a_face_nothing_was_added_for_is_set_in_the_paragraphs_and_reported)
{
    Stage stage(false);
    REQUIRE(ImTypeset::AddFontFromFileTTF(ImTypesetFace_Regular,
                                          font_path("Roboto-Regular.ttf").c_str()));
    ImTypesetRun unbacked;
    unbacked.Start = 6;
    unbacked.End = 11;
    unbacked.Face = ImTypesetFace_CodeBold;
    ImTypesetParagraph* q = ImTypeset::CreateParagraph();
    ImTypeset::SetParagraphText(q, "naive words", nullptr, &unbacked, 1);
    // Set, in the paragraph's face, and the call says so.
    CHECK(!ImTypeset::LayoutParagraph(q, 300.0f));
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_NoFont));
    CHECK_EQ(ImTypeset::GetParagraphStatus(q), static_cast<int>(ImTypesetStatusFlags_LaidOut));
    CHECK_EQ(ImTypeset::GetParagraphError(q)->Code, static_cast<int>(ImTypesetError_NoFont));
    CHECK_EQ(ImTypeset::GetParagraphError(q)->BytePos, 6);
    CHECK_EQ(ImTypeset::GetParagraphError(q)->CodepointPos, 6);
    CHECK_EQ(copied_all(q), std::string("naive words"));
    CHECK_EQ(ImTypeset::GetParagraphLineCount(q), 1);
}

TEST(text_without_a_font_falls_back_to_imgui_and_says_so_every_frame)
{
    Stage stage(false);
    for (int n = 0; n < 3; ++n) {
        CHECK_CONTEXT("frame %d", n);
        Frame frame;
        begin_window("fallback", 300.0f);
        // Something else succeeds in between: the widget's answer is its own.
        CHECK(ImTypeset::CreateParagraph() != nullptr);
        CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_None));
        const int had = ImGui::GetWindowDrawList()->VtxBuffer.Size;
        CHECK(!ImTypeset::TextUnformatted("No font was added, and the words still appear."));
        CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_NoFont));
        CHECK(std::strstr(ImTypeset::GetLastError()->Message, "ImTypesetFace_Regular") != nullptr);
        // ImGui's own text, wrapped: an item with a size and vertices.
        CHECK(ImGui::GetItemRectSize().y > 0.0f);
        if (n == 2)
            CHECK(ImGui::GetWindowDrawList()->VtxBuffer.Size > had);
        ImGui::End();
    }
    // The paragraph entry says the same of the same condition, and draws
    // nothing rather than pretend it drew.
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    CHECK(ImTypeset::SetParagraphText(p, "words"));
    CHECK(!ImTypeset::LayoutParagraph(p, 200.0f));
    CHECK_EQ(ImTypeset::GetParagraphSize(p).y, 0.0f);
    CHECK_EQ(ImTypeset::GetParagraphError(p)->Code, static_cast<int>(ImTypesetError_NoFont));
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_NoFont));
    CHECK_EQ(ImTypeset::GetParagraphStatus(p), static_cast<int>(ImTypesetStatusFlags_None));
    CHECK_EQ(ImTypeset::GetParagraphLineCount(p), 0);
    Frame frame;
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    CHECK(ImTypeset::CreateParagraph() != nullptr); // clears the last error
    CHECK(!ImTypeset::DrawParagraphToBuffers(p, ImVec2(0.0f, 0.0f), ImVec4(0, 0, 1e4f, 1e4f),
                                             &vertices, &indices));
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_NoFont));
    CHECK(!ImTypeset::DrawParagraph(p, ImGui::GetBackgroundDrawList(), ImVec2(0.0f, 0.0f)));
    CHECK_EQ(vertices.Size, 0);
}

// --- What the widgets report ------------------------------------------------------------

TEST(a_widget_answers_for_its_text_on_every_frame_it_is_submitted)
{
    Stage stage;
    // A formula outside the subset, a run of words too long for the limit is
    // tested in test_stress_text: here, what is cheap to make.
    const char* refused = "The matrix $\\begin{matrix} a \\end{matrix}$ is not set.";
    for (int n = 0; n < 4; ++n) {
        CHECK_CONTEXT("frame %d", n);
        Frame frame;
        begin_window("reports", 400.0f);
        CHECK(ImTypeset::TextUnformatted("Nothing wrong with this one."));
        CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_None));
        // Shown, as its source, and reported: on the frame it is laid out and
        // on every frame after.
        CHECK(!ImTypeset::TextUnformatted(refused, nullptr, ImTypesetTextFlags_Math));
        const ImTypesetErrorInfo* error = ImTypeset::GetLastError();
        CHECK_EQ(error->Code, static_cast<int>(ImTypesetError_MathRefused));
        CHECK_EQ(error->BytePos, static_cast<int>(std::strstr(refused, "\\begin") - refused));
        CHECK(ImGui::GetItemRectSize().y > 20.0f);
        // The next one's success is the next one's.
        CHECK(ImTypeset::TextUnformatted("Nor with this."));
        CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_None));
        // A formatted text really formats, and a per cent sign in a plain one
        // is a per cent sign.
        CHECK(ImTypeset::Text("%d items at %d%%", 3, 50));
        CHECK(ImTypeset::TextUnformatted("%d items at %d%%"));
        ImGui::End();
    }
    CHECK_EQ(ImTypeset::GetStats().Layouts, 5);
    bool formatted = false;
    bool verbatim = false;
    for (const auto& entry : stage.ctx->widgets) {
        formatted = formatted || entry.second->text == "3 items at 50%";
        verbatim = verbatim || entry.second->text == "%d items at %d%%";
    }
    CHECK(formatted);
    CHECK(verbatim);

    // Outside a frame a widget is refused, not dereferenced.
    CHECK(!ImTypeset::TextUnformatted("after Render"));
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_InvalidArgument));
    CHECK(std::strstr(ImTypeset::GetLastError()->Message, "frame") != nullptr);
}

// --- What is refused instead of crashing --------------------------------------------------

TEST(drawing_without_its_imgui_context_is_refused_with_a_reason)
{
    Stage stage;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    ImTypeset::SetParagraphText(p, LOREM);
    CHECK(ImTypeset::LayoutParagraph(p, 300.0f));
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    const ImVec4 clip(0.0f, 0.0f, 1000.0f, 1000.0f);
    ImGuiContext* own = ImGui::GetCurrentContext();

    // Another ImGui context, with an atlas of its own, is current.
    ImGuiContext* other = ImGui::CreateContext();
    ImGui::SetCurrentContext(other);
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().DisplaySize = ImVec2(800.0f, 600.0f);
    ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImGui::NewFrame();
    ImGui::Begin("other");
    const int had = ImGui::GetWindowDrawList()->VtxBuffer.Size;
    CHECK(!ImTypeset::DrawParagraphToBuffers(p, ImVec2(0.0f, 0.0f), clip, &vertices, &indices));
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_NoContext));
    CHECK(std::strstr(ImTypeset::GetLastError()->Message, "not the current one") != nullptr);
    CHECK(!ImTypeset::DrawParagraph(p, ImGui::GetWindowDrawList(), ImVec2(0.0f, 0.0f)));
    CHECK(!ImTypeset::TextUnformatted(LOREM));
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_NoContext));
    CHECK(!ImTypeset::SelectableText("id", LOREM));
    CHECK(ImTypeset::AddFontFromFileTTF(ImTypesetFace_Bold, font_path("Roboto-Bold.ttf").c_str()) ==
          nullptr);
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_NoContext));
    CHECK(!ImTypeset::RemoveFont(ImTypesetFace_Regular));
    CHECK_EQ(vertices.Size, 0);
    CHECK_EQ(ImGui::GetWindowDrawList()->VtxBuffer.Size, had);
    // Layout and the questions about it need no ImGui context of any kind.
    CHECK(ImTypeset::LayoutParagraph(p, 250.0f));
    CHECK(ImTypeset::GetParagraphLineCount(p) > 3);
    ImGui::End();
    ImGui::Render();
    ImGui::DestroyContext(other);

    // No ImGui context at all.
    ImGui::SetCurrentContext(nullptr);
    CHECK(!ImTypeset::DrawParagraphToBuffers(p, ImVec2(0.0f, 0.0f), clip, &vertices, &indices));
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_NoContext));
    CHECK(std::strstr(ImTypeset::GetLastError()->Message, "no current ImGui") != nullptr);
    CHECK(!ImTypeset::DrawParagraph(p, nullptr, ImVec2(0.0f, 0.0f)));
    CHECK(!ImTypeset::TextUnformatted(LOREM));
    CHECK(!ImTypeset::Text("%s", LOREM));
    CHECK(ImTypeset::LayoutParagraph(p, 300.0f));
    CHECK(ImTypeset::CalcTextSize(LOREM, nullptr, 300.0f).y > 100.0f);
    CHECK(ImTypeset::HitTestParagraph(p, ImVec2(40.0f, 40.0f)).TextOffset > 0);
    CHECK_EQ(copied_all(p), std::string(LOREM));
    CHECK_EQ(vertices.Size, 0);

    // Its own again: it draws.
    ImGui::SetCurrentContext(own);
    Frame frame;
    CHECK(ImTypeset::DrawParagraphToBuffers(p, ImVec2(0.0f, 0.0f), clip, &vertices, &indices));
    CHECK(vertices.Size > 0);
}

TEST(a_backend_without_texture_updates_is_refused_with_a_reason)
{
    // A renderer that has introduced itself and does not update textures: a
    // font is refused before the atlas is asked for it.
    {
        Stage stage(false);
        ImGui::GetIO().BackendFlags &= ~ImGuiBackendFlags_RendererHasTextures;
        ImGui::GetIO().BackendRendererName = "a renderer from before 1.92";
        CHECK(ImTypeset::AddFontFromFileTTF(ImTypesetFace_Regular,
                                            font_path("Roboto-Regular.ttf").c_str()) == nullptr);
        CHECK_EQ(ImTypeset::GetLastError()->Code,
                 static_cast<int>(ImTypesetError_BackendUnsupported));
        CHECK(std::strstr(ImTypeset::GetLastError()->Message,
                          "ImGuiBackendFlags_RendererHasTextures") != nullptr);
        CHECK_EQ(ImGui::GetIO().Fonts->Fonts.Size, 0);
        ImGui::GetIO().BackendRendererName = nullptr;
    }
#ifndef IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    // One that had not introduced itself when the fonts were added, and
    // builds its atlas the old way: found out at the first frame.
    ImGui::SetAllocatorFunctions(&counted_alloc, &counted_free, nullptr);
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(800.0f, 600.0f);
    io.DeltaTime = 1.0f / 60.0f;
    ImTypesetContext* ctx = ImTypeset::CreateContext();
    REQUIRE(ctx != nullptr);
    // A legacy atlas bakes a font at the size it was added with.
    ImFontConfig sized;
    sized.SizePixels = 16.0f;
    REQUIRE(ImTypeset::AddFontFromFileTTF(ImTypesetFace_Regular,
                                          font_path("Roboto-Regular.ttf").c_str(), 0, &sized));
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    ImTypeset::SetParagraphText(p, LOREM);
    CHECK(ImTypeset::LayoutParagraph(p, 300.0f)); // layout needs no backend
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    for (int n = 0; n < 2; ++n) {
        CHECK_CONTEXT("frame %d", n);
        ImGui::NewFrame();
        begin_window("legacy", 400.0f);
        ImDrawList* list = ImGui::GetWindowDrawList();
        const int had = list->VtxBuffer.Size;
        CHECK(!ImTypeset::TextUnformatted(LOREM));
        CHECK_EQ(ImTypeset::GetLastError()->Code,
                 static_cast<int>(ImTypesetError_BackendUnsupported));
        // Still the words, by ImGui's own text: an item, and vertices.
        CHECK(ImGui::GetItemRectSize().y > 30.0f);
        CHECK(list->VtxBuffer.Size > had);
        const int after_text = list->VtxBuffer.Size;
        CHECK(!ImTypeset::DrawParagraph(p, list, ImVec2(0.0f, 0.0f)));
        CHECK_EQ(ImTypeset::GetLastError()->Code,
                 static_cast<int>(ImTypesetError_BackendUnsupported));
        ImVector<ImDrawVert> vertices;
        ImVector<ImDrawIdx> indices;
        CHECK(!ImTypeset::DrawParagraphToBuffers(
            p, ImVec2(0.0f, 0.0f), ImVec4(0.0f, 0.0f, 1e4f, 1e4f), &vertices, &indices));
        CHECK_EQ(vertices.Size, 0);
        CHECK_EQ(list->VtxBuffer.Size, after_text);
        ImGui::End();
        ImGui::Render();
    }
    CHECK_EQ(ImTypeset::GetStats().Layouts, 1);
    ImTypeset::DestroyContext(ctx);
    ImGui::DestroyContext();
#else
    // The legacy atlas calls are ImGui's obsolete API, which this build of it
    // leaves out: a host cannot have such a backend here.
    std::printf("api: a backend that builds its atlas the old way is not tried: this Dear ImGui "
                "was built with IMGUI_DISABLE_OBSOLETE_FUNCTIONS\n");
#endif
}

TEST(a_font_the_host_removed_from_the_atlas_is_found_missing_not_dereferenced)
{
    Stage stage;
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    ImTypesetRun bold;
    bold.Start = 0;
    bold.End = 11;
    bold.Face = ImTypesetFace_Bold;
    ImTypeset::SetParagraphText(p, LOREM, nullptr, &bold, 1);
    const auto frame = [&](bool expect_ok) {
        Frame f;
        begin_window("fonts", 400.0f);
        CHECK_EQ(ImTypeset::TextUnformatted(LOREM), expect_ok);
        const bool laid = ImTypeset::LayoutParagraph(p, 300.0f);
        ImTypeset::DrawParagraph(p, ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos());
        ImGui::End();
        return laid;
    };
    CHECK(frame(true));
    CHECK(frame(true));

    // The host takes the bold font out of the atlas behind the library's back.
    ImFont* bold_font = ImTypeset::GetFont(ImTypesetFace_Bold);
    REQUIRE(bold_font != nullptr);
    atlas->RemoveFont(bold_font);
    // A paragraph laid out before that is not drawn from the freed font.
    {
        Frame f;
        ImVector<ImDrawVert> vertices;
        ImVector<ImDrawIdx> indices;
        CHECK(!ImTypeset::DrawParagraphToBuffers(
            p, ImVec2(0.0f, 0.0f), ImVec4(0.0f, 0.0f, 1e4f, 1e4f), &vertices, &indices));
        CHECK(std::strstr(ImTypeset::GetLastError()->Message, "LayoutParagraph") != nullptr);
        CHECK_EQ(vertices.Size, 0);
    }
    CHECK(ImTypeset::GetFont(ImTypesetFace_Bold) == nullptr);
    CHECK(ImTypeset::GetFont(ImTypesetFace_CodeBold) == nullptr);
    // Laid out again its bold run is in the paragraph's face, and it says so.
    CHECK(!frame(true));
    CHECK_EQ(ImTypeset::GetParagraphError(p)->Code, static_cast<int>(ImTypesetError_NoFont));
    CHECK(ImTypeset::GetParagraphLineCount(p) > 3);

    // The Regular font too: the widget falls back, every frame, and nothing
    // is read from what was freed. (ASan is what would say otherwise.)
    atlas->AddFontDefault();
    atlas->RemoveFont(ImTypeset::GetFont(ImTypesetFace_Regular));
    CHECK(!frame(false));
    CHECK_EQ(ImTypeset::GetParagraphError(p)->Code, static_cast<int>(ImTypesetError_NoFont));
    CHECK(!frame(false));
    CHECK(ImTypeset::GetFont(ImTypesetFace_Regular) == nullptr);

    // Added again, everything comes back.
    REQUIRE(ImTypeset::AddFontFromFileTTF(ImTypesetFace_Regular,
                                          font_path("Roboto-Regular.ttf").c_str()));
    REQUIRE(
        ImTypeset::AddFontFromFileTTF(ImTypesetFace_Bold, font_path("Roboto-Bold.ttf").c_str()));
    CHECK(frame(true));

    // RemoveFont takes the role and the atlas's font together.
    const int fonts = atlas->Fonts.Size;
    ImFont* italic = ImTypeset::GetFont(ImTypesetFace_Italic);
    REQUIRE(italic != nullptr);
    CHECK(ImTypeset::RemoveFont(ImTypesetFace_Italic));
    CHECK_EQ(atlas->Fonts.Size, fonts - 1);
    CHECK(!atlas->Fonts.contains(italic));
    CHECK(ImTypeset::GetFont(ImTypesetFace_Italic) == nullptr);
    CHECK(!ImTypeset::RemoveFont(ImTypesetFace_Italic));
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_NoFont));
    CHECK(!ImTypeset::RemoveFont(ImTypesetFace_Default));
    CHECK(!ImTypeset::RemoveFont(99));
    CHECK(frame(true));
    // Inside a frame of a backend that locks the atlas it would be refused;
    // with texture updates the atlas is never locked, and it is allowed.
}

// Between two frames a host can empty the atlas and fill it again with AS
// MANY fonts as it held: the count of fonts is what it was and so is the
// frame. The roles' fonts are gone all the same, and one of the host's new
// fonts may stand at the address one of them had. Nothing of the library's
// may then name a font: not GetFont(), not the font AddFont replaces, not the
// one RemoveFont() removes. The host's fonts must all survive.
TEST(an_atlas_refilled_with_as_many_fonts_between_two_frames_has_none_of_the_roles)
{
    Stage stage(false);
    REQUIRE(stage.ctx != nullptr);
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    const std::string path = font_path("Roboto-Regular.ttf");
    int stale = 0;
    int lost = 0;
    int reused = 0;
    for (int cycle = 0; cycle < 24; ++cycle) {
        CHECK_CONTEXT("cycle %d", cycle);
        atlas->Clear();
        atlas->AddFontDefault();
        atlas->AddFontDefault();
        ImFont* regular = ImTypeset::AddFontFromFileTTF(ImTypesetFace_Regular, path.c_str());
        REQUIRE(regular != nullptr);
        CHECK(ImTypeset::GetFont(ImTypesetFace_Regular) == regular);
        CHECK_EQ(atlas->Fonts.Size, 3);

        // The host starts over, with three fonts of its own.
        atlas->Clear();
        ImFont* host[3] = {atlas->AddFontDefault(), atlas->AddFontDefault(),
                           atlas->AddFontDefault()};
        for (ImFont* font : host)
            reused += font == regular ? 1 : 0;
        const auto survive = [&]() {
            int there = 0;
            for (ImFont* font : host)
                there += atlas->Fonts.contains(font) ? 1 : 0;
            return there;
        };
        switch (cycle % 3) {
        case 0:
            // Asked for, the role has no font.
            stale += ImTypeset::GetFont(ImTypesetFace_Regular) != nullptr ? 1 : 0;
            break;
        case 1: {
            // Added again, the role's font is a fourth font.
            ImFont* again = ImTypeset::AddFontFromFileTTF(ImTypesetFace_Regular, path.c_str());
            REQUIRE(again != nullptr);
            lost += 3 - survive();
            CHECK_EQ(atlas->Fonts.Size, 3 + survive() - 2);
            CHECK(ImTypeset::GetFont(ImTypesetFace_Regular) == again);
            break;
        }
        default:
            // Removed, there is nothing to remove.
            CHECK(!ImTypeset::RemoveFont(ImTypesetFace_Regular));
            CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_NoFont));
            lost += 3 - survive();
            break;
        }
    }
    CHECK_EQ(stale, 0);
    CHECK_EQ(lost, 0);
    std::printf("api: 24 refills of the atlas with as many fonts: %d of the host's new fonts at "
                "the address a role's font had; %d stale answers, %d of the host's fonts lost\n",
                reused, stale, lost);
}

// --- Widget semantics -----------------------------------------------------------------

TEST(the_item_is_the_text_block_and_takes_a_width_and_a_style_per_call)
{
    Stage stage;
    ImTypesetStyle left = ImTypeset::GetStyle();
    left.Align = ImTypesetAlign_Left;
    ImTypesetStyle centred = left;
    centred.Align = ImTypesetAlign_Center;
    const ImTypesetStyle context_style = ImTypeset::GetStyle();
    for (int n = 0; n < 2; ++n) {
        Frame frame;
        begin_window("items", 600.0f, 900.0f);
        const float available = ImGui::GetContentRegionAvail().x;

        // Three words, ragged right: as wide as the words.
        CHECK(ImTypeset::TextUnformatted("Three short words", nullptr, 0, 0.0f, &left));
        const ImVec2 words = ImGui::GetItemRectSize();
        CHECK(words.x > 80.0f && words.x < 160.0f);
        CHECK_EQ(words.x, ImTypeset::CalcTextSize("Three short words", nullptr, 0.0f).x);
        // So something fits beside it.
        ImGui::SameLine();
        const float beside = ImGui::GetCursorPosX();
        CHECK(beside < 200.0f);
        ImGui::NewLine();

        // The same words justified or centred are placed across the measure.
        CHECK(ImTypeset::TextUnformatted("Three short words"));
        CHECK_EQ(ImGui::GetItemRectSize().x, available);
        CHECK(ImTypeset::TextUnformatted("Three short words", nullptr, 0, 0.0f, &centred));
        CHECK_EQ(ImGui::GetItemRectSize().x, available);

        // A width per call, whatever is available: the paragraph wraps at it.
        CHECK(ImTypeset::TextUnformatted(LOREM, nullptr, 0, 250.0f));
        const ImVec2 narrow = ImGui::GetItemRectSize();
        CHECK_EQ(narrow.x, 250.0f);
        CHECK(ImTypeset::TextUnformatted(LOREM, nullptr, 0, 0.0f));
        const ImVec2 wide = ImGui::GetItemRectSize();
        CHECK_EQ(wide.x, available);
        CHECK(narrow.y > wide.y * 1.8f);
        // Ragged, a paragraph that fills its lines is nearly the width, and
        // never more.
        CHECK(ImTypeset::TextUnformatted(LOREM, nullptr, 0, 250.0f, &left));
        CHECK(ImGui::GetItemRectSize().x <= 250.0f);
        CHECK(ImGui::GetItemRectSize().x > 200.0f);
        // A width that is not a number is the width available.
        CHECK(
            ImTypeset::TextUnformatted(LOREM, nullptr, 0, std::numeric_limits<float>::quiet_NaN()));
        CHECK_EQ(ImGui::GetItemRectSize().x, available);
        ImGui::End();
    }
    // The context's style was never written to.
    CHECK(std::memcmp(&context_style, &ImTypeset::GetStyle(), sizeof context_style) == 0);
}

TEST(a_colour_a_host_gave_dims_with_imguis_alpha_like_the_default_one)
{
    Stage stage;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    ImTypesetRun red;
    red.Start = 0;
    red.End = 5;
    red.Color = IM_COL32(200, 0, 0, 255);
    ImTypeset::SetParagraphText(p, "alpha beta", nullptr, &red, 1);
    ImTypesetStyle style = ImTypeset::GetStyle();
    style.Color = IM_COL32(10, 20, 30, 255);
    style.SelectionColor = IM_COL32(1, 2, 3, 200);
    CHECK(ImTypeset::LayoutParagraph(p, 300.0f, &style));
    const ImTypesetTextRange all = {0, 10};
    const ImVec4 clip(-1e4f, -1e4f, 1e4f, 1e4f);
    const auto colours = [&](std::map<ImU32, int>& seen) {
        ImVector<ImDrawVert> vertices;
        ImVector<ImDrawIdx> indices;
        REQUIRE(ImTypeset::DrawParagraphToBuffers(p, ImVec2(0.0f, 0.0f), clip, &vertices, &indices,
                                                  &all));
        for (const ImDrawVert& v : vertices)
            ++seen[v.col];
    };
    Frame frame;
    std::map<ImU32, int> full;
    colours(full);
    const auto count = [](const std::map<ImU32, int>& seen, ImU32 colour) {
        const auto found = seen.find(colour);
        return found != seen.end() ? found->second : 0;
    };
    CHECK_EQ(count(full, IM_COL32(200, 0, 0, 255)), 5 * 4);
    CHECK_EQ(count(full, IM_COL32(10, 20, 30, 255)), 4 * 4);
    CHECK_EQ(count(full, IM_COL32(1, 2, 3, 200)), 4);
    CHECK_EQ(full.size(), size_t{3});
    // What BeginDisabled() does: style.Alpha down.
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    std::map<ImU32, int> dimmed;
    colours(dimmed);
    ImGui::PopStyleVar();
    // Half of 255 and of 200, as ImGui::GetColorU32 halves them.
    CHECK_EQ(count(dimmed, IM_COL32(200, 0, 0, 127)), 5 * 4);
    CHECK_EQ(count(dimmed, IM_COL32(10, 20, 30, 127)), 4 * 4);
    CHECK_EQ(count(dimmed, IM_COL32(1, 2, 3, 100)), 4);
    CHECK_EQ(dimmed.size(), size_t{3});
    // And it allocates nothing the second time.
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    ImTypeset::DrawParagraphToBuffers(p, ImVec2(0.0f, 0.0f), clip, &vertices, &indices, &all);
    vertices.resize(0);
    indices.resize(0);
    const size_t before = g_allocations;
    ImTypeset::DrawParagraphToBuffers(p, ImVec2(0.0f, 0.0f), clip, &vertices, &indices, &all);
    CHECK_EQ(g_allocations - before, size_t{0});
    ImGui::PopStyleVar();
}

// --- Selectable text --------------------------------------------------------------------

namespace
{

std::string g_clipboard;

void set_clipboard(ImGuiContext*, const char* text)
{
    g_clipboard = text != nullptr ? text : "";
}

} // namespace

TEST(selectable_text_selects_with_the_mouse_and_copies_to_the_clipboard)
{
    Stage stage;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::GetPlatformIO().Platform_SetClipboardTextFn = &set_clipboard;
    g_clipboard.clear();
    const char* text = "The quick brown fox jumps over the lazy dog, twice over and again.";
    const int brown = static_cast<int>(std::strstr(text, "brown") - text);
    ImTypesetTextRange selection = {0, 0};
    ImVec2 item_min;
    ImTypesetStyle style = ImTypeset::GetStyle();
    style.SelectionColor = IM_COL32(7, 8, 9, 255);
    int highlighted = 0;
    const auto frame = [&] {
        Frame f;
        begin_window("select", 220.0f, 300.0f);
        ImDrawList* list = ImGui::GetWindowDrawList();
        const int had = list->VtxBuffer.Size;
        CHECK(ImTypeset::SelectableText("##text", text, nullptr, 0, 0.0f, &style, &selection));
        item_min = ImGui::GetItemRectMin();
        highlighted = 0;
        for (int i = had; i < list->VtxBuffer.Size; ++i)
            highlighted += list->VtxBuffer[i].col == IM_COL32(7, 8, 9, 255) ? 1 : 0;
        ImGui::Button("elsewhere");
        ImGui::End();
    };
    io.AddMousePosEvent(-100.0f, -100.0f);
    frame();
    frame();
    CHECK_EQ(selection.Start, 0);
    CHECK_EQ(selection.End, 0);
    CHECK_EQ(highlighted, 0);

    // Where the word is, from the paragraph the widget keeps.
    REQUIRE_EQ(stage.ctx->widgets.size(), size_t{1});
    const ImTypesetParagraph* kept = stage.ctx->widgets.begin()->second;
    ImVector<ImVec4> rects;
    const ImTypesetTextRange word = {brown, brown + 5};
    REQUIRE_EQ(ImTypeset::GetParagraphRangeRects(kept, word, &rects), 1);
    const ImVec2 on_word(item_min.x + (rects[0].x + rects[0].z) * 0.5f,
                         item_min.y + (rects[0].y + rects[0].w) * 0.5f);
    REQUIRE(ImTypeset::GetParagraphLineCount(kept) >= 3);
    const ImTypesetLine third = ImTypeset::GetParagraphLine(kept, 2);
    const ImVec2 further(item_min.x + 60.0f, item_min.y + third.Baseline - 3.0f);

    // A press and a drag: by character, from the middle of the word on.
    io.AddMousePosEvent(on_word.x, on_word.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMousePosEvent(further.x, further.y);
    frame();
    frame();
    CHECK(selection.Start > brown && selection.Start < brown + 5);
    CHECK(selection.End > third.TextStart && selection.End <= third.TextEnd);
    CHECK(highlighted >= 3 * 4); // a rectangle a line, three lines
    const ImTypesetTextRange dragged = selection;
    io.AddMouseButtonEvent(0, false);
    frame();
    // Released, it stays.
    frame();
    CHECK_EQ(selection.Start, dragged.Start);
    CHECK_EQ(selection.End, dragged.End);

    // Ctrl+C puts those words on the clipboard, as they were written.
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_C, true);
    frame();
    io.AddKeyEvent(ImGuiKey_C, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    frame();
    CHECK_EQ(g_clipboard,
             std::string(text + dragged.Start, static_cast<size_t>(dragged.End - dragged.Start)));

    // A double click, some time later, selects the word under it.
    io.AddMousePosEvent(on_word.x, on_word.y);
    for (int n = 0; n < 60; ++n)
        frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    CHECK_EQ(selection.Start, brown);
    CHECK_EQ(selection.End, brown + 5);
    CHECK_EQ(highlighted, 4);
    // The selection is bytes: at another width it is the same word.
    // Ctrl+A is everything.
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_A, true);
    frame();
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiKey_C, true);
    frame();
    io.AddKeyEvent(ImGuiKey_C, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    frame();
    CHECK_EQ(selection.Start, 0);
    CHECK_EQ(selection.End, static_cast<int>(std::strlen(text)));
    CHECK_EQ(g_clipboard, std::string(text));

    // A click elsewhere clears it, and Ctrl+C then copies nothing of ours.
    g_clipboard = "untouched";
    io.AddMousePosEvent(on_word.x, item_min.y + 250.0f);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    CHECK_EQ(selection.Start, selection.End);
    CHECK_EQ(highlighted, 0);
    // Without an ID there is nothing for the mouse to take: the words are
    // set and the call says so.
    {
        Frame f;
        begin_window("select", 220.0f, 300.0f);
        CHECK(!ImTypeset::SelectableText("", text));
        CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_InvalidArgument));
        CHECK(ImGui::GetItemRectSize().y > 20.0f);
        CHECK(!ImTypeset::SelectableText(nullptr, text));
        ImGui::End();
    }
    // The selection is the host's: set from outside, it is what is shown.
    selection = word;
    frame();
    CHECK_EQ(highlighted, 4);
    CHECK_EQ(selection.Start, brown);
    selection = ImTypesetTextRange{0, 0};
    frame();
    CHECK_EQ(highlighted, 0);
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_C, true);
    frame();
    io.AddKeyEvent(ImGuiKey_C, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    frame();
    CHECK_EQ(g_clipboard, std::string("untouched"));
}

// --- The math seam --------------------------------------------------------------------

namespace
{

// A stand-in for the formula layer: `$...$` is a formula, set as its letters
// in one atom box, and refused if it holds a '!'.
bool fake_scan(void*, std::string_view text, size_t from, size_t* begin, size_t* end)
{
    const size_t open = text.find('$', from);
    if (open == std::string_view::npos)
        return false;
    const size_t close = text.find('$', open + 1);
    if (close == std::string_view::npos)
        return false;
    *begin = open;
    *end = close + 1;
    return true;
}

bool fake_build(void*, std::string_view source, FontSpec font, NodeArena& arena,
                CharMeasure measure, MetricsResolver, Node* atom, std::string* why,
                uint32_t* byte_pos, uint32_t* codepoint_pos)
{
    std::vector<uint32_t> cps;
    std::vector<uint32_t> offsets;
    decode_utf8(source, cps, &offsets);
    std::vector<Node> inside;
    for (size_t i = 1; i + 1 < cps.size(); ++i) {
        if (cps[i] == '!') {
            *why = "'!' is not in the subset";
            *byte_pos = offsets[i];
            *codepoint_pos = static_cast<uint32_t>(i);
            return false;
        }
        inside.push_back(char_node(font.key(), cps[i]));
    }
    const PackResult packed = hpack(arena, inside, 0, PackMode::ADDITIONAL, measure);
    *atom = atom_box(arena, arena.add_box(packed.box, inside));
    return true;
}

} // namespace

TEST(the_math_seam_takes_an_atom_copies_its_source_and_reports_a_refusal)
{
    Stage stage;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    const char* text = "Area is $ab$ here, and $cd$ there.";

    // Without the flag a dollar is a dollar.
    ImTypeset::SetParagraphText(p, text);
    ImTypeset::LayoutParagraph(p, 400.0f);
    int atoms = 0;
    for (const DrawCmd& cmd : shipped_commands(*p))
        atoms += cmd.is(CmdType::ATOM) ? 1 : 0;
    CHECK_EQ(atoms, 0);
    CHECK_EQ(copied_all(p), std::string(text));
    const int plain_length = item_count(p);

    // The flag is the whole switch: with nothing installed the built-in
    // typesetter sets both, in the math face the stage added.
    ImTypeset::SetParagraphText(p, text, nullptr, nullptr, 0, ImTypesetTextFlags_Math);
    CHECK(ImTypeset::LayoutParagraph(p, 400.0f));
    for (const DrawCmd& cmd : shipped_commands(*p))
        atoms += cmd.is(CmdType::ATOM) ? 1 : 0;
    CHECK_EQ(atoms, 2);
    CHECK_EQ(ImTypeset::GetParagraphFormulaCount(p), 2);

    InlineMath math;
    math.scan = &fake_scan;
    math.build = &fake_build;
    set_inline_math(*stage.ctx, math);
    ImTypeset::LayoutParagraph(p, 400.0f);
    CHECK_EQ(ImTypeset::GetParagraphError(p)->Code, static_cast<int>(ImTypesetError_None));
    atoms = 0;
    for (const DrawCmd& cmd : shipped_commands(*p))
        atoms += cmd.is(CmdType::ATOM) ? 1 : 0;
    CHECK_EQ(atoms, 2);
    // Each formula is one item where its source was four characters.
    CHECK_EQ(item_count(p), plain_length - 6);
    // And copies as its source, dollars included.
    CHECK_EQ(copied_all(p), std::string(text));
    // A range that holds its opening dollar holds the formula, whole; one
    // that starts inside it does not hold it at all.
    const int at = static_cast<int>(std::strstr(text, "$cd$") - text);
    CHECK_EQ(copied(p, at, at + 1), std::string("$cd$"));
    CHECK_EQ(copied(p, at, at + 4), std::string("$cd$"));
    CHECK_EQ(copied(p, at + 1, at + 4), std::string());
    CHECK_EQ(copied(p, at + 1, at + 10), std::string(" there"));
    // A click on it is at its start or its end, never inside.
    ImVector<ImVec4> box;
    const ImTypesetTextRange formula = {at, at + 4};
    REQUIRE_EQ(ImTypeset::GetParagraphRangeRects(p, formula, &box), 1);
    const float middle = (box[0].y + box[0].w) * 0.5f;
    CHECK_EQ(ImTypeset::HitTestParagraph(p, ImVec2(box[0].x + 1.0f, middle)).TextOffset, at);
    CHECK_EQ(ImTypeset::HitTestParagraph(p, ImVec2(box[0].z - 1.0f, middle)).TextOffset, at + 4);
    // And a caret asked for inside it stands after it.
    const ImVec4 caret = ImTypeset::GetParagraphCaretRect(p, at + 2);
    const ImVec4 after = ImTypeset::GetParagraphCaretRect(p, at + 4);
    CHECK_EQ(caret.x, after.x);
    CHECK_NEAR(caret.x, box[0].z, 0.01f);
    // Narrow enough to wrap, the formulas still copy whole.
    ImTypeset::LayoutParagraph(p, 90.0f);
    CHECK(ImTypeset::GetParagraphLineCount(p) > 2);
    CHECK_EQ(copied_all(p), std::string(text));

    // Without the flag a dollar is a dollar.
    ImTypeset::SetParagraphText(p, text);
    ImTypeset::LayoutParagraph(p, 400.0f);
    CHECK_EQ(item_count(p), plain_length);

    // A refusal says where, in bytes and in codepoints, and the source is set
    // as the text it is.
    const char* refused = "caf\xC3\xA9 $x!y$ and $ok$";
    ImTypeset::SetParagraphText(p, refused, nullptr, nullptr, 0, ImTypesetTextFlags_Math);
    CHECK(!ImTypeset::LayoutParagraph(p, 400.0f));
    const ImTypesetErrorInfo* error = ImTypeset::GetParagraphError(p);
    CHECK_EQ(error->Code, static_cast<int>(ImTypesetError_MathRefused));
    CHECK_EQ(error->BytePos, 8);      // c a f é(2) space $ x -> '!' is byte 8
    CHECK_EQ(error->CodepointPos, 7); // and codepoint 7
    CHECK(std::strstr(error->Message, "subset") != nullptr);
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_MathRefused));
    atoms = 0;
    for (const DrawCmd& cmd : shipped_commands(*p))
        atoms += cmd.is(CmdType::ATOM) ? 1 : 0;
    CHECK_EQ(atoms, 1);
    CHECK_EQ(copied_all(p), std::string(refused));

    // The layer removed, the built-in one reads the same text, to which a
    // factorial is no stranger: nothing is refused, and nothing is left over
    // from the layout that refused.
    set_inline_math(*stage.ctx, InlineMath());
    CHECK(ImTypeset::LayoutParagraph(p, 400.0f));
    CHECK_EQ(ImTypeset::GetParagraphError(p)->Code, static_cast<int>(ImTypesetError_None));
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_None));
    CHECK_EQ(ImTypeset::GetParagraphFormulaCount(p), 2);
}

// --- Forgetting -----------------------------------------------------------------------

TEST(a_paragraph_no_longer_submitted_is_forgotten)
{
    Stage stage;
    const auto frame = [&](bool submit) {
        Frame f;
        begin_window("forget", 300.0f);
        ImTypeset::TextUnformatted("always here");
        if (submit)
            ImTypeset::TextUnformatted("here for a while");
        ImGui::End();
    };
    frame(true);
    CHECK_EQ(ImTypeset::GetStats().CachedParagraphs, 2);
    for (int n = 0; n < 800; ++n)
        frame(false);
    CHECK_EQ(ImTypeset::GetStats().CachedParagraphs, 1);
    const ImU64 layouts = ImTypeset::GetStats().Layouts;
    frame(true);
    CHECK_EQ(ImTypeset::GetStats().CachedParagraphs, 2);
    // At most one layout: none at all when the paragraph that comes back out
    // of the pool is the one that held these words.
    CHECK(ImTypeset::GetStats().Layouts <= layouts + 1);
}
