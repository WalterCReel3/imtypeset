# Lead correction for task `demo`: a windowed host CAN be built here (user pointed this out)

The `demo` board spec says no GLFW or SDL is available and asks for an unbuilt GLFW host. That is
WRONG and item 3 of that spec is replaced by this file. Verified by the lead on 2026-10-09:

- `../example-project/build/desktop-debug/` holds SDL2 `release-2.32.10`, fetched and BUILT:
  source `_deps/sdl2-src`, build tree `_deps/sdl2-build` (headers under
  `_deps/sdl2-build/include` and `include-config-debug/SDL2/SDL_config.h`), static libraries
  `lib/libSDL2d.a` and `lib/libSDL2maind.a`. Its SDL_config.h enables the X11, Wayland, DUMMY and
  OFFSCREEN video drivers and the OpenGL renderer. `../example-project/cmake/Dependencies.cmake` shows
  how that project consumes it; read it for the link line (static SDL2 needs its system libraries).
- `../imgui` is a checkout of Dear ImGui master, `1.93.0 WIP` (IMGUI_VERSION_NUM 19298), with
  `backends/imgui_impl_sdl2.*`, `imgui_impl_sdlrenderer2.*`, `imgui_impl_opengl3.*` and
  `examples/example_sdl2_sdlrenderer2/main.cpp`, `examples/example_sdl2_opengl3/main.cpp`,
  `examples/example_null/main.cpp`. The pinned v1.92.9 in `.deps/imgui` has the same backends.
- `../hello_imgui` is a checkout too (see docs/distribution.md once task `dist` writes it).
- This session may READ those trees and link against them; it may not write into them. `DISPLAY=:1`
  and `WAYLAND_DISPLAY=wayland-0` are set in the environment; whether the sandbox lets a process reach
  either display is NOT known — try, and report what happened.

So, replacing item 3:
3. `examples/demo_sdl2/main.cpp`: a real host — SDL2 + ImGui's SDL2 platform backend + the
   SDL_Renderer backend (`imgui_impl_sdlrenderer2`; it needs no GL and is what example-project's own
   software path uses) — written from ImGui's `examples/example_sdl2_sdlrenderer2/main.cpp` in the
   pinned source (MIT: keep its notice, add the THIRD-PARTY.md line). Built when SDL2 is found:
   `find_package(SDL2 CONFIG QUIET)` first, then a cache variable `IMTYPESET_SDL2_DIR` naming an SDL2
   build tree, so that here `-DIMTYPESET_SDL2_DIR=../example-project/build/desktop-debug` (or pointing
   CMAKE_PREFIX_PATH at its `_deps/sdl2-build`, whichever that tree's SDL2Config.cmake supports — look)
   works and elsewhere a system SDL2 does. It calls `ImTypeset::ShowDemoWindow()` and
   `ImGui::ShowDemoWindow()`, takes `--frames N --screenshot out.bmp` (as example-project's skratch
   does) so it can run unattended.
   BUILD IT AND RUN IT: (a) `SDL_VIDEODRIVER=dummy` (or `offscreen`) with the software renderer,
   `--frames 30 --screenshot` — read the pixels back (`SDL_RenderReadPixels`), convert, LOOK at the
   image, and compare it with the soft-raster capture of the same frame from `demo_headless` (same
   draw data through two independent rasterisers: report where they differ and why); (b) on the real
   display if the sandbox allows, for a few frames with a screenshot; if it does not, say exactly what
   error came back. A GLFW host is NOT needed.
   Also add the same host for the 16-bit ImWchar and stb configurations to the build so the demo is
   seen working without IMGUI_USE_WCHAR32 and without FreeType.

## Screenshots of a REAL window are doable (user, 2026-10-09) — and the reference has already worked out how

Do not stop at the dummy video driver. The reference records, as measured facts about this machine
(`../imgui-adv-text/Makefile` lines ~55-100, `scripts/shot.py`, `scripts/shot.sh`,
`scripts/mouse_probe.py::xvfb_command`, `tests/conftest.py` fixture `window`):

- In some sessions `DISPLAY=:1` is live and a real window opens in about half a second.
- In sandboxed sessions `DISPLAY=:1` is set but unreachable (`socket(AF_UNIX)` raises
  PermissionError). What opens a window there is an **Xvfb in a user namespace listening on TCP**
  (`unshare -r -m`, a private tmpfs over /tmp, `-listen tcp -nolisten unix`, `DISPLAY=127.0.0.1:NN`),
  with Mesa llvmpipe for GL. `Xvfb`, `xvfb-run`, `xwd` and ImageMagick `import` are installed
  (lead checked `which`). Which of the two this session is has NOT been checked: ASK the machine
  (try to connect) rather than assume, exactly as the reference's conftest does.

So the demo host is to be photographed three ways, in this order of preference, and the report says
which worked and what each error was when one did not:
1. On a real X server — `DISPLAY=:1` if it answers, otherwise the Xvfb-over-TCP recipe (copy the
   mechanism from the reference's scripts into a script of ours, e.g. `tools/shot.sh`; do not call
   into `../imgui-adv-text`). Grab with the host's own `--screenshot` (SDL_RenderReadPixels) AND with
   `xwd`/`import` of the window, so what the X server shows is compared with what the renderer says.
   Use the OpenGL3 backend here too if a GL context comes up under llvmpipe (`example_sdl2_opengl3`),
   because that is the path most hosts ship; the SDL_Renderer backend otherwise.
2. SDL `offscreen`/`dummy` video driver with the software renderer and `--screenshot`.
3. The soft-raster capture from `demo_headless`.
Compare 1, 2 and 3 for the same scripted frame: same draw data, three rasterisers. Differences are
findings (which rasteriser, where, how many pixels), not noise to be tolerated silently.
Scripted input for the real-window run goes through SDL events or `io.Add*Event` — your choice, say
which. `tools/shot.sh` and the screenshots under `docs/images/` are yours for this task.
