# oracle-core-thresholds — result

## What exists now

- `tools/oracle/dump_core.py`: new `THRESHOLD_GLUE` and `fitness_threshold_cases(w)`,
  called from `dump_linebreak()` after the `random-NN` cases and before the
  `parshape` cases. No existing function's signature or output changed.
- `tests/golden/core/linebreak.golden`: six new cases of the EXISTING kind
  `linebreak` (no new record tag, no new case kind), 12 runs, 638 lines, one
  contiguous insertion:
  `fitness-threshold-stretch-99` (runs `permissive`, `pretolerance-99`, `pretolerance-98`),
  `fitness-threshold-stretch-100` (`permissive`, `pretolerance-100`, `pretolerance-99`,
  `final-pass-tolerance-99`, `final-pass-tolerance-100`),
  `fitness-threshold-stretch-12`, `-stretch-13`, `-shrink-12`, `-shrink-13` (`permissive` each).
  Each has the usual ship section (`HAND_SKIP`), so test_pack / test_shipout /
  test_nodes replay them too.
- `tests/test_linebreak.cpp`: `fitness_class_thresholds_fall_where_the_reference_puts_them`
  no longer builds lists natively. It reads the six cases, checks the golden
  against the 12-row table (line count, total demerits, pass, and the badness
  and hpack fitness of the line under test), then replays each through
  `replay_case` (port vs golden, full text). The generic replay's floors were
  raised to cases >= 142, runs >= 500 and two of the new names added to its
  "needed" list.

## Facts downstream inherits

- core goldens are now 599 cases (linebreak.golden 123: 116 `linebreak` + 7
  `parshape`); linebreak + paragraphs hold 143 `linebreak` cases / 500 runs.
- The 12 reference answers in the golden equal the 12 hard-coded ones the
  linebreak builder recorded, value for value.
- The dump asserts its own construction (badness of the line under test, pass
  used, and that each threshold pair's totals differ by the badness term plus
  adj_demerits). A regeneration that stops landing on a threshold fails in the
  script.
- hpack's `PackFitness` for a line of badness 100 stretching is `loose`, not
  `underfull`; it is the breaker's own classifier (§817) that calls it very
  loose. The two are different classifications; the table records both.
- very-loose first line is charged adj_demerits twice (against the paragraph
  start, which counts as decent, and against the decent last line): 27754 -
  11981 = 219 + 2*7777.

## What I ran

- `dump_core.py` (regenerate): per-file diff against a pre-change copy:
  0 removed lines in all seven files; 638 added in linebreak.golden, 0 elsewhere.
- `dump_core.py --verify`: 599 cases, 6624 replayed checks ok.
- `dump_math.py --verify`: 8531 cases, 10701 checks ok. `dump_para.py --verify`:
  253 cases, 2088 ok. `dump_selection.py --verify`: 57 cases, 25267 ok.
- `dump_math.py` and `dump_para.py` regenerated into $TMPDIR (OUT_DIR
  redirected by a driver; the tree's files not rewritten): byte-identical to
  tests/golden/math and tests/golden/para.
- build/oracle-core-thresholds (Debug) and build/oracle-core-thresholds-asan
  (ASan+UBSan), modules units;badness;nodes;pack;linebreak;shipout:
  test_nodes, test_pack, test_linebreak, test_shipout — 4/4 pass in each.
  test_linebreak: 12 tests, 3550 checks.
- Mutation, on a copy in $TMPDIR (tree untouched): the three threshold
  comparisons in src/linebreak.cpp (`> 99`, stretch `> 12`, shrink `> 12`),
  each moved one up and made `>=`: all six mutants fail BOTH
  `line_breaking_matches_the_reference` and the dedicated test.
- clang-format --dry-run -Werror clean on tests/test_linebreak.cpp.

## Not verified

- That the six mutants survived the goldens BEFORE this change (builder-2's
  finding) was not re-run; only that they die now.
- Release build not run. Presets not run. dump_draw.py / dump_font*.py not
  re-run (they do not replay core cases as far as grep shows).
- No mutation of the feasibility comparison (`badness > threshold`); the
  pretolerance runs are there for it but were not mutation-tested.
- The two `final-pass-tolerance-*` runs both total 0 (as in the original
  table); they pin "a forced break on the final pass is kept and free", not a
  threshold.
