# tex-web-pass — result (builder-25)

Comments, names and documents only. No behaviour changed; the proof is at the
end. Everything asked for was got to; what was not is the last section.

## Files

All eighteen are done (none half-done):
`src/units.h`, `src/badness.h`, `src/badness.cpp`, `src/nodes.h`,
`src/nodes.cpp`, `src/pack.h`, `src/pack.cpp`, `src/linebreak.h`,
`src/linebreak.cpp`, `src/shipout.h`, `src/shipout.cpp`, `src/math_atoms.h`,
`src/math_mlist.h`, `src/math_mlist.cpp`; citation/naming comments only in
`src/style.h`, `src/style.cpp`, `src/paragraph.h` (`src/paragraph.cpp` needed
nothing). `docs/divergences.md` (a register inserted ahead of the existing
sections, which are byte-identical to what they were), `THIRD-PARTY.md` (one
paragraph under `tex.web`: `MATH_SPACING` is the one verbatim datum),
`.concerns/tex-web-pass--builder-25--undeclared-departures.md` (the reading
list for `tex-fixes`).

Each module header now has "WHAT OF tex.web THIS IS" (a map by section number
and title, with the port's name beside TeX's) and, where there is one, "WHERE
THIS IS NOT tex.web".

## Citations

Section numbers were counted from section starts in `tex.web` (1,380
sections; `@ `, `@*` or a bare `@` at the start of a line), calibrated on §108
`badness`, §749 `make_op`, §859 the demerits.

- Before: about 210 citing lines in these files. Every one was looked up.
- **Wrong, corrected (6):** `pack.cpp` zero-stretch case §659 → §658 (review
  G); `pack.h`/`pack.cpp` `OVERFULL_BADNESS` and the ratio of one, §666 → §664
  (§666 is the report); `math_mlist.h` "the reason section 726 gives" → §725;
  `math_mlist.cpp` "section 726's check_dimensions" → §727; `math_atoms.h`
  `is_cramped` §688's `odd(cur_style)` → the bit is §688's, the test §758;
  `math_mlist.h` `MathFonts` "section 699's fam_fnt" → §700 and §722.
- **Imprecise, made exact (review G):** `build_line` (879, 881–887 by step),
  `find_breakpoints` (866, 868, 869, 873), `hpack` arms (653, 654, 656, 651),
  `vpack` arms (669, 670, 671, 668), `PackMode` (644), `classify` (the §852
  guard, and two cases the review did not list), `run` (863, 864, 874),
  `chain` (878), `post_line_break` (877, 888–890).
- After: 516 citing lines naming 197 distinct sections.
- **A finding about the calibration itself:** `tex-notes.md` and the task
  text say "§851 = try_break's demerits". §851 opens the part of `try_break`
  that computes the badness; the demerits are §859. Nothing in the port
  depended on it.
- One citation in `docs/port-hazards.md` is wrong and is not mine to edit:
  linebreak item 7 says omitting a zero `left_skip` is "not tex.web". §887
  does omit one; the departure is the test used (register B6).

## Renames (10, all file-local or private)

`src/pack.cpp`: `set_glue` → `determine_glue_setting` (it collided with
`DrawCmd::set_glue`; tex.web's name for §657 is that phrase).
`src/linebreak.h`/`.cpp`, all private to `LineBreaker` or local to
`try_break`/`demerits`: `assemble` → `post_line_break` (§877);
`Passive::position` → `cur_break`, `Passive::previous` → `prev_break` (§821);
`Active::passive` → `break_node` (§819); `best_demerits` →
`minimal_demerits`, `minimum` → `minimum_demerits` (§833); `artificial` →
`artificial_demerits`, `stays_active` → `node_r_stays_active` (§851, §854);
`penalty_value` → `pi` (§829).

Not renamed, TeX's name given at the definition instead, because other
builders' files or the tests call them: `MATH_ON`/`MATH_OFF` (math_node
before/after), `RUNNING` (null_flag), `break_paragraph` (line_break), `ship`
(ship_out), `Fitness` members, `PackResult::badness` (last_badness), `vpack`
with `max_depth` (vpackage), `from_mu` (math_glue), `space_factor_after`
(adjust_space_factor), `interword_glue` (app_space's glue), `unskip`
(delete_last), `Break::position`, `how_bad` (b).

## Verbatim tex.web text (review J): paraphrased

`linebreak.cpp` `SATURATED_DEMERITS` and `is_prunable`; `shipout.cpp` three
Pascal statements and `confusion("vlistout")`; `shipout.h` the same in
`ShipError`; `nodes.h` `GlueTotal`'s worked example. Kept on purpose: the five
one-line macro definitions of §702 in `math_atoms.h` (each identifies the line
it transcribes, which the ruling allows) and `MATH_SPACING` (lead ruling T5).

## Departures, by classification (`docs/divergences.md`, the register)

65 entries: **27 deliberate in the prototype** (file and line of the
statement given for each), **24 undeclared in the prototype**, **12 the
port's own** (decision numbers given), 2 not departures.

Undeclared, with an effect somebody could see or measure:

- **M9** §755 l.14868 italic correction with a subscript (T1).
- **M10** §735 l.14449 underline depth (T2).
- **B7** §877 l.17279 pruning after a discretionary with post-break text. NEW.
- **B6** §887 l.17408 / §1229 the left skip is omitted on five-field equality,
  TeX on zero amounts.
- **B5** §827 l.16309 the three skips keep infinite shrink; a line holding it
  always fits.
- **B3a** §854 with no `easy_line`: the last-resort break goes to the chain
  with the most lines. Declared by the prototype for ragged text only. NEW as
  a statement about justified text.
- **B4** §845/§874 tie order (review H).
- **Y1** §1044 `xn_over_d` truncation vs `round()`.
- **M11** §761 l.14996 a fraction spaces as Inner; this `tex.web` leaves it
  Ord. NEW — and probably NOT to be fixed: see below.
- **M14** §703 `cur_mu` rounded where TeX truncates; **M13** §754/§720
  sublist not packed, `clean_box` re-wraps (T3).
- **Y6** §1091 indent twice (already fixed at the API); **Y10** U+00A0 is a
  breakpoint (not a tex.web matter).

Undeclared and without visible effect or reachable only by odd input: B5a,
B8, B14, P3, S3, U3, M12, M15, M16, M17, Y5, Y8.

The port's own bugs found by reading tex.web: **none**.

## T1: how visible

Read from `tests/golden/font/math.answers.golden` (`IC` records), and the
prototype run for `f_i`, `f^2`, `f_i^2`, `x_i`, `\underline{x}`.

- `f` carries 0.090 em (58,982 sp at 10 pt): **every `f_i` in the wild is
  affected**, the subscript 0.09 em too far right (1.4 px at a 16 px em), and
  in `f_i^2` the superscript too.
- Other letters with a correction, in thousandths of an em — lower case: q 34,
  c 25, k 15, b 14, w 3; upper case: F 134, J 106, N 106, U 105, I 85, C 73,
  S 60, X 51, R 24, D 4; Greek: Γ 134, Ν 106, Ι 85, Π 77, ς 74, Ζ 68, Σ 54,
  Χ 51, Ω 42, β 36, δ 36, η 27, Β 25; ∂ 63. So `N_k`, `U_i`, `C_n`, `S_n`,
  `F_n`, `X_i`, `\Gamma_i`, `\Pi_n`, `\Sigma_i` all show it.
- **Not** affected: a d e g h i j l m n o p r s t u v x y z and
  A B E G H K L M O P Q T V W Y Z have no correction in this font, so `x_i`,
  `a_n`, `y_j` are right; and big operators (`\int_a^b`) go through `make_op`,
  which the prototype has right.

## Proof that behaviour did not change

1. **Token streams.** Comments stripped, the 18 files tokenised and compared
   with the snapshot taken before the first edit (`$TMPDIR/twp/tokdiff.py`):
   15 files have identical token streams; `pack.cpp` differs in 3 tokens,
   `linebreak.h` in 5, `linebreak.cpp` in 43, every one of them a pair from
   the rename list above and nothing else. Token counts are equal in all 18.
2. **Suite, unnarrowed, private dirs** `build/tex-web-pass`, `-rel`, `-asan`,
   before and after: see the table. 21 of the 24 test executables that existed
   before have identical test and check counts in debug and in release
   (3,349,024 checks). The three that differ are not this task's doing:
   `test_opentype_wild` (failed before, as expected; `font-wild` has since
   changed it and it passes) and `test_stress_font` (reads `font-wild`'s
   files; time-bounded) changed under another builder, and
   `test_stress_layout`'s count is not a constant: the *same* after-binary
   answered 150,963, 150,966 and 150,977 in three consecutive runs (its
   loops are bounded by a clock). `test_font_loader_wild` is new, `font-wild`'s.
   ctest: before, 26 of 27 passed (`test_opentype_wild`); after, 28 of 28.
3. clang-format was run on the files; the snapshot was already clean under
   it, so it re-wrapped only the lines the renames lengthened.

ASan+UBSan (`build/tex-web-pass-asan`): the same 21 executables identical
(3,349,024 checks), the same three differing for the same reasons, 28 of 28
ctest tests passed, no sanitizer report. The sources were not touched after
the "after" builds were configured.

| | before | after |
|---|---|---|
| debug: ctest | 26/27 (`test_opentype_wild`) | 28/28 |
| release: ctest | 26/27 | 28/28 |
| asan: ctest | 26/27 | 28/28 |
| executables with identical test and check counts | | 21 of 24 in each of the three |
| `test_stress_layout` checks, debug / release / asan | 150,991 / 150,976 / 150,968 | 150,977 / 150,955 / 150,967 (and 150,963, 150,966 on re-running the same debug binary) |

## What I am least sure of, and what nobody asked

- **M11.** That later `tex.web` changed §761 to make a fraction Inner is my
  recollection, unverified. If it is wrong, M11 is an ordinary undeclared
  departure; if right, the prototype matches current TeX and the reference
  copy is the outlier. Somebody should read a current `tex.web` before
  `tex-fixes` touches it.
- **B3a** is a quality defect in justified text with an over-wide word, not
  only the ragged-text cost the prototype measured, and restoring `easy_line`
  would fix it, B4, and the growth the work budget exists for at once. D32.3
  lists the class-per-line-count formulation as deliberate, so it is the
  lead's call.
- The register classifies "deliberate" by a statement I found at a file and
  line. For entries where the statement describes the behaviour without
  calling it a departure (M9's kern, M11) I classed them undeclared.
- B4's and B3a's inputs are described, not constructed: I did not build a
  paragraph with an exact tie.
- `linebreak.h` used to say that which breaking wins does not depend on
  `easy_line`. That sentence is gone; it was true of the demerits only.

## Not got to

- `docs/port-hazards.md` linebreak item 7 and `tex-notes.md`'s "§851" are
  wrong and are not in my files.
- No test was added: nothing non-obvious changed in code. The renames are
  covered by every linebreak and pack test.
- `stb` and `wchar16` configurations were not built.
