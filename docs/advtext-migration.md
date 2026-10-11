# advtext on imtypeset: a migration map

Research for a shape the user is weighing (2026-10-09, **not a decision**):
imtypeset is the C++ typesetting core; advtext (`../imgui-adv-text`) stops being
a prototype to discard and becomes the Python library that wraps it, keeping
markdown, streaming, diff, find, the embeddable views and themes, with its own
layout core replaced by calls into imtypeset.

**Read with what came after it (note added 2026-10-10).** This is the map as
it was drawn on 2026-10-09. Since then the shape was decided (D34), the
Document of section 3 was built (D43; which of R1 to R11 are met, and by which
test, is the table in `.concerns/api-document--builder--result.md`: R1 to R5,
R7 and R10 met, R6 tables reserved and not built, R8, R9 and R11 not part of
it), and the plan that replaces section 7's recommendation, measured in both
applications, is `docs/advtext-rework-plan.md`. The text below is unchanged.

Nothing here is implemented, and nothing here changes `docs/decisions.md`. D32
still stands as written: imtypeset is not designed as an accelerator under
advtext. Section 3 is therefore stated in host-neutral terms, and the
requirements that would exist *only* to keep advtext's present types alive are
listed separately and not recommended.

**How to read the citations.** A bare `file.py:N` is a line of
`../imgui-adv-text/advtext/file.py` at commit `a3c589a` (the working tree's
`advtext/` is unmodified; section 5). `pptmstr:` prefixes a path under
`../pptmstr`. Paths with `src/`, `include/`, `docs/`, `tests/`, `tools/` are
this repository's, and its sources are cited **by symbol, not by line**:
several of them were being edited by other tasks while this was written, and
line numbers in `src/math_mlist.h` moved by fifty between two reads. Line
counts are `wc -l`.

**What was read, and how.** Stated in full in the last section. In short: the
facade, the import graph, the block types, the draw entry, the unit cache and
every line cited below were opened by the author; `markdown.py`, `spans.py`,
`stream.py`, `columns.py`, `blocklayout.py`, `selection.py`, `ui/__init__.py`,
`ui/units.py`, `ui/bare_view.py`, `ui/stream_view.py` and
`backends/imgui_draw.py` were read in full by three read-only sub-agents whose
line citations the author then spot-checked (about forty of them, none found
wrong). `ui/diff_view.py`, `ui/document_view.py` and `diff.py` were read in
part. The demo (`ui/app.py`, `ui/pages/*`) was classified from its imports only.

**Five things the task's premises got wrong, found on the way.**

1. **`../pptmstr` imports nothing from advtext.** A case-insensitive search for
   `advtext`, `imgui-adv-text` and `adv_text` over the whole tree outside
   `.venv` and `.git` finds no file. Its dependencies are `imgui-bundle`,
   `claude-agent-sdk`, `anthropic` and `markdown-it-py`
   (`pptmstr:pyproject.toml:10-41`), the last "for inline parsing only … block
   structure is our own segmenter" (`pptmstr:pyproject.toml:34-39`). It has its
   own renderer: a streaming block segmenter (`pptmstr:pptmstr/ui/blocks.py:93`
   `BlockKind`, `:107` `Block`, `:418` `BlockCursor`), inline tokens
   (`pptmstr:pptmstr/ui/inline.py:42`), a greedy wrap
   (`pptmstr:pptmstr/ui/span_layout.py:64`, `:83`) and one module that touches
   ImGui (`pptmstr:pptmstr/ui/rich_pane.py:41`). "pptmstr is meant to depend on
   advtext" is a plan (`../imgui-adv-text/docs/adoption-plan.md:1-6`: "The
   pptmstr side is untouched and was never in scope"), not a dependency. No
   consumer of the facade exists outside advtext's own examples, demo and
   tests, as far as these three trees show.
2. **The class is `_Converter`, not `_Builder`** (`markdown.py:1011`;
   `_append_text` at `:1047`). D20 writes `_Builder._append_text`; the header
   comment of `src/style.h` has the right name.
3. **`blocklayout.py` is not the document block layer.** It is a row/column
   flex demonstration over `hpack`/`vpack` (`blocklayout.py:350-403`), used by
   one demo page and one test. Document composition is
   `ui/__init__.py:933-1497`. (`oracle-blocks` found the same.)
4. **`src/blocks.h`, `docs/bindings-notes.md` and a `bundle-research` result do
   not exist yet.** The block layer has goldens (`tests/golden/blocks`, 8
   files) and no port; how Python reaches the core is still being researched
   (the docstring of `tools/experiments/exp_b_run.py` says that of its three
   variants that use the installed bundle's ImGui, `deps` and `docking` "stop"
   at the struct-layout gate on this machine). Everything
   below that depends on bindings says so.
5. **`include/imtypeset.h` on disk is already 0.2.0** and its declarations
   match the lead's rulings R1, R3, R4, R5, R9, R10 and R12 as read (byte
   offsets everywhere, `TextUnformatted`/`TextWithID`/`SelectableText`,
   `CalcTextSize`, lines and caret rectangles, one math flag). "As ruled" below
   means that text. It was read, not compiled or tested here.

---

## 1. Module by module

50 files, 44,179 lines under `advtext/`.

| bucket | files | lines |
|---|---|---|
| A. Stays in Python as is | 2 | 2,049 |
| B. Stays in Python, but calls imtypeset | 12 | 18,700 |
| C. Replaced by imtypeset | 14 | 10,387 |
| D. No counterpart in imtypeset yet | 5 | 3,512 |
| E. The demo application | 17 | 9,531 |
| | 50 | 44,179 |

Bucket B is the misleading one. "Stays, but calls imtypeset" means *kept as a
file*; for five of its twelve files most of the body is code whose data model
is node tuples or shipped indices, and that code is rewritten, not re-pointed.
The table says which.

### A. Stays as is

| file | lines | why |
|---|---|---|
| `diff.py` | 2,030 | Pure text and data: imports `re`, `difflib`, `dataclasses`, `enum`, `functools`, `typing` and nothing of advtext (`diff.py:211-219`). |
| `backends/__init__.py` | 19 | A docstring. |

### B. Stays, calls imtypeset

| file | lines | what survives | what is rewritten |
|---|---|---|---|
| `__init__.py` | 495 | The lazy facade (`:473-488`). | `_HOMES` (`:249-360`); section 4 says which names. |
| `markdown.py` | 2,382 | The markdown-it configuration and custom rules (`:696-959`), the source map (`:582-664`, `:2252-2283`), the stream-resume helpers (`:1788-2250`). | Every site that makes a node: `_append_text` (`:1047-1094`), `_stand_in` (`:295-377`), `_formula` (`:1150-1191`), `_text_block` (`:1243-1271`), `_marker_material` (`:1291-1302`), `_code_lines` (`:1740-1784`), `_table` (`:1322-1374`), `_fill_and_break` (`:1575-1614`), and the block dataclasses whose fields are node tuples (`:497-572`). |
| `stream.py` | 1,673 | Almost all of it: "This module performs no layout and imports none" (`stream.py:18`); `Node` is a type annotation only (`:450`, `:1663`). | It passes a `MetricsResolver` through to the converter (`:682-692`, `:1147-1148`), which goes. |
| `spans.py` | 1,386 | `FindIndex` (`:1143-1227`), the public span types. | The material-to-shipped alignment, `:177-877` (700 lines), which the port derives instead (`ParagraphLayout::shipped`, `src/paragraph.h`; `docs/divergences.md`, "para"). `material_text`, `ColourSpan`, `TextMatch` change coordinates (section 2.4). |
| `ui/__init__.py` | 2,469 | `Viewport` (`:711`), `Hosted` (`:1500`). | Composition, `:903-1497`, is what the core's block layer replaces; `LayoutCache` (`:419-659`) goes with it; tables (`:1579-2469`, 890 lines) have no counterpart yet. |
| `ui/units.py` | 2,489 | The copy policy (`:248-264`, `:452-515`, `:1061-1247`). | `ship_unit` (`:383`), `moved` (`:696`), `UnitCache` (`:2197`), `layout_units` (`:2313`), `carry_selection` (`:1753`), `UnitModel` (`:1024`): all are machinery for retained per-unit layout in Python, which a retained native document replaces. |
| `ui/bare_view.py` | 2,529 | The imperative shell: mouse contract, selection ownership (`:347`), hosts (`:697-782`), scrolling (`:1549-1597`, `:2471`). | `_paint` (`:1758-1804`), decoration (`:1806-1983`), every query that returns or takes a reading-order `Selection`. |
| `ui/stream_view.py` | 601 | `sync` (`:294-310`), tail following (`:553-599`). | Table sizing (`:334-497`), `_carry` (`:503-546`). |
| `ui/diff_view.py` | 2,659 | The frame plan and interaction. | It builds `Glyph` and `Line` commands itself (`:553`, `:588`) and calls `draw_runs` (`:2093-2104`); section 2.3. |
| `ui/document_view.py` | 600 | Page arithmetic (`:250-393`). | `alphabet_length` measures `CharNode`s (`:341-358`). |
| `ui/fonts.py` | 379 | `load_faces` as a wrapper. | Its body, over `AddFontFromFileTTF`. |
| `style.py` | 1,038 | `Theme` and `Theme.build` (`:762-1038`), `Faces`, `BlockKind`, `Emphasis`, `Align`, `Spacing`: how a body size becomes per-kind styles is input to the core, not part of it (`tools/oracle/FORMAT-blocks.md`; `.concerns/oracle-blocks--builder-22--result.md`, "Not covered"). | `font_key`, `parse_font_key`, `char_measure`, `interword_glue`, `space_factor_after`, `Strut` (`:483-680`), ported as `src/style.*`. |

### C. Replaced

| file | lines | by | what the differential goldens already hold equal |
|---|---|---|---|
| `units.py` | 200 | `src/units.h` | `tests/golden/core` (7 files, 135,890 lines): every record replayed exactly; 593 cases, 488 line-breaking runs (board notes of `oracle-core` and `linebreak`). |
| `badness.py` | 126 | `src/badness.*` | same |
| `nodes.py` | 498 | `src/nodes.*` | same |
| `pack.py` | 564 | `src/pack.*` | same |
| `linebreak.py` | 1,004 | `src/linebreak.*` | same; total fit and first fit |
| `shipout.py` | 698 | `src/shipout.*` | same |
| `selection.py` | 981 | `src/selection.*` | `tests/golden/selection`: 57 cases and 600 fuzz buffers (board note of `selection`). Not `UnitModel`, which is in `ui/units.py`. |
| `fontmetrics.py` | 316 | `src/fontmetrics.h` | `tests/golden/font` (23 files): ink boxes, kerns, cmap, MATH values, scaled metrics exact (board note of `font-reader`). |
| `fontlib.py` | 1,137 | `src/fontlib.*`, `src/opentype.*`, `src/font_loader.*` | same; and `tests/golden/font-wild`: 2,882 of 2,991 installed faces bit-exact at the census, 85 refused, 24 mismatched (board note of `font-wild-census`; task `font-wild` is open). The cmap rewrite on disk (`fontlib.py`, about 1 s) is replaced by an in-memory cmap (D27). |
| `math/__init__.py` | 130 | `src/math_atoms.h`, `src/math_mlist.*` | `tests/golden/math`: 4,613 conversions equal (board note of `math-mlist`). |
| `math/atoms.py` | 658 | same | same |
| `math/mlist.py` | 1,263 | same | same |
| `mathtext.py` | 1,587 | `src/mathtext.*` | 3,765 parse cases and 1,971 end-to-end cases equal (board note of `mathtext`); 71 cases of `inline.golden` through the public API (D31). |
| `backends/imgui_draw.py` | 1,225 | `src/draw.*` | `tests/golden/draw`: 343,536 vertices byte-equal through the public API (D30). The palettes (`:298-527`, about 230 lines) are data and stay in Python. |

Paragraph assembly is replaced too but is not a file of its own in the
reference: `_skips`, `_shape`, `_text_boxes`, `_unshrinkable`, `_hang_marker`
(`ui/__init__.py:1312-1478`) and `_append_text` are `src/paragraph.*` and
`src/style.*` (D20), held by `tests/golden/para` (243 cases; 225 replayable
through the public API, D30) and `tests/golden/para-map` (36 blocks at 3
widths).

**Not yet held equal by anything**, although the reference has it and the
goldens exist: stacking several blocks (`tests/golden/blocks`, 69 cases; the
port is task `blocks`, pending).

### D. No counterpart

| file | lines | why not |
|---|---|---|
| `columns.py` | 542 | TeX's `vert_break`/`vsplit` over the document's vertical list (`columns.py:229`, `:380`, `:435`). Nothing in `src/` splits a vertical list (`grep vert_break\|vsplit\|prune_top src include`: no file). |
| `blocklayout.py` | 469 | Builds node lists by hand and packs them (`:350-403`). Needs public nodes; see 3, "not recommended". |
| `backends/svg.py` | 380 | A second backend over shipped commands and fontTools outlines. imtypeset has one backend and does not expose commands. |
| `ui/formulas.py` | 1,230 | Builds formulas from atoms and node lists by hand (imports `math.atoms`, `math.mlist`, `nodes`, `pack`). Needs the public math-list builder of section 6. |
| `ui/specimen.py` | 891 | Builds node lists by hand (imports `nodes`, `pack`). |

Inside bucket B, also with no counterpart: tables (`ui/__init__.py:1579-2469`)
and the positioned-glyph draw `DiffView` uses.

### E. The demo

`ui/app.py` (1,431) and `ui/pages/*` (16 files, 8,100 lines). Classified from
imports only. Eight of the seventeen import `nodes`, `pack` or `shipout`
directly: `app.py`, `pages/__init__.py`, `pages/blocks.py` and
`pages/mathtext.py` import `nodes`; `chat.py`, `stream.py`, `stress.py` and
`tables.py` import `pack` alone, which may be for a type and was not looked
at. What builds node lists falls with bucket D. D32 already gives
the product its own C++ demo (task `demo`).

### The reference's tests

63 files, 54,837 lines under `../imgui-adv-text/tests`. 49 of the 63 import
`nodes`, `pack`, `linebreak`, `shipout`, `units` or `badness` directly. A swap
does not carry them: they are tests of the reference, and they stay with it
(section 5).

---

## 2. Call-site inventory, grouped into operations

Frequency words: *per frame*; *per reflow* (a width, theme or font change);
*per append* (a stream revision that changed the tail); *per event* (mouse,
copy, find).

### 2.1 Text into material (what markdown builds)

| call site | passes | gets | how often |
|---|---|---|---|
| `markdown.py:1047` `_append_text(run, text, font, breakable)` from `:1110-1135` (text, code span, soft break), `:1182` (refused formula), `:1291-1302` (marker), `:1698-1704` (`text_block`) | a `str`, a `FontSpec` from `Theme.font_for(kind, emphasis)`, a no-break flag; reads `resolve(key).space_width`, `.has(cp)`, `.kern(a, b)` (`:1058-1093`) | `CharNode`, `Kern`, `Glue(interword_glue(...))`, `Penalty(INF_PENALTY)` appended to the run | per block per parse; for the live tail of a stream, per append |
| `markdown.py:1189` `atom_box(built)` | the `Box` a `MathBuilder` returned for `(source, display, size)` (`:480`, `:1177`) | one node in the material; a `FormulaSpan` one node wide (`:1191`) | per formula per parse |
| `markdown.py:1254` | `style.first_line_indent` | an empty `Box` at material index 0, and every span index shifted by one (`:1262`, `:1268`) | per text block |
| `markdown.py:1613-1614` `_fill_and_break(align)` after `_unskip` (`:1136-1138`) | the block's alignment | `Penalty(EJECT)` or `Penalty(INF), Glue(PARFILLSKIP), Penalty(EJECT)` | per hard break |
| `markdown.py:1763-1784` `_code_lines` | source with tabs expanded to 4 (`:247`, `:1763`) | one tuple of `CharNode` per line; a space is a `CharNode`, never glue (`:519-528`) | per code block per parse; per appended line in a live fence (`stream.py:1198-1247`) |
| `markdown.py:1355-1362` | a cell's inline tokens | `TableCell(material, links, header, formulas)`: one flat run list per cell | per cell per parse |

**In `include/imtypeset.h` as ruled:** partly. `SetParagraphText(text, runs,
flags)` takes one string with `ImTypesetRun{Start, End, Face, Scale, Color,
Flags}` and `ImTypesetRunFlags_NoBreak`, which is `_append_text`'s `font` and
`breakable`. Hard breaks are `ImTypesetTextFlags_HardBreaks`. Formulas are
`ImTypesetTextFlags_Math`, found by the library's own scanner.

**Internal only:** a list marker (`set_marker`, `hang_marker`,
`BlockStyle::marker_gap` in `src/paragraph.h`; D30 lists markers as "not in the
API, on purpose"); `hard_break(run, align, source_offset)` as an item rather
than a newline; `stand_in` (`src/style.h`); a formula's box placed by the
caller (`InlineMath::build`, `src/context.h`).

**Missing:** verbatim text (a paragraph collapses white space, so a code line
cannot be a paragraph); an *explicit* formula range (below); a table cell.

#### Is a block description enough for what `markdown.py` emits?

A block description here means: per block, a kind, a resolved style, an
absolute left indent, a quote depth, an optional marker string, and text with
styled runs, hard breaks and formula ranges.

**It is enough for** — each checked against the emitting line:

- Paragraphs, headings, quote text and list-item text. A run is text under a
  face chosen by `Theme.font_for(kind, emphasis)` (`:1110-1124`); nothing else
  is attached to the nodes.
- Inline code: a face and `breakable=False`, nothing more. No background, no
  padding and no span is recorded (`:1114-1116`).
- Soft breaks (one space, `:1131-1135`) and hard breaks (`:1136-1138`).
- List markers: the marker is plain text, `"•"` or `f"{n}."`
  (`:1509-1518`), in the item kind's regular face, unbreakable (`:1291-1302`).
- Nesting: `left_indent` is a running sum of `theme.list_indent` and
  `theme.quote_indent` (`:1466-1477`, `:1488-1493`) and `quote_depth` is an
  integer (`:1266`). No bar node is built here; composition draws the bar from
  the depth (`ui/__init__.py:1212-1250`).
- Thematic breaks: a thickness and an indent (`:1498-1505`).
- Code blocks: a language string and lines of characters (`:1307-1320`).
- Display math that was closed: one formula, centred, no first-line indent
  (`:1215`), taking the pending marker (`:1226-1236`).
- Refused and unclosed formulas: the source in the code face (`:1182-1187`,
  `:1221-1225`).
- Images, raw HTML, footnotes, task lists, strikethrough: nothing is emitted
  for any of them (`:1143-1147`; `html_block` has no case in `:1457-1507`; the
  parser is `MarkdownIt("commonmark").enable("table")`, `:921`).

**It is not enough for** — each needs something named:

1. **Tables.** A `TableBlock` is a header and rows of cells with one alignment
   per column (`:559-572`), each cell its own run list with its own link and
   formula spans (`:547-556`). Widths are not in the block; composition solves
   them (`ui/__init__.py:2000-2054`) or a host states them (`SizedTable`,
   `:1579`). This is a second description shape, not a block with runs. A cell
   is a run list and not nested blocks, so one level is enough.
2. **A formula the host builds.** `MathBuilder` is a callable returning a `Box`
   (`:480`), and that box goes into the material as it is (`:1189`). A text
   description cannot carry a box. The default builder (`markdown_math`,
   `mathtext.py:443`) fits, because the core has the same subset natively; a
   host's own builder does not, without the public math list of section 6 or an
   "inline object" slot in the text model.
3. **Formula ranges decided by the front end.** In the reference, which dollars
   are math is markdown-it's decision (`_math_inline`, `:711-807`, registered
   after the escape rule, `:929`), including a streaming rule: a closer that is
   the last character of text still arriving is not a closer (`:792-795`). The
   public API decides again with its own scanner (`ImTypesetTextFlags_Math`) and
   its header tells a host that parsed markup itself to "escape the dollars it
   means literally". A front end that has already tokenised needs to say *this
   range is a formula, inline or display* and have nothing rescanned.
4. **Every public coordinate that is a material index.** `LinkSpan.start/end`
   (`:398-410`), `FormulaSpan.start/end/index` (`:414-459`), `ColourSpan`
   (`spans.py:1020`), `TextMatch` (`spans.py:1110`), `code_position`
   (`spans.py:880`) and `MaterialText.indices` (`spans.py:901-925`) all count
   nodes, kerns, penalties and the indent box included. With no nodes in Python
   they have nothing to count. They become ranges of the block's text.
5. **Replaced characters as nodes.** `ReplacedChar(CharNode)` carries the
   written codepoint on the node (`:273-292`); `replaced_characters` finds them
   by `isinstance` (`:2285`). The core keeps the same fact
   (`ParagraphLayout::originals`, `src/paragraph.h`) and must report it.
6. **Code lines as separately addressable values.** The stream appends to a
   live fence by concatenating line tuples (`stream.py:1231`, `:1663`), and the
   unit layer cuts a long block into 16-line pieces with
   `replace(block, lines=chunk)` (`ui/units.py:271`, `:2068-2100`). A code
   block that is one string re-laid-out whole per appended line loses that.
7. **A block that is a host's widget.** `HostBlock(CodeBlock)` reserves a
   rectangle of a height the host is asked for every frame and ships nothing
   (`ui/__init__.py:1534-1575`; `ui/bare_view.py:755-762`, `:770-782`). Not
   emitted by `markdown.py`, but it is a member of the block union the views
   compose.
8. **A style carried by the block.** Every block holds a whole `BlockStyle`
   (`:497-545`), and display math overrides two of its fields in place
   (`:1215`). A description whose style is implied by the kind cannot say that.
   The kind is then only a role (for ink), and all geometry is stated.

**Fits, but will not be node-for-node what the reference builds** — a
difference to expect, not a missing feature. `_append_text` kerns only within
one call (`:1090-1093`) and collapses white space only within one call
(`:1064-1070`), so the reference's material depends on where markdown-it cut
its text tokens. The public API takes one string with ranges and collapses
across them (`docs/divergences.md`, "public-api", item 2). Whether it also
kerns across a run boundary of the same face was **not checked** here.

### 2.2 Blocks into lines, lines into a document

| call site | passes | gets | how often |
|---|---|---|---|
| `ui/__init__.py:1413` `break_paragraph` in `_text_boxes` (`:1369-1422`) | material, `ParShape` from `_shape` (`:1347-1366`), params from `_skips` (`:1312-1344`); for text that is not justified, `_unshrinkable(material)` and `first_fit=True` (`:1411-1412`) | line boxes | per text block on a cache miss: per reflow; the live block per append |
| `ui/__init__.py:1448-1478` `_hang_marker` | first line, marker nodes, indent, `theme.marker_gap` | the first line with the marker hung | per list item per reflow |
| `ui/__init__.py:1295`, `:1301`, `:2192` `strut.applied_to` | a line box | the box floored at the strut | per line per reflow |
| `ui/__init__.py:1302` `hpack(line)` with `shift_amount=left_indent` | one code line | a box at natural width, never wrapped | per code line per reflow |
| `ui/__init__.py:1481-1497` `_rule_box` | width, indent, thickness | `hpack([Kern, Rule])` | per rule per reflow |
| `ui/__init__.py:1107-1173` `_stacked`, `append_to_vlist` (`:1161`) | blocks, theme, width | a vertical list: one `Kern(space_below + space_above)` between runs (`:1141-1143`), `prev_depth` reset at each (`:1146`) | per reflow of a run |
| `ui/__init__.py:1212-1250` `_barred` | a run of quoted blocks | `hpack([Kern, Rule, Kern, vbox])` | per quote per reflow |
| `ui/__init__.py:1056-1084` `RunComposer.compose(run)` from `ui/units.py:2305` | one run | `ComposedRun(blocks, box, extents)` | per unit miss |
| `ui/units.py:397` `ship(box, Point(0, box.height))`, `:398` `CommandIndex.of` | a unit's box | a tuple of commands and their vertical extents | per unit miss |

**As ruled:** one block. `LayoutParagraph(paragraph, width, style)` with
`ImTypesetStyle{Align, LeftIndent, FirstLineIndent, LineHeight, LineBreak}`,
then `GetParagraphSize`, `GetParagraphLineCount`, `GetParagraphLine`.

**Internal only:** `ParagraphComposer::compose` and `set_lines`, `shape`,
`skips`, `hang_marker`, `BlockStyle` with `interline`, `strut` and
`ragged_total_fit` (`src/paragraph.h`). The header of that file says what it
leaves to its caller: "more than one block (the space above and below a block,
and prev_depth being reset between them …), quote bars, code blocks, rules,
tables".

**Missing:** the stack itself, in source and in the header. The reference's
rules for it are written down and held by goldens that nothing replays yet
(`tools/oracle/FORMAT-blocks.md`; `.concerns/oracle-blocks--builder-22--result.md`
lists fifteen shortcuts in those rules that are candidates for D32.3).

### 2.3 Drawing

`draw_runs` (`backends/imgui_draw.py:587-600`) against `Drawer::draw_runs`
(`src/draw.h`) and against the public `DrawParagraph`:

| channel | reference | `src/draw.h` (internal) | `include/imtypeset.h` (as ruled) |
|---|---|---|---|
| ink per run | `runs: (commands, ink)`; the views pass one run per stretch of same-role units (`ui/bare_view.py:2383-2412`, `Palette.ink_for`, `imgui_draw.py:460`) | `DrawRun{cmds, count, ink, lines}` | `ImTypesetStyle::Color` per paragraph, fixed at `LayoutParagraph` |
| ink finer than a run | `inks: (start, end, colour)` over glyph reading indices, sorted, bisected per glyph (`:693`, `:723-729`); links, syntax tokens, a diff's per-character colours (`ui/diff_view.py:580-586`) | `InkRange{start, end, colour}`, same contract | `ImTypesetRun::Color`, which is part of the text: changing it is a `SetParagraphText` |
| fills | `fills: (Rect, colour)`, drawn first, snapped like a rule (`:685-687`): find marks, link underlines (`spans.py:1308-1386`) | `Fill{rect, colour}` | none |
| selection | `highlights: Rect[]` in `palette.highlight`, after fills, before glyphs (`:689-691`) | `DrawArgs::highlights` | `selection` argument, `ImTypesetStyle::SelectionColor` |
| rules | `palette.rule` (`:736`) | `Palette::rule` | not reachable: a paragraph has no rule |
| debug overlay | `debug=True`: line boxes, baselines, glue coloured by stretch or shrink (`:1194-1225`) | `DrawArgs::debug` | none ("not in the API, on purpose", D30) |
| culling | none: the caller culls (`ui/units.py:982-1020`, `:1563-1576`) | `LineIndex`, `DrawArgs::cull_lines` | by line, inside `DrawParagraph`, against the draw list's clip rectangle |
| a second sink | no | `DrawBuffers` | `DrawParagraphToBuffers` |

Call sites: `ui/bare_view.py:1793-1804`, once per frame, with one screenful of
commands gathered from the visible units, plus `inks`, `fills` and
`highlights` rebuilt in Python; `ui/diff_view.py:2093-2104`, two to four times
per frame per visible diff.

**The internal draw has every channel the reference has. The public draw has
two of them.** What is missing publicly: colour ranges and fills supplied at
draw time, addressed in text positions, that do not re-lay-out anything; the
debug overlay; ink per block role (which a document needs and one paragraph
does not).

**`DiffView` does not typeset.** It places one glyph per character cell on a
grid it computes itself, builds `Line` and `Glyph` commands in Python
(`ui/diff_view.py:553`, `:588`) and hands them to `draw_runs`. Measured by the
reference at about 3.3 µs per glyph on screen
(`../imgui-adv-text/docs/adoption-plan.md:371-376`). Nothing in a text or block
description produces that. It needs one of: a public "draw these positioned
glyphs" entry (which is the command list the design note warns against
returning, `../imgui-adv-text/docs/cpp-extension.md:209-214`, but as an
*input*); a native grid block (rows of cells, per-range ink, row fills, a
gutter); or to stop using the library and call `ImDrawList::AddText` per
same-colour run, as pptmstr does today (`pptmstr:pptmstr/ui/rich_pane.py:219`).

### 2.4 Culling, hit testing, selection, source maps

| what | reference | how often |
|---|---|---|
| find the visible units | bisect two monotone arrays of ink reach over the placed units (`ui/units.py:1381-1410`, `:1563-1576`), then per unit none, all, or a per-command filter (`:982-1020`) | per frame |
| point to position | `layout.model.select_at(point, point, granularity)` (`ui/bare_view.py:2193-2204`), which bisects to a unit and applies `selection.py`'s rules (`ui/units.py:1333-1358`) | per press, per drag frame |
| selection rectangles | `layout.visible_rects(selection, viewport)` (`ui/units.py:1578-1621`) | per frame while something is selected |
| a range of a block to rectangles | `map_text_block` / `map_code_block` / `map_table`, then `span_rects` (`ui/bare_view.py:1235-1310`) | per host call |
| copy | `UnitModel.copy_text` (`ui/units.py:1192-1247`) with separators `"\n\n"`, `"\n"`, `"\t"`, two spaces per list level (`:248-264`), atoms (`:517-559`) and originals (`:561-619`) | per copy |
| carry a selection over a re-layout | `carry_selection`, `carry_caret` (`ui/units.py:1753-1948`), restating each end as "the n-th glyph of the unit that begins at this block" | per reflow and per append, while something is selected |
| block under a point, block rectangle | `block_at` (`ui/units.py:1644`), `block_rect` (`ui/bare_view.py:1206`) | per event |
| block to source | `Document.sources[i]` (`markdown.py:668-691`): whole source lines, as line numbers and as `str` offsets (`:582-631`) | per copy-as-markdown |

Three facts decide what the core must offer here.

- **The reference's selection is not in source coordinates and cannot be.**
  `BareView.selection` is a pair of reading-order boundaries
  (`ui/bare_view.py:837-841`; `selection.py:128`, `:215`), which count the
  glue a line break inserts, so the same pair means other text after a
  re-layout: "5286 items at 500 px and 5295 at 400 px" (`ui/units.py:1753-1770`).
  `carry_selection` exists to repair that. The public API's byte offsets (R3)
  make the repair unnecessary within a block, by construction.
- **The reference has no character-level map back to the markdown.** "Nothing
  maps a character of rendered text back to an offset in the markdown"
  (`ui/bare_view.py:996-1003`); `selected_source` returns whole blocks'
  lines. `BlockSource` lives beside the blocks, not in them, so that an edit
  above does not change a block's cache key (`markdown.py:668-681`). So
  `BlockSource`/`SourceSpan` need **nothing** from the core but a stable way to
  say which blocks a selection touches. The byte offsets the core speaks are
  offsets into the *text the front end handed over for that block* (markup
  already removed), not into the markdown. Those are two coordinate systems,
  and the second stays entirely on the Python side.
- **`SourceSpan` offsets are Python `str` indices** into the `\n`-normalised
  source (`markdown.py:582-607`; `stream.py:787-796`), not bytes. The public
  API has `TextCodepointToByteOffset` and `TextByteToCodepointOffset` for
  exactly this.

**As ruled, per paragraph:** `HitTestParagraph`, `SelectParagraphRange` with
`ImTypesetGranularity`, `GetParagraphRangeRects`, `GetParagraphCaretRect`,
`GetParagraphLine`, `AppendParagraphRangeText` (formula as its source, replaced
character as written, a blank line between paragraphs appended in turn).

**Missing:** all of it across blocks. Which block is at a point; a block's
rectangle; a range that starts in one block and ends in another; the visible
blocks without visiting the others; copy with a marker's text, a list's
indentation, a cell's tab.

### 2.5 Streaming

What happens per appended piece of text, as built:

1. `StreamDocument.append` only buffers, under a lock, and may be called from
   another thread (`stream.py:712-738`, `:546-560`). Parsing is deferred to the
   next read (`:1002-1070`).
2. At most once a frame, `StreamView.sync` reads `stream.revision`
   (`ui/stream_view.py:294-310`). That parses **only the uncommitted tail**:
   every top-level markdown block before the last is committed and never
   revised (`stream.py:1072-1122`); three fast paths keep a long list, a
   top-level fence and a top-level table from being re-read whole
   (`:397-499`).
3. Committed blocks are the same objects for ever (`stream.py:1108`). Live
   blocks are new objects each revision, equal by value when their text did not
   change.
4. `layout_units` walks every block. A run found by identity costs nothing; one
   found by value is re-homed; a miss is composed, shipped and indexed
   (`ui/units.py:2284-2308`). What misses is the changed tail: a live
   paragraph whole; of a live fence over 16 lines only the last piece; of a
   live table over 4 rows only the last piece, unless a column width moved
   (`ui/stream_view.py:216-221`, `:334-413`; `ui/units.py:271`, `:291`).
5. Units below a change get a new offset; their commands are moved lazily
   (`ui/units.py:696-751`, `:804-809`).

Measured on this machine (`docs/performance.md`, "The reference on its own
sample"): parse 0.28 to 2.14 ms, layout 0.33 to 8.32 ms, frame work 0.10 to
0.87 ms per sampled word. The port re-lays-out the paragraph being written in
0.011 ms median, 0.110 ms worst (same document, headline table).

**What the core needs so a stream costs O(changed):** a retained document whose
blocks can be replaced from an index onward, in which a block that was not
replaced keeps its layout, its identity and every position inside it, and the
blocks below a change are only moved. That is "replace the tail blocks". Two
refinements follow from the fast paths: appending lines to the last code block
and rows to the last table must not cost the whole block, either as an append
operation or by letting a host submit one block as several pieces with no gap
between them (the reference computes that gap with `interline_glue`,
`ui/units.py:2161-2193`); and a table that is still growing needs columns that
can be held or widened without re-setting settled rows
(`ui/__init__.py:1852-1959`, `TableMeasure`).

**As ruled:** an `ImTypesetParagraph` is a retained block with exactly these
properties, one at a time: `SetParagraphText` on the live one, nothing on the
others. A host can therefore build the stream *today* out of a vector of
paragraphs and its own running sum of heights; what it cannot get that way is
markers, code, rules, quote bars, tables or the reference's spacing.

The parse does not move. After a swap markdown-it's 0.3 to 2.1 ms is the whole
of a streaming frame's Python cost, against pptmstr's stated 1.7 ms line
(`../imgui-adv-text/docs/adoption-plan.md:1099`).

### 2.6 Tables and columns

Tables, as the reference sets them (`ui/__init__.py`):

- Natural width of a column: the widest cell packed on one line plus padding
  (`:1988-1997`). Floor of a column: its longest unbreakable word, found by
  breaking the cell at a one-point measure (`:2082-2104`).
- Widths: a host's (`SizedTable.column_widths`, or fractions shared out,
  `:2036-2037`, `:2057-2079`); else natural widths scaled up to the measure
  (`:2210-2232`); else squeezed toward the floors (`:2107-2131`); and if the
  floors do not fit, overfull.
- A row that fits is one line (`:2235-2257`). A row that does not is every cell
  broken first-fit, ragged, to its column and re-boxed as a `\vtop`
  (`:2260-2311`, `:2365-2428`), and the next row's interline glue reads a
  resumed depth (`:1162-1164`, `:1974-1985`).
- One rule, under the header (`:2193-2206`). No cell borders.
- Facade: `with_columns` (`:1705`), `table_columns` (`:1745`), `SizedTable`
  (`:1579`), `ColumnLimits` and `table_column_limits` (`:1796-1849`).
- How often: natural widths per cell per reflow, or once per new row with
  `TableMeasure` (`:1905-1912`); cell breaking per wrapping row per reflow.

**In the core:** nothing, in source or header; D32.4(ii), not started. No
golden either: `dump_blocks.py` drops the tables from the demo documents
(`tests/golden/blocks/demo.golden`, header comment). A host could set its own
tables over paragraphs with two measures the API half-has: max-content is
`CalcTextSize` unwrapped (but it takes no runs), and min-content (the longest
unbreakable piece) is missing.

Columns: `compose_into_columns` composes once and cuts the document's vertical
list with `vert_break` (`columns.py:502-542`, `:229-342`). It reads the list
itself: node types, heights, depths, glue stretch and shrink, penalties
(`:194-342`). Callers are the demo (`ui/app.py`, `ui/pages/columns.py`) and
tests. **In the core:** nothing.

### 2.7 `LayoutCache` and `UnitCache`

- `LayoutCache` (`ui/__init__.py:419-659`) maps `(block value, width, params,
  CharMeasure identity, theme as a tuple of its fields)` to that block's line
  boxes (`:321-346`, `:662-678`). Complete key, so no invalidation; `clear()`
  only frees (`:596-606`). It keeps two "passes" (width and context pairs),
  least recently used, and within a pass drops what the last pass at that key
  did not use (`:397-415`, `:608-631`).
- `UnitCache` (`ui/units.py:2197-2238`) sits in front and holds shipped units,
  found by identity first and by a cheap shape then value equality
  (`:2284-2301`). "A unit is keyed by the width, so a resize misses on every
  unit" (`:2217-2218`).
- Their statistics are public: `LayoutStats` (`ui/units.py:1414-1463`).

What this means for the core: the cache is a consequence of blocks being
immutable *values* that a re-parse recreates. A retained document with stable
block identity needs no value-keyed cache; it needs the splice of 2.5. The one
behaviour worth keeping is the report: which blocks a layout built and which it
reused. The public API already goes further than the reference on a resize
(R7: out-of-view text keeps its height and is laid out when seen), and at
0.465 ms for the whole sample document (`docs/performance.md`) the two-width
residency has nothing left to save.

### 2.8 The operations, in one table

| # | operation the stays-modules need | in the header as ruled | internal today | missing |
|---|---|---|---|---|
| 1 | Styled text to material, one block | `SetParagraphText` + `ImTypesetRun` | | verbatim text; explicit formula ranges; run tags |
| 2 | Marker, block style, quote depth | | `set_marker`, `hang_marker`, `BlockStyle` | public form |
| 3 | Break to a width | `LayoutParagraph` | `ParagraphComposer` | |
| 4 | Stack blocks: gaps, bars, rules, code | | | `blocks` (goldens exist) |
| 5 | Retained, spliceable document | one paragraph at a time | | the document |
| 6 | Draw with ink ranges, fills, debug, culled | selection only | `Drawer::draw_runs`, `LineIndex` | public channels; cull by block |
| 7 | Positions, rectangles, hit tests | per paragraph, byte offsets | `SelectionModel` | across blocks |
| 8 | Copy | per paragraph | `SelectionModel::text`, `TextData` | separators across blocks |
| 9 | Tables | | | everything |
| 10 | A reserved rectangle in the stack | | | trivial once 4 exists |
| 11 | A formula on its own; what is supported; why refused | inside a paragraph; `ImTypesetFormula::Reason` | `typeset`, `supported_command`, `RefusalKind` (`src/mathtext.h`) | public form; the kind |
| 12 | Font numbers: advance, has-glyph, em for a pixel height | `GetFont` | `src/fontlib.*` | public queries |
| 13 | What the last layout built and reused | `GetStats` (context-wide, debug) | | per document |
| 14 | Split a document into columns | | | everything |
| 15 | Positioned glyphs (`DiffView`) | | `Drawer::draw_runs` over commands | a decision, 2.3 |
| 16 | Nodes, `hpack`, `vpack`, `ship`, commands | | all of `src/` | deliberately not public |

---

## 3. Requirements for the core's public Document API

Ordered by how much of advtext each unlocks. Each is stated so that a C++ host
with its own markdown parser makes the same call. **[E]** means the behaviour
exists internally and needs exposing; **[P]** part exists; **[N]** new.

**R1. A document is an ordered list of blocks the host describes; the library
never parses markup. [P]**
Per block: a kind that is a *role* (paragraph, heading level, list item, quote,
code, rule, and room to append); a complete resolved style (face, size,
leading, alignment, space above and below, absolute left indent, first-line
indent, line-breaking mode) so that nothing is implied by the kind; a quote
depth on *every* kind; an optional marker (text and face). For text kinds: UTF-8
text with runs (face, scale, colour, no-break, and an opaque host tag such as a
link number), hard breaks, and **explicit formula ranges** (inline or display)
as an alternative to dollar scanning. For code: lines set verbatim, never
wrapped, white space kept. For a rule: a thickness.
Unlocks: all of `markdown.py`'s emission but tables; `text_block`,
`code_block`; `compose`, `compose_blocks`. Without it nothing else here
matters. Acceptance: every block of `tests/golden/blocks` can be described
through it and composes to the golden's boxes.

**R2. Layout to a width is retained and incremental. [P]**
Replace, insert and remove blocks at an index; a block not touched keeps its
layout, its identity and the meaning of every position inside it; blocks below
are moved, not re-laid-out. Replacing a block's text "in place" keeps its
identity, so a selection in a paragraph still being written survives. Appending
to the last code block costs the appended lines (or: adjacent blocks can be
declared pieces of one, with the interline glue between them and no gap).
Unlocks: `stream.py` and `StreamView` at O(changed); retires `LayoutCache`,
`UnitCache`, `layout_units`, `moved`, `carry_selection`. Acceptance: appending
one word to the last of N blocks does work independent of N, shown by scaling N
as R11 of the API rulings does for culling.

**R3. Positions are (block, byte offset into that block's text), stable across
re-layout, with every geometric question answerable in them. [P]**
Point to position, with "inside or not" and with the block under the point even
where there is no text; select by character, word and line across blocks;
rectangles for a range that spans blocks; a block's rectangle; a caret
rectangle; lines with baselines per block; the blocks a range touches.
Per-paragraph forms of all but the cross-block ones are in the header already.
Unlocks: `BareView`'s mouse, `selected_blocks`, `block_rect`, `span_rects`,
`scroll_to`, `pointed`, `set_lines` (which also wants each line's glyph x
positions, `ui/bare_view.py:1432-1481`), and the whole of `BlockSource`, which
needs only "which blocks".

**R4. One draw call for a document, culled by block then by line, with colour
and fills supplied at draw time. [E]**
Work proportional to what is visible plus a logarithm of the block count
(`docs/performance.md` shows per-paragraph calls are not flat). Inputs beside
the position: a selection; ink per block; ink for ranges of positions (links,
syntax tokens) that changes nothing about layout; filled rectangles for ranges
with a colour and a place (behind the line, or at an offset under the
baseline), snapped as rules are; the debug overlay. The same into buffers.
Unlocks: `BareView.draw`, `set_colours`, find marks, link underlines, role
palettes. `Drawer::draw_runs` has every channel; `LineIndex` culls by line.

**R5. Copy across blocks, with the separators stated and replaceable. [P]**
The text of a range: a wrap is a space, a hard break a newline, a formula its
source, a replaced character what was written (all true per paragraph today).
Between blocks the host chooses or accepts a default: blank line between
blocks, newline between code lines, tab between cells, the marker's text and an
indent per list level (`ui/units.py:248-264`). Enough if the host can get each
touched block's own text and join them itself.

**R6. Tables. [N]**
A table block: rows of cells, each cell text with runs as in R1, a header row,
an alignment per column. Column widths chosen by the library or stated by the
host as lengths or as shares; the chosen widths and each column's limits
(longest unbreakable piece, and width on one line) can be asked for. Rows can
be appended to the last table with settled rows not re-set unless a width
moved. Positions inside a cell are (block, row, column, byte offset).
Unlocks: `TableBlock`, `with_columns`, `table_columns`, `SizedTable`,
`ColumnLimits`, `table_column_limits`, `TableMeasure`, the live-table path.
A cheaper first step that lets a host build its own: two measures of a styled
text, its width on one line and its narrowest unbreakable width.
**Needs goldens first**, and they must be dumped before the reference is
changed (section 5).

**R7. A block that reserves a rectangle. [N]**
A height the host sets, cheaply changeable, with a rectangle to draw into and a
text the host supplies for copy. Unlocks `HostBlock`, `Hosted`, `HostFor`,
`DiffFences`: a widget inside a document.

**R8. Formulas as things of their own. [E]**
Measure and draw one formula outside a paragraph; list the supported commands;
report a refusal as a kind (an enum that only grows), a position and a
sentence. `ImTypesetFormula` has the sentence and the position and not the
kind. Unlocks `typeset`, `Refusal`, `RefusalKind`, `supported_commands`,
`formula_spans`. The public math list of section 6 belongs here.

**R9. Font numbers. [E]**
For a face at a size: ascent, descent, em, the advance of a codepoint, whether
it has a glyph; the em that gives a wanted pixel height. Unlocks
`em_size_for_pixel_height`, `DiffView`'s cell grid
(`ui/diff_view.py:841-917`), `DocumentView`'s alphabet length, `Theme.build`'s
marker probe (`style.py:727`).

**R10. A report of what a layout did. [P]**
Per document: blocks laid out, blocks reused, blocks deferred. Unlocks
`LayoutStats`.

**R11. Flow a document into columns of a height. [N]**
Either the split itself, or enough to do it outside: each line's top and bottom
and a draw of a line range at an offset. One demo page uses it. Last, and only
if columns are wanted in the product.

### Not recommended: what would exist only to keep advtext's present types

- **Public nodes, `hpack`, `vpack`, `ship` and draw commands.** They would keep
  `compose`, `compose_blocks`, `ship`, `Point`, `DrawCmd`, `draw`,
  `blocklayout.py`, `ui/specimen.py` and the SVG backend alive. They are the
  whole of `src/` as an API, they put the per-glyph loop back in the host
  language (`../imgui-adv-text/docs/cpp-extension.md:209-214`), and D30 and
  D32.1 both decline them.
- **Material indices** as a public coordinate. They count kerns and penalties.
- **A value-keyed layout cache.** R2 replaces the need.
- **Reading-order boundaries** as the selection type. R3 withdrew them (the
  lead's ruling R3 on the API review).

`DiffView`'s positioned glyphs (2.3) are the one case where declining leaves a
whole view unserved; it is a decision for whoever takes the views, not a
requirement on the Document API.

---

## 4. What changes for advtext's own users

### The facade: 94 names (`__init__.py:370-465`)

**48 can keep signature and meaning.** Pure Python, or a thin layer over what
the header already has.

- Diff model (9): `Diff`, `FileDiff`, `Hunk`, `DiffRow`, `RowKind`,
  `FileStatus`, `UnifiedDiffStream`, `DiffApplyError`, `ContextUnavailable`.
- Diff view data (3): `DiffLayout`, `SyntaxColour`, `language_for`.
- Stream policy (2): `CommitPolicy`, `live_theme`.
- Theme (7): `Theme`, `Faces`, `Align`, `Spacing`, `BlockKind`, `Emphasis`,
  `Face`. The seven `Face` roles are the header's seven.
- Colour (7): `Palette`, `DEFAULT_PALETTE`, `DARK_PALETTE`, `DiffPalette`,
  `DEFAULT_DIFF_PALETTE`, `DARK_DIFF_PALETTE`, `DrawStats`.
- Source map (2): `BlockSource`, `SourceSpan`.
- Units (3): `Scaled`, `sp_from_px`, `px_from_sp`.
- Hosting (4): `HostFor`, `Hosted`, `HostedDiff`, `DiffFences` (with R7).
- Math data (3): `Refusal`, `RefusalKind`, `supported_commands` (with R8).
- Fonts (8): `load_faces`, `LoadedFaces`, `TEXT_FACES`, `ALL_FACES`,
  `register_font`, `unregister_font`, `registered_fonts`,
  `em_size_for_pixel_height` (the last with R9). `load_pending()` and the
  patched font file on disk disappear behind them.

**19 keep their name and call shape, and something about them stops being
true.**

- `build_document`, `text_block`, `code_block`, `StreamDocument`: the `resolve`
  argument is no longer read, because no material is built in Python.
- `MetricsResolver`, `fontlib_metrics`, `char_measure`: kept as tokens for
  those arguments. A caller who passed a resolver of its own (round numbers in
  a test) is no longer obeyed.
- `BareView`, `StreamView`, `DiffView`: the `measure` argument likewise; and
  `selection`, `select`, `Selection` change type (below).
- `Document`, `Block`, `RuleBlock`, `HostBlock`: same roles, other field types.
- `markdown_math`: returns a token that turns the core's subset on, not a
  callable that builds boxes; its `remember=` memo is not ported
  (`docs/divergences.md`, "api-math", item 8).
- `MathRefused`: still what a host's own builder returns to decline, if host
  builders survive at all (2.1, item 2).
- `ComposeParams`, `PROSE_BREAKING_TOTAL_FIT_RAGGED`: `ragged_total_fit` maps
  to `ImTypesetLineBreak_TotalFit`. The other `LineBreakParams` fields a
  `ComposeParams` inherits (tolerance, penalties, emergency stretch) have no
  public counterpart.
- `LayoutStats`: fields that count units and unit builds (`ui/units.py:1414`)
  describe a cache that no longer exists.

**27 cannot keep their signature.**

- The pipeline (6): `compose`, `compose_blocks` (return a `Box`), `ship`,
  `Point`, `DrawCmd`, `draw` (take and return commands). `examples/minimal.py`
  is written on exactly these (its import at line 75), and it is the example
  the facade was created for (`__init__.py:55-58`).
- Blocks that are node tuples (3): `TextBlock` (`.material`, `.marker`),
  `CodeBlock` (`.lines`), `TableBlock` (cells' `.material`).
- Material coordinates (9): `ColourSpan`, `colour_text`, `find_spans`,
  `code_position`, `material_text`, `FindIndex`, `TextMatch`, `FormulaSpan`,
  `formula_spans`. Same purpose, positions in text instead of node indices.
- Formulas as boxes (3): `MathBuilder`, `Typeset` (`.box`), `typeset`.
- Tables over blocks and a measure (5): `SizedTable`, `with_columns`,
  `table_columns`, `ColumnLimits`, `table_column_limits`; they come back with
  R6.
- `SetLine` (1): glyph positions read off commands
  (`ui/bare_view.py:436-455`); comes back with R3's line query.

Outside the facade and reached "with no promise" (`__init__.py:100-115`), all
gone: `advtext.nodes`, `pack`, `linebreak`, `shipout`, `selection`, `math`,
`draw_runs`, `LayoutCache`, `ui.units`.

### Behaviour a user would see, already recorded in `docs/divergences.md`

- **A first-line indent is applied once.** The reference applies it twice: a
  box in the material and a narrower, shifted first line ("public-api", item
  1). No default style sets one (the field's default at `style.py:700` is the
  only occurrence of the name in `style.py`), so default documents do not
  move.
- **White space collapses across runs.** Two styled runs meeting inside a run
  of spaces set one space, not two; a space after a hard break is dropped
  ("public-api", item 2).
- **Fixes toward `tex.web`** (D32.3): the italic correction with a subscript
  (§755), underline depth (§735), and whatever task `tex-fixes` lands. Formulas
  with those shapes change by scaled points.
- **Total fit has a work budget.** A ragged paragraph asked for by total fit
  that would take the reference seconds is broken by first fit and says so
  ("stress", item 1). The reference's `PROSE_BREAKING_TOTAL_FIT_RAGGED` user is
  the one who meets it.
- **A font the reference would draw can be refused for drawing** by the strict
  structural pass ("stress", item 8), and USE_TYPO_METRICS is cleared in the
  atlas's copy so that FreeType sizes a font as layout does (item 11): glyphs of
  such a font sit differently, and correctly.
- **Escapes around dollars**, only if the core's scanner is used rather than
  explicit ranges: `\\` shows two backslashes, `\*` shows both characters
  ("api-math", item 1). With markdown-it still in front and explicit ranges
  (R1), this does not arise.
- **The 16-bit `ImWchar` path is irrelevant in the bundle.** The stand-ins of
  D15 exist for a host built with ImGui's default 16-bit `ImWchar`. The
  reference's own `ImWchar` is 32 bits ("stress", item 9; D8), and it runs on
  the bundle's ImGui, so a Python user never takes that path.
- **Item size is whole pixels and a line is 1.35 em by default** (the header's
  "THE ITEM"). The reference's default leading is its theme's.

Not in `docs/divergences.md`, and would be new: every one of the fifteen
block-layer shortcuts the `oracle-blocks` result lists, if D32.3 fixes them in
the product (a bar cut by a code block inside a quote, tight and loose lists
set alike, an item holding only code losing its marker).

### Performance, view by view

Reference figures are this machine's unless marked (`docs/performance.md`); the
design note's are another machine's
(`../imgui-adv-text/docs/cpp-extension.md:157-196`).

| view | pays today | after | still paid in Python |
|---|---|---|---|
| `BareView`, steady frame | full draw 12.8 ms; culled 5.7 ms (note: 13.7 to 15.9 and 5.1 to 5.9) | 0.100 ms; 0.039 ms | assembling ink ranges and fills for what is visible; hosts |
| `BareView`, a resize | 102 ms for the document, 104 with the selection index | 0.47 ms; 0.53 | nothing per block |
| `BareView`, font setup | 1.25 to 1.30 s for six faces; 0.8 s for the math face alone | 4.9 ms; 2.9 ms | |
| `StreamView`, per frame with new text | parse 0.28 to 2.14 ms + layout 0.33 to 8.32 ms + frame 0.10 to 0.87 ms (note: 1.4 to 2.2 ms a view) | layout 0.011 ms median, 0.110 worst | **the parse, all of it**, and one description of the changed block across the binding |
| find | `FindIndex.of` 13 ms for 740 blocks, then 0.25 to 1.9 ms a search; `find_spans` 10 to 12 ms (`../imgui-adv-text/docs/adoption-plan.md:406-409`) | unchanged | all of it; the hit-to-rectangle step moves to the core |
| `DiffView`, steady frame | 4.3 to 9.4 ms, about 3.3 µs a glyph (`…/adoption-plan.md:371-376`) | unchanged unless 2.3 is decided | all of it |
| `Diff.from_texts` | 3.8 to 4.2 s at 50,000 lines (`…/adoption-plan.md:364-367`) | unchanged: `difflib` | all of it |

**Not measured anywhere:** the cost of a call across the binding, the cost of
describing a block through it, and any figure for a Python process drawing
through the core at all. Task `bundle-research` is taking the first.

---

## 5. The oracle problem

Today the reference's pure-Python core is what imtypeset is checked against:
`tools/oracle/*.py` run it and write `tests/golden`, and the C++ tests replay
those files exactly. **Once advtext runs on imtypeset, running it produces
imtypeset's answers.** It cannot check the thing it is made of. Three
consequences, plainly:

- From that moment the goldens on disk are the only record of what the
  reference computed. They were 146 files and about 1.65 million lines when counted
  (`find tests/golden -type f`; task `font-wild` was still adding to them).
- No golden can be regenerated, extended or re-derived under a patched
  reference (D32.3's method) unless the pure-Python tree still exists somewhere
  and still runs.
- Anything the reference can do that has no golden yet becomes unrecoverable as
  an oracle the day its Python is rewritten.

### What has to be preserved

1. **The tree.** Commit `a3c589a1ea150182cc7d7deefa0eccff680a5b7b`. The working
   tree differs from it in four paths, none under `advtext/`
   (`git -C ../imgui-adv-text status --short`):

       M  docs/cpp-extension.md
       M  docs/guide/README.md
       M  tests/test_docs.py
       ?? docs/postmortem-2026-10-adoption-and-demo.md

   `git diff --stat` shows 312 insertions and 85 deletions, all but two lines
   in `docs/cpp-extension.md`. So the code every golden was dumped from is
   exactly `a3c589a`'s. (Ignored files were not listed; `.gitignore` covers
   `.venv`, caches and `*.ini`.)
2. **All of `advtext/`, not just the layout core.** The oracle scripts reach
   into the modules a wrap would rewrite, and into private names:
   `advtext.ui._stacked`, `_text_boxes`, `_skips`, `_block_boxes`,
   `compose_blocks`, `compose`; `advtext.ui.units.ship_unit`,
   `_after_hard_breaks`, `_originals_within`;
   `advtext.ui.bare_view.block_kind`; `advtext.markdown.build_document`,
   `text_block`, `_stand_in`; `advtext.spans.map_text_block`;
   `advtext.mathtext.markdown_math`, `parse`; `advtext.ui.fonts`;
   `advtext.ui.pages` (read off the scripts' imports and attribute uses). A
   freeze of "the core modules" would not run a single one of
   `dump_para.py`, `dump_blocks.py`, `dump_para_map.py` or `dump_math.py`.
3. **Its environment.** Python 3.11.2; `imgui_bundle` 1.92.900; fontTools
   4.66.0; markdown-it-py 4.2.0; numpy 2.4.6 (the reference's `.venv`). The
   bundle matters three ways: its ImGui and FreeType draw the vertex goldens,
   and **the reference's fonts are the bundle's own asset files**
   (`fontlib.py:272-277`). A different wheel can change `tests/golden/font` and
   `tests/golden/draw` with no change to any Python.
4. **The machine's fonts**, for `tests/golden/font-wild` only (2,991 installed
   faces). Those goldens are not regenerable elsewhere and should be treated as
   a census, which is what they are called.

Nothing records any of this today: no golden's header names the commit or a
version (`tests/golden/blocks/demo.golden:1-5` names the script and the format
only), and the scripts take whatever is at `../imgui-adv-text`.

### Proposal

1. **Freeze the pure-Python tree as `advtext-reference`**: an annotated tag on
   `a3c589a`, and a branch of the same name for the day a document or a
   comment needs fixing. `advtext/` on that branch does not change; a commit
   that touches it is a new reference and every golden is regenerated and
   reviewed. This is the user's action: nothing here may write to that
   repository. Whatever advtext becomes continues on its main branch.
2. **Pin the oracle scripts to it.** They take the reference's path from one
   place (an environment variable with a default of a checkout of the tag
   beside this repository, not `../imgui-adv-text` at whatever it is), and
   refuse to run unless `git rev-parse HEAD` there is the pinned hash and
   `git status --porcelain -- advtext` is empty. Both are reads.
3. **Stamp provenance into every golden**: comment lines with the reference's
   commit, the Python version and the four package versions. Comment lines are
   skipped by the reader (D10), so no test changes. Keep the resolved package
   list beside the scripts (`tools/oracle/reference-environment.txt`).
4. **Treat the goldens as the durable record, and say so.** Add a manifest of
   their SHA-256s; a regeneration that reproduces the manifest proves the
   reference still runs as it did, and one that does not is looked at before
   anything is accepted (the rule `CLAUDE.md` already has). If the day comes
   when the reference cannot be run at all (the wheel for that Python is gone),
   the goldens are still valid evidence of what it computed, and the tests
   still hold the port to them; only extending them is lost.
5. **Dump what has no golden before the reference is touched.** From
   `.concerns/oracle-blocks--builder-22--result.md`, "Not covered", and this
   document: tables (column widths at several measures, wrapped rows, the
   header rule, `with_columns`), column splitting, copy across blocks with
   separators and markers, `HostBlock`, links and decorations, `Faces.mono()`,
   stream commit boundaries and tail stability, and `layout_units` against
   `compose_blocks` (they already disagree on an empty first code block, same
   result file, item 1). This is the one step with a deadline.
6. **Do not import the two under one name.** The frozen tree and its successor
   are both the package `advtext`; a process holds one. The oracle scripts
   already run in a process of their own with `PYTHONPATH` set, which is
   sufficient. Renaming the frozen package would make it no longer the commit
   that was tagged.
7. **Reverse the direction for the wrapper.** The new advtext is tested by
   replaying the same goldens through its own API (blocks in, positions and
   vertices out), which makes the goldens the contract between three things:
   the frozen reference, the C++ core, and the Python layer on top.

What no freeze provides: an oracle for what the reference never had. Accents,
matrices, extensible delimiters and a LaTeX front end have no executable
specification here. For those the oracle is TeX itself, run as a program and
read through its box dumps, or `tex.web` and hand-checked cases; D23 already
makes `tex.web` the structural authority.

---

## 6. The LaTeX posture

Not built, not designed in detail. What follows is what such a layer would hand
over, what it would find missing, and what should be fixed now so that it is
not blocked later.

### What a full LaTeX front end would hand the core

- **Math mode: a math list**, through a public builder. TeX's own interface
  between the parser and the typesetter is the list of noads, and the engine
  here already takes exactly that: `MathItem{kind, limits, nucleus, sup, sub}`
  and fractions, with fields that are a character, a sublist, prose, a
  ready-made box, or mu glue (`AtomType`, `FieldType`, `MathItem` in
  `src/math_atoms.h`), converted by `mlist_to_hlist` (`src/math_mlist.h`). The
  subset parser `src/mathtext` is one producer of such lists; a LaTeX parser
  would be another, beside it and not through it.
- **Text mode: blocks**, as in R1. Paragraphs with runs, lists, headings,
  displays. A LaTeX `\section` and a markdown `##` are the same block.
- **Macros, environments, counters, references**: entirely the front end's.
  The core never sees a control sequence.

### What the engine lacks for it today

From the header comment of `src/math_atoms.h` ("Not here, as in the
reference"), the reference's stated omissions (`math/atoms.py:45-52`,
`math/mlist.py:80-92`) and `RefusalKind` in `src/mathtext.h`:

| lacking | where it shows | TeX |
|---|---|---|
| Alignments: `matrix`, `pmatrix`, `array`, `cases`, `aligned`, `substack` | refused as `MATRIX` and `ALIGNMENT` (`mathtext.py:1059-1081`) | `\halign`, §768 to §812 |
| Extensible construction from the MATH glyph-assembly table | "glyph assemblies of the MATH table" are not read (the header comment of `src/opentype.h`); a delimiter or radical taller than the tallest ready-made variant is refused, `MathError::DELIMITER_TOO_SMALL` (`src/math_mlist.h`) | `var_delimiter` (§706), its extensible half §713 |
| `\mathchoice` | no atom kind | §731 |
| Accents | `AtomType::ACCENT` is declared and the conversion refuses it, `MathError::NOT_IMPLEMENTED` (`src/math_mlist.h`) | `make_math_accent`, §738 |
| Delimiters around a fraction (`\atopwithdelims`), a radical's degree, `\middle`, `\nonscript` | not built (`math/mlist.py:80-88`) | §748 and macros |
| Penalties after binary operators and relations | not built; an inline formula is one unbreakable box (`math/mlist.py:89-92`, which says §766) | §761 chooses the penalty, §767 inserts it |
| Horizontal extensibles: `\overbrace`, wide arrows, wide accents | variants are read both ways (`math_vertical_variants`, `math_horizontal_variants`, `src/opentype.h`) but nothing assembles or uses the horizontal ones | macros over `\halign` and leaders |
| Displays with a number, several lines, alignment points | a display is one centred formula | §1199 to §1206 |
| Box operations: `\phantom`, `\smash`, `\raise`, `\llap` | expressible only as a ready-made `SUB_BOX`, which nothing public can build | |
| Colour inside a formula | no channel | not TeX |

### What must be decided now

1. **That the math list is a public, stable construction API**, and its shape.
   It should be a builder over an opaque list (begin a list, append an atom or
   a fraction, open and close a sublist, set a field), not the internal structs
   and not arena indices: `Field` today names a box by its index in a
   `NodeArena` (`Field::box`, `src/math_atoms.h`), which cannot be promised. It is
   the only way a second front end reaches the engine without going through
   `$...$` source.
2. **That its enums only grow, with the missing kinds given their names now.**
   The header's rule is "enum values are written out and only ever appended
   to". `ACCENT` is already there. Naming a choice, an alignment and a phantom
   kind today, refused until built, costs nothing and means a front end's
   output does not change meaning when they arrive.
3. **That every item can carry a source range.** A refusal has to point into
   the source the user wrote, which the front end knows and the engine does
   not. Two integers per item, opaque to the engine, returned with any error
   about that item. The reference's `Refusal.position` is this for one
   parser.
4. **One refusal protocol for every front end.** A formula is set whole or
   refused whole, never in part; a refusal is a kind from an enum that only
   grows, the item's source range, and one sentence. The parser's kinds
   (`RefusalKind`, `src/mathtext.h`) and the engine's (`MathError`,
   `src/math_mlist.h`) become one public enum. What is shown instead is the front end's choice
   (the source as code is the default the reference chose). Today the public
   struct carries the sentence and a position and no kind (`ImTypesetFormula`).
5. **Bounds as published numbers.** Nesting 64 (`MAX_MATH_NESTING`), items
   2^20 (`MAX_MATH_ITEMS`), the subset parser's own 24 (`MAX_DEPTH` in
   `src/mathtext.h`), boxes 1,024
   deep (D31). A front end that expands macros can reach any of them from a
   short source; it needs to know them, and exceeding one is already a refusal
   and not a crash (D33).
6. **That a paragraph's text can hold an object that is not text.** An inline
   formula built from a list, a host's own box, later an inline image: each is
   a thing with a width, a height and a depth, one position for selection, and
   a text it copies as. The explicit formula range of R1 is the first case.
   Deciding now that runs can bind a range to an object is what lets the LaTeX
   layer place its formulas without the dollar scanner.
7. **That tables are built over boxes, not over paragraphs.** A matrix is an
   alignment whose cells are math lists; a table is one whose cells are text.
   If R6's column solver takes cells that are already boxes with a natural and
   a minimum width, `array` and `cases` can reuse it. If it is written into the
   paragraph layer, they cannot.
8. **Where its oracle comes from.** Not the Python reference, which has none of
   this (section 5). TeX itself, and the MATH table's assembly records read
   against fontTools as the font goldens are.

Nothing in 1 to 7 requires implementing anything a LaTeX layer needs. Each
only keeps a door from being closed by the Document API's first release.

---

## 7. Recommendation

The three options, and one the evidence adds.

**(a) advtext wraps imtypeset, as a separate project.**
Keeps: `stream.py` (1,673 lines of commit-boundary logic tuned against
markdown-it), `diff.py` and `DiffView` (4,689), find, themes, hosted blocks,
the views' interaction shell.
Costs: bucket C's 10,387 lines leave; of bucket B's 18,700 the node-making half
of `markdown.py`, 700 lines of `spans.py`, the composition and cache of
`ui/__init__.py`, most of `ui/units.py` and the drawing and selection parts of
`ui/bare_view.py` are rewritten against a different data model; 27 of 94 facade
names break and 19 shift; the 63-file test suite stays with the frozen tree and
a new one is written; two repositories with a version triangle (advtext needs
an imtypeset that is inside a bundle release). It depends on Python reaching
the core at all, which is unproven here. And it has no consumer waiting:
pptmstr imports nothing from advtext.

**(b) A `python/` package in this repository replaces advtext.**
The same Python, selectively carried over (`stream.py`, `diff.py`, the parse
half of `markdown.py`), versioned with the core and tested on the same
goldens.
Costs: everything (a) costs in rewriting, plus Python packaging and a
markdown-it dependency in a repository whose shape is "C++ over ImGui, bindings
generated on top"; and a circular release, since the package needs bindings
that live in imgui_bundle, which vendors this repository. Gains over (a): one
version, API and consumer changed in one commit while the Document API is
still unreleased.

**(c) Markdown in C++.**
D32.4(iii) already has it on the backlog as an optional separate file.
Costs: a parser choice (md4c, or a small one); restating the reference's rules
(asterisk-only emphasis, the dollar rule, which `scan_math` already has
natively); a streaming story without markdown-it's token maps; no oracle for
where the two parsers differ; and it does nothing for diff.
Gains: every host language gets markdown, math and streaming, which is the
stated goal ("adoption by C++, Python and other language implementations");
the parse, which after a swap is the whole of a streaming frame's Python cost
(2.5), moves too; no per-block traffic across a binding.

**(d) Neither wrap nor replace: hosts call the Document API; advtext is the
frozen reference and nothing else.** pptmstr already has a block segmenter
with a never-revise invariant and an inline tokeniser
(`pptmstr:pptmstr/ui/blocks.py:418`, `pptmstr:pptmstr/ui/inline.py:85`). Its
blocks and runs are a block description. It would use the bundle's bindings
directly and lose nothing it has today.

### What I would choose

**Build the host-neutral Document API first (section 3, R1 to R5), and take
(a) only afterwards, as a new thin layer and not as a retrofit.**

The reasons, in order:

1. The Document API is the common prerequisite of (a), (b), (c) and (d), and
   the only part whose design is constrained by all of them. Nothing in R1 to
   R5 is specific to advtext; the block layer is already being built for D32.
2. What is worth keeping in advtext is what is *not* layout: about 4,700 lines
   of diff, 1,700 of streaming, the markdown-it rules and the views' shell.
   Carrying those onto a new core is a small project. Preserving the present
   facade is a large one with no beneficiary, since nothing imports it.
3. Separate, because a `python/` directory here buys atomic changes at the
   price of a circular release and of bending a C++ extension's repository
   around a Python package. D32.1 points the same way.
4. Section 5's step 5 has a deadline and the rest does not: dump the goldens
   for tables, columns and cross-block copy *before* anyone edits the
   reference. The freeze itself is cheap and should happen whichever option is
   taken.

So: (d) is what the first host does, (a) is how advtext's deeper functions
come along once that works, and (c) is taken when a second language asks or
when the measurements below say so. (b) I would not take.

### What would change my mind

- **Toward (c), sooner:** a measured binding cost that makes describing blocks
  from Python expensive (thousands of runs per document, each crossing); or
  the markdown-it parse measured over pptmstr's 1.7 ms line in a real
  transcript once layout is native (this machine's samples reach 2.14 ms for
  the parse alone, `docs/performance.md`); or a C++ host asking for markdown;
  or md4c shown to give the same blocks as markdown-it on the demo documents,
  which would remove the "no oracle" cost.
- **Toward (b):** `bundle-research` concluding that imtypeset cannot be loaded
  beside the installed bundle and must be built against the bundle's own ImGui
  to be called from Python. Then the Python-reachable build is made from this
  repository anyway, and the Python layer may as well be built and tested with
  it.
- **Toward (d) alone, advtext frozen for good:** if pptmstr, on reading R1 to
  R5, finds its own `BlockCursor` and `parse_inline` sufficient and wants diff
  as a separate small package. Then nothing of advtext needs to run on the new
  core.
- **Against the whole shape:** evidence that some consumer does depend on the
  present facade. None was found in the three trees read; anything outside
  them was not looked at.

---

## What was not read

- **Not opened at all:** `ui/app.py` and the sixteen files of `ui/pages/`
  (9,531 lines), classified from their import lines; `ui/formulas.py`,
  `ui/specimen.py`, `backends/svg.py`, `ui/fonts.py` beyond their imports;
  `nodes.py`, `pack.py`, `linebreak.py`, `shipout.py`, `units.py`,
  `badness.py`, `fontmetrics.py`, `math/mlist.py` and `math/atoms.py` beyond
  the lines cited (they are the modules already ported and held by goldens);
  the reference's 63 test files, counted and searched for imports only; its
  `docs/decisions.md`, `status.md`, `tex-notes.md` and the postmortem.
- **Read in part:** `ui/diff_view.py` (about 1,150 of 2,659 lines, by a
  sub-agent; the rest by definition lines); `ui/document_view.py` (lines
  150-600); `diff.py` (lines 1-345, then definition lines); `style.py` (about
  350 of 1,038 lines); `mathtext.py` (the lines cited);
  `../imgui-adv-text/docs/adoption-plan.md` (lines 1-140, 362-444, 969-1110,
  1250-1290, 1414-1470 of 1,614); `../imgui-adv-text/docs/cpp-extension.md`
  (sections 3, 4, 6 and 9).
- **Read by sub-agents in full, spot-checked by the author:** `markdown.py`,
  `spans.py`, `stream.py`, `columns.py`, `blocklayout.py`, `selection.py`,
  `ui/__init__.py`, `ui/units.py`, `ui/bare_view.py`, `ui/stream_view.py`,
  `backends/imgui_draw.py`. A citation in this document that was not among the
  spot-checks rests on the sub-agent's reading.
- **In this repository:** `src/imtypeset.cpp` and `src/context.h` were searched,
  not read; `src/style.h`, `src/shipout.h`, `src/nodes.h`, `src/linebreak.h`
  not read; `src/draw.h` lines 1-330; `src/selection.h` lines 1-60 and its
  declarations; `src/math_atoms.h`, `src/math_mlist.h`, `src/mathtext.h` by
  their declarations and the lines cited; `docs/performance.md` lines 86-131
  and 430-519; `docs/divergences.md` lines 1-76, 126-194 and 377-561;
  `docs/port-hazards.md`, `docs/hardening.md`, `docs/font-support.md` and
  `docs/distribution-notes.md` not read. `../imgui_bundle` was not opened
  beyond a directory listing.
- **Not run:** anything. No build, no test, no reference function. Every figure
  is quoted from the document or board note named beside it.
- **Golden counts in section 1** ("593 cases", "4,613 conversions", and the
  like) are the builders' own reports on the board and in D30 and D31; they
  were not recounted.
