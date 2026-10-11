# draw-seams — result (builder)

Every finding reproduced when run, with the numbers the reviewer derived by hand. The product's
draw rules now share one mechanism along a line (a *leader*) and keep one bound, asserted over
prose with text glued to formulas on both sides: **two neighbours on a line are drawn less than
a pixel and a half from their laid-out distance apart, a formula taken at the left edge of its
box.** `reference()` is byte for byte what it was: no golden, tolerance or skipped record
(checksum of `tests/golden` identical before and after; `test_draw` 17,135 checks green in every
configuration).

Three things the lead should hear before the detail:

1. **One joint is not "never worse than the reference".** Text straight after a formula
   (`$x$)`, `$x^2$-like`): the reference puts it one rounded gap from the formula's last glyph
   and holds it to nothing. The product holds it beside its own place — that hold is what fixes
   F1 — and where the hold moves it, it is a pixel off: 8.2 % of such joints over half a pixel
   from the ink they follow against the reference's 3.4 %, 0.16 % over a pixel against none,
   worst 1.26 px against 1.00. At the other three kinds of joint, and by the furthest part of
   the formula at all four, the product is better than the reference and than it was. I could
   not have both this joint exact and the space after it bounded; I chose the bound. It is one
   expression to choose the other way (section 2).
2. **`$n$th` is not a formula.** A closing dollar followed by a letter, a digit, `{` or `(`
   closes nothing (`scan_math`, `src/imtypeset.cpp`, after the reference's markdown rule), so the
   demo draws `$n$th` as its source, dollars and all. Seen in the first pictures; it is in
   `.concerns/draw-seams--builder--shot-joints-1-fs16-w337-x3.png`. Not draw's, not changed. What
   can be glued to the end of a formula is punctuation: `$n$-th`, `$x$-axis`, `$f$'s`, `$x$)`.
3. **Draw cost did not move; evenness did not move.** Letter pairs in words over half a pixel
   off: 13,348 of 150,624, 8.9 %, the same count as before. Bench: within the noise (section 5).

Files written: `src/draw.cpp`, `src/draw.h`, `tests/test_word_gaps.cpp`, `tests/test_harden.cpp`
(one expectation and its comment), `bench/bench_draw.cpp` (two rows). Doc text:
`.concerns/draw-seams--builder--doc-additions.md`. Crash log:
`.concerns/draw-seams--builder--progress.md`. Nothing `git add`ed or committed.

## 1. The findings, each run before anything was fixed

Hand cases through `Drawer::place` with the test's fixed binder at 72.27 px per inch (the file's
`PlainBinder`; the reviewer wrote "FixedBinder"), run against the unchanged `src/draw.*`; corpus
through the public API with the real faces.

| finding | reproduced? | how large when run | fixed how | test (`tests/test_word_gaps.cpp` unless said) |
|---|---|---|---|---|
| F1 text glued to the end of a formula placed unclamped; next space pays | yes, exactly: pens 11, 17, 22, 27, 32, 39; the space 7 px against 8.65 (1.65 off, bound 1.5) | by hand only: the corpus's worst space between words was 1.30, so no measured corpus showed it | text after a formula is led and held beside its own place like any word | `text_glued_to_the_end_of_a_formula_is_kept_beside_its_place_and_the_space_after_keeps_its_width` |
| F1, bracket after a fraction a pixel inside the bar | yes: bracket 46, bar ends 47. **The reference does the same** (46 and 47) | 1 px overlap | text after a formula is placed from the formula's rightmost part (here the bar's end), not its last shipped glyph | `a_bracket_after_a_fraction_stands_clear_of_the_bar_it_follows` |
| F2 text straight before a formula worse than reference | yes: bracket 40, box 44, 4 px against 5.47; reference 5 | corpus: -1.83 to +1.75 px, 159 of 2,448 over a pixel; reference 28 | the box's edge is led from the text before it | `a_bracket_before_a_formula_is_no_further_from_it_than_under_the_references_rules` |
| F3 "within 1.5 px" false beside a formula | yes: a 2.45 px space drawn 0.48 (1.97 off) | corpus: -1.87 to +1.91, 601 of 6,822 over a pixel, 82 past 1.5 by the furthest part | same; and the comment in `src/draw.h` now states the bound that is asserted | `a_justified_space_before_a_formula_is_within_a_pixel_and_a_half_of_its_laid_out_width`, and the corpus test below |
| F6 last sub-case never clamps | yes: 18 + snap(3.6) = 22 is its own place | — | comment corrected; a new sub-case (`k`: place exactly 14, led to 15, held to 14) exercises `high == low`, and a twin a hundredth of a pixel along shows 15 | `a_word_after_a_space_is_placed_from_the_word_before_and_kept_beside_its_own_place` |
| F7 `nth_element` worst case; `_gaps` never shrinks | by reading (libstdc++ only here; clang not installed) | — | both gone: selection by a byte histogram over the roundings' bits, four passes and no buffer | `the_gaps_that_give_are_the_ones_rounded_furthest_whatever_their_roundings_are` (300 gaps, many ties, both directions, against a stable sort); the 20,000-glyph and megabyte-word tests still pass |
| F8 `in_formula` outlives the formula for a foreign rule | yes with a hand-built list; **not reachable from ship-out** that I can construct (a formula's rules are in its box; a page's rules follow a LINE) | a later glyph 1 px off in the hand case | a rule wholly left or right of the formula's box ends the formula | `a_rule_that_lies_outside_a_formulas_box_is_not_the_formulas` |
| carets against drawn glyphs (measure only) | measured | below | not changed | printed by the corpus test |

Carets: `GetParagraphCaretRect` answers `px(cmd.x)` of the item (read in `src/imtypeset.cpp`; I
did not call it in a test), so a caret before a glyph, and a highlight's edge, stand at the
glyph's laid-out place, and the distance to where the glyph was drawn is the glyph's own
displacement. Worst over every glyph, text and formula: word-gaps' corpus **reference 5.71 px,
product 2.80** (unchanged by this task); the joint corpus reference 2.66, product 1.98. So the
reviewer's note holds — a caret can stand 2 to 3 px into a neighbour in the middle of a long
word under the product, about half as far as under the reference — and nothing in selection was
touched.

## 2. The fix

`Drawer::place_commands`, the *leader* (comment there; `DrawRules::word_spaces_led` in
`src/draw.h` states the rule and the bound):

- The leader is the last glyph of the word before, or after a formula the formula's rightmost
  part (a glyph by its origin, a rule by its right end; the box's edge until one is further).
  Set glue and the start of a formula keep it; a line, a foreign rule, anything else drop it.
- A word after a space, text glued after a formula, and **the left edge of a formula's box** are
  each placed at the leader's pixel plus the laid-out distance rounded once, then held to the two
  pixels either side of their own place. A formula's inside is as `formula_placed_whole` had it.
- The bound follows from: a led thing is under 1 px from its place; a word's end under 1.5; the
  hold only moves a thing towards its own place. Per joint: text to box edge < 1.5; box edge to
  text after <= 1.0; text after to the rightmost part < 1.5; edge to edge of two formulas <= 1.0.

Tried first and dropped: placing text after a formula from the box's edge (the reviewer's
suggestion). It bounds the same, but puts that text two roundings from the ink it follows; from
the rightmost part it is one, as inside a word.

The choice in item 1 at the top is the expression `leads ? led_to(...) : lead_pen + snap_px(...)`
in the branch commented "Text straight after a formula's glyphs". Taking the second arm always
makes that joint half a pixel exact and lets the following space be up to 2.5 px out.

The per-glyph path inside a word is unchanged; nothing allocates (`_gaps` removed, the histogram
is 256 counters on the stack); work is linear in commands.

## 3. Measured afterwards

`under_the_products_rules_a_joint_of_text_and_formula_is_within_a_pixel_and_a_half_too`: three
paragraphs with `$n$-th`, `$x$-axis`, `$f$'s`, `($x$)`, `f($x$)`, `is $x$`, `$x$,`, fractions and
radicals in brackets; justified at 520, 337 and 250 px; 13 to 20 px as em and as font size; three
origins; 18,990 joints. FreeType 2.12.1 and stb_truetype print identical lines (pens come from
layout). Drawn less laid-out distance, px, text to the formula's nearest part:

| joint | n | reference | product before | product now |
|---|---|---|---|---|
| text, formula | 2,448 | -1.25..+1.19; 738 over 0.5; 28 over 1 | -1.83..+1.75; 947; 159 | **-0.96..+0.92; 244; 0** |
| text, space, formula | 6,822 | -1.90..+1.79; 2,591; 380 | -1.87..+1.91; 2,860; 601 | **-1.32..+1.23; 843; 48** |
| formula, text | 8,208 | -1.00..+1.00; 280; 0 | -1.00..+1.00; 216; 0 | **-1.20..+1.26; 673; 13** (box edge -0.97..+0.99) |
| formula, space, text | 1,512 | -0.97..+0.96; 374; 0 | same | **-0.89..+0.90; 103; 0** |

Furthest part of the formula: reference 2.08 / 2.53 / 2.51 / 0.97; before 2.10 / 2.24 / 1.26 /
0.97; now 1.33 / 1.60 / 1.39 / 0.90. Worst at the box edge now: 1.32 (bound 1.5).
**Worst space between two words: reference -2.92..+2.50 (1,341 of 13,227 over 1); before
-1.30..+1.28 (83); now -1.30..+1.33 (97).** Two formulas edge to edge: 0.98.

Asserted there: every joint under 1.5 at the box edge and every space under 1.5, per draw; words
within 0.5; all pens whole; the test's own model of the rule reproduces every pen; at every kind
of joint the furthest part no worse than the reference; at three kinds the nearest part no worse
either; for "formula, text" under 1.5 and under 1 % over a pixel — which is the honest form of
item 1.

word-gaps' own corpus, re-run (`IMTYPESET_WORD_GAPS_REPORT=1`): spaces under 60 %: 11 of 37,778
(same); over 1 px: 308 -> 314; **letter pairs over 0.5 px: 13,348 of 150,624 = 8.9 % (same;
3,632 over 0.6, 118 over 0.75, worst 0.84)**; spaces beside a formula over 1 px: 27 -> 4 of 430,
range -1.34..+1.56 -> -0.97..+1.23.

## 4. Pictures (looked at)

Soft rasteriser, dark theme, reference / product before / product now from top to bottom.
Made before the corpus was corrected, so they show `$n$th` as text.

- `.concerns/draw-seams--builder--crop-joints-1-fs16-w337-1-x6.png` — the clearest: before, the
  `u` of `h(u)` sits against its bracket and "(p) and" is closed up; now both are open.
- `...-crop-joints-2-fs16-w460-0-x6.png` — a fraction and its comma: no difference I can see
  between before and now at that joint; the `n` before "integers" moved a pixel.
- `...-crop-joints-3-fs13-w250-0-x6.png` — 13 px, a formula every third word. **Still uneven**:
  now "and e" is close and "is  f" wide where before "d and" was close. Different, not better;
  inside the bound, which on a 2.5 px space is not much of a promise.
- `...-crop-joints-1-fs19-w460-1-x6.png`, and the three whole strips `...-shot-joints-*-x3.png`:
  looked at; at whole-paragraph scale I cannot tell before from now.

Not re-shot: the demo pages and `docs/images/*` (demo-guard is writing the demo), anything
through the wheel.

## 5. bench_draw

Rows added: "Drawer into a draw list, DrawRules::product()" and "placement alone,
DrawRules::product()" in `bench_page`.

Release, GNU 12.2.0, FreeType 2.12.1; two builds of one snapshot differing only in `src/draw.*`,
alternated 8 times: `( ulimit -v 6000000; timeout 300 build/draw-seams-rel{A,B}/bench/bench_draw )`.
Load average 3.6 to 4.0. Median of medians, us (untouched rows moved -4 % to +8 %):

| row | before | after | |
|---|---|---|---|
| full draw, x1 (public API) | 161.6 | 158.4 | -2.0 % |
| full draw, x10 | 1814.7 | 1815.8 | +0.1 % |
| widget frame, x1 | 78.7 | 77.9 | -1.0 % |
| draw the paragraph (498 glyphs) | 14.46 | 14.13 | -2.3 % |
| sample: Drawer into a draw list, product | 105.9 | 104.6 | -1.3 % |
| sample: placement alone, product | 68.0 | 67.3 | -1.1 % |
| mixed: placement alone, product | 74.4 | 73.6 | -1.1 % |
| sample: placement alone, reference | 56.6 | 53.0 | -6.4 % |

No change the measurement can see. An earlier series at load 8 to 9 said the same more noisily.
The absolute figures are about 25 % above word-gaps' (122.4 us for full draw x1) because the
machine was busier; the "before" column is word-gaps' code. Product over reference in one build:
+15 % for a draw into a list, 3.6 ns a glyph in placement — word-gaps' "about 10 %" was the
public draw, and by these rows it is nearer 15 % of the Drawer's own time.

## 6. What was run

Live tree, unnarrowed, after the last change to any of my five files (md5 checked afterwards),
each configuration in its own directory, `-j3`, `-Werror`, one at a time, every binary guarded:

| configuration | build | ctest |
|---|---|---|
| debug, FreeType, 32-bit ImWchar | 0 warnings | 41 of 41 |
| release | 0 warnings | 41 of 41 |
| stb_truetype | 0 warnings | 41 of 41 |
| 16-bit ImWchar | 0 warnings | 41 of 41 |
| ASan + UBSan (`hard_rss_limit_mb=3000:halt_on_error=1`) | 0 warnings | 41 of 41, no report |

`test_word_gaps` 19 tests / 23,541 checks; `test_draw` 16 / 17,135; `test_harden` 22 / 474,784.

**Not run:** the presets themselves (private directories with their flags); the consumer tests
(`IMTYPESET_BUILD_CONSUMER_TESTS=OFF` in all five, where the debug preset has it on); the
`amalgamation` preset — I added no file-local name outside functions to `src/`, but a unity
build was not compiled; the throwing build; clang and libc++ (so F7's worst case was removed, not
demonstrated); MSVC; the SDL2 demo, the Python hosts, the wheel. Not measured: faces other than
the bundled ones, sizes outside 13 to 20 px, densities other than 96, ragged or centred text,
display formulas beside text.

## 7. Outside my files

- `$n$th` (item 2 above): whoever owns the demo text or the README should not use it as an
  example of inline math.
- `tests/test_harden.cpp` `d7_every_rule_of_a_formula_...` computes the formula's edge as
  `snap_px(own place)`. It passes because every formula there starts its line. A formula after
  text on the line would now be led; the test in `test_word_gaps.cpp` recovers the edge from the
  formula's first glyph instead. Left as it is.
- word-gaps' and d7-fix's doc additions have three sentences this overtakes; listed in my doc
  additions, section 4.

## Housekeeping

`build/draw-seams*` removed. Pictures kept: seven `.concerns/draw-seams--builder--*.png`.
