# device-finish progress (builder-43); appended after every item and preset

Base: 5077b22, working tree clean of source changes at the start (git status: only
`.concerns/review-9-device--reviewer--findings.md` untracked). Session premise (000-premises.md):
an earlier session was lost to a hard crash; no `device-finish` notes existed, so this starts
from nothing. Machine at the start: 31 GB, 9 GB available, the 975 MB of swap already full.

RESUMING AFTER A CRASH: read this file bottom-up; `git status --short` and `git diff --stat`
show what was written. The order is: source writes (items 1-10) -> hash the tree -> presets
from clean -> Python route. Nothing below the line "PRESET PASS BEGINS" may be followed by a
source write without a note saying what and what was re-run.

- 10:15 claimed; read the findings, the first builder's result, the board entry.
- 11:45 new test file tests/test_device_grid.cpp; REPRODUCTION AT HEAD saved in
  `.concerns/device-finish--builder--repro-head.txt` (built with -DREPRODUCTION_AT_HEAD in
  build/device-finish). Reproduced: 1 (absent Cc/Cf: 0.975 cells, glyphs 0.17 px off; guests
  1.86 cells, columns 3-5 px off and growing), 2 (in-place rebuild: 'aaaa' 40 / 'ZZZZ' 44 with
  +1 px extra advance; NoHinting does NOT move the cell at 22 px with the usual faces),
  density-push (15 px: 6 then 7), another size's entry (22 px laid out with the 16 px cell),
  3 (DrawParagraph true, 160 vertices, stale), 4 (273 -> 266.24, 1 block re-laid), 5 (hang
  3.70-4.05 cells), 7 (code paragraph justified, 3-6 px off a cell). Advance-only route: 1280
  advances equal by both routes.
- 12:30 first source pass written (builds, debug): src/fontmetrics.h (cell, guest_device),
  src/style.{h,cpp} (unspaced, stand_in on the grid's device, rigid spaces), src/device.{h,cpp}
  (rewritten: grid, grid devices, GetCharAdvance, witness check, entry held to its size),
  src/draw.{h,cpp} (face_bake always at rasteriser density 1, face_bake_size), src/blocks.{h,cpp}
  (tab columns in cells, hang in cells, CodeLines::cell), src/imtypeset.cpp, src/context.h,
  src/imtypeset_document.{h,cpp} (paragraph refused on another device; device kept off-context;
  ragged grid text; MeasuredWithoutDevice), version 0.4.0 in 7 places, C + Python mirrors.
  NOT YET: header prose, tests fixed up, docs, decisions, probe rows (none needed so far: no
  new ImGui FIELD is read; GetCharAdvance / IsGlyphLoaded / IsGlyphInFont are calls).
- 13:10 debug private build (build/device-finish, FreeType): 54 of 54 after two test changes that
  are NOT weakenings: tests/test_specimen.cpp now sets ImTypesetAdvances_Font (its golden is the
  reference's; it passed before only because its code row's em is a whole number of pixels and
  nothing compared glue until spaces became cells); tests/test_device_advances.cpp: a dangling
  ImFontGlyph* fixed (value copied before the library loads glyphs), and the off-context test
  rewritten to the new rule (ruling 4). NoHinting does not move Inconsolata's cell at any pixel
  height 13-24 (measured); FreeType's Bold flag and GlyphExtraAdvanceX do, and are what the
  test uses. tests/test_device_grid.cpp: 10 tests pass.
- 14:40 item 9 tests written (tests/test_device_grid.cpp, 17 tests, all pass; whole debug
  suite 54 of 54 in build/device-finish). FOUND by them and fixed in src/draw.{h,cpp}: two code
  cells of one table column, laid out at ONE x (319.4529 px), were drawn a pixel apart (320 and
  319) because a cell's first word was led from the last glyph of the cell before it
  (DrawRules::word_spaces_led). Now a word on a grid key is never led: FontBinder::on_grid.
  Observed and NOT changed: a click in the right part of a tab wider than a cell answers the
  position before the tab unless it is in the right half of the tab's last cell (older rule).
  Real ideograph (IPA Gothic read in place from /usr/share/fonts): U+4E2D advance 16 px, 2
  cells of 8.
- (clock on this machine reads ~11:05; earlier times in this file were guesses) Items 1-8, 10
  written and tested. Header prose, D79-D81, CHANGELOG 0.4.0, port-hazards, performance,
  versioning, sidecar notes written. Tests: 0.3.0-size refusal in C++ (test_device_grid), C
  (test_c_api.c), Python (test_binding.py). bench_code extended to 50,000 lines.
  Private runs so far, all on the current tree unless a later line says a file changed:
  debug FreeType whole suite 54/54; stb: test_device_grid 19, test_device_advances,
  test_word_gaps, test_hidpi ok; wchar16: the same four ok (after a harness fix); release:
  test_device_grid + test_device_advances ok; python_binding + python_demo_sections ok
  (build/device-finish-py, before the last formatting pass).
  Pictures looked at: .concerns/device-finish--builder--shot-freetype-{1x,1.5x-rows,2x,3x}.png.
  NEXT: asan on the device tests; then hash the tree and the presets from clean.
- PRESET PASS BEGINS (11 presets from clean, one at a time, -j4; log build/device-finish-gate/presets.txt). Source writes are finished: tools/ci/expected_imgui_symbols.txt was regenerated (two functions more) before this line.
- preset debug configure=0 build=0 (56 s, 0 warning lines, 0 in configure) ctest=0 (121 s): 100% tests passed, 0 tests failed out of 55
- preset release configure=0 build=0 (85 s, 0 warning lines, 0 in configure) ctest=0 (48 s): 100% tests passed, 0 tests failed out of 54
- preset asan configure=0 build=0 (104 s, 0 warning lines, 0 in configure) ctest=0 (233 s): 100% tests passed, 0 tests failed out of 54
- preset stb configure=0 build=0 (57 s, 0 warning lines, 0 in configure) ctest=0 (90 s): 100% tests passed, 0 tests failed out of 54
- preset wchar16 configure=0 build=0 (71 s, 0 warning lines, 0 in configure) ctest=0 (84 s): 100% tests passed, 0 tests failed out of 54
- preset amalgamation configure=0 build=0 (62 s, 0 warning lines, 0 in configure) ctest=0 (98 s): 100% tests passed, 0 tests failed out of 54
- preset fp-host configure=0 build=0 (92 s, 0 warning lines, 0 in configure) ctest=0 (50 s): 100% tests passed, 0 tests failed out of 54
- preset exceptions configure=0 build=0 (108 s, 4 warning lines, 0 in configure) ctest=0 (264 s): 100% tests passed, 0 tests failed out of 54
- preset bundle-config configure=0 build=0 (66 s, 4 warning lines, 0 in configure) ctest=0 (97 s): 100% tests passed, 0 tests failed out of 54
- preset imgui-next configure=0 build=0 (61 s, 0 warning lines, 1 in configure) ctest=8 (99 s): 98% tests passed, 1 tests failed out of 54
- preset python-host configure=0 build=0 (62 s, 0 warning lines, 0 in configure) ctest=0 (98 s): 100% tests passed, 0 tests failed out of 56
- AFTER THE PRESET PASS (10 of 11 green; imgui-next: test_device_grid failed in ONE test, a harness flaw: it read texture coordinates in a frame in which its own lookups loaded glyphs). ONE WRITE: tests/test_device_grid.cpp, the first frame of a_paragraph_in_the_code_face_... now does the lookups too. Re-running that test binary in all 11 presets.
- presets: 11 from clean; test_device_grid rebuilt and rerun in all 11 after the one test write (build/device-finish-gate/regrid.txt), imgui-next whole suite again 54/54. PYTHON ROUTE BEGINS: build/device-finish-gate/python.sh -> python.txt
- GATE DONE. Presets 11 green (imgui-next after one test-only write, re-run everywhere for that binary); Python route green in both environments; oracle, format, release pre-flight on a scratch copy (no tag). Result file FINAL. No helper was started; nothing is running.
