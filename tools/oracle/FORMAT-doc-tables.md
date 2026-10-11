# Table-in-a-document goldens: `tests/golden/doc-tables/*.golden`

Written by `tools/oracle/dump_doc_tables.py` from the pinned Python reference
(`build/advtext-reference`, commit `32e3953`): what happens to a **table among
other blocks** — selection, hit testing and copy through its cells and across
its edges, find hits and links in cells, and a table that grows a row at a
time.

`tests/golden/tables` ([FORMAT-tables.md](FORMAT-tables.md)) holds how one
table is set, alone. `tests/golden/doc` ([FORMAT-doc.md](FORMAT-doc.md)) holds
selection, copy, decorations and streaming for documents **with their tables
dropped**. These files are what lies between, and re-record neither: a table's
layout appears here only as the commands of the document it is in.

| file | kind | what |
|---|---|---|
| `selection.golden` | `tdoc` | 6 documents, 10 layouts: every index as (block, row, column, byte), row and cell ranges and rectangles, separators, drags, probes, ranges with the copied string, find hits and links in cells |
| `stream.golden` | `tstream` | 9 feeds of a table in chunks: per step its cells, the widths held, the widths set, how many moved, its pieces, the units built |

Regenerate, from the repository root, with

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=build/advtext-reference \
        ../imgui-adv-text/.venv/bin/python -P -B tools/oracle/dump_doc_tables.py

(a few seconds; `--only selection` writes one file). `--check` dumps again in
memory and compares with the files byte for byte. It is **not** the
`--verify` of the other scripts: it does not re-read the records with a
reader written from this document. What stands in for that is asserted while
dumping (§6), and a failure there writes nothing.

**No test replays these files yet.** The product has no table block; the
replay belongs to the task that adds one (`tables-core`).

The lexical layer and `cmds … endcmds` are [FORMAT.md](FORMAT.md)'s; `spec`,
`lineskiplimit`, `strut` are [FORMAT-para.md](FORMAT-para.md)'s; the `theme`,
`faces`, `decor`, `text`, `code`, `rule` blocks and everything not described
here are FORMAT-doc.md's with the same fields.

## 1. Positions: (block, row, column, byte)

FORMAT-doc.md §1 gives every reading-order index a position *(block, byte
offset into the block's text)*. A table has no one text; its cells have one
each. So in these files:

- **pos** is four fields, `<block> <row> <column> <byte>`: row and column are
  `-1` outside a table; inside one the byte is an offset into **that cell's
  text**, `-1` for an item the text does not hold. `-1 -1 -1 -1` past the end.
- **span** is eight: the pos of the range's first sourced item, then the block,
  row and column of its last sourced item and that item's END byte
  (exclusive). Eight `-1` when the range holds no sourced item.

**Row 0 is the header when the table has one** (`map_table`'s and
`TextMatch.cell`'s convention); rows count on from it.

**A cell's text** is defined as a text block's is: the concatenation of what
the reference's converter was handed for that cell (each `_append_text` call
as given, a set formula as its source with dollars). The bytes of each node
are read off against it and asserted, as FORMAT-doc.md §1 describes.

**Every cell owns at least one index**, an empty one included: the fill glue
that aligns it. That index has byte `-1`. A table's indices are its cells'
indices in row-major order and nothing else — the rule under the header and
the padding kerns have none. The script asserts both.

## 2. A `tdoc` case

    case <name:string> tdoc
    markdown <math:bool> 0 <source:string>
    themein <body_px:float> <align>
    seed <n>
    links <styled:bool>                         BareView(style_links=…)
    needle <ignore_case:bool> <text:string>     zero or more
    width <run-name:string> <px:float>          one per layout
    expect
    … blocks … layouts …
    endcase

Driven as FORMAT-doc.md §2 says, with no block dropped.

### 2.1 `blocks`

FORMAT-doc.md §2.1's, with one line more in the header and one more block:

    theme … / faces … / decor <table_rule_thickness>
    tabledecor <cell_padding>
    blocks <n>
      text … endtext | code … endcode | rule … endrule          FORMAT-doc.md §2.1
      table <i> <ncolumns> <nrows> <has_header:bool> <font:string> <left_indent>
            <space_above> <space_below>
      spec baselineskip … / spec lineskip … / lineskiplimit <v> / strut <h> <d>
      nest <list_depth>
      aligns <n> <ColumnAlign>{n}
      row <r> <ncells>                           nrows times
        cell <i> <r> <c> <header:bool> <nlinks> <nformulas>     ncells times
        content <nbytes> <nmaterial> <text:string>
        piece run|formula …                      FORMAT-doc.md §2.1, bytes in this cell's text
        link <number> <byte_start> <byte_end> <material_start> <material_end> <target:string>
        formula <number> <byte_start> <byte_end> <material_start> <material_end> <display:bool> <refused:bool> <text:string>
        endcell
    endtable
    endblocks

- `ncolumns` is the longest row; `aligns` is as the block carries it
  (FORMAT-tables.md §3.1). `left_indent` is absolute: a table in a list item
  carries the list's.
- `link`'s `number` counts within the **cell**. `linkarea` (§4) numbers a link
  within its **block**: cells in row-major order.
- No metrics and no node lists: `tests/golden/tables` holds a table's
  composition, with the node lists and font tables a replay of it needs.

## 3. A layout: `layout <run-name:string> <width> … endlayout`

In order: `size`, `unit`, `bspan`, `lead`, `cmds … endcmds` (FORMAT-doc.md
§3.1–3.2, a table being one unit of one block), then

    imap <block> <index_start> <n> (<byte> <length>){n}                    a block that is not a table
    tmap <block> <index_start> <n> (<row> <column> <byte> <length>){n}     a table
    unmapped <n> <block>{n}

`tmap`: for each index the table owns, the cell it is in and its bytes in that
cell's text (`-1 0` for the fill glue and anything else the text does not
hold). Built from `advtext.spans.map_table` for the bytes and from the cell
boxes for the cell; the script asserts the two agree.

### 3.1 Geometry: `tgeom`, `trow`, `tcell`

    tables <n>
    tgeom <block> <left> <nrows> <ncolumns> <width>{ncolumns}
    trow <block> <row> <index_start> <index_end> <wrapped:bool> <top> <height> <ncells>
    tcell <block> <row> <column> <index_start> <index_end> <nrects>
    rect <x> <y> <width> <height>

- `tgeom`: `left` is where the first column begins (the measure's left edge
  plus the table's `left_indent`); the widths are the cell boxes', padding
  included. Asserted equal to `ui.table_columns` at this width. Column edge
  `k` is `left + sum(width[:k])`.
- `trow`: the row's index range and its extent in document coordinates, read
  from the `line` command the row box ships (`top = y − height`, `height =
  height + depth`). `wrapped` is whether the row was built as wrapped cells
  (FORMAT-tables.md §3.3). Rows do not tile the table: the interline glue and
  the rule under the header lie between them and belong to no row.
- `tcell`: the cell's index range (a row has a cell box per **column**, so a
  ragged row's missing cells are here, each with its one index) and
  `UnitModel.rects(Selection(start, end))`: the highlight of the whole cell,
  one rectangle per line of it, none for a cell with no text.

### 3.2 What a copy writes

`lead`, then

    seps <n> / sep <index> <pos> <text:string>
    atomtexts <n> / atomtext <index> <pos> <text:string>
    originals <n> / original <index> <pos> <text:string>

as FORMAT-doc.md §3.4 with four-field positions. In a table
(`advtext.ui.units._separators_within`):

- **`\n` before the first index of every row but the first; `\t` before the
  first index of every cell but the first of its row.** Separators at one
  index are concatenated, and an empty cell still owns an index, so an empty
  cell keeps its tab: `a\t\tc`.
- The table's own `lead` is at its first index, by FORMAT-doc.md §3.4's rule
  for any block: `\n\n`, and `\n` for a table inside a list item (case
  `in-a-list`, block 2).
- A wrapped line of a cell joins with a space, like a wrapped line of a
  paragraph.
- A copy is **row-major whatever was dragged**: a selection is one index
  range, so a drag down a column selects everything between the two cells in
  reading order (the `drag` rows from a cell to the cell below it show this).
- A formula in a cell copies as its source (`atomtext`); a replaced character
  as what was written (`original`).

### 3.3 Questions: `drag`, `probe`, `range`

    drags <n>
    drag <ax> <ay> <bx> <by> <granularity> <start> <end> <span>
    probes <n>
    probe <x> <y> <hit> <block_at> <pos> <word_start> <word_end> <line_start> <line_end>
    ranges <n>
    range <start> <end> <span> <nblocks> <block>{nblocks}
    copy <text:string>
    rects <n> / rect <x> <y> <width> <height>
    blocktexts <n> / blocktext <block> <text:string>

The calls are FORMAT-doc.md §3.5's. The questions, in file order — the
table's (thinned evenly to 150 drags, 220 probes, 220 ranges), then
`dump_doc_extras.choose_queries`' for the whole document (thinned to 60, 40,
60), which is where a drag from the paragraph above into the table and the
ranges across three blocks come from:

- **ranges**, per table: the whole table; from three indices before it to the
  middle of its first cell; from the middle of its last cell to three past
  it; from three before to three after; the one index before it and the one
  after; each cell whole, and the empty range at its start; for each two
  cells consecutive in reading order (the last of a row and the first of the
  next included) middle to middle, one index either side of the boundary,
  start to start and end to end; each row whole; for each two consecutive
  rows middle to middle, and for each column but the first the middle of a
  cell to the middle of the cell below it.
- **drags**, at `character`, `word` and `line`: from the centre of a cell's
  first rectangle to the centre of the next cell's and back; to the centre of
  the last rectangle of the cell below and back; from the first cell to the
  last.
- **probes**: the centre of each cell's first and last rectangle; for each
  row, at its vertical middle, each column edge and one pixel (at 96 px per
  inch) either side of it, and a thousand pixels left and right of the table;
  and at the horizontal middle of the first and of the last column, `top − 1`,
  `top`, `bottom − 1` and `bottom` of the row — the first and last of those
  are in the gap between rows, or on the rule.

`blocktext` of a table is the copy of its own range: cells tab-separated, rows
newline-separated.

## 4. Find and links in cells

    founds <n>
    found <needle:string> <ignore_case:bool> <nmatches>
    match <block> <row> <column> <material_start> <material_end> <byte_start> <byte_end> <nrects>
    rect <x> <y> <width> <height>

`FindIndex.of(blocks).find(needle, ignore_case=…)` for each `needle` row, each
match with `BareView.span_rects(block, start, end, cell=…)`. Row and column
are `-1` for a match in a text or code block; in a table the material and
byte ranges are in that cell's. **Each cell is searched on its own: no match
runs from one cell into the next** (`hpack measures`, which a copy of the
table's second row contains with a tab between, is not found), and the text
inside a formula is not searched. Matches come block by block, a table's
cells in row-major order.

    unmapped_blocks <n>
    decoinks <n> / ink <start> <end> <colour> <span>
    fills <n> / fill <x> <y> <width> <height> <colour>
    linkareas <n>
    linkarea <block> <number> <row> <column> <target:string> <nruns> <nrects>
    lrun <line> <start> <end> <span>
    rect <x> <y> <width> <height>

`BareView._decorate` over the whole document with `Palette()` and no host
colour spans (FORMAT-doc.md §4 has the rules): the link ink ranges and
underlines when links are styled, none when not; and `BareView._links`, a
link in a cell included — `number` counts the block's links, cells in
row-major order, and a link that wraps in its cell is one area with a
rectangle per line.

**The reference cannot mark a find hit in a cell.** A colour span set on a
table (`BareView.set_colours`) is kept and not applied: a `ColourSpan` has no
coordinates across cells. The script asserts it — setting one on every table
changes neither `decoinks` nor `fills`. A host of the reference outlines one
match from `span_rects`' rectangles, which is what `match`'s `rect` rows are.
Marks drawn through cells are therefore a requirement with no oracle.

## 5. A `tstream` case

    case <name:string> tstream
    themein <body_px:float> <align>
    policy <keep_streamed|reset>
    width <run-name:string> <px:float>
    chunk <text:string>                          in order; their concatenation is the source
    expect
    step … endstep                               one per chunk, and one for the close
    final <nblocks> <height> <length> <digest>
    totals <steps> <built> <moved>
    cmds … endcmds                               the final layout's
    endcase

Driven as FORMAT-doc.md §6: a `StreamDocument` with math on, a `StreamView`,
per chunk `append`, `sync`, `layout_for(width)`; then `close` and once more.

    step <number> <append|close> <revision> <ncommitted> <nlive> <nunits> <height> <length>
    sblock …                                     FORMAT-doc.md §6, a block that is not a table and is new
    stable <block> <committed|live> <ncolumns> <nrows> <has_header:bool> <left_indent> <naligns> <ColumnAlign>{naligns}
    cell … endcell                               §2.1's, each cell whose rows differ from the step before
    ttable <block> <committed|live> <nrows> <npieces> <held|kept|plain> <moved> <nwrapped>
    tnatural <block> <n> <width>{n}
    tstated <block> <n> <width>{n}
    tset <block> <n> <width>{n}
    built <n> <unit>{n}
    unit <position> <block_start> <nblocks> <part> <y> <height> <base> <count> <digest>
    endstep

- `stable` is written for a table that is a new object or has a cell that
  changed, followed by exactly the changed cells. A step's table is the last
  `stable` row for its block with every `cell` group since.
- `ttable`, `tnatural`, `tstated`, `tset` are written every step for every
  table laid out:
  - `tnatural`: `ui.table_natural_widths` of the table as it is now.
  - `mode` and `tstated`: `held` — the table is live and `tstated` is
    `StreamView._held`, **each column's widest natural width over every
    layout so far**; `kept` — it committed under `keep_streamed` and
    `tstated` is `StreamView._kept`, the wider of what it was held at and its
    natural widths at the commit; `plain` — neither (`reset` after the
    commit), nothing stated.
  - `tset`: the widths the table was **set to**, read off its cell boxes. It
    is `tstated` (a column of zero raised to one scaled point) **when that
    sums to no more than the measure less the table's indent**; otherwise the
    default apportioning of FORMAT-tables.md §3.3 at this width, which
    narrows and wraps. So a held table does not fill the measure, and a
    column moves only when its own widest cell grows or the table stops
    fitting.
  - `moved`: how many columns of `tset` differ from the step before (`-1` the
    first time, or when the column count changed). `totals`' third field is
    the sum of the non-negative ones.
  - `npieces`: how many units set the table. A `StreamView` lays a table of
    more than `TABLE_PIECE_ROWS` = 4 rows out in pieces of 4 rows (the header
    is a row of the first piece), each at the whole table's widths. `nwrapped`
    is how many rows were built as wrapped cells.
- `built`: the positions in `placed` of the units the reference composed this
  step (`LayoutStats.built`). A row added to a long table whose widths did
  not move builds the last piece only; a width that moved builds every piece.
- `unit` rows are written for positions whose row changed, with FORMAT-doc.md
  §6's digest of the unit's commands.

| case | what |
|---|---|
| `growing/by-row` (400 px), `-reset`, `-wide` (900 px), `-narrow` (220 px) | a paragraph, a ten-row table a line at a time, a paragraph: columns growing, the table ceasing to fit, a row that wraps, the commit under each policy |
| `growing/9-chars` | the same nine characters at a time: cells arriving mid-row and markup in a cell open across chunks |
| `growing/table-and-after-in-one`, `growing/first-row-then-rest-with-after`, `growing/one-chunk` | the table committing in the sync it arrived in: never seen live, or seen with one row only |
| `demo-tables/by-line` (660 px) | the reference's `demo/tables.md` a line at a time |

## 6. What the script asserts

- Every index a table owns is in exactly one cell box, and the cell boxes
  account for all of them.
- `map_table`'s shipped indices for a cell lie in that cell's box.
- Every row of a table is set to the same widths; in a `tdoc` layout they are
  `ui.table_columns`'.
- A row box's width is the table's indent plus its columns.
- A colour span on a table changes nothing `_decorate` returns.
- FORMAT-doc.md §1's reading of each node's bytes, per cell.

## 7. Decisions taken in writing this oracle

- **A position in a table is (block, row, column, byte in the cell's text)**,
  header row 0. The reference has no such thing — it has reading-order
  indices, and material indices per cell — and the product's API will need
  one; this is the script's definition, in the shape
  `docs/advtext-migration.md` R6 and the other session's RQ-T8 ask for.
- **A cell's text is defined here**, as a block's is in FORMAT-doc.md §7.
- **Queries are INPUT in spirit but are not re-read**: they are chosen from
  the layout by the script and a port replays the rows as written.

## 8. What is not in here

- **Selection, hit testing and copy across COLUMNS of a multi-column
  layout.** The reference has none: `advtext.columns` returns boxes, the
  demo's Columns page draws them, and no selection model is ever built over
  them. `tests/golden/tables/columns.golden` has each column's commands and
  `tests/golden/selection` the model over a command list; nothing in the
  reference composes the two.
- **Marks through cells** (§4), and colour spans on a table generally.
- A table **in a quote**; a table whose rows are ragged in a document (the
  layout of one is in `tests/golden/tables/shapes.golden`); hard breaks in a
  cell.
- A table with **stated widths or shares** (`SizedTable`, `with_columns`) in a
  document, and the demo's column dragging: the widths are in
  `tests/golden/tables/widths.golden`; selection over such a table is the same
  code path as here with other widths.
- A long table in pieces in a `tdoc` layout (`BareView` does not cut; only
  `StreamView` does), and selection, find or links in a table while it is
  live. `carry_selection` across a relayout (FORMAT-doc.md §8).
- The draw list: what `draw_runs` emits for a table is `cmds` drawn like any
  other block's, and FORMAT-doc.md §4's `drawn` section is not repeated.
- Culling a table by a viewport; a hosted block beside a table.
