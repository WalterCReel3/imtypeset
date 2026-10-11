# Text for `docs/divergences.md`: tables (`src/align.*`)

Written by the helper that wrote `src/align.*` for task `tables-core`. Not
applied: `docs/divergences.md` is not the helper's file. Everything between the
two rules below is meant to be appended as it stands, in the register's format.
Section numbers were counted from the top of `tex.web` (WEB modules: lines
beginning `@ `, `@*` or a lone `@`) and each was read; the count was calibrated
on §768 (`@* \[37] Alignment`, l.15108), §679 (`append_to_vlist`, l.13312),
§649 (`hpack`), §668 (`vpack`) and §977 (`vsplit`). Reference lines are those
of the pinned tree, `build/advtext-reference/advtext/ui/__init__.py`.

---

## tables-core

`src/align.*`: the reference's table layout (`_natural_widths`,
`_longest_word`, `_column_widths`, `_apportioned`, `_squeezed`, `_shared`,
`_table_boxes`, `_table_row`, `_wrapped_row`, `_cell_lines`, `_cell_params`,
`_aligned`, `_WrappedRow`, `prev_depth_after` and `_stacked`'s arm for it, of
`advtext/ui/__init__.py`). The port is the reference: `tests/test_align.cpp`
replays the 36 `table` cases of `tests/golden/tables` (`widths`, `shapes`,
`demo`; 97 layouts, 441 rows) record by record, exactly, twice over (the steps
one at a time, and `Aligner::fin_align`).

### Alignment — `src/align.*` (tex.web part 37, §768–§812)

| # | tex.web | TeX | the port | class |
|---|---|---|---|---|
| T1 | §801 l.15767, §804 l.15836 | the final width `w_j` of a column is the widest entry in it (spans aside) and nothing changes it; `\halign to` and `spread` are met by packaging the prototype row, which stretches or shrinks the tabskip glue between columns | a table whose widest cells sum to no more than the measure has every column scaled **up** in proportion, `widest[c] * available // total`, the last column taking the remainder, so the table always fills the measure (`apportioned`) | deliberate: `ui/__init__.py:2211-2226`, `2015-2017` |
| T2 | §801 l.15767, §810 l.15937 | a table wider than its `to` is an overfull prototype row: tabskip shrink is used up and the columns stay at `w_j` | each column is given its longest unbreakable piece plus padding and the measure left over is shared in proportion to `widest - narrowest`, the last column taking the remainder (`squeezed`); when the floors alone exceed the measure they are the widths and the table is wider than it | deliberate: `ui/__init__.py:2018-2031`, `2108-2120` |
| T3 | §801 l.15767 | no counterpart: a width is never given, only a template | a caller's absolute widths are used as they are, whatever the measure; a caller's fractions divide the measure, each share rounded half to even and floored at 1 sp, the last column taking the remainder and borrowing from the first of the widest what would leave it under 1 sp (`shared`) | deliberate: `ui/__init__.py:1580-1611`, `2058-2071` |
| T4 | §801 l.15767 (`null_flag`, "which we regard as −∞"), §802 l.15804 | a column's width starts at `null_flag` and becomes 0 only if no entry ended in it | starts at 0, so a column whose entries are all narrower than nothing is 0 wide | undeclared (`ui/__init__.py:2041`, `[0] * columns`); reachable only with a cell of negative natural width and no padding to cover it |
| T5 | §796 l.15665 | an entry is packaged as it ends, unset, and its natural width recorded; its stretch and shrink totals are kept in the unset node for §810 | a cell is a list the caller keeps; its natural width is one `hpack` asked for when wanted, with the padding added (`natural_width`, `fin_col`), and nothing is unset | deliberate, as representation: `ui/__init__.py:2144-2147` (two passes over the cells) |
| T6 | §810 l.15937 | an entry wider than its column has its glue set as far as it goes and is left overfull; an entry is restricted horizontal material and is never broken | a row with a cell whose natural width exceeds its column has **every** cell of the row, the ones that fit included, broken first fit at `max(column - 2 * padding, 1 pt)` from the material with its shrink removed, ragged on the side its alignment leaves free and without `\parfillskip` unless left-aligned | deliberate: `ui/__init__.py:2149-2168`, `2270-2291`, `2374-2410` |
| T7 | §1087 l.21078 (`\vtop`), §807 l.15881 | a `\vtop` is a user's box: height the first item's, the rest depth | each cell of a wrapped row is that reboxing of its lines, done here: `vpack`, then height := the first line's strutted height and depth := the rest; its width is the column's whatever its lines are | deliberate: `ui/__init__.py:2400-2404` |
| T8 | §679 l.13312, §799 l.15714 | `fin_row` appends the row by `append_to_vlist`, and the next row's interline glue is computed from the row's own depth | after a wrapped row the next append is handed the depth of the row's **lowest line** (`strutted.depth - lowest`), not the row's depth, which is every line below the first; the row's box is unchanged (`SetRow::prev_depth`, `TableBox::prev_depth_carried`) | deliberate: `ui/__init__.py:2316-2350`, `1162-1164` (what a document that assigned `\prevdepth` after the row would get) |
| T9 | §778 l.15379, §795 l.15661, §809 l.15920 | columns are separated by tabskip glue, which is in the row's list between the entries' boxes and takes part in the prototype row's glue setting | no glue between columns: the padding is a kern on each side **inside** each cell's box, column widths include both, and a row is its cells' boxes side by side at natural width | deliberate: `ui/__init__.py:2246-2257` (`_table_row`), `1600-1603` |
| T10 | §807 l.15881 (`shift_amount(q):=o`) | a row is shifted by the display indent, or not at all | the table's indent is a kern at the head of every row's list (and of the rule's box), and the row's `shift_amount` is 0 | deliberate in effect: `ui/__init__.py:2246`, `2292`, `2197` (no comment says why) |
| T11 | §797 l.15683, §798 l.15699, §803 l.15812, §808 l.15898; §788 l.15553, §790 l.15586; §785 l.15499; §811 l.15958 | `\span`, `\omit`, `\noalign`, `\valign` | none: one cell, one column, rows only. A row with fewer cells than the table has columns is given an empty cell for each it lacks (packed to the column's width, or one empty line when the row wraps); §807 leaves such a row's list short | deliberate: the reference's `TableBlock` has no such thing; `ui/__init__.py:2248`, `2295` for the missing cell |
| T12 | §777–§784 l.15367-15498, §789 l.15567, §793 l.15640, §794 l.15649 | the preamble's `u_j` and `v_j` templates are inserted around each entry | no templates: a column's alignment is `\hss` (`0 plus 1fil minus 1fil`) after the material, before it, or both, inside the cell's box (`aligned`) | deliberate: `ui/__init__.py:2456-2462` |
| T13 | §806 l.15871 | a rule between rows has running dimensions, extended to the alignment's edges when it is set | the rule under a header is an hbox of the indent kern and a rule of written width `sum(columns)` and the theme's thickness, not strutted; there is no other rule | deliberate: `ui/__init__.py:2193-2206`, `2222-2224` |
| T14 | §799 l.15714, §805 l.15859 | a row's height and depth are its entries' | each row is floored at the table's strut, once, on the row; a line of a wrapped cell is floored at it too | deliberate: `ui/__init__.py:2188-2191`, `2400-2401` |
| T15 | §800 l.15736, §796, §799 | rows are packaged unset as they are read and all set at the end by `fin_align` | a row is built once, at widths already decided, and appended afterwards; `fin_row` here is only §799's `append_to_vlist` | the port's own (representation); no decision number |
| T16 | — | no counterpart | the longest unbreakable piece of a cell is asked of the line breaker: the cell's material (shrink left in) broken first fit at a measure of 1 pt with no left or right skip, and the widest line's natural width taken (`Aligner::longest_word`); measured only when the table does not fit | deliberate: `ui/__init__.py:2086-2092`, `1888-1890` |
| T17 | — | no counterpart | a policy the reference refuses when the table is constructed (not one entry per column; a width under 1; a fraction that is not finite and positive, or nothing as a share of the sum) is refused here at the solve: the default policy is taken and `ColumnSolve::refused` says why. Also refused, the port's alone: a share whose width `available * share / total` is not finite, for which the reference's `round` raises | the port's own: D17 (no exceptions; a status for anything reachable from data) |
| T18 | — | no counterpart | sums through `add_checked`; `a * b // total` through a 128-bit product where the compiler has one (otherwise exact while the product fits 64 bits, and a double's quotient with `ColumnSolve::overflowed` past that); the style's lengths clamped to `MAX_DIMEN`; at most 2^20 columns; a BOX or DISC node the arena does not hold, or one nested within 8 levels of `NodeArena::MAX_NESTING`, set as an empty rule and reported as `NODE_NOT_IN_ARENA` | the port's own: D17, D33 |

Stated in `src/align.h` ("WHERE THIS IS NOT tex.web") and at each function in
`src/align.cpp`.

### Where the reference's two paths differ, and which the port follows

1. **A negative natural width in a column only later rows have.**
   `_column_widths` (`ui/__init__.py:2041-2044`) starts every column at 0;
   `TableMeasure` (`_column_maxima`, `ui/__init__.py:1962-1971`) lets a column
   that only one of two rows has keep that row's width as it is, negative
   included. The oracle script asserts the two equal on every golden, and they
   are, since no golden cell is negative. The port follows `_column_widths`
   (T4). *Read, not run.*
2. **More fraction columns than the measure has scaled points.** `_shared`
   floors every share at 1 and takes the excess from the first of the widest
   columns, which can leave that column negative when the columns outnumber
   `available` (the docstring's "the widest column always has it to give"
   holds for any table a person would write). The port does the same and does
   not clamp. *Read, not run; no golden reaches it.*

### What the goldens do not hold

- `rigid` (FORMAT-tables.md §3.3 says so): reached only by numbers, in
  `a_table_with_nothing_to_give_is_rigid`.
- A tie in `_shared`'s rounding. Replacing half-to-even by half-up fails no
  golden record; `shares_round_half_to_even_and_the_last_column_borrows_from_the_widest`
  holds it (65537 sp in two equal shares is 32768.5 each and rounds to 32768).
  The expectation there is Python's `round`, stated from its definition; the
  reference was not run for it.

---
