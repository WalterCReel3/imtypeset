# patch-geometry (builder): H27 and H49 for 0.4.3

## Reproduced first (tests written before the fix, both failed)

- H27, `tests/test_api_code_caret.cpp`: "ab\n\ty" gave cells 0,1,0,0,4,5 (imscribe's numbers).
  The blank-line case was AS THE TRIAGE SAID: in "a\n\nb" the caret at byte 2 (the blank
  line's newline) was on line 2, and a click on the blank line answered byte 3 ('b'), Line 2.
  Cause confirmed: no item carries a newline (`walk_code` emits none; a blank line is an empty
  box), so a line's end and the next line's start shared one index.
- H49, `tests/test_api_device_advance.cpp`: code face '0' at pixel heights 12-24 gave
  5.0000081, 6.0000014, 7.0000153 ... (imscribe's residues); at density 1.5 likewise.

## Fixes

- H27: `CodeExtent::breaks` (src/blocks.*) -> `DocBlock::code_breaks` (src/imtypeset_document.*):
  the byte of each source line's end ("\n", the "\r" of "\r\n", lone "\r"). Caret at either
  byte of an end -> end of that source line's LAST visual line (`last_box_of` over
  `BlockLayout::continues`); x is the line box's x1, or under Clip the first glyph's x + the
  line's natural width (`BlockLayout::naturals`, recorded by `code_boxes` only when a line
  was clipped). Hit: when the model's fragment is the last piece of a source line and the
  boundary is at/after its last item -> Offset = that line end, Line = that box. Wrap breaks
  keep the header rule (later piece's start). Index map, range rects, copies untouched.
  Deliberate consequence: a drag ending past a line's end ends before its newline (copy has no
  trailing "\n"), where it ended after it.
- H49: `GetDeviceGlyphAdvance` returns `py_round(px_from_sp(width, ppi*density)) / density`
  using the measuring Device. Equal to `snap_px(ImFontBaked::GetCharAdvance)/density`; the
  atlas number is whole under FreeType (checked), so equal to it there.
- Header: H49 paragraph reworded (whole fb px / density; caret and range rects keep < 1 sp per
  character, under 0.00003 px at 96 ppi); a POSITIONS sentence on code line ends. CHANGELOG
  `## 0.4.3 (in progress)` with both; decisions D88. No new public name.

## Evidence layout did not change

Every golden replay, test_device_grid and test_device_advances pass unchanged; the only
layout-side code added stores `packed.natural`, already computed by hpack.

## Results

- debug (private dir, all modules, all tests): 56/56 ctest pass.
- ASan+UBSan (private dir): 56/56 pass.
- stb_truetype (private dir): test_api_code_caret, test_api_device_advance, test_device_grid,
  test_device_advances pass.
- release, wchar16, amalgamation preset, exceptions, python-host: NOT run.
- Python: not run (the package passes the same C calls through, with byte<->char conversion).
- C entries: checked in test_api_code_caret (pass-through).

## Status-file rows (docs/advtext-swap-status.md; lead to paste)

| H27 | a caret at a code line's end | **fixed for 0.4.3** (task `patch-geometry`, D88; reproduced first, run): a code line's end ("\n", either byte of "\r\n", a lone "\r") is a position of its own. The caret there stands after the line's last character on its last visual line ("ab\n\ty": cells 0, 1, 2, 0, 4, 5), a blank line's caret is its start, under Clip it is where the characters end, not the clip; a click past a line's end or anywhere on a blank line answers that line end and `Line` under the point. Wider than reported, as the triage read: a blank line held no caret and a click on it answered the next line. Under Wrap a break between pieces stays the later piece's start. A drag ending past a line's end now stops before its newline. Range rectangles, copies, layout unchanged | fixed (0.4.3) |
| H49 | `device_glyph_advance` near, not on, a whole pixel | **fixed for 0.4.3** (task `patch-geometry`, D88; run): answers whole framebuffer pixels divided by the density, equal to Dear ImGui's `GetCharAdvance` for the cell (heights 12 to 24, densities 1, 1.5, 2, FreeType; under stb_truetype the atlas number rounded as PixelSnapH would). Carets and range rectangles keep a residue under one scaled point a character (under 0.00003 px); the header says so | fixed (0.4.3) |
