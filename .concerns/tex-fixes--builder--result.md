# tex-fixes — result (builder)

Spec: `.concerns/tex-fixes--lead--rulings.md` with its 2026-10-10 addendum. The exact text for the
shared documents is in `.concerns/tex-fixes--builder--doc-additions.md` (nothing under `docs/` was
edited except five new pictures); the crash log is `.concerns/tex-fixes--builder--progress.md`.

## Defects and surprises first

1. **A formula could be set holding kerns of 18,000,000,000 sp** (`MAX_DIMEN` is 1,073,741,823).
   This is the case `harden-2` left open with no repro. A cheap one exists: a math face with
   `big_op_spacing5` at a million ems and `big_op_spacing1..4` at minus a million; §750 states the
   height and depth of a stack of limits and the two cancel there. A second: `default_rule_thickness`
   at minus a million ems, where a fraction's rule and the kern above it cancel inside the height
   §747 states. Fixed in `Converter::push` (`src/math_mlist.cpp`): a kern or rule outside
   ±`MAX_DIMEN` is `TOO_LARGE` (`MathStatus::inside`), under both rule sets. Two tests in
   `tests/test_math_mlist.cpp`; both fail with the bound taken out (16 failed checks: checked) and
   pass with it. The math goldens are untouched by it.
2. **The reference's last-resort rule is worse than tex-web-pass described, and is in the goldens.**
   §854's free break is taken for *any* forced break (§851), not only an overfull line. Because the
   reference's `minimum_demerits` starts afresh for every line count, at a hard line break the chain
   with the most lines gets a free line even though a shorter chain was feasible. One golden shows
   it with no overfull line anywhere: `hard-breaks-after-space-justify` at 45 pt is 7 lines at
   152,542 demerits under the reference and 5 at 10,661 under `easy_line`.
3. **`test_stress_layout`'s count varied because a test seeded a random generator from a stack
   address** (`hostile_measure(user = &arena)`), not because of a clock. Fixed in the test.
4. **My first hand-working of B7 was wrong** (7,700 demerits; the answer is 2,600) for the same
   §851/§854 reason. The test caught it; the comment now shows the right working.
5. **In the pictures** (looked at, enlarged): nothing collides. `f_i` at a 32 px em: the subscript
   moves 3 px left and sits under the hook of the `f`; in `f_i^2` both scripts move 2 px.
   `\underline{x}` alone is pixel-identical under both rule sets (only the box's depth changes). In
   `\frac{\underline{x}}{y}` the underlined numerator is 1 px higher, and the gap between the
   underline and the fraction bar goes from 1 px (it read as a double rule) to 2 px. The underline
   under D7's product draw rule spans its glyphs exactly (x 262..299 for glyphs 262..299 in the
   16 px row). One thing a reader may question: in `F_n^2` the `2` now sits close to the arm of
   the `F`; that is advance + δ, the same place as in `F^2`, where the reference had 2δ.
6. **Another agent is writing in the tree** (`src/imtypeset_demo.cpp`, `examples/imtypeset_demo.h`,
   `examples/demo_headless.cpp`, since about 08:20), although my launch note said I was the only
   writer. The ctest count went 35 → 36 → 37 between my runs for that reason. Nothing of theirs
   failed in my runs.

## Per departure

| # | fixed under `product()`? | switch (`src/tex_rules.h`) | where | test (`tests/test_tex_rules.cpp`) |
|---|---|---|---|---|
| M9 §755 | yes | `italic_kern_only_without_subscript` | `Converter::translate_nucleus`, which now takes whether the atom has a subscript | `m9_...`: `f^2` 642,252 sp both; `f_i` 571,145 → 512,163; `f_i^2` 701,234 → 642,252, superscript shifted δ = 58,982 inside the pair; `x_i` and `\int_a^b`, `\sum_i^n` unchanged |
| M10 §735 | yes | `underline_clearance` | `Converter::make_under` | `m10_...`: depth 112,065 → 138,279 (θ = 26,214), nothing else moves |
| M13 §754/§720 | yes | `sublist_nucleus_packed` | `translate_nucleus` (pack), `clean_pack` (already clean), `simplified` (§721) | `m13_...`: `{a+x}^2` is hbox + script; a nested fraction's numerator is one box around the vlist, not two; dimensions equal |
| M14 §703 | yes | `mu_truncates` | `MathFonts::mu` | `m14_...`: 36,409 → 36,408 at 10 pt; `a=b` 10 sp narrower |
| Y1 §1044 | yes | `sentence_space_truncates` | `interword_glue` (new last argument), `Composition::rules` | `y1_...`: space 15 sp, factor 3000: shrink 2 → 1; factor 999: stretch 8 → 7 |
| B5 §827 | yes | `skips_made_shrink_finite` | `LineBreaker::break_paragraph` | `b5_...`: one line of 4 at 100 demerits → 2 + 2 at 584 |
| B6 §887 | yes | `left_skip_omitted_when_zero` | `LineBreaker::left_skip_omitted` (used by `build_line` and `trial`) | `b6_...`: 7 nodes → 6 |
| B7 §877 | yes | `post_break_text_stops_pruning` | `LineBreaker::build_line` | `b7_...`: second line 14 pt → 19 pt; total fit and first fit |
| B3a, B4 §835/§848/§850/§854 | yes, and **ON** | `easy_line` | `LineBreaker::try_break_by_class` (a second walk; `try_break` is untouched), `ParShape::last_special_line` | `easy_line_...` ×5: last resort (2,2,2,1 at 1,452 → 3,3,1 at 200), ties (3,1 → 1,1,1,1), a hanging shape (deferred creation at `easy_line`), brute force over every breaking for three shapes, the work budget |
| M11 §761 | **no**, as ruled | — | comment only | reclassified in doc-additions §2b |
| B5a, B8, B14, P3, S3, U3, M12, M15, M16, M17, Y5, Y8 | no, as ruled (no visible effect) | — | — | listed in doc-additions §1 and §2d |

Each hand-worked case is also run under `reference()` in the same test, so the test shows what the
switch changes. **No TeX engine is installed here; nothing was compared with TeX's output.**

Not in the ruling, done because the fixes needed them:

- `Paragraph::artificial_breaks` (how often §854 was taken): needed to classify what `easy_line`
  moves, and useful to a host.
- `ParagraphComposer::breaker()`: the bench needs the budget.
- §721 on an already-clean box (new register row M13a), and the new register row B3b (item 2 above).
- `tests/test_api.cpp` and `tests/test_api_math.cpp`: one line each, so their golden replays run
  under `TexRules::reference()` (they failed in 32 and 70 checks under product, as they should).
- `src/context.h`: `tex_rules` beside `draw_rules`. `src/imtypeset.cpp`: five lines, not one (the
  rule is read in three places: math fonts, two `Composition`s, the breaker's parameters).

## The `easy_line` decision: ON

(a) Golden replays run under `reference()` and are untouched: every replay test passes unchanged in
all five configurations.

(b) Under `easy_line`, breaks change in:

| corpus | paragraphs (total fit) | breaks changed | of which §854 taken | ties | other | line boxes |
|---|---|---|---|---|---|---|
| core | 500 (336) | 11 | 4 | 7 (built with `line_penalty 0`) | 0 | 146 of 2,864 |
| para, as the reference composes it | 710 (250) | 17 | 17 | 0 | 0 | 669 of 4,902 |
| para, ragged blocks by total fit | 460 (460) | 57 | 57 | 0 | 0 | 2,085 of 3,712 |
| blocks | 705 (465) | 14 | 14 | 0 | 0 | 120 of 1,694 |

In every changed paragraph the product has fewer lines. No total-fit paragraph anywhere has
different total demerits unless §854 was taken (asserted 0). The justified ones are all at the 40 px
and 60 px columns. So the expectation holds, with the amendment in "Defects" item 2.

(c) `bench_reflow --only growth`, release, budget at maximum (medians; run 1 of 2, load average 3-5):

| | x1 | x4 | x16 | x64 |
|---|---|---|---|---|
| justified, reference classes | 0.092 ms | 0.90 ms | 9.9 ms | 133 ms |
| justified, `easy_line` | 0.089 ms | 0.53 ms | 2.3 ms | 10.0 ms |
| ragged total fit, reference classes | 5.8 ms | 105 ms | 1.73 s | 21.5 s then over the maximum budget |
| ragged total fit, `easy_line` | 0.143 ms | 0.63 ms | 2.58 ms | 10.9 ms |

Growth per 4x of text is 4.1x to 4.4x where it was 16x to 18x. With the default budget no row
falls back to first fit (work at x64 is about 100,000 of 1,048,576 units). The budget is kept.

## How much ordinary text moves (B5, B6, B7, Y1)

Break sets / line boxes differing from `reference()`, one switch at a time:

| corpus | B5 | B6 | B7 | Y1 |
|---|---|---|---|---|
| core (500 paragraphs, 2,864 line boxes) | 4 / 22 | 0 / 2 | 0 / 3 | n/a |
| para (710, 4,902) | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| para, ragged by total fit (460, 3,712) | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| para, every case forced NONFRENCH (710, 4,903) | | | | 0 / 55 (21 of 4,171 glues, 1 sp each) |
| blocks (705, 1,694) | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |

The core cases that move under B5, B6, B7 are the ones the oracle built for those inputs. Formulas
(1,950 golden sources): M9 changes 22 widths, M10 14 depths, M14 181 widths by at most 40 sp, M13
none; none is refused under one preset and set under the other.

Commands: `( ulimit -v 6000000; timeout 600 build/<dir>/tests/test_tex_rules )` prints every row
(lines starting `tex-rules`) and asserts every figure. `build/<dir>/bench/bench_reflow --only growth`
for (c). Pictures: `IMTYPESET_TEXFIX_SHOTS=docs/images build/<dir>/tests/test_tex_rules --filter=pictures`.

## `test_stress_layout` (ruling item 6)

Cause in "Defects" item 3. After the fix four consecutive runs of one debug binary gave 150,967
checks each. I then made the hostile fuzz run every other list under `TexRules::product()`:
151,037 in debug and under ASan, the same on every run. Contract violations in that fuzz went
8,546 → 8,549. With the product's rules in, they are of seven kinds, six of them `NodeArena`'s and
the seventh "badness: t must not be negative" (135); "break_paragraph: no pass produced a
breaking" is not among them. I did not take the breakdown before the change, so which kind gained
three is not established.

## What was run

Private directories, unnarrowed (everything the globs find), built `-j4 -Werror`, every binary
under a guard (`ulimit -v 6000000`, or `ASAN_OPTIONS=hard_rss_limit_mb=3000` with
`UBSAN_OPTIONS=halt_on_error=1`), one configuration at a time:

| configuration | build | ctest | note |
|---|---|---|---|
| debug, FreeType 2.12.1, 32-bit ImWchar | ok, 0 warnings | 35 of 35 | before the demo task's files appeared |
| ASan + UBSan | ok, 0 warnings | 35 of 35, no sanitiser report | the picture test also run under it by hand |
| release | ok, 0 warnings | 36 of 36 | |
| stb_truetype | ok, 0 warnings | 37 of 37 | |
| 16-bit ImWchar | ok, 0 warnings | 37 of 37 | |

Then, on the tree as it stood at the end (my sources unchanged since the runs above, the demo
task's `test_demo` and `example_demo_headless` now present): debug 37 of 37, ASan + UBSan 37 of 37,
both rebuilt first.

Counts under ASan: `test_tex_rules` 20 tests, 42,470 checks; `test_math_mlist` 31, 219,211;
`test_linebreak` 13, 3,553; `test_linebreak_totals` 2, 335,325; `test_style` 16, 183,609;
`test_paragraph` 20, 767,445; `test_blocks` 23, 128,674; `test_mathtext` 17, 201,809; `test_api`
40, 71,574; `test_api_math` 19, 28,839; `test_harden` 21, 326,071; `test_draw` 16, 17,135;
`test_stress_layout` 16, 151,037.

`test_api_math` is 28,839 where harden-2 reported 28,841. Checked: with the context defaulted to
`reference()` it is 28,841 again, so two checks follow the shape of a formula's list (which has
two nodes fewer somewhere under product). Nothing fails either way; I did not find which test.

**Not run:** the presets (`build/debug` ...); the throwing-`IM_ASSERT` build (`test_exception_safety`
is a skip elsewhere); clang, MSVC, Windows; ImGui 1.93; the SDL2 demo; the Python host;
`bench_reflow`'s other sections and `bench_draw`. The two bench runs were on a loaded machine
(load average 3 to 5.7); the ratios are safe and the last digits are not. `docs/images/py-host.png`
and the other existing pictures were not re-shot and show the reference's `f_i` spacing wherever
they show a subscripted `f`.

## Files changed

New: `src/tex_rules.h`, `tests/test_tex_rules.cpp`, `docs/images/texfix-f_i.png`,
`texfix-f_i-sup.png`, `texfix-underline.png`, `texfix-underline-in-fraction.png`,
`texfix-letters.png`, `.concerns/tex-fixes--builder--{progress,doc-additions,result}.md`.

Edited: `src/linebreak.h`, `src/linebreak.cpp`, `src/style.h`, `src/style.cpp`,
`src/math_mlist.h`, `src/math_mlist.cpp`, `src/paragraph.h` (one accessor), `src/context.h` (one
member, one include), `src/imtypeset.cpp` (five lines), `tests/test_math_mlist.cpp` (two tests
appended), `tests/test_stress_layout.cpp`, `tests/test_api.cpp` (one line), `tests/test_api_math.cpp`
(one line), `bench/bench_reflow.cpp`.

Not edited, though the ruling listed them: `src/pack.*`, `src/paragraph.cpp`, `src/blocks.*`
(the rules ride in `LineBreakParams`, which those already pass through), `tests/test_linebreak.cpp`,
`tests/test_style.cpp` (the tests are all in `tests/test_tex_rules.cpp`), and the shared documents.
Nothing was `git add`ed. `build/tex-fixes*` is removed.

## For whoever owns them

- `fold-docs`: the doc-additions file has four sections (decisions, divergences, performance,
  hardening).
- `api-document` / whoever wires `Document` into the public API: set
  `composer.params.rules = ctx.tex_rules` and the blocks' `Composition::rules`; today the block
  layer defaults to the reference's rules and nothing public reaches it.
- `docs/port-hazards.md` linebreak items 6 and 7 describe the reference's behaviour as the port's;
  under `product()` both are now `tex.web`'s. Not mine to edit.
- `include/imtypeset.h` says nothing about which rules layout follows. A sentence there ("laid out
  as `tex.web` does where the prototype differed; see `docs/divergences.md`") is the header
  owner's call.
- M11 still needs someone to read a current `tex.web`.
