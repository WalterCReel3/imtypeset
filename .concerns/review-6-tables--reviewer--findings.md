# review-6-tables findings (filed by the lead, 2026-10-11) — ALL READ-DERIVED, nothing run

Full text: the two notes on board task `review-6-tables` (read_board()). Commit reviewed: be40444.

1. HIGH. A click in a cell's trailing blank resolves to the NEXT cell (the next row in the last
   column). `hit_test` in src/imtypeset_document.cpp derives Position and Row/Column from the item at
   the model's boundary index; `SelectionModel::boundary_in` (src/selection.cpp) splits every item at
   its midpoint, the filling \hss included. selection.golden line 534 (`probe 14751560 2585223 71`:
   1 px inside the table's right edge, inside row 0) -> product answers Row 1, Column 0; line 531
   (x = 12510227, 1 px left of column 2's edge 12559563) -> Column 2. For 50 px of text in a 300 px
   column, ~39 % of the cell reports the wrong column. `ask_probes` in
   tests/test_api_tables_golden.cpp ASSERTS this. (Least sure: that item edges are contiguous.)
2. HIGH-MEDIUM. The end of an unwrapped RIGHT-aligned cell has no item (`aligned()` in src/align.cpp
   puts no trailing \hss): GetDocumentCaretRect there draws the caret at the next cell's first glyph;
   GetDocumentTablePosition hands the position out and a hit test can never return it.
3. MEDIUM. GetDocumentCaretRect in a wrapped row is the whole row tall (`index_lines` in src/draw.cpp
   keeps nested LINE commands in the outer span; `ensure_lines` takes top/bottom from the row's LINE).
4. MEDIUM. FixedLineHeight on a table whose last row wraps makes the block (n-1)L too short
   (`Aligner::set_row` applies the strut to the row; `Strut::exact` replaces the row's depth): the
   next block is set over the wrapped lines; rows stop tiling; lines 2..n are outside every line box.
5. MEDIUM. Exact struts and culling: cull box = strut box + CULL_MARGIN_PX (2); ink overhanging more
   is dropped while still visible at the clip edge (cull loop itself not read).
6. MEDIUM. Held widths survive a style change (`b.held` cleared only in `describe`; `apply_spec`
   keeps it; `build_cells` takes the maximum): old wider columns after the font shrinks.
7. MEDIUM, cost. Appending rows one at a time is quadratic below the limit: each append re-runs
   `sync` over the block, the held scan over every cell, a full copy in `view_of` (~430 s total for
   65,536 four-cell rows by the author's own 0.2 us/row figure; ~13 ms for one append at the end).
8. LOW-MEDIUM. Short rows: a hit in a column the row lacks reports that Column, but
   GetDocumentTableCell(hit.Position) reports the one before (unresolved; may be a misreading).
9. LOW-MEDIUM. Touching cells are not a fixed point: "abcdef", cells [0,3) and [3,6): a hit at the
   end of cell 0 returns byte 3, whose caret is drawn at the start of cell 1.
10. TEST. `ask_tables` never asserts `trow` top/height/wrapped, `tgeom` left, or a `linkarea`'s cell
    against the golden; two tallies are lower bounds (>= 1751, >= 473). No tolerance in the file.
11. LOW. `shared()` sums fractions naively; Python 3.12+ sum() is compensated — exact only against
    a 3.11 oracle (the pinned venv is 3.11.2).
12. LOW, D23. tex.web's fin_col is §791; the port's `fin_col` is one line of §796 and §791 is cited
    nowhere; `fin_row` aliases append_to_vlist; §810 (every cell gets the row's height and depth) is
    an unlisted departure.
13. LOW. `TableInput` has no cell count; fin_align trusts row_first against cells.
14. LOW. FaceHasGlyph: true for the math face's minted plane-15 codepoints, false for a 16-bit
    build's BMP stand-ins, answers ImTypesetFace_Default as role 0.
The 597 differing probes, five classes decided by hand: the script's byte is -1 (no position); the
product is right in every one. Confirmed by reading: section numbers in align.h; squeezed and
apportioned widths re-derived by hand match the goldens (prose-swept ui-200px: 3135168, 4370892,
2361204; ui-1070px: remainder 1 to the last column, 4475686); width functions match the reference line
for line (reference table code is advtext/ui/__init__.py, not advtext/tables.py); hostile
descriptions bounded; product copy rules do not leak into the replay; quads_off_clip_dropped drops
only wholly invisible quads.
Not reached: tests/test_align.cpp, the stream replay, the C mirror and Python wrapper, a table in a
list or quote, `kept.assembled` lifetime, a cell that is one display formula.
