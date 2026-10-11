# draw-seams — text for the shared documents (builder)

Nothing below has been applied. It goes after the entries `d7-fix` and `word-gaps` wrote for the
same rules (`.concerns/d7-fix--builder--doc-additions.md`,
`.concerns/word-gaps--builder--doc-additions.md`) and corrects two sentences of theirs; those are
listed in section 4. Numbers are from `tests/test_word_gaps.cpp` (FreeType 2.12.1, release build,
2026-10-10) and `bench_draw` as described in section 3.

## 1. docs/decisions.md — a new entry (next free number, after word-gaps' entry)

### Dnn. Where the product's draw rules meet: one leader along a line, and one bound (task `draw-seams`)

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

## 2. docs/divergences.md

No new divergence from `tex.web`. If the file lists the product draw rules beside the reference's:
`word_spaces_led` now also places a formula's box and the text after a formula (Dnn above).

## 3. docs/performance.md — under bench_draw

Two rows were added to `bench_page`: "Drawer into a draw list, DrawRules::product()" and
"placement alone, DrawRules::product()" (the other Drawer rows place under the reference's rules,
which is what a `DrawArgs` is made with).

Before and after this task: two release builds (GNU 12.2.0, `-O3 -DNDEBUG -ffp-contract=off
-fno-exceptions -fno-rtti`, FreeType 2.12.1, Ryzen 7 PRO 4750U) of one snapshot of the tree
differing only in `src/draw.cpp` and `src/draw.h`, run alternately 8 times each,
`( ulimit -v 6000000; timeout 300 build/draw-seams-rel{A,B}/bench/bench_draw )`, load average 3.6
to 4.0 (other builders were compiling; word-gaps measured at 1.7 to 3.5 and its absolute figures
are some 25 % lower for the same code). Rows this task cannot have touched moved by -4 % to
+8 % between the two builds (copy -4.0 %, a culled row +8.1 %): that is the floor. Median of the
8 medians, us:

| row | before | after | |
|---|---|---|---|
| full draw, x1 (public API: product rules) | 161.6 | 158.4 | -2.0 % |
| full draw, x10 | 1814.7 | 1815.8 | +0.1 % |
| full draw, x100 (ms) | 18.00 | 17.99 | -0.1 % |
| widget frame, x1, scroll 0.25 | 78.7 | 77.9 | -1.0 % |
| draw the paragraph (498 glyphs) | 14.46 | 14.13 | -2.3 % |
| sample: Drawer into a draw list (reference rules) | 92.9 | 90.7 | -2.4 % |
| sample: placement alone (reference rules) | 56.6 | 53.0 | -6.4 % |
| sample: Drawer into a draw list, product rules | 105.9 | 104.6 | -1.3 % |
| sample: placement alone, product rules | 68.0 | 67.3 | -1.1 % |
| mixed: Drawer into a draw list, product rules | 112.7 | 112.9 | +0.1 % |
| mixed: placement alone, product rules | 74.4 | 73.6 | -1.1 % |

**No change that the measurement can see.** An earlier series of the same 16 runs at load 8 to 9
put the same rows between -1 % and +5 % with untouched rows between -7 % and +5 %; it says the
same thing with less authority. What the product's rules cost over the reference's in one build,
same run: placement alone 53.0 -> 67.3 us on the sample (3.6 ns a glyph), 61.5 -> 73.6 on the
mixed page; a draw into a list 90.7 -> 104.6 (+15 %). The per-glyph path inside a word is the
code it was; what was added runs once per word, per formula and per formula glyph.

## 4. Sentences of the earlier additions this overtakes

- word-gaps, decisions entry, "`word_spaces_led` — ... Line starts, and words after a formula,
  rule or atom, are anchored as before": a word after a formula is led from the formula's
  rightmost part, and a formula's box is led from the text before it. Line starts and words after
  a rule of the page are anchored as before.
- word-gaps, "Spaces beside a formula (not led, by design ...): 430 measured; over 1 px off 66 ->
  27, range -2.80..+2.78 -> -1.34..+1.56 px": now 4 of 430 over a pixel, -0.97 to +1.23 px.
- d7-fix, "the left edge of its box is put on the nearest pixel": on the nearest pixel when
  nothing leads it; otherwise on one of the two pixels beside its place. A formula is still one
  picture from its edge, whatever the edge's pixel.
