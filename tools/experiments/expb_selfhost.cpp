// Experiment B, control: the ImGui frame calls a Python driver needs when there
// is NO imgui_bundle in the process. Compiled only into the `selfhost` variant
// of the experiment library, which links this repository's own ImGui
// (.deps/imgui) instead of leaving ImGui's symbols undefined. It exists so that
// tools/experiments/exp_b_run.py can be run end to end on this machine, where
// the bundle variants stop at the layout check.

#include "imgui.h"

#define EXPB_API extern "C" __attribute__((visibility("default")))

EXPB_API const char* expb_host_version()
{
    return ImGui::GetVersion();
}

EXPB_API void expb_host_create(float width, float height)
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(width, height);
    io.DeltaTime = 1.0f / 60.0f;
}

EXPB_API void expb_host_new_frame()
{
    ImGui::NewFrame();
}

EXPB_API void expb_host_begin(const char* name, float x, float y, float width, float height)
{
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoSavedSettings);
}

EXPB_API void expb_host_end()
{
    ImGui::End();
}

// Returns the frame's vertex count.
EXPB_API int expb_host_render()
{
    ImGui::Render();
    return ImGui::GetDrawData()->TotalVtxCount;
}

EXPB_API void expb_host_destroy()
{
    ImGui::DestroyContext();
}
