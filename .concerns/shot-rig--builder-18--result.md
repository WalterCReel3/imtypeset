# shot-rig result (builder-18, 2026-10-09)

Everything below was run in this session; nothing is inferred. `src/` was not touched.

## Which display route works in THIS session

| route | result |
|---|---|
| (a) `DISPLAY=:1` as set | REFUSED. `xdpyinfo:  unable to open display ":1".` The cause, asked directly: `socket(AF_UNIX).connect("/tmp/.X11-unix/X1")` and the abstract name both raise `PermissionError [Errno 1] Operation not permitted`. The socket file is there (`srwxr-xr-x wreel X1`); the sandbox refuses the socket family. |
| Wayland (`WAYLAND_DISPLAY=wayland-0`) | REFUSED. `SDL_VIDEODRIVER=wayland` -> `SDL_Init: wayland not available`; `SDL_VIDEODRIVER=x11` (on :1) -> `SDL_Init: x11 not available`. |
| (b) Xvfb in a user namespace over TCP | WORKS, two ways. `xvfb-netns` (`unshare -rmn`, `ip link set lo up`, tmpfs over /tmp, `Xvfb :99 -listen tcp -nolisten unix`, `DISPLAY=127.0.0.1:99`): the route `tools/shot.sh` takes. `xvfb-tcp` (the reference's recipe, `unshare -rm`, host's TCP 6099): also works, forced with `--route xvfb-tcp`. No policy refusal on `unshare`, `mount -t tmpfs tmpfs /tmp` or `ip link`. |
| SDL `dummy` / `offscreen` drivers | WORK, no X at all. |

The netns form is my addition to the reference's mechanism: `-listen tcp` without it binds the
host's port 6000+N on every interface for the life of the run and two concurrent runs collide on
the number; inside a private network namespace neither is true. The plain form is the fallback.

TRAP for task `demo`: with no `SDL_VIDEODRIVER` set and both displays refused, SDL2 does NOT fail —
it falls through to `offscreen` and the program "runs" with no window (`demo_sdl2: video_driver=offscreen`).
A run that is supposed to be on a real window must check the `video_driver=` line (x11), not the exit status.

GL: came up on Xvfb. `GL_RENDERER=llvmpipe (LLVM 15.0.6, 256 bits)`, `GL_VERSION=4.5 (Compatibility
Profile) Mesa 22.3.6`, `GL_SHADING_LANGUAGE_VERSION=4.50`; the host asks for GL 3.0 and passes
`#version 130` to ImGui's backend, as ImGui's example does on Linux. GL also comes up under
`SDL_VIDEODRIVER=offscreen` (EGL surfaceless, same llvmpipe) with the opengl3 backend, and its
image is pixel-identical to the Xvfb one — an X-less route to the GL backend.

## What was built

- `tools/shot.sh` — `tools/shot.sh [--route auto|display|xvfb|xvfb-netns|xvfb-tcp] [--screen WxHxD] [--probe] [--] cmd...`.
  Tries DISPLAY (xdpyinfo), then xvfb-netns, then xvfb-tcp; prints `shot.sh: route=<...> DISPLAY=<...>`
  and the earlier route's error on stderr; exit 3 and every route's error if none; else the command's
  status. Inside the Xvfb routes /tmp is a private tmpfs (TMPDIR=/tmp), so outputs go to a repo path.
  `tools/shot.sh --probe` takes 0.43 s here.
- `examples/demo_sdl2/main.cpp` — one binary, `--backend sdlrenderer|opengl3 --size WxH --frames N
  --screenshot out.bmp`, plus `--stay`, `--sdl-renderer NAME`, `--dpi-scale F`, `--page demo|paragraph`.
  From ImGui v1.92.9's two SDL2 examples, MIT notice kept in the file header. Screenshot is
  `SDL_RenderReadPixels` / `glReadPixels` of the back buffer on frame N, written to `out.bmp.part`
  and renamed after the present. GL entry points via `SDL_GL_GetProcAddress`: no GL headers, no -lGL.
- `examples/demo_sdl2/CMakeLists.txt` — standalone. `find_package(SDL2 CONFIG)` when
  IMTYPESET_SDL2_DIR is empty; otherwise that directory as an install prefix or as example-project's
  build tree (which has NO SDL2Config.cmake: headers are `_deps/sdl2-build/include/SDL2` +
  `_deps/sdl2-build/include-config-debug/SDL2`, library `lib/libSDL2d.a`). Options
  IMTYPESET_DEMO_WCHAR32 (ON), IMTYPESET_DEMO_FREETYPE (ON when found), IMTYPESET_DEMO_WITH_LIBRARY (OFF).
  With the last ON it defines the `imgui` target itself and `add_subdirectory`s the repo root, which
  leaves an existing `imgui` alone and, not being top level, brings no tests.
- `examples/demo_sdl2/grab.sh` — (not named in the board's `touches`, inside `examples/demo_sdl2/*`
  which the launch message gave me) runs the host with `--stay`, waits for the BMP, grabs the window
  by id with `xwd` and `import`, and counts pixels differing from the read-back.

Link line for the static SDL2 (from build/shot-rig/build.ninja):
`libdemo_sdl2_backends.a libimgui.a /usr/lib/x86_64-linux-gnu/libfreetype.so .../example-project/build/desktop-debug/lib/libSDL2d.a -lm -ldl`
(Threads::Threads is requested and is empty on this glibc.) Nothing else: this SDL2 dlopens
libX11, libwayland-client, libGL/EGL, ALSA, udev at run time (`SDL_VIDEO_DRIVER_X11_DYNAMIC "libX11.so.6"` etc.).

## Commands and what came back

```
cmake -S examples/demo_sdl2 -B build/shot-rig -G Ninja -DIMTYPESET_SDL2_DIR=../example-project/build/desktop-debug -DIMTYPESET_IMGUI_DIR=.deps/imgui   # 0.8 s
cmake --build build/shot-rig                                                                         # 15.3 s, 13 edges, no warnings
SDL_VIDEODRIVER=dummy     build/shot-rig/demo_sdl2 --backend sdlrenderer --size 1220x720 --dpi-scale 1 --frames 30 --screenshot X.bmp
   -> accelerated_renderer_refused=Couldn't find matching render driver; sdl_renderer=software; 30 frames 0.19 s (0.27 s wall)
SDL_VIDEODRIVER=offscreen (same)   -> sdl_renderer=opengl; 0.34 s (0.51 s wall)
SDL_VIDEODRIVER=offscreen ... --backend opengl3   -> llvmpipe, GL 4.5, GLSL 4.50; 0.22 s
tools/shot.sh -- examples/demo_sdl2/grab.sh build/shot-rig/demo_sdl2 OUT --backend sdlrenderer --size 1220x720 --dpi-scale 1
   -> route=xvfb-netns, video_driver=x11, sdl_renderer=opengl; xwd 0 px, import 0 px differ from read-back; 2.1 s wall
tools/shot.sh -- examples/demo_sdl2/grab.sh ... --backend opengl3 ...
   -> video_driver=x11, llvmpipe; xwd 0 px, import 0 px differ from read-back; 1.6 s wall
tools/shot.sh -- examples/demo_sdl2/grab.sh ... --backend sdlrenderer --sdl-renderer software ...   -> same, 0 px / 0 px
```
Also built and run for 5 frames on Xvfb/opengl3: `-DIMTYPESET_DEMO_WCHAR32=OFF -DIMTYPESET_DEMO_FREETYPE=OFF`
(build/shot-rig-stb16; `imwchar_bits=16`), without the library.

What the X server shows equals what the renderer says it drew, exactly, for all three renderers
(xwd and import against `--screenshot`: 0 differing pixels each time).

## Three rasterisers, same ImGui demo frame (demo-window region 570x720, default bitmap font)

- SDL software (dummy) vs SDL software on X: 0 px.
- SDL_Renderer/GL on X vs offscreen: 0 px. opengl3 on X vs offscreen: 0 px.
- SDL_Renderer/GL vs opengl3: 36400 px, all by 1/255, all the clear colour: the example's
  `(Uint8)(0.45f*255)` truncates to 114 where `glClearColor` rounds to 115. 0 px beyond that.
- SDL software vs either GL: 352334 px by 1/255 (blend rounding, e.g. window bg 20 vs 21), and
  3887 px by more than 5%, ALL on anti-aliased geometry: the window border (drawn visibly heavier),
  the tree-node arrows, the close cross, the resize grip. No text pixel is among them.
  So: SDL's software renderer is not a faithful reference for ImGui's AA lines/fringes; it is for glyph quads.

## Step 4: the library through a GPU-style backend (the tree built at the moment I tried)

```
cmake -S examples/demo_sdl2 -B build/shot-rig-lib -G Ninja -DIMTYPESET_SDL2_DIR=... -DIMTYPESET_IMGUI_DIR=.deps/imgui -DIMTYPESET_DEMO_WITH_LIBRARY=ON
cmake --build build/shot-rig-lib      # 24.6 s, 29 edges, built clean (src/ as of ~22:18)
tools/shot.sh -- examples/demo_sdl2/grab.sh build/shot-rig-lib/demo_sdl2 OUT --backend opengl3 --size 814x330 --dpi-scale 1 --page paragraph
```
`--page paragraph` is examples/specimen.cpp's `page_paragraph` copied statement for statement (same
style, fonts, window, 814x330), so the draw data is the one behind docs/images/paragraph.png.
Result, opengl3 on Xvfb (xwd grab) against docs/images/paragraph.png (soft raster), 268620 px:

- 1651 px differ by exactly 1/255 (glyph edge rounding), 16 px differ by more, 0 anywhere in text.
- The 16: the end caps of the four red margin hairlines (`AddLine` at x+0.5). The soft raster paints
  row y=27 (top) at ~31% red and leaves the bottom row white; GL leaves the top row white and paints
  the bottom row (y=224 / y=246). Columns x=281,282,532,533,551,552,802,803. A one-row disagreement
  about where an AA line starts and ends — a property of tests/support/soft_raster.h, not of the library.
- SDL_Renderer/GL on Xvfb: identical to opengl3 (0 px). SDL software (dummy): text within 1/255
  (559 px), ~2480 px on the hairlines (drawn 2 px wide, opaque) — same AA story as above.

So the library's text through llvmpipe with ImGui's stock GL3 backend and bilinear sampling is, to
1/255, the picture the soft raster drew: glyphs land on whole pixels and bilinear sampling has nothing to blur.
Caveat: docs/images/paragraph.png was produced by an earlier build of src/; I did not regenerate it
from the tree I linked. The agreement says they did not diverge on this page.

## Images (looked at with Read)

- docs/images/rig-demo-opengl3-xvfb.png (1220x720, xwd of the X window): grey-blue clear colour; a
  "Host" window top-left reading video driver x11, imgui_impl_opengl3, GL_RENDERER llvmpipe, GL_VERSION
  4.5, GLSL 4.50, FreeType, ImWchar 32 bits, a ticked checkbox; "Dear ImGui Demo" window at x=650 with
  menu bar, "dear imgui says hello! (1.92.9) (19290)" and eight collapsed headers. Crisp, nothing missing.
- docs/images/rig-demo-sdlrenderer-dummy.png (1220x720, SDL software, no display): same frame;
  the windows' borders are visibly heavier than in the GL image.
- docs/images/rig-paragraph-opengl3-xvfb.png (814x330, xwd, opengl3): three columns on white —
  ImGui::TextWrapped, ImTypeset::Text ragged, ImTypeset::Text justified, red margin guides; text sharp,
  justified column flush on both guides.

## Decisions I took (unattended)

- Private net namespace first, reference's shared-port recipe second (reason above).
- ImGui's own `imgui_demo.cpp` is compiled into the host's `imgui` target (the root build's target
  omits it); `demo` will need the same when it folds this in.
- `--dpi-scale 1` in every shot for reproducibility; default 0 asks SDL as ImGui's examples do (Xvfb answers 1).
- No scripted input yet (no SDL events, no `io.Add*Event`): nothing in this task needed a click.

## Not verified

- Route (a) on a live display: `DISPLAY=:1` never answered here, so `route=display` was exercised
  only as far as its failure path.
- A GPU. Everything GL is llvmpipe.
- HiDPI (`DisplayFramebufferScale` != 1): the GL read-back multiplies size by scale but no display here has one.
- The library on wchar16/stb through this host (the stb16 host was built without the library).
- `find_package(SDL2 CONFIG)` branch: no system SDL2 dev package here, so only the IMTYPESET_SDL2_DIR branch ran.
- THIRD-PARTY.md has no line for examples/demo_sdl2/main.cpp (derived from ImGui's examples, MIT);
  that file is docs-hygiene's — told builder-20.
- clang-format was applied to main.cpp; nothing here is in the tree-wide gate (standalone CMake).
