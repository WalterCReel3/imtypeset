# advtext full swap: the core's half

Kept current by imtypeset's coordinating session for the session working in
`../imgui-adv-text` (branch `experimental/imtypeset-adoption`), which reads this
tree and cannot write it. One entry per numbered item of the swap request.
States: **done**, **in progress**, **queued**, **declined** (with the reason),
**needs the user**. A decision an agent took is marked *agent decision*; the
user is the only one who moves the border.

Last updated: 2026-10-11. Commit `12d77ac` has items 2, 6, 7, 8. Item 5 (stage 0 of
`tables-core`) is in the working tree, green in debug and under ASan + UBSan, and
waits for its commit: the coordinating session names it under "Build from".

## Build from

| | |
|---|---|
| commit | **`12d77ac`** on `main` ("Pin the oracle, answer advtext's API questions, add the galleries"). Before it: `5654eee`, the tree the release gate passed |
| what it is | `5654eee` (every preset from clean) plus items 2, 6 and 7. On `12d77ac` itself only the `debug` preset was run: 43 of 43. Rebuild the library: the Python package of this commit refuses one built from `5654eee` |
| how | `python/host/build_bundle_library.sh` as before; one library per interpreter |
| pushed? | no — the session cannot reach the network; the local repository has it |

A library built from a working tree with uncommitted changes is not
reproducible, and this tree will have such changes while the items below are
in progress. Build from a commit named here. Each later commit that changes
what advtext can call is listed under "API changes".

## API changes since `5654eee`

**In `12d77ac`** (tasks `swap-api-answers` and `galleries`). Nothing was renamed or
removed. A library and the `python/imtypeset` package must come from the same
commit: the package refuses a library that lacks a function it declares.
Also new in `12d77ac`, from `galleries`: `ShowDemoSection` /
`GetDemoSectionName` / `ImTypesetDemoSection_` (C: `ImTypeset_ShowDemoSection`,
`ImTypeset_GetDemoSectionName`; Python: `imtypeset.demo`), see item 6.

| what | C++ | C | Python | note |
|---|---|---|---|---|
| a role's font by its place in the atlas | — (a C++ host has the pointer) | `ImTypeset_GetFontAtlasIndex(face)`, -1 for none | `imtypeset.font_atlas_index(face)` → `int` or `None` | new. `imgui.get_io().fonts.fonts[i]` is then the wheel's own `ImFont` object for the role: answer (f) |
| no draw list means the current window's | `DrawParagraph(p, NULL, pos)`, `DrawDocument(d, NULL, pos)` | the same, `draw_list` null | `Document.draw(None, pos)`, `Paragraph.draw(None, pos)` | was refused (`InvalidArgument`); still refused outside a frame. Request C1: `typesetter.window_draw_list()` and its mangled symbol are no longer needed |
| `GetFont` with the ImGui context gone | null unless the context's ImGui context is current | the same | `get_font()` → `None` | was a dangling address: answer (c) |
| a refused `import imtypeset` | | | leaves no `imtypeset.*` in `sys.modules` | answer (e) |
| `Document.pack_marks` | | | same result, about four times faster | request C3, in part |
| one section of the demo window, as a page of the host's | `ShowDemoSection(ImTypesetDemoSection)`, `GetDemoSectionName(section)`, `enum ImTypesetDemoSection_` (11 values, `_COUNT`) | `ImTypeset_ShowDemoSection`, `ImTypeset_GetDemoSectionName`, `enum ImTypeset_DemoSection_` | new module `imtypeset.demo`: `DemoSection`, `sections()`, `section_name(s)`, `show_demo_section(s)`, and `show_demo_window` again | new (task `galleries`): item 6 |
| the demo window in the sidecar | | | `imtypeset.show_demo_window()` draws the window in a bundle-hosted library | was an empty function there (`IMTYPESET_DISABLE_DEMO`); the library is built with the demo now |

**After `12d77ac`, not yet in a commit** (task `tables-core`, stage 0: item 5 and
FS-S2 section 4's notes; D56). Nothing was renamed or removed. Two things a
caller can notice: `Hit` has a fourth field, and a library and a package (or a
C++ host) from either side of this change refuse each other, because
`sizeof(ImTypesetHit)` is part of the version check.

| what | C++ | C | Python | note |
|---|---|---|---|---|
| R1: every line of a block exactly its line height | `ImTypesetBlockFlags_FixedLineHeight` (`1 << 3`) on `ImTypesetBlock::Flags` | `ImTypeset_BlockFlags_FixedLineHeight` | `BlockFlags.FixedLineHeight` | new. Line `n` of the block is `n` line heights below its top whatever glyphs it holds; the height is `BlockStyle.LineHeight` ems, rounded once to a scaled point (about 8e-6 px a line at 20 px). A taller glyph overhangs into the next line's box instead of moving it. Text and code blocks. Two documents with the flag stay in step to the bit |
| R1b: does a role's own font have a glyph | `FaceHasGlyph(face, codepoint)` | `ImTypeset_FaceHasGlyph(face, codepoint)` → 0 / 1 | `imtypeset.face_has_glyph(face, char_or_codepoint)` → `bool` | new. The role's font alone, not the faces a missing character is borrowed from. Needs no ImGui context. `CodeBold` answers for `Bold`'s font until it has its own |
| the tag under a point of a paragraph | `ImTypesetHit::Tag` (appended) | `ImTypeset_Hit::Tag` | `Hit.tag` (a fourth field, default 0) | new. **`a, b, c = paragraph.hit_test(...)` no longer unpacks: read the fields by name** |
| the tag under a point of a document | `ImTypesetDocumentHit::Tag` | | `DocumentHit.tag` | **fixed**: it answered the run of the character the caret was BEFORE, so the right half of a link's last letter gave 0 and the right half of the character before a link gave the link. It is now the character the point is on |
| a paragraph's natural width | `GetParagraphLine(p, 0)`: `Max.x - Min.x` after a layout wider than the text | the same | `paragraph.lines()[0]`: `max[0] - min[0]` | no new call; stated at `GetParagraphSize`. It holds for every alignment (justified, centred and right-aligned text report the wrap width as their size, which is what tripped the probe) |
| slices of a document under clips that meet | `DrawDocument` / `DrawDocumentToBuffers` | the same | `Document.draw` | **changed**: a glyph whose quad is wholly at or below the clip rectangle's bottom edge, or wholly above its top, is no longer emitted. A clip ending exactly at a line's top no longer emits that line; the half pixel FS-S2 subtracted is not needed. Glyphs a cut goes through are still in both slices |
| a Table block in a copy | | | | **changed by the table stage, below**: a `BlockKind.Table` block is a table now, and copies as its cells |

**Tables (item 3; task `tables-core`, table stage; D57). In the working tree, not yet in a
commit.** A table is ONE block whose text is its cells; positions stay `(block, offset)`.
Read "Tables" in `include/imtypeset.h` first: it is the specification. In Python a table is
`Block(BlockKind.Table, rows=[[...], ...], columns=[...])` and nothing else changes shape.

| what | C++ | C | Python | note |
|---|---|---|---|---|
| a table block | `ImTypesetBlockKind_Table` with `ImTypesetBlock::CellStart`, `CellCount`, `ColumnStart`, `ColumnCount`; `ImTypesetCell {TextStart, TextEnd, Flags}`, `ImTypesetCellFlags_RowStart`; `ImTypesetDocumentContent::Cells`, `CellCount`, `Columns`, `ColumnCount` | `ImTypeset_Cell`, `ImTypeset_CellFlags_RowStart`, the same fields | `Block(BlockKind.Table, rows=..., columns=...)`: `rows` is a sequence of rows, a cell a `str` or `Cell(text, runs)` with runs indexing the CELL's str; the package joins cells with a tab and rows with a newline into the block's text, so `Block.text` is left empty | **renamed, never usable before:** `ImTypesetBlock::TableRows` / `TableColumns` (reserved, "leave zero") are `CellStart` / `CellCount`. A cell is text and runs like a paragraph: faces, scale, colour, `Tag` for links, `RunFlags.Formula` or the block's `BlockFlags.Math` for formulas. A row may have fewer cells than the table has columns |
| columns: alignment, stated widths or shares | `ImTypesetTableColumn {Align, Width, Share}` | `ImTypeset_TableColumn` | `TableColumn(align, width, share)` | `Width` (px, padding included) is used when EVERY column states one, else `Share` when every column states one, else the library fits the table (scaled up to the width; squeezed toward longest words and its cells wrapped when too wide). `Align.Justify` (the zero) is Left |
| header row, held columns | `ImTypesetBlockFlags_TableHeader` (1<<4), `ImTypesetBlockFlags_TableHeld` (1<<5) | `ImTypeset_BlockFlags_TableHeader`, `_TableHeld` | `BlockFlags.TableHeader`, `BlockFlags.TableHeld` | Header: row 0 gets the rule under it; how header cells look is their runs (Bold). Held: see "a table that grows" |
| padding and rule | `ImTypesetDocumentOptions::TableCellPadding`, `TableRuleThickness`, `TableRuleColor` | the same fields | `DocumentOptions(TableCellPadding=..., ...)` | 0 is the default (half an em; six hundredths of an em), below zero is none |
| a table that grows | `AppendDocumentTableCells(document, text, text_end, cells, n, runs, n)`; `AppendDocumentText` appends to the LAST CELL | `ImTypeset_AppendDocumentTableCells` | `Document.append_table_rows(rows)`; `Document.append_text(...)` | one call an edit. Rows already set are not set again unless a column's width changed (`DocumentStats.TableRowsSet`, `TableCellsMeasured`). **This replaces RQ-T7's stopgap: a table block CAN be appended to** |
| the streaming width rule | `ImTypesetBlockFlags_TableHeld` | | | each column is the widest its cells have wanted since the block was described: never narrower as rows arrive, and the table is as wide as its columns, not the pane. A column moves only when a wider cell arrives in it. If the held widths stop fitting, the default fitting applies until they fit. The widths survive `replace_blocks(i, 1, [one table with the flag and as many columns])`, so re-describing the table from re-parsed markdown each chunk keeps them. To settle at the end, replace once more without the flag (your `reset`), or keep it (your `keep_streamed`). It is `StreamView._held` / `_sized_to`, moved into the block |
| restating columns (a drag) | `SetDocumentTableColumns(document, block, columns, n)` | `ImTypeset_SetDocumentTableColumns` | `Document.set_table_columns(block, columns)` | rows are set again only if a width or an alignment changed; no cell is measured again |
| geometry out (RQ-T10, RQ-T4) | `GetDocumentTableGeometry`, `GetDocumentTableColumn` (`Min`, `Max`, `Natural`, `Floor`), `GetDocumentTableRow` (`Min`, `Max`, `CellStart`, `CellCount`, `Wrapped`) | `ImTypeset_GetDocumentTable…(…, out)` | `Document.table_geometry(block)`, `table_column(block, column)`, `table_row(block, row)` | `Natural` and `Floor` are a column's two limits whatever the width: what a drag handle is held between |
| positions (RQ-T8) | `ImTypesetPosition` as everywhere; `GetDocumentTableCell(document, position)` → `ImTypesetCellPosition {Row, Column, Cell, Offset}`; `GetDocumentTablePosition(document, block, row, column, offset)` | `ImTypeset_GetDocumentTableCell`, `ImTypeset_GetDocumentTablePosition` | `Document.table_cell(position)` → `CellPosition(row, column, cell, offset)`; `Document.table_position(block, row, column, offset)` | hit test, `select_range`, `range_rects`, `caret_rect`, `range_text` and `Mark` take and return the same `(block, offset)` they always did, and work in cells. A caret at the end of a cell is that cell's end (your golden script's `TMap.pos` says the next cell's start: not the API's convention) |
| which cell the mouse is in | `ImTypesetDocumentHit::Row`, `Column` (appended; -1 outside a table); `Tag` works in a cell | `ImTypeset_DocumentHit` | `DocumentHit.row`, `.column` (appended, default -1) | **`DocumentHit` is two fields longer: unpack by name** |
| what a copy gives | `AppendDocumentRangeText` | | `Document.range_text` | each cell's text, a tab between the cells of a row, a newline between rows, for every cell from the one the range starts in to the one it ends in. An empty cell keeps its tab; a row gives the cells it has. A copy of a whole table is the block's text. **Not the reference's in two places** (it drops a tab that has no text on one side, and pads a short row with tabs): see `docs/divergences.md`, "tables in a document" |
| find marks in cells | `ImTypesetMark` over `(block, offset)` ranges | | `Mark` | work in cells in all three places (Ink, Behind, Under). The reference could not mark a match in a cell; search each cell's text yourself (it is `Block.rows`) and hand the marks over |
| counters | `ImTypesetDocumentStats::TableCellsMeasured`, `TableRowsSet` | the same | `DocumentStats` | appended |
| layout check | `DebugCheckTableDataLayout` (inside `IMTYPESET_CHECKVERSION()`) | `ImTypeset_DebugCheckTableDataLayout`; `ImTypeset_BuildInfo` gained six `Sizeof…` fields | the import check | a library and a package from either side of this change refuse each other |

Limits: 1,024 columns and 262,144 cells a table (more is cut and said). A table is laid out
whole the first time any of it is looked at, and costs nothing while out of view.

(Between the first library advtext built and `5654eee`:
`ImTypeset_GetContextSerial` / `imtypeset.get_context_serial()` were added —
the package refuses a library without it, so rebuild; run trouble in
`set_blocks` / `replace_blocks` / `append_text` is `DescriptionCut`, a soft
outcome returning `False`, where it used to be `InvalidArgument`; a paragraph
with out-of-order runs likewise; `Document.edit_error` keeps what was cut.)

Rule from here on: no public Python name is renamed or removed without a line
in this section naming the commit.

## Items

| # | item | state |
|---|---|---|
| 1 | a committed, reproducible state | **done**: `5654eee` |
| 2 | freeze the oracle at `advtext-reference` (32e3953); goldens for tables, columns, cross-block copy | **done** (task `oracle-freeze`). See "The oracle" below |
| 3 | tables in the core | **in the working tree** (task `tables-core`; the commit is named under "Build from" when it exists): a table is a block of the Document in C++, C and Python. Names and behaviour under "API changes", "Tables"; decisions in `docs/decisions.md` D57; what is not the reference's in `docs/divergences.md`. RQ-T1 to T10 of `FS-S2` are each answered there. Not built: a table laid out in pieces (it is one block, laid out whole when first seen; an appended row costs that row plus about 0.2 microseconds for each row the table has, measured), cell borders or fills, column spans, hard breaks in a cell |
| 4 | columns in the core | **queued** behind 3 (task `columns-core`) |
| 5 | what DiffView needs | **done, in the working tree** (stage 0 of task `tables-core`; the commit is named under "Build from" when it exists): **R1** is `BlockFlags.FixedLineHeight`, **R1b** is `face_has_glyph`; names and behaviour under "API changes", reasons in `docs/decisions.md` D56, tests in `tests/test_api_requests.cpp` and `tests/python/test_binding.py` (`Requests`). *Agent decisions (the builder's): a flag and not a style field; the line's box is the pitch and taller ink overhangs it.* One thing to know: ink that overhangs its line by more than 2 px can go undrawn in the strip where its line is just outside the clip rectangle. The reproduction here used U+2320, U+27E8 and U+0416: the Inconsolata bundled in this tree has U+2502 and U+1EB2, so FS-S1's 22.4 px was not re-measured. **Declined**, as the request itself asks: anything exposing nodes, packing or draw commands — FS-S1 asks for none |
| 6 | specimen and formula galleries | **done** in `12d77ac` (task `galleries`): see "Galleries (item 6)" |
| 7 | open API questions, and C1–C8 | **done** for (a)–(g), in `12d77ac`: see "Answers (item 7)". Of C1–C8: C1 done, C3 in part, C8 answered in the header; C5 is item 3; C2, C4, C6, C7 queued, each with its reason |
| 8 | output that is not ImGui | **needs the user**: assessed, nothing built; the recommendation and what each choice costs are under "Border questions" |

## Things the advtext side should know

- **Do not draw or call a widget after `imgui.end_frame()` unless
  `imgui.render()` followed it.** *Run here under ASan + UBSan:* `new_frame();
  end_frame(); paragraph.draw(None, pos)` (and `document.draw(None, pos)`, any
  widget, `Document.view`) crashes inside ImGui on a null current window. The
  library refuses these before the first frame and after `render()`; between an
  `end_frame()` and the next `new_frame()` with no `render()` it cannot tell
  where it is, because `imgui.h` gives no sign of that state (D56 lists what
  was looked at). A headless test that ends frames without rendering draws
  before `end_frame()`, or passes a draw list it got inside the frame.
  Calling `imgui.get_window_draw_list()` yourself there crashes the same way.
- **One imtypeset context per ImGui context, destroyed with it.** *Read by a
  reviewer on your side of the border, not run here:* `typesetter.load_fonts`
  reuses one imtypeset context across ImGui contexts; where a test makes a new
  ImGui context without destroying the old one, `add_font` is refused
  (`NoContext`), `load_fonts` then calls `disable()`, and imtypeset is silently
  OFF for the rest of that process. Call `destroy_context()` / `unload()` when
  the ImGui context goes, or make an imtypeset context per ImGui context
  (answer (c) below has the rule). The rarer case of a new ImGui context at the
  old one's address is queued here as task `swap-fixes`.

## Border questions for the user

- **The mathtext page.** The request lists `pages/mathtext.py` with specimen
  and formula as moving to the core; the user named specimen and formula. The
  core's demo already has a Math section with an editable source and the subset
  table; `galleries` treats the mathtext page's content as in scope and will
  say what it could not carry. *Agent decision, the other session's extension;
  unconfirmed.*
- **Item 8: output that is not ImGui.** Not decided; nothing was built. What
  follows is an assessment by an agent (`swap-api-answers`), from reading
  `scripts/render_litmus.py`, `advtext/backends/svg.py`, `tests/test_litmus.py`
  and the `svg` target (read by a helper agent and checked against FS-S3's
  section 5, not run).

  *What the litmus chain takes from the old core.* The flat ship-out list:
  for a glyph its font key (face and size), codepoint, x and baseline y; for a
  rule a rectangle; for the debug page each line's box and baseline and each
  glue's set width beside its natural width; for the selection page the
  selection model's rectangles; the page size from the packed box. Positions
  are unsnapped integer scaled points. Glyph shapes are outlines read with
  fontTools **by glyph name**, because the display sigma is a variant with no
  codepoint of its own. `make svg` writes five files from three layouts (a
  whole markdown document, the same with the debug overlay, a selection, the
  display sum, the sum beside its text-style form). `tests/test_litmus.py`
  (23 tests) asserts in scaled points on the command list, and several reach
  inside the algorithm: a line's fitness class, a box's glue order and glue
  set, a private rounding hook.

  *Choice A: a window-free positioned-glyph query in this library.* The
  smallest form: per laid-out paragraph or document block, a count and an
  array of `{face, em size, glyph, x, baseline y}`, and the rules as
  rectangles. `DrawDocumentToBuffers` already gives output without a window,
  but as textured quads of the ImGui atlas on whole pixels, and it needs an
  ImGui context: good for a raster picture, useless for outlines. What A
  would promise to every binding, for good: (1) which positions — layout's
  own, or the ones drawn after the pixel rules; they differ, the litmus wants
  the first and a non-ImGui renderer wants the second; (2) a glyph identity
  that is not a codepoint (a glyph index into the host's file, or a name), so
  the reader's view of a font's glyphs becomes public; (3) a second unit, or
  doubles: the header's lengths are `float` pixels, which cannot carry the
  exact scaled points the litmus tests compare; (4) nothing for the debug
  page or for the tests that look at fitness and glue set, which stay
  declined under item 5's rule (no nodes, packing or draw commands as API).
  Size: two or three structs, four to six functions, their C mirrors, Python,
  mapping rows and tests; about the size of the whole of answer (f) and C1
  together, several times over. It would serve one script and one test file.

  *Choice B: the litmus artifacts become this repository's tests and leave
  advtext.* The two litmus claims are claims about the layout core, and the
  core is here. Their geometry is already held exactly, against the reference
  itself, by the golden replays (`tests/golden/doc`, `blocks`, `math`,
  `para`); the checks that reach inside the algorithm can be written here as
  C++ tests, where tests may use internals; the pictures can be rendered with
  no GPU and no display by `tests/support/soft_raster.h`, as the images under
  `docs/images` are. Cost: advtext deletes `backends/svg.py`,
  `scripts/render_litmus.py`, the `svg` target, `test_litmus.py`,
  `test_svg.py` and the few `glyph_outline` tests (FS-S3 counts 46 tests in
  the two files), and its Formula page stops saying "use `make svg`"; the
  artifacts become PNG where they were vector; someone here writes the two
  litmus pictures and the handful of internal checks as tests (not sized; a
  small task). What is lost for good is an SVG a reviewer can zoom.

  **Recommendation (an agent's): B.** A public glyph-position API is a
  permanent promise in three languages made for one diagnostic script, and it
  could not serve that script's debug page or a third of its tests anyway. If
  vector output is wanted later, the cheaper route is a program in this tree
  that uses the internals directly, not a public query.

### The oracle (item 2)

The core's goldens no longer depend on advtext's working tree or on which
branch it has checked out. The reference is commit `32e3953` (your tag
`advtext-reference`), taken with `git archive` from your repository's object
store into `build/advtext-reference` here, and every dump script refuses to
run against anything else — your working tree included, which is what the
reference interpreter's editable install would otherwise import. Nothing was
written into your repository. **Two things there the oracle still needs: the
commit `32e3953` must stay reachable (keep the tag, or any branch containing
it), and `.venv` must stay as it is** (Python 3.11.2, imgui_bundle 1.92.900,
fontTools 4.66.0, markdown-it-py 4.2.0) — the interpreter is not something a
commit holds. Deleting `advtext/`'s layout core from the working tree, on any
branch, no longer affects the core.

Every existing golden was dumped again from the pinned tree and no record
changed. One correction to what was passed on: `a3c589a` has the tag's tree;
`30496e1` does not (it adds the postmortem and revises `cpp-extension.md`;
nothing under `advtext/`).

New goldens for the swap, `tests/golden/doc-tables` (format:
`tools/oracle/FORMAT-doc-tables.md`): a table among other blocks — every
position as (block, row, column, byte in the cell's text), row and cell
rectangles, hit tests, drags, selection rectangles and the exact copied
string through cells and across the table's edges (`\t` between cells, `\n`
between rows, row-major), find hits and links in cells, and a table arriving
row by row with the widths held and the widths set at each step. Table
layout and column cutting were already held by `tests/golden/tables`.

What the reference does not do, and the core will therefore do without an
oracle: selection and copy across the columns of a multi-column layout
(advtext draws columns and never selects in them); find marks drawn inside
cells (a colour span on a table is ignored; only a match's rectangles
exist); balanced columns; a caret rectangle in a cell. FS-S2's RQ-T1 to T8,
T10 and RQ-C1 have goldens; RQ-T9 (lazy), RQ-C2's selection across slices and
RQ-C3 do not.

## Answers (item 7)

Each was first reproduced here by a test. Tests: `tests/test_api_answers.cpp`
(C++, 8 tests), `tests/python/test_binding.py` (8 new), and in both wheels
`tools/bundle_route/check_swap_answers.py` (18 checks, passing in
imgui_bundle 1.92.900 and 1.92.801). The header says the same things at the
places named. State: in the working tree, not committed.

**(a) `range_text` around a block that gives nothing.** Reproduced: nothing
and no separator. **Kept, stated and pinned.** The separator stands between
two blocks that each gave text, once, however many blocks between them gave
none: a rule, a Host block with no text, an empty block, a Code block of
blank lines, a block the range only touches the edge of. Text, rule, text is
the two texts with one separator, which is what the reference copies
(`UnitModel.copy_text`: one `BLOCK_BREAK` before the later block; read, not
run). A range that starts or ends in such a block has no separator at that
end. A Host block WITH text gives it whole, behind a separator. Not the
reference's: between two blocks of one list the reference copies a single
newline, and the library always writes the one separator it is given (list
markers are not copied either; both were already known on your side).
Header: "WHAT A COPY GIVES". Python: `Document.range_text`.

**(b) A Mark over part of a Formula run.** A formula is one thing: a mark
takes it whole when the mark's range holds the formula's FIRST character
(the first character of a `RunFlags.Formula` run; the opening dollar of a
dollar formula) and takes none of it otherwise. So: a mark strictly inside
the source draws nothing; one over its first character alone marks all of
it; one from the text before into its middle marks that text and the whole
formula; one from its middle into the text after marks only the text after.
The same in all three places (Ink, Behind, Under), and the same rule for a
selection, its rectangles and a copy. For find: a match inside a formula's
source can be shown only by marking from the formula's first character.
Header: `ImTypesetTextRange`, `ImTypesetMark`.

**(c) A context whose ImGui context was destroyed first.** Run under
ASan + UBSan, no report in any case below.
- No ImGui context current, or another one: every draw, `add_font`,
  `remove_font` and widget is refused with `ErrorCode.NoContext` (Python:
  `Error` raised); layout, hit tests and `range_text` still answer.
  **One defect found and fixed:** `get_font` returned the dangling `ImFont`
  address; it now returns `None`.
- `destroy_context()` is safe then, and with no ImGui context at all. It is
  the one thing left to do with such a context.
- **A new ImGui context at the address of the destroyed one is taken for the
  old one.** An address is all `imgui.h` gives to know a context by, and the
  allocator reused it in every unsanitised run here. What then happens, in
  every case tested: each role is found without a font (its `ImFont` is not
  in the new atlas), text reports `NoFont` and draws nothing, the host's own
  fonts are untouched, and after `add_font` again the kept documents lay out
  and draw. That is observed, not promised: two collisions were reasoned
  about and not reproduced (a host font landing at an old font's address
  with the same `FontId`; the first call into the library in the new context
  falling on the same frame number with the same number of fonts). Closing
  them needs the library to read two more ImGui fields and the wheel probe to
  learn them: **queued**, not built.
- **The rule for `typesetter.load_fonts()`:** do not reuse a context you find
  (`current_context()` says one exists, never that its ImGui context does).
  Call `destroy_context()` whenever the ImGui context is destroyed, or, if
  that moment is not yours to see, at the top of `load_fonts()` before
  `create_context()`. Both are safe at any time.
Header: `CreateContext` ("LIFETIME"), `DestroyContext`, `GetFont`. Python:
`destroy_context`, `get_font`.

**(d) A Code block whose text ends in a blank line.** Positions and copies
end at the block's last character: the line end after it and blank lines at
the end are not part of the block. `range_text(((b, 0), (b, len(text))))` is
`text.rstrip("\r\n")`; no position the library answers is past that, and
one given past it is that end. A line of spaces is not blank; blank lines
before the last character are kept. **Found and kept, now stated:** the
bytes are held all the same — `GetDocumentBlockTextSize` (C/C++) counts them
— and when `append_text` puts text after them they are inside the block
again. So a Code block may be handed over with its closing newline, or
streamed line by line, and nothing needs stripping on your side.
Header: "POSITIONS", `ImTypesetBlockKind_Code`, `GetDocumentBlockTextSize`.

**(e) A refused import.** Reproduced: `imtypeset._capi` stayed in
`sys.modules`, and another copy of the package tried afterwards got the
first copy's `_capi`. **Fixed:** a refused import removes every
`imtypeset.*` module; a second attempt in the same process, with the
variable set right or another copy of the package in front, starts clean
(tested for three kinds of refusal). **Not undone, and cannot be portably:**
the shared object stays mapped once loaded, so its static initialisers have
run; loading it again later is harmless. The `.gate` stamp file beside a
bundle library is deliberate and stays.

**(f) Seven more fonts in the atlas.** The library cannot adopt a font the
host added: what it adds is a copy of the file with its character map
rewritten (every codepoint layout can name, the unencoded math variants
among them, gets a glyph), one OS/2 bit cleared and, for some fonts, glyph
boxes repaired. Adopting a host font soundly would mean proving, per font,
that the host's rasteriser maps every codepoint as the reader does, that it
sizes the font as layout measures it, that the host's `ImFontConfig` moves
no glyph, and it could never cover the math face; it would also read several
more ImGui fields through the wheel probe. **Declined as an adoption API;
the sharing goes the other way and works today:** the host draws its own
text with the library's font and does not add the file a second time.

    index = imtypeset.font_atlas_index(imtypeset.Face.Code)       # new
    font = imgui.get_io().fonts.fonts[index]                      # the wheel's ImFont object
    imgui.push_font(font, 0.0)

Run in both wheels. Measured (`tests/test_api_answers.cpp`, FreeType, the
seven test faces, 1.68 MB of files): the role's font and a host's copy of the
same file set the same words to the same width and height at 13, 16 and
24 px for the six text faces; a second copy of every face costs 1.85 MB of
ImGui allocations before a glyph is drawn and 1.88 MB more once the same
words are drawn in both, the atlas textures going from 1.84 MB to 3.15 MB.
Differences a host can see in the library's font: `OversampleH` is 1 (it
matters under stb_truetype only; not run under stb), and under FreeType a
font that asks for OS/2 typographic sizing is sized by `hhea` (the six text
faces here measure the same either way). For advtext after the swap: stop adding the seven faces
yourself; keep the one UI font, and take any role you want for ImGui text
from the library.

**(g) C1–C8.**

| | request | outcome |
|---|---|---|
| C1 | draw without finding the draw list | **done**: `draw(None, pos)`; see API changes |
| C2 | say how much the height above the window changed, or anchor; better estimates for prose | **queued**. It is a change to how unlaid blocks are estimated and to `DocumentStats`, in the sources `tables-core` rewrites next; it goes after tables, with your 244-of-815 figure as its test |
| C3 | find in the library, or marks from packed bytes | **in part**: `pack_marks` packs with `struct` (2.0 → 0.5 µs a mark for ASCII blocks, 2.5 → 1.0 otherwise, 2,000 marks, one run here). A `FindInDocument` is **declined for now**: find is advtext's by the border, and case folding is the host's policy |
| C4 | copying a range should not lay out the blocks inside it | **queued**. The copy is produced from the laid-out lines' model; a second way to produce it without lines must agree with the first to the byte (hard breaks, stand-ins, escaped dollars, formulas), which is a task with its own goldens, not a small change |
| C5 | tables | item 3 |
| C6 | line height and advances a host can match to ImGui's | **queued, not reproduced here**: the 8.01 px against 7.626 px was not re-measured, and which ImGui size was live in that pane is not known here. `Style.FontSize` (ImGui's meaning of size) against `EmSize` is the first thing to check on your side; not checked here |
| C7 | replacing the last block without the pass over all blocks | **queued** behind tables (same sources); your own measurement puts it below notice under 10,000 blocks |
| C8 | a tag separate from style, or overlapping runs allowed | **answered, no change**: runs do not overlap; a link over plain and bold text is two runs, each with the tag and its own face. The header says so at `ImTypesetRun::Tag`. Your converter already does this |

## Galleries (item 6)

The library's demo has the three pages advtext retires, as sections of
`ImTypeset::ShowDemoWindow()` written against `imgui.h` and `imtypeset.h` only:
**Specimen** (every face at one size with its set width, one face up the
modular scale, seven pairs kerned and letter by letter with the kern), **Formula
gallery** (the eleven plates of `advtext/ui/formulas.py`, same sources, panels
on one baseline, labels on the panels' columns) and **Math** (what
`pages/mathtext.py` showed: formulas in a paragraph, in a heading, a list and
a quotation, a stream fed three bytes at a time, the subset table and the list
of refusals with the reasons the library gives when the list is made).

**How advtext's demo reaches it**, inside the stock wheel, in either venv:

```python
import imtypeset                      # the sidecar, as before: rebuild it from this tree
from imtypeset import demo

demo.show_demo_window()               # the whole window, floating; (True) adds a close button
                                      # and returns whether it is still open

for section in demo.sections():       # or one section as a page of advtext's own demo
    name = demo.section_name(section) # "Specimen", "Formula gallery", "Math", ...
...
imgui.push_font(None, 19.0)           # a section sets its text at ImGui's current size
demo.show_demo_section(demo.DemoSection.Specimen)   # into the current window or child
imgui.pop_font()
```

`examples/python/bundle_demo.py` is both, runnable (`--show window|pages`),
and is what the pictures `docs/images/py-bundle-demo-*.png` were taken from in
advtext's venv (wheel 1.92.900) and pptmstr's (1.92.801). A section shown alone
keeps its state per imtypeset context and apart from the window's; it adds no
font and makes no context. The pages advtext deletes map to
`DemoSection.Specimen`, `DemoSection.Formulas` and `DemoSection.Math`; the other
eight sections can be listed or left out.

**What the old pages showed that is not there**, each because the public API
cannot say it (the full list with reasons is in
`.concerns/galleries--builder--result.md`): the debug overlay on the specimen
and the gallery (a document can be drawn with one, a paragraph and the widgets
cannot); a face's x-height, ascent and descent; "N codepoints have no glyph in
the math face" and "N glyphs have no raster"; a formula in a table cell (item
3); markdown in the source box (the box edits one formula; markup is
advtext's); captions set from markdown with bold and code spans (they are
plain text); the count of atoms and glyphs per pane. What a gallery cannot be
without advtext: nothing. The demo's stream is a paragraph re-given as bytes
arrive, not `advtext.stream`'s commit policies, which stay advtext's.
