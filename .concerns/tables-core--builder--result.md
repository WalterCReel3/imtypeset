# tables-core: result (builder)

Tables are a block of the Document, in C++, C and Python; stage 0 (advtext's DiffView request and
four API notes) is in the same tree. Decisions are D56 (stage 0) and D57 (tables) of
`docs/decisions.md`; the reference's departures from tex.web are T1 to T18, and the Document's
from the reference D1 to D8, of `docs/divergences.md` ("tables-core"); every public name is in
`docs/advtext-swap-status.md`.

## What was run, on the tree as handed over ("tree whole")

| configuration (private directory) | result |
|---|---|
| debug, FreeType, Python host on (`build/tables-core`) | 48 of 48: `python_binding`, `python_demo_sections` and the `amalgamation` ctest among them |
| ASan + UBSan (`build/tables-core-asan`) | 46 of 46, no report |
| release (`build/tables-core-rel`) | 46 of 46 |
| stb_truetype (`build/tables-core-stb`) | 46 of 46 |
| 16-bit `ImWchar` (`build/tables-core-wchar16`) | 46 of 46 |
| both sidecars, `python/host/build_bundle_library.sh` (wheels of advtext's venv and pptmstr's) | built, gate accepted by probe; `tools/bundle_route/check_document_edits.py` 23 of 23 and `check_swap_answers.py` 18 of 18 in each |
| `clang-format --dry-run -Werror` over `src`, `include`, `tests`, `bench` | clean |

**Not run:** the preset directories themselves (`build/debug` and the rest: every configuration
above is a private directory with the preset's flags); the exceptions-enabled build; the
`amalgamation` and `fp-host` presets (the amalgamation ctest inside the debug tree passed, which
compiles the generated single file); ImGui 1.93; clang, MSVC; the demo photographed (the new
table in its Document section was run by `test_demo` and `example_demo_headless`, not looked
at); `test_binding.py` under ASan; `mutate_gate.py`; the applications themselves in either wheel;
`bench_tables` on a quiet machine.

## Stage 0

| item | what | test |
|---|---|---|
| R1 | `ImTypesetBlockFlags_FixedLineHeight` (`Strut::exact`) | `tests/test_api_requests.cpp`: four tests; Python `Requests` |
| R1b | `FaceHasGlyph` | `face_has_glyph_answers_for_the_role_alone`; C and Python |
| note 1 | `ImTypesetHit::Tag`; and a defect found: the document hit's Tag was the caret's run | `a_paragraph_hit_…`, `a_document_hit_…` |
| note 2 | no query: the natural width is a line's, stated at `GetParagraphSize` | `the_natural_width_…` |
| note 3 | `DrawRules::quads_off_clip_dropped` (product) | `a_document_drawn_in_slices_…` |
| note 4 | superseded by tables | |
| review F2 | reproduced under ASan, NOT fixable through `imgui.h` (lead accepted); the two refused states pinned | `a_null_draw_list_is_refused_…` |

## The acceptance list

| asked | how it is met | test |
|---|---|---|
| core module against tex.web 768-812 | `src/align.*`; sections checked by count and by reading (679, 768, 777, 796, 799-802, 804, 806-810, 1087) | `tests/test_align.cpp` |
| `tests/golden/tables` exact | at the core: 36 cases, 97 layouts, 441 rows, every record, and the reading-index map against the golden commands (185,754 checks). Through the public API: 31 cases, 84 layouts | `test_align`, `test_api_tables_golden` (`the_table_goldens_…`) |
| `tests/golden/doc-tables` exact | selection: 9 cases, 15 layouts, 16,402 commands, 6,671 index bytes, 2,013 probes, 1,540 drags, 1,387 ranges with copies and rectangles, 261 matches, 23 link areas. stream: 9 cases, 213 steps | `test_api_tables_golden` (67,496 checks in all) |
| cells as text and runs: faces, colour, links by tag, formulas | a cell is a paragraph's text | `cells_take_runs_faces_colours_links_and_formulas`; goldens |
| per-column alignment, header row, wrapping, rule and padding | `ImTypesetTableColumn`, `TableHeader`, options | goldens; `a_table_too_wide_…` |
| selection, copy, find marks through cells | positions (block, byte); marks in all three places | `a_copy_of_a_table_…`, `a_mark_in_a_table_…`; goldens |
| hit testing into a cell | `ImTypesetDocumentHit::Row/Column`, `GetDocumentTableCell` | `a_hit_in_a_cell_…`; 1,848 golden probes |
| a table that grows without its columns jumping | `ImTypesetBlockFlags_TableHeld` | `a_row_appended_to_a_held_table_…`, `a_table_that_is_not_held_…`; `stream.golden` |
| lazy layout, O(changed) edits, by the counters | one block; kept rows; `TableRowsSet`, `TableCellsMeasured` | the two above, `a_table_out_of_view_…`; 85 quiet golden steps |
| a table in a list item | absolute indent, like any block | `a_table_in_a_list_item_…`; `shapes.golden`, `selection.golden` `in-a-list` |
| C ABI with static_asserts and mapping rows | 8 functions, 6 structs | `test_c_api`, `test_c_api_map` |
| Python, one binding call an edit | `Block(rows=, columns=)`, `append_table_rows`, … | `test_binding.py` `Tables` (10 tests) |
| a table in the demo's Document section | a held table ends the document; Stream appends into its last cell | `test_demo`, not looked at |
| hostile input bounded | cut at 1,024 columns / 262,144 cells; ragged, empty, out-of-order, non-finite | `ragged_rows_…`, `a_description_that_is_wrong_…`, `a_table_past_its_limits_…`; `test_align`'s direct tests |

## Decisions taken (D57 has each with its reason)

1. **Position addressing:** (block, byte of the block's text); a cell is a byte range of that text.
   `GetDocumentTableCell` is a bisection, O(log cells), not the O(1) asked for.
2. **A caret at a cell's end is the cell's end** (the dump script's forward rule is not the API's:
   597 of 1,667 probe positions differ, counted by the replay).
3. **Copy separators:** tab and newline, the reference's. Under product rules the copy is cell by
   cell (an empty first cell keeps its tab; a short row is not padded) — NOT the reference's,
   which runs under reference rules and is what the goldens replay.
4. **Streaming width rule:** `TableHeld`, which is the reference's own `StreamView._held`, in the
   block; survives a one-for-one replace by a held table with as many columns.
5. **One block, laid out whole, rows kept.** Limits 1,024 x 262,144.
6. `TableRows`/`TableColumns` renamed `CellStart`/`CellCount`.

## What the goldens cover and what stands on decision alone

Covered exactly: the widths in every regime but `rigid`, rows that fit and wrap, the rule, the
stack, the commands; indices, hits, word and line units, drags, copies and rectangles through
cells and across a table's edges under reference rules; held widths step by step.
On decision and my own tests alone: the public description itself; the product's copy (D2, D3);
marks drawn in cells (the reference has none); the caret convention; `SetDocumentTableColumns`;
the limits; `Natural`/`Floor` as a query; a table in a quote; `GetDocumentCaretRect` in a cell.
Not covered through the public API though covered at the core: a cell holding a formula (the
golden records it as a box, with no source), a row with no cells at all (the API cannot say
one), seven layouts whose width or share is not a float of pixels. Half-to-even in `shared` is
held by a direct test only: no golden has a tie.

## Measured

`docs/performance.md`, "Tables" (release, one run, the machine not quiet). A quiet append sets
1.0 rows and measures 4.0 cells at any size but costs about 0.2 us for each row the table has
(155 us at 1,000 rows); replacing a held table sets every row again.

## Helpers

Three: the alignment core and its replay; the C mirror and Python; the replay through the public
API. I re-ran everything each reported, in the configurations above, and added the index map to
the core myself.

## State at hand-over

"Tree whole" was posted to the lead with the file list; no source file was edited after it (this
file and the progress file are the only writes since). Stage 0 and the table stage are in one
tree and share files, so they are one commit unless a patch of stage 0 against `12d77ac` is
wanted. Private build directories `build/tables-core*` are removed.

## Least sure of

1. **The product's copy of a table (D2, D3)** is my departure from the reference, taken because
   a pasted row must keep its columns; the coordinator has not ruled on it.
2. **A replace of a held table sets every row again** although its widths are kept: a host that
   re-describes the table from re-parsed markdown each chunk pays the whole table each chunk.
   `AppendDocumentTableCells` is the cheap path; whether advtext's stream can use it is theirs.
3. **The quiet append is not flat** (0.2 us a row of table): passes over the whole block remain
   in `sync` and `build_cells`. Timings were taken on a loaded machine.
4. **The table in the demo was never looked at**, only run.
5. `FixedLineHeight` on a table is accepted and makes rows and wrapped lines exactly the strut;
   no test or golden holds that.
6. A cell that is one `$$…$$` block is set as a display-style formula inline; untested.
