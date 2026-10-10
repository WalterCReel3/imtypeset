// Fonts by role: which file backs each Face, and FontMetrics over a real file.
// The port of the reference's advtext/fontlib.py, on src/opentype in place of
// fontTools. The only module that owns font bytes.
//
// WHAT DIFFERS FROM THE REFERENCE, on purpose:
//   - There are no bundled files. Every role is registered by the host; the
//     reference's defaults are whatever imgui-bundle ships.
//   - A FontLibrary is an object, not module state, so a context owns one.
//   - A file is parsed when it is registered, so a font that cannot be used is
//     refused there, with a reason, and not at the first glyph of a frame.
//   - Failure is a null result and a reason string where the reference raises
//     MissingFontError.
//   - Synthetic codepoints are minted as the reference mints them, but nothing
//     rewrites a font file for them: `FaceData::glyph` and the atlas mapping
//     below are what the font loader uses instead.
//
// REGISTRATION SHOULD PRECEDE BUILDING, as in the reference: numbers already
// taken from an old file (kerns baked into material, widths a char_measure
// memoised) are not repaired by registering a new one. What this library
// handed out stays valid, though — a FaceData or FontMetrics that a registry
// change replaces is kept alive until the library is destroyed, so a pointer
// held across the change still answers, from the old file. `generation()`
// changes whenever the registry does.
//
// TeX's math parameters come from the OpenType MATH table like this (TeX name,
// MathConstants entry): sigma5 math_x_height = OS/2.sxHeight; sigma6 math_quad
// = 1 em; num1 = FractionNumeratorDisplayStyleShiftUp; num2 =
// FractionNumeratorShiftUp; num3 = StackTopShiftUp; denom1 =
// FractionDenominatorDisplayStyleShiftDown; denom2 =
// FractionDenominatorShiftDown; sup1 = sup2 = SuperscriptShiftUp; sup3 =
// SuperscriptShiftUpCramped; sub1 = sub2 = SubscriptShiftDown; sup_drop =
// SuperscriptBaselineDropMax; sub_drop = SubscriptBaselineDropMin; axis_height
// = AxisHeight; default_rule_thickness = FractionRuleThickness;
// big_op_spacing1..4 = UpperLimitGapMin, LowerLimitGapMin,
// UpperLimitBaselineRiseMin, LowerLimitBaselineDropMin; the three radical
// values and DisplayOperatorMinHeight by name; the script scales are the two
// percentages over 100. Synthesised, having no OpenType counterpart: delim1 =
// 2.4 em, delim2 = 1 em, big_op_spacing5 = 0.1 em.
//
// Not thread-safe.

#pragma once

#include "fontmetrics.h"
#include "opentype.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ImTypeset::detail
{

// Plane 15, the Supplementary Private Use Area, assigned downward from the top.
constexpr uint32_t SYNTHETIC_FIRST = 0xFFFFD;
constexpr uint32_t SYNTHETIC_LAST = 0xF0000;

struct SyntheticCodepoint {
    uint32_t codepoint = 0;
    uint32_t glyph = 0;
};

// One font file, parsed once and shared by every size of every role it backs.
class FaceData
{
public:
    // The file's name without its directory, for messages.
    const std::string& name() const { return _name; }
    // The file itself, for whoever hands it to a rasteriser.
    const uint8_t* bytes() const { return _bytes.data(); }
    size_t byte_count() const { return _bytes.size(); }

    const OpenTypeFont& font() const { return _font; }

    int units_per_em() const { return _font.units_per_em(); }
    // hhea's, and not OS/2's: it is what ImGui's rasterisers scale by, and a
    // face whose ascent differed between layout and the atlas would put every
    // baseline in the wrong place.
    int ascent() const { return _font.hhea_ascender(); }
    int descent() const { return -_font.hhea_descender(); } // positive down
    // OS/2.sxHeight, or round(0.45 * unitsPerEm) when the font does not say.
    int x_height() const { return _x_height; }

    // True, with `*glyph` set, if the font maps the codepoint or it is one of
    // the synthetic codepoints minted for this font.
    bool lookup(uint32_t codepoint, uint32_t* glyph) const;
    bool has(uint32_t codepoint) const { return lookup(codepoint, nullptr); }
    // The glyph a codepoint is measured and drawn as: glyph 0, the font's
    // missing-glyph glyph, for one it does not map.
    uint32_t glyph(uint32_t codepoint) const
    {
        uint32_t found = 0;
        return lookup(codepoint, &found) ? found : 0;
    }

    // Every codepoint minted for this font, by descending codepoint: the order
    // they were assigned in. Empty for a font with no MATH table, and for one
    // whose vertical variant chains reach only encoded glyphs.
    const std::vector<SyntheticCodepoint>& synthetic_codepoints() const { return _minted; }

    // The codepoint a GlyphVariant carries for a glyph a vertical variant
    // chain reaches: the lowest codepoint the font maps to it, else the one
    // minted for it. Zero for a glyph no chain reaches.
    uint32_t variant_codepoint(uint32_t glyph) const;

    // The CFF name of the glyph a codepoint resolves to; empty for a TrueType
    // font and for a CID-keyed one. A view into this object.
    std::string_view glyph_name(uint32_t codepoint) const
    {
        return _font.glyph_name(glyph(codepoint));
    }

    // --- The atlas boundary (decision D15) -------------------------------------
    // Layout carries codepoints up to U+10FFFF, the minted ones in plane 15
    // among them. An ImGui built with a 16-bit ImWchar cannot name those, so
    // each supplementary codepoint this font maps (minted ones included) has a
    // stand-in: a BMP private-use codepoint, U+E000..U+F8FF, that the font's
    // own cmap leaves free. There are 6400 of those at most, fewer in a font
    // that uses the block itself. When they do not go round, the minted
    // codepoints are served first (nothing else can draw their glyphs), then
    // the font's own supplementary codepoints in ascending order, and
    // `warnings()` says how many went without. Among those served, stand-ins
    // ascend with the codepoint stood in for. `wchar_bits` is
    // 8 * sizeof(ImWchar).
    //
    // The codepoint to ask the atlas for. Identity when wchar_bits is 32. For
    // a 16-bit atlas: a BMP codepoint is itself, unless it is one of the
    // stand-ins, which the face does not map and which therefore answers 0; a
    // supplementary codepoint is its stand-in, or 0 if the face does not map
    // it or ran out of free private-use codepoints (6400 at most). 0 means
    // the atlas cannot be asked: the glyph is missing.
    uint32_t atlas_codepoint(uint32_t codepoint, int wchar_bits) const;
    // The glyph an atlas codepoint stands for. False if it stands for none.
    bool glyph_for_atlas_codepoint(uint32_t atlas_codepoint, int wchar_bits, uint32_t* glyph) const;
    // (stand-in, the supplementary codepoint it stands for), ascending in both.
    const std::vector<std::pair<uint32_t, uint32_t>>& atlas_stand_ins() const { return _stand_ins; }
    // How many supplementary codepoints the face maps and has no stand-in for.
    size_t atlas_stand_ins_unserved() const { return _stand_ins_unserved; }

    // What was tolerated while building this object, one line each. The
    // file's own are `font().warnings()`.
    const std::vector<std::string>& warnings() const { return _warnings; }

private:
    friend class FontLibrary;
    FaceData() = default;
    bool init(std::vector<uint8_t> bytes, std::string name, const FontLoadOptions& options,
              std::string* why);
    bool mint(std::string* why);
    void build_stand_ins();

    std::string _name;
    std::vector<uint8_t> _bytes;
    OpenTypeFont _font;
    int _x_height = 0;
    std::array<uint32_t, 256> _latin{};      // glyph + 1 for U+0000..U+00FF; 0 unmapped
    std::vector<SyntheticCodepoint> _minted; // descending codepoint
    std::vector<std::pair<uint32_t, uint32_t>> _variant_codepoints; // (glyph, codepoint), by glyph
    std::vector<std::pair<uint32_t, uint32_t>> _stand_ins;          // (stand-in, codepoint)
    std::vector<std::pair<uint32_t, uint32_t>> _stood_in; // (codepoint, stand-in), by codepoint
    size_t _stand_ins_unserved = 0;
    std::vector<std::string> _warnings;
};

// How many sizes of one role FontLibrary::metrics keeps live at the least;
// see there.
constexpr size_t METRICS_SIZES_KEPT = 256;

class FontLibrary
{
public:
    FontLibrary();
    ~FontLibrary();
    FontLibrary(const FontLibrary&) = delete;
    FontLibrary& operator=(const FontLibrary&) = delete;

    // Back a role with a font file, replacing whatever backed it. The file is
    // read and parsed now; on failure the registry is unchanged and `why`, if
    // given, says what is wrong with the file. `name` is used in messages.
    // `face_index` picks a face of a collection (.ttc, .otc); a file that is
    // not a collection has face 0 only, and an index the file does not have
    // is a failure whose reason says how many faces there are.
    bool register_font(Face face, const char* path, std::string* why = nullptr, int face_index = 0);
    // The same from memory. The bytes are copied.
    bool register_font(Face face, const void* data, size_t size, const char* name,
                       std::string* why = nullptr, int face_index = 0);
    // The same, with the face index before the reason.
    bool register_font(Face face, const void* data, size_t size, const char* name, int face_index,
                       std::string* why = nullptr)
    {
        return register_font(face, data, size, name, why, face_index);
    }

    // Whether a font whose GPOS has no pair kerning is kerned from a version 0
    // 'kern' table instead, where it has one. On unless turned off. The
    // reference never reads that table, so this is kerning the reference does
    // not have (docs/divergences.md); off gives its answers. It applies to
    // fonts registered after the call.
    void set_legacy_kern(bool enabled) { _legacy_kern = enabled; }
    bool legacy_kern() const { return _legacy_kern; }
    // Withdraw a role's file, restoring its stand-in if it has one. A role
    // with nothing registered is left alone.
    void unregister_font(Face face);
    bool is_registered(Face face) const;

    // The role whose file this role is read from: itself, unless it has no
    // file of its own and stands in for another (CODE_BOLD for PROSE_BOLD).
    Face backing_face(Face face) const;

    // The parsed file behind a role, or null, with a reason, if nothing backs it.
    const FaceData* face_data(Face face, std::string* why = nullptr) const;

    // A role at one size. Null, with a reason, if nothing backs the role, or
    // if the role is MATH and its file has no MATH table. For Face::MATH the
    // result's as_math() is not null.
    //
    // THE OBJECTS ARE CACHED, AND HOW MANY ARE LIVE IS BOUNDED. Each role
    // keeps the sizes most recently asked for: at least METRICS_SIZES_KEPT of
    // them and at most twice that. A size that has gone that long without
    // being asked for EXPIRES. What that means for a pointer returned here:
    //   - Identical arguments return the identical object while it is live,
    //     and an equal one after it has expired.
    //   - An expired object is not freed by this function, or by anything
    //     but release_expired_metrics() and the destructor. A pointer is
    //     therefore valid until the owner of the library calls that, however
    //     long it has been held and whatever has been asked for since.
    //   - release_expired_metrics() frees every expired object. The caller
    //     must hold no FontMetrics pointer across the call that it has not
    //     asked for again since `metrics_expired()` was last zero; the simple
    //     way to keep that is to drop every pointer held (clear the caches
    //     that keep them) and then call it. A context does so between builds.
    //     Never calling it is safe and unbounded: a host animating a zoom
    //     then grows the library by a few objects a frame.
    //   - A registry change expires nothing and frees nothing: what it
    //     replaces is retired and lives as long as the library, as described
    //     at the top of this file.
    // With the release called, a role holds at most 2 * METRICS_SIZES_KEPT
    // objects between calls plus what expired since the last one: a few
    // hundred bytes each, plus for Face::MATH the font's vertical variant
    // chains at that size.
    const FontMetrics* metrics(Face face, double size, std::string* why = nullptr);
    const MathFontMetrics* math_metrics(double size, std::string* why = nullptr);
    // How many objects have expired and not been released.
    size_t metrics_expired() const { return _expired_metrics.size(); }
    void release_expired_metrics();
    // How many live metrics objects a role holds now.
    size_t metrics_cached(Face face) const;

    // The em size at which a role comes out `pixel_height` tall as ImGui means
    // it (hhea ascender minus descender): pixel_height * unitsPerEm / (ascent
    // + descent), not rounded. `pixel_height` unchanged when the face states
    // no vertical extent, or when nothing backs the role.
    double em_size_for_pixel_height(Face face, double pixel_height) const;

    // Changes on every successful register_font and effective unregister_font.
    uint32_t generation() const { return _generation; }

private:
    class Metrics;

    void registry_changed();

    std::array<std::unique_ptr<FaceData>, FACE_COUNT> _registered;
    struct MetricsCache {
        std::unordered_map<uint64_t, std::unique_ptr<Metrics>> recent, previous;
    };
    std::array<MetricsCache, FACE_COUNT> _metrics;
    // Aged out of the cache; freed by release_expired_metrics().
    std::vector<std::unique_ptr<Metrics>> _expired_metrics;
    // What a registry change replaced, kept so pointers handed out stay valid.
    std::vector<std::unique_ptr<FaceData>> _retired_data;
    std::vector<std::unique_ptr<Metrics>> _retired_metrics;
    uint32_t _generation = 0;
    bool _legacy_kern = true;
};

} // namespace ImTypeset::detail
