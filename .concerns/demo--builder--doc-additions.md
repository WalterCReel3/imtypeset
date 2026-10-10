# demo — text for files this task did not edit (for `api-document` / `fold-docs`)

Until these land, hosts and tests include `examples/imtypeset_demo.h`, which holds the one
declaration. When the first item lands: delete `examples/imtypeset_demo.h`, drop its `#include` from
`examples/demo_sdl2/main.cpp`, `examples/demo_headless.cpp`, `tests/test_demo.cpp`, drop the
`target_include_directories(demo_sdl2 PRIVATE "${_demo_repo}/examples")` line from
`examples/demo_sdl2/CMakeLists.txt`, and delete the forward declaration at the top of
`src/imtypeset_demo.cpp` (the `namespace ImTypeset { IMTYPESET_API void ShowDemoWindow(bool* p_open); }`
block).

## 1. `include/imtypeset.h` — one declaration

At the end of `namespace ImTypeset`, after the "Offsets for bindings" block:

```cpp
// --- Demo ----------------------------------------------------------------------------
//
// A window that shows what the library does, section by section, in the fonts
// the host has added; its source, imtypeset_demo.cpp, is where to look up how
// each thing is called. Call it between ImGui::NewFrame() and ImGui::Render(),
// as ImGui::ShowDemoWindow() is. `p_open`, if given, puts a close button on
// it. With no current context it says so. Compiled with
// IMTYPESET_DISABLE_DEMO it is still there to link against and draws nothing.
IMTYPESET_API void ShowDemoWindow(bool* p_open = NULL);
```

`tests/test_api.cpp`'s scan of the header for standard-library names is unaffected (`bool*`, `NULL`).

## 2. `include/imtypeset_c.h` — its C counterpart

```c
/* ImTypeset::ShowDemoWindow(). `p_open` may be null; otherwise it points at an
 * int read as a bool and written 0 when the window's close button is pressed.
 * Returns 1, or 0 with the error set if a C++ exception was caught. */
IMTYPESET_C_API int ImTypeset_ShowDemoWindow(int* p_open);
```

## 3. `src/imtypeset_c.cpp`

```cpp
int ImTypeset_ShowDemoWindow(int* p_open)
{
    return guarded(0, [&]() -> int {
        bool open = p_open == nullptr || *p_open != 0;
        ImTypeset::ShowDemoWindow(p_open != nullptr ? &open : nullptr);
        if (p_open != nullptr)
            *p_open = open ? 1 : 0;
        return 1;
    });
}
```

(Not compiled by me: `src/imtypeset_c.cpp` is another task's. If `int*` for a bool out-parameter is
not that file's convention, follow the file.)

## 4. `tests/test_c_api_map.cpp` — one row in `TABLE`

```cpp
    BOTH(ShowDemoWindow),
```

Its scan fails the build on a C++ declaration with no row, so 1, 2, 3 and 4 land together.
`python/imtypeset` then wants `show_demo_window(open: bool | None) -> bool | None`.

## 5. `THIRD-PARTY.md`

Table row, replacing `| SDL2 | zlib | planned optional demo host; nothing uses it yet | no |`:

```
| SDL2 | zlib | linked by the optional demo host `examples/demo_sdl2` only; found on the machine | no |
```

Section `## SDL2`, replacing its body:

```
zlib licence (`/usr/share/doc/libsdl2-2.0-0/copyright`, SDL 2 as Debian
packages it: "zlib/libpng" for the library's own files). Used by one program,
`examples/demo_sdl2`, the windowed host of the demo window: a build-time
dependency of that host only, found on the machine (`find_package(SDL2)` or
`IMTYPESET_SDL2_DIR`) and never copied in. The library does not depend on it,
and no SDL2 source or header is in this repository. The host is built only when
asked for (its own project, or `-DIMTYPESET_BUILD_DEMO_SDL2=ON`).

`examples/demo_sdl2/main.cpp` is derived from Dear ImGui's
`examples/example_sdl2_sdlrenderer2/main.cpp` and
`examples/example_sdl2_opengl3/main.cpp` (v1.92.9, MIT) and carries Dear
ImGui's notice at its head. The host also compiles Dear ImGui's
`backends/imgui_impl_sdl2.cpp`, `imgui_impl_sdlrenderer2.cpp`,
`imgui_impl_opengl3.cpp` and `imgui_demo.cpp` from the Dear ImGui checkout,
under Dear ImGui's licence above; none of them is copied in.
```

## 6. `docs/decisions.md` — one entry (next free D-number)

```
## Dnn. The demo window is part of the library, and adds no font

**Decided.** `ImTypeset::ShowDemoWindow(bool* p_open)` is `src/imtypeset_demo.cpp`, a translation
unit of the library as `imgui_demo.cpp` is of Dear ImGui, using only `imgui.h` and `imtypeset.h`;
`IMTYPESET_DISABLE_DEMO` compiles it to an empty function. It shows the faces the host added and
adds none itself (its Fonts section can add one from a path the user types): which files play
which role is the host's decision, and a demo that loaded fonts would need paths the library has no
business knowing.

**Decided.** The demo keeps four `ImTypesetParagraph*` between frames and drops them when the
current context is another one, or one whose `ImTypesetStats::TextBuilds` is lower than last frame
saw (a new context at the old address). That second test leans on a debug counter because the
public API has no way to ask whether a handle's context is still alive.

**Decided.** The Stress section submits only the copies of its document that are in view (heights
from `CalcTextSize`, measured when the width, size or fonts change), so x1000 costs what is on
screen. The "submit everything" mode is held to 20 copies.

**Measured** (2026-10-10, FreeType 2.12.1, 32-bit ImWchar, debug): `test_demo` 8 tests / 84 checks,
2.7 s, 109 MB peak resident including 400 frames of scrolling through 6,000 paragraphs; the widget
cache held about 33 KiB per laid-out paragraph of some 300 bytes.
```

## 7. `CLAUDE.md` / README (task `docs`)

How to run: `.concerns/demo--builder--result.md`, "How to run". The Layout table wants
`src/imtypeset_demo.cpp` (the demo window) and `examples/demo_sdl2/` (its windowed host, a project of
its own; `tools/shot.sh` photographs it).
