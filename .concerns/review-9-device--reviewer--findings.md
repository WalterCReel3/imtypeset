# review-9-device findings (filed by the lead, 2026-10-12) — ALL READ-DERIVED, nothing run

Full text: the notes on board task `review-9-device` (read_board()). Commit reviewed: 5077b22.
Numbers are hand arithmetic from the builder's table (mono at pixel height 16: font advance 7.6263,
baked 8; at 15: baked 7; at 22: 11).

1. MOST SEVERE. A Code row holding a character the code face lacks leaves the grid; every row in
   tests/test_device_advances.cpp CODE_ROWS is ASCII.
   - Fractional cell: for space/Cc/Cf/Mn/Me characters `stand_in` (src/style.cpp) returns the
     character unchanged; `device_advance_px` (src/device.cpp) fails at `!font.has(codepoint)` and
     `DeviceMetrics::advance` falls back to the font file's .notdef advance, memoised under the device
     key (BOM, U+200B, U+200D, U+FE0F, ESC, U+0301 when the face lacks them). Row `a<U+200B>bcd` at 16,
     .notdef assumed 0.5 em: layout puts `b` at 8 + 7.6263 = 15.6263; the draw skips the absent glyph,
     gap snap_px(15.6263) = 16; every later glyph 0.3737 px right of its rectangle, row off the 8 px grid.
   - Another face's cell: any other absent codepoint (U+4E2D, emoji) is borrowed through
     `font_key(face, bound.size, bound.device)`, ending at U+FFFD or `?` in the regular face: whole
     pixels but not the cell; later columns shift cumulatively.
2. The memo of device advances is not keyed to how the atlas rasterises: `ctx.cache` and
   `ctx.devices.metrics` are dropped only by `forget_measurements`; `validate_fonts` sees only a font
   leaving the atlas. Trigger with no host code: ImGui's own Fonts debug UI changes loader flags in
   place (.deps/imgui/imgui.cpp, `atlas->FontLoaderFlags = loader_flags`, `src->FontLoaderFlags = …`),
   same ImFont*, FontId, count. At 22 px: lay out `aaaa` (cell 11), tick NoHinting, lay out `ZZZZ`
   (10.486 -> 10): one document with cells of 11 and 10. Not a crash. Same class at density 1:
   `face_bake` (src/draw.cpp) calls GetFontBaked(requested) with no density, so it follows
   ImFont::CurrentRasterizerDensity, which changes when a host pushes the role's font; at 15 px a
   density-2 bake gives 7.5 -> 8 against 7. Passing 1.0f explicitly removes the dependence.
3. A retained Paragraph is drawn stale after a density change, silently: `device_current`
   (src/context.h, src/imtypeset.cpp) is defined and called nowhere; `can_draw` tests the font
   generation, not the device. Laid out at density 1, 15 px, drawn at density 2: the 40th character
   at 546 framebuffer px against 585 for a re-laid Document beside it.
4. A Document queried while its ImGui context is not current is RE-LAID from font advances and
   answers from that layout (`ready()` in src/imtypeset_document.cpp: `device_number` returns 0 when
   unbound): rectangles up to 0.3737 x N px from the glyphs on screen; the next draw re-lays
   everything again. The builder's test asserts it as intended; no decision records it.
5. Wrapped code continuations are off the grid: the hang is 2 * em (src/blocks.cpp,
   `code_continuation_indent`) = 30.505 px at 16 = 3.81 cells.
6. API: IMTYPESET_VERSION still 0.3.0 while two host-filled structs grew by 4 bytes (a C++ host on
   v0.3.0 headers that skips IMTYPESET_CHECKVERSION has `Advances` read past its struct: validated,
   so Font/Device/Default by stack garbage); the header's "zero keeps meaning 'as before'" near a
   default that changes every Code-face width; `device_advance_px` uses FindGlyph (rasterises and
   packs) where GetCharAdvance is ImGui's metrics-only route — layout fills the atlas with glyphs that
   may never be drawn.
7. If ImGui ever hands back another size's glyph, its advance is memoised as the cell for the life
   of the context (not established from imgui_freetype.cpp).
8. Test coverage: ASCII only, whole-pixel origin, densities 1 and 2, sizes 14-20; not 22, 24.
Also reasoned only: a Code-face PARAGRAPH is justified by default, so its columns will not agree
line to line; at fractional densities ImGui rasterises 15 x 1.5 = 22.5 px where the library rounds
to 23.
Confirmed by reading: units above density 1 (N characters = N x one constant in logical units at
1.5); the FreeType and stb advance formulas; tabs are whole cells; Document and widget invalidation
on size, density, a block's Advances, an atlas cleared and refilled; `word_glyphs_held` by hand
(drifts <= 1 px, D48/D50 bounds hold, linear, allocation-free, reference() untouched); the 1/256
tolerance (a whole pixel is not a whole scaled point: 8.9e-6 px a character; could be N x 0.5 sp);
pens compared with real ImDrawList::AddText vertices.
Not reached: tables, marks and FixedLineHeight, columns, an inline formula in a device run, the C
mirror and Python shapes, the probe row.
