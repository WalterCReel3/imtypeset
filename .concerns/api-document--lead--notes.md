# Lead notes for `api-document` (accumulating)

- Requirements: `docs/advtext-migration.md` §3 (R1-R11) is the acceptance test; R1-R5 are the core of
  this task, R6 (tables) only if a table layer exists internally by then, R7 (a block that reserves a
  rectangle for a host widget) is trivial and included, R8-R11 are noted as deferred with where they
  attach.
- Goldens to replay beyond `tests/golden/blocks`: `tests/golden/doc` (FORMAT-doc.md) — selection and
  copy across blocks (1,167 drags, 556 ranges with the exact copied string), decorations (find hits,
  ink ranges, fills, link areas), host blocks, streaming steps and the stability property (a committed
  block's layout never moves). `.concerns/oracle-doc-extras--builder-29--result.md` lists the
  prototype's shortcuts in this area; same pattern as everywhere else: reference rules for the goldens,
  product rules for the public default, each product rule with its own test.
- RULING on the coordinate space (the oracle builder asked): a position is (block, byte offset into
  THE TEXT THE HOST HANDED IN for that block). For a text block that is what the golden already
  records (run texts concatenated, a formula as its `$source$`, a hard break as `\n`). For a code
  block the golden's byte fields are into the TAB-EXPANDED text, because the prototype expands tabs
  before anything keeps the source; the product's offsets are into the RAW code text. The test maps
  between the two with the same tab expansion and says so; do not regenerate the goldens for this.
- Product rules worth taking from that shortcut list: find is case-insensitive by proper case folding
  with a mapping back to source bytes (the prototype silently turns case-sensitive when folding
  changes length); overlapping marks are merged or ordered deterministically; a formula always copies
  as its source and a hard break as a newline (the prototype can swallow both); a link underline has
  its own thickness; a block's rectangle is its ink/advance box with the full-measure box available
  separately; the live tail of a stream uses the document's alignment unless the host asks for ragged.
- Tables (`tests/golden/tables`, FORMAT-tables.md, `.concerns/oracle-tables--builder-28--result.md`)
  are NOT ported by any task yet; reserve the block-description fields.

## Added 2026-10-10 after c-api (lead)

Decide before freezing the header (raised by c-api's builder, not verified by me):
`TextCodepointToByteOffset(text, text_end, n)` and `TextByteToCodepointOffset` have a non-defaulted
`text_end` in the middle, the only two functions a litgen binding cannot fix by dropping `text_end`
by name. Ruling: check the claim against the header; if it holds, reorder to
`(text, n, text_end = NULL)` in include/imtypeset.h, its C counterpart, the mapping table, the
Python package and callers, and record it in docs/decisions.md. `test_c_api_map` catches a C++
declaration without a C form only at ctest time, not at compile time — keep running it.
From py-host (measured design, their report): the ctypes package converts str index <-> byte with a
linear helper for non-ASCII text; over a long document that is quadratic. Make the conversion per
block (or let the Python Document API speak bytes) and show the cost at N = 10,000 blocks.
Lead ruling on py-host's error policy: accepted as built — "nothing done" codes raise
`imtypeset.Error`; "done, differently" codes (NoFont, TextTooLong, MathRefused, LayoutFailed) return
False with the reason in last_error(). Reason: a widget that raises every frame over one refused
formula is unusable. Carry the same policy into Document. Also: `remove_font` on an empty role names
the internal face 'prose-italic' in a public message — fix in src/imtypeset.cpp while you own it.
Re-shoot docs/images/py-host.png (command in py-host's result) once harden-2 settles the fraction bar.
From harden-2 (you own include/imtypeset.h next; add these sentences, checking each against the code):
Reason/Message spell a NUL byte as `\x00`; a formula source over 524,288 bytes is refused; under
ImTypesetTextFlags_HardBreaks two newlines in a row give ONE line break (src/paragraph.cpp
de-duplicates hard breaks at one index, as the reference does) — the header's "one per newline" is
wrong. Lead ruling: document the behaviour as it is and add it to the known-gaps list (R10); do not
change it here — whether a host's blank lines should survive is a product rule to raise with the user.
From review-3a (read-derived; check first — `.concerns/review-3a--reviewer--findings.md` items 5, 6):
python/imtypeset range_rects/range_text turn a -1 (exception at the C boundary) into []/"" without
raising; src/imtypeset_c.cpp `terminated` cuts a (begin, end) ID or file name at the first NUL, so
"a\0b" and "a\0c" are one ID — state it in the header or refuse.
From tex-fixes: the block layer still defaults to reference rules — when wiring blocks into the public
Document API pass `ctx.tex_rules` (src/context.h) through, and select reference() in the golden replays
by the test-only hook. From bundle-route: under hello_imgui fonts are added in load_additional_fonts
(the atlas is cleared after post_init); python/imtypeset/_bundle.py's field census is by hand — any new
ImGui field the library touches needs a probe row.

## Added about 10:00 — READ THIS SECTION FIRST

USER PRIORITY (their words, 2026-10-10): "My need is being able to use imtypeset in advtext and,
importantly, pptmstr." Both run on stock imgui_bundle wheels through the sidecar that bundle-route
proved (`.concerns/bundle-route--builder--result.md`; spikes in python/spike_bundle/). So the Document
seam is judged by: can pptmstr's rich_pane / transcript_pane and an advtext view hand their blocks
over in ONE call per edit and draw/select in O(1) calls per frame, inside the wheel. Do the C++
core, the C ABI and the Python package in that order, and prove the Python side in BOTH venvs through
the sidecar (python/spike_bundle/pptmstr_pane.py is the shape), not only on python/host. Where time
runs short, cut the demo's Document section and the self-hosted example before the wheel proof.
- Apply `.concerns/demo--builder--doc-additions.md` (ShowDemoWindow in the public header, C counterpart,
  map row, THIRD-PARTY entry, decisions entry) and retire examples/imtypeset_demo.h as that file says.
  The demo's scripted input is by pixel coordinates: add any Document section LAST and re-look at
  every image. The demo TU cannot link in the wheel (fork's InputText* signatures): keep it out of
  the bundle library.
- `.concerns/review-round-3--reviewer--findings.md` items 3 (probe census gaps in
  python/imtypeset/_bundle.py) and 4 (X1 narrow, src/imtypeset.cpp): read-derived — check, then fix.
- From demo's builder (their observations): the header says a missing character is drawn as the
  face's missing glyph, but src/style.cpp stand_in tries Regular, Math, Code, then U+FFFD — make the
  header say what happens; the ImFont debug name is the first 39 bytes of the font PATH — use the
  file name; API gaps it had to build around are listed in `.concerns/demo--builder--result.md`
  (no public debug overlay, no handle-alive query, widgets report no line count, SelectableText has
  no granularity/copy hook): take the ones the Document object needs anyway, list the rest.
- Draw rules are in motion: d7-fix (rule placement) is writing src/draw.*, shipout.*, math_mlist.*
  now and word-gaps follows in src/draw.*. Not your files. Product draw/TeX rules are selected in
  src/imtypeset.cpp (yours): do not change which are selected.
- Guard every run (N = 10,000 tests especially): rules at the top of `.concerns/BOARD.md`.
