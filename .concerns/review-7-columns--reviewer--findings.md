# review-7-columns findings (filed by the lead, 2026-10-11) — ALL READ-DERIVED, nothing run

Full text: the notes on board task `review-7-columns` (read_board()). Commit reviewed: 0b21807.
The cutting core (src/vsplit.*) matches tex.web §968-977 and the reference; two golden cuts were
re-derived by hand and agree. The problems are in the layer with no oracle.

F1 HIGH. GetDocumentCaretRect / GetDocumentRangeRects (`append_px`) in src/imtypeset_document.cpp
   read `d->doc.block_top(i)`, THEN call `shown_at` -> `DocumentColumns::column_at`; cutting lays out
   the units above and changes that top: a stale top mixed with a fresh column offset.
   Input 1 (in order): 10,000 paragraphs, three columns cut; GetDocumentCaretRect({5000, 0}) names the
   wrong column (and again on the second call if estimates were too small). Input 2 (balanced):
   append a line to block c, then ask for a caret/range in a block below before the next draw: one
   line off, in the neighbouring column near a cut.
F2 MEDIUM-HIGH. In DocumentView, in-order columns hide what is overfull: GetDocumentSize answers
   columns.height() (= the rule height under fill) and the view lends its own height. A 40-line quote
   in a 30-line view with Count = 2, Height = 0: the tail is past the content height, no scrollbar.
   (The ImGui half is inferred.)
F3 MEDIUM. Under balance, `Overfull` is true for columns that fit: `DocumentColumns::holds` counts an
   overfull column as holding its first piece; when the document has no more uncuttable pieces than
   Count the bisection ends at `_balanced_at = 1` sp. Three one-line paragraphs, Count = 3: all three
   Overfull while ContentMax.y == Max.y. The header's sentence about Overfull is false under balance.
F4 MEDIUM. An empty block defeats "not after a heading": `Document::unit_vlist` tests
   `_blocks[first - 1].keep_with_next` (the block immediately before). Heading, Text "", Text "para".
   (Not confirmed that an empty text block sets no line.)
F5 MEDIUM-LOW. GetDocumentColumnPos cannot carry a rectangle though the header offers it for that:
   body lines tile, so the bottom of a column's last line equals the next column's top; at large
   heights the `nextafter` step widens this (0.25 px at two million px).
F6 LOW-MEDIUM. `prepare`'s "same unit" test is by index and extent, not line structure or
   keep_with_next (blocks.h promises "with the lines it had"): remove block 0 where a two-line
   paragraph cut after line 1 is followed by a host block of exactly the same extent — the kept cut
   falls inside the host block, unflagged (needs an extent coincidence).
F7 LOW. Header says a point below a column's last line is "that line's end"; `uncut_of` clamps y and
   keeps x (the boundary nearest x in the last line).
F8 LOW (with review-6). A FixedLineHeight table's wrapped last row ending a column hangs below the
   foot with no Overfull; `draw_block` drops its highlight; `append_px` puts those rectangles in the
   next column. A click in the trailing blank of a column's last row lands at the next column's head.
F9 LOW. SetDocumentColumns zeroes `view_height` on every call: a host that sets columns each frame
   (Count >= 2, Height = 0) and asks anything before DocumentView that frame is answered uncut.
F10 LOW, cost. A far-right hit test lays out everything (`uncut_of` cuts through column
   floor(x / pitch)); width 0 with a negative gutter makes the pitch 1 sp.
Confirmed by reading: section numbers; vsplit.cpp against §968-977 and columns.py; balancing
terminates and `holds` is monotone without negative glue; reading order (drag from column 2 into
column 1; gutter belongs to the column on its left; a cut block's text copied once, drawn once);
the two product rules do not leak into the reference replay; untrusted Count/Height/Gutter bounded;
laziness claims match the code.
Not reached: the tests, the C mirror, Python, the demo toggle, DocumentColumns::flatten in detail,
anything visual.
