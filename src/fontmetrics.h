// What layout is allowed to know about a font: the seam between typesetting and
// typography, as the reference's advtext/fontmetrics.py is.
//
// Everything above this header — pack, linebreak, the math list — asks its
// questions of a FontMetrics and never learns that a file or an OpenType table
// exists. src/fontlib answers them from a real font; a test answers them from
// a hand-written fake with round numbers, which is why this is an interface and
// not a concrete class. The virtual call needs no RTTI.
//
// UNITS. A FontMetrics is one face at one size, and every length it returns is
// a double in whatever unit that size was given in: build it at 16.0 and
// advances come back in the same 16-per-em unit; build it at 16 * 65536 and
// they come back in scaled points. Nothing is rounded here — a metric is a
// ratio of the em, and rounding is the caller's one decision (char_measure in
// src/style rounds once, half to even, on the way into a node).
//
// Not in scope: shaping, ligatures, GSUB, bidi, vertical writing, variable-font
// axes, colour glyphs, hinting, and anything that names a file.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ImTypeset::detail
{

// The typographic roles, named by role and not by family. CODE_BOLD is the one
// role that may have no file of its own: until a host registers one it is read
// from PROSE_BOLD's (FontLibrary::backing_face).
enum class Face : uint8_t {
    PROSE = 0,
    PROSE_BOLD,
    PROSE_ITALIC,
    PROSE_BOLD_ITALIC,
    CODE,
    CODE_BOLD,
    MATH,
};

constexpr int FACE_COUNT = 7;

// The reference's `Face.value`: "prose", "prose-bold", "prose-italic",
// "prose-bold-italic", "code", "code-bold", "math". It is the face part of a
// font key ("prose-bold@789381").
inline const char* face_value(Face face)
{
    static const char* const values[FACE_COUNT] = {
        "prose", "prose-bold", "prose-italic", "prose-bold-italic", "code", "code-bold", "math",
    };
    const int index = static_cast<int>(face);
    return index >= 0 && index < FACE_COUNT ? values[index] : "";
}

// The inverse. False for anything that is not one of the seven.
inline bool face_from_value(std::string_view value, Face* out)
{
    for (int i = 0; i < FACE_COUNT; ++i) {
        if (value == face_value(static_cast<Face>(i))) {
            if (out != nullptr)
                *out = static_cast<Face>(i);
            return true;
        }
    }
    return false;
}

// The ink extent of one glyph, measured from its origin on the baseline, y up.
struct GlyphBBox {
    double x_min = 0.0;
    double y_min = 0.0;
    double x_max = 0.0;
    double y_max = 0.0;

    // TeX's height: how far the ink reaches above the baseline. Negative for a
    // glyph entirely below it; not clamped.
    double height() const { return y_max; }
    // TeX's depth: how far the ink reaches below the baseline, positive down.
    double depth() const { return -y_min; }
    // Ink width, which is not the advance.
    double width() const { return x_max - x_min; }
};

// One link in a chain of ascending-size variants of a glyph. `extent` is the
// variant's size along the axis the chain grows on (for the vertical chain,
// height plus depth). `codepoint` may be synthetic: a private-use codepoint
// src/fontlib minted for a glyph the font does not encode. It means something
// only to the face that issued it and must never be written out as text.
// `glyph` is the font's glyph id, which the font loader maps a synthetic
// codepoint to.
struct GlyphVariant {
    uint32_t codepoint = 0;
    uint32_t glyph = 0;
    double extent = 0.0;
};

struct GlyphVariants {
    const GlyphVariant* data = nullptr;
    size_t size = 0;

    const GlyphVariant* begin() const { return data; }
    const GlyphVariant* end() const { return data + size; }
    bool empty() const { return size == 0; }
    const GlyphVariant& operator[](size_t index) const { return data[index]; }
};

// TeX's math font parameters, under TeX's names, at one size: the family-2
// sigma and family-3 xi \fontdimens (tex.web sections 700, 701). src/fontlib.h
// says which OpenType MATH constant each comes from and which are synthesised.
struct MathParams {
    double math_x_height = 0; // sigma 5
    double math_quad = 0;     // sigma 6: one em; cur_mu is a quad over 18
    double num1 = 0;          // sigma 8-10: numerator shift up: display, with rule, \atop
    double num2 = 0;
    double num3 = 0;
    double denom1 = 0; // sigma 11, 12: denominator shift down: display, non-display
    double denom2 = 0;
    double sup1 = 0; // sigma 13-15: superscript shift up: display, non-display, cramped
    double sup2 = 0;
    double sup3 = 0;
    double sub1 = 0; // sigma 16, 17: subscript shift down: alone, with a superscript
    double sub2 = 0;
    double sup_drop = 0; // sigma 18, 19: script baselines from the nucleus box's edges
    double sub_drop = 0;
    double delim1 = 0; // sigma 20, 21: \atopwithdelims delimiter size
    double delim2 = 0;
    double axis_height = 0;            // sigma 22
    double default_rule_thickness = 0; // xi 8
    double big_op_spacing1 = 0;        // xi 9-13 in section 751's roles
    double big_op_spacing2 = 0;
    double big_op_spacing3 = 0;
    double big_op_spacing4 = 0;
    double big_op_spacing5 = 0;
    // No TeX parameter: TeX derives all three; OpenType states them.
    double radical_rule_thickness = 0;
    double radical_vertical_gap = 0;
    double radical_display_style_vertical_gap = 0;
    // Dimensionless ratios of the script sizes to the text size.
    double script_scale = 0;
    double script_script_scale = 0;
    // OpenType's rule for a display operator: the first variant at least this tall.
    double display_operator_min_height = 0;
};

class MathFontMetrics;

// One face at one size: everything horizontal layout needs to place a glyph.
class FontMetrics
{
public:
    virtual ~FontMetrics() = default;

    virtual Face face() const = 0;
    // The em size every other length here is expressed against.
    virtual double size() const = 0;
    // Positive up from the baseline.
    virtual double ascent() const = 0;
    // Positive down from the baseline, like TeX's depth.
    virtual double descent() const = 0;
    virtual double x_height() const = 0;
    // One em, which is the size.
    virtual double em() const = 0;
    // The advance of U+0020: the natural width of an interword glue.
    virtual double space_width() const = 0;

    // Whether the face maps this codepoint, a synthetic one included.
    virtual bool has(uint32_t codepoint) const = 0;
    // How far the pen moves after this codepoint, kerning excluded. A
    // codepoint the face does not map is measured as its missing-glyph glyph.
    virtual double advance(uint32_t codepoint) const = 0;
    virtual GlyphBBox bbox(uint32_t codepoint) const = 0;
    // The pair kern between two adjacent codepoints, usually negative or zero.
    virtual double kern(uint32_t codepoint, uint32_t following) const = 0;

    // This face as a math face, or null if it carries no MATH table. Stands in
    // for the reference's separate protocol, since there is no RTTI to ask.
    virtual const MathFontMetrics* as_math() const { return nullptr; }
};

// A face that also carries an OpenType MATH table.
class MathFontMetrics : public FontMetrics
{
public:
    virtual const MathParams& math() const = 0;
    // TeX's char_italic: the lean to add after a glyph. Zero if it has none.
    virtual double italic_correction(uint32_t codepoint) const = 0;
    // The ascending-size chain TeX reaches through list_tag; empty if there is
    // none. The first entry is the base glyph itself. Valid for the life of
    // this object.
    virtual GlyphVariants vertical_variants(uint32_t codepoint) const = 0;

    const MathFontMetrics* as_math() const override { return this; }
};

} // namespace ImTypeset::detail
