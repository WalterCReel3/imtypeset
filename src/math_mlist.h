// mlist_to_hlist: a math list becomes horizontal material hpack can measure.
// Mirrors advtext/math/mlist.py, which is tex.web sections 699 to 767 (TeX
// Appendix G) read over an OpenType MATH table.
//
// WHAT OF tex.web THIS IS. Parts 35, "Subroutines for math mode" (sections
// 699 to 718), and 36, "Typesetting math formulas" (sections 719 to 767):
//
//   sections 700, 701  the math-symbol and              MathParams, through
//                      math-extension font parameters   MathFonts::params
//   section 703        cur_size, cur_mu                 size_for, MathFonts::mu
//   sections 704, 705  fraction_rule, overbar           overbar
//   sections 706-709   var_delimiter, char_box          delimiter_box (the
//                                                       search only)
//   section 715        rebox                            rebox
//   section 716        math_glue                        from_mu
//   sections 720, 721  clean_box                        clean_box, clean_pack
//   sections 725-733   mlist_to_hlist, first pass       mlist_to_hlist
//   sections 734-736   make_over, make_under,           the same names
//                      make_vcenter
//   section 737        make_radical                     make_radical
//   sections 743-747   make_fraction                    make_fraction
//   sections 749-751   make_op                          make_op, stack_limits
//   sections 754, 755  the nucleus as an hlist          translate,
//                                                       translate_nucleus
//   sections 756-759   make_scripts                     make_scripts
//   sections 760-766   the second pass, make_left_right mlist_to_hlist,
//                      and the spacing table            size_boundaries,
//                                                       make_left_right,
//                                                       inter_atom_glue
//
// Two passes, for the reason section 725 gives: the first translates each atom
// into boxes and learns how tall the tallest came out; only then does the
// second know how much air to put between them, and whether each BIN was
// really binary.
//
// WHAT IS THE FONT'S. Every length is a MathParams value (src/fontmetrics.h),
// rounded half to even as it is read. The script sizes are the font's own
// percentages. A big operator in display style takes the next glyph of the
// font's vertical variant chain, exactly one step (section 749), and is centred
// on the math axis.
//
// WHERE THIS IS NOT tex.web, all of it the reference's (docs/divergences.md
// classifies each, and says which the reference's own documents state):
//
//   - Delimiters are chosen, never built. var_delimiter (sections 706 to 708)
//     walks a character's chain of ever-larger variants and, failing that,
//     assembles one from pieces (sections 710 to 714). Only the walk is here.
//     When the last variant is still too short tex.web returns it; here a
//     \left or \right delimiter does the same while it covers three quarters of
//     the content (LARGEST_VARIANT_MUST_COVER, which is the reference's number)
//     and is refused below that, and a radical is always refused.
//   - A surd is hung from the top of its ink. make_radical (section 737)
//     assumes a TFM surd whose height is the rule thickness; an OpenType surd
//     is an ordinary outline, so the rule thickness comes from the font's
//     RadicalRuleThickness and the glyph is shifted to put the top of its ink
//     where tex.web puts the top of the rule.
//   - \delimiterfactor (901), \delimitershortfall (5pt), \nulldelimiterspace
//     (0.12 of a quad) and \scriptspace (0.5pt) are plain.tex's values, which
//     tex.web leaves to the format (sections 236 and 247).
//   - A fraction gets no delimiters (section 748 is left out); with tex.web's
//     own null delimiter that step adds two boxes of \nulldelimiterspace.
//   - No accents (section 738), no \mathchoice (section 731), no \nonscript,
//     no style nodes in a list, and no penalties after BIN and REL atoms
//     (section 767's bin_op_penalty and rel_penalty).
//   - Prose in a formula (MATH_TEXT) is set here, at the size of the style it
//     lands in; tex.web has an hbox made elsewhere.
//   - The italic correction of a math character is always followed by a kern,
//     and still handed to make_scripts. Section 755 adds the kern only when
//     there is no subscript: with one, a subscript here sits the correction
//     too far right, and a superscript above it twice the correction where
//     section 759 puts it once. TexRules::italic_kern_only_without_subscript
//     is section 755's test.
//   - make_under leaves out the rule thickness that section 735 adds below
//     the rule, so an underlined box is that much shallower.
//     TexRules::underline_clearance adds it.
//   - A fraction spaces as an Inner atom. Section 761 of the tex.web this was
//     checked against leaves it spacing as Ord.
//   - No make_ord (sections 752 and 753): no kern or ligature between
//     adjacent math characters from the font's own table.
//   - A nucleus that is a list is spliced into the outer list. Section 754
//     packs it into one box first. clean_box always packs what it converted;
//     section 720 uses a result that is already one unshifted box as it is.
//     TexRules::sublist_nucleus_packed does both as the sections do.
//   - The math unit is the quad over eighteen rounded half to even, and every
//     font parameter is rounded so; section 703 truncates (x_over_n). A space
//     is a whole number of math units, which section 716 does not require.
//     TexRules::mu_truncates divides the rounded quad as the section does.
//
// The four switches are MathFonts::rules (src/tex_rules.h); what is described
// above is the reference's behaviour, which is the default.
//   - make_vcenter takes any nucleus; section 736 insists on a vbox.
//   - A division of a quantity that could be negative floors, where Pascal's
//     div truncates: section 762's division by 500 is the one place.
//
// This port's own (decision D33): statuses for exceptions, MAX_MATH_NESTING,
// MAX_MATH_ITEMS, and math_glue's products held to MAX_DIMEN.
//
// ERRORS. Where the reference raises, the conversion returns a MathStatus and
// appends nothing. A math list reaches here from the formula parser and,
// through the public API, from a host that built it by hand, so everything in
// it is data: a span outside the list, a tag that is not one, a list nested
// past MAX_MATH_NESTING or sharing sublists into more than MAX_MATH_ITEMS, and
// a field where it cannot be are all statuses.

#pragma once

#include "fontmetrics.h"
#include "math_atoms.h"
#include "nodes.h"
#include "pack.h"
#include "style.h"
#include "tex_rules.h"
#include "units.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ImTypeset::detail
{

// tex.web section 764's math_spacing, indexed 8 * row + column by the spacing
// class of the left and right atom (section 765's magic offset comes to that).
// The sixty-four characters are that section's string exactly (tex.web line
// 15064), the one datum in this library copied from it verbatim, because a
// table is either that string or wrong; THIRD-PARTY.md says so.
//
//     0  no space
//     1  conditional thin (\nonscript\mskip\thinmuskip)
//     2  thin
//     3  conditional medium
//     4  conditional thick
//     *  impossible: section 728's BIN conversion makes the pair unreachable
//
// A conditional space is dropped in script and scriptscript styles.
constexpr char MATH_SPACING[] = "02340001"
                                "22*40001"
                                "33**3**3"
                                "44*04004"
                                "00*00000"
                                "02340001"
                                "11*11111"
                                "12341011";

// \scriptspace, plain.tex's 0.5pt: added to the width of every script box
// (sections 757 to 759). A parameter in tex.web (script_space, section 247).
constexpr Scaled SCRIPT_SPACE = UNITY / 2;

// U+221A SQUARE ROOT, the one delimiter a radical is built from. tex.web's
// radical noad carries its delimiter in a field (section 683); every radical
// anybody writes passes the same one.
constexpr uint32_t SURD = 0x221A;

// plain.tex's \delimiterfactor: a delimiter must cover this many thousandths of
// twice the content's greater reach from the axis (section 762).
constexpr Scaled DELIMITER_FACTOR = 901;

// plain.tex's \delimitershortfall, 5pt, absolute as tex.web has it (section
// 762): however tall the content, a delimiter may fall short of it by at most
// this. The default of MathFonts::delimiter_shortfall.
constexpr Scaled DELIMITER_SHORTFALL = 5 * UNITY;

// How much of the content the largest variant of a \left or \right delimiter
// must cover to be used when no variant satisfies tex.web's rule: three
// quarters. The reference's number, not tex.web's.
constexpr Scaled LARGEST_VARIANT_MUST_COVER_NUMERATOR = 3;
constexpr Scaled LARGEST_VARIANT_MUST_COVER_DENOMINATOR = 4;

// plain.tex's \nulldelimiterspace of 1.2pt at ten points, kept as that share of
// the math quad: the width of \left. and \right. .
constexpr double NULL_DELIMITER_SPACE_QUADS = 0.12;

// How deep sublists may nest. The reference has no bound but its interpreter's
// recursion limit, which at the default of 1000 stops a tower of superscripts
// at 197 levels; its formula parser stops at 24. Each level costs several stack
// frames here, and a host may be on a thread with a small stack, so the bound
// is set by that and not by what a formula needs. It also keeps the boxes, at
// most three levels of them per level of list, inside NodeArena::MAX_NESTING.
constexpr int MAX_MATH_NESTING = 64;

// How many items one conversion will translate, counting an item once for
// every path to it. A list built by the formula parser is a tree and has as
// many as it has items; a list built by hand can name one sublist from many
// fields, and a few dozen levels of that is more work than there is time for.
// The reference has no bound and would not return.
constexpr uint32_t MAX_MATH_ITEMS = 1u << 20;

// tex.web section 100's half: x div 2 for an even x and (x + 1) div 2 for an
// odd one, so a half rounds up. Both dividends are even, so flooring, as the
// reference's division does, and truncating, as Pascal's does, agree.
inline Scaled half(Scaled value)
{
    return floor_mod(value, 2) != 0 ? floor_div(value + 1, 2) : floor_div(value, 2);
}

// tex.web's math_glue (section 716) under the reference's name: a spec in
// math units scaled by cur_mu. The amounts here are whole math units, so the
// products are exact and the section's fraction arithmetic (mu_mult, which
// truncates) has nothing to do; and there are no infinite orders to pass
// through unscaled. Each product is brought inside the dimension invariant of
// src/units.h, which the reference, whose integers are unbounded, does not
// need to do.
inline GlueSpec from_mu(const MuGlue& mu_glue, Scaled mu)
{
    GlueSpec spec;
    spec.width = clamp_dimen(clamp_dimen(mu_glue.width) * mu);
    spec.stretch = clamp_dimen(clamp_dimen(mu_glue.stretch) * mu);
    spec.shrink = clamp_dimen(clamp_dimen(mu_glue.shrink) * mu);
    return spec;
}

// --- Errors ---------------------------------------------------------------------------

// Why a list was not set. The first three are the reference's MathLayoutError:
// a well-formed formula beyond what the fonts in hand can do, which a caller
// with somewhere else to put the source turns into a refusal. The rest are
// about the list itself.
enum class MathError : uint8_t {
    NONE = 0,
    // No variant of a delimiter is tall enough for what it has to cover.
    // `codepoint` is the delimiter as the formula named it; `needed` and
    // `largest` are height plus depth in scaled points.
    DELIMITER_TOO_SMALL,
    // Prose in a formula holds a character its face has no glyph for:
    // `codepoint`, in `face`. A formula is one face's work at one size and is
    // not patched from another, as a paragraph's characters are.
    MISSING_GLYPH,
    // The formula holds prose, or a box holding characters of another font,
    // and the fonts cannot measure it: there is no text resolver, or it does
    // not know the face.
    TEXT_FACE_UNAVAILABLE,
    // An ACCENT atom. The reference raises NotImplementedError, which is not a
    // MathLayoutError.
    NOT_IMPLEMENTED,
    // A field where it cannot be, where the reference raises TypeError: a
    // MU_GLUE anywhere but the nucleus of a SPACE, a SPACE without one, a
    // boundary whose nucleus is neither a character nor empty. Also, with no
    // counterpart in the reference: a span or a box index that names nothing,
    // a tag that is not a member of its enum, and a box list hpack refuses.
    MALFORMED,
    // Sublists nested past MAX_MATH_NESTING, or boxes past the arena's bound.
    TOO_DEEP,
    // More than MAX_MATH_ITEMS items to translate (`largest` is then 0). Or a
    // box, a kern or a rule with a dimension outside +-MAX_DIMEN, which no
    // node may hold (src/units.h): `needed` is the dimension, `largest` is
    // MAX_DIMEN, and `inside` says it was a kern or a rule in a box that is
    // itself an ordinary size.
    TOO_LARGE,
};

struct MathStatus {
    MathError error = MathError::NONE;
    uint32_t codepoint = 0;
    Scaled needed = 0;
    Scaled largest = 0;
    Face face = Face::PROSE;
    // MISSING_GLYPH, and TEXT_FACE_UNAVAILABLE when prose caused it: the bytes
    // of the prose, in the MathList that was converted.
    MathSpan text;
    // TOO_LARGE: the dimension is a kern's or a rule's and not a box's.
    bool inside = false;

    bool ok() const { return error == MathError::NONE; }

    // Whether this is one of the reference's MathLayoutErrors.
    bool is_layout_refusal() const
    {
        return error == MathError::DELIMITER_TOO_SMALL || error == MathError::MISSING_GLYPH ||
               error == MathError::TEXT_FACE_UNAVAILABLE;
    }
};

// The first of a box's width, height, depth and shift that is outside
// +-MAX_DIMEN, or 0 if all four are inside.
Scaled dimension_outside_max_dimen(const Box& box);

// What the reference's exception says, for DELIMITER_TOO_SMALL and
// MISSING_GLYPH character for character; a short description for the others,
// whose reference messages hold the Python spelling of an object.
std::string math_error_message(const MathStatus& status);

// Python's repr() of a string, given as codepoints: between single quotes,
// or double quotes if it holds a single quote and no double one; a printable
// character (`str.isprintable()`) as itself, the others as \t, \n, \r by name
// or \xNN, \uNNNN, \UNNNNNNNN in lower-case hex; a backslash doubled, and the
// quote in use escaped. The reference's messages embed it. UTF-8 out.
std::string python_repr(const uint32_t* codepoints, size_t count);

// The same for a one-character string.
std::string python_char_repr(uint32_t codepoint);

// --- Fonts ----------------------------------------------------------------------------

// The math face at a size in scaled points. Null means there is none. The
// object must stay valid for as long as the MathFonts built from it is used.
struct MathResolver {
    using Function = const MathFontMetrics* (*)(void* user, Scaled size);

    Function function = nullptr;
    void* user = nullptr;

    explicit operator bool() const { return function != nullptr; }
    const MathFontMetrics* operator()(Scaled size) const
    {
        return function != nullptr ? function(user, size) : nullptr;
    }
};

class FontLibrary;

// The production resolver: FontLibrary::math_metrics at the size, as the em
// size, so every length is in scaled points. The library must outlive it.
MathResolver fontlib_math_metrics(FontLibrary& library);

// The math face at the three sizes a formula needs, and how to measure it.
// Three faces and not one scaled three ways because that is what tex.web has
// -- a font for each family at each of section 699's three sizes, reached as
// fam_fnt(family + size) (sections 700 and 722) -- and what a math font is
// drawn for. One family here, where tex.web has sixteen.
//
// `measure()` hands out a pointer to this object, so it is neither copied nor
// moved.
class MathFonts
{
public:
    MathFonts();
    MathFonts(const MathFonts&) = delete;
    MathFonts& operator=(const MathFonts&) = delete;

    // The reference's math_fonts(): the three sizes from one, by the font's own
    // script percentages — round(base_size * script_scale), half to even, with
    // the scales read from the text-size face — and the key `math@size` for
    // each.
    //
    // `text` resolves every font key that is not one of the three: the prose
    // faces a MATH_TEXT is set in. Without it a formula holding prose is
    // refused with TEXT_FACE_UNAVAILABLE, and one holding none is unaffected.
    //
    // False, with a reason, if `base_size` is not within (0, MAX_DIMEN], the
    // resolver has no face at one of the sizes, or the font's script
    // percentages do not give a size in that range. The object is then unusable.
    bool init(Scaled base_size, MathResolver resolve, MetricsResolver text = MetricsResolver(),
              std::string* why = nullptr);

    bool ready() const { return _ready; }

    Scaled size(MathSize size) const { return _sizes[math_size_index(size)]; }
    const MathFontMetrics& face(MathSize size) const { return *_faces[math_size_index(size)]; }
    FontKey key(MathSize size) const { return _keys[math_size_index(size)]; }

    // The face a style is set from (section 703), its parameters, and what a
    // CHAR node set at that style carries.
    const MathFontMetrics& face(Style style) const { return face(size_for(style)); }
    const MathParams& params(Style style) const { return face(style).math(); }
    FontKey key(Style style) const { return key(size_for(style)); }

    // Section 703's cur_mu: one math unit is an eighteenth of the math quad.
    // Rounded half to even, as the reference rounds it; the section truncates,
    // and so does this under TexRules::mu_truncates.
    Scaled mu(Style style) const;

    // How a prose face is resolved, or empty if it cannot be.
    MetricsResolver text() const { return _text; }

    // What hpack and ship-out take for anything this object's fonts set: the
    // three math keys answer from the math faces, every other key from `text`.
    // Rounded as char_measure rounds, and memoised. A key nothing resolves
    // measures as nothing and is noted (`unmeasured`), where the reference
    // raises TextFaceUnavailableError out of hpack.
    CharMeasure measure();
    CharDimens measure(FontKey font, uint32_t codepoint);

    // Whether `measure` has been asked about a key nothing resolves since the
    // last `clear_unmeasured`. mlist_to_hlist clears it on entry and refuses
    // the list if it is set; a caller packing or shipping the result with
    // `measure()` can ask the same.
    bool unmeasured() const { return _unmeasured; }
    void clear_unmeasured() { _unmeasured = false; }

    // \delimitershortfall, in scaled points. A member and not only a constant
    // because it is a parameter in tex.web (section 247) and the reference's
    // tests set it.
    Scaled delimiter_shortfall = DELIMITER_SHORTFALL;

    // Which of the reference's departures from tex.web a conversion with
    // these fonts keeps: all of them unless said otherwise (src/tex_rules.h).
    TexRules rules;

private:
    static const FontMetrics* metrics_for(void* user, FontKey key);
    static CharDimens measure_thunk(void* user, FontKey font, uint32_t codepoint);
    const FontMetrics* resolve_key(FontKey key);

    bool _ready = false;
    bool _unmeasured = false;
    Scaled _sizes[MATH_SIZE_COUNT] = {0, 0, 0};
    const MathFontMetrics* _faces[MATH_SIZE_COUNT] = {nullptr, nullptr, nullptr};
    FontKey _keys[MATH_SIZE_COUNT];
    MetricsResolver _text;
    // The last key that was not a math key, and what it resolved to.
    uint64_t _last_text_key = ~uint64_t(0);
    const FontMetrics* _last_text_metrics = nullptr;
    CharMeasureCache _cache;
};

// --- The conversion ------------------------------------------------------------------

// tex.web's mlist_to_hlist (section 726): converts a math list into
// horizontal material at one style and appends it to `out`. The result is
// unpacked: a caller hpacks it with `fonts.measure()`. `style` is its
// cur_style; its other inputs, cur_mlist and mlist_penalties, are `items` and
// nothing (no penalties are made).
//
// `items` is the list to convert; the sublists and prose its fields name are
// `list`'s. Boxes are made in `arena`, which is also where every SUB_BOX of the
// list must already be.
//
// On any error nothing is appended to `out`, `arena` is rewound to how it was
// on entry, and the status says why.
MathStatus mlist_to_hlist(const MathList& list, MathView items, Style style, MathFonts& fonts,
                          NodeArena& arena, std::vector<Node>& out);

// The same for a span of `list`.
MathStatus mlist_to_hlist(const MathList& list, MathSpan items, Style style, MathFonts& fonts,
                          NodeArena& arena, std::vector<Node>& out);

} // namespace ImTypeset::detail
