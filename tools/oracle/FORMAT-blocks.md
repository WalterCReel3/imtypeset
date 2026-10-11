# Block goldens: `tests/golden/blocks/*.golden`

Written by `tools/oracle/dump_blocks.py` from the Python reference: several
blocks — paragraphs, headings, list items with their markers, code blocks,
quotes with their bar, thematic rules — stacked into one document box. In the
reference that is `advtext/ui/__init__.py`:

| step | reference |
|---|---|
| one block → the boxes it gives the stack | `_block_boxes`: `_text_boxes` + `Strut.applied_to` (text), `hpack` + shift + strut (code), `_rule_box` (rule) |
| which blocks stack together | `_runs`, `_quote_depth`, `block_runs` |
| the vertical list | `_stacked`, with `pack.append_to_vlist` |
| a quote's bar | `_barred` |
| the document box | `compose_blocks` (`vpack` at natural height) |
| units and ink roles | `RunComposer`, `ui/units.py` `ship_unit`, `_spans`, `moved`; `ui/bare_view.py` `block_kind` |

`advtext/blocklayout.py` is **not** this layer: it is a row/column UI demo
over `hpack`/`vpack` and nothing in document composition calls it.

Regenerate, from the repository root, with

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=build/advtext-reference \
        ../imgui-adv-text/.venv/bin/python -P -B tools/oracle/dump_blocks.py

Two runs write identical bytes. `dump_blocks.py --verify`, per case:

1. reads the INPUT records, rebuilds every block from them through the
   reference's constructors with a `Theme` holding only the recorded values
   (**no block styles at all** at composition time) and a metrics resolver
   that answers only from the recorded tables, and checks the derived node
   lists;
2. reads the EXPECTED records of each width with a reader that calls nothing
   in `advtext.ui`, rebuilds the document's vertical list from them, `vpack`s
   and `ship`s it and compares with `vbox` and `cmds` — the records alone
   give the document;
3. runs the reference on the rebuilt blocks and compares every record.

The lexical layer, the node, `spec`, `measure`, `packed` and draw-command
records and the `break … endbreak` group are [FORMAT.md](FORMAT.md)'s. `run`,
`hardbreak`, `marker`, `space_width`, `has`, `kern`, `strut`, `hung`,
`strutted`, `dims` and the enums are [FORMAT-para.md](FORMAT-para.md)'s, with
the same meaning. Only what is new is described here.

Every expectation is an answer of one of the reference's own functions. The
script re-implements none of their logic; where it reads structure off a
result (which node of the list is the gap kern, the four children of a barred
box) it asserts what it read.

## 1. Enums added

| type | tokens |
|---|---|
| BlockKind (as a role) | FORMAT-para.md's, plus `code` `rule` |
| block type | `text` `code` `rule` |
| source kind | `markdown` `demo-file` `built` |
| gap rule | `sum` `list-empty` |

## 2. Files

    golden 1 blocks/<name>

Every case is of kind `blocks`.

| file | what |
|---|---|
| `paragraphs.golden` | two paragraphs × 4 alignments × 2 body sizes; 144 px per inch; three one-liners; one block alone; a hard break |
| `headings.golden` | levels 1–6 each followed by text (2 themes); headings in a row; a heading last; a heading that wraps; setext |
| `lists.golden` | paragraph then 3 items; nested 3 deep, ordered and unordered (2 themes); an item of two paragraphs; markers wider than the gutter; an empty item and an item holding only code; tight and loose |
| `quotes.golden` | a quote between paragraphs; a quote holding a list (2 themes); two paragraphs; a quote in a quote; twice quoted alone; in a list; with a heading; a bar interrupted by code and by a rule; two quotes in a row |
| `code.golden` | code between paragraphs with a tab, a blank line and a line longer than the measure (2 themes); characters the code face lacks; indented code; code in a list; two blocks; alone; `markdown.code_block` with CR LF |
| `rules.golden` | a rule between paragraphs (2 themes); first and last; two in a row; alone; in nested lists (negative width when the column is narrower than the indent); after a heading, before code |
| `edges.golden` | no blocks; an empty code block first / middle / last / alone / twice; an empty text block; edge space; zero space; a negative gap; a gap summing to zero; a first-line indent; hand-built blocks |
| `demo.golden` | `demo/sample.md` (16 px justified, 2 widths) and `demo/themes.md` (13 px ragged right, 1 width), tables dropped |

The two themes are `Theme.build(16)` (justified) and
`Theme.build(13, align=Align.LEFT)`. Widths are 40, 240, 400, 640 and 2000 px
at 16 px, and 40, 240, 640 px elsewhere unless the table says otherwise.

## 3. The `blocks` case

    case <name:string> blocks
    source <kind> <text:string>
    theme <px_per_inch:float> <spacing:Spacing> <marker_gap> <quote_indent>
          <quote_rule_thickness> <list_indent> <rule_thickness>           (one line)
    faces <prose:string> <math:string> <code:string>
    space_width ... / has ... / kern ... / measure ...        the whole document's, sorted
    blocks <n>
      ... n block groups, §3.2
    endblocks
    compose <run-name:string> <width> ... endcompose          once per width, §4
    endcase

### 3.1 INPUT: source, theme, metrics

`source` says where the block list came from and is **not** an input:

- `markdown`: `markdown.build_document(text, theme, fontlib_metrics).blocks`.
  The text is the markdown, for a reader.
- `demo-file`: the same for the named file of the reference; any
  `TableBlock` is dropped from the list (tables are a later item of D32.4).
- `built`: the script called `markdown.text_block`, `markdown.code_block`;
  the text is `%`.

`theme` is every `Theme` value building and composing the list reads besides
the per-block ones:

- `px_per_inch`, `spacing`, `faces`: as FORMAT-para.md §3.1.
- `marker_gap`: read by `_hang_marker`.
- `quote_indent`, `quote_rule_thickness`: read by `_barred`.
- `list_indent`, `rule_thickness`: **informational**. They are already inside
  each block's `left_indent` and a rule's `thickness`; `--verify` composes
  with both set to zero.

The metrics records are FORMAT-para.md §3.3's, for all blocks together. Code
lines ask `has` once per distinct character of the block (and whatever
`_stand_in` asks for one the code face lacks), and never `kern` or
`space_width`.

### 3.2 INPUT: one block

Blocks are numbered from 0 in document order; the number is the first field.

    text <i> <kind:BlockKind> <align:Align> <font:string> <left_indent>
         <first_line_indent> <quote_depth> <space_above> <space_below>     (one line)
    spec baselineskip ... / spec lineskip ... / lineskiplimit <v>
    strut <height> <depth>
    runs <n> ... endruns                  run / hardbreak, FORMAT-para.md §3.2
    marker <font:string> <text:string>    only with a list marker
    nodes material <n> ... endnodes       derived: TextBlock.material
    nodes marker <n> ... endnodes         derived, only with a marker
    nodes unshrinkable <n> ... endnodes   derived, only when align is not justify
    endtext

- `kind` is the block's role (what an ink is chosen by) and what selected its
  style; layout reads only the values written out.
- `left_indent` is absolute: every enclosing list and quote plus the style's
  own. `quote_depth` is how many quotes enclose the block.
- `space_above`, `space_below`, the interline triple, `align` and
  `first_line_indent` are the block's `BlockStyle`. `strut` is
  `Strut.filling(baselineskip.width)`.
- `font` is the style's own key and is informational: runs carry theirs.
- The marker's font is the one the reference chose for it (the innermost
  open list's), which need not be the block's.

    code <i> <font:string> <left_indent> <space_above> <space_below> <nlines> <source:string>
    spec baselineskip ... / spec lineskip ... / lineskiplimit <v>
    strut <height> <depth>
    nodes line <m> ... endnodes           derived, nlines times: CodeBlock.lines
    endcode

`source` is what `_Converter._code_block` was handed. The lines are
`_code_lines` of it with trailing blank lines dropped: tabs expanded to the
next multiple of 4 columns counted from the start of the line, split at
`\n`, one `char` per character **including spaces** (no glue, no kerns); a
character the code face lacks is stood in for as in a paragraph
(`char` with another face's key, or `rchar`). A blank interior line is a list
of no nodes. `left_indent` is the enclosing indent plus the code style's own.

    rule <i> <left_indent> <thickness> <space_above> <space_below>
    spec baselineskip ... / spec lineskip ... / lineskiplimit <v>
    strut <height> <depth>
    endrule

A rule's interline triple is handed to `append_to_vlist` and, since a rule is
always the first box of its run, never used; its strut is not applied.

## 4. EXPECTED: `compose`, one per width

    compose <run-name:string> <width>
    lines ... endlines            once per block, in order         §4.1
    stack 0 <nruns> ... endstack                                   §4.2
    extents <n> <top0> <bottom0> ... <top n-1> <bottom n-1>        §4.3
    vbox <width> <height> <depth>
    origin <x> <y>
    cmds ... endcmds
    units <nruns>                                                  §4.4
      unit ... / span ...
    endcompose

`width` is the measure in scaled points; a run name `ui-240px` is
`sp_from_px(240, px_per_inch)`.

### 4.1 One block alone: `lines`

What `_block_boxes(block, theme, width, measure, PROSE_BREAKING)` returns.

    lines <i> text <nlines>
    break <run-name/i:string> <list> ... endbreak      FORMAT.md §6, no ship section
    hung                                               only with a marker
    box ... endbox
    strutted <nlines>
      dims <width> <height> <depth> <shift_amount>     nlines times
    endlines

Exactly FORMAT-para.md §3.4–3.5 up to `strutted`: the `break` group is what
`_text_boxes` handed the breaker and got back, `hung` is line 0 after
`_hang_marker`, and each strutted line is the box it was made from with
`height = max(height, strut.height)`, `depth = max(depth, strut.depth)`.

    lines <i> code <nlines>
    box ... endbox                                     nlines times
    endlines

Each line is `hpack` of the line's nodes at its natural width, then
`shift_amount = left_indent`, then the strut. It is **never broken and never
clipped**: a line longer than the measure gives a box wider than the measure,
and the document `vbox` is as wide as `left_indent` plus the longest line. A
blank line is an empty box of the strut's height and depth. A code block of
no lines gives no boxes.

    lines <i> rule 1
    box ... endbox
    endlines

`_rule_box`: `hpack` of `kern <left_indent>` and
`rule <width - left_indent> <thickness> 0`, at natural width. Not strutted.
When `left_indent > width` the rule's width is negative, the box is `width`
wide, and ship-out writes no `rulerect` for it.

### 4.2 The stacking: `stack`

`_stacked(blocks, theme, width, measure, PROSE_BREAKING, depth, None)`, item
by item, in the order of the vertical list it returns.

    stack <depth> <nruns>
      srun <first_block> <nblocks> <barred:bool>
      gap <space_below> <space_above> <amount> <rule>      every run but the first
      kern <amount> normal                                 only when rule is sum and amount != 0
      append <prev_depth_in> <prev_depth_out>              once per box of the run
      glue ...                                             the interline glue, when there is one
      ref <block> <line>                                   or a barred group, below
      endsrun
    endstack

**Runs.** `_runs(blocks, depth)`: a run is one block, or a maximal sequence of
consecutive blocks whose `quote_depth` is greater than `depth` (`barred` = 1).
Only a text block has a quote depth; a code block or a rule inside a quote
reads as depth 0 and ends the run.

**The gap is the SUM, not the maximum.** Between two runs the reference
appends one `kern` of

    amount = space_below of the LAST block of the run above
           + space_above of the FIRST block of the run below

(`gap`'s first two fields, then their sum). The kern is of kind `normal`. It
is omitted when the sum is zero, and it may be negative. There is no
collapsing of any kind.

`gap … 0 list-empty` is the one exception, and it is a condition on the
**list**, not on the position: the reference adds a gap only when the list it
is building already holds something. So no space goes above the first run —
and none goes above a later run either, if every run before it set no box (an
empty code block). In that case the earlier run's `space_below` and this
run's `space_above` are both dropped.

A run that sets no box still takes part on both sides once the list is not
empty: an empty code block between two paragraphs leaves two kerns back to
back (`edges.golden`, `empty-code-in-the-middle`).

The first block's `space_above` and the last block's `space_below` are not
applied, at the top level or inside a quote.

**`prev_depth`.** `append <in> <out>` is the `prev_depth` the box was appended
with and the one `append_to_vlist` returned (the box's depth). **Every run
starts at `IGNORE_DEPTH` (−65536000)**, whether or not a kern was written, so
there is never interline glue between two blocks: the kern is the whole gap.
Within a run of one block the lines are appended under that block's
`InterlineSkip`; from the second line on there is a `glue` record before the
`ref` (`glue 0 0 normal 0 normal` between lines no taller than the strut; the
lineskip when a stood-in character makes a line taller).

`ref <block> <line>` is strutted line `line` of block `block` from §4.1.

**A barred run** is one box in the list:

    append <prev_depth_in> <prev_depth_out>
    barred <width> <height> <depth> <shift_amount> <glue_set:float> <glue_sign> <glue_order>
    kern <bar_x> normal
    rule <quote_rule_thickness> <inner.height> <inner.depth>
    kern <-(bar_x + thickness)> normal
    inner <width> <height> <depth> <shift_amount> <glue_set:float> <glue_sign> <glue_order>
    stack <depth+1> ... endstack
    endbarred

`barred` is an hlist box holding exactly those three nodes and the `inner`
box; `inner` is a vlist box, `vpack` at natural height of the nested stack's
list — `_stacked` over the run's blocks at `depth + 1`, so a block quoted
twice is barred again inside it. `bar_x` is
`max(0, min(left_indent of the run's text blocks) - quote_indent)`. The rule
overlaps the text's own indent; the negative kern puts `inner` back at x = 0,
because each line already carries its indent as `shift_amount`.

### 4.3 `extents`, `vbox`, `cmds`

`extents` is `_stacked`'s own bookkeeping: per block, `(top, bottom)` in
scaled points down from the top of the document box. A block's origin is
`(0, top)`; its first baseline is `top` plus its first line's height. A block
that set no box has `top == bottom`. Gap kerns belong to no block.

`vbox` is `compose_blocks(blocks, …)`: `vpack` of the depth-0 list at its
natural height (its reference point on the last baseline, as any vbox).
`origin` is `Point(0, vbox.height)`, as in FORMAT-para.md, which puts the
top-left of the document at zero. `cmds` is
`ship(vbox, origin, measure)`.

**Which channel draws what.** A thematic rule and a quote bar are `rule`
nodes and arrive as `rulerect` commands, in list order — a bar's `rulerect`
comes before the commands of the text it stands beside. The reference draws
no background behind a code block and no fill of any other kind: there is no
second channel.

### 4.4 `units`: the data an ink is chosen by

The port's draw takes colours, not roles. The reference colours per **unit**
(a depth-0 run, `block_runs`) with `Palette.ink_for(role of its first
block)`, then re-colours the later blocks of a multi-block unit by index
range. Every `rulerect` takes `Palette.rule` whatever the unit. The default
palette names no roles, so everything is one ink.

    units <nruns>
    unit <first_block> <nblocks> <top> <cmd_start> <cmd_end> <index_start> <index_end>
    span <block> <index_start> <index_end> <role:BlockKind>       nblocks times

- `[cmd_start, cmd_end)` are the unit's commands in `cmds`; the units tile
  it. The script asserts `cmds` equals each unit shipped alone
  (`RunComposer.compose` + `units.ship_unit`) and moved down by `top` and up
  by `index_start` (`units.moved`).
- `[index_start, index_end)` are reading-order indices (`glyph`/`setglue`
  `index`, `line` `start`/`end`).
- `span` is `Unit.spans` in document indices: for a unit of one block the
  whole range; for a quote, each block owns the items whose baseline `y` is
  at or above its bottom edge. `role` is `bare_view.block_kind(block)`.

## 5. Things a port must reproduce that no single record shows

- The gap rule and the `prev_depth` reset of §4.2, including `list-empty`.
- A paragraph inside a list item has the item's kind; one inside a quote has
  kind `quote` **even inside a list in the quote** (its marker is still set).
  That is how the blocks arrive; the port's block layer is handed the style
  values and need not know why.
- A second paragraph of a list item is a block of the item's kind with no
  marker and the same indent.
- Consecutive quoted blocks are one run with one bar, even when they came
  from two separate quotes; a code block or a rule in a quote splits it.
- A marker wider than `left_indent` overfulls its box and runs into the text
  (`lists.golden`, `ordered-from-9-wide-markers`).

## 6. Decisions taken in writing this oracle

- **Blocks are recorded, not markdown.** The runs are the reference's own
  `_append_text` calls, the code source is what `_code_block` received.
- **Expectations are computed from the rebuilt blocks under the cut-down
  Theme**, after asserting at every width that they compose to the same box
  as the reference's own blocks under its own Theme. Dump and `--verify`
  therefore run the same code on the same inputs.
- **A block's commands are not shipped separately.** The document is shipped
  once and `units` gives the ranges; FORMAT-para.md's per-block `vlist`,
  `vbox`, `cmds` would repeat `stack` and `cmds`.
- **The style's own `left_indent` is not recorded**: composition reads only
  the block's absolute one.
- **Tables are dropped** from the demo documents rather than the documents
  being left out.
- **Links are dropped** (`TextBlock.links`): they index material and are not
  layout. No case has a formula.

## 7. What is not in here

- Tables (`TableBlock`, `SizedTable`), hosted blocks (`HostBlock`), display
  and inline formulas, link areas.
- `LayoutCache`, `UnitLayout`/`layout_units` and incremental relayout. Note
  that `layout_units` places units with `run_gap` unconditionally and so
  **disagrees with `compose_blocks`** when the first block sets no box
  (`edges.golden`, `empty-code-first`): these goldens hold `compose_blocks`.
- Any breaking parameters but `PROSE_BREAKING`; `ragged_total_fit`.
- `Theme.build`, `Theme.at_pixel_height`, `Theme.font_for`, markdown parsing,
  marker numbering: how a host's choices become the values recorded here.
- A monospace theme (`Faces.mono()`), `Spacing` other than the default
  (`tests/golden/para` has both for one block).
- A palette: only the roles and ranges an ink would be chosen by.
- Selection, copy text and hit testing across blocks.
