# The design note, section by section, against what was found

imtypeset started from a design note in the Python prototype's repository,
`../imgui-adv-text/docs/cpp-extension.md` ("A C++ ImGui extension: notes",
revised 2026-10-08, 464 lines). This file walks that note in its own order and
says, for each claim that mattered, which of three things happened to it:

- **established** — built or measured here, with the file or command that shows it;
- **refuted** — found false, or overtaken by a decision that went the other way;
- **open** — not settled here, with what would settle it.

A claim this tree cannot check (it is about the prototype's history, another
machine or another project) is marked **not checkable here** and left as the
note states it. Every number below names the file it is in or the command that
prints it. Numbers marked *(run for this file)* were taken on 2026-10-10 from a
clean release build of the tree as it then stood; the rest are quoted from the
document named, whose author ran them, and were not re-run.

Section numbers (`§`) are the note's. `Dn` is an entry of `docs/decisions.md`.

## Summary

| § | the note | status |
|---|---|---|
| 1 | an ImGui-only extension in `implot`'s shape, sitting on `imgui_internal.h` | the shape established; the dependence on `imgui_internal.h` **refuted** (not needed); the narrow scope overtaken (D32, D34) |
| 1 | "`snap_px` matches `IM_ROUND`" | **refuted** (D18) |
| 1 | the active rasteriser is FreeType, not stb_truetype | established for the wheels; this tree runs under both |
| 2 | a custom `ImFontLoader` is the capability that decides it, "structurally unreachable from Python" | **refuted** for the font path; no loader was built (D27) |
| 2 | the on-disk font rewriting can die | established: rewritten in memory |
| 3 | performance is real and not the reason | the measurements established and bettered; "not the reason" overtaken: speed is what the two applications came for |
| 4 | return vertices, not commands; a Python host must have the extension write the draw list | established, both halves, with one correction (a bulk route exists) |
| 4 | where the port stops is not decided | decided: all of layout is native (D1, D20, D32, D34, D37, D43) |
| 5 | a subset OpenType reader of 1,000 to 1,500 lines, not HarfBuzz | built; the size was an underestimate by about four times |
| 6 | the bundle vendors it and generates bindings with litgen | **open** (backlog); a different route into the bundle's wheel is established |
| 7 | build the differential harness first | established |
| 8 | ImGui font-API churn is the permanent risk | avoided on the font path (D27); 1.93 WIP builds and passes in debug |
| 9 | staging 0 to 3 | done in another order, with step 2's loader replaced |
| 10 | the name `imtypeset` | taken; still **not checked** against existing projects |
| 11 | seven open items | three answered here, one moot, three open |

---

## §1. The scope

**"The `implot` shape: C++ over ImGui, then bindings."** Established. One
public header (`include/imtypeset.h`, 72 exported functions by
`grep -c '^IMTYPESET_API ' include/imtypeset.h`), a C ABI over it
(`include/imtypeset_c.h`, checked declaration by declaration by
`tests/test_c_api_map.cpp`), a `ctypes` package over that (`python/imtypeset`).

**"Not over `imgui.h` alone: the font loader is declared in
`imgui_internal.h`."** Refuted. The library includes `imgui.h` only
(`grep -rn imgui_internal.h src/ include/` finds two lines, both comments: the
public header saying so, and `src/font_loader.h` on why no loader was built). It does use four members of `ImDrawList` that
`imgui.h` itself marks `[Internal]` (`_VtxWritePtr`, `_IdxWritePtr`,
`_VtxCurrentIdx`, `_CmdHeader.ClipRect` read-only; `docs/distribution.md`
section 6). Going through the public `PrimWriteVtx`/`PrimWriteIdx` instead was
measured at 3 % slower and through `PrimRectUV` at 15 % (`docs/decisions.md`
D39 item 8, `docs/performance.md` "X3").

**"Not a general-purpose text engine"; the narrow reading.** Overtaken by
decision. D1 put the whole layout core on the native side; D20 added paragraph
assembly; D32 and D34 made imtypeset the product and the Python library the
prototype it grew from; D37 and D43 added the block layer and a public
`Document`; D57 put tables in the core as a block of a `Document` (commit
`be40444`) and D58 columns as a way a `Document` is shown (commit `0b21807`),
on the ruling that both are TeX's. What the note called "the wide reading"
(the views) is still not here, and neither are markdown or diff.

**The device-space decisions "carry across intact".** Established for
`reference()` rules, which is what every golden replay runs under:
`tests/test_draw.cpp` holds the port's draw lists equal to the reference's
(the draw census in `docs/distribution.md` section 6: 0 of 583 glyph boxes
differ, 22 of 22 cases equal its buffers outright). Not intact in the public
API: three of the reference's screen rules were found wanting and are switches
(`DrawRules::product()`: D47 a formula placed whole, D48 words held and spaces
led, D50 the joints between them), and nine of its departures from `tex.web`
likewise (`TexRules::product()`, D41). `docs/divergences.md` lists each.

**"`snap_px` matches `IM_ROUND` so we agree with ImGui about which pixel a
coordinate is on."** Refuted. `snap_px` floors; `IM_ROUND` truncates and is
marked positive-only. They differ for every negative non-integer (D18). The
port uses its own helpers (`src/units.h`), never `std::round` or `lround`.

**"`baked_size_px` exists because ImGui sizes a face by pixel height and not by
its em."** Established and carried: the size correction is ported and the draw
goldens depend on it.

**"The rasteriser this build uses is FreeType (2.13.3) and not stb_truetype."**
Established for the wheels: the bundle's loader reported FreeType 2.13.3 in
`docs/bindings-notes.md` section 6A. This tree is built and tested under both
(presets `debug` with FreeType where installed, `stb` without; D7), with the
system's FreeType **2.12.1**, one minor version behind the reference's. D7
records that the glyph boxes probed did not move between the two versions and
which cases were not probed. **Open:** glyph boxes under the wheels' 2.13.3
against the goldens' 2.12.1 across the whole draw golden set
(`.concerns/bundle-route--builder--result.md`, "Not run").

## §2. The finding that decides it

**"A capability that is structurally unreachable from Python": a custom
`ImFontLoader`.** Refuted for the font path, in two steps.

1. No custom `ImFontLoader` was built (D27). At v1.92.9 `FontBakedLoadGlyph`
   is entered with a codepoint and both built-in loaders resolve it themselves,
   so a custom loader could not delegate: it would be a second copy of each
   built-in loader. Instead each face reaches the atlas through
   `AddFontFromMemoryTTF` from a copy whose `cmap` is rewritten **in memory**
   (`src/font_loader.cpp`, `build_atlas_font`). ImGui's own loader then runs
   unchanged.
2. Those bytes reach the unmodified wheel from Python.
   `docs/bindings-notes.md` section 6A: `ImFontConfig.font_data` accepts a
   `PyCapsule` named `nb_handle`; the font this library rebuilds, handed over
   that way, had 227 of 227 minted codepoints found and inked under the
   bundle's FreeType, against 0 of 227 for the original file.

What stays out of Python's reach is producing those bytes without native code
or fontTools, and everything after the font.

**The struct as quoted, and "the loader is asked for a codepoint, not a glyph
index".** Established by the note's own reading and relied on by D27. Not
re-read for this file.

**"What it would delete": the on-disk rewriting, the temp directory, the
degradation path, the second of cost.** Established by another mechanism. The
rewrite is in memory, needs no directory and always emits a format-12 subtable.
Cost, `docs/performance.md` headline: the reference's cmap rewrite 790 to
808 ms on this machine (the note records "about a second"); here 0.24 ms of a
6.1 ms setup for the math face, 9.6 ms for all six faces.

**"What does not go away: the minting of synthetic codepoints."** Established.
Layout names a math variant by a minted codepoint in a 32-bit type of its own;
with a 16-bit `ImWchar` the atlas is asked through private-use stand-ins (D15).
The note's "a full port could carry a glyph id and drop the minting" was not
done: the goldens name glyphs by codepoint.

**"The hinting half is already reachable from Python" (`font_loader_flags`).**
Not checkable here beyond the note's own table; nothing in this tree sets
loader flags. See §11.

## §3. Performance

**The note's figures** (26 ms a frame before the memo; 15.9 and 13.7 ms full
draw after; 5.9 and 5.1 ms culled; ImGui's share 0.7 to 1.3 ms) are another
machine's. **Established as an order of magnitude** by running the reference
on this machine on the same text (`docs/performance.md`, "Headline"): full draw
12.76 to 12.87 ms, culled 5.57 to 5.79 ms, ImGui's share 1.13 to 1.17 ms.

**"Performance is real."** Established, by more than the note's argument
needed. Same table, same text (3,918 glyphs, 74 lines), same machine:

| | reference | here | ratio |
|---|---|---|---|
| full draw, per frame | 12.76 to 12.87 ms | 0.100 ms | about 130x |
| culled draw, 768 px window | 5.57 to 5.79 ms | 0.039 to 0.043 ms | about 140x |
| reflow of the whole document at a new width | 101.6 to 103.3 ms | 0.465 ms | about 220x |
| font setup, six faces | 1.25 to 1.30 s | 9.6 ms | about 130x |

Since that table the public draw has paid for better word spacing: 110.8 to
122.4 us for the same full draw (`docs/performance.md`, "After `word-gaps`").

**"…and not the reason."** Overtaken. The note rests the case on the font
loader (§2), which fell away, and treats speed as secondary. The two
applications the library is now wanted in are after the speed: pptmstr's RICH
pane 2,514 us today against 516 us through one `Document`, advtext's `BareView`
7,026 us against 167 us (`docs/advtext-rework-plan.md`; Xvfb, a loaded machine:
ratios, not microseconds).

**"Cost and complexity live in different places … the case for native code is
concentrated in the least risky part."** Half established. The draw loop was
the cheap part to port and the largest per-frame gain. But the note's own
exception (streaming puts layout on the per-frame path) turned out to be the
rule for the applications: the reference's `StreamView` costs 11,058 us a frame
against 413 us through a `Document` (same plan), and that needed the whole
layout core native, not `draw_runs`.

**"A find at 10 to 22 ms a keystroke, and a 50,000-line diff at seconds."**
Not addressed: there is no find and no diff here. The plan ranks them as costs
the typesetter does not remove.

## §4. Architecture

**"Return vertex data, not a command list."** Established.
`ImTypeset::DrawParagraph` writes quads into an `ImDrawList`;
`DrawParagraphToBuffers` and `DrawDocumentToBuffers` fill vertex and index
buffers a host owns. Nothing public returns a command list.

**"From Python the copy is not one copy … the extension has to write into the
`ImDrawList` itself."** Established, with one correction. The sidecar library
does write into the wheel's draw list directly. And a bulk route does exist in
the wheel's Python API (`prim_reserve`, then `memmove` to the vector's data
address, then setting `_vtx_current_idx`), measured for one paragraph of 666
glyphs by `tools/bundle_route/measure_buffer_route.py`: direct draw 19 to
34 us, bulk hand-over 57 to 65 us with numpy and 212 to 277 us without, one
binding call per vertex 1.8 to 2.3 ms
(`.concerns/bundle-route--builder--result.md`). It is a draw-only fallback: a
library with a private ImGui has a private atlas.

**"A litgen binding allows it and a plain C ABI to a non-C++ host does not."**
Refuted as stated: the plain C ABI, loaded by `ctypes` beside the wheel and
resolving the wheel's own ImGui symbols, does it. Run for this file in both
wheels: `examples/python/bundle_host.py --frames 30` exits 0 with
`selection: (74, 284) layouts: 2`. The qualification is large and is in
`docs/distribution.md` section 1: stand-in headers, a field probe in place of
ImGui's own layout check, one library per interpreter, Linux only.

**"The input is no longer just a command list" (inks, fills, highlights, a
palette; one origin and one viewport).** Established: `Drawer::draw_runs` takes
them, and the public draw calls take per-run colours, fills, marks and a clip
rectangle.

**"Where the port stops is not decided. Choose before step 1."** Decided, and
moved twice: D1 (the layout core is native, the views are not), D20, then D32
and D34 (the product), D37 and D43 (blocks and the Document). The harness
compares everything on the native side: `tests/golden/` has 157 files
(`tests/golden/MANIFEST.sha256`).

## §5. Dependencies

**"A subset reader, not HarfBuzz."** Established: no HarfBuzz, no fontTools
equivalent linked, FreeType optional (D3, D7, D9). The reader is this tree's
own (`src/opentype.*`).

**"Roughly 1,000 to 1,500 lines … should be read as a guess."** Refuted as an
estimate. `wc -l`: `src/opentype.cpp` 3,868 and `src/opentype.h` 545, with
`src/fontlib.*` 876 and `src/font_loader.*` 430 on top: 5,719 lines for the
font stack. Most of the difference is the next item.

**"The font set is no longer closed."** Established and acted on (D25, D35,
D38): the reader is for the fonts a host brings. Over every font installed on
one Debian 12 machine, 2,991 faces, 2,990 are read with every compared value
equal to the reference's and 1 is refused with a reason
(`docs/font-support.md`, "Census"). That took TrueType collections, CID-keyed
CFF, legacy `kern` and a strict pass that refuses what would crash a
rasteriser, none of which the note's subset had.

**"Some of the reader may not need writing": `imstb_truetype.h`'s GPOS and
glyph boxes.** Not taken, so the question is moot: layout needs the same
answers whichever rasteriser the host's ImGui uses, and the goldens need ink
boxes equal to fontTools' to the last bit. Whether stb's readers would have
been enough was never measured.

**"Math support is what constrains the font stack."** Established: the `MATH`
table reader is this tree's own, and no rasteriser is asked for it.

**`markdown-it-py` and `difflib` stay in Python.** Established: neither is
replaced here.

## §6. The ecosystem

**`imgui_md`: "no justification and no paragraph-level line breaking"; math
through a vendored MicroTeX as textures.** Not checkable here; neither was
looked at for this project (D24 forbids writing from another implementation,
and nothing was).

**"The distribution path is the `implot` one: the extension lands in the
bundle and the Python demo consumes it back."** Open, and no longer the plan of
record. `bindings/imgui_bundle/` holds an outline of that binding which has
never been built (it needs the network and a full bundle build). What is
established instead is a sidecar library inside the stock wheel
(`docs/distribution.md` section 1), because the two applications run on wheels
from PyPI and cannot wait for upstream.

**"The public header wants to stay narrow and POD-ish."** Established and
tested: `tests/test_header_pod.cpp` and `tests/test_api.cpp` scan the header
for standard-library names; structs are mirrored field for field in C with
`static_assert`s.

## §7. What makes a port cheaper

**"A differential harness … Build that first, not last."** Established. The
oracle scripts (`tools/oracle/dump_*.py`) were written before the modules they
check, and the tests compare exactly: integers equal, doubles equal to the last
bit, no tolerance (D10, D22 as amended by D28). 35 `tests/test_*.cpp` files; 41
ctest entries in `release`, all passing *(run for this file)*.

**"Replaces most of the re-testing."** True of what the goldens cover and not
of what the product added. Everything under `product()` rules has no reference
to be compared with and is held by tests that state a bound
(`tests/test_word_gaps.cpp`, `tests/test_tex_rules.cpp`, the `d7_` tests of
`tests/test_harden.cpp`) and by hand-worked cases; the TeX-side fixes were
checked by hand against `tex.web`, not against a TeX engine (D41).

**The line counts of the prototype** (44,179 lines, 54,837 of tests) are not
checkable here and were not counted.

## §8. The risk

**"ImGui font-API churn … a permanent maintenance cost."** Avoided on the font
path: with no custom loader (D27) nothing here sits on `ImFontLoader`, the
struct that changed between 1.92.9 and 1.93 WIP. Established by building:
Dear ImGui 1.93.0 WIP (19298) from `../imgui` compiles with no warning and
passes the suite in debug, 41 of 41 (`docs/distribution.md` sections 6 and 7;
not re-run for this file). The floor is 1.92.0 for another reason the note
names: 1.91.9 has no dynamic font atlas and does not compile (22 errors, same
source).

What exposure remains: the four `[Internal]` members of `ImDrawList` above, and
for the Python sidecar every ImGui struct field the library touches, which a
wheel upgrade can move (the import gate re-probes them).

**"The FreeType loader reads neither `oversample_h` nor `pixel_snap_h`."** Read
by the note's author, not re-read here. Related and established: under
stb_truetype `add_face_to_atlas` sets `OversampleH = 1` when the caller leaves
it unset, because stb otherwise bakes glyph boxes off the pixel grid below
36 px (D27).

## §9. Staging

| the note's step | what happened |
|---|---|
| 0. decide where the port stops; try the two things in §11 | decided (D1). `font_data` was tried later and works (§2 above). |
| 1. the differential harness | first, as advised |
| 2. the OpenType reader and a custom `ImFontLoader` behind the existing Python | the reader, yes; the loader, no (D27); and not behind the existing Python: advtext is unchanged, and the plan to put imtypeset under it is `docs/advtext-rework-plan.md` |
| 3. the draw loop, then the core | both, and then what the note did not stage: math, selection, a public API, a C ABI, blocks and a Document, hardening, distribution |

## §10. Naming

`imtypeset`, with the note's pitch as the README's first line. **"No name has
been checked against existing projects, crates or packages."** Still true:
nobody here checked, and this work had no network for it. Open, and to be
closed before publication.

## §11. What the note left open

| the note's item | status |
|---|---|
| where the port stops | **decided** (§4 above) |
| whether font bytes can be passed through `ImFontConfig.font_data` | **answered: yes**, through a `PyCapsule` named `nb_handle` (`docs/bindings-notes.md` 6A) |
| what the FreeType loader flags do to this project's text | **open**. The goldens and every measurement here are under the default flags. A host's flags are passed through untouched (D27), and nobody looked at a page under another setting. |
| whether the demo's runner (`hello_imgui`) uses the same loader as a bare context | **open** as asked. What was established instead: under hello_imgui the atlas is emptied after `post_init`, so fonts are added in `load_additional_fonts` (`.concerns/bundle-route--builder--result.md`), and the font callback is not called again by itself in these versions (`.concerns/py-fixes--builder--result.md`, read at the tagged commits and run for 60 frames). |
| which of the reference's docstrings explain a measurement by stb_truetype's behaviour | **open**, and the prototype's to fix; this project reads the pinned ImGui source instead of those docstrings |
| whether stb_truetype's GPOS and glyph-box readers are enough | **moot** (§5 above) |
| the reader's size | **measured**: 4,413 lines for `src/opentype.*`, 5,719 with `fontlib` and `font_loader` |
| "all timings are from one machine and one document" | still true of this project too: one laptop, one compiler, one day (`docs/performance.md`) |

## What the note did not foresee

Recorded because a reader of the note would not guess them.

- **The reference's own screen rule collapses word spaces.** Under the advance
  rule the note calls the most valuable content, 530 of 37,778 justified word
  spaces are drawn at under 60 % of their laid-out width and some at less than
  no width (D48). The product's rule fixes the spaces and costs letter
  evenness: 8.9 % of adjacent letter pairs more than half a pixel from their
  laid-out distance.
- **The reference departs from `tex.web` in ways its documents do not record.**
  A reading of the layout core against the WEB source found them
  (`docs/divergences.md`, the register); nine are switches in the product
  (D41), among them one active node per line count where TeX has `easy_line`,
  which made ragged total fit quadratic.
- **A library other programs link meets hostile input.** Two reviews and three
  fuzzers found crashes, unbounded memory and unbounded time in fonts, formulas
  and text (`docs/hardening.md`). The session that built this was itself ended
  twice by the kernel's OOM killer, in each case just after a test fed a
  100 MB formula to a parser that did not yet bound it; that this was the
  cause is an inference from the timing (`.concerns/BOARD.md`).
- **Floating-point contraction changes results.** Compiled at
  `-O3 -march=native` without a guard, 38 of 41 tests pass: the font reader's
  ink boxes move by one unit in the last place (`docs/distribution.md`
  section 5). The drop-in file carries a pragma for it, measured under GCC
  only.
- **`ImWchar` is 16 bits by default** and the math face lives in planes 1 and
  15 (D8, D15); the note's "bindings over ImGui only" has to work in a host
  that never defined `IMGUI_USE_WCHAR32`, and does (preset `wchar16`).

## Not verified for this file

Everything quoted from another document was read there and not re-run, except
where marked *(run for this file)*. Not run here at all: any sanitised preset,
`stb`, `wchar16`, 1.93, the font corpus with `IMTYPESET_WILD_ALL=1`, any
benchmark on a quiet machine, any oracle script.
