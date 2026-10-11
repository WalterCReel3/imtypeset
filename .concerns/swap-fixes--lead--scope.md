# Lead scope for `swap-fixes` (adds to the board detail)

Start from commit 0b21807 + the working tree as you find it (columns-core finished; nobody else is
writing). Besides the board's items (F3, F4 C++ half, F7, F8, F9 and the CalcTextSize sentence):
- columns-core touched src/imtypeset_demo.cpp (a Columns slider in the Document section): keep it.
- From columns-core's notes, small: `bench/bench_tables.cpp` fails clang-format (fix); docs/spec-status.md,
  README.md and CLAUDE.md's Scope line still say tables and columns are not ported (make them true:
  tables and columns are in the core as of be40444 / 0b21807; D34, D57, D58); `range_text` drops the
  space at a line break when a range ends exactly at a line's start, with or without columns —
  a helper's observation: reproduce, then fix or state (tables-fixes follows you in the Document
  sources; if the fix is there, post the exact change to lead for it instead of editing).
- NOBODY HAS LOOKED at a columned document or the demo's table row: render the demo's Document
  section with 2 and 3 columns and with its table through example_demo_headless / the soft
  rasteriser, LOOK, and report defects first. (You do not fix layout defects you see: report them.)
- F4: per `.concerns/oracle-fixes--builder--result.md`, the exact specimen replay must read widths as
  Scaled inside the library (tests/test_specimen.cpp over internals); a float cannot carry it.
- Process: the lead only sees a builder's notes when a builder finishes, so "stop and wait for a
  reply" does not work. Instead: finish the whole task on a tree that builds and passes, do not
  leave helpers writing, and list every file changed. The lead exports, builds, tests and commits.
