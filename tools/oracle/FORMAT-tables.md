# Table and column goldens: `tests/golden/tables/*.golden`

Written by `tools/oracle/dump_tables.py` from the Python reference: a table's
column widths and its rows set to them, and a vertical list cut into columns.
Neither is in the port (D32.4 (ii)); these files exist so that they can be
ported after the reference's layout core is gone (D34, "the oracle is frozen
before the swap").

| step | reference |
|---|---|
| a table's cells | `advtext/markdown.py` `_Converter._table`, `_inline`, `_formula` |
| what a table wants | `advtext/ui/__init__.py` `_natural_widths`, `_longest_word`, `table_column_limits` |
| column widths | `_column_widths`: `_apportioned`, `_squeezed`, `_shared`; `SizedTable`, `with_columns` |
| rows | `_table_boxes`: `_table_row` (every cell fits), `_wrapped_row` + `_cell_lines` (one does not) |
| the table in a document | `_stacked` (the `_WrappedRow` arm), `compose_blocks` |
| cutting into columns | `advtext/columns.py` `split_into_columns`: `prune_top`, `vsplit`, `vert_break`, `_break_badness` |

Regenerate, from the repository root, with

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=build/advtext-reference \
        ../imgui-adv-text/.venv/bin/python -P -B tools/oracle/dump_tables.py

Two runs write identical bytes. Every file's header comments name the
reference's commit, the paths its working tree differed in, and the versions
of imgui_bundle, fontTools, markdown-it-py and Python it was dumped with.

`dump_tables.py --verify`, per case: (1) reads the INPUT records, rebuilds the
table (or the blocks) through the reference's constructors under a `Theme`
holding only the recorded values, and re-emits the INPUT; (2) reads the
EXPECTED records with a reader that calls nothing in `advtext.ui` or
`advtext.columns` — it re-derives the column widths by the arithmetic written
below, rebuilds each row and each column from its records, packs, ships, and
compares with the recorded box and commands; (3) runs the reference again and
compares every record.

**Intermediate values are the reference's own locals**, copied out while its
functions ran under `sys.settrace`; the script restates none of the arithmetic.
The arithmetic *is* restated here and in the `--verify` reader, which is what
checks that this document is sufficient.

The lexical layer, node, `spec`, `measure`, `packed`, draw-command records and
the `break … endbreak` group are [FORMAT.md](FORMAT.md)'s. `run`,
`space_width`, `has`, `kern` (metrics) and the enums are
[FORMAT-para.md](FORMAT-para.md)'s. The block INPUT records and the `compose`
group are [FORMAT-blocks.md](FORMAT-blocks.md)'s. Only what is new is here.

## 1. Enums added

| type | tokens |
|---|---|
| ColumnAlign | `left` `center` `right` |
| case kind | `table` `vsplit` `columns` |
| width policy | `apportioned` `widths` `fractions` |
| solve regime | `given` `fractions` `apportioned` `unscaled` `squeezed` `floors` `rigid` |
| row kind | `fits` `wrapped` |
| scan stop | `end` `eject` `overfull` |

## 2. Files

    golden 1 tables/<name>

| file | kind | what |
|---|---|---|
| `demo.golden` | `table` | the five tables of `demo/tables.md` and the one of `demo/sample.md`, each alone, 2–5 widths; one again at 13 px |
| `shapes.golden` | `table` | 1×1; header and one row; no header; no rows; ragged rows; empty cells, an empty table, the same with no padding; one alignment per column (2 themes); wrapped cells in each alignment; more alignments than columns; inline math, set and refused; 40 rows; a table in a list; 144 px per inch; no rule and no padding |
| `widths.golden` | `table` | one table at 12 widths from 1 px to 2000 px; the same at the exact edges between regimes ±1 sp; a table with nothing to give; caller's widths (ordinary, below the longest word, 1 sp, wider than the measure); caller's fractions (even, uneven, shares that round to zero) |
| `vertbreak.golden` | `vsplit` | hand-built vertical lists: uniform lines at 14 heights and settings; empty; one line; leading discardables; kerns and glue; a rule; a box taller than the column; a deep line with `split_max_depth`; penalties (eject, below eject, infinite, positive, negative); stretch and shrink with and without a penalty; infinite stretch; infinite shrink |
| `documents.golden` | `vsplit` | `demo/sample.md`, `demo/columns.md` (at three column widths) and `demo/tables.md` composed and cut, the list given by dimensions only |
| `columns.golden` | `columns` | four short markdown documents from block records to each column's commands |

## 3. The `table` case

    case <name:string> table
    source <kind> <text:string>
    theme <px_per_inch:float> <spacing:Spacing> <cell_padding> <table_rule_thickness>
    faces <prose:string> <math:string> <code:string>
    space_width ... / has ... / kern ... / measure ...
    table <ncolumns> <nrows> <has_header:bool> <left_indent> <space_above> <space_below> <font:string>
    spec baselineskip ... / spec lineskip ... / lineskiplimit <v>
    strut <height> <depth>
    aligns <n> <ColumnAlign>...
    policy apportioned | policy widths <n> <w>... | policy fractions <n> <share:float>...
    row <r> <ncells>                        nrows times
      cell <r> <c> <nevents>                ncells times
        run <font:string> <breakable:bool> <text:string>      FORMAT-para.md §3.2
        atom                                                  followed by one box record
        nodes material <n> ... endnodes     derived: TableCell.material
        nodes unshrinkable <n> ... endnodes derived: what a wrapped cell is broken from
      endcell
    endrow
    endtable
    limits ... endlimits                    §3.2
    layout <run-name:string> <width> ... endlayout      once per width, §3.3
    endcase

### 3.1 INPUT

`source` is not an input: `markdown` (the text is the markdown, for a reader),
`demo-file` (`<file>#<n>`: the n-th table of that file of the reference) or
`built` (the script wrote the cells).

- **Rows.** Row 0 is the header when `has_header` is 1. Layout treats the
  header as a row like any other; the flag decides only the rule (§3.3). Rows
  may have different numbers of cells; `ncolumns` is the longest. A missing
  cell is set as an empty one. A table may have no rows.
- **A cell** is its events in order. `run` is one `_append_text` call, as in a
  paragraph. `atom` is an inline formula: the box the reference's math builder
  returned, already wrapped by `shipout.atom_box` (an hbox holding `mathon 0`,
  the formula's box, `mathoff 0`); it enters the material as that one node and
  resets the space factor. A formula the builder refused arrives as an
  unbreakable `run` in the code face. Links are dropped.
- **`aligns`** is as the block carries it: it may be shorter than `ncolumns`
  (the rest are `left`) or longer (the rest are ignored).
- **`policy`**: `widths` are absolute scaled points, padding included, one per
  column, each ≥ 1. `fractions` are positive finite shares, one per column, as
  `with_columns` stored them (not normalised).
- **`theme`**: `cell_padding` goes left and right of every cell;
  `table_rule_thickness` is the header rule's height, 0 for none.
- `left_indent` is absolute. `space_above`/`space_below` take part in the gap
  rule of FORMAT-blocks.md §4.2 like any block's; a table is always a run of
  one block and a table in a quote is not barred.
- `font` is informational: runs carry theirs. `strut` is
  `Strut.filling(baselineskip.width)`.

### 3.2 EXPECTED, whatever the measure: `limits`

    limits
    natural <r> <ncells> <w>...           once per row
    widest <ncolumns> <w>...
    word <r> <c> <width>                  once per cell, row by row
      break <name> material ... endbreak  only when the cell has material
      wordlines <nlines> <w>...
    floor <ncolumns> <w>...
    endlimits

A table with no rows has an empty group.

- `natural[r][c]` = `hpack(material)` at natural width, plus `2 * cell_padding`.
- `widest[c]` = the maximum of `natural[r][c]` over the rows that have a cell
  `c`; 0 if none. This is `table_natural_widths`.
- `word` is `_longest_word`: 0 for a cell with no material. Otherwise the
  cell's `material` (**not** `unshrinkable`) is broken first-fit at a measure
  of one point (65536) with `left_skip` and `right_skip` zero and the other
  parameters of `PROSE_BREAKING` — the `break` group is that call — and
  `wordlines` is `hpack(line.contents).box.width` for each line: the natural
  width of the line's contents, not the 1 pt the line was set to. `width` is
  their maximum.
- `floor[c]` = the maximum over cells of `word + 2 * cell_padding`; 0 for a
  column with no cells. This is `table_column_limits(...).floor`.

### 3.3 EXPECTED: `layout`, one per width

    layout <run-name:string> <width>
    solve <regime> <available> <ncolumns> ... endsolve
    row <r> fits | row <r> wrapped ... endrow          once per row
    headrule                                           after row 0, when there is one
    box ... endbox
    stack <n> ... endstack
    extents 1 <top> <bottom>
    vbox <width> <height> <depth>
    origin <x> <y>
    cmds ... endcmds
    endlayout

A table with no rows has no `solve` and no rows: `stack 0`, an empty `vbox`.
A run name `ui-240px` is `sp_from_px(240, px_per_inch)`; other names are
widths chosen in scaled points.

#### The widths: `solve`

`available = max(width - left_indent, 65536)`. All divisions floor.

| regime | condition | records, in order | `columns` |
|---|---|---|---|
| `given` | policy `widths` | | the caller's, whatever the measure |
| `fractions` | policy `fractions` | `total` `rounded` `last` `borrowed` | below |
| `apportioned` | `sum(widest) <= available` | `widest` `total` `floored` | `floored`, the last column `+ available - sum(floored)` |
| `unscaled` | the same, and `sum(widest) <= 0` | `widest` `total` | `widest` (all zero) |
| `floors` | `sum(widest) > available`, `slack <= 0` | `widest` `narrowest` `slack` | `narrowest`: the table is wider than the measure |
| `rigid` | …, `slack > 0`, `sum(give) <= 0` | `widest` `narrowest` `slack` `give` `total` | `narrowest` |
| `squeezed` | …, `slack > 0`, `sum(give) > 0` | `widest` `narrowest` `slack` `give` `total` `floored` | `floored`, the last column `+ available - sum(floored)` |

    widest <n> <w>...        §3.2's
    total <v>                apportioned: sum(widest). squeezed: sum(give). fractions: sum(shares), a float
    floored <n> <w>...       apportioned: widest[c] * available // total
                             squeezed:    narrowest[c] + slack * give[c] // total
    narrowest <n> <w>...     §3.2's floor
    slack <v>                available - sum(narrowest)
    give <n> <w>...          widest[c] - narrowest[c]
    rounded <n> <w>...       max(1, round(available * share[c] / total))
    last <v>                 rounded[-1] + available - sum(rounded)
    borrowed <c>             -1, or the column the last one borrowed from
    columns <n> <w>...       the answer: `table_columns`
    endsolve

- `fractions`: `round` is Python's (half to even) of a double computed as
  `available * share / total` in that order, `total` the left-to-right double
  sum of the shares. When `last < 1`, the first column with the largest width
  (after `last` replaced the last) gives up `1 - last`, and the last column
  is 1.
- `apportioned` scales **up**: a table narrower than the measure always fills
  it. Nothing sets a table at its natural width but policy `widths`.
- `rigid` is in the reference and no case reaches it: from `_column_widths` it
  would need `sum(widest) > available >= sum(narrowest)` with
  `widest == narrowest`.
- The longest words are only measured when the table does not fit.

#### A row in which every cell fits

A row fits when `natural[r][c] <= columns[c]` for every cell it has.

    row <r> fits
    box hlist ... endbox                  the row before its strut
    strutted <width> <height> <depth> <shift>
    endrow

The box is `hpack`, at natural width, of `kern left_indent` and one box per
**column** (a missing cell is an empty one). Each is `hpack` *exactly* to
`columns[c]` of `kern cell_padding`, the material with the alignment's glue,
`kern cell_padding`. The glue is `ui.HSS` (`0 plus 1fil minus 1fil`): after
the material for `left`, before it for `right`, both for `center`.

`strutted` is the row with `height = max(height, strut.height)`,
`depth = max(depth, strut.depth)`: one strut on the row, none on a cell.

#### A row in which one cell does not

**Every** cell of the row is then built as a stack of lines, the ones that
fit included.

    row <r> wrapped
    cell <c> <column_width> <inner>          once per column
      break <name> unshrinkable ... endbreak
      lines <n>
        append <prev_depth_in> <prev_depth_out>       n times
        glue ...                                      the interline glue, from the second line
        dims <width> <height> <depth> <shift>
      stacked <width> <height> <depth>
      vtop <width> <height> <depth> <below>
    endcell
    rowbox <width> <height> <depth> <shift> <glue_set:float> <glue_sign> <glue_order>
    lowest <last_baseline>
    strutted <width> <height> <depth> <shift>
    resumed <resumed_depth>
    endrow

- `inner = max(column_width - 2 * cell_padding, 65536)`.
- The `break` group is the cell's `unshrinkable` list broken **first-fit** at
  a uniform `inner`, with `PROSE_BREAKING` and:

  | align | `left_skip` | `right_skip` | `par_fill_skip` |
  |---|---|---|---|
  | `left` | 0 | `0 plus 1fil` | unchanged |
  | `right` | `0 plus 1fil` | 0 | 0 |
  | `center` | `0 plus 1fil` | `0 plus 1fil` | 0 |

  A cell with no material is still one line.
- Each line of the break is given `shift = cell_padding` and the strut
  (`dims`), and appended with `append_to_vlist` under the table's interline
  triple, starting from `IGNORE_DEPTH` (`append`, as FORMAT-blocks.md §4.2).
- `stacked` is `vpack` of that list at natural height. `vtop` is the same box
  with `width = column_width`, `height = first` (the first line's strutted
  height), `depth = below + stacked.depth`, where
  `below = stacked.height - first`: the reference point moved from the last
  baseline to the first. Its width is the column's whatever its lines are.
- `rowbox` is `hpack`, at natural width, of `kern left_indent` and the
  `vtop`s. `lowest` is the largest `below`.
- `strutted` as above. `resumed = strutted.depth - lowest`: the depth of the
  row's lowest line, which the next interline glue is computed from.

#### The rule, the stack, the commands

`headrule` is present when the table has a header and
`table_rule_thickness` is not 0: after row 0, `hpack` of `kern left_indent`
and `rule <sum(columns)> <table_rule_thickness> 0`. It is not strutted. There
is no other rule and no cell border or fill.

    stack <n>
    append <prev_depth_in> <prev_depth_out> <prev_depth_carried>
    glue ...                                 when there is interline glue
    ref row <r> | ref headrule

The table's boxes as `_stacked` appends them, under the table's interline
triple, from `IGNORE_DEPTH`. `prev_depth_out` is what `append_to_vlist`
returned (the box's depth); `prev_depth_carried` is what the next append is
handed: the same, except after a wrapped row, where it is `resumed`. Without
that the row after a wrapped row would be set `lineskip` below it instead of
one leading below its last line.

`extents`, `vbox`, `origin`, `cmds` are FORMAT-blocks.md §4.3's for a document
holding this one table: `vpack` at natural height, shipped from
`(0, vbox.height)`. Reading order follows from the boxes: a row that fits is
one `line`; a wrapped row is one `line` per line of each cell, cell by cell.

## 4. The `vsplit` case

    case <name:string> vsplit
    source <kind> <text:string>
    vlist <n>
      v box <BoxKind> <width> <height> <depth> <shift>
      v rule <width> <height> <depth>
      v glue <width> <stretch> <order> <shrink> <order>
      v kern <width> <KernKind>
      v penalty <value>
    endvlist
    split <name:string> <height> <count> <split_max_depth> ... endsplit      one or more
    endcase

The INPUT is the vertical list by its dimensions. A `v box` stands for a box
with no contents: the splitter reads a box's height and depth and nothing
else, and the script asserts that the reference cuts the real list (boxes
full) at the same places with the same column boxes. `source demo-file` names
the document, theme and measure the list was composed at; it cannot be rebuilt
from this file and does not need to be.

### 4.1 `split`

`split_into_columns(material, height=, count=, split_top_skip=, split_max_depth=)`.

    split <name:string> <height> <count> <split_max_depth>
    spec split_top_skip ...
    column <k> <head:bool> <start> <place>           count times
      glue ...                                       the head glue, when head is 1
      scan <n>
        cand <index> <penalty> <cur_height> <prev_dp> <stretch0> <stretch1> <stretch2> <stretch3>
             <shrink> <badness> <cost> <taken:bool>                          (one line) n times
      place <place> <least_cost> <stop>
      packed <fitness> <natural> <excess> <badness> <overfull_by>
      colbox <width> <height> <depth> <shift> <glue_set:float> <glue_sign> <glue_order>
      origin <x> <y>
      cmds ... endcmds
    endcolumn
    overflow <head:bool> <start> <n>
    glue ...                                         when head is 1
    endsplit

**What a column is handed** (`prune_top`, §968–969). The list given to column
`k` is one new glue (when `head` is 1) followed by `material[start:]`.
`start` is the first box or rule not yet placed: every glue, kern and penalty
between the previous column's cut and `start` is discarded. The head glue is
`split_top_skip` with its width replaced by
`max(0, split_top_skip.width - height of material[start])`. With nothing left
(`head` 0, `start == n`) the list is empty. **The first column is pruned
too.** `start` is this script's bookkeeping, asserted against the reference's
lists; the reference keeps no index.

**The scan** (`vert_break`, §970–976). `index` counts in the list the column
was handed (the head glue is index 0); `index == its length` is the end of
the list. One `cand` per legal breakpoint reached, in order:

- a glue whose predecessor is a box or a rule (never a glue at index 0);
  penalty 0;
- a kern followed by a glue, whatever the kern's kind; penalty 0;
- a penalty node of value below 10000;
- the end of the list; penalty −10000.

`cur_height`, `prev_dp`, the four stretch totals (one per glue order) and
`shrink` are the running totals **before** the node at `index` is added:
a box or rule adds `prev_dp + height` and sets `prev_dp = depth`; a glue or a
kern adds `prev_dp + width` and sets `prev_dp = 0`, a glue also adding its
stretch (by order) and its shrink (**whatever its order**); then, if
`prev_dp > split_max_depth`, the excess moves into `cur_height`.

`badness` (§975): with `cur_height < height`, 0 if any infinite stretch total
is non-zero, else `badness(height - cur_height, stretch0)`; else 2³⁰−1
(`awful_bad`) if `cur_height - height > shrink`, else
`badness(cur_height - height, shrink)`.

`cost` (§974): `awful_bad` stays; else `penalty` if `penalty <= -10000`; else
`badness + penalty` if `badness < 10000`; else 100000 (`deplorable`).

`taken` is `cost <=` the least cost so far (starting at `awful_bad`): **ties
go to the later breakpoint**. The scan stops after a candidate whose cost is
`awful_bad` (`overfull`) or whose penalty is ≤ −10000 (`eject`, or `end` at
the end of the list). `place` is the last taken index — the length of the
list if none was — and `least_cost` its cost.

**The column** is `vpack(list[:place], height, exactly, split_max_depth)`:
`packed` and `colbox`. Every column is `height` tall whatever it holds.
`cmds` is `ship(column, Point(0, height))`: each column in its own
coordinates, top-left at the origin, as the reference's demo ships them
(`ui/app.py` `App.column_layout`). **Where columns go across the page is not
the reference's splitter's business**: it has no gutter and no x. The demo's
arithmetic, for information (`ui/app.py` `ColumnPage`):
`column_width = (measure - gutter * (count - 1)) // count`, column `i` at
`i * (column_width + gutter)`.

`overflow` is what `count` columns did not hold, pruned like a column's list:
`n` nodes, of which the head glue is one.

## 5. The `columns` case

    case <name:string> columns
    source markdown <text:string>
    theme ... faces ... metrics ... blocks <n> ... endblocks      FORMAT-blocks.md §3
    compose <run-name:string> <width> ... endcompose              FORMAT-blocks.md §4, once
    vlist <n> ... endvlist
    split ... endsplit                                            one or more, §4.1
    endcase

The material is the document's vertical list: the nodes `stack 0` of the
`compose` group describes, in order, which is `vbox.contents`. `vlist` lists
them by dimension so that the indices of §4.1 can be read without rebuilding
it; here the boxes are full and `cmds` are each column's glyphs, rules and
lines. `width` is the width of one column. The script asserts
`compose_into_columns` gives the same split.

## 6. Things a port must reproduce that no single record shows

- **No balancing.** Columns are filled in order and the last is as short as it
  turns out.
- **Composed documents have no vertical stretch and no penalties**, so in
  `documents.golden` and `columns.golden` every underfull candidate costs
  100000 and the cut is simply the last breakpoint that fits; a column that
  is not full packs `underfull` with badness 10000 and is not spread. Only
  `vertbreak.golden` reaches the other arms of the cost function.
- **A block boundary is not a breakpoint.** Between two blocks the list has a
  kern and no glue, so a heading and the line under it, or a list of one-line
  items, cannot be divided. A quote is one box. A table row with wrapped cells
  is one box. When such a thing is taller than the column it is placed alone
  and the column is overfull.
- **Nothing keeps a table's header with its rule or its first row.** The glue
  between them is a legal cut: `documents.golden` has columns that begin with
  the header rule (`sample-262pt` `six-of-250pt` column 4, `tables-md-400px`
  `eight-of-200px` column 2).
- `split_top_skip` is 10 pt and smaller than a body line, so the head glue is
  usually 0 and first baselines differ between columns by the height of
  their first lines.
- A row flips from `fits` to `wrapped` when one cell is 1 sp over, and the
  whole row changes shape with it (`widths.golden`, `prose-at-the-edges`).

## 7. Decisions taken in writing this oracle

- **A table is dumped alone**, as a document of one block. How it sits among
  other blocks is FORMAT-blocks.md's gap rule; nothing about a table changes
  it, and `_stacked` resets `prev_depth` at every block.
- **Cells are recorded, not markdown**; a formula as the box it became.
- **Expectations are computed from the rebuilt table under the cut-down
  Theme**, after asserting at every width that it composes to the same box as
  the reference's own table under its own Theme.
- **The big column inputs are dimensions only.** `demo/columns.md` in full at
  three widths would be about 2 MB of boxes the splitter never reads.
- **The fully wrapped row is not also written as one nested box**: its lines
  are in the `break` groups and its commands in `cmds`.
- `rigid` is named though unreachable, so that a later golden which did reach
  it would not need a new token.

## 8. What is not in here

- A table among other blocks; a table through `layout_units`/`UnitLayout`
  (which lays a long table out in pieces with `prev_depth_after`);
  `LayoutCache`.
- `TableMeasure` as an incremental object: the script asserts its `columns`
  equals `table_columns` on every layout and records nothing more.
- The refusals: `with_columns` and `SizedTable` on a wrong count, a width
  below 1, a fraction that is not a positive finite share; `_checked_widths`
  rounding a float width; `split_into_columns` on a count below 1 or a height
  not above 0; a character or a running rule in a vertical list.
- Links and formula spans inside cells; selection, copy and hit testing in a
  table, wrapped or not; a table among other blocks as far as selection goes;
  a table growing in a stream. Those are in `tests/golden/doc-tables`
  ([FORMAT-doc-tables.md](FORMAT-doc-tables.md)). The demo's column dragging
  is in neither.
- Hard breaks inside a cell, and breaking parameters other than
  `PROSE_BREAKING`.
- Column pages, gutters, the rule between columns (`ui/app.py`,
  `ui/pages/columns.py`): demo geometry over the split.
- Marks, inserts, a page builder: the reference has none.
