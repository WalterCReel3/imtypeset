// How a face from src/fontlib gets into an ImGui font atlas so that every
// codepoint layout can name is one the atlas can rasterise — the synthetic
// codepoints minted for unencoded math variants included — with no font file
// written to disk.
//
// ImGui rasterises by codepoint: both of its loaders look the glyph up in the
// font's own cmap (stbtt_FindGlyphIndex in imgui_draw.cpp, FT_Get_Char_Index in
// misc/freetype/imgui_freetype.cpp), and an ImFontLoader's FontBakedLoadGlyph
// hook is itself entered with a codepoint. So the place a codepoint the font
// does not encode can be given a glyph is the cmap. `build_atlas_font` makes,
// in memory, a copy of the font whose cmap is one table written from
// src/fontlib's own view of it, and ImGui's built-in loader is then used
// unchanged.
//
// That is deliberately not a custom ImFontLoader:
//   - A custom loader has to rasterise the glyph itself, which means a second
//     copy of each of ImGui's two loaders (oversampling, density, atlas
//     packing), kept in step with them, one of them linking FreeType directly.
//   - It needs imgui_internal.h, where ImFontLoader lives and where it changed
//     between 1.92.9 and 1.93. Nothing here does: this file uses imgui.h only,
//     and AddFontFromMemoryTTF is the whole of what it calls.
//   - A host's own rasteriser choice, flags and ImFontConfig keep working,
//     because its loader is still the one that runs.
//   - The atlas and layout agree on every codepoint by construction: the cmap
//     ImGui reads is written from the mapping layout measured with, whichever
//     subtable stb_truetype or FreeType would have preferred in the original.
// The cost is one more copy of the font's bytes, owned by the atlas.
//
// A 16-bit ImWchar cannot name a codepoint above U+FFFF. For such a build the
// cmap also maps each stand-in FaceData assigns (a free BMP private-use
// codepoint per supplementary codepoint), and draw code asks the atlas for
// `atlas_codepoint(face, codepoint)` and never for a layout codepoint.

#pragma once

#include "fontlib.h"

#include "imgui.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ImTypeset::detail
{

// The width of this build's ImWchar: what FaceData's atlas mapping is asked with.
constexpr int ATLAS_WCHAR_BITS = static_cast<int>(sizeof(ImWchar) * 8);

// The codepoint to ask an atlas of this build for when layout means
// `codepoint` in this face. Identity when ImWchar is 32 bits or the codepoint
// is in the BMP; 0 when a 16-bit build has no stand-in for it.
inline uint32_t atlas_codepoint(const FaceData& face, uint32_t codepoint)
{
    return face.atlas_codepoint(codepoint, ATLAS_WCHAR_BITS);
}

// The face's font, as a single-face file whichever kind of file it came from
// (a face of a collection is written out with its own tables and no others),
// with its cmap replaced by a single (3,10) subtable (format 12, or format 13 for a face whose own
// cmap gives one glyph to whole groups of codepoints) holding: every codepoint the face
// maps to a real glyph; every codepoint minted for it; and, when `wchar_bits` is under 32, every
// BMP stand-in. All other tables are the file's own bytes; a DSIG table is dropped, since the
// signature no longer covers the file. One bit of OS/2 is cleared: fsSelection's USE_TYPO_METRICS,
// which makes FreeType size the font by OS/2's typographic ascender and descender where layout,
// like stb_truetype, sizes it by hhea's. A glyf header that puts a glyph's box at or below the
// baseline while its outline rises above it has its yMax raised over the outline
// (OpenTypeFont::atlas_box_repairs says why). Table checksums and head.checkSumAdjustment are
// recomputed.
//
// The result is never larger than the file plus the new cmap, the directory
// and padding.
//
// False, with `why` set, if the result cannot be expressed (a file over 2 GiB,
// which ImGui's int size cannot hold) or the file's tables overlap by more
// than the file's own size, which only a file made to be copied large does.
bool build_atlas_font(const FaceData& face, int wchar_bits, std::vector<uint8_t>& out,
                      std::string* why = nullptr);

// Adds the face to an atlas at a registration size, through
// AddFontFromMemoryTTF, with the cmap above. `config` is a template for the
// ImFontConfig, as ImGui's own Add functions take one; its FontData fields are
// ignored. Two fields are filled in when the template leaves them at their
// defaults: Name, and OversampleH, which becomes 1. ImGui's automatic choice
// is 2 below 36 px under stb_truetype, and that gives glyph boxes with
// fractional left edges, which defeats placing every glyph on a whole pixel;
// FreeType ignores the field.
//
// The face must pass OpenTypeFont::strict_check: a font the reader accepted
// with a damaged glyph is good for metrics and is refused here, because the
// rasteriser would follow the damage, and stb_truetype checks no offset it
// follows. What ImGui's AddFont asserts on is refused here first as well: a
// null or locked atlas, a RasterizerDensity that is not positive, a file of
// 100 bytes or fewer.
//
// NOT BOUNDED BY THE STRICT PASS: what a TrueType font's hinting program does.
// FreeType runs it, and a hostile one can move a glyph's points far outside
// the outline the pass measured; the glyph is then too large for the atlas,
// and ImGui asserts where its assertions are compiled in and drops the glyph
// where they are not. A host that takes fonts from strangers and rasterises
// with FreeType should pass ImGuiFreeTypeLoaderFlags_NoHinting in
// `config->FontLoaderFlags`. stb_truetype runs no font code.
//
// Null, with `why` set, for any of those, if the font could not be built, or
// if ImGui's loader refused it (FreeType does refuse some files that pass the
// strict pass; it says no more than that it did). The atlas owns the bytes
// and frees them with the font.
ImFont* add_face_to_atlas(ImFontAtlas* atlas, const FaceData& face, float size_pixels,
                          const ImFontConfig* config = nullptr, std::string* why = nullptr);

} // namespace ImTypeset::detail
