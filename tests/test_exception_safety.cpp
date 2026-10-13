// What is left behind when something throws through the library.
//
// The library neither throws nor catches, and is built without exceptions
// everywhere but here. A host can still unwind through it: Dear ImGui built for
// a scripting host defines IM_ASSERT to throw (imgui_bundle's Python build
// does), and with exceptions enabled a failed allocation is std::bad_alloc.
// The claim tested is the basic guarantee, at the public entry points:
// whatever was thrown, and wherever, nothing leaks, the draw list holds whole
// quads and no vertex that was reserved and not written, the widget cache
// counts what it holds, and the same call made again does what it would have
// done had nothing happened.
//
// Only in a build configured with
//
//     -DIMTYPESET_ENABLE_EXCEPTIONS=ON
//     -DIMTYPESET_IMGUI_USER_CONFIG=<repository>/tests/imconfig_throwing.h
//
// and meant to be run under ASan, which is what sees a leak. In any other
// build every test here says that it was skipped and why.
//
// How a throw is made to happen anywhere: every form of operator new is
// replaced, and can be told to throw std::bad_alloc on its n-th call from now.
// A call is then repeated with n = 0, 1, 2, ... until it completes without
// reaching n, so that it has been unwound from every allocation it makes.
// ImGui's own assertion is met where ImGui raises it: an atlas too small for
// what is drawn.

#include "golden.h"

#include "imtypeset.h"

#include "context.h"
#include "imgui_internal.h" // the atlas builder, to see what a throw left in it

#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#if defined(IMTYPESET_TEST_IM_ASSERT_THROWS) && defined(__cpp_exceptions)
#define TEST_UNWINDING 1
#else
#define TEST_UNWINDING 0
#endif

#if TEST_UNWINDING

// --- An operator new that can be told to fail --------------------------------------

#if defined(__GNUC__)
#define TEST_NOINLINE __attribute__((noinline))
#else
#define TEST_NOINLINE
#endif

namespace
{
// How many more allocations succeed before one throws; negative: all of them.
long g_allocations_left = -1;
long g_allocations_seen = 0;

struct FailAfter {
    explicit FailAfter(long n)
    {
        g_allocations_left = n;
        g_allocations_seen = 0;
    }
    ~FailAfter() { g_allocations_left = -1; }
    FailAfter(const FailAfter&) = delete;
    FailAfter& operator=(const FailAfter&) = delete;
};

bool must_fail()
{
    ++g_allocations_seen;
    if (g_allocations_left < 0)
        return false;
    if (g_allocations_left == 0) {
        g_allocations_left = -1; // once: what unwinds may allocate
        return true;
    }
    --g_allocations_left;
    return false;
}
} // namespace

TEST_NOINLINE void* operator new(size_t size)
{
    if (must_fail())
        throw std::bad_alloc();
    void* const memory = std::malloc(size != 0 ? size : 1);
    if (memory == nullptr)
        throw std::bad_alloc();
    return memory;
}
TEST_NOINLINE void* operator new(size_t size, const std::nothrow_t&) noexcept
{
    // The form that reports by returning null is left alone: std::stable_sort
    // asks it for a buffer and does without.
    return std::malloc(size != 0 ? size : 1);
}
TEST_NOINLINE void* operator new[](size_t size)
{
    return operator new(size);
}
TEST_NOINLINE void* operator new[](size_t size, const std::nothrow_t& tag) noexcept
{
    return operator new(size, tag);
}
TEST_NOINLINE void operator delete(void* memory) noexcept
{
    std::free(memory);
}
TEST_NOINLINE void operator delete(void* memory, size_t) noexcept
{
    std::free(memory);
}
TEST_NOINLINE void operator delete(void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}
TEST_NOINLINE void operator delete[](void* memory) noexcept
{
    std::free(memory);
}
TEST_NOINLINE void operator delete[](void* memory, size_t) noexcept
{
    std::free(memory);
}
TEST_NOINLINE void operator delete[](void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}

using namespace ImTypeset::detail;

namespace
{

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

struct Stage {
    explicit Stage(bool with_fonts = true)
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(800.0f, 600.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
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
        ImTypeset::GetStyle().EmSize = 16.0f;
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

const char* const TEXT =
    "The radius $r = \\frac{a}{2}$ and a root $\\sqrt{x^2 + 1}$, set in a paragraph long "
    "enough to be broken into several lines at the width it is given, with a formula that is "
    "refused, $\\hat{x}$, and a character no face has, \xE4\xB8\xAD, in it.";

ImTypesetRun bold_run()
{
    ImTypesetRun run;
    run.Start = 4;
    run.End = 10;
    run.Face = ImTypesetFace_Bold;
    run.Scale = 1.2f;
    return run;
}

// Everything about a list that ImGui's renderers rely on: whole quads, six
// indices to four vertices, and commands that account for every index.
void check_list_is_whole(const ImDrawList& list)
{
    CHECK_EQ(list.VtxBuffer.Size % 4, 0);
    CHECK_EQ(list.IdxBuffer.Size % 6, 0);
    unsigned int elements = 0;
    for (const ImDrawCmd& cmd : list.CmdBuffer)
        elements += cmd.ElemCount;
    CHECK_EQ(elements, static_cast<unsigned int>(list.IdxBuffer.Size));
    // Where ImGui will write next is the end of what is there. (A list
    // nothing has been written to since NewFrame has no such place yet.)
    if (list.VtxBuffer.Size > 0) {
        CHECK(list._VtxWritePtr == list.VtxBuffer.Data + list.VtxBuffer.Size);
        CHECK(list._IdxWritePtr == list.IdxBuffer.Data + list.IdxBuffer.Size);
    }
}

// The widget cache's three views of itself agree.
void check_cache_is_consistent(const ImTypesetContext& ctx)
{
    size_t listed = 0;
    size_t bytes = 0;
    const ImTypesetParagraph* before = nullptr;
    for (const ImTypesetParagraph* p = ctx.oldest; p != nullptr; p = p->newer) {
        CHECK(p->older == before);
        CHECK(ctx.widgets.count(p->key) == 1);
        bytes += p->footprint;
        before = p;
        ++listed;
    }
    CHECK(ctx.newest == before);
    CHECK_EQ(listed, ctx.widgets.size());
    CHECK_EQ(static_cast<size_t>(ctx.stats.CachedParagraphs), listed);
    CHECK_EQ(static_cast<size_t>(ctx.stats.CachedBytes), bytes);
}

std::vector<ImDrawVert> vertices_from(const ImDrawList& list, int first)
{
    return std::vector<ImDrawVert>(list.VtxBuffer.Data + first,
                                   list.VtxBuffer.Data + list.VtxBuffer.Size);
}

bool same_vertices(const std::vector<ImDrawVert>& a, const std::vector<ImDrawVert>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].pos.x != b[i].pos.x || a[i].pos.y != b[i].pos.y || a[i].col != b[i].col)
            return false;
    }
    return true;
}

// The most allocations any one call under test is expected to make. A sweep
// that reaches it without the call completing has gone wrong.
constexpr long SWEEP_LIMIT = 200000;

} // namespace

// --- Layout --------------------------------------------------------------------------

TEST(a_throw_from_anywhere_in_setting_and_laying_out_a_paragraph_leaves_it_usable)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const ImTypesetRun run = bold_run();
    const ImTypesetTextFlags flags = ImTypesetTextFlags_Math;

    // What it comes to when nothing is thrown.
    ImTypesetParagraph* reference = ImTypeset::CreateParagraph();
    REQUIRE(reference != nullptr);
    ImTypeset::SetParagraphText(reference, TEXT, nullptr, &run, 1, flags);
    ImTypeset::LayoutParagraph(reference, 300.0f, nullptr);
    const std::vector<DrawCmd> expected = shipped_commands(*reference);
    const ImVec2 expected_size = ImTypeset::GetParagraphSize(reference);
    const std::string expected_error = ImTypeset::GetParagraphError(reference)->Message;
    REQUIRE(expected.size() > 100);
    REQUIRE_EQ(ImTypeset::GetParagraphFormulaCount(reference), 3);

    const auto check_as_expected = [&](const ImTypesetParagraph* p) {
        const std::vector<DrawCmd>& cmds = shipped_commands(*p);
        REQUIRE_EQ(cmds.size(), expected.size());
        size_t different = 0;
        for (size_t i = 0; i < cmds.size(); ++i)
            different += cmds[i] != expected[i] ? 1u : 0u;
        CHECK_EQ(different, size_t(0));
        CHECK_EQ(ImTypeset::GetParagraphSize(p).x, expected_size.x);
        CHECK_EQ(ImTypeset::GetParagraphSize(p).y, expected_size.y);
        CHECK_EQ(std::string(ImTypeset::GetParagraphError(p)->Message), expected_error);
        CHECK_EQ(ImTypeset::GetParagraphFormulaCount(p), 3);
    };

    long thrown = 0;
    long n = 0;
    for (; n < SWEEP_LIMIT; ++n) {
        CHECK_CONTEXT("the allocation that throws is number %ld", n);
        ImTypesetParagraph* p = ImTypeset::CreateParagraph();
        REQUIRE(p != nullptr);
        bool completed = false;
        bool set = false;
        try {
            const FailAfter armed(n);
            ImTypeset::SetParagraphText(p, TEXT, nullptr, &run, 1, flags);
            set = true;
            ImTypeset::LayoutParagraph(p, 300.0f, nullptr);
            completed = true;
        } catch (const std::bad_alloc&) {
            ++thrown;
        }
        // The same calls again. Where the text had been taken, laying out
        // alone must do: a host does not know which of its calls was cut short.
        if (!set)
            ImTypeset::SetParagraphText(p, TEXT, nullptr, &run, 1, flags);
        ImTypeset::LayoutParagraph(p, 300.0f, nullptr);
        check_as_expected(p);
        // And another paragraph, which shares the context's scratch and its
        // memo of formulas' fonts, is not disturbed by what was left there.
        if (n % 16 == 0) {
            ImTypeset::LayoutParagraph(reference, 250.0f, nullptr);
            ImTypeset::LayoutParagraph(reference, 300.0f, nullptr);
            check_as_expected(reference);
        }
        ImTypeset::DestroyParagraph(p);
        if (completed)
            break;
    }
    std::printf("    unwound from %ld allocations of SetParagraphText + LayoutParagraph\n", thrown);
    CHECK(n < SWEEP_LIMIT);
    CHECK(thrown > 50);
    ImTypeset::DestroyParagraph(reference);
}

// --- Draw ----------------------------------------------------------------------------

TEST(a_throw_from_anywhere_in_a_draw_leaves_the_draw_list_whole)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    REQUIRE(p != nullptr);
    ImTypeset::SetParagraphText(p, TEXT, nullptr, nullptr, 0, ImTypesetTextFlags_Math);
    ImTypeset::LayoutParagraph(p, 300.0f, nullptr);
    const ImTypesetTextRange selection = {10, 120};

    std::vector<ImDrawVert> expected;
    {
        ImGui::NewFrame();
        ImDrawList* list = ImGui::GetBackgroundDrawList();
        REQUIRE(ImTypeset::DrawParagraph(p, list, ImVec2(8.0f, 8.0f), &selection));
        expected = vertices_from(*list, 0);
        ImGui::Render();
    }
    REQUIRE(expected.size() > 400);

    long thrown = 0;
    long n = 0;
    for (; n < SWEEP_LIMIT; ++n) {
        CHECK_CONTEXT("the allocation that throws is number %ld", n);
        // A context of its own each time, so that every draw is a first draw:
        // scratch not yet grown, selection model not yet built.
        ImTypesetParagraph* fresh = ImTypeset::CreateParagraph();
        REQUIRE(fresh != nullptr);
        ImTypeset::SetParagraphText(fresh, TEXT, nullptr, nullptr, 0, ImTypesetTextFlags_Math);
        ImTypeset::LayoutParagraph(fresh, 300.0f, nullptr);
        ImGui::NewFrame();
        ImDrawList* list = ImGui::GetBackgroundDrawList();
        bool completed = false;
        try {
            const FailAfter armed(n);
            ImTypeset::DrawParagraph(fresh, list, ImVec2(8.0f, 8.0f), &selection);
            completed = true;
        } catch (const std::bad_alloc&) {
            ++thrown;
        }
        check_list_is_whole(*list);
        const int before = list->VtxBuffer.Size;
        if (!completed)
            CHECK_EQ(before, 0);
        // Drawn again in the same frame, it is all there.
        CHECK(ImTypeset::DrawParagraph(fresh, list, ImVec2(8.0f, 8.0f), &selection));
        check_list_is_whole(*list);
        CHECK(same_vertices(vertices_from(*list, before), expected));
        ImGui::Render();
        ImTypeset::DestroyParagraph(fresh);
        if (completed)
            break;
    }
    std::printf("    unwound from %ld allocations of DrawParagraph\n", thrown);
    CHECK(n < SWEEP_LIMIT);
    ImTypeset::DestroyParagraph(p);
}

TEST(an_assertion_thrown_by_imgui_under_a_draw_leaves_the_draw_list_whole)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    // An atlas that cannot hold one size of this text: ImGui asserts that it
    // is out of texture memory, from under the glyph load of a draw.
    atlas->TexMaxWidth = 512;
    atlas->TexMaxHeight = 128;
    std::string text;
    for (char c = '!'; c <= '~'; ++c)
        text += c;
    ImTypesetStyle style = ImTypeset::GetStyle();
    style.EmSize = 60.0f;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    REQUIRE(p != nullptr);
    ImTypeset::SetParagraphText(p, text.c_str());
    ImTypeset::LayoutParagraph(p, 700.0f, &style);

    int thrown = 0;
    for (int frame = 0; frame < 4; ++frame) {
        CHECK_CONTEXT("frame %d", frame);
        ImGui::NewFrame();
        ImDrawList* list = ImGui::GetBackgroundDrawList();
        try {
            ImTypeset::DrawParagraph(p, list, ImVec2(4.0f, 4.0f), nullptr);
        } catch (const imtypeset_test::AssertionFailure& failure) {
            ++thrown;
            if (frame == 0)
                std::printf("    thrown from under DrawParagraph: IM_ASSERT(%s)\n",
                            failure.expression);
        }
        // Nothing was reserved before the glyphs were loaded, so nothing of
        // the paragraph is in the list, and nothing half-written is.
        check_list_is_whole(*list);
        if (thrown == frame + 1)
            CHECK_EQ(list->VtxBuffer.Size, 0);
        ImGui::Render();
    }
    CHECK_EQ(thrown, 4);

    // The library is as it was: a text the atlas has room for is laid out
    // and drawn, in the same context.
    ImTypesetParagraph* small = ImTypeset::CreateParagraph();
    REQUIRE(small != nullptr);
    style.EmSize = 12.0f;
    ImTypeset::SetParagraphText(small, "il");
    REQUIRE(ImTypeset::LayoutParagraph(small, 700.0f, &style));
    CHECK(ImTypeset::GetParagraphSize(small).y > 0.0f);
    CHECK(ImTypeset::GetLastError()->Code == ImTypesetError_None);
    ImTypeset::DestroyParagraph(small);
    ImTypeset::DestroyParagraph(p);
}

// --- The widget entry ----------------------------------------------------------------

TEST(an_assertion_thrown_by_imgui_under_a_text_widget_leaves_the_frame_and_the_cache_usable)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    atlas->TexMaxWidth = 512;
    atlas->TexMaxHeight = 128;
    std::string text;
    for (char c = '!'; c <= '~'; ++c)
        text += c == '$' || c == '\\' ? 'x' : c;
    ImTypesetStyle large = ImTypeset::GetStyle();
    large.EmSize = 60.0f;
    int thrown = 0;
    for (int frame = 0; frame < 4; ++frame) {
        CHECK_CONTEXT("frame %d", frame);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(780.0f, 580.0f));
        ImGui::Begin("asserting", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        try {
            ImTypeset::TextUnformatted(text.c_str(), nullptr, 0, 0.0f, &large);
        } catch (const imtypeset_test::AssertionFailure&) {
            ++thrown;
        }
        check_list_is_whole(*ImGui::GetWindowDrawList());
        check_cache_is_consistent(*stage.ctx);
        // The paragraph was laid out before the draw that threw: the item is
        // there, with its size, and is not laid out again next frame.
        CHECK_EQ(ImTypeset::GetStats().Layouts, 1);
        CHECK_EQ(ImTypeset::GetStats().CachedParagraphs, 1);
        ImGui::End();
        ImGui::Render();
    }
    CHECK_EQ(thrown, 4);
}

TEST(a_throw_from_anywhere_in_a_text_widget_leaves_the_cache_counting_what_it_holds)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const auto frame = [&](long fail_at, const char* text, bool* completed) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(420.0f, 500.0f));
        ImGui::Begin("widgets", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        std::vector<ImDrawVert> drawn;
        const int before = ImGui::GetWindowDrawList()->VtxBuffer.Size;
        try {
            const FailAfter armed(fail_at);
            ImTypeset::TextUnformatted(text, nullptr, ImTypesetTextFlags_Math);
            *completed = true;
        } catch (const std::bad_alloc&) {
            *completed = false;
        }
        check_list_is_whole(*ImGui::GetWindowDrawList());
        check_cache_is_consistent(*stage.ctx);
        if (*completed)
            drawn = vertices_from(*ImGui::GetWindowDrawList(), before);
        ImGui::End();
        ImGui::Render();
        return drawn;
    };

    bool completed = false;
    const std::vector<ImDrawVert> expected = frame(-1, TEXT, &completed);
    REQUIRE(completed);
    REQUIRE(expected.size() > 400);

    long thrown = 0;
    long n = 0;
    // A text of its own each time, so that each call is a first call: a new
    // slot in the cache, a new paragraph, a first layout.
    for (; n < SWEEP_LIMIT; ++n) {
        CHECK_CONTEXT("the allocation that throws is number %ld", n);
        const std::string text = std::string(TEXT) + " " + std::to_string(n);
        (void)frame(n, text.c_str(), &completed);
        const bool finished = completed;
        if (!finished)
            ++thrown;
        // The frame after: the same call, whole. And the first text, which
        // was in the cache throughout, is drawn as it always was.
        const std::vector<ImDrawVert> again = frame(-1, text.c_str(), &completed);
        CHECK(completed);
        CHECK(again.size() > 400);
        if (n % 8 == 0)
            CHECK(same_vertices(frame(-1, TEXT, &completed), expected));
        if (finished)
            break;
    }
    std::printf("    unwound from %ld allocations of TextUnformatted\n", thrown);
    CHECK(n < SWEEP_LIMIT);
    CHECK(thrown > 50);
    check_cache_is_consistent(*stage.ctx);
}

// --- Font registration ---------------------------------------------------------------

TEST(a_throw_from_anywhere_in_adding_a_font_leaves_the_role_empty_or_filled)
{
    // What the text is set as once every font is in.
    std::vector<DrawCmd> expected;
    {
        Stage stage;
        REQUIRE(stage.ctx != nullptr);
        ImTypesetParagraph* p = ImTypeset::CreateParagraph();
        ImTypeset::SetParagraphText(p, TEXT, nullptr, nullptr, 0, ImTypesetTextFlags_Math);
        ImTypeset::LayoutParagraph(p, 300.0f, nullptr);
        expected = shipped_commands(*p);
        ImTypeset::DestroyParagraph(p);
    }
    REQUIRE(expected.size() > 100);

    long thrown = 0;
    long n = 0;
    // Every allocation of adding the math face, which is the one that does
    // the most: its tables are read and its codepoints minted.
    for (; n < SWEEP_LIMIT; n += n < 48 ? 1 : 1 + n / 12) {
        CHECK_CONTEXT("the allocation that throws is number %ld", n);
        Stage stage(false);
        REQUIRE(stage.ctx != nullptr);
        for (const auto& entry : DEFAULT_FONTS) {
            if (entry.face != ImTypesetFace_Math)
                REQUIRE(ImTypeset::AddFontFromFileTTF(entry.face, font_path(entry.file).c_str()) !=
                        nullptr);
        }
        const std::string math = font_path("latinmodern-math.otf");
        const int fonts_before = ImGui::GetIO().Fonts->Fonts.Size;
        bool completed = false;
        try {
            const FailAfter armed(n);
            ImTypeset::AddFontFromFileTTF(ImTypesetFace_Math, math.c_str());
            completed = true;
        } catch (const std::bad_alloc&) {
            ++thrown;
        }
        if (!completed) {
            // Not half there: no font for the role, none left in the atlas
            // that nothing names, and the registry does not have the face.
            CHECK(ImTypeset::GetFont(ImTypesetFace_Math) == nullptr);
            CHECK_EQ(ImGui::GetIO().Fonts->Fonts.Size, fonts_before);
            CHECK(!stage.ctx->library.is_registered(Face::MATH));
            REQUIRE(ImTypeset::AddFontFromFileTTF(ImTypesetFace_Math, math.c_str()) != nullptr);
        }
        CHECK(ImTypeset::GetFont(ImTypesetFace_Math) != nullptr);
        ImTypesetParagraph* p = ImTypeset::CreateParagraph();
        REQUIRE(p != nullptr);
        ImTypeset::SetParagraphText(p, TEXT, nullptr, nullptr, 0, ImTypesetTextFlags_Math);
        ImTypeset::LayoutParagraph(p, 300.0f, nullptr);
        const std::vector<DrawCmd>& cmds = shipped_commands(*p);
        REQUIRE_EQ(cmds.size(), expected.size());
        size_t different = 0;
        for (size_t i = 0; i < cmds.size(); ++i)
            different += cmds[i] != expected[i] ? 1u : 0u;
        CHECK_EQ(different, size_t(0));
        ImTypeset::DestroyParagraph(p);
        if (completed)
            break;
    }
    std::printf("    unwound from %ld allocations of AddFontFromFileTTF\n", thrown);
    CHECK(n < SWEEP_LIMIT);
    CHECK(thrown > 20);
}

TEST(a_throw_from_creating_a_context_or_a_paragraph_leaves_nothing_behind)
{
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    long thrown = 0;
    for (long n = 0; n < SWEEP_LIMIT; ++n) {
        CHECK_CONTEXT("the allocation that throws is number %ld", n);
        bool completed = false;
        try {
            const FailAfter armed(n);
            ImTypeset::CreateContext();
            completed = true;
        } catch (const std::bad_alloc&) {
            ++thrown;
        }
        if (!completed) {
            CHECK(ImTypeset::GetCurrentContext() == nullptr);
            continue;
        }
        REQUIRE(ImTypeset::GetCurrentContext() != nullptr);
        break;
    }
    for (long n = 0; n < SWEEP_LIMIT; ++n) {
        CHECK_CONTEXT("the allocation that throws is number %ld", n);
        ImTypesetParagraph* p = nullptr;
        bool completed = false;
        const size_t before = ImTypeset::GetCurrentContext()->paragraphs.size();
        try {
            const FailAfter armed(n);
            p = ImTypeset::CreateParagraph();
            completed = true;
        } catch (const std::bad_alloc&) {
            ++thrown;
        }
        if (!completed) {
            size_t held = 0;
            for (const ImTypesetParagraph* each : ImTypeset::GetCurrentContext()->paragraphs)
                held += each != nullptr ? 1u : 0u;
            CHECK_EQ(held, size_t(0));
            (void)before;
            continue;
        }
        REQUIRE(p != nullptr);
        ImTypeset::DestroyParagraph(p);
        break;
    }
    std::printf("    unwound from %ld allocations of CreateContext and CreateParagraph\n", thrown);
    CHECK(thrown >= 2);
    ImTypeset::DestroyContext();
    ImGui::DestroyContext();
}

// --- Documents ---------------------------------------------------------------------------

namespace
{

// A document of `count` paragraphs of TEXT and one code block whose line is
// far longer than any column here.
ImTypesetDocument* a_document(int count)
{
    std::string text;
    std::vector<ImTypesetBlock> blocks;
    for (int i = 0; i <= count; ++i) {
        ImTypesetBlock block;
        block.Kind = i < count ? ImTypesetBlockKind_Paragraph : ImTypesetBlockKind_Code;
        block.Flags = i < count ? ImTypesetBlockFlags_Math : 0;
        block.TextStart = static_cast<int>(text.size());
        text += i < count ? std::string(TEXT) : std::string(400, 'w');
        block.TextEnd = static_cast<int>(text.size());
        blocks.push_back(block);
    }
    ImTypesetDocumentContent content;
    content.Text = text.data();
    content.TextSize = static_cast<int>(text.size());
    content.Blocks = blocks.data();
    content.BlockCount = static_cast<int>(blocks.size());
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    if (document != nullptr)
        ImTypeset::SetDocumentBlocks(document, &content);
    return document;
}

} // namespace

// DocumentView() opens a child window. Unwound from anywhere inside it, the
// child is ended: the host's window is the current one again and its own End()
// is the one that matches.
TEST(a_throw_from_anywhere_in_a_document_view_leaves_the_hosts_window_current)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    long thrown = 0;
    long n = 0;
    for (; n < SWEEP_LIMIT; ++n) {
        CHECK_CONTEXT("the allocation that throws is number %ld", n);
        ImTypesetDocument* document = a_document(6);
        REQUIRE(document != nullptr);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(500.0f, 400.0f));
        ImGui::Begin("host", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        ImGuiContext& g = *ImGui::GetCurrentContext();
        const ImGuiWindow* const host = g.CurrentWindow;
        const int depth = g.CurrentWindowStack.Size;
        const ImTypesetDocumentRange all = {{0, 0}, {6, 400}};
        ImTypesetDocumentRange selection = all;
        bool completed = false;
        try {
            const FailAfter armed(n);
            ImTypeset::DocumentView("doc", document, ImVec2(0.0f, 300.0f), &selection);
            completed = true;
        } catch (const std::bad_alloc&) {
            ++thrown;
        }
        const bool ended = CHECK_EQ(g.CurrentWindowStack.Size, depth);
        CHECK(g.CurrentWindow == host);
        // So that the frame can be ended whatever was found.
        while (g.CurrentWindowStack.Size > depth)
            ImGui::EndChild();
        check_list_is_whole(*ImGui::GetWindowDrawList());
        // Called again in the same frame, it is a view: blocks are laid out
        // and drawn (below the first call's, where that got as far as taking
        // its place in the child), and what it answers is what it answers
        // when nothing was thrown (false: TEXT holds a formula that is refused).
        if (ended && !completed) {
            const ImU64 drawn = ImTypeset::GetDocumentStats(document).BlocksDrawn;
            CHECK(!ImTypeset::DocumentView("doc", document, ImVec2(0.0f, 300.0f), &selection));
            CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_MathRefused));
            CHECK(ImTypeset::GetDocumentStats(document).BlocksDrawn > drawn);
        }
        CHECK_EQ(g.CurrentWindowStack.Size, depth);
        ImGui::End();
        ImGui::Render();
        ImTypeset::DestroyDocument(document);
        if (completed)
            break;
    }
    std::printf("    unwound from %ld allocations of DocumentView\n", thrown);
    CHECK(thrown >= 10);
    CHECK(n < SWEEP_LIMIT);
}

// A code block whose lines are clipped is drawn inside a clip rectangle of its
// own. Unwound from the draw, the draw list has the clip rectangles it had.
TEST(a_throw_from_anywhere_in_drawing_a_clipped_code_block_leaves_the_clip_stack_as_it_was)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    long thrown = 0;
    long n = 0;
    for (; n < SWEEP_LIMIT; ++n) {
        CHECK_CONTEXT("the allocation that throws is number %ld", n);
        ImTypesetDocument* document = a_document(0);
        REQUIRE(document != nullptr);
        ImTypesetDocumentOptions options;
        options.CodeOverflow = ImTypesetCodeOverflow_Clip;
        ImTypeset::SetDocumentOptions(document, &options);
        ImTypeset::LayoutDocument(document, 200.0f);
        ImGui::NewFrame();
        ImDrawList* list = ImGui::GetBackgroundDrawList();
        const int clips = list->_ClipRectStack.Size;
        bool completed = false;
        try {
            const FailAfter armed(n);
            ImTypeset::DrawDocument(document, list, ImVec2(8.0f, 8.0f));
            completed = true;
        } catch (const std::bad_alloc&) {
            ++thrown;
        }
        CHECK_EQ(list->_ClipRectStack.Size, clips);
        while (list->_ClipRectStack.Size > clips)
            list->PopClipRect();
        check_list_is_whole(*list);
        const int before = list->VtxBuffer.Size;
        CHECK(ImTypeset::DrawDocument(document, list, ImVec2(8.0f, 8.0f)));
        CHECK(list->VtxBuffer.Size > before);
        CHECK_EQ(list->_ClipRectStack.Size, clips);
        ImGui::Render();
        ImTypeset::DestroyDocument(document);
        if (completed)
            break;
    }
    std::printf("    unwound from %ld allocations of DrawDocument\n", thrown);
    CHECK(thrown >= 5);
    CHECK(n < SWEEP_LIMIT);
}

namespace
{

// Two paragraphs round a table of `rows` rows of three cells under a header,
// one cell in three long enough to be broken at the widths used here.
struct TableContent {
    std::string text;
    std::vector<ImTypesetBlock> blocks;
    std::vector<ImTypesetCell> cells;

    explicit TableContent(int rows)
    {
        const char* const prose = "A paragraph of plain words, long enough to be broken into "
                                  "several lines at the width the document is given here.";
        paragraph(prose);
        ImTypesetBlock table;
        table.Kind = ImTypesetBlockKind_Table;
        table.Flags = ImTypesetBlockFlags_TableHeader;
        table.TextStart = static_cast<int>(text.size());
        const size_t base = text.size();
        for (int r = 0; r < rows; ++r) {
            if (r != 0)
                text += '\n';
            for (int c = 0; c < 3; ++c) {
                if (c != 0)
                    text += '\t';
                ImTypesetCell cell;
                cell.TextStart = static_cast<int>(text.size() - base);
                text += "r" + std::to_string(r) + "c" + std::to_string(c);
                if (r % 3 == 2 && c == 2)
                    text += " with several more words in it";
                cell.TextEnd = static_cast<int>(text.size() - base);
                cell.Flags = c == 0 ? ImTypesetCellFlags_RowStart : 0;
                cells.push_back(cell);
            }
        }
        table.TextEnd = static_cast<int>(text.size());
        table.CellStart = 0;
        table.CellCount = static_cast<int>(cells.size());
        blocks.push_back(table);
        paragraph(prose);
    }

    void paragraph(const char* body)
    {
        ImTypesetBlock block;
        block.Kind = ImTypesetBlockKind_Paragraph;
        block.TextStart = static_cast<int>(text.size());
        text += body;
        block.TextEnd = static_cast<int>(text.size());
        blocks.push_back(block);
    }

    ImTypesetDocumentContent content() const
    {
        ImTypesetDocumentContent described;
        described.Text = text.data();
        described.TextSize = static_cast<int>(text.size());
        described.Blocks = blocks.data();
        described.BlockCount = static_cast<int>(blocks.size());
        described.Cells = cells.data();
        described.CellCount = static_cast<int>(cells.size());
        return described;
    }
};

// Describes the document, lays it out, cuts it into columns and draws it:
// every call a table and a cut are reached through. False if any refuses.
bool describe_cut_and_draw(ImTypesetDocument* document, const TableContent& table, bool balance,
                           ImDrawList* list)
{
    const ImTypesetDocumentContent content = table.content();
    ImTypesetColumns columns;
    columns.Gutter = 10.0f;
    if (balance) {
        columns.Count = 3;
        columns.Flags = ImTypesetColumnFlags_Balance;
    } else {
        columns.Height = 120.0f;
    }
    return ImTypeset::SetDocumentBlocks(document, &content) &&
           ImTypeset::LayoutDocument(document, 260.0f) &&
           ImTypeset::SetDocumentColumns(document, &columns) &&
           ImTypeset::GetDocumentColumnCount(document) >= 2 &&
           ImTypeset::DrawDocument(document, list, ImVec2(8.0f, 8.0f));
}

} // namespace

// A table is laid out whole and a document in columns is cut when it is asked
// about. Unwound from anywhere in describing, laying out, cutting or drawing
// one, the document is one the same calls can be made on again, and they then
// draw what a document nothing was thrown through draws.
TEST(a_throw_from_anywhere_in_a_table_cut_into_columns_leaves_the_document_usable)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const TableContent table(8);
    for (const bool balance : {false, true}) {
        CHECK_CONTEXT("balanced %d", balance ? 1 : 0);
        std::vector<ImDrawVert> expected;
        {
            ImTypesetDocument* document = ImTypeset::CreateDocument();
            REQUIRE(document != nullptr);
            ImGui::NewFrame();
            ImDrawList* list = ImGui::GetBackgroundDrawList();
            const int first = list->VtxBuffer.Size;
            REQUIRE(describe_cut_and_draw(document, table, balance, list));
            expected = vertices_from(*list, first);
            ImGui::Render();
            ImTypeset::DestroyDocument(document);
        }
        REQUIRE(!expected.empty());
        long thrown = 0;
        long n = 0;
        for (; n < SWEEP_LIMIT; ++n) {
            CHECK_CONTEXT("the allocation that throws is number %ld", n);
            ImTypesetDocument* document = ImTypeset::CreateDocument();
            REQUIRE(document != nullptr);
            ImGui::NewFrame();
            ImDrawList* list = ImGui::GetBackgroundDrawList();
            const int clips = list->_ClipRectStack.Size;
            bool completed = false;
            try {
                const FailAfter armed(n);
                // Not unwound from: the sweep is over, whatever was answered.
                CHECK(describe_cut_and_draw(document, table, balance, list));
                completed = true;
            } catch (const std::bad_alloc&) {
                ++thrown;
            }
            CHECK_EQ(list->_ClipRectStack.Size, clips);
            while (list->_ClipRectStack.Size > clips)
                list->PopClipRect();
            check_list_is_whole(*list);
            // Whatever the document holds after the throw, it is a document:
            // it can be asked about and drawn as it stands, before anything
            // is described again.
            if (!completed) {
                ImTypeset::LayoutDocument(document, 260.0f);
                ImTypeset::GetDocumentColumnCount(document);
                ImTypeset::HitTestDocument(document, ImVec2(300.0f, 60.0f));
                ImTypeset::DrawDocument(document, list, ImVec2(8.0f, 8.0f));
                check_list_is_whole(*list);
            }
            const int first = list->VtxBuffer.Size;
            CHECK(describe_cut_and_draw(document, table, balance, list));
            CHECK(same_vertices(vertices_from(*list, first), expected));
            ImGui::Render();
            ImTypeset::DestroyDocument(document);
            if (completed)
                break;
        }
        std::printf("    unwound from %ld allocations of a table in columns (%s)\n", thrown,
                    balance ? "balanced" : "in order");
        CHECK(thrown >= 20);
        CHECK(n < SWEEP_LIMIT);
    }
}

#else // !TEST_UNWINDING

TEST(skipped_outside_a_build_whose_im_assert_throws)
{
    std::printf("    SKIPPED: this build's IM_ASSERT does not throw, or it has no exceptions. "
                "Configure with -DIMTYPESET_ENABLE_EXCEPTIONS=ON "
                "-DIMTYPESET_IMGUI_USER_CONFIG=<repository>/tests/imconfig_throwing.h\n");
}

#endif
