# advtext-map — result (builder-26)

Wrote `docs/advtext-migration.md` (seven sections plus "What was not read").
Nothing else written; nothing built or run; no git write; nothing touched in
any sibling tree.

## Recommendation

Build the host-neutral Document API first (R1 to R5 below). Then: the first
host calls it directly (option d, which the evidence adds); advtext's
non-layout parts (stream, diff, markdown-it rules, view shell) come along
afterwards as a **new thin layer in its own repository** (option a, not a
retrofit of the present facade); markdown in C++ (c) when a second language or
a measurement asks. Not (b).

Would change it: a measured binding cost that makes describing blocks from
Python expensive, or the markdown-it parse over pptmstr's 1.7 ms line once
layout is native (toward c); `bundle-research` concluding the core can only be
called from Python when built against the bundle's own ImGui (toward b);
pptmstr finding its own `BlockCursor` sufficient (toward d alone); any real
consumer of the present facade (against the shape).

## Premises that were wrong

1. `../pptmstr` imports nothing from advtext (searched; zero files). It has its
   own block segmenter, inline tokeniser and wrap. No consumer of the facade
   exists in the three trees.
2. The class is `_Converter`, not `_Builder` (`markdown.py:1011`). D20 has the
   wrong name.
3. `blocklayout.py` is a flex demo, not the document block layer.
4. `src/blocks.h`, `docs/bindings-notes.md` and a bundle-research result do not
   exist yet.
5. `include/imtypeset.h` on disk is already 0.2.0 and matches the rulings as
   read.

## Requirements for the Document API, in priority order

- R1 blocks the host describes: role, complete resolved style, quote depth on
  every kind, marker, text with runs + host tag, hard breaks, **explicit
  formula ranges**, verbatim code lines, rules. [part internal]
- R2 retained and spliceable: replace/insert/remove at an index; untouched
  blocks keep layout, identity and positions; in-place text update keeps
  identity; appending to the last code block costs the appended lines. [part]
- R3 positions are (block, byte offset), stable; hit test, selection by
  granularity, rectangles, caret, lines, block at point, block rectangle,
  across blocks. [per paragraph: public already]
- R4 one culled draw with selection, ink per block, ink ranges and fills at
  draw time, debug overlay; also to buffers. [internal: `Drawer::draw_runs`
  has every channel]
- R5 copy across blocks with stated, replaceable separators. [part]
- R6 tables (cells are run lists; widths chosen or stated; limits queryable;
  rows appendable). [new; no golden]
- R7 a block that reserves a rectangle. [new, trivial]
- R8 formulas on their own; supported commands; refusal kind. [internal]
- R9 font numbers. [internal]
- R10 a per-document layout report. [part]
- R11 flow into columns. [new; last]

Not recommended: public nodes/hpack/ship/commands, material indices, a
value-keyed cache, reading-order boundaries.

## Constructs that do not fit a block description

Tables; a formula box a host builds (`MathBuilder`); formula ranges the front
end already decided (the API rescans dollars); every public coordinate that is
a material index (`LinkSpan`, `FormulaSpan`, `ColourSpan`, `TextMatch`,
`code_position`, `material_text`); `ReplacedChar` as a node; code lines as
separately addressable values; `HostBlock`; a style carried by the block. And
outside markdown: `DiffView` builds `Glyph`/`Line` commands in Python and
calls `draw_runs`, which nothing in a block description produces.

## Oracle freeze

Tag `a3c589a` as `advtext-reference` (the four working-tree changes are docs
and one doc test; `advtext/` is unmodified). Freeze **all** of `advtext/`: the
oracle scripts import `advtext.ui`, `ui.units`, `ui.bare_view`, `markdown`,
`spans` and private names in them. Pin the scripts to the tag and refuse on a
dirty or different tree; stamp commit and package versions into golden
headers (none is recorded today); manifest of SHA-256s; goldens are the
durable record. **Deadline item:** dump goldens for tables, columns,
cross-block copy, hosts, decorations and stream stability before the reference
is edited.

## Least sure of

- Sub-agents read eleven files in full for me; I spot-checked about forty of
  their line citations and found none wrong. A citation outside those checks
  rests on their reading.
- "No consumer of the facade" covers three trees only.
- Section 1's "held equal" figures are builders' reports, not recounted.
- Whether the public API kerns across a run boundary of one face: not checked.
- No measurement of anything through a binding exists; every "after" figure in
  section 4 is the C++ core's own.

## Noticed, not asked

- `src/` line numbers moved under me (`src/math_mlist.h` by about fifty lines
  between two reads; another task is editing it). The document cites this
  repository by symbol.
- `tests/golden` grew by 583 lines while I worked (font-wild).
- The reference's docstring cites §766 for the Bin/Rel penalties
  (`math/mlist.py:89`); by counting section starts in `tex.web` the penalty is
  chosen in §761 and inserted in §767.
- The reference's fonts are the installed `imgui_bundle` wheel's asset files
  (`fontlib.py:272-277`): a different wheel can change font and draw goldens.
- `layout_units` and `compose_blocks` are two layout paths in the reference
  and already disagree (oracle-blocks result, item 1); only one has goldens.
