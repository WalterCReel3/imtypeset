# dist — result (builder, 2026-10-10)

Everything below was run on this machine today (Linux x86-64, GCC 12.2.0, CMake 3.25.1, FreeType
2.12.1), each binary under a memory guard, one preset at a time, `-j4`. The user-facing document
is `docs/distribution.md`; the decisions are `docs/decisions.md` D44. Nothing was staged or
committed. No golden was touched and no tolerance added.

## The routes, and the command that proves each

| route | what it is | proof (ran) |
|---|---|---|
| (a) Python sidecar in a stock imgui_bundle wheel | `python/host/build_bundle_library.sh [bundle-checkout] [python]`: generates the drop-in files, 4 compiler lines + 1 link line against that interpreter's wheel module, then the import gate. Now executable. One library per interpreter | Run for both: `../pptmstr/.venv` (wheel 1.92.801, ImGui 1.92.8) and `../imgui-adv-text/.venv` (1.92.900, 1.92.9). 28 s each, stand-in headers, gate accepted with 22 probes, 77 `ImTypeset_*` + 8 `ImTypesetBundle_*` exported. `examples/python/bundle_host.py --frames 30` on Xvfb in both wheels: selection (74, 284), 2 layouts; the advtext screenshot was looked at and is right. No screenshot in pptmstr's venv: it has no numpy, which the example's screenshot path needs |
| (b1) drop-in files | `python3 -I -B tools/amalgamate.py [--out DIR] [--check]` -> `dist/imtypeset.h`, `imtypeset.cpp` (28,979 lines), `imtypeset_demo.cpp`, `imtypeset_c.h`, `imtypeset_c.cpp`; `/dist/` is git-ignored | ctest `amalgamation` (every preset that builds its own ImGui): generate, `--check`, `c++ -std=c++17 -c X.cpp -I<imgui>` for the three files, `-ffast-math` refused, `--check` fails after a one-byte edit. Whole suite against the files: preset `amalgamation`, 41/41 |
| (b2) `add_subdirectory` | target contract: `IMTYPESET_IMGUI_TARGET`, else package (`IMTYPESET_USE_IMGUI_PACKAGE`), else `imgui::imgui`, else `imgui`, else own ImGui only at top level or with `IMTYPESET_BUILD_IMGUI`; otherwise FATAL_ERROR | ctest `consumer_projects` (preset `debug`), steps 1-4: host target `imgui` built with `CMAKE_UNITY_BUILD` and run; alias `imgui::imgui`; named target; no target -> refused with the message |
| (b3) installed package | `IMTYPESET_INSTALL` (only against an imported ImGui; never installs ImGui), `cmake/imtypesetConfig.cmake.in` with `find_dependency(imgui CONFIG)`, version file SameMinorVersion | same test, steps 5-6: install against an in-tree ImGui refused; ImGui package fixture installed; imtypeset built and installed against it; a `find_package(imtypeset CONFIG)` host built and run; `find_package(imtypeset 0.99)` refused. 15 steps, 26 s |

## Presets and counts (final tree; incremental rebuilds of each preset directory, not from empty)

| preset | ctest |
|---|---|
| debug (with `consumer_projects`) | 42 / 42 |
| release | 41 / 41 |
| asan (now also instruments `tests/test_c_api.c`: `CMAKE_C_FLAGS` added; checked in compile_commands and by `__asan` symbols in the object) | 41 / 41 |
| stb | 41 / 41 |
| wchar16 | 41 / 41 |
| amalgamation (library = generated files) | 41 / 41 |
| fp-host (generated files at `-O3 -march=native`, NO `-ffp-contract=off` on the library) | 41 / 41 |
| exceptions (ASan+UBSan, exceptions on, `tests/imconfig_throwing.h`) | 41 / 41; `test_exception_safety` 7 tests, 52,730 checks |
| bundle-config (the bundle's `imgui_bundle_config.h` + `IMGUI_BUNDLE_BUILD_PYTHON` + `IMGUI_DISABLE_OBSOLETE_FUNCTIONS`) | 41 / 41 |
| imgui-next (`../imgui`, 1.93.0 WIP 19298) | 41 / 41 |
| python-host (`python/` wired from the root; `python_binding`) | 42 / 42 |

Baseline before any change: preset debug 40/40. The board's note that
`test_api_document_golden` has a failing tab test is stale: it passes.
`-ffp-contract=off` is on every one of this tree's C++ translation units in every preset except
the three library files of `fp-host` (checked by reading each `compile_commands.json`); no
fast-math flag anywhere. No warning from this tree's sources; fresh builds of `exceptions` and
`bundle-config` print four `-Wterminate` warnings in ImGui's own destructors.
Also run once, private directory: 1.93 WIP + `IMGUI_DISABLE_OBSOLETE_FUNCTIONS`, 41/41.
`python/CMakeLists.txt` and `examples/demo_sdl2/CMakeLists.txt` still configure standalone.

## The floating-point test (ruling 7)

GCC 12.2, `-O3 -march=native`, FMA on this CPU; GCC contracts by default at `-std=c++17`.

- WITHOUT the pragma (`amalgamate.py --no-floating-point-guard`, private dir): 65 FMA instructions
  in the object; **38 of 41**. Failing: `test_opentype` (776 checks), `test_fontlib` (1,384),
  `test_opentype_wild` (84 corpus faces). All are ink boxes one ulp off. No layout, draw or API
  golden moved in that run.
- WITH `#pragma GCC optimize("fp-contract=off")` (the file as generated; preset `fp-host`): 0 FMA
  instructions, 41 of 41. So the pragma fixed it.
- `#pragma STDC FP_CONTRACT OFF` is ignored by GCC (warning). It is emitted for Clang, and
  `#pragma fp_contract(off)` for MSVC: **neither has been compiled**, there is no Clang or MSVC here.
- Fast-math: `#error` in `src/units.h` on `__FAST_MATH__`, `_M_FP_FAST` or non-zero
  `__FINITE_MATH_ONLY__`, unless `IMTYPESET_ALLOW_FAST_MATH`. In `src/` and not only in the
  generated file, so an `add_subdirectory` host with `-ffast-math` in its global flags is refused too.

## The ImGui floor, and how it was decided

Floor 19200 (v1.92.0), tested version 19290 (v1.92.9): the header's numbers stand, and
`CMakeLists.txt` now READS them (and the project version) from `include/imtypeset.h`, so the two
cannot disagree. Below the floor: FATAL_ERROR. Above the tested version: WARNING. `.deps/imgui`
must still be exactly the tested tag. Decided by building release tags taken from `../imgui`'s git
objects: v1.91.9 does not compile (22 errors); v1.92.0 and v1.92.8 pass the whole suite 41/41
(debug) with the same draw census as 1.92.9 (0 of 583 glyph boxes differ, 22/22 outright);
v1.92.1/.2/.4/.6 compile with 0 warnings (syntax only), not run. The library needed no change.
The TESTS needed guards for three names newer than the floor, numbers read from the introducing
commits: `tests/support/imgui_compat.h` (19274 PlatformIO draw callbacks, 19282
`ImDrawListFlags_TextNoPixelSnap`, 19298 `ImDrawListFlags` merged into `ImDrawFlags`).

## Renames in src/ (all mechanical; for doc-fixes)

| file | was | is |
|---|---|---|
| `src/mathtext.cpp` | `append_utf8` | `mathtext_append_utf8` |
| `src/mathtext.cpp` | `formatted` | `mathtext_formatted` |
| `src/selection.cpp` | `append_utf8` | `selection_append_utf8` |
| `src/nodes.cpp` | `EMPTY_DISC` | `NODES_EMPTY_DISC` |
| `src/style.cpp` | `set` (the anonymous `set(std::string*, std::string)`) | `style_set` |
| `src/blocks.cpp` | `clamped` (two overloads) | `blocks_clamped` |
| `src/blocks.cpp` | `is_space(const Node&)` | `blocks_is_space` |
| `src/imtypeset_c.cpp` | `fail` | `c_fail` |
| `src/imtypeset_demo.cpp` | `g_context` | `g_demo_context` |
| `src/opentype.cpp` | local `struct Point` in `draw_glyf` | `GlyfPoint` |

clang-format re-wrapped 20 lines of `src/mathtext.cpp` as a consequence. The one non-rename in
`src/`: the fast-math `#error` at the top of `src/units.h`. `include/` was not touched.
Left alone, defined in more than one file but as overloads on unrelated types: `note` (blocks,
pack, paragraph), `classify` (linebreak, selection), `can_draw`/`owned`/`px_of` (the two API files).

## Other files changed

`CMakeLists.txt`, `CMakePresets.json`, `cmake/imgui.cmake`, `cmake/imgui_version.cmake` (new),
`cmake/imtypesetConfig.cmake.in` (new), `tools/amalgamate.py` (new), `tests/CMakeLists.txt`,
`tests/amalgamation/compile.cmake` (new), `tests/consumer/*` (new), `vcpkg.json`, `ports/imtypeset/*`
(new), `.gitignore`, `docs/distribution.md` (new), `docs/decisions.md` (D44; pointers in D6, D16).
Tests, each for the test's own reason: `tests/smoke.cpp` (floor, not the exact version),
`tests/test_soft_raster.cpp` (PrimRect; sampler test skips below 19274),
`tests/support/soft_raster.h`, `tests/support/imgui_compat.h` (new), `tests/test_draw.cpp`,
`tests/test_harden.cpp`, `bench/bench_draw.cpp` (flag names), `tests/test_api.cpp` (the
legacy-atlas half of one test is skipped, with a printed line, under
`IMGUI_DISABLE_OBSOLETE_FUNCTIONS`), `tests/test_header_pod.cpp` (its STL check stands aside when
the ImGui user config itself includes the STL; the preset says so with a define).
Outside my listed files, and why: `python/host/build_bundle_library.sh` (the lead's note: use the
amalgamation, chmod +x) and one line in `python/CMakeLists.txt` (`IMTYPESET_BUILD_IMGUI ON` for
its standalone `add_subdirectory`, which would otherwise now be refused).

## Lead's relayed items, checked against the tree

- asan preset lacked `CMAKE_C_FLAGS`: true, fixed.
- throwing `IM_ASSERT` is in `imgui_bundle_cmake/imgui_bundle_config.h`, not `litgen_imconfig.h`:
  true (read; the preset uses it and `test_exception_safety` runs its 7 tests there).
- ruling 2's 1.93 list: on today's tree only `test_soft_raster` still failed on 1.93 (harden-2 had
  done `test_draw`); the `ImDrawListFlags` errors under OBSOLETE are fixed through the compat header.
- unity collisions: the list had changed (30 errors, other names); the table above is today's.

## Cut, blocked, not run

- **vcpkg**: `vcpkg.json` and `ports/imtypeset/` are sketches, never run (no vcpkg, no network).
- **1.93**: a preset exists and passes in debug; no release, ASan, stb or 16-bit run on 1.93.
- Clang, MSVC, Windows, macOS: nothing compiled. hello_imgui as a CMake host: not run.
- The fork's headers for the sidecar (network; a person): still stand-in headers.
- The install route ran against this tree's ImGui package fixture, not a package manager's.
- Presets were incremental rebuilds, not clean directories: that is release-gate's.
- Not edited, now stale: CLAUDE.md's preset table and "pinned exactly" wording (`docs` owns it);
  `docs/advtext-rework-plan.md` and the spikes still say `sh python/host/build_bundle_library.sh`
  (still works).
- Private build directories and `dist/` removed. `build/dist-research` is dist-research's.
