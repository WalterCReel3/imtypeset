# review-round-3 findings (filed by the lead from the reviewer's concern, 2026-10-10 about 10:00)

ALL READ-DERIVED; nothing was run. Confirmed by re-derivation from tex.web: all nine hand answers in
tests/test_tex_rules.cpp (B5, B6, B7 at 2,600, easy_line x3, Y1, M10, M14; M9 and M13 partly — font
figures and the nested fraction's §748 hbox not worked), every cited section number, try_break_by_class
against §829-855, reference() unchanged behind flags, no tolerance, no golden touched, H1/H5 bounds,
X1 guards, cmap format 13 never expanded.

1. LATENT: `ParagraphComposer::map_shipped` (src/paragraph.cpp,
   `const size_t left = _line_params.left_skip != GlueSpec() ? 1 : 0;`) uses the reference's left-skip
   test while `LineBreaker::left_skip_omitted()` under product() tests width/stretch/shrink == 0.
   A left skip {0, 0, FIL, 0, NORMAL} under product() would put every shipped index off by one.
   Not reachable today (skips() overwrites the left skip); live the day a caller passes one.
   -> task b6-map-shipped.
2. easy_line is O(n x breakpoints per line); the work budget is the only bound. Ragged total fit in
   a column wide enough to hold the paragraph (or zero-advance font) is n^2/2 until the budget falls
   back to first fit: correct, but a long ragged total-fit paragraph in a wide column silently
   becomes first fit (roughly words x words-per-line > 1,048,576). -> docs: say "linear at a fixed
   measure", and state the fallback.
3. python/imtypeset/_bundle.py census gaps: (a) ImDrawVert member offsets (library's own, incl. uv)
   never compared; (b) ImFontGlyph::Colored only tested 0 == 0; (c) no enum constant probed:
   ImGuiCol_Text, ImGuiCol_TextSelectedBg, ImGuiBackendFlags_RendererHasTextures (library's compiled
   value), ImGuiKey_A, ImGuiKey_C, ImGuiMod_Ctrl, ImGuiMouseCursor_TextInput, ImGuiMouseButton_Left.
   -> api-document (owns python/imtypeset).
4. X1 narrow (src/imtypeset.cpp): ~InAtlas calls RemoveFont and ~WrapPos calls PopTextWrapPos — an
   IM_ASSERT inside either in the throwing build is std::terminate (no trigger found); in AddFont
   after `added.font = nullptr`, an unwind out of RemoveFont(old)/refresh_fonts leaves the old font
   in the atlas with no role. -> api-document (owns src/imtypeset.cpp).
5. Test name `easy_line_the_last_resort_is_forced_from_the_cheapest_chain` overclaims: §854 forces
   from the LAST active node. -> b6-map-shipped (rename).
Not reached: byte-identity of try_break (no diff tool), the corpus-count tests, bench_reflow, the
28,841 -> 28,839 check count in test_api_math, FontLibrary's unwind, the bundle build script.
