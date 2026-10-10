# d7-fix — text for the shared documents (builder-8)

Nothing under `docs/` was edited. Each section names the file and what the text replaces. The
symbol `DrawRules::rule_joins_span` and the constant `RULE_JOIN_LOOKBACK` no longer exist; every
mention of them in `docs/` is stale (found: `docs/hardening.md` row D7, `docs/performance.md` near
line 629, and the decisions entry fold-docs numbered D39, item 10).

## 1. docs/decisions.md — a new entry (next free number), which supersedes D39 item 10

**Dn. A formula is drawn whole, from the left edge of its box (2026-10-10).**

*Decided.* Under `DrawRules::product()` (the public API) the draw loop does not treat a formula as
a rigid span. The left edge of the formula's box (the ATOM command's x) is put on the nearest pixel,
and every glyph and both ends of every rule of the formula go to that pixel plus their own distance
from the edge in scaled points, converted and rounded once (`DrawRules::formula_placed_whole`,
`Drawer::place_commands` in `src/draw.cpp`). A rule's top and bottom are taken from the baseline
the formula's first glyph was put on, and a rule does not end the span. A glyph of the text that
follows a formula with no glue between is placed from the formula's last glyph, as before.
`DrawRules::reference()` is untouched and is what every golden replay runs under; no golden changed.

*Why.* The first product rule (`rule_joins_span`, D39 item 10) left the glyphs where the reference
puts them — each from the glyph shipped before it — and joined each end of a rule to the glyph
whose origin was nearest along the line. A formula's rows overlap along the line and are shipped
one after another, so "nearest" picked glyphs of another row (review-3a items 1a to 1c), a
denominator was placed from the numerator's last glyph and stood off the middle of its bar by half
the rounding the numerator had gathered (item 2), and past 64 glyphs the join gave out (item 3).
All four reproduce (below). The advance rule is for a word, where the only relation that shows is
between a glyph and its neighbour. In a formula the relations that show are between rows, and a
rounding carried along one row has no way to reach another. Placing every item from one edge
bounds every one of those relations at once, needs no knowledge of the box structure in the draw
loop, no second pass and no look-back, and makes a formula the same picture at every sub-pixel
origin.

*Measured* (`tests/test_harden.cpp`,
`d7_every_rule_of_a_formula_stands_where_it_should_against_the_ink_of_its_own_box`: 14 formulas,
7 sizes from 11 to 32 px, 0/13/40 terms before, 8 sub-pixel origins, 168 draws a formula, the
bundled math face, FreeType; each rule against hand-assigned glyphs of its own box). Worst
distance, in pixels, of a rule's end from where layout puts it against the glyph at that end as
drawn, and of the centred part from the middle of its bar:

| | left end | right end | centre |
|---|---|---|---|
| the reference's rules | 6.91 | 10.17 | 6.47 |
| `rule_joins_span` (measured before it was removed, 11 of the 14 formulas) | 1.42 | 1.79 | 4.82 |
| `formula_placed_whole` | 0.91 | 0.95 | 0.77 |

Under `rule_joins_span`, by review item: 1a `\frac{a+b+c+d+e+f+g+h+i+j}{\sqrt{x}}`, the radical's
bar 1.42 px from its sign and 1.19 px from the x (the reviewer's hand case, run: sign at 142 so
ending at 152, bar 156..164, x at 152 — a 4 px hole, exactly as read); 1b
`\frac{\sqrt{x}}{abcdefghijkl}`, the fraction's bar 1.00 px; 1c `\frac{abcdefghijkl}{\frac{x}{y}}`,
the inner bar 1.43 px; item 2, centre 2.09 px (1a's formula), 2.23 px (`\frac{1}{11111111111}`),
4.82 px (seventy letters over 2), 4.18 px (2 over seventy letters); item 3, left end 1.00 px on
seventy letters over 2 with this face (the worst case the reviewer gave needs every gap to round
the same way); an overline, right end 1.79 px (its rule is shipped before its glyphs and was
snapped alone).

By construction, and asserted for every glyph and rule end in those draws: each is within 0.5 px
of its distance from the formula's edge, so any two are within 1 px of their distance from each
other, and the offsets are the same at all eight origins.

*What it costs, measured.* (a) Evenness inside a formula. Of 121,408 pairs of glyphs shipped side
by side on one baseline in those draws, 20,392 (16.8 %) stand more than half a pixel from the
distance layout gives them, the furthest under 1 px; under the reference's rules none does. This
is `whole` of `../imgui-adv-text/docs/advance-rule.md` §5 (478 of 3,453 there), which the user
ranked below `advance` for prose. Looked at, enlarged four times, on `abc...xyz + \sin(2\pi x) +
1234567890` at 13, 16 and 19 px: at 13 px a wider gap shows between `i` and `j` and the digits'
gaps are not all alike; at 16 and 19 px I could not tell the two rows apart by spacing. (b) The
radical's joint. `rule_joins_span` held the bar's start against its sign to 0.42 px and the same at
every position; now it is within 1 px. Over 576 draws of `a+b+...+\sqrt{x}` (six sizes, 0 to 60
terms): the bar starts 2, 1 or 0 px inside the sign's quad where it was always 1; in 5 of 36
formulas it differs by a pixel from the formula with nothing before the sign; never a clear column
between them. At 11 px after 26 terms the join shows as one lighter pixel.

*Considered and not taken.* (1) Keep the advance rule in a formula and tell the draw loop which
box a rule belongs to, so a bar follows the glyphs it lies over and a narrower part is centred on
the bar as drawn. It keeps (a) and (b). It needs the box structure in the command stream: a
`DrawCmd` has no spare field that golden replays do not compare (`operator==` takes every field,
and unused ones are zero by contract), so it means a tagged ship under a switch set in
`src/imtypeset.cpp`, a device-space centring pass over glyphs already placed, and a formula whose
drawn width differs from its layout width by its widest row's rounding (which is what moves the
word space after an inline fraction today). (2) Restoring the reference's rule under `product()`:
6.9 / 10.2 / 6.5 px above. (3) Joining only a radical's bar to the glyph shipped before it: an
overline that follows a glyph is the same sequence of commands and would start a pixel off its
first letter in some positions (how often was not measured).

*Not measured.* Faces other than Latin Modern Math; stb_truetype's rasters by eye (the placement
tests run under it; the pictures were made under FreeType only); display formulas (`$$`), which
take the same path; sizes above 32 px.

## 2. docs/hardening.md — row D7, the "fix" and "test" cells

Fix: `DrawRules::formula_placed_whole`, on under `product()` (the public API), off under
`reference()` (the goldens): every glyph and rule end of a formula is placed from the pixel the
formula's left edge was put on, by its own distance from that edge rounded once; a formula's rule
takes its top and bottom from the formula's baseline and does not end the span (decisions Dn).
It replaces `rule_joins_span`, which joined a rule's ends to the glyph nearest along the line and
picked glyphs of another row.

Tests (`tests/test_harden.cpp`): `d7_a_radical_sign_meets_its_bar_however_far_along_a_formula_it_stands`
(576 draws: the joint the same at every origin, within a pixel of the formula with nothing before
the sign, never open); `d7_a_formula_is_placed_glyph_by_glyph_by_the_reference_and_whole_here`
(both rule sets worked by hand on a radical, an overline, a fraction, rules that are no formula's,
four origins); `d7_a_rule_under_a_wider_row_is_placed_with_its_own_box_and_not_the_row_above`
(review-3a's cases 1a, 1c and 3 by hand);
`d7_every_rule_of_a_formula_stands_where_it_should_against_the_ink_of_its_own_box` (14 formulas,
2,352 draws under each rule set: ends and centre within 1 px, every item within 0.5 px of its
place, one picture at every origin); `d7_pictures_...` writes, when `IMTYPESET_HARDEN_SHOTS` names
a directory, 28 pictures with the reference's rules above the product's.
`d7_a_fraction_bar_reaches_over_its_parts_as_they_were_drawn` is gone: its four formulas are in the
14, measured against their own boxes.

## 3. docs/performance.md — in place of the sentence beginning "Placement now asks of every glyph whether it is a formula's (`DrawRules::rule_joins_span`)."

Placement asks of every glyph whether a formula placed whole is open (one test of a flag that is
never set under the reference's rules; `DrawRules::formula_placed_whole`). Release, GNU 12.2.0
`-O3`, FreeType, `bench_draw` built from the sources before and after d7-fix and run alternately,
three rounds, load average 1.6 to 1.9; medians in us for 3,918 glyphs:

| row | before (`rule_joins_span`) | after |
|---|---|---|
| sample: placement alone, no vertices | 42.96 / 43.83 / 43.56 | 43.62 / 44.16 / 44.33 |
| sample: Drawer into a draw list | 72.25 / 73.55 / 73.01 | 72.73 / 73.66 / 73.75 |
| sample: Drawer into buffers | 68.45 / 70.16 / 69.36 | 69.03 / 69.97 / 70.18 |
| mixed: placement alone, no vertices | 47.72 / 48.52 / 48.13 | 49.04 / 49.34 / 49.79 |
| mixed: Drawer into a draw list | 78.93 / 80.77 / 80.08 | 80.65 / 81.20 / 81.98 |
| full draw, x1 | 109.5 / 110.9 / 110.1 | 110.1 / 110.7 / 112.9 |

So within about 1 % on the sample and 2 % slower on the mixed page, which is at the edge of the
run-to-run spread. With the test compiled out placement alone is 42.4 to 42.6 us (two runs): the
question costs about 0.4 ns a glyph, and the rest of what harden-2 measured (71.7 against 66.6 us)
is not in it. The benchmark draws under the reference's rules and holds no formula, so the cost of
placing a formula whole is not in these rows; per glyph it is the same arithmetic as a glyph of a
word, and the look-back and the second pass over a formula's rules are gone. Command:
`( ulimit -v 6000000; timeout 300 build/<dir>/bench/bench_draw )`, rows as named.

Two shapes of the same rule that were measured and not kept, because they are worth knowing:
choosing between two rounded expressions per glyph (a formula's or a word's) cost 4 % on the sample
and 8 % on the mixed page; the kept shape swaps the anchor a formula's glyph is placed from and
leaves one expression.

## 4. docs/divergences.md — the draw rules' row for D7 (where `rule_joins_span` is named)

Draw, device space. The reference places a formula's glyphs as a word's (each from the glyph
shipped before it, gap rounded once) and snaps each rule alone. The product places a formula whole
(`DrawRules::formula_placed_whole`, decisions Dn). Goldens replay under `reference()`.

## 5. For whoever keeps the list of layout findings (not draw; none of it was changed)

Three things the demo's builder saw by eye at 19 px, measured (`ImTypeset::DrawParagraph`, FreeType,
origin (3, 40); the numbers are the same under both draw rule sets unless said):

- `\sqrt{\frac{a}{b}}`, "the rule touches the a". Layout. The radical's rule ends at y 44.41 and
  the `a` (script size) has its baseline at 51.75; drawn, the rule is the row 44 and the `a`'s
  quad starts at row 46: one clear row. The clearance is what §737 gives in text style with this
  face; nothing in draw moves it. Whether a product layout rule should give a radical over a
  fraction more room is a `tex-fixes`-kind question.
- `\int f(x) dx`, "12 px between the sign and the integrand". Layout. The `f` starts 22.1 px after
  the integral's origin: its advance (665/1000 em = 12.6 px), plus its italic correction (332/1000
  em = 6.3 px, read from the face's MATH table), plus a thin space (3.2 px). The sign's ink ends
  609/1000 em along. §749 adds the italic correction to an operator with no subscript, which is
  right for a TFM, where the width stops short of the slant; an OpenType advance already covers
  the glyph. The reference does the same, so the port is faithful and the result is wide. The same
  correction pushes the upper limit of `\int_0^1` far to the right. To check against a current
  engine before changing: I believe OpenType-aware engines do not widen the operator, but I did
  not verify it.
- "About 2 px of word space after an inline formula ending in a fraction, 5 px before." Partly this
  rule, partly layout. Not reproduced at 2 px on the sentences I tried (unjustified, 16 and 19 px:
  3 to 6 px after, 4 to 7 before). What is there: (i) under the reference's rules and under
  `rule_joins_span` the formula's glyphs gather rounding, and the ink of
  `\sum_{i=1}^{n} i = \frac{n(n+1)}{2}` at 16 px ends at 138 where its box ends at 136.66, taking a
  pixel and a half out of the space after it; placed whole it ends at 137. (ii) A fraction's bar
  runs to the very edge of its box (the reference sets no null delimiter space around a fraction,
  §748), so the space after it has no side bearing in it, while the space before a formula that
  starts with a letter or a sum sign has that glyph's bearing added: 7 against 5 px at 19 px from
  that alone. (iii) In a justified line the glue may also be shrunk. (ii) and (iii) are layout.
