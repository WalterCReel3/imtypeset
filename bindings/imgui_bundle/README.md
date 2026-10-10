# Draft bindings for Dear ImGui Bundle

**UNRUN.** Nothing in this directory has been executed, generated or compiled. litgen and nanobind are not installed on the machine it was written on, PyPI and GitHub were not reachable, and the bundle clone it was read from (`../imgui_bundle`, `v1.92.900-723-gf639fda4`) has no submodule checked out, so the bundle could not be built. What is here is what a reading of the bundle's own files says a new library needs, shaped file for file on `external/implot/bindings/`. Expect to correct it on the first run.

`docs/bindings-notes.md` has the reasoning: how the bundle adds a library, what each declaration of `include/imtypeset.h` needs, and what was measured against the installed wheel.

The files are written against `include/imtypeset.h` 0.2.0, the header as task `api-revise` left it (`docs/decisions.md` D36); task `c-api` brought them to it, by reading. The last section says what changed. Not through a generator: for a binding that needs none, `include/imtypeset_c.h` is the C ABI (`docs/bindings-notes.md`, section 5.5).

## What is here and where it goes

`<bundle>` is a checkout of https://github.com/pthom/imgui_bundle.

| file here | goes to | what it is |
|---|---|---|
| `litgen_options_imtypeset.py` | `<bundle>/external/imtypeset/bindings/` | the litgen options, started from imgui's as implot's are |
| `generate_imtypeset.py` | `<bundle>/external/imtypeset/bindings/` | the generator script and the hand-written bindings: two opaque handles, seven functions |
| `pybind_imtypeset.cpp` | `<bundle>/external/imtypeset/bindings/` | the frame litgen writes the C++ bindings into |
| `imtypeset.pyi` | `<bundle>/bindings/imgui_bundle/` | the frame litgen writes the stubs into |

The library itself goes to `<bundle>/external/imtypeset/imtypeset/` as a git submodule: the bundle's layout is `external/<lib>/<lib>/` for the code and `external/<lib>/bindings/` for the bindings (`external/_doc/Howto_Bindings.md`, "Typical bound library structure").

## What else has to be edited in the bundle

In the order `docs/book/devel_docs/bindings_newlib.md` gives them.

1. **Submodule.** `git submodule add <url> external/imtypeset/imtypeset` (bindings_newlib.md, step 1-a).
2. **Registry.** In `external/bindings_generation/bundle_libs_tooling/all_external_libraries.py`, add a `lib_imtypeset()` returning an `ExternalLibrary(name="imtypeset", official_git_url=..., official_branch="main")` and put it in `ALL_LIBS` after `lib_imgui()`, which has to stay first (the file's own comment at the head of `ALL_LIBS`, line 238 on; implot's entry is at 171-178). No `fork_git_url` unless the bundle's author wants a fork.
3. **CMake.** In `external/CMakeLists.txt`, next to implot's block (114-124). `add_simple_external_library_with_sources(imtypeset imtypeset)` is NOT enough as the repository is laid out today: that helper globs `external/<lib>/<lib>/*.cpp` and `*.h`, flat (`imgui_bundle_cmake/internal/add_simple_library.cmake:21-35`), and this repository keeps its sources in `src/` and its header in `include/`. Either:
   ```cmake
   option(IMGUI_BUNDLE_WITH_IMTYPESET "Build with imtypeset" ON)   # in the root CMakeLists.txt, next to line 179
   if(IMGUI_BUNDLE_WITH_IMTYPESET)
       add_simple_external_library(imtypeset imtypeset)
       file(GLOB imtypeset_sources ${CMAKE_CURRENT_LIST_DIR}/imtypeset/imtypeset/src/*.cpp)
       target_sources(imtypeset PRIVATE ${imtypeset_sources})
       target_include_directories(imtypeset PUBLIC
           $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/imtypeset/imtypeset/include>)
       # Layout arithmetic is compared bit for bit with a reference: no contraction (docs/decisions.md D16).
       if(NOT MSVC)
           target_compile_options(imtypeset PRIVATE -ffp-contract=off)
       else()
           target_compile_options(imtypeset PRIVATE /fp:precise)
       endif()
       lg_disable_warning_exception_in_destructor(imtypeset)
       target_compile_definitions(imgui_bundle INTERFACE IMGUI_BUNDLE_WITH_IMTYPESET)
       set(IMGUI_BUNDLE_WITH_IMTYPESET ON CACHE INTERNAL "" FORCE)
   endif()
   ```
   or, once task `dist` has produced the generated single `imtypeset.cpp` next to `imtypeset.h`, the one-line helper works as it does for implot. The option and the define carry the same name, which is the bundle's rule (`AGENTS.md`, "Code Conventions").
   **Do not pass `-fno-exceptions`.** Every target that links the bundle's `imgui` inherits `IMGUI_USER_CONFIG=imgui_bundle_config.h` (`imgui_bundle_cmake/internal/add_imgui.cmake:48-49`), whose `IM_ASSERT` is a `throw` in a Python build (`imgui_bundle_cmake/imgui_bundle_config.h:38-47`). Measured here: with that header, 14 of the 15 `src/*.cpp` fail under `-fno-exceptions` ("exception handling disabled") and all 15 pass a syntax check with exceptions on.
4. **The list of binding sources.** `external/bindings_generation/cpp/all_pybind_files.cmake` gets `${external_dir}/imtypeset/bindings/pybind_imtypeset.cpp`. `autogenerate_all.py` rewrites that file from the registry (31-55), so step 2 plus one run does it.
5. **The module.** In `external/bindings_generation/cpp/pybind_imgui_bundle.cpp`, as for implot at lines 15 and 109-120:
   ```cpp
   void py_init_module_imtypeset(nb::module_& m);
   ...
   #ifdef IMGUI_BUNDLE_WITH_IMTYPESET
       _register_submodule("imtypeset");
       auto module_imtypeset = m.def_submodule("imtypeset");
       py_init_module_imtypeset(module_imtypeset);
   #else
       _register_submodule("imtypeset", false);
   #endif
   ```
6. **The package.** `bindings/imgui_bundle/__init__.py`, as for implot at 167-181:
   ```python
   if has_submodule("imtypeset"):
       from imgui_bundle._imgui_bundle import imtypeset as imtypeset
       _publish("imtypeset", imtypeset)
       __all__.extend(["imtypeset"])
   ```
   and `from . import imtypeset as imtypeset` in `bindings/imgui_bundle/__init__.pyi` (implot's line is 4).
7. **Generate.** `just libs_bindings imtypeset` (`justfile:57-60`), then build with the `python_bindings` preset (`AGENTS.md`, "Build folders"), then `just mypy` and `just test_pytest` (`docs/book/devel_docs/testing.md`).
8. **A demo**, in `bindings/imgui_bundle/demos_python/` and `demos_cpp/` (bindings_newlib.md, step 2-c). The bundle's rules for one are in `AGENTS.md` ("Demos").

## What to look at first when it is run

- The two opaque handles. `ImTypesetContext` and `ImTypesetParagraph` are declared in the public header and defined in `src/context.h`. They are registered as empty classes in a custom binding, and `pybind_imtypeset.cpp` includes `context.h` so that nanobind sees complete types: this is what the bundle does for `RichMd::Context` (`external/imgui_rich_md/bindings/generate_rich_md.py:50-59`, `pybind_rich_md.cpp:18`). It needs `src/` on the binding's include path (add `$<BUILD_INTERFACE:.../imtypeset/imtypeset/src>` as a PRIVATE include of the bindings target, or PUBLIC on `imtypeset`), and it was never compiled.
- Whether litgen keeps or drops the contents of `#ifndef IMTYPESET_API ... #endif` and of the `#if !defined(IMTYPESET_DISABLE_IMGUI_VERSION_CHECK) ...` block. Both hold only preprocessor lines, so either answer should be harmless; implot's options had to name the guards it wanted read (`litgen_options_implot.py:19`).
- The docstrings. litgen takes a comment directly above a declaration as its doc, and a comment followed by an empty line as a section title (`docs/book/devel_docs/bindings_intro.md`, "Docstrings"). The header's long comments (the block above the widgets, the one inside `ImTypesetTextFlags_`) are written for a C++ reader and will arrive whole as Python docstrings.
- `LG_MODULE` in `generate_imtypeset.py` is the placeholder imgui's options use for a custom binding of the module (`external/imgui/bindings/litgen_options_imgui.py:614-622`). The lambdas there were never compiled as nanobind bindings; their bodies pass `g++ -fsyntax-only` against the header with `std::string` in place of `nb::bytes`.

## What `api-revise` changed here

Applied to these files by task `c-api`, by reading the revised header; unrun like the rest.

| change in the header (D36) | what was done here |
|---|---|
| `CreateContext()` takes nothing; `IMTYPESET_CHECKVERSION()` | the block of the options that dropped the five build-check parameters is gone; `py_init_module_imtypeset` calls the macro and throws with `GetLastError()->Message` if it fails; `DebugCheckVersionAndDataLayout` is excluded and not rebound |
| `Text` became `TextUnformatted(text, text_end, ...)`; `Text(fmt, ...)` and `TextV` added; `TextWithID`, `CalcTextSize` | the `text_end` exclusion covers the three with a defaulted `text_end`. `Text(fmt, ...)` is left to the generator (one `str` through `"%s"`); `TextV` is excluded |
| `SelectableText(..., ImTypesetTextRange* selection)` | by hand: `Optional[TextRange]` in, `(bool, TextRange)` out |
| byte offsets everywhere; `ImTypesetSelection` is `ImTypesetTextRange`; `...SelectionRects` / `...SelectionText` are `...RangeRects` / `...RangeText` | renamed in the custom bindings and in `class_copy__regex` |
| `TextCodepointToByteOffset`, `TextByteToCodepointOffset` | by hand, two lines each: their `text_end` has no default, so it cannot be dropped by name |
| `GetParagraphFormula` returns the struct | no longer by hand |
| `ImTypesetLayoutFlags` became `ImTypesetLineBreak`; `ImTypesetStatusFlags` added | the stub's alias list |
| `GetStats()` by value, `ImTypesetLine`, `ImTypesetHit`, `GetParagraphLine`, `GetParagraphCaretRect`, `GetParagraphStatus`, `RemoveFont`, `SetTextCacheLimits`, the two `Paragraph...Offset` | nothing: as written |
| `EnableMath()`, `TextJustified`, `GetParagraphLength`, the two index/offset functions removed | nothing: generated functions fewer |
