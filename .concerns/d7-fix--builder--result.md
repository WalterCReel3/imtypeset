# d7-fix — result (builder-8)

All four review-3a items reproduce. The fix is a different rule, not a repair of the join: under
`DrawRules::product()` a formula is placed whole — every glyph and both ends of every rule at
`snap(left edge of the formula's box) + snap(own distance from that edge)` — and nothing in a
formula is placed from the glyph before it (`DrawRules::formula_placed_whole`, `src/draw.h`,
`Drawer::place_commands` in `src/draw.cpp`). `rule_joins_span`, `RULE_JOIN_LOOKBACK`,
`Drawer::settle_span` and the two scratch vectors are gone. `reference()` is untouched; no golden,
no tolerance, no newly skipped record. `src/shipout.*`, `src/math_mlist.*`, `tests/test_draw.cpp`
and `tests/test_shipout.cpp` were NOT changed (the mechanism needs no structure in the commands).

Text for the shared documents, including the decision with its evidence and what was weighed:
`.concerns/d7-fix--builder--doc-additions.md`. Crash log: `.concerns/d7-fix--builder--progress.md`.

## What to know first

1. **It costs evenness inside formulas, and that is the user's kind of call.** 16.8 % of
   side-by-side glyph pairs in a formula (20,392 of 121,408 measured) are now more than half a pixel
   from their layout distance, never a full pixel; under the reference none is. This is the `whole`
   rule of advance-rule.md, which the user ranked last *for prose*. Looked at enlarged: visible at
   13 px on a row of 26 letters (`i j`), not something I could see at 16 or 19 px. I judged aligned
   bars worth it in a formula; the switch is one bool if the user disagrees.
2. **The radical's joint is a pixel looser than harden-2 had it** (within 1 px, was 0.42; 5 of 36
   formulas a pixel off; never a clear column; one lighter pixel at 11 px after 26 terms —
   `.concerns/d7-fix--builder--shot-root-em11-terms26.png`).
3. **The full unnarrowed suite could not be run on the live tree.** `api-document`'s builder was
   mid-edit from about 09:10: `include/imtypeset.h` declares `DebugCheckDocumentDataLayout` (called
   by `IMTYPESET_CHECKVERSION`) with no definition, `src/imtypeset_c.cpp` fails a `static_assert`
   (`ImTypeset_Run` 24 bytes against 28), and `bench/bench_draw.cpp` no longer compiles (`Document`
   and `Block` ambiguous against `src/blocks.h`). I ran everything else on a private copy of the
   tree; see "What was run".
4. **`bench/bench_draw.cpp` is broken by that work** and is in nobody's current `touches` that I
   can see; `word-gaps` needs it.

## Per review item

| item | reproduced? (run, before any change; real math face, FreeType, 168 draws a formula) | size under `rule_joins_span` | now | test |
|---|---|---|---|---|
| 1a radical under a wider numerator | Yes. The reviewer's hand case run as written: sign at 142 (ends 152), bar 156..164, x at 152 — the 4 px hole, exactly. | real face: bar 1.42 px from its sign, 1.19 px at its right end | 0.67 / 0.79; hand case: sign 150, bar 160..168, x 160 | `d7_a_rule_under_a_wider_row_...` (hand), `d7_every_rule_...` |
| 1b radical over a wider denominator | Yes, at the bound. | fraction's bar left end 1.00 px; radical's own bar 0.42 / 0.81 | 0.91 / 0.81 radical; 0.00 / 0.71 fraction | `d7_every_rule_...` |
| 1c fraction under a wider numerator | Yes. | inner bar right end 1.43 px | 0.86; hand case: x, bar and y all from 150, bar to 158 | both |
| 1d the old test cannot see it | Yes: it measured the wider rule only, against the leftmost and rightmost glyph of everything. | — | replaced; its four formulas are among the 14 | — |
| 2 narrower part off-centre | Yes, and larger than the conditional example. | centre 2.09 (1a's formula), 2.23 (`\frac{1}{11111111111}`), 4.82 (70 letters over 2), 4.18 (2 over 70 letters), 1.18 (1c) px | at most 0.77 px over all 14 | `d7_every_rule_...` |
| 3 past 64 glyphs | Yes, small with this face: left end 1.00 px on 70 letters over 2. The 68 px worst case needs every gap rounding one way; not constructed against the old code. | 1.00 px | 0.00; by hand, 200 glyphs: bar 50..2130, last glyph 2120 | both |
| 4 a rule does not re-anchor the span | By design then and now; stated in `DrawRules`' comment and asserted (the glyph after a rule is on the formula's grid; a text glyph after a formula is placed from its last glyph). | — | — | `d7_a_formula_is_placed_glyph_by_glyph_...` |
| (not in the review) overline | Its rule is shipped before its glyphs, so the join never applied: right end 1.79 px. | 1.79 px | 0.82 | `d7_every_rule_...`, hand case |

Under the reference's rules, same measurement: left 6.91, right 10.17, centre 6.47 px.
Whole table of 14 formulas x 2 rule sets: run `test_harden --filter=d7_every`.

## The three things seen by eye in the demo (19 px)

| seen | what it is | numbers |
|---|---|---|
| `\sqrt{\frac{a}{b}}`: rule touching the `a` | **Layout**, same under both rule sets. | rule's lower edge y 44.41, `a` baseline 51.75; drawn: rule row 44, `a`'s quad from row 46: one clear row |
| ~12 px between `\int` and its integrand | **Layout.** The operator's italic correction is added to an advance that already covers the glyph. | `f` starts 22.1 px after the sign's origin = advance 665 + italic correction 332 + thin space, in 1/1000 em; the ink ends at 609 |
| ~2 px after an inline formula ending in a fraction, ~5 before | **Not reproduced at 2 px** (3 to 6 after, 4 to 7 before, unjustified, 16/19 px). Part is this rule: glyph rounding pushed a formula's ink 1.3 px past its box (ends 138, box 136.66; now 137). Part is layout: a bar runs to the box's edge with no side bearing, and no null delimiter space is set; and justified glue shrinks. | see doc-additions §5 |

Not chased, as instructed. Details and section numbers in doc-additions §5.

## Pictures (looked at, 4x, reference above / product below)

`.concerns/d7-fix--builder--shot-quadratic-em17-terms40.png` (reference: bar 5 px left of its
numerator, sign over the `b`; product: joined, `2a` centred), `...-root-under-numerator-em19-terms13.png`,
`...-one-over-ones-em19-terms13.png` (reference: denominator hanging out past the bar's right end;
product: under it), `...-root-em11-terms26.png` (the loosest joint), `...-letters-em13-terms0.png`
(the cost). All 28: `IMTYPESET_HARDEN_SHOTS=<dir> test_harden --filter=d7_pictures`.

## bench_draw

Release, GNU 12.2.0 -O3, FreeType; binaries built from the sources before and after, run
alternately three times, load average 1.6 to 1.9;
`( ulimit -v 6000000; timeout 300 build/<dir>/bench/bench_draw )`. Medians, us, 3,918 glyphs:

| row | before | after |
|---|---|---|
| sample: Drawer into a draw list | 72.25 / 73.55 / 73.01 | 72.73 / 73.66 / 73.75 |
| sample: placement alone | 42.96 / 43.83 / 43.56 | 43.62 / 44.16 / 44.33 |
| mixed: Drawer into a draw list | 78.93 / 80.77 / 80.08 | 80.65 / 81.20 / 81.98 |

About +1 % on the sample, +2 % on the mixed page; not faster. harden-2's 71.7 against 66.6 is not
recovered and is mostly not this rule: with the per-glyph formula test compiled out, placement is
42.4 to 42.6 us, so the test is about 0.4 ns a glyph. The benchmark has no formula in it and runs
under the reference's rules. Taken before `bench_draw.cpp` stopped compiling.

## What was run, and what was not

On the live tree, narrowed (`test_harden`, `test_draw`, `test_shipout`; debug, FreeType): green
until the tree broke at about 09:15.

On a private copy of the tree taken at about 09:20 (my nine `touches` files byte-identical to the
live ones, checked with `cmp` afterwards), with one added file defining the missing
`DebugCheckDocumentDataLayout` as `return true`, all modules but `imtypeset_c`, the 32 test
programs that are not `test_c_api` or `test_c_api_map`, `bench_reflow`, examples `minimal`, `specimen`,
`demo_headless`; `-j4`, `-Werror`, every binary under a guard:

| configuration | build | ctest |
|---|---|---|
| debug, FreeType 2.12.1, 32-bit ImWchar | 0 warnings | 35 of 35; `test_harden` 22 tests, 474,784 checks |
| ASan + UBSan (`hard_rss_limit_mb=3000`, `halt_on_error=1`) | 0 warnings | 35 of 35, no report; the five `d7_` tests 166,429 checks |
| release | 0 warnings | 35 of 35 |
| stb_truetype | 0 warnings | 35 of 35; `d7_` figures identical to FreeType's |
| 16-bit ImWchar | 0 warnings | 35 of 35 |

**Not run:** the C API and its two tests, `bench_draw` after 09:15, the presets, the throwing
build, the SDL2 demo and the Python hosts (so no screenshot of the real demo window after the
change — the pictures are the soft rasteriser's), clang, MSVC. Nothing was compared with TeX.
`docs/images/py-host.png` and any demo screenshot showing a fraction predate this.

## Tests changed in tests/test_harden.cpp

- `d7_a_radical_sign_meets_its_bar_...`: was "the joint is identical at every length"; is "the same
  at every origin, within a pixel of the formula with nothing before the sign, never open". That
  is a looser statement, because the rule is looser there (item 2 of "What to know first").
- `d7_a_rule_in_a_rigid_span_is_snapped_alone_by_the_reference_and_with_the_span_here` became
  `d7_a_formula_is_placed_glyph_by_glyph_by_the_reference_and_whole_here`: the reference's expected
  values are unchanged; the product's are reworked by hand.
- `d7_a_fraction_bar_reaches_over_its_parts_as_they_were_drawn`: removed; subsumed (see 1d).
- New: `d7_a_rule_under_a_wider_row_...`, `d7_every_rule_of_a_formula_...`. The own-box glyph
  numbers in `ruled_formulas()` were assigned by hand from a dump of each formula's commands.

## For `word-gaps` (same file next)

- A word's place is decided in two spots of `Drawer::place_commands`: the anchor,
  `span.prev_pen = snap_px(x)` in the `if (!span.open)` block (absolute, +-0.5 px), and each later
  glyph, `span.prev_pen + snap_px(step)`. A drawn word space is therefore the layout space, plus
  the next anchor's rounding, minus this anchor's, minus the rounding the word gathered over its
  gaps (up to 0.5 px a gap, same sign possible throughout). The last term is what closes a space.
  `SET_GLUE`'s width is never read for placement; only the next glyph's x is.
- `Span` keeps only the previous glyph. A bound on a word's drift needs the anchor's x and pen
  kept too; the formula rule here is that bound at 0.5 px with the box's edge as anchor.
- A second rounded expression in the glyph path cost 4 % (sample) to 8 % (mixed) in release;
  swapping what the one expression is anchored to cost about 1 %. Measure the shape, not only the rule.
- A text glyph straight after a formula (`$x$,`) is placed from the formula's last *shipped* glyph,
  which for a fraction is the denominator's last.
- Bounding drift at half a pixel is exactly `whole`, with its 1-in-6 uneven pairs; any looser
  bound trades those for position error. The measurement loop in `d7_every_rule_...` counts both.

## Housekeeping

Nothing was `git add`ed or committed. `build/d7-fix*` removed. Files written: `src/draw.h`,
`src/draw.cpp`, `tests/test_harden.cpp`, and under `.concerns/`: this file, `...--progress.md`,
`...--doc-additions.md`, five `...--shot-*.png`.
