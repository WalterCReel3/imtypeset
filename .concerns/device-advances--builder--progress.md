# device-advances: progress (builder)

Written as the work goes, so that a session that dies can be resumed.

- 2026-10-12: claimed. Tree clean at edbaddb. No edits yet.
- Reading done: board entry, HANDOFF H34/H35, draw.{h,cpp}, fontlib, style.h,
  context.h, the pinned ImGui advance paths (FreeType: slot->advance/64/density,
  hinted => whole raster px; stb: units*scale*Size, fractional; PixelSnapH
  rounds in UNITS after dividing by density).
- Design direction (not built yet): a FontKey that says "measured on the
  device" (face id carries a device slot = pixels-per-inch + density), a
  MetricsResolver that answers such a key from ImFontBaked::GetCharAdvance of
  the very bake draw uses, rounded to a framebuffer pixel; kern 0; math stays
  on font advances. Draw needs no new rule: a whole-pixel gap rounds to itself.
- Next: tests/test_device_advances.cpp as the reproduction (table first).
- STEP 1 DONE: reproduction at HEAD under FreeType and stb, densities 1 and 2,
  ems and pixel heights 14..20, two face tables. Raw tables:
  `.concerns/device-advances--builder--repro-head.txt`. Their figures reproduce
  exactly at density 1 (mono px16: 7 x15, 8 x24; 297 / 297.43 / 312; glyph
  -16.91 from its rectangle; fill +13/+12; hit tests wrong 170 of 340).
  The test file is tests/test_device_advances.cpp (has a temporary
  `#define REPRODUCTION_AT_HEAD 1` at the top: remove once ImTypesetAdvances_
  exists). Private builds: build/device-advances (FreeType, Release),
  build/device-advances-stb.
- Known test defect to fix: baked_size_of() predicts ImGui's size from float
  metrics and misses a tie (usual em16 d2: 37.5); use the library's own size
  function once it is factored out of draw.cpp's bake().
- H35 (text on font advances) BUILT in src/draw.{h,cpp}: DrawRules::word_glyphs_held
  (product on), WORD_GLYPH_BOUND_PX = 1. Span::note + `give` lambda in
  place_commands; end_span takes the run's candidate instead of settle_word when
  the rule is on. Measured with test_word_gaps's tallies (release, files
  `.concerns/device-advances--builder--word-gaps-{before,after}.txt`): pairs over
  half a pixel 13348 -> 13660 of 150624; over 0.6: 3632 -> 3834; over 0.75:
  118 -> 146; worst glyph from its place 2.80 -> 1.91 px; spaces unchanged.
  STILL TO DO there: tests/test_word_gaps.cpp fails (its own model of the
  product at `pens_that_differ(drawn, PRODUCT)`, and the hand tests
  a_list_with_an_index_repeated..., a_word_of_twenty_thousand...): bring its
  model to the new rule; do not weaken.
- NEXT: the device-advance feature itself (FontSpec.device, key bits 0x4000,
  src/device.{h,cpp}, resolve_style, document ready(), public API, C, Python).
- DEVICE ADVANCES BUILT (C++ and C): src/device.{h,cpp} (new), FontSpec.device +
  key bit 0x4000 (src/style.*), face_bake (src/draw.*), TexRules::code_on_device,
  ResolvedStyle.{advances,device,device_faces,device_epoch} + resolve_device /
  device_current (src/context.h, src/imtypeset.cpp), document: measured_style,
  own_advances, device_seen in take_style/ready (src/imtypeset_document.*).
  Public: ImTypesetAdvances_, ImTypesetStyle::Advances, ImTypesetBlockStyle::
  Advances, GetDeviceGlyphAdvance (+ C mirror; C ABI revision left at 1).
- tests/test_device_advances.cpp PASSES in build/device-advances (FreeType,
  Release) and build/device-advances-stb: 5 tests. After-table draft:
  `.concerns/device-advances--builder--after-freetype-draft.txt`.
- TO DO, in order: (1) tests/test_word_gaps.cpp model; (2) tests for caches and
  counters, kerning off, math on font advances, reference rules, unbound context,
  GetDeviceGlyphAdvance; (3) full debug suite for fallout; (4) Python binding,
  _capi.py struct fields, probe row for ImFontGlyph.AdvanceX (FACT in
  python/host/imtypeset_bundle_shim.cpp + _bundle.py), mutate_gate; (5) demo row,
  docs (decisions D77.., port-hazards, CHANGELOG, python-errors, font-support?),
  benches; (6) all presets; pictures; result file.
- tests/test_word_gaps.cpp brought to the new rule (its own model: Rule::HELD_GLYPHS;
  two hand tests state both rules). tests/test_c_api_map.cpp counts updated.
- tests/test_device_advances.cpp now 14 tests (pages x3 modes, PixelSnapH, document
  counters at density change / size change / edit, block-level Advances, paragraph +
  widget, kerns + math, reference rules, missing ImGui context, GetDeviceGlyphAdvance,
  device-number wraparound). PASSES debug FreeType (build/device-advances-dbg).
- Whole debug suite in build/device-advances-dbg: 53 of 53 pass (run before the last
  test additions; test_c_api_map fixed after).
- NEXT: Python (_capi.py fields, Style/BlockStyle, Advances enum, device_glyph_advance,
  tests/python, docs/python-errors.md), sidecar probe row (ImFontGlyph.AdvanceX), then
  demo, docs/decisions, benches, presets, pictures.
- Python package edited (not yet RUN): _capi.py (Advances fields, GetDeviceGlyphAdvance),
  __init__.py (Advances enum, device_glyph_advance), tests/python/test_binding.py (one test,
  15 hostile rows), docs/python-errors.md. Sidecar: FACT(ImFontGlyph, AdvanceX) in
  python/host/imtypeset_bundle_shim.cpp and the probe row "ImFontGlyph.AdvanceX" in
  python/imtypeset/_bundle.py (not yet RUN; mutate_gate not yet run).
- Demo: Known limits has the ruler three ways (src/imtypeset_demo.cpp). test_demo passes.
- NEXT: bench (5,000-line code document: first layout, steady, density change), bench_draw
  before/after; python_binding build+run; sidecars; docs (decisions, port-hazards, CHANGELOG,
  performance.md); clang-format; presets; pictures; result.
- H35 draw rule reworked to a backward look taken only when a glyph passes the bound
  (Drawer::give_in_run, Span::floor_op, DrawOp::y1 = a word glyph's drift). Same pens as
  the first form (test_word_gaps's independent model: 0 differ). bench_draw A/B, release,
  best of six interleaved runs (load 5-14): full draw x1 149.3 -> 150.6 us, x100 17.30 ->
  18.14 ms; file `.concerns/device-advances--builder--bench-draw-ab.txt`.
- bench/bench_code.cpp (new): `.concerns/device-advances--builder--bench-code.txt`.
- python_binding + python_demo_sections PASS in build/device-advances-py (102 + 4 tests).
- NEXT: sidecars (stand-in) + probe + mutate_gate + tools/bundle_route checks; docs
  (decisions D77/D78, port-hazards draw, CHANGELOG, performance.md, sidecar.md row count,
  bindings-notes census); clang-format; all presets; pictures; result.
- SIDECARS: build/device-advances-bundle-advtext (wheel 1.92.900) and
  build/device-advances-bundle-pptmstr (1.92.801), stand-in headers: build exit 0, 0 warning
  lines, probe 26 of 26 (new row ImFontGlyph.AdvanceX), run_gate accepted, mutate_gate
  32 offsets / 93 mutants / 12 crashed / 0 survived, check_swap_answers 18 PASS,
  check_document_edits 23 PASS, check_device_advances (NEW tool) 70 PASS, in both venvs.
- NEXT: docs; clang-format; presets (debug, asan, release, stb, wchar16, amalgamation);
  pictures; result file; concern to lead; complete_task.
- Docs written: docs/decisions.md D77 + D78, docs/port-hazards.md (last section),
  docs/CHANGELOG.md ("Unreleased"), docs/performance.md, docs/sidecar.md, docs/bindings-notes.md,
  docs/python-errors.md. clang-format run on the changed C++ files.
- FINAL RUNS so far: debug full suite 53/53 (build/device-advances-dbg) after all changes.
  Still to run: asan, release, stb, wchar16, amalgamation preset equivalent, python_binding
  again, sidecars again (sources changed since: draw.cpp rework came BEFORE the sidecar
  builds? NO - rebuild both sidecars at the end), pictures, result file.
- FINAL RUNS (after all code changes and clang-format): debug 53/53, ASan+UBSan 53/53 (553 s),
  release 53/53, stb (debug) 53/53, 16-bit ImWchar (debug) 53/53; each includes the
  `amalgamation` ctest. Dirs build/device-advances{-dbg,-asan,,-stb,-w16}.
- STILL: python_binding rerun; both sidecars rebuild + checks; after-tables; pictures; result.
- DONE. Result: `.concerns/device-advances--builder--result.md`. Final runs in its section 8.
  Nothing staged or committed.
