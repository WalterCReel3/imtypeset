# oracle-doc-extras: result

All four groups and the manifest are done. Nothing was written into
`../imgui-adv-text`; its `git status --short` has the same four entries before
and after. No oracle script but the new one was edited.

## What exists

| path | what |
|---|---|
| `tools/oracle/dump_doc_extras.py` | the dump, `--verify`, `--only <groups>` |
| `tools/oracle/FORMAT-doc.md` | the record vocabulary, the rules no record shows, decisions, what is not covered |
| `tests/golden/doc/selection.golden` | group 1, 7 cases / 10 layouts, 797 KB |
| `tests/golden/doc/decorations.golden` | group 2, 3 cases / 4 layouts, 151 KB |
| `tests/golden/doc/hosts.golden` | group 3, 7 cases / 8 layouts, 93 KB |
| `tests/golden/doc/stream.golden` | group 4, 9 stream cases + 3 `flips` cases, 1.40 MB |
| `tools/oracle/manifest.py` | writes the two files below; `--check` for the gate |
| `tests/golden/MANIFEST.sha256` | 157 files, sorted |
| `tests/golden/PROVENANCE.md` | reference commit `a3c589a1`, 4 dirty files, Python 3.11.2, imgui_bundle 1.92.900, fontTools 4.66.0, markdown-it-py 4.2.0, font hashes (ours and the reference's seven roles: identical files), date, command and REFERENCE_PATCHES per directory (none anywhere) |

`tests/golden/doc` is 2.4 MB.

Per group:

1. **Selection/copy across blocks.** Documents: a mixed one (heading with a
   link, paragraph with emphasis / inline code / inline formula / wrapping link
   / hard break, display formula, bullets with a second paragraph and nested
   numbered items, a two-paragraph quote, code with a blank line, a rule, a
   replaced character) at 16 px justified (240, 400 px) and 13 px ragged; and
   `dump_blocks.py`'s `TWO_PARAGRAPHS`, `NESTED`, `CODE`, `HEADINGS`, and
   `demo/sample.md` less its table. Per layout: units, each block's index range
   and rectangle, the document's commands, **every reading-order index as (byte
   offset, byte length) in its block's text** (`imap`), the separators / formula
   sources / replaced characters a copy writes with index AND (block, byte),
   the per-block lead and indent, 1167 drags at character/word/line across every
   block boundary and the gap, 478 probes (`hit`, `block_at`, `word_at`,
   `line_at`), 556 ranges each with touched blocks, the exact copied string and
   the rectangles.
2. **Decorations.** Seven searches (plain, ignore-case, across a source line,
   across a run boundary that is also a set-line boundary at 240 px, inside a
   link, in code, absent), four syntax inks by (line, column, length), text inks
   with a mark and across a hard break, links styled and unstyled, role inks
   incl. a heading inside a quote. Recorded: `FindIndex.find` matches with
   `span_rects`, the spans handed to `set_colours`, `_inked` runs, `_decorate`'s
   inks and fills, `_role_inks`, `_overlaid` (= `draw_runs`' `inks`), link areas
   with runs and rects, then every `add_rect_filled` and the colour of every
   `add_text` that `draw_runs` issued (through `dump_draw.py`'s recorder).
3. **Host blocks.** `BareView(host_for=…)` claiming `diff` fences: between
   paragraphs, two near each other, inside a list item, first, last, alone,
   height 0, height 0 first, and not hosted at all; each resized where it says.
   Recorded: what the widget was asked and given, `_host_box`, units and
   extents, `run_gap` per unit, `compose_blocks` against `layout_units`,
   separators (where the widget's text goes), `edge_hosts`, `copy_text` with
   and without `everything`, select-all, ranges with copy, probes in the
   rectangle, `LayoutStats.built` after a resize.
4. **Streaming.** `demo/sample.md` less its table plus a paragraph with an
   inline formula and a `$$` block: by line (keep-streamed, reset, ragged
   theme), every 7 chars, every 64, one chunk, and 17 chosen cuts (mid-word,
   inside `$…$`, on/inside the `$$` lines, inside the opening fence, the code,
   the closing fence — this case with full commands); `sample.md` whole by
   line; the mixed document 3 chars at a time. Per step: revision, committed /
   live counts, every block that changed (state, kind, align, text),
   `LayoutStats.built` and `blocks_broken`, every unit whose row changed with an
   FNV-1a digest of its commands, `stable` (the property, checked: 0 moved in
   all 1 472 steps), `settled` for the live block. Plus the reference's own
   `tests/test_stream_stability.py` `_flips` at 270 pt and 420 pt prose and
   270 pt plain, 45 words: material, words per line per prefix, the independent
   first-fit, 0 flips.

## What was run

- `dump_doc_extras.py` twice: byte-identical (`sha256sum -c`).
- `dump_doc_extras.py --verify`: selection 7 cases / 15 763 records,
  decorations 3 / 3 153, hosts 7 / 2 418, stream 12 / 28 735, all re-derived
  and equal. About 1 m 45 s; the dump about 1 m 30 s (the `flips` cases are
  most of it).
- `manifest.py` twice (second run "wrote nothing"); `manifest.py --check` → 157
  match, exit 0; then with one hash altered, one line removed and one stray
  file added → `DIFFERS`, `UNLISTED` ×2 named, exit 1; restored → match.
- Not run: any C++ build or test. **No C++ test reads `tests/golden/doc`**;
  that is `api-document`'s (or a follow-up's) to write.

## Decisions taken (recorded in FORMAT-doc.md §1, §7)

- **A block's text is defined by the script**, since the reference keeps no
  source offsets (`advtext/spans.py:101`): the concatenation of what the
  converter was handed (`_append_text` texts, a set formula as `$source$`, a
  hard break as `\n`); a code block's is the tab-expanded
  `material_text(block).text`. Every character node is asserted against the
  character at its recorded byte.
- Tables dropped from groups 1–3, as in the block goldens.
- No metrics tables or node lists (precedent: `dump_para_map.py`).
- Stream steps carry digests; one case carries full commands.
- `tables` → `dump_tables.py` was added to `manifest.py`'s directory table when
  that directory appeared mid-session.

## Prototype shortcuts noticed (file:line in `../imgui-adv-text/advtext`)

- `spans.py:101` — no source offsets at all; every decoration and find result
  is in material indices, which count kerns and penalties.
- `spans.py:938` — `material_text` reads "glue of zero natural width" as a
  newline; a reading of this converter's output, not a property of glue.
- `spans.py:1093` — case-insensitive find silently becomes case-sensitive for
  a block whose case-folded text has another length.
- `spans.py:1377` — marks are neither merged nor ordered: two searches hitting
  the same word give two translucent rectangles drawn over each other
  (`fills` in `decorations.golden` shows it; the alpha doubles).
- `ui/units.py:405` — a unit's item count is the largest `Line.end`, not a
  count of items.
- `ui/units.py:443` — which block of a quote an item belongs to is decided by
  comparing its baseline y with block bottoms (geometry, not structure).
- `ui/units.py:488` — the space after a list marker is placed at
  `start + 1 + index_count(marker)`, assuming `\hss` then the marker's glyphs.
- `ui/units.py:495` — a code block's newlines are found by scanning the whole
  unit's commands for `Line`s.
- `ui/units.py:645` (and the caveat at `:638`) — list depth is recovered by
  dividing `left_indent` by the theme's `list_indent`; ambiguous when
  `quote_indent` is a multiple of it.
- `ui/units.py:556` (also `_after_hard_breaks`, `_originals_within`) — a block
  that cannot be mapped is swallowed: formulas copy as nothing, hard breaks as
  spaces.
- `ui/units.py:1230` — a host block at a document edge is copied only under an
  `everything` flag the view keeps as state (`ui/bare_view.py:913`), because a
  host block has no index for a range to cover.
- `ui/bare_view.py:1824` — a link's underline thickness and offset are the
  theme's **table** rule thickness, reused.
- `ui/bare_view.py:1232` — a block's rectangle is the full measure wide; no
  per-block x extent exists.
- `ui/bare_view.py:746` — the widget is asked for its height at
  `px_from_sp(width - indent)`, which for a 240 px measure is
  `0x1.e000008805f38p+7`, not 240.
- `ui/__init__.py:1534`, `:1575` — a host block is a `CodeBlock` subclass with
  no lines, set as a vbox of one kern; it keeps the code style's gaps and
  `left_indent`. A document that is only a widget composes to a box of width 0.
- `markdown.py:1217` — a display formula's source has its white space
  collapsed before anything keeps it; the original spelling is gone.
- `stream.py:607`, `:113` — the live tail is always ragged; and settled lines
  of the block being written do move when markup closes (6 `settled … 0` rows
  across the stream cases).

## Not covered

- Tables in any group (cell tabs, `map_table`, links/finds in cells, held
  columns while streaming).
- `carry_selection` / `carry_caret`; `BlockSource` / `SourceSpan` and
  `selected_source`; culling with a real viewport; link hover/click; drawing of
  a hosted widget; a block that raises `SpanMapError`.
- Refused formulas and an unclosed `$$` as selection/decoration cases (streams
  pass through both, recorded as block text and digests only).
- `--verify` re-runs the reference from the file's INPUT rows and questions and
  compares; unlike `dump_blocks.py --verify` it has no second reader that
  rebuilds results without the reference.
- The manifest is stale as soon as another builder regenerates a directory;
  PROVENANCE.md says the last gate rewrites it.
