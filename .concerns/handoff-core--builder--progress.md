# handoff-core — progress (builder-35)

Kept so a session that dies can be resumed. Newest state at the bottom.

- start: tree clean at `caaeab3`; no earlier progress file for this task. Claimed on the board.
- reading done: HANDOFF-imtypeset.md, CLAUDE.md, port-hazards.md, advtext-swap-status.md,
  tables-fixes result.
- helpers launched (background): H12 (python audit; owns python/imtypeset/*.py, tests/python/test_binding.py,
  docs/python-errors.md until it reports), H13 (demo sizes; read-only, scratch in build/handoff-h13).
- private build: build/handoff-core (debug, full). Scratch: build/handoff-core-scratch/.
- H8 MEASURED (pinned reference, build/handoff-core-scratch/h8/ref_tables.py, 684 px): the reference's stacked
  heights are 6393984 / 13853632 / 7459648 / 20247616 / 8525312 sp = 129.6 / 280.8 / 151.2 / 410.4 / 172.8 px,
  i.e. advtext's "library" numbers; advtext's "reference" numbers (108.96 ...) are sum(_table_boxes heights),
  which the reference never draws (difference 1018301 sp = 20.64 px = baselineskip - rule). No code change.
  Tests in tests/test_api_handoff.cpp (new): two H8 tests pass under both rule sets.
- H11 REPRODUCED then FIXED in src/draw.cpp (emit + ImGuiBinder::load take the rules; product test is on the
  quad alone), src/draw.h comment. Test a_glyph_quad_is_emitted_exactly_when... failed 64 checks before, passes.
  TODO: look at pixels; header sentence.
- next: H10 (marks culling in draw_block), H9, H7, then header/C/Python, docs, gates.
- H13 helper DONE (report in my transcript; pictures kept in build/handoff-h13/): every section runs 10-40 px,
  size change between frames honoured; at 1100 px wide all fit to 13 px, all but Math to 19; Math's subset table
  clips from 16 px (SizingStretchProp columns, formulas never break) and inline formulas collide from 24;
  unwrapped ImGui::Text lines cut from 24/32; Stress/Document children 24/18 em tall. Proposed fixes: TextWrapped
  for info lines; fixed-width formula columns in ShowDemoMath. NOT yet applied; header sentence not yet written.
- H10: bench/bench_marks.cpp written (new file, outside touches); "before" tree = git archive HEAD in
  build/handoff-core-before (release).
- H10 DONE in src/imtypeset_document.{cpp,h} (MarkOrder, order_marks, pick_marks, bytes_shown; overlay fast path),
  tests in test_api_handoff.cpp (2), bench/bench_marks.cpp. RELEASE numbers (load avg 4-5, medians, 2000 frames,
  600-line code block, 768 px window, 1,881 marks): before (HEAD) none 54 us / Ink sorted 2,766 / Ink shuffled
  8,914 / Behind 710; after none 45 / 83 / 119 / 75. No precondition; sorted input skips the sort.
  Test weakness noted: generous margins hide small boundary errors (mutations of 2 bytes / one em pass; a gross
  mutation fails 326 checks).
- next: H9 (decide: python append_table_cells + advice vs a new call), H7, pixels for H11, header text, H13 text.
- H9 DECIDED: advice + (pending) Python Document.append_table_cells; no new C call; D64 invariant kept. Test
  a_row_arriving_in_pieces... in test_api_handoff.cpp: 200 rows, 7 bytes a piece: padded 944 replaces / 288,914
  cells measured / 96,418 rows set; unpadded 0 replaces / 1,513 cells / 1,410 rows set; whole 603 / 201.
- H7 DONE in C++ and C: ImTypesetFontMetrics, GetFontMetrics(face, em, out, out_size), GetGlyphAdvance; C mirror +
  static_asserts + map rows + test_c_api.c rows; 3 tests in test_api_handoff.cpp (42 role/size answers, 20,508
  advances equal to tests/golden/font; 7 role/size skipped: 14.4 is no float). PENDING: Python wrappers
  (font_metrics, glyph_advance, append_table_cells) after the H12 helper reports; decisions D66+; divergences.
- Header text written for H8-H11, H13 (H13 paragraph presumes demo fixes NOT yet made: revisit).
- TODO: H13 demo fixes + look; H11 pixels; clang-format; docs; full gates.
- H12 helper DONE: docs/python-errors.md, HostileInput class in tests/python/test_binding.py (491 rows), clamping
  in python/imtypeset/__init__.py + demo.py; scratch in build/h12-probe/. It found process-ending inputs needing C:
  DocumentView("") (ImGui assert: empty ID), ShowDemoWindow / ShowDemoSection outside a frame, double
  destroy_context / bogus ctx address, wrong handle kinds. TODO: fix DocumentView empty id + demo outside a frame
  in C++ (my files), then Python: font_metrics, glyph_advance, append_table_cells + rows in TABLE and the doc.
- H13 demo edits in progress in src/imtypeset_demo.cpp: PushTextWrapPos around info lines (done); Math table
  fixed-width formula columns: FIRST TRY BROKEN (display width from CalcTextSize is the wrap width) -> measuring
  with a paragraph's line instead. Scratch driver: build/handoff-core-scratch/h13/shot (from build/handoff-h13/shot.cpp).
- H13 DONE: src/imtypeset_demo.cpp (info lines wrap; Math subset table: formula columns fixed to the widest
  formula, measured by CalcTextSize / a paragraph's line; InsideFrame guard on ShowDemoWindow/ShowDemoSection);
  header paragraph at ShowDemoSection rewritten from pictures in build/handoff-core-scratch/h13/.
- H12 C side: DocumentView empty id -> "##document"; demo outside a frame draws nothing; tests in
  test_api_handoff.cpp (2). Python: font_metrics, glyph_advance, FontMetrics, Document.append_table_cells added;
  view("") guard removed; TABLE rows + doc rows added; python tests 101 OK + 4 OK against
  build/handoff-core-py (debug + python host, built from this tree).
- TODO: H11 pixels; clang-format (my C++ files) ; decisions D66..; divergences; full gates (debug, asan, release,
  amalgamation, python ctest, both sidecars + tools/bundle_route); result file; concern; complete_task.
- debug full suite: 51/51 (build/handoff-core), after docs; tree state at that time = final sources except later edits listed below
- ASan+UBSan full suite 51/51 (build/handoff-core-asan)
- final sources: ASan 51/51, release 51/51, debug 51/51, python-host 53/53 (gate-final.log)
- both sidecars rebuilt (stand-in headers; fork needs the network) and checked: 24/24 rows, mutate_gate 0
  survived, check_document_edits 23, check_swap_answers 18, new-calls script 8/8, in each venv.
- private build dirs removed. Result: .concerns/handoff-core--builder--result.md. DONE; nothing left running.
