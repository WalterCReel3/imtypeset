// A real host for the demo window: SDL2 for the window and input, and one of
// two of Dear ImGui's stock renderer backends, chosen at run time. Run with no
// arguments it opens a window holding ImTypeset::ShowDemoWindow().
//
//     demo_sdl2 [--backend sdlrenderer|opengl3] [--size WxH] [--fonts DIR]
//               [--frames N] [--screenshot out.bmp] [--stay]
//               [--sdl-renderer NAME] [--dpi-scale F]
//               [--open SECTIONS] [--fill] [--scroll Y] [--imgui-demo]
//               [--drag X0,Y0,X1,Y1]
//
//   --fonts DIR     where the font files are (Roboto-Regular.ttf and the rest
//                   of tests/data/fonts). Default: that directory of the tree
//                   this was built from.
//   --backend       sdlrenderer (default): ImGui's SDL_Renderer backend, which
//                   needs no OpenGL and also runs under SDL's `dummy` and
//                   `offscreen` video drivers. opengl3: ImGui's OpenGL 3
//                   backend on a GL context SDL creates.
//   --frames N      run N frames and exit. 0 (default): run until closed.
//   --screenshot F  on frame N, read the frame back from the renderer and write
//                   it as a BMP. The file appears under its name only once it
//                   is complete and the frame has been presented, so a script
//                   may wait for it and then photograph the window itself.
//   --stay          keep running after frame N (for that script).
//   --sdl-renderer  ask SDL for a named render driver (`software`, `opengl`,
//                   `opengles2`), for the sdlrenderer backend.
//   --dpi-scale F   the style and font scale. 0 (default): what SDL reports
//                   for display 0, as ImGui's examples do.
//   --open SECTIONS for an unattended run: the demo window's sections to open,
//                   by their titles, separated by commas ("Paragraph,Math"), or
//                   `all`.
//   --fill          for an unattended run: the demo window covers the whole
//                   window, and the Host window is not shown.
//   --scroll Y      for an unattended run: the demo window scrolled down Y px.
//   --drag X0,Y0,X1,Y1
//                   for an unattended run: the left mouse button goes down at
//                   X0,Y0 on frame 4, the pointer is at X1,Y1 from frame 6 on,
//                   and the button stays down. Fed through ImGuiIO's event
//                   queue, after the backend's own events.
//   --imgui-demo    also show ImGui::ShowDemoWindow().
//
// One line per fact is printed to stdout as `demo_sdl2: key=value`: the video
// driver, the renderer, and for opengl3 the GL_RENDERER, GL_VERSION and
// GL_SHADING_LANGUAGE_VERSION strings.
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

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "imtypeset.h"

#ifndef IMTYPESET_DEMO_FONT_DIR
#define IMTYPESET_DEMO_FONT_DIR "tests/data/fonts"
#endif

#if !SDL_VERSION_ATLEAST(2, 0, 17)
#error The SDL_Renderer backend requires SDL 2.0.17+ because of SDL_RenderGeometry()
#endif

namespace
{

struct Options {
    bool OpenGL = false;
    int Width = 1280;
    int Height = 800;
    int Frames = 0;
    const char* Screenshot = nullptr;
    bool Stay = false;
    const char* SdlRenderer = nullptr;
    float DpiScale = 0.0f;
    const char* Fonts = IMTYPESET_DEMO_FONT_DIR;
    const char* Open = nullptr;
    bool Fill = false;
    float Scroll = 0.0f;
    bool ImGuiDemo = false;
    bool Drag = false;
    float DragFrom[2] = {0.0f, 0.0f};
    float DragTo[2] = {0.0f, 0.0f};
};

int usage(const char* complaint)
{
    std::fprintf(stderr,
                 "demo_sdl2: %s\n"
                 "usage: demo_sdl2 [--backend sdlrenderer|opengl3] [--size WxH] [--fonts DIR]\n"
                 "                 [--frames N] [--screenshot out.bmp] [--stay]\n"
                 "                 [--sdl-renderer NAME] [--dpi-scale F]\n"
                 "                 [--open SECTIONS] [--fill] [--scroll Y] [--imgui-demo]\n"
                 "                 [--drag X0,Y0,X1,Y1]\n",
                 complaint);
    return 2;
}

// The few GL entry points this file calls itself, fetched from the context
// SDL made. Nothing here needs GL headers or a libGL at link time: ImGui's
// backend brings its own loader and so, in effect, does this.
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
const unsigned GL_VERSION_ = 0x1F02;
const unsigned GL_SHADING_LANGUAGE_VERSION_ = 0x8B8C;
const unsigned GL_RGB_ = 0x1907;
const unsigned GL_UNSIGNED_BYTE_ = 0x1401;
const unsigned GL_PACK_ALIGNMENT_ = 0x0D05;
const unsigned GL_BACK_ = 0x0405;

// Writes top-down RGB24 pixels as `path`, by way of `path`.part so that the
// name never refers to half a file.
bool write_bmp(const char* path, std::vector<unsigned char>& rgb, int width, int height)
{
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(rgb.data(), width, height, 24,
                                                              width * 3, SDL_PIXELFORMAT_RGB24);
    if (surface == nullptr) {
        std::fprintf(stderr, "demo_sdl2: SDL_CreateRGBSurfaceWithFormatFrom: %s\n", SDL_GetError());
        return false;
    }
    const std::string part = std::string(path) + ".part";
    const bool ok = SDL_SaveBMP(surface, part.c_str()) == 0;
    if (!ok)
        std::fprintf(stderr, "demo_sdl2: SDL_SaveBMP(%s): %s\n", part.c_str(), SDL_GetError());
    SDL_FreeSurface(surface);
    return ok;
}

bool publish_bmp(const char* path)
{
    const std::string part = std::string(path) + ".part";
    if (std::rename(part.c_str(), path) != 0) {
        std::fprintf(stderr, "demo_sdl2: cannot rename %s to %s\n", part.c_str(), path);
        return false;
    }
    return true;
}

// Everything the library needs from a host, once, after ImGui's context and
// its backends and before the first frame: the version check, a context, and
// a font file for each role the text will name.
bool setup_library(const char* font_dir)
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
    if (!IMTYPESET_CHECKVERSION() || ImTypeset::CreateContext() == nullptr) {
        std::fprintf(stderr, "demo_sdl2: imtypeset: %s\n", ImTypeset::GetLastError()->Message);
        return false;
    }
    // The first font in an atlas is ImGui's default, so ImGui's own goes in
    // first and the UI keeps it.
    ImGui::GetIO().Fonts->AddFontDefault();
    // A face that is missing is not fatal: the demo window says which roles
    // have no font, and its Fonts section can add one.
    for (int i = 0; i < IM_ARRAYSIZE(faces); ++i) {
        const std::string path = std::string(font_dir) + "/" + faces[i].File;
        if (ImTypeset::AddFontFromFileTTF(faces[i].Face, path.c_str()) == nullptr)
            std::fprintf(stderr, "demo_sdl2: imtypeset: %s\n", ImTypeset::GetLastError()->Message);
    }
    return true;
}

// For an unattended run: arranges the demo window from outside it, through
// the window's own name. A collapsing header keeps whether it is open in its
// window's storage under the ID of its label.
void arrange_demo_window(const Options& opt)
{
    static const char* const all = "Paragraph,Faces and sizes,Math,Selection,Inspector,Stress,"
                                   "Fonts,Known limits";
    ImGui::Begin("ImTypeset Demo");
    if (opt.Open != nullptr) {
        const std::string list = std::strcmp(opt.Open, "all") == 0 ? all : opt.Open;
        size_t from = 0;
        while (from <= list.size()) {
            size_t to = list.find(',', from);
            if (to == std::string::npos)
                to = list.size();
            if (to > from)
                ImGui::GetStateStorage()->SetInt(ImGui::GetID(list.substr(from, to - from).c_str()),
                                                 1);
            from = to + 1;
        }
    }
    if (opt.Fill) {
        ImGui::SetWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetWindowSize(ImGui::GetIO().DisplaySize);
    }
    if (opt.Scroll > 0.0f)
        ImGui::SetScrollY(opt.Scroll);
    ImGui::End();
}

} // namespace

int main(int argc, char** argv)
{
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        const char* value = i + 1 < argc ? argv[i + 1] : nullptr;
        if (std::strcmp(arg, "--stay") == 0) {
            opt.Stay = true;
            continue;
        }
        if (std::strcmp(arg, "--fill") == 0) {
            opt.Fill = true;
            continue;
        }
        if (std::strcmp(arg, "--imgui-demo") == 0) {
            opt.ImGuiDemo = true;
            continue;
        }
        if (value == nullptr)
            return usage((std::string("unknown option or missing value: ") + arg).c_str());
        ++i;
        if (std::strcmp(arg, "--backend") == 0) {
            if (std::strcmp(value, "opengl3") == 0)
                opt.OpenGL = true;
            else if (std::strcmp(value, "sdlrenderer") == 0)
                opt.OpenGL = false;
            else
                return usage("--backend is sdlrenderer or opengl3");
        } else if (std::strcmp(arg, "--size") == 0) {
            if (std::sscanf(value, "%dx%d", &opt.Width, &opt.Height) != 2 || opt.Width < 1 ||
                opt.Height < 1 || opt.Width > 16384 || opt.Height > 16384)
                return usage("--size is WxH");
        } else if (std::strcmp(arg, "--frames") == 0) {
            opt.Frames = std::atoi(value);
            if (opt.Frames < 0)
                return usage("--frames is 0 or more");
        } else if (std::strcmp(arg, "--screenshot") == 0) {
            opt.Screenshot = value;
        } else if (std::strcmp(arg, "--sdl-renderer") == 0) {
            opt.SdlRenderer = value;
        } else if (std::strcmp(arg, "--dpi-scale") == 0) {
            opt.DpiScale = (float)std::atof(value);
            if (opt.DpiScale < 0.0f || opt.DpiScale > 8.0f)
                return usage("--dpi-scale is between 0 and 8");
        } else if (std::strcmp(arg, "--fonts") == 0) {
            opt.Fonts = value;
        } else if (std::strcmp(arg, "--open") == 0) {
            opt.Open = value;
        } else if (std::strcmp(arg, "--scroll") == 0) {
            opt.Scroll = (float)std::atof(value);
        } else if (std::strcmp(arg, "--drag") == 0) {
            if (std::sscanf(value, "%f,%f,%f,%f", &opt.DragFrom[0], &opt.DragFrom[1],
                            &opt.DragTo[0], &opt.DragTo[1]) != 4)
                return usage("--drag is X0,Y0,X1,Y1");
            opt.Drag = true;
        } else {
            return usage((std::string("unknown option: ") + arg).c_str());
        }
    }
    if (opt.Screenshot != nullptr && opt.Frames == 0)
        return usage("--screenshot needs --frames N: the frame to photograph");

    // Setup SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "demo_sdl2: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    std::printf("demo_sdl2: video_driver=%s\n", SDL_GetCurrentVideoDriver());

    // From 2.0.18: Enable native IME.
#ifdef SDL_HINT_IME_SHOW_UI
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif
    if (opt.SdlRenderer != nullptr)
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, opt.SdlRenderer);

    // GL 3.0 + GLSL 130, the choice ImGui's example makes for desktop Linux
    // and Windows.
    const char* glsl_version = "#version 130";
    Uint32 window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (opt.OpenGL) {
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
        opt.DpiScale > 0.0f ? opt.DpiScale : ImGui_ImplSDL2_GetContentScaleForDisplay(0);
    std::printf("demo_sdl2: dpi_scale=%g\n", (double)main_scale);
    SDL_Window* window = SDL_CreateWindow(
        opt.OpenGL ? "imtypeset demo (SDL2 + OpenGL3)" : "imtypeset demo (SDL2 + SDL_Renderer)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, opt.Width, opt.Height, window_flags);
    if (window == nullptr) {
        std::fprintf(stderr, "demo_sdl2: SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Renderer* renderer = nullptr;
    SDL_GLContext gl_context = nullptr;
    GL gl;
    if (opt.OpenGL) {
        gl_context = SDL_GL_CreateContext(window);
        if (gl_context == nullptr) {
            std::fprintf(stderr, "demo_sdl2: SDL_GL_CreateContext: %s\n", SDL_GetError());
            return 1;
        }
        SDL_GL_MakeCurrent(window, gl_context);
        SDL_GL_SetSwapInterval(1); // Enable vsync
        if (!gl.load()) {
            std::fprintf(stderr, "demo_sdl2: the GL context lacks a GL 1.1 entry point\n");
            return 1;
        }
        std::printf("demo_sdl2: gl_renderer=%s\n", (const char*)gl.GetString(GL_RENDERER_));
        std::printf("demo_sdl2: gl_version=%s\n", (const char*)gl.GetString(GL_VERSION_));
        std::printf("demo_sdl2: glsl_version=%s\n",
                    (const char*)gl.GetString(GL_SHADING_LANGUAGE_VERSION_));
        std::printf("demo_sdl2: glsl_requested=%s\n", glsl_version);
    } else {
        // Accelerated where there is something to accelerate with; SDL's own
        // software renderer where there is not (the dummy video driver).
        renderer =
            SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
        if (renderer == nullptr) {
            std::printf("demo_sdl2: accelerated_renderer_refused=%s\n", SDL_GetError());
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        }
        if (renderer == nullptr) {
            std::fprintf(stderr, "demo_sdl2: SDL_CreateRenderer: %s\n", SDL_GetError());
            return 1;
        }
        SDL_RendererInfo info;
        if (SDL_GetRendererInfo(renderer, &info) == 0)
            std::printf("demo_sdl2: sdl_renderer=%s\n", info.name);
    }
    std::fflush(stdout);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // A demo that is run from anywhere leaves nothing behind.
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Setup Dear ImGui style
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    ImGui::StyleColorsDark();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;

    // Setup Platform/Renderer backends
    if (opt.OpenGL) {
        ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
        ImGui_ImplOpenGL3_Init(glsl_version);
    } else {
        ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer2_Init(renderer);
    }
    std::printf("demo_sdl2: imgui=%s renderer_backend=%s font_loader=%s imwchar_bits=%d\n",
                IMGUI_VERSION, io.BackendRendererName,
                io.Fonts->FontLoaderName ? io.Fonts->FontLoaderName : "(not chosen yet)",
                (int)sizeof(ImWchar) * 8);

    if (!setup_library(opt.Fonts))
        return 1;
    std::printf("demo_sdl2: imtypeset=%s fonts=%s\n", ImTypeset::GetVersion(), opt.Fonts);
    std::fflush(stdout);

    // Main loop
    bool show_imtypeset_demo = true;
    bool show_imgui_demo = opt.ImGuiDemo;
    bool done = false;
    bool ok = true;
    int frame = 0;
    const Uint64 started = SDL_GetPerformanceCounter();
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                done = true;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window))
                done = true;
        }
        // A scripted run has a frame to reach, so it does not wait on a window
        // manager's idea of whether the window is minimised.
        if (opt.Frames == 0 && (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) {
            SDL_Delay(10);
            continue;
        }

        // Start the Dear ImGui frame
        if (opt.OpenGL)
            ImGui_ImplOpenGL3_NewFrame();
        else
            ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        if (opt.Drag) {
            // After the backend's, so that where the real pointer is does not
            // matter to a scripted run.
            const bool arrived = frame + 1 >= 6;
            io.AddMousePosEvent(arrived ? opt.DragTo[0] : opt.DragFrom[0],
                                arrived ? opt.DragTo[1] : opt.DragFrom[1]);
            if (frame + 1 == 4)
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        }
        ImGui::NewFrame();
        ++frame;

        // The demo window places itself: 20, 20 from the viewport's corner,
        // 880 x 760.
        if (show_imtypeset_demo)
            ImTypeset::ShowDemoWindow(&show_imtypeset_demo);
        if (show_imtypeset_demo && frame == 1 &&
            (opt.Open != nullptr || opt.Fill || opt.Scroll > 0.0f))
            arrange_demo_window(opt);
        if (show_imgui_demo)
            ImGui::ShowDemoWindow(&show_imgui_demo);
        if (!opt.Fill) {
            ImGui::SetNextWindowPos(ImVec2(920.0f * main_scale, 20.0f * main_scale),
                                    ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(340.0f * main_scale, 0.0f), ImGuiCond_FirstUseEver);
            ImGui::Begin("Host");
            ImGui::TextWrapped("video driver: %s", SDL_GetCurrentVideoDriver());
            ImGui::TextWrapped("renderer backend: %s", io.BackendRendererName);
            if (opt.OpenGL) {
                ImGui::TextWrapped("GL_RENDERER: %s", (const char*)gl.GetString(GL_RENDERER_));
                ImGui::TextWrapped("GL_VERSION: %s", (const char*)gl.GetString(GL_VERSION_));
                ImGui::TextWrapped("GLSL: %s",
                                   (const char*)gl.GetString(GL_SHADING_LANGUAGE_VERSION_));
            } else {
                SDL_RendererInfo info;
                if (SDL_GetRendererInfo(renderer, &info) == 0)
                    ImGui::TextWrapped("SDL_Renderer: %s", info.name);
            }
            ImGui::TextWrapped("font loader: %s, ImWchar %d bits",
                               io.Fonts->FontLoaderName ? io.Fonts->FontLoaderName : "?",
                               (int)sizeof(ImWchar) * 8);
            ImGui::TextWrapped("%.1f frames per second", (double)io.Framerate);
            ImGui::Checkbox("ImTypeset demo window", &show_imtypeset_demo);
            ImGui::Checkbox("ImGui demo window", &show_imgui_demo);
            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        const bool shoot = opt.Screenshot != nullptr && frame == opt.Frames;
        bool shot = false;
        if (opt.OpenGL) {
            const int w = (int)io.DisplaySize.x * (int)io.DisplayFramebufferScale.x;
            const int h = (int)io.DisplaySize.y * (int)io.DisplayFramebufferScale.y;
            gl.Viewport(0, 0, w, h);
            gl.ClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w,
                          clear_color.z * clear_color.w, clear_color.w);
            gl.Clear(GL_COLOR_BUFFER_BIT_);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            if (shoot && w > 0 && h > 0) {
                // GL's rows run bottom to top; a BMP surface's run top to bottom.
                std::vector<unsigned char> up((size_t)w * (size_t)h * 3);
                std::vector<unsigned char> down(up.size());
                gl.PixelStorei(GL_PACK_ALIGNMENT_, 1);
                gl.ReadBuffer(GL_BACK_);
                gl.ReadPixels(0, 0, w, h, GL_RGB_, GL_UNSIGNED_BYTE_, up.data());
                const size_t row = (size_t)w * 3;
                for (int y = 0; y < h; ++y)
                    std::memcpy(&down[(size_t)y * row], &up[(size_t)(h - 1 - y) * row], row);
                shot = write_bmp(opt.Screenshot, down, w, h);
                ok = shot && ok;
            }
            SDL_GL_SwapWindow(window);
        } else {
            SDL_RenderSetScale(renderer, io.DisplayFramebufferScale.x,
                               io.DisplayFramebufferScale.y);
            SDL_SetRenderDrawColor(renderer, (Uint8)(clear_color.x * 255),
                                   (Uint8)(clear_color.y * 255), (Uint8)(clear_color.z * 255),
                                   (Uint8)(clear_color.w * 255));
            SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
            if (shoot) {
                // Before the present: afterwards the back buffer is undefined.
                int w = 0, h = 0;
                SDL_GetRendererOutputSize(renderer, &w, &h);
                std::vector<unsigned char> rgb((size_t)w * (size_t)h * 3);
                if (w > 0 && h > 0 &&
                    SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGB24, rgb.data(),
                                         w * 3) == 0) {
                    shot = write_bmp(opt.Screenshot, rgb, w, h);
                } else {
                    std::fprintf(stderr, "demo_sdl2: SDL_RenderReadPixels: %s\n", SDL_GetError());
                }
                ok = shot && ok;
            }
            SDL_RenderPresent(renderer);
        }
        if (shot) {
            ok = publish_bmp(opt.Screenshot) && ok;
            std::printf("demo_sdl2: screenshot=%s frame=%d\n", opt.Screenshot, frame);
            std::fflush(stdout);
        }
        if (opt.Frames > 0 && frame >= opt.Frames && !opt.Stay)
            done = true;
    }
    const double seconds =
        (double)(SDL_GetPerformanceCounter() - started) / (double)SDL_GetPerformanceFrequency();
    std::printf("demo_sdl2: frames=%d seconds=%.3f vertices_last_frame=%d\n", frame, seconds,
                ImGui::GetDrawData() ? ImGui::GetDrawData()->TotalVtxCount : 0);

    // Cleanup. The library's context goes before ImGui's.
    ImTypeset::DestroyContext();
    if (opt.OpenGL)
        ImGui_ImplOpenGL3_Shutdown();
    else
        ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    if (opt.OpenGL)
        SDL_GL_DeleteContext(gl_context);
    else
        SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return ok ? 0 : 1;
}
