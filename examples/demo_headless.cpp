// The demo window with no display: ImTypeset::ShowDemoWindow() driven by
// scripted input through ImGuiIO's event queue and drawn by the software
// renderer the tests use.
//
//     example_demo_headless              runs every section, writes nothing
//     example_demo_headless DIR          also writes DIR/demo-<section>.png
//     example_demo_headless --frame OUT.png [--size WxH] [--open SECTIONS]
//                           [--frames N] [--drag X0,Y0,X1,Y1] [--scroll Y]
//
// The first two open one section at a time, in a window that fills the frame,
// and act on it as a user would: a drag across the selectable text, a formula
// typed into the math source, the wheel over the long document, a file that is
// not a font offered as one. They fail if a frame in which nothing happens lays
// anything out.
//
// --frame renders the frame examples/demo_sdl2 renders when given the same
// options with --fill --dpi-scale 1, so that one frame can be held against the
// same draw data through SDL's and OpenGL's rasterisers.
//
// Each run has ImGui and ImTypeset contexts of its own, made and destroyed
// around it: the demo window has to survive that, as it does in a host that
// recreates its contexts.

#include "imgui.h"
#include "imtypeset.h"

#include "support/soft_raster.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifndef IMTYPESET_TEST_DATA_DIR
#define IMTYPESET_TEST_DATA_DIR "tests/data"
#endif
#ifndef IMTYPESET_SOURCE_DIR
#define IMTYPESET_SOURCE_DIR "."
#endif

namespace
{

const char* const SECTIONS[] = {
    "Paragraph", "Faces and sizes", "Math",         "Selection", "Inspector",
    "Stress",    "Fonts",           "Known limits", "Document",
};

// What is behind the window, which is not quite opaque: the clear colour of
// ImGui's examples, and of examples/demo_sdl2, as OpenGL rounds it.
const ImU32 CLEAR = IM_COL32(115, 140, 153, 255);

// What a run does to the window before frame `frame` (1 is the first).
typedef void (*Script)(int frame, ImGuiIO& io);

struct Run {
    int Width = 900;
    int Height = 600;
    std::string Open; // Section titles separated by commas.
    float Scroll = 0.0f;
    int Frames = 14;
    Script Input = nullptr;
    // The --drag of examples/demo_sdl2, for --frame.
    bool Drag = false;
    float DragFrom[2] = {0.0f, 0.0f};
    float DragTo[2] = {0.0f, 0.0f};
};

// Opens the sections named in `open` and closes the others, makes the demo
// window fill the frame and scrolls it: from outside, through the window's
// name. A collapsing header keeps whether it is open in its window's storage
// under the ID of its label.
void arrange_demo_window(const Run& run)
{
    ImGui::Begin("ImTypeset Demo");
    const std::string list = "," + run.Open + ",";
    for (const char* section : SECTIONS) {
        const bool open = list.find("," + std::string(section) + ",") != std::string::npos;
        ImGui::GetStateStorage()->SetInt(ImGui::GetID(section), open ? 1 : 0);
    }
    ImGui::SetWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetWindowSize(ImGui::GetIO().DisplaySize);
    if (run.Scroll > 0.0f)
        ImGui::SetScrollY(run.Scroll);
    ImGui::End();
}

// One run from context to context. Fills `image` with the last frame.
bool run_demo(const Run& run, soft_raster::Image& image)
{
    static const struct {
        ImTypesetFace Face;
        const char* File;
    } faces[] = {
        {ImTypesetFace_Regular, "Roboto-Regular.ttf"},
        {ImTypesetFace_Bold, "Roboto-Bold.ttf"},
        {ImTypesetFace_Italic, "Roboto-RegularItalic.ttf"},
        {ImTypesetFace_BoldItalic, "Roboto-BoldItalic.ttf"},
        {ImTypesetFace_Code, "Inconsolata-Medium.ttf"},
        {ImTypesetFace_Math, "latinmodern-math.otf"},
    };
    const std::string fonts = IMTYPESET_TEST_DATA_DIR "/fonts/";

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DeltaTime = 1.0f / 60.0f;
    io.DisplaySize = ImVec2((float)run.Width, (float)run.Height);
    ImGui::StyleColorsDark();
    soft_raster::Renderer renderer;
    renderer.init();

    bool ok = IMTYPESET_CHECKVERSION() && ImTypeset::CreateContext() != nullptr;
    // ImGui's own font first, so that it stays the UI's.
    io.Fonts->AddFontDefault();
    for (int i = 0; ok && i < IM_ARRAYSIZE(faces); ++i)
        ok = ImTypeset::AddFontFromFileTTF(faces[i].Face, (fonts + faces[i].File).c_str()) !=
             nullptr;
    if (!ok)
        std::fprintf(stderr, "demo_headless: %s\n", ImTypeset::GetLastError()->Message);

    image = soft_raster::Image(run.Width, run.Height, CLEAR);
    for (int frame = 1; ok && frame <= run.Frames; ++frame) {
        if (run.Input != nullptr)
            run.Input(frame, io);
        if (run.Drag) {
            const bool arrived = frame >= 6;
            io.AddMousePosEvent(arrived ? run.DragTo[0] : run.DragFrom[0],
                                arrived ? run.DragTo[1] : run.DragFrom[1]);
            if (frame == 4)
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        }
        ImGui::NewFrame();
        ImTypeset::ShowDemoWindow();
        if (frame == 1)
            arrange_demo_window(run);
        ImGui::Render();
        image.clear(CLEAR);
        renderer.render(ImGui::GetDrawData(), image);
    }

    // Three more frames with the pointer away and nothing held: whatever the
    // script changed has been laid out, and nothing may be laid out again.
    if (ok && run.Input != nullptr) {
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        ImU64 layouts = 0;
        for (int frame = 0; frame < 4; ++frame) {
            if (frame == 1)
                layouts = ImTypeset::GetStats().Layouts;
            ImGui::NewFrame();
            ImTypeset::ShowDemoWindow();
            ImGui::Render();
        }
        const ImU64 steady = ImTypeset::GetStats().Layouts - layouts;
        if (steady != 0) {
            std::fprintf(stderr, "demo_headless: [%s] %llu layout(s) in three idle frames\n",
                         run.Open.c_str(), (unsigned long long)steady);
            ok = false;
        }
    }

    ImTypeset::DestroyContext();
    renderer.shutdown();
    ImGui::DestroyContext();
    return ok;
}

// --- The scripts ---------------------------------------------------------------------
// Positions are in the window as arrange_demo_window() leaves it, with one
// section open: its header is at y = 77 + 23 * (its index).

void idle(int, ImGuiIO&) {}

// Math: click into the source, replace it by a formula with an accent, which
// is outside the subset, so that the refusal and its place are shown.
void script_math(int frame, ImGuiIO& io)
{
    if (frame == 4) {
        io.AddMousePosEvent(100.0f, 150.0f);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    } else if (frame == 5) {
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    } else if (frame == 6) {
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_A, true);
    } else if (frame == 7) {
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
    } else if (frame == 8) {
        io.AddInputCharactersUTF8("\\frac{1}{2} + \\hat{x}");
    } else if (frame == 10) {
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }
}

// Selection: a drag by words in the paragraph selected by hand, released, then
// a drag across three lines of the selectable text, held. (The other order
// would show one selection: a click elsewhere clears the widget's.)
void script_selection(int frame, ImGuiIO& io)
{
    if (frame == 4) {
        io.AddMousePosEvent(60.0f, 545.0f);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    } else if (frame == 6) {
        io.AddMousePosEvent(380.0f, 572.0f);
    } else if (frame == 8) {
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    } else if (frame == 10) {
        io.AddMousePosEvent(120.0f, 238.0f);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    } else if (frame == 12) {
        io.AddMousePosEvent(300.0f, 305.0f);
    }
}

// Inspector: the pointer over the second line.
void script_inspector(int frame, ImGuiIO& io)
{
    if (frame == 4)
        io.AddMousePosEvent(210.0f, 262.0f);
}

// Stress: the slider of copies dragged past its right end, to x1000, then the
// wheel over the document.
void script_stress(int frame, ImGuiIO& io)
{
    if (frame == 4) {
        io.AddMousePosEvent(80.0f, 215.0f);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    } else if (frame == 6) {
        io.AddMousePosEvent(3000.0f, 215.0f);
    } else if (frame == 8) {
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    } else if (frame == 9) {
        io.AddMousePosEvent(400.0f, 420.0f);
    } else if (frame >= 10 && frame <= 40) {
        io.AddMouseWheelEvent(0.0f, -40.0f);
    }
}

// Fonts: this tree's CMakeLists.txt offered as a font for the Regular role.
void script_fonts(int frame, ImGuiIO& io)
{
    if (frame == 4) {
        io.AddMousePosEvent(100.0f, 481.0f);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    } else if (frame == 5) {
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    } else if (frame == 7) {
        io.AddInputCharactersUTF8(IMTYPESET_SOURCE_DIR "/CMakeLists.txt");
    } else if (frame == 9) {
        io.AddMousePosEvent(40.0f, 527.0f);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    } else if (frame == 10) {
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    } else if (frame == 12) {
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }
}

struct Page {
    const char* File;
    const char* Section;
    int Height;
    Script Input;
    int Frames;
};

const Page PAGES[] = {
    {"demo-paragraph", "Paragraph", 600, idle, 14},
    {"demo-faces", "Faces and sizes", 520, idle, 14},
    {"demo-math", "Math", 1500, script_math, 14},
    {"demo-selection", "Selection", 720, script_selection, 14},
    {"demo-inspector", "Inspector", 540, script_inspector, 14},
    {"demo-stress", "Stress", 720, script_stress, 44},
    {"demo-fonts", "Fonts", 640, script_fonts, 14},
    {"demo-limits", "Known limits", 660, idle, 14},
    {"demo-document", "Document", 720, idle, 14},
};

int usage()
{
    std::fprintf(stderr,
                 "usage: example_demo_headless [DIR]\n"
                 "       example_demo_headless --frame OUT.png [--size WxH] [--open SECTIONS]\n"
                 "                             [--frames N] [--drag X0,Y0,X1,Y1] [--scroll Y]\n");
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--frame") == 0) {
        if (argc < 3 || (argc - 3) % 2 != 0)
            return usage();
        Run run;
        run.Width = 1280;
        run.Height = 800;
        run.Frames = 30;
        for (int i = 3; i + 1 < argc; i += 2) {
            const char* arg = argv[i];
            const char* value = argv[i + 1];
            if (std::strcmp(arg, "--size") == 0) {
                if (std::sscanf(value, "%dx%d", &run.Width, &run.Height) != 2 || run.Width < 1 ||
                    run.Height < 1 || run.Width > 4096 || run.Height > 4096)
                    return usage();
            } else if (std::strcmp(arg, "--open") == 0) {
                run.Open = value;
            } else if (std::strcmp(arg, "--frames") == 0) {
                run.Frames = std::atoi(value);
                if (run.Frames < 1 || run.Frames > 10000)
                    return usage();
            } else if (std::strcmp(arg, "--scroll") == 0) {
                run.Scroll = (float)std::atof(value);
            } else if (std::strcmp(arg, "--drag") == 0) {
                if (std::sscanf(value, "%f,%f,%f,%f", &run.DragFrom[0], &run.DragFrom[1],
                                &run.DragTo[0], &run.DragTo[1]) != 4)
                    return usage();
                run.Drag = true;
            } else {
                return usage();
            }
        }
        soft_raster::Image image;
        if (!run_demo(run, image))
            return 1;
        if (!soft_raster::write_png(argv[2], image)) {
            std::fprintf(stderr, "demo_headless: cannot write %s\n", argv[2]);
            return 1;
        }
        std::printf("demo_headless: wrote %s (%d x %d)\n", argv[2], run.Width, run.Height);
        return 0;
    }
    if (argc > 2)
        return usage();

    const char* out_dir = argc > 1 ? argv[1] : nullptr;
    bool ok = true;
    for (const Page& page : PAGES) {
        Run run;
        run.Height = page.Height;
        run.Open = page.Section;
        run.Input = page.Input;
        run.Frames = page.Frames;
        soft_raster::Image image;
        if (!run_demo(run, image)) {
            std::fprintf(stderr, "demo_headless: %s failed\n", page.Section);
            ok = false;
            continue;
        }
        if (out_dir == nullptr)
            continue;
        const std::string path = std::string(out_dir) + "/" + page.File + ".png";
        if (!soft_raster::write_png(path.c_str(), image)) {
            std::fprintf(stderr, "demo_headless: cannot write %s\n", path.c_str());
            ok = false;
            continue;
        }
        std::printf("demo_headless: wrote %s (%d x %d)\n", path.c_str(), run.Width, run.Height);
    }
    std::printf("demo_headless: %d sections, %s\n", IM_ARRAYSIZE(PAGES), ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
