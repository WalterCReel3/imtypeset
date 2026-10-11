# columns-core: result (builder)

A Document can be shown in columns, in C++, C and Python: TeX's `\vsplit` at the core, cut lazily
from the front or balanced, with draw, hit test, selection and copy across columns in reading
order. Decisions are D58 of `docs/decisions.md`; the reference's departures from tex.web are V1
to V10, and the Document's from the reference C1 to C8, of `docs/divergences.md`
("columns-core"); every public name is in `docs/advtext-swap-status.md` ("Columns").

## What was run, on the tree as handed over ("tree whole" #2)

No source file was edited after these. `docs/performance.md` and this file were written after.

| configuration (private directory) | result |
|---|---|
| debug, FreeType, Python host on | 51 of 51: `python_binding` (88 tests, 15 new), `python_demo_sections` and the `amalgamation` ctest among them |
| ASan + UBSan | 49 of 49, no report |
| release | 49 of 49, no warning |
| stb_truetype | 49 of 49, no warning |
| 16-bit `ImWchar` | 49 of 49, no warning |
| both sidecars, `python/host/build_bundle_library.sh` (advtext's venv, wheel 1.92.900; pptmstr's, 1.92.801) | built, gate accepted by probe; `tools/bundle_route/check_document_edits.py` 23 of 23 and `check_swap_answers.py` 18 of 18 in each |
| columns inside each stock wheel (a scratch script, not kept: in order 30 columns tiling the document, a copy equal to the uncut one, a hit in column 2, balanced into 4, uncut again) | ok in both |
| `clang-format --dry-run -Werror` over `src`, `include`, `tests` | clean (`bench/bench_tables.cpp` is not, and was not at `be40444`: not mine) |

**Not run:** the preset directories themselves (every configuration above is a private directory
with the preset's flags); the exceptions-enabled build; the `amalgamation` and `fp-host` presets
(the amalgamation ctest inside each tree passed, which compiles the generated single file with
`src/columns.*` and `src/vsplit.*` in it); ImGui 1.93; clang, MSVC; any benchmark (none was
written: the timings are single prints of two tests in release); `test_binding.py` under ASan;
`mutate_gate.py`; the applications themselves in either wheel; **the demo's column toggle, which
is compiled and whose default (one column) is what `test_demo` runs: columns in the demo were
never looked at, and nothing in this task was looked at as a picture**.

## The acceptance list

| asked | how it is met | test |
|---|---|---|
| a core module against tex.web's page-splitting sections, every number checked | `src/vsplit.*`: 968, 969 `prune_page_top`; 970 to 976 `VertBreak` / `vert_break` (971 `best_height_plus_depth`); 977 `vsplit`; 978, 979 named as not applying; 833 `awful_bad`; 1001 and 1082 cited. Counted from the top of `tex.web` | `tests/test_vsplit.cpp` |
| the reference's departures recorded and kept behind reference rules | V1 to V10; C1 to C8; `DocumentRules::break_between_blocks` (item 16) and the product's top skip are off under reference rules | the replays below run under them |
| goldens exact, at the core | `vertbreak` 19 cases, `documents` 5, `columns` 4: 85 splits, 247 columns, 1,850 candidate breaks, 4,450 shipped commands, record by record | `test_vsplit` (7,935 checks) |
| goldens exact, through the public API | `columns.golden` described through `imtypeset.h` with every length a float of pixels held to the scaled point by `==`: 4 cases, 8 splits, 18 columns, 2,634 commands; and the same through a `Document` at the block layer | `test_api_document` (`the_column_goldens_...`), `test_columns` |
| a way to ask for columns that keeps one Document usable at any width | `SetDocumentColumns` beside `LayoutDocument`: a property of how the document is shown. Weighed against the description and against per-call slices in D58 | `one_document_is_shown_in_columns_at_any_width_and_uncut_again` |
| draw across columns | column by column; a block a cut falls in is drawn by its lines | `what_columns_draw_is_what_the_uncut_document_draws_...` (as many vertices and indices as the uncut document, the same glyphs by texture coordinate, each column inside its rectangle; a selection and a mark on both sides of a cut) |
| hit test, selection, copy in reading order | points taken between the page and the uncut column | `every_line_is_found_in_its_column_...`, `a_drag_across_columns_...`, `a_point_beside_a_columns_lines_...` |
| per-column geometry | `GetDocumentColumn`, `GetDocumentColumnCount`, `GetDocumentColumnPos` | `columns_tile_the_document_in_reading_order` |
| a table, code block or host block taller than a column | code and tables are cut between lines and rows; a host block, a quote, a wrapped row stand alone, overfull, and say so | `what_cannot_be_cut_and_is_taller_than_a_column_...` |
| the lazy-layout question, N = 10,000 | below | `ten_thousand_blocks_in_columns_cost_...`, and two tests of `test_columns` |
| C ABI with mapping rows | 5 functions, 2 structs, 1 enum, 2 `BuildInfo` sizes, static asserts | `test_c_api` (`columns`), `test_c_api_map` |
| Python | `Columns`, `ColumnFlags`, `ColumnGeometry`, `Document.set_columns / column_count / column / column_pos` | `test_binding.py` `Columns` (12 tests), `Loading` (+3) |
| a toggle in the demo's Document section | a "Columns" slider (1 to 4) and "Balance", with the counters | compiled; not looked at |
| every public name in the status file | "Columns" under "API changes"; item 4 set | |

## What stands on a golden and what on a decision

**On a golden, exactly, under the reference's rules:** where a column is cut and why (every legal
breakpoint with its totals, badness and cost); the glue at a column's head; what is dropped at a
cut; an overfull column; what the reference's `count` columns leave over; every command a column
ships.

**On a property (no golden, but the reference's own code is the yardstick):** 2,113 columns of 40
random documents are the columns `split_into_columns` cuts from the same blocks composed at
once, shipped — however the columns were asked for and whatever was edited between.

**On a decision and its own tests alone** (the reference draws columns and never selects in
them, and does not balance):

1. **Columns are set beside the layout, not in the description.** One document, any width,
   uncut or in columns; changing columns lays out no block again.
2. **The document is not cut: a column is a stretch of the uncut column, moved.** Exact, because
   a document's list has nothing to stretch; layouts, positions and marks are untouched.
3. **In order is lazy from the front; balanced is not.** Stated in the header.
4. **Balanced means the least height at which `Count` columns hold the document**, by bisection
   (monotone for a rigid list; one scaled point less does not hold it, checked).
5. **A column may end between two blocks, not after a heading** (product rule; the reference
   never ends one between blocks, so a list of one-line items cannot be cut there).
6. **The product's `\splittopskip` is the body line's height above its baseline**; 10 pt under
   reference rules.
7. **Reading order:** a point is its column's (a gutter belongs to the column on its left), held
   between that column's first and last lines; a drag across columns selects everything between
   in the document's order; a copy is the uncut document's to the byte.
8. **Which calls speak of the page** (draw, hit test, select, range rectangles, caret, size) **and
   which of the uncut column** (block geometry, block line, table queries, `LayoutDocumentWindow`),
   with `GetDocumentColumnPos` between.
9. **What cannot be cut is overfull and the host's to deal with**; nothing is clipped.
10. **What is kept across an edit:** the columns before the first changed unit.

Each has its reason in D58.

## The lazy-layout numbers (N = 10,000 paragraphs; counters, so machine-independent)

| | blocks laid out | columns cut |
|---|---|---|
| in order: a frame showing three 600 px columns | 14 (the columns hold 14) | 3 |
| the next frame, a hit test, `GetDocumentSize`, `GetDocumentColumnCount` | 0 | 0 |
| a frame of columns 10 to 12 | 62 in all (they end at block 57) | 14 in all |
| text appended to the last block while it is out of view | 0 | 0 |
| every column (`LayoutDocumentWindow(0, 1e9)`) | 10,000 | 2,308 (estimated 2,653 after three) |
| then text appended to the last block | 1 | at most 2 |
| shown uncut, and in columns again | 0 | (cut again when reached) |
| **balanced into six: the first question** | **10,000** | 6, after 36 heights tried |
| balanced: a word appended | 1 | 6, after the search again |

**Balancing needs every height: it forces a full layout, and every edit searches again.** In
release, re-balancing 10,000 laid-out paragraphs took 29 ms (one run, machine not quiet), and
re-cutting them in order 4.6 ms. Filled in order is the lazy mode; it is lazy from the FRONT
only: column k costs every column before it, once.

## Helper

One: the Python package and its tests (`python/imtypeset/_capi.py`, `__init__.py`,
`tests/python/test_binding.py`). I read its package diff and re-ran its tests in the debug tree
and inside both wheels. It found a wrong sentence in my header (the rule for `UncutTop`), which
I corrected, and two things outside this task, passed on to the lead.

## State at hand-over

"Tree whole" was posted twice (stage 1, the core with no public change; stage 2, everything) with
file lists; no reply came to either within ten minutes, so stage 1 was not committed apart and
the two are one tree. Private build directories `build/columns-core*` are removed. Not committed,
not added.

Files: new `src/vsplit.{h,cpp}`, `src/columns.{h,cpp}`, `tests/test_vsplit.cpp`,
`tests/test_columns.cpp`, `tests/test_api_columns.cpp`; modified `src/blocks.{h,cpp}`,
`src/imtypeset_document.{h,cpp}`, `src/imtypeset_c.cpp`, `src/imtypeset_demo.cpp`,
`include/imtypeset.h`, `include/imtypeset_c.h`, `python/imtypeset/__init__.py`,
`python/imtypeset/_capi.py`, `tests/python/test_binding.py`, `tests/test_api_document.cpp`,
`tests/test_c_api.c`, `tests/test_c_api_map.cpp`, `docs/decisions.md`, `docs/divergences.md`,
`docs/advtext-swap-status.md`, `docs/performance.md`. `src/columns.*`, the two new test files,
`src/imtypeset_demo.cpp` and `docs/performance.md` are outside the write-set the board declared.

## Least sure of

1. **Nothing was looked at.** Every claim is a count, an equality or a bound; no picture of a
   columned document was rendered and the demo toggle was never seen. A wrong but self-consistent
   placement (a column a pixel off, a gutter that reads badly) would pass.
2. **The two product rules are mine and unruled** (5 and 6 above). A column ending between blocks
   also lets one end after a paragraph's first line or before its last: there is no widow or
   orphan control, and TeX's penalties cannot give it in a list that does not stretch.
3. **Balanced columns cost a search per edit** (29 ms at 10,000 paragraphs). It is said, not
   solved. A cheaper search (fewer trials, or trials over the candidate heights only) was not
   tried.
4. **After an edit that adds, removes or re-describes blocks, columns from the document's first
   quote on are cut again**, because a quoted run is stacked again after every `prepare` (D37)
   and so reads as changed. Correct, but more work than the edit warrants for a document with a
   quote near its top.
5. **A point exactly on a column's first line's top** is that column's "to within what a float
   holds": `GetDocumentColumnPos` picks the column by the next representable height. At two
   million pixels a float's step is a quarter pixel; untested there.
6. **Lines are assigned to columns by their tops.** A box of no height at a cut, or a negative
   interline skip, can put a line's commands in the neighbouring column's draw. The public
   description cannot make either; untested.
7. **A mark drawn Under a line** goes with its line by its rectangle's middle; with a line height
   tight enough that the bar falls below the line's box at a cut it would be drawn at the head of
   the next column. Not reproduced.
8. `DocumentView` scrolling sideways was driven by synthetic mouse events in one test; never
   under a real mouse.
