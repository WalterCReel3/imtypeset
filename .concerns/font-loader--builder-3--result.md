[from builder-3] font-loader done — NOT an ImFontLoader (filed by lead; ACCEPTED, see docs/decisions.md D27)

src/font_loader.{h,cpp}: build_atlas_font rewrites the cmap in memory (one
(3,10) format-12 subtable from fontlib's codepoint->glyph view + minted
codepoints + BMP stand-ins for a 16-bit ImWchar); add_face_to_atlas hands it
to AddFontFromMemoryTTF and sets OversampleH=1 when unset. No imgui_internal.h.

For draw / public-api: ask the atlas for detail::atlas_codepoint(face, cp)
(font_loader.h) and treat 0 as missing. add_face_to_atlas takes ImGui's
pixel-height size; em conversion is FontLibrary::em_size_for_pixel_height.

Ran: test_font_loader 7 tests on FreeType, stb, 16-bit ImWchar, ASan+UBSan,
release. Glyphs bake identically to ImGui's own load of the same file; all 227
minted glyphs visible; vs the reference's boxes FreeType 0 of 7,510 differ,
stb 6,657 (reported, not gated).

Least sure: FreeType's cmap validation on versions other than 2.12.1; a font
whose cmap maps codepoints past its last glyph (dropped by design).
