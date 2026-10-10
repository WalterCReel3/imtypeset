# Distribution notes

Groundwork for task `dist`, which turns this into `docs/distribution.md` and
implements it. Written by `dist-research` (builder-19), 2026-10-09.

Three kinds of statement are kept apart: **read** (a file was opened; the
reference follows), **run** (a command was executed here; the log is named) and
**reasoned** (neither; say so). Paths beginning `imgui/` and `hello_imgui/` are
the sibling checkouts `../imgui` (commit `2859f672`, `IMGUI_VERSION_NUM 19298`,
`imgui/imgui.h:32-33`, master: no `IMGUI_HAS_DOCK` in `imgui.h`) and
`../hello_imgui` (commit `39893731`). `pin/` is `.deps/imgui`, v1.92.9.

Everything under "The 1.93 run" was done on a **snapshot** of the tree
(`build/dist-research/snap`, taken 2026-10-09 22:12 EDT while task `stress` was
writing `src/`), not on the tree. Logs are in `build/dist-research/*.log`.

---

## 1. What Dear ImGui does (read)

**No build system for the library, on purpose.** "We don't use nor mandate a
build system for the main library ... providing a build system for the main
library would be of little-value" (`imgui/imgui.cpp:236-239`). "No specific
build process is required: you can add all files into your existing project"
(`imgui/docs/README.md:42`). The integration instruction is "Add the Dear ImGui
source files + selected backend source files to your projects", built and
statically linked, "NOT as a shared library" (`imgui/imgui.cpp:267-268`).
There is no `CMakeLists.txt` in the root; the only ones are in five examples
(`examples/example_{glfw_vulkan,glfw_wgpu,sdl2_wgpu,sdl3_wgpu,android_opengl3}`),
and `docs/EXAMPLES.md:251-252` points CMake users at two unmerged pull requests.

**The file set is flat and prefixed.** Root: `imgui.h`, `imgui_internal.h`,
`imconfig.h`, `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`,
`imgui_widgets.cpp`, `imgui_demo.cpp`, three `imstb_*.h`. Every file name
carries the `imgui`/`imstb` prefix, so the files can be dropped into a host's
own source directory.

**Opt-in parts are extra files plus a define.** FreeType: add
`misc/freetype/imgui_freetype.{h,cpp}`, define `IMGUI_ENABLE_FREETYPE`
(`imgui/misc/freetype/README.md:8-10`, `imgui/imconfig.h:89-93`,
`imgui/docs/FONTS.md:392`). Backends: one `imgui_impl_XXXX.cpp/.h` pair each
(`imgui/docs/BACKENDS.md:54-55`).

**Examples carry their own per-example build files**: Makefiles, `.bat`,
`.vcxproj`, Xcode projects (`imgui/docs/EXAMPLES.md:240-249`). A Makefile lists
the sources by hand: `imgui/examples/example_null/Makefile:14-16`, FreeType by
`WITH_FREETYPE=1` and `pkg-config` (`:29-35`). `example_null` is the headless
one; in 1.93 it sits on `backends/imgui_impl_null.{h,cpp}`
(`imgui/examples/example_null/main.cpp:7-9`). **The pin has no
`imgui_impl_null`** (`ls pin/backends | grep null` is empty), so a headless
example written against it is a 1.93-only example.

**An amalgamation exists and is discouraged.** `misc/single_file/imgui_single_file.h`
is a 29-line stub that `#include`s the five `.cpp` files under
`IMGUI_IMPLEMENTATION` (`:20-28`). Its own header says "We use this to validate
compiling all *.cpp files in a same compilation unit" (`:2-3`), and
`imgui/misc/README.txt:19-23` adds "we don't really recommend you use this in
your projects". What ImGui promises is that a **unity build works**, not that
one file is the distribution.

**Configuration is compile-time and must agree everywhere.**
- `imconfig.h` is edited in place, or replaced by
  `#define IMGUI_USER_CONFIG "file.h"` from the build system
  (`imgui/imconfig.h:6-7`, `imgui/imgui.h:64-66`).
- "configuration settings [must be] defined consistently _everywhere_ Dear
  ImGui is used ... some compile-time options have an affect on data
  structures", with `IMGUI_CHECKVERSION()` as the check
  (`imgui/imconfig.h:9-12`, `imgui/imgui.h:104`).
- `IMGUI_API` decorates every declaration and defaults to empty
  (`imgui/imgui.h:83-90`, `imgui/imconfig.h:23-29`).
- `IMGUI_USE_WCHAR32` is **off by default**; `ImWchar` is `unsigned short`
  without it (`imgui/imconfig.h:72-73`, `imgui/imgui.h:281-287`).
- Options that change what imtypeset touches: `ImDrawIdx` may be redefined to
  `unsigned int` (`imgui/imconfig.h:125-129`); `IMGUI_USE_BGRA_PACKED_COLOR`
  (`:66-67`); `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` (`:31-32`), which
  `imgui/imgui.cpp:258` recommends for finding old API use.

**On third-party extensions the repository says almost nothing.** Every mention
found is a link to the wiki: `imgui/imgui.h:16-17`, `imgui/docs/README.md:19`,
`:124`, `:153-156` (which names ImPlot, ImPlot3D and the Test Engine as
"notable and well supported"), `imgui/docs/CONTRIBUTING.md:14`. **No file in
`imgui/docs/` states how an extension should be packaged or consumed.** The
only normative hints are indirect: `imgui/imgui.cpp:228-235` (C-ish code, no
C++ headers, `ImGui::` is a namespace, to "facilitate use from other
languages"), `imgui/imconfig.h:149` ("You can add extra functions within the
ImGui:: namespace from anywhere"), and `imgui/docs/README.md:151` (bindings are
generated by cimgui / dear_bindings from the headers). The wiki page itself was
not read (it is not in the checkout).

## 2. What hello_imgui does (read)

**It is a CMake project that builds ImGui for the application.** Options:
platform backends `HELLOIMGUI_USE_GLFW3` / `_SDL2`
(`hello_imgui/CMakeLists.txt:84-85`), renderers `HELLOIMGUI_HAS_OPENGL3` /
`_METAL` / `_VULKAN` / `_DIRECTX11` / `_DIRECTX12` (`:87-92`), and
`HELLOIMGUI_HEADLESS` for the null rendering/platform backends only (`:94-98`;
`src/hello_imgui/internal/backend_impls/null_config.h`, `rendering_null.cpp`).
With nothing chosen it picks GLFW + OpenGL3 if `find_package(glfw3)` succeeds
(`hello_imgui_cmake/hello_imgui_build_lib.cmake:673-710`).

**Dependencies: target, then `find_package`, then download.** GLFW, SDL2 and
FreeType are fetched only if no target of that name exists and `find_package`
fails, and the download switches default **OFF on Linux** (`CMakeLists.txt:160-172`).
A vcpkg toolchain file forbids fetching altogether
(`hello_imgui_build_lib.cmake:658-671`).

**Where ImGui comes from.** `HELLOIMGUI_IMGUI_SOURCE_DIR`, default the submodule
`external/imgui` (`CMakeLists.txt:196-199`), built when `HELLOIMGUI_BUILD_IMGUI`
is on (`:201-203`); or an installed CMake package under
`HELLOIMGUI_USE_IMGUI_CMAKE_PACKAGE` (`:327`), described as "*not* recommended"
because "ImGui voluntarily chose to not provide cmake tooling" (`:308-315`). The
submodule is the **docking** branch (`:310`, `.gitmodules`). *Not verified:*
`hello_imgui/external/imgui` is an empty directory here (submodule not
initialised), so which ImGui commit hello_imgui pins was not read; the project
version is `1.93.0` (`CMakeLists.txt:3`).

**The `imgui` target it makes** (`hello_imgui_build_lib.cmake:359-380`):
- named plain **`imgui`** (`:365`/`:367`), static unless `HELLO_IMGUI_IMGUI_SHARED`;
- every root `*.cpp` (the demo included) plus `misc/cpp/imgui_stdlib.cpp` (`:360-363`);
- include directories PUBLIC: the root, `backends`, `misc/cpp`, `misc/freetype` (`:372-377`);
- `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` PUBLIC when the option of that name is on
  (`:369-371`; the option is `CMakeLists.txt:101`, default OFF).

With the package route the target is **`imgui::imgui`** instead (`:247-248`),
and hello_imgui refuses to configure if a package is findable but the option is
off (`:264-272`).

**FreeType and `IMGUI_USE_WCHAR32` — the part that decides which imtypeset path
runs.** `HELLOIMGUI_USE_FREETYPE` defaults ON, **OFF on Android and MinGW**
(`CMakeLists.txt:106-113`). `_him_add_freetype_to_imgui`
(`hello_imgui_build_lib.cmake:382-500`) adds `imgui_freetype.cpp`, links
FreeType ≥ 2.12, and defines on the `imgui` target, PUBLIC:

    IMGUI_ENABLE_FREETYPE     (:478)
    IMGUI_USE_WCHAR32         (:491)

That is the **only** place `IMGUI_USE_WCHAR32` is defined in hello_imgui (`grep`
over the tree: the other hit is an `#ifdef` in a demo). It is called only when
hello_imgui builds ImGui itself with FreeType on (`:283-288`). So:

| hello_imgui host | rasteriser | `ImWchar` |
|---|---|---|
| default desktop / emscripten build | FreeType | 32-bit |
| `HELLOIMGUI_USE_FREETYPE=OFF`, and Android / MinGW by default | stb_truetype | **16-bit** |
| `HELLOIMGUI_USE_IMGUI_CMAKE_PACKAGE=ON` (vcpkg) | whatever the package was built with | whatever the package was built with |

Both of imtypeset's `ImWchar` paths (D15) are therefore reachable from
hello_imgui. `hello_imgui_build_lib.cmake:387` says "Note: also change
add_imgui.cmake in bundle!", from which imgui_bundle is **inferred** to mirror
this (not read: imgui_bundle is not checked out). hello_imgui's own font code is
already version-gated on `IMGUI_VERSION_NUM >= 19198`
(`src/hello_imgui/impl/hello_imgui_font.cpp:93`). One more ABI switch:
`him_with_imguiws` defines `ImDrawIdx=unsigned int` PUBLIC on `imgui` (`:1253`).

**Install and package.** `HELLOIMGUI_INSTALL` defaults to
`PROJECT_IS_TOP_LEVEL` (`CMakeLists.txt:227`). It installs its targets into an
export set (`:425-430`), writes `hello_imgui-targets.cmake` with namespace
`hello_imgui::` (`:441-444`), a version file (`:447-450`), and a config
**generated** from `hello_imgui_cmake/hello_imgui-config.cmake.in`, whose
`find_dependency` calls are switched by how it was built: `imgui CONFIG` only
with the package route, `Freetype` only when FreeType was a system library
(`CMakeLists.txt:452-485`, the `.in` file lines 4-27). It **installs ImGui
itself**: the static library and every root header, flat into `include/`
(`hello_imgui_build_lib.cmake:292-329`). A parent project can redirect the
export set and the cmake directory (`HELLOIMGUI_INSTALL_EXPORT`,
`HELLOIMGUI_INSTALL_CMAKE_DIR`, `CMakeLists.txt:228-234`); the comment names
imgui_bundle as the user of that.

**`hello_imgui_add_app`** (`hello_imgui_cmake/hello_imgui_add_app.cmake:27-89`,
`:151-165`) is `add_executable` plus asset bundling, platform customisation and
`target_link_libraries(app PRIVATE hello_imgui::hello_imgui)`. It is included
by the package config, so it is available after `find_package`. It is an
application helper; a library has no use for an equivalent.

**`vcpkg.json`** (36 lines) is a *manifest of what hello_imgui's own build
needs* — `freetype`, `glfw3`, `lunasvg`, `glad`, `stb`, and `imgui` with
`docking-experimental`, `freetype` and backend features — not a port. The port
is separate: `hello_imgui_cmake/overlay_vcpkg/hello-imgui/{portfile.cmake,vcpkg.json,usage}`,
wired in by `vcpkg-configuration.json` (`overlay-ports`). A consumer test lives
in `.github/ci_vcpkg_package_tests/CMakeLists.txt` (five lines:
`find_package`, `hello_imgui_add_app`).

**`_example_integration/`** is a submodule
(`pthom/hello_imgui_template`, `.gitmodules`) and is **empty here**. Its content
was not read. `CMakeLists.txt:40` points at the same template.

### What the first-pass claims came to

| claim | finding |
|---|---|
| ImGui has no root build system; sources + opt-in backends/misc; per-example Makefiles | confirmed (§1) |
| hello_imgui is CMake with backend options | confirmed |
| find_package-or-download dependencies | confirmed, with the order target → package → download, and download **off by default on Linux** |
| ImGui from `HELLOIMGUI_IMGUI_SOURCE_DIR` or a CMake package | confirmed; the package route is documented as not recommended and yields `imgui::imgui`, not `imgui` |
| `install()` + package config | confirmed; the config is generated, and ImGui is installed with it |
| a `vcpkg.json` | confirmed as a dependency manifest; the port is elsewhere |
| `hello_imgui_add_app` | confirmed |
| a headless null backend | confirmed (`HELLOIMGUI_HEADLESS`) |

---

## 3. The 1.93 run

### What was built

`build/dist-research/snap` is `cp -r` of `src include tests cmake CMakeLists.txt
CMakePresets.json bench examples`. One snapshot was enough: nothing in it was
half-written (every configuration compiles).

**The snapshot's `cmake/imgui.cmake` had to be changed to configure at all.**
Unmodified, `-DIMTYPESET_IMGUI_DIR=/home/wreel/Source/imgui` stops with

    imtypeset: /home/wreel/Source/imgui is Dear ImGui 19298, and the pin is 19290 (v1.92.9).

from the `FATAL_ERROR` on `_imgui_version_num STREQUAL IMTYPESET_IMGUI_VERSION_NUM`.
In the snapshot only, that `FATAL_ERROR` was made a `WARNING` (the original is
kept as `build/dist-research/snap-imgui.cmake.orig`). Nothing else in the
snapshot was edited. So: **D30 says the pin is a floor; the build system still
enforces it as exact**, and `IMTYPESET_IMGUI_DIR` cannot be pointed at any other
ImGui today.

GCC 12.2.0, FreeType 2.12.1, Ninja, `-Werror` on.

| directory | ImGui | rasteriser | `ImWchar` | other | compile | ctest (27) |
|---|---|---|---|---|---|---|
| `dbg-ft` | 1.93 | FreeType | 32 | debug | 0 errors, 0 warnings | 4 fail |
| `dbg-stb` | 1.93 | stb | 32 | debug | 0, 0 | 4 fail |
| `asan-ft` | 1.93 | FreeType | 32 | ASan+UBSan | 0, 0 | 4 fail, 0 sanitiser reports |
| `asan-stb` | 1.93 | stb | 32 | ASan+UBSan | 0, 0 | 4 fail, 0 sanitiser reports |
| `w16-193` | 1.93 | FreeType | 16 | debug | 0, 0 | 4 fail |
| `obs-193` | 1.93 | FreeType | 32 | `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` | **7 errors, in 2 files, none in `src/` or `include/`** | 3 fail, 1 not built |
| `base-ft` | **1.92.9** | FreeType | 32 | debug (control) | 0, 0 | 1 fail |
| `base-stb` | **1.92.9** | stb | 32 | debug (control) | 0, 0 | 1 fail |
| `obs-1929` | **1.92.9** | FreeType | 32 | `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` (control) | 0, 0 | 1 fail |

The same four tests fail in every 1.93 configuration, with the same first
failing checks (the `asan-*` logs differ from the `dbg-*` ones only in timings
and ordering). The one failure in the controls is `test_opentype_wild`, which
does not touch ImGui and which the `font-wild-census` task reports as failing by
design until task `font-wild`; it is the fourth failure in the 1.93 rows too and
is **not** a 1.93 finding.

### The library compiles and behaves the same; three tests and one overlay do not

**Library sources (`src/`, `include/`): no compile error, no warning, in any
configuration**, including `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` on 1.93 and a
16-bit `ImWchar` on 1.93. D27/D30's "nothing to port for 1.93" holds for
compiling. `ImFontLoader` did change as the design note said
(`pin/imgui_internal.h:3901-3918` against `imgui/imgui_internal.h:3973-3991`:
`FontSrcContainsGlyph` and `FontBakedLoadGlyph` take a glyph index, and
`FontSrcGetGlyphIndexFromCodepoint` is new and required) and nothing here uses it.

**Glyph-box equality with the reference's goldens holds under 1.93.**
`test_draw`'s census, FreeType, both `ImWchar` widths:

    draw: 0 of 583 glyph boxes and 0 of 44 baked keys differ from the reference's (FreeType 2.13.3);
          22 of 22 cases compared with its buffers outright

which is the line `base-ft` prints under 1.92.9. Under stb the census is
`399 of 583 ... 3 of 22` on 1.93 and on 1.92.9 alike. `misc/freetype/imgui_freetype.cpp`
differs between the two by 51 lines and `imstb_truetype.h` by none; whatever
the 51 lines do, they moved no box in this set. Every other draw, API, paragraph,
selection, font-loader and stress test passes on 1.93 as on 1.92.9.

**What moved is `ImDrawList`'s shape rendering**, which 1.93 rewrote
(`imgui/docs/CHANGELOG.txt:42-101`, breaking changes at `:105-181`). Probed
directly (`build/dist-research/probe/probe.cpp`, compiled against each ImGui;
outputs in `out_.deps_imgui.txt` and `out_Source_imgui.txt`):

| call | 1.92.9 | 1.93 | CHANGELOG |
|---|---|---|---|
| `AddRect((10,20),(30,40))` | 8 vertices, AA strip at ±1.5 px about the half-pixel centre line (8.9999..12.0001) | 8 vertices, an exact 1 px inside stroke (10..11) | `:136` |
| `AddLine((10,50),(30,50), t=1)` | centred on **y = 50.5**, x 10.5..30.5 | centred on **y = 50**, x 10..30 | `:107` (the +0.5 offset was removed) |
| `AddRectFilled`, whole-pixel corners | 4 vertices | 4 vertices, **identical** | — |
| `AddRectFilled`, half-pixel corners | 4 vertices, aliased | 8 vertices, anti-aliased fringe | `:143` |
| `AddRectFilled`, zero height | 4 vertices (degenerate quad) | **nothing** | `imgui/imgui_draw.cpp` `AddRectFilled`: `min_dim < (1/255)*_FringeScale` returns |
| `AddRectFilled`, `min.x > max.x` | 4 vertices | **nothing** | `:146` |
| `PrimReserve` + `PrimRect`, half-pixel corners | 4 vertices | 4 vertices, **identical** | — |
| any of the above with `ImDrawFlags_StrokeLegacy` (1.93 only) | — | `AddRectFilled`: as 1.92.9 in all three cases. `AddRect`: 10 vertices at 9.5/11.5. `AddLine`: y 49.5..51.5. **Neither is 1.92.9's vertices.** | `:70-74` |

`src/draw.cpp` writes its own quads for glyphs and rules (`ListSink::quad`,
through `PrimReserve`), which is why none of that reaches the library's output.
It calls ImGui's shape functions in exactly one place, `ListSink::outline`,
for the debug overlay.

#### Failure 1 — `smoke`: the test still asserts the exact pin

    tests/smoke.cpp:21: CHECK_EQ(ImTypeset::GetImGuiVersionNum(), IMTYPESET_IMGUI_VERSION_NUM)
        left: 19298   right: 19290

Not an ImGui change. *Minimal change, version-neutral:* drop line 21 (line 22
already compares with `IMGUI_VERSION_NUM`) or make it
`CHECK(... >= IMTYPESET_IMGUI_VERSION_NUM_MIN)`. With it goes the configure-time
`FATAL_ERROR` in `cmake/imgui.cmake` (a floor check on 19200, and a `STATUS`
line when the version is not the tested one).

#### Failure 2 — `test_soft_raster`: a test of ImGui's own `AddRectFilled`

    tests/test_soft_raster.cpp:103: [a_rectangle_on_half_pixels_follows_the_top_left_rule]
        pixel (10, 20) is ff000080, expected ff0000ff (inside the rectangle)      (50 pixels wrong)

The test draws `AddRectFilled((10.5,20.5),(30.5,25.5))` to exercise the
rasteriser's top-left rule and expects aliased edges. 1.93 anti-aliases a
rectangle with non-integer corners (CHANGELOG `:143-145`). The software
rasteriser is not wrong and neither is the library. *Minimal change,
version-neutral:* the test emits the rectangle with `PrimReserve(6, 4)` +
`PrimRect(...)`, which is unchanged between the two versions (probe, last row).

#### Failure 3 — `test_draw`, `the_vertices_are_those_of_one_add_text_per_glyph`

Two separate things, 12 failed checks under FreeType, 2 under stb.

(a) **"Rule 1" — the port's list equals a replay through ImGui's own calls —
no longer holds for degenerate rules.** First failing check:

    tests/test_draw.cpp:658: CHECK_EQ(got.VtxBuffer.Size, want.VtxBuffer.Size)   left: 40   right: 28
        in: case rules-by-hand                (and rules-by-hand-fractional-negative-origin)

The case has ten rules, of which one has zero height, one zero width and one
`x0 > x1` (`tests/golden/draw/draw.golden`, case `rules-by-hand`, `rectfilled`
records 3, 4 and 9). The port emits ten quads, as the reference did and as the
golden holds (the golden comparison for this case **passes** on 1.93). The
replay calls `AddRectFilled`, which on 1.93 emits nothing for those three:
28 = 40 − 12. The port's output did not change; the yardstick did.
*Minimal change:* in `replay_through_imgui`, emit `RECT_FILLED` with
`PrimReserve` + `PrimRect` (version-neutral; the port's rules are on whole
pixels, where `AddRectFilled` and `PrimRect` agree on both versions), or pass
`ImDrawFlags_StrokeLegacy` under `#if IMGUI_VERSION_NUM >= 19293`-style gating
(the exact number at which the flag appeared was not determined; `PrimRect`
avoids needing it).

*Noticed, not asked:* this is also a behavioural fork a host can see. A rule
with inverted corners is drawn by imtypeset (as by the reference on 1.92.9) and
would **not** be drawn by the same `AddRectFilled` call on 1.93. Whether an
inverted rule should be drawn at all is a question for whoever owns `src/draw`;
the three zero-area cases are invisible either way.

(b) **The debug overlay's outlines are ImGui's, and ImGui's moved.** Cases
`span-edges-by-hand-debug` and `debug-overlay`:

    tests/test_draw.cpp:1124: vertex 0: port (0x1.8p+2, 0x1.8p+3) ..., reference (0x1.400066p+2, 0x1.5fffdp+3) ...

Port (6, 12) on 1.93 against the golden's (5.0001, 10.9999) from 1.92.9:
`AddRect`'s inside stroke against the old anti-aliased strip (probe, first
row). Vertex and index counts are equal; positions are not. **This is the one
place where equality with the reference's goldens legitimately does not hold
under 1.93**, and no flag restores it: `ImDrawFlags_StrokeLegacy` gives a third
set of vertices (probe, last row). The golden was dumped from an ImGui 1.92.9
`AddRect`/`AddLine` and can only be reproduced by one.

It is not only a test matter. `ListSink::outline` calls
`AddLine(p0, p1, colour, 1.0f)` for the overlay's line segments; on 1.93 a
1 px line given whole-pixel coordinates is centred on the pixel **boundary**
(probe: y 49..51 for y = 50), i.e. half a pixel up and left of where 1.92.9 put
it, and straddling two pixel rows. The overlay is internal (`DrawArgs::debug`;
D30 keeps it out of the public API), so no host sees this today.
*Minimal change, version-neutral:* draw the overlay's outlines as the library's
own quads (four 1 px `quad()`s for a rectangle, one for an axis-aligned
segment), as rules already are. That makes the overlay identical on every
ImGui, lets `draw_runs(DrawBuffers&)` carry it, and removes the library's last
call into the shape API. The two debug goldens then compare ops and glyph quads
only, and say so; the outline vertices in them are a record of ImGui 1.92.9,
not of the reference's logic. *The cheaper alternative* — `#if
IMGUI_VERSION_NUM >= …` adding 0.5 to `AddLine`'s coordinates — fixes the
placement and leaves the goldens unreproducible on 1.93 all the same.

### `IMGUI_DISABLE_OBSOLETE_FUNCTIONS`

On **1.92.9**: everything compiles, tests as the control.

On **1.93**: `libimtypeset.a`, every example, and every test and benchmark
except `test_draw` and `bench_draw` compile. The 7 errors are in two files that are not library sources:

    tests/test_draw.cpp:614, :1073   'ImDrawListFlags' does not name a type
    tests/test_draw.cpp:615          'ImDrawListFlags_TextNoPixelSnap' was not declared
    tests/test_draw.cpp:1503         'ImDrawListFlags_AllowVtxOffset' was not declared
    bench/bench_draw.cpp:436         'ImDrawListFlags_TextNoPixelSnap' was not declared

(the other two are `flags` undeclared as a consequence). 1.93 merged
`ImDrawListFlags` into `ImDrawFlags` and kept the old names only as obsolete
redirections (`imgui/imgui.h:4318-4329`; CHANGELOG `:153-160`).
*Minimal change:* the new names exist only from 1.93, so a three-line shim in
the two files —

    #if IMGUI_VERSION_NUM >= 19293   // whichever number introduced ImDrawFlags_TextNoPixelSnap; not determined here
    typedef ImDrawFlags DrawListFlags; ... ImDrawFlags_TextNoPixelSnap / ImDrawFlags_UseVtxOffset
    #else
    typedef ImDrawListFlags DrawListFlags; ... ImDrawListFlags_TextNoPixelSnap / ImDrawListFlags_AllowVtxOffset
    #endif

`src/draw.h:41` names `ImDrawListFlags_TextNoPixelSnap` in a comment only.

### `imgui_internal.h`, and symbols not in `imgui.h`

- `grep -rn imgui_internal src include`: two hits, both comments
  (`include/imtypeset.h:72`, `src/font_loader.h:19`). The only ImGui header any
  file under `src/` or `include/` includes is `imgui.h`
  (`include/imtypeset.h:64`, `src/units.h:35`, `src/draw.h:59`,
  `src/font_loader.h:38`). Since those compile, every ImGui symbol used is
  declared in `imgui.h`. D27/D30's claim is **confirmed by compiling**, on
  1.92.9 and 1.93.
- `IM_ROUND` (which *is* `imgui_internal.h`'s) appears in `src/draw.h` in
  comments only.
- **But four of the symbols are marked `[Internal]` in `imgui.h`.**
  `src/draw.cpp`'s `ListSink` reads and writes `ImDrawList::_VtxWritePtr`,
  `_IdxWritePtr`, `_VtxCurrentIdx` (`imgui/imgui.h:3414-3417`,
  `pin/imgui.h:3336-3339`) and reads `_CmdHeader.ClipRect` (`imgui/imgui.h:3419`).
  They are the fields `PrimWriteVtx`/`PrimWriteIdx` use inline
  (`imgui/imgui.h:3538-3540`) and are unchanged across the two versions, but
  "nothing sits on internals" should be stated as "nothing includes
  `imgui_internal.h`; the draw loop uses four `[Internal]` members of
  `ImDrawList` that `imgui.h` exposes".
- Used and public, same declaration in both: `PrimReserve`/`PrimUnreserve`,
  `ImFont::GetFontBaked`, `ImFontBaked::FindGlyph`, `ImFontConfig::RasterizerDensity`,
  `GetFontTexUvWhitePixel`, `ImFontAtlas::AddFontFromMemoryTTF`,
  `IM_UNICODE_CODEPOINT_MAX`.

### A unity build does not work yet

Since ImGui's own bar is "all `.cpp` in one compilation unit"
(`misc/single_file`), it was tried: the 15 `src/*.cpp` of the snapshot
`#include`d into one file, `g++ -std=c++17 -fsyntax-only`
(`build/dist-research/unity/unity.log`). **6 errors**, all collisions between
anonymous namespaces of different files:

    mathtext.cpp:222, selection.cpp:78   redefinition of {anonymous}::append_utf8   (three definitions in all)
    mathtext.cpp:243                     redefinition of {anonymous}::formatted     (first in math_mlist.cpp)
    nodes.cpp:19                         redefinition of {anonymous}::EMPTY_DISC
    style.cpp:100, :517                  call of overloaded set(std::string*&, std::string) is ambiguous

Any amalgamation, and any host that builds with `CMAKE_UNITY_BUILD`, needs
these four names resolved first (one shared `append_utf8`/`formatted` in a
header would also remove the duplication). The snapshot was taken mid-`stress`;
the list should be re-run on the settled tree.

---

## 4. Recommendation for imtypeset

### 4.1 The drop-in file set

**What the ecosystem expects is a handful of prefixed files you add to your
project — not a build system, and not a single-file amalgamation.** ImGui's
position is §1: flat prefixed files; its one amalgamation is a test stub it
tells users not to use. (ImPlot is the nearest precedent — five files,
`implot.h`, `implot_internal.h`, `implot.cpp`, `implot_items.cpp`,
`implot_demo.cpp` — **stated from memory; ImPlot is not checked out and this was
not verified**.)

imtypeset today is 15 `.cpp` and 19 headers / `.inc` in `src/` (19,155 lines)
behind one public header, with names like `nodes.cpp`, `pack.cpp`, `style.cpp`,
`draw.cpp`, `units.h`. Shipping `src/` as the drop-in has two real costs: the
basenames are generic enough to collide with a host's own (ImGui prefixes every
file for that reason), and 34 files is not "a few". Renaming them would break
the side-by-side reading with the reference that D5 and CLAUDE.md set up.

So, file by file:

| file | status | what |
|---|---|---|
| `imtypeset.h` | exists (`include/`) | the public header, unchanged |
| `imtypeset.cpp` | **generated** by `tools/amalgamate.py` | `src/*.cpp` and their private headers concatenated in dependency order, internals staying in `ImTypeset::detail`; carries the notices `THIRD-PARTY.md` requires |
| `imtypeset_demo.cpp` | from task `demo` | separate, as `imgui_demo.cpp` is, so a host can leave it out |
| `LICENSE`, `THIRD-PARTY.md` | when the licence is chosen (D24) | |

`src/` stays the source of truth and the thing CMake builds; the three files
are a **release artefact** written to a directory (`dist/`), never edited by
hand and never the input of the tests. That is an amalgamation in mechanism but
the ImGui/ImPlot shape in what a user holds. Three conditions make it honest:

1. **A unity configuration in the gate.** The amalgamated `imtypeset.cpp` is
   compiled and the suite's API tests run against it; otherwise it rots. It
   fails today (§3, six errors).
2. **Floating point.** The drop-in is compiled with the *host's* flags, and the
   library's results are specified with `-ffp-contract=off` and no fast-math
   (D16). The generated file should refuse `__FAST_MATH__` with an `#error`
   (overridable) and state the contraction requirement at its top; `#pragma
   STDC FP_CONTRACT OFF` is honoured by Clang and, as far as I know, ignored by
   GCC. *Reasoned, not run:* no drop-in build with contraction enabled was
   tried, and which goldens move under it is unknown. This is the largest
   unmeasured risk in the drop-in story.
3. **Warnings.** A host may compile with flags we do not (`-Wold-style-cast`,
   `-Wsign-conversion` under Clang, MSVC `/W4`). ImGui's CI does this with
   `WITH_EXTRA_WARNINGS` (`imgui/examples/example_null/Makefile:24-27`, `:43-48`).
   Only GCC 12 with our own flag set has been run.

A drop-in host then does what `imgui/misc/freetype/README.md` asks for
FreeType: add the files next to ImGui's, nothing else. No define is needed: the
library follows the host's `ImWchar` and rasteriser (D15, D27).

### 4.2 The CMake contract with the host's ImGui target

What exists (`CMakeLists.txt`): `if(NOT TARGET imgui)` build one from
`cmake/imgui.cmake`; `target_link_libraries(imtypeset PUBLIC imgui)`; tests,
benchmarks and examples off unless top-level.

1. **Accept `imgui::imgui` as well as `imgui`.** hello_imgui's own build makes
   `imgui` (`hello_imgui_build_lib.cmake:365-367`), its package route and vcpkg
   make `imgui::imgui` (`:247-248`). Today a host that has only `imgui::imgui`
   gets a **second ImGui built from `.deps`** and linked next to its own.
   Resolve once into `IMTYPESET_IMGUI_TARGET` (a cache variable a host may set):
   the variable if set; else `imgui::imgui`; else `imgui`; else, only when
   top-level or `IMTYPESET_BUILD_IMGUI=ON`, build ours. When included by another
   project with no ImGui target in sight, **fail with a message naming the
   variable** rather than silently building a private ImGui.
2. **Never define anything on a host's target** (already D15), and never
   `find_package(Freetype)` when the host supplied ImGui (already so: it is
   inside the `if(NOT TARGET imgui)`).
3. **The version check is a floor.** `cmake/imgui.cmake`: fail below 19200,
   `STATUS` when not the tested version. `IMTYPESET_IMGUI_DIR` then works for
   1.93 (§3 shows it does once the check is out of the way).
4. **Rely on PUBLIC propagation, and keep the runtime check.** hello_imgui puts
   every ABI-changing define on `imgui` PUBLIC (`IMGUI_USE_WCHAR32` `:491`,
   `IMGUI_ENABLE_FREETYPE` `:478`, `ImDrawIdx=unsigned int` `:1253`,
   `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` `:370`), so linking the target is enough
   for an in-tree build. A host that configures through `IMGUI_USER_CONFIG` or
   an edited `imconfig.h` is covered the same way as long as its include path
   comes with the target. An *installed* `libimtypeset.a` used with a different
   ImGui configuration is what `CreateContext`'s size arguments exist for (D30);
   that check should stay and the docs should name it as the answer.
5. **Install and package, modelled on hello_imgui's.** `IMTYPESET_INSTALL`
   defaulting to top-level; `install(TARGETS imtypeset EXPORT …)`, the header to
   `include/`, `imtypesetTargets.cmake` with namespace `imtypeset::`, a version
   file, and a config generated from `cmake/imtypesetConfig.cmake.in` whose one
   dependency line is `find_dependency(imgui CONFIG)`. **Install only when ImGui
   came from an imported target** (a package). hello_imgui can install the ImGui
   it built because it owns that decision for an application; a library
   installing its own copy of ImGui's headers into `include/` would collide with
   the host's. With our own `.deps` ImGui, `install` should be refused with a
   reason.
6. **A redirectable export set** (`IMTYPESET_INSTALL_EXPORT`, as
   `HELLOIMGUI_INSTALL_EXPORT`, `hello_imgui/CMakeLists.txt:228-234`) is what
   lets a bundling project ship imtypeset inside its own package. Cheap to add;
   only worth it if imgui_bundle integration is pursued.
7. **`vcpkg.json`** at the root as hello_imgui has it: a manifest naming
   `imgui` (with the `freetype` feature optional), so `cmake --preset` under a
   vcpkg toolchain finds `imgui::imgui`. A port (`ports/imtypeset/`) is a
   separate, later thing. *Neither can be tested here:* the sandbox reaches no
   registry (a FetchContent of ImGui was refused: `github.com:443`).
8. **For hello_imgui hosts**, nothing special: `add_subdirectory(imtypeset)`
   after hello_imgui (the `imgui` target exists by then), then
   `hello_imgui_add_app(app main.cpp)` and
   `target_link_libraries(app PRIVATE imtypeset::imtypeset)`. *Not run:*
   hello_imgui's submodules are empty here and it cannot fetch.

### 4.3 What the two host families get

| host | `ImWchar` | rasteriser | imtypeset path | tested here on 1.93 |
|---|---|---|---|---|
| stock ImGui, files dropped in | 16 | stb | BMP stand-ins (D15), `OversampleH = 1` (D27) | 16-bit with FreeType: yes (`w16-193`). **16-bit with stb on 1.93: not run** |
| stock ImGui + `IMGUI_ENABLE_FREETYPE` | 16 | FreeType | stand-ins | yes (`w16-193`) |
| hello_imgui default | 32 | FreeType | identity; the reference's configuration | yes (`dbg-ft`, `asan-ft`) |
| hello_imgui without FreeType (Android, MinGW default) | 16 | stb | stand-ins | not run on 1.93 |
| 32-bit + stb | 32 | stb | identity | yes (`dbg-stb`, `asan-stb`) |

The header already warns that nothing may be merged into a returned font in a
16-bit build (D30: the stand-ins start at U+E000, where icon fonts live).
hello_imgui hosts merge icon fonts routinely; on its Android/MinGW defaults that
warning is the live constraint and belongs in `docs/distribution.md`, not only
in the header.

### 4.4 imgui_bundle and litgen

**imgui_bundle is not checked out; nothing in this section was verified against
it.** What can be said from `hello_imgui` and from the header:

- It builds on hello_imgui and ships it inside its own CMake package
  (`hello_imgui/CMakeLists.txt:228-232`, `hello_imgui/README.md:130`), and has
  an `add_imgui.cmake` that mirrors hello_imgui's FreeType/`WCHAR32` block
  (inferred from the comment at `hello_imgui_build_lib.cmake:387`). So a bundle
  build is expected to be **32-bit `ImWchar` + FreeType + the docking branch**.
  The docking branch was not built here: `../imgui` is master. That is the
  first thing to run when a docking checkout is available.
- The reference runs on imgui-bundle 1.92.900 (D6), i.e. the Python that
  imtypeset replaces already lives there. A binding would be a new
  `external/imtypeset/` with generated bindings beside implot's.
- What the header gives a generator: no STL, no templates, enums as
  `typedef int` + `enum X_`, opaque handles, `IMTYPESET_API` on every
  declaration (`include/imtypeset.h`), which is the shape D30 chose for this.
- What will need hand attention, **reasoned from the header alone**:
  - `CreateContext`'s defaulted `sizeof` arguments (`include/imtypeset.h:289-293`)
    are meaningful only when evaluated in a C++ caller; a binding must call it
    with no arguments from C++ glue and not expose them.
  - `const char* text, const char* text_end` pairs become one `str`.
  - `ImVector<ImDrawVert>*` / `ImVector<ImDrawIdx>*` outputs of
    `DrawParagraphToBuffers`, and `ImGuiTextBuffer*` of
    `AppendParagraphSelectionText`, need a Python-side shape.
  - `AddFontFromMemoryTTF(const void*, …)`: who owns the bytes must be stated
    for a caller whose buffer is a Python `bytes`.
  - `GetStyle()` returns a reference to mutable state.
- Whether litgen needs annotations or exclusions for any of these, what its
  CMake glue looks like, and whether the bundle's ImGui has
  `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` on, are **unknown**.

---

## 5. Not verified

- hello_imgui was **read, not built**: `external/imgui`, `external/plutosvg`,
  `_example_integration` and `ios-cmake` are uninitialised submodules and the
  sandbox cannot fetch. `_example_integration/` was not read at all.
- The ImGui commit hello_imgui pins; the vcpkg `imgui` port and whether its
  package defines `IMGUI_USE_WCHAR32`; imgui_bundle; litgen; ImPlot's file set.
- ImGui's wiki (Useful-Extensions, Getting-Started) — linked from the docs, not
  in the checkout.
- ImGui **docking** branch, any ImGui between 1.92.0 and 1.92.8, and 1.93 with
  a 16-bit `ImWchar` under stb.
- Release builds against 1.93; any compiler but GCC 12.2; MSVC and Clang
  entirely; FreeType other than 2.12.1.
- The `IMGUI_VERSION_NUM` at which `ImDrawFlags_StrokeLegacy` /
  `ImDrawFlags_TextNoPixelSnap` first exist (some value in 19291..19298).
- No rendered image was compared between 1.92.9 and 1.93; "the library's output
  is the same" rests on the vertex-level tests (`test_draw`, `test_api`) passing
  on both, not on pixels.
- The snapshot is of a tree in motion. Line numbers under `snap/` are the
  snapshot's; the failures should be re-run on the settled tree before `dist`
  acts on them.
- The floating-point and warning-flag risks of a drop-in build (§4.1).
