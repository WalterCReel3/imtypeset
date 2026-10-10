// The demo window, run without a display: every section opened and left open
// for several frames, alone and all together, with and without fonts, across a
// change of context, and with a pointer dragged across it. Nothing is
// rasterised here (examples/demo_headless.cpp does that); what is checked is
// that the window runs, that it draws through the library, and that a frame in
// which nothing changed lays nothing out.
//
// Built with IMTYPESET_DISABLE_DEMO in the compiler flags of the whole build,
// the one test that remains checks that the symbol links and draws nothing.

#include "check.h"

#include "imgui.h"
#include "imtypeset.h"

#include <cfloat>
#include <string>

#ifndef IMTYPESET_TEST_DATA_DIR
#define IMTYPESET_TEST_DATA_DIR "tests/data"
#endif

namespace
{

const char* const SECTIONS[] = {
    "Paragraph", "Faces and sizes", "Math",         "Selection", "Inspector",
    "Stress",    "Fonts",           "Known limits", "Document",
};
const char ALL[] =
    "Paragraph,Faces and sizes,Math,Selection,Inspector,Stress,Fonts,Known limits,Document";

// A headless host: an ImGui context, optionally an ImTypeset context in it,
// optionally the six test fonts.
struct Host {
    explicit Host(bool with_library = true, bool with_fonts = true)
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(900.0f, 720.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        if (!with_library)
            return;
        if (!IMTYPESET_CHECKVERSION() || ImTypeset::CreateContext() == nullptr) {
            FAIL("no context: %s", ImTypeset::GetLastError()->Message);
            return;
        }
        library = true;
        io.Fonts->AddFontDefault();
        static const struct {
            ImTypesetFace face;
            const char* file;
        } fonts[] = {
            {ImTypesetFace_Regular, "Roboto-Regular.ttf"},
            {ImTypesetFace_Bold, "Roboto-Bold.ttf"},
            {ImTypesetFace_Italic, "Roboto-RegularItalic.ttf"},
            {ImTypesetFace_BoldItalic, "Roboto-BoldItalic.ttf"},
            {ImTypesetFace_Code, "Inconsolata-Medium.ttf"},
            {ImTypesetFace_Math, "latinmodern-math.otf"},
        };
        for (const auto& font : fonts) {
            if (!with_fonts)
                break;
            const std::string path = std::string(IMTYPESET_TEST_DATA_DIR "/fonts/") + font.file;
            if (ImTypeset::AddFontFromFileTTF(font.face, path.c_str()) == nullptr)
                FAIL("%s: %s", font.file, ImTypeset::GetLastError()->Message);
        }
    }
    ~Host()
    {
        if (library)
            ImTypeset::DestroyContext();
        ImGui::DestroyContext();
    }
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    // The sections to have open from the next frame on, by title, separated
    // by commas; the others are closed.
    void open(const char* sections)
    {
        wanted = std::string(",") + sections + ",";
        arrange = true;
    }

    // One frame of the demo window, filling the display.
    void frame()
    {
        ImGui::NewFrame();
        ImTypeset::ShowDemoWindow();
        if (arrange) {
            // From outside, through the window's name: a collapsing header
            // keeps whether it is open in its window's storage, under the ID
            // of its label.
            ImGui::Begin("ImTypeset Demo");
            for (const char* section : SECTIONS) {
                const bool is_open =
                    wanted.find("," + std::string(section) + ",") != std::string::npos;
                ImGui::GetStateStorage()->SetInt(ImGui::GetID(section), is_open ? 1 : 0);
            }
            ImGui::SetWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetWindowSize(size);
            ImGui::End();
            arrange = false;
        }
        ImGui::Render();
        ++frames;
    }
    void run(int count)
    {
        for (int i = 0; i < count; ++i)
            frame();
    }
    void resize(float width, float height)
    {
        size = ImVec2(width, height);
        arrange = true;
    }

    bool library = false;
    bool arrange = false;
    std::string wanted = ",";
    ImVec2 size = ImVec2(900.0f, 720.0f);
    int frames = 0;
};

int vertices_of_the_last_frame()
{
    const ImDrawData* data = ImGui::GetDrawData();
    return data != nullptr ? data->TotalVtxCount : 0;
}

} // namespace

#ifdef IMTYPESET_DISABLE_DEMO

TEST(with_the_demo_disabled_the_symbol_links_and_draws_nothing)
{
    Host host;
    host.run(4);
    // No window was begun, so there is nothing in the frame at all.
    CHECK_EQ(vertices_of_the_last_frame(), 0);
    CHECK_EQ(ImTypeset::GetStats().DrawCalls, ImU64(0));
    CHECK_EQ(ImTypeset::GetStats().Layouts, ImU64(0));
}

#else

TEST(each_section_alone_runs_for_several_frames_and_draws_through_the_library)
{
    Host host;
    REQUIRE(host.library);
    for (const char* section : SECTIONS) {
        CHECK_CONTEXT("section %s", section);
        host.open(section);
        host.run(4);
        const ImTypesetStats before = ImTypeset::GetStats();
        host.run(4);
        const ImTypesetStats after = ImTypeset::GetStats();
        CHECK(vertices_of_the_last_frame() > 0);
        // Every section sets some text with the library, every frame.
        CHECK(after.DrawCalls > before.DrawCalls);
        CHECK(after.VerticesEmitted > before.VerticesEmitted);
        // Open and left alone, it has nothing to lay out.
        CHECK_EQ(after.Layouts - before.Layouts, ImU64(0));
        CHECK_EQ(after.TextBuilds - before.TextBuilds, ImU64(0));
    }
}

TEST(all_sections_open_together_lay_nothing_out_once_they_have_settled)
{
    Host host;
    REQUIRE(host.library);
    host.resize(900.0f, 4000.0f); // Tall enough that every section is in view.
    ImGui::GetIO().DisplaySize = ImVec2(900.0f, 4000.0f);
    host.open(ALL);
    host.run(6);
    const ImTypesetStats settled = ImTypeset::GetStats();
    CHECK(settled.Layouts > 0);
    host.run(10);
    const ImTypesetStats after = ImTypeset::GetStats();
    CHECK_EQ(after.Layouts - settled.Layouts, ImU64(0));
    CHECK_EQ(after.TextBuilds - settled.TextBuilds, ImU64(0));
    CHECK_EQ(after.CacheEvictions, ImU64(0));
    CHECK(after.DrawCalls - settled.DrawCalls >= ImU64(10 * 30));
}

TEST(a_change_of_width_lays_out_what_follows_the_window_and_then_nothing)
{
    Host host;
    REQUIRE(host.library);
    host.open("Paragraph,Known limits");
    host.run(6);
    const ImTypesetStats before = ImTypeset::GetStats();
    // The host's window is made narrower: the paragraphs that fill it are set
    // again, once.
    host.resize(640.0f, 720.0f);
    host.run(4);
    const ImTypesetStats changed = ImTypeset::GetStats();
    CHECK(changed.Layouts > before.Layouts);
    host.run(6);
    CHECK_EQ(ImTypeset::GetStats().Layouts - changed.Layouts, ImU64(0));
}

// With one section open its header is at y = 77 + 23 * (its index) in a window
// at the origin, and its first control 23 px below that.
TEST(a_pointer_dragged_across_every_section_breaks_nothing_and_it_settles_again)
{
    Host host;
    REQUIRE(host.library);
    ImGuiIO& io = ImGui::GetIO();
    for (const char* section : SECTIONS) {
        CHECK_CONTEXT("section %s", section);
        host.open(section);
        host.run(3);
        // Down each of a few columns with the button held, releasing at the
        // bottom: sliders are dragged, text is selected, combos open.
        for (float x = 30.0f; x < 900.0f; x += 170.0f) {
            io.AddMousePosEvent(x, 60.0f);
            host.frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            for (float y = 60.0f; y < 720.0f; y += 9.0f) {
                io.AddMousePosEvent(x + y * 0.2f, y);
                host.frame();
            }
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            host.frame();
            // A popup a click may have opened is closed again.
            io.AddKeyEvent(ImGuiKey_Escape, true);
            host.frame();
            io.AddKeyEvent(ImGuiKey_Escape, false);
            host.frame();
        }
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        host.run(4);
        const ImU64 settled = ImTypeset::GetStats().Layouts;
        host.run(4);
        CHECK_EQ(ImTypeset::GetStats().Layouts - settled, ImU64(0));
    }
}

TEST(a_thousand_copies_of_the_document_cost_what_is_in_view)
{
    Host host;
    REQUIRE(host.library);
    ImGuiIO& io = ImGui::GetIO();
    host.open("Stress");
    host.run(4);
    // The slider of copies, dragged past its right end: x1000.
    io.AddMousePosEvent(80.0f, 215.0f);
    host.frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    host.frame();
    io.AddMousePosEvent(3000.0f, 215.0f);
    host.run(2);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    host.run(2);

    // The wheel over the document for 400 frames, 6,000 paragraphs long.
    io.AddMousePosEvent(400.0f, 420.0f);
    host.frame();
    ImU64 most_drawn = 0;
    ImU64 most_laid_out = 0;
    for (int i = 0; i < 400; ++i) {
        const ImTypesetStats before = ImTypeset::GetStats();
        io.AddMouseWheelEvent(0.0f, -25.0f);
        host.frame();
        const ImTypesetStats after = ImTypeset::GetStats();
        const ImU64 drawn =
            after.DrawCalls + after.CulledCalls - before.DrawCalls - before.CulledCalls;
        most_drawn = drawn > most_drawn ? drawn : most_drawn;
        const ImU64 laid_out = after.Layouts - before.Layouts;
        most_laid_out = laid_out > most_laid_out ? laid_out : most_laid_out;
    }
    const ImTypesetStats stats = ImTypeset::GetStats();
    // The scroll went a long way: far more paragraphs were laid out than a
    // hundred copies hold, which says the slider did reach past x100.
    CHECK(stats.Layouts > ImU64(1200));
    // A frame submits the copies in view (three at most at this height, six
    // paragraphs each) and lays out no more than it submits.
    CHECK(most_drawn <= ImU64(24));
    CHECK(most_laid_out <= ImU64(24));
    // What is held is bounded by the cache's limits, not by the document.
    CHECK(stats.CachedParagraphs <= ImU64(2048 + 64));
    CHECK(stats.CachedBytes <= ImU64(80) * 1024 * 1024);
    CHECK(stats.CacheEvictions > ImU64(0));

    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    host.run(4);
    const ImU64 settled = ImTypeset::GetStats().Layouts;
    host.run(4);
    CHECK_EQ(ImTypeset::GetStats().Layouts - settled, ImU64(0));
}

TEST(the_demo_outlives_the_context_it_was_first_shown_in)
{
    // The demo keeps paragraphs of its own between frames. Each of these
    // hosts has a new ImTypeset context, as likely as not at the address of
    // the one before: under the sanitisers a kept handle would be a use
    // after free.
    for (int round = 0; round < 4; ++round) {
        CHECK_CONTEXT("round %d", round);
        Host host;
        REQUIRE(host.library);
        host.open(ALL);
        host.run(6);
        CHECK(ImTypeset::GetStats().DrawCalls > 0);
        const ImU64 settled = ImTypeset::GetStats().Layouts;
        host.run(3);
        CHECK_EQ(ImTypeset::GetStats().Layouts - settled, ImU64(0));
    }
}

TEST(without_a_context_the_window_says_so_and_typesets_nothing)
{
    Host host(false);
    host.open(ALL);
    host.run(4);
    CHECK(vertices_of_the_last_frame() > 0); // The window and its message.
    CHECK(ImTypeset::GetCurrentContext() == nullptr);
}

TEST(without_fonts_every_section_still_runs)
{
    Host host(true, false);
    REQUIRE(host.library);
    host.open(ALL);
    host.run(8);
    CHECK(vertices_of_the_last_frame() > 0);
    // Nothing could be typeset: the widgets drew with ImGui's own text.
    CHECK_EQ(ImTypeset::GetStats().VerticesEmitted, ImU64(0));
}

#endif // IMTYPESET_DISABLE_DEMO
