# Lead scope for `doc-fixes` (supersedes the board detail where they differ)

Board item 1 (tab in code) is reported FIXED by api-document and confirmed by review-4-document's
reading and by word-gaps' 40/40 suite: verify by running the test and move on. Board items 2-6 stand.
ADD, from `.concerns/review-4-document--reviewer--findings.md` (READ-DERIVED: reproduce each with a
test first; a finding that does not reproduce is reported, not "fixed"): items 1, 3, 4, 5, 6, 7 (C++
half), 8, 10, 11, 12.
- Item 8 is a rule of this repository, not a preference: no tolerance. Make the replay compare
  exactly (the helper says all 2,398 differences were 0 sp — then assert equality, and compare
  interline/strut/marker_gap/first_line instead of overwriting them); where a value truly cannot be
  expressed through the public description, say which and why, in the test and in docs. Move the
  test-only hook out of the shipped library if that is possible without a second build of the
  sources; otherwise guard it and say so.
- Item 1: an append that was applied must not return a "nothing done" code; decide whether run
  trouble is DescriptionCut (applied in part) and make SetDocumentBlocks report it at set time.
- Item 4: one UTF-8 walker, shared.
Files: as on the board, plus src/blocks.{h,cpp}, tests/test_blocks.cpp, src/style.{h,cpp} if the
shared walker needs exporting, src/imtypeset_c.cpp, tests/test_c_api.c, tests/test_c_api_map.cpp.
NOT yours: python/ (task `py-fixes`), src/draw.* (task `draw-seams`), src/imtypeset_demo.cpp
(task `demo-guard`). Those three run beside you.
