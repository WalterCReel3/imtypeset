# Lead scope for `tables-fixes` (adds to the board detail)

You also take the COLUMN findings, since they are in the same Document sources:
`.concerns/review-7-columns--reviewer--findings.md` F1-F10 (read-derived: reproduce each first).
Order: F1 (stale block top in caret and range rectangles under columns — make the cut settle before
the top is read, and test at N = 10,000 in-order and after an edit under balance), F2 (in-order
columns in DocumentView: overfull content must be reachable — decide: content height includes the
overflow, or the view scrolls columns horizontally; record it), F3 (Overfull under balance; make the
header true), F4 (empty block after a heading), F5/F7 (make header and code agree; a rectangle
needs a mapping that keeps both corners in one column — offer one or say how), F6, F9, F10; F8 falls
out of the table fixes: test a table cut across columns, with and without FixedLineHeight.
Read `.concerns/swap-fixes--builder--result.md` first: it LOOKED at a columned document and the
demo's table and lists the visual defects it saw — those in your files are yours.
Process: the lead only sees a builder's notes when the builder finishes. Finish on a tree that
builds and passes, leave no helper writing, list every file changed; the lead exports, builds,
tests and commits after your report. Keep "Known defects" in docs/advtext-swap-status.md current
(there is a tables section and a columns section): strike each as fixed, naming nothing as fixed
that you did not reproduce and re-test.
