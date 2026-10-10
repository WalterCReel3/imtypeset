# How to run the native demo (milestone 1, builder, 2026-10-10)

## The window, at a real display

From the repository root, in an ordinary shell (not this sandbox: it cannot reach DISPLAY=:1):

```sh
cmake -S examples/demo_sdl2 -B build/demo -G Ninja \
    -DIMTYPESET_SDL2_DIR=../example-project/build/desktop-debug
cmake --build build/demo -j4
build/demo/demo_sdl2
```

- Needs: CMake >= 3.21, Ninja, g++, `.deps/imgui` (in the tree), the SDL2 that
  `../example-project/build/desktop-debug` already holds built (`lib/libSDL2d.a`), FreeType if
  installed. No network. First build is 41 edges (ImGui, three backends, the whole library, the host).
- `IMTYPESET_SDL2_DIR` is not needed where `find_package(SDL2 CONFIG)` finds an SDL2 (not tried: no
  system SDL2 dev package on this machine).
- It opens a 1280x800 window titled "imtypeset demo (SDL2 + SDL_Renderer)" holding the
  **ImTypeset Demo** window and a small **Host** window (driver, renderer, frames per second, and
  checkboxes for the ImTypeset and the ImGui demo windows). Close the OS window to quit.
- `build/demo/demo_sdl2 --backend opengl3` uses ImGui's OpenGL 3 backend instead of SDL_Renderer.
- Fonts are read from `tests/data/fonts` of the tree it was built from (`--fonts DIR` for another).
- Check the first line it prints: `demo_sdl2: video_driver=x11` (or `wayland`). `offscreen` means SDL
  found no display and there is no window, although the program runs.

## What I ran here (all in this session, every binary under `ulimit -v 6000000; timeout 600`)

`build/demo` did not exist before the configure line above; those two commands are the ones I ran
(build clean, no warnings from our sources). Then:

```sh
# no display at all: SDL's dummy video driver + software renderer, 10 frames, read-back to BMP
SDL_VIDEODRIVER=dummy build/demo/demo_sdl2 --size 1280x1000 --dpi-scale 1 \
    --open "Paragraph,Faces and sizes" --fill --frames 10 --screenshot out.bmp          # exit 0
SDL_VIDEODRIVER=dummy build/demo/demo_sdl2 --size 1000x1500 --dpi-scale 1 --open Math --fill \
    --frames 10 --screenshot out.bmp                                                    # exit 0
# a scripted drag through io.AddMouse*Event: SelectableText selects bytes [74, 321)
SDL_VIDEODRIVER=dummy build/demo/demo_sdl2 --size 1000x700 --dpi-scale 1 --open Selection --fill \
    --frames 12 --drag 120,238,300,305 --screenshot out.bmp                             # exit 0
# SDL's offscreen driver (EGL surfaceless, SDL_Renderer on GL)
SDL_VIDEODRIVER=offscreen build/demo/demo_sdl2 --size 1280x800 --dpi-scale 1 \
    --open "Paragraph,Math" --frames 30 --screenshot out.bmp                            # exit 0
# a REAL X window: Xvfb in a private namespace (tools/shot.sh), photographed by xwd and import
tools/shot.sh -- examples/demo_sdl2/grab.sh build/demo/demo_sdl2 OUT --backend opengl3 \
    --size 1280x800 --dpi-scale 1 --open "Paragraph,Math"   # video_driver=x11, llvmpipe GL 4.5
tools/shot.sh -- examples/demo_sdl2/grab.sh build/demo/demo_sdl2 OUT --backend sdlrenderer \
    --size 1280x800 --dpi-scale 1 --open "Paragraph,Math"   # video_driver=x11
```

I looked at every image. `docs/images/demo-sdl2-opengl3-xvfb.png` is the xwd grab of the X window
from the opengl3 run (to be re-shot later). Not run: a real display (refused in this sandbox, as
shot-rig found), a GPU, HiDPI, the stb and 16-bit ImWchar builds of this host, any sanitiser.

Unattended-run options added to the host: `--open SECTIONS|all`, `--fill`, `--scroll Y`,
`--drag X0,Y0,X1,Y1`, `--fonts DIR`, `--imgui-demo`; `--frames N --screenshot F --stay`,
`--backend`, `--size`, `--dpi-scale`, `--sdl-renderer` are shot-rig's.

## Without a window

```sh
cmake -S . -B build/demo-dev -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DIMTYPESET_TESTS=test_demo -DIMTYPESET_BUILD_BENCH=OFF -DIMTYPESET_EXAMPLES=demo_headless
cmake --build build/demo-dev -j4
( ulimit -v 6000000; timeout 600 build/demo-dev/tests/test_demo )                      # 8 tests, 84 checks
( ulimit -v 6000000; timeout 600 build/demo-dev/examples/example_demo_headless DIR )   # DIR/demo-<section>.png
```

## State at the end of the task

All eight sections are written, run and looked at; see `.concerns/demo--builder--result.md` for what
was run in which configuration, the defects seen in the images, and the API gaps. `build/demo*` was
removed when the task finished: the commands above build from nothing (the host: 32 edges, 23 s
here at -j4).
