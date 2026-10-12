To:         lead
From:       builder (device-finish, builder-43)
Date:       2026-10-12
Status:     FINAL. Green in all eleven presets (ten on the hashed tree; `imgui-next` after one
            test-only write, section 1); the Python route green in both environments

# device-finish: the grid made unconditional, stale advances, 0.4.0, the gate

Everything here was measured or read by an agent (this builder) on Linux x86-64, GCC 12.2.0,
headless: vertices read from draw lists and a software renderer's pictures; no real window but
Xvfb's, no GPU. Nothing was committed, staged or tagged. Base: `5077b22`.

## 1. The preset table

Every preset of `CMakePresets.json`, each from a removed directory, one at a time, `-j4`, under
the guards (`ulimit -v 6000000`; sanitised: `ASAN_OPTIONS=hard_rss_limit_mb=3000`). The tree was
hashed before and after (539 files outside `.concerns/`): **unchanged over the pass**. Logs:
`build/device-finish-gate/`.

| preset | configure / build | warning lines | ctest |
|---|---|---|---|
| debug | 0 / 0, 56 s | 0 | 55 of 55, 121 s |
| release | 0 / 0, 85 s | 0 | 54 of 54, 48 s |
| asan | 0 / 0, 104 s | 0 | 54 of 54, 233 s |
| stb | 0 / 0, 57 s | 0 | 54 of 54, 90 s |
| wchar16 | 0 / 0, 71 s | 0 | 54 of 54, 84 s |
| amalgamation | 0 / 0, 62 s | 0 | 54 of 54, 98 s |
| fp-host | 0 / 0, 92 s | 0 | 54 of 54, 50 s |
| exceptions | 0 / 0, 108 s | 4 (the `-Wterminate` of `tests/imconfig_throwing.h`, as at v0.3.0) | 54 of 54, 264 s |
| bundle-config | 0 / 0, 66 s | 4 (the same warning, in imgui_bundle's own config header, as at v0.3.0) | 54 of 54, 97 s |
| imgui-next | 0 / 0, 61 s | 0 (1 in configure: the version notice, as at v0.3.0) | **53 of 54 on the hashed tree**; 54 of 54 after the one write below |
| python-host | 0 / 0, 62 s | 0 | 56 of 56, 98 s (`python_binding`, `python_demo_sections` among them) |

**The one write after the pass began, exactly.** `imgui-next` failed `test_device_grid` in ONE
test (`a_paragraph_in_the_code_face_is_ragged_...`, 2 checks, density 2): a flaw of the test's
own harness, not of the library. It read quads' texture coordinates in a frame in which its
own glyph lookups in the other faces grew the atlas. I changed `tests/test_device_grid.cpp`
only (the first of its two frames now does the lookups too; 11 lines), formatted it, and
re-ran what that file can affect: the `test_device_grid` target rebuilt and run in ALL eleven
presets (11 of 11 ok, 0 warnings: `build/device-finish-gate/regrid.txt`), and the whole
`imgui-next` suite again (54 of 54). No library source changed after the pass began. The
other ten presets' full suites were NOT run again on the final tree; their other 53 or more
tests cannot see that file. The final tree's hash differs from the hashed one in that one
file and was unchanged over everything in section 7.

## 2. The findings, each reproduced by running at `5077b22` before anything changed

Raw output: `.concerns/device-finish--builder--repro-head.txt` (`tests/test_device_grid.cpp`
built with `-DREPRODUCTION_AT_HEAD`, debug, FreeType 2.12.1, the usual faces: Roboto, Inconsolata,
Latin Modern Math). At a pixel height of 16 the code face's cell there is 7 px (the em comes
from Roboto's line height); the reviewer's numbers were for every role the monospace file (cell
8), so sizes differ and mechanisms do not.

| # | finding | reproduced? | how large, run | what changed | its test (`tests/test_device_grid.cpp` unless said) |
|---|---|---|---|---|---|
| 1a | a character the face lacks that draws nothing takes the missing glyph's advance | YES for U+FEFF, U+200D, ESC, U+2003. NOT for the reviewer's own example: Inconsolata HAS U+200B (advance half an em, no ink), so `a<U+200B>bcd` was on the grid, a cell wider than a terminal has it | 0.9752 cells; every later glyph 0.17 px from layout (0.09 at density 2, 13 px) | such a character takes no cell (U+200B too, where the font gives an inkless glyph an advance); every device advance is a whole framebuffer pixel | `every_character_of_a_code_row_takes_a_whole_number_of_cells_and_is_drawn_in_them` |
| 1b | a borrowed character takes another face's width | YES | Cyrillic 1.857 cells, Greek 1.286, U+1D400 1.714; after three of them columns 3 px off (5 px at density 2); U+FFFD for an ideograph 2 cells (2.09 at density 2) | whole cells: `ceil(advance / cell)`, at least one; U+FFFD and `?` one cell, the code face's own glyph where it has one | the same; `an_ideograph_borrowed_from_a_face_that_has_it_takes_two_cells` (IPA Gothic read in place: 16 px, 2 cells of 8) |
| 2a | remembered advances outlive the rasterisation | YES for the class; **NOT with the reviewer's flag**: `NoHinting` moves Inconsolata's cell at no pixel height 13 to 24 | `GlyphExtraAdvanceX` +1 and a rebuild: `aaaa` 40 px and `ZZZZ` 44 in one document for good; FreeType `Bold`: 28 and 32 | a once-a-frame witness per face in use; on a difference everything is measured again | `advances_remembered_do_not_outlive_the_rasterisation_they_were_read_from` (3 changes, 12 sizes, with and without a frame between) |
| 2b | the entry follows `CurrentRasterizerDensity` | YES | 15 px: cell 6, then 7 for the next character and 28 for four | `face_bake` says rasteriser density 1 at every density | `the_entry_asked_for_does_not_follow_the_density_a_font_was_last_pushed_at` |
| 3 | a retained Paragraph drawn stale after a density change | YES | `DrawParagraph` true, 160 vertices of the density-1 layout at density 2 | refused, `InvalidArgument`, "call LayoutParagraph again"; `device_current` wired | `a_paragraph_laid_out_for_another_display_is_not_drawn_as_it_stands` |
| 4 | a Document asked off-context is re-laid by its fonts | YES | 40th character 273 px on screen, answered 266.24; 1 block laid out for the question | keeps its style, device and layout; a first layout there is by font and says so | `a_document_asked_without_its_imgui_context_answers_from_the_layout_it_has`; `tests/test_device_advances.cpp` `without_its_imgui_context_what_is_laid_out_keeps_its_device_and_what_is_new_is_by_its_font` |
| 5 | wrapped continuations off the grid | YES | 3.70, 3.90, 4.05, 3.75 cells at 13, 16, 19, 22 px (up to 2.45 px off) | the whole number of cells nearest two ems | `a_wrapped_code_line_continues_a_whole_number_of_cells_in` |
| 6 | version; "zero keeps meaning as before"; FindGlyph rasterises | read, all true | | 0.4.0; the header's paragraph rewritten; `GetCharAdvance` (see ruling 6) | `a_host_compiled_against_0_3_0_is_refused_by_its_version_and_by_its_struct_sizes`; `tests/test_c_api.c`; `tests/python/test_binding.py` `test_a_package_from_0_3_0_is_refused_at_import` |
| 7 | another size's glyph memoised as the cell | YES, with a route the reviewer did not have: `ImFontFlags_LockBakedSizes` | a line at 22 px laid out with the 16 px entry's cell (28 px for four) | an entry further than ImGui's rounding from the size asked is refused; measured again when the atlas can answer | `an_advance_is_never_taken_from_an_entry_of_another_size` |
| 8 | coverage: ASCII, whole origins, densities 1 and 2, 14 to 20 | true | | 19 tests: section 4 | |
| also | a Code-face paragraph is justified | YES | characters up to 3 px (6.1 at density 2) from a whole cell, both margins flush | ragged right, spaces rigid | `a_paragraph_in_the_code_face_is_ragged_and_its_columns_agree_line_to_line` |
| also | "ImGui rasterises 22.5 px where the library rounds to 23" at density 1.5 | not a defect found: at 1.5 the library asks ImGui for the size in framebuffer pixels and ImGui rounds it; the grid holds there (192-page sweep) | | | |

## 3. The rulings

| # | ruling | built? |
|---|---|---|
| 1 | the grid is unconditional | AS RULED. Fixed-pitch is decided from hmtx (every mapped character U+0020..U+007E one advance); `post.isFixedPitch` not read. Two things beyond the ruling: the stand-in U+FFFD is the code face's OWN where it has one (the regular face's is an em wide and would overprint the next cell); a non-unspaced character that draws nothing (U+2003 where the face lacks it) takes one cell. Tabs count cells. |
| 2 | stale advances | AS RULED: the witness, chosen over keying on flags (no new ImGui field; catches loader, PixelSnapH, extra advance too). Cost: steady frame 43.6 us device, 43.6 font. Density said outright. Finding 7 guarded by the entry's `Size`. The reviewer's NoHinting sequence shows one cell afterwards because it showed one before; `Bold` and extra advance are the ones that move it. |
| 3 | stale Paragraph | AS RULED: a refusal, as for the font generation. Only for a paragraph that HOLDS device text. |
| 4 | Document off-context | AS RULED, and the same for `LayoutParagraph` (not ruled). `ImTypesetDocumentStats::MeasuredWithoutDevice` is new. The paragraph test that asserted the old behaviour is rewritten and says why. |
| 5 | wrap hang | AS RULED. |
| 6 | no rasterising at layout | **NOT as ruled.** The advance-only route answers the same number (1,280 comparisons equal to the bit). Layout now asks through `GetCharAdvance`. But below 128 px ImGui rasterises on that call unless `ImFontBaked::LoadNoRenderOnLayout` is set, and I did not set it: a write to a bit-field of the host's ImFontBaked that the stock wheel has no accessor for (the probe cannot attest it under stand-in headers; the neighbouring bit is `WantDestroy`), and it changes the host's own CalcTextSize on that entry. Cost of not: every distinct character laid out on the device is rasterised once (704 glyphs packed against 128 for the same questions). D80 has it for you to overturn. |
| 7 | spaces are cells | AS RULED. Justified prose with a code run: the run's spaces are one cell each, 142 steps checked, the prose spaces took 2.9 to 6.2 px. A Code-face paragraph: ragged. `reference()` untouched: no golden changed, whole suite passes. There is no letter-spacing in the library. |
| 8 | 0.4.0 | AS RULED. Seven copies, `tools/version.py --check`. Refusal of a 0.3.0-sized struct tested in C++, C, Python. |
| 9 | combinations | section 4. |
| 10 | lazy re-layout in a block | **NOT built: not small** (material, boxes, ship-out, selection model and column cuts are per block). Measured instead: section 5. |

## 4. The combinations nobody had tested, defects first

**Defects found and fixed.**

1. **A table: two code cells of one column, laid out at ONE x (319.45 px), drawn at 320 and at
   319.** A cell's first word was placed from the last glyph of the cell before it
   (`word_spaces_led`). Fixed: a word on a grid is never led (`FontBinder::on_grid`); now both at
   319. Prose cells are still led (two prose cells of a column can differ by a pixel: not
   changed, not asked; say if it should be).
2. **A dangling `ImFontGlyph*` in `tests/test_device_advances.cpp`**, exposed because layout now
   loads a face's ASCII to find its cell: the test read freed memory. Fixed in the test.
3. **`tests/test_specimen.cpp` passed by coincidence**: its code row's em is a whole number of
   pixels, so device widths equalled the reference's, and nothing compared glue until spaces
   became cells. It now says `Advances_Font`, which is what its golden is.

**Defect observed and NOT changed** (older than this work, outside it): a click in a tab wider
than one cell answers the position BEFORE the tab unless it is in the right half of the tab's
last cell (the tab's spaces share its byte).

**What each test and picture shows.** Pictures, software renderer, looked at:
`.concerns/device-finish--builder--shot-freetype-{1x,2x,3x}.png` (the whole page) and
`…-1.5x-rows.png` (the rows enlarged), hairlines every cell, darker every fourth.

| combination | test | what was seen / asserted |
|---|---|---|
| densities 1, 1.5, 2, 3 x pixel heights 13 to 24 (22 and 24 among them) x origin 0, 1/4, 1/2, 3/4 of a framebuffer pixel off; FreeType and stb | `the_grid_holds_at_every_density_size_and_origin_and_rows_agree_at_every_column` (192 pages, 18 rows of diff text) | every laid-out character a whole number of cells from its row's start; every glyph on a whole framebuffer pixel; all glyphs of a block the same distance from their places (under 1/128 px apart), at most half a pixel, nothing at a whole origin. In the pictures every glyph starts on a hairline at all four densities |
| FixedLineHeight + Device + marks (Ink, Behind, Under) + selection + one block of 5,000 lines | `a_diff_views_block_of_five_thousand_lines_with_marks_and_a_selection_is_on_the_grid`, densities 1, 1.5, 2 | line n at n line heights; under 60 lines given glyphs; each fill's two edges on the pens of the first character marked and the first after; Ink on exactly its 3 glyphs; hit tests in marked cells right; every glyph a whole number of cells from `pos`. Picture: the yellow, red, green and blue fills start and end on hairlines; a fill over a zero-width character and its neighbours is as wide as the neighbours |
| rectangles, carets, hit tests, copy on the diff rows | `a_rows_rectangles_carets_hit_tests_and_copy_agree_with_its_cells` | a row's rectangle is whole cells; a quarter and three quarters across any character's cells answer the carets at its two edges; a copy of each row is the row as written (tab a tab, the ideograph itself) |
| a table, mixed Font and Device cells, with and without held columns and exact line heights | `code_in_a_tables_cells_keeps_its_cells_beside_cells_measured_by_their_font` | steps inside a code cell one cell each; the column's two code cells at one drawn x; the code column's natural width sixteen cells and the padding. Picture: the code column is straight |
| columns | `code_in_columns_is_on_whole_pixels_and_steps_by_the_cell_in_every_column` | per column, one fraction of a pixel for all its glyphs, at most half |
| an inline formula; prose on the device | `prose_on_the_device_is_on_whole_pixels_whatever_it_holds_and_a_formula_keeps_its_own_width` | every caret of device prose a whole framebuffer pixel, with a ZWJ, ESC, a math-alphabet letter and an ideograph in it; after a formula words are on whole pixels, less than a pixel from their places. NOT on a grid after a formula: said in the header |
| a Code-face paragraph; justified prose with a code run | the two tests of ruling 7 | picture: ragged paragraph on the hairlines; justified prose flush both sides with the purple code run unstretched |
| a real ideograph | `an_ideograph_borrowed_from_a_face_that_has_it_takes_two_cells` | IPA Gothic, read in place from `/usr/share/fonts`, skipped with a message where absent: 2 cells, `b` at the same x as in a row of digits |
| stb_truetype | all of the above in the `stb` preset | pass; what Device promises there is in the header: the grid in full; ImGui's own text on it only with `PixelSnapH` at density 1. One default for both loaders, kept |

## 5. Costs (release, GCC 12.2.0, FreeType 2.12.1, Ryzen 7 PRO 4750U; one-minute load average 3.9 to 4.4 from other sessions: NOT timings to quote)

`bench/bench_code.cpp`, every block with exact line heights;
`.concerns/device-finish--builder--bench-code.txt`.

| | device (min / median / p95) | font |
|---|---|---|
| 5,000 lines, ONE block, laid out whole, warm | 17.65 / 17.99 / 18.26 ms | 17.83 / 18.37 / 18.75 ms |
| steady frame of a 768 px window | 43.6 / 44.1 / 46.0 us | 43.6 / 44.9 / 48.0 us |
| **first frame after a density change** | **15.68 / 16.05 / 17.01 ms** | 46.8 / 50.3 / 51.3 us |
| 5,000 lines as 250 blocks, first frame after a density change | 233 / 236 / 244 us | 50.7 / 54.1 / 55.1 us |
| 50,000 lines, ONE block, laid out whole, warm | 186 / 189 / 201 ms | 191 / 199 / 201 ms |
| **first frame after a density change** | **170 / 177 / 194 ms** | 48.4 / 52.0 / 58.8 us |
| 50,000 lines as 2,500 blocks, first frame after a density change | 660 / 721 / 759 us | |

The check of remembered advances: nothing measurable in a steady frame once it skipped faces
unused for a frame (its first form cost 17 us a frame after twelve sizes had been met; caught by
this benchmark). `bench_draw`, `bench_reflow`, `bench_marks`, `bench_tables`: NOT run before and
after.

## 6. For the lead's file

**H34, row text.** Done in the working tree on `5077b22`, version 0.4.0. Code is measured on the
device by default (the advance of the glyph in the atlas entry it is drawn from, a whole number
of framebuffer pixels, no kerns) and, the code face being fixed-pitch, is on a GRID that holds
whatever the text holds: every character takes a whole number of cells, a character the face
lacks takes as many cells as hold it in the face it is borrowed from (an ideograph two), a
control, format character or combining mark with no ink takes none, U+FFFD for what no face has
takes one, a tab goes to a multiple of four cells, a wrapped line hangs whole cells, spaces
never stretch, a Code-face paragraph is ragged. Measured by this library's builder (an agent),
vertices read, FreeType 2.12.1 and stb_truetype: on 192 pages (densities 1, 1.5, 2, 3; pixel
heights 13 to 24; origins 0, 1/4, 1/2, 3/4 of a framebuffer pixel) of eighteen rows holding
tabs, box-drawing, Cyrillic, Greek, a math-alphabet letter, U+200B, U+200D, U+FEFF, U+0301,
ESC, an ideograph and an emoji, every laid-out character is a whole number of cells from its
row's start, every glyph is on a whole framebuffer pixel, and all glyphs of a block are one
distance from their places, at most half a pixel and nothing at a whole origin. With IPA
Gothic as the regular face an ideograph takes two cells. Under FreeType the pens are still Dear
ImGui's own for ASCII (the 49 pages of `device-advances`); under stb only with `PixelSnapH` at
density 1. Remembered advances are checked against the atlas once a frame. NOT promised: a
grid after a formula on the same line, or for a face that is not fixed-pitch. Not measured: a
real window beyond Xvfb's, a Retina display, macOS, FreeType's light and mono modes.

**H35, row text.** Done. On the device the rectangle, caret, hit test and copy agree with the
cells for every row above: a row's rectangle is whole cells, a point a quarter and three
quarters across any character answers the carets at its two edges, a copy is the row as
written. In the diff view's shape (one Code block of 5,000 lines, `FixedLineHeight`, Behind,
Under and Ink marks and a selection, densities 1, 1.5, 2) each fill's edges are on the pens of
the first character marked and the first after it, and hit tests in marked cells are right. A
retained Paragraph laid out for another density is refused by the draw and not drawn stale; a
Document asked while its ImGui context is away answers from the layout on screen. Text by its
font: as `device-advances` left it (under 2 px, 1.00 measured).

**API lines since v0.3.0** (C++ / C / Python):

| | C++ | C | Python |
|---|---|---|---|
| version | `IMTYPESET_VERSION "0.4.0"`, `_NUM 400` | `IMTYPESET_C_VERSION "0.4.0"` | `imtypeset.version() == "0.4.0"`; wheel `0.4.0.192900` |
| new enum | `ImTypesetAdvances_Default = 0`, `_Font = 1`, `_Device = 2` | `ImTypeset_Advances_*` | `imtypeset.Advances` |
| new field, last | `ImTypesetStyle::Advances` | `ImTypeset_Style.Advances` | `Style(Advances=...)` |
| new field, last | `ImTypesetBlockStyle::Advances` | `ImTypeset_BlockStyle.Advances` | `BlockStyle(Advances=...)` |
| new field, last | `bool ImTypesetDocumentStats::MeasuredWithoutDevice` | `ImTypeset_DocumentStats.MeasuredWithoutDevice` | `DocumentStats.MeasuredWithoutDevice` |
| new call | `float GetDeviceGlyphAdvance(ImTypesetFace, unsigned int codepoint, const ImTypesetStyle* = NULL, float scale = 1.0f)` [E]: for a fixed-pitch face, the cell | `ImTypeset_GetDeviceGlyphAdvance` | `device_glyph_advance(face, char, style=None, scale=1.0)` |
| changed default | Code and CodeBold on the device | same | same |
| changed layout | a fixed-pitch face on the device is on a grid (borrowed characters whole cells, inkless controls none, rigid spaces, ragged code paragraph, tabs and hang in cells) | same | same |
| changed draw | every glyph of a word within a pixel of its place; a grid word on the pixel nearest its own place | same | same |
| changed failure | `DrawParagraph` false, `InvalidArgument`, for device text laid out for another density or rasterisation | `ImTypeset_DrawParagraph` 0 | `Paragraph.draw` raises `Error` |
| changed | a Document asked off-context keeps its layout | same | same |
| sidecar | probe still 26 rows; imports `ImFontBaked::GetCharAdvance` and `IsGlyphLoaded` from the wheel (119 names) | | a 0.3.0 package and a 0.4.0 library refuse each other |

**What advtext must set for a diff view.**

1. `imtypeset==0.4.*` with its exact imgui-bundle pin; library and package from one build.
2. One `BlockKind.Code` block per hunk (or per file: see 7), `BlockFlags.FixedLineHeight`,
   `DocumentOptions(CodeOverflow=CodeOverflow.Grow)` (or `Clip`) so rows are lines and line n is
   at n line heights. `Style(Advances=...)` left at Default; nothing else for the grid.
3. Draw at a `pos` whose x is a whole number of FRAMEBUFFER pixels (`round(x * scale) / scale`)
   and a block `LeftIndent` that is whole too. Then glyphs, rectangles and marks coincide
   exactly; otherwise all are right against each other and the whole block is up to half a
   pixel from layout's answers.
4. **Columns by asking, never by counting characters.** A character can be two cells or none.
   Use `caret_rect` / `range_rects` for a mark's x, and `device_glyph_advance(Face.Code, "0",
   style, scale)` for the cell width (gutter arithmetic, a ruler). Ask again when the size or
   `io.display_framebuffer_scale` changes.
5. Marks: `MarkPlace.Behind` for a changed range, `Ink` for syntax colour, `Under` for a
   squiggle; all take byte ranges of the block's text and land on cells.
6. A line of Dear ImGui's own beside it (a gutter of numbers): the role's ImFont pushed at the
   Code face's pixel height (`device-advances` result, section 6.3). FreeType: on the grid at
   every density tested. stb: only with `PixelSnapH` and density 1.
7. What to expect: at density 1 and 2, FreeType, a cell is a whole number of framebuffer pixels
   (at density 2 that can be a number of units and a half). At 1.5 and 3 likewise. A density or
   size change lays a block out again whole: 16 ms for 5,000 lines in one block, 0.24 ms as
   twenty-line blocks (section 5). A file's worth of distinct characters is rasterised at
   layout.
8. If it keeps `Paragraph` objects in code faces across a display change it must call
   `layout` again when `draw` raises; `Document` does that itself.
9. Do not query a Document from a thread or a moment where another ImGui context is current
   expecting a re-layout: it answers from what it has (that is the point).

## 7. The Python route and the rest

All on the final tree, hashed before and after: unchanged. Interpreters named explicitly
(`../imgui-adv-text/.venv/bin/python`, imgui-bundle 1.92.900; `../pptmstr/.venv/bin/python`,
1.92.801; `/usr/bin/python3`), `-P -B`, `PYTHONDONTWRITEBYTECODE=1`. Log:
`build/device-finish-gate/python.txt` and the files beside it.

| | 1.92.900 | 1.92.801 |
|---|---|---|
| sidecar from clean, `IMTYPESET_BUNDLE_HEADERS=stand-in` | exit 0, 40 s, 0 warning lines, attested, accepted by probe, 26 rows | exit 0, 39 s, the same |
| `run_gate` | accepted (probe) | accepted (probe) |
| `mutate_gate` | 32 offsets, 93 mutants, 12 crashed, **0 survived** | the same |
| `check_document_edits` / `check_swap_answers` / `check_device_advances` | 23 / 18 / 70 PASS, 0 FAIL | 23 / 18 / 70 PASS, 0 FAIL |
| handoff-core's new-calls script | all passed | all passed |
| Xvfb (`tools/shot.sh`): `bundle_host.py` | exit 0, `selection: (74, 284) layouts: 2` | exit 0 without `--screenshot`; exit 1 with it (numpy is not in that venv: known at v0.3.0) |
| `bundle_demo.py`, 8 runs | all exit 0, `last error: 0` | all exit 0 |
| `hidpi_check.py` at 1, and assumed 1.5, 2, 3 | 4 PASS, 0 off the grid | 4 PASS (two of them re-run without a screenshot, as above) |
| `font_lifetime.py`, 7 scenarios | all done, no errors | all done, no errors |
| wheel, `build_wheel.py --headers stand-in` | `imtypeset-0.4.0.192900.dev0+standin-cp311-none-manylinux_2_28_x86_64.whl`, 605,517 bytes, 11 files | `…0.4.0.192801.dev0+standin…`, 605,518 bytes |
| `check_wheel.py`; `--release --tag v0.4.0` | ok; refused ("NOT A RELEASE", as a stand-in build must be) | the same |
| throwaway environment (`local_env.py`), `pip install --no-index --pre`, `pip check` | installed, no broken requirements | the same |
| `check_installed.py` (`--other-python`, `--dev-library`, `--self-hosted-library`) | **92 PASS, 0 FAIL, 0 SKIP** | **92 PASS, 0 FAIL, 0 SKIP** |

| | |
|---|---|
| `tools/ci/test_wheel.py` | 24 tests OK, with `/usr/bin/python3` and with the throwaway environment's |
| `tools/ci/test_fetch_headers.py`; `check_workflows.py`; `tools/version.py --check --tag v0.4.0` | 17 tests OK; 3 files ok; every copy agrees |
| `tools/oracle/pin_reference.sh` | `32e3953`, 179 files, matches `REFERENCE.lock` |
| oracle read-only modes, pinned tree | `--verify`: `dump_core` 599 cases / 6,624 checks, `dump_para` 253 / 2,088, `dump_selection` 57 / 25,267, `dump_blocks` 69 / 284, `dump_tables` 64 / 262, `dump_doc_extras` 28,735 records, `dump_math` 8,555 / 10,728, `dump_specimen`; `--check`: `dump_doc_tables`, `dump_specimen` identical; `gen_mathtext_tables.py --check` up to date; `manifest.py --check`: 160 files match. No golden changed |
| `clang-format --dry-run -Werror` | the 131 tracked C and C++ files and the new test: exit 0 |
| `PYTHON=<advtext venv python> sh tools/release.sh v0.4.0` on a committed scratch copy under `build/` (815 files; removed since) | exit 0: "v0.4.0: everything this script checks holds"; it printed the two commands and **created no tag** (the copy had none; this repository has `v0.3.0` only). On this tree it stops, as it must, at "uncommitted changes" |

Mistakes of my own script, corrected and re-run, none of them the tree's: the oracle scripts
first ran without `PYTHONPATH=build/advtext-reference` (they refused, exit 2); the new-calls
script first ran from the wrong directory depth (it finds the fonts from its own path).
NOT run: `sidecar_probe.py`, `shellcheck`, pptmstr's patch checks, the README's commands by
hand, `demo_sdl2`.

## 8. What has never run

- **macOS, Windows, MSVC, Clang**: nothing compiled but GCC 12.2.0 on Linux x86-64.
- **The GitHub workflows** (`wheels`, the sidecar matrix): never run; nothing was pushed.
- **The fork's real headers**: both sidecars and both wheels are built against STAND-IN headers
  (upstream Dear ImGui v1.92.9), accepted by the field probe, not by ImGui's own layout check.
- **A real display, a Retina display**: density above 1 is `io.DisplayFramebufferScale` set in a
  headless frame or `--assume-density` on Xvfb.
- advtext's and pptmstr's own views; FreeType's light and mono hinting; FreeType 2.13.
- `bench_draw`, `bench_reflow`, `bench_marks`, `bench_tables` before and after.
- The font corpus dump (told not to).

## 9. Files changed (working tree against `5077b22`)

Edited: `include/imtypeset.h`, `include/imtypeset_c.h`, `src/blocks.{h,cpp}`, `src/context.h`,
`src/device.{h,cpp}`, `src/draw.{h,cpp}`, `src/fontmetrics.h`, `src/imtypeset.cpp`,
`src/imtypeset_c.cpp`, `src/imtypeset_document.{h,cpp}`, `src/style.{h,cpp}`,
`python/imtypeset/_capi.py`, `vcpkg.json`, `ports/imtypeset/vcpkg.json`,
`tools/ci/expected_imgui_symbols.txt` (regenerated by its tool: two names more),
`bench/bench_code.cpp`, `tests/test_device_advances.cpp`, `tests/test_specimen.cpp`,
`tests/test_c_api.c`, `tests/python/test_binding.py`, `docs/decisions.md` (D79, D80, D81),
`docs/CHANGELOG.md`, `docs/performance.md`, `docs/port-hazards.md`, `docs/sidecar.md`,
`docs/versioning.md`.

New: `tests/test_device_grid.cpp` (untracked: not added, as told), and under `.concerns/` the
files named `device-finish--builder--*` (this result, progress, the reproduction at head, the
benchmark output, four pictures).

Not edited: `docs/advtext-swap-status.md`, any golden, any CMake file, `python/imtypeset/_bundle.py`
and the shim (no new ImGui field is read, so no probe row was due), `../imgui-adv-text`.

Build directories of mine under `build/`, safe to delete: `device-finish`, `-rel`, `-stb`,
`-w16`, `-asan`, `-py`, `-shots`, `-bundle-advtext`, and `device-finish-gate` (logs, both
sidecars, both wheels, two throwaway environments, the Xvfb pictures). The
presets' own directories were rebuilt from clean by the gate. `build/advtext-reference` and
`.deps/` were not touched.

## 10. What I am least sure of

1. **Ruling 6 as I left it.** I did not set `LoadNoRenderOnLayout`, on a judgement about
   writing a host bit-field the wheel cannot attest. If one block of thousands of distinct
   ideographs matters, that judgement costs atlas area and tens of milliseconds at first
   layout, and you may want it overturned (D80 has the measurement).
2. **The witness is complete for a fixed-pitch face and not proved complete for a
   proportional one.** It re-asks everything only when the first glyph read has left its
   entry. Every in-place route I know (the debug window's, `SetFontLoader`, a merged source)
   destroys the entries; one that did not would leave prose on the device stale. Detection is
   also a frame late for a change made inside a frame (measured: consistent old cell, then
   consistent new one).
3. **A grid word is no longer led**: the space before an inline code run in justified prose
   can now be a pixel different from what the word-gaps rule gave. `test_word_gaps` passes
   unchanged and I looked at one picture of it; I did not measure its tallies before and
   after, and it is the owner's axis.
4. **`DrawParagraph` now refuses where it drew.** A host that keeps code-face Paragraphs and
   ignores the return value shows nothing after a density change until it lays out again. It
   is the ruling; it is also the change most likely to surprise an existing host.
5. **U+200B and friends take no cell even where the font gives them an advance**, and a
   guest slightly wider than a cell (a ligature at 0.55 em) takes two. Both follow the
   ruling's letter; a terminal agrees on the first and has no opinion on the second.
6. **Ten presets' full suites ran on the tree before the last test-only write**, not after
   (section 1). I judged the re-run of that one binary everywhere sufficient.
7. **Prose cells of a table can still be drawn a pixel apart in one column** (the leading
   rule, untouched for text not on a grid). Nobody asked; it is visible only by measuring.
