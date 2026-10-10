# Document goldens: `tests/golden/doc/*.golden`

Written by `tools/oracle/dump_doc_extras.py` from the Python reference: what
happens **above one block and across several** — selection and copy across
blocks, decorations, host blocks, streaming. None of it had a golden, and none
of it can be dumped once advtext runs on imtypeset (D34, "the oracle is frozen
before the swap").

| file | kind | what |
|---|---|---|
| `selection.golden` | `doc` | 7 documents, 10 layouts: units, index ↔ (block, byte), separators, drags, probes, ranges with rectangles and the copied string |
| `decorations.golden` | `deco` | links, colour spans, syntax ink, find hits → ink ranges, fills, link areas, and what reaches the draw list |
| `hosts.golden` | `host` | a block reserving a rectangle: stacking, extents, copy across it, a resize |
| `stream.golden` | `stream`, `flips` | a document fed in chunks, step by step; the settled-line property |

Regenerate, from the repository root, with

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=../imgui-adv-text \
        ../imgui-adv-text/.venv/bin/python -B tools/oracle/dump_doc_extras.py

(about two minutes; `--only selection,hosts` writes some files only). Two runs
write identical bytes. `--verify` (about as long) re-reads every file, rebuilds
each case from the rows before `expect` alone, re-asks every recorded question
— each `range`, `drag` and `probe` row's own fields, read back from the file —
and compares every record after `expect`.

The lexical layer, `cmds … endcmds` and `nodes … endnodes` are
[FORMAT.md](FORMAT.md)'s; `spec`, `lineskiplimit`, `strut` and the enums are
[FORMAT-para.md](FORMAT-para.md)'s; the `theme`, `faces`, `text`, `code`,
`rule` headers are [FORMAT-blocks.md](FORMAT-blocks.md)'s with the same fields.
Only what is new is described here.

## 1. Positions: the reference's index AND (block, byte offset)

The reference speaks two index spaces (`advtext/spans.py`'s docstring):
**reading-order indices** over what was shipped (one per glyph, one per
horizontal glue, one per formula) and **material indices** into a block's node
list. The product's API speaks neither: it speaks *(block, byte offset into
that block's text)* (`.concerns/api-revise--lead--rulings.md`, R3;
`docs/advtext-migration.md`, R3). So wherever a record carries a reference
index it also carries the position, and both are stated.

**A block's text is not something the reference keeps.** `advtext/spans.py`
says so ("Source offsets … would need the converter to keep them, and it does
not"). It is defined here, and it is the script's definition:

- a **text block**: the concatenation, in order, of what the reference's
  converter was handed for it — each `_append_text` call's text as given
  (after markdown's inline parsing: no `*`, no backquotes, a soft break as one
  space), a set formula as `FormulaSpan.text` (its source **with its dollars**,
  white space inside a display formula collapsed as the reference collapses
  it), a hard break as one `\n`. The marker is not part of it.
- a **code block**: `advtext.spans.material_text(block).text` — the lines
  after tab expansion, trailing blank lines dropped, joined by `\n`. Its
  `source` (what `_code_block` was handed) is recorded beside it; the two
  differ exactly where FORMAT-blocks.md §3.2 says.

The byte of a node is read off against that text and **asserted**: every
character node is the next character of its piece that is not white space
(compared with what the node says it stands for), every interword glue is the
white-space run at the cursor. A script that mis-read the converter would fail
there, not write a wrong offset.

An item's bytes are `[byte, byte + length)`: a character's UTF-8 bytes; a glue's
whole white-space run; a formula's whole source, dollars included; the `\n` of
a hard break for the fill glue before it. An item the text does not hold — a
marker's glyphs, `\leftskip`, `\rightskip`, `\parfillskip`, the `\hss` before a
marker — has byte `-1`.

Two derived forms appear in records, both computed by the script from `imap`
(§3.3) and nothing else:

- **pos** `<block> <byte>` of one index: its block and its byte, byte `-1` for
  an item the text does not hold; `-1 -1` past the end.
- **span** `<block_a> <byte_a> <block_b> <byte_b>` of an index range
  `[start, end)`: the block and first byte of its first sourced item, and the
  block and END byte (exclusive) of its last sourced item; four `-1` when it
  holds none. A range's unsourced items at either end (a marker, a line's fill)
  are therefore not in its span, and two different index ranges can have one
  span.

## 2. A case

    case <name:string> <kind>
      ... INPUT rows ...
    expect
      ... EXPECTED rows ...
    endcase

INPUT rows of `doc`, `deco` and `host`:

    markdown <math:bool> <drop_tables:bool> <source:string>
    themein <body_px:float> <align>
    seed <n>
    width <run-name:string> <px:float>          one per layout

The reference is driven with exactly these: `Theme.build(body_px, align=…)`,
`markdown.build_document(source, theme, fontlib_metrics, math=markdown_math())`
(`math=None` when the flag is 0), `TableBlock`s dropped from the list when
`drop_tables` (tables are a later item of D32.4), a
`BareView(document, theme, char_measure(fontlib_metrics))`, and
`view.layout_for(sp_from_px(px, theme.px_per_inch))` per width. `seed` seeds
the choice of questions (§3.5).

**A port has no markdown parser.** What it is driven with is the first
EXPECTED section, the block list as data:

### 2.1 `blocks`

    theme <px_per_inch:float> <spacing> <marker_gap> <quote_indent>
          <quote_rule_thickness> <list_indent> <rule_thickness>    FORMAT-blocks.md §3.1
    faces <prose:string> <math:string> <code:string>
    decor <table_rule_thickness>           a link's underline is this thick, twice this below the baseline
    blocks <n>
      text <i> <kind> <align> <font:string> <left_indent> <first_line_indent>
           <quote_depth> <space_above> <space_below>               FORMAT-blocks.md §3.2
      spec baselineskip … / spec lineskip … / lineskiplimit <v> / strut <h> <d>
      marker <font:string> <text:string> <numbered:bool>           only with a list marker
      nest <list_depth>
      content <nbytes> <nmaterial> <text:string>
      piece run <byte_start> <byte_end> <material_start> <material_end> <font:string> <breakable:bool>
      piece hardbreak <byte_start> <byte_end> <material_start> <material_end>
      piece formula <byte_start> <byte_end> <material_start> <material_end> <display:bool> <source:string>
      link <number> <byte_start> <byte_end> <material_start> <material_end> <target:string>
      formula <number> <byte_start> <byte_end> <material_start> <material_end> <display:bool> <refused:bool> <text:string>
      endtext
      code <i> <font:string> <left_indent> <space_above> <space_below> <nlines> <source:string>
      spec … / lineskiplimit / strut
      nest <list_depth>
      language <string>
      content <nbytes> <nmaterial> <text:string>
      endcode
      rule <i> <left_indent> <thickness> <space_above> <space_below>
      spec … / lineskiplimit / strut
      nest <list_depth>
      endrule
    endblocks

- `content` is §1's text. `nmaterial` is `len(block.material)` (a code
  block's: its nodes over all lines).
- `piece` rows tile the text in order. A `run` is one `_append_text` call: its
  bytes, the material nodes it made, the font key it was given and whether it
  may be broken (an inline code span may not). Runs are the reference's calls,
  not maximal: two consecutive rows may have one font. `formula`'s `source` is
  without dollars; its bytes cover the dollars. A formula the builder
  **refused** is a `run` in the code face holding its source with dollars, and
  has a `formula` row with `refused` 1 and no `piece formula`.
- `link`: `TextBlock.links[number]`, as bytes and as the material range the
  reference holds.
- `nest` is `advtext.ui.units._list_depth`: how many lists the block is
  inside, `0` for an item with no list's indent, `-1` for a block in no list.
  `numbered` is `_numbered`: whether the marker holds a digit. Copy reads both.
- No metrics tables: a port replays these with its own font reader, which
  `tests/golden/para` and `tests/golden/blocks` hold to the reference
  (`dump_para_map.py` set that precedent).

## 3. `doc`: selection and copy across blocks

After `endblocks`, one section per width:

    layout <run-name:string> <width>
    size <height> <length> <nunits>
    unit <block_start> <nblocks> <part> <y> <height> <base> <count> <nlines>
    bspan <block> <index_start> <index_end> <x> <y> <width> <height>
    lead <block> <has:bool> <separator:string> <indent:string>
    cmds <n> … endcmds
    imap <block> <index_start> <n> (<byte> <length>){n}
    unmapped <n> <block>{n}
    seps <n> / sep <index> <block> <byte> <text:string>
    atomtexts <n> / atomtext <index> <block> <byte> <text:string>
    originals <n> / original <index> <block> <byte> <text:string>
    drags <n> / drag …
    probes <n> / probe …
    ranges <n> / range … / copy … / rects <n> / rect …
    blocktexts <n> / blocktext <block> <text:string>
    endlayout

### 3.1 `size`, `unit`, `bspan`

`advtext.ui.units.layout_units`' answer (`UnitLayout`). `length` is how many
reading-order items the document has. A **unit** is one depth-0 run
(FORMAT-blocks.md §4.2): one block, or consecutive quoted blocks. `y` is its
top in scaled points down from the document's top, `base` its first index,
`count` how many it owns, `nlines` how many `line` commands it shipped. `part`
is `-1` except for a piece of a long code block or table in a `StreamView`
(§6).

`bspan`: `UnitLayout.indices_of_block` and `BareView.block_rect` — the
indices a block owns (they tile `[0, length)`; a rule and a host block own
none) and its rectangle: **the full measure wide**, from its first line's top
to its last line's bottom, the gap between two blocks in neither.

### 3.2 `cmds`

`UnitLayout.cmds`: the document's commands in document coordinates, the top
left of the document at (0, 0). The script does not re-assert here that they
equal `ship(compose_blocks(…))`; `hosts.golden`'s `composed` row records that
comparison where it is made.

### 3.3 `imap`: every index as bytes

For block `<block>`, the `n` indices from `index_start`: each item's byte
offset and byte length in the block's text, `-1 0` for an item the text does
not hold. Built from `advtext.spans.map_text_block` (a block of a quote is
mapped against its own share of the unit's commands, as
`advtext.ui.units._atoms_within` does it) and `map_code_block`. `unmapped`
lists blocks for which the reference raised `SpanMapError`; there are none in
these files.

### 3.4 What a copy writes: `lead`, `sep`, `atomtext`, `original`

`lead` is `UnitModel._leads`: for each block, the separator that goes
**before** it when a copy runs into it from the block above, and the indent
its own later lines take. The rule, which no single record shows:

- every block in one outermost list — an item at any depth, a later paragraph
  of an item, a fence or a quote inside one — is led by `\n`, never a blank
  line; everything else, the first block of a list included, by `\n\n`;
- the indent is two spaces per list the block is inside **beyond the first**,
  and is appended to the separator;
- a bulleted list followed by a numbered one is two lists (`numbered`), and a
  list ends at a block quoted less deeply than it began.

`sep` is `UnitModel.separators_within(0, length)`: every index at which a
break is content, with the text written there — the `lead` of a block at its
first index; **one space after a list marker** (the marker is `\hss`, glyphs
and a kern, and the kern has no index); `\n` at the first item after a hard
break; `\n` at the start of each code line after the first. A separator is
written only between two pieces of selected text
(FORMAT-selection.md §4, "`text` edits the tail").

`atomtext`: `atoms_within` — a formula copies as its source, dollars
included. `original`: `originals_within` — a character no face has is drawn
as U+FFFD and copies as what was written.

The **marker's glyphs are selected and copied** (they have indices): a
bulleted item copies as `• text`.

### 3.5 Questions: `drag`, `probe`, `range`

The questions are chosen by the script from the layout (and `seed`), written
into the row, and are INPUT: `--verify` reads them back.

    drag <ax> <ay> <bx> <by> <granularity> <start> <end> <span>

`UnitModel.select_at(Point(ax, ay), Point(bx, by), granularity)`. For every
pair of consecutive blocks that hold a line: a point a third along the upper
block's last line, a point two thirds along the lower block's first line and
a point in the gap between them, dragged upper→lower, lower→upper, upper→gap,
gap→lower and gap→gap at each of `character`, `word`, `line`; and one drag
from above the first line to below the last.

    probe <x> <y> <hit> <block_at> <pos> <word_start> <word_end> <line_start> <line_end>

`UnitModel.hit`, `UnitLayout.block_at` (`-1` in a gap or outside: the space
between blocks belongs to neither), `pos` of the hit, `word_at`, `line_at`. At
the three points above, just right of the upper line's end, left of the lower
line's start, and far above and below the document.

    range <start> <end> <span> <nblocks> <block>{nblocks}
    copy <text:string>
    rects <n>
    rect <x> <y> <width> <height>

`Selection(start, end)`: the blocks it touches (`BareView.selected_blocks`),
the exact copied string (`UnitModel.copy_text`), and the highlight
(`UnitModel.rects`). Ranges: the whole document; each block's own; for each
pair of neighbours — middle to middle, one item either side of the boundary,
start to start, end to end, and the empty range at the boundary; middle to
middle across three blocks; twelve at random. Thinned evenly to at most 140.

`blocktext` is `BareView.block_text`: the copy of the block's own range.

### 3.6 Files

| case | what |
|---|---|
| `mixed/16px-justify` (240, 400 px), `mixed/13px-left` (240 px) | a heading with a link, a paragraph with emphasis, inline code, an inline formula, a link that wraps and a hard break, a display formula, bullets with a second paragraph and a nested numbered list, a quote of two paragraphs, a code block with a blank line, a rule, a paragraph with a character no face has |
| `two-paragraphs`, `nested-lists`, `code`, `headings` | `dump_blocks.py`'s `TWO_PARAGRAPHS`, `NESTED`, `CODE`, `HEADINGS`: the block goldens' inputs |
| `demo-sample` (400 px) | `demo/sample.md`, its table dropped |

## 4. `deco`: decorations

More INPUT rows:

    links <styled:bool>                                   BareView(style_links=…)
    role <kind> <colour>                                  Palette().with_roles({…})
    find <needle:string> <ignore_case:bool> <mark:colour>
    codeink <block> <line> <column> <length> <ink:colour>
    textink <block> <char_start> <char_end> <ink or -1> <mark or -1>
    select <from_percent> <to_percent> <origin_x:float> <origin_y:float>

Colours are `IM_COL32` integers. The host's colour spans are built **in row
order** — `find`: `advtext.spans.find_spans(block, needle, mark=…)` for every
text and code block; `codeink`: `ColourSpan(code_position(block, line,
column), code_position(block, line, column + length), ink=…)`, a tokeniser's
shape; `textink`: `colour_text(block, start, end, …)`, offsets in
**characters** of `material_text(block).text` — and handed to
`BareView.set_colours` per block. `select` is the selection drawn with them,
as percentages of the document's length, and where the document's origin is
on screen.

Per width, after `size`/`unit`/`bspan`, `cmds` and `imap` as in §3:

    found <needle:string> <ignore_case:bool> <mark> <n>
    match <block> <material_start> <material_end> <byte_start> <byte_end> <nrects>
    rect <x> <y> <width> <height>

`advtext.spans.FindIndex.of(blocks).find(needle, ignore_case=…)`, each match
with `BareView.span_rects`: one rectangle per line it lands on, the full
height of the line. The script asserts `find_spans` gives the same ranges.
The rules a port must reproduce: matches do not overlap and are found left to
right; in a text block a space in the needle matches a hard break and the
reverse, in a code block not; the text searched has one space per interword
glue, so a phrase is found across a source line end and across a set line
end; text inside a formula is not searched; case folding is skipped for a
block whose folded text has another length.

    colours <block> <n>
    colour <material_start> <material_end> <ink or -1> <mark or -1> <byte_start> <byte_end>

What `set_colours` was handed, in order.

    unmapped_blocks <n>
    runs <n> / inkrun <ncmds> <ink>
    decoinks <n> / ink <start> <end> <colour> <span>
    roleinks <n> / ink …
    inks <n> / ink …
    fills <n> / fill <x> <y> <width> <height> <colour>
    linkareas <n>
    linkarea <block> <number> <target:string> <nruns> <nrects>
    lrun <line> <start> <end> <span>
    rect <x> <y> <width> <height>

Exactly what `BareView._paint` computes with the whole document in view:

- `runs`: `bare_view._inked` — the commands as `(commands, ink)` runs, one
  per stretch of units whose FIRST block has the same role ink. With no
  `role` rows it is one run in `Palette.ink`.
- `decoinks`: `BareView._decorate`'s ink ranges — links (when styled) and the
  host's spans. **Where two overlap the later span wins, and any host span
  wins over a link's ink.** Sorted, non-overlapping, merged where adjacent and
  equal.
- `roleinks`: `_role_inks` — the later blocks of a multi-block unit (a quote)
  whose role ink differs from the unit's first block's.
- `inks`: `_overlaid(roleinks, decoinks)`, the `inks` argument of
  `draw_runs`: a decoration wins over a role.
- `fills`: the `fills` argument of `draw_runs`, in the reference's order: per
  unit, per block, each link's underlines, then each colour span's marks in
  span order — marks and underlines are NOT merged or sorted, and overlapping
  marks are both drawn. A mark is the full height of its line. An underline is
  `Rect(x, baseline + 2·t, width, t)` with `t = decor`'s thickness, one per
  line of the link, only when links are styled.
- `linkarea`: `BareView._links`. One per link, with one `lrun` (the block's
  line number and the index range) and one `rect` per line the link's text is
  on. They are returned whether or not links are styled.

Then what reaches the draw list, through `dump_draw.py`'s recording draw list
and its ImGui context (FORMAT-draw.md):

    drawn <sel_start> <sel_end> <origin_x:float> <origin_y:float> <nhighlights>
    rectsfilled <n>
    rectfilled <texts_before> <x0:float> <y0:float> <x1:float> <y1:float> <colour>
    glyphinks <ntext> <nruns> (<count> <colour>){nruns}

`draw_runs(recorder, runs, origin, palette=…, highlights=
layout.visible_rects(selection, viewport), inks=inks, fills=fills)`. Every
`add_rect_filled` in call order — the fills, then the highlights (colour
`Palette.highlight`), then each rule where it comes among the glyphs — with
how many `add_text` calls came before it; and the colour of every `add_text`
in order, run-length encoded. FORMAT-draw.md holds how a rectangle is snapped;
this records that the channels arrive in that order with those colours.

| case | what |
|---|---|
| `mixed/links-styled` (240, 400 px) | §3.6's document; seven searches (a word; the same ignoring case; a phrase across a source line; a phrase across a run boundary, which at 240 px is also across a set line; one inside a link; one in code; one that is not there), four syntax inks, an ink with a mark in the heading, an ink across a hard break |
| `mixed/links-unstyled-roles` (240 px) | the same with links not styled and role inks for quote, heading and code |
| `quote/roles` | a heading and a paragraph in one quote: the role-ink channel, with a link and a find hit over it |

## 5. `host`: a block that reserves a rectangle

More INPUT rows:

    hostfence <language:string> <height_px:float> <text:string>
    resize <language:string> <height_px:float>

`BareView(host_for=…)` with a callback that claims every code block of that
language for a widget (`advtext.ui.Hosted`) answering `height_px` whatever
width it is asked about and copying as `text`. That is how the reference's
own `DiffFences` hosts a diff.

Per width:

    hosts <n>
    hosted <block> <indent> <width_px:float> <height_px:float> <height> <left_indent>
           <box_width> <box_height> <box_depth> <text:string>
    size / unit / bspan                         §3.1
    gap <unit> <amount>                         before every unit but the first
    composed <width> <height> <same_height:bool> <same_cmds:bool>
    built <n> <unit>{n}
    lead / cmds / imap / unmapped / seps / atomtexts / originals    §3
    edge_hosts <nleading> <block>… <ntrailing> <block>…
    copyall <everything:bool> <text:string>     twice: 0, 1
    selectall <nblocks> <block>… <text:string>
    ranges <n> / range … / copy …               no rects
    probes <n> / probe <x> <y> <hit> <block_at> <pos>
    blocktexts <n> / blocktext …
    resized <changed:bool> … endresized         only with a `resize` row
    endlayout

What a port has to reproduce:

- **The rectangle.** A host block is laid out as `advtext.ui.HostBlock`: a
  unit of its own with **no commands, no index and no line**, `height` scaled
  points tall (`sp_from_px(height_px)`), the full measure wide. `indent` is
  `max(block.left_indent - block.style.left_indent, 0)` — the enclosing list's
  indent and NOT the code style's own — and the widget is asked for its height
  at `width_px = max(px_from_sp(width - indent), 1.0)`, which is not exactly
  the pixel width given (`0x1.e000008805f38p+7` for 240). `box_*` is
  `advtext.ui._host_box`: a vbox of one kern, height `max(height, 0)`, depth 0.
- **Stacking.** `gap` is `advtext.ui.run_gap`: the upper block's
  `space_below` plus the lower's `space_above`, the code style's for a host
  block — it keeps the block's style. A host block of height 0 still takes
  both gaps.
- `composed` is `compose_blocks` over the same blocks: its width, its height
  plus depth, whether that equals `layout.height` and whether its shipped
  commands equal `layout.cmds`. **Both are 1 in every case here, a host of
  height zero first in the document included** (`zero-height-first`): a host
  block always sets a box (a vbox of one kern), so the vertical list is not
  empty after it and the gap below it is added by both. That is unlike an
  EMPTY CODE BLOCK first, where the two disagree (FORMAT-blocks.md §7). The
  composed box's width is that of its widest box: 0 for a document that is
  only a widget (`only-a-host`), while `bspan` gives the widget the measure.
- **Hit testing.** A point inside the rectangle gives `block_at` the host's
  block; `hit` there is the nearest text's boundary, as for any gap.
- **Copy.** A host block has no index. Its text is written **into the
  separator of the next block that has text** (`sep` rows), behind the host's
  own lead and before that block's: so it is copied only when the selection
  has text on both sides of it. A host with no text before it, or none after
  (`edge_hosts`, `UnitModel.edge_hosts`), is copied only by
  `copy_text(…, everything=True)`, which the select-all gesture sets and no
  range implies (`copyall 0` and `copyall 1` differ exactly there;
  `selectall` is `BareView.select_all` then `selected_blocks` and
  `selected_text`). The text loses one trailing newline; inside a list its
  later lines take the list's indent. `blocktext` of a host block is its text.
- **Resize.** After the widgets' heights change: `resized` says
  `BareView._hosts_resized()` saw it; then `hosts`, `size`, `unit`, `bspan`,
  `gap`, `composed` and `built` again after `invalidate()`. `built` is
  `LayoutStats.built`: the units the reference composed and shipped — the
  host blocks alone; everything else is moved, not laid out.

| case | what |
|---|---|
| `between-text` (240, 400 px), `between-text/13px` | a widget between paragraphs, two with one paragraph between, one inside a list item, an ordinary code block not hosted; resized |
| `at-the-edges` | a widget first and a widget last; resized |
| `only-a-host` | a document that is one widget |
| `zero-height`, `zero-height-first` | height 0, then resized; height 0 first in the document |
| `not-hosted` | the same source with no widget, for the difference |

## 6. `stream`: a document fed in chunks

INPUT rows:

    themein <body_px:float> <align>
    policy <keep_streamed|reset>
    math <bool>
    full <bool>
    width <run-name:string> <px:float>
    chunk <text:string>                          in order; their concatenation is the source

`StreamDocument(theme, fontlib_metrics, policy=…, math=markdown_math())`, a
`StreamView(stream, measure)`; per chunk `stream.append(chunk)`, `view.sync()`,
`view.layout_for(width)`; after the last, `stream.close()` and once more.

    step <number> <append|close> <revision> <ncommitted> <nlive> <nunits> <height> <length>
    sblock <block> <committed|live> <text|code|table|rule> <kind> <align> <nmaterial> <text:string>
    stable <ncommitted_before> <nlines> <moved>
    settled <block> <lines_before> <lines_now> <kept:bool>
    built <n> <unit>{n}
    broken <blocks_broken> <nunits>
    unit <position> <block_start> <nblocks> <part> <y> <height> <base> <count> <digest>
    cmds <n> … endcmds                           after a `unit` that was built, when `full`
    endstep
    …
    final <nblocks> <equal_whole:bool> <height> <length> <digest>
    totals <steps> <moved> <unsettled> <built> <units>
    cmds <n> … endcmds                           the closed document's, when `full`

- **`sblock` is written only for a block that is not the object it was a step
  ago.** A block with no row in a step is unchanged, by identity. Blocks
  `[0, ncommitted)` are `stream.committed`; the rest `stream.live`. `text` is
  `material_text(block).text` (a formula's source is not in it). **A live
  block's `align` is never `justify`**: the live tail is built with
  `live_theme` (ragged right), and under `keep_streamed` it stays so when it
  commits; under `reset` it is rebuilt justified at commit — one `sblock` row
  with the new alignment, in the step it commits.
- **`unit` is written only for a position whose row differs from the step
  before.** `digest` is of the unit's commands in its OWN coordinates (top at
  0, first index 0): FNV-1a, 64 bits, offset basis `cbf29ce484222325`, prime
  `100000001b3`, fed 8 little-endian bytes of each value as a two's-complement
  64-bit integer, per command: `glyph` → 1, each UTF-8 byte of the font key
  (each as a value), codepoint, x, y, index; `rulerect` → 2, x, y, width,
  height; `setglue` → 3 (`atom` → 4), x, y, width, natural, height, depth,
  index; `line` → 5, x, y, width, height, depth, start, end. Written as 16
  hex digits. `final`'s is of the whole document's commands.
- **`built`** is `LayoutStats.built`: the positions of the units the
  reference composed and shipped in this step; every other unit was found.
  `broken` is `LayoutStats.blocks_broken` and `.units`. **This is what the
  reference re-lays-out per chunk**: the live tail's units, and — once — a
  block that commits under `reset`.
- `part`: a `StreamView` lays a code block of more than 16 lines out as
  pieces of 16 (`CODE_PIECE_LINES`), each a unit with the block's
  `block_start` and `part` counting from 0, so that a growing fence costs its
  last piece. A table likewise by 4 rows.
- **`stable` is the stability property, checked**: of the `ncommitted_before`
  blocks that had committed a step ago, `moved` counts those that are no
  longer the same object, plus one if `BareView.set_lines(0,
  ncommitted_before)` — every set line of them: block, baseline, each glyph's
  codepoint and x — differs from what it was. **It is 0 in every step of
  every case** (`totals`' second field).
- `settled` is the weaker property of the block being WRITTEN, for each live
  text block that was live a step ago with more than one line: whether every
  line but its last is where it was. It is **not** always 1 (`totals`' third
  field: 2 in `sample/7-chars`, 1 in the cuts case, 3 in `mixed/3-chars`), and
  the reference says why: a source prefix is not a word prefix (an unclosed
  emphasis or link is literal until it closes), and the measure's widest word.
- `final`: `equal_whole` is whether the closed stream's blocks equal one
  `build_document(source, live_theme or theme, references=False, math=…)`.
- A table appears as an `sblock … table` row and in the units; nothing else
  is recorded of it.

| case | chunks |
|---|---|
| `sample/by-line`, `-reset`, `-ragged` | `demo/sample.md` less its table, plus a paragraph with an inline formula and a `$$` block, a line at a time: keep-streamed, reset, and a ragged theme |
| `sample/7-chars`, `sample/64-chars`, `sample/one-chunk` | the same source cut every 7 characters (inside words), every 64, and not at all |
| `sample/cuts-mid-word-formula-fence` (`full`) | cuts inside a word, inside `$…$`, on and inside the `$$` lines, inside the opening fence, the code and the closing fence |
| `sample-with-table/by-line` | `demo/sample.md` whole |
| `mixed/3-chars` | §3.6's document, three characters at a time, 240 px |

### 6.1 `flips`: the settled-line property the reference tests

    property <width_pt:float> <prose|plain> <words>          INPUT
    nodes material <n> … endnodes
    measure_width <width> <widest_word_fits:bool>
    prefix <words> <nnodes> <nlines> <words_on_line>{nlines}
    first_fit <nlines> <words_on_line>{nlines}
    flips <steps> <most_lines> <nflips> <words>{nflips}

`../imgui-adv-text/tests/test_stream_stability.py`, imported from its file and
driven as it drives itself: the 635-node paragraph of `demo/sample.md`, every
glue's shrink set to zero (`_without_shrink`), ragged right (`_ragged` of
`PROSE_BREAKING` or of `LineBreakParams()`), cut before each interword glue
(`_word_prefixes`), the first `words` prefixes. `material` is the last of
them; prefix *k* is its nodes before the *k*-th glue. `prefix` is
`break_paragraph` of each (`_lines`) as words per line; `first_fit` the test's
independent greedy filler on the last; `flips` is `_flips`: the steps
compared, the most lines any prefix had, and the steps at which a line other
than the last changed — **none**, at 270 and 420 pt.

## 7. Decisions taken in writing this oracle

- **The block's text is defined here** (§1), because the reference has none
  and the product's API needs one. A code block's is the tab-expanded text,
  not its source: the reference's own text coordinates for code are that.
- **Tables are dropped** from `doc`, `deco` and `host` documents, as
  FORMAT-blocks.md does; cells have their own material and the product has no
  tables yet.
- **No metrics, no node lists** for blocks: `tests/golden/blocks` and
  `tests/golden/para` hold composition; these hold what is above it.
- **A stream's steps carry digests**, with full commands for one case, to
  stay within a few megabytes.
- **The whole document is "in view"** for decorations: culling is not what is
  recorded.

## 8. What is not in here

- Tables: cell separators (tab, newline), `map_table`, links and find hits in
  cells, live-table column holding in a stream.
- `carry_selection` / `carry_caret`: restating a selection across a relayout.
  R3's byte offsets replace them by construction.
- `BlockSource` / `SourceSpan` (which markdown lines a block came from) and
  `selected_source`: they are markdown's, and markdown is not ported (D34).
- A hosted block's drawing, `handle_keys`, hover and click handling of links,
  scrolling, `set_lines` as a record of its own (it is what `stable` compares).
- Culling: `visible_units`, `visible_rects` with a real viewport.
- A document wider than one measure, columns, a diff view.
- A text block whose material cannot be mapped (`SpanMapError`).
- Refused formulas and an unclosed `$$` as selection cases (a stream passes
  through both; only its block text and digests are recorded there).
