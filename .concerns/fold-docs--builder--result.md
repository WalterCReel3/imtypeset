# fold-docs: result (builder-9, 2026-10-10)

The four `.concerns/*--doc-additions.md` files (font-wild-finish, harden-2, c-api, tex-fixes) are
folded into the shared documents. Text was moved by a script that cut it out of the additions
files and asserted every anchor it replaced matched exactly once; nothing was retyped. **No binary
was run.** What was checked against the tree was checked by `grep` and by reading, and is listed
under "Checked" below. Nothing was `git add`ed or committed.

## D-number mapping

Highest present before: D37. Assigned in the order the tasks completed.

| placeholder in the additions | assigned | entry |
|---|---|---|
| `D-FW` (font-wild-finish) | **D38** | Font reader: five rulings from finishing `font-wild` |
| `D<harden>` (harden-2) | **D39** | Hardening, second pass |
| `D<n>` (c-api; it guessed D38) | **D40** | The C ABI: `include/imtypeset_c.h` |
| `D<tex>` (tex-fixes) | **D41** | `TexRules::reference()` and `TexRules::product()` |

Cross-references rewritten to those numbers: `D-FW.5` -> `D38.5` (hardening row W-D4);
`D<harden>, item 10` -> `D39, item 10` (hardening row D7); every `D<tex>`, `D<tex>.2`, `D<tex>.3`
-> `D41`, `D41.2`, `D41.3` (divergences register, performance). Four references that named "the
decision" or a section of the additions file, which mean nothing once folded, were pointed at the
number: hardening X2 "(decision above, item 7)" -> "(D39, item 7)"; X3 "(decision item 8)" ->
"(D39, item 8)"; hardening Not verified "see the decision's own list" -> "see D39's own list";
performance "see the decision for what was not done" -> "see D39 for ..."; and D41 item 6
"two were found (section 4)" -> "(`docs/hardening.md`, row K1)".

## What went where

| file | from | what |
|---|---|---|
| `docs/decisions.md` | all four | D38, D39, D40, D41 appended at the end, in that order |
| `docs/hardening.md` | font-wild-finish | heading 16 -> 22 tests; the mutants table and its paragraph; the census table, the "same executable in the other builds" paragraph and the 10x paragraph (replacing the old census intro line, table, "Wall time" and "10x" paragraphs); rows N1, W-D1, W-D2, W-D3, W-D4, W-K after S9 in "Font stack"; the "Since `font-wild`" text appended to the "Installed fonts" paragraph |
| | harden-2 | `## Draw` rows D6, D3, D4, D7 replaced; new section `## Second pass` before `## Counts`; four bullets appended to `## Not verified`; the 2d test counts as a paragraph under the `## Counts` table (that table totals review items, not tests, so there was nothing to add them to) |
| | tex-fixes | row K1 appended to the Second pass table; the `hostile_measure` paragraph at the end of "Layout core: `tests/test_stress_layout.cpp`" |
| | c-api | its one optional bullet, at the end of the Second pass section |
| `docs/divergences.md` | tex-fixes | register intro bullet replaced; class cell of twelve rows replaced (B3, B3a, B4, B5, B6, B7, M9, M10, M11, M13, M14, Y1; its heading says eleven, its table lists twelve); rows B3b and M13a added; `### Counts` replaced; `## tex-fixes` appended at the end |
| | harden-2 | three bullets added to `## draw` (overlay, `DrawRules::product()`, white texel); `## mathtext` item 5 replaced and items 7 to 9 added; `## api-revise` items 9 and 10 (the additions wrote them as bullets; the list there is numbered) |
| | font-wild-finish | `## font-wild` items 10 to 13 under a lead-in after item 9. Items 7 to 9 of that section already follow the "Beyond the reference" list and are cited by number elsewhere, so the new ones are numbered on rather than inserted |
| `docs/performance.md` | harden-2 | `## After the second hardening pass` before `## Not measured`; two bullets appended to `## Not measured` |
| | tex-fixes | `## \`easy_line\`` after harden-2's section and before `## Not measured` (the addition says "append"; `## Not measured` is that file's last section and the new text says "above" of what precedes it) |
| `docs/port-hazards.md` | (lead) | linebreak items 6 and 7: one sentence each, that the item describes `TexRules::reference()` only and which switch of `product()` changes it (D41) |
| `THIRD-PARTY.md` | — | **not edited.** font-wild-finish is right: the fontTools notice is in `src/opentype.cpp` (the `UPSTREAM NOTICE` block above `BoundsPen`, pointers at the two other sites) and in `tools/oracle/check_font_bounds.py`, and the "**Open:** the notice is not yet at the point of use" paragraph is no longer in the file. The docs-hygiene edit is not outstanding. c-api also adds nothing |

The one known case, carried as written: harden-2's D7 draw rule is recorded as done in D39 item
10, hardening row D7 and the `## draw` bullet in divergences, and each has one sentence under it
saying the rule is under revision in task `d7-fix` (`.concerns/review-3a--reviewer--findings.md`).

## Not applied, and why (reported to lead)

1. **`try`/`catch` in `src/`.** harden-2 (D39 item 11): "there is no `try` or `catch` in `src/`".
   c-api (D40): "`src/imtypeset_c.cpp` is the one file of the library with `try` and `catch`, under
   `#if defined(__cpp_exceptions)`: each entry catches, fails as its return type fails (0, -1 or
   null), and `ImTypeset_GetLastError()` answers `ImTypeset_Error_Exception` (1000, an enumerator
   only the C header has) with the exception's message." Neither is in the documents: the clause is
   cut from D39 item 11, the sentence from D40's "No exception crosses" bullet, and the three words
   "No `try`/`catch`." from hardening row X1. The tree: `grep` finds `try {` and two `catch` in
   `src/imtypeset_c.cpp` and none in any other `src/*.cpp` or `src/*.h`.
2. **1,023 or 1,024 quads.** `docs/divergences.md`, `## draw`, first bullet (already in the tree):
   "the port's commands split up to 1,023 quads earlier". harden-2: "by up to 1,024 quads". Its
   divergences bullet "Past 64k vertices with 16-bit indices a draw command ends earlier ..." would
   sit in the same list as the other count and is **withheld**. The same 1,024 is in D39 item 9 and
   in hardening row D4 ("bound: 1,024"), which are applied as harden-2 wrote them because cutting
   the count there removes the item; all three are named in the concern.

## Left stale by the additions, not touched (nobody supplied text)

- `docs/hardening.md` `## Counts`, row "the draw review": still "D3, D4, D7 not done". A pointer
  sentence under the table says the second pass took them up; the row is unchanged.
- `docs/hardening.md` row S2 still says "63 cases"; the paragraph "Run once more at 10x on the
  tree as it ends ... 8,053 + 16,800 + 484 mutants" predates the 53 seeds; `## Not verified` still
  says a hand-built command list was not given to `src/selection.cpp` (H5 now does) and that
  `src/math_mlist.cpp` keeps unchecked sums.
- `docs/divergences.md` `## draw`: "The buffer output ... the debug overlay's line boxes and
  baselines ... are left out of it" is overtaken by harden-2's overlay bullet in the same list.
  The register's first bullet still says deliberate entries "stay (D32.3)" while B3 is now a switch.
- `docs/decisions.md` D32.3 has no "amended by D41" marker; D41 says it amends it.

## Checked (by grep or reading; nothing run)

`^TEST(` counts: `tests/test_stress_font.cpp` 22, `tests/test_harden.cpp` 21 (d7-fix is editing
it), `tests/test_exception_safety.cpp` 8 (7 + 1), `tests/test_mathtext.cpp` 17, `tests/test_api.cpp`
40, `tests/test_stress_layout.cpp` 16. Symbols present: `MAX_SOURCE_BYTES` (2^19), `rule_joins_span`,
`RULE_JOIN_LOOKBACK` = 64, `CmapRange::one_glyph`, `atlas_box_repairs`, `artificial_breaks`,
`IMTYPESET_C_ABI_VERSION` 1, `src/tex_rules.h`. Arithmetic inside the additions: the census columns
sum, 53 seeds, 67 register entries, 1,915 compositions and 42 changed, the percentages in the
performance tables. No check count, timing or mutant count in the additions was reproduced.

## Files

`/home/wreel/Source/imtypeset/docs/decisions.md`, `docs/divergences.md`, `docs/hardening.md`,
`docs/performance.md`, `docs/port-hazards.md` edited; `THIRD-PARTY.md` untouched.
