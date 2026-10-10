# c-api — result (builder)

A C ABI over the whole public API of `include/imtypeset.h` 0.2.0 (D36), a test of it compiled by a
C compiler, and a test that holds the two headers to each other declaration for declaration.

## Files

| file | state |
|---|---|
| `include/imtypeset_c.h` (563 lines) | found on disk after the session loss, complete; read end to end, kept as it was apart from clang-format |
| `src/imtypeset_c.cpp` (933) | found on disk, complete; kept apart from clang-format. Built into the library by the `src/*.cpp` glob, so it is in every build of the tree |
| `tests/c_api_host.cpp` (294) | found on disk and NOT truncated (it ended at its closing brace). Changed: the asserting-font-config helper replaced by a throwing ImGui allocator (see "Exceptions") |
| `tests/test_c_api.c` (947) | new. C99, includes `imtypeset_c.h` and three C standard headers only |
| `tests/test_c_api_map.cpp` (536) | new. 7 tests |
| `tests/CMakeLists.txt` | edited, see below |
| `docs/bindings-notes.md` | section 5 rewritten for the 0.2.0 header with a column for the C ABI and a new 5.5; the sketch and three bullets of section 1, one sentence of 7 and two bullets of 9 corrected. Sections 2-4, 6, 8 untouched |
| `bindings/imgui_bundle/*` (5 files) | brought to the 0.2.0 header by reading; still UNRUN and say so |
| `.concerns/c-api--builder--doc-additions.md` | text for `docs/decisions.md` (one entry) and one optional line for `docs/hardening.md`; nothing for `THIRD-PARTY.md` or `docs/divergences.md` |
| `.concerns/c-api--builder--progress.md` | the crash log of this run |

Not touched: `include/imtypeset.h`, `src/imtypeset.cpp`, `src/context.h`, the root `CMakeLists.txt`,
`docs/decisions.md`, `THIRD-PARTY.md`. Nothing was `git add`ed or committed.

## The exact edit to `tests/CMakeLists.txt`

After the line `list(APPEND _imtypeset_all_tests "${CMAKE_CURRENT_SOURCE_DIR}/smoke.cpp")`, two
lines went in BEFORE the existing `imtypeset_select(...)` call and one block AFTER it. Nothing else
in the file changed.

```cmake
# test_c_api is the one test a C compiler builds; it is selected by name like
# the others and built further down.
set(_imtypeset_c_api_test "${CMAKE_CURRENT_SOURCE_DIR}/test_c_api.c")
list(APPEND _imtypeset_all_tests "${_imtypeset_c_api_test}")
imtypeset_select(_imtypeset_tests "test" "${IMTYPESET_TESTS}" ${_imtypeset_all_tests})   # unchanged

# The C ABI, used from C: ...
if(_imtypeset_c_api_test IN_LIST _imtypeset_tests)
    list(REMOVE_ITEM _imtypeset_tests "${_imtypeset_c_api_test}")
    enable_language(C)
    add_library(c_api_host STATIC "${CMAKE_CURRENT_SOURCE_DIR}/c_api_host.cpp")
    target_link_libraries(c_api_host PRIVATE imtypeset::imtypeset imtypeset_build_flags)
    target_compile_definitions(c_api_host PRIVATE
        "IMTYPESET_TEST_DATA_DIR=\"${PROJECT_SOURCE_DIR}/tests/data\"")
    add_executable(test_c_api "${_imtypeset_c_api_test}")
    set_target_properties(test_c_api PROPERTIES
        C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE CXX)
    target_include_directories(test_c_api PRIVATE "${PROJECT_SOURCE_DIR}/include")
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test_c_api PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    target_link_libraries(test_c_api PRIVATE c_api_host)
    add_test(NAME test_c_api COMMAND test_c_api)
    set_tests_properties(test_c_api PROPERTIES TIMEOUT 600)
endif()
```

- **Two targets, not the one the spec says.** `test_c_api` is the C file alone, with C flags
  alone; `c_api_host` is the C++ half, compiled with the tree's flags like every other test. One
  target holding both would hand `-fno-exceptions -fno-rtti -Wshadow ...` to the C compiler
  through `imtypeset_build_flags`, or need generator expressions per language.
- **C is enabled only when the test is built**: `IMTYPESET_TESTS=test_badness` configures with no C
  compiler looked for (checked: 0 mentions of `test_c_api` in that `build.ninja`); an un-narrowed
  configure builds it (checked). `test_c_api` is a valid name in `IMTYPESET_TESTS`.
- **The root `CMakeLists.txt` needed no line.** `enable_language(C)` in `tests/` works with Ninja
  here. CMake's documentation recommends enabling a language at the top level; if the lead wants
  that, the line is `LANGUAGES C CXX` in the root `project()` call, and `enable_language(C)` here
  then becomes a no-op that can stay. I did not ask for it: it would make every project that
  `add_subdirectory()`s this one find a C compiler it does not need.
- `-ffp-contract=off` is not on the C test: it holds no arithmetic that is compared.

## What was built and run

Private directories `build/c-api` (debug) and `build/c-api-{release,stb,wchar16,exc,asan,asanexc}`,
GCC 12.2.0 for C and C++, ImGui 1.92.9 from `.deps/imgui`, `-j4`, `-Werror`, on the final sources.
All modules; tests narrowed to `test_c_api;test_c_api_map;test_header_pod;smoke`. Every binary ran
under `( ulimit -v 6000000; timeout 600 ... )`, the sanitised ones under
`ASAN_OPTIONS=hard_rss_limit_mb=3000 timeout 600`.

| configuration | `test_c_api` | `test_c_api_map` | `test_header_pod` | `smoke` | warnings in our sources |
|---|---|---|---|---|---|
| debug, FreeType 2.12.1, 32-bit ImWchar | 376 checks, 0 failed | 7 tests, 397 checks, 0 failed | exit 0 | 15 tests, 166 checks | 0 |
| release | 376, 0 | 397, 0 | exit 0 | pass | 0 |
| stb_truetype | 376, 0 | 397, 0 | exit 0 | pass | 0 |
| 16-bit ImWchar | 376, 0 | 397, 0 | exit 0 | pass | 0 |
| ASan+UBSan (C file instrumented too) | 376, 0; no sanitiser report | 397, 0 | exit 0 | pass | 0 |
| exceptions on + `tests/imconfig_throwing.h` | **394**, 0 | 397, 0 | exit 0 | pass | 0 |
| the same under ASan+UBSan | 394, 0; no report, no leak | 397, 0 | exit 0 | pass | 0 |

The 18 extra checks with exceptions are the boundary test, which does not run without them and
prints that it did not. In the debug directory `ctest -j4` (guarded) also passes 5 of 5
(`test_c_api`, `test_c_api_map`, `test_header_pod`, `example_minimal`, `example_specimen`).

In the two exception builds ImGui itself gives 4 `-Wterminate` warnings (an `IM_ASSERT` that throws
inside a destructor). They are `tests/imconfig_throwing.h` meeting ImGui's destructors, in the
`imgui` target, not in a file of mine; harden-2 owns that header.

**Not run**: the preset directories (`build/debug` ...), the whole test suite (I built every module
but only the four tests above; nothing I wrote is linked by any other test), any benchmark, clang,
MSVC, Windows, a shared-library build (so the `visibility` / `__declspec` branches of
`IMTYPESET_C_API` have never mattered to a link), any FFI. The `asan` preset sets
`CMAKE_CXX_FLAGS` only, so in a preset build `test_c_api.c` itself is NOT instrumented (the C++
on both sides of it is); my private ASan directories set `CMAKE_C_FLAGS` as well. `CMakePresets.json`
is not mine: the line would be `"CMAKE_C_FLAGS": "-fsanitize=address,undefined -fno-omit-frame-pointer"`
in the `asan` preset.

Header alone, `-fsyntax-only`: C99 and C11 with `-Wall -Wextra -Wpedantic -Werror`, C++17 with the
tree's flags: clean. C++98 `-Wpedantic` refuses `unsigned long long`; the project is C++17.

## What the tests hold

`tests/test_c_api.c` (C99, a C compiler; it cannot see ImGui):
- build info against what the host's translation unit says (sizes, offsets, version, FreeType,
  exceptions), and a caller with a shorter struct gets its prefix only;
- with no context: every query answers, `NoContext` is reported, a null struct is
  `InvalidArgument` with a message naming the function, and the next call replaces it;
- the version check: a wrong `ImWchar` or `ImDrawVert` size is refused and `CreateContext` then
  refuses; the version as `(begin, end)` inside a longer string passes;
- fonts from a file name that is not terminated, and from memory freed straight after;
- **one paragraph with two runs, set through the C ABI from unterminated text and through the C++
  API from the same text**: same size and line count; `ImTypeset_DrawParagraph` and
  `ImTypeset::DrawParagraph` into one draw list at one place give byte-identical vertices, with
  and without a selection; `ImTypeset_DrawParagraphToBuffers` gives byte-identical vertices AND
  indices to `ImTypeset::DrawParagraphToBuffers`, the size query, a half-capacity prefix, and a
  sentinel byte after each buffer;
- lines tile the text; hit test, word and line selection, range rectangles (count, prefix),
  caret; copy against the C++ function with and without preceding text; a cut inside a two-byte
  character lands before it;
- formulas: one set, one refused, its reason by pointer and by copy, the paragraph's error;
- widgets over three frames: `(begin, end)` text and ID, a per-call style, `Text("%d")`,
  `SelectableText`'s selection read (one rectangle more in the draw list) and written (a text with
  no font answers an empty selection), `CalcTextSize` equal to the C++ one;
- exceptions (below).

`tests/test_c_api_map.cpp` reads both headers as text: all 43 `IMTYPESET_API` functions have a row
and a C counterpart of the expected name; all 48 `IMTYPESET_C_API` functions are in the table (5
marked as the C ABI's own, with the reason); all 43 enumerators have a C twin with the same value;
all 8 structs have a twin with the same field names in the same order (49 fields); all 9 `typedef
int`s; the two version macros; the C header's `#include`s are exactly three C headers.

**"Fails the build" is true for half of it.** A function REMOVED or RENAMED in either header fails
to compile (the table takes both addresses), and a struct or enumerator that changed fails the
`static_assert`s in `src/imtypeset_c.cpp`. A declaration ADDED to `imtypeset.h` with no C form
cannot be seen by a compiler: it fails `test_c_api_map` when ctest runs, by name. I know of no way
to get that at compile time without a code generator.

Mutation: 35 mutants of `src/imtypeset_c.cpp`, compiled out of tree and linked in front of the
library. 32 failed `test_c_api` as it stood; 2 survived and got checks that now fail them
(`DrawParagraphToBuffers` ignoring its selection; `CalcTextSize` always answering success); 1
survives: `AddFontFromMemoryTTF` given one byte fewer than the file still loads the font.

## Exceptions

`guarded()` in `src/imtypeset_c.cpp` wraps every entry but the error getters; under
`__cpp_exceptions` it catches `std::exception` and `...`.

The partial work tested this by handing `ImTypeset_AddFontFromFileTTF` an `ImFontConfig` with
`RasterizerDensity = 0`, on which ImGui's `AddFont` asserts. That never worked: the library
refuses that config itself before ImGui sees it (`src/font_loader.cpp`, "ImFontConfig::RasterizerDensity
must be positive", held by `tests/test_stress_font.cpp`), so nothing threw and the first
exceptions build failed 1 of 380. I do not know when that check went in; the partial work was
written without being built. A test that depends on which input
the library has not yet learned to refuse breaks again at the next hardening, so the host now
installs ImGui allocator functions (`ImGui::SetAllocatorFunctions`) that throw on request, and the
C test arms one allocation inside `ImTypeset_AppendParagraphRangeText`: once a
`std::runtime_error` (the message arrives through `ImTypeset_GetLastErrorMessage`), once an `int`
(the generic message), then a third call that succeeds with the paragraph intact. Three mutants of
the catch path all fail it.

So: **no exception that started as an ImGui assertion was driven through the C ABI**; the mechanism
is the same `catch`, and whether the library is in a usable state after an assertion unwinds
through layout or draw is harden-2's X1, not tested here.

## Decisions taken unattended

1. **Kept the partial header and implementation.** I read both to the end against the 0.2.0
   header before building: 43 of 43 functions covered, the mirror asserts complete, the rules at
   the top of the header followed without exception. Nothing in them had to be redone; the first
   build passed.
2. **A struct never crosses by value**, including the ones the C++ API returns by value: `out`
   pointers. (Reasoning in the doc-additions entry.)
3. **`bool` stays inside the two mirrored structs** that have it, as C99 `_Bool` via `<stdbool.h>`,
   because "mirrored field for field" and "proved by offset" require it; it is not used as a
   parameter or result.
4. **`ImTypeset_DrawParagraphToBuffers` reports the full counts and never fails for capacity**, and
   does not append: the caller owns the arrays. It draws into a scratch `ImVector` kept between
   calls and released by `ImTypeset_DestroyContext`, so a steady frame allocates nothing, at the
   price of one copy of the vertices.
5. **`ImTypeset_Error_Exception = 1000`** exists only in the C header.
6. **The throwing allocator** in place of an assertion, above.
7. **Two CMake targets**, above.

## Open questions, for the lead

1. **`TextCodepointToByteOffset(text, text_end, n)` and `TextByteToCodepointOffset`** are the two
   functions of the revised header a generated binding cannot take with the `text_end` option:
   their `text_end` has no default and is not last. `(text, n, text_end = NULL)` would fix it and
   is a change to a public declaration (and to both C functions and the map test's row order,
   which is nothing). Not mine to make; `docs/bindings-notes.md` 5.4 records it and the drafts
   bind the two by hand meanwhile.
2. **Where the C ABI ships.** Today `src/imtypeset_c.cpp` is in `libimtypeset.a` because the glob
   puts it there. For a shared library `py-host` will need `IMTYPESET_C_API` to export (it
   defaults to default visibility on GCC/Clang and needs `IMTYPESET_C_BUILD_DLL` on Windows) and
   `-fPIC` on the library target; neither has been built.
3. **`IMTYPESET_C_VERSION` is a second copy of the version string.** A `static_assert` in
   `src/imtypeset_c.cpp` and a check in `test_c_api_map` hold the two equal, so a version bump
   that forgets one fails the build; it is still two places to edit.
4. **Thread note.** The layer adds process globals of its own (the boundary error slot, the scratch
   vectors, a scratch string for `(begin, end)` names). Same rule as the C++ API, one thread at a
   time, and the header says so.

## Noticed outside the task

- **For harden-2's X1** ("throw from inside font registration"): an `ImFontConfig` with
  `RasterizerDensity = 0` does NOT reach ImGui's assertion through `AddFontFromFileTTF`; the
  loader refuses it first. `tests/test_exception_safety.cpp` did not exist when I looked. An
  allocator installed with `ImGui::SetAllocatorFunctions` that throws on the n-th allocation
  (`tests/c_api_host.cpp`, `host_alloc`) provokes a throw at any depth without depending on what
  the library validates.
- **`docs/bindings-notes.md` sections 6 and 7 are measurements of the pre-D36 header** (`TextJustified`,
  pointer-keyed cache, a private shim). I marked 7 as superseded in behaviour and left the numbers:
  they are a record of what was run. `py-host` is the task that can take them again.
- **The asan preset does not instrument C** (above).
- `bindings/imgui_bundle/pybind_imtypeset.cpp` now throws from module init on a failed version
  check; whether nanobind turns that into a clean `ImportError` is unknown (unrun).
- The mapping test reads `include/*.h` from `IMTYPESET_SOURCE_DIR`; an installed or amalgamated
  distribution (`dist`) that moves the headers has to keep that path or the test skips nothing: it
  `REQUIRE`s the files and fails.
