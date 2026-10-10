# tex-fixes: exact additions for the shared documents (for `fold-docs`)

Written by the builder of `tex-fixes`. Nothing below has been applied: `docs/decisions.md`,
`docs/divergences.md`, `docs/performance.md` and `docs/hardening.md` were not edited. `D<tex>` is the
decision number fold-docs assigns to the entry in section 1. Numbers are from this machine (GCC
12.2, x86-64, FreeType 2.12.1, ImGui 1.92.9); "release" is `-O3 -DNDEBUG -ffp-contract=off
-fno-exceptions -fno-rtti`. Every count below is printed by `tests/test_tex_rules.cpp` and asserted
there, so a change in any of them fails a test.

---------------------------------------------------------------------------------------------------

## 1. `docs/decisions.md` — append one entry

```markdown
## D<tex>. The reference's undeclared departures from `tex.web` are switches: `TexRules::reference()` and `TexRules::product()`

Task `tex-fixes`, on `.concerns/tex-fixes--lead--rulings.md`. This amends D32.3, which had each fix
keep the differential net "by regenerating the affected goldens from the reference patched in
memory". No golden and no oracle script was changed.

**Decided**

1. *The mechanism is a rules struct with two presets* (`src/tex_rules.h`), the shape `DrawRules`
   (`src/draw.h`) and `DocumentRules` (`src/blocks.h`) already have. `TexRules::reference()` is the
   prototype's behaviour exactly and is the default of everything that carries a `TexRules`, so
   every golden replay runs under it without saying so. `TexRules::product()` is what the public
   API lays out with: `ImTypesetContext::tex_rules` (`src/context.h`), read in `src/imtypeset.cpp`
   where the math fonts are handed out, where text is composed and where a paragraph is broken.
   The rules are carried by what already carried parameters: `LineBreakParams::rules`,
   `Composition::rules`, `MathFonts::rules`. Nine switches:

   | switch | section | register entry |
   |---|---|---|
   | `skips_made_shrink_finite` | §827, §868 | B5 |
   | `left_skip_omitted_when_zero` | §887, §1229 | B6 |
   | `post_break_text_stops_pruning` | §877, §884 | B7 |
   | `easy_line` | §835, §845, §848, §850, §854 | B3, B3a, B4 |
   | `sentence_space_truncates` | §1044, §107 | Y1 |
   | `italic_kern_only_without_subscript` | §755, §759 | M9 |
   | `underline_clearance` | §735 | M10 |
   | `sublist_nucleus_packed` | §754, §720, §721 | M13 |
   | `mu_truncates` | §703, §106 | M14 |

2. *`easy_line` is ON in `product()`*, on the evidence the ruling asked for (below). D32.3 listed
   "the line breaker's class-per-line-count formulation" among the deliberate choices that stay;
   it stays as `TexRules::reference()` and is no longer what the product does. The work budget
   (D33) is kept unchanged as the bound on a list built to be expensive.
3. *M11 is not changed.* A fraction spaces as Inner under both presets. It agrees with *The
   TeXbook* (Appendix G, rule 15e) and, by the recollection of the builder of `tex-web-pass` only,
   with `tex.web` as corrected in 2014; it differs from the 2008 copy in
   `../imgui-adv-text/docs/tex.web` (§761 l.14996). Nobody has read a current `tex.web` to settle
   it; that is the user's to do or to decide.
4. *The golden replays through the public API run under `reference()`.*
   `tests/test_api.cpp` (`text_to_commands_to_vertices_equals_the_reference_chain`) and
   `tests/test_api_math.cpp` (`formulas_in_running_text_give_the_reference_material_lines_and_commands`)
   set `ctx->tex_rules = TexRules::reference()` first. Without that line they fail in 32 and 70
   checks: that is the product differing from the prototype, which is the point.
5. *Section 721 is applied to a box used as it stands.* Under `sublist_nucleus_packed`, `clean_box`
   of a field that is already one unshifted box (a `SUB_BOX`, or a list that converted to one box)
   drops the italic-correction kern after a lone character, as the section does for any result.
   The reference leaves a `SUB_BOX` alone. No dimension changes. Not in the register before this
   task (M13a below).
6. *A kern or a rule of a formula is held to ±`MAX_DIMEN` like a box* (`Converter::push`,
   `src/math_mlist.cpp`): `MathError::TOO_LARGE` with `MathStatus::inside` set, reason "the formula
   is too large to set: a space or a rule inside it is larger than the largest dimension there
   is". Under both presets; the port's own (D33). `harden-2` left this open with no failing input;
   two were found (section 4).
7. *`Paragraph::artificial_breaks`* is new: how many times the pass that produced the lines took
   §854's free break. It is what the measurements below classify by, and a host can read it as
   "some line of this paragraph could not be made to fit".

**Measured** (`tests/test_tex_rules.cpp`; every figure is asserted there)

How much ordinary text moves, over every paragraph the goldens hold, each broken under
`reference()` and under one switch at a time:

| corpus | paragraphs (by total fit) | B5 | B6 | B7 | Y1 | `easy_line` |
|---|---|---|---|---|---|---|
| `tests/golden/core` (500 breakings, 2,864 line boxes) | 500 (336) | 4 break sets, 22 line boxes | 0 break sets, 2 line boxes | 0 break sets, 3 line boxes | n/a (the lists are already made) | 11 break sets, 146 line boxes |
| `tests/golden/para`, as the reference composes it (4,902 line boxes) | 710 (250) | 0, 0 | 0, 0 | 0, 0 | 0, 0 | 17 break sets, 669 line boxes |
| the same, ragged blocks by total fit (`ImTypesetLineBreak_TotalFit`; 3,712 line boxes) | 460 (460) | 0, 0 | 0, 0 | 0, 0 | 0, 0 | 57 break sets, 2,085 line boxes |
| the same, every case set `Spacing::NONFRENCH` | 710 (250) | | | | 0 break sets, 55 of 4,903 line boxes (21 of 4,171 glues, one scaled point each) | |
| `tests/golden/blocks` (1,694 line boxes) | 705 (465) | 0, 0 | 0, 0 | 0, 0 | 0, 0 | 14 break sets, 120 line boxes |

- B5, B6 and B7 move only the core cases that were built with the input each is about (a skip
  with `fil` shrink; a zero left skip of infinite order; a discretionary with post-break text
  before glue). Nothing the library composes itself reaches them: `skips()` writes stretch only
  and no discretionary is ever made.
- Y1 moves nothing under the default spacing (`EXTRA_SPACE_ONLY`, which does not scale stretch or
  shrink); two para cases are NONFRENCH and neither changes. With every case forced to NONFRENCH,
  21 glues change by one scaled point and no break moves.
- `easy_line`: every paragraph whose breaks change is one where §854's free break was taken in at
  least one of the two breakings (17 of 17 para, 57 of 57 ragged, 14 of 14 blocks, 4 of 11 core),
  or one of the seven core cases built with `line_penalty 0` so that every breaking ties. In no
  total-fit paragraph of any corpus do the total demerits differ when no free break was taken
  (asserted: 0). Where the free break was taken the product has fewer lines in every case (17, 57,
  14, 4) and never more; it costs more demerits than the reference's breaking in 9 of the 57
  ragged ones and in none of the others (the reference's free break is free: it charges nothing
  for the line it forces, and it forces from the chain with the most lines).
- The 17 + 14 justified ones are all at the goldens' two narrowest columns, 40 px and 60 px (20,
  30 or 45 pt: 15 + 2 of the para ones at 30 and 45 pt, 12 + 2 of the blocks ones at 30 and
  20 pt), where a word does not fit its line. Of the 57 ragged ones 42 are at 30 pt, 3 at 45 pt
  and 12 at 240 px, the cases built around a word that cannot be broken. One of the justified
  ones has no overfull line at all (`hard-breaks-after-space-justify` at 45 pt: 7 lines at 152,542 demerits
  under the reference, 5 at 10,661 under the product): §854 is taken for any forced break
  (§851), and the reference, whose `minimum_demerits` starts afresh for every line count, gives
  the free break to the last line count at a hard break even when a shorter chain was feasible.

Formulas (`tests/golden/math/formulas.golden`, 1,950 sources set at their own sizes and styles):

| switch | boxes whose width changes | height or depth |
|---|---|---|
| M9 | 22 (by at most 58,040 sp) | 0 |
| M10 | 0 | 14 (by at most 31,575 sp) |
| M13 | 0 | 0 |
| M14 | 181 (by at most 40 sp) | 0 |
| all of `product()` | 194 | 14 |

No formula is set under one preset and refused under the other.

The cost (`bench_reflow --only growth`, release, one run, load average 3 to 5 on the machine;
the sample's last paragraph concatenated, 420 pt; the work budget raised to its maximum for both
columns so that neither is cut short):

| | x1 (8 lines) | x4 (30 to 32) | x16 (120 to 128) | x64 (480 to 512) |
|---|---|---|---|---|
| justified, a class per line count | 0.092 ms | 0.90 ms | 9.9 ms | 133 ms |
| justified, `easy_line` | 0.089 ms | 0.53 ms | 2.3 ms | 10.0 ms |
| ragged right by total fit, a class per line count | 5.8 ms | 105 ms | 1.73 s | 21.5 s, and then over even the maximum budget (268,442,463 units): first fit |
| ragged right by total fit, `easy_line` | 0.143 ms | 0.63 ms | 2.58 ms | 10.9 ms |
| work units at x64, `easy_line` | | | | 97,218 justified, 120,746 ragged (the default budget is 1,048,576) |

With the default budget and `product()`, which is what a host gets, none of the sixteen rows of
the table falls back to first fit; before this task justified x64 and ragged x4, x16 and x64 did
(`docs/performance.md`, "After the API revision").

**Read, not measured**

- Each product rule against the section it cites, by hand, for the cases in
  `tests/test_tex_rules.cpp`; the working is in the comment above each test. **No TeX engine is
  installed on this machine** (`which tex pdftex luatex etex`: none), so nothing was compared with
  TeX's output and agreement with TeX is claimed for those hand-worked cases only.
- `tex.web` here is version 3.1415926 (2008).

**Consequences a host can see**

- A formula with a subscript on `f`, `F`, `N`, `U`, `J`, `I`, `C`, `S`, `X`, `Γ`, `Π`, `Σ` and
  the other letters with an italic correction is narrower by that correction and its subscript
  sits under the letter's overhang (pictures: `docs/images/texfix-f_i.png`, `texfix-f_i-sup.png`,
  `texfix-letters.png`; each shows the reference's rules above and the product's below).
- Under `product()` a group is a box (§754), so a formula's node tree is one level deeper per
  group, `\frac` included (the parser makes it a group holding the fraction). Positions are
  unchanged.
- With `line_penalty 0` and a `fil` right skip every breaking ties, and `easy_line` then sets one
  word to a line where the reference set as few lines as it could
  (`easy_line_of_two_breakings_with_equal_demerits_the_later_node_wins`). That is `tex.web`'s
  order (§855's `<=`) and is not reachable through the public API, which has no way to set
  `line_penalty`.

**Not done**

- §826's error for infinite shrink is not reported, under either preset.
- The twelve undeclared departures with no visible effect stay as they are: B5a, B8, B14, P3, S3,
  U3, M12, M15, M16, M17, Y5, Y8. So do Y4's `str.isupper()` and Y10 (U+00A0 is a breakpoint).
- `DocumentComposer` and `Document` (`src/blocks.h`) carry the rules in `params.rules` and
  default to the reference; nothing in the public API uses them yet. Whoever wires the document
  layer in sets `composer.params.rules = ctx.tex_rules` and the blocks' `Composition::rules`.
```

---------------------------------------------------------------------------------------------------

## 2. `docs/divergences.md`

### 2a. In the register's introduction, replace the second bullet

Replace the bullet that begins "**undeclared in the prototype**" with:

```markdown
- **undeclared in the prototype** — the prototype does it and no document of
  its says so, or says the opposite. Task `tex-fixes` made nine of them
  switches (`src/tex_rules.h`, `docs/decisions.md` D<tex>): the port does what
  the prototype does under `TexRules::reference()`, which every golden replay
  runs under, and what `tex.web` does under `TexRules::product()`, which the
  public API uses. Such an entry says **switch** and names it. An entry marked
  *no visible effect* changes no position or dimension anybody can observe and
  is listed for completeness.
```

### 2b. The class column of eleven rows

Replace the last cell of each row named, leaving the other cells as they are:

| row | new class cell |
|---|---|
| B3 | deliberate in the prototype (`advtext/linebreak.py:41-45`); **switch** `easy_line`, ON in `product()` (D<tex>.2) |
| B3a | deliberate for ragged text only (`advtext/linebreak.py:74-77`), undeclared for justified; **switch** `easy_line`. Hand-worked case: `easy_line_the_last_resort_is_forced_from_the_cheapest_chain` (reference 2, 2, 2, 1 at 1,452 demerits; `tex.web` 3, 3, 1 at 200) |
| B4 | undeclared; **switch** `easy_line`. Hand-worked case: `easy_line_of_two_breakings_with_equal_demerits_the_later_node_wins` |
| B5 | undeclared; **switch** `skips_made_shrink_finite`. §826's error is still not reported |
| B6 | undeclared; **switch** `left_skip_omitted_when_zero` |
| B7 | undeclared; **switch** `post_break_text_stops_pruning` |
| M9 | undeclared; **switch** `italic_kern_only_without_subscript`. Review item T1 |
| M10 | undeclared; **switch** `underline_clearance`. Review item T2 |
| M11 | **not changed, and not established as a departure**: agrees with *The TeXbook* (Appendix G, rule 15e) and, by recollection only, with current `tex.web`; differs from the 2008 copy in this tree (§761 l.14996). Left for the user (D<tex>.3) |
| M13 | undeclared; **switch** `sublist_nucleus_packed`; dimensions equal. Review item T3 |
| M14 | undeclared; **switch** `mu_truncates`. Review item T3 |
| Y1 | undeclared; **switch** `sentence_space_truncates`. One scaled point |

### 2c. Two new rows

After row B3a:

```markdown
| B3b | §851 l.16769, §854 l.16825 | the free break is taken for any forced break, not only an overfull line, when `minimum_demerits` is still `awful_bad`; with one class of line numbers that means no feasible break to this point was found at all | `minimum_demerits` starts afresh for each line count, so at a forced break (a hard line break, the paragraph's end) the last line count's lone node gets the free break although a chain with fewer lines was feasible | undeclared, found by `tex-fixes`; a consequence of B3 and gone under **switch** `easy_line`. `hard-breaks-after-space-justify` at 45 pt: 7 lines at 152,542 demerits, 5 at 10,661 under the switch, no overfull line in either |
```

After row M13:

```markdown
| M13a | §721 l.14202-14209 | "Simplify a trivial box" is applied to whatever `clean_box` returns, a box used as it stands included | a `SUB_BOX` field that is already clean is returned untouched | undeclared, found by `tex-fixes`; under **switch** `sublist_nucleus_packed` the section is applied. No dimension changes |
```

### 2d. Replace "### Counts"

```markdown
### Counts

67 entries: 27 deliberate in the prototype (B12 and B13 with the same result
as TeX; B3a deliberate for ragged text and unweighed for justified), 25
undeclared in the prototype, 12 the port's own, 2 that are not departures
(M5, M6), and M11, which is not established as one.

Of the undeclared ones, eleven are behind nine switches of `TexRules` and are
`tex.web`'s under `TexRules::product()`: B3a, B3b, B4, B5, B6, B7, Y1, M9,
M10, M13 with M13a, M14 (and B3, deliberate, goes with them). Y6 was already
fixed at the public API. Still the prototype's under both presets: B5a, B8,
B14, P3, S3, U3, M12, M15, M16, M17, Y5, Y8 (no visible effect, or reachable
only by input nobody writes), Y4's `str.isupper()`, and Y10.

How many golden paragraphs and formulas each switch moves is in
`docs/decisions.md` D<tex> and is asserted by `tests/test_tex_rules.cpp`.
```

### 2e. A new section at the end

```markdown
## tex-fixes

What the product's rules were checked against, and what they were not.

1. **No TeX engine.** None is installed here. Each switch has a case worked by
   hand from the cited section in `tests/test_tex_rules.cpp`; the comment above
   the test is the working. Agreement with TeX is claimed for those cases.
2. **§854 is taken for a forced break** (§851's `(b > inf_bad) or (pi =
   eject_penalty)`), so on the final pass the last line of a paragraph costs
   nothing when it is forced from the only active node left and nothing
   feasible was recorded at that break. The port has always done this under
   both presets; it is `tex.web`'s. It surprised the first hand-working of B7
   (2,600 demerits, not 7,700).
3. **Under `sublist_nucleus_packed` a `\frac` is a box in a box**: the parser
   makes it an Inner atom whose nucleus is a list holding the fraction, and
   §754 packs that list. `{a \over b}` in TeX is the same shape.
4. **`easy_line` with `line_penalty 0` and a `fil` right skip** sets one word
   to a line: every breaking ties at zero demerits and §855's `<=` lets the
   latest active node win. The reference keeps line counts apart and takes the
   fewest lines. Not reachable through the public API.
5. **The reference's rule for which chain gets the last resort** (the one with
   the most lines) is in the goldens at narrow measures: 17 para and 14 blocks
   compositions, all at columns of 40 px and 60 px. The public-API replays of those
   goldens run under `TexRules::reference()` for that reason.
```

---------------------------------------------------------------------------------------------------

## 3. `docs/performance.md` — append a section

```markdown
## `easy_line` (task `tex-fixes`, D<tex>)

`TexRules::easy_line` gives the optimiser `tex.web`'s classes of line numbers (§835, §848, §850):
past the last specially shaped line every line number is one class, so a breakpoint keeps at most
one active node per fitness class however many lines led to it. `TexRules::product()`, which the
public API uses, has it on. The reference keeps a node per line count, which is where the growth
measured above came from.

`build/<dir>/bench/bench_reflow --only growth`, release, on a machine with a load average of 3 to
5 (another task was building), so read the ratios and not the last digit. The second and third tables raise the work
budget to `MAX_LINE_BREAK_WORK_BUDGET` so that the optimiser is not cut short; they differ only in
the rule. "work" is `LineBreaker::work_done()`, the unit the budget is counted in.

| the optimiser alone, budget at its maximum | x1 | x4 | x16 | x64 | growth per 4x |
|---|---|---|---|---|---|
| justified, a class per line count | 0.092 ms (work 792) | 0.90 ms (9,881) | 9.9 ms (115,938) | 133 ms (1,704,259) | 9.8x, 11.0x, 13.4x |
| justified, `easy_line` | 0.089 ms (792) | 0.53 ms (5,468) | 2.3 ms (24,404) | 10.0 ms (97,218) | 5.9x, 4.4x, 4.3x |
| ragged right by total fit, a class per line count | 5.8 ms (74,607) | 105 ms (1,363,730) | 1.73 s (22,380,790) | 21.5 s, then over the maximum budget (268,442,463): first fit | 18.2x, 16.5x: quadratic |
| ragged right by total fit, `easy_line` | 0.143 ms (1,613) | 0.63 ms (7,286) | 2.58 ms (29,978) | 10.9 ms (120,746) | 4.4x, 4.1x, 4.2x: linear |

As a host gets it (product rules, the default budget of 1,048,576 units), same run:

| | x1 (8 lines) | x4 (30 to 32) | x16 (120 to 128) | x64 (480 to 512) |
|---|---|---|---|---|
| justified, total fit | 0.148 ms | 0.88 ms | 3.8 ms | 16.0 ms |
| justified, first fit | 0.060 ms | 0.239 ms | 0.97 ms | 4.6 ms |
| ragged right, first fit | 0.063 ms | 0.236 ms | 0.73 ms | 3.4 ms |
| ragged right, total fit | 0.140 ms | 0.62 ms | 2.6 ms | 10.9 ms |

(Every table times `LayoutParagraph` whole. This one was the first of the run and was taken while
the machine was busiest: its justified rows do the same work as the `easy_line` rows of the table
above and took 1.6x as long. A second run, under a load average of 5.7, gave medians 1.2x to 1.45x
those of every table here and the same ratios between them; `build/tex-fixes-bench1.txt` and
`-bench2.txt` were the two outputs and are not kept. The work counts depend only on which of the
two alternating widths was laid out last.)

- No row falls back to first fit. In "After the API revision" above, justified x64 and ragged
  total fit from x4 up did.
- At x64 the optimiser uses a tenth of the default budget. The budget is unchanged and is now a
  bound on hostile material and not on long paragraphs.
- Ragged right by total fit is 40 times cheaper at eight lines and about 2,000 times at 512.
- The breaks: over the 1,915 paragraph compositions of the core, para and blocks goldens,
  `easy_line` changes the breaks of 42, and of 57 of the 460 ragged para compositions when they
  are broken by total fit; every one is either a paragraph in which §854's free break was taken
  or one of seven built to tie (D<tex> has the table).

Not measured: the `document`, `heaviest` and `stream` sections of `bench_reflow` were not re-run
for this change; their justified rows are total fit at 8 to 30 lines, where the tables above put
the gain between nothing and 1.7x. `bench_draw` is not affected (layout only). stb_truetype,
clang and MSVC as everywhere else in this document.
```

---------------------------------------------------------------------------------------------------

## 4. `docs/hardening.md` — append to the second pass's table and its notes

A row:

```markdown
| K1 (left open by `harden-2`) | A MATH table whose parameters cancel: `big_op_spacing5` a million ems and `big_op_spacing1..4` minus a million. §750 states the height and depth of a stack of limits, in which the padding and the distances cancel, so `\sum_i^n` in display style was SET, an ordinary box holding kerns of ±18,000,000,000 sp (`MAX_DIMEN` is 1,073,741,823). The same with `default_rule_thickness` minus a million ems: a fraction's rule and the kern above it cancel inside the height §747 states. | `Converter::push` (`src/math_mlist.cpp`): a kern or a rule with a dimension outside ±`MAX_DIMEN` is `TOO_LARGE` with `MathStatus::inside`; nothing is stored. Every node the conversion makes goes through `push`. | `tests/test_math_mlist.cpp`: `two_kerns_that_cancel_inside_an_ordinary_box_are_refused_and_not_stored`, `a_rule_that_cancels_against_its_kerns_is_refused_and_not_stored`. Both fail with the bound taken out (16 failed checks): checked. |
```

And under "Layout core: `tests/test_stress_layout.cpp`":

```markdown
The check count of this file was not constant from run to run (150,963, 150,966, 150,977 on one
binary). Cause: `hostile_measure`, the measure that lies, seeded itself from its `user` pointer,
and `fuzz_hostile_lists_come_back` passed the address of a stack object, which the loader moves
between runs; what the font answered decided which lines reported an error, and a check follows
each error. It is now the round number. Four consecutive runs of one debug binary: 150,967 each.
The fuzz then gained the product's rules on every other list (`TexRules::product()`, for the
second walk of the active list that `easy_line` is): 151,037 checks in debug, the same in every
run. No time-boxed loop was involved; the clock is only compared against.
```
