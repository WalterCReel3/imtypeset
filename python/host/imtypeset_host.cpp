// The window a Python program needs around imtypeset: see imtypeset_host.h,
// which says what this is and, more to the point, what it is not.
//
// This file is derived from Dear ImGui's examples/example_sdl2_sdlrenderer2/main.cpp
// and examples/example_sdl2_opengl3/main.cpp (v1.92.9), which are:
//
//   The MIT License (MIT)
//
//   Copyright (c) 2014-2026 Omar Cornut
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in all
//   copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//   SOFTWARE.

#include "imtypeset_host.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if !SDL_VERSION_ATLEAST(2, 0, 17)
#error The SDL_Renderer backend requires SDL 2.0.17+ because of SDL_RenderGeometry()
#endif

namespace
{

// The few GL entry points this file calls itself, fetched from the context SDL
// made: no GL header and no libGL at link time. ImGui's backend carries its
// own loader.
struct GL {
    void (*Viewport)(int, int, int, int) = nullptr;
    void (*ClearColor)(float, float, float, float) = nullptr;
    void (*Clear)(unsigned) = nullptr;
    const unsigned char* (*GetString)(unsigned) = nullptr;
    void (*ReadPixels)(int, int, int, int, unsigned, unsigned, void*) = nullptr;
    void (*PixelStorei)(unsigned, int) = nullptr;
    void (*ReadBuffer)(unsigned) = nullptr;

    bool load()
    {
        Viewport = (void (*)(int, int, int, int))SDL_GL_GetProcAddress("glViewport");
        ClearColor = (void (*)(float, float, float, float))SDL_GL_GetProcAddress("glClearColor");
        Clear = (void (*)(unsigned))SDL_GL_GetProcAddress("glClear");
        GetString = (const unsigned char* (*)(unsigned))SDL_GL_GetProcAddress("glGetString");
        ReadPixels = (void (*)(int, int, int, int, unsigned, unsigned, void*))SDL_GL_GetProcAddress(
            "glReadPixels");
        PixelStorei = (void (*)(unsigned, int))SDL_GL_GetProcAddress("glPixelStorei");
        ReadBuffer = (void (*)(unsigned))SDL_GL_GetProcAddress("glReadBuffer");
        return Viewport && ClearColor && Clear && GetString && ReadPixels && PixelStorei &&
               ReadBuffer;
    }
};
const unsigned GL_COLOR_BUFFER_BIT_ = 0x4000;
const unsigned GL_RENDERER_ = 0x1F01;
const unsigned GL_RGB_ = 0x1907;
const unsigned GL_UNSIGNED_BYTE_ = 0x1401;
const unsigned GL_PACK_ALIGNMENT_ = 0x0D05;
const unsigned GL_BACK_ = 0x0405;

struct Host {
    bool SdlUp = false;
    SDL_Window* Window = nullptr;
    SDL_Renderer* Renderer = nullptr;
    SDL_GLContext GlContext = nullptr;
    GL Gl;
    bool OpenGL = false;
    bool ImGuiUp = false;
    bool BackendsUp = false;
    bool CloseRequested = false;
    std::string RendererName;
    std::string Error;
    // The scripted mouse (ImTypesetHost_ScriptMouse).
    bool Scripted = false;
    float ScriptX = 0.0f;
    float ScriptY = 0.0f;
    bool ScriptDown = false;
    std::vector<unsigned char> Scratch;
};

Host g_host;

int fail(const std::string& what)
{
    g_host.Error = what;
    return 0;
}

void copy_name(char* out, size_t out_size, const char* text)
{
    std::snprintf(out, out_size, "%s", text != nullptr ? text : "");
}

void framebuffer_size(int* width, int* height)
{
    *width = 0;
    *height = 0;
    if (g_host.Window == nullptr)
        return;
    if (g_host.OpenGL)
        SDL_GL_GetDrawableSize(g_host.Window, width, height);
    else
        SDL_GetRendererOutputSize(g_host.Renderer, width, height);
}

void teardown()
{
    Host& h = g_host;
    if (h.BackendsUp) {
        if (h.OpenGL)
            ImGui_ImplOpenGL3_Shutdown();
        else
            ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        h.BackendsUp = false;
    }
    if (h.ImGuiUp) {
        ImGui::DestroyContext();
        h.ImGuiUp = false;
    }
    if (h.GlContext != nullptr)
        SDL_GL_DeleteContext(h.GlContext);
    if (h.Renderer != nullptr)
        SDL_DestroyRenderer(h.Renderer);
    if (h.Window != nullptr)
        SDL_DestroyWindow(h.Window);
    h.GlContext = nullptr;
    h.Renderer = nullptr;
    h.Window = nullptr;
    if (h.SdlUp)
        SDL_Quit();
    h.SdlUp = false;
    h.Scripted = false;
    h.CloseRequested = false;
}

} // namespace

extern "C" {

int ImTypesetHost_GetInfo(ImTypesetHost_Info* out, int out_size)
{
    ImTypesetHost_Info info;
    std::memset(&info, 0, sizeof info);
    info.StructSize = (int)sizeof info;
    info.ImGuiVersionNum = IMGUI_VERSION_NUM;
    info.SizeofImWchar = (int)sizeof(ImWchar);
    info.SizeofImDrawVert = (int)sizeof(ImDrawVert);
    info.SizeofImDrawIdx = (int)sizeof(ImDrawIdx);
    info.SizeofImGuiIO = (int)sizeof(ImGuiIO);
    info.SizeofImGuiStyle = (int)sizeof(ImGuiStyle);
    copy_name(info.ImGuiVersion, sizeof info.ImGuiVersion, IMGUI_VERSION);
#ifdef IMGUI_ENABLE_FREETYPE
    copy_name(info.FontLoader, sizeof info.FontLoader, "FreeType");
#else
    copy_name(info.FontLoader, sizeof info.FontLoader, "stb_truetype");
#endif
    if (g_host.Window != nullptr && g_host.ImGuiUp) {
        info.HasWindow = 1;
        framebuffer_size(&info.FramebufferWidth, &info.FramebufferHeight);
        copy_name(info.VideoDriver, sizeof info.VideoDriver, SDL_GetCurrentVideoDriver());
        copy_name(info.RendererBackend, sizeof info.RendererBackend,
                  ImGui::GetIO().BackendRendererName);
        copy_name(info.Renderer, sizeof info.Renderer, g_host.RendererName.c_str());
    }
    if (out != nullptr && out_size > 0) {
        const size_t n = (size_t)out_size < sizeof info ? (size_t)out_size : sizeof info;
        std::memcpy(out, &info, n);
    }
    return (int)sizeof info;
}

const char* ImTypesetHost_GetError(void)
{
    return g_host.Error.c_str();
}

int ImTypesetHost_Create(const char* title, int width, int height, int backend, float dpi_scale)
{
    Host& h = g_host;
    if (h.Window != nullptr)
        return fail("ImTypesetHost_Create: there is a window already");
    if (width < 1 || height < 1 || width > 16384 || height > 16384)
        return fail("ImTypesetHost_Create: the size is out of range");
    if (backend != ImTypesetHost_Backend_SDLRenderer && backend != ImTypesetHost_Backend_OpenGL3)
        return fail("ImTypesetHost_Create: no such backend");
    if (!(dpi_scale >= 0.0f && dpi_scale <= 8.0f))
        return fail("ImTypesetHost_Create: dpi_scale is between 0 and 8");
    h.Error.clear();
    h.OpenGL = backend == ImTypesetHost_Backend_OpenGL3;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
        return fail(std::string("SDL_Init: ") + SDL_GetError());
    h.SdlUp = true;
#ifdef SDL_HINT_IME_SHOW_UI
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif

    // GL 3.0 + GLSL 130, the choice ImGui's example makes for desktop Linux
    // and Windows.
    const char* glsl_version = "#version 130";
    Uint32 window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (h.OpenGL) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
        window_flags |= SDL_WINDOW_OPENGL;
    }
    const float main_scale =
        dpi_scale > 0.0f ? dpi_scale : ImGui_ImplSDL2_GetContentScaleForDisplay(0);
    h.Window = SDL_CreateWindow(title != nullptr ? title : "imtypeset", SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, width, height, window_flags);
    if (h.Window == nullptr) {
        const std::string why = std::string("SDL_CreateWindow: ") + SDL_GetError();
        teardown();
        return fail(why);
    }

    if (h.OpenGL) {
        h.GlContext = SDL_GL_CreateContext(h.Window);
        if (h.GlContext == nullptr || SDL_GL_MakeCurrent(h.Window, h.GlContext) != 0 ||
            !h.Gl.load()) {
            const std::string why = std::string("no OpenGL context: ") + SDL_GetError();
            teardown();
            return fail(why);
        }
        SDL_GL_SetSwapInterval(0);
        const unsigned char* name = h.Gl.GetString(GL_RENDERER_);
        h.RendererName = name != nullptr ? (const char*)name : "";
    } else {
        // Accelerated where there is something to accelerate with; SDL's own
        // software renderer where there is not (the dummy video driver).
        h.Renderer = SDL_CreateRenderer(h.Window, -1, SDL_RENDERER_ACCELERATED);
        if (h.Renderer == nullptr)
            h.Renderer = SDL_CreateRenderer(h.Window, -1, SDL_RENDERER_SOFTWARE);
        if (h.Renderer == nullptr) {
            const std::string why = std::string("SDL_CreateRenderer: ") + SDL_GetError();
            teardown();
            return fail(why);
        }
        SDL_RendererInfo info;
        h.RendererName = SDL_GetRendererInfo(h.Renderer, &info) == 0 ? info.name : "";
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    h.ImGuiUp = true;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsLight();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;

    bool ok;
    if (h.OpenGL)
        ok = ImGui_ImplSDL2_InitForOpenGL(h.Window, h.GlContext) &&
             ImGui_ImplOpenGL3_Init(glsl_version);
    else
        ok = ImGui_ImplSDL2_InitForSDLRenderer(h.Window, h.Renderer) &&
             ImGui_ImplSDLRenderer2_Init(h.Renderer);
    if (!ok) {
        teardown();
        return fail("Dear ImGui's SDL2 backends refused to start");
    }
    h.BackendsUp = true;
    return 1;
}

void ImTypesetHost_Destroy(void)
{
    teardown();
}

int ImTypesetHost_BeginFrame(void)
{
    Host& h = g_host;
    if (!h.BackendsUp)
        return fail("ImTypesetHost_BeginFrame: no window");
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // A scripted run takes its input from the script alone: a pointer
        // that happens to be over the window, or a focus change, would make
        // the same script draw two different frames.
        if (!h.Scripted)
            ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT)
            h.CloseRequested = true;
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
            event.window.windowID == SDL_GetWindowID(h.Window))
            h.CloseRequested = true;
    }
    if (h.CloseRequested)
        return fail("the window was closed");

    if (h.OpenGL)
        ImGui_ImplOpenGL3_NewFrame();
    else
        ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    if (h.Scripted) {
        // After the backend, which may have queued the real pointer's place.
        ImGuiIO& io = ImGui::GetIO();
        io.AddFocusEvent(true);
        io.AddMousePosEvent(h.ScriptX, h.ScriptY);
        io.AddMouseButtonEvent(0, h.ScriptDown);
    }
    ImGui::NewFrame();
    return 1;
}

int ImTypesetHost_EndFrame(unsigned char* rgb, int rgb_capacity, int* out_width, int* out_height)
{
    Host& h = g_host;
    if (!h.BackendsUp)
        return fail("ImTypesetHost_EndFrame: no window");
    ImGui::Render();
    int w = 0;
    int h_px = 0;
    framebuffer_size(&w, &h_px);
    if (out_width != nullptr)
        *out_width = w;
    if (out_height != nullptr)
        *out_height = h_px;
    const bool shoot = rgb != nullptr;
    const size_t row = (size_t)w * 3;
    bool ok = true;
    if (shoot &&
        (w <= 0 || h_px <= 0 || rgb_capacity < 0 || (size_t)rgb_capacity < row * (size_t)h_px)) {
        h.Error = "ImTypesetHost_EndFrame: the buffer is smaller than the frame";
        ok = false;
    }
    const ImVec4 clear(1.0f, 1.0f, 1.0f, 1.0f);
    if (h.OpenGL) {
        h.Gl.Viewport(0, 0, w, h_px);
        h.Gl.ClearColor(clear.x, clear.y, clear.z, clear.w);
        h.Gl.Clear(GL_COLOR_BUFFER_BIT_);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (shoot && ok) {
            // GL's rows run bottom to top.
            h.Scratch.resize(row * (size_t)h_px);
            h.Gl.PixelStorei(GL_PACK_ALIGNMENT_, 1);
            h.Gl.ReadBuffer(GL_BACK_);
            h.Gl.ReadPixels(0, 0, w, h_px, GL_RGB_, GL_UNSIGNED_BYTE_, h.Scratch.data());
            for (int y = 0; y < h_px; ++y)
                std::memcpy(rgb + (size_t)y * row, &h.Scratch[(size_t)(h_px - 1 - y) * row], row);
        }
        SDL_GL_SwapWindow(h.Window);
    } else {
        const ImGuiIO& io = ImGui::GetIO();
        SDL_RenderSetScale(h.Renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(h.Renderer, 255, 255, 255, 255);
        SDL_RenderClear(h.Renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), h.Renderer);
        // Before the present: afterwards the back buffer is undefined.
        if (shoot && ok &&
            SDL_RenderReadPixels(h.Renderer, nullptr, SDL_PIXELFORMAT_RGB24, rgb, (int)row) != 0) {
            h.Error = std::string("SDL_RenderReadPixels: ") + SDL_GetError();
            ok = false;
        }
        SDL_RenderPresent(h.Renderer);
    }
    return ok ? 1 : 0;
}

void ImTypesetHost_ScriptMouse(int enabled, float x, float y, int left_down)
{
    g_host.Scripted = enabled != 0;
    g_host.ScriptX = x;
    g_host.ScriptY = y;
    g_host.ScriptDown = left_down != 0;
}

void ImTypesetHost_ScriptCopyShortcut(int down)
{
    if (!g_host.ImGuiUp)
        return;
    ImGuiIO& io = ImGui::GetIO();
    // The shortcut key as a backend submits it: Cmd on a Mac, which Dear ImGui
    // exchanges with Ctrl as the event arrives under ConfigMacOSXBehaviors.
    io.AddKeyEvent(io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl, down != 0);
    io.AddKeyEvent(ImGuiKey_C, down != 0);
}

void ImTypesetHost_SetNextWindowPos(float x, float y)
{
    ImGui::SetNextWindowPos(ImVec2(x, y));
}

void ImTypesetHost_SetNextWindowSize(float width, float height)
{
    ImGui::SetNextWindowSize(ImVec2(width, height));
}

int ImTypesetHost_Begin(const char* name, int flags)
{
    return ImGui::Begin(name, nullptr, flags) ? 1 : 0;
}

void ImTypesetHost_End(void)
{
    ImGui::End();
}

int ImTypesetHost_BeginChild(const char* str_id, float width)
{
    return ImGui::BeginChild(str_id, ImVec2(width, 0.0f), ImGuiChildFlags_AutoResizeY) ? 1 : 0;
}

void ImTypesetHost_EndChild(void)
{
    ImGui::EndChild();
}

void ImTypesetHost_SameLine(float spacing)
{
    ImGui::SameLine(0.0f, spacing);
}

void ImTypesetHost_Separator(void)
{
    ImGui::Separator();
}

void ImTypesetHost_Dummy(float width, float height)
{
    ImGui::Dummy(ImVec2(width, height));
}

void ImTypesetHost_TextWrapped(const char* text, const char* text_end)
{
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text, text_end);
    ImGui::PopTextWrapPos();
}

int ImTypesetHost_SliderFloat(const char* label, float* value, float value_min, float value_max)
{
    return ImGui::SliderFloat(label, value, value_min, value_max, "%.0f") ? 1 : 0;
}

void ImTypesetHost_PushFont(struct ImFont* font, float size)
{
    ImGui::PushFont(font, size);
}

void ImTypesetHost_PopFont(void)
{
    ImGui::PopFont();
}

void ImTypesetHost_GetContentRegionAvail(float* out_xy)
{
    const ImVec2 v = ImGui::GetContentRegionAvail();
    out_xy[0] = v.x;
    out_xy[1] = v.y;
}

void ImTypesetHost_GetCursorScreenPos(float* out_xy)
{
    const ImVec2 v = ImGui::GetCursorScreenPos();
    out_xy[0] = v.x;
    out_xy[1] = v.y;
}

void ImTypesetHost_GetMousePos(float* out_xy)
{
    const ImVec2 v = ImGui::GetMousePos();
    out_xy[0] = v.x;
    out_xy[1] = v.y;
}

void ImTypesetHost_GetItemRect(float* out_rect)
{
    const ImVec2 lo = ImGui::GetItemRectMin();
    const ImVec2 hi = ImGui::GetItemRectMax();
    out_rect[0] = lo.x;
    out_rect[1] = lo.y;
    out_rect[2] = hi.x;
    out_rect[3] = hi.y;
}

int ImTypesetHost_IsMouseDown(int button)
{
    return button >= 0 && button < ImGuiMouseButton_COUNT && ImGui::IsMouseDown(button) ? 1 : 0;
}

struct ImDrawList* ImTypesetHost_GetWindowDrawList(void)
{
    return ImGui::GetWindowDrawList();
}

const char* ImTypesetHost_GetClipboardText(void)
{
    const char* text = ImGui::GetClipboardText();
    return text != nullptr ? text : "";
}

void ImTypesetHost_SetClipboardText(const char* text)
{
    ImGui::SetClipboardText(text != nullptr ? text : "");
}

} // extern "C"
