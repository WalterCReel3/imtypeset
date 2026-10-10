[from the reviewer of draw / paragraph / style] (filed and condensed by lead; READ from source, nothing
run, unless marked). style.cpp and paragraph.cpp are clean on read; all ten tex.web citations in the
three modules are correct. Two defects, both in src/draw.cpp. OWNER: whoever holds src/draw.cpp —
`stress` if still running, otherwise the follow-up the lead declares.

D1. UB: a glyph command whose key bits are all ones reads out of bounds (Drawer::place_commands).
    `Scaled key_bits = -1` is the "no key yet" sentinel; if the first glyph of a place_commands call
    has cmd.a == -1, key_slot() is never called, key_index stays 0, and the code reads `_latin[cp]` on
    a just-cleared vector, or `_keys[0]` in glyph_slot for codepoints >= 256. Not reachable through
    include/imtypeset.h today (FontKey::make keeps bit 63 clear) but draw.cpp says hand-built commands
    are in contract. Confirm under ASan: DrawCmd::glyph(FontKey{~0ull}, 'a', 0, 0, 0) as the first
    command of a run.
D2. Wrong pixels on valid input: for an UNBOUND face, ImGuiBinder::bake() writes
    state.font = ImGui::GetFont(), after which has_glyph checks IsGlyphInFont and drops codepoints the
    current font lacks. The reference never checks an unbound face (imgui_draw.py _Preambles.character)
    and draw.h documents "no check". Because the public draw path culls by line, which glyph is met
    first changes with scroll: a character can appear and vanish. Confirm: all faces null, default
    ImGui font, glyph commands 'a' then U+2014 -> reference glyphs=2 missing=0 with a fallback quad;
    port (read) glyphs=1 missing=1; swap the order and the dash is drawn.
D3. ListSink takes the white-pixel UV from the current context; ImGui's PrimRect takes it from
    draw_list->_Data. They differ for a draw list on other shared data.
D4. Past 64k vertices with 16-bit indices, reserving 1,024 quads at a time splits draw commands earlier
    than one AddText per glyph would (same vertices, different command boundaries).
D5. Tests: `the_vertices_are_those_of_one_add_text_per_glyph` SKIPS the reference-buffer comparison
    whenever any glyph box differs and only prints the count — "gated under FreeType" is not asserted;
    `commands_nobody_shipped_draw_nothing_and_break_nothing` has assertions that cannot fail
    (Size % 2, Size % 6) and omits D1's key; the unbound-face test uses only characters the default
    font has.
D6. Unsettled by reading: the atlas reaching its maximum texture size during ImGuiBinder::load —
    whether compaction can discard a bake an earlier key still points to. Needs a run with a very
    small TexMaxWidth/TexMaxHeight.
D7. (INFERRED, shared with the reference) glue inside an atom emits no SET_GLUE, so a whole inline
    formula is one rigid span while its rules are snapped absolutely; late in a long formula a surd can
    part from its bar. Input: `$a+b+c+d+e+f+g+h+i+j+k+l+m+\sqrt{x}$`. Render it and look.

For `tex-web-pass`: names — space_factor_after is TeX's adjust_space_factor, interword_glue is
app_space, unskip is delete_last; paragraph.h does not say tex.web has no first-fit breaker;
docs/port-hazards.md linebreak item 7 is itself wrong — tex.web §887 DOES skip a zero left skip (the
reference's departure is omitting on equality with the default spec rather than on zero).

Refuted: "interword glue re-exceeds MAX_DIMEN after the clamp" (public-api builder) — interword_glue
clamps on the way in and out in the current tree.
