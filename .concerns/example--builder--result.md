# example — result

Files: `examples/minimal.cpp` (82 lines; about 25 of them typeset-related), `examples/specimen.cpp`,
`docs/images/{paragraph,widths,faces,math-inline,math-display,math-refused,selection}.png`
(4.4 MB together; the writer does not compress). Nothing else in the tree touched. Findings for the
lead: `.concerns/example--builder--visual-findings.md`.

## What exists

- `example_minimal [font dir]`: imgui.h + imtypeset.h only. CreateContext, two fonts (Regular,
  Math), EnableMath, three headless frames of `ImGui::TextWrapped` + `ImTypeset::TextJustified`
  with a formula. Exits 0 only if there was exactly 1 layout in 3 frames and vertices were emitted.
- `example_specimen [out dir]`: seven pages through `tests/support/soft_raster.h`; with no
  argument renders everything and writes nothing. Also prints, for three paragraphs at 36 widths
  (150..500 px step 10), the text of each line, for comparison with the reference.

## What was run

| dir | config | ctest (smoke, example_minimal, example_specimen) |
|---|---|---|
| `build/example` | debug, FreeType 2.12.1, 32-bit ImWchar | 3/3 |
| `build/example-stb` | `-DIMTYPESET_USE_FREETYPE=OFF` | 3/3 |
| `build/example-wchar16` | `-DIMTYPESET_IMGUI_WCHAR32=OFF` | 3/3 |
| `build/example-asan` | ASan+UBSan | 3/3, no report (after fixing a use-after-free of MINE in minimal.cpp: `GetStats()` read after `DestroyContext()`) |

Scratch directories `build/*/examples/run_*` are empty after ctest. clang-format applied to both
files. Zero warnings under the project flags.

**Line breaks against the reference**: PROSE, LINE_MATH (7 inline formulas), INLINE_SUM (7 inline
formulas) of `examples/specimen.cpp`, each at 36 widths, `EmSize = 16`, justified: the port's lines
(through `SetParagraphText`/`LayoutParagraph`) and the reference's
(`markdown.build_document(math=markdown_math())` -> `ui.compose_blocks` -> `ship`, grouped by `Line`)
are identical in all 108 cases, line for line, formula for formula. Reference SVGs for four of them
were written under `$TMPDIR`, rasterised with ImageMagick and looked at: same lines, same looseness
at 170 px, formulas in the same places. Not compared: glyph x positions to the pixel.

**Builds**: the 16-bit ImWchar build's seven PNGs are byte-identical to the default build's, so
every math glyph (plane-1 italics, the minted big operators and delimiters) survives. The stb
build's differ in antialiasing only as far as I looked (`math-display`, `math-inline` viewed: all
math glyphs present, same positions).

## What the images show (FreeType build; measured on the pixels where a number is given)

- `paragraph.png` — TextWrapped | ragged | justified, 250 px, ImGui font size 19. Justified: every
  line but the last ends at x = 800 or 801, the margin's last pixel being 801; left ink at the
  margin. Ragged: first-fit, 10 lines against TextWrapped's 9 (a line that fits ImGui's measure by
  5 px does not fit here; not investigated further — ImGui's advances and the typeset ones differ).
  Line pitch 22 px against ImGui's 19 (LineHeight 1.35 em). Crispness: the share of mid-tone
  pixels among inked pixels is 0.471 / 0.471 / 0.470 for the three columns, i.e. the typeset
  glyphs are exactly as sharp as ImGui's own; stems are single dark columns in the 4x zoom.
- `widths.png` — justified at 170 / 240 / 330 px, em 16: 14 / 10 / 7 lines, right ink within 2 px
  of the margin on every non-final line (179-181 of 181; 439-441 of 441; 789-791 of 791). The
  170 px column is loose (3-word lines spread wide), as in the reference's SVG.
- `faces.png` — regular, bold, italic, bold italic, code, 1.5x, 0.75x and a coloured bold run in
  one paragraph. Baselines: the bottom ink row of every `H` on line 1 is the same row (25) in all
  five faces, and on line 2 the same row (50) for the 1.5x, 0.75x, coloured and regular runs.
  The NoBreak code span moved whole to the last line. Nothing clipped.
- `math-inline.png` — inline formulas sit on the text baseline; scripts, the text-style sum with
  limits beside it, the integral, fractions, `\left(`/`\right)` (one size up from the typed pair,
  as in the reference: U+FFFBE/U+FFFB7), two root signs, upright `\text`. Lines holding a fraction
  are set further apart and only those. Right margin held (449-451 of 451). Defect-ish: math
  glyphs are visibly lighter than the text and the nested fraction is illegibly small (finding 4).
- `math-display.png` — display sum with stacked limits (minted big sigma), integral with bounds,
  `\left( \sum \frac \right)^2 \le \sqrt{n} \quad \text{for every } n` with tall minted
  parentheses: all present, centred (sum spans 157-267 in a 12-412 column), unclipped. A refused
  `$$\begin{matrix}...$$` is shown as its source in the code face, centred.
- `math-refused.png` — `$5 and $10` stay text, `\$` shows a dollar without its backslash, matrix
  and `\hat` are shown as source with dollars; the reasons from `GetParagraphFormula` are listed
  under it. Spaces inside the refused source stretch (finding 2).
- `selection.png` — a word-granularity drag between two marked points: highlight starts at the
  left edge of "formula" (x = 124, the reference's 112.44 + the 12 px margin), covers the whole
  formula line, ends after "between"; the copied text printed under it is exactly that range with
  the formula as `$\sum_{i=1}^{n} i = \frac{n(n+1)}{2}$`. Defect: 2 px white seam between
  lines 2 and 3 of the highlight (finding 1; the reference has it too).

## Where the API made the example awkward (input for `api-revise`)

1. **No line geometry.** To print each line's text I hit-test the left edge at
   `height * (line + 0.5) / lines` and convert the index to a byte offset. That assumes equal
   line pitch, which a line with a fraction breaks; it happened to work. (R10.)
2. **No baseline.** A paragraph drawn with `DrawParagraph` cannot be aligned with ImGui text
   beside it, and the specimen could not draw a baseline rule to show alignment.
3. **Per-call alignment means writing to the global style** (`GetStyle().Align = ...` before each
   `Text()`), and `GetStyle() = ImTypesetStyle()` to reset. (R8.)
4. **Two sizes.** `FontSize = 0` follows ImGui (line height); matching the reference or a design
   spec needs `EmSize`. I used both in one program and had to explain it in comments.
5. **Runs are byte offsets into a string the host must assemble**; the example needs a table and
   a loop to build text and runs together. A NoBreak run that swallows a space gives an overfull
   line silently (finding 3).
6. **`GetStats()` (and `GetLastError()`) return pointers into the context.** I read one after
   `DestroyContext()`; only ASan noticed.
7. **`SelectionColor` is part of the style given to `LayoutParagraph`**, not an argument of
   `DrawParagraph`, so the highlight colour is fixed at layout.
8. **Math takes two switches** (`EnableMath()` + the flag) and a display formula must be its own
   `Text()` call. (R12.)
9. `Text(text, NULL, flags)`: every math call spells out the `NULL`. (R9.)
10. The `ImTypeset::Text` item is the full available width, so a margin guide drawn from
    `GetItemRectMax()` is the column edge, not the text's. (R8.)

## Not verified

- Glyph positions against the reference to the pixel (only line contents and a visual read of
  four rasterised SVGs).
- Ragged or centred breaks against the reference (only justified was compared).
- The stb images beyond looking at two of them; `paragraph`/`widths`/`faces`/`selection` under
  stb were not viewed.
- The preset builds; anything on a real GPU backend or a display; a font directory passed to
  `example_minimal` by argument.
- Whether `GetLastError()` is valid when `CreateContext()` returns null (both examples rely on it).
