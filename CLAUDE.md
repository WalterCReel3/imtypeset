# Working in this repository

imtypeset: typeset text for Dear ImGui. A C++17 extension in the shape implot
has — C++ over ImGui, a C ABI and bindings on top — that lays text out on TeX's
box-and-glue model and draws it through ImGui's font atlas. It began as a port
of the Python library in `../imgui-adv-text` and is now the product (D32, D34);
that library stays where it is as the prototype it grew from, the reference and
the test oracle. The first users are two Python applications on stock
`imgui_bundle` wheels, `../imgui-adv-text` itself and `../pptmstr`
(`docs/advtext-rework-plan.md`).

Read first:

- [docs/decisions.md](docs/decisions.md) — what has been decided and why. The
  `Dn` numbers below point into it.
- [docs/port-hazards.md](docs/port-hazards.md) — **required before starting any
  task.** Where a literal transcription of the reference goes wrong, module by
  module, and the rulings taken. Where it disagrees with a task's specification,
  it wins: the specifications were written first and cannot be edited.
- [docs/divergences.md](docs/divergences.md) — where this, the reference and
  `tex.web` differ, and which differences are switches.
- [docs/distribution.md](docs/distribution.md) — how the library reaches a
  program, the ImGui version floor, every preset.
- `../imgui-adv-text/docs/cpp-extension.md` — the design note this starts from,
  and [docs/spec-status.md](docs/spec-status.md), which says what became of each
  of its claims. Several are refuted.
- `../imgui-adv-text/docs/tex.web` — the TeX82 source, which the layout core
  transcribes, and `../imgui-adv-text/docs/tex-notes.md`, an audited digest of
  it. Read the digest's "CORRECTIONS" section first: it overrides its own body.
- `../imgui-adv-text/docs/advance-rule.md`, `docs/subpixel.md` — the
  device-space rules, which are the part specific to ImGui and the part to carry
  across intact.

---

## How to think here

### Correctness over speed

A wrong answer delivered quickly costs more than a right one delivered slowly,
because the wrong one gets built upon. This is a library other people's programs
will link, in several languages.

- Prefer the fix that addresses the cause over the one that makes the symptom go
  away. If you can only do the latter, say so explicitly.
- Don't paper over a failure to make output look clean. A failing test that
  reveals a real defect is a better outcome than a passing one that hides it.
- If finishing properly needs another step — a read of the reference, a build, a
  run under the sanitisers — take the step.

### The Python reference is the oracle

The tests under `tests/` replay inputs dumped from the reference
(`tests/golden/`, written by `tools/oracle/`) and compare the port's answers
with the reference's, exactly: integers equal, doubles equal to the last bit.

**A divergence is a finding to record, never something to tune a test around.**
When the port and a golden disagree, the port is wrong until shown otherwise:
find out why. If it is the reference that is wrong, that is a decision — write
it in `docs/decisions.md` and name it in the test. Never add a tolerance to make
a comparison pass, never delete the awkward case, never regenerate a golden from
the port's output. If exactness fails, the reason is the interesting part
(rounding mode, floor versus truncate, order of float operations), and it is
usually a real difference.

Things the reference does that C++ does not do by default: `round()` is
round-half-to-even; `//` and `%` floor toward negative infinity; integers do
not overflow; `float` is a double; `sorted()` is stable. Use the helpers in
`src/units.h` (`py_round`, `floor_div`, `ceil_div`, the checked float→`Scaled`
conversion) and never `std::round`, `lround` or `(int)(x + 0.5)` (D18).

**Floating point is never contracted or relaxed** (D16). Everything is built
with `-ffp-contract=off`, and `-ffast-math`, `-Ofast` and
`-funsafe-math-optimizations` are never used — benchmarks included. A fused
multiply-add rounds once where the reference rounds twice. `src/units.h`
refuses to compile under fast-math. The one exception is deliberate: the
`fp-host` preset compiles the generated drop-in files without the flag, to test
the pragma that stands in for it in a host's build (D44).

**The goldens run under the reference's rules; the public API does not.** Where
the reference was found wanting, the improvement is a switch, off in
`reference()` and on in `product()`: `TexRules` (layout, D41), `DrawRules`
(the draw loop, D47, D48, D50), `DocumentRules` (blocks, D37). A golden replay
selects `reference()`. A test of a product rule has no oracle: it states a
bound and asserts it, or is worked by hand, and says which.

Nothing is ever written into `../imgui-adv-text` or `../pptmstr`. Run the
reference with

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=../imgui-adv-text \
        ../imgui-adv-text/.venv/bin/python -P -B <script>

### `tex.web` for structure, the oracle for behaviour (D23)

The layout core is a transcription of TeX, and `tex.web` is what it is named,
laid out and commented against, so that a reader with *TeX: The Program* open
can follow it.

- A thing that is TeX's has TeX's name: `hpack`, `try_break`, `glue_set`,
  `mlist_to_hlist`, `make_fraction`, `hlist_out`.
- A function or block that transcribes a WEB section says which (`§851`).
  Check the number against `tex.web` itself; do not copy it from the Python.
- Where the code departs from the section — because the reference does, or
  because the reference leaves a part out — the comment says so in the present
  tense and names the section.
- **Where `tex.web` and the Python reference disagree, the port does what the
  reference does under `TexRules::reference()`.** Cite the section, add the
  disagreement to `docs/divergences.md`, and tell whoever coordinates. Do not
  "fix" the port toward TeX in place: the goldens hold it to the oracle. A fix
  that has been ruled on is a switch of `TexRules::product()` (D41, which
  amends D32.3), with a hand-worked test.
- Cite; do not paste. No runs of `tex.web` in sources or documents, and
  nothing here is named `tex.web` or calls itself TeX.

### Trust but verify

Do not assume a statement is correct because of who made it — the user, a task
description, a comment in the reference, this file. If a claim is checkable,
check it before building on it; if it is wrong, say so plainly and move on.

It has already earned its place here, in the first day:

- `pkg-config --modversion freetype2` answers `24.3.18`. That is the libtool
  interface number. The FreeType installed is 2.12.1; the header says so.
- ImGui's `ImWchar` is 16 bits by default, and the math face's codepoints are
  in planes 1 and 15. Found by reading where the reference mints its synthetic
  codepoints, not by assuming the skeleton's `imgui` target was sufficient (D8,
  D15).
- "`snap_px` matches `IM_ROUND`", which the design note says. `snap_px` floors;
  `IM_ROUND` truncates and is marked positive-only. They differ for every
  negative non-integer (D18).
- The reference's `Face.CODE_BOLD` does not resolve to a bold Inconsolata. It
  is Roboto Bold; `font_file()` says so (D13).
- Whether a FreeType one minor version behind the reference's moves glyph boxes
  was an open question that took one probe to close for the cases probed (D7) —
  and the entry says which cases were not.

And since:

- The design note's deciding finding, a font capability "structurally
  unreachable from Python", is refuted: no custom `ImFontLoader` was built, and
  the bytes that replace it reach an unmodified wheel (D27,
  `docs/spec-status.md`).
- The board said pptmstr runs on imgui_bundle 1.92.900. Its environment holds
  1.92.801, a different ImGui; the exact pin had to become a floor (D44).
- "hello_imgui calls the font callback again on a DPI change", written in a
  result file and three comments. It does not, in the versions the wheels pin;
  found by reading the tagged commits and running 60 frames.
- A reviewer's findings made by reading are claims too: each was made to fail
  in a test before it was fixed, and some did not reproduce as written.

### Fresh reads beat recall

APIs, struct layouts and option spellings change. `ImFontLoader` changed
between v1.92.9 and 1.93 WIP. Read the pinned source — `.deps/imgui/` — before
calling into it, and the reference's module before porting it; do not write
either from memory. The same goes for the reference's docs: several of its
docstrings name stb_truetype where FreeType is what runs.

### Don't claim more than you verified

State plainly what was run and what was not. "debug and asan pass, 14 tests;
release not run; the MSVC flags have never been compiled" is useful. "The build
works" is not. `docs/decisions.md` keeps *decided*, *measured* and *read* apart
for the same reason, with a *not measured* list where there is one. Maintain it.

### Decisions are recorded, not re-litigated

`docs/decisions.md` holds them with their reasoning. Read it before proposing a
direction. If you think an entry is wrong, say so and say why, engaging with
what it says. When you take a decision, append an entry in the same change —
work here is often done with nobody to ask, and an unrecorded decision is one
the next reader has to take again without your evidence.

---

## Scope (D1, as amended by D20, D27, D32, D34, D37, D43)

Native: the OpenType reader, an in-memory `cmap` rewrite that hands each face
to ImGui's own font loader (no custom `ImFontLoader` was built, D27), the
layout core (`units`, `badness`, `nodes`, `pack`, `linebreak`, `shipout`), the
style subset, paragraph assembly (`src/paragraph.*`, D20), the draw loop,
selection and hit-testing geometry, the math list and the `mathtext` subset,
the block layer (`src/blocks.*`, D37), a public API with widgets, paragraphs
and a `Document` (D36, D43), the demo window (D42), a C ABI (D40) and a
`ctypes` Python package over it.

Not here: markdown, diff, find, tables and columns (a block's table fields are
reserved and unused), the views. Dear ImGui Bundle binding the library the
bundle's way (`bindings/imgui_bundle/`) is an outline that has never been built.

No HarfBuzz, no fontTools equivalent, no third-party test or benchmark
framework (D3, D9). FreeType is optional (D7).

The OpenType reader is for the fonts a host brings, not for the seven under
`tests/data/fonts` (D25): what real fonts contain is either supported or
refused with a reason, and `docs/font-support.md` says which.

## Licensing (D24)

No licence has been chosen; that is the user's decision. Until it is, keep the
tree compatible with a permissive, MIT-style one.

- **Write from the specification and the Python reference, never from another
  implementation.** Reading fontTools, FreeType, HarfBuzz or MicroTeX to learn
  what a table means is fine. Porting a function from one is not.
- Code adapted from Dear ImGui, or using `imstb_truetype.h`, carries the
  upstream notice where it is used and an entry in `THIRD-PARTY.md`.
- No GPL or LGPL code or data. Fonts installed on the machine are read in
  place and never copied into the tree.
- A table generated from Unicode data says so, names the Unicode version and
  carries the Unicode notice.
- If you bring in anything this does not cover, say so before it lands.

## Layout

| path | what it is |
|---|---|
| `include/imtypeset.h` | the whole public C++ surface |
| `include/imtypeset_c.h` | the same as a C99 ABI; `tests/test_c_api_map.cpp` fails the build on a declaration without a counterpart |
| `src/` | the implementation; one `snake_case` pair per reference module, plus `imtypeset*.cpp` (the API, the Document, the C ABI, the demo window) |
| `tests/` | `test_*.cpp`, `check.h` (harness), `golden.h` (golden reader), `support/`, `data/`, `golden/`, `consumer/`, `python/` |
| `tools/oracle/` | Python scripts that dump goldens from the reference, and the record formats |
| `tools/amalgamate.py` | writes the drop-in files into `dist/` (git-ignored; generated, never edited) |
| `tools/shot.sh` | runs a command on an X display (Xvfb in a private namespace when the session has none) |
| `bench/`, `examples/` | `bench_*.cpp`; `*.cpp` |
| `examples/demo_sdl2/` | the windowed host of the demo window: a CMake project of its own |
| `python/imtypeset/`, `python/host/` | the `ctypes` package; the self-hosted library and the one built for an `imgui_bundle` wheel |
| `python/spike_*`, `examples/python/` | spikes for pptmstr and advtext; the Python examples |
| `cmake/imgui.cmake`, `cmake/imgui_version.cmake` | build the `imgui` target when this is the top-level project; read the version floor from the header |
| `.deps/imgui/` | untracked copy of the tested ImGui (v1.92.9), so configuring needs no network |
| `cmake/sdl2.cmake` | the one SDL2 search order the demo and the Python host share: `IMTYPESET_SDL2_DIR`, the CMake package, pkg-config, then `.deps/sdl2` |
| `.deps/sdl2/` | untracked SDL2 (static, debug, x86-64 Linux) for the demo and the Python host; `tools/vendor_sdl2.sh` fills it |

---

## C++ rules (D2, D5)

- **C++17**, and it must build with **`-fno-exceptions -fno-rtti`**, which the
  build passes, and also with exceptions on, because a host's `IM_ASSERT` may
  throw (presets `exceptions`, `bundle-config`; D39 item 11). No `throw`,
  `dynamic_cast`, `typeid`. `try` and `catch` are in two places only, both
  under `#if defined(__cpp_exceptions)`: the entry guard of
  `src/imtypeset_c.cpp`, and the macro `IMTYPESET_NO_UNWIND` of
  `src/context.h` for a call made while cleaning up. Everywhere else the
  library is safe to unwind through by ordering and destructors.
- **Errors** (D17). `IM_ASSERT` for a contract the caller broke on an internal
  entry point. A status with a reason for anything reachable from data: a
  malformed font, a formula outside the subset, hostile text, a hand-built node
  list. Asserts are often compiled out, so the code after one must still be
  defined behaviour. Input from a font file or a string is untrusted —
  bounds-check every offset and count read from one.
- **No iostreams**, anywhere. `<cstdio>`.
- **`include/imtypeset.h` has no STL in it.** Only `imgui.h` types, PODs,
  `const char*`, `ImVector`. No templates, no callbacks with captures, no
  overload sets that differ only by pointer type. It must not include
  `imgui_internal.h`. Bindings are generated from it, so every declaration is a
  promise to every binding: keep it small.
- **Public names** as ImGui and implot write them: `namespace ImTypeset`,
  `CamelCase` functions, `ImTypesetXxx` structs, `ImTypesetXxxFlags_` enums.
- **Internals** are in `namespace ImTypeset::detail`, in files named after the
  reference's modules, and keep the reference's names — `hpack`, `try_break`,
  `GlueSpec` — so the two can be read side by side.
- **The STL is fine inside `src/`**, but mind the hot paths: the draw loop runs
  per glyph per frame and the line breaker's inner loop per candidate break. No
  allocation there; contiguous storage; reuse buffers across calls.
- **Ownership is explicit**; rule of zero; `const`-correct; no `using
  namespace` in headers; include what you use.
- **Zero warnings** under `-Wall -Wextra -Wpedantic -Wshadow -Wconversion
  -Werror` (D12). Fix a conversion warning by deciding what the conversion
  should be, not by casting until it is quiet.
- **ImGui has a floor and a tested version** (D44, which supersedes D6's exact
  pin): 1.92.0 or later, tested against v1.92.9, both numbers stated in
  `include/imtypeset.h` and read from it by CMake. `.deps/imgui` is the tested
  tag. Nothing in `include/` or `src/` includes `imgui_internal.h`; the library
  uses four members of `ImDrawList` that `imgui.h` marks `[Internal]`. A test
  may include `imgui_internal.h`. A new ImGui struct field read in `src/` must
  be added to the sidecar's probe (`python/imtypeset/_bundle.py`).
- **`ImWchar` may be 16 or 32 bits** (D15). Ours is 32 by default; a host's is
  whatever it is. Layout carries the reference's codepoints, plane 15 included,
  in our own 32-bit type; only the font loader and the draw loop convert, through
  the one mapping function in `src/fontlib`. Never store a layout codepoint in
  an `ImWchar`.

### Comments describe the code as it is, not as it was

No defect archaeology in the source: not what a line used to do wrong, not what
was tried first, not which task wrote it. The next reader is looking at the
current state and has to spend attention working out that the comment is
history. A comment *should* explain a constraint that holds right now and is not
visible from the code:

```cpp
// Python's round() is half-to-even; std::lround is half-away-from-zero, and
// the goldens contain the ties.                             // good: a live constraint

// This used to call lround and was off by one sp on ties.   // bad: history
```

History goes in `docs/decisions.md` when it carries a decision, and otherwise
nowhere.

### Style

[.clang-format](.clang-format): 4 spaces, 100 columns, brace on its own line
for functions, types and namespaces, attached for control flow, `Type* name`.

    clang-format -i <the files you own>

Never format ImGui's sources, and do not reformat files you are not otherwise
changing.

---

## Build and test

Needs CMake ≥ 3.21, Ninja, a C++17 compiler. FreeType is used if found.

```sh
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
```

| preset | what |
|---|---|
| `debug` | unoptimised, ImGui's assertions live; also the consumer-project test |
| `release` | optimised; the only one to take a timing from |
| `asan` | debug with ASan + UBSan over the library, ImGui and the tests |
| `stb` | debug with stb_truetype in place of FreeType |
| `wchar16` | debug with ImGui's default 16-bit `ImWchar` (D15) |
| `amalgamation` | debug with the library built from the generated drop-in files |
| `fp-host` | release, the drop-in files at `-O3 -march=native` without `-ffp-contract=off` |
| `exceptions` | ASan + UBSan with exceptions on and an `IM_ASSERT` that throws |
| `bundle-config` | exceptions on, Dear ImGui Bundle's configuration header (needs `../imgui_bundle`) |
| `imgui-next` | the Dear ImGui checked out in `../imgui` (1.93 WIP) |
| `python-host` | debug plus `python/` and the `python_binding` test (needs an SDL2) |

`docs/distribution.md` section 7 says what each gave when last run. All but
`stb` use FreeType where it is installed (D7). Anything that compares
what the rasteriser produced is valid only under FreeType and must say so when
it skips under `stb`.

Sources are found by glob (D11): a new `src/x.cpp`, `tests/test_x.cpp`,
`bench/bench_x.cpp` or `examples/x.cpp` is built by being there. **Do not edit
a CMake file to add one.**

### A private build directory

The presets build everything, so they break whenever any file in the tree is
half-written. While others are writing, build in your own directory, narrowed
to the modules your code links against and the tests you are working on:

```sh
cmake -S . -B build/<task-id> -G Ninja \
    -DIMTYPESET_MODULES="units;badness;nodes;pack" \
    -DIMTYPESET_TESTS="test_pack"
cmake --build build/<task-id> && ctest --test-dir build/<task-id> --output-on-failure
```

- Names are basenames without extension: `src/pack.cpp` is `pack`,
  `tests/test_pack.cpp` is `test_pack`, `tests/smoke.cpp` is `smoke`. Empty
  means all. A header-only module (`src/units.h`, no `.cpp`) may be listed.
- The lists are not transitive: name every module whose `.cpp` you need.
- Add `-DCMAKE_BUILD_TYPE=Release` for an optimised build, or
  `-DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined
  -fno-omit-frame-pointer" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"`
  for what the `asan` preset does, in a second directory (`build/<task-id>-asan`).
- With modules narrowed, benchmarks and examples are built only if named:
  `-DIMTYPESET_BENCHES=bench_draw`, `-DIMTYPESET_EXAMPLES=minimal`.
- The directory named after each preset (`build/debug`, `build/release`,
  `build/asan`, … `build/python-host`) belongs to that preset. For the 16-bit
  build in a private directory, add `-DIMTYPESET_IMGUI_WCHAR32=OFF`. Leave
  them alone while the tree is in motion. A preset can be configured into a
  private directory instead: `cmake --preset release -B build/<task-id>`.
- When another agent saves a header while your build is compiling, ninja can
  finish an object whose compile started before the save and ended after it.
  The object is then newer than the header and is never rebuilt, and the link
  mixes two layouts of one struct: a crash that makes no sense right after
  somebody else's header changed is this first. `ninja -C build/<task-id> -t
  clean imtypeset` and a rebuild cures it.
- Remove your own `build/<task-id>*` directories when you finish.
- Never delete `.deps/`. Without it a configure needs GitHub.

Do not hand-edit anything under `build/`.

### Writing a test

`tests/test_<module>.cpp`, including `check.h` (or `golden.h`, which includes
it). `check.h` supplies `main()`.

```cpp
#include "golden.h"
#include "pack.h" // src/ is on the include path

TEST(hpack_matches_the_reference)
{
    const std::vector<std::string> files = golden::list("core");
    REQUIRE(!files.empty()); // a missing directory must not read as a pass
    for (const std::string& path : files) {
        CHECK_CONTEXT("%s", path.c_str());
        golden::File file;
        if (!file.load(path))
            continue;
        for (const golden::Record& r : file) {
            CHECK_CONTEXT("line %d", r.line);
            if (r.is("glue"))
                CHECK_EQ(r.i64(0), /* the port's answer */ 0);
        }
    }
}
```

- `CHECK`, `CHECK_EQ`, `CHECK_NE`, `CHECK_NEAR` record and continue. `REQUIRE`,
  `REQUIRE_EQ` return from the test. `FAIL("printf %d", n)` fails outright.
- `CHECK_EQ` prints both values, doubles in decimal and as hex floats.
- `CHECK_CONTEXT(...)` adds a line to every failure in its scope — the file,
  the case, the record. Use it; a golden replay fails in bulk.
- `./build/<dir>/tests/test_pack --list`, `--filter=TEXT`, `-v`.
- Test data: `golden::data_path("fonts/Roboto-Regular.ttf")`,
  `golden::path("core/x.golden")`, `golden::read_file(path, bytes)`.
- A test that creates an ImGui context sets `io.IniFilename = nullptr` and
  `io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures`; `tests/smoke.cpp`
  shows a headless frame.
- Tests exercise internals freely. A helper header shared by several tests goes
  in `tests/` with a name that is not `test_*.cpp`.

### The golden format (D10)

Line-oriented UTF-8; `#` comment lines; one record per line; space-separated
tokens; the first token is the record tag. Integers decimal. Floats as Python's
`float.hex()` prints them (`0x1.8000000000000p+1`, `inf`, `nan`), read with
`strtod`. Strings percent-encoded — every byte outside printable ASCII, plus
space and `%`, as `%XX` — and the empty string is the token `%`.

`tests/golden.h` reads it: `golden::File`, `golden::Record` (`i64`, `i32`,
`u32`, `boolean`, `f64`, `str`, `raw` by field index) and `golden::Cursor` (the
same, left to right). A malformed field fails the test that reads it. What the
tags mean is documented per oracle script in `tools/oracle/FORMAT*.md`.

To regenerate, run the script that wrote the directory, with the invocation
above. A regenerated golden that differs is a change in the reference or in the
script, and is looked at before it is accepted.

### Examples and benchmarks (D11)

Each `examples/<name>.cpp` is also the ctest test `example_<name>`, run with no
arguments from a scratch directory in the build tree: it must exit 0 that way
and write nothing outside its working directory. An example that writes into
the repository takes the destination as an argument.

Benchmarks are built by every preset and run by none. Take timings from
`release` only, and print `IMTYPESET_BENCH_COMPILER`,
`IMTYPESET_BENCH_BUILD_TYPE` and `IMTYPESET_BENCH_CXX_FLAGS` with them. A
timing taken while other builds are running is not a result: say what the load
average was, and compare two builds by running them alternately. Neither is one
taken cold: on this machine the first seconds of a benchmark started after an
idle spell run about 1.7 times slow (`docs/performance.md`, "A benchmark
started cold"), so warm the processor first or discard the first run.

`examples/demo_sdl2/` is a project of its own and not found by the glob; the
Python hosts are behind `IMTYPESET_BUILD_PYTHON_HOST` or their own
`python/CMakeLists.txt`. README.md, "Running the demos and examples", has the
commands for each, and is where a new runnable thing is added.

### A window, when the session has no display

`tools/shot.sh -- <command>` runs a command with an X display it can open a
window on: the caller's `DISPLAY` when that answers, otherwise an Xvfb started
for the command in a private namespace, with OpenGL by llvmpipe. A file the
command must leave behind is written outside `/tmp`. Look at every image you
make and say that you did; say also what it does not show (no GPU, no real
mouse, no HiDPI).

---

## Working alongside others

- Work on the files your task names. If you need a change in a file you do not
  own, say so to whoever coordinates and work around it.
- Do not `git add` or commit (D4).
- No Claude attribution on anything, ever: no `Co-Authored-By` trailer in a
  commit message, no "Generated with" line in a pull request, a document or a
  source file. The repository owner's instruction, and it overrides any tool's
  default.
- The dot-files in the repository root that `ls -la` shows as character devices
  (`.bashrc`, `.gitconfig`, `.mcp.json`, `.idea`, …) are sandbox artefacts, not
  project files. Do not touch, add or ignore-list them.
- What you notice outside your task — a bug in another module, a hole in the
  plan, a claim in a task description that turned out false — is worth a
  message. It is the part nobody else can see.

## Run every binary under a memory guard

The machine has no swap, and a test of hostile input run against code that does
not yet bound it can take all the memory there is; the kernel's OOM killer then
ends the whole session, not just the test.

- Sanitised builds: `ASAN_OPTIONS=hard_rss_limit_mb=3000 timeout 600 <binary>`,
  and export the variable for `ctest`. ASan cannot run under `ulimit -v`.
- Everything else: `( ulimit -v 6000000; timeout 600 <binary> )`.
- Write a hostile-input test so that the unfixed code fails it cheaply: a bound
  one step past the limit, not a hundred megabytes.
- Build and test with `-j4` while others are working.

## Scratch Python: never directly in `$TMPDIR`

`$TMPDIR` (`/tmp/claude-1000`) is shared with every other session on this
machine and its top level holds stray `.py` files from them — including ones
named like standard-library modules (`bisect.py`). Python puts a script's own
directory first on `sys.path`, so a scratch script saved directly in `$TMPDIR`
imports those instead of the standard library and runs them. That happened
once in this project (an old headless-Firefox experiment ran eight times).

- Put scratch scripts in a fresh directory: `d=$(mktemp -d); … python -B "$d/x.py"`,
  or under `tools/` in this tree if they are worth keeping.
- Run the reference interpreter with `-P` as well as `-B` (Python 3.11: do not
  prepend the script's directory or the current directory to `sys.path`);
  `PYTHONPATH=../imgui-adv-text` still applies.
- Never `cd` into `$TMPDIR`'s top level to run an interpreter or a build tool.
