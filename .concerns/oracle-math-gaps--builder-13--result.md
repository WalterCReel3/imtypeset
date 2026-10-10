# oracle-math-gaps — result (builder-13)

## What exists now

- `tools/oracle/dump_math.py`
  - `BRACED_SOURCES` (11 sources) appended to `dump_mathtext()` after the `words`
    fuzz, prefix `braced`. Last on purpose: every case name carries a running
    number, so anything inserted earlier renames what follows.
  - `boundary_cases(w)`, called at the end of `dump_mlist()` after the gallery:
    13 cases of the EXISTING kind `mlist`, names `boundary/...`. It asserts its
    own construction (see "Facts").
  - No existing function's output changed; `Kern` added to an import.
- `tests/golden/math/mathtext.golden`: +11 `parse` cases (`braced#3766`..`#3776`), +202 lines.
- `tests/golden/math/mlist.golden`: +13 `mlist` cases, +3 `params` lines in the
  `font-params` case (`math@18001`, `math@4`, `math@5`), +484 lines.
  The other four files are byte-identical.
- `tests/test_math_mlist.cpp`: new test
  `the_boundary_cases_hold_the_references_answers_and_the_port_gives_them` —
  a 13-row table of reference answers checked against the golden's records,
  then each case replayed through `run_mlist_case` under recorded + (fake or
  library) fonts, then the tie case's question records. The four native
  hard-coded tests of builder-7 are untouched apart from the comment above
  them. Floors raised: recorded 2500 -> 2589, fake 1500 -> 1536, library 1000 -> 1053.
- `tests/test_mathtext.cpp`: `a_group_holding_one_bare_ordinary_atom_...` is now
  a table (`BRACED`, the same 11 hard-coded answers, text unchanged); it checks
  golden == table, then port == table, and that the golden has exactly those 11.
  Floors: formulas `> 1400` -> `>= 1545`, refusals `> 2000` -> `>= 2231`.

## The cases

| case | fonts | what it stands on |
|---|---|---|
| `boundary/largest-variant-cover/reached`, `/one-short` | chained 18001, shortfall 327680 | content reaching 12000 / 12001 from the axis; cover is 18000 / 18001.5; bracket is 18001. Ceiling refuses the second (needed 21624, largest 18001), floor would set it |
| `boundary/delimiter-tie/script` | fake 5 | script size 4 and text size 5 both give a bracket 2 high. Refused either way; pins the QUESTIONS (see below) |
| `boundary/op-one-step/chained/{display,display_cramped,text,script}` | chained 18000 | `(` as an Op, chain of 4: display takes U+E001, others U+0028 |
| `boundary/op-one-step/real/{display,text}` | real 32px | Latin Modern's `(` has a chain of 8; display takes the second (U+FFFBE) |
| `boundary/atop-clearance/{display,display_cramped,text,script}` | fake 180 | builder-7's boxes (72/100 over 150/0): 229/157 gap 64 in display, 220/130 gap 28 in text |

## Facts downstream inherits

- Math goldens are now 8,555 cases (was 8,531): mathtext 3,776 parse sources, mlist 2,589 `mlist` cases.
- The oracle reproduces every one of builder-7's hard-coded numbers that I
  re-asked: ceiling at chained 181 / shortfall 0 (181, shift 46; refused 242/181),
  make_op (U+E001 22500/13500; `(` 13500/4500), atop (229/157/64, 220/130/28),
  and all 11 braced lists (the golden equals the table in the test).
- Old goldens: only the SUBSCRIPT half of the group rule was uncovered. Dropping
  `only.sup.empty()` already failed 4 checks before this change; dropping
  `only.sub.empty()` failed none.

## What I ran

- Baseline: `dump_math.py` regenerated into $TMPDIR before any edit — byte-identical to the tree (6 files).
- After: `dump_math.py` (in place). Per-file diff against the pre-change copy:
  removed lines 0 in all six; added 202 (mathtext), 484 (mlist), 0 elsewhere.
  A second regeneration into $TMPDIR is byte-identical.
- `dump_math.py --verify`: 8,555 cases, 10,728 replayed checks ok.
- `tools/gen_mathtext_tables.py --check`: up to date. `ruff check --select F,E9` on the script: clean.
- `build/oracle-math-gaps` (Debug) and `build/oracle-math-gaps-asan` (ASan+UBSan),
  modules units;badness;nodes;pack;linebreak;shipout;style;opentype;fontlib;fontmetrics;math_atoms;math_mlist;mathtext:
  test_math_mlist (29 tests, 219,179 checks), test_mathtext (16 tests, 204,526 checks),
  test_shipout (it also reads mlist.golden) — 3/3 pass in each.
- `clang-format --dry-run -Werror` clean on the two test files.
- Mutation, on a copy of the tree in $TMPDIR (tree untouched; the copy's tests
  have the OLD floors so one binary reads either golden set), running ONLY the
  generic replays (`--filter=mlist_golden_with`, `--filter=every_source_parses`):

  | mutant | old goldens | new goldens |
  |---|---|---|
  | unmutated | pass | pass |
  | M1 `covering = ceil_div` -> `floor_div` | pass | FAIL (2 checks, recorded + fake) |
  | M2 `extent <= largest` -> `<` | pass | FAIL (1 check, recorded only) |
  | M3a `chain[1]` -> last of chain | pass | FAIL (27, all three font kinds) |
  | M3b `chain[1]` -> `chain[2]` when present | pass | FAIL (27) |
  | M4a atop display `7 *` -> `3 *` | pass | FAIL (16) |
  | M4b atop display `7 *` -> `6 *` | pass | FAIL (16) |
  | M4c atop non-display `3 *` -> `2 *` | FAIL (8) | FAIL (24) |
  | M5a group: drop `sup.empty()` | FAIL (4) | FAIL (8) |
  | M5b group: drop `sub.empty()` | pass | FAIL (4) |
  | M5c group: drop both | FAIL (22) | FAIL (30) |

## Decisions

- **Ceiling case uses chained at 18001 with the standard shortfall, not
  builder-7's 181 / shortfall 0.** FORMAT-math.md says the shortfall is 327680
  everywhere but chained@18000 (9000); a 0 would have made that sentence false
  in a file I do not own. 18001 is the same arithmetic (2*18001+1 = 3*12001).
  So the table in the new test holds this case's numbers, and builder-7's
  native test keeps its own.
- **No new record kind and no new `fonts` keyword.** See the weak point below.
- The braced cases go last in mathtext.golden (name numbering).
- Floors are the present counts with `>=`.

## The weak point: the tie is pinned by questions, not by the box kept

`extent <= largest` decides which of two equally tall candidates is kept. For
that to show in the OUTPUT the tied pair must be the largest of all and must
cover 3/4 of the need without reaching it. No face the format defines can do
that: `fake` and `chained` scale with size, so two sizes tie only by rounding
at sizes under 10 sp, where the largest is 1 or 2 and the window
`1.5*delta1 <= largest < needed` is empty (I enumerated it); the real font
likewise. Builder-7's native test uses a bespoke face (`[` 100 high at every
size) for exactly this reason.

What the golden case does hold: the reference measures the text-size bracket,
passes it over, and never asks for its italic correction. The mutant builds the
box and asks. Under RECORDED fonts that is "a question the reference did not
ask" and the replay fails (shown above: `math@5 asked for italic_correction(91, 0)`).
Under FAKE or LIBRARY fonts the mutant passes. The dump asserts the question
set, and the new C++ test checks the records say so.

A result-level pin needs a fourth math-face keyword (a fake whose bracket is
the same height at every size), i.e. a row in FORMAT-math.md section 4.3 and
either support in `math_golden::FakeFonts` or a skip in the fake-font replay.
Both files are outside this task, and the specification said to stop for a
format extension, so it is not done. Builder-7's native test remains the only
thing that pins which box is kept.

## Not verified

- Release build; the presets; `stb` and `wchar16`.
- `dump_core.py` / `dump_para.py` / `dump_draw.py` / `dump_font*.py` were not re-run; none imports
  dump_math.py or reads these two goldens (grep: the font scripts only have a function of that name).
- Mutants other than the eleven above. In particular `chain.size > 1` -> `> 2`,
  and the `floor = min(at_least, needed)` line, were not tried.
- MSVC / other compilers.

## Stale text in files I do not own

- `tools/oracle/FORMAT-math.md`: section 3 says 3,765 sources and 2,576 lists
  (now 3,776 and 2,589); section 5's corpus list has no `braced` prefix; nothing
  mentions `boundary/`. The shortfall sentence in section 4 is still true.
- `docs/port-hazards.md` (last bullet: 2,576 and 3,765) and `docs/divergences.md` `## mathtext` (3,765)
  quote the old counts. They are true of the port as measured then; the files now hold more.
