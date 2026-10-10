# oracle-tables — result

## What exists

- `tools/oracle/dump_tables.py` — imports `dump_blocks.py`, `dump_para.py`,
  `dump_core.py`; edits none.
- `tools/oracle/FORMAT-tables.md` — the record vocabulary.
- `tests/golden/tables/*.golden` — 6 files, 64 cases, 4,108,898 bytes.

| file | kind | cases | bytes |
|---|---|---|---|
| demo | table | 7 | 1,374,434 |
| shapes | table | 18 | 996,894 |
| widths | table | 11 | 1,059,152 |
| vertbreak | vsplit (hand-built lists) | 19 | 111,170 |
| documents | vsplit (demo documents, dimensions only) | 5 | 173,530 |
| columns | columns (block records to commands) | 4 | 393,718 |

Tables: 96 `layout` groups; regimes reached: apportioned 26, squeezed 29,
floors 26, fractions 9, given 5, unscaled 1; rows: 235 fit, 204 wrapped.
Columns: 247 scans; stops: overfull 179, end 65, eject 3.

Headers carry: reference commit `a3c589a1ea150182cc7d7deefa0eccff680a5b7b`;
its working tree dirty in 4 paths, none under `advtext/` (listed in each
file); imgui_bundle 1.92.900, fontTools 4.66.0, markdown-it-py 4.2.0,
Python 3.11.2.

INPUT: a table as rows of cells, each cell its runs (and a formula as the box
it became), alignments, width policy, the two Theme values read
(`cell_padding`, `table_rule_thickness`), the interline triple and strut,
every metrics answer. A column split as a vertical list by dimensions, the
height, the count, `split_top_skip`, `split_max_depth`.

EXPECTED, per stage: per-cell natural widths; each cell's longest-word break
and line widths; column maxima and floors; the solve with its intermediates
(`slack`, `give`, `total`, the widths before the last column takes the
remainder, the rounded shares, which column was borrowed from); each row
(fits: the nested box; wrapped: per cell the break, each line's append and
strutted dims, the stacked box, the vtop, then the row box, lowest baseline,
resumed depth); the header rule; the stack with prev_depth in / out /
carried; vbox; commands. Columns: per column the pruned head, every legal
breakpoint with running height, depth, stretch totals, shrink, badness, cost
and whether it became the best, the place, the stop reason, the packed column,
its commands; the overflow.

Intermediates are the reference's own locals, copied out under
`sys.settrace` (`Tracer` in the script). Nothing in the reference is patched
and none of its arithmetic is restated in the dump path.

## What was run

- `dump_tables.py` three times; sha256 of all six files identical between runs.
- `dump_tables.py --verify`: 64 cases, 262 replayed checks ok (3.7 s).
- 30 single-field mutations across five files (a floored width, a column, a
  slack, a rounded share, a natural, a word, a floor, resumed, lowest, vtop,
  stacked, strutted, dims, extents, give, a cand's height / badness / cost, a
  place, a least cost, a column's start and place, colbox, packed, a vlist
  box, overflow): `--verify` failed on all 30. Files restored and re-hashed.
- Asserted at dump time: the rebuilt table composes to the reference's own
  table's box at every width; `table_columns` and `TableMeasure().columns`
  equal the solved widths on every layout; `table_natural_widths` equals the
  limits; the full-box list splits where the dimensions-only list does;
  `compose_into_columns` equals composing and splitting.
- Not run: any C++ build or test. No C++ reader for these records exists.
- Nothing written to `../imgui-adv-text`; its `git status` is as it was.

**One thing I did that I did not intend.** A scratch check script of mine sat
directly in `$TMPDIR`, where some other process had left a file named
`bisect.py`. Python put the script's directory first on its path, so
`import bisect` (from `advtext.selection`) ran that file's top level. It
printed eight lines about browser preferences (two of them `TimeoutError`)
and then failed the import. I do not know what it launched. I moved my
scratch files into a fresh `mktemp -d` directory and re-ran; no project file
was involved and no golden was produced in that run.

## How the reference solves column widths

`ui/__init__.py` `_column_widths` (2000–2054). `available =
max(width - left_indent, 1pt)`.

1. Caller's widths (`SizedTable`): used as given, whatever the measure.
   Caller's fractions: `max(1, round(available*share/total))` each (float,
   half-even), last column takes the remainder; if that leaves it under 1 the
   widest column pays (`_shared`).
2. Else each column's `widest` = its widest cell `hpack`ed on one line + 2 ×
   padding. If `sum(widest) <= available`: scaled **up** to fill,
   `widest*available//total`, last column takes the remainder.
3. Else each column's `narrowest` = its longest unbreakable stretch (the cell
   broken first-fit at a 1 pt measure, widest line) + 2 × padding;
   `slack = available - sum(narrowest)`. `slack <= 0`: the floors, table wider
   than the measure. Else `narrowest + slack*(widest-narrowest)//sum(widest-narrowest)`,
   last column takes the remainder.

Then per row: all cells' natural widths ≤ their columns → one hbox of cells
packed exactly with fil glue for alignment. Otherwise every cell of the row is
broken first-fit, ragged, to `column - 2×padding`, stacked, and re-referenced
to its first baseline; the next row's interline glue is computed from the
depth of the row's lowest line, not the row's depth.

## How the reference splits columns

`columns.py`: TeX's `\vsplit`, repeated. Prune leading glue/kern/penalty and
put `split_top_skip − first box height` (≥ 0) there; scan the list
accumulating height; at each legal breakpoint (glue after a box or rule, a
kern before glue, a penalty < 10000, the end) cost = `awful_bad` if overfull
past the shrink, the penalty if ≤ −10000, badness + penalty if badness <
10000, else 100000; keep the last candidate with cost ≤ the least so far; stop
at the first overfull one or a forced one; `vpack` exactly to the height.
**No balancing, no gutter, no x.** On composed documents there is no stretch
and no penalty, so it reduces to "the last breakpoint that fits".

## Prototype shortcuts noticed (file:line in `../imgui-adv-text/advtext`)

Each is held by a golden as the reference does it. Candidates for D32.3 /
design decisions when (ii) is ported.

Tables (`ui/__init__.py`):

1. `:2045–2046` — a table that fits is always scaled **up** to the full
   measure; there is no natural-width table except through `with_columns`.
   A 1×1 table of one word is as wide as the pane.
2. `:2183–2187`, `:2277–2286` — fits/wrapped is decided per row, and one cell
   1 sp over rebuilds the **whole row** as stacks of lines, so the row's box
   shape, its `line` commands and its reading order all flip at once
   (`widths` `prose-at-the-edges`, `natural-minus-1sp`). Rows of one table
   have two different structures.
3. `:2193–2206` — exactly one rule, under the header, only if there is a
   header; found by `row is rows[0]`. No cell borders, no row rules, no fills,
   no row padding: rows are one leading apart.
4. `:2037` — caller's widths ignore the measure; the document box grows with
   them and nothing reports it (`widths` `given-widths-wider-than-the-measure`).
5. `:2165–2168`, `:2418` — a column narrower than its longest word: the word
   runs into the next column. `_cell_lines` takes `broken.boxes` and drops the
   overfull report (`given-widths-below-the-longest-word`).
6. `:2411` — `inner = max(width - 2*padding, 1pt)`; `:2428` forces the cell
   box to the column width whatever its lines measure. A column narrower than
   its own padding sets every word on an overfull 1 pt line inside a box that
   claims the column's width (`given-widths-of-one-unit`).
7. `:2034` — `available` floored at 1 pt: a table indented past the measure
   is set at its floors, overfull (`shapes` `table-in-a-list` at 20 px).
8. `:2096–2100` vs `:2412–2414` — the floor is measured on `material`
   (shrinkable glue, skips zero) and the cell is broken from
   `_unshrinkable(material)`: two different lists. At a 1 pt measure it has
   not mattered in any case dumped; it is not the same call.
9. `:2098` — "a width nothing fits in" is 1 pt. Two stretches that together
   are under 1 pt would share a line and the floor would be their sum. Not
   reached at 13–16 px; reachable at very small sizes.
10. `:2127–2128` — `_squeezed`'s `total <= 0` return cannot be reached from
    `_column_widths` (it needs `sum(widest) > available >= sum(narrowest)`
    with `widest == narrowest`). No golden has the `rigid` regime.
11. `:2073` — a float in layout: `round(available * share / total)`,
    half-to-even on a double. The port must keep the operation order (D16).
12. `:2129–2130`, `:2230–2231`, `:2074` — the last column takes the whole
    rounding remainder (up to `columns − 1` sp). Deliberate per the docstring.
13. `:2179` — missing alignments default to left; extra ones are ignored
    silently. `:2248` — a missing cell is an empty one.
14. `:1162–1164`, `:2314–2362` — the `\prevdepth` fix for a wrapped row is an
    `isinstance` test in `_stacked`, and `ui/units.py` has to repeat it
    through `prev_depth_after` (`:1974`). The strut is applied to the row
    after the fact and `resumed_depth` depends on that order (`:2192`, `:2362`).
15. `:2406–2409` — an empty cell in a wrapped row is still broken: one line
    holding only the paragraph-end glue, and one `line` command.
16. `:1953–1958` — every relayout of a table that does not fit costs one line
    break per cell for the floors, plus one per cell of each wrapped row;
    `TableMeasure` exists only to dodge the first.
17. `markdown.py:1357–1362` — `TableCell.header` is set and nothing in layout
    reads it: header cells are in the body face, not bold.
18. `markdown.py:1344–1352` — alignments are read from header cells only.
19. `ui/__init__.py:1187` (already in oracle-blocks' list) — a table in a
    quote is not barred.

Columns (`columns.py`):

20. `:47–60` — no balancing; the last column is short.
21. `:62–69`, `:314` — composed material has no vertical stretch, so every
    underfull candidate costs 100000 and the `<=` tie-break is the whole
    algorithm. In `documents` and `columns` every scan reads 100000 until it
    overflows. Columns are packed `exactly` and report `underfull`, badness
    10000, unspread.
22. `:75–80` with `ui/__init__.py:1142–1146` — a block boundary is a kern
    with no glue after it: not a breakpoint. Headings stay with their text by
    accident of that, and lists of one-line items cannot be divided.
23. `:81–83` — a quote is one box; taller than the column it overflows it
    (`columns` `a-quote-taller-than-the-column`: packed `overfull`).
    The same holds for a wrapped table row.
24. `:84–86` — no vertical penalties anywhere: **no widow or orphan control**.
    A column may end after a paragraph's first line. Only `vertbreak` reaches
    §974's penalty arms.
25. Not written anywhere in the reference: the glue between a table's header,
    its rule and its first row is a legal cut. Columns that **begin with the
    header rule**: `documents` `sample-262pt` `six-of-250pt` column 4,
    `tables-md-400px` `eight-of-200px` column 2.
26. `:183`, `:467–480` — `split_top_skip` is a constant 10 pt, under the
    12.2 pt body line, so head glue is 0 and first baselines differ by line
    height. `:456–465` — `\topskip` and `\splittopskip` are one parameter, and
    the first column is pruned (§977 does not).
27. `:325–328` — infinite shrink is added as if finite, silently
    (`vertbreak` `infinite-shrink-is-counted-as-finite`).
28. `:115–118`, `ui/app.py:545–566` — gutter and column x are the demo's.
    `ui/app.py:677–694` — the page count is an estimate plus a spare page,
    capped at 24.
29. `:53–56`, `:88–95` — the docstring's measurements are stale:
    `demo/sample.md` at 262 pt is 253 nodes now, not 193; three 527 pt columns
    fill to 524.4, 512.9, 514.0 pt and **108 nodes overflow**, where the text
    says the document fits in three.

## Not covered

- A table among other blocks; `layout_units`' piecewise table layout;
  `LayoutCache`; `TableMeasure` beyond the equality assert.
- Refusals (`with_columns`, `SizedTable`, `split_into_columns`, a CharNode or
  running rule in a vlist); float widths being rounded.
- Links and formula spans in cells; selection, copy and hit testing in a
  wrapped table.
- Hard breaks in a cell; breaking parameters other than `PROSE_BREAKING`.
- Column pages, gutters, rules between columns.
- `demo/columns.md` and `demo/tables.md` as full boxes through the splitter:
  dimensions only (the four `columns` cases carry full boxes).
- `Faces.mono()`, non-default `Spacing`.

## Least sure of

- 4.1 MB against "a few MB". `widths` `prose-swept` (12 widths) is the
  cheapest thing to thin.
- `--verify` (2) restates the solver and the cost rule from FORMAT-tables.md;
  it does not re-break cells or re-run `vert_break`'s accumulation — it checks
  the recorded running heights only through (3), the re-emission.
- `tools/oracle/manifest.py` (another task's) was not touched: if it lists
  golden hashes, `tests/golden/tables` is not in it.
- `dump_tables.py` is longer on disk than I wrote it (2,146 lines): something
  reformatted it on save. It runs and verifies as it stands; I did not diff it.
