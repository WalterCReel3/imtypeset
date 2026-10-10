# word-gaps — text for the shared documents (builder-13)

Nothing below has been applied. Numbers are from `tests/test_word_gaps.cpp`
(`IMTYPESET_WORD_GAPS_REPORT=1 test_word_gaps --filter=measured`; full output in
`.concerns/word-gaps--builder--report-freetype.txt` and `...-report-stb.txt`) and from
`bench_draw` as described in section 3.

## 1. docs/decisions.md — a new entry (next free number)

### Dnn. Word spaces: under `DrawRules::product()` a word is held to its laid-out width and a word after a space is placed from the word before it (task `word-gaps`)

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
- Time: section 3.

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

## 2. docs/divergences.md — with the draw rules (after the bullet beginning "Under `DrawRules::product()`, a rule inside a rigid span…", or its replacement from d7-fix)

- **Under `DrawRules::product()`, a word is held to its laid-out width and a word after a space
  is placed from the word before it** (`word_width_held`, `word_spaces_led`, `src/draw.h`). The
  reference rounds every gap of a span and anchors every span on the pixel nearest its own
  place, so what a word gathers comes out of the space after it: measured, spaces drawn at less
  than no width. The product gives back whole pixels through the gaps that were nearest to
  rounding the other way, and anchors a word a rounded space after the last, within a pixel of
  its place. `reference()` is unchanged and is what every golden replays under;
  `tests/test_word_gaps.cpp` measures both.

## 3. docs/performance.md — a paragraph for the draw section

Holding words and leading spaces (`DrawRules::word_width_held`, `word_spaces_led`) cost the
public draw about a tenth. Release, GNU 12.2.0 `-O3 -DNDEBUG -ffp-contract=off -fno-exceptions
-fno-rtti`, FreeType 2.12.1, AMD Ryzen 7 PRO 4750U; two builds of one snapshot of the tree that
differ only in `src/draw.cpp` and `src/draw.h`, run alternately eight times with the load average
between 1.7 and 3.5, `( ulimit -v 6000000; timeout 300 build/<dir>/bench/bench_draw )`; the
figure is the median of the eight medians, in us unless marked:

| row | before | after | |
|---|---|---|---|
| full draw, x1 (product rules) | 110.8 | 122.4 | +10.5 % |
| full draw, x10 | 1342.6 | 1452.5 | +8.2 % |
| full draw, x100, ms | 13.54 | 14.67 | +8.3 % |
| culled, host skips, x1, scroll 0.25 | 48.06 | 51.93 | +8.1 % |
| widget frame, x1, scroll 0.25 | 56.25 | 61.55 | +9.4 % |
| draw the paragraph (498 glyphs) | 10.51 | 11.75 | +11.8 % |
| sample: Drawer into a draw list (reference rules) | 73.2 | 74.7 | +2.0 % |
| sample: placement alone (reference rules) | 43.85 | 45.5 | +3.7 % |
| mixed: Drawer into a draw list (reference rules) | 81.0 | 83.3 | +2.9 % |
| mixed: placement alone (reference rules) | 49.2 | 51.6 | +4.9 % |

That is about 3 ns a glyph under the product's rules, of which the hold is about two thirds
(measured on "placement alone" with each rule switched on by itself: +6.5 us and +3.3 us of
47.1). The rows through `detail::Drawer` run under the reference's rules and pay only for the
state the two rules keep. Three shapes of the hold were measured before this one: finding the gap
to give by walking the word's commands again (+12 us on the same row), keeping the largest
rounding up and down as each glyph is placed (+12 us, with or without branches), and the one
kept, which stores each gap's rounding with its op (`DrawOp::x1`) and looks only at the ops of a
word that is out. A word out by more than a pixel has its gaps selected, not found one pass at a
time: a megabyte with no space in it is placed in one pass (`test_stress_text`, 2.0 s for the
whole test in debug, where a pass a pixel took 362 s).

`bench_draw` has no row that draws through `detail::Drawer` under `DrawRules::product()`; the
per-rule figures above came from a private copy of the benchmark with the rules set from an
environment variable.
