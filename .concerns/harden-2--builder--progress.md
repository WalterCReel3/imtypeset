# harden-2 progress (builder-2, third holder; started 2026-10-10 after the second loss)

- START: claimed. Read board, scope, both review files, port-hazards. Found: NO fix is in src/ yet
  (MAX_SOURCE_BYTES, c_string_reason do not exist); tests/test_harden.cpp is written against fixes
  that are not there (it compiles only with -DHARDEN_REPRO). CMakeLists.txt already has
  IMTYPESET_ENABLE_EXCEPTIONS and IMTYPESET_IMGUI_USER_CONFIG. Next: shrink the H1 oversized case,
  then reproduce H1-H5 guarded.
- H1-H8 CODE IN (not yet under ASan): src/mathtext.{h,cpp} (MAX_SOURCE_BYTES = 2^19 refused in parse before
  decoding; TOO_LARGE/TOO_DEEP -> LAYOUT "the formula is too large to set: ..."; set_formula refuses a box
  outside +-MAX_DIMEN; quotient_to_two_places replaces "%.2f"; c_string_reason), src/math_mlist.{h,cpp}
  (Converter::node_of refuses any box outside MAX_DIMEN; dimension_outside_max_dimen), src/selection.{h,cpp}
  (held() clamp in build; ByIndex/Breaks order-tolerant lookups), src/imtypeset.cpp (ErrorState::set and
  FormulaRecord.reason go through c_string_reason). tests/test_harden.cpp: 12 tests, all pass in
  build/harden-2 (debug, guarded, LOCPATH=build/harden-2-locale for de_DE). tests/test_mathtext.cpp: H6
  (depth 50 = 2*MAX_DEPTH+2 measured by full recursion), H7 (set 1950 / refused 21 / by hand 66 exact).
  Pre-fix repro (ASan, guarded, per test): H1 x2 FAIL, H2 x2 FAIL, H3 FAIL under de_DE, H4 FAIL, H5 buffer:
  12 UBSan signed-overflow reports, H5 text out of order FAIL; H8 passes before and after (it is a guard).
  Test premises of the earlier holder that were wrong and corrected: "\qquad is exactly 24pt"; "an empty
  original copies as a space"; "malformed UTF-8 copies as written"; "\n\n copies as two newlines".
  NEXT: ASan run of the above; then D3, D4, D6, D7, X1-X5.
- ASan+UBSan (halt_on_error) guarded: test_harden 12/12, test_mathtext 17/17, test_selection 10/10 pass;
  test_math_mlist had 1 failure CAUSED BY MY H2 FIX (math_glue_stays_inside_the_dimension_invariant pinned a
  fraction box taller than MAX_DIMEN as ok) -> test updated to expect TOO_LARGE; not yet re-run under ASan.
- X2 + X3 + D3 CODE IN src/draw.{h,cpp}, tests/test_draw.cpp (lead's dist note says test_draw.cpp is mine):
  overlay outlines are own 1px rectangles (write_frame/write_segment), in both sinks; rectangles go through
  ImDrawList::PrimRect (white texel from the list's own shared data = D3 fix); glyph quads still through the
  [Internal] write pointers. test_draw 16/16 debug build. Debug goldens: 2 cases now compared at the calls
  only (port 136/1324 vertices vs reference 120/1268: own quads cannot equal AddRect's).
  X3 numbers (release, min of 3 interleaved runs, "sample: Drawer into a draw list", 3918 glyphs, us):
  original loop 66.6; final 70.1; every quad through PrimRectUV 80.9; inline PrimWriteVtx/Idx 72.5 (still
  reads _VtxCurrentIdx). Buffers row 69.5 -> 67.0. DECISION: keep the three write-position members.
  D3 repro test NOT YET WRITTEN. NEXT: D3/D4/D6 tests in test_harden.cpp, D7 render, X1, X4, X5.
- D3 fixed + test (d3_: own list with its own shared data; overlay geometry pinned). D4 decided: keep batching,
  test d4_ pins "same vertices, same triangles, command ends up to 94 quads earlier (bound 1,024)". D6 settled by
  a run (d6_: atlas capped 512x1024, 120 frames, 224 bakes discarded, repacked under a draw in 24 frames, all
  11,040 quads on live glyph rects) - debug build so far. D7 REPRODUCED (576 draws: bar -3..+3 px from the
  sign; pictures looked at: sign visibly parted from bar at em 13 / 60 terms and 1 px at em 24 / 13 terms) and
  FIXED as a product rule: DrawRules{rule_joins_span} in src/draw.h, reference() default (goldens untouched),
  product() set by src/imtypeset.cpp draw_args. After: joint identical in all 576 draws. Hand-worked test
  d7_a_rule_in_a_rigid_span_... NEXT: X5, X4, X1, then full ASan/release/stb/wchar16 runs, docs additions.
- X5 done: examples/demo_sdl2/main.cpp two edits; built with IMTYPESET_DEMO_WITH_LIBRARY=ON against ../example-project SDL2 (build/harden-2-demo), ran --page paragraph --frames 3 --screenshot under SDL dummy driver: rc 0, 22,692 dark pixels. NEXT: X4 (gprof), X1.
- X4: gprof (build/harden-2-prof, -pg) on bench_draw x100: evict 139 ns/call and widget_slot 145 ns/call of the widget call; cause: over the cache limit (3,400 shown > 2,048) evict() relinked the whole arrival list every frame. Fixed in src/imtypeset.cpp evict()/widget_for() + src/context.h (submitted_now/before). New test_api test a_steady_frame_larger_than_the_cache_limit_... (fails with the old condition: verified). Timing: machine noisy; one quiet interleaved round x100 1532->1330 us (content), 1523->1300 (TextWithID); x10 unchanged. Style memo and flat map considered, NOT done. NEXT: X1.
- X1 done in code: tests/test_exception_safety.cpp (7 tests; allocation-failure sweeps over SetParagraphText+
  LayoutParagraph (148 points), DrawParagraph (49), TextUnformatted (145), AddFontFromFileTTF (84 sampled),
  CreateContext/CreateParagraph (5); ImGui's "Out of texture memory" IM_ASSERT thrown under DrawParagraph and under
  TextUnformatted). Reproduced before fixing: CreateParagraph leaked (LSan); a paragraph unwound mid-layout stayed
  at 3 commands of 215 for good (flags set before the work); an atlas font orphaned when the registry threw.
  Fixes in src/imtypeset.cpp: Building guard on has_material/has_layout/has_lines/has_model; set_text invalidates
  first and sets given_size last; unique_ptr in CreateContext/CreateParagraph/widget_slot; pool reserved; InAtlas
  guard in add_font; WrapPos guard. No try/catch in src/. build/harden-2-exc (ASan+UBSan, exceptions ON, throwing
  IM_ASSERT): test_exception_safety 7/7, test_api 40, test_api_math 19, test_harden 18, test_draw 16,
  test_mathtext 17, test_selection 10, smoke 15 all pass, no leak reports.
  NEXT: bundle config trial; format; full runs (asan/release/stb/wchar16); doc-additions; result.
- D7 REWORKED after py-host's finding (reproduced: the first version placed a fraction bar from the numerator's
  LAST glyph; hand-worked case gives 46 where 48 is right). Now: a formula's rule joins the span; left end from the
  nearest formula glyph placed BEFORE it, right end from the nearest placed before or after (64 each way), settled
  when the span ends (Drawer::place_commands `settle`). ctx->draw_rules (context.h) lets a test pick reference().
  New test d7_a_fraction_bar_reaches_over_its_parts_as_they_were_drawn: reference rules up to 6.00 px (bar end) and
  4.91 px (radical joint) off; product <= 1.00 and 0.42. Pictures under both rules written and LOOKED AT
  (build/harden-2-shots): reference after 40 terms has the numerator 5 px off its bar and the sign over the b;
  product identical at 0 and 40 terms. NEXT: rerun the six-configuration verify, benches, result file.
- FINAL: six configurations 28/28 on final sources (debug, asan, release, stb, wchar16, exc); result + doc-additions written. Remaining: cleanup build dirs, concern to lead, complete_task.
- Lead's math-path MAX_DIMEN question: not reproduced (two tests added, ASan); rounded() left as it was. DONE; build dirs removed.
