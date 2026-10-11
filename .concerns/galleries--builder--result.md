# galleries — result (builder, 2026-10-11)

Nothing staged or committed. `build/galleries*` removed. Every binary and every Python process
ran under `ulimit -v 6000000; timeout …` (sanitised: `ASAN_OPTIONS=hard_rss_limit_mb=3000`), `-j3`,
one sanitised build at a time. No golden touched, no tolerance added to a golden comparison.

## Short form

- The demo has a **Specimen** section and a **Formula gallery** section, and its **Math** section
  carries what `pages/mathtext.py` showed. All of it is `imgui.h` + `imtypeset.h`.
- `ImTypeset::ShowDemoSection(section)` / `GetDemoSectionName(section)` / `enum
  ImTypesetDemoSection_` are **landed** in `include/imtypeset.h`, `include/imtypeset_c.h`,
  `src/imtypeset_c.cpp`, the map test, `tests/test_c_api.c`, `python/imtypeset/_capi.py`; Python is
  `python/imtypeset/demo.py`. The interim copies are gone.
- The sidecar is **built with the demo** and links against both wheels. Shown and photographed in
  advtext's venv (1.92.900) and pptmstr's (1.92.801): the whole window, and sections as pages.
- The Specimen's numbers equal the reference's `build_specimen` to the four decimals it prints.

## Defects seen in the pictures (first, as asked)

1. **`\oint_\gamma` in the Cauchy plate: the subscript stands about a third of an em right of the
   sign's lower tail** instead of tucked against it (`docs/images/demo-formulas.png`, both venv
   shots). The reference gives the same box sequence (operator box 33.97 px wide at a 34 px em,
   then the subscript's box: probed with `build_gallery` at the tag). It is the family already in
   `docs/divergences.md` ("`\int f(x) dx` … the reference does the same, so the port is faithful
   and the result is wide"). Not changed; the port follows the oracle.
2. **Table cells clip what is wider than they are**: the display column of "a long one, for the
   line breaker", "all of it at once", "bars over parts of unequal width", "rules and what they
   cover", and the unbroken sources in the refusals table (`demo-math-subset.png`). The old demo
   had this (its finding 7); more rows show it now.
3. **Ink is on whole pixels, so the kerning columns differ by the kern rounded to a pixel**
   (-0.71 px shows as one pixel or none). The numbers are exact; the picture is not. Said in the
   section's own text.
4. Script-script type in the continued fraction and in the table's inline fractions is 4 to 5 px
   tall at the default sizes (existing Known limits item).
5. Mine, cosmetic: in the embedded pages ImGui's own labels grow with the pushed font size (the
   section follows `ImGui::GetFontSize()`, which is the contract; a host that wants small labels
   pushes less). In `bundle_demo.py --show window` the application's one line of text sits above
   the window by design.

Nothing else looked wrong to me in: `demo-specimen`, `demo-formulas`, `demo-math-stream`,
`demo-math-subset` (headless, soft raster) and `py-*-window`, `py-*-window-formulas`,
`py-*-page-specimen`, `py-*-page-formulas`, `py-*-page-math`, `py-*-page-math-typed`,
`py-*-page-document` (Xvfb + llvmpipe, `import -window root`). **Opened and looked at**: those
four headless pages, `demo-section-specimen`, and from Python advtext's page-specimen, window,
page-formulas and pptmstr's window-formulas, page-math, page-math-typed, advtext's page-document,
plus a 3x crop of the Cauchy plate. **Not opened**: the other headless pages
(`demo-section-formulas`, `demo-section-math`, the nine older sections) and the remaining venv
twins; the twins have the same frame statistics as the ones opened (layouts, paragraphs,
vertices equal in the two venvs), which is not the same as having looked.

## What each old page showed, and where it is now

### `advtext/ui/specimen.py` + `pages/specimen.py` → section **Specimen** (`ImTypesetDemoSection_Specimen`)

| old | now |
|---|---|
| six faces, one pangram each at 24 px, labels right-aligned in a measured gutter | the same pangram, a row per role that has a font (CodeBold only when it has its own), gutter measured from the labels; **plus the set width of each row as a number**. Size is a slider |
| size ladder: 16 px × 1.25^k, k = -2..3, "Hamburgefonstiv" | the same six sizes as `EmSize`, with widths. Replaces the old demo's "One face, several sizes" in Faces and sizes (moved, not duplicated) |
| kerning: 7 pairs at 44 px, kerned and unkerned columns, kern in the gutter | the same 7 pairs; "kerned" is one text, "letter by letter" is two texts placed by their widths; the gutter is the difference; a hairline marks where each setting ends |
| three markdown notes (bold, italic, code spans) | three plain paragraphs, reworded to be about roles and not about Roboto/Inconsolata (a host brings its fonts) |
| "N face(s) could not be resolved" | "N roles have no font" |
| debug overlay toggle | **not here** (gap 1) |
| the x-height figures 0.457 / 0.431 / 0.528 in the note | **not here** (gap 2) |

Numbers, port against the reference at the tag (px): faces 473.44 / 480.56 / 464.26 / 471.20 /
516.00 / 479.42 — equal; ladder 78.89 … 240.75 — equal; kerns -1.87 -2.13 -0.71 -1.40 -6.96 -5.91
+1.07 — equal. `tests/test_demo.cpp` holds eleven of them to 0.0001 px.

### `advtext/ui/formulas.py` + `pages/formula.py` → section **Formula gallery** (`…_Formulas`)

All eleven plates, in order, from the sources `tests/test_mathtext.py::GALLERY` writes for them
(the reference holds each source to the hand-built entry's box). Panels of a plate share a
baseline and the labels sit on the panels' columns, as there. 34 px em by default (a slider).
Each panel is a paragraph object with one `ImTypesetRunFlags_DisplayFormula` / `_Formula` run.
A panel that is refused shows its source and the reason (all do with no math font).

Not carried: captions as markdown (plain text; titles are separators); the `tex.web` section
numbers in captions (I did not check them, so I did not print them); `Limits.NO_LIMITS`,
`bin_`/`ord_` in captions (reworded to what a source says: `{+}`); "N codepoints have no glyph in
the math face" and "N glyphs have no raster" (gap 3); debug overlay (gap 1); the sheet growing
sideways for a wide formula (the row overhangs and is clipped by the window).

### `pages/mathtext.py` + `demo/math.md` → section **Math** (extended)

| old | now |
|---|---|
| the text pane (`math.md`): what a formula does to its line; a formula at a line break | "In a paragraph": the same two paragraphs, at the section's Width slider |
| formulas in a heading, list items, a quotation, after a link | "Where a formula can go": a five-block `ImTypesetDocument` (heading, two list items, quote, a paragraph with a coloured, tagged run after a formula; the tag under the pointer is shown). A `ImTypesetDocumentDrawFlags_Debug` checkbox |
| … in a table cell | **not here**: tables are item 3 |
| `{{reference}}`: 42 rows, sorted by asking the library | "The subset": 54 rows = those 42 + 12 the old demo had and they did not; what / source / `$…$` / `$$…$$`; sorted by asking (`CalcTextSize` + `GetLastError`). Click a source to edit it |
| `{{refusals}}`: 17, with the reason given when the page is built | "Refused, and why": the same 17 with `ImTypesetErrorInfo::Message`. **All 42 reference rows set and all 17 are refused in the port** (the window says "54 of 71 sources set") |
| the source box: markdown, re-set per edit, tally and each refusal | the existing box edits ONE formula, shown inline and displayed with the refusal and its byte. No markdown (advtext's), no tally there |
| the stream: `StreamDocument`, Restart / Step / Play / Finish, opens inside its first formula, readout | "In text that is still arriving": the same source, 3 bytes a piece, the same four buttons, opens at piece 7 with a dollar standing as text; readout = pieces, formulas set, refused so far, dollars standing as text. It is three paragraph objects re-given text, not a commit policy |
| "selection copies …" line | the Selection section already shows what a selection copies (formula as its source) |
| measure slider, prices | Width slider, "Prices are not math" (both were there) |
| counts of atoms / glyphs / missing glyphs per pane | **not here** (gap 3, 6) |

## API gaps (nothing worked around through internals)

1. **No debug overlay for a paragraph or a widget.** `ImTypesetDocumentDrawFlags_Debug` exists for
   documents only; the specimen and the gallery are paragraphs and widgets.
2. **Nothing describes a face**: ascent, descent, x-height, cap height, the em of a `FontSize`.
3. **Nothing says what a font covers**: no "has a glyph for U+…", no count of characters drawn as
   a stand-in. (`ImFont::IsGlyphInFont` is not it: layout codepoints are not `ImWchar`s, D15.)
4. **`CalcTextSize()` and the widgets answer whole pixels.** A set width to a fraction of a pixel
   needs a paragraph object and `GetParagraphLine()`. The header says the item is whole pixels;
   it does not say CalcTextSize is, and a kern cannot be read off it (`AV` at 44 px: 55 against
   54.83).
5. **No kern query and no "kerning off".** Shown by construction instead; fine for a demo, but a
   host cannot ask "what is the kern of this pair".
6. **Nothing says "the fonts changed".** Kept measurements compare the eight `ImFont*` and the
   count of fonts the demo added (`DemoSetting`); a host replacing a font at the same address
   outside the demo is not seen. (The earlier demo's gap 3, still open.)
7. **A widget reports no baseline and no formula status**, so a row of formulas on one baseline
   needs paragraph objects (17 handles for the gallery).
8. **No table block** (item 3), so no formula in a cell.
9. A document block's runs cannot mark a link's hit area beyond `Tag` (used); fine.

## The declarations (landed)

`include/imtypeset.h`: `typedef int ImTypesetDemoSection;`, `enum ImTypesetDemoSection_`
(Paragraph 0, Faces 1, Specimen 2, Math 3, Formulas 4, Selection 5, Inspector 6, Stress 7, Fonts 8,
KnownLimits 9, Document 10, COUNT), `ShowDemoSection(ImTypesetDemoSection)`,
`GetDemoSectionName(ImTypesetDemoSection)`. C: `ImTypeset_DemoSection`, the enum,
`ImTypeset_ShowDemoSection` (guarded), `ImTypeset_GetDemoSectionName`. Map: two `BOTH` rows; the
typedef count 16 → 17. `_capi.py`: two prototypes, so **the package refuses a library built
before this**. `tests/test_c_api.c`: `test_demo_sections()` inside a frame.

Changed from what I first posted: a section shown alone keeps a state of its **own** (lead has
the corrected comment; reason and measurement in `docs/decisions.md` D55).

## How advtext's demo reaches it (item 6; written into `docs/advtext-swap-status.md`)

```python
from imtypeset import demo
demo.show_demo_window()            # whole window; (True) -> bool, with a close button
demo.sections()                    # the DemoSection members the library has
demo.section_name(s)               # "Specimen", "Formula gallery", "Math", …
imgui.push_font(None, 19.0); demo.show_demo_section(demo.DemoSection.Specimen); imgui.pop_font()
```

The three retired pages are `DemoSection.Specimen`, `.Formulas`, `.Math`. Runnable:
`examples/python/bundle_demo.py --show window|pages [--page NAME]`.

## How the demo links in the wheel

The fork exports `InputTextMultiline` / `InputTextWithHint` with a `std::function` callback
(read with `nm -DC` on both wheels' modules). `python/host/imtypeset_bundle_shim.cpp` defines
upstream's two names under `IMTYPESET_BUNDLE_STAND_IN` (set by the script in its stand-in branch)
and forwards. The demo TU is untouched by this. `-DIMTYPESET_DISABLE_DEMO` is gone from the
script. Both libraries link with `--no-undefined`; the gate accepts (22 probes, stand-in). A
`\frac{1}{2} + \hat{x}` typed into the Source field through `io.add_input_characters_utf8`
arrives and is refused at byte 14, in both venvs (`py-bundle-demo-*-page-math-typed.png`).

## What ran, on the final tree unless said

| configuration | result |
|---|---|
| debug, FreeType, 32-bit: `test_demo`, `test_c_api`, `test_c_api_map`, `test_header_pod`, `example_demo_headless` (ctest) | 5/5. `test_demo` 20 tests / 272 checks |
| `tests/amalgamation/compile.cmake` run by hand with the ctest's arguments | all steps passed. (The `amalgamation` ctest itself is not registered in a narrowed build; this is its command) |
| ASan+UBSan, debug, FreeType, 32-bit: `test_c_api`, `test_c_api_map`, `test_demo` (42 s), `example_demo_headless` (54 s) | 4/4. Run before the last `clang-format` of `tests/test_c_api.c` (one line re-wrapped); not rerun after |
| debug, stb, 16-bit `ImWchar`: `test_demo`, `test_c_api`, `example_demo_headless` | 3/3 |
| release, FreeType: `test_demo` 20/272, headless 16 pages | ok, before the declarations moved into the public headers; not rerun in release after |
| self-hosted Python library (`python/CMakeLists.txt`, release): `python_binding`, `python_demo_sections` (new) | 2/2, advtext's interpreter |
| sidecar, advtext venv, wheel 1.92.900 / ImGui 1.92.9: `bundle_demo.py` 7 runs (window ×2, pages ×5 incl. typing) on Xvfb | all exit 0, "last error: 0" |
| sidecar, pptmstr venv, wheel 1.92.801 / ImGui 1.92.8: the same 7 | all exit 0; identical frame statistics |
| `-DIMTYPESET_DISABLE_DEMO`: `src/imtypeset_demo.cpp` and `tests/test_demo.cpp`, `-fsyntax-only` | both compile, after the landing. Never linked or run that way |

**Not run:** any preset; the full suite; release after the landing; the `fork:` branch of the
build script; clang, MSVC; a real display or GPU; `demo_sdl2` (I changed one string in
`examples/demo_sdl2/main.cpp`: the `--open all` list; not compiled); `IMTYPESET_DISABLE_DEMO` as
a whole build; the 16-bit run under the sanitisers.

## The 16-bit index abort the lead relayed

Mine, a half-state: I had made two tests' windows 12,000 px tall "so that everything is in view",
and one window then held more than 65,535 vertices. The windows are 4,000 and 3,000 px now and
debug passes (assertions live), FreeType/32 and stb/16. Separately the gallery now skips
`DrawParagraph` for a row that is not visible (`ImGui::IsItemVisible()` on the row it reserved),
which is what a paragraph-object page should do; the widgets cull themselves. **Not shown:** that
no realistic window can pass 65,535 vertices. The Math section with everything in view is about
3,500 px of mostly small type; a host with a 4K-tall window, the demo window maximised and Math
plus Formula gallery open has not been tried.

## Files

New: `python/imtypeset/demo.py`, `examples/python/bundle_demo.py`,
`tests/python/test_demo_sections.py`, `docs/images/demo-{specimen,formulas,math-stream,
math-subset,section-specimen,section-formulas,section-math}.png`,
`docs/images/py-bundle-demo-{advtext,pptmstr}-*.png` (9).
Changed: `src/imtypeset_demo.cpp`, `include/imtypeset.h`, `include/imtypeset_c.h`,
`src/imtypeset_c.cpp`, `tests/test_demo.cpp`, `tests/test_c_api.c`, `tests/test_c_api_map.cpp`,
`examples/demo_headless.cpp`, `examples/demo_sdl2/main.cpp` (one string),
`python/host/imtypeset_bundle_shim.cpp`, `python/host/build_bundle_library.sh`,
`python/CMakeLists.txt` (the new ctest), `python/imtypeset/_capi.py` (two prototypes),
`python/imtypeset/__init__.py` (one docstring), `docs/distribution.md` (two passages),
`docs/advtext-swap-status.md` (two API rows, item 6, a section), `docs/decisions.md` (D55),
and the nine existing `docs/images/demo-*.png` re-rendered (they were stale before this task:
demo-guard's note). All sixteen headless pages were recompressed losslessly with ImageMagick
(`compare -metric AE` = 0 against the writer's output); the writer's own files are 1.4 to 9 MB.

`examples/demo_headless.cpp` now rasterises only the last frame of a run (D55): 25 s → 3 s in
release, and under ASan the sixteen pages took more than 550 s before it, past the example's
600 s ctest timeout.

## Decisions taken unattended

- One function + enum (lead accepted). State per presentation, not shared (D55).
- The interim header went in `examples/`, not `src/` as the task said: `tools/amalgamate.py`
  refuses a `src/*.h` no library source includes and any non-public include in the demo TU.
  Moot now that it is landed.
- "Faces and sizes" lost its "One face, several sizes" block to the Specimen's ladder.
- Captions and notes are reworded for a library whose fonts are the host's.
- The § numbers of `tex.web` are not printed in the gallery: CLAUDE.md says check a number
  against `tex.web` before citing it, and I did not.

## Least sure of

1. **Whether the Math section is too much for one section.** It is about 3,500 px tall with
   everything laid out, 71 candidates put through `CalcTextSize` whenever the style or fonts
   change (the demo's font-size slider re-sorts all 71 on every tick while dragged).
2. **`DemoSetting` and fonts** (gap 6): a stale Specimen after a host swaps a font in place.
3. **The shim's hand-written declarations of the fork's two functions.** A mismatch is a link
   error, not a wrong call, and both wheels link; but it is one more place that knows the fork,
   and the `fork:` branch has still never run.
4. **Vertices in one window** (above): bounded in the tests' windows, not proven for any window.
