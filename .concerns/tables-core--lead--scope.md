# Lead scope for `tables-core` (adds to the board detail; read both)

The three spike results now exist in `../imgui-adv-text/.concerns/` (read-only):
`FS-S2--builder--result.md` is your requirements input (sections 1.1, 1.2 tables; 1.3 columns for
the next task; section 2 on what the core reserved and whether it matches; section 4 API notes;
E1.6 rows arriving one at a time; E1.7 selection, copy, links, find). `FS-S1` is DiffView, `FS-S3`
the inventory. They are the other session's findings, RAN or READ as each says: where one bears on
a decision of yours, check it here.

## STAGE 0 — small, first, and landed before the table work (it unblocks advtext's DiffView)

Item 5 of the swap request: "what DiffView needs". FS-S1's answer is that DiffView can be rebuilt on
the public API as it is, with ONE request, justified by a measurement there (one borrowed glyph in
4,848 lines moved 3,112 rows by 5.6 px; two side-by-side Code documents go out of step for good):
- **R1** — a way to make a block's LineHeight EXACT: every line of a Code block on the pitch asked
  for, whatever glyphs it holds (their suggestion: a block flag such as
  `ImTypesetBlockFlags_FixedLineHeight`, or a BlockStyle field). This is leading — typesetting —
  and is accepted (lead ruling). Reproduce first (a Code block with U+2502 / U+1EB2 set from a
  stand-in face: line boxes 22.4 px against 20 asked), decide the mechanism (TeX's answer is a
  strut and \lineskiplimit: a line whose box exceeds the pitch keeps the pitch and its ink
  overhangs — say what overhang does to clipping and selection rectangles), reference() untouched.
- **R1b** — "does face F have a glyph for codepoint C": accepted as a small read-only query
  (`ImTypeset::FaceHasGlyph(face, codepoint)` or the name that fits the header), since after the
  swap advtext's own fontlib is gone. It must answer for the face ALONE (not the stand-in chain).
- FS-S2 section 4, four notes: (1) `ImTypesetHit` (paragraph) carries no Tag although runs do — add
  it if it is a field at the end of a POD and the C mirror/static_asserts/Python follow, else say
  why not; (2) a paragraph's natural width is unreadable when justified — a documented way to ask
  (state it; add a query only if there is none); (3) `Document.draw` under a clip ending exactly at
  a line's top emits that line — reproduce and fix the comparison; (4) a Table block's text is
  dropped from a copy today — goes away with tables; until your table stage lands, say so in the
  status file.
Each: C counterpart, mapping row, Python, a test, a line under "API changes since 5654eee" in
docs/advtext-swap-status.md, and item 5 set to done with the names. DECLINED for DiffView, to be
written there too: anything that exposes nodes, packing or draw commands (FS-S1 asks for none).
When stage 0 is green in debug and ASan+UBSan, post_concern to lead ("stage 0 landed") so it can be
committed and named in the status file before you go on. Then tables.

## What landed just before you (do not undo)

- `swap-api-answers`: null draw_list = current window; GetFont null unless the context's ImGui
  context is current; C-only `ImTypeset_GetFontAtlasIndex`; D53. `.concerns/swap-api-answers--builder--result.md`.
- `galleries`: `ShowDemoSection` / `GetDemoSectionName` / `ImTypesetDemoSection_` in the public
  headers; Specimen and Formula gallery sections in src/imtypeset_demo.cpp (not your file, but your
  Table row goes in its Document section: coordinate by adding it at the end).
- `oracle-freeze`: the oracle runs from build/advtext-reference (pinned tag); new golden directory
  for selection/copy through tables (`.concerns/oracle-freeze--builder--result.md`, FORMAT-doc-tables.md).

## Queued requests you may meet (D53; not yours unless they fall out for free)

C2, C4, C6, C7 of the plan's core requests; the same-address ImGui-context collision in
validate_fonts (D53 item 5).
