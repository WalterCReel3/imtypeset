// The smallest host: a paragraph of justified text, a formula in it, in an
// ImGui window. Everything the library needs from a host is in main().
//
//     example_minimal [directory holding the .ttf/.otf files]
//
// A real host has a platform and a renderer backend where the comments say so.
// This one has neither and runs a few frames headless, so that the snippet a
// README shows is a program that is built and run with the tests.

#include "imgui.h"
#include "imtypeset.h"

#include <cstdio>
#include <string>

#ifndef IMTYPESET_TEST_DATA_DIR
#define IMTYPESET_TEST_DATA_DIR "tests/data"
#endif

int main(int argc, char** argv)
{
    const std::string fonts = argc > 1 ? argv[1] : IMTYPESET_TEST_DATA_DIR "/fonts";

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    // A host's backends do these three: ImGui_ImplXxx_Init() and _NewFrame().
    // The renderer must be one that announces RendererHasTextures (any stock
    // backend of ImGui 1.92 or later).
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.DisplaySize = ImVec2(640.0f, 480.0f);
    io.DeltaTime = 1.0f / 60.0f;

    // After ImGui's context, before the first frame. The check is ImGui's
    // IMGUI_CHECKVERSION() for this library: this file and the library must
    // have been compiled against the same headers.
    if (!IMTYPESET_CHECKVERSION() || ImTypeset::CreateContext() == nullptr) {
        std::fprintf(stderr, "imtypeset: %s\n", ImTypeset::GetLastError()->Message);
        return 1;
    }
    // One font per role the text uses. The first one added is also ImGui's
    // default font, as with ImFontAtlas::AddFontFromFileTTF.
    const bool have_fonts =
        ImTypeset::AddFontFromFileTTF(ImTypesetFace_Regular,
                                      (fonts + "/Roboto-Regular.ttf").c_str()) != nullptr &&
        ImTypeset::AddFontFromFileTTF(ImTypesetFace_Math,
                                      (fonts + "/latinmodern-math.otf").c_str()) != nullptr;
    if (!have_fonts) {
        std::fprintf(stderr, "imtypeset: %s\n", ImTypeset::GetLastError()->Message);
        return 1;
    }

    static const char paragraph[] =
        "ImGui draws text; this typesets it. Lines are chosen for the whole paragraph at "
        "once, as TeX chooses them, and every glyph lands on a whole pixel. A formula such "
        "as $e^{i\\pi} + 1 = 0$ is a box on the baseline among the words.";

    int vertices = 0;
    for (int frame = 0; frame < 3; ++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f));
        ImGui::Begin("Typeset");
        ImGui::TextWrapped("ImGui::TextWrapped, for comparison.");
        // One call per paragraph per frame. The layout is kept between frames,
        // found again by the words themselves, and redone when the text, the
        // window's width or the style changes. The flag is only for text whose
        // `$...$` are formulas; without it a dollar is a dollar.
        if (!ImTypeset::TextUnformatted(paragraph, nullptr, ImTypesetTextFlags_Math))
            std::fprintf(stderr, "imtypeset: %s\n", ImTypeset::GetLastError()->Message);
        ImGui::End();
        ImGui::Render();
        // A host's renderer draws ImGui::GetDrawData() here; the typeset text
        // is in it, in the window's own draw list.
        vertices = ImGui::GetDrawData()->TotalVtxCount;
    }

    const int layouts = (int)ImTypeset::GetStats().Layouts;
    std::printf("minimal: 3 frames, %d layout(s), %d vertices in the last frame\n", layouts,
                vertices);

    ImTypeset::DestroyContext();
    ImGui::DestroyContext();
    return layouts == 1 && vertices > 0 ? 0 : 1;
}
