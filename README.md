# imtypeset

ImGui draws text; this typesets it.

A C++ extension for Dear ImGui that lays text out on TeX's model — boxes, glue,
kerns and penalties, packed and broken into lines, shipped out as glyph
positions — in the shape `implot` has: C++ over ImGui, with language bindings
generated on top.

**Status: a skeleton.** The build is wired up, ImGui is pinned, and one smoke
test runs an ImGui frame with no window. No typesetting code exists here yet.

## Build

    cmake --preset debug
    cmake --build --preset debug
    ctest --preset debug

Presets: `debug`, `release`, `asan` (ASan + UBSan). Needs CMake 3.21, Ninja and
a C++17 compiler. The first configure fetches Dear ImGui from GitHub; to build
offline or against a checkout you already have:

    cmake --preset debug -DIMTYPESET_IMGUI_DIR=/path/to/imgui

## Layout

| path | what it is |
|---|---|
| `include/imtypeset.h` | the whole public surface; narrow and POD-ish, because bindings are generated from it, and free of `imgui_internal.h` |
| `src/` | the implementation; the only place allowed to include `imgui_internal.h` |
| `tests/` | one executable per test, non-zero exit on failure, run by ctest; no framework chosen yet |
| `cmake/imgui.cmake` | builds the `imgui` target (core only, no backend) and holds the pin |

## The ImGui pin is exact, not a floor

Dear ImGui `v1.92.9`, by commit. The extension is meant to sit on
`ImFontLoader`, declared in `imgui_internal.h`, and that struct changed between
`v1.92.9` and 1.93 WIP. `src/imtypeset.cpp` refuses to compile against any other
`IMGUI_VERSION_NUM`. Moving the pin is a porting task.

`v1.92.9` is the ImGui inside imgui-bundle 1.92.900, which is what the Python
reference runs on.

## The reference implementation

`../imgui-adv-text` (`advtext`) is the Python implementation this is a port of,
and it is the oracle: a differential harness that runs one document through both
and diffs the results is meant to be built before the code it checks.

Read, in that repository:

- `docs/cpp-extension.md` — the design note this project starts from: scope,
  the font-loader finding, where the cost is, dependencies, risks and staging.
- `docs/status.md` — what is true of the reference today.
- `docs/advance-rule.md`, `docs/subpixel.md` — the device-space decisions, which
  are the part specific to ImGui and the part worth carrying across intact.

## Not decided

- **Where the port stops.** `draw_runs` and the font loader with the views left
  in Python, or the views as well. `docs/cpp-extension.md` §4 says to choose
  before the harness is built, because the harness can only compare what is on
  the native side of the line.
- **The font rasteriser.** The skeleton builds ImGui's core alone, so the
  loader here is stb_truetype. The reference measured its placement rules under
  FreeType, which is what imgui-bundle runs.
- **A test framework.**
- **A licence.** There is no `LICENSE` file yet.
