# dist-research — builder-19 — result

Deliverable: `docs/distribution-notes.md` (read / run / reasoned kept apart;
every statement about `../imgui` and `../hello_imgui` has a file reference).
Evidence: `build/dist-research/` — `snap/` (the snapshot), nine build trees,
`*.configure.log`, `*.build.log`, `*.ctest.log`, `probe/` (ImDrawList probe and
its two outputs), `unity/unity.log`.

Nothing outside `docs/distribution-notes.md`, `.concerns/` and
`build/dist-research*` was written. No library source or build file in the tree
was touched. One file in the **snapshot** was edited: `snap/cmake/imgui.cmake`,
`FATAL_ERROR` -> `WARNING` on the version pin (original kept as
`build/dist-research/snap-imgui.cmake.orig`), because without it no 1.93
configure gets past line 48.

## The 1.93 run

Snapshot taken once (2026-10-09 22:12 EDT); nothing in it was half-written.
GCC 12.2.0, FreeType 2.12.1, `-Werror`.

| tree | ImGui | config | compile | ctest of 27 |
|---|---|---|---|---|
| dbg-ft / dbg-stb | 19298 | debug, FreeType / stb | 0 errors 0 warnings | 4 fail |
| asan-ft / asan-stb | 19298 | ASan+UBSan | 0 / 0 | 4 fail, 0 sanitiser reports |
| w16-193 | 19298 | 16-bit ImWchar, FreeType | 0 / 0 | 4 fail |
| obs-193 | 19298 | IMGUI_DISABLE_OBSOLETE_FUNCTIONS | 7 errors, tests/test_draw.cpp + bench/bench_draw.cpp only | 3 fail + test_draw not built |
| base-ft / base-stb / obs-1929 | 19290 | controls | 0 / 0 | 1 fail (test_opentype_wild) |

- `src/` and `include/` compile clean against 1.93 in every configuration.
  D27/D30 "no imgui_internal.h, nothing to port" is confirmed by compiling.
- Glyph-box equality holds: `0 of 583 glyph boxes and 0 of 44 baked keys
  differ ... 22 of 22 cases compared with its buffers outright` on 1.93
  (FreeType, 32- and 16-bit), the same line as on 1.92.9. stb: 399 of 583 on
  both versions.
- `test_opentype_wild` fails on 1.92.9 too (by design until `font-wild`); not a
  1.93 finding.
- Three 1.93 failures, none in the library's own output:
  1. `smoke.cpp:21` asserts `GetImGuiVersionNum() == 19290`. (Same exact-pin
     assumption as the configure-time FATAL_ERROR in `cmake/imgui.cmake`.)
  2. `test_soft_raster.cpp:103` — the test draws `AddRectFilled` on half pixels
     and expects aliasing; 1.93 anti-aliases non-integer rectangles
     (CHANGELOG.txt:143). Fix: `PrimReserve`+`PrimRect` in the test.
  3. `test_draw.cpp:658` and `:1124` —
     (a) the ImGui replay of `rules-by-hand` has 28 vertices against the port's
     40: 1.93's `AddRectFilled` emits nothing for the zero-height, zero-width
     and inverted rule; the port still matches the reference golden. Fix in the
     test's replay: `PrimRect`.
     (b) the debug overlay's `AddRect`/`AddLine` outlines: ImGui's geometry
     changed (inside stroke; `AddLine` lost its +0.5 offset, CHANGELOG.txt:107,
     :136). The two debug goldens cannot be reproduced on 1.93 by any flag —
     `ImDrawFlags_StrokeLegacy` gives a third set of vertices (probe). Also a
     real, internal-only behaviour change: the overlay's 1 px lines sit half a
     pixel up/left and straddle two rows on 1.93. Version-neutral fix: draw the
     outlines as the library's own quads in `ListSink::outline`.
- `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` on 1.93 breaks only `ImDrawListFlags`,
  `ImDrawListFlags_TextNoPixelSnap`, `ImDrawListFlags_AllowVtxOffset` in
  `tests/test_draw.cpp` (614, 615, 1073, 1503) and `bench/bench_draw.cpp` (436):
  merged into `ImDrawFlags` (imgui.h:4318-4329). On 1.92.9 nothing breaks.
- `src/draw.cpp` uses four members `imgui.h` marks `[Internal]`:
  `_VtxWritePtr`, `_IdxWritePtr`, `_VtxCurrentIdx`, `_CmdHeader`. Unchanged
  between the versions, but "only imgui.h" is not "only public API".
- Unity build of the 15 `src/*.cpp`: 6 errors (anonymous-namespace collisions:
  `append_utf8` x3, `formatted` x2, `EMPTY_DISC`, ambiguous `set` in style.cpp).

## Distribution, in five lines

- ImGui: no build system by design (imgui.cpp:236-239), flat prefixed files,
  opt-in files + a define, per-example Makefiles; its single-file stub is a
  unity-build test it tells users not to use (misc/README.txt:19-23). Nothing in
  its docs says how an extension should ship; only wiki links.
- hello_imgui: CMake; target -> find_package -> download (download off on
  Linux); builds a target named `imgui` from a source dir, or uses `imgui::imgui`
  from a package; generated package config; installs ImGui with itself.
- hello_imgui defines `IMGUI_USE_WCHAR32` in exactly one place
  (hello_imgui_build_lib.cmake:491), only when it builds ImGui with FreeType.
  FreeType is off by default on Android and MinGW, so those hosts are 16-bit +
  stb: both D15 paths are reachable from hello_imgui.
- Recommendation: public header + a **generated** `imtypeset.cpp` +
  `imtypeset_demo.cpp` as the drop-in (not `src/` with its generic basenames,
  not a hand-kept amalgamation), gated by a unity-build configuration; CMake
  accepts `imgui::imgui` as well as `imgui` via `IMTYPESET_IMGUI_TARGET` and
  fails rather than building a private ImGui when included by a host; floor
  version check; install only against an imported ImGui.
- Today a host with only `imgui::imgui` (vcpkg; hello_imgui's package route)
  gets a second ImGui built from `.deps` and linked beside its own.

## Not verified

hello_imgui was read, not built (submodules empty; no network).
`_example_integration/`, hello_imgui's pinned ImGui commit, imgui_bundle,
litgen, ImPlot's file set, the vcpkg imgui port, ImGui's wiki: not read.
Not run: the docking branch, ImGui 1.92.0-1.92.8, 16-bit + stb on 1.93,
release on 1.93, Clang, MSVC. No pixel comparison between 1.92.9 and 1.93.
The `IMGUI_VERSION_NUM` at which `ImDrawFlags_StrokeLegacy` appeared.
A drop-in build under a host's floating-point flags (contraction, fast-math).
The snapshot is of a tree `stress` was writing; re-run before `dist` acts.
