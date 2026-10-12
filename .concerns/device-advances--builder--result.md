To:         lead
From:       builder (device-advances)
Date:       2026-10-12
Status:     done; the tree builds and passes in every configuration listed in section 8

# device-advances: H34 and H35

Everything here was measured or read by an agent (this builder), on Linux x86-64, headless:
vertices read from draw lists, no window, no GPU. "Measured" means a program in this tree was run
and its output is what is quoted; "read" means from sources and not run. Nothing was committed,
staged or tagged. Base: `edbaddb` (v0.3.0 + 1).

## 1. The reproduction, before anything was changed

`tests/test_device_advances.cpp` (new), `IMTYPESET_DEVICE_ADVANCES_TABLE=1`, at `edbaddb`, release
(`-O3`), FreeType 2.12.1 and stb_truetype, densities 1 and 2, two face tables, an em and a pixel
height of 14 to 20. The two hosts' inputs exactly: the fenced block of eight rows, the ruler of
forty digits, the row of 74 characters; and the ruler as a paragraph in the body face. A pen is a
quad's left edge less its bitmap's offset in the atlas entry, checked by the quad's texture
coordinates. All 112 rows: `.concerns/device-advances--builder--repro-head.txt`. One-minute load
average 17 to 19 from other sessions; these are positions, not timings.

**Their figures reproduce exactly at density 1.** Every role the monospace file, FreeType, density
1 (their "mono" pages); lengths in pixels:

| size | laid-out advance | ImGui's baked advance | the ruler's steps, in order | 40th: drawn / laid out / ImGui | rows apart at a column | glyph from its rectangle, worst | fill edges less pens (chars 21..40 of the 74 row) | hit tests wrong (2 points a cell) | 40-digit paragraph: glyph from its rectangle |
|---|---|---|---|---|---|---|---|---|---|
| em 14 | 7.0000 | 7 | 7 x39 | 273 / 273.00 / 273 | 0 | 0.00 | 0 / 0 | 0 of 340 | 0.00 |
| em 15 | 7.5000 | 8 | 8 x19, 7 x20 | 292 / 292.50 / 312 | 17 | +18.00 | -10 / -16 | 173 of 340 | +9.50 |
| em 16 | 8.0000 | 8 | 8 x39 | 312 / 312.00 / 312 | 0 | 0.00 | 0 / 0 | 0 of 340 | 0.00 |
| em 17 | 8.5000 | 9 | 9 x19, 8 x20 | 331 / 331.50 / 351 | 17 | +18.00 | -10 / -16 | 162 of 340 | +9.50 |
| em 18 | 9.0000 | 9 | 9 x39 | 351 / 351.00 / 351 | 0 | 0.00 | 0 / 0 | 0 of 340 | 0.00 |
| em 19 | 9.5000 | 10 | 10 x19, 9 x20 | 370 / 370.50 / 390 | 17 | +18.00 | -10 / -16 | 152 of 340 | +9.50 |
| em 20 | 10.0000 | 10 | 10 x39 | 390 / 390.00 / 390 | 0 | 0.00 | 0 / 0 | 0 of 340 | 0.00 |
| px 14 | 6.6730 | 7 | 6 x13, 7 x26 | 260 / 260.25 / 273 | 15 | -16.15 | +13 / +11 | 182 of 340 | -8.75 |
| px 15 | 7.1497 | 7 | 8 x6, 7 x33 | 279 / 278.84 / 273 | 8 | +9.35 | -8 / -5 | 128 of 340 | +5.10 |
| px 16 | 7.6263 | 8 | 7 x15, 8 x24 | 297 / 297.43 / 312 | 16 | -16.91 | +13 / +12 | 170 of 340 | -9.39 |
| px 17 | 8.1030 | 8 | 9 x4, 8 x35 | 316 / 316.02 / 312 | 6 | +7.18 | -6 / -4 | 72 of 340 | +3.59 |
| px 18 | 8.5796 | 9 | 8 x16, 9 x23 | 335 / 334.60 / 351 | 15 | -17.97 | +12 / +14 | 167 of 340 | -9.27 |
| px 19 | 9.0562 | 9 | 10 x2, 9 x37 | 353 / 353.19 / 351 | 3 | +3.78 | -3 / -2 | 24 of 340 | +1.89 |
| px 20 | 9.5329 | 10 | 9 x18, 10 x21 | 372 / 371.78 / 390 | 16 | -18.12 | +11 / +15 | 153 of 340 | -9.59 |

Against advtext's table ("P21 to P23, measured"): every cell of theirs is the same number here
(their "worst between rows" over eight rows; their fill figures; -16.91 at pixel height 16).
**New from this run:** hit tests, which neither host measured: at pixel height 16, 170 of 340
points taken a quarter and three quarters across each DRAWN cell answered another character.

What their tables did not have:

- **Density 2, FreeType.** At an EM of 14 to 20 everything is exact (the em in framebuffer pixels
  is even, so half of it is whole). At a PIXEL HEIGHT of 14 to 20 it is as wrong as at density 1:
  two or three sizes of step, rows 4 to 17 framebuffer pixels apart, a glyph up to 18.24 from its
  rectangle.
- **stb_truetype.** The drawn and laid-out figures are the same as FreeType's to the digit (the
  draw rule does not depend on the loader). Dear ImGui's own text is different: its advance is
  the FONT's (7.6263 at pixel height 16), its pens are at fractions of a pixel, and its 40th
  character is at 297.42, where layout is, not at 312.
- **The default face table at a pixel height** (their "17.6 px for a code advance of 6.400"):
  +17.40 px here at pixel height 15, rows 16 apart.

## 2. The two advances (read, `.deps/imgui` v1.92.9)

- FreeType loader: `AdvanceX = slot->advance.x / 64 / rasterizer_density`; with hinting (its
  default) FreeType has already rounded `advance.x` to a whole pixel of the raster.
- stb loader: `AdvanceX = advance_units * ScaleFactor * baked->Size`: the font's own, a fraction.
- `ImFontConfig::PixelSnapH`: `IM_ROUND(advance)` afterwards, in UNITS (after the division by the
  density).
- `ImFont::RenderText` adds advances to a float pen without rounding.
- `GetFontBaked(size)` rounds the size to a whole number of units at any density.
- This library (read and run): advances from its own OpenType reader, `advance_units * em /
  unitsPerEm`, rounded to a scaled point; the draw loop rounds each GAP to a pixel.
- The pinned reference (`build/advtext-reference`) has no seam on baked advances at all; the
  "old seam" of H34 was pptmstr's own deleted core. `docs/advance-rule.md` is the record of
  `advance` chosen over `whole`.

## 3. The design, and each decision with its reason

Recorded in full as **D77** and **D78** in `docs/decisions.md`; hazards for later work at the
end of `docs/port-hazards.md`.

1. **A second way to measure a character**, `ImTypesetAdvances_Device`: the `AdvanceX` of the
   glyph in the atlas entry the draw loop draws that face from at that size on that display,
   rounded to a whole framebuffer pixel (half up), no kerns. *Why it is enough:* the draw loop
   places a glyph by the rounded gap from the one before; a gap that is whole in the layout
   rounds to itself, so within a rigid span nothing gathers. A Code block's row is one span. **No
   new draw rule was needed for this**, and none was added.
2. **One function says which atlas entry a face is drawn from** (`face_bake`, `src/draw.cpp`,
   factored out of the binder): the draw loop and the measurement both call it. *Why:* the
   feature is the property that the advance laid out with is the advance of the entry drawn.
3. **Chosen by a style**: `ImTypesetStyle::Advances` and `ImTypesetBlockStyle::Advances` (0 in a
   block style: the document's). *Why not a block flag or a run field:* it is a property of how a
   kind of text looks, like its face; a run needs none because of 4.
4. **Default: the Code and CodeBold faces on the device, every other face by its font** (the
   lead's ruling). A Code block, an inline code run, a paragraph whose face is Code.
5. **Behind the product's rules**: `TexRules::code_on_device`. Under `TexRules::reference()` the
   default measures every face by its font. No golden was touched and the whole suite passes;
   `Device` asked for outright is the device under either.
6. **A font key carries a device number** (units to the inch + framebuffer pixels to the unit;
   a context numbers those it meets). *Why:* the memo of measurements, a paragraph's material and
   a block's layout are all keyed by the key, so a layout for one display is never reused on
   another and going back finds the old one's number. The device is part of the style as
   resolved; a document re-resolves whenever it is drawn or asked anything.
7. **stb_truetype and unhinted FreeType: "baked" is the entry's advance rounded by the
   library.** Requirements 1 to 3 hold exactly; the pens are ImGui's own only where the host
   bakes the font with `PixelSnapH` at density 1. The library does NOT set that flag itself:
   *measured*, under FreeType at density 2 it moves ImGui's own advance off the grid wherever the
   entry's advance is an odd number of framebuffer pixels.
8. **Kerns off on the device; formulas always by the math font; a borrowed character measured as
   the text it stands in.**
9. **H35 for text by its font: bound the glyph** (`DrawRules::word_glyphs_held`, D78), not move
   the answers. *Why:* drawn positions depend on the origin's fraction and on what leads a word
   along its line, so answering carets from them would make every selection query a placement.
   The bound asserted: **every glyph less than two pixels from where layout has it** (measured
   worst: 1.00 px).
10. **`GetFontMetrics` and `GetGlyphAdvance` still answer the font's numbers**; new
    `GetDeviceGlyphAdvance` answers the device's, by layout's own path.
11. **Tables, FixedLineHeight, marks, columns**: nothing needed (D77 says why); a held table's
    widths start afresh when the block's font key changes.
12. **Relation to `docs/vector-output.md`'s proposed rule 1** ("a laid-out object never holds a
    pixel decision", proposed, not accepted): text by its FONT still keeps it; text on the DEVICE
    holds one on purpose. A host that wants a layout it can zoom or export says
    `ImTypesetAdvances_Font`.

**Not decided by me: the version.** Two host-filled structs grew and a default changed; by
`docs/versioning.md` that is 0.4.0. `IMTYPESET_VERSION` is still 0.3.0 and the changelog entry
is headed "Unreleased". `tools/version.py --check` passes (every copy says 0.3.0).

## 4. After

`.concerns/device-advances--builder--after.txt` has all 336 rows (three ways of measuring, both
loaders). Same pages as the table in section 1 (mono faces, FreeType, density 1), with a style
that says nothing, which now measures code on the device:

| size | laid-out advance | ImGui's | steps | 40th: drawn / laid out / ImGui | rows apart | glyph from rectangle | fill less pens | hits wrong | 40-digit paragraph (by its font) |
|---|---|---|---|---|---|---|---|---|---|
| em 14 | 7 | 7 | 7 x39 | 273 / 273.00 / 273 | 0 | 0.00 | 0 / 0 | 0 of 340 | 0.00 |
| em 15 | 8 | 8 | 8 x39 | 312 / 312.00 / 312 | 0 | 0.00 | 0 / 0 | 0 of 340 | +0.50 |
| em 16 | 8 | 8 | 8 x39 | 312 / 312.00 / 312 | 0 | 0.00 | 0 / 0 | 0 of 340 | 0.00 |
| em 17 | 9 | 9 | 9 x39 | 351 / 351.00 / 351 | 0 | 0.00 | 0 / 0 | 0 of 340 | +0.50 |
| em 18 | 9 | 9 | 9 x39 | 351 / 351.00 / 351 | 0 | 0.00 | 0 / 0 | 0 of 340 | 0.00 |
| em 19 | 10 | 10 | 10 x39 | 390 / 390.00 / 390 | 0 | 0.00 | 0 / 0 | 0 of 340 | +0.50 |
| em 20 | 10 | 10 | 10 x39 | 390 / 390.00 / 390 | 0 | 0.00 | 0 / 0 | 0 of 340 | 0.00 |
| px 14 | 7 | 7 | 7 x39 | 273 / 273.00 / 273 | 0 | 0.00 | 0 / 0 | 0 of 340 | -0.90 |
| px 15 | 7 | 7 | 7 x39 | 273 / 273.00 / 273 | 0 | 0.00 | 0 / 0 | 0 of 340 | +0.96 |
| px 16 | 8 | 8 | 8 x39 | 312 / 312.00 / 312 | 0 | 0.00 | 0 / 0 | 0 of 340 | -0.92 |
| px 17 | 8 | 8 | 8 x39 | 312 / 312.00 / 312 | 0 | 0.00 | 0 / 0 | 0 of 340 | +0.97 |
| px 18 | 9 | 9 | 9 x39 | 351 / 351.00 / 351 | 0 | 0.00 | 0 / 0 | 0 of 340 | -0.97 |
| px 19 | 9 | 9 | 9 x39 | 351 / 351.00 / 351 | 0 | 0.00 | 0 / 0 | 0 of 340 | +0.99 |
| px 20 | 10 | 10 | 10 x39 | 390 / 390.00 / 390 | 0 | 0.00 | 0 / 0 | 0 of 340 | -0.99 |

**What is asserted, per glyph, on every one of the 56 pages of each way of measuring**
(`tests/test_device_advances.cpp`, 15 tests; FreeType and stb; densities 1 and 2; an em and a
pixel height of 14 to 20; two face tables):

| requirement | text on the device | text by its font |
|---|---|---|
| drawn where it was laid out | under 1/256 px (see "exact" below) | under 2 px asserted; 1.00 px the worst measured |
| one size of step, a whole number of framebuffer pixels; N characters N times it, on every row | exactly | not claimed |
| rows agree at every column | 0 px | at most 2 px asserted; 1 px measured |
| pens equal Dear ImGui's for the same string, ImFont and size | 0.000 px on all 49 pages where ImGui has that size, FreeType. stb: only with `PixelSnapH` at density 1 (28 pages, asserted under both loaders) | not claimed |
| selection fill's edges on the pens | 0 / 0 | within 2.5 px asserted; 1 px measured |
| caret on the pen | under 1/256 px | under 2 px |
| a point in a drawn cell is that character's (two points a cell) | 0 wrong of 19,040 code + 4,368 prose | 0 wrong of 23,408 measured (not asserted: no bound says it) |

"Exact" is to the rounding of an advance to a scaled point: a pen is a whole framebuffer pixel
and layout's position is a sum of advances each rounded to 1/65,536 pt, about 0.000005 px a
character at 96 units to the inch. That is the unit's rounding, not a tolerance on a golden; no
golden comparison has one and none was regenerated.

The 7 pages not compared with ImGui are at density 2 where the face's size in framebuffer pixels
is odd: ImGui draws whole sizes of units only and has no such line.

**Pictures, looked at** (software renderer; `IMTYPESET_DEVICE_ADVANCES_SHOTS=<dir>`):
`.concerns/device-advances--builder--shot-freetype-{1x,2x}.png`, `…-{1x,2x}-selection.png` (six
times enlarged), `…-stb-{1x,2x}.png`. Each has the ruler and the long row three times from one x,
by Dear ImGui, on the device's advances, on the font's, with red hairlines every ten cells and
characters 21 to 40 selected. What I saw: under FreeType at both densities the device block's
glyphs stand on Dear ImGui's, every tenth digit ends at a hairline, and the selection starts at
the hairline before `g` and ends between `d` and `e`; the font block is narrower (7.63) and its
selection is within a pixel of its glyphs. Under stb Dear ImGui's own rows are on the font's
advances and do NOT stand on the device block's grid, as section 3.7 says.

**Caches, lazy layout, edits, with the work counters** (`a_document_is_laid_out_again_for_
another_display_and_only_where_it_is_shown` and four more): 2,000 blocks; a density change with
no call but the draw goes over every block once without breaking a line (StructureVisits +2 a
block, the cost of a style change) and lays out exactly the blocks the window shows, the rest
deferred; a steady frame lays nothing out; an appended line lays out its one block; a size
change costs the same; going back to the first display finds the first advance and the first
device number; a document by its fonts does nothing on a density change; a block style that
says the device inside a document by its fonts is still seen; a paragraph and a widget are set
again once per display; 2,057 devices in a row wrap the numbering once and the next layout is
set again to the same lines.

## 5. Costs (measured; say the load)

Release (`-O3 -DNDEBUG -ffp-contract=off`), GCC 12.2.0, FreeType 2.12.1, Ryzen 7 PRO 4750U;
one-minute load average **5 to 14 throughout, from other sessions**. Not timings to quote.

`bench/bench_code.cpp` (new), 5,000 lines of code, 327 kB (load 6.3 at the start):

| | the device's (min / median / p95) | the font's |
|---|---|---|
| one block of 5,000 lines, laid out whole, a size not met before | 21.96 / 23.78 / 24.99 ms | 20.22 / 21.63 / 25.38 ms |
| the same, warm | 19.71 / 20.59 / 20.95 ms | 20.15 / 20.69 / 21.26 ms |
| a steady frame of a 768 px window | 50.9 / 54.7 / 58.7 us | 52.1 / 53.5 / 55.2 us |
| **the first frame after the density changed** | **17.23 / 18.98 / 23.37 ms** | 54.3 / 58.2 / 58.8 us |
| 250 blocks of 20 lines, laid out whole, warm | 19.10 / 20.83 / 30.78 ms | 19.69 / 20.00 / 20.39 ms |
| a steady frame | 57.6 / 59.3 / 63.3 us | 57.3 / 62.3 / 65.5 us |
| **the first frame after the density changed** | **271 / 277 / 290 us** | 60.3 / 64.5 / 67.2 us |

So: layout and a frame cost the same either way; what the device adds is the re-layout on a
display change, which is lazy across blocks and NOT inside one: a diff held as one block of
5,000 lines pays 17 to 19 ms once when its window changes display; as blocks of twenty lines,
a quarter of a millisecond.

`bench_draw`, this tree against `edbaddb` (built from `git archive`), six runs of each in turn,
best minimum: full draw 149.3 us before, 150.6 after; a hundred times over 17.30 and 18.14 ms;
culled 65.7 and 59.6 us; a frame of widgets 82.5 and 77.4 us. The rows disagree about the sign:
D78's hold is inside this machine's noise. (Its first form was measurably slower and was
replaced; `docs/performance.md` has the rows.)

Letter evenness, the axis the owner chose `advance` on (`tests/test_word_gaps.cpp`, 150,624
pairs, bundled faces, 13 to 20 px): pairs over half a pixel from their laid-out distance 13,348
before, 13,660 after (8.86 % to 9.07 %); over 0.6 px 3,632 to 3,834; over 0.75 px 118 to 146;
the furthest a glyph of a word stands from its own place 2.80 px to 1.91; every figure about
SPACES unchanged to the digit.

## 6. For the lead's file: row text, API lines, what advtext must set

**H34, row text.** Done, in the working tree on `edbaddb`. A style says what measures a
character: `ImTypesetAdvances_Device` is the advance of the glyph in the atlas entry it is drawn
from, at the size and display density it is drawn at, a whole number of framebuffer pixels, with
no kerns; the Code and CodeBold faces are measured that way BY DEFAULT and every other face by
its font as before. Measured by this library's builder (an agent), vertices read, FreeType
2.12.1 and stb_truetype, densities 1 and 2, an em and a pixel height of 14 to 20, your block and
ruler: on all 56 pages a glyph is drawn under 1/256 px from the rectangle `range_rects` answers,
the eight rows have one size of step and agree at every column to 0 px, and N characters take N
times one whole number of framebuffer pixels; under FreeType the pens are Dear ImGui's own for
the same string in the same ImFont at the same size on all 49 pages where ImGui has that size
(the 40th character of the ruler at 312 / 312.00 / 312 at a pixel height of 16, where it was
297 / 297.43 / 312). Under stb_truetype ImGui's own advance is the font's and its pens are at
fractions of a pixel: the first three hold there and the fourth only with `PixelSnapH` at
density 1. Word spaces: in text measured on the device a space's natural width is the device's
advance of U+0020; a justified line still stretches them. Prose is NOT on the device by default
(the owner's "I think I prefer hinted" is theirs to restate): `Style(Advances=Advances.Device)`
sets it so today. A layout on the device is for one display and is made again when the density
changes, by the draw, with no call from the host. Not measured: any real window, a Retina
display, the wheels (not rebuilt), FreeType's light, mono and no-hinting modes.

**H35, row text.** Done, both halves. Text measured on the device is drawn where it was laid
out, so the selection's fill, the caret and the hit test are on the glyphs: measured, fill edges
less pens 0 / 0 for characters 21 to 40 of the 74-character row on all 56 pages (was +13 / +12
at a pixel height of 16), carets under 1/256 px from their pens, 0 of 19,040 points in drawn
cells answering another character (hit tests: 170 of 340 wrong before, at that size). Text
measured by its FONT now has every glyph of a word held within a pixel of its laid-out distance
from the word's first glyph, so every glyph is LESS THAN TWO PIXELS from the rectangle answered
for it, however long the word: measured worst 1.00 px for the 74-character row and for the
40-digit number (were 18.2 and 9.8), two rows 1 px apart at a column at the worst (was 17). It
cost 0.2 % of letter pairs their half-pixel evenness (8.86 % to 9.07 % over half a pixel off),
nothing in the spaces between words.

**API lines** (C++ / C / Python):

| | C++ | C | Python |
|---|---|---|---|
| new enum | `ImTypesetAdvances_Default = 0`, `_Font = 1`, `_Device = 2` | `ImTypeset_Advances_*` | `imtypeset.Advances.Default / .Font / .Device` |
| new field, LAST in the struct | `ImTypesetStyle::Advances` | `ImTypeset_Style.Advances` | `Style(Advances=...)` |
| new field, LAST in the struct | `ImTypesetBlockStyle::Advances` (0: the document's) | `ImTypeset_BlockStyle.Advances` | `BlockStyle(Advances=...)` |
| new call | `float GetDeviceGlyphAdvance(ImTypesetFace face, unsigned int codepoint, const ImTypesetStyle* style = NULL, float scale = 1.0f)` [E] | `float ImTypeset_GetDeviceGlyphAdvance(ImTypeset_Face, unsigned int, const ImTypeset_Style*, float)` | `device_glyph_advance(face, char, style=None, scale=1.0) -> float`, raises `Error` |
| unchanged, said in their comments | `GetFontMetrics`, `GetGlyphAdvance`: the FONT's numbers whatever a style says | same | `font_metrics`, `glyph_advance` |
| changed default | a style that says nothing sets Code-face text on the device | same | same |
| changed draw | every glyph of a word within a pixel of its place in the word | same | same |
| sidecar | probe row 26, `ImFontGlyph.AdvanceX`; `sizeof` of the two structs +4 | | a package and a library must be from one tree |

**What advtext must set to get aligned code.**

1. **Nothing in the style**, for code: a `BlockKind.Code` block, and any run in `Face.Code`, is
   on the device by default. It must take a library and a package from this tree together
   (struct sizes and the probe both differ from `c9f8902` and from v0.3.0).
2. **Stop deriving a cell's width from the font.** Anything that computed columns from
   `glyph_advance(Face.Code, em, …)` or from half an em (its `code_size(pixel_height=)`) asks
   `imtypeset.device_glyph_advance(imtypeset.Face.Code, "0", style, scale)` instead, with the
   `Style` it lays out with and the block style's `Scale`. The answer is in ImGui's units and is
   a whole number of framebuffer pixels (so 7.5 can be an answer at density 2). Ask again when
   the size or the display changes; it needs the ImGui context.
3. **A line of ImGui's own beside it** (pptmstr's renderers, a gutter): the role's ImFont,
   `imgui.get_io().fonts.fonts[imtypeset.font_atlas_index(imtypeset.Face.Code)]`, pushed at the
   Code face's PIXEL HEIGHT for the em in use: `font_metrics(Face.Code, em).font_size`, where
   `em = pixel_height * m.em_size / m.font_size` for `m = font_metrics(Face.Regular, 1000)` when
   the style states `FontSize`. Through `push_font(font, size)`, not `add_text(font, size, …)`
   with a size that is not whole: ImGui rounds a pushed size and draws that entry, and scales
   the entry for a fractional size on fractional pens. This answers the question under "H34 and
   H35: advtext's own measurement"; the sentence is now in `imtypeset.h` at `AddFontFromFileTTF`.
4. **Start code on a whole pixel** to have the rectangles EXACTLY on the glyphs: the draw's `pos`
   and the block's left indent whole in framebuffer pixels. Otherwise the first glyph of a row
   is on the nearest pixel and every row is off by the same fraction (under half a pixel);
   columns still agree row to row.
5. **For "hinted" prose**: `Style(Advances=imtypeset.Advances.Device)`; no kerns, each word drawn
   exactly as laid out, a word's first glyph less than a pixel from its place on a justified
   line. To keep code as it was: `Advances.Font`, or `BlockStyle(Advances=Advances.Font)`.
6. **One block per file is the expensive shape**: a block is laid out whole, so a diff of 5,000
   lines as one Code block is laid out again whole (17 to 19 ms here) when its window changes
   display; as hunks, only what is shown.
7. **A view that caches anything derived from layout** (line widths, wrap columns) must drop it
   when `io.display_framebuffer_scale` changes: the library re-lays-out by itself, the host's
   own arithmetic does not.

## 7. What changed

Edited: `include/imtypeset.h`, `include/imtypeset_c.h`, `src/context.h`, `src/draw.{h,cpp}`,
`src/imtypeset.cpp`, `src/imtypeset_c.cpp`, `src/imtypeset_demo.cpp`,
`src/imtypeset_document.{h,cpp}`, `src/style.{h,cpp}`, `src/tex_rules.h`,
`python/imtypeset/__init__.py`, `python/imtypeset/_capi.py`, `python/imtypeset/_bundle.py`,
`python/host/imtypeset_bundle_shim.cpp`, `tests/test_word_gaps.cpp`, `tests/test_c_api_map.cpp`,
`tests/python/test_binding.py`, `docs/decisions.md` (D77, D78), `docs/port-hazards.md`,
`docs/CHANGELOG.md`, `docs/performance.md`, `docs/python-errors.md`, `docs/sidecar.md`,
`docs/bindings-notes.md`.

New: `src/device.h`, `src/device.cpp`, `tests/test_device_advances.cpp`, `bench/bench_code.cpp`,
`tools/bundle_route/check_device_advances.py`, and under `.concerns/` the files named
`device-advances--builder--*` (this result, the progress notes, the before and after tables, the
two benchmark outputs, the word-gaps tallies before and after, six pictures).

Not edited: `docs/advtext-swap-status.md`; any golden; any CMake file; `IMTYPESET_VERSION`.
Nothing outside the repository. No `git add`, commit or tag (an intent-to-add I made by mistake
on the test file early on was undone with `git reset` on that path; the index is as it was).

`tests/test_word_gaps.cpp` was brought to the new rule, not weakened: its own model of the
product's pens (a second implementation of the arithmetic, in the test) follows the new rule and
agrees with the draw on every pen of the corpus; two hand-worked tests now state both rules, the
old one under `word_width_held` alone and the new one under `product()`, and the 20,000-glyph
test asserts the per-glyph bound on each of them.

Build directories of mine, all under `build/`, safe to delete: `device-advances` (release),
`-dbg`, `-asan`, `-stb`, `-w16`, `-py`, `-head` and `-head-src` (an export of `edbaddb` for the
benchmark), `-bundle-advtext`, `-bundle-pptmstr`. The presets' own directories,
`build/advtext-reference` and `.deps/` were not touched.

## 8. What was run at the end, on the final tree

| | result |
|---|---|
| whole suite, debug, FreeType (`build/device-advances-dbg`) | 53 of 53, run again after the last comment edit |
| whole suite, ASan + UBSan, `hard_rss_limit_mb=3000`, two at a time | 53 of 53, 553 s; `test_device_advances` again alone after its last addition |
| whole suite, release | 53 of 53 |
| whole suite, stb_truetype (debug) | 53 of 53 |
| whole suite, 16-bit ImWchar (debug) | 53 of 53 |
| the `amalgamation` ctest | passed in each of the five |
| `python_binding`, `python_demo_sections` (`build/device-advances-py`, self-hosted library) | 102 and 4 tests, OK |
| both sidecars from clean, `IMTYPESET_BUNDLE_HEADERS=stand-in`, advtext's venv (imgui-bundle 1.92.900) and pptmstr's (1.92.801) | exit 0, 0 warning lines, accepted by probe, **26 of 26** rows; 98 `ImTypeset_*` exported, 117 Dear ImGui symbols undefined (as before: no new symbol of ImGui's is needed) |
| in both | `run_gate` accepted; `mutate_gate` 32 offsets, 93 mutants, 12 crashed, **0 survived** (the three moves of `ImFontGlyph.AdvanceX` each caught by its row); `check_swap_answers` 18 PASS; `check_document_edits` 23 PASS; `check_device_advances` (new) 70 PASS: in the real wheels the device's advance is the wheel's own entry's, whole, at densities 1 and 2 |
| `tools/version.py --check` | every copy agrees (0.3.0) |
| `clang-format` on every C++ file changed | applied; it moved only lines of mine |

## 9. What was NOT run

- Any real window: no Xvfb run of `bundle_host.py`, `bundle_demo.py`, `hidpi_check.py`,
  `font_lifetime.py`, `measure_buffer_route.py`; nothing through advtext's or pptmstr's own views.
- A Retina display, or any display: density 2 is `io.DisplayFramebufferScale` set in a headless
  frame.
- The wheels (`tools/wheel/build_wheel.py`): not rebuilt.
- The presets' own directories (`build/debug` and the rest): the same configurations in private
  directories instead. The `amalgamation` PRESET (the whole suite against the generated files),
  `fp-host`, the exceptions build, the consumer-project tests, the SDL2 demo: not run.
- FreeType's light and mono hinting and `NoHinting`; FreeType 2.13 (the reference's).
- macOS, Windows, MSVC, Clang: nothing compiled but GCC 12.2.0 on Linux.
- Device text in a table's cells, under a mark's fill, in columns, in a wrapped code line
  (`ImTypesetCodeOverflow_Wrap`: a continuation is indented by two ems, which is not a whole
  number of cells or of pixels, so its glyphs are on whole pixels but its first is up to half a
  pixel from layout and its columns do not continue the first line's).
- `bench_reflow`, `bench_marks`, `bench_tables`, `bench_font` before and after. Every timing
  here was taken on a busy machine.
- Tabs: a Code block expands them to spaces before layout as before; not measured for this.

## 10. What I am least sure of

1. **The default under stb_truetype.** There Dear ImGui's own monospace is on the FONT's
   fractional advances, so code on the device (8 px) does not stand under an ImGui-drawn line
   in the same font (7.63 px), where code by its font now does to within a pixel. I kept one
   default for both loaders because rows on one grid and selections on glyphs are what a diff
   needs and both hosts run FreeType; a host on stb that wants the ImGui line too bakes the Code
   font with `PixelSnapH` (density 1 only). It is a judgement, and the picture
   `…shot-stb-1x.png` shows what it looks like.
2. **A stale memo if the host changes the atlas's loader or its flags after text was laid out.**
   Nothing tells the library; the device's advances it remembers are then the old loader's until
   the role's fonts are added again. Not tested.
3. **`Default` changes how every existing host's code is set** (widths, line breaks of wrapped
   code), at zero in a field whose header says zero means "as before". It is the ruling, and the
   changelog says "changed"; the version number does not yet.
4. **An em whose bake rounds up**: at an em of 15 the Code face is baked at 16 px and its cell is
   8, not 7.5: the layout follows the raster that is drawn (which was already 16 px), so text
   stated in ems is wider than half an em a character suggests. That is the point of the
   feature, but a host that sized panes from the em will see it.
