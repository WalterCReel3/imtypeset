# word-gaps — result (builder-13)

The eye was right and the cause is in draw. Under the reference's pen rule a word space carries
everything the word before it gathered by rounding its gaps; measured, spaces are drawn at under
60 % of the width layout set 1.4 % of the time, and at **less than zero width** at font sizes 13
and 17. `DrawRules::product()` now holds a word to its laid-out width (0.5 px) and places a word
after a space from the word before it. Every space between two words is within 1.5 px of its
layout width (tested, strict); spaces under 60 % fall from 530 to 11 of 37,778. `reference()` is
byte for byte what it was: no golden, tolerance or skip touched.

It is paid for in two currencies, and both are the user's kind of call: **8.9 % of letter pairs
inside words are now more than half a pixel from their laid-out distance** (reference: none;
`whole`, which the user ranked last: 18.4 %), and **the public draw is about 10 % slower**.

A second cause is NOT in draw and is not fixed: the line breaker sets some lines' spaces at 2.4 to
2.9 px (72 % of natural). Those lines still read tight. See "What is left".

Files written: `src/draw.h`, `src/draw.cpp`, `tests/test_word_gaps.cpp` (new), and ONE line plus
its comment in `tests/test_api.cpp` (not in my list; see "Outside my files").
`tests/test_draw.cpp` was not changed. Doc text: `.concerns/word-gaps--builder--doc-additions.md`.
Crash log: `.concerns/word-gaps--builder--progress.md`.

## 1. The measurement

`tests/test_word_gaps.cpp`, test `word_spaces_measured_under_each_rule`, run with
`IMTYPESET_WORD_GAPS_REPORT=1`. Inputs: the pane's two prose paragraphs (as plain text with
formulas: the pane's bold run is not reproduced, so its line breaks differ a little from the
screenshot's), the demo's three, and the 8 paragraphs of `bench/data/sample.txt` longer than 200
bytes at 520 and 337 px; justified; sizes 13 to 20 as `EmSize` and again as `FontSize` (the pane
and the demo use `FontSize`, where "16 px" is an em of 13.65); origins 8.0 and 8.375. Each
candidate rule is worked from the shipped commands by the draw loop's own arithmetic, and that
arithmetic is checked: it reproduces every pen of a real draw under `reference()` and under
`product()`. Full tables, every candidate, every size:
`.concerns/word-gaps--builder--report-freetype.txt`, `...-report-stb.txt`.

A "space" is a set glue with a glyph of text on each side on one line. "Ideal" is the width the
line breaker set the glue to. Pen to pen: drawn space = ideal + (pen distance - layout distance)
of the two glyphs either side. Ink to ink: from the quads ImGui was given.

### Spaces, reference (`advance`) against product, FreeType 2.12.1

| size | spaces | under 60 % of ideal: ref / product | over 1 px off: ref / product | least, % of ideal: ref / product | most lost, px: ref / product | ink under 2 px: ref / product |
|---|---|---|---|---|---|---|
| em 13 | 2396 | 3 / 0 | 370 / 28 | 57 / 68 | 1.42 / 0.99 | 2 / 2 |
| em 14 | 2384 | 51 / 1 | 339 / 29 | 28 / 55 | 2.04 / 1.15 | 20 / 0 |
| em 15 | 2366 | 15 / 0 | 230 / 21 | 41 / 63 | 2.43 / 1.25 | 2 / 0 |
| em 16 | 2348 | 5 / 0 | 294 / 13 | 50 / 61 | 2.04 / 1.28 | 0 / 0 |
| em 17 | 2332 | 25 / 0 | 285 / 13 | 44 / 68 | 2.80 / 1.30 | 1 / 0 |
| em 18 | 2320 | 15 / 0 | 293 / 15 | 29 / 75 | 2.40 / 1.25 | 1 / 0 |
| em 19 | 2298 | 7 / 1 | 347 / 17 | 46 / 59 | 2.27 / 1.37 | 1 / 0 |
| em 20 | 2290 | 24 / 0 | 340 / 21 | 36 / 72 | 3.10 / 1.26 | 0 / 0 |
| font size 13 (em 11.1) | 2426 | 129 / 5 | 278 / 18 | **-12** / 41 | 2.77 / 1.33 | 138 / 23 |
| font size 14 | 2418 | 81 / 1 | 291 / 21 | 11 / 51 | 2.34 / 1.28 | 29 / 10 |
| font size 15 | 2400 | 11 / 1 | 204 / 20 | 37 / 56 | 1.63 / 1.16 | 11 / 9 |
| font size 16 (em 13.65) | 2390 | 41 / 2 | 393 / 21 | 27 / 52 | 2.37 / 1.22 | 37 / 0 |
| font size 17 | 2378 | 99 / 0 | 390 / 13 | **-5** / 68 | 2.96 / 1.31 | 31 / 0 |
| font size 18 | 2360 | 7 / 0 | 367 / 29 | 47 / 69 | 2.25 / 1.33 | 1 / 0 |
| font size 19 | 2342 | 5 / 0 | 226 / 16 | 54 / 67 | 2.05 / 1.22 | 0 / 0 |
| font size 20 | 2330 | 12 / 0 | 281 / 13 | 51 / 73 | 2.14 / 1.20 | 1 / 0 |
| **all** | 37,778 | **530 / 11** | **4,928 / 308** | -12 / 40 | 3.10 / 1.37 | 275 / 44 |

Root mean square error of a space: 0.67 px -> 0.36 px. stb_truetype: the pens, and so every
pen-to-pen column, are identical; the ink columns differ by a few (ink under 2 px, all sizes:
256 / 43).

### The smallest, and its cause

Smallest under the reference: `"missing" | "feature"`, sample, font size 13, width 520, origin
8.375: glue natural 2.746 px, set 1.918 px, **drawn -0.224 px** = 1.918 - 0.427 (anchor of
"feature" rounds down) - 0.383 (anchor of "missing" rounds up) - 1.332 ("missing" gathered over
its 6 gaps). The third term is the cause; it is the only one without a bound. Words gather up to
5.64 px (em 20); 2,894 of 42,176 words are a pixel or more out.

One from the pane image, by hand — "nodes and," in line 1 at font size 16, natural 3.38 px, set
2.45 px (my plain-text setting of that paragraph; the pane has "active nodes" in bold):

    n   belongs at 203.674   anchored at 204                       +0.326
    o   211.207              gap 7.533 -> 8, at 212                +0.467
    d   218.994              gap 7.787 -> 8, at 220                +0.213
    e   226.694              gap 7.700 -> 8, at 228                +0.300
    s   233.927              gap 7.233 -> 7, at 235                -0.233   (gathered +0.747)
    a   243.421              anchored at 243                       -0.421
    layout s..a 9.494, drawn 8: space 2.45 - 0.421 - 0.326 - 0.747 = 0.96 px, ink to ink 1 px

Under `product()` the same space is 1.96 px, ink 2: "nodes" is 0.747 too wide, so its gap nearest
a tie (n-o, 7.533) is rounded down instead, and "and," is led from the `s`.
Every space of the shot paragraphs, both rule sets, and the narrowest of each glyph by glyph:
`.concerns/word-gaps--builder--spaces-of-the-shots.txt`.

### Not the cause

- Hinted advances disagreeing with layout advances: nothing is placed from a rasteriser's
  advance; pens come from layout x alone (stb's pens equal FreeType's).
- The space glyph: a word space is glue, not a glyph.
- Justification: not the cause of *closed* spaces, but see "What is left".

## 2. The decision

Two switches in `DrawRules`, both on in `product()`, off in `reference()` (`src/draw.h` says what
each does and what the reference does instead; `Drawer::settle_word`, `Drawer::place_commands`):

1. **`word_width_held`** — when a word ends more than 0.5 px wider or narrower than layout has
   it, the gaps that were rounded furthest in that direction are rounded the other way, one per
   whole pixel, earliest of equals. Depends on the word alone: one picture wherever it stands.
2. **`word_spaces_led`** — a word after a word and nothing but glue is placed from that word's
   last glyph by the rounded layout distance, clamped to the pixel either side of its own place.
   Line starts, and words after a formula, rule or atom, are anchored as before.

Bound: space error < 1.5 px (first glyph of a word < 1 px from its place, last < 1.5 px; under
the reference the last is up to 5.7 px out, which is also how far ink stood from a selection
rectangle). Asserted over all 37,778 spaces in
`under_the_products_rules_a_space_is_within_a_pixel_and_a_half_of_its_laid_out_width`.

Candidates measured (all sizes, em + font size; spaces under 60 % / over 1 px off / letter pairs
over 0.5 px off, of 37,778 and 150,624):

| rule | under 60 % | over 1 px | pairs over 0.5 px | worst pair |
|---|---|---|---|---|
| advance (reference) | 530 | 4,928 | 0 | 0.50 |
| whole | 76 | 1,460 | 27,680 | 0.99 |
| hold 0.5 by nearest tie | 76 | 1,460 | 13,348 | 0.85 |
| hold 1.0 by nearest tie | 249 | 3,310 | 3,032 | 0.71 |
| advance + led | 261 | 2,251 | 0 | 0.50 |
| **hold 0.5 + led (product)** | **11** | **308** | 13,348 | 0.85 |
| hold 0.75 + led | 46 | 650 | 6,582 | 0.78 |
| hold 1.0 + led | 93 | 1,222 | 3,032 | 0.71 |

### What it costs, plainly

- **Evenness inside words.** 13,348 of 150,624 pairs (8.9 %) more than half a pixel from their
  laid-out distance; 3,632 (2.4 %) more than 0.6; 118 (0.08 %) more than 0.75; worst 0.85. The
  reference has none over 0.5. This is the axis on which the user chose `advance` over `whole`
  (advance-rule.md section 6); the product's rule sits between them and closer to `advance`, but
  it is not `advance`. If the user weighs it otherwise, `hold 0.75 + led` is one constant
  (`WORD_WIDTH_BOUND_PX`) away and halves the uneven pairs for four times the collapsed spaces.
- **Position depends on the line.** A word after a space can stand a pixel left or right
  depending on the words before it. Two lines that start a word at one layout x after different
  words can differ by a pixel.
- **A glyph in the middle of a long word** can still be up to 2.8 px from its place (measured);
  only the ends are held.
- **Time**: next section.

## 3. Images (looked at)

Reference above, product below, 3x, soft rasteriser, dark theme as in the screenshots:
`.concerns/word-gaps--builder--shot-pane-fs16-w626-x3.png` ("nodesand," / "howbadthe" open up),
`...-pane-fs13-w520-x3.png` ("keepsa listof" -> "keeps a list of"; the clearest),
`...-demo-paragraph-fs19-w250-x3.png` ("everyday output", "this page is"),
`...-demo-math-fs19-w460-x3.png`, `...-demo-math-refused-fs19-w460-x3.png`,
`...-pane-2-fs16-w626-x3.png`, `...-demo-paragraph-fs17-w250-x3.png`. Regenerate:
`IMTYPESET_WORD_GAPS_SHOTS=<dir> test_word_gaps --filter=pictures`.
Looked at all but the last two. I could not pick out the letter gaps that changed.

Demo pages: `example_demo_headless` re-run into a scratch directory and recompressed with
Pillow: all nine are **byte-identical to the `docs/images/demo-*.png` already there** (made at
09:56 by api-document's builder with this rule already in the tree), so nothing was rewritten.
Looked at paragraph, math and document, and at 4x crops of the lines that looked closed at 1x
("Set in text style", "this page is", "processor behind"): the spaces are there, 3 to 4 px of
ink; those lines are set tight by layout (2.89 px of a natural 4.01).
`docs/images/py-bundle-pptmstr-pane.png` predates the change and was not re-shot (needs the
wheel route).

## 4. bench_draw

Release, GNU 12.2.0 `-O3 -DNDEBUG -ffp-contract=off -fno-exceptions -fno-rtti`, FreeType 2.12.1,
Ryzen 7 PRO 4750U. Two builds of one snapshot of the tree differing only in `src/draw.cpp` and
`src/draw.h` ("before" reconstructed by removing this task's edits; it passes `test_draw` and
`test_harden`), run alternately 8 times, load average 1.7 to 3.5:
`( ulimit -v 6000000; timeout 300 build/word-gaps-rel{A,B}/bench/bench_draw )`.
Median of the 8 medians, us:

| row | before | after | |
|---|---|---|---|
| full draw, x1 (public API: product rules) | 110.8 | 122.4 | +10.5 % |
| full draw, x10 | 1342.6 | 1452.5 | +8.2 % |
| full draw, x100 (ms) | 13.54 | 14.67 | +8.3 % |
| culled, host skips, x1, scroll 0.25 | 48.06 | 51.93 | +8.1 % |
| widget frame, x1, scroll 0.25 | 56.25 | 61.55 | +9.4 % |
| draw the paragraph (498 glyphs) | 10.51 | 11.75 | +11.8 % |
| sample: Drawer into a draw list (reference rules) | 73.2 | 74.7 | +2.0 % |
| sample: placement alone (reference rules) | 43.85 | 45.5 | +3.7 % |
| sample: Drawer into buffers (reference rules) | 69.7 | 71.1 | +2.0 % |
| mixed: Drawer into a draw list (reference rules) | 81.0 | 83.3 | +2.9 % |
| mixed: placement alone (reference rules) | 49.2 | 51.6 | +4.9 % |

About 3 ns a glyph under product rules. With each rule alone ("placement alone", a private copy
of the benchmark taking the rules from an environment variable, one quiet series): reference
47.1, hold only 53.6, led only 50.4, both 55.5. Two other shapes of the hold cost twice as much
(+12 us) and were dropped; details in the doc additions. `bench_draw` itself has no Drawer row
under product rules — worth adding by whoever owns it.

A defect found on the way and fixed before this report: the first hold made one pass over the
word per pixel of drift, and `test_stress_text`'s megabyte word took 362 s (the lead relayed the
same from api-document's builder). It now selects the gaps in one pass: 2.0 s for that test in
debug. `a_word_of_twenty_thousand_glyphs_...` holds it cheaply.

## 5. What was run

Live tree, unnarrowed, after the last change to `src/`, each in its own `build/word-gaps*`
directory, `-j4`, `-Werror`, every binary guarded:

| configuration | build | ctest |
|---|---|---|
| debug, FreeType 2.12.1, 32-bit ImWchar | 0 warnings | 40 of 40 |
| release | 0 warnings | 40 of 40 |
| ASan + UBSan (`hard_rss_limit_mb=3000:halt_on_error=1`) | 0 warnings | 40 of 40, no report |
| stb_truetype | 0 warnings | 40 of 40 |
| 16-bit ImWchar | 0 warnings | 40 of 40 |

`test_word_gaps`: 12 tests, 8,633 checks (bound over all spaces; one picture at 8 origins; six
cases worked by hand with a fake binder, including a missing glyph, a formula, a rule, a line,
two glues; the 20,000-glyph word; the measurement's self-check). `test_draw` 16 tests / 17,135
checks and `test_harden` 22 / 474,784 unchanged and green.

**Not run:** the presets themselves (private directories with the same flags instead); the
throwing build; clang; MSVC; the SDL2 demo and anything on a GPU; the Python hosts and the wheel
route (so no new pane screenshot); `test_word_gaps` under ASan after its last edit, which only
touched the picture test's printing. After the five configuration runs nothing in `src/` changed.
Not measured: other faces than the bundled Roboto, sizes outside 13 to 20 px, densities other
than 96, ragged or centred text (same rule, counts not taken), nothing compared with TeX.

## 6. Outside my files

- **`tests/test_api.cpp`, one line.** `text_to_commands_to_vertices_equals_the_reference_chain`
  compares the PUBLIC draw (product rules) with the reference chain; it already selected
  `TexRules::reference()` and now selects `DrawRules::reference()` as well. The comparison is not
  relaxed. It was the answer to the lead's question (the change does not leak into `reference()`:
  `test_draw`'s golden replay is green). Verified in a snapshot before touching the live file.
- `git status` shows `src/draw.cpp`, `src/draw.h`, `tests/test_draw.cpp` as untracked (`??`),
  i.e. they were never in the index; I did not `git add` anything (CLAUDE.md, D4).

## 7. What is left (not draw's)

1. **Tight lines are layout's.** 1,774 of 37,778 spaces are set under 75 % of natural; the
   narrowest 1.83 px (font size 13), typically 2.4 to 2.9 px at 16 to 19 px. A 2.45 px space is
   drawn as 2 or 3 px under any pen rule. In `demo-paragraph.png` such a line sits under one
   stretched to 7.9 px. If the user still sees "tight" after this change, the knob is the line
   breaker's (how much a space may shrink at screen sizes, or the tolerance), in
   `src/style.*`/`src/linebreak.*`/`TexRules`, not here.
2. **Spaces beside a formula** (not led, by design: a formula is placed whole on its own pixel):
   430 measured; over 1 px off 66 -> 27, range -2.80..+2.78 -> -1.34..+1.56 px. Better only
   because the word before is now held.
3. `bench/bench_draw.cpp`: a "Drawer under product rules" row.
4. `docs/images/py-bundle-pptmstr-pane.png` re-shot through the wheel.

## Housekeeping

`build/word-gaps*` removed. Nothing `git add`ed or committed.
