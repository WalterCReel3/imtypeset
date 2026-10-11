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
| `selection.golden` | `tdoc` | 9 documents, 15 layouts: every index as (block, row, column, byte), row and cell ranges and rectangles, separators, drags, probes, ranges with the copied string, find hits and links in cells |
| `stream.golden` | `tstream` | 9 feeds of a table in chunks: per step its cells, the widths held, the widths set, how many moved, its pieces, the units built |

Regenerate, from the repository root, with

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=build/advtext-reference \
        ../imgui-adv-text/.venv/bin/python -P -B tools/oracle/dump_doc_tables.py

(a few seconds; `--only selection` writes one file). `--check` dumps again in
memory and compares with the files byte for byte. It is **not** the
`--verify` of the other scripts: it does not re-read the records with a
reader written from this document. What stands in for that is asserted while
dumping (§6), and a failure there writes nothing.

**Cases are only ever appended.** A case already in a file keeps its record
lines byte for byte; a new question is a new case after the last one (§9 lists
them in file order). A change to the script that would alter an existing
record is a change to the format and gets a new `golden` version, not an edit.

**No test replays these files yet.** The replay belongs to the task that adds
the table block (`tables-core`), and it is the first reader of this document
that is not its writer.

The lexical layer and `cmds … endcmds` are [FORMAT.md](FORMAT.md)'s; `spec`,
`lineskiplimit`, `strut` are [FORMAT-para.md](FORMAT-para.md)'s; the `theme`,
`faces`, `decor`, `text`, `code`, `rule` blocks and everything not described
here are FORMAT-doc.md's with the same fields.

## 0. Whose answer each field is

Read this before holding a port to a field. The reference answers in
reading-order indices, rectangles and strings. It has no (block, row, column,
byte) and no table geometry call: those fields are **this script's
arithmetic over what the reference returned**. They are deterministic and
asserted where §6 says, and they are a convention this file defines, not a
behaviour the reference has. A port that chooses another convention for the
same reference answer is not wrong by these fields.

**The reference's own return values. Hold the port to these.**

| record | fields | from |
|---|---|---|
| `size`, `unit`, `bspan`, `lead`, `cmds` | all | FORMAT-doc.md §3.1–3.2: `UnitLayout`, `BareView.block_rect`, `UnitModel._leads`, the shipped commands |
| `tgeom` | the column widths | the cell boxes' `width`; asserted equal to `ui.table_columns` in a `tdoc` layout |
| `tcell` | its `rect` rows | `UnitModel.rects(Selection(start, end))` |
| `sep`, `atomtext`, `original` | `<index>` and `<text>` | `UnitModel.separators_within`, `atoms_within`, `originals_within` over the whole layout |
| `drag` | `<start> <end>` | `UnitModel.select_at(a, b, granularity)` |
| `probe` | `<hit>`, `<block_at>`, `<word_start> <word_end>`, `<line_start> <line_end>` | `UnitModel.hit`, `UnitLayout.block_at`, `word_at`, `line_at` |
| `range` | `<nblocks> <block>…` | `BareView.selected_blocks()` with that selection |
| `copy` | the string | `UnitModel.copy_text(Selection(start, end))` |
| `rects` + `rect` | all | `UnitModel.rects` |
| `blocktext` | the string | `BareView.block_text(block)` |
| `found` | `<nmatches>` | `FindIndex.find` |
| `match` | `<block> <row> <column> <material_start> <material_end>`, its `rect` rows | `TextMatch` (`block`, `cell`, `start`, `end`); `BareView.span_rects` |
| `unmapped_blocks`, `ink` (`<start> <end> <colour>`), `fill` | all named | `BareView._unmapped`, `BareView._decorate` |
| `linkarea` | `<block> <number> <target> <nruns> <nrects>`, its `rect` rows | `BareView._links` (`LinkArea`) |
| `lrun` | `<line> <start> <end>` | `LinkArea.runs` |
| `link`, `formula` (in a `cell`) | `<number>`, `<material_start> <material_end>`, target / display / refused / text | the cell's `links` and `formulas` |
| `step` | all | `StreamDocument` and the layout after `sync` |
| `tnatural` | all | `ui.table_natural_widths` |
| `tstated` | the widths | `StreamView._held[block]`, else `StreamView._kept[block]` (private state, read as it is) |
| `tset` | all | the cell boxes' `width` |
| `built` | all | `LayoutStats.built` |
| `final` | `<nblocks> <height> <length>` | the stream and its last layout |

**This script's arithmetic. A convention, or a convenience; not an oracle.**

| record | fields | how the script gets them |
|---|---|---|
| every `<pos>` (`probe`, `sep`, `atomtext`, `original`) | `<block> <row> <column> <byte>` | `TMap.pos(index)`: the block that owns **the item at that index**, the cell box that item is in, and that item's first byte in the cell's (or block's) text; `-1` for the byte of an item the text does not hold. See "A caret" below |
| every `<span>` (`drag`, `range`, `ink`, `lrun`) | eight fields | `TMap.span`: the pos of the first item in the range that has a byte, and of the last, with its END byte |
| `tmap`, `imap` | all | `spans.map_table` / `map_text_block` / `map_code_block` say which shipped index a material node became; the bytes are the script's reading of the cell's text (§1), the row and column are which cell box the index was counted into |
| `content`, `piece`, and the byte ranges of `link` and `formula` | text and bytes | the script's definition of a cell's text (§1) |
| `tgeom` | `<left>` | the x of the row box's `line` command plus the table's `left_indent` |
| `trow` | `<index_start> <index_end>` | running sums of `spans.index_count` over the cell boxes |
| `trow` | `<top> <height>` | the `line` command whose index range is the row's: `y − height`, `height + depth` |
| `trow` | `<wrapped>` | whether any cell box of the row is a vertical list |
| `tcell` | `<index_start> <index_end>` | the same running sums, per cell box |
| `match` | `<byte_start> <byte_end>` | the material range through the script's byte map |
| `linkarea` | `<row> <column>` | the cell box holding the first index of its first run |
| `ttable` | `<nrows>`, `<npieces>`, `<nwrapped>` | rows, units and wrapped rows counted off the laid-out boxes |
| `ttable` | `<mode>` | which of `_held`, `_kept` has an entry for the block |
| `ttable` | `<moved>`; `totals` | `tset` compared with the step before, by the script |
| `stream` `unit`, `final` | `<digest>` | FORMAT-doc.md §6's digest of the commands |
| the questions themselves | every `drag`, `probe`, `range` INPUT | chosen by the script (§3.3) |

**A caret.** `hit` answers a boundary: an integer `k` meaning "between item
`k − 1` and item `k`". `<pos>` after it is the position of **item `k`**, the one
*after* the boundary — a forward resolution, and the script's choice. It is
why the caret a click after the last letter of `Name` gets
(`small/16px-justify`, `probe 2842233 2585223 51 …`) reads `1 0 0 -1`: item 51
is the last item of the first cell (`tcell 1 0 0 47 52`) and the cell's text
does not hold it. The
reference has no caret convention for a table, and these files are **not an
oracle for one**: whether that caret is "cell (0, 0), byte 4" or anything else
is the port's decision (D54). What the port is held to in a `probe` is `hit`
and the four word and line boundaries.

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
are read off against it as FORMAT-doc.md §1 describes; what that reading
asserts is that each character node is the next character of the text that is
not white space and each glue stands on white space. The definition is the
script's (§0, §7).

**Every cell owns at least one index**, an empty one included: fill that
aligns it and has no byte. In these files an empty cell owns one or two
indices in a row that did not wrap and two or three in one that did (case
`wrapped-empty-cell`), and its `tcell` has no rectangle. A table's indices are its cells' indices in row-major
order and nothing else — the rule under the header and the padding kerns have
none.

What of this is asserted, exactly: every cell box counts at least one index;
the cell boxes' counts add up to the table's index range, so no index is
outside a cell or in two; no index of a cell with no material has a byte. Not
asserted: that the indices without a byte are fill — the script does not look
at what the item is.

## 2. A `tdoc` case

    case <name:string> tdoc
    markdown <math:bool> 0 <source:string>
    themein <body_px:float> <align>
    seed <n>
    links <styled:bool>                         BareView(style_links=…)
    dropheader                                  optional, no fields: see below
    needle <ignore_case:bool> <text:string>     zero or more
    width <run-name:string> <px:float>          one per layout
    expect
    … blocks … layouts …
    endcase

Driven as FORMAT-doc.md §2 says, with no block dropped.

`dropheader`: after the document is built from the markdown, the header row of
every table in it is removed (`dataclasses.replace(block, header=())`), before
anything is laid out or searched. Markdown cannot write a table without a
header; a host that builds its blocks can. The `table` record then says
`has_header` 0, **row 0 is the first body row**, and no rule is drawn. The
`blocks` section describes the table as laid out, so a replay that builds from
`table` / `row` / `cell` needs nothing more; one that parses the `markdown`
row must do the same removal.

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

The `lead` rows are **not here**: they come earlier in the layout, between
`bspan` and `cmds` (§3). After the geometry:

    seps <n> / sep <index> <pos> <text:string>
    atomtexts <n> / atomtext <index> <pos> <text:string>
    originals <n> / original <index> <pos> <text:string>

as FORMAT-doc.md §3.4 with four-field positions (`<index>` and `<text>` are
the reference's, `<pos>` the script's: §0). In a table
(`advtext.ui.units._separators_within`):

- **`\n` before the first index of every row but the first; `\t` before the
  first index of every cell but the first of its row.** An empty cell owns an
  index, so its tab has an index of its own.
- **A separator is written only between two pieces of selected text**
  (`advtext.selection`, `text`): one with no selected character before it in
  the selection, or none after it, is dropped. So an empty cell keeps its tab
  when there is text on both sides — `short\t\trow`; the whole table of case
  `small` copies `…lines\t\n\tempty first\tlast` — and loses it at an edge
  of the selection: the row `| vpack | stacks $x^2$ lines | |` selected alone
  copies `vpack\tstacks $x^2$ lines`, with no tab for its empty last cell,
  and `| | empty first | last |` selected alone copies `empty first\tlast`,
  two fields for a three-column row. This
  is the reference's behaviour and the `copy` rows hold it.
- The table's `lead` (FORMAT-doc.md §3.4's rule for any block) is at its
  first index: `\n\n`, and `\n` for a table inside a list item (case
  `in-a-list`, block 2). The `sep` at a table's first index is that lead; the
  first row has no `\n` of its own.
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

The calls are FORMAT-doc.md §3.5's. Which fields are the reference's answers
is §0: in a `probe`, `<hit>`, `<block_at>` and the word and line boundaries;
in a `drag`, `<start> <end>`; in a `range`, the blocks, `copy` and the
rectangles. `<pos>` and `<span>` are the script's reading of those indices.

The questions, in file order — the
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
newline-separated, with §3.2's rule at the two ends of the range.

Four things the reference does here that read like defects and are its
answers; each is in the records and a port is held to it:

- **A word runs across a cell boundary.** `word_at` just after `Name`
  (`small/16px-justify`, `probe 2842233 2585223 …`) answers `51 53`, and the
  second cell begins at 52: the last item of one cell and the first of the
  next are one "word".
- **A row that did not wrap highlights as one rectangle**, gaps between cells
  included (`range 47 190`, first `rect`: the full width of the row). A row
  that wrapped highlights per line of each cell.
- **A probe at the vertical middle of a wrapped row, in a column whose cell
  has no line at that height, lands in another cell** (`small/16px-justify`
  at 300 px, the four probes at `y = 5782215` with `x` from `-49336` to
  `2842233`: column 0 ends at `x = 2891569`, its cell has one line and that
  line ends above this y, and the hit is index 99, in column 1's cell).
- **A copy drops a tab at the edge of a selection** (§3.2).

## 4. Find and links in cells

    founds <n>
    found <needle:string> <ignore_case:bool> <nmatches>
    match <block> <row> <column> <material_start> <material_end> <byte_start> <byte_end> <nrects>
    rect <x> <y> <width> <height>

`FindIndex.of(blocks).find(needle, ignore_case=…)` for each `needle` row, each
match with `BareView.span_rects(block, start, end, cell=…)`. Row and column
are `-1` for a match in a text or code block; in a table the material and
byte ranges are in that cell's (the material range is the reference's, the
byte range the script's: §0). **Each cell is searched on its own: no match
runs from one cell into the next** (`hpack measures`, which a copy of the
table's second row contains with a tab between, is not found), and the text
inside a formula is not searched. Matches come block by block, a table's
cells in row-major order. An empty cell is not searched. Row 0 is the header
when there is one and the first body row when there is not (case `no-header`:
`Name`, which only the removed header held, finds nothing).

**`ignore_case` folds a whole cell or does not fold it at all**
(`FindIndex.of`, `find`). A cell — or a text block — is searched folded
(`str.casefold` of both sides) only if folding leaves its length in
characters unchanged. One that holds a sharp s does not, and is **searched as
it stands: needle and text exactly as written, case and all.** It is not
lowercased instead. Case `find-as-it-stands` is built to tell the two apart:

| needle, `ignore_case` | in `Straße und STRASSE` | in `straße` | in `strasse` |
|---|---|---|---|
| `STRA` | bytes 12–16 only: the `Stra` at 0 is not found | not found | found (the cell folds) |
| `stra` | not found | found at 0 | found |
| `straße` | not found (capital S) | found | found: the needle folds to `strasse` |
| `ß` | found, as itself | found | the `ss` at bytes 4–6 |
| `SS` | bytes 16–18, the capitals only | not found | the `ss` |

A lowercasing search finds `STRA` at byte 0 of the first and in the second;
the reference finds neither.

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
  changed, followed by exactly the changed cells: each `cell` group whose
  lines differ from the group written for the same (block, row, column) at
  the step before, or that had none. A step's table is therefore the last
  `stable` row for its block — for `ncolumns`, `nrows`, the header and the
  alignments — with, for each (row, column) below those counts, **the most
  recent `cell` group for it in any earlier step or this one**, not only
  those since the last `stable`.
- **`stable` has no cell count per row, and there is no `row` record in a
  stream.** None is needed in these files: the reference's converter gives
  every row of a table the header's number of cells, so every row has exactly
  `ncolumns` (asserted at every step of every case). A row that is short in
  the source arrives with empty cells, and their `cell` groups are written. A
  reader that meets a (row, column) below the counts with no `cell` group has
  found a defect in the file, not a ragged row.
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
  account for all of them. Every cell box owns at least one.
- No index of a cell with no material has a byte.
- Every row of a table in a stream has as many cells as the table has
  columns.
- `map_table`'s shipped indices for a cell lie in that cell's box.
- Every row of a table is set to the same widths; in a `tdoc` layout they are
  `ui.table_columns`'.
- A row box's width is the table's indent plus its columns.
- A colour span on a table changes nothing `_decorate` returns.
- FORMAT-doc.md §1's reading of each node's bytes, per cell.

Not asserted, and so only as good as the script: `trow`'s `top`, `height` and
`wrapped`; `tgeom`'s `left`, beyond the row box's width being the indent plus
the columns; that `<pos>` names the cell a reader would say a caret is in
(§0); `moved`, `npieces`, `nwrapped`, `mode` and `totals`.

The pin: every `advtext` module is refused at import unless it comes from the
pinned tree, and the modules loaded are checked once more after every group
has been dumped in memory and before the first file is written
(`pinned_reference.require_all_modules`).

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
- A table **in a quote**; hard breaks in a cell. A table whose rows are
  **ragged**: markdown never builds one (a short row is padded with empty
  cells, a long one cut to the header's count), so no document here has one;
  the layout of a hand-built ragged table is in
  `tests/golden/tables/shapes.golden`.
- A header-less table **in a stream** (`dropheader` is a `tdoc` input only).
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

## 9. The cases, in file order

`selection.golden`:

| case | layouts | what it is there for |
|---|---|---|
| `small/16px-justify` | 300, 700 px | a three-column table between two paragraphs: a row that wraps, a formula and a link in cells, an empty last cell, an empty first cell |
| `small/13px-left-unstyled` | 240 px | the same at another size, ragged right, links not styled |
| `edges` | 200, 520 px | a table first and last in the document, two tables in a row, a one-column table, a link that wraps inside a cell |
| `in-a-list` | 260, 600 px | a table in a list item (indent, `lead` of `\n`); a quote before a table; a sharp s and a replaced character in cells |
| `demo-sample` | 400 px | the reference's `demo/sample.md` |
| `demo-tables` | 360, 660 px | the reference's `demo/tables.md` |
| `no-header` | 240, 700 px | `dropheader`: a table with no header row — row 0 is a body row, no rule; a wrapped row with an empty last cell; an empty first cell |
| `wrapped-empty-cell` | 260, 420 px | rows that wrap holding an empty cell first, in the middle, last, and two at once; an unwrapped row with an empty middle cell |
| `find-as-it-stands` | 520 px | §4's `ignore_case` rule: needles `STRA`, `stra`, `STRASSE`, `straße`, `ß`, `SS` against cells and a paragraph with and without a sharp s |

The last three were added after the first six and after them in the file; the
first six are byte for byte what they were.

`stream.golden`: §5's table.
