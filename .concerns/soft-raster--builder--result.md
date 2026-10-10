[from builder] soft-raster done: header-only renderer + PNG/PPM writers; what example/draw should know, and one finding about stb glyph quads
Exists: tests/support/soft_raster.h (header-only: soft_raster::Renderer {init, render(ImDrawData*, Image&), shutdown, add_texture, find, stats}, Image, write_png, write_ppm, encode_png) and tests/test_soft_raster.cpp (17 tests). Include as "support/soft_raster.h" (tests/ is on the include path of tests, benches, examples).

Ran, private dirs, modules narrowed to imtypeset: debug 17 tests / 195 checks pass; ASan+UBSan same; stb_truetype build 191 checks pass (one sub-test skips with a printed reason, below); 16-bit ImWchar build passes. clang-format clean. Mutation check: 9 deliberate breaks of the header (edge ownership both ways, updates not uploaded, texel-centre offset, scissor, winding cull, blend alpha, DisplayPos) each fail at least one test. I rendered a real ImGui window (title bar, wrapped Roboto text, button, checkbox, slider, coloured text) to PNG and LOOKED at it: upright, channel order right, text legible. The PNG also decodes with Python's zlib (an independent decoder), CRCs verified.

For `example` / `draw` / `api` builders:
- Call renderer.init() after CreateContext (it sets RendererHasTextures + RendererHasVtxOffset itself; do not also set the flag by hand) and renderer.shutdown() before DestroyContext. render() does not clear: construct Image(w, h, opaque colour).
- write_png is stored-deflate (no compressor): ~4 bytes/pixel. A 1200x800 specimen is 3.8 MB. The example task says "keep files small" - it will need small canvases or an external optimiser; this writer cannot compress.
- Coverage is exact-integer top-left rule at 1/256 px; sampling is bilinear with texel centres at half-integers; no gamma. A glyph quad on whole pixels reproduces the atlas alpha exactly (tested texel-for-texel on 12 glyphs, 3 sizes, 2 fonts, both rasterisers).

Finding (affects `draw` and anyone judging "crisp vs blurry" in a specimen): under stb_truetype ImGui's OWN glyph quads are not on whole pixels (Roboto 20 px 'H': X0 = 0.75, advance 12.1667), under FreeType they are. So text rendered through the stb preset is bilinear-resampled by any correct backend and will look softer in images; that is ImGui+stb, not this rasteriser and not our draw code. Judge crispness in the FreeType build.

Licensing (D24): update_texture() and the scissor projection follow docs/BACKENDS.md's pseudo-code and six lines of imgui_impl_opengl3.cpp closely enough that I put the ImGui MIT notice at the point of use. It needs a THIRD-PARTY.md entry (docs task).

Not verified: MSVC; FramebufferScale with non-uniform x/y; textures in ImTextureFormat_Alpha8 (code path written, no ImGui configuration here produces one, untested); performance (it is per-pixel double arithmetic with no span setup - fine for tests, would be slow for a benchmark and must not be used as one).
