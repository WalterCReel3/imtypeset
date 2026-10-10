# Lead scope note for `harden-2` (read before the board spec; it adds to it)

State when you start: `stress` is done (docs/hardening.md, D33) and already fixed draw items D1, D2, D5
and answered D6 by reading; `api-revise` is done (D36) and reshaped the public header; `font-wild` may
still be running in src/opentype.*, src/fontlib.*, src/font_loader.*, tests/test_stress_font.cpp,
tests/test_opentype_formats.cpp — do not touch those, and expect those two tests to fail until it
finishes (it is fixing four reader defects its helper found); `c-api` runs in parallel in NEW files.

YOUR ITEMS
H1-H8  from .concerns/review-math-selection--reviewer--findings.md.
D3, D4, D7 from .concerns/review-draw-para-style--reviewer--findings.md (stress left them: D3 "needs
       imgui_internal.h" — check that claim: ImDrawList::_Data is declared in imgui.h; if the white
       pixel UV is reachable through public declarations use it, otherwise record why not; D4 only
       moves draw-command boundaries — decide and record; D7 needs the render-and-look).
D6     settle by a RUN, not a reading: a very small TexMaxWidth/TexMaxHeight so the atlas fills
       during a draw.
X1  Exception safety (.concerns/py-host--lead--rulings.md): imgui_bundle builds ImGui with an
    IM_ASSERT that THROWS. Add a test configuration that compiles the library and tests WITH
    exceptions enabled and IM_ASSERT defined to throw (a small test-only IMGUI_USER_CONFIG in tests/;
    also try the bundle's own ../imgui_bundle/litgen_imconfig.h read-only when present and report what
    it needs), and tests that throw from inside layout, draw (between a PrimReserve and its writes),
    font registration and the widget entry: afterwards the context is usable, the draw list is
    consistent (no reserved-but-unwritten vertices: use ImDrawList::PrimUnreserve or reserve after
    all fallible work), nothing leaks under ASan, caches are consistent. The library must still build
    with -fno-exceptions: no try/catch in src/ — use RAII guards only.
X2  Debug overlay version-neutral (.concerns/dist--lead--from-dist-research.md item 3): draw the
    overlay's outlines as the library's own quads instead of ImGui's AddRect/AddLine, whose geometry
    changed in ImGui 1.93.
X3  [Internal] ImDrawList members (same file, item 4): measure with bench_draw whether writing through
    ImGui's public inline PrimWriteVtx/PrimWriteIdx/PrimRectUV costs anything against the four
    [Internal] members src/draw.cpp uses; switch if within noise, else keep and record the number.
X4  Widget-call regression from api-revise (.concerns/api-revise--builder-23--result.md): a steady
    `TextUnformatted` call is 1.03x / 1.16x / 1.44x slower than the old pointer-keyed `Text` at
    34 / 340 / 3,400 calls a frame (110-180 ns more per call), cause not isolated; hit test 1.30x.
    Find where the time goes with manual timers or gprof (perf is not installed) — suspects: hashing
    the whole text every call, the ID-stack lookup, the LRU bookkeeping, an ImGui call per widget —
    and remove what is avoidable WITHOUT going back to pointer identity (e.g. hash once and compare
    length + hash before bytes; skip hashing entirely for `TextWithID`). You own src/imtypeset.cpp and
    src/context.h for X4 and X1 only; no declaration in include/imtypeset.h changes.
X5  examples/demo_sdl2/main.cpp no longer compiles against the revised header (three-line fix in the
    api-revise result file): apply it; that file is yours for this.
Files this note adds to your `touches`: src/imtypeset.cpp, src/context.h, tests/test_api.cpp (new
tests only), tests/test_exception_safety.cpp, tests/imconfig_throwing.h, examples/demo_sdl2/main.cpp,
bench/bench_draw.cpp, docs/performance.md, docs/decisions.md, docs/divergences.md (append only).
