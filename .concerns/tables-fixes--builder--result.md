# tables-fixes — result (builder-32, 2026-10-11)

Words used strictly: **RAN** (a command of mine, output seen), **READ**, **NOT RUN**. Nothing is
`git add`ed or committed. Decisions: `docs/decisions.md` **D60 to D64**. Departures:
`docs/divergences.md` T19 to T21, D9 to D12 (tables in a document), C9 to C12 (columns). For the
other session: `docs/advtext-swap-status.md` ("After `ce4d66d`" under "API changes", four new
"Border questions", and both "Known defects" sections rewritten). The tree was clean at `ce4d66d`
when I started; there was no progress file from an earlier run of this task.

## 0. What is still wrong in the pictures (looked at after the fixes)

Rendered the demo's Document section through the software rasteriser with the scratch driver of
`swap-fixes` (`.concerns/swap-fixes--builder--look.cpp.txt`, built against the release library of
this tree; 1100 x 720, 19 px, ten blocks and the table). Pictures:
`.concerns/tables-fixes--builder--shot-*.png`.

| still visible | picture | why it stands |
|---|---|---|
| **The first row under a table's header is two leadings below it, the rule nearer the body** | `c1-end-first-scroll`, `c2-end` | the reference's own (T20), held by every golden with a header. A border question, three choices written out |
| **Single lines of a paragraph at a column's foot or head** ("A formula written between dollar signs is set" alone at a foot; "gutter." alone at a head; a code block's first line alone at a foot) | `c3` | widow/orphan control: a typographic rule. A border question with the ten-line change that would do it and what it costs |
| **No common first baseline**: a column that starts with the heading has its first baseline 5 px lower | `c3` | the top skip is a body line's height. A border question, three choices |
| In a narrow column a held table that no longer fits is squeezed ("The / golden / ratio" on three lines) | `stream3-end` | as the header says and as the reference's `StreamView._sized_to` does; the demo's sentence was what was wrong, and is corrected |

What the pictures show fixed: one scroll to the end reaches the table's last row
(`c1-end-first-scroll`); the header, its rule and the row under it are in one column (`c2-end`);
both selected rows are highlighted cell by cell (`sel-table`); with Stream on in three columns
the view at the right end shows the table's last columns and scrolls DOWN the overfull row
(`stream3-end`, `stream3-end-scrolled-down`); balanced into three (`b3-end`).

**Looking found two defects nobody had listed**, both fixed: with text arriving in the last cell
and three columns in order, the view scrolled to the right end was BLANK — every frame the edit
dropped the last columns and the page was estimated as many columns wide as the growing row is
tall (20 where there are 8), so the view sat on columns that were not there; and the page lost
its extra height every frame, so the overfull row could not be scrolled down while it grew.
`DocumentColumns` now estimates with the count and the reach it had before the edit, and
`column_count` cuts the rest when every block is laid out. Test:
`a_view_at_the_end_of_columns_stays_on_the_last_column_while_text_arrives` (379 failed checks
with the estimate as it was, RAN).

## 1. Review-6 (tables), per finding

| # | reproduced? how large | fixed how | test |
|---|---|---|---|
| 1 | **yes**, RAN: 600 px table, a point 60 % across cell (0,0) answered column 1, the same in the last column answered row 1. Of 1,848 golden probes in a table the reference's index is in ANOTHER cell than the point for **465** (253 inside a cell's rectangle) | product rule D60: Row/Column are the cell whose rectangle holds the point (under either set of rules: the reference has no such answer); under product rules Position is a byte of that cell, from the cell's own items (`cell_under`, `hit_in_cell`). `SelectionModel::hit` untouched | `a_point_in_a_cells_rectangle_answers_that_cell_and_its_caret_answers_the_same_position`; golden replay, below |
| 2 | **yes**: caret at the end of right-aligned "cd" at 443.3 px (next cell's first glyph); the text ends at 427.3 | `table_caret`: in the position's cell, the right edge of the last thing the text set; an empty cell's caret by its alignment | `a_caret_at_a_cells_end_stands_at_the_end_of_its_text_whatever_the_alignment` |
| 3 | **yes** (the row's height) | the innermost LINE that holds the position's item | `in_a_wrapped_row_a_caret_is_one_line_tall_and_every_point_answers_its_cell` |
| 4 | **yes**: 73 failed checks; every row one pitch whatever its lines, row 1's top at 64.8 px where row 0's box ended at 21.6 | D62: per LINE. `Aligner::set_row`: under an exact strut a wrapped row takes the strut's height and keeps its cells' depth | `fixed_line_height_on_a_table_is_per_line_and_a_wrapped_row_is_its_lines_tall` |
| 5 | **yes**: a formula 27 px tall on an 8 px line, clip 4 px below the box: 0 quads where 1 reaches. (Above the box it was drawn by accident: a fraction's bar is a rule and rules extend a span) | fixed, with stated bounds: `LineIndex::overhang` (two ems of the largest type plus the tallest formula), and a draw looks four ems past the clip for blocks of exact lines (`exact_reach`). Header rewritten | `ink_that_overhangs_a_fixed_line_is_drawn_when_only_the_overhang_is_in_the_clip` |
| 6 | **yes**: at half the size the columns stayed 166.0 and 55.7 px where a fresh table has 83.0 and 27.9; also across a change of padding | `DocBlock::held_under` (face, size, padding, font generation); a change starts the widths afresh; a replace under the same style keeps them | `held_widths_do_not_outlive_the_style_they_were_measured_under` |
| 7 | **yes** by the builder's own figure (2,140 us an append at 10,000 rows) | D64: `TableCells::epoch`; five passes made incremental. RAN release, load average 3.0: **4.3 us an append at 10,000 rows; 65,536 rows one at a time in 0.39 s**, 5.0 us an append at the start and at the end. Counters: 1 row set, <= 3 rows gone over (5 fitted), 1 row indexed, an append | `a_row_appended_goes_over_that_row_and_not_the_table` (3,000 appends, by the counters), `a_table_grown_a_row_at_a_time_is_the_table_described_whole` (what is kept beside the layout equals a table described whole, at six sizes, held and fitted); `bench/bench_tables.cpp` `bench_grow` |
| 8 | **real**: Column 1 or 2 from the hit, 0 from `GetDocumentTableCell` | a column the row has no cell for answers the row's LAST cell; the hit and the position agree. `ragged_rows_...` asserted `Column == 2`; it now asserts 0 and the cell's end | `ragged_rows_empty_cells_and_empty_tables_are_set`, and the grid test |
| 9 | **yes** as described | the header's rule kept (the shared byte is the later cell's). A point in the earlier cell's blank answers its last OWN position (before its last character); an empty cell sharing its byte has none: Row/Column say it, Position names the next. Stated in the header | `cells_written_with_nothing_between_them_keep_hit_and_caret_a_fixed_point` |
| 10 | **yes** (READ, and the tallies showed all of them already equal) | `trow` top/height/wrapped, `tgeom` left, a `linkarea`'s cell are asserted, each with a comment saying it is script arithmetic over the reference's objects; `>= 1751`, `>= 473` are `==` | `tests/test_api_tables_golden.cpp` |
| 11 | READ | stated at `shared()`: one addition a term, Python's `sum()` up to 3.11 (the oracle's is 3.11.2); 3.12's is compensated. Not changed | — |
| 12 | **yes**, checked against `tex.web` by counting sections: fin_col is §791 (l.15597), §796 is its packaging, fin_row §799 (l.15719), §810 l.15938 | `src/align.h` cites §791, says what `fin_col` and `fin_row` ARE here (one statement and the append), and lists §810; divergences T19 | — |
| 13 | READ | `TableInput::cell_count`; an offset past it is cut to it | `tests/test_align.cpp`, the hostile-input test (offsets of 1,000 over three cells) |
| 14 | READ (`FaceData::has` is the font file's cmap plus the minted codepoints) | stated in the header at `FaceHasGlyph`. Not changed | — |

**The golden replay after the split** (`ask_probes`), what each assertion compares:
(1) model index at the golden's scaled point `==` reference `hit`: 1,848 / 1,848, unchanged;
(2) under reference rules the public Position is that index's by the convention: 1,848 / 1,848,
unchanged; (3) NEW Row/Column `==` the cell under the point, computed in the test from the golden
alone (`tgeom` left + the reference's widths, `trow` top and height): 1,848 / 1,848;
(4) NEW under the product's rule the Position is a byte of that cell, names it through
`GetDocumentTableCell` and names itself back: 1,848 / 1,848. REMOVED: Row/Column `==` the cell of
the reference's index. Pinned counts: 1,284 probes inside a cell's rectangle; the two rules give
one position for 1,366; the reference's index is in another cell for 465.

## 2. Review-7 (columns), per finding

Seven of the eight new tests of `tests/test_api_columns.cpp` were built against `ce4d66d`'s
sources (an export of HEAD with the new test file): all seven FAIL there, 5,486 failed checks,
and pass now (RAN). The eighth was written after and checked by taking its fix out (§0).

| # | reproduced? | fixed how | test |
|---|---|---|---|
| F1 | **yes**: N = 10,000 in order, the first question; and after an edit under balance | `DocumentColumns::cut_past_unit` + `settled_top`: the cut is settled before a block's top is read | `a_caret_and_a_range_asked_before_anything_is_cut_...`, `after_an_edit_under_balance_a_caret_below_it_...` |
| F2 | **yes** | the page is as tall as the tallest Overfull column cut so far holds (`reach`). Decision recorded (D63) with what it was weighed against | `the_page_is_as_tall_as_what_an_overfull_column_holds_...` |
| F3 | **yes**: three one-line paragraphs, three columns, all Overfull | never Overfull under BALANCED; header says so. `tests/test_columns.cpp` asserted `overfull` for a column holding an uncuttable piece under balance: replaced by "none is, and `used`, `height()`, `reach()` are that piece's" | `balanced_columns_are_never_overfull_...` |
| F4 | **half**: an empty paragraph DOES set a (blank) line; what happened was heading, blank line, cut | an empty text block is transparent to keep-with-next | `a_heading_is_kept_with_what_follows_it_through_a_block_with_nothing_in_it` |
| F5 | READ | header: how to carry a rectangle (corner + size), and what a float of pixels holds. No new call | — |
| F6 | **yes** at the block layer (needs the extents equal all the way to the document's end, which a run of equal blocks gives) | `splice` notes the change; a unit also compares its layout's serial | `a_unit_of_the_same_height_but_another_shape_is_not_the_unit_that_stood_there` |
| F7 | READ | header corrected to what the code does | — |
| F8 | **yes**, and wider than reviewed: ANY wrapped row at a column's foot hung below it by all its lines but the first, with or without the flag | product `\splitmaxdepth` = a body line's depth; the hit and highlight parts fell out of D60/D61 | `a_table_cut_across_columns_keeps_its_rows_whole_and_its_header_with_its_rule` (with and without `FixedLineHeight`, 31 column heights, in order and balanced: rows in order, each whole in its column, hit / caret / rectangles per row, the draw equal to the uncut draw's vertices) |
| F9 | **yes** | the lent height is kept while the same columns are said | `a_host_that_says_the_same_columns_every_frame_keeps_the_height_the_view_lent` |
| F10 | READ | stated in the header at `HitTestDocument`. Not changed. **Not stated anywhere: a width of 0 with a negative gutter makes the pitch one scaled point** (every x is then a far column: the same full layout, nothing worse) | — |
| seen #4 | **yes**: with rule 17 off, the header is parted from its rule at 13 of 91 column heights | `DocumentRules::keep_table_header` (rule 17) | the same test's sweep round the header's foot |

## 3. The seven things seen by `swap-fixes`

1 estimate: **fixed** (88 -> exact for one-line rows; `a_table_not_yet_laid_out_is_estimated_with_its_header_rule`).
2 rule spacing: **the reference's**; not changed; border question. 3 held columns moving: **the
demo's sentence was wrong**; corrected in two places in `src/imtypeset_demo.cpp`. 4 header cut
from its rule: **fixed**. 5 overflow unreachable: **fixed**, and two more found by looking (§0).
6 highlight: **changed** (D61). 7 baselines and single lines: **not changed**; two border
questions.

## 4. Product rules and decisions I took, for the lead to confirm or overturn

- D60: hit answers the cell under the point; Row/Column are geometric under BOTH sets of rules.
- D61: rectangles cell by cell.
- D62: `FixedLineHeight` per line in a table; overhang bounds.
- D63: `\splitmaxdepth` a body line's depth; rule 17; Overfull never under balance; page height
  includes an overfull column; heading kept through an empty paragraph.
- D64: `TableCells::epoch` (rows only change at the end between clears) is now an invariant the
  layout relies on. `TableCells` is only ever filled through `start_row` / `end_cell` /
  `reopen_last_cell` / `clear` (READ: grep).

## 5. What was run on the final tree

All RAN by me, on the tree as it is left, each in a private directory with the preset's flags,
`-j4`, every binary under `ulimit -v 6000000` and a timeout (ASan under
`ASAN_OPTIONS=hard_rss_limit_mb=3000`). No source file was edited after the first of these.

| configuration | result |
|---|---|
| debug, FreeType | 50 of 50 (the `amalgamation` ctest among them) |
| debug with the Python host | `python_binding` and `python_demo_sections` pass |
| ASan + UBSan | 50 of 50, no report in the log |
| release | 50 of 50 |
| stb_truetype | 50 of 50 |
| 16-bit `ImWchar` | 50 of 50 |
| sidecar for advtext's venv (`build_bundle_library.sh`) | built; gate accepted by probe, 24 of 24 rows; `mutate_gate.py` 87 mutants, 0 survived; `check_document_edits.py` 23 of 23; `check_swap_answers.py` 18 of 18 |
| sidecar for pptmstr's venv | the same four, the same figures |
| `clang-format --dry-run -Werror` over the 18 changed sources | clean |

The private build directories and the two sidecar outputs are removed.

## 6. Not run, not done

- Timings on a quiet machine (load average 3.0 throughout).
- The preset directories themselves (`build/debug` ...): every configuration is a private
  directory with the preset's flags.
- The exceptions-enabled build, the `fp-host` preset, ImGui 1.93, clang, MSVC.
- The Python wrapper has no new test: no declaration changed, and `python_binding` passes, but
  nothing in `tests/python/test_binding.py` asks a table's blank or a caret in a cell.
- `view_of` still copies a table's commands once per layout looked at through a view (first hit,
  caret or selection after an append): not made incremental, and not measured.
- A hit test at a far x in a long in-order document still lays everything out (F10: stated).
- The C mirror and Python docstrings were not re-read for sentences the header changes
  contradict (the C header points at `imtypeset.h` for these).

## 7. Files changed (22, plus this directory's notes and pictures)

`src/align.cpp`, `src/align.h`, `src/blocks.cpp`, `src/blocks.h`, `src/columns.cpp`,
`src/columns.h`, `src/draw.cpp`, `src/draw.h`, `src/imtypeset_document.cpp`,
`src/imtypeset_document.h`, `src/imtypeset_demo.cpp` (two sentences), `include/imtypeset.h`
(comments only: no declaration changed), `tests/test_api_tables.cpp`,
`tests/test_api_tables_golden.cpp`, `tests/test_api_columns.cpp`, `tests/test_columns.cpp`,
`tests/test_align.cpp`, `bench/bench_tables.cpp` (`bench_grow`), `docs/decisions.md`,
`docs/divergences.md`, `docs/advtext-swap-status.md`, `docs/performance.md`.
Outside the write-set the board declared: `src/columns.*`, `src/imtypeset_demo.cpp`,
`include/imtypeset.h`, `tests/test_api_columns.cpp`, `tests/test_columns.cpp`,
`bench/bench_tables.cpp`, `docs/performance.md` (the column findings and the lead's scope put
them in reach; I was the only writer).
New, untracked: `.concerns/tables-fixes--builder--{progress,result}.md`, eight
`.concerns/tables-fixes--builder--shot-*.png`.

## 8. Least sure of

1. **D60's rule for Row/Column under reference rules too.** I removed the reference-rules branch
   for Row/Column because the reference has no such answer; the lead may prefer the old value
   kept behind reference rules. One line either way.
2. **`TableCells::epoch` as an invariant.** If a later change edits a row that is not the last
   without `clear()`, kept widths and kept rows go stale silently. The equivalence test would
   catch a path it exercises, not one nobody wrote.
3. **The overhang bound is a bound I chose** (two ems of the largest glyph size): a font whose
   glyphs reach further than two ems from the baseline at an exact line's clip edge would still
   lose ink. Not measured against real fonts' extremes.
4. **`\splitmaxdepth` as a body line's depth changes where EVERY product column is cut**, not
   only at tables: a last line deeper than the body's (larger type, a formula) now counts its
   extra depth as height. The existing column tests pass; no picture was compared before and
   after for prose.
5. **The estimate from the last known count** keeps a page too wide after blocks are removed
   until the end is reached again.
