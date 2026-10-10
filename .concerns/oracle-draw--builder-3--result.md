[from builder-3] oracle-draw done: vertices are box-local in their rasteriser sensitivity (measured: FT 2.12.1 = 0/7540 boxes differ, stb = 6657/7540); stb default config breaks the pixel-grid rule
Files: tools/oracle/dump_draw.py, tools/oracle/FORMAT-draw.md, tests/golden/draw/{tables,draw,boxes}.golden (2.15 MB; 22 draw cases, 2953 glyph commands, 11004 vertices). Two runs byte-identical. Nothing staged (D4) — and I unstaged everything I had staged during oracle-font, including builder-2's two files; index is empty again.

What the goldens are (matches docs/port-hazards.md 'draw' and D22): per case (1) `ops` = every draw-list call the reference made, with arguments = pen-level golden, with baked size/ascent per key as INPUT; (2) `box` = ImGui's glyph box per (key, codepoint); (3) vertices/triangles/draw cmds. The script ASSERTS two things on every case, so they are checked facts and not prose: (a) vertices == ops combined with boxes by the rule in FORMAT-draw.md "From ops to vertices" (all 2619 quads + every filled rect); (b) every op and the stats recomputed from the INPUT records by a from-scratch `replay()` that imports nothing of the reference equal the recorded ones. (b) confirms the reviewer's float32-hop description exactly: f32 at at(), widen, floor(+0.5); gap = px(integer sp difference).

FINDING YOU ASKED FOR — sensitivity of vertices to the rasteriser:
- pen_x never depends on the rasteriser: AdvanceX is not read at all (one add_text per glyph).
- pen_y contains -ascent and the loader's glyph Y0 contains +ascent; baked ascent is integral for every key (asserted), so it cancels. A quad depends on the rasteriser ONLY through that glyph's own box; nothing accumulates, a differing box moves one quad.
- Measured with a scratch C++ program against boxes.golden (7540 boxes: 6 files x 5 sizes, ASCII + Latin-1 + all 227 minted codepoints + variant bases), linked to .deps/imgui: system FreeType 2.12.1 -> 0 of 7540 boxes differ from the reference's 2.13.3, ascent/descent/rounded size identical. This extends D7's 'not measured' list (non-ASCII, math variants, other sizes) — all equal here. So gating full vertex equality in the FreeType build on this machine should pass; D22's 'reported, not gated' is conservative.
- stb_truetype, default ImFontConfig -> 6657 of 7540 differ, max 2 px; ascent/descent identical. With OversampleH=1 -> 4046 differ.
- NOT IN ANY TASK SPEC: under stb with the default ImFontConfig, OversampleH auto-resolves to 2 below 36 px and 776 of ~940 visible prose boxes have a FRACTIONAL X0 (366 fractional width). The reference's 'one texel to one pixel' device rule (docs/subpixel.md) is then silently broken in the port's stb build: pens are whole pixels, quads are not. OversampleH=1 or PixelSnapH restores integral boxes (measured). public-api/font-loader should set one of them on the faces it adds, or the stb preset ships blurry text. The reference never met this (FreeType only).
The scratch probe is build/oracle-draw/boxprobe.cpp (not in the tree, outside my touches); it would make a good tests/test_font_loader.cpp check.

Other things the port will trip on, each pinned by a case:
- A glyph with codepoint 0, U+000A or U+000D on a face that maps it (Roboto maps 0 and 0x0D) is 'drawn' by the reference: counted in stats.glyphs, advances the span, emits no quad (empty C string / RenderText's newline and CR handling). A direct PrimReserve path must special-case all three. Case span-edges-by-hand.
- No partial clipping: a quad straddling the clip rect is emitted whole; whole lines are dropped by y (pen_y > clip.y1, or f32(pen_y+size) < clip.y0), quads by x. Case clip-rect-cutting-lines (218 of 254 dropped).
- Size rounding is IM_ROUND in float32 on the requested size (asserted on 11 requests per face); the drawn size is the baked size, so scale is exactly 1.
- Font setup: the reference sets NO ImFontConfig field — add_font_from_file_ttf(path, 16.0) with defaults (read back into `fontconfig` records); 16 is a registration size only. Math role loaded from the on-disk cmap-patched copy (recorded as patched=1); the unpatched file sees 0 of 227 minted codepoints (case math-display-summation-unpatched-font: missing=1).

NOT verified / limits:
- Debug-overlay AddRect/AddLine vertices are recorded but not checked by any rule (AA-flag dependent); compare ops there.
- no-face-bound case: ops checked, vertices recorded only.
- No colored glyph exists in any bundled font: 'Colored glyphs ignore ink' is untested.
- The C++ box probe ran at -O1 on this machine only; pixels were not compared, only boxes.
- uv is written for 3 cases only (it depends on bake order/packer).
- Each run of the script leaves an advtext-fonts-* temp dir under $TMPDIR (the reference's imgui_font_file does that by design).

Least sure about: whether pinning the reference's FaceFont=None path (no-face-bound) is wanted at all — the port may reasonably drop it; the case is there so that dropping it is a decision and not an accident.
