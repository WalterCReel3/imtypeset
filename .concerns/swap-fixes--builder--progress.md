# swap-fixes progress (builder)

- F3 REPRODUCED on unfixed tree (tests/test_api_answers.cpp, two new tests): forced collision ->
  GetFont returns the host's fonts, RemoveFont(Regular) removes a host font, add_font removes host
  fonts; natural allocator with exactly 7 host fonts -> GetFont returns a dangling pointer and
  RemoveFont runs ImFontAtlas::RemoveFont on it (assert `removed`).
- F3 FIXED in src/imtypeset.cpp + src/context.h: a mark in the last 8 bytes of ImFontConfig::Name,
  checked in validate_fonts (witness before the skip). test_api_answers 10/10 debug.
- NEXT: probe rows (shim FACT(ImFont, Sources), _bundle.py), F9, header text, F4, F7, F8, docs, looking.
- Probe: shim FACT(ImFont, Sources) + F9 static_assert; _bundle.py rows "ImFont.Sources", "ImFontConfig.Name mark" WRITTEN, NOT YET RUN (sidecars not rebuilt).
- Header: lifetime text, mark note at AddFontFromFileTTF, CalcTextSize sentence. DONE.
- F4 DONE: tests/test_specimen.cpp (2 tests, 2534 checks, exact; fails when off by 1 sp - checked), test_demo's CHECK_NEARs replaced by a two-decimals display check against the golden.
- NEXT: F7, F8, docs (decisions D59, status file, spec-status/README/CLAUDE scope), bench_tables format, range_text space, LOOK at columns/table, full suites, sidecars, mutate_gate.
- F7 DONE: list() snapshot + refused RELOAD keeps submodules (reproduced the second _capi on HEAD's package; fixed). New test in tests/python/test_binding.py (not yet run in ctest).
- F8 DONE: reproduced with a nine-live-contexts test at cap 8 (108 handles vs 27); cap raised to 32 with reason; Known limits states it; "No tables" limit removed (false). test_demo 21/21 debug.
- build/swap-fixes is now the full unnarrowed debug + python host.
- NEXT: LOOK at columns/table; docs; bench_tables format; range_text; suites; sidecars; mutate_gate.
- LOOKED (build/swap-fixes/look/*.png via a scratch driver of ShowDemoWindow): defects to report, none mine to fix:
  (1) a table's estimated height omits the header-rule gap: 4-row table 88 px estimated, 111.2 laid out; GetDocumentSize right after LayoutDocument is 23 px short until a geometry query or draw; first scroll to the end stops one row short.
  (2) header rule: 21.4 px between header row bottom and first body row top, rule nearer the body (asymmetric).
  (3) 2 columns fill order: table cut between header row and its rule (header at a column's foot, rule + body at next column's head).
  (4) Stream into last cell with TableHeld: columns 1 and 2 SHRINK ("The / golden / ratio" on 3 lines), contradicting the demo's "moves no other".
  (5) 3 columns + stream: the growing row overflows the column foot, clipped by the view, no vertical scroll (review-7 F2 seen).
  (6) table selection: blank between col 1 and col 2 highlighted in a fully selected row, not in a partly selected one.
  (7) columns: no common first baseline (heading column 5 px lower), widows/clubs at column tops/feet (c4).
- ALL DONE: debug 52/52, ASan+UBSan 50/50, release 50/50, sidecars+gate+mutate_gate, result written; build dirs removed.
