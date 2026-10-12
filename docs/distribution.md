# Distribution: how imtypeset reaches a program

Four routes, in the order they are wanted. Each has one command that proves it
on this tree; "ran" below means that command was run here on 2026-10-10 (Linux
x86-64, GCC 12.2.0, CMake 3.25.1, FreeType 2.12.1) and what it printed is what
is reported. What was not run is listed at the end and not anywhere else.

| route | what the host holds | proved by |
|---|---|---|
| 1. Python, inside a stock Dear ImGui Bundle wheel | one shared library per interpreter + the `python/imtypeset` package | `python/host/build_bundle_library.sh [bundle-checkout] [python]` (builds, then runs the import gate) |
| 2. C++, drop-in files | `imtypeset.h`, `imtypeset.cpp`, optionally `imtypeset_demo.cpp`, `imtypeset_c.h/.cpp`, next to ImGui's files | `ctest --preset debug -R amalgamation`; the whole suite against those files: `ctest --preset amalgamation` |
| 3. C++, `add_subdirectory` | this repository as a subdirectory, linked as `imtypeset::imtypeset` | `ctest --preset debug -R consumer_projects` (steps 1 to 4) |
| 4. C++, installed package | `find_package(imtypeset CONFIG)`, the same target name | `ctest --preset debug -R consumer_projects` (steps 5 and 6) |

The background reading (what Dear ImGui and hello_imgui themselves do, with file
references) is `docs/distribution-notes.md`. This file is what was built from it.

---

## 1. Python: a sidecar library inside an unmodified imgui_bundle wheel

For an application that already runs on the `imgui_bundle` wheel from PyPI. The
library holds no Dear ImGui of its own: its ImGui symbols are left undefined
and resolve against the wheel's extension module, so it draws into the
application's own context, font atlas and windows. Nothing is installed into
the interpreter's environment and the wheel is not touched.

**One library per interpreter.** A library is linked against one wheel binary
and refuses any other at import. Two applications on two wheel versions need
two libraries:

```sh
# the interpreter whose wheel the library is for is the second argument
IMTYPESET_BUNDLE_HEADERS=stand-in IMTYPESET_BUNDLE_OUT=build/py-host-bundle-pptmstr \
    python/host/build_bundle_library.sh ../imgui_bundle ../pptmstr/.venv/bin/python
IMTYPESET_BUNDLE_HEADERS=stand-in IMTYPESET_BUNDLE_OUT=build/py-host-bundle-advtext \
    python/host/build_bundle_library.sh ../imgui_bundle ../imgui-adv-text/.venv/bin/python
```

(`IMTYPESET_BUNDLE_HEADERS=stand-in` is what every run here has said. Without
it the script fetches the fork's headers from GitHub and never substitutes;
with no network it stops at the fetch.)

and then, in the application's environment:

```sh
IMTYPESET_LIBRARY=<out>/libimtypeset_bundle.so  PYTHONPATH=<this repository>/python  python app.py
```

The variable is read by the package of a source checkout only; a package
installed from a wheel ignores it and says so in
`host_info()["ignored_environment"]` (D73).

```python
import imtypeset            # raises ImportError, with the reason, if the pairing is refused
imtypeset.host_info()       # which ImGui, which headers, how the pairing was checked
```

Create the imtypeset context and add its fonts in hello_imgui's
`load_additional_fonts` callback, never in `post_init`: hello_imgui empties the
atlas after `post_init`.

**What the script needs**, all read-only:

- `g++` (or `CXX`), and the interpreter with its wheel;
- a clone of Dear ImGui Bundle (first argument, default `../imgui_bundle`): the
  ImGui configuration header of the wheel's release tag is read from its git
  objects (`imgui_bundle_cmake/imgui_bundle_config.h`);
- Dear ImGui's headers for that version. By default the fork the wheel is
  built from, fetched from GitHub at the commit `python/host/bundle_pins.toml`
  records for the wheel's version (`python/host/fetch_headers.py`; **never
  run against GitHub**). With `IMTYPESET_BUNDLE_HEADERS=stand-in`, and only
  then, upstream's `v<version>-docking` tag from a clone of `ocornut/imgui`
  (`IMTYPESET_IMGUI_CLONE`, default `../imgui`).

**What it does**, which is short enough to do by hand:

1. `tools/amalgamate.py --out <out>/dist` (section 2): the library as one file.
2. Four compiler lines: `<out>/dist/imtypeset.cpp`, `imtypeset_c.cpp`,
   `imtypeset_demo.cpp` and `python/host/imtypeset_bundle_shim.cpp`, with
   `-O2 -fPIC -fvisibility=hidden -ffp-contract=off -fexceptions`, the bundle's
   configuration (`IMGUI_USER_CONFIG`, `IMGUI_BUNDLE_BUILD_PYTHON`,
   `IMGUI_USE_WCHAR32`, `ImTextureID=int`, `IMGUI_DISABLE_OBSOLETE_FUNCTIONS`,
   `IMGUI_ENABLE_FREETYPE`). The demo window is in the library. Its two text
   fields call `ImGui::InputTextMultiline` and `InputTextWithHint`, whose
   callback the wheel's fork takes as a `std::function`; under stand-in headers
   (`IMTYPESET_BUNDLE_STAND_IN`) the shim defines upstream's two names and
   hands over to the wheel's (D55).
3. One link line against the wheel's module with `-Wl,--no-undefined` and a
   version script, so the library exports `ImTypeset_*` and `ImTypesetBundle_*`
   and nothing else.
4. The import gate, in a child interpreter (`tools/bundle_route/run_gate.py`),
   whose verdict is written beside the library as
   `<library>.attestation.json`.

Exceptions are on because the wheel's `IM_ASSERT` throws; the C ABI turns a
C++ exception into an error status at each entry.

**Ran** (both through the amalgamation; first on 2026-10-10 at 28 s each and
22 probe rows, last on 2026-10-12 by task `v030-gate` at 37 s each, which is
what the table says):

| interpreter | wheel | headers | gate | example |
|---|---|---|---|---|
| `../pptmstr/.venv` | imgui_bundle 1.92.801, Dear ImGui 1.92.8 | stand-in `v1.92.8-docking` | accepted, 25 probes | `examples/python/bundle_host.py --frames 30` on Xvfb: selection (74, 284), 2 layouts |
| `../imgui-adv-text/.venv` | imgui_bundle 1.92.900, Dear ImGui 1.92.9 | stand-in `v1.92.9-docking` | accepted, 25 probes | the same numbers; its screenshot was not looked at this time |

The library exports 97 `ImTypeset_*` and 9 `ImTypesetBundle_*` symbols (`nm -D
--defined-only`, type `T`; 106 defined in all) and leaves 194 symbols
undefined, 117 of them Dear ImGui's for the wheel to supply (by `nm -D
--undefined-only | c++filt`, names in `ImGui::` or a member of one of its
classes); counted on 2026-10-12, with the demo window built in, for both
wheels alike. On 2026-10-11 the same count gave 95 and 8, and 99 functions
and 18 members among the 117; that split was not counted again.

**The caveat that remains.** Both libraries above are built against stand-in
headers. Dear ImGui's own layout check then refuses one size
(`sizeof(ImGuiStyle)`: the fork adds a field), and the package accepts the
library because a probe of every ImGui field the library touches passes
(`python/imtypeset/_bundle.py`). `IMTYPESET_BUNDLE_STRICT=1` refuses stand-in
libraries. A library that passes ImGui's own check needs the fork's headers,
which the script fetches when it is not told `stand-in`, and which needs the
network. No library has been compiled against them, anywhere.

**When the wheel is upgraded** the import fails (another module path, another
ImGui version, or another binary of the same version) and the remedy is to run
the script again.

**This section describes a development build**, and parts of it are older
than the script: `docs/sidecar.md` is the current account (headers are
fetched and never substituted; the last step writes an attestation; the gate
no longer starts a child at import). **The way into an application is a
wheel**: `tools/wheel/build_wheel.py` puts this library inside the Python
package, pinned to its imgui-bundle, and `import imtypeset` then needs no
variable (`docs/sidecar.md`, sections 8 to 12; `docs/versioning.md`). Linux,
x86-64, GCC, CPython 3.11 and a GNU linker are all that ran; a wheel of
imgui-bundle ships no import library, so the Windows equivalent of step 3
does not exist as written.

### The other Python library: self-hosted

`python/` also builds `libimtypeset_pyhost.so`, which holds its own Dear ImGui,
SDL2 and imtypeset and opens its own window. It is the test rig of the binding
(`tests/python/test_binding.py`, ctest test `python_binding`), not the way into
an application:

```sh
cmake --preset python-host && cmake --build --preset python-host && ctest --preset python-host
```

It needs an SDL2, found as `cmake/sdl2.cmake` says and as the demo host finds
it: `-DIMTYPESET_SDL2_DIR=` when given; else an installed one, by
`find_package(SDL2 CONFIG)` (a target, or the variables older packages set) and
then by pkg-config, which is what a distribution's `libsdl2-dev` or `SDL2-devel`
provides; else the untracked `.deps/sdl2` of this tree, which
`tools/vendor_sdl2.sh <SDL2 install prefix or build tree>` fills. The configure
step prints the route, and stops with the list where there is none. **The
installed-package route has not been run against a distribution's package**
(this machine has none): each branch of it was run against a hand-written
`sdl2.pc` or `SDL2Config.cmake` that points at `.deps/sdl2`, and this library
was built and `python_binding` passed through the `sdl2.pc` one (D52). The
test skips, with the reason, when it has no interpreter. With `IMTYPESET_BUILD_PYTHON_HOST` on, the `imtypeset`
target is position-independent: take no timing from such a tree.

---

## 2. C++: the drop-in files

Dear ImGui is consumed by adding its files to a project, and an extension is
expected to be consumable the same way. `src/` is 19 sources and 23 private
headers with names like `nodes.cpp` and `units.h`, so what a host is given is
generated from it:

```sh
python3 -I -B tools/amalgamate.py             # writes dist/ (git-ignored)
python3 -I -B tools/amalgamate.py --check     # exit 1 if dist/ is missing, stale or edited
```

| file | what | needed |
|---|---|---|
| `imtypeset.h` | the public header, unchanged | yes |
| `imtypeset.cpp` | every source but the two below and every private header, as one translation unit (about 29,000 lines) | yes |
| `imtypeset_demo.cpp` | `ImTypeset::ShowDemoWindow()`, `ShowDemoSection()`, `GetDemoSectionName()`; `IMTYPESET_DISABLE_DEMO` compiles them to functions that draw nothing | optional, as `imgui_demo.cpp` is |
| `imtypeset_c.h`, `imtypeset_c.cpp` | the C ABI, for bindings | optional |

Add them next to ImGui's files and compile them as ImGui's are compiled:

```sh
c++ -std=c++17 -c imtypeset.cpp -I<directory holding imgui.h>
```

That line, for each of the three `.cpp` files, with no other flag, is the ctest
test `amalgamation` (`tests/amalgamation/compile.cmake`): **ran**, 12 s, about
0.5 GB for the large file at `-O0`; 37 s and 0.8 GB at `-O2 -g`. With this
tree's warning set (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion`) the three
files give no warning under GCC 12.2.

What a drop-in host should know:

- **C++17.** Nothing else is required of the build. The files build with or
  without exceptions and RTTI.
- **No define is needed.** The library follows the host's ImGui: its `ImWchar`
  width, its rasteriser (FreeType or stb_truetype), its `ImDrawIdx`, its
  `IMGUI_USER_CONFIG`. Compile the files with the same ImGui configuration as
  the rest of the program, and call `IMTYPESET_CHECKVERSION()` after
  `ImGui::CreateContext()`: it is `IMGUI_CHECKVERSION()` for this library.
- **Floating point** — section 5. The short form: do not compile
  `imtypeset.cpp` with `-ffast-math` (it refuses), and prefer passing
  `-ffp-contract=off` for it.
- **16-bit `ImWchar`** (stock ImGui without `IMGUI_USE_WCHAR32`): the math
  face's glyphs beyond the BMP are reached through private-use stand-ins from
  U+E000 up, so nothing may be merged into a font this library returned (icon
  fonts live in the same range). The header says so at `AddFont*`.
- The files are an artefact. `src/` is the source of truth, and the build
  compiles the generated files as a test so that they cannot rot.

The whole suite runs against the generated files in the `amalgamation` preset
(the library target is built from them in place of `src/*.cpp`).

---

## 3. C++: `add_subdirectory`

```cmake
# the host's Dear ImGui target exists by now: `imgui`, or `imgui::imgui`
add_subdirectory(third_party/imtypeset)
target_link_libraries(app PRIVATE imtypeset::imtypeset)
```

**The contract with the host's ImGui.** The library is built against exactly
one ImGui target and links it `PUBLIC`. First match wins:

1. `IMTYPESET_IMGUI_TARGET`, when the host names its target;
2. the package's `imgui::imgui`, when `IMTYPESET_USE_IMGUI_PACKAGE=ON`;
3. an existing `imgui::imgui` (a package, vcpkg, hello_imgui's package route);
4. an existing `imgui` (hello_imgui's default build, most hand-written hosts);
5. one built here from `cmake/imgui.cmake`, when `IMTYPESET_BUILD_IMGUI` is on —
   which it is by default only when this repository is the top-level project.

With none of those, **configuring fails** and the message names the four ways
out. It does not build a private ImGui inside a host that has its own under
another name: that is a program with two ImGuis.

Nothing is defined on a host's target, and no `find_package(Freetype)` is made
on its behalf. Included by another project, the tests, benchmarks and examples
are off, `-Werror` is off, and nothing is installed.

A host that builds with `CMAKE_UNITY_BUILD` can: `src/*.cpp` compile as one
translation unit (step 1 of the consumer test builds the host that way).

Options a host might set: `IMTYPESET_IMGUI_TARGET`,
`IMTYPESET_ENABLE_EXCEPTIONS` (compile the library with exceptions and RTTI, for
a host whose `IM_ASSERT` throws), `IMTYPESET_AMALGAMATED` (build the target from
the generated files; needs a Python 3 at build time).

**hello_imgui hosts**: `add_subdirectory(imtypeset)` after hello_imgui (its
`imgui` target exists by then), then `hello_imgui_add_app(app main.cpp)` and the
`target_link_libraries` line above. *Not run*: hello_imgui's submodules are
empty on this machine and it cannot fetch.

---

## 4. C++: the installed package

```sh
cmake -S . -B build/pkg -DIMTYPESET_USE_IMGUI_PACKAGE=ON \
      -DCMAKE_PREFIX_PATH=<where the imgui package is> -DCMAKE_INSTALL_PREFIX=<prefix> \
      -DIMTYPESET_BUILD_TESTS=OFF -DIMTYPESET_BUILD_BENCH=OFF -DIMTYPESET_BUILD_EXAMPLES=OFF
cmake --build build/pkg --target install
```

```cmake
find_package(imtypeset CONFIG REQUIRED)
target_link_libraries(app PRIVATE imtypeset::imtypeset)
```

Installed: `lib/libimtypeset.a`, `include/imtypeset.h`, `include/imtypeset_c.h`,
and `lib/cmake/imtypeset/` (`imtypesetConfig.cmake`, the version file,
`imtypesetTargets.cmake`). The version file accepts the same minor version only
(0.x: a minor version may change the API).

**Only against an installed ImGui, and ImGui is never installed from here.**
The package's one dependency line is `find_dependency(imgui CONFIG)`, and the
exported target links `imgui::imgui`. `IMTYPESET_INSTALL` is on by default when
this is the top-level project and its ImGui target is an imported one; asking
for it against an ImGui built in the tree is an error that says why. Dear ImGui
ships no CMake package of its own, so "an installed ImGui" means a package
manager's (vcpkg's port) or the host's own.

An installed static library compiled against one ImGui configuration and used
with another is what `IMTYPESET_CHECKVERSION()` catches at run time; the package
also records `imtypeset_IMGUI_VERSION_NUM`.

**Ran**: `tests/consumer/run.cmake` (15 steps, 26 s). The ImGui package it uses
is a fixture (`tests/consumer/imgui_package`: ImGui 1.92.9 built and installed
with a hand-written config defining `imgui::imgui`), not a package manager's.

### vcpkg — a sketch, untested

`vcpkg.json` at the root is a manifest (this tree's own dependency when
configured under a vcpkg toolchain with `IMTYPESET_USE_IMGUI_PACKAGE=ON`), and
`ports/imtypeset/` is the outline of a port. **Neither has been run**: there is
no vcpkg on this machine and no network. Assumed and not checked: that vcpkg's
`imgui` port installs a config package whose target is `imgui::imgui`, and how
it spells a version floor. The port has no repository, reference, hash or
licence to name yet.

---

## 5. Floating point

The library's results are compared with the reference's to the last bit, and
are defined as IEEE double arithmetic with each operation rounded on its own.
Every configuration of this tree's own build passes `-ffp-contract=off`
(`/fp:precise` in the MSVC branch) and never a fast-math flag: to this tree's
sources, and to the Dear ImGui it compiles itself from `.deps/imgui` or
`IMTYPESET_IMGUI_DIR` (`cmake/imgui.cmake`, and the backends the demo host and
the Python host compile). It is never added to an ImGui target a host brings
(`add_subdirectory` into a project that has one, `imgui::imgui` from a
package, the wheel's ImGui under the sidecar): that is compiled however the
host compiles it, and D52 says why the difference is accepted. A drop-in host
compiles `imtypeset.cpp` with its own flags, so the question was measured.

GCC 12.2.0, `-O3 -march=native` on a CPU with FMA, `-std=c++17`. (GCC's default
there is to contract: a one-line probe compiles `a*b+c` to `vfmadd`.) The
generated file as the library, ImGui and the tests compiled as in `release`:

| `imtypeset.cpp` compiled with | FMA instructions in the object | suite |
|---|---|---|
| `-O3 -march=native`, no pragma in the file (`amalgamate.py --no-floating-point-guard`) | 65 | **38 of 41**: `test_opentype` (776 checks), `test_fontlib` (1,384 checks), `test_opentype_wild` (84 corpus faces) fail |
| `-O3 -march=native`, the file as generated | 0 | 41 of 41 (preset `fp-host`) |
| `-ffast-math` | — | does not compile: `#error` (`src/units.h`), unless `IMTYPESET_ALLOW_FAST_MATH` |

So contraction does change results, and the pragma removes the change. What
moved without it is the font reader's ink boxes (curve extrema), by one unit in
the last place: `10.255066522277227` against the reference's
`10.255066522277229`. Every layout, drawing and API golden still passed in that
run. That is one compiler on one CPU: a box that is one ulp off can round to a
different scaled point at some size, and nothing here shows that it cannot.

What the generated file does about it:

- `#pragma GCC optimize("fp-contract=off")` for GCC (which ignores
  `#pragma STDC FP_CONTRACT OFF`, with a warning under `-Wunknown-pragmas`:
  checked); `#pragma STDC FP_CONTRACT OFF` for Clang; `#pragma fp_contract(off)`
  for MSVC. **Only the GCC line has been compiled and measured.** There is no
  Clang or MSVC on this machine.
- `#error` when `__FAST_MATH__`, `_M_FP_FAST` or a non-zero
  `__FINITE_MATH_ONLY__` is defined, unless `IMTYPESET_ALLOW_FAST_MATH` is. The
  check is in `src/units.h`, so the CMake build has it too: a host that adds
  `-ffast-math` to its global flags and uses `add_subdirectory` is refused the
  same way.
- Not detectable from the source: `-funsafe-math-optimizations` and
  `-fassociative-math` on their own (they define no macro). Do not use them
  for this file.

`fp-host` is the one preset in which `-ffp-contract=off` is not on the
library's command line; `build/fp-host/compile_commands.json` shows it on every
other entry, Dear ImGui's files and the one C test included.

---

## 6. Dear ImGui versions

`include/imtypeset.h` states two numbers and the build system reads them from
it (`CMakeLists.txt`, `cmake/imgui_version.cmake`), so the two cannot disagree:

| | number | meaning |
|---|---|---|
| `IMTYPESET_IMGUI_VERSION_NUM_MIN` | 19200 (v1.92.0) | the floor: `#error` in the header, `FATAL_ERROR` at configure |
| `IMTYPESET_IMGUI_VERSION_NUM` | 19290 (v1.92.9) | the tested version: what `.deps/imgui` and the fetch are, and what the draw goldens were dumped against. Above it, a configure-time **warning** |

**How the floor was decided: by building.** ImGui's release tags were taken
from a clone's git objects and the whole tree configured against each with
`IMTYPESET_IMGUI_DIR`:

| Dear ImGui | library | suite (debug, FreeType 2.12.1, 32-bit `ImWchar`) |
|---|---|---|
| v1.91.9 (19190) | does not compile: 22 errors (no dynamic font atlas) | — |
| v1.92.0 (19200) | 0 warnings | 41 of 41 |
| v1.92.1, .2, .4, .6 | 0 warnings (all sources as one unit, syntax only) | not run |
| v1.92.8 (19280) — the ImGui of imgui_bundle 1.92.801 | 0 warnings | 41 of 41 |
| v1.92.9 (19290) | 0 warnings | every preset, section 7 |
| 1.93.0 WIP (19298), `../imgui` | 0 warnings | preset `imgui-next`, section 7 |

On 1.92.0 and on 1.92.8 the draw test prints the same census as on 1.92.9
(0 of 583 glyph boxes differ from the reference's, 22 of 22 cases equal its
buffers outright). The library uses `imgui.h` only, never `imgui_internal.h`;
it does use four members of `ImDrawList` that `imgui.h` marks `[Internal]`
(`_VtxWritePtr`, `_IdxWritePtr`, `_VtxCurrentIdx`, and `_CmdHeader.ClipRect`
read-only), which are the same in every version above.

Three things the *tests* use are newer than the floor, and
`tests/support/imgui_compat.h` holds their version numbers (each read from the
commit that introduced it): `ImGuiPlatformIO`'s draw callbacks (19274),
`ImDrawListFlags_TextNoPixelSnap` (19282) and the merge of `ImDrawListFlags`
into `ImDrawFlags` (19298). The library uses none of them.

Not run: the docking branch as a source build (the bundle wheels are docking
builds, and the sidecar runs in them); anything between 1.92.2 and 1.92.8 beyond
a syntax check; 1.93 in any configuration but debug.

---

## 7. Presets

`cmake --preset X && cmake --build --preset X && ctest --preset X`. Build and
test presets run four jobs. The two sanitised test presets set
`ASAN_OPTIONS=hard_rss_limit_mb=3000`.

| preset | what | ctest, 2026-10-12 |
|---|---|---|
| `debug` | Debug; the consumer-project test runs here | 53 of 53 |
| `release` | `-O3`; the one to take a timing from | 52 of 52 |
| `asan` | ASan + UBSan, the C test included | 52 of 52 |
| `stb` | stb_truetype in place of FreeType | 52 of 52 |
| `wchar16` | ImGui's default 16-bit `ImWchar` | 52 of 52 |
| `amalgamation` | the library built from the generated drop-in files | 52 of 52 |
| `fp-host` | release, the drop-in files at `-O3 -march=native` with **no** `-ffp-contract=off` on the library (section 5) | 52 of 52 |
| `exceptions` | ASan + UBSan, exceptions and RTTI on, `IM_ASSERT` throws (`tests/imconfig_throwing.h`); `test_exception_safety` does its work only here | 52 of 52 |
| `bundle-config` | exceptions on, Dear ImGui Bundle's own configuration header from `../imgui_bundle` (throwing `IM_ASSERT`, 32-bit `ImDrawIdx`), `IMGUI_DISABLE_OBSOLETE_FUNCTIONS`. `test_exception_safety` reports itself skipped here: it catches the exception type of `tests/imconfig_throwing.h`, which this configuration does not use | 52 of 52 |
| `imgui-next` | the Dear ImGui checked out in `../imgui` (1.93.0 WIP, 19298) | 52 of 52 |
| `python-host` | debug plus `python/` and the `python_binding` and `python_demo_sections` tests (SDL2 from `.deps/sdl2`) | 54 of 54 |

Each was run on the tree the first tag is to be made from (D1 to D76), by GCC
12.2 on Linux x86-64 and by nothing else, one at a time in a single pass, from a directory under `build/` that did not exist before it, FreeType 2.12.1 except `stb`, 32-bit `ImWchar`
except `wchar16`. No warning from this tree's sources in any of them;
`exceptions` and `bundle-config` each print four `-Wterminate` warnings, all in
Dear ImGui's own destructors (`~ImDrawListSharedData`, `~ImFontAtlas`,
`~ImGuiContext`, `~ImGuiWindow`), where the throwing `IM_ASSERT` is expanded.
A build at `-O2` (`RelWithDebInfo`, a private directory, not a preset): 41 of 41
on 2026-10-10, on the tree before D52's changes; not run again since.

Also run once in a private directory and not a preset: 1.93.0 WIP with
`IMGUI_DISABLE_OBSOLETE_FUNCTIONS` (`-DIMTYPESET_IMGUI_DIR=../imgui
-DIMTYPESET_IMGUI_DEFINITIONS=IMGUI_DISABLE_OBSOLETE_FUNCTIONS`), debug: 41 of 41 (2026-10-10; not run again since).

`bundle-config` and `imgui-next` need a sibling checkout, and `python-host` an
SDL2 (installed, or `.deps/sdl2`), and fail at configure where it is absent: CMake presets have no condition that tests for
a directory.

`consumer_projects` runs in `debug` only (`IMTYPESET_BUILD_CONSUMER_TESTS`): it
builds ImGui and the library three more times. `amalgamation` (the plain
compiler lines) runs in every preset that builds its own ImGui.

---

## 8. Not run, not verified

- **Compilers and platforms**: only GCC 12.2 on Linux x86-64. Clang, MSVC,
  macOS, Windows: nothing. The Clang and MSVC contraction pragmas, the MSVC
  branch of the CMake flags, `IMTYPESET_API` as a DLL export: never compiled.
- **vcpkg**: the manifest and the port sketch.
- **hello_imgui** as a CMake host; **imgui_bundle** as a source build with
  imtypeset bound the bundle's way (`bindings/imgui_bundle/`, backlog).
- **The fork's headers** for the Python sidecar, and so a sidecar that passes
  Dear ImGui's own layout check.
- **An ImGui package from a package manager**: the install route was run
  against this tree's fixture only.
- **A shared `libimtypeset`** through CMake (`BUILD_SHARED_LIBS`): not tried.
  Dear ImGui recommends static linking, and the two Python libraries are built
  by their own scripts.
- **A host's warning flags** beyond this tree's set (`-Wold-style-cast`,
  `-Wsign-conversion` under Clang, `/W4`).
- **Whether a one-ulp ink box can move a layout** (section 5): not shown either
  way.
