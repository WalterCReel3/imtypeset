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

*Amended by D27:* no custom `ImFontLoader` was built; the atlas gets its glyphs
through an in-memory cmap.

*Amended by D32, D34, D37 and D43 (2026-10-10):* the "not ported" line no longer
holds for block layout. imtypeset is the product and not a port of a part: the
block layer (`src/blocks.*`) and a public Document (`include/imtypeset.h`,
"Documents") are native. Still not here: markdown, diff, tables and columns
(the table fields of a block are reserved, D43), and the views.

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
3. **Where the prototype has a defect, the product does not inherit it.**
   (*Amended by D41:* no golden was regenerated; each fix is a switch, off under
   `TexRules::reference()` and on under `TexRules::product()`, and the
   class-per-line-count formulation listed below as staying is one of them,
   `easy_line`.) This
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
`src/selection` and `src/draw` (H1 to H8, and the draw review's items D3, D4, D6, D7, which are
review item numbers here and not decisions) and five of the lead's (X1 to X5). The
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
   what makes them take the white texel of the list's own shared data (review item D3). `_CmdHeader.ClipRect`
   stays because the public `GetClipRectMin()` reads `_ClipRectStack.back()`, which is undefined on
   a list whose clip stack is empty, and `AddText` itself reads `_CmdHeader`.
9. *Batched reservation stays (review item D4).* Past 64k vertices with 16-bit indices a draw
   command ends up to 1,023 quads earlier than with a reservation per glyph (ImGui starts a new
   command when a reservation would reach 65,536 vertices; a reservation here is of at most
   `BATCH_QUADS` = 1,024 quads, so the earliest it can do so is at 65,536 - 4 x 1,024 vertices,
   where a quad at a time goes on to 65,532; the test allows 1,024 and saw 94). Same vertices,
   same triangles (tested); a different place where one `ImDrawCmd` stops. Reproducing the reference's boundary
   would need the list's vertex counter before every quad and ImGui's own order of reserve and
   cull, for something no renderer shows.
10. *`DrawRules`: `reference()` and `product()` for the draw loop* (`src/draw.h`), the same pattern
    as the layout rules. One rule so far, `rule_joins_span` (review item D7): under `product()`, which the
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

    *Superseded by D47:* `rule_joins_span` (and `RULE_JOIN_LOOKBACK`, `Drawer::settle_span`) no
    longer exist. A review made by reading (`.concerns/review-3a--reviewer--findings.md`) found
    the placement wrong where a formula's rows overlap; all of it reproduced, and the rule was
    replaced by `formula_placed_whole`. `DrawRules` itself, with `reference()` and `product()`,
    stands.
11. *The library is safe to unwind through, without catching anything.* A host's `IM_ASSERT` may
    throw (imgui_bundle's Python build), and with exceptions enabled so may `operator new`. The
    guarantee is the basic one at every public entry: nothing leaks, the draw list holds whole
    quads, the widget cache counts what it holds, and the same call made again does what it would
    have done. Held by ordering and by destructors (`Building` in `src/imtypeset.cpp`,
    `std::unique_ptr` until ownership has passed, the pool's room reserved when the context is
    made). There is no `try` or `catch` in `src/` outside
    `src/imtypeset_c.cpp` (D40) and the macro `IMTYPESET_NO_UNWIND` of `src/context.h`, which under
    `__cpp_exceptions` swallows what one call throws where the call is itself made while
    cleaning up (`PopClipRect`, `EndChild`, `PopTextWrapPos`, `ImFontAtlas::RemoveFont`: four
    uses, added with the Document, D45). In the draw loop nothing fallible happens
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
- Review item D7 before the rule, 576 draws (6 sizes, 0 to 60 terms before `\sqrt{x}`, 16 sub-pixel origins):
  the bar's left edge was from 3 px inside the sign's quad to 3 px clear of it, its top from 1 px
  above the sign's to 1 px below. After: the same offset across in all 576, within a pixel down.
- Review item D7 on fractions, 144 draws under each set of rules (four fractions, one of them the quadratic
  formula; 13, 17 and 24 px; 0, 13 and 40 terms before; four origins): against the glyph at that
  end as it was drawn, a fraction bar's end is up to 6.00 px from where the commands put it under
  `reference()` and at most 1.00 px under `product()`; a radical's bar inside the numerator 4.91 px
  and 0.42 px from its sign.
- Pictures under both rules were rendered with the software renderer and looked at
  (`IMTYPESET_HARDEN_SHOTS`): under the reference's, a radical sign plainly apart from its bar at
  13 px after sixty terms and by a pixel at 24 px after thirteen, and after forty terms the
  quadratic formula's numerator 5 px right of its bar with the sign printed over the `b`; under the
  product's, the same fraction whether nothing or forty terms come before it.
- Review item D6 by a run: an atlas capped at 512 x 1024, 120 frames each at another size out of 27: 224 bakes
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
- **No exception crosses.** `src/imtypeset_c.cpp` holds the library's one `try` with handlers that
  report, under `#if defined(__cpp_exceptions)`: each entry catches, fails as its return type
  fails (0, -1 or null), and `ImTypeset_GetLastError()` answers `ImTypeset_Error_Exception` (1000,
  an enumerator only the C header has) with the exception's message. (The only other `try` in
  `src/` is the macro `IMTYPESET_NO_UNWIND`, D39 item 11.) Built without
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

*Superseded by D49:* the demo keeps its state per context, found by `GetContextSerial()` (D46).

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
  (WITHDRAWN by D45: the hook is gone, nothing is replaced and the comparison is `==`.)
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
  place that is not the document's is still InvalidArgument and does nothing. (D45 extends the
  code to runs out of order, for a paragraph too, and says which call reports what.)
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

## D45. The Document after its first review: what a call answers for, no tolerance in the replays, one walk over code (task `doc-fixes`)

`.concerns/review-4-document--reviewer--findings.md` read the Document as D43 left it and ran
nothing. Each finding was first made to fail in a test; `.concerns/doc-fixes--builder--result.md`
says which did, item by item. What follows is what was DECIDED on the way, with the reason.

**Decided.**

- **A call that was applied never answers with a "nothing done" code, and answers for itself
  alone.** A run that is out of order (it does not reach past the end of the run before it) is
  left out and the rest is set; that was reported as `ImTypesetError_InvalidArgument`, which the
  header files under "nothing done" and the Python policy raises on, and for a document it was
  reported late, by every later `AppendDocumentText`, for good. It is now
  `ImTypesetError_DescriptionCut`, from the one place that validates runs (`validate_runs`), so
  for `SetParagraphText` too: four expectations in `tests/test_api.cpp` moved with it.
  `SetDocumentBlocks`/`ReplaceDocumentBlocks` say it when the blocks are handed over, naming the
  block (and `ImTypesetError_TextTooLong` likewise). `AppendDocumentText` reports what THAT call
  cut or left out and nothing else; what an earlier call found stays readable as
  `GetDocumentBlockError`, and a draw still answers false for a block in view that has it.
  Alternative considered: keep reporting the block's standing error from every append. Rejected:
  a host cannot tell "this append was cut" from "something was once wrong here".
- **Everything `describe` cuts is said**: a container's `Parent` that is not an earlier
  container, a container `Kind` that is not one, a marker past 256 bytes (cut before the
  character that crosses the limit, not inside it). All `DescriptionCut`.
- **A refused edit leaves the document as it was.** `SetDocumentBlocks` checks the one thing that
  can refuse (more than 4,194,304 blocks) before it gives anything up.
- **The selection `DocumentView` keeps is positions, and an edit moves it.** After
  `ReplaceDocumentBlocks`: below the edit nothing moves; above it a block moves by the
  difference; a block replaced one for one keeps its number and what was selected in it (offsets
  are cut to the new text when used), which is what a host that re-describes a streaming block
  wants; where the number of blocks changes, the part of the selection in the replaced blocks is
  given up (a start moves to after them, an end to before). A selection the host keeps
  (`selection` not null) is the host's to move. Alternative considered: clear the selection on any
  edit that touches it. Rejected for the one-for-one case only, because a pane that replaces its
  last block every frame would never hold a selection in it.
- **No tolerance in the golden replays, and no hook in the library.** The two replays compared a
  resolved length with the golden's "within 2 sp" and then SUBSTITUTED the golden's, and
  overwrote the interline glue, the strut, the first-line rule and the marker gap wholesale.
  Measured after removing every substitution: all of it is expressible through the public
  description as float pixels and equal by `==` (8,967 comparisons over the block goldens, 2,146
  over the doc goldens, the quote theme through `ImTypesetDocumentOptions` included), and the
  same 67 of 69 block cases and all three doc replays pass on the commands. So `DocumentHook`,
  `set_document_hook` and `ImTypesetParagraph::run_fonts` are deleted. What a replay still
  needs that the header does not offer is the reference's RULES, and that is one `bool`,
  `ImTypesetDocument::reference_rules` (the reference's `DocumentRules` and its first line
  narrowed by the indent), of the same standing as `ImTypesetContext::tex_rules`: internal
  state a test sets, read by no public call. It cannot leave the library without a second build
  of the sources, because the choice is made where the rules are applied. A run's font is not
  read back by the replays: it is in every glyph of the commands they compare.
  `tests/document_reference.h` is the one place both replays do this.
- **One walk over a code block's characters.** `map_code` (the document's map from nodes to
  source bytes) walked UTF-8 by lead byte while `set_code` decodes with `decode_utf8`; on
  ill-formed input the two counted different characters and every position of the block became
  byte 0. `src/blocks.cpp` `walk_code` is now the only walk: `set_code` returns the byte of each
  node it makes (`node_bytes`) and `code_extent` counts lines and finds the end of what is set,
  both through it. There is no "the two walks disagree" branch left to reach.
- **Views are bounded at 256 and given back.** The cache of block views grew with the number of
  blocks the last draw showed (2 per block), so a clip rectangle over ten thousand selected
  blocks kept twenty thousand. It is now between 32 and 256 whatever is shown, and trimmed to
  the limit after each draw. The price, accepted: a window that really shows more than 256
  blocks with a selection or marks in them rebuilds the excess views every frame. 256 and not
  the 64 an earlier test names because a screen of one-line blocks at a small size passes 64.
- **Blocks of no height are not special-cased.** A run of them with no space between stands at
  one height; a window that starts just below it lays them all out once (5,000 host blocks of
  Extent 0: 5,012 layouts in that frame, 0 in the next, 12 blocks drawn a frame), and a window
  that shows that height visits each every frame. That is linear in what the host handed over
  and the header now says so (BLOCKS OF NO HEIGHT). Not changed: a second index over units of no
  height would be a data structure for a document nobody has shown to exist.
  `GetDocumentStats` stays one step a block for `BlocksDeferred`, as the header already said.
- **Unwinding through a document** (a host whose `IM_ASSERT` throws, or `std::bad_alloc`):
  `DocumentView` ends its child and a clipped code block pops its clip rectangle however they are
  left. A sweep that throws from every allocation of `DocumentView` and `DrawDocument`
  (`tests/test_exception_safety.cpp`) found four more places where a throw left the document
  wrong for good, all fixed at the cause: a code block flagged as built before it was; a block
  flagged as synced before its line index was built; a view leaked when listing it failed; and
  the document's arena, which unlike a paragraph's is never cleared, left with its two box
  tables out of step (`NodeArena::add_box` pushes to one and then the other), so the NEXT formula
  tripped a contract. A layout now rewinds the arena on every exit (`Rewound`) and an unwound
  material build rewinds to where it started.
- **The header moved, not the behaviour, for a code block's copy.** It gives the source bytes as
  handed in as far as the block's text goes, which ends at the last character that is set; the
  line end after it is not part of the block. The reference drops trailing blank lines the same
  way (`advtext/markdown.py`, `_code_block`; read, not run).
- **The C ABI keeps `(text, text_end, offset)` for the two `Text*Offset` functions** (D40, D43);
  `include/imtypeset_c.h` now says so at the declarations.

**Measured** (GCC 12.2, FreeType 2.12.1, private directories; every binary under a memory guard):
debug `test_api_document` 24 tests, `test_api_document_golden` 5, `test_api` 40, `test_c_api`
464 checks, `test_exception_safety` in an ASan + UBSan build with exceptions on: 9 tests, 842
allocations of `DocumentView` and 65 of `DrawDocument` unwound from, no leak. The result file
has the full table and what was not run.

**Read, not run.** The pointer formed past the text buffer for a `TextStart` beyond `TextSize`
(length 0, never dereferenced; now clamped). The clip-rectangle half of the unwinding finding:
no allocation falls between the push and the pop on the input swept, so the guard is by reading.

**Not done.** A paragraph run that only OVERLAPS the one before it is still cut silently to
where that one ends, as the header has always said; only a run of which nothing is left is
reported. The Python package and the pptmstr patch (task `py-fixes`).

## D46. `GetContextSerial`, and a font check that cannot be fooled by an atlas refilled with as many fonts (task `doc-fixes-now`)

Two additions the lead relayed after the Document review: one new public declaration, ruled by
the lead, and one defect reproduced by `py-fixes` inside both wheels.

**Decided.**

- **`ImU64 ImTypeset::GetContextSerial()`** (C: `ImTypeset_GetContextSerial`, Python:
  `get_context_serial()`): a number no other context of the process has had or will have, never
  zero; zero with no current context. Lead's ruling and reason: the demo keeps handles and does
  not own the context, and showed that it cannot tell a context from a later one at the same
  address with what the header offered (the review's use-after-free). A liveness query on a raw
  pointer is unsound once the address is reused, and a destructor callback is excluded from the
  header (no callbacks: bindings are generated from it). It is a process-wide counter read in
  `CreateContext`, so it needs no registry and costs nothing. It answers for the CURRENT
  context, like every context-level getter here. In C it is the one function that returns a
  64-bit number; `include/imtypeset_c.h` lists the case.
- **`validate_fonts` keys on what cannot stay the same when a font leaves the atlas.** It
  skipped its check while the frame count and the NUMBER of fonts in the atlas were what they
  were at the last check, and said so ("short of being removed and another added between two
  calls of one frame"). A host that empties the atlas between two frames and adds as many fonts
  is exactly that: the roles' `ImFont*` were freed, `GetFont` answered one, `RemoveFont` handed
  one to `ImFontAtlas::RemoveFont` (ImGui's assertion `removed`), and `AddFont…` could remove
  the HOST's font that now stood at the old address. The key is now the count AND the `FontId`
  of the atlas's last font (and the atlas itself). Read in the pinned source and in v1.92.0:
  ImGui assigns `FontId` from `ImFontAtlas::FontNextUniqueID`, which only the constructor sets,
  and appends each new font, so no font leaves the atlas, alone or replaced, without one of the
  two changing. Alternative proposed by `py-fixes` and not taken: reset the cache at the three
  entries that use a role's font. It fixes those three and leaves every draw to the same
  mistake; the cause is the key. No new ImGui field is read (`Fonts`, `ImFont::FontId` were
  read already), so the sidecar's layout probes need no new row.

**Measured.** `tests/test_api.cpp`
`an_atlas_refilled_with_as_many_fonts_between_two_frames_has_none_of_the_roles`: before the
change it ends in ImGui's assertion; after it, 24 refills, 3 of the host's new fonts at the
address a role's font had, 0 stale answers, 0 of the host's fonts lost. Inside the wheels
(`tools/bundle_route/font_lifetime.py`, the sidecar rebuilt from this tree): `same-count-cycle`
exits 0 with no application font removed in wheel 1.92.900 and in 1.92.801, where it exited 1.
`a_contexts_serial_is_its_own_and_is_never_used_again`: 64 contexts made and destroyed in turn
all came back at one address, with 64 distinct serials.

**Not measured.** A host that edits `ImFontAtlas::Fonts` by hand (reorders it) is outside what
the key sees; the per-frame check still catches it a frame late, as before.

## D47. A formula is drawn whole, from the left edge of its box (task `d7-fix`, 2026-10-10)

Supersedes D39 item 10. *Amended by D50* where it says "nearest pixel".

*Decided.* Under `DrawRules::product()` (the public API) the draw loop does not treat a formula as
a rigid span. The left edge of the formula's box (the ATOM command's x) is put on the nearest pixel,
and every glyph and both ends of every rule of the formula go to that pixel plus their own distance
from the edge in scaled points, converted and rounded once (`DrawRules::formula_placed_whole`,
`Drawer::place_commands` in `src/draw.cpp`). A rule's top and bottom are taken from the baseline
the formula's first glyph was put on, and a rule does not end the span. A glyph of the text that
follows a formula with no glue between is placed from the formula's last glyph, as before.
`DrawRules::reference()` is untouched and is what every golden replay runs under; no golden changed.

*Why.* The first product rule (`rule_joins_span`, D39 item 10) left the glyphs where the reference
puts them — each from the glyph shipped before it — and joined each end of a rule to the glyph
whose origin was nearest along the line. A formula's rows overlap along the line and are shipped
one after another, so "nearest" picked glyphs of another row (review-3a items 1a to 1c), a
denominator was placed from the numerator's last glyph and stood off the middle of its bar by half
the rounding the numerator had gathered (item 2), and past 64 glyphs the join gave out (item 3).
All four reproduce (below). The advance rule is for a word, where the only relation that shows is
between a glyph and its neighbour. In a formula the relations that show are between rows, and a
rounding carried along one row has no way to reach another. Placing every item from one edge
bounds every one of those relations at once, needs no knowledge of the box structure in the draw
loop, no second pass and no look-back, and makes a formula the same picture at every sub-pixel
origin.

*Measured* (`tests/test_harden.cpp`,
`d7_every_rule_of_a_formula_stands_where_it_should_against_the_ink_of_its_own_box`: 14 formulas,
7 sizes from 11 to 32 px, 0/13/40 terms before, 8 sub-pixel origins, 168 draws a formula, the
bundled math face, FreeType; each rule against hand-assigned glyphs of its own box). Worst
distance, in pixels, of a rule's end from where layout puts it against the glyph at that end as
drawn, and of the centred part from the middle of its bar:

| | left end | right end | centre |
|---|---|---|---|
| the reference's rules | 6.91 | 10.17 | 6.47 |
| `rule_joins_span` (measured before it was removed, 11 of the 14 formulas) | 1.42 | 1.79 | 4.82 |
| `formula_placed_whole` | 0.91 | 0.95 | 0.77 |

Under `rule_joins_span`, by review item: 1a `\frac{a+b+c+d+e+f+g+h+i+j}{\sqrt{x}}`, the radical's
bar 1.42 px from its sign and 1.19 px from the x (the reviewer's hand case, run: sign at 142 so
ending at 152, bar 156..164, x at 152 — a 4 px hole, exactly as read); 1b
`\frac{\sqrt{x}}{abcdefghijkl}`, the fraction's bar 1.00 px; 1c `\frac{abcdefghijkl}{\frac{x}{y}}`,
the inner bar 1.43 px; item 2, centre 2.09 px (1a's formula), 2.23 px (`\frac{1}{11111111111}`),
4.82 px (seventy letters over 2), 4.18 px (2 over seventy letters); item 3, left end 1.00 px on
seventy letters over 2 with this face (the worst case the reviewer gave needs every gap to round
the same way); an overline, right end 1.79 px (its rule is shipped before its glyphs and was
snapped alone).

By construction, and asserted for every glyph and rule end in those draws: each is within 0.5 px
of its distance from the formula's edge, so any two are within 1 px of their distance from each
other, and the offsets are the same at all eight origins.

*What it costs, measured.* (a) Evenness inside a formula. Of 121,408 pairs of glyphs shipped side
by side on one baseline in those draws, 20,392 (16.8 %) stand more than half a pixel from the
distance layout gives them, the furthest under 1 px; under the reference's rules none does. This
is `whole` of `../imgui-adv-text/docs/advance-rule.md` §5 (478 of 3,453 there), which the user
ranked below `advance` for prose. Looked at, enlarged four times, on `abc...xyz + \sin(2\pi x) +
1234567890` at 13, 16 and 19 px: at 13 px a wider gap shows between `i` and `j` and the digits'
gaps are not all alike; at 16 and 19 px I could not tell the two rows apart by spacing. (b) The
radical's joint. `rule_joins_span` held the bar's start against its sign to 0.42 px and the same at
every position; now it is within 1 px. Over 576 draws of `a+b+...+\sqrt{x}` (six sizes, 0 to 60
terms): the bar starts 2, 1 or 0 px inside the sign's quad where it was always 1; in 5 of 36
formulas it differs by a pixel from the formula with nothing before the sign; never a clear column
between them. At 11 px after 26 terms the join shows as one lighter pixel.

*Considered and not taken.* (1) Keep the advance rule in a formula and tell the draw loop which
box a rule belongs to, so a bar follows the glyphs it lies over and a narrower part is centred on
the bar as drawn. It keeps (a) and (b). It needs the box structure in the command stream: a
`DrawCmd` has no spare field that golden replays do not compare (`operator==` takes every field,
and unused ones are zero by contract), so it means a tagged ship under a switch set in
`src/imtypeset.cpp`, a device-space centring pass over glyphs already placed, and a formula whose
drawn width differs from its layout width by its widest row's rounding (which is what moves the
word space after an inline fraction today). (2) Restoring the reference's rule under `product()`:
6.9 / 10.2 / 6.5 px above. (3) Joining only a radical's bar to the glyph shipped before it: an
overline that follows a glyph is the same sequence of commands and would start a pixel off its
first letter in some positions (how often was not measured).

*Not measured.* Faces other than Latin Modern Math; stb_truetype's rasters by eye (the placement
tests run under it; the pictures were made under FreeType only); display formulas (`$$`), which
take the same path; sizes above 32 px.

## D48. Word spaces: under `DrawRules::product()` a word is held to its laid-out width and a word after a space is placed from the word before it (task `word-gaps`)

*(D50 amends the second switch below: a word after a formula is led too, and so is a formula's box.)*

**Seen.** In three screenshots of justified prose at 16 and 19 px some word spaces were nearly
closed beside normal ones ("nodesand,", "howbadthe line", "everyday output").

**Measured** (the three paragraphs of those screenshots and the 8 paragraphs of
`bench/data/sample.txt` longer than 200 bytes, each at 520 and at 337 px; justified; 13 to 20 px as an em and
again as a font size; two origins; 37,778 spaces between two words, 150,624 pairs of glyphs side
by side in a word; FreeType 2.12.1, and stb_truetype with the same pens):

- The cause is in the draw loop and it is the reference's rule working as designed
  (`../imgui-adv-text/docs/advance-rule.md` sections 1, 3 and 4). A drawn space is
  `set width + (rounding of the next word's anchor) - (rounding of this word's anchor) - (what
  this word gathered by rounding each of its gaps)`. The first two terms are at most half a pixel
  each. The third is not bounded: words up to **5.64 px** wider than they were laid out (em
  20 px), 2,894 of 42,176 words a pixel or more out.
- Under the reference's rules 530 spaces (1.4 %) are drawn at under 60 % of the width layout set,
  4,928 (13 %) more than a pixel from it, the worst 3.10 px narrower, and at font sizes 13 and
  17 px some are drawn at **less than no width** (-0.22 px of a set 1.92 px).
- One of them by hand, "nodes and," in the first line of the pane's paragraph at font size 16
  (em 13.65 px), glue natural 3.38 px, set 2.45 px: `n` belongs at 203.674 and is anchored at 204
  (+0.326); the gaps n-o 7.533, o-d 7.787, d-e 7.700, e-s 7.233 round to 8, 8, 8, 7 (+0.747
  together), so `s` is at 235 where it belongs at 233.927; `a` belongs at 243.421 and is anchored
  at 243 (-0.421). Drawn space: 2.45 - 0.421 - 0.326 - 0.747 = **0.96 px**, one pixel of ink to
  ink.
- Layout is not the cause of the closed spaces but it is the cause of tight lines: 1,774 of the
  37,778 spaces are set by the line breaker at under 75 % of their natural width, the narrowest
  at 1.83 px (font size 13). No draw rule widens those; see "Not decided here".

**Decided.** Two switches in `DrawRules` (`src/draw.h`), both on in `product()` and off in
`reference()`, which is byte for byte what it was (every golden replay is unchanged; no golden,
tolerance or skip was touched):

1. `word_width_held`. A word — the glyphs of a rigid span that are not a formula's — is placed by
   rounded gaps as before. When the span ends, if its last glyph is more than
   `WORD_WIDTH_BOUND_PX` (0.5) from where layout has it against its first, as many gaps as it
   takes whole pixels are rounded the other way: those that were rounded furthest in the
   direction of the error, the earliest of equals. The choice depends on the word and its size
   alone, so a word is one picture wherever it stands (asserted at eight sub-pixel origins).
2. `word_spaces_led`. A word that follows a word and nothing but set glue is placed from that
   word's last glyph by the distance layout gives them, rounded once, and held to the two pixels
   either side of its own place. A word that starts a line or follows a formula, a rule or an
   atom is on the pixel nearest its own place, as before.

**What is promised and tested:** every word within 0.5 px of its laid-out width; every space
between two words less than 1.5 px from its set width; every glyph on a whole pixel; a word's
first glyph less than 1 px and its last less than 1.5 px from where layout has it (under the
reference: 5.7 px), which is also how far a selection rectangle can stand from the ink.

**What it bought, same measurement:** spaces under 60 % of their width 530 -> **11**; more than a
pixel out 4,928 -> **308** (0.8 %); worst loss 3.10 -> 1.37 px; root mean square error 0.67 ->
0.36 px; none below 40 % (that one is set at 1.92 px by layout).

**What it costs, and it is a trade the user ranked the other way for a neighbouring rule.**
- Evenness inside words: 13,348 of 150,624 pairs of glyphs (8.9 %) are more than half a pixel
  from their laid-out distance, where the reference has none. They are not far past it: 3,632
  (2.4 %) are more than 0.6 px out, 118 (0.08 %) more than 0.75, the worst 0.85. `whole`, which
  the user ranked last for prose in advance-rule.md section 6, has 27,680 (18.4 %), 6,494 of
  them more than 0.75 px out. Looked at three times enlarged at font sizes 13, 16, 17 and 19: I
  could not pick out the changed gaps; that is one reader on a soft rasteriser, not a judgement.
- Where a word after a space stands depends, by a pixel, on the words before it on its line. Two
  lines whose layout starts a word at the same x after different words can start it a pixel
  apart. Nothing in the tree lines text up that way today (a list marker is its own box), but a
  host that builds columns out of words and fixed glue would see it.
- Time: about a tenth of the public draw (`docs/performance.md`, "After `word-gaps`").

**Weighed and not taken.**
- A looser hold. At 0.75 px: 4.4 % of pairs past half a pixel, 46 spaces under 60 %; at 1.0 px:
  2.0 % and 93. The hold at 0.5 with the nearest-tie choice costs fewer uneven pairs than `whole`
  for the same spaces, and the looser ones leave four to eight times as many collapsed spaces.
- Rounding the gap at which the drift first passes the bound ("greedy"): at 0.5 px it is exactly
  `whole`.
- Holding the last glyph to its true place instead of to the word's anchor: one more half pixel
  off every bound, and a word would be two pictures depending on where its anchor rounds.
- Leading alone, without the hold: 261 spaces under 60 %; the lead cannot follow a word that is
  three pixels too wide.
- Spreading a line's gathered error over all its spaces: letters stay even and spaces come out
  alike, but words move off their layout places by the running sum, away from selection
  rectangles, fills and underlines, which are drawn from layout. Not measured.

**Not decided here, and not draw's:** how far the line breaker may shrink a space on a screen. A
line shrunk by 27 % sets a 3.38 px space at 2.45 px, which is drawn as 2 or 3 px whatever the pen
rule; `docs/images/demo-paragraph.png` shows such a line under a line stretched to 7.9 px.

**Not measured:** faces other than the bundled Roboto; sizes outside 13 to 20 px; a pixel density
other than 96; ragged, centred or right-aligned text (the rule is the same; the counts are not
taken); runs that change face inside a word; anything on a GPU.

## D49. The demo window keeps one state per context, found by the context's serial (task `demo-guard`, 2026-10-10)

Supersedes the second **Decided** of D42.

**Found by review, then reproduced by running** (`.concerns/review-4-draw--reviewer--findings.md`,
F4a, F4b, F5; `.concerns/demo-guard--builder--result.md`). The demo told "another context" from
the context's address and a debug counter. (a) A context destroyed and another made at the same
address, while the window was collapsed, left the demo holding handles of the dead one: with
ASan's quarantine off, a SEGV in `LayoutParagraph` from `ShowDemoFaces`. (b) Two live contexts
each showing the demo took the handles for the other's every frame: 5 handles a context expected,
25 after 6 frames, 125 after 26, and 200 layouts in 20 frames in which nothing changed. (c) The
demo's controls were file statics, so `tests/test_demo.cpp` depended on the order of its tests:
one test alone measured 3,894 px of scroll in a fresh host and 9,140 px in a third.

**Decided.** Everything the demo remembers is a `DemoState` found by
`ImTypeset::GetContextSerial()` (D46) of the context the window is shown in. A handle is used
only in the context it was made in, and the demo never destroys one: it dies with its context.
There is no pointer compare, no debug counter and no frame counting, and no remembered file
static: each section's controls are a `DemoXxxState` with its defaults as member initialisers.
States are kept for the `DEMO_STATES_MAX` = 8 contexts the window was last shown in, least
recently used dropped first; a live context that comes back after eight others starts from the
defaults and makes its handles again beside the old ones, which stay until that context is
destroyed. Eight is the builder's number, not a measured one.

**Measured** (the builder's runs; `tests/test_demo.cpp`, 14 tests / 176 checks in release, each of
the 14 also passing alone; the 14 / 176 were seen again by the `docs` task in a clean release
build): two live contexts hold 5 handles each and lay
out nothing once settled; a context replaced twelve times with no frame between came back at the
old address 11 times of 12 in release and got handles of its own each time; 5 handles after
seven other contexts, 10 after eight. The nine pages `example_demo_headless` writes were
byte-identical before and after the change, built against the same library.

**Not measured, and said by the builder.** The replaced-between-two-frames test was written after
the first design was gone, so that it fails without the serial is by reasoning. The result file's
table of sanitised and debug runs was left with its cells unfilled; the builder's progress file
has them for the earlier 13-test state (ASan + UBSan with FreeType / 32-bit and with stb / 16-bit,
debug, and `IMTYPESET_DISABLE_DEMO`: all passing), not for the final sources.

## D50. Where the product's draw rules meet: one leader along a line, and one bound (task `draw-seams`, 2026-10-10)

**Found by review, then reproduced by running** (`.concerns/review-4-draw--reviewer--findings.md`,
F1 to F3). `formula_placed_whole` and `word_width_held` + `word_spaces_led` were each measured by
their author on text that never put a letter or a bracket hard against a formula. Where they met:

- Text glued to the END of a formula was placed from the glyph the formula shipped last, by one
  more rounded gap, with nothing holding it to its own place. A word up to 2 px from its place,
  and all of it out of the next space: by hand `$x^2$-th a`, pens 11, 17, 22, 27, 32, 39, the
  3 px space 1.65 px short. A bracket after a fraction was placed from the middle of the
  denominator and could stand a pixel inside the bar.
- A formula stood on the pixel nearest its own place whatever the text before it had done. A
  bracket led to 40 before a box put at 44: 4 px against 5.47, where the reference draws 5.
  Measured over prose with glued formulas (below): text to formula from -1.83 to +1.75 px, 159
  of 2,448 over a pixel (reference: 28); across a space -1.87 to +1.91, 601 of 6,822 over a pixel
  (reference: 380), a justified 2.45 px space drawn 0.48 px wide by hand.
- So "a space is within a pixel and a half of its laid-out width" held between two words and
  not beside a formula, and `src/draw.h` said "always".

**Decided.** One mechanism for everything along a line, under `DrawRules::word_spaces_led`
(`Drawer::place_commands`, the leader):

1. The *leader* is what the next thing on the line is placed from: the last glyph of the word
   before it, or, after a formula, the formula's **rightmost part** — whichever glyph (by its
   origin) or rule (by its right-hand end) stands furthest right in the layout. Set glue and the
   start of a formula keep the leader; a line, a rule that is no formula's and anything else end
   it.
2. What is placed from it: the first glyph of a word after a space, the first glyph of text
   glued to the end of a formula, and **the left edge of a formula's box**. Each goes to the
   leader's pixel plus the laid-out distance between them rounded once, and is then held to the
   two pixels either side of its own place.
3. Everything inside a formula stays where `formula_placed_whole` puts it: at its own rounded
   distance from the box's edge.

**The bound** (stated in `src/draw.h` at `word_spaces_led`, asserted in
`tests/test_word_gaps.cpp`). Take a word at its glyph nearest the neighbour and a formula at the
left edge of its box. Two neighbours on a line — two words across a space; text and a formula
after it, with a space or with none; a formula and text after it likewise; two formulas — are
drawn **less than a pixel and a half** from their laid-out distance apart. Why: whatever is led
stands less than a pixel from its own place; a word's last glyph less than a pixel and a half
(half a pixel from its laid-out distance to the first, by `word_width_held`); and the hold to its
own place only moves a thing towards that place, so by less than its leader stands from the
leader's.

What the bound does not say, and the reader should know:

- A glyph or rule end inside a formula is up to half a pixel from its distance to the box's edge,
  so text before a formula can be up to 2 px from a *particular glyph* of it (measured worst:
  1.60). Text after a formula is under 1.5 px from the formula's rightmost part and at most 1 px
  from the box's edge.
- A glyph in the middle of a long word is held by nothing (2.80 px measured, as before).

**Why the rightmost part and not the box's edge or the last glyph shipped.** The last glyph
shipped is an accident of ship-out's order (for a fraction, the denominator's). The box's edge
was tried first: text after a formula was then two roundings from the ink next to it (the
superscript of `$x^2$-like`, the `b` of `($a+b$)`), where a letter inside a word is one. From the
rightmost part it is one rounding from the ink it follows — the bar for a bracket closing a
fraction, the last letter for one closing a row of letters — unless the hold to its own place
moves it.

**Measured** (three paragraphs of prose with text glued to formulas on both sides — `$n$-th`,
`$x$-axis`, `$f$'s`, `($x$)`, `f($x$)`, `is $x$`, `$x$,`, fractions and radicals in brackets —
justified at 520, 337 and 250 px, 13 to 20 px as an em and as a font size, three origins; 18,990
joints, 13,227 spaces between words). Drawn distance less laid-out distance, px:

| joint | n | reference, nearest part | product before | product now | now, at the box's edge |
|---|---|---|---|---|---|
| text, formula | 2,448 | -1.25 to +1.19; 28 over 1 | -1.83 to +1.75; 159 over 1 | -0.96 to +0.92; 0 over 1 | same |
| text, space, formula | 6,822 | -1.90 to +1.79; 380 over 1 | -1.87 to +1.91; 601 over 1 | -1.32 to +1.23; 48 over 1 | same |
| formula, text | 8,208 | -1.00 to +1.00; 0 over 1; 280 over 0.5 | -1.00 to +1.00; 0 over 1; 216 over 0.5 | -1.20 to +1.26; 13 over 1; 673 over 0.5 | -0.97 to +0.99 |
| formula, space, text | 1,512 | -0.97 to +0.96; 374 over 0.5 | same as reference | -0.89 to +0.90; 103 over 0.5 | same |

"Nearest part" is the glyph origin or rule end of the formula nearest the text in the layout,
which both rule sets have. Furthest part of the formula from its laid-out distance to the text:
reference 2.08 / 2.53 / 2.51 / 0.97, now 1.33 / 1.60 / 1.39 / 0.90. Spaces between two words in
the same prose: reference -2.92 to +2.50 (1,341 over 1 px); before -1.30 to +1.28 (83); now
-1.30 to +1.33 (97).

**What it costs, plainly.**

- **Text straight after a formula is the one joint the reference draws more evenly.** It places
  that text one rounded gap from the formula's last glyph and holds it to nothing; here it is
  held beside its own place, and where the hold moves it, it is a pixel nearer or further: 673
  of 8,208 (8.2 %) over half a pixel from the nearest part against the reference's 280 (3.4 %),
  13 (0.16 %) over a pixel against none, worst 1.26 against 1.00. Not holding it gives that back
  and takes up to 2.5 px out of the space that follows, which is finding F1. The space is where
  the eye sees it less, but the bound is on the space, so the joint pays.
- A formula is on one of the two pixels beside its place and not always the nearer: where it
  stands depends, by a pixel, on the text before it on the line (as a word's place already did).
- Spaces between two words over a pixel off: 308 -> 314 of 37,778 in word-gaps' corpus, 83 -> 97
  of 13,227 here. Words after formulas are led now, and a led word can be held.
- Letter evenness inside words is unchanged: 13,348 of 150,624 pairs (8.9 %) over half a pixel.
- At 13 px on a line with a formula every third word, spaces are still visibly unequal: a 1.4 px
  error on a 2.5 px space is inside the bound.

**Also in this task.**

- `Drawer::settle_word` no longer uses `std::nth_element` or a buffer: the smallest rounding
  that gives is found a byte at a time from the top of its bits (four passes over the word and a
  fifth to move it, for a word more than a pixel and a half out; one pass otherwise). Linear in
  the worst case on any standard library, and a megabyte word leaves nothing in the `Drawer`.
- A rule that lies wholly to the left or right of a formula's box is not the formula's and ends
  it. Ship-out makes no such list (a formula's rules are in its box and a page's rules follow a
  line); a hand-built list can, and is now drawn as the reference draws it.

**Not decided here.** Whether the user prefers the joint after a formula exact and the bound on
the following space loosened to 2.5 px for that one case: it is the `leads ? led_to(...)` choice
in the "text straight after a formula's glyphs" branch of `Drawer::place_commands`. And a closing
dollar followed by a letter, digit, `{` or `(` closes nothing (`scan_math`, `src/imtypeset.cpp`,
after the reference), so `$n$th` is drawn as its source, dollars and all: that is the text layer's
rule, not draw's, and it is what a reader of the demo sees for that input.

**Amends D47 and D48** in three sentences. D47, "the left edge of the formula's box … is put on the
nearest pixel": on the nearest pixel when nothing leads it, otherwise on one of the two pixels
beside its place; a formula is still one picture from its edge. D48, "A word that starts a line
or follows a formula, a rule or an atom is on the pixel nearest its own place": a word after a
formula is led from the formula's rightmost part, and a formula's box from the text before it;
line starts and words after a rule of the page are anchored as before. D48's count of spaces
more than a pixel out, 308 of 37,778, is 314 on the same corpus now, and the 430 spaces beside
a formula it left alone are now 4 over a pixel, from -0.97 to +1.23 px (were 27, -1.34 to
+1.56).

**Cost in time** (`docs/performance.md`, "After `draw-seams`"): none the measurement can see.

**Run** by the builder on the final sources: debug, release, stb_truetype, 16-bit `ImWchar` and
ASan + UBSan, 41 of 41 in each, in private directories; and by the `docs` task from a clean
`release` and a clean `debug` preset configuration, 41 of 41 and 42 of 42, with
`test_word_gaps` 19 tests / 23,541 checks. Not run: the `amalgamation` preset, the throwing
build, clang.

## D51. SDL2 comes from the machine or from `.deps/sdl2`; the tables goldens name the reference's present commit (task `release-gate`, 2026-10-10)

**Decided, SDL2** (the owner's request: no instruction in this tree sends a reader to a sibling
project). The demo host (`examples/demo_sdl2`) and the self-hosted Python library (`python/`)
take SDL2, first match wins, from `IMTYPESET_SDL2_DIR` when it is given, from an installed SDL2
that `find_package(SDL2 CONFIG)` finds, and otherwise from `.deps/sdl2` of this tree, an
untracked directory laid out as an install prefix, as `.deps/imgui` is the untracked ImGui.
`tools/vendor_sdl2.sh <install prefix or build tree>` fills it from an SDL2 that is already built
and writes `PROVENANCE.txt` and SDL2's licence beside it. The `python-host` preset names no
directory. SDL2 is not in the repository (`THIRD-PARTY.md`).

**Run:** `build/demo` and `build/py-host` from empty directories by the README's lines, and the
`python-host` preset from an empty directory (42 of 42): each configure reports
`.deps/sdl2/lib/libSDL2d.a`, and `example-project` occurs nowhere in their `CMakeCache.txt` or
`build.ninja`. The script, run in scratch copies on the build tree the directory was first
copied from by hand, gives the same files. **Not run:** a system SDL2 package (the machine has
none); the script on a shared-library SDL2 or on macOS; a tree with neither SDL2.

**Decided, goldens.** The gate ran all thirteen `tools/oracle/dump_*.py` into a scratch copy.
Twelve directories were byte for byte what is here. `tests/golden/tables` differed in nine
comment lines a file, the header that names the reference's commit and its uncommitted files,
and in no record: the reference had been committed to since (`a3c589a` to `30496e1`; `git diff
--stat` between them touches `docs/`, `scripts/bench_draw.py` and `tests/`, nothing under
`advtext/`). The six regenerated files were taken, so that the goldens and `PROVENANCE.md` name
one commit, and the manifest was written again. They are the reference's output, not the port's.
`tools/oracle/dump_font_wild.py` could not be started with `-P` (it did not put its own directory
on `sys.path`, alone among the scripts); it does now.

## D52. SDL2 by the standard chain; `-ffp-contract=off` on the Dear ImGui built here (task `final-touches`, 2026-10-10)

**Decided, SDL2** (amends D51's order; the owner asked whether an SDL2 installed from a
distribution's packages is found without a scanner). `cmake/sdl2.cmake` holds the one order, for
`examples/demo_sdl2` and `python/` alike, first match wins:

1. `IMTYPESET_SDL2_DIR`, when given. A directory that holds no SDL2 is an error; nothing else is
   tried in its place.
2. `find_package(SDL2 CONFIG)`: the target `SDL2::SDL2`, else `SDL2::SDL2-static`.
3. The same package when it defines no target: `SDL2_INCLUDE_DIRS` and `SDL2_LIBRARIES`, the
   second stripped of the trailing space some of those files leave in it.
4. pkg-config's `sdl2`, as an imported target.
5. `.deps/sdl2`.

With none, `FATAL_ERROR` names the distribution packages (`libsdl2-dev`, `SDL2-devel`),
`tools/vendor_sdl2.sh` and the variable. The `STATUS` line names the route taken.

**Why.** 2 to 4 are how CMake projects find a system library, and need nothing of the user. An
installed SDL2 stays ahead of `.deps/sdl2`, as in D51: the vendored copy is a debug build for
one platform, and a machine that has a package should not need it. It does mean that installing
a package changes what an existing checkout links; the configure output says so when it happens.

**Run** (GCC 12.2, CMake 3.25.1, pkg-config 1.8.1; private build directories, since removed):
route 5 and route 1, demo configured; route 1 on an empty directory, the error; `.deps/sdl2`
renamed away, the `FATAL_ERROR` for the demo and for `python/`, and the directory back with
every file's hash as before; routes 2 (each target), 3 and 4 against hand-written
`SDL2Config.cmake` and `sdl2.pc` files that point at `.deps/sdl2`: each configured, built with no
warning and ran `demo_sdl2 --frames 3` under SDL's dummy driver; `python/` on its own through the
`sdl2.pc`: built, `python_binding` passed. **Not run:** any of 2 to 4 against a distribution's
package, of any version or distribution: the machine has none. In particular nothing here shows
what a real old-style `sdl2-config.cmake` puts in `SDL2_LIBRARIES` beyond what the stand-in
copies from memory of them (`-L<dir> -lSDL2` with a trailing space), nor a shared `libSDL2.so`
on any route, nor SDL2 found both as a package and by pkg-config with different versions.

**Decided, floating point** (amends D12, "None of it is applied to ImGui", for this one flag;
D16). The Dear ImGui this tree compiles itself is compiled with `-ffp-contract=off`
(`/fp:precise` in the unrun MSVC branch): the `imgui` target of `cmake/imgui.cmake`, the `imgui`
and the backends of `examples/demo_sdl2`, the backends inside `python/`'s library, and with them
the demo host's and the Python host's own file and `tests/test_c_api.c`, which did not carry it
either. PRIVATE in each case. It is **never** added to an ImGui target that existed before this
tree's CMake ran (a host's `imgui`, `imgui::imgui` from a package), and the sidecar compiles no
ImGui at all. The warnings and `-fno-exceptions -fno-rtti` are still not applied to ImGui.

**Why.** The draw goldens compare vertices that ImGui's code computes from what the library
hands it. With the flags of every preset nothing contracts (no `-march`), which is why all of
them passed without the flag; a developer who adds `-march=native` to the global flags would
have had an ImGui that contracts and goldens that may fail on that machine only, which is what
D16 exists to prevent. For a host's ImGui the library can promise nothing and does not: its own
results (layout, the positions it hands to ImGui) are computed in its own translation units.
What a contracted host ImGui does to drawn vertices has not been measured.

**Run:** every compile line of `build.ninja` carries the flag in a demo build (29 of 29) and in
`python/` on its own (28 of 28); the eleven presets from empty directories afterwards, counts in
`docs/distribution.md` section 7, with `compile_commands.json` read for the flag. **Not run:**
any build with `-march` on ImGui; Clang; MSVC.

## D53. What a host met at the Document's edges, answered (task `swap-api-answers`, 2026-10-10)

*Agent decisions, taken unattended for the session swapping advtext onto the library; the user
may overrule any. The answers as that session reads them are in `docs/advtext-swap-status.md`.*

**Decided.**

1. **A copy has one separator between two blocks that each gave text, and none beside a block
   that gave none** (a rule, a host block without text, an empty block). Kept as it was: it is
   what the reference copies for text, rule, text (`UnitModel.copy_text`; read, not run). The
   reference's single newline between two blocks of one list is still not reproduced.
2. **A formula is marked, selected and copied whole when the range holds its first byte, and
   not at all otherwise.** Kept; now said at `ImTypesetTextRange` and `ImTypesetMark`.
3. **`GetDocumentBlockTextSize` answers the bytes handed in; positions in a code block end at
   its last character.** Two numbers on purpose: an append goes on from the first, and blank
   lines at the end stop being the end when text arrives after them.
4. **`GetFont` answers null unless the context's ImGui context is the current one.** It
   answered the stale pointer. Fixed.
5. **The lifetime rule is `DestroyContext()` before `ImGui::DestroyContext()`, and it is not
   enforced.** No ImGui context, or another one, is refused. A new ImGui context at the address
   of the destroyed one is taken for the old one, because `bound()` compares addresses and
   `imgui.h` offers nothing else. In every case run the roles are then found empty and nothing of
   the old atlas is touched. Two collisions are possible by reading and were not reproduced:
   `validate_fonts` accepting a host's font that stands at an old font's address with the same
   `FontId` (ids restart with each atlas), and its once-a-frame skip matching across the two
   contexts. **Not fixed.** What would close both: a stamp unique to the process in
   `ImFontConfig::Name` of each font added, compared in `validate_fonts`, and one such font
   looked at before the skip is taken. It reads `ImFont::Sources` and `ImFontConfig::Name`,
   which the wheel probe (`python/imtypeset/_bundle.py`) would have to learn. Queued.
6. **A null `draw_list` is the current window's** in `DrawParagraph` and `DrawDocument`, and is
   refused outside a frame by the test the widgets use. It was `InvalidArgument`. For a binding
   that cannot name a draw list (both applications reached `GetWindowDrawList` by its mangled
   name). No new ImGui symbol: the library already called it.
7. **No adoption of a host's font; one C-only function instead.** The atlas font is a rewritten
   copy (D27), so the two ImFonts are different by design. `ImTypeset_GetFontAtlasIndex(face)`
   says where the role's font stands in `ImFontAtlas::Fonts`, which is how a binding with its own
   ImFont objects finds it; the host then draws its own text with the library's font. It is not
   in `imtypeset.h`: a C++ host has the pointer.
8. **A refused `import imtypeset` removes the package's submodules from `sys.modules`.**

**Measured** (debug, FreeType 2.12.1, GCC 12.2, `tests/test_api_answers.cpp`, one run): a second
copy of the seven test faces (1,680,856 bytes of files) adds 1,847,800 bytes of ImGui
allocations before a glyph is drawn and 1,878,327 more once the same words are drawn in both;
atlas textures 1,835,008 bytes with the roles' fonts drawn, 3,145,728 with both. A role's font
and a host's copy of the file give equal `ImGui::CalcTextSize` for six text faces at 13, 16 and
24 px. `Document.pack_marks`: 2.0 to 0.5 microseconds a mark (2,000 marks, ASCII blocks).

**Not measured, not run:** any of it under stb_truetype or a 16-bit `ImWchar`; the two
collisions of item 5; a host font with `USE_TYPO_METRICS` set beside the library's copy.


## D54. The oracle is a pinned commit of the reference, not its working tree (task `oracle-freeze`, 2026-10-10)

**Decided.** The reference the goldens are dumped from is commit
`32e395346551cac82c7a6dd6e9b6d8e47feeac8e` of `../imgui-adv-text` (its tag `advtext-reference`),
extracted read-only into `build/advtext-reference` by `tools/oracle/pin_reference.sh` and
described by the tracked `tools/oracle/REFERENCE.lock` (commit, tree, file count, one SHA-256 over
every file's SHA-256). `PYTHONPATH` is that directory. Every `tools/oracle/dump_*.py` that imports
the reference, `manifest.py` when it writes, and `tools/gen_mathtext_tables.py` call
`pinned_reference.require()` first and exit 2 unless the tree matches the lock and `advtext` was
imported from it. This replaces D51's "the tables goldens name the reference's present commit":
they name the pinned one, read from the lock, and no script runs git against the sibling.

**Why.** The sibling is being rewritten onto this library on a branch and its Python layout core
is no longer in its working tree; the reference interpreter has an editable install pointing at
that working tree, so a run without `PYTHONPATH` imported whatever was there. A commit is the only
state of that repository that does not move. The commit is pinned and the tag only checked,
because a tag can be moved. `git worktree add` would have been the obvious tool and writes the
sibling's `.git`; `git archive` reads it.

**Not pinned, and it cannot be by this:** the interpreter and its libraries,
`../imgui-adv-text/.venv` (Python 3.11.2, imgui_bundle 1.92.900, fontTools 4.66.0, markdown-it-py
4.2.0). No commit holds a virtual environment. The commit must also stay reachable in the
sibling's object store; nothing in this repository keeps a copy, and `build/` is ignored.

**Read (git).** `a3c589a` has the pinned tree. `30496e1`, which `PROVENANCE.md` and the tables
headers named until now, does not: it is three commits later and differs in `docs/`,
`scripts/bench_draw.py`, `tests/test_docs.py` and `tests/test_style.py`, in nothing under
`advtext/` or `demo/` and not in `tests/test_stream_stability.py`, which are all a dump reads.

**Run.** All thirteen existing dump scripts from the pinned tree, one at a time, writing in place:
twelve directories byte-identical, the font corpus among them (2,991 faces, `--jobs 4`, 905 s);
`tests/golden/tables` differs in its header comment lines only and in no record. The four
refusals a wrong invocation meets (no `PYTHONPATH`, the sibling as `PYTHONPATH`, no `-B`,
`manifest.py` writing without the pin). **Not run:** the refusal of an edited tree or a wrong
stamp; `pin_reference.sh` against a moved or missing tag; any C++.

**Decided, new goldens.** `tests/golden/doc-tables` (`tools/oracle/dump_doc_tables.py`,
`FORMAT-doc-tables.md`): a table among other blocks — positions, selection, hit tests and copy
through cells, find hits and links in cells, a table growing by rows. Two things in it are the
script's definition, because the reference has neither and the product needs both: **a position
in a table is (block, row, column, byte offset into the cell's text), the header being row 0**,
and a cell's text is what the converter was handed for it, as `FORMAT-doc.md` §1 defines a block's. No test
replays the directory until the product has a table block. Selection across columns, find marks
drawn inside cells, balanced columns and a caret rectangle in a cell are not in the reference at
all: the core meets those without an oracle.

## D55. The demo's sections are callable one at a time, and the demo is in the sidecar (task `galleries`, 2026-10-11)

**Decided.** `ImTypeset::ShowDemoSection(ImTypesetDemoSection)` submits one section of the demo
window into the current window and `GetDemoSectionName()` answers its title; the enum has eleven
stable values and `_COUNT`. One function and an enum, not a function a section: a host that makes
a page of each (advtext's demo, which gives up its specimen, formula and math pages) lists them,
and a new section costs no declaration, C counterpart, map row or wrapper. Python:
`imtypeset.demo`.

**Decided: a section shown alone keeps a state of its own**, apart from the demo window's, per
context (two tables of `DemoState`, each bounded as D49's one was). *Measured*, with the state
shared: a host showing the window and the same sections alone in one frame, at two type sizes,
laid out 230 times a frame, because every kept measurement and every paragraph object was taken
again at each call's style. With two states: 0.

**Decided: the bundle-hosted library is built with the demo.** The wheel's fork declares the
callback of `InputTextMultiline` and `InputTextWithHint` as a `std::function`
(*read*: `nm -DC` on the modules of wheels 1.92.801 and 1.92.900), so upstream's names, which a
library compiled against stand-in headers calls, are not exported. `python/host/
imtypeset_bundle_shim.cpp` defines those two names under `IMTYPESET_BUNDLE_STAND_IN` and hands
over to the wheel's, declared there by hand. `src/imtypeset_demo.cpp` is unchanged by this and
still uses only `imgui.h` and `imtypeset.h`. *Measured*: both libraries link with
`--no-undefined`; text typed into the demo's formula field through the wheel reaches it, in both
venvs. **Not run:** the `fork:` branch of the build script, where the shim's two definitions are
compiled out and the demo calls the fork's functions directly.

**Decided: what a gallery needs that the public API cannot say is listed, not reached for.**
The Specimen reads set widths off a paragraph object's line (`GetParagraphLine`), since
`CalcTextSize()` answers whole pixels; it shows kerning as one text against two texts placed by
their widths, with no "kerning off" switch and no kern query. *Measured*: the six set widths at a
24 px em, the ladder and the seven kerns at 44 px agree with the reference's
`advtext.ui.specimen.build_specimen` (tag `advtext-reference`) to the four decimals of a pixel it
prints; `tests/test_demo.cpp` holds eleven of them. The gaps (no overlay for a paragraph, no face
metrics, no glyph coverage, no public "the fonts changed") are in the task's result file.

**Decided: `examples/demo_headless.cpp` rasterises the last frame of a run only.** The frames
before it go through the renderer, which takes the atlas's textures, into a strip one row tall.
*Measured*: the sixteen pages are pixel-identical to rasterising every frame; 25 s to 3 s in
release, and 46 s under ASan+UBSan where the sixteen pages took more than 550 s, past the
example's 600 s ctest timeout.

## D56. An exact line height, a glyph query, a paragraph hit's tag, and a draw that drops what a clip cannot show (task `tables-core`, stage 0, 2026-10-11)

*Agent decisions, taken unattended for the session swapping advtext onto the library
(`docs/advtext-swap-status.md`, item 5); the user may overrule any. The requests are
`../imgui-adv-text/.concerns/FS-S1--builder--result.md` R1 and R1b and
`FS-S2--builder--result.md` section 4.*

**Decided.**

1. **`ImTypesetBlockFlags_FixedLineHeight`: every line of the block is exactly its line height.**
   The mechanism is the strut's: `Strut::exact` makes `Strut::apply_to` REPLACE a line box's
   height and depth with the strut's where it otherwise floors them. `Strut::filling` already
   makes height plus depth one `\baselineskip`, so `append_to_vlist` (tex.web section 679) finds
   a gap of exactly zero between every two lines and the baselines are one `\baselineskip`
   apart. It is what TeX reaches with a strut on a line whose contents are smashed, or with
   `\lineskiplimit` at `-\maxdimen`; the box is changed rather than the limit because the
   document stacks blocks from their line boxes (`LineDims`) and a block's height must be the sum
   of them. A flag on the block and not a field of `ImTypesetBlockStyle`: it is one bit, a host
   wants it for some blocks of a style and not others (a diff's code beside its prose), and a
   flag costs the C mirror and the Python package one enumerator. It holds for text blocks as
   well as code, since nothing in the mechanism is code's. `DocumentRules` and the reference
   replays are untouched: the flag is not set by them.
   **What overhang does.** The line's box is the pitch, so a selection's rectangle, a `Behind`
   mark and `ImTypesetLine` are the pitch tall and ink that is taller reaches into the
   neighbouring lines' boxes; nothing is clipped to a line box. A draw skips whole lines by their
   box grown by `CULL_MARGIN_PX` (2 px), and a document lays out the units within
   `WINDOW_MARGIN_PX` (2 px) of the clip rectangle, so ink that overhangs its line by more than
   two pixels is not drawn while its line is wholly outside the clip rectangle by more than that:
   it can go missing in a strip at most the overhang tall at the very edge of a scrolled view.
   *Not measured:* how far the tallest glyph of an installed face overhangs a 1.25 em pitch.
2. **`FaceHasGlyph(face, codepoint)`** answers for the font added for the role and nothing else:
   `FaceData::has`, the reader's own character map with the codepoints minted for a math face,
   which is what layout asks before it looks for a stand-in. `CodeBold` answers for `Bold`'s font
   until it has its own, as layout sets it. It needs no ImGui context and is not an [E] function.
3. **`ImTypesetHit::Tag`**, appended. `sizeof(ImTypesetHit)` goes from 12 to 16, which
   `IMTYPESET_CHECKVERSION()` compares: a host and a library from either side of this change
   refuse each other, as intended.
4. **No natural-width query.** A paragraph laid out wider than its text has the text's natural
   width in its line (`GetParagraphLine(p, 0)`: `Max.x - Min.x`), whatever the alignment, and the
   header now says so at `GetParagraphSize`, whose own width is the wrap width for text that is
   not left-aligned. A table's columns are the library's business from the next stage, which is
   what the question was for.
5. **`DrawRules::quads_off_clip_dropped`** (product; the reference's rule is unchanged): a glyph
   whose quad lies wholly at or below the clip rectangle's bottom edge, or wholly above its top
   edge, is not emitted. The comparison that let it through is `ImFont::RenderText`'s own, on the
   font's cell: a glyph whose pen is exactly on the bottom edge passes it. A document drawn in
   slices under clip rectangles that meet (columns) then emits a line in the slice that shows it
   and in no other.

**Found on the way, and fixed.** The tag a document's hit test answered was wrong on the right
half of two characters of every tagged run: `HitTestDocument` looked for the item whose box held
the point by `cmd.x + cmd.width`, and a GLYPH command carries no width, so no glyph ever matched
and the tag was the run of the character the CARET was before. The right half of a link's last
letter answered 0 and the right half of the character before a link answered the link. Both hit
tests now find the last item of the line that starts at or left of the point (`byte_under`,
`src/imtypeset.cpp`). Shown by `a_paragraph_hit_answers_the_tag_of_the_run_under_the_point`,
which failed at the last letter with the old search copied into `HitTestParagraph`; the document
test beside it was written after the fix and was not seen to fail.

**Measured** (debug, FreeType 2.12.1, GCC 12.2, the bundled faces at a 16 px em and a 20 px
pitch, `tests/test_api_requests.cpp`). A code line holding U+2320, U+27E8 and U+0416, none in the
bundled Inconsolata: without the flag its box is more than 20.5 px and the line three below is
more than half a pixel low; with it all four boxes are equal to the scaled point and the block is
four pitches. 500 lines, every seventh with borrowed glyphs: the document's height is 10,000 px
and line tops agree with a document of plain lines to the bit. The pitch is 20 px rounded to a
scaled point (line `n` is within `n` times 8e-6 px of `20 n`). Under the reference's draw rule a
whole-pixel cut through a line emits more vertices in the two slices than under the product's;
under the product's a cut at any line top, at 16 document heights, emits exactly the whole
document's. *Not reproduced as reported:* FS-S1's U+2502 and U+1EB2 are in the Inconsolata bundled
here, so the reproduction uses other characters; the 22.4 px against 20 was not re-measured.

**Reproduced and NOT fixed (review-5-swap F2): a null draw list after `EndFrame()` with no
`Render()`.** `NewFrame(); EndFrame(); DrawParagraph(p, NULL, pos)` dies in
`ImGui::GetCurrentWindow()` on a null window (run under ASan + UBSan: `imgui_internal.h:3350`
"member access within null pointer of type 'struct ImGuiWindow'", then SEGV, from
`ImGui::GetWindowDrawList()` at `src/imtypeset.cpp` `DrawParagraph`). The test for "inside a
frame" is `GetFrameCount() > 0 && GetDrawData() == NULL`, which `NewFrame` makes true and only
`Render` makes false. *Read, in the pinned `imgui.cpp`:* everything `EndFrame` changes that
`imgui.h` can read — `io.MousePosPrev`, `io.AppFocusLost`, `io.MouseWheel`,
`io.InputQueueCharacters`, `io.MetricsActiveWindows`, `ImGuiPlatformIO::Textures`,
`ImTextureData::RefCount`, and `ImFontAtlas::Locked` (set only under a backend without texture
updates, which nothing here draws under) — is something `NewFrame` does not reset, so none tells
"ended, not rendered" from "inside"; the current font is the default font in both; and every
public function that reads the current window (`GetWindowDrawList`, `GetCursorScreenPos`,
`GetWindowPos`, `PushItemWidth`, `GetID`, `IsWindowAppearing`) dereferences it unchecked.
`ImGuiContext::WithinFrameScope` is what answers, and it is `imgui_internal.h`: the library
reads no internal field, and the sidecar in the stock wheel is compiled against stand-in headers
whose `ImGuiStyle` is a different size, so an offset into `ImGuiContext` past it would be wrong
there. **Decided:** the header says what is and is not refused (`DrawParagraph`), a test pins the
two states that are (before the first frame, after `Render()`), and the state that is not is
named in it. The same hole is in the widgets and `DocumentView()`, and was there before a null
draw list was accepted. What would close it: a host-called `ImTypeset::EndFrame()`-style hook, or
reading the internal field under upstream headers only; neither built.

## D57. Tables are a block of the Document: cells as stretches of the block's text, TeX's alignment under them (task `tables-core`, 2026-10-11)

*Agent decisions, taken unattended for the swap of advtext onto the library (the user's ruling:
tables are TeX's alignment and belong in the core). The user may overrule any. Names are in
`include/imtypeset.h` under "Tables" and, for the other session, in
`docs/advtext-swap-status.md`.*

**Decided: the layout core is `src/align.*`, named and commented against tex.web part 37
(sections 768 to 812), and it does what the reference does.** `fin_col` records an entry's width
in its column (796), `column_widths` is the step of 801 as the reference has it (`apportioned`,
`squeezed`, `shared`), `Aligner::set_row` is 807 to 810, `fin_row` 799, `Aligner::fin_align` 800;
a wrapped cell is reboxed as section 1087's `\vtop`. The eighteen departures are T1 to T18 of
`docs/divergences.md`. *Checked:* 679, 768, 777, 796, 799 to 802, 804, 806 to 810 and 1087 by
counting modules from the top of `tex.web` and reading each.

**Decided: a table is ONE block, and a cell is a stretch of the block's text.**
`ImTypesetCell {TextStart, TextEnd, Flags}`, in reading order, `ImTypesetCellFlags_RowStart`
opening a row. Considered and not taken: a block a cell (the document's unit of laziness would
be a cell, and a row's height is a property of all its cells); rows and columns as counts on the
block (the reserved `TableRows`/`TableColumns`: a ragged row cannot be said, and the reference
sets a row that LACKS a cell differently from one whose cell is EMPTY — the cell it lacks takes
no part in whether the row fits its columns); a position of four numbers. **Positions stay
(block, byte offset into the text the host handed in)**, so every call that takes or answers a
position, a range, a mark or a copy works in a table with no new form, in three languages.
`GetDocumentTableCell(position)` answers (row, column, cell, byte in the cell) by bisection over
the cells' starts, a step for each doubling of their number, and
`GetDocumentTablePosition` the other way in constant time; neither lays anything out. Asked for
was "O(1) a query": the way back from a byte to its cell is O(log cells), which for the limit of
262,144 cells is eighteen steps, and a table the size of a cell's index to make it one step
would be paid by every table for a query a host makes on a click.
**The host is advised to write the block's text as a copy of the table reads** (tab between
cells, newline between rows). Then the bytes between two cells belong to no cell, the end of one
cell is not also the start of the next, and the block's text is what a copy of the table gives.
Nothing requires it.

**Decided: a caret at the end of a cell is that cell's end.** An index that came from no
character (the `\hss` that places a cell, the skips of a wrapped line) is, before its cell's
first character, that character's byte, and after its last, the end of the cell's text
(`map_table_indices`). The dump script of `tests/golden/doc-tables` resolves such an index
FORWARD, to the next cell's first byte (`TMap.pos`); that is the script's convention, not the
reference's, and is not the API's (coordinator's ruling). A position between two cells — which
only exists when the host put a byte there — is the start of the later cell.

**Decided: a copy separates cells by a tab and rows by a newline, fixed.** They are the
reference's (`advtext.ui.units._separators_within`: `CELL_BREAK`, `LINE_BREAK`), written before
the first index of each cell but the first of its row and of each row but the first; a copy
writes a separator before the text it leads, so tabs after the last text copied are not written.
Not a parameter: `AppendDocumentRangeText` has one separator argument and it is the blocks'; a
host that wants another form of a table has the cells' texts.

**Decided: the widths while a table grows — `ImTypesetBlockFlags_TableHeld`.** TeX's alignment
needs every row before it knows a width, and the reference's default rule refits the table to
the measure at every layout, so each arriving row moves every column. With the flag each column
is the widest its cells have wanted at any layout since the block was described, never narrower;
the table is as wide as that and not the measure; and when the held widths stop fitting, the
default rule applies until they fit again. This is the reference's own streaming rule
(`StreamView._held` and `_sized_to`, a column of no width raised to one scaled point), moved from
a view into the block. The widths survive a replace of the one table by one table with the flag
and as many columns, as `_held` is kept by block number and compared by length, because a host
that parses its source again as it arrives re-describes the block each time. "Settle on close"
is the host's: it replaces the table once more without the flag (the reference's `reset`), or
leaves the flag on for good (its `keep_streamed`).

**Decided: a table is laid out whole, and a row is kept as it was shipped.** One block, one unit
of the lazy document: nothing of a table costs anything until a window reaches it. Its layout
keeps each row shipped alone (`TableLayout`), and a row is set again only if its cells changed
(a stamp from `table_stamp()`), the column widths changed, or anything else it was set from did
(the indent, the alignments, the padding, the breaking parameters). The rows are stacked by
arithmetic on their boxes, with the prev_depth a wrapped row carries (`LineDims::resumed`). A
cell is made into material and measured once (`TableCellsMeasured`); its longest unbreakable
piece is measured only when a solve needs it, and kept by the row's stamp. What stays O(rows) in
an append: the rows' kept commands are copied into the block's and renumbered, and the widest
cell of each column is taken again over the cells' kept widths. No line is broken and nothing
is packed for a row that stands.

**Decided: limits, by construction.** 1,024 columns and 262,144 cells a table; cells past
either are left out and said (`DescriptionCut`). A table is set whole, so the limits are what
bound one layout. *The reason for 2^18 and not 2^20:* a table of a million cells would be a
million paragraphs' material built by the first frame that shows any of it.

**Decided: what was reserved is renamed.** `ImTypesetBlock::TableRows` and `TableColumns`, which
every host was told to leave zero, are `CellStart` and `CellCount`; `ColumnStart` and
`ColumnCount` are appended. A block of `ImTypesetBlockKind_Table` with text and no cells was an
empty Host block that said `DescriptionCut`; it is now a table with no rows, of no height, and
says nothing.

**Decided: the rule under a header is the options' colour, a formula's rule in a cell is ink.**
`DrawArgs::rule_between` colours a rule that is no formula's, which under the product's draw
rules is the head rule alone.

## D58. Columns are a way a Document is shown: TeX's `\vsplit` under them, cut lazily from the front or balanced (task `columns-core`, 2026-10-11)

*Agent decisions, taken unattended for the swap of advtext onto the library (the user's ruling:
"splitting a vertical list into COLUMNS" is TeX's and belongs in the core). The user may overrule
any. Names are in `include/imtypeset.h` under "Columns" and, for the other session, in
`docs/advtext-swap-status.md`. What stands on a golden and what on a decision alone is said at the
end.*

**Decided: the cut is `src/vsplit.*`, named and commented against tex.web part 44 (sections 967
to 979), and it does what the reference does.** `prune_page_top` is 968 and 969, `VertBreak` the
scan of 970 to 976 with 971's `best_height_plus_depth`, `vsplit` 977, and `split_into_columns`
the reference's loop over them. The departures are V1 to V10 of `docs/divergences.md`; no
disagreement between tex.web and the reference was found in what the reference implements.
*Checked:* 148, 833, 968 to 979, 1001 and 1082 by counting modules from the top of `tex.web` and
reading each. (FS-S2 gives 1071 for the `\vsplit` primitive; 1071 is `\hbox`'s, and `\vsplit` is
dealt with at 1082.)

**Decided: columns are a property of how a document is SHOWN, not of its description.**
`SetDocumentColumns(document, columns)` beside `LayoutDocument(document, width)`, whose width
stays the width of ONE column. Weighed against a property of the description (a container kind,
or fields of `ImTypesetDocumentContent`): the description would have to be handed over again to
change the count, the same document could not be shown uncut in one pane and in columns in
another frame, and every binding's `set_blocks` would grow for something no block knows.
Weighed against a parameter of each draw and query call (a "slice" a host draws under a clip, as
FS-S2's E2 does it): that leaves the cut, the reading order of a drag and the lazy bookkeeping to
every host, which is the code the ruling moves here. As a property of the shown document it costs
four functions and two structs, no call changes shape, and **no block is laid out again when
columns change**: a block's lines depend on the column's width and on nothing else.

**Decided: the document is not cut; a column is a stretch of the one uncut column, moved.** The
block layer goes on laying a document out as one column (`src/blocks`), and `src/columns` keeps,
per column, where its stretch begins in that column and how far down the column's box it is put.
This is exact, not an approximation of the reference: a document's list has no stretch or shrink,
so the box section 977 packs `exactly` sets every glue at its natural width (`columns.golden`
packs its overfull columns `shrinking` at ratio 1 against no shrink: nothing moves), and
`DocumentColumns::flatten` equals the reference's shipped columns command for command. What it
buys: layouts, positions, the index maps, selection models and marks are the uncut document's,
untouched, and a point is taken between the page and the uncut column on the way in and out
(`uncut_of`, `shown_at`). What it cannot show is a list WITH stretch spread to a column's height;
no block makes one.

**Decided: two ways of filling, and which is lazy.**
- *In order* (`Height` above zero): the reference's. Every column is that tall and takes what
  `vert_break` gives it; as many columns as it takes, to the right. Lazy from the front: the
  document's list is made a unit at a time (`Document::unit_vlist`, a box as a RULE node of its
  dimensions, so no arena and no node outlives a cut) and the scan is fed it as it grows
  (`VertBreak::scan` with `complete` false). A column costs the units it holds and the one after,
  where the scan finds the page too full. Column k costs every column before it, once: where it
  starts is where they ended, and no estimate can stand in for that.
- *Balanced* (`ImTypesetColumnFlags_Balance`, `Count` of two or more): not the reference's, which
  does not balance (`columns.py`: "Balance: not done, and here is the price"). `Count` columns of
  one height, the LEAST at which that many hold the document, found by bisection over the height
  with one pass over the list a trial (TeX's own way to balance is the same cut tried at one
  height after another; *The TeXbook*'s `\balancecolumns` steps the height up by a point at a
  time). **It needs every height, so it lays out every block, and any edit searches again**; that
  is said in the header, and in order is the lazy mode offered instead. Why the least height that
  holds it and not, say, the height that makes the columns most nearly equal: it is one number
  with a definition a host can state, it is monotone (more height never holds less of a rigid
  list, so the bisection is exact: one scaled point less and the columns do not hold the
  document, which `tests/test_columns.cpp` checks), and the columns before the last then differ by
  no more than what cannot be cut. The page's height is the tallest column's material, so a block
  that cannot be cut and is taller than the balanced height makes the page as tall as it is.

**Decided: what is kept, and what an edit costs.** A cut column is kept with the first unit its
scan did not read. `Document::take_changed` answers the first unit that is not what it was
(found in `prepare` by comparing each unit with the one that stood there; `edit_text` names its
unit); the columns whose scans read that far are dropped and cut again when next reached, and
the ones before stand. Text appended to the last block cuts the last column or two again. A scan
that came to the end of the document also "read" what is not there yet, so a block appended
after the last cuts the last column again. Balanced columns are all dropped by any change. Not
refined: after an edit that adds, removes or re-describes blocks, a quoted run is stacked again
when next shown (D37), so the columns from the document's FIRST QUOTE on are cut again, whatever
the edit was.

**Decided: under the product's rules a column may end between two blocks, but not after a
heading** (`DocumentRules::break_between_blocks`, item 16 of the block layer's switches;
`Block::keep_with_next`, set for `ImTypesetBlockKind_Heading`). In the reference the space
between two blocks is a kern with no glue after it, which section 973 does not break at: a run
of one-line blocks (a list of short items, twenty short paragraphs) is one piece however long
and runs off the foot of its column. `columns.py` names the fix itself ("`_stacked` emitting
`Glue(GlueSpec(width=gap))` instead of `Kern(width=gap)` would make every block boundary legal").
The product does that in the list a column is cut from, and there only: the gap is glue, there
between any two blocks (of no width where they have none), and stays a kern after a heading —
TeX's `\nobreak` after a heading, said in the terms section 973 already has. The uncut document
does not read the switch, and under reference rules the list is the reference's, which the
goldens hold. *Not done:* widows and orphans. Section 974 cannot weigh a penalty in a list that
does not stretch (every short page costs `deplorable` whatever its penalty), so `\clubpenalty`
and `\widowpenalty` would change nothing; keeping two lines together needs another mechanism and
none was built.

**Decided: the product's `\splittopskip` is the height of a line of the document's style above
its baseline** (`Strut::filling(baselineskip).height`), where the reference's is ten points
whatever the type. A column headed by a line of body text, or by anything taller, is then flush
with the top; one headed by something shorter (a thematic rule, the rule under a table's header,
small type) has it where a line's baseline would be, which is what section 969 is for. Ten points
stays under reference rules.

**Decided: what cannot be cut and is taller than a column** — a host block, a quote, a table row
whose cells wrapped, a single line — is put in a column alone and reaches past its foot
(`ImTypesetColumnGeometry::Overfull`, `ContentMax.y` past `Max.y`). That is the reference's
(`vert_break` takes the one break there is; `columns.golden`, `a-quote-taller-than-the-column`).
The library does not clip it, shrink it or refuse: what to do with it is the host's, which has
the geometry. A code block and a table are cut between their lines and their rows; nothing keeps
a table's header with its rule or its first row, as in the reference (FORMAT-tables.md section
6). *Not built:* cutting a quote; a quote taller than a column is overfull.

**Decided: the page, and which calls speak of it.** Column k is at `x = k * (width + Gutter)`,
`y = 0`; a default gutter is one line of the document's style (the reference demo's
`GUTTER_LEADINGS = 1.0`). The calls a view makes speak of the page: `DrawDocument`,
`HitTestDocument`, `SelectDocumentRange`, `GetDocumentRangeRects`, `GetDocumentCaretRect`,
`GetDocumentSize`. The calls that describe a block speak of the UNCUT column, as if there were
no columns: `GetDocumentBlockGeometry`, `GetDocumentBlockLine`, the three table queries,
`LayoutDocumentWindow`; and `GetDocumentColumnPos` takes a point from there to the page. Why not
answer block geometry on the page: a block cut by a column is not a rectangle, and a line's
rectangle "relative to its block's corner" has no meaning when the block has two corners. One
function that moves a point is smaller than a second form of five answers. A top handed back to
`GetDocumentColumnPos` is its own line's to within what a float of pixels holds (the column is
chosen by the next height a float can say): a float does not carry a scaled point.

**Decided: reading order, selection and copy across columns** (no oracle: the reference draws
columns and never selects in them). Positions are the document's, so order is the document's:
down a column, then the next. A point is taken to the uncut column by the column it is over —
a point in a gutter is the column's on its left, one past the last column the last column's —
and, within that column, by its height, held between the column's first line and its last: a
point below a column's last line is at that line, not at the head of the next column. So a drag
from a point in one column to a point in a later one selects from the first point to the foot of
its column, every column between, and the later column from its head to the second point; and
dragged the other way, the same range. Rectangles of a range are each in the column their line
is in. A copy is the uncut document's, to the byte: a cut adds nothing to the text, since it is
not in the text.

**Decided: `DocumentView` in columns.** `Count` columns across the view's width, each what the
gutters leave; in order, a column is as tall as the view when `Height` is 0, and the view
scrolls sideways through the rest; balanced columns are scrolled down. `LayoutDocument` with a
`Height` of 0 and no balance shows the document uncut: outside a view there is no height to cut
at.

**Decided: limits.** A column's height is at least one scaled point; at most 4,096 balanced
columns; values that are not numbers are zero.

**Measured** (GCC 12.2, debug, private build directories; `tests/test_vsplit.cpp`,
`tests/test_columns.cpp`, `tests/test_api_document.cpp`, `tests/test_api_columns.cpp`):
- *Goldens.* At the core, the 28 `vsplit` and `columns` cases of `tests/golden/tables` replay
  record by record: 85 splits, 247 columns, 1,850 legal breakpoints with their totals, badness,
  cost and taken flag, 4,450 shipped commands. Through a `Document`, and again through the public
  header with every length a float of pixels held to the golden's scaled points by `==`, the 4
  `columns` cases: 8 splits, 18 columns (and the 3 the reference leaves empty), 2,634 commands.
- *A property, for what no golden holds.* 2,113 columns of 40 random documents (quotes, code,
  rules, random heights, `\splittopskip` and `\splitmaxdepth`) are the columns
  `split_into_columns` cuts from the same blocks composed at once, shipped; 1,993 of them start
  inside a block.
- *Lazy layout, N = 10,000 paragraphs, by the counters.* In order: a frame that shows three
  columns lays out 14 blocks (the columns hold 14) and cuts 3 columns; the next frame, a hit
  test, the size and the count cost nothing; a frame of columns 10 to 12 brings it to 62 blocks
  (they end at block 57) and 14 columns; the whole document is 2,308 columns and 10,000 blocks,
  against an estimate of 2,653 made after three columns; text appended to the last block then
  lays out 1 block and cuts at most 2 columns. Balanced into six: the first question lays out
  10,000 blocks, tries 36 heights and cuts 6 columns; an appended word lays out 1 block and
  searches again.

**Not measured.** Any time in a release build before the numbers in `docs/performance.md`,
"Columns". Memory: the working list of a balanced document is one node of 32 bytes, a height and
a unit number for each line and gap (84,000 nodes at 10,000 paragraphs), kept while the columns
stand. The estimate of the column count on real text. Columns under a real mouse. A document
with vertical stretch (none can be described).

**What stands on a golden and what on a decision alone.** On a golden, exactly: where a column
is cut and why (every candidate), the glue at its head, what is dropped at a cut, an overfull
column, what the reference's `count` columns leave over, and the commands each column ships —
under reference rules. On a decision and its own tests alone: that columns are set beside the
layout; balanced columns; a break between blocks and none after a heading; the product's
`\splittopskip`; the page (gutter, x of a column); hit testing, selection, rectangles and copy
across columns; per-column geometry and `GetDocumentColumnPos`; `DocumentView` in columns; what
is kept across an edit; the estimate of the count.

## D59. A font added here is marked, and the library knows its own by the mark; an ImGui context replaced under a kept context is the atlas emptied (task `swap-fixes`, 2026-10-11)

*Agent decisions, taken unattended on a reviewer's finding (review-5, F3); the user may overrule
any. It closes item 5 of D53, which is left as it was written. For the other session:
`docs/advtext-swap-status.md`, answer (c).*

**The defect, reproduced before anything changed** (`tests/test_api_answers.cpp`, against the tree
of `0b21807`). One ImTypeset context, an ImGui context with the seven roles' fonts,
`ImGui::DestroyContext()` alone, a second ImGui context at the first one's address:
- with the host's seven fonts each put at the address of an old role font (the test's allocator
  hands the blocks out again in order; an atlas numbers its fonts from one, so the `FontId`s agree
  too): `GetFont(role)` answered the HOST's fonts, `RemoveFont(ImTypesetFace_Regular)` answered
  true and took a host font out of the atlas, and adding the roles' fonts again removed host fonts
  (7 fonts in the atlas where 14 were expected). With the first context run for no frame the
  once-a-frame skip of `validate_fonts` was taken; with it run for two, the full look accepted
  each font by address and `FontId`. Both of D53's collisions, then.
- with the fonts where glibc put them, and exactly seven host fonts (the count the skip compares):
  `GetFont` answered dangling pointers and `RemoveFont` ran `ImFontAtlas::RemoveFont` on one
  (ImGui's `removed` assertion; a use after free without it). With zero to six host fonts the
  roles were found empty, as D53 saw. No host font landed on an old font's address in any of the
  eight runs: the reviewer's "fourth of seven" was not observed on this allocator.

**Decided: a mark in the last eight bytes of `ImFontConfig::Name`.** Seven bytes of a counter
that goes up with every font this process adds through the library, and a last byte of 0xFF. The
name ImGui shows is cut to 31 bytes and ends before it. `validate_fonts` keeps a role's font only
while the atlas holds that address (looked for before the pointer is read) with that `FontId`
and that mark, and it takes its once-a-frame skip only while the first role font there is still
stands so. Everything that hands a role's font out, draws with it or removes it goes through
that look first. A host's font has its name's terminator or padding in the last byte, so it
carries no mark whatever its address and id. `add_font` reads the mark back from the atlas and
refuses the font (`BackendUnsupported`) if a Dear ImGui did not keep it.
- *Weighed against a hook on the ImGui context's shutdown* (`ImGui::AddContextHook`, which would
  SEE the context die and let the two outcomes below be one): it is `imgui_internal.h`, and the
  library uses `imgui.h` alone (`docs/distribution.md`), which is what lets the sidecar be
  compiled against stand-in headers. Both wheels export the function; the struct it takes is
  internal and no probe row could hold it the way a field is held.
- *Weighed against the font's data pointer as the witness:* the atlas owns a copy
  (`add_face_to_atlas`), so there is no pointer of the library's to compare.
- *What it does not cover:* a host that copies the `ImFontConfig` of a font added here into a
  font of its own carries the mark with it; it would still have to stand at the address of that
  very font with its `FontId` after that font was removed.

**Decided: the outcome is the one a host gets by emptying the atlas, and it is not NoContext.**
An ImGui context made at the address of the one destroyed is taken for it (`bound()` still
compares addresses; `imgui.h` has nothing else), every role is found empty at the next look, text
reports `NoFont`, no host font is read as a role's, handed out or removed, and after the roles
are given fonts again the context and what it kept work on. Refusing with `NoContext`, as at any
other address, was weighed and not taken: the library cannot tell this case from a host that
called `ImFontAtlas::Clear()` to load its fonts again, which hello_imgui does and which must go
on working. So which of the two refusals a context left behind gives is the allocator's choice,
both are clean, and the rule a host follows is unchanged: `DestroyContext()` before
`ImGui::DestroyContext()`, or `DestroyContext()` and `CreateContext()` before adding fonts where
it cannot see the ImGui context go.

**Decided, small, same task.**
- *The demo keeps 32 states, not 8* (review-5, F8; reproduced: nine live contexts shown in turn
  for five turns held 108 handles each where 27 are one set). The leak is not bounded: the header
  has nothing to ask whether a context of some serial is alive, so a dropped state's handles
  cannot be destroyed for it. Past 32 live contexts it is as before, and Known limits says so.
  A state is 4,392 bytes; two sets of 32 are kept until exit at the most.
- *A refused `importlib.reload(imtypeset)` leaves the package's submodules in `sys.modules`*
  (review-5, F7; reproduced on the package of `0b21807`: a second `imtypeset._capi`). A refused
  first import still removes them (D53, item 8), now over a copy of the names.
- *The specimen is held to the reference in scaled points inside the library*
  (`tests/test_specimen.cpp`; review-5, F4): 33 settings of `tests/golden/specimen` by `==`, with
  every character's box, kern and glue. `tests/test_demo.cpp` keeps a display check (what the
  section prints at two decimals is what the reference's figure prints as) and says it is not
  oracle coverage. The reference's unkerned settings are held as the two letters set apart: the
  library has no switch for kerning.
- *A range that ends where a line starts copies without the space at the break.* Kept: it is the
  reference's rule for every separator (`advtext/selection.py`, `SelectionModel.text`). Pinned.

**Measured** (debug, GCC 12.2, FreeType 2.12.1, one run each): the numbers above;
`test_specimen` 2,534 checks, and it fails 33 of them when one expected width is moved by one
scaled point.

**Not measured, not run:** what the witness costs a draw (one search of the atlas's font list
for an address and eight bytes compared, per call that looks); a wheel whose Dear ImGui rewrites
`ImFontConfig::Name` (the probe row and `add_font`'s read-back are what would say so); MSVC.

## D60. In a table a point answers the cell whose rectangle holds it (task `tables-fixes`, 2026-10-11)

**Found by running** (review-6 items 1, 2, 3, 8, 9, all reproduced on `ce4d66d`). In a table of
three columns at 600 px, a point 60 % of the way across the first cell of a row answered the
SECOND cell, and the same in the last column answered the first cell of the NEXT row; a caret at
the end of a right-aligned cell was drawn at the next cell's first glyph (443.3 px where the
cell's text ends at 427.3); a caret in a wrapped row was the whole row tall; a hit in a column a
row has no cell for said `Column 1` or `2` while `GetDocumentTableCell` of its position said 0.
Over the 1,848 golden probes that fall in a table, the reference's own index is in another cell
than the point for 465, of which 253 are inside a cell's rectangle.

Why: the reference's selection model (`SelectionModel::boundary_in`) splits every item of a line
at its middle, the `\hss` that fills a cell included, and an item reaches to the next one's left
edge, across the padding and the column's edge. A row set on one line is one line. That index is
the reference's return value and is still what `SelectionModel::hit` answers, exactly: the
replay asks the model at the golden's scaled point for every probe.

**Decided: the product's rule.**
- `ImTypesetDocumentHit::Row` and `Column` are the cell whose rectangle holds the point: its
  column's edges, its row's top and bottom. They are that under either set of rules, because the
  reference has no answer for them (FORMAT-doc-tables.md §0: `tmap`'s row and column are the
  dump script's arithmetic). A point between two rows is the nearer row's and the upper one's
  when as near to both; a point beside the table is the first or last column's; a point in a
  column the row has no cell for is the row's LAST cell's.
- `Position` is a byte of that cell's own text: only the cell's own items are asked, on the line
  of the cell nearest the point, each split at its middle as the reference splits it, the last
  ending where the cell's text area does (the column's right edge less the padding, or a wrapped
  cell's line end). So the blank after left-aligned text is the text's end, the blank before
  right-aligned text its start, and the room under a short cell of a wrapped row that cell's
  last line. Under reference rules (`ImTypesetDocument::reference_rules`, which no public call
  sets) `Position` is the reference's index's, as before.
- `GetDocumentCaretRect` in a table is in the cell the position is in, on the innermost line that
  holds the position's item and as tall as that line, and the byte that ends a cell's text is the
  right edge of the last thing the text set (the glue after it, or the text area's end where
  right-aligned text has none). An empty cell's caret is where its text would begin: left, right
  or middle by the column's alignment.
- hit -> position -> caret rectangle -> hit is a fixed point, tested for every point of a grid in
  every cell of four tables (unwrapped, wrapped, `FixedLineHeight`, cut across columns).
- *Cells written with nothing between them* (`"abcdef"` as `[0,3)`, `[3,6)`): the header already
  said the shared byte is the later cell's start. Kept. So the earlier cell's last caret position
  is the start of its last item (before the `c`), which is what a point in its blank answers;
  and an empty cell that shares its byte with the next has no position of its own: a point in it
  says its Row and Column and a Position that names the next cell. Weighed against an affinity
  bit on `ImTypesetPosition`: a field in three languages for text no host that writes its table
  as advised (a tab between cells) produces.

**Replay changed, and what each assertion compares now** (`tests/test_api_tables_golden.cpp`,
`ask_probes`): (1) the model's index at the golden's scaled point `==` the reference's `hit`,
1,848 of 1,848, unchanged; (2) under reference rules the public `Position` is that index's by
the convention, 1,848 of 1,848, unchanged; (3) NEW: `Row`/`Column` `==` the cell under the point,
worked out in the test from the golden alone (`tgeom`'s left plus the reference's widths; `trow`'s
top and height), 1,848 of 1,848; (4) NEW: with the product's rule the public `Position` is a byte
of that cell, names that cell through `GetDocumentTableCell`, and names itself back, 1,848 of
1,848. REMOVED: the assertion that `Row`/`Column` are the cell of the reference's index, which
pinned the next cell for 465 probes. Counts that were lower bounds (`>= 1751`, `>= 473`) are
`==`; `trow` top/height/wrapped, `tgeom` left and a `linkarea`'s cell, which were counted, are
asserted (they are script arithmetic over the reference's own line commands and boxes, and the
test says so).

## D61. A range through a table is highlighted cell by cell (task `tables-fixes`, 2026-10-11)

**Seen** (`swap-fixes`, picture `shot-sel-table`): of two selected rows one had the blank between
its first column's text and the right-aligned second column filled and the other did not.
**Read and run:** the reference's `rects` is one rectangle a LINE from the first selected text
item to the last, over whatever lies between, and a rectangle runs to the next item's left edge.
A row set on one line is one line, so a range through three of its cells is one bar over the
blanks and the padding; a row whose cells wrapped is one rectangle a cell. Both are the
reference's (1,286 of 1,286 golden rectangles through tables equal, exactly, under its rules).

**Decided: under the product's rules the rectangles of a range in a table are made cell by cell
and kept inside each cell's text area** (`range_rects`), for `GetDocumentRangeRects`, a drawn
selection and marks placed Behind or Under. A drawn selection or mark works out only the rows
the clip rectangle shows. Under reference rules the reference's. Divergences D9.

## D62. `FixedLineHeight` on a table is per line (task `tables-fixes`, 2026-10-11)

**Found by running** (review-6 item 4, reproduced): three rows of which two wrap, 260 px,
`FixedLineHeight`: every row's box was one pitch (21.6 px) whatever its lines, row 1's top was
64.8 px where row 0's box ended at 21.6, and 73 checks of the test below failed (carets outside
their row, the last row's lines outside every box). The exact strut was applied to the ROW,
whose depth it replaced, and a wrapped row's depth is every line below its first.

**Decided: the flag means every LINE is the line height**, which is what its name and the header
say. A row set on one line is one line and is the strut exactly. A wrapped row's lines are each
already the strut (`cell_lines`), so the row takes the strut's height for its first line and
keeps the depth its cells' lines came to: rows tile, the block is its lines tall, every line is
inside its row's box. `Aligner::set_row`. The reference has no exact strut: nothing to replay.

**Decided: ink that overhangs an exact line is looked for** (review-6 item 5, reproduced: a
formula 27 px tall on a line of 8 px, a clip starting 4 px below the line's box, 0 quads where 1
reaches). A list whose lines were set to an exact height carries `LineIndex::overhang`, a bound on
how far ink stands from its baseline (two ems of the largest type in the block, plus the tallest
formula's height or depth), and a line is skipped only when its box grown by that misses the
clip. A draw also looks for such blocks four ems of the largest of them past the clip
(`ImTypesetDocument::exact_reach`). The header states both bounds. Cost: a few more lines and
blocks visited at a clip's edge, in documents that use the flag only.

## D63. Columns: what is settled before a rectangle is answered, what a column may not part, and what Overfull means (task `tables-fixes`, 2026-10-11)

Review-7's findings, each run first.

- **F1, reproduced.** 10,000 paragraphs in order, `GetDocumentCaretRect({5000, 0})` as the first
  question: a rectangle that differed from the one the cut columns give (and a cold
  `GetDocumentRangeRects` differed in all six of its rectangles). Balanced, 60 paragraphs, block
  8 replaced by a longer one: the caret of block 9 asked before the next draw was not where the
  draw put it. **Decided:** a rectangle to be put on the page is computed after the columns are
  cut past the block's unit (`DocumentColumns::cut_past_unit`, `settled_top`): every unit above
  is then laid out and the block's top is exact. In order that costs what column k always costs:
  everything before it, once.
- **F2, reproduced.** `GetDocumentSize().y` was the columns' height while a host block of 900 px
  stood in a 300 px column. **Decided: the page is as tall as the tallest Overfull column cut so
  far holds** (`DocumentColumns::reach`), so a view that scrolls over the page reaches it.
  Weighed against scrolling the columns sideways inside the view to it: there is nowhere
  sideways to go, the content is below. A column that is not overfull is not counted, though its
  last line's depth hangs below its foot: counting it would give every columned view a vertical
  scrollbar for a descender.
- **F3, reproduced.** Three one-line paragraphs balanced into three: all three `Overfull`.
  **Decided: no column is Overfull under BALANCED.** The page is as tall as the tallest column,
  so nothing reaches past it; the height the search cut at (one scaled point when the document
  has no more pieces than columns) says nothing about that. The search itself is unchanged: a
  piece that cannot be cut still gets a column and the rest are balanced among themselves.
  `tests/test_columns.cpp` asserted `overfull` for the column holding such a piece; it now
  asserts that none is and that `used`, `height()` and `reach()` are that piece's height.
- **F4, the premise was half wrong.** An empty paragraph DOES set a line (a blank one, 21.6 px);
  the heading was kept with that line and the cut fell after it: heading, blank, cut. Reproduced
  as that. **Decided: a text block with nothing in it is transparent to "kept with what
  follows"** (`Document::unit_vlist`): the heading is kept with the next block that has
  something in it.
- **F6, reproduced at the block layer** (a rule two lines thick taking the index of a two-line
  paragraph a column was cut inside: the kept cut fell inside the rule). **Decided:** a splice
  says from which unit the document changed, and "the unit that stood here" also compares the
  serial of the layout it had.
- **F8 and the table cut across columns, which nobody had tested.** Run: a wrapped row at a
  column's foot hung below it by all its lines but the first with no `Overfull`, with or without
  `FixedLineHeight` (a wrapped row is a box one line high and the rest deep, and the reference's
  `\splitmaxdepth` is `\maxdimen`, as plain TeX's is); and a header row was left at a column's
  foot with its rule at the head of the next (seen by `swap-fixes`, picture `c2-end`; 13 of 91
  column heights round the header's foot). **Decided, two product rules:**
  `ColumnRules::split_max_depth` is the depth of a line of the document's style (tex.web §971
  then counts what is deeper as height: a column holds the whole of its last row or does not take
  it), and `DocumentRules::keep_table_header` (rule 17: the glue above a header's rule and above
  the row under it is a kern in the list the columns are cut from). Both off under reference
  rules; divergences C9, C10.
- **F9.** `SetDocumentColumns` gave up the height a view lent on every call. **Decided:** only
  when what is said changes.
- **F5, F7, F10: the header was wrong or silent; the code stands.** A point below a column's last
  line is a point ON that line at the same x, not "that line's end" (F7). `GetDocumentColumnPos`
  carries a point; a rectangle is carried by its top-left corner and its size, and the header now
  says so and says what a float of pixels can hold (F5). A hit test costs the columns up to the
  one the point is in; the header says so (F10). An explicit rectangle-mapping call was weighed
  and not added: it is two lines for a host and a declaration in three languages.

**Not decided here, for the user** (docs/advtext-swap-status.md, "Border questions"): widows and
orphans at a cut, and a common first baseline across columns.

## D64. A table that gains a row costs that row (task `tables-fixes`, 2026-10-11)

**Measured before** (`tables-core`'s own figure, and review-6 item 7): about 0.2 us for each row
the table has, each append: 2.1 ms at 10,000 rows, and by that figure some 430 s to append 65,536
rows one at a time. Nothing was typeset for the rows that stood; five passes over the whole table
were made all the same.

**Decided: `TableCells::epoch`.** Between two clears a table's cells only grow at their end (a
cell joins the last row, the last cell is made again, a row is started), so every row but the
last is final. What was worked out over those rows is kept under the epoch and only the rows
closed since are added: the widest cell of each column, the longest words when a solve needs
them, which kept rows stand (the last is the only one compared), the widths held, and beside the
layout the line index, the colours and the index-to-byte map, which are brought up from the first
row set again when the layout went on from the one they were made for (`sync_grown_table`).
`TableCells::columns()` is no longer a pass either.

**Measured after** (release, GCC 12.2 `-O3`, one run, the machine NOT quiet: load average 3.0):
an append to a held table of 10, 100, 1,000 and 10,000 rows: 6.0, 4.3, 4.5, 4.3 us (median of
200); 65,536 rows appended one at a time, the table laid out after each: 0.39 s in all, 5.0 us an
append over the first 4,096 and 5.0 over the last. By the counters, which do not depend on the
machine (`a_row_appended_goes_over_that_row_and_not_the_table`, 3,000 appends): one row set, at
most 3 rows gone over by layout (5 in a table fitted to 500 px), one row's lines and index map
made, an append; held to the unit by the test.

**Held:** a table grown a row at a time, with text arriving in its last cell, has beside its
layout exactly what the same table described whole has (line spans, colours, index bytes) and
answers every question alike, at six sizes, held and fitted
(`a_table_grown_a_row_at_a_time_is_the_table_described_whole`).

**Not done:** a row that widens a column still sets every row again (it must); a view of the
block (`view_of`, taken for a hit, a caret, a selection or a mark in the table) is still a copy
of all of its commands, once per layout looked at that way; a REPLACE of a held table still sets
every row. Timings on a quiet machine were not taken.

## D65. What the gate of 2026-10-11 changed, and what it found and left (task `swap-gate`, 2026-10-11)

**Changed, one defect.** `Document::splice` and `Document::add_block` (`src/blocks.cpp`) grew the
blocks, their layouts and their slots in one loop keyed on the blocks' length. In a build with
exceptions on, a `bad_alloc` from the second or third growth left the blocks longer than the
others, and the next call wrote a slot past the end of its array. Each sequence is now grown by
its own length. *Measured:* found by a new sweep of `tests/test_exception_safety.cpp` (a table,
columns in order and balanced) in the `exceptions` configuration under ASan, which reported a
heap-buffer-overflow in `reset_block` on its first run and nothing after the change
(`docs/hardening.md`, X6). *Read:* the defect is older than tables and columns and independent of
both; the sweeps that existed described their documents before arming the allocator, so none
reached it.

**Decided, three things left as they are.**

1. `test_exception_safety` reports itself skipped in the `bundle-config` preset, and always has:
   it catches the exception type `tests/imconfig_throwing.h` throws, and that preset's
   `IM_ASSERT` is Dear ImGui Bundle's, which throws `std::runtime_error`. `docs/distribution.md`
   said the test did its work in both; the sentence is corrected and the preset is not changed.
   Making the test work there means catching by a type both configurations share, which is a
   change to the two tests that catch it, for a configuration whose allocator sweeps would then run
   without ASan.
2. `src/imtypeset_demo.cpp` places the second letter of a kerning pair with
   `(float)(int)(left.x + 0.5f)`, the spelling D18 forbids. It is the demo standing in for a
   host that rounds a positive width to a pixel as `IM_ROUND` does, nothing in layout reads it,
   and changing it can move a drawn letter by a pixel at a tie. Reported, not changed.
3. The pptmstr patch (`python/spike_bundle/pptmstr_overlay`) was not regenerated: against a
   library built from this tree, with the longer `Hit` and `DocumentHit` and the renamed `Block`
   fields, the overlay's proof run and its twelve fault scenarios give what they gave at the
   release gate.

**Not measured:** `AppendDocumentTableCells`, `AppendDocumentText` into a cell,
`SetDocumentTableColumns` and `DocumentView` over columns under a throwing allocator; the same
sweep under anything but GCC 12 with ASan.

## D66. A role's font metrics are a query with the struct's size in the call (task `handoff-core`, 2026-10-11)

*Agent decisions, taken unattended for advtext's request H7 (`../imgui-adv-text/HANDOFF-imtypeset.md`);
the user may overrule any.*

**Read first, in advtext:** what its last uses of its own font reader ask. `Theme.at_pixel_height`
asks a face at a probe size for `ascent`, `descent` and `em` (the ratio that turns ImGui's size
into an em), and for the advances of the ten digits and the period (`_widest_marker_em`).
`code_size(pixel_height=)` asks `em_size_for_pixel_height(face, h)`, the same ratio. Nothing asks
for a kern, a glyph box or a name.

**Decided.**

1. **`GetFontMetrics(face, em_size, ImTypesetFontMetrics* out, size_t out_size)`** and
   **`GetGlyphAdvance(face, em_size, codepoint)`**; `ImTypesetFontMetrics {EmSize, FontSize,
   Ascent, Descent, LineGap, XHeight, SpaceAdvance}`, seven floats. C: `ImTypeset_GetFontMetrics`,
   `ImTypeset_GetGlyphAdvance`, `ImTypeset_FontMetrics`. Python: `font_metrics`, `glyph_advance`,
   `FontMetrics`. Two calls and not one struct with advances in it: which characters a host wants
   an advance for is the host's (advtext wants eleven), and a struct cannot hold "any".
2. **The size is the em, in pixels**, the library's own unit of type (`ImTypesetStyle::EmSize`).
   `FontSize` in the answer is what ImGui calls the size of that em (ascent plus descent), so the
   conversion advtext needs is `h * EmSize / FontSize` from the metrics at any size. No third
   call for it: it is one line for a host, and the library's own conversion
   (`FontLibrary::em_size_for_pixel_height`, used for `ImTypesetStyle::FontSize`) is the same
   ratio.
3. **Floats, as every length of the header is.** The doubles are the reference's own to the last
   bit (one scale `size / unitsPerEm`, one product a length: `fontlib.py`'s order) and each field is
   that double as a float.
4. **From the role's file alone, not through the metrics cache.** The numbers are computed from
   `FaceData` at the call. `FontLibrary::metrics()` would have answered the same doubles, but it
   keeps an object a size and expires old ones (*read*, `src/fontlib.h`), and a host dragging a
   size slider through this query would then age out sizes layout is holding.
5. **hhea, and it is said.** `Ascent` and `Descent` are the hhea table's, as layout's and the
   atlas's are (`docs/font-support.md`, "Where the product departs from the reference on fonts";
   `the_atlas_sizes_a_font_by_hhea_whatever_os2_says`). `LineGap` is hhea's too and nothing in
   layout reads it: baselines are `ImTypesetStyle::LineHeight` ems apart.
6. **`out_size`, and no addition to `IMTYPESET_CHECKVERSION()`.** The struct's size is an argument
   (defaulted in C++, `sizeof` in C and in the package): a caller compiled against another header
   is refused with `ImTypesetError_VersionMismatch` and not written past. Weighed against a fourth
   `DebugCheck...DataLayout` call with a `Sizeof` field in `ImTypeset_BuildInfo`, which is how
   tables and columns did it: that makes a library and a package from either side of this change
   refuse each other at import for a query most hosts never make, and it changes
   `ImTypeset_BuildInfo` while two other tasks are writing the sidecar's gate. Here only the call
   itself can fail.
7. **Refused with a reason, not clamped:** a size that is not finite, not above zero, or above
   1,000,000 px is `InvalidArgument`. Layout clamps an em to 1 to 1,024 px; a metrics query is
   linear in the size and has no reason to answer for another size than it was asked.

**Measured** (`tests/test_api_handoff.cpp`, debug, FreeType 2.12.1, GCC 12.2):
`font_metrics_and_advances_are_the_references_as_floats` replays `tests/golden/font/*.answers.golden`
through the public calls: 42 (role, size) answers of six fields each and 20,508 advances equal to
the reference's doubles as floats, exactly, for the seven roles. 7 (role, size) pairs are passed
over and counted: the reference was asked at 14.4, which no float holds. `LineGap` is not among
the reference's answers; it is held to the raw goldens' `HHEA` record by this file's own formula.
`the_em_for_an_imgui_size_follows_from_the_metrics_at_any_size`: a paragraph sized by
`FontSize = h` and one sized by `EmSize = h * EmSize / FontSize` break into the same lines at 13,
16, 19 and 24 px.

**Not measured:** a font with a non-zero hhea line gap other than the bundled math face; a
variable font; the query under the 16-bit `ImWchar` build beyond the suite running there.

## D67. The table header's rule: the reference's height, measured again (task `handoff-core`, 2026-10-11)

advtext's request H8 says every table with a header is 20.64 px taller than the reference's.
`tables-fixes` said the height is the reference's own (`docs/divergences.md`, T20). Both numbers
are right and they are numbers of different things.

**Measured** (`build/handoff-core-scratch/h8/ref_tables.py`, the pinned reference at `32e3953`
through its own interpreter, `demo/tables.md` at 684 px, 16 px body, in scaled points):

| table | as the reference stacks it (`ui._stacked`, and its views' units) | sum of the heights of `ui._table_boxes`' boxes | difference |
|---|---|---|---|
| 0 | 6,393,984 (129.6 px) | 5,375,683 (108.96 px) | 1,018,301 |
| 1 | 13,853,632 (280.8 px) | 12,835,331 (260.16 px) | 1,018,301 |
| 2 | 7,459,648 (151.2 px) | 6,441,347 (130.56 px) | 1,018,301 |
| 3 | 20,247,616 (410.4 px) | 19,229,315 (389.76 px) | 1,018,301 |
| 4 | 8,525,312 (172.8 px) | 7,507,011 (152.16 px) | 1,018,301 |

The first column is what advtext measured from the library; the second is what its
`compare_tables.py` took for "the old core" (`height_old = sum(b.height + b.depth for b in boxes)`,
*read*). The reference never draws that sum: `_stacked` appends each box with
`append_to_vlist`, and the rule (47,363 sp high) gets a `\baselineskip` like a line. The
difference is one `\baselineskip` less the rule: 1,065,664 - 47,363 = 1,018,301 sp = 20.64 px.

**Through the public calls at this tree:** the replay of `tests/golden/doc-tables` (demo/tables.md
at 360 and 660 px among its cases) holds every block's top and height
(`GetDocumentBlockGeometry`) and every row's (`GetDocumentTableRow`) to the reference's `bspan`
and `trow` records under reference rules; and
`a_table_with_a_header_is_a_leading_taller_than_its_rows_under_either_set_of_rules` holds the
first table at 684 px to 6,393,984 sp under reference rules and under product rules alike.
`the_rule_under_a_header_takes_the_same_height_whatever_its_thickness`: 0.96, 2 and 5 px rules
give one height; no rule gives five leadings.

**Decided: nothing changed.** The product is not taller than the reference. What advtext's view
drew before the swap was the first column too. Whether the space under a header SHOULD be a whole
leading is the border question already written for the user (`docs/advtext-swap-status.md`); the
header now says at `ImTypesetBlockFlags_TableHeader` what the height is.

## D68. A row that arrives in pieces: described without its unwritten cells; no new call (task `handoff-core`, 2026-10-11)

advtext's request H9: a 200-row table streamed 7 characters a piece cost 468 replaces and 146,304
cell measurements, because its parser pads the unfinished row with empty cells and the cell being
written is then not the last.

**Decided: advice, a Python method over the call that exists, and no new C or C++ declaration.**
A row may have fewer cells than the table has columns, so the unfinished row is described with the
cells it has. The cell being written is then the table's last: `AppendDocumentText` grows it, and
`AppendDocumentTableCells` with a first cell that is not a row's start adds the next cell to the
row. Both existed; what did not was a way to say the second from Python
(`Document.append_table_rows` starts a row), so the package gains
`Document.append_table_cells(cells)`.

**Declined: appending text to a named cell (row, column).** Text in the middle of a block moves
every byte after it: the cells, the runs, the positions a host holds, the index-to-byte maps and
the kept rows of every later cell. `TableCells::epoch` (D64: between two clears a table's cells
change only at their end) is what makes an append cost a row, and a write into the middle is the
one edit it rules out. **The invariant is kept, knowingly.** What it costs a host: markup that
closes late in the row being written (`**bo` becoming bold) changes text that was already handed
over, and that piece is a replace of the table, as before. A "replace the last row" call would
cover it within the invariant (the last row is the open one); not built, because nothing measured
asks for it yet.

**Measured** (`a_row_arriving_in_pieces_costs_its_cells_when_described_without_the_unwritten_ones`,
by the library's counters, which do not depend on the machine; 200 rows of three cells under a
header, held, 7 bytes of source a piece, laid out after each piece):

| the unfinished row | appends | replaces | cells measured | rows set |
|---|---|---|---|---|
| padded with empty cells | 370 | 944 | 288,914 | 96,418 |
| the cells it has | 1,512 | 0 | 1,513 | 1,410 |
| (the table given whole) | | | 603 | 201 |

The three tables end equal: rows, columns, cells, every column's edges, sampled rows' tops and
bottoms, and the copied text. The figures are this test's table, not advtext's: its 468 and
146,304 were for its own text.

## D69. Marks are looked for where the clip is; a glyph's quad is emitted when part of it is inside (task `handoff-core`, 2026-10-11)

**Marks (H10).** `draw_block` worked out every mark of a draw against every block it drew, and
put each Ink mark over the block's colours by copying them: 1,881 marks on one block were 1,881
copies of a growing list a frame. **Decided:** the marks of a draw are kept in the order they
start in the document (`MarkOrder`: used as given when they come in that order, sorted once
otherwise, and made again only when a range handed over differs from the ranges it was made
from, which a draw checks with one comparison a mark); a block's marks are found by bisection;
and when a block has more than 16, only those on bytes the lines within reach of the clip hold
are worked out (`bytes_shown`: the lines by bisection, their items' indices by a pass over the
commands those lines have, the bytes from the block's index-to-byte map when it is in order). No
precondition on the host: any order, overlapping or not, draws what it drew. A later mark is
still put over an earlier one in the order GIVEN.

*Measured* (`bench/bench_marks.cpp`, release `-O3`, GCC 12.2, the machine NOT quiet: load average
4 to 5; medians of 2,000 warm frames; a code block of 600 lines, a window of 768 px, 1,881 marks):

| a draw | before (`caaeab3`) | after |
|---|---|---|
| with no marks | 54 us | 45 us |
| every mark, Ink, in text order | 2,766 us | 83 us |
| every mark, Ink, shuffled | 8,914 us | 119 us |
| every mark, Behind, in text order | 710 us | 75 us |

The two "no marks" rows are the same code and say how far a run strays. *Held:*
`marks_culled_to_the_clip_draw_what_every_mark_worked_out_draws` (3,000 and 40 marks, sorted and
not, in the three places, some across blocks, windows of 40 and 300 px every 173 px down a document
of six blocks: the quads inside the window equal, vertex for vertex, to a draw with the culling
off; 15,302 marks looked at where working out every mark of a shown block is 96,095) and
`the_order_of_a_mark_set_is_made_once_and_again_only_when_a_range_changes`. *The test's limit:*
the reach a draw looks past the clip is generous (four ems where a document has a block of exact
lines), so a culling window wrong by a line would not fail it; one wrong by twelve reaches fails
326 checks (tried).

**One quad at a clip edge (H11).** Reproduced in
`a_glyph_quad_is_emitted_exactly_when_part_of_it_is_inside_the_clip_rectangle` (64 failed checks
before the change): U+2502 in the bundled Inconsolata has a bitmap that starts one pixel above
its font's cell, and with the clip's bottom edge on the row's top its quad, one row inside, was
not emitted. *Cause:* `Drawer::emit` and `ImGuiBinder::load` kept `ImFont::RenderText`'s test on
the font's cell (the pen below the bottom edge, or the cell above the top edge) in front of the
product's test on the quad, so `quads_off_clip_dropped` could only drop quads the cell test had
kept, never keep one it had dropped. (*Inferred, not run:* advtext's U+4E2D was emitted in the same
frame because it is set from another face, whose cell starts higher; here no bundled face has it
and it is set as U+FFFD.) **Decided:** under `quads_off_clip_dropped` the quad's
own top and bottom are the whole test, and every glyph placed is loaded (its box is not known
before): with lines culled those are the glyphs of the lines at the clip. The reference's rule is
untouched and still what the goldens replay. *Looked at:*
`.concerns/handoff-core--builder--shot-h11-{before,after}-clip60.png`, the last pixel row inside
a clip ending on the row's top: empty before, the top of the bar and of the integral sign after
(7 pixels lit).

## D70. What a binding can hand over without ending the process, and the demo at other sizes (task `handoff-core`, 2026-10-11)

**From the audit of the Python package (H12; `docs/python-errors.md`).** Three inputs ended the
process and are now answered by the library: `DocumentView` with an empty `str_id` (ImGui asserts
on a child with no ID; null was already the view's own ID, and empty now is too), and
`ShowDemoWindow` and `ShowDemoSection` outside a frame (they draw nothing before the first frame,
after `Render()` and with no ImGui context). *Reproduced by the audit in child processes against
the debug library; the C++ tests that hold them were written after the change and were not seen
to fail.* **Left:** a context address that is not a context, or destroyed twice, and a handle of
the wrong kind, each of which needs the library to keep a list of what it has made; and the state
between `EndFrame()` and `NewFrame()` with no `Render()` (D56).

**In the package,** by the audit's helper: every non-`Error` exception a public call can raise on
host input is now `TypeError`, `ValueError` or `OverflowError`; indices, counts and enums past a C
int are clamped where ctypes kept their low 32 bits (a block `2**32 + 1` meant block 1); a `str`
where an address goes is `TypeError` where it was a segmentation fault. A run's `tag` past 32 bits
keeps its low 32 bits in a document, as it already did in a paragraph.

**The demo at other sizes (H13).** Looked at, 11 sections at 7 sizes in a window of 1100 px. Two
things changed in `src/imtypeset_demo.cpp`: lines of counters wrap at the window's edge where they
were cut, and the two formula columns of Math's table are as wide as their widest formula where
they were a share of the window, in which formulas ran under each other from 24 px and were cut
from 16. The numbers are in the header at `ShowDemoSection`. Nothing new is exposed.
