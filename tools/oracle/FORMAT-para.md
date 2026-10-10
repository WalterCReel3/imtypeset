# Paragraph goldens: `tests/golden/para/*.golden`

Written by `tools/oracle/dump_para.py` from the Python reference: what turns
styled text, a width and an alignment into the line boxes of one paragraph and
the commands that draw them. In the reference that is spread over three
modules, none of which is ported whole (D20):

| step | reference |
|---|---|
| text → horizontal material | `advtext/markdown.py`: `_Converter._append_text`, `_char_node`, `_stand_in`, `ReplacedChar`, `_unskip`, `_fill_and_break`, `_Converter._text_block`, `text_block` |
| space factor, interword glue, strut | `advtext/style.py`: `space_factor_after`, `interword_glue`, `char_measure`, `Strut` |
| skips, shape, first-fit choice, marker, stacking | `advtext/ui/__init__.py`: `RAGGED_RIGHT`, `PROSE_BREAKING`, `_skips`, `_shape`, `_text_boxes`, `_unshrinkable`, `_hang_marker`, `_block_boxes`, `_stacked`, `compose_blocks` |

Regenerate, from the repository root, with

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=../imgui-adv-text \
        ../imgui-adv-text/.venv/bin/python -B tools/oracle/dump_para.py

Two runs write identical bytes. `dump_para.py --verify` re-reads every file
with a reader that uses only this document and `FORMAT.md`, rebuilds each case
**from its records alone** — a `Theme` holding only the recorded values and
zero everywhere else, a metrics resolver that answers only from the recorded
tables and fails on any other question — replays it through the reference and
compares. It is the check that the records are sufficient: a port needs no
`Theme` and, for these cases, no font reader.

The lexical layer, the enums, the node records, `spec`, `measure`, `packed`,
the draw-command records and the whole `break … endbreak` group are
[FORMAT.md](FORMAT.md)'s, sections 1 to 4 and 6 (`linebreak`), written by the
same functions. The `has`, `kern`, `strut_filling` and `unicode.golden`
records are [FORMAT-math.md](FORMAT-math.md)'s, unchanged. Only what is new is
described here.

Every expectation is an answer of one of the reference's own functions,
called directly. The script re-implements none of their logic.

## 1. Enums added

| type | tokens |
|---|---|
| Spacing | `nonfrench` `french` `extra_space_only` |
| Align | `justify` `left` `center` `right` |
| BlockKind | `heading_1` … `heading_6` `paragraph` `bullet_item` `ordered_item` `quote` |
| source kind | `text` `text-hard-breaks` `runs` `markdown-block-of-demo/sample.md` |

A face is written as its `Face` value, a string: `prose`, `prose-bold`,
`prose-italic`, `prose-bold-italic`, `code`, `code-bold`, `math`. A font key
is `face@size`, the size in scaled points (`prose@789381`).

## 2. Files

    golden 1 para/<name>

| file | case kinds | what |
|---|---|---|
| `tables.golden` | `table` `standin` | constants; `_skips`, `_fill_and_break`, `_shape`, `_unshrinkable`, `Strut`, `_stand_in`, one call per record group |
| `unicode.golden` | `ranges` | `str.isspace`, `str.isupper`, categories Cc Cf Mn Me, whole Unicode range |
| `edge.golden` | `para` | empty text, one character, one line of `xxx`, a word wider than the column, punctuation only, a column at exactly the natural width ± 1 sp |
| `whitespace.golden` | `para` | every kind of white space, as text and as a raw run; hard breaks |
| `standin.golden` | `para` | characters the run's face lacks |
| `runs.golden` | `para` | several faces and sizes in one paragraph, run boundaries, markers, indents |
| `prose.golden` | `para` | plain text through `text_block`; the space-factor sentences under each `Spacing`; headings; a monospace theme; 144 px per inch |
| `sample.golden` | `para` | four paragraphs of `demo/sample.md` × 4 alignments × 2 sizes × 5 widths |

`unicode.golden` holds the same records as `tests/golden/math/unicode.golden`
for the six predicates paragraph assembly reads (FORMAT-math.md §8); the
script asserts the two files agree. Unicode 14.0.0, with the Unicode notice in
the file's header.

## 3. The `para` case

One block of text, set at several widths.

    case <name:string> para
    source <kind> <text:string>
    block <px_per_inch:float> <spacing:Spacing> <align:Align> <kind:BlockKind>
          <font:string> <left_indent> <first_line_indent> <marker_gap>     (one line)
    faces <prose:string> <math:string> <code:string>
    spec baselineskip ...
    spec lineskip ...
    lineskiplimit <value>
    strut <height> <depth>
    runs <n>
      run <font:string> <breakable:bool> <text:string>
      hardbreak
    endruns
    marker <font:string> <text:string>              only with a list marker
    space_width <font:string> <value:float>         zero or more
    has <font:string> <codepoint> <bool>            zero or more
    kern <font:string> <codepoint> <following> <value:float>   zero or more
    measure <font:string> <codepoint> <width> <height> <depth> zero or more
    nodes material <n> ... endnodes
    nodes marker <n> ... endnodes                   only with a list marker
    ... then, for each width, in order:
    nodes unshrinkable <n> ... endnodes             once, before the first run that uses it
    break <run-name:string> <list:keyword> ... endbreak        FORMAT.md §6, no ship section
    assemble <run-name:string> <width> ... endassemble         §3.5 below
    endcase

### 3.1 INPUT: the values read from the Theme

`block` and the four records after it are **every value the composition path
reads from the reference's `Theme` and `BlockStyle`** for this block; a port
needs nothing else, and `--verify` holds that.

- `px_per_inch` is not read by layout. It is what turned the column's pixel
  width into scaled points (`sp_from_px`) and is here so a run name such as
  `ui-240px` can be checked against its `assemble` width.
- `spacing` is `Theme.spacing`, handed to `interword_glue`.
- `align` is the block style's.
- `kind` and `font` (the block style's own font key) are informational: the
  runs carry their fonts. `kind` only decides which style the reference looked
  up.
- `left_indent` is `TextBlock.left_indent`: what encloses the block plus the
  style's own, already added. `first_line_indent` is the style's. Both are in
  scaled points and feed `_shape`.
- `marker_gap` is `Theme.marker_gap`, read only when there is a marker.
- `faces` is the order `_stand_in` searches: the theme's prose face
  (`Faces.regular`), `Face.MATH`, the theme's code face. A face that appears
  twice is consulted once, at its first position. The first is also the face
  U+FFFD is set in.
- `spec baselineskip`, `spec lineskip`, `lineskiplimit`: the block style's
  `InterlineSkip`, used between the lines.
- `strut`: `Strut.filling(baselineskip.width)` — height
  `round(leading * 0.75)` half-to-even, depth the remainder.

The breaking parameters are `advtext.ui.PROSE_BREAKING` in every case
(tolerance 1000, emergency stretch 20 pt; `tables.golden` has all of it), and
each `break` group repeats what was actually handed to the breaker.

### 3.2 INPUT: runs

    run <font> <breakable> <text>

One call of `_Converter._append_text(run, text, font, breakable)`, in order,
all appending to the same material and sharing one space factor, which starts
at 1000. `breakable` is 0 for an inline code span. `text` is exactly what the
call received: nothing has been stripped from it unless the `source` says so.

    hardbreak

What markdown's inline walk does at a hard break: `_unskip` (drop trailing
glue from the material so far, and with each glue an `INF_PENALTY` that
immediately precedes it), then append `_fill_and_break(align)` — for `justify`
and `left`: `penalty 10000`, `glue 0 65536 fil 0 normal`, `penalty -10000`;
for `right` and `center`: `penalty -10000` alone.

    marker <font> <text>

A list marker: `_append_text` on a material and a space factor **of its own**,
unbreakable. Its nodes are the list `marker`.

`source` says where the runs came from:

- `runs`: the script wrote them; the text field is `%`.
- `text`: `markdown.text_block(text, theme, resolve, kind=, marker=, indent=)`.
  The reference turns `\r\n` and a lone `\r` into `\n`, strips white space
  (`str.strip()`, so `str.isspace()`) from both ends, and makes **one** run in
  the block's font. The `run` record is that call as the reference made it,
  captured by wrapping `_append_text` in memory.
- `text-hard-breaks`: the same with `hard_breaks=True`: after the strip the
  text is split at `\n`, each piece stripped, one run per piece (an empty
  piece is still a run, with text `%`) and a `hardbreak` between consecutive
  pieces.
- `markdown-block-of-demo/sample.md`: a paragraph of the reference's demo, as
  `markdown.build_document` built it. The runs are the `_append_text` calls
  the reference made for that block — a soft line end is a run of one space —
  and the text field is `%`. Markdown parsing is not ported and is not an
  input.

For every case with a `text*` or markdown source the script asserts that the
direct driver, fed the recorded runs, rebuilds the reference's block exactly.

### 3.3 INPUT: metrics

What the reference asked of `advtext.style.fontlib_metrics` while building the
material, captured by a recording resolver, sorted by key:

- `space_width`: `FontMetrics.space_width` of a run's font, a float.
  `_append_text` rounds it half-to-even once per run.
- `has`: every `FontMetrics.has(codepoint)` asked, of the run's own face and
  of the faces `_stand_in` tried. A codepoint or face with no record was not
  asked; a port that asks is not following the reference.
- `kern`: every `FontMetrics.kern(codepoint, following)` asked, zero answers
  included, a float; the node is `round()` of it half-to-even and is omitted
  when that is zero.
- `measure`: `char_measure(fontlib_metrics)` for every `char`/`rchar` in
  `material` and `marker` (FORMAT.md §3).

### 3.4 EXPECTED: material, then one `break` group per width

`nodes material` is `TextBlock.material`: the nodes the runs produced,
preceded by `box hlist <first_line_indent> 0 0 …` with no contents when
`first_line_indent` is not zero. A character set from another face is a
`char` with that face's key; one replaced by U+FFFD is an `rchar` with the
written codepoint as `original`.

`nodes unshrinkable` is `_unshrinkable(material)` and appears when `align` is
not `justify`.

The `break` group (FORMAT.md §6) holds (2) and (3a) of what a port must
reproduce: the `shape` from `_shape(block, width)`; the `params` and the three
skips from `_skips(align, PROSE_BREAKING)`, with `first_fit` set and the
`unshrinkable` list named for any alignment but `justify`; then the paragraph,
its break positions and each line as a `packed` record with its box. The
script asserts these boxes are what `advtext.ui._text_boxes` returns.

### 3.5 EXPECTED: `assemble`

What `advtext.ui` does once the breaker has returned, for the same width.

    assemble <run-name:string> <width>
    hung                                  only with a marker
    box ... endbox
    strutted <nlines>
      dims <width> <height> <depth> <shift_amount>      nlines times
    vlist <m>
      ref <i>                             the i-th strutted line box, from 0
      glue ... / kern ...                 a node record
    endvlist
    vbox <width> <height> <depth>
    origin <x> <y>
    cmds ... endcmds
    endassemble

- `width` is the column width in scaled points handed to `_shape`.
- `hung` is the first line after `_hang_marker`: `hpack` at natural width of
  [a box exactly `left_indent` wide holding `glue 0 65536 fil 0 normal`, the
  marker's nodes and `kern <marker_gap> normal`, packed `exactly`; the first
  line with its `shift_amount` set to 0]. The other lines are the `packed`
  boxes unchanged.
- `strutted`: `_block_boxes`, every line set on the strut. A line box differs
  from the box it was made from (the `packed` box, or `hung` for line 0) in
  `height = max(height, strut.height)` and `depth = max(depth, strut.depth)`
  and in nothing else, so only the four numbers are written; the script
  asserts the rest is identical.
- `vlist`: `_stacked` over the one block — the strutted boxes appended in
  order with `append_to_vlist` from `prev_depth = IGNORE_DEPTH` under the
  block's `InterlineSkip`. Between two lines no taller than the strut the
  interline glue is `glue 0 0 normal 0 normal`: present, and zero.
- `vbox`: `compose_blocks` over the one block, which is `vpack` of that list
  at its natural size. `origin` is `(0, vbox.height)`.
- `cmds`: `ship(vbox, origin, measure)`.

A block's `space_above` and `space_below` are between blocks and are not here.

## 4. `tables.golden`

### `table` cases

`constants`:

    const <name> float <value>     space_stretch_fraction, space_shrink_fraction,
                                   extra_space_fraction, strut_height_fraction
    const <name> int <value>       neutral_space_factor, sf_uppercase, sf_sentence_end,
                                   sf_transparent, extra_space_threshold,
                                   replacement_character, inf_penalty, eject_penalty, unity
    const sentence_ending string <text>      the characters whose \sfcode is 3000
    const transparent string <text>          the characters whose \sfcode is 0
    spec ragged_right ...                    advtext.ui.RAGGED_RIGHT
    spec hard_break_fill ...                 the glue in _fill_and_break
    params ...                               PROSE_BREAKING, as in FORMAT.md §6

`hard_break_fill` has a stretch of 65536 `fil`; the breaker's own default
`par_fill_skip` has a stretch of 1 `fil`. Both are the reference's.

`skips`, per alignment — `_skips(align, PROSE_BREAKING)` and whether
`_text_boxes` breaks it first-fit on unshrinkable material:

    skips <align> <first_fit_and_unshrinkable:bool>
    spec left_skip ... / spec right_skip ... / spec par_fill_skip ...

`fill-and-break`, per alignment:

    fill_and_break <align>
    nodes fill <n> ... endnodes

`shape` — `_shape` for a block with the given indents at a column `width`:

    shape_for <width> <left_indent> <first_line_indent>
    shape <n> <width1> <indent1> ...                     FORMAT.md §6

`available = max(width - left_indent, 65536)`; with no first-line indent one
entry `(available, left_indent)`; otherwise two,
`(max(available - first, 65536), left_indent + first)` and
`(available, left_indent)`. Negative indents are included.

`unshrinkable`: `nodes in`, then `nodes out` = `_unshrinkable(in)`. Every
top-level glue with a non-zero shrink gets shrink 0 of order `normal`; glue
inside a box is not reached.

`strut`:

    strut_filling <leading> <height> <depth>
    strut_applied <strut_height> <strut_depth> <box_height> <box_depth> <height> <depth>

### `standin` cases

    faces <prose> <math> <code>
    has <font> <codepoint> <bool>          every answer the calls below read
    stand_in <font:string> <codepoint> <set_font:string> <shown>

`markdown._stand_in(theme, resolve, font, codepoint)` for a codepoint the face
of `font` lacks, in three themes: the default, `Faces.mono()`, and one with
the math face as prose and the prose face as code. `shown != codepoint` is an
`rchar`. White space and categories Cc, Cf, Mn, Me come back unchanged
(`set_font == font`, `shown == codepoint`) without any face being asked.

## 5. Things a port must reproduce that no single record shows

- **White space is `str.isspace()`**, including U+00A0, U+2000–200A, U+2028,
  U+2029, U+202F, U+205F, U+3000, U+0085 and 0x1C–0x1F. A no-break space is
  interword glue and a legal breakpoint. U+200B, U+2060 and U+FEFF are not
  white space.
- A run of white space is one glue. White space at the start of a run whose
  material is still empty is dropped; that test is on the material, so a
  marker (which has its own) and a first-line indent box (added afterwards)
  do not count. White space ending one run and white space opening the next
  are **two** glues.
- In an unbreakable run each glue is preceded by `penalty 10000`.
- The glue is `interword_glue(round(space_width), space_factor, spacing)` with
  the factor as it stands when the white space is met. The factor is updated
  after every non-space character, stood-in ones included, from the character
  **written**, and carries across runs.
- A kern is looked up only after a character its own face has, only against
  the next character of the **same run**, only if that is not white space; in
  the run's face, even when the next character will be stood in for. Nothing
  is looked up after a stood-in character or across a run boundary.
- A stood-in character is set at the size of the run it is in, not the
  block's.
- The math face answers `has()` for the plane-15 codepoints the reference
  mints for its variant glyphs, so U+FFFFD written in prose is set from the
  math face (`standin.golden`, `unassigned-and-private`).
- **A first-line indent is applied twice**: as an empty box at the head of the
  material and again as the first entry of the shape, which narrows the first
  line by it and shifts it right by it. The first line's text therefore starts
  `left_indent + 2 × first_line_indent` from the column's edge. No theme the
  reference builds has a first-line indent; `runs.golden`'s `first-line-indent-*`
  cases record what happens when one does. See `docs/divergences.md`.
- An empty paragraph is one line: the breaker's terminator alone, on the
  strut.

## 6. Decisions taken in writing this oracle

- **Markdown is an input nowhere.** `sample.golden` records the reference's
  own `_append_text` calls instead of markdown source, because the port has no
  markdown parser (D1) and the runs are what its paragraph layer is handed.
- **The line boxes are written once.** The strutted boxes are four numbers
  each and the vertical list refers to them by index, where the reference's
  values repeat the full boxes three times; the script asserts the parts left
  out are identical.
- **Metrics are recorded per case** although a port has its own font reader
  (`tests/golden/font/` checks that), so that a failure in paragraph assembly
  cannot be a font-reader failure, and so that a lookup the reference never
  made is visible.
- **`unicode.golden` repeats six tables of `tests/golden/math/unicode.golden`**
  so this directory stands alone; agreement is asserted at dump time.
- **Runs at a size other than the block's** (`runs.golden`, `mixed-sizes-*`,
  `line-taller-than-the-leading-*`) are not something the reference's own
  callers produce, but `_append_text` takes a font per run and the task's
  input is "face, size per run". They are also the only cases here where a
  line is taller than the strut and the interline glue is not zero, or falls
  back to the lineskip.

## 7. What is not in here

- Markdown, links, formulas (`math_inline`), quote bars, code blocks, rules
  and tables.
- `ComposeParams(ragged_total_fit=True)`, the reference's switch back to
  total-fit for ragged text; and any breaking parameters but `PROSE_BREAKING`
  (`tests/golden/core/paragraphs.golden` has those).
- More than one block: `space_above`/`space_below` kerns, and `prev_depth`
  being reset between blocks.
- `Theme.at_pixel_height`, `Theme.build` and `Theme.font_for`: how a host's
  choices become the values in `block` and the fonts of the runs.
- A resolver that raises for a face (`_stand_in` skips that face), and a font
  key that does not parse (`_stand_in` leaves the character alone).
- Text that is not valid UTF-8, and lone surrogates: Python strings hold
  codepoints, and what a port does with malformed bytes is its own decision.
