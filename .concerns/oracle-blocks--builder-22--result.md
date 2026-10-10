# oracle-blocks — result

## What exists

- `tools/oracle/dump_blocks.py` — imports `dump_core.py` and `dump_para.py`,
  edits neither.
- `tools/oracle/FORMAT-blocks.md` — the record vocabulary, over
  FORMAT-para.md and FORMAT.md.
- `tests/golden/blocks/*.golden` — 8 files, 69 cases, 260 blocks, 215
  `compose` groups (document × width), 4,017,649 bytes.

| file | cases | bytes |
|---|---|---|
| paragraphs | 12 | 744,927 |
| headings | 6 | 512,471 |
| lists | 9 | 536,547 |
| quotes | 11 | 420,087 |
| code | 8 | 210,084 |
| rules | 7 | 103,984 |
| edges | 14 | 201,372 |
| demo | 2 | 1,288,177 |

INPUT per case: the block list as data — per block its type, role, absolute
`left_indent`, `quote_depth`, `space_above`/`space_below`, interline triple,
strut, runs / marker (dump_para's records) or code source or rule thickness —
plus the five Theme values composition reads and every metrics answer.
EXPECTED per width: each block's boxes alone (`lines`: the `break` group,
`hung`, `strutted`), the stacking item by item (`stack`: runs, `gap` with its
two addends, the kern, `append` with prev_depth in/out, interline glue,
`ref`, and `barred` groups for quotes), `extents` (each block's top/bottom),
`vbox`, `origin`, `cmds`, and `units` (command ranges, index ranges and role
per block, for a draw that takes colours).

## What was run

- `dump_blocks.py`: twice, sha256 of all 8 files identical.
- `dump_blocks.py --verify`: 69 cases, 284 replayed checks ok. It rebuilds
  each case from INPUT records alone (Theme with **no** block styles,
  `list_indent` and `rule_thickness` zeroed, table-only metrics), rebuilds the
  document vlist from EXPECTED records with a reader that calls nothing in
  `advtext.ui`, then compares every record with the reference's.
- Four hand mutations of `edges.golden` (a gap amount, a prev_depth, an
  extent, a span's block): `--verify` failed on each. A fifth pattern did not
  match anything and proves nothing.
- At dump time the script asserts, per width, that the rebuilt blocks under
  the cut-down Theme compose to the same box as the reference's own blocks
  under its own Theme, and that the units shipped alone and moved tile the
  document's commands.
- Not run: any C++ build or test (none in this task). Nothing was written to
  `../imgui-adv-text` (its `git status` shows only doc/test edits that are not
  mine).

## How the reference combines space between blocks

`advtext/ui/__init__.py` `_stacked`, 1132–1173:

- **SUM, not max.** Between two runs: one `Kern` of
  `space_below(last block of the run above) + space_above(first block of the
  run below)` (1141). Omitted when the sum is 0 (1142); may be negative. No
  collapsing.
- **prev_depth is reset to IGNORE_DEPTH at every run** (1133, 1146), so there
  is never interline glue between blocks; the kern is the whole gap. Lines
  inside a block get interline glue under that block's `InterlineSkip`.
- **The gap is conditional on the list being non-empty** (`if vlist:`, 1140),
  not on "not the first block". First block's `space_above` and last block's
  `space_below` are never applied — also inside a quote, so a bar is exactly
  as tall as its text.
- A "run" is one block, or consecutive text blocks quoted deeper than the
  current depth, which become ONE hbox `[kern bar_x, rule, kern -(bar_x+t),
  vbox of the nested stack]` (`_barred`, 1238–1250).
- Rules and bars are `Rule` nodes → `rulerect` commands. No code-block
  background, no other fill channel anywhere in the reference.
- Ink is per unit (depth-0 run) by the first block's role, later blocks of a
  quote by index range; `rulerect`s take `Palette.rule`. Default palette has
  no roles.

## Prototype shortcuts noticed (candidates for D32.3; file:line in `../imgui-adv-text/advtext`)

Each is held by a golden as the reference does it.

1. `ui/__init__.py:1140` — gap gated on `if vlist:`. A block that sets no box
   (empty fenced code) at the start swallows its own `space_below` and the
   next block's `space_above`. `ui/units.py:2436` (`layout_units`) adds
   `run_gap` unconditionally, so **the reference's two layout paths disagree**:
   "```` ``` ```` (empty) + paragraph" is 1,065,664 sp tall through
   `compose_blocks` and 1,539,293 through `layout_units` (measured).
   `edges.golden` `empty-code-first`, `two-empty-code-blocks-then-text`.
2. `ui/__init__.py:1171` with 1140–1143 — a block that sets no box in the
   middle still contributes both its spaces: two kerns back to back around
   nothing (`edges` `empty-code-in-the-middle`).
3. `ui/__init__.py:1187` — only `TextBlock` has `quote_depth`; a code block,
   rule or table inside a quote reads as depth 0 and **cuts the bar** in two
   (the docstring at 1180–1185 admits it). `quotes` `bar-interrupted-by-code-and-rule`.
4. `ui/__init__.py:1239–1240` — `bar_x` is `min(indent) - quote_indent`
   whatever the nesting depth. A document quoted twice with nothing quoted
   once draws **both bars at the same x** (measured: both kerns 1,184,072);
   `quotes` `twice-quoted-alone`. A quote with `quote_depth` but no indent
   puts the bar under the text (`edges` `marker-on-a-paragraph-and-unquoted-quote-kind`).
5. `ui/__init__.py:1200` (`_runs`) — two separate quotes with nothing set
   between them merge into one run with one continuous bar through the gap
   (`quotes` `two-quotes-in-a-row`, `quote-in-a-quote`).
6. `ui/__init__.py:1494` — rule width is `width - left_indent` with no floor:
   negative when the column is narrower than the indent (box still `width`
   wide, nothing drawn). `rules` `rule-in-nested-lists` at 40 px.
7. `ui/__init__.py:1302` — code lines are `hpack` natural: never wrapped or
   clipped, and the document vbox grows to the longest line (42,231,831 sp
   wide in a 1,973,453 sp column, `code` at 40 px). Deliberate per
   `style.py:884–887`, but the document box no longer equals the measure and
   nothing reports the overflow.
8. `ui/__init__.py:1357` — `available = max(width - indent, UNITY)`: deep
   nesting in a narrow column sets every word on its own overfull 1 pt line
   rather than refusing or capping the indent.
9. `markdown.py:1285–1286` — a paragraph in a quote is kind `QUOTE` even
   when it is a list item inside the quote (marker font still the list's,
   1300). A quoted list's items are therefore 789,380 sp apart (quote
   0.5 em below + 0.5 em above) against 157,876 unquoted.
10. `style.py:870–881` — list items have `space_below` 0.2 em and no
    `space_above`; a second paragraph of an item is just another block of the
    item's kind (`markdown.py:1287–1288`). So: paragraphs within one item are
    spaced exactly like items; paragraph→list is 0.55 em but list→paragraph
    0.2 em; tight and loose lists are identical (`lists` `tight-then-loose`).
11. `markdown.py:1482–1487` — an item that emits no text block loses its
    marker: an empty item vanishes and an item holding only code is unmarked
    code (`lists` `empty-item-and-item-holding-only-code`: "1." is never set).
12. `ui/__init__.py:1468–1477` with `style.py:919` — the gutter is one fixed
    `list_indent` per level; a wider marker overfulls its box and overprints
    the text (`lists` `ordered-from-9-wide-markers`, "98765432.").
13. `markdown.py:1503` — a rule's `left_indent` is the enclosing indent with
    no style indent and (see 3) no bar.
14. `ui/units.py:443` — which block of a quote an item belongs to (for ink) is
    decided by bisecting baselines against block bottoms, not carried from
    composition; its docstring names the case it gets wrong (a first line of
    no height).
15. `markdown.py:262, 1509–1518` — every bullet is "•" and every ordered
    marker "N." (a `)` delimiter is lost). Front-end, noted for D32.4(iii).

Not a shortcut but a spec correction: **`advtext/blocklayout.py` is not the
document block layer.** It is a row/column UI demonstration; nothing in
`compose_blocks` calls it. The task text names it first.

## Not covered

- Tables (dropped from the demo documents), `HostBlock`, formulas, links.
- `LayoutCache`, `layout_units` / `UnitLayout`, incremental relayout.
- Breaking parameters other than `PROSE_BREAKING`; `ragged_total_fit`.
- `Faces.mono()`, non-default `Spacing`, `Theme.at_pixel_height`.
- `Theme.build` itself: how a body size becomes the per-kind spaces is INPUT
  here, not expectation. The `blocks` port is handed values.
- Selection / copy across blocks (separators between blocks, marker text).
- Right and centre alignment only on the two-paragraph case; the rest are
  justified (16 px) and ragged right (13 px).
- 144 px per inch on one case.
- No C++ reader for the new records exists; `tests/golden.h` reads the
  lexical layer, the `blocks` task writes the rest.

## Least sure of

- Size: 4.0 MB against "a few MB". Half of `demo.golden`'s 1.3 MB is
  `sample.md` at two widths; dropping to one width saves ~0.5 MB.
- `--verify` step 3 compares records re-emitted by the same emitter. Step 2
  is the independent reader, and it covers `lines`, `stack`, `vbox`, `cmds`
  and `units`, but for `break` it trusts the packed boxes and does not
  re-break (dump_para/dump_core's verify do that for one block).
- `gap`'s `list-empty` is determined by position (nothing in the list yet),
  which is the reference's condition read off its result, not traced.
