[from builder-6] draw done (filed and condensed by lead). src/draw.{h,cpp} (imgui.h only),
tests/test_draw.cpp, bench/bench_draw.cpp (the `bench` task may replace it).

API: `Drawer` places commands into a list of calls, loads the glyphs, then writes quads into an
ImDrawList or into DrawBuffers (32-bit indices; no debug-overlay outlines). `index_lines` / `LineIndex`
give opt-in culling by line (2 px margin; with it on the output is not byte-equal to the reference by
design). `Palette`, and the pure helpers (snap_px etc.).

Ran: test_draw 14 tests in Debug/FreeType (17,107 checks), ASan+UBSan, Release, stb, 16-bit ImWchar.
Pen-level calls match the goldens exactly on all 22 cases. Under any rasteriser the draw list equals
byte for byte what ImGui produces from the same calls made one AddText at a time (22 cases + 1,164
swept clip rects). Vs the reference's buffers: FreeType 2.12.1 22 of 22 cases equal outright; stb 3 of
22 (399 of 583 glyph boxes differ).

Benchmark, indicative (Release -O3, FreeType, single run, medians; page = tiled golden command lists,
4,377 glyph commands, NOT demo/sample.md): full draw 68.7 us (15.7 ns/glyph); the same calls as one
AddText each 102.9 us; buffer output 72.9 us; 600 px window on a 200,142-glyph page with the line
index 15.0 us, without 2.83 ms.

Decisions: the "no font bound" path is kept (current ImGui font); an unknown face id, a size baking
under 0.5 px or over 2,048 px, and a non-codepoint are counted as `missing`; codepoint 0 is asked of
the atlas as itself. draw.cpp carries a Dear ImGui notice -> THIRD-PARTY.md entry.
Not verified: coloured glyphs; FontRasterizerDensity != 1; a draw list from another ImGui context;
the atlas hitting max texture size mid-draw.
