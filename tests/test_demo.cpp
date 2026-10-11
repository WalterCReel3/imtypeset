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

#include "context.h" // The handles a context holds, to be counted.
#include "imgui.h"
#include "imgui_internal.h" // GetHoveredID(): which control a scripted pointer is on.
#include "imtypeset.h"

#include <cfloat>
#include <cstdio>
#include <functional>
#include <string>

#ifndef IMTYPESET_TEST_DATA_DIR
#define IMTYPESET_TEST_DATA_DIR "tests/data"
#endif

namespace
{

const char* const SECTIONS[] = {
    "Paragraph", "Faces and sizes", "Specimen", "Math",         "Formula gallery", "Selection",
    "Inspector", "Stress",          "Fonts",    "Known limits", "Document",
};
const char ALL[] = "Paragraph,Faces and sizes,Specimen,Math,Formula gallery,Selection,Inspector,"
                   "Stress,Fonts,Known limits,Document";

// A headless host: an ImGui context, optionally an ImTypeset context in it,
// optionally the six test fonts. Several can be alive at once: each makes its
// own contexts current before it does anything.
struct Host {
    explicit Host(bool with_library = true, bool with_fonts = true)
    {
        imgui = ImGui::CreateContext();
        ImGui::SetCurrentContext(imgui);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(900.0f, 720.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.Fonts->AddFontDefault();
        ImTypeset::SetCurrentContext(nullptr);
        if (with_library)
            create_library(with_fonts);
    }
    ~Host()
    {
        destroy_library();
        ImGui::DestroyContext(imgui);
    }
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    void create_library(bool with_fonts = true)
    {
        ImGui::SetCurrentContext(imgui);
        if (IMTYPESET_CHECKVERSION())
            typeset = ImTypeset::CreateContext();
        if (typeset == nullptr) {
            FAIL("no context: %s", ImTypeset::GetLastError()->Message);
            return;
        }
        ImTypeset::SetCurrentContext(typeset);
        library = true;
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
    void destroy_library()
    {
        if (typeset != nullptr)
            ImTypeset::DestroyContext(typeset);
        if (ImTypeset::GetCurrentContext() == typeset)
            ImTypeset::SetCurrentContext(nullptr);
        typeset = nullptr;
        library = false;
    }
    void activate() const
    {
        ImGui::SetCurrentContext(imgui);
        ImTypeset::SetCurrentContext(typeset);
    }

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
        activate();
        ImGui::NewFrame();
        if (show)
            ImTypeset::ShowDemoWindow(closable ? &show : nullptr);
        // Sections shown the other way, in a window of the host's.
        if (alone_first < alone_end) {
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(size);
            ImGui::Begin("Pages of the host's");
            for (int section = alone_first; section < alone_end; ++section)
                ImTypeset::ShowDemoSection(section);
            ImGui::End();
        }
        if (show && arrange) {
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
        if (show && collapse != 0) {
            ImGui::SetWindowCollapsed("ImTypeset Demo", collapse > 0);
            collapse = 0;
        }
        if (in_demo_window) {
            ImGui::Begin("ImTypeset Demo");
            in_demo_window();
            ImGui::End();
            in_demo_window = nullptr;
        }
        // The host's own typesetting beside the demo: a text never set before
        // on each line.
        if (own_lines > 0) {
            ImGui::Begin("Host");
            for (int i = 0; i < own_lines; ++i) {
                ImGui::PushID(i);
                ImTypeset::Text("The host's own paragraph number %d.", own_serial++);
                ImGui::PopID();
            }
            ImGui::End();
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

    ImGuiContext* imgui = nullptr;
    ImTypesetContext* typeset = nullptr;
    bool library = false;
    bool arrange = false;
    // Run once, in the next frame, inside the demo window after the demo: a
    // question about the window asked without a frame the demo is not in.
    std::function<void()> in_demo_window;
    bool show = true;      // Whether the host calls ShowDemoWindow() at all.
    bool closable = false; // Whether it hands `show` over as p_open.
    int collapse = 0;      // +1 collapses the demo window in the next frame, -1 expands it.
    int own_lines = 0;     // Paragraphs of the host's own, new ones every frame.
    // The sections [alone_first, alone_end) are shown by ShowDemoSection().
    int alone_first = 0;
    int alone_end = 0;
    int own_serial = 0;
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
    // Tall, so that several sections are in view at once; not so tall that
    // one window holds more vertices than a 16-bit index reaches, which
    // ImGui asserts on under a backend without ImGuiBackendFlags_RendererHasVtxOffset.
    host.resize(900.0f, 4000.0f);
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

// Down each of a few columns of the window with the button held, releasing
// at the bottom: sliders are dragged, text is selected, combos open. Whatever
// is under the pointer is acted on; nothing here depends on where a control is.
void drag_across_the_window(Host& host)
{
    ImGuiIO& io = ImGui::GetIO();
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
}

TEST(a_pointer_dragged_across_every_section_breaks_nothing_and_it_settles_again)
{
    Host host;
    REQUIRE(host.library);
    for (const char* section : SECTIONS) {
        CHECK_CONTEXT("section %s", section);
        host.open(section);
        host.run(3);
        drag_across_the_window(host);
        host.run(4);
        const ImU64 settled = ImTypeset::GetStats().Layouts;
        host.run(4);
        CHECK_EQ(ImTypeset::GetStats().Layouts - settled, ImU64(0));
    }
}

// How far the demo window scrolls with every section open: a number that
// moves with the font size, the widths and most of what the controls set.
float extent_of_the_demo(Host& host)
{
    host.open(ALL);
    host.run(6);
    float extent = 0.0f;
    host.in_demo_window = [&extent]() { extent = ImGui::GetScrollMaxY(); };
    host.frame();
    return extent;
}

TEST(what_was_done_to_the_demo_in_one_context_is_not_there_in_the_next)
{
    float untouched = 0.0f;
    {
        Host host;
        REQUIRE(host.library);
        untouched = extent_of_the_demo(host);
        CHECK(untouched > 0.0f);
    }
    {
        Host host;
        REQUIRE(host.library);
        for (const char* section : SECTIONS) {
            host.open(section);
            host.run(3);
            drag_across_the_window(host);
        }
        // The drags did change it: the font size among other things.
        CHECK_NE(extent_of_the_demo(host), untouched);
    }
    Host host;
    REQUIRE(host.library);
    CHECK_EQ(extent_of_the_demo(host), untouched);
}

TEST(a_thousand_copies_of_the_document_cost_what_is_in_view)
{
    Host host;
    REQUIRE(host.library);
    ImGuiIO& io = ImGui::GetIO();
    host.open("Stress");
    host.run(4);
    // The slider of copies, dragged past its right end: x1000. Where it is
    // depends on everything above it in the window, so that the pointer is on
    // it is checked before the button goes down.
    io.AddMousePosEvent(80.0f, 261.0f);
    host.frame();
    ImGuiID copies = 0;
    host.in_demo_window = [&copies]() { copies = ImGui::GetID("Copies of the document"); };
    host.frame();
    REQUIRE_EQ(ImGui::GetHoveredID(), copies);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    host.frame();
    io.AddMousePosEvent(3000.0f, 261.0f);
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

// What the demo holds in a context: the paragraph objects and documents the
// host (here, the demo) created in it and has not destroyed.
size_t handles_of(const Host& host)
{
    return host.typeset->paragraphs.size() + host.typeset->documents.size();
}

// With every section open the demo keeps 25 paragraphs (Faces and sizes 1,
// Specimen 1, Math 1 and the 3 of its stream, the Formula gallery's 17 panels,
// Selection 1, Inspector 1) and two documents (Math, Document).
const size_t DEMO_HANDLES = 27;
// With no font added the Specimen section has nothing to measure and makes no
// paragraph.
const size_t DEMO_HANDLES_WITHOUT_FONTS = 26;

TEST(a_context_made_at_the_address_of_a_dead_one_gets_handles_of_its_own)
{
    Host host;
    REQUIRE(host.library);
    host.open(ALL);
    host.run(6);
    CHECK_EQ(handles_of(host), DEMO_HANDLES);
    const ImTypesetContext* first = host.typeset;
    const ImU64 built = ImTypeset::GetStats().TextBuilds;

    // The window is collapsed, and while it is the host replaces its context
    // and typesets more in the new one than the demo ever did in the old.
    host.collapse = +1;
    host.run(3);
    host.destroy_library();
    host.create_library();
    REQUIRE(host.library);
    std::printf("    the second context is at %s address\n",
                host.typeset == first ? "the first one's" : "another");
    host.own_lines = 40;
    while (ImTypeset::GetStats().TextBuilds <= built && host.frames < 1000)
        host.frame();
    CHECK(ImTypeset::GetStats().TextBuilds > built);
    host.own_lines = 0;
    CHECK_EQ(handles_of(host), size_t(0));

    // Expanded again, the demo draws with handles of the context it is in.
    host.collapse = -1;
    host.run(4);
    CHECK_EQ(handles_of(host), DEMO_HANDLES);
    const ImU64 settled = ImTypeset::GetStats().Layouts;
    host.run(3);
    CHECK_EQ(ImTypeset::GetStats().Layouts - settled, ImU64(0));
}

TEST(two_live_contexts_showing_the_demo_each_keep_one_set_of_handles)
{
    Host one;
    Host two;
    REQUIRE(one.library && two.library);
    one.open(ALL);
    two.open(ALL);
    for (int i = 0; i < 6; ++i) {
        one.frame();
        two.frame();
    }
    CHECK_EQ(handles_of(one), DEMO_HANDLES);
    CHECK_EQ(handles_of(two), DEMO_HANDLES);
    one.activate();
    const ImU64 settled_one = ImTypeset::GetStats().Layouts;
    two.activate();
    const ImU64 settled_two = ImTypeset::GetStats().Layouts;
    for (int i = 0; i < 20; ++i) {
        one.frame();
        two.frame();
    }
    // Nothing was made again and nothing was laid out again.
    CHECK_EQ(handles_of(one), DEMO_HANDLES);
    CHECK_EQ(handles_of(two), DEMO_HANDLES);
    one.activate();
    CHECK_EQ(ImTypeset::GetStats().Layouts - settled_one, ImU64(0));
    two.activate();
    CHECK_EQ(ImTypeset::GetStats().Layouts - settled_two, ImU64(0));
}

TEST(a_context_replaced_between_two_frames_that_show_the_demo_gets_handles_of_its_own)
{
    // No frame without the demo, nothing typeset in between: nothing for the
    // demo to notice but that the context is another one. Whether the new one
    // is at the old one's address is the allocator's choice; without fonts,
    // and without the sanitisers' quarantine, it mostly is, and the test says
    // how often it was.
    Host host(true, false);
    REQUIRE(host.library);
    host.open(ALL);
    host.run(6);
    CHECK_EQ(handles_of(host), DEMO_HANDLES_WITHOUT_FONTS);
    int same_address = 0;
    const int rounds = 12;
    for (int round = 0; round < rounds; ++round) {
        CHECK_CONTEXT("round %d", round);
        const ImTypesetContext* before = host.typeset;
        host.destroy_library();
        host.create_library(false);
        REQUIRE(host.library);
        same_address += host.typeset == before ? 1 : 0;
        CHECK_EQ(handles_of(host), size_t(0));
        host.run(3);
        CHECK_EQ(handles_of(host), DEMO_HANDLES_WITHOUT_FONTS);
    }
    std::printf("    %d of %d contexts were at the address of the one before\n", same_address,
                rounds);
}

TEST(what_the_demo_holds_is_kept_while_the_window_is_collapsed_closed_or_not_shown)
{
    Host host;
    REQUIRE(host.library);
    const float untouched = extent_of_the_demo(host);
    drag_across_the_window(host);
    const float changed = extent_of_the_demo(host);
    CHECK_NE(changed, untouched);
    CHECK_EQ(handles_of(host), DEMO_HANDLES);
    const ImU64 built = ImTypeset::GetStats().TextBuilds;

    host.collapse = +1;
    host.run(3);
    CHECK_EQ(handles_of(host), DEMO_HANDLES);
    host.collapse = -1;
    host.run(4);

    // The host stops calling for a while, as one does that shows the window
    // from a menu.
    host.show = false;
    host.run(3);
    host.show = true;
    host.run(4);

    // The close button: the frame in which ImGui clears *p_open is the last
    // the host calls in.
    host.closable = true;
    float right = 0.0f;
    host.in_demo_window = [&right]() {
        right = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x;
    };
    host.frame();
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(right - 12.0f, 10.0f);
    host.frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    host.frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    host.run(2);
    CHECK(!host.show);
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    host.show = true;
    host.run(4);

    // The same handles, with the text they were given, and the controls
    // where the drags left them.
    CHECK_EQ(handles_of(host), DEMO_HANDLES);
    CHECK_EQ(ImTypeset::GetStats().TextBuilds - built, ImU64(0));
    CHECK_EQ(extent_of_the_demo(host), changed);
}

// The demo keeps a state for each of the eight contexts it was last shown in.
// One that comes back after eight others is a new one to it: defaults, and
// handles made again beside those it made before, which stay the context's.
TEST(a_context_shown_again_after_eight_others_starts_over_and_after_seven_does_not)
{
    Host host;
    REQUIRE(host.library);
    host.open(ALL);
    host.run(6);
    CHECK_EQ(handles_of(host), DEMO_HANDLES);
    for (int others = 7; others <= 8; ++others) {
        CHECK_CONTEXT("%d other contexts", others);
        for (int i = 0; i < others; ++i) {
            Host other;
            REQUIRE(other.library);
            other.run(2);
        }
        host.run(4);
        CHECK_EQ(handles_of(host), others == 7 ? DEMO_HANDLES : 2 * DEMO_HANDLES);
    }
}

TEST(the_sections_have_the_titles_the_window_shows_and_nothing_else_has_one)
{
    REQUIRE_EQ(int(ImTypesetDemoSection_COUNT), int(IM_ARRAYSIZE(SECTIONS)));
    for (int section = 0; section < ImTypesetDemoSection_COUNT; ++section)
        CHECK_EQ(std::string(ImTypeset::GetDemoSectionName(section)),
                 std::string(SECTIONS[section]));
    CHECK_EQ(std::string(ImTypeset::GetDemoSectionName(-1)), std::string());
    CHECK_EQ(std::string(ImTypeset::GetDemoSectionName(ImTypesetDemoSection_COUNT)), std::string());
}

TEST(each_section_shown_alone_in_a_window_of_the_host_draws_and_settles)
{
    Host host;
    REQUIRE(host.library);
    host.show = false;
    for (int section = 0; section < ImTypesetDemoSection_COUNT; ++section) {
        CHECK_CONTEXT("section %s", ImTypeset::GetDemoSectionName(section));
        host.alone_first = section;
        host.alone_end = section + 1;
        host.run(4);
        const ImTypesetStats before = ImTypeset::GetStats();
        host.run(4);
        const ImTypesetStats after = ImTypeset::GetStats();
        CHECK(after.DrawCalls > before.DrawCalls);
        CHECK(after.VerticesEmitted > before.VerticesEmitted);
        CHECK_EQ(after.Layouts - before.Layouts, ImU64(0));
        CHECK_EQ(after.TextBuilds - before.TextBuilds, ImU64(0));
    }
}

TEST(a_value_that_is_not_a_section_draws_nothing)
{
    Host host;
    REQUIRE(host.library);
    host.show = false;
    host.alone_first = ImTypesetDemoSection_COUNT;
    host.alone_end = ImTypesetDemoSection_COUNT + 3;
    host.run(3);
    const int empty = vertices_of_the_last_frame();
    CHECK_EQ(ImTypeset::GetStats().DrawCalls, ImU64(0));
    CHECK_EQ(handles_of(host), size_t(0));
    host.alone_first = ImTypesetDemoSection_Specimen;
    host.alone_end = ImTypesetDemoSection_Specimen + 1;
    host.run(3);
    CHECK(vertices_of_the_last_frame() > empty);
}

// A host that shows the window and embeds every section as well has two demos
// that know nothing of each other: here at two sizes of type, in windows of two
// widths. Each keeps its own handles, and once both have settled nothing is
// laid out, whichever section it is.
TEST(the_window_and_the_sections_shown_alone_each_keep_their_own_state)
{
    Host host;
    REQUIRE(host.library);
    host.resize(900.0f, 3000.0f);
    ImGui::GetIO().DisplaySize = ImVec2(900.0f, 3000.0f);
    host.open(ALL);
    host.run(6);
    CHECK_EQ(handles_of(host), DEMO_HANDLES);
    host.alone_first = 0;
    host.alone_end = ImTypesetDemoSection_COUNT;
    host.run(6);
    CHECK_EQ(handles_of(host), 2 * DEMO_HANDLES);
    const ImU64 settled = ImTypeset::GetStats().Layouts;
    host.run(4);
    CHECK_EQ(ImTypeset::GetStats().Layouts - settled, ImU64(0));
    CHECK_EQ(handles_of(host), 2 * DEMO_HANDLES);
}

TEST(shown_alone_with_no_context_a_section_says_so_and_typesets_nothing)
{
    Host host(false);
    host.show = false;
    host.alone_first = 0;
    host.alone_end = ImTypesetDemoSection_COUNT;
    host.run(3);
    CHECK(vertices_of_the_last_frame() > 0);
    CHECK_EQ(ImTypeset::GetStats().DrawCalls, ImU64(0));
}

// What the Specimen section reads its numbers off: the extent of the one line
// of a paragraph object is the width layout set the text to, in fractions of a
// pixel. Held to the widths the Python reference's specimen
// (advtext.ui.specimen.build_specimen, tag advtext-reference) reports for the
// same faces, printed there to four decimals of a pixel: so the comparison is
// to half of the last printed digit plus a float's own step at 500, and it is
// not a golden replay.
TEST(the_width_of_a_line_is_the_set_width_the_reference_measures)
{
    Host host;
    REQUIRE(host.library);
    ImTypesetParagraph* paragraph = ImTypeset::CreateParagraph();
    REQUIRE(paragraph != nullptr);
    const auto width = [&](const char* text, ImTypesetFace face, float em) {
        ImTypesetStyle style;
        style.Face = face;
        style.EmSize = em;
        style.Align = ImTypesetAlign_Left;
        ImTypeset::SetParagraphText(paragraph, text);
        ImTypeset::LayoutParagraph(paragraph, 4096.0f, &style);
        const ImTypesetLine line = ImTypeset::GetParagraphLine(paragraph, 0);
        return double(line.Max.x - line.Min.x);
    };
    const double step = 0.0001;
    static const char PANGRAM[] = "The quick brown fox jumps over the lazy dog";
    CHECK_NEAR(width(PANGRAM, ImTypesetFace_Regular, 24.0f), 473.4376, step);
    CHECK_NEAR(width(PANGRAM, ImTypesetFace_Bold, 24.0f), 480.5627, step);
    CHECK_NEAR(width(PANGRAM, ImTypesetFace_Italic, 24.0f), 464.2618, step);
    CHECK_NEAR(width(PANGRAM, ImTypesetFace_BoldItalic, 24.0f), 471.1994, step);
    CHECK_NEAR(width(PANGRAM, ImTypesetFace_Code, 24.0f), 516.0001, step);
    CHECK_NEAR(width(PANGRAM, ImTypesetFace_Math, 24.0f), 479.4241, step);
    CHECK_NEAR(width("Hamburgefonstiv", ImTypesetFace_Regular, 31.25f), 240.7532, step);
    // A pair set as one text, and letter by letter: the difference is the
    // font's kern for the pair at this size, with its sign.
    CHECK_NEAR(width("AV", ImTypesetFace_Regular, 44.0f), 54.8281, step);
    CHECK_NEAR(width("A", ImTypesetFace_Regular, 44.0f) + width("V", ImTypesetFace_Regular, 44.0f),
               56.6973, step);
    CHECK_NEAR(width("rt", ImTypesetFace_Regular, 44.0f), 30.3359, step);
    CHECK_NEAR(width("r", ImTypesetFace_Regular, 44.0f) + width("t", ImTypesetFace_Regular, 44.0f),
               29.2617, step);
    // What CalcTextSize() answers for the same text is whole pixels, rounded up.
    ImTypesetStyle style;
    style.EmSize = 44.0f;
    style.Align = ImTypesetAlign_Left;
    CHECK_EQ(ImTypeset::CalcTextSize("AV", nullptr, 0.0f, 0, &style).x, 55.0f);
    ImTypeset::DestroyParagraph(paragraph);
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
