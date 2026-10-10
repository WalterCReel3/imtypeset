# Decisions

What was decided, why, and what it rests on. Read this before proposing a
direction; if an entry looks wrong, argue with its reasoning rather than
starting over.

**How this file is kept.** Entries are numbered and appended, never renumbered.
An entry that is overturned stays, marked *superseded by Dn*, and the new entry
says what changed. Each entry separates what was **decided** from what was
**measured** or **read** (and where), because a decision resting on a
measurement falls when the measurement does. Whoever takes a decision writes it
here in the same change; there is nobody to ask afterwards.

"The reference" is the Python implementation in `../imgui-adv-text`. "The spec"
is `../imgui-adv-text/docs/cpp-extension.md`.

[port-hazards.md](port-hazards.md) is the companion to this file: the places
where a literal transcription of the reference goes wrong, line by line, and
the rulings taken on them. D15 onward record those rulings; the detail and the
line references stay there.

---

## D1. Where the port stops

**Decided.** Native: the OpenType subset reader, a custom `ImFontLoader`, the
layout core (`units`, `badness`, `nodes`, `pack`, `linebreak`, `shipout`), the
style subset, `draw_runs`, selection and hit-testing geometry, the math list and
the `mathtext` subset, and a narrow public API with an ImGui-style widget entry.

Not ported: markdown, stream, diff, columns / block layout / tables, and every
view under `advtext/ui`.

**Why.** The spec (§4) describes a narrow reading — `draw_runs` and the font
loader, everything else left in Python — and says to choose before the
differential harness is built. The narrow reading leaves a C++ host with nothing
to typeset *with*: it could draw a command list it has no way to produce. The
stated goal is adoption by C++ hosts and by bindings in other languages, so the
layout core has to be native. The views are the "wide reading" the spec
explicitly does not argue for, and they stay out.

**Consequence.** The differential harness (D10) can compare exactly what is on
the native side of this line and nothing else.

*Amended by D20:* paragraph assembly, which the reference keeps under
`advtext/ui` and in `markdown.py`, is ported.

## D2. Language rules

**Decided.**

- C++17. Builds clean with `-fno-exceptions -fno-rtti`, and the build applies
  both to the library, tests, benchmarks and examples so that this is tested
  rather than asserted (D12).
- Errors: `IM_ASSERT` for programmer error (a contract the caller broke);
  return values or a status for data error (a malformed font, a formula outside
  the supported subset, hostile text). No exception types.
- No iostreams anywhere. `<cstdio>`.
- The STL is allowed inside `src/`. It is **not** allowed in
  `include/imtypeset.h`, which uses only `imgui.h` types, PODs, `const char*`
  and `ImVector`.
- Public names follow the ImGui / implot convention: `namespace ImTypeset`,
  `CamelCase` functions, `ImTypesetXxx` structs, `ImTypesetXxxFlags_` enums.
- Comments describe the code as it is. Ownership is explicit, rule of zero,
  const-correct.

**Why.** ImGui hosts commonly build without exceptions and RTTI, and an
extension that needs either cannot be dropped into them. The public header is
what litgen generates bindings from (spec §6), so every `std::` type in it is a
type every binding has to map; keeping it out is far cheaper now than later.

## D3. No dependencies by default

**Decided.** No HarfBuzz and nothing standing in for fontTools: the font data
the layout needs is read by our own OpenType subset reader (spec §5). FreeType
is optional (D7).

**Why.** It matches the norm implot and its neighbours set, and the subset is
already scoped by what `advtext/fontlib.py` reads.

## D4. Workers do not commit

**Decided.** Nothing is committed or staged by the agents building this tree.

**Why.** The tree had no commits when the work started and the user did not ask
for any; what goes into history, and in what shape, is theirs to choose.

## D5. Internal namespace and naming

**Decided.**

- Everything under `src/` is in `namespace ImTypeset::detail`. Nothing in
  `detail` is declared in `include/imtypeset.h`.
- Files under `src/` are `snake_case` and mirror the reference's module names:
  `advtext/linebreak.py` is `src/linebreak.h` + `src/linebreak.cpp`.
- Inside `detail`, a thing that exists in the reference keeps the reference's
  name: functions and fields `snake_case` (`hpack`, `try_break`,
  `glue_set`), types `CamelCase` (`GlueSpec`, `CharNode`). Private members take
  a leading underscore, as the reference's do.
- The public header follows D2, not this.

**Why.** The port is checked against the reference, by the goldens and by
people reading the two side by side. A function that keeps its name can be
found; ImGui-style renaming of the internals would buy consistency with a
header they never appear in, at the cost of that.

## D6. Where Dear ImGui comes from, and the pin

**Decided.** `cmake/imgui.cmake` builds the `imgui` target unless the host
already has one, taking the source from, in order: `IMTYPESET_IMGUI_DIR`;
`.deps/imgui` in this repository if it exists; `FetchContent` of the pinned
commit. Whichever it is, configure fails unless `imgui.h` in that directory
says `IMGUI_VERSION_NUM 19290`.

`.deps/` is untracked. It is a copy of the pinned tag's sources so that a new
or wiped build directory needs no network; a fresh clone does not have it and
fetches.

The pin is `v1.92.9`, commit `01380c579715e62fb9a8d6ec0502c4ea83bfde6e`, exact
and not a floor.

*Superseded by D44 (2026-10-10): the version is a floor, 19200, with v1.92.9 as
the tested version; a host's ImGui target is never replaced by one built here.*

**Why.** The work was done in a sandbox that cannot reach GitHub, with several
build directories being created at once. The exact pin is because
`ImFontLoader` (in `imgui_internal.h`) changed shape between `v1.92.9` and 1.93
WIP — spec §8 quotes both — so moving it is a porting task. `v1.92.9` is the
ImGui inside imgui-bundle 1.92.900, which the reference runs on.

**Read.** `.deps/imgui` was copied from a `FetchContent` checkout whose `HEAD`
was the pinned commit and which `git describe --tags` named `v1.92.9`; `diff
-rq` of the two showed only `.git` and `.github` missing from the copy.

## D7. The font rasteriser: FreeType where present, and which results depend on it

**Decided.** `IMTYPESET_USE_FREETYPE` adds `misc/freetype/imgui_freetype.cpp`
and `IMGUI_ENABLE_FREETYPE` to the `imgui` target built here. Its default is ON
when CMake finds FreeType and OFF otherwise. The `debug`, `release` and `asan`
presets take that default, as does `wchar16` (D15); the `stb` preset forces it
OFF. Both configurations build and pass.

Code that needs to know asks `#ifdef IMGUI_ENABLE_FREETYPE` (the definition is
PUBLIC on the target) or reads `ImFontAtlas::FontLoaderName` at run time.

**A test that compares anything the rasteriser produced — glyph quads, baked
advances, vertex buffers — is valid only under FreeType**, because that is what
the reference's numbers were taken under. Such a test must not silently pass
under `stb`: it skips with a printed reason, or checks a property that holds
for both. Layout that depends only on font-unit metrics from our own reader
(D3) is unaffected and is tested under both.

**Why.** With FreeType compiled in, ImGui selects it for every atlas (spec §1,
read there from `ImFontAtlasBuildInit`); the reference runs on a build that has
it, and its device-space rules were measured against it. A host may have
either, so both have to build.

**Measured, 2026-10-09, this machine.**

- The FreeType CMake finds here is **2.12.1** (Debian 12 `libfreetype-dev
  2.12.1+dfsg-5+deb12u4`). `pkg-config --modversion freetype2` prints
  `24.3.18`, which is the libtool interface number and not the release. The
  reference's imgui-bundle carries its own FreeType **2.13.3**.
- That version gap did not move anything that was looked at: for
  Roboto-Regular, Roboto-BoldItalic, Inconsolata-Medium and latinmodern-math,
  codepoints 33–126 at 13, 16, 17.5 and 24 px, `ImFontGlyph` `AdvanceX`, `X0`,
  `Y0`, `X1`, `Y1` from this build equal the reference's `find_glyph` answers
  exactly — 0 of 376 differ per face.
- Under stb_truetype, for Roboto-Regular, all 376 differ (373 in the advance).

**Not measured.** Any codepoint outside ASCII, the math face's variant glyphs,
any other size, any loader flag other than the default, the rasterised pixels
themselves (only boxes and advances were compared), and any other machine's
FreeType.

## D8. `ImWchar` is 32 bits

**Decided.** The `imgui` target built here defines `IMGUI_USE_WCHAR32` (option
`IMTYPESET_IMGUI_WCHAR32`, default ON). `tests/smoke.cpp` checks it.

**Why.** ImGui's default `ImWchar` is 16 bits and cannot name a codepoint above
U+FFFF. Math needs two ranges above it: the mathematical alphabets in plane 1,
and the synthetic codepoints the reference mints for variant glyphs that have
no cmap entry, which are in plane 15 (`advtext/fontlib.py`: `SYNTHETIC_FIRST =
0xFFFFD`, assigned downward to `0xF0000`, with the BMP private-use block
avoided on purpose because real fonts use it). The font-loader hook is entered
with an `ImWchar`.

**Read, not re-measured.** The reference records that imgui-bundle is built
with a 32-bit `ImWchar` (`advtext/fontlib.py` and `docs/imgui-risks.md`, both
of 1.92.801), and it draws plane-15 codepoints through it.

*The question this entry left open — what a host with a 16-bit `ImWchar` gets
— is settled by D15: the library works with both, and this entry is only the
default of our own target.*

## D9. The test harness is ours

**Decided.** `tests/check.h`: a `TEST(name)` registry, `CHECK`, `CHECK_EQ`,
`CHECK_NE`, `CHECK_NEAR`, `REQUIRE`, `REQUIRE_EQ`, `FAIL`, `CHECK_CONTEXT`,
`check::ExpectFailures`, and `main()` with `--list`, `--filter=TEXT` and `-v`.
One executable per `tests/test_*.cpp` (plus `tests/smoke.cpp`), each a ctest
test. No third-party framework.

Details that are choices and not accidents:

- A failed `CHECK` records and continues; there is no unwinding to stop a test
  with. `REQUIRE` returns from the enclosing `void` function.
- `CHECK_EQ` compares integers by mathematical value regardless of signedness,
  and anything string-like by content.
- Floating-point values print as `%.17g` and as a hex float, since goldens are
  compared to the last bit.
- After 25 printed failures a test's further failures are counted and not
  printed.
- A `--filter` that selects no test exits 2, so a misspelt filter is not a pass.

**Why.** Dependency-free is the project's own rule (D3) and the sandbox could
not fetch one anyway; the common frameworks either want exceptions or want
configuring not to. What the tests here need is small.

## D10. The golden format and the differential harness

**Decided.** The oracle scripts under `tools/oracle/` run the reference and
write `tests/golden/<group>/*.golden`. Each file carries inputs and the
reference's answers; the C++ tests replay the inputs through the port and
compare. No Python runs on the C++ side, and nothing is ever written into
`../imgui-adv-text`.

Base format, read by `tests/golden.h` (the header's opening comment is the
normative statement):

- UTF-8 text, one record per line; `#` as the first non-blank character makes a
  comment line; blank lines are skipped.
- Tokens separated by spaces. First token: the record tag. The rest: fields.
- Integers decimal. Floats exactly as Python's `float.hex()` prints them, read
  with `strtod`, so doubles round-trip bit for bit.
- Strings percent-encoded: every byte outside printable ASCII, plus space and
  `%`, as `%XX`. The empty string is the single token `%`.

The record vocabulary belongs to each oracle script and is documented beside
it in `tools/oracle/FORMAT*.md`.

**The rule that makes it worth having.** The reference is the oracle. A
difference between the port and a golden is a finding: fix the port, or — if
the reference is what is wrong — record that here and in the test, by name. It
is never a reason to add a tolerance, drop a case, or regenerate a golden from
the port.

**Why.** Spec §7: the reference's tests are the asset, and a harness that diffs
the two implementations replaces most of the re-testing, but only if it is
built first. Hex floats because the reference's glue-set ratios are doubles and
"equal" should mean equal.

## D11. Sources are found by glob, and a build can be narrowed

**Decided.**

- The library is every `src/*.cpp`; tests are `tests/test_*.cpp` plus
  `tests/smoke.cpp`; benchmarks are `bench/bench_*.cpp`; examples are
  `examples/*.cpp`. All by `file(GLOB ... CONFIGURE_DEPENDS)`: adding a file
  adds it to the build, and no task edits a CMake file to do so.
- `IMTYPESET_MODULES`, `IMTYPESET_TESTS`, `IMTYPESET_BENCHES`,
  `IMTYPESET_EXAMPLES` are semicolon lists of basenames; empty means all. A
  name that matches nothing is a configure error, except that a module may be
  header-only (`src/<name>.h` with no `.cpp`).
- With `IMTYPESET_MODULES` narrowed, benchmarks and examples are not built
  unless named.
- Tests, benchmarks and examples all get `src/` and `tests/` on the include
  path and the definitions `IMTYPESET_SOURCE_DIR`, `IMTYPESET_TEST_DATA_DIR`,
  `IMTYPESET_GOLDEN_DIR`, `IMTYPESET_BENCH_DATA_DIR` (absolute paths, as string
  literals).
- Every example is also a ctest test (`example_<name>`), run with no arguments
  in a scratch directory under the build tree. An example run that way must
  exit 0 and write nothing outside its working directory.
- Benchmarks are not ctest tests.

**Why.** The tree is written by several builders at once, and one builder's
half-saved file must not break another's build: each works in
`build/<task-id>` narrowed to what it depends on. The shared presets build
everything and are for when the tree is whole. Examples run under ctest because
the README quotes one and a quoted example that only compiles rots. Benchmarks
do not because a timing means something only from the release preset and no
benchmark asserts one.

**Known cost.** `CONFIGURE_DEPENDS` re-checks the globs on every build, and
the narrowing lists are not transitive: a builder names the modules its code
links against by hand.

## D12. Warning and code-generation flags

**Decided.** `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -fno-exceptions
-fno-rtti -ffp-contract=off` (the last is D16) on everything of ours — library, tests, benchmarks, examples —
through one INTERFACE target, `imtypeset_build_flags`; `-Werror` on top when
`IMTYPESET_WARNINGS_AS_ERRORS` is ON, which it is when this is the top-level
project. None of it is applied to ImGui, whose headers are included as SYSTEM.
The MSVC branch (`/W4 /GR- /fp:precise _HAS_EXCEPTIONS=0`, `/WX`) was written
and **not run**.

**Why.** One target so the four kinds of program cannot drift apart. Not on
ImGui because it is not ours to hold to our warnings, and a host's ImGui is
built however the host builds it.

## D13. Test fonts

**Decided.** `tests/data/fonts/` holds byte-for-byte copies of the seven fonts
listed in `tests/data/fonts/LICENSES.md`, taken from imgui-bundle 1.92.900:
the six files the reference's seven `Face`s resolve to, and `DroidSans.ttf`.

**Why.** The goldens were dumped from those bytes; the C++ tests must read the
same ones and cannot reach into the reference's virtualenv. DroidSans is there
because the font set is no longer closed (spec §5, `fontlib.register_font`) and
the font oracle registers it as a face the reference does not bundle.

**Read.** `advtext.fontlib.font_file(face)` for every `Face`, 2026-10-09:
`PROSE` → `Roboto/Roboto-Regular.ttf`, `PROSE_BOLD` → `Roboto/Roboto-Bold.ttf`,
`PROSE_ITALIC` → `Roboto/Roboto-RegularItalic.ttf`, `PROSE_BOLD_ITALIC` →
`Roboto/Roboto-BoldItalic.ttf`, `CODE` → `Inconsolata-Medium.ttf`, `CODE_BOLD`
→ `Roboto/Roboto-Bold.ttf` (not an Inconsolata), `MATH` →
`latex/latinmodern-math.otf`.

**Open.** The OFL and GUST licence texts are not in the directory;
`LICENSES.md` says what to add before it is published.

## D14. Formatting

**Decided.** `.clang-format`: LLVM base, 4 spaces, 100 columns, brace on its
own line for functions, types and namespaces and attached for control flow,
`Type* name`, includes kept in the order written. clang-format 14. Format the
files you own; never ImGui's.

**Why.** It is the style the skeleton was already written in. 100 columns
because the skeleton's lines ran past 80 and ImGui's own run far longer.

---

D15 to D22 are rulings by the lead, taken after a reviewer read the reference
against the task specifications. [port-hazards.md](port-hazards.md) has each in
full with the reference's line numbers, and marks which of its supporting
claims were run and which were only read; where that file and a task
specification disagree, that file is the current position.

## D15. The library works with a 16-bit `ImWchar` as well as a 32-bit one

**Decided.** Nodes, shipped commands and goldens carry the reference's
codepoints unchanged, plane 15 included. `src/fontlib` exposes one function
mapping a layout codepoint to the codepoint the **atlas** is asked for:
identity when `ImWchar` is 32 bits, and a remap into BMP private-use slots the
face's own cmap does not occupy when it is 16. The font loader and the draw
loop call it at the ImGui boundary and nowhere else.

Our own `imgui` target keeps `IMGUI_USE_WCHAR32` ON by default (D8), so the
default build matches the reference. The `wchar16` preset builds it OFF, and
the gate runs both. A host's own `imgui` target is never given defines by us.

**Why.** Stock ImGui is 16-bit, and requiring `IMGUI_USE_WCHAR32` of every C++
host is an adoption cost not worth imposing for the sake of 227 synthetic
codepoints (`0xFFF1B..0xFFFFD` for Latin Modern Math, counted by the lead).

**State when this was written.** The build side exists: both configurations
configure, build and pass `tests/smoke.cpp`, which checks that `ImWchar` is the
width the build asked for. The remap does not exist yet.

## D16. Floating point is not contracted, and never "fast"

**Decided.** `-ffp-contract=off` on the library, tests, benchmarks and examples
in every preset (`/fp:precise` in the unrun MSVC branch). `-ffast-math`,
`-Ofast` and `-funsafe-math-optimizations` are never used, benchmarks included.

**Why.** Goldens are compared to the last bit (D10). GCC contracts `a*b+c` into
an FMA once `-march` allows it, and Clang does by default where the target has
one; an FMA rounds once where the reference rounds twice, so the goldens would
stop matching on some machines only.

**Limit.** The flag reaches our sources through the CMake targets. A host that
drops the `.cpp` files into its own build has to pass it itself; the README has
to say so.

*Measured and narrowed by D44 (2026-10-10): the drop-in file switches
contraction off itself, and refuses fast-math.*

## D17. What the reference's exceptions become

**Decided.** `IM_ASSERT` for a caller's programming error on an internal entry
point. A status or `bool`, with a reason, for anything reachable from data:
fonts, text, math source, node lists built by hand through the public API.
After an assert the function must still not execute undefined behaviour when
asserts are compiled out — it returns something defined.

**Why.** D2 rules exceptions out, and release builds of ImGui hosts commonly
compile `IM_ASSERT` to nothing, so an assert cannot be the only thing standing
between bad input and an out-of-bounds read. port-hazards.md lists the
reference's raise sites that each need an outcome.

## D18. Rounding helpers live in `src/units.h`

**Decided.** `py_round` (half-to-even: `std::nearbyint` under `FE_TONEAREST`,
never `std::round`, `lround` or `(int)(x + 0.5)`), `floor_div`, `ceil_div`, and
a checked float→`Scaled` conversion that clamps NaN, infinities and
out-of-range values to a stated bound. Every other module calls these. Every
`//` in the reference is a `floor_div`. `pascal_round` (shipout) and `snap_px`
(draw) are their own formulas and are transcribed branch for branch; `snap_px`
is `floor(v + 0.5)` in double and is **not** ImGui's `IM_ROUND`, which
truncates and is wrong for negative values.

**Why.** Python's `round()` and `//` do not behave like their nearest C++
spellings, the goldens contain the cases where it shows (exact halves are
routine with a 2048-upm font), and one definition is easier to get right than
a dozen.

## D19. `Rect` lives in `src/shipout.h`

**Decided.** So that `draw` and `selection` share it without depending on each
other. `BlockKind` and `Palette.ink_for` stay on the Python side of D1: the
native draw entry takes colours, not block roles.

## D20. Paragraph assembly is ported — a carve-out from D1

**Decided.** `src/paragraph.*` ports what turns styled text into a broken
paragraph: `_skips`, `_text_boxes`, `_shape` and `Strut` from
`advtext/ui/__init__.py` and `style.py`, and `_Builder._append_text` from
`markdown.py`, with the reference's own breaking parameters
(`PROSE_BREAKING`: tolerance 1000, emergency stretch 20 pt; first-fit for
anything not justified). The public API builds on it and does not invent that
layer.

**Why.** D1 put "every view under `advtext/ui`" and markdown out of scope, and
these functions live there, but they are not views or markdown parsing: without
them the native side has a line breaker and nothing that builds what it
breaks, and each host would reimplement the stand-in, whitespace, kerning and
strut rules differently from the reference.

## D21. Font keys are interned; the char node remembers what was written

**Decided.** The reference's font key is the string `face@size`, stored in
every `CharNode` and `Glyph` and re-parsed by the draw loop. The port stores an
interned (face id, size) in the node; `core-nodes` fixes the scheme and the
golden reader maps the strings onto it. The char node also carries the
original codepoint of a character set from a stand-in face (the reference's
`ReplacedChar`), as a field or a declared side table, so that copy and find
return what was written.

**Why.** A string per glyph is the wrong thing to carry through the hottest
value in the core. Nothing ported orders by font key (read by the reviewer,
not run), so interning cannot change a result.

## D22. What "equal to the reference" means where exactness is not available

**Decided.**

- **Glyph ink boxes.** What must be exact is what layout consumes:
  `round(height)`, `round(depth)`, `round(width)` in sp at every size the
  oracle dumps. The raw float box is compared with a stated tolerance, and the
  largest deviation and its cause are reported.
- **Drawing.** The exact, rasteriser-independent comparison is a pen-level
  golden — per glyph the font key, baked size, pen position, colour and
  codepoint; per rectangle its corners — with baked size and ascent per key
  given as input. Full vertex equality is checked only in a FreeType build and
  is reported, not gated, unless the FreeType version equals the golden's.
- **Kerning.** Where the reference's GPOS lookup differs from the OpenType
  rule (a format-1 subtable that covers the first glyph but not the pair falls
  through to later subtables), the port matches the reference and the
  difference is recorded in `docs/divergences.md`.

**Why.** D10 forbids tuning a test to pass. These are the cases where a
tolerance or a narrower comparison is the honest test and not a loosened one:
the ink box depends on a curve-extrema computation fontTools does in floating
point, and quads depend on a rasteriser the host chooses (D7). Each is named
here so that no other tolerance can claim the same excuse.

**Bearing on D7's measurement.** D7 found the boxes and advances of four faces
identical between FreeType 2.12.1 and 2.13.3 for the cases it lists. That is
evidence the vertex comparison would hold here, not a reason to gate on it.

---

D23 to D25 are instructions the user gave while the work was under way,
relayed and written up by the lead in [port-hazards.md](port-hazards.md).

## D23. `tex.web` is the structural reference; the Python reference is the behavioural one

**Decided (the user's instruction, 2026-10-09).** The original TeX82 WEB
source, `../imgui-adv-text/docs/tex.web`, is what this tree is structured,
named and documented against. `../imgui-adv-text/docs/tex-notes.md` is an
audited digest of it; its "CORRECTIONS" section overrides its own body and is
read first.

- A thing that is TeX's has TeX's name: `hpack`, `vpack`, `try_break`,
  `active` / `passive`, `glue_set`, `mlist_to_hlist`, `make_fraction`,
  `hlist_out`. (D5 said "the reference's names"; the reference took them from
  TeX, and where the two differ D5 still holds for anything that is the
  reference's own.)
- A function or block that transcribes a WEB section cites it (`§851`), and
  the citation is checked against `tex.web` itself, not copied from the
  Python. Section numbers are counted from the top of the file;
  `tex-notes.md` explains and spot-checks the numbering.
- Where the code does something `tex.web` does not — because the reference
  does, or because the reference leaves a part out (delta nodes, hyphenation,
  `\looseness`, the penalties of §890) — the comment says so in the present
  tense and names the section departed from.
- **Behaviour follows the Python oracle, not `tex.web`.** Where they disagree
  the port matches the oracle, cites the section, and the disagreement is
  added to `docs/divergences.md`. The port is not "fixed" toward TeX.
- `tex.web` is not copied into this tree and runs of it are not pasted into
  sources or documents: cite section and line. Nothing here is named `tex.web`
  or calls itself TeX.

**Why.** A reader with *TeX: The Program* open should be able to follow the
port, and the goldens can only hold the port to what the oracle does.

**Read.** `tex.web` is 25,010 lines (the lead's count). Its opening notice:
copyright D. E. Knuth, all rights reserved; copying and redistribution
permitted while the file is not modified; modification permitted only if the
result is not named `tex.web`; "TeX" is a trademark of the American
Mathematical Society.

## D24. Licensing hygiene

**Decided (the user's instruction, 2026-10-09: whatever affects licensing must
stay compatible).** No licence has been chosen for this project, and choosing
one is the user's decision. Until then the tree stays compatible with a
permissive, MIT-style licence, which is the norm among ImGui extensions.

- Code is written from specifications and from the Python reference, not from
  other implementations. fontTools, FreeType, HarfBuzz and MicroTeX may be
  read to learn what a table means; a function ported from one of them does
  not enter the tree unless it is listed in `THIRD-PARTY.md` with its notice.
- Code adapted from Dear ImGui (MIT) or using `imstb_truetype.h` (MIT / public
  domain) carries the upstream notice where it is used and has an entry in
  `THIRD-PARTY.md`.
- No GPL or LGPL code or data. Fonts installed on the machine are read where
  they are and never copied in; goldens made from them hold paths, hashes and
  measured values only.
- A table generated from Python's `unicodedata` says so, names the Unicode
  version, and carries the Unicode licence notice.
- The fonts under `tests/data/fonts` are test inputs, never compiled in or
  installed (D13).
- Anything brought in that this entry does not cover is raised before it lands.

**State when this was written.**

- `THIRD-PARTY.md` does not exist yet; the documentation task writes it from
  what the tree then contains.
- The OFL 1.1 and GUST Font License texts are not in `tests/data/fonts`
  (D13). Neither could be obtained verbatim from the sandbox the work ran in:
  the lead found only an OFL 1.0 text on the machine, and tug.org refused the
  request.
- `tests/golden/math/unicode.golden` is derived from `unicodedata` (Unicode
  14.0.0). Its header names the source and the licence and points at
  `https://www.unicode.org/license.txt`; **the notice text itself is not in
  the tree**, for the same reason, and belongs in `THIRD-PARTY.md`.

## D25. The OpenType reader is for the fonts hosts bring, not for the bundled seven

**Decided (the user's instruction, 2026-10-09).** Hosts will hand this library
arbitrary fonts, so the reader supports what real fonts contain or refuses it
with a reason, and `docs/font-support.md` lists which: TrueType and CFF
outlines, collections by face index, the cmap formats in use, `hmtx` shorter
than the glyph count, composite and empty glyphs, both `loca` widths, a
missing `OS/2`, GPOS PairPos formats 1 and 2 with every value format,
Extension lookups, variable fonts at their default instance. CFF2, WOFF, WOFF2
and Type 1 are refused with a reason. The legacy `kern` table is read only if
the reference reads it, and is otherwise listed as a known gap, because the
oracle cannot check behaviour the reference does not have.

It is validated against every font installed on the machine the work is done
on (about 2,900 `.ttf`, 54 `.otf` and 14 `.ttc` files, by the lead's count),
compared with what fontTools and the reference answer for each, and the font
loader is run over a sample of the same corpus.

This withdraws the sentence in the `font-reader` task that scoped the kern
lookup to "whatever the bundled faces really use", and it narrows D3: the
subset is still what `advtext/fontlib.py` reads, but read from any font.

**Why.** D3's subset was scoped when the font set was closed. It is not
(spec §5: `fontlib.register_font` is public), and a reader that has only met
seven files fails on the eighth in a host's hands, not in a test.

---

## D26. Ship-out commands: one 64-byte tagged value, and what `ship` does that the reference does not

**Decided (the builder of `src/shipout`).**

- A command is `DrawCmd`: one trivially copyable 64-byte struct with a type
  tag (`GLYPH`, `RULE_RECT`, `SET_GLUE`, `ATOM`, `LINE`), in a
  `std::vector<DrawCmd>`. `ATOM` is a tag with `SET_GLUE`'s fields, not a
  subtype; `DrawCmd::is_set_glue()` is the reference's
  `isinstance(cmd, SetGlue)`, true of both.
- Every coordinate and dimension in it is a 64-bit `Scaled`. The font is the
  `FontKey` of D21, sharing its slot with `natural`, which a glyph does not
  have. Indices are 32 bits.
- `ship` appends to a buffer the caller owns and numbers indices from 0 on
  each call, as the reference does for each call.
- Where the reference raises `ValueError`, `ship` returns a `ShipError` and
  leaves the buffer at the size it had on entry (D17).
- `ship` takes `max_items`, a bound on the nodes it will visit, and returns
  `TOO_MANY_ITEMS` past it. The default is the largest index.

**Why.**

- One size, one buffer: the draw loop walks it every frame and hit-testing
  indexes it, and a C ABI or a binding can hand it across as memory (spec §4).
- 64 bits, not 32, although a dimension *stored in a node* is bounded by
  `MAX_DIMEN`: a position is a sum down a page (a million 20 pt lines pass
  2^31 sp), and a line's width is a sum too. Narrower fields would make the
  port differ from the oracle on long documents only. A 40- or 48-byte
  command was not measured against this one; the benchmark task can, and if
  it matters the glyph can be narrowed there with a number to show for it.
- The bound exists because a box may be named by more than one node — that is
  how the reference's shared tuples are represented — so a tree of forty nodes
  can describe a walk of a million (twenty levels, each holding its child
  twice), and one of a thousand levels a walk that never ends. The reference
  has the same property and no bound. `hpack`, `vpack`, `deep_equal` and
  `NodeArena::import` walk the same trees and have none either; see
  "not decided" below.

**Measured.** `tests/test_shipout.cpp` ships every case of
`core/shipout.golden` (126), the assembled lines of every paragraph the
reference broke (586 paragraphs; 26,965 glyphs, 10,862 set glues, 2,933
lines, 94 atoms) and every formula box of the math goldens (4,397), and
compares the commands as text with what the reference shipped: identical, in
debug, release and under ASan and UBSan. Twenty deliberate changes to
`src/shipout` (rounding rule, per-node rounding, shrink sign, order test, the
clamp, each atom rule, pen position after a box, shift direction, rule
placement, the vlist's top, the error contract) each fail at least one test.

**Not decided.** Whether the other walks need the same bound, and what a host
that hands in a shared-box tree through the public API is promised. That is
the hardening task's, with this as its first case.

**Read.** WEB section numbers cited in `src/shipout.h` and `src/shipout.cpp`
were checked against `tex.web` by counting section starts: `hlist_out` 619,
characters 620, the other nodes 622, a box in an hlist 623, a rule 624, glue
and `vet_glue` 625, leaders 626–628, `vlist_out` 629, its nodes 630–631, box
632, rule 633, glue 634, leaders 635–637, `ship_out` 638 with the page offset
in 640, `is_running` 138, math nodes 147, pruning at a line start 879. One
citation in the reference differs: `advtext/shipout.py` places
`cur_v := cur_v - height(this_box)` in §630; in `tex.web` it is in §629. The
behaviour is the same.

## D27. The atlas gets its glyphs through an in-memory cmap, not a custom `ImFontLoader`

*Decided by the lead on the `font-loader` builder's evidence. It departs from
the design note's §2, which names a custom `ImFontLoader` as the mechanism;
the user may overrule it.*

Each face reaches the atlas through `AddFontFromMemoryTTF`, from a copy of the
font whose `cmap` is rewritten in memory (`src/font_loader`,
`build_atlas_font`): one (3,10) format-12 subtable written from `src/fontlib`'s
own codepoint-to-glyph view, plus the minted codepoints, plus — for a 16-bit
`ImWchar` — the BMP stand-ins of D15. Every other table is the file's bytes;
`DSIG` is dropped; checksums are recomputed. ImGui's built-in loader, whichever
it is, then runs unchanged. No `ImFontLoader` is implemented and nothing in the
font path includes `imgui_internal.h`.

Why. At v1.92.9 `FontBakedLoadGlyph` is entered with a codepoint and both
built-in loaders resolve it themselves (`stbtt_FindGlyphIndex`;
`FT_Get_Char_Index`), so a custom loader cannot delegate: it has to rasterise
by glyph index itself, which is a second copy of each built-in loader on
`imgui_internal.h`, one of them linking FreeType. The `cmap` is the only other
place a codepoint the font does not encode can be given a glyph. What the
design note objected to in the reference's on-disk rewrite was its cost (about
a second through fontTools), the temp directory, and the degradation path for
fonts with no format-12 subtable. In memory the rewrite is measured at 0.53 ms
for Latin Modern Math (release, this machine, the builder's run), needs no
directory, and always emits format 12. A host's rasteriser, loader flags and
`ImFontConfig` keep working.

What it buys beyond that: the design note's §8 risk — sitting on a struct that
has already changed in 1.93 — does not apply to this path. Not verified by
compiling against 1.93; no 1.93 source is in the tree.

What it costs: one extra copy of the font's bytes, owned by the atlas (733 KB
for the math face).

What it means for the design note's argument: §2 calls the loader "a capability
that is structurally unreachable from Python". An in-memory `cmap` rewrite
handed over as bytes is not obviously unreachable from Python — it depends on
the note's own open item in §11, whether `ImFontConfig.font_data` accepts
bytes, which nobody here has tried. `docs/spec-status.md` must say this
plainly rather than repeat §2.

Also recorded here from the same report: `add_face_to_atlas` sets
`OversampleH = 1` when the caller leaves it unset (stb otherwise bakes glyph
boxes off the pixel grid below 36 px); with a 16-bit `ImWchar`, 0 of 7,505
boxes differ from the reference's, so hosts do not need `IMGUI_USE_WCHAR32`;
`draw` asks the atlas for `detail::atlas_codepoint(face, cp)` and treats 0 as
missing.

## D28. Decisions taken in module headers, indexed

Builders who did not own this file recorded their decisions at the top of
their headers and in `.concerns/<task>--<role>--result.md`. The ones later
tasks inherit: `Scaled` is `int64_t` with a stated `MAX_DIMEN` invariant and
float-to-`Scaled` clamps (NaN to 0, huge to 2^62) (`src/units.h`); a node is a
32-byte tagged value, boxes and discs live in a `NodeArena`, nesting is refused
past 1,024 (`src/nodes.h`); **`FontKey` is a packed 64-bit value, which
replaces D21's "interned"**; `hpack`/`vpack` are measure-only and report data
errors as `PackResult::error` (`src/pack.h`); `LineBreaker` is reusable scratch
with zero steady-state allocation, parameters are clamped to 2^40
(`src/linebreak.h`); `imstb_truetype.h` is not used for metrics because its
glyph box is the header or control-point box, not the curve extrema the
reference computes; fonts are parsed at registration and a registry change
retires old data instead of freeing it (`src/opentype.h`, `src/fontlib.h`).
**D22's tolerance on the float ink box is withdrawn: the reader matches all
11,838 boxes exactly.**

---

## D29. Selection: the reference's rules, found by bisection where the reference scans

**Decided (the builder of `src/selection`).**

- `SelectionModel::build` indexes a shipped buffer once and keeps no reference
  to it. Fragments are identified by the position of their `LINE` in the
  buffer, never by value (port-hazards: the reference keys by `id(line)`).
- The line under a point is found through *clusters*: runs of fragments, in
  reading order, that no fragment outside the run overlaps vertically, where a
  fragment's extent is its band together with its row's band. The cluster is
  found by bisection and the reference's three-step rule is applied inside it;
  when no band contains the point, clusters are tried outward until every
  line further away is strictly further in y than the best found.
- Within a line, an x is found by bisection when the line's edges ascend, and
  by the reference's walk when they do not.
- Indices are looked up with CPython's `bisect_left` and `bisect_right`
  transcribed step for step, not `std::lower_bound`.
- A range query (`rects`, `text`) looks only at fragments that neither end
  before the range starts nor start after it ends.
- `text` takes its data — hard breaks, separators, what an atom copies as,
  what a replaced glyph was written as — as arrays sorted by index.
- `src/selection.cpp` carries its own tables for `str.isalnum` and
  `str.isspace`, generated by `tools/oracle/dump_selection.py
  --emit-unicode-tables`.

**Why.** A host hit-tests on every mouse move, and the reference's model scans
every line of the document to do it. Each shortcut above is one for which the
reference's answer can be shown to be the same: only a cluster's own fragments
can have a band or a row containing a point inside it; a fragment outside a
range gives `owned_range` a first index above its last, for which the two
bisections return "none" whatever order the indices are in; and where a
shortcut needs an ordering that a hand-built buffer may not have (edges), it
is checked at build time and the reference's own loop is used instead.
`bisect` is transcribed because on a list that does not ascend the answer is a
property of the algorithm, and the reference's is the one to match.

**Measured.** `tests/test_selection.cpp`: all 57 cases of
`tests/golden/selection` (245 fragments, 12,000 probe points, 1,300 drags,
1,650 ranges, 8,200 texts) answered as the reference answered them; then
600 random buffers — columns, overlapping bands, nested and level lines —
with 240,000 points and 24,000 ranges compared with the rule restated as a
plain scan in the test. Debug, release, ASan and UBSan. Thirty-one deliberate
changes to `src/selection.cpp`: twenty-nine fail a test; one was this file's
own unnecessary guard, since removed; one cannot be observed (it concerns a
fragment the range narrowing has already excluded). Point queries perform no
allocation (counted through a replaced `operator new`). Over a column of
100,000 lines, `hit` and `fragment_at` together took about 0.4 µs in a
release build on the machine this was written on; that is one run, not a
benchmark.

**Where the port differs from the reference.**

- `rects` on a line whose items' indices do not ascend: the reference raises
  `ValueError`; the port gives that line no rectangle. Ship-out never makes
  such a line.
- A codepoint that is not a Unicode scalar value is written by `text` as
  U+FFFD; Python cannot hold one past U+10FFFF and raises.
- A query point is clamped to ±2^62 sp before any arithmetic.
- `Selection` and boundaries are unsigned 32-bit; the reference's are
  unbounded integers and `Selection(-1, 3)` can be written there.

**Not decided.** Three modules now need Python's Unicode predicates (this one,
`mathtext`, and the paragraph builder), each from its own task. One shared
table would be smaller and could not disagree with itself; that is a
consolidation for whoever has all three in front of them.

---

## D30. The public API: what `include/imtypeset.h` promises, and the ImGui version it needs

**Decided (the builder of `public-api`).** The full list with reasons is in
`.concerns/public-api--*--result.md`; what later work inherits:

- **The ImGui pin is a floor, not an exact version.** No file under `src/`
  includes `imgui_internal.h` any more (`grep` finds the name only in
  comments): D27 took the font path off `ImFontLoader`, and `src/draw` uses
  `imgui.h` only. So the `#error` on `IMGUI_VERSION_NUM != 19290` in
  `src/imtypeset.cpp` is replaced by `IMTYPESET_IMGUI_VERSION_NUM_MIN 19200`,
  checked in the header. 1.92.0 is where the dynamic atlas arrived
  (`ImFontBaked`, sizes baked on demand, `ImGuiBackendFlags_RendererHasTextures`;
  read in `.deps/imgui/docs/CHANGELOG.txt` under "VERSION 1.92.0"). That removes
  the design note's §8 risk for hosts. `IMTYPESET_IMGUI_VERSION_NUM` stays as
  "built and tested against". *Not measured:* nothing here has been compiled
  against any ImGui but 1.92.9; 1.92.0 to 1.92.8 and 1.93 are read, not run.
  `cmake/imgui.cmake` still explains the pin by `ImFontLoader`; that comment
  is now out of date.
- **`CreateContext` checks the caller's build through default arguments**
  (`sizeof(ImWchar)`, `ImDrawVert`, `ImDrawIdx`, `ImTypesetStyle`, the version
  string), which are evaluated in the caller's translation unit, and refuses a
  mismatch with a reason instead of asserting (review item P2). It also
  refuses when the ImGui it is linked with is not the one it was compiled
  against.
- **Fonts** (P1, P3): one atlas font per face added, never per size;
  `ImFontConfig::FontNo` is forced to 0; `MergeMode` is refused; the header
  says nothing may be merged into a returned font in a 16-bit `ImWchar` build
  (the stand-ins of D15 start at U+E000, where icon fonts live). A font is
  tried on a registry of its own first, so a refused font leaves the
  context's registry and the atlas as they were. `face_index` is in the
  signature and anything but 0 is refused until `font-wild` reads collections.
  A font added for a role that already has one leaves the old `ImFont` in the
  atlas: removing it could pull the default font out from under the host.
- **Errors are data**: null or false plus `GetLastError()` /
  `GetParagraphError()` (code, message, byte and codepoint position). No
  public function asserts on its arguments; all work with no context.
- **One validating entry** (`resolve_style`, `resolve_width` in
  `src/context.h`): every host length is clamped there, the em to 1..1024 px
  and to `MAX_DIMEN / 16`. Glue built from a font's space is clamped again as
  material is built, because a space factor widens it after `src/style`
  clamped the space.
- **An index is exposed only next to its paragraph.** `ImTypesetSelection`
  and every index-taking function take the `ImTypesetParagraph*`; there is
  one `SelectionModel` per paragraph, built at the first query. Copy across
  paragraphs is `AppendParagraphSelectionText` called per paragraph on one
  `ImGuiTextBuffer`, which is where the `"\n\n"` goes. Hard breaks are
  `"\n"` separators, as `para` said they must be.
- **`DrawParagraphToBuffers` writes `ImDrawIdx`**, the type a host's renderer
  already takes, and refuses (nothing appended) when the vertices would not
  fit it, rather than writing 32-bit indices no ImGui renderer consumes.
- **`Text()` finds its cached layout by the text pointer and the ID stack**,
  then compares the bytes; `ImTypesetTextFlags_NoTextCompare` drops the
  comparison for a caller that can promise stable bytes. The n-th use of one
  ID in a frame has its own slot. Entries unused for 600 frames are dropped.
- **Item size is whole pixels**, the width being the width given; ink can
  reach one pixel past the right edge (measured: 10 pixels of 19,143 in the
  rendered test paragraph), because glyphs are snapped to whole pixels after
  the line was justified in scaled points.
- **Not in the API, on purpose**: list markers, more than one block per call,
  a general `\parshape`, the debug overlay, a selectable `Text()` widget,
  unregistering a font.

**Measured.** `tests/test_api.cpp`, 19 tests, in debug, release, ASan+UBSan,
stb_truetype and 16-bit `ImWchar` builds: 225 of the 243 `para` goldens
through the public API give the reference's commands exactly and, drawn,
343,536 vertices byte-equal to `src/draw` on the reference's commands; a
steady-state `TextJustified` call performs 0 text builds, 0 layouts and 0
allocations (`operator new` and ImGui's allocator both counted); a call whose
rectangle is clipped emits 0 vertices and, with `NoTextCompare`, reads 0 bytes.

---

## D31. Formulas in the public API: two switches, the reference's dollar rule, and what replaces markdown

**Decided (the builder of `api-math`).** The full list is in
`.concerns/api-math--*--result.md`; the departures from the reference are in
`docs/divergences.md`, `## api-math`.

- **Added to `include/imtypeset.h`, nothing changed**: `EnableMath(bool)`,
  `GetParagraphFormulaCount`, `GetParagraphFormula`, the plain struct
  `ImTypesetFormula`. `ImTypesetTextFlags_Math` keeps its value and gains its
  meaning. No existing declaration, struct size or enum value moved.
- **Opt-in is the context's switch AND the text's flag.** The flag alone does
  nothing. Why two: a context serves a host's whole UI, and only the host
  knows which of its strings are prose with formulas and which are labels
  with prices; and `tests/test_api.cpp`, which this task may not edit, holds
  that the flag with nothing installed changes nothing while a math face is
  registered. Collapsing it to the flag alone is a one-line change in
  `build_material` (`built_in`) if the lead prefers it.
- **The scanner is the reference's `_math_inline` with markdown-it's escape
  rule in front and nothing else of markdown** (`scan_math`). `\$` is a
  literal dollar whose backslash is not set; no other backslash is touched;
  backquotes, asterisks and style runs hide nothing.
- **Display math enters by the same flag**: a text that is wholly a `$$`
  block is centred, unindented and in display style (`display_block`, the
  reference's `_math_block` held to one text), and `$$...$$` inside a line is
  display style in the line, as in the reference. A reviewer asked for a
  separate flag instead; kept as one because the reference decides it from
  the text, the golden's 13 block cases are held that way, and a second flag
  would make every host and binding reimplement the block rule to get the
  reference's result. No style field was added: a display takes the block's
  own indent and leading.
- **A refused formula** is its source, dollars included, in the code face at
  the size of the text around it, with no break inside (breakable for a
  display block), as the reference sets it. `GetParagraphError()` answers the
  first refusal of a paragraph with the reference's sentence and the place in
  the text in bytes and in codepoints; `GetParagraphFormula()` answers for
  every formula. For `Text()`, which has no handle, the refusal is
  `GetLastError()` on the frame the text was laid out.
- **The space factor after a formula is neutral** (tex.web §1076, and the
  reference's `_formula`); `public-api` had left it untouched around an atom.
  `inline.golden` case 57 (a period after `NASA $y$`) is the case that tells
  the two apart; that it fails without the reset is read, not run.
- **Bounds are the modules' own**: `mathtext` refuses past 24 levels of
  source nesting (at most 49 levels of list), `math_mlist` past 64, the arena
  past 1,024. Nothing here raises or re-checks them; a test asserts the
  arithmetic and walks 60 depths of three shapes through the API.

**Measured.** `tests/test_api_math.cpp`, 20 tests, 28,810 checks, in debug,
release, ASan+UBSan, stb_truetype and 16-bit `ImWchar` builds, with
`tests/test_api.cpp` unchanged and passing in each. All 71 cases of
`inline.golden` through `SetParagraphText`/`LayoutParagraph`: material node
for node, 102 widths, breaks, line boxes, 2,749 glyphs and 78 atoms shipped
where the reference ships them. Steady-state frames of a justified paragraph
with three formulas and a display block: 0 text builds, 0 layouts, 0
allocations; the same for a kept paragraph laid out, drawn to buffers with a
selection, hit-tested and asked for rectangles. A display-size summation
(U+FFFA4, minted; asked of the atlas as U+E43A when `ImWchar` is 16 bits)
inks 430 of the 1,656 pixels of its box and none outside it under FreeType,
420 under stb_truetype.

**Not measured.** The golden's vertical assembly has no strut, so a
command's vertical place is compared relative to its line's baseline, and
line heights only as "not less"; there is no reference record of a strutted
line holding a tall formula. No timing. No mutation run. `Text()` inside
tables. `AppendParagraphSelectionText` allocates (about 14 allocations for
the test paragraph, in `src/selection`'s `text`); it is a copy, not a frame,
and was left.

## D33. Hardening: what a list, a font and a text nobody vetted get back

Taken by the `stress` task, alone, from two reviewers' located lists
(`.concerns/review-layout-core--reviewer-3--findings.md`,
`.concerns/review-font-stack--reviewer-2--findings.md`) and from what the
fuzzers then found. `docs/hardening.md` has one row per item with the input
that broke the code, the fix and the test; `docs/divergences.md`, `## stress`,
has what now differs from the reference. The decisions:

**Decided.**

- **Total fit's budget is 2^20 candidate lines** (`DEFAULT_LINE_BREAK_WORK_BUDGET`),
  counted across passes; a candidate that has to be laid out node by node
  costs one unit per sixteen nodes besides. Chosen from the measured curve
  (`docs/hardening.md`): one unit is about 80 ns in an optimised build, so
  the budget is about 85 ms; a justified paragraph reaches it at roughly 400
  lines (the sample's last paragraph, 8 lines, costs 792; four copies 9,676;
  sixteen 118,915; sixty-four 1,656,882 and fall back), a ragged one broken
  by total fit at roughly 28 lines (75,153 for 8 lines; 1,363,730 for 32).
  Sixteen times the "ordinary long paragraph" of the ruling has a factor of
  nine in hand. The fallback is first fit on the same prepared list, so it
  is exactly what asking for first fit gives.
- **The fallback is a status, not an error**, and the public header has no
  status that is not an error: `GetParagraphError()` answers `Code` None with
  a `Message`. A proper field is `api-revise`'s to add. A paragraph that fell
  back is laid out by first fit directly at its next width, until its text,
  fonts or style change: otherwise every step of a window resize pays the
  budget again for the same answer.
- **Arithmetic over a hand-built list is checked, and free where the list is
  the library's own.** `hpack` and `vpack` are written once against two
  policies (`src/pack.cpp`): `Guarded` folds every dimension that enters a
  sum into a guard word and lets the sums wrap, which is exact, and proven
  so afterwards, when every dimension was within +-2^30 and the list within
  2^30 nodes; `Checked` tests each addition and is run only when the guard
  says the first answer cannot be vouched for. A checked addition in the
  walk's loop was measured at +57% on a four-node packing (18.8 to 29.5 ns),
  nearly all of it from keeping the sums in a struct the optimiser would not
  split; with plain locals and the guard it is 21.3 ns, and the reflow
  benchmark is level with the tree before (462-473 us against 460-475 us,
  same sitting, alternating). `ship` checks every addition; its cost did not
  move (52.4 us against 52.7 us for 74 lines).
- **`src/units.h` and `src/pack.h` were edited** though neither is in the
  task's file list: `add_checked`/`sub_checked`/`SCALED_MAX` belong with the
  arithmetic every module shares, and two `PackError` values have to be in
  the enum. So was `tests/core_golden.h` (review item K is the task's) and
  two tests that pinned the behaviour item F overturns
  (`tests/test_shipout.cpp`, `tests/test_pack.cpp`: a slug each, and the
  test of a non-box root).
- **`ship`'s default bound is proportional, not exact** (divergence 3).
- **What is not fixed, and why.** ImGui's font atlas asserts ("Out of texture
  memory") when the glyphs asked of it in one frame do not fit its largest
  texture: one paragraph of ASCII and Latin-1 (188 different glyphs) at
  `EmSize` 1024 does it; at 700 it fits. That is ImGui's limit for any text
  at such a size and is reached through `DrawParagraph`. Lowering
  `MAX_EM_PX` would move the threshold, not remove it; drawing large type
  from a smaller bake would, and is a change to the device rules
  (`docs/subpixel.md`), which is not this task's to make.

- **A face is checked strictly before a rasteriser sees it**
  (`OpenTypeFont::strict_check`, called by `add_face_to_atlas`): every `loca`
  pair, glyph record, composite and CFF index and charstring a rasteriser
  follows. stb_truetype checks none of them, and a `loca` pair a megabyte
  past `glyf` is a wild read on the first draw. "A defect confined to one
  glyph does not fail the load" stays true of a face registered for metrics
  and stops being true of one given to the atlas. Among what it refuses that
  a reader could live with: a composite that places a component by point
  matching (stb_truetype asserts on one), a glyph whose last contour is one
  off-curve point (stb_truetype reads a byte past its buffer), a glyph
  reaching further than 16 ascender-to-descender heights (the furthest in
  2,905 installed fonts is between 6 and 8). It costs 2.9 ms on the math
  face and 0.2 ms on a text face, once; setting up the math face through the
  public API goes from 2.9 ms to 6.2 ms. Of 2,905 installed fonts the reader
  accepts, it refuses none.
- **The atlas's copy of a font has `USE_TYPO_METRICS` cleared in OS/2.**
  FreeType sizes a font by its typo metrics when that bit is set; layout and
  stb_truetype size it by hhea. 66 of 2,917 installed fonts set it with a
  different extent, and under FreeType their glyphs were baked at up to 1.65
  times, or as little as 0.675 of, the size layout had set them at. The
  bundled fonts do not set it.
- **A set of metrics is kept for 256 to 512 sizes a role**
  (`METRICS_SIZES_KEPT`), and an expired one is freed only when the owner of
  the registry says it holds no pointer (`release_expired_metrics`): three
  places keep such pointers between calls, so eviction inside `metrics()`
  would have been a use after free. The context releases at the start of
  each text build.
- **One GPOS budget** for the whole table, with lookups and subtables read
  once per offset; **a directory naming a tag twice is not a font**; the
  atlas's copy is never larger than the tables it was made from.

**Not measured.** 32-bit targets (review item S4 is about one); a compiler
other than GCC 12; MSVC, where `add_checked` takes its portable branch,
which no build here compiles; FreeType other than 2.12.1; hinting programs,
which FreeType runs and no structural pass can bound (a mutated `fpgm`
reaches the same ImGui assertion; `ImGuiFreeTypeLoaderFlags_NoHinting` is
the host's defence against a font it does not trust, and `src/font_loader.h`
says so).

## D32. imtypeset is the product; advtext is the prototype it grew from

*Direction from the user, 2026-10-09: "advtext may only have been a proof of
concept prototype. The touch point may be better as a full imgui ecosystem
addition." The consequences below are the lead's reading of that direction and
are the user's to overrule.*

1. **The integration point is imtypeset itself**, as a first-class Dear ImGui
   extension (the implot shape): used directly from C++, and from Python
   through generated bindings in imgui_bundle. It is not designed as an
   accelerator underneath advtext, and no API is added for the sake of keeping
   advtext's Python views running on top of it.
2. **advtext remains the differential oracle** for everything already ported:
   it is the only executable specification there is, and the goldens are the
   regression net. That role does not make it the authority on what is
   *right*.
3. **Where the prototype has a defect, the product does not inherit it.** This
   amends D23's "behaviour follows the Python oracle". A departure of the
   reference from `tex.web` is FIXED in imtypeset when (a) it is established
   against `tex.web` by section and line, and (b) the reference's own documents
   (`docs/decisions.md`, `docs/advance-rule.md`, module docstrings) do not
   record it as a deliberate choice. Deliberate choices stay: the device-space
   rules, first-fit for ragged text, no hyphenation, no `\mathchoice`, the
   line breaker's class-per-line-count formulation. Each fix keeps the
   differential net intact by regenerating the affected goldens from the
   reference **patched in memory** by the oracle script (never on disk), with
   the patch stated in the script and in `docs/divergences.md`.
   Known so far: Appendix G §755 (italic correction with a subscript), §735
   (underline depth), §1091 (paragraph indent applied twice — already fixed at
   the public API).
4. **D1's "not ported" list is no longer a boundary, it is a backlog.** A
   library offered to the ecosystem has to stand without Python behind it. In
   order: (i) a native block/document layer — several paragraphs, headings,
   list items with markers, code blocks, quotes, rules, with the spacing
   between them — which is being added now; (ii) tables and columns; (iii) a
   markdown front end as an optional separate file, where the ecosystem
   already has `imgui_md` and a dependency choice (md4c or a small parser) has
   to be made; (iv) streaming text; (v) diff views. (ii)-(v) are NOT started
   and wait for the user's word on priority.

## D34. The product shape: imtypeset core, a C ABI and bindings, and advtext reworked as the Python layer over it

*User direction, 2026-10-09, in three steps: the repo should offer the core
with a Python host layer for markdown and the math subset, posturing for an
optional full LaTeX parser later; then "the adv-text project actually does
become a necessary wrapper around imtypeset"; then "ensure that a plan for
adv-text rework for imtypeset is created that will be fully optimized for the
performance gains of the typesetter", and runnable demos for both a native
host and a minimal Python host with direct hooks. This amends D32.1.*

- **Core (this repo, C++):** everything that is typesetting — the engine, the
  block/document layer, tables and columns when they come, selection, draw.
  Its public boundary is host-neutral: styled blocks in; layout, draw and
  selection out (task `api-document`). advtext is its first and most demanding
  client, not its only possible one.
- **Bindings (this repo):** a C ABI (`include/imtypeset_c.h`) as the seam for
  every other language; a ctypes Python package over it that runs today
  against an installed imgui_bundle; the litgen/imgui_bundle route drafted for
  vendoring.
- **advtext (its own repo):** keeps what is content — markdown with the math
  subset, streaming, diff, find, the views, themes — and gives up its layout
  core to imtypeset. That rework happens in that repo, in another session;
  this repo owes it a migration map (`docs/advtext-migration.md`), a measured
  plan (`docs/advtext-rework-plan.md`) and an API that makes the plan's best
  case reachable.
- **The oracle is frozen before the swap.** advtext's pure-Python core is what
  the goldens were dumped from; once advtext runs on imtypeset it cannot check
  it. Step 0 of the rework is preserving that core (tag/branch) with the
  oracle scripts pinned to it.
- **Full LaTeX is postured for, not built:** the math boundary is the math
  list, so a parser can live outside the core; the engine gaps it would need
  (alignments, extensible assembly, `\mathchoice`) are written down.
- **Markdown in C++ is not planned.** A C++ host feeds the Document API from
  its own parser; an md4c or imgui_md adapter is a possible later addition.
- **Two demos are deliverables:** the native SDL2 host and the Python host.

## D35. Fonts: what the reader reads beyond the reference, and what it keeps of the reference's shortcuts

Taken by task `font-wild`, alone, on the lead's rulings in
`docs/port-hazards.md` ("the reader is for real fonts", "collections") and the
task brief. `docs/font-support.md` has the census and the feature table;
`docs/divergences.md`, `## font-wild`, has each departure.

**Decided.**

- **A face index reaches the reader** as `FontLoadOptions::face_index` and
  `FontLibrary::register_font(..., why, face_index)`, a trailing defaulted
  parameter so that nothing that registered a font before changes. An index a
  file does not have is a refusal that says how many faces there are. The
  atlas's copy of a collection face is a single-face font made from that
  face's directory (`build_atlas_font` takes it from `OpenTypeFont`), so
  `ImFontConfig::FontNo` stays 0.
- **The legacy `kern` table is read, as an extension, on by default.** 21 of
  2,991 installed faces have no other kerning. It is used only for a face
  whose GPOS has no pair subtable under `kern`, so no font the reference
  kerns is kerned differently. `FontLibrary::set_legacy_kern(false)` gives the
  reference's behaviour; the differential corpus test runs that way and the
  extension has goldens of its own from fontTools' reading of the table.
- **A codepoint mapped to glyph 0 is not mapped** (`has()` false), in every
  cmap format, as fontTools reads it. `OpenTypeFont::cmap()` never holds such
  an entry.
- **Glyph names come from where fontTools takes them**, because the reference
  mints synthetic codepoints in name order: the CFF charset (`cidNNNNN` for a
  CID-keyed font), or a format 1 or 2 `post` table with fontTools' own rule
  for unnamed and repeated names. That rule and the acceptance of cmap format
  12 and 13 groups are fontTools' behaviour and not the specification's; both
  are listed in the upstream notice in `src/opentype.cpp` and in
  `THIRD-PARTY.md`, although the code was written afresh.
- **Where the reference refuses or reads nothing and the specification is
  clear, the product reads**: cmap format 10, the next subtable when the
  preferred one is unreadable, Symbol-only and Macintosh-only fonts, a font
  with `numberOfHMetrics` zero or a `loca` one entry short. Each warns. The
  last two are refused by the strict pass, because stb_truetype reads outside
  the table for both.
- **The missing glyph is glyph 0**, not "the glyph named `.notdef`" (review
  item W12; two installed faces differ).
- **A CID-keyed font is checked for what stb_truetype does not check**: a
  glyph whose Font DICT index is past the Font DICT INDEX, or that no FDSelect
  range covers, is refused by the strict pass. stb_truetype asserts on the
  first (`stbtt__cff_index_get`); the second is safe in the copy Dear ImGui
  vendors, which carries a fix upstream stb_truetype lacks, and is refused
  all the same so that the answer does not depend on which copy is linked.
- **The reference's kerning shortcuts stay** until someone decides otherwise
  (D32.3 asks that they be listed, not silently chosen): first value across
  lookups, script and language ignored, lookup flags ignored, second value
  record ignored.

**Measured.** Whole corpus: 2,990 of 2,991 faces read with every compared
value equal, 1 refused by design, 0 mismatched (release, debug, ASan+UBSan).
Kerning by accumulation instead of first value: 1,426 of 149,304 sampled
pairs differ, on 84 of the 590 faces with more than one `kern` lookup. The
loader over 353 sampled faces under FreeType and stb_truetype:
`IsGlyphInFont` equals `FaceData::has` on all 113,013 codepoints asked, which
settles D27 beyond reading.

**Read, not run.** What stb_truetype does with the two CID shapes above is
from reading `.deps/imgui/imstb_truetype.h` (`stbtt__cid_get_glyph_subrs`,
`stbtt__cff_index_get`); the strict pass keeps both from reaching it.

**Not measured.** Variable fonts, CFF2, WOFF, Apple `kern`: none is installed.
The Type 2 arithmetic operators against fontTools (no installed font uses
them; the tests hold hand-computed boxes).


## D37. The block layer: the reference stacked exactly, the product's rules as switches, and a document laid out lazily

Taken by the `blocks` task, alone, under the lead's rulings
(`.concerns/blocks--lead--rulings.md`). The number skips D36, which
`api-revise` refers to and had not written when this was appended.

**Decided.**

- **What is ported is `_stacked` and its helpers**, not
  `advtext/blocklayout.py` (a row/column demo; the board's specification named
  it first). `src/blocks.*`, in `ImTypeset::detail`; no public header change.
- **`DocumentRules` holds the fifteen prototype shortcuts** of the oracle
  report: thirteen switches (two of them for item 7), default the reference,
  `DocumentRules::product(em)` everything on. Three items have no switch
  because there is nothing to switch: item 1 (the product follows
  `compose_blocks`, as the goldens do), item 14 (which block an item belongs
  to is data in every mode, and agrees with the reference's bisection on all
  799 golden spans) and item 15 (markers are data; this layer makes none).
  `docs/divergences.md`, `## blocks`, has the table.
- **Items 9 and 13 are fixed by what a block can say, and their switches gate
  the part of that this layer acts on.** In the reference both are defects of
  the markdown front end (a quoted list item is given the quote's style; a
  rule is given the enclosing indent and no more), and this layer is handed
  style values. So a `Block` names its list *and* its quote, and
  `list_spacing_in_quotes` (9) is whether the list's spacing rule applies
  inside a quote; `rule_in_container` (13) is whether a rule takes what
  happens to its container's indent (widened gutter, least measure).
- **Containers are data, by id**: `Quote {parent, list, bar_x}` and
  `List {parent, indent, tight}`, a parent being an earlier entry (so a chain
  ends without a visited set), and `Block::quote`, `Block::list`,
  `Block::item`. A depth alone (`Block::quote_depth`, what the goldens give)
  still works: quotes then merge as the reference merges them and a bar's x
  comes from indents. Reason for ids over flags: two quotes in a row, a bar's
  own x, a list's widest marker and tight-or-loose are all properties of one
  container instance, and a front end has that instance in its hands when it
  opens it.
- **The product's gap rule** (item 10): two blocks of one item are spaced by
  their own two spaces; otherwise the innermost list both blocks are in
  decides, nothing if it is tight and the two spaces if it is loose; blocks
  with no list in common are spaced by their two spaces. So a tight list
  nested in a loose item is tight inside and loose around. Not taken from
  tex.web, which has no lists; chosen to match what CommonMark's tight and
  loose mean.
- **`min_measure` cuts the indent, not the text** (item 8): the alternative,
  refusing to lay out, leaves a window with nothing in it.
  `product()` sets eight ems, and two ems of hanging indent for a wrapped code
  line; both are parameters, picked without a measurement.
- **Clip keeps every character** (item 7): a clipped code line is a box as
  wide as the measure whose contents run past it, and the layout says so. The
  alternative, dropping the characters that do not fit, would lose them to
  copy and leave half a glyph's worth of space unexplained. Clipping pixels is
  the draw's (`PushClipRect`), which this layer does not do.
- **`Document` keeps one layout per block: the block shipped alone**, from its
  own corner, with its own indices. Not nodes: the arena is scratch, rewound
  after each block, so a block's layout is freed by overwriting it and no
  node storage outlives a call. A quote is stacked from its blocks' line
  dimensions (boxes with no contents through the same `Stacker`), so a change
  to one block of a quote lays out that block only. `flatten` rebuilds the
  reference's one sequence and is what the goldens are compared with.
- **A unit is the reference's unit** (a depth-0 run). Unit heights are in a
  Fenwick tree, exact or estimated. `prepare` is one O(blocks) pass of
  arithmetic after any change of content, rules or width; `layout_window` is
  O(log units + units shown) and never touches a unit above the window, so
  nothing above it moves.
- **Positions for selection and copy are (block, index)**, with
  `ParagraphLayout`'s map on each `BlockLayout` for (block, source byte): one
  `SelectionModel` per block is what `api-document` builds. Nothing here
  selects across blocks.

**Measured** (private build dirs, GCC 12): the goldens replay exactly in debug,
release and ASan+UBSan. 35 single-line mutations of `src/blocks.cpp`: one did
not compile; of the 34 that ran, 32 failed the suite at once, one survived
through a gap in a test (two items of one list never stood next to each
other; the test was extended and the mutation now fails) and one was
equivalent (a condition `derive` already implied, removed). N = 10,000 paragraphs: `prepare` composes 0 blocks, a 300 pt window
10, a window at mid-document 10 to 12 without composing anything above it, a
width change 0 until asked.

**Not measured.** Any timing: no benchmark was written, and the O() claims
rest on the counters and on reading the loops. Memory per laid-out block
(64 bytes a command; nothing evicts). A document edited while it streams
(every `add_block` makes the next `prepare` O(blocks)). Estimates' accuracy on
real text: the one in the test is exact by construction.

## D36. The public API, revised before its first release (task `api-revise`)

Taken by the builder of `api-revise`, alone, from the lead's rulings R1 to R14
(`.concerns/api-revise--lead--rulings.md`), the review they answer
(`.concerns/review-public-api--reviewer--findings-full.md`) and the binding
notes (`docs/bindings-notes.md` §5). This supersedes what D30 says about
`CreateContext`'s arguments, the pointer-keyed widget cache, public indices,
`NoTextCompare` and "unregistering a font is not in the API", and what D31
says about two switches. The full before/after table is in
`.concerns/api-revise--*--result.md`; departures from the reference are in
`docs/divergences.md`, `## api-revise`.

**Reproduced first.** 21 of the review's claims were written as tests against
the header as it was and run under ASan+UBSan before anything changed: 21
failed as claimed (A1 four ways, A3, B1 to B6, B8, B10, B11 twice, B12, C4,
C7, D2, D4, D5). B6 reproduced as something else than claimed: on a backend
without texture updates it is ImGui's own atlas build that asserts
(`font_size > 0`) on a font added with no size, before any call of ours. Not
reproduced: B7 (Windows; nothing here compiles it), B9 (no width was found at
which a layout fails and another at which it does not; the error stages below
make it impossible by construction all the same).

**Decided.**

- **Positions are bytes of the source text, everywhere** (R3). The struct is
  `ImTypesetTextRange {Start, End}`; hit tests, lines, carets, ranges and copy
  speak it; shipped indices are internal. A range covers the items whose
  source byte is in it. Four free functions convert bytes and codepoints, two
  on any text and two on a paragraph's own, because a binding's wrapper has
  the paragraph and not the string (`docs/bindings-notes.md` §5.5).
- **A widget finds its layout by the ID stack and a hash of the text**, then
  compares the bytes, so a collision costs a layout and never shows the wrong
  words. `TextWithID` takes a string ID instead, for text that changes every
  frame. `NoTextCompare` is gone: nothing public depends on an address. The
  cache has two limits (`SetTextCacheLimits`: 2048 entries, 64 MiB by
  default) and never drops what the current frame or the one before
  submitted, so a frame larger than the limit lays each text out once and
  not once a frame. It is a clock, not a list in order of use: entries stay
  in order of arrival and the oldest arrival is either dropped or, if still
  in use, sent round; a hit moves nothing. A use-ordered list was written
  first and writes to two cold neighbours on every hit of every call. The
  limit is a function on the context and not a field of `ImTypesetStyle`,
  because a style is handed to single calls.
- **One error model** (R5). Every entry that can fail returns false or null
  and is marked `[E]` in the header; `GetLastError()` is one process-wide
  slot (it must answer when no context could be made), set by a failing
  `[E]` call and cleared by a succeeding one. A paragraph keeps its own
  error in three stages (text, material, layout), each starting from the one
  before, so nothing is left over from a layout at another width. A widget
  reports its text's standing error on every frame it is submitted, not only
  the frame it was laid out. `LayoutParagraph` therefore returns `bool`, and
  the size is `GetParagraphSize`. The widgets return `bool` too, which in
  ImGui means "pressed" or "changed"; here it means "set as asked", and the
  header says so.
- **Not an error: `ImTypesetStatusFlags`.** The total-fit fallback D33 left
  as "Code None and a Message" is `ImTypesetStatusFlags_FirstFitFallback`
  from `GetParagraphStatus()`; the message is gone.
- **The build check is a macro** (R4): `IMTYPESET_CHECKVERSION()` passes the
  version and eleven sizes to `DebugCheckVersionAndDataLayout`, as ImGui's
  does. Unlike ImGui's it does not assert (D17): it returns false, and
  `CreateContext()` then refuses until a check passes, so a host that ignores
  the result still gets no context. `CreateContext()` takes nothing and makes
  the new context current only if none is.
- **A struct a host fills is all zero by default** (`ImTypesetStyle`,
  `ImTypesetRun`): `LineHeight` 0 is 1.35, `PixelsPerInch` 0 is 96, `Scale` 0
  is 1. For that `ImTypesetFace_Default` is 0 and the roles are 1 to 7; the
  registry's `Face` is the public value minus one (`role_of`). Enum values
  are written out; `tests/test_header_pod.cpp` holds the numbers.
- **A struct the library answers with is returned by value**
  (`ImTypesetLine`, `ImTypesetHit`, `ImTypesetFormula`, `ImTypesetStats`,
  the caret as an `ImVec4`), never through an output pointer: it is what a
  generated binding returns as written (`docs/bindings-notes.md` §5.2). The
  one in-out pointer left is `SelectableText`'s `selection`, which is state
  the host may own, as a checkbox's `bool*` is.
- **The context is bound to the ImGui CONTEXT, not to its atlas** (review
  B5). Every entry that touches the atlas, a draw list or ImGui's style
  refuses unless that ImGui context is current. Two ImGui contexts sharing
  one atlas therefore need an ImTypeset context each, which registers the
  fonts twice; binding to the atlas was not done because nothing in
  `imgui.h` tells an atlas that was destroyed and reallocated at the same
  address from the one the fonts are in.
- **A font the host removed is found missing**: before each use the roles'
  `ImFont*` are looked up in the atlas's list, with the atlas's `FontId`
  beside the pointer. A role that lost its font is unregistered. A paragraph
  laid out before a font change is not drawn until it is laid out again.
  `RemoveFont(face)` exists, and adding a font to a role that has one
  removes the old `ImFont` from the atlas (D30 kept it; a font picker then
  grew the atlas by one font a pick).
- **A backend without `RendererHasTextures`** is refused where it can be
  known: `AddFont*` when a renderer has already introduced itself
  (`io.BackendRendererName`) without the flag, and every drawing entry at
  the first frame, the widgets falling back to ImGui's text. What cannot be
  intercepted is a legacy host that adds a font with no size before its
  renderer is up and then builds the atlas: ImGui asserts in its own build.
  The font's `SizePixels` was left at 0 rather than given a size that would
  avoid it, because that size would become ImGui's default font size for a
  host whose first font is ours.
- **Font files are read by the library and registered from memory**, so a
  UTF-8 name works on Windows (`_wfopen`) and the file is read once, not
  three times. The Windows branch has never been compiled.
- **`face_index` is passed to the registry when the registry takes one**
  (R13): `register_in` in `src/imtypeset.cpp` detects the overload at compile
  time, so this task and `font-wild` could land in either order.
- **Out of view and only the width changed: not laid out** (R7). Evidence
  for doing it: 200 paragraphs, one step of a resize, 200 layouts before and
  2 after. The price is in the header: the scroll extent is an estimate
  until the rest has been seen at the new width.
- **Per-call style and wrap width are parameters, not a Push/Pop stack**
  (R8): `TextUnformatted(text, text_end, flags, wrap_width, style)`. An
  alignment for one call is a copy of the style with one field changed. A
  stack was not added: it is state a binding has to balance, and the
  paragraph entry already takes a style pointer.
- **`Text(fmt, ...)` formats** and `TextUnformatted` does not (R9);
  `TextJustified` is gone (it was the default); first fit / total fit is the
  enum `ImTypesetLineBreak`.
- **Added** (R10): `CalcTextSize`, `GetParagraphLine`, `GetParagraphCaretRect`,
  `ImTypesetHit` from `HitTestParagraph` (offset, line, and whether the point
  is on text), `SelectableText`.
- **Kept though the review wanted them gone**: `GetImGuiVersionNum()` and
  `IMTYPESET_IMGUI_VERSION_NUM` (`tests/smoke.cpp`, which this task does not
  own, uses them, and they are not inert); `text_end` (ImGui's idiom; a
  binding drops it by name, as it does ImGui's); `TextBuilds` and
  `BytesHashed` in `ImTypesetStats`, now documented as a debug facility whose
  names follow the implementation.
- **Not done**: a fallback chain for characters a face lacks (R10 says not
  now); the alternative pen rule (it is in `src/draw.cpp`); a status for the
  widgets beyond their error (a widget whose text fell back to first fit
  cannot say so; a paragraph can); `SelectionColor` as a draw-time argument
  (a host that wants another colour fills `GetParagraphRangeRects` itself).

**What the Document API inherits** (R14). Nothing shipped here needs
renaming for it: a document's block IS an `ImTypesetParagraph` (the query
functions take a `const ImTypesetParagraph*` and do not ask who owns it, and
`DestroyParagraph` already ignores one that is not in its context's own
list), and a position in a document is a block number beside the byte offset
every function here already speaks: `ImTypesetTextRange` stays the range
within one block, and a document range is two (block, offset) pairs in a
struct of its own. `AppendParagraphRangeText` already joins blocks with a
blank line.

**Measured.** In private directories, the full suite unnarrowed, on the final
sources, in debug, release, ASan+UBSan, stb_truetype and 16-bit `ImWchar`:
the result per configuration is in `.concerns/api-revise--*--result.md`,
with the two test files of another task that were failing while it was being
edited. `tests/test_api.cpp` (39 tests), `tests/test_api_math.cpp` (19),
`tests/test_stress_text.cpp` (10) and `tests/test_header_pod.cpp` pass in all
five. What they hold that D30's did not:

- 225 of the 243 `para` goldens and all 71 formula cases are still replayed
  exactly through the new entries, 343,536 vertices byte-equal, and for each
  of 4,539 lines `GetParagraphLine` answers the reference's LINE command.
- Every byte range of a wrapped paragraph (1,653 of them) copies as exactly
  those bytes of the source, less a space the line was broken at when the
  range begins or ends with it. One selection kept as bytes copies the same
  words at 24 other layouts (four alignments, six widths); its item numbers
  differ from the first layout's in 12 of them.
- The SECOND frame of five paragraphs: 0 text builds, 0 layouts, 0
  allocations between the first call and the last, and 0 in the whole frame
  with ImGui's own work counted. The paragraph path with a selection,
  `DrawParagraphToBuffers`, a hit test, range rectangles, a line and a caret:
  0 in the second frame. A frame that reflows two paragraphs allocates (10
  the first time, 1 the second); the frame after it does not.
- A 100 px window over 29, 285 and 2,850 lines places 259, 259 and 259
  commands. That the lines are found by bisection is read in `src/draw.cpp`
  and asserted only as far as the line index being sorted.
- A clipped widget on the default path costs its bytes read twice (hash and
  compare) and nothing else: 0 layouts, 0 allocations, 0 vertices.
- 200 paragraphs, one step of a resize: 2 layouts and 198 deferred.
- Timings: `docs/performance.md`, "After the API revision". A widget call
  costs about 50 ns more at 340 calls a frame and 110 to 180 ns more at
  3,400; where that goes was not isolated.

**Not measured.** The Windows file branch (never compiled). clang, MSVC, any
ImGui but 1.92.9. litgen or any other generator on the new header: the
per-declaration notes in the result file are a reading. `SelectableText`
under a real mouse and a real clipboard (the test feeds ImGui's event queue
and a clipboard function of its own); keyboard focus between two selectable
texts in one window. Two ImGui contexts with one shared atlas. A text of more
than 16 MiB through a widget. `FontRasterizerDensity` other than 1.

## D38. Font reader: five rulings from finishing `font-wild` (2026-10-10)

**Decided.**

1. **A cmap format 13 group stays one range, end to end.** `CmapRange` has a
   flag, `one_glyph`: every codepoint of the range maps to `glyph`, where
   without it glyph ids run on. The reader keeps a format 13 group as one such
   range; `FaceData` inverts it as one; `build_atlas_font` writes the atlas's
   cmap as format 13 when the face's ranges are of that kind and as format 12
   otherwise (both rasterisers read both). The alternative was a cap on the
   codepoints a format 13 table may map, which would refuse or truncate the one
   real font of this shape, the Unicode Last Resort font.
2. **A format 12 group's glyph ids do not wrap.** A group whose ids would pass
   2^32 - 1 maps the codepoints whose ids fit and no others, with a warning.
   `OpenTypeFont::lookup` therefore never answers glyph 0 and never an id the
   file did not name.
3. **A legacy `kern` sum is held to ±MAX_DIMEN where it becomes a length**, in
   `FontMetrics::kern` (`src/fontlib.cpp`), not in the reader: the reader keeps
   what the table states (`pair_kern` may answer up to 2^31 units), and a value
   inside the bound is returned exactly as computed, so no golden moves. The
   bound is applied there, and not left to each caller, because the math list
   rounds a kern with `round_to_scaled` alone (`src/math_mlist.cpp`), which
   bounds at `SCALED_LIMIT` and not at `MAX_DIMEN`.
4. **A Type 2 charstring whose arithmetic leaves a coordinate infinite or NaN
   draws no glyph.** The glyph's ink box is empty, with the warning every
   malformed glyph gets, and the strict pass refuses the font. fontTools
   computes in unbounded integers and would have a box; no font was found that
   does this, and the reference has no golden for one.
5. **A glyf header box that ends at or below the baseline over an outline that
   rises above it is repaired in the atlas's copy, not refused.**
   `OpenTypeFont::atlas_box_repairs()` lists such glyphs (the strict pass
   finds them) and `build_atlas_font` raises each header's yMax over the
   glyph's points. Refused only when no 16-bit yMax can state the bound, which
   takes a composite placed past 32,767 units.

**Measured** (for 5; the task's specification asked for a refusal and its
pre-written test asserted one):

- stb_truetype called directly (a scratch program over `imstb_truetype.h` with
  `STBTT_assert` counting): a Roboto glyph with its header rewritten to
  yMin -100, yMax 0 fails `z->ey >= scan_y_top` four times at every size from
  8 to 96 px; with yMax -50 at 8 to 24 px and not at 48 or 96; with yMax -200
  at 8 px only. That is the model the rule is written from: the assertion needs
  the bitmap to start at row 0 and an edge wholly above it.
- A refusal rule ("header yMax <= 0 and a point above the baseline") refused
  **5 of the 2,990** installed faces the atlas had taken until then:
  NanumBarunGothic.ttf and NanumBarunGothicBold.ttf (glyph 17929: header yMax
  -4, outline to 11; stb_truetype really does fail the assertion on it, 6
  times at 200 px and at 240 px, at no other size tried), Ponnala.ttf (glyphs
  697 and 698: header yMax 0, one control point at 2; no failure at any size
  tried), kalimati.ttf and samanata.ttf (glyph 174: a one-point contour 505
  units up, which has no edge; no failure).
- With the repair: 2,990 of 2,990 baked under FreeType (release, and
  ASan+UBSan), under stb_truetype (debug, ImGui's assertions live) and under
  FreeType with a 16-bit ImWchar; 3 faces and 4 glyphs have a header raised
  (the two Nanum faces and Ponnala). The Nanum glyph with yMax raised to 11:
  no assertion failure at any of 9 sizes, 8 to 1000 px, at oversample 1 and 2.

**Inferred, not read.** That FreeType does not use the glyf header's box for
what ImGui asks of it: the corpus baked the same under FreeType before and
after (277,868 inked glyphs, all visible with coverage). FreeType's source was
not read for it.

**Not measured.** The Nanum glyph baked through ImGui at 200 px (the corpus
test bakes at 16 px; the mechanism is tested at 16 and 200 px on hand-made
fonts). Any font whose composite bound is far larger than its outline: the
raised box is an outer bound there, and the bitmap is larger than it need be.

## D39. Hardening, second pass: formulas, selection, the draw loop, and a host that unwinds

Task `harden-2`. The items are a reviewer's reading of `src/mathtext`, `src/math_mlist`,
`src/selection` and `src/draw` (H1 to H8, D3, D4, D6, D7) and five of the lead's (X1 to X5). The
table of what each was, what was observed and what holds it is `docs/hardening.md`, "Second pass".

**Decided**

1. *A formula has a longest source: 524,288 bytes (`MAX_SOURCE_BYTES`, `src/mathtext.h`).* A longer
   one is refused before it is decoded, kind `LAYOUT`, "the formula is too large to set: it is
   longer than 524288 bytes, which is the longest that is read". The reference has no bound. The
   value is the smallest power of two above the reference's own table of hostile sources (450,000
   bytes), which `tests/test_mathtext.cpp` replays at its real sizes and which must go on being
   answered as the reference answers. A smaller bound (64 KiB was tried) is cheaper and would have
   meant rewriting that replay.
2. *"Too large" is not the font's fault.* `MathError::TOO_LARGE` and `TOO_DEEP` become a refusal of
   kind `LAYOUT` whose reason starts "the formula is too large to set: ", not kind `FONT` with "the
   math face could not set it". No new `RefusalKind`: the eighteen kinds are the reference's, and
   `LAYOUT` is its word for a source in the subset that the engine would not set.
3. *No box of a formula is outside ±`MAX_DIMEN`.* `Converter::node_of` (every box `mlist_to_hlist`
   stores) and `set_formula` (the formula's own box) answer `TOO_LARGE` with the dimension in
   `needed` and `MAX_DIMEN` in `largest`; nothing is stored. Refused rather than clamped: a box
   whose stated width is not the width of what it holds is a second defect. No new `MathError`:
   `tests/math_golden.h` switches over the enum.
4. *A reason is a C string.* A reason quotes the source, and the source may hold U+0000 (a
   character like any other to the parser, as to the reference). Where a reason enters a public
   struct (`ImTypesetFormula::Reason`, `ImTypesetErrorInfo::Message`) each NUL is written as the
   four characters `\x00`, Python's spelling (`c_string_reason`). `detail::Refusal::reason` is
   unchanged and still the reference's byte for byte, NUL included. The alternative, a length
   beside every `const char*`, was not taken: it changes two public structs for one character.
5. *The proportion in a layout refusal is written without `printf("%.2f")`*, whose decimal
   separator is the host's `LC_NUMERIC`. `quotient_to_two_places` forms the same double the
   reference does and writes its exact value to two places, ties to even, with integers.
6. *`SelectionModel` takes any buffer and any `TextData`.* Coordinates read from a command buffer
   are held to a quarter of `SCALED_LIMIT` (2^60), so no sum or difference the model forms can
   overflow. Each `TextData` array is checked for order once per `text()` call and searched entry
   by entry if it is out of order: same answer, more time. The contract ("sorted, no index twice")
   stands as the fast path; nothing asserts.
7. *The debug overlay's outlines are the library's own rectangles*, one pixel thick, not ImGui's
   `AddRect`/`AddLine` (whose geometry is anti-aliased, depends on the list's flags and changed in
   ImGui 1.93). They are written into buffers too, which used to leave them out. The two debug
   goldens are compared at the calls (`ops`) and no longer at the vertices: own rectangles cannot
   equal `AddRect`'s (136 and 1,324 vertices against the reference's 120 and 1,268).
8. *The draw loop keeps three `[Internal]` members of `ImDrawList` for glyph quads*
   (`_VtxWritePtr`, `_IdxWritePtr`, `_VtxCurrentIdx`), and reads `_CmdHeader.ClipRect`. Measured
   (X3, `docs/performance.md`): every quad through the public `PrimRectUV` is 15 % slower for a
   draw into a list; through the public inline `PrimWriteVtx`/`PrimWriteIdx` 3 % slower, and that
   route still has to read `_VtxCurrentIdx`. Neither is within the 1 % noise of the measurement, so
   by the lead's ruling they stay. Rectangles do go through the public `PrimRect`, which is also
   what makes them take the white texel of the list's own shared data (D3). `_CmdHeader.ClipRect`
   stays because the public `GetClipRectMin()` reads `_ClipRectStack.back()`, which is undefined on
   a list whose clip stack is empty, and `AddText` itself reads `_CmdHeader`.
9. *Batched reservation stays (D4).* Past 64k vertices with 16-bit indices a draw command ends up
   to 1,024 quads earlier than with a reservation per glyph. Same vertices, same triangles
   (tested); a different place where one `ImDrawCmd` stops. Reproducing the reference's boundary
   would need the list's vertex counter before every quad and ImGui's own order of reserve and
   cull, for something no renderer shows.
10. *`DrawRules`: `reference()` and `product()` for the draw loop* (`src/draw.h`), the same pattern
    as the layout rules. One rule so far, `rule_joins_span` (D7): under `product()`, which the
    public API draws with (`ImTypesetContext::draw_rules`), a rule inside a formula belongs to the
    formula's rigid span. Its left end is placed from the nearest glyph of the formula placed
    before it, its right end from the nearest placed before or after it (64 glyphs each way, by
    origin), each by the gap in scaled points rounded once; top and bottom from the span's
    baseline; the span goes on. The ends are settled when the span ends, when the glyphs after the
    rule have been placed. Under `reference()`, the default of `DrawArgs` and what every golden
    replay runs under, a rule is snapped alone as the prototype does. No golden changed.
    The first form of the rule placed both ends from the glyph placed before the rule. That is
    right for a radical's bar and wrong for a fraction's, whose last numerator glyph is at the
    bar's far end: `py-host`'s builder saw the bar of the quadratic formula stand 3 px off its
    numerator, and the form above is the correction. Left end from earlier glyphs only, because a
    radicand's first glyph begins nearer the bar's start than the sign's origin does, and the
    joint that shows is the sign's.

    The rule of this item is under revision in task `d7-fix`: a later review, made by
    reading with nothing run, found its placement suspect
    (`.concerns/review-3a--reviewer--findings.md`).
11. *The library is safe to unwind through, without catching anything.* A host's `IM_ASSERT` may
    throw (imgui_bundle's Python build), and with exceptions enabled so may `operator new`. The
    guarantee is the basic one at every public entry: nothing leaks, the draw list holds whole
    quads, the widget cache counts what it holds, and the same call made again does what it would
    have done. Held by ordering and by destructors (`Building` in `src/imtypeset.cpp`,
    `std::unique_ptr` until ownership has passed, the pool's room reserved when the context is
    made). In the draw loop nothing fallible happens
    between a `PrimReserve` and its `PrimUnreserve`: every glyph is loaded before anything is
    reserved, so there is nothing to guard there (a guard object was tried and measured, not kept).
12. *Not done, on purpose (X4):* trusting the content hash instead of comparing the text (the one
    thing that would bring a content-keyed widget call to the cost of a pointer-keyed one), a
    one-entry memo of `resolve_style`, and a flat table in place of `std::unordered_map` for the
    widget cache. The first changes a guarantee and is the user's call; the other two are worth
    some tens of nanoseconds of a call by the profile and were not tried.

**Measured**

- Parsing a run of `x` asks for 208 bytes of heap per byte of source (109 MB at the bound).
- H1 before the bound: a 100 MB source passed 2.5 GB resident and was still growing when killed
  (the lead's measurement); at 524,289 bytes the unbounded parser simply parsed it.
- D7 before the rule, 576 draws (6 sizes, 0 to 60 terms before `\sqrt{x}`, 16 sub-pixel origins):
  the bar's left edge was from 3 px inside the sign's quad to 3 px clear of it, its top from 1 px
  above the sign's to 1 px below. After: the same offset across in all 576, within a pixel down.
- D7 on fractions, 144 draws under each set of rules (four fractions, one of them the quadratic
  formula; 13, 17 and 24 px; 0, 13 and 40 terms before; four origins): against the glyph at that
  end as it was drawn, a fraction bar's end is up to 6.00 px from where the commands put it under
  `reference()` and at most 1.00 px under `product()`; a radical's bar inside the numerator 4.91 px
  and 0.42 px from its sign.
- Pictures under both rules were rendered with the software renderer and looked at
  (`IMTYPESET_HARDEN_SHOTS`): under the reference's, a radical sign plainly apart from its bar at
  13 px after sixty terms and by a pixel at 24 px after thirteen, and after forty terms the
  quadratic formula's numerator 5 px right of its bar with the sign printed over the `b`; under the
  product's, the same fraction whether nothing or forty terms come before it.
- D6 by a run: an atlas capped at 512 x 1024, 120 frames each at another size out of 27: 224 bakes
  discarded, the texture replaced under a draw in 24 frames, and all 11,040 quads on a glyph
  rectangle of the atlas as it then stood.
- X1: allocation-failure sweeps unwound `SetParagraphText` + `LayoutParagraph` from 148 points,
  `DrawParagraph` from 49, `TextUnformatted` from 145, `AddFontFromFileTTF` from 84 (sampled past
  the 48th), `CreateContext`/`CreateParagraph` from 5; ImGui's "Out of texture memory" assertion was
  thrown from under `DrawParagraph` and under `TextUnformatted`. ASan + UBSan + LeakSanitizer: no
  report.
- The library and tests also build, with no warning in this tree's sources, against the bundle's
  real configuration header (`../imgui_bundle/imgui_bundle_cmake/imgui_bundle_config.h` with
  `IMGUI_BUNDLE_BUILD_PYTHON`: `IM_ASSERT` throws `std::runtime_error`, `ImDrawIdx` is 32 bits);
  `smoke`, `test_api`, `test_draw`, `test_harden` pass there. `../imgui_bundle/litgen_imconfig.h`
  is ImGui's stock `imconfig.h` with nothing enabled but a Windows `IMGUI_API`; it needs nothing.

**Read, not run**

- That nothing between `PrimReserve` and `PrimUnreserve` in `Drawer::emit` can throw: read from
  `ImDrawList::PrimRect` in the pinned ImGui (no assertion) and from the loop (no container of
  ImGui's indexed, no font asked).
- ImGui's own state after it throws "Out of texture memory" from inside its atlas: the test holds
  that this library and the draw list are whole and that a later, smaller text is laid out; it
  does not hold that the atlas itself is as it was.

**Not measured / not verified**

- MSVC, clang, Windows. ImGui 1.93 (the overlay change is meant for it and was not built against
  it here).
- An exception thrown by anything but `operator new` and `IM_ASSERT`, and one thrown inside a
  destructor (ImGui's own destructors assert, and a throwing `IM_ASSERT` there is `std::terminate`:
  GCC warns of four such sites in ImGui's sources).
- `FontLibrary` and the font loader under allocation failure beyond what the `AddFontFromFileTTF`
  sweep reaches (those files belong to `font-wild-finish`).

## D40. The C ABI: `include/imtypeset_c.h` (task `c-api`)

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
- **No exception crosses.** Built without
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

## D41. The reference's undeclared departures from `tex.web` are switches: `TexRules::reference()` and `TexRules::product()`

Task `tex-fixes`, on `.concerns/tex-fixes--lead--rulings.md`. This amends D32.3, which had each fix
keep the differential net "by regenerating the affected goldens from the reference patched in
memory". No golden and no oracle script was changed.

**Decided**

1. *The mechanism is a rules struct with two presets* (`src/tex_rules.h`), the shape `DrawRules`
   (`src/draw.h`) and `DocumentRules` (`src/blocks.h`) already have. `TexRules::reference()` is the
   prototype's behaviour exactly and is the default of everything that carries a `TexRules`, so
   every golden replay runs under it without saying so. `TexRules::product()` is what the public
   API lays out with: `ImTypesetContext::tex_rules` (`src/context.h`), read in `src/imtypeset.cpp`
   where the math fonts are handed out, where text is composed and where a paragraph is broken.
   The rules are carried by what already carried parameters: `LineBreakParams::rules`,
   `Composition::rules`, `MathFonts::rules`. Nine switches:

   | switch | section | register entry |
   |---|---|---|
   | `skips_made_shrink_finite` | §827, §868 | B5 |
   | `left_skip_omitted_when_zero` | §887, §1229 | B6 |
   | `post_break_text_stops_pruning` | §877, §884 | B7 |
   | `easy_line` | §835, §845, §848, §850, §854 | B3, B3a, B4 |
   | `sentence_space_truncates` | §1044, §107 | Y1 |
   | `italic_kern_only_without_subscript` | §755, §759 | M9 |
   | `underline_clearance` | §735 | M10 |
   | `sublist_nucleus_packed` | §754, §720, §721 | M13 |
   | `mu_truncates` | §703, §106 | M14 |

2. *`easy_line` is ON in `product()`*, on the evidence the ruling asked for (below). D32.3 listed
   "the line breaker's class-per-line-count formulation" among the deliberate choices that stay;
   it stays as `TexRules::reference()` and is no longer what the product does. The work budget
   (D33) is kept unchanged as the bound on a list built to be expensive.
3. *M11 is not changed.* A fraction spaces as Inner under both presets. It agrees with *The
   TeXbook* (Appendix G, rule 15e) and, by the recollection of the builder of `tex-web-pass` only,
   with `tex.web` as corrected in 2014; it differs from the 2008 copy in
   `../imgui-adv-text/docs/tex.web` (§761 l.14996). Nobody has read a current `tex.web` to settle
   it; that is the user's to do or to decide.
4. *The golden replays through the public API run under `reference()`.*
   `tests/test_api.cpp` (`text_to_commands_to_vertices_equals_the_reference_chain`) and
   `tests/test_api_math.cpp` (`formulas_in_running_text_give_the_reference_material_lines_and_commands`)
   set `ctx->tex_rules = TexRules::reference()` first. Without that line they fail in 32 and 70
   checks: that is the product differing from the prototype, which is the point.
5. *Section 721 is applied to a box used as it stands.* Under `sublist_nucleus_packed`, `clean_box`
   of a field that is already one unshifted box (a `SUB_BOX`, or a list that converted to one box)
   drops the italic-correction kern after a lone character, as the section does for any result.
   The reference leaves a `SUB_BOX` alone. No dimension changes. Not in the register before this
   task (M13a below).
6. *A kern or a rule of a formula is held to ±`MAX_DIMEN` like a box* (`Converter::push`,
   `src/math_mlist.cpp`): `MathError::TOO_LARGE` with `MathStatus::inside` set, reason "the formula
   is too large to set: a space or a rule inside it is larger than the largest dimension there
   is". Under both presets; the port's own (D33). `harden-2` left this open with no failing input;
   two were found (`docs/hardening.md`, row K1).
7. *`Paragraph::artificial_breaks`* is new: how many times the pass that produced the lines took
   §854's free break. It is what the measurements below classify by, and a host can read it as
   "some line of this paragraph could not be made to fit".

**Measured** (`tests/test_tex_rules.cpp`; every figure is asserted there)

How much ordinary text moves, over every paragraph the goldens hold, each broken under
`reference()` and under one switch at a time:

| corpus | paragraphs (by total fit) | B5 | B6 | B7 | Y1 | `easy_line` |
|---|---|---|---|---|---|---|
| `tests/golden/core` (500 breakings, 2,864 line boxes) | 500 (336) | 4 break sets, 22 line boxes | 0 break sets, 2 line boxes | 0 break sets, 3 line boxes | n/a (the lists are already made) | 11 break sets, 146 line boxes |
| `tests/golden/para`, as the reference composes it (4,902 line boxes) | 710 (250) | 0, 0 | 0, 0 | 0, 0 | 0, 0 | 17 break sets, 669 line boxes |
| the same, ragged blocks by total fit (`ImTypesetLineBreak_TotalFit`; 3,712 line boxes) | 460 (460) | 0, 0 | 0, 0 | 0, 0 | 0, 0 | 57 break sets, 2,085 line boxes |
| the same, every case set `Spacing::NONFRENCH` | 710 (250) | | | | 0 break sets, 55 of 4,903 line boxes (21 of 4,171 glues, one scaled point each) | |
| `tests/golden/blocks` (1,694 line boxes) | 705 (465) | 0, 0 | 0, 0 | 0, 0 | 0, 0 | 14 break sets, 120 line boxes |

- B5, B6 and B7 move only the core cases that were built with the input each is about (a skip
  with `fil` shrink; a zero left skip of infinite order; a discretionary with post-break text
  before glue). Nothing the library composes itself reaches them: `skips()` writes stretch only
  and no discretionary is ever made.
- Y1 moves nothing under the default spacing (`EXTRA_SPACE_ONLY`, which does not scale stretch or
  shrink); two para cases are NONFRENCH and neither changes. With every case forced to NONFRENCH,
  21 glues change by one scaled point and no break moves.
- `easy_line`: every paragraph whose breaks change is one where §854's free break was taken in at
  least one of the two breakings (17 of 17 para, 57 of 57 ragged, 14 of 14 blocks, 4 of 11 core),
  or one of the seven core cases built with `line_penalty 0` so that every breaking ties. In no
  total-fit paragraph of any corpus do the total demerits differ when no free break was taken
  (asserted: 0). Where the free break was taken the product has fewer lines in every case (17, 57,
  14, 4) and never more; it costs more demerits than the reference's breaking in 9 of the 57
  ragged ones and in none of the others (the reference's free break is free: it charges nothing
  for the line it forces, and it forces from the chain with the most lines).
- The 17 + 14 justified ones are all at the goldens' two narrowest columns, 40 px and 60 px (20,
  30 or 45 pt: 15 + 2 of the para ones at 30 and 45 pt, 12 + 2 of the blocks ones at 30 and
  20 pt), where a word does not fit its line. Of the 57 ragged ones 42 are at 30 pt, 3 at 45 pt
  and 12 at 240 px, the cases built around a word that cannot be broken. One of the justified
  ones has no overfull line at all (`hard-breaks-after-space-justify` at 45 pt: 7 lines at 152,542 demerits
  under the reference, 5 at 10,661 under the product): §854 is taken for any forced break
  (§851), and the reference, whose `minimum_demerits` starts afresh for every line count, gives
  the free break to the last line count at a hard break even when a shorter chain was feasible.

Formulas (`tests/golden/math/formulas.golden`, 1,950 sources set at their own sizes and styles):

| switch | boxes whose width changes | height or depth |
|---|---|---|
| M9 | 22 (by at most 58,040 sp) | 0 |
| M10 | 0 | 14 (by at most 31,575 sp) |
| M13 | 0 | 0 |
| M14 | 181 (by at most 40 sp) | 0 |
| all of `product()` | 194 | 14 |

No formula is set under one preset and refused under the other.

The cost (`bench_reflow --only growth`, release, one run, load average 3 to 5 on the machine;
the sample's last paragraph concatenated, 420 pt; the work budget raised to its maximum for both
columns so that neither is cut short):

| | x1 (8 lines) | x4 (30 to 32) | x16 (120 to 128) | x64 (480 to 512) |
|---|---|---|---|---|
| justified, a class per line count | 0.092 ms | 0.90 ms | 9.9 ms | 133 ms |
| justified, `easy_line` | 0.089 ms | 0.53 ms | 2.3 ms | 10.0 ms |
| ragged right by total fit, a class per line count | 5.8 ms | 105 ms | 1.73 s | 21.5 s, and then over even the maximum budget (268,442,463 units): first fit |
| ragged right by total fit, `easy_line` | 0.143 ms | 0.63 ms | 2.58 ms | 10.9 ms |
| work units at x64, `easy_line` | | | | 97,218 justified, 120,746 ragged (the default budget is 1,048,576) |

With the default budget and `product()`, which is what a host gets, none of the sixteen rows of
the table falls back to first fit; before this task justified x64 and ragged x4, x16 and x64 did
(`docs/performance.md`, "After the API revision").

**Read, not measured**

- Each product rule against the section it cites, by hand, for the cases in
  `tests/test_tex_rules.cpp`; the working is in the comment above each test. **No TeX engine is
  installed on this machine** (`which tex pdftex luatex etex`: none), so nothing was compared with
  TeX's output and agreement with TeX is claimed for those hand-worked cases only.
- `tex.web` here is version 3.1415926 (2008).

**Consequences a host can see**

- A formula with a subscript on `f`, `F`, `N`, `U`, `J`, `I`, `C`, `S`, `X`, `Γ`, `Π`, `Σ` and
  the other letters with an italic correction is narrower by that correction and its subscript
  sits under the letter's overhang (pictures: `docs/images/texfix-f_i.png`, `texfix-f_i-sup.png`,
  `texfix-letters.png`; each shows the reference's rules above and the product's below).
- Under `product()` a group is a box (§754), so a formula's node tree is one level deeper per
  group, `\frac` included (the parser makes it a group holding the fraction). Positions are
  unchanged.
- With `line_penalty 0` and a `fil` right skip every breaking ties, and `easy_line` then sets one
  word to a line where the reference set as few lines as it could
  (`easy_line_of_two_breakings_with_equal_demerits_the_later_node_wins`). That is `tex.web`'s
  order (§855's `<=`) and is not reachable through the public API, which has no way to set
  `line_penalty`.

**Not done**

- §826's error for infinite shrink is not reported, under either preset.
- The twelve undeclared departures with no visible effect stay as they are: B5a, B8, B14, P3, S3,
  U3, M12, M15, M16, M17, Y5, Y8. So do Y4's `str.isupper()` and Y10 (U+00A0 is a breakpoint).
- `DocumentComposer` and `Document` (`src/blocks.h`) carry the rules in `params.rules` and
  default to the reference; nothing in the public API uses them yet. Whoever wires the document
  layer in sets `composer.params.rules = ctx.tex_rules` and the blocks' `Composition::rules`.

## D42. The demo window is part of the library, and adds no font

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

**Measured** (2026-10-10, FreeType 2.12.1, 32-bit ImWchar, debug; the `demo` task's numbers):
`test_demo` 8 tests / 84 checks, 2.7 s, 109 MB peak resident including 400 frames of scrolling
through 6,000 paragraphs; the widget cache held about 33 KiB per laid-out paragraph of some 300
bytes. Inside an imgui_bundle wheel the demo TU does not link (the fork's `InputText*` take a
`std::function`): the bundle library compiles it with `IMTYPESET_DISABLE_DEMO`.

## D43. The Document: styled blocks in; layout, drawing and selection out (task `api-document`)

D34 names the host-neutral seam; `docs/advtext-migration.md` section 3 (R1 to R11) is what it was
held to. This is what was built and why. `.concerns/api-document--builder--result.md` has the
requirement-by-requirement account and what was run.

**Decided.**

- **One opaque `ImTypesetDocument`, described by plain arrays.** An edit hands over ONE
  `ImTypesetDocumentContent`: a text buffer, blocks that name byte ranges of it, runs, a table of
  `ImTypesetBlockStyle` the blocks index, and optional containers. Nothing is kept by pointer.
  Reason: a binding makes one call per edit whatever the document's length, and a C++ host with
  its own parser fills the same structs. Nothing is implied by a block's kind: Paragraph, Heading
  and ListItem are all text, and how a heading looks is its style.
- **Lengths are float pixels in the header, scaled points inside**, as everywhere in the public
  API. A test reaches the reference's exact scaled points through `DocumentHook`
  (`src/imtypeset_document.h`), which is not public: it selects `DocumentRules` (reference) and
  replaces each resolved length after checking the pixel description came within 2 sp of it.
  Measured on the block goldens: all 2,398 lengths came to the reference's scaled point exactly.
- **Containers are optional and by index**, 1-based so that zero is "none": `Quote` (bar position,
  colour) and `List` (gutter, tight). A block that names none can say only how deeply it is quoted,
  and then the block layer's depth-only behaviour applies (D37). Styles and containers handed to
  `ReplaceDocumentBlocks` are APPENDED, never replaced, so an edit never re-describes a block it
  does not name; entries no block names stay until `SetDocumentBlocks`. Not measured: how many
  dead entries a long stream leaves.
- **Material is made late.** A block's text becomes nodes when the block layer first lays the
  block out (`DocumentComposer::materialize`), by the function that builds a paragraph's
  (`build_material_now`), so setting N blocks costs copying their text. What `prepare` needs
  before that is on the description: the marker (built at once, it is a few glyphs), and
  `Block::length_hint` — bytes for a text's height estimate, and for code the EXACT line count,
  because whether a code block sets a box decides the gaps round it.
- **Edits.** `AppendDocumentText` and `SetDocumentBlockExtent` go through `Document::edit_text`,
  which marks one block and one unit stale and leaves the units standing: no pass over the
  document. `ReplaceDocumentBlocks` rotates the block layer's three parallel arrays
  (`Document::splice`) and costs one `prepare` over every block: the widest marker of a list
  decides the gutter of all its items, so a structural edit cannot be local without a second data
  structure. It is counted (`ImTypesetDocumentStats::StructureVisits`) and not hidden.
- **Positions are `(block, byte offset into the text the host gave for that block)`.** For code
  the text is the raw source; the block ends at its last set character (trailing blank lines are
  dropped, as the reference drops them).
- **A block as a paragraph.** Lines, the index-to-byte map and the selection model are a
  paragraph's, computed by the paragraph's own functions on a view filled from the block's layout.
  Views are a small reused set (the blocks the mouse, a selection or a mark touch), not one a
  block. This is why `src/imtypeset.cpp`'s helpers moved from an anonymous namespace to
  `ImTypeset::detail::core`.
- **Product rules, each tested in `tests/test_api_document.cpp`:** a point between two blocks
  belongs to the lower one, and says it is not in a block; a marker is never selectable and never
  copied; a copy joins the touched blocks with one separator the caller names (default a blank
  line), gives code as its source bytes and a host block's text whole; word and line selection
  across blocks is the union of the units under each end; marks are drawn in the order given, a
  later ink over an earlier; an underline has its own thickness; a table is refused (set as an
  empty host block) with the fields reserved.
- **Drawing is one `draw_runs` call per visible block**, each at its own origin computed in
  double, so a document two million pixels tall keeps sub-pixel placement where a single flattened
  command list would not in `float`.
- **`formulas` given by the host**: `ImTypesetRunFlags_Formula` and `_DisplayFormula` mark a run
  as one formula's source, without delimiters; dollar scanning (`ImTypesetBlockFlags_Math`) stays
  available. A marked run and a found formula that overlap: the marked one stands.
- **`ImTypesetError_DescriptionCut` (12), a "done, differently" code**, for a description that
  was wrong in part and was cut to what is right: an offset outside the text, a style, a container
  or runs that are not there, a table. Reason: the Python policy raises on "nothing done" codes,
  and with InvalidArgument a pane would have raised on every edit that held a table. An edit at a
  place that is not the document's is still InvalidArgument and does nothing.
- **Python sees `(block, index into that block's str)`**, converted per block (an ASCII block for
  free), never across the document.
- **The two `Text*Offset` functions take `(text, offset, text_end = NULL)`** in C++ (a binding
  generator cannot drop a non-defaulted `text_end` from the middle). The C ABI keeps
  `(begin, end, offset)`: D40.

**Measured** (GCC 12.2, debug, FreeType 2.12.1; `tests/test_api_document.cpp`): 67 of the 69 block
goldens (211 compositions, 34,647 commands) equal the reference through the public calls; the two
others have a negative space above, which the header does not take. Four windows on a document
cost 34 block layouts and 25 text builds at 500 blocks and the same at 10,000. Twenty-five
appends to the last block cost 25 text builds, 25 block layouts and no structure visit, at 200
blocks and at 10,000. A steady frame with a selection and three marks allocates nothing.

**Measured in Python, inside both stock wheels** (`python/spike_bundle/pptmstr_document_pane.py`
and `advtext_document_view.py`; medians of 3 runs of 240 frames, Xvfb + llvmpipe, load 2 to 7;
the tables are in the result file): pptmstr's real blocks, 2,250 of them, cost 8.1 ms a frame in
`rich_pane.draw()` (which windows itself to 400 lines), 14.3 ms as one paragraph a block (6,750
binding calls) and 0.21 ms as one Document (4 calls); with the tail streaming, 0.30 ms and 5 calls.
advtext, 2,100 paragraphs: 7.3 ms in `StreamView`, 9.8 ms per paragraph, 0.10 ms as a Document.
Offset conversion at the far end of a non-ASCII document: 2.9 us a hit test at 1,000 blocks and at
10,000.

**Not measured.** Any time in C++ but one: appending one block to a 10,000-block document and
drawing the tail took 610 us in a release build on a loaded machine, nearly all of it the pass
over every block (`prepare`). Every other C++ claim above is a counter. Memory per block. The scroll bar's
behaviour on real text (heights are estimates until seen). A document edited from several places
at once. `DocumentView` under a real mouse.

**Known gaps.** Tables (fields reserved). A clipped code line is clipped when drawn into a draw
list and not when drawn into buffers. Find is the host's (it has the text); the product rule the
lead asked for, case folding with a map back to source bytes, is not built. Scroll anchoring when
a block above the window takes its true height is the host's (`GetDocumentBlockGeometry` before
and after). R8 to R11 of the migration map are not part of this: formulas on their own, font
numbers, columns.

## D44. Distribution: four routes, an ImGui floor decided by building, and floating point a host cannot switch off by accident (task `dist`)

The routes, their commands and what ran are `docs/distribution.md`. The decisions in it:

**Decided: the order.** The Python sidecar inside a stock Dear ImGui Bundle wheel comes first (the
user's need, 2026-10-10), then a C++ host adding the library as ImGui extensions are added
(drop-in files, `add_subdirectory`, an installed package), then the rest. Upstream adoption by the
bundle is backlog.

**Decided: the drop-in files are generated, and are the sidecar's sources.** `tools/amalgamate.py`
writes `imtypeset.h`, `imtypeset.cpp` (every source but two, with the private headers, as one
translation unit), `imtypeset_demo.cpp`, `imtypeset_c.h` and `imtypeset_c.cpp` into `dist/`
(git-ignored, `--check`). `src/` stays the source of truth and keeps the reference's file names
(D5); what a host holds has ImGui's shape. `python/host/build_bundle_library.sh` compiles those
files, so the route the user depends on exercises the artefact a C++ host is given. **Why not ship
`src/`**: 42 files with names like `units.h` and `nodes.cpp` in a host's source directory.

**Decided: sources may not define one name in two anonymous namespaces**, since they are compiled
as one unit (the generated file, and a host's `CMAKE_UNITY_BUILD`). Ten renames made that true
(`.concerns/dist--builder--result.md` lists them); the generator renames nothing. Three names are
still defined in more than one file as overloads on unrelated parameter types (`note` in blocks,
pack and paragraph; `classify` in linebreak and selection; `can_draw`, `owned`, `px_of` in the two
API files, by design) and stay: the `amalgamation` preset runs the whole suite against the one-unit
build, which is what would notice a call resolving differently.

**Decided: the build system takes the ImGui versions from the header.** `IMTYPESET_VERSION`,
`IMTYPESET_IMGUI_VERSION_NUM_MIN` (19200) and `IMTYPESET_IMGUI_VERSION_NUM` (19290) are read out of
`include/imtypeset.h` by `CMakeLists.txt`: below the floor is an error, above the tested version a
warning, and `.deps/imgui` must still be exactly the tested tag (a stale copy would fail the draw
goldens for no visible reason). This replaces D6's exact pin.

**Measured: the floor.** The whole tree, debug, FreeType 2.12.1, against Dear ImGui release tags
taken from a clone's git objects: v1.91.9 does not compile (22 errors); v1.92.0 (19200) and v1.92.8
(19280, the ImGui of imgui_bundle 1.92.801) pass 41 of 41 with the same glyph-box census as
v1.92.9; v1.92.1, .2, .4, .6 compile without a warning and were not run. 1.93.0 WIP (19298): preset
`imgui-next`. So the header's 19200 stands. The tests needed three version guards for names newer
than the floor (`tests/support/imgui_compat.h`); the library needed none.

**Decided: the contract with a host's ImGui target.** `IMTYPESET_IMGUI_TARGET`, else the package
when `IMTYPESET_USE_IMGUI_PACKAGE`, else `imgui::imgui`, else `imgui`, else one built here only
when `IMTYPESET_BUILD_IMGUI` is on (the default only at top level). Otherwise configuring fails. **Why
fail**: before this, a host whose target was `imgui::imgui` silently got a second ImGui built from
`.deps` and linked beside its own.

**Decided: install only against an installed ImGui; never install ImGui.** The package's dependency
is `find_dependency(imgui CONFIG)`. With an ImGui built in the tree `IMTYPESET_INSTALL=ON` is an
error. hello_imgui installs the ImGui it builds because it owns an application's choice of ImGui; a
library that did would put a second `imgui.h` in the host's prefix.

**Measured: floating point in a host's build** (the limit D16 left open). GCC 12.2,
`-O3 -march=native`, FMA available, the generated file as the library: with the compiler's default
contraction and no pragma, 65 FMA instructions and 3 of 41 tests fail (`test_opentype`,
`test_fontlib`, `test_opentype_wild`: ink boxes one ulp off; no layout, draw or API golden moved in
that run). With `#pragma GCC optimize("fp-contract=off")` at the top of the file, 0 FMA instructions
and 41 of 41 (preset `fp-host`, the one configuration without `-ffp-contract=off` on the library's
command line). GCC ignores `#pragma STDC FP_CONTRACT OFF`.

**Decided.** The generated file carries the pragma for GCC, Clang and MSVC, and `src/units.h`
refuses fast-math with an `#error` (`IMTYPESET_ALLOW_FAST_MATH` overrides), which covers the CMake
build of a host with `-ffast-math` in its global flags as well. **Not measured**: the Clang and
MSVC pragmas (neither compiler is here); `-funsafe-math-optimizations` alone, which defines no
macro to test; whether a one-ulp ink box can change a layout at some size.

**Decided: presets.** `debug`, `release`, `asan`, `stb`, `wchar16` as before (`asan` now
instruments the C test too); `amalgamation`; `fp-host`; `exceptions` (ASan, exceptions on,
`tests/imconfig_throwing.h`: the configuration in which `test_exception_safety` does its work);
`bundle-config` (the bundle's own ImGui configuration header, read from `../imgui_bundle`);
`imgui-next` (`../imgui`); `python-host`. CMake presets cannot test for a directory, so the three
that need a sibling checkout fail at configure where it is absent.

**Not done.** vcpkg: a manifest and a port sketch exist and have never been run. Clang, MSVC,
Windows, macOS: nothing was compiled. hello_imgui as a CMake host: not run.
