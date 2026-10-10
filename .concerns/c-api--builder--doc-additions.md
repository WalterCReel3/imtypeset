# c-api — text for the shared documents (for `fold-docs`)

Rule 4 of the board: this task does not edit `docs/decisions.md` or `THIRD-PARTY.md`. The text
below is what to add, per file. The decision number is the folder's to assign (D38 if harden-2 and
font-wild-finish have not taken it).

## docs/decisions.md — append one entry

```markdown
## D<n>. The C ABI: `include/imtypeset_c.h` (task `c-api`)

D34 names a C ABI as the seam for every language that is not C++. This is how it crosses.

**Decided.**

- **One function per public C++ function, `ImTypeset_` + the C++ name**, and nothing in the header
  a C99 compiler or an FFI cannot read: it includes `<stdarg.h>`, `<stddef.h>` and (for C)
  `<stdbool.h>`, no C++ header and no ImGui header. Five functions are the ABI's own:
  `ImTypeset_GetBuildInfo`, `ImTypeset_SetStyle`, `ImTypeset_GetLastErrorMessage`,
  `ImTypeset_GetParagraphErrorMessage`, `ImTypeset_GetParagraphFormulaReason`.
- **Text is `(begin, end)`, `end` null for a zero-terminated string**, everywhere, file names and
  string IDs included: a language with counted strings never has to terminate one. Where the C++
  parameter is a zero-terminated string (a file name, a string ID, the version) the layer copies.
- **A struct crosses by pointer only**, in both directions, and one the library fills is a
  caller's `out` parameter. By-value struct returns were rejected: they are where C ABIs differ
  between compilers and what ctypes and cffi get wrong most easily. The eight public structs are
  mirrored field for field under the C names; `src/imtypeset_c.cpp` `static_assert`s size,
  alignment, every field's offset and every field's type, and every enumerator's value.
- **Nothing is allocated on one side and freed on the other.** A string or an array the library
  produces is written into the caller's buffer with a capacity and the function returns how much
  there was, as `snprintf` does; capacity 0 asks the size. A string cut to fit is cut at a UTF-8
  boundary. Consequences: `ImTypeset_DrawParagraphToBuffers` does not append (indices start at 0),
  and `ImTypeset_AppendParagraphRangeText` takes `after_text` in place of the C++ function's "does
  the buffer already hold text".
- **`GetStyle()` is a reference in C++ and a copy out plus `ImTypeset_SetStyle` here**, so nothing
  a binding holds dangles when the context goes.
- **`bool` does not cross as a parameter or a result** (`int`, zero or not). It stays inside the
  two mirrored structs that hold it (`ImTypeset_Hit`, `ImTypeset_Formula`), as C99 `_Bool`.
- **Handles are the C++ handles** (typedefs of the same struct tags), so one process may use both
  headers on one context. `ImDrawList*`, `ImFont*`, `ImFontConfig*` are passed through unopened.
- **`ImTypeset_GetBuildInfo(out, out_size)`** is what a binding calls first: version, the ImGui it
  was compiled against, `sizeof` of `ImWchar`, `ImDrawVert`, `ImDrawIdx`, `ImDrawVert`'s field
  offsets, FreeType or stb_truetype, whether exceptions are caught, the eight struct sizes. Only
  `int` and `char`, sized by the caller, appended to in later versions. It does not vouch for
  ImGui's own structs (`ImGuiIO`, `ImGuiStyle`): that remains `IMGUI_CHECKVERSION()`'s.
- **No exception crosses.** `src/imtypeset_c.cpp` is the one file of the library with `try` and
  `catch`, under `#if defined(__cpp_exceptions)`: each entry catches, fails as its return type
  fails (0, -1 or null), and `ImTypeset_GetLastError()` answers `ImTypeset_Error_Exception`
  (1000, an enumerator only the C header has) with the exception's message. Built without
  exceptions nothing catches and nothing throws. The layer adds one other failure of its own, a
  null pointer where a struct is required (`ImTypeset_Error_InvalidArgument`).
- **`IMTYPESET_C_ABI_VERSION` (1)** is the shape of the ABI itself, raised when a function changes
  its parameters; adding a function does not raise it.

**Measured** (private build directories, GCC 12.2, ImGui 1.92.9; counts in
`.concerns/c-api--builder--result.md`): `tests/test_c_api.c`, compiled as C99 with `-Wall -Wextra
-Wpedantic -Werror`, passes in debug, release, ASan+UBSan, stb_truetype, 16-bit `ImWchar`, and
with exceptions enabled (plain and under ASan+UBSan). In each, the vertices
`ImTypeset_DrawParagraph` puts into a draw list and the vertices and indices
`ImTypeset_DrawParagraphToBuffers` writes are byte-identical to the C++ API's for the same
paragraph, runs and selection. 35 mutants of `src/imtypeset_c.cpp` were built out of tree: 32
failed the test as it stood, 2 led to new checks and fail it now, 1 survives (a font handed over
one byte short still loads).

**Read, not run**: Windows (`__declspec` branch), clang, MSVC, a shared-library build (the tree
builds the library static; visibility attributes are therefore untested), and any FFI: no Python
has loaded it (task `py-host`).

**Not decided here**: whether the ABI ships in the same library as the C++ API or beside it
(task `dist`); the root `project()` still lists `CXX` only and `tests/CMakeLists.txt` enables C
for the one C test.
```

## docs/divergences.md — nothing

The C ABI adds no behaviour of its own to typesetting.

## THIRD-PARTY.md — nothing

No third-party code or data was brought in. `include/imtypeset_c.h` says it is "what cimgui is to
Dear ImGui": that names the role; no line of cimgui was read or used.

## docs/hardening.md — one line, if the folder wants it

```markdown
- **C ABI** (`src/imtypeset_c.cpp`): every pointer the layer itself dereferences is checked for
  null and refused with `ImTypeset_Error_InvalidArgument`; capacities are honoured exactly (tests
  hold a sentinel byte after each buffer); a C++ exception is turned into an error at every entry
  when the file is compiled with exceptions. Handles are not validated: a stale handle is a use
  after free here as in the C++ API.
```
