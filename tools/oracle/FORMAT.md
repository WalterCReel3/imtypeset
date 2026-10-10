# Core goldens: `tests/golden/core/*.golden`

Written by `tools/oracle/dump_core.py` from the Python reference
(`../imgui-adv-text/advtext/{units,badness,nodes,pack,linebreak,shipout}.py`).
Regenerate, from the repository root, with

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=../imgui-adv-text \
        ../imgui-adv-text/.venv/bin/python -B tools/oracle/dump_core.py

Two runs write identical bytes. `dump_core.py --verify` re-reads every file
with a reader that uses only what this document says, rebuilds the inputs from
the records, replays them through the reference and compares with the recorded
expectation; it is the check that this document is sufficient.

Sections 1 to 4 (lexical layer, enums, node records, draw-command records) are
shared: the draw, math and selection oracles reuse them verbatim.

## 1. Lexical layer

The base format `tests/golden.h` reads.

- UTF-8, `\n` line ends, no blank lines, no leading or trailing spaces, no tabs.
- A line starting with `#` is a comment.
- Every other line is one record: tokens separated by exactly one space. The
  first token is the record's tag.
- **Integer**: decimal, optional leading `-`. Every integer in these files
  fits a signed 64-bit integer; several do not fit 32 bits.
- **Float**: Python's `float.hex()`, e.g. `0x1.8000000000000p+1`, `0x0.0p+0`,
  `-0x0.0p+0`. `strtod` reads it back to the same double. `nan`, `inf` and
  `-inf` occur in one case only, `non-finite-and-huge` of `units.golden`.
- **Boolean**: `0` or `1`.
- **Enum**: the lower-case member name (section 2).
- **String**: UTF-8 in which every byte `<= 0x20` or `>= 0x7F`, and `%` and
  `#`, is written `%XX` (two upper-case hex digits). The empty string is the
  single character `%`. Font keys, case names and source text are strings.
- **Keyword**: a bare lower-case word fixed by this document (a tag, a case
  kind, a list name, a spec role).

A record that holds a variable number of items carries the count before them.

## 2. Enums

| type | tokens |
|---|---|
| GlueOrder | `normal` `fil` `fill` `filll` (ordered; numeric values 0..3) |
| GlueSign | `normal` `stretching` `shrinking` |
| KernKind | `normal` `explicit` `accent` |
| BoxKind | `hlist` `vlist` |
| PackMode | `exactly` `additional` |
| PackFitness | `exact` `loose` `underfull` `tight` `overfull` `infinite_glue` `empty` |
| BreakPass | `first` `second` `emergency` `first_fit` |
| break kind | `glue` `penalty` `kern` `math` `disc` `paragraph_end` |

## 3. Node records

One node is one record, except `box` and `disc`, which bracket their children.
`Scaled` values are integers in scaled points. A rule's RUNNING dimension is
written as its value, `-1073741824`.

    char <font:string> <codepoint:int>
    rchar <font:string> <codepoint:int> <original:int>
    rule <width> <height> <depth>
    glue <width> <stretch> <stretch_order> <shrink> <shrink_order>
    kern <width> <kind:KernKind>
    penalty <value>
    mathon <width>
    mathoff <width>
    box <kind:BoxKind> <width> <height> <depth> <shift_amount> <glue_set:float>
        <glue_sign> <glue_order> <n>          (one line)
      ... n node records, the box's contents, each possibly a box or disc ...
    endbox
    disc <replace_count> <npre> <npost>
      ... npre node records (pre_break), then npost node records (post_break) ...
    enddisc

`n`, `npre` and `npost` count direct children only. `endbox`/`enddisc` carry
no fields and are redundant with the counts; a reader may use either.

A **node list** is

    nodes <name:keyword> <n>
      ... n node records ...
    endnodes

`char` carries the font key exactly as the reference's `CharNode.font` holds
it. Hand-built cases use the made-up keys `fake@10` and `big@20`; font-built
cases use the reference's `face@size` keys (`prose@789381`: face value, `@`,
em size in scaled points). The core never interprets the key.

`rchar` is the reference's `markdown.ReplacedChar`: a `CharNode` subclass
whose `font` and `codepoint` are what is *set* (U+FFFD in the prose face, or
`?`) and whose `original` is the codepoint that was *written*. Everything in
the core treats it exactly as the `char` with the same font and codepoint — it
is measured, packed, broken and shipped as one, and ships as an ordinary
`glyph` — but it must survive, with `original` intact, through `hpack`,
`break_paragraph` and into the line boxes, because copy and find read it
there. It compares unequal to the plain `char` with the same two fields. It
occurs only in the `stand-in-characters*` cases of `paragraphs.golden`.

### Shared helper records

    spec <role:keyword> <width> <stretch> <stretch_order> <shrink> <shrink_order>

A `GlueSpec` in a named role: `baselineskip`, `lineskip`, `left_skip`,
`right_skip`, `par_fill_skip`.

    measure <font:string> <codepoint> <width> <height> <depth>

One `CharMeasure` answer (`CharDimens`). A case lists one `measure` record for
every distinct (font, codepoint) of every `char` in its inputs, including
those inside boxes and discretionaries, sorted by font then codepoint, before
the first node record. **A case with no `measure` record was run with
`measure=None`.** `CharDimens` has no italic correction and none is dumped.
Pair kerns are not listed separately: they are the `kern ... normal` nodes
already in the material.

    packed <fitness:PackFitness> <natural> <excess> <badness> <overfull_by>
    box ... endbox

A `PackResult`: the `packed` record, then its box as a node record.

    error <slug:keyword>

The reference raised `ValueError` where a result was expected. The port has no
exceptions; it must report the same condition by whatever means it defines.
Slugs: `char-without-measure`, `running-width-in-hlist`, `char-in-vlist`,
`running-height-or-depth-in-vlist`, `empty-parshape`.

## 4. Draw-command records

What `ship()` returns, in order.

    cmds <n>
      glyph <font:string> <codepoint> <x> <y> <index>
      rulerect <x> <y> <width> <height>
      setglue <x> <y> <width> <natural> <height> <depth> <index>
      atom <x> <y> <width> <natural> <height> <depth> <index>
      line <x> <y> <width> <height> <depth> <start> <end>
    endcmds

`atom` is the reference's `Atom`, a `SetGlue` subclass with the same seven
fields; it is a distinct tag here so the port can make it a distinct command
tag. `origin <x> <y>` is the `Point` handed to `ship`.

## 5. File and case structure

    golden <format-version:int> <file-id:string>      first record; version is 1
    case <name:string> <kind:keyword>
      ... records, by kind (section 6) ...
    endcase

Case names are unique within a file. The files:

| file | case kinds | what |
|---|---|---|
| `units.golden` | `table` | constants and every conversion of `units.py` |
| `badness.golden` | `table` | `badness(t, s)` |
| `nodes.golden` | `table` `gluetotal` `predicates` | constants, `GlueTotal`, break predicates |
| `pack.golden` | `hpack` `vpack` `interline` `append` | `pack.py` |
| `shipout.golden` | `table` `atom` `ship` | `pascal_round`, `is_atom`/`atom_box`, `ship` |
| `linebreak.golden` | `linebreak` `parshape` | hand-built paragraphs, both breakers |
| `paragraphs.golden` | `linebreak` | real text through the reference's composition path |

## 6. Case kinds

### `table`

Independent records; each is one call and its answer, input fields first.

    const <name> int <value:int>
    const <name> float <value:float>
    sp_from_pt <points:float> <result:int>
    sp_from_bp <big_points:float> <result:int>
    sp_from_px <pixels:float> <px_per_inch:float> <result:int>
    pt_from_sp <length:int> <result:float>
    px_from_sp <length:int> <px_per_inch:float> <result:float>
    round_to_px <length:int> <px_per_inch:float> <result:int>
    badness <t:int> <s:int> <result:int>
    badness_raises <t:int> <s:int>
    pascal_round <value:float> <result:int>
    raises <function:keyword> <exception:keyword> <argument>...
    beyond_int64 <function:keyword> <result:string> <argument>...

`raises` and `beyond_int64` occur only in the case `non-finite-and-huge` of
`units.golden`, which records what Python does with NaN, infinite and huge
inputs to the five conversions. `function` is one of the conversion tags above
and the arguments are that function's inputs in the same order and types.
`raises`: the reference raised `valueerror`, `overflowerror` or
`zerodivisionerror`. `beyond_int64`: the reference returned an integer outside
the signed 64-bit range, written in decimal as a string token. The same case
holds ordinary records whose float fields are `nan`, `inf` or `-inf`
(`px_from_sp` does not raise). The port cannot raise and has no unbounded
integers: these records enumerate the inputs for which it must define its own
answer, and they are the only records in the core goldens a port is not
expected to reproduce literally.

`badness_raises` lists inputs (all with `t < 0`) for which the reference
raises; the port must treat them as a contract violation.

Things a port must reproduce for these to match, none of them visible in a
record: `sp_from_*` and `round_to_px` round **half to even** (Python's
`round`); `pascal_round` rounds half **away from zero** and is computed as
`floor(v + 0.5)` for `v >= 0`, `ceil(v - 0.5)` otherwise, so
`0.49999999999999994` gives 1; the float expressions are evaluated in the
reference's order (`pixels * (72.27 / px_per_inch) * 65536`,
`length * (px_per_inch / 72.27) / 65536`), in doubles; an integer length is
converted to double first (some lengths here exceed 2^53).

### `gluetotal`

    contrib <amount> <order:GlueOrder>        zero or more, added in order
    total <normal> <fil> <fill> <filll> <dominant_order> <dominant>

### `predicates`

A node record followed by one result record, repeated:

    pred <is_discardable:bool> <permits_break_at_following_glue:bool>
    penalty_pred <forbids_break:bool> <forces_break:bool>      after a penalty
    rule_pred <is_running:bool>                                after a rule

### `hpack`, `vpack`

    measure*                                  (hpack only)
    nodes in <n> ... endnodes
    hpack <target> <mode:PackMode>
      or
    vpack <target> <mode:PackMode> <max_depth>
    packed ... box ... endbox    or    error <slug>

The result box's contents are the input nodes.

### `interline`

    spec baselineskip ...
    spec lineskip ...
    lineskiplimit <value>
    prev_depth <value>
    box ... endbox                            the box about to be appended
    none    or    glue ...                    interline_glue's answer

### `append`

    spec baselineskip ... / spec lineskip ... / lineskiplimit <value>
    prev_depth <start>
    nodes items <n> ... endnodes              boxes and rules, appended in order
    prev_depths <n> <d1> ... <dn>             append_to_vlist's return, per item
    nodes vlist <m> ... endnodes              the list afterwards

### `atom`

    box ... endbox                            a box
    is_atom <bool>
    box ... endbox                            atom_box(box)
    is_atom <bool>

### `ship`

    measure*
    box ... endbox                            the root box
    origin <x> <y>
    cmds ... endcmds    or    error <slug>

### `parshape`

    shape <n> <width1> <indent1> ... <widthn> <indentn>
    error empty-parshape
      or
    for_line <line> <width> <indent>          one or more; lines count from 1

### `linebreak`

    source <kind:keyword> <text:string>       optional, informational (below)
    theme ...                                 optional, informational (below)
    measure*
    nodes material <n> ... endnodes
    ... then, in any order, further node lists and runs:
    nodes unshrinkable <n> ... endnodes       always precedes the runs that use it
    break <run-name:string> <list:keyword>    which node list this run breaks
    shape <n> <width1> <indent1> ... <widthn> <indentn>
    params <pretolerance> <tolerance> <line_penalty> <adj_demerits>
           <double_hyphen_demerits> <final_hyphen_demerits> <hyphen_penalty>
           <ex_hyphen_penalty> <emergency_stretch> <first_fit:bool>   (one line)
    spec left_skip ...
    spec right_skip ...
    spec par_fill_skip ...
    paragraph <nlines> <total_demerits> <pass:BreakPass> <prepared_length>
    breaks <nlines> <position1> <kind1> ... <positionn> <kindn>
    packed ... box ... endbox                 nlines times: each line's PackResult
    [ship section]                            present in most runs
    endbreak

The call is `break_paragraph(list, ParShape(shape), LineBreakParams(params,
skips), measure=...)`.

**Break positions index the prepared list, not the input.** The reference
first rewrites the input (`_prepare`): every glue's shrink order is forced to
`normal`; then a trailing glue node is *replaced* by `penalty 10000`, otherwise
`penalty 10000` is appended; then `glue <par_fill_skip>` is appended.
`prepared_length` is the length of that list. A break at `position` with kind
`glue`/`penalty`/`kern`/`math`/`disc` is at that node of the prepared list;
the last break is always `<prepared_length> paragraph_end`. The positions are
read from the reference's private functions and every run asserts that they
reproduce the public result.

Each line's box carries its `shift_amount` (the `LineSpec` indent). The lines'
contents are what the reference built for that line, including the
`\leftskip`/`\rightskip` glue it added and what it pruned, so comparing them
node for node checks `_line_nodes`. The breaker's own four-way `Fitness` class
per line is not exposed by the reference and is not dumped; `total_demerits`
contains it.

**Ship section.** Checks pack + ship-out on the broken lines:

    spec baselineskip ...
    spec lineskip ...
    lineskiplimit <value>
    vbox <width> <height> <depth>
    origin <x> <y>
    cmds ... endcmds

To replay: start an empty vertical list with `prev_depth = IGNORE_DEPTH`;
`append_to_vlist` each line box in order with that `InterlineSkip`; `vpack` the
list at natural size (target 0, `additional`, default `max_depth`); the result
has the dimensions in `vbox`; `ship` it at `origin`, which is
`(0, vbox.height)`, with the case's measure. No strut is applied: that belongs
to the style layer and is not core behaviour.

**`source` and `theme`** (in `paragraphs.golden` only) say where the material
came from, for the style port; the core tests ignore them.

    source text <text>                        markdown.text_block(text, theme, fontlib_metrics, ...)
    source text-hard-breaks <text>            the same with hard_breaks=True
    source markdown <text>                    the one TextBlock of build_document(text, ...)
    source markdown-math <text>               the same with math=markdown_math()
    source markdown-block-of-demo/sample.md % a block of the reference's demo file
    theme <px_per_inch:float> <spacing> <block_kind> <align> <font_key:string>
          <left_indent> <first_line_indent>   (one line)

`spacing` is `nonfrench`, `extra_space_only` or `french`; `align` is `justify`,
`left`, `right` or `center`; `block_kind` is the lower-case `BlockKind` name.
The list `material` is the block's material as built. Runs named `ui-<W>px`
are what `advtext.ui._text_boxes` hands the breaker at a column of W pixels:
`_skips(align, PROSE_BREAKING)`, `_shape(block, width)`, and, for any alignment
but `justify`, the list `unshrinkable` (`_unshrinkable(material)`: every glue's
shrink zeroed) with `first_fit` set. Other runs hand `material` to the breaker
with the parameters recorded.

## 7. What is not in here

- Integer inputs outside 64 bits, and layout (pack, break, ship) on
  dimensions whose sums leave 64 bits. Python integers are unbounded and the
  reference carries on; only the unit conversions' behaviour there is recorded
  (`non-finite-and-huge`).
- The breaker's intermediate state (active lists, per-candidate demerits).
- `Strut`, markers hung on first lines, and everything else `advtext.ui` does
  after `break_paragraph` returns.
