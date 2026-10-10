// The vocabulary of a math list, and the style algebra that governs it.
// Mirrors advtext/math/atoms.py, which is tex.web sections 681 to 688 and 702.
//
// WHAT OF tex.web THIS IS. From part 34, "Data structures for math mode"
// (sections 680 to 698), and the start of part 35, "Subroutines for math mode":
//
//   section 681        the noad and its three fields;   MathItem, Field,
//                      math_type: empty, math_char,     FieldType
//                      sub_box, sub_mlist
//   section 682        ord_noad .. inner_noad; the      AtomType, Limits
//                      op_noad subtypes limits and
//                      no_limits
//   section 683        radical_noad, fraction_noad,     AtomType::RADICAL,
//                      default_code                     MathItem::is_fraction,
//                                                       has_thickness
//   section 687        under_noad, over_noad,           the AtomTypes of those
//                      accent_noad, vcenter_noad,       names; LEFT, RIGHT
//                      left_noad, right_noad
//   section 688        the eight styles, cramped        Style
//   section 699        text_size, script_size,          MathSize
//                      script_script_size
//   section 702        cramped_style, sub_style,        the functions of those
//                      sup_style, num_style,            names
//                      denom_style
//   section 703        cur_size from cur_style          size_for
//   section 761        the type a noad spaces as        spacing_class
//
// tex.web says "noad" where this says atom or item, "nucleus", "supscr" and
// "subscr" for an atom's three fields (`sup` and `sub` here, the reference's
// spelling), and keeps a noad's kind in its type code where this has `kind`.
// An atom's delimiter fields (left_delimiter, right_delimiter, section 683)
// and a character's family (fam, section 681) are not here: one math face
// serves every character, and the only delimiters are a boundary's, held in
// its nucleus, and the surd.
//
// A formula is a flat sequence of atoms: a nucleus with an optional superscript
// and subscript, classified by what it is and not by how it looks.
// src/math_mlist turns that sequence into the boxes, kerns and glue hpack can
// measure. Nothing here measures anything or touches a font.
//
// STYLES are tex.web's integers (section 688): display, text, script and
// scriptscript are 0, 2, 4, 6, and the cramped variant of each is one more. A
// smaller style has a larger value. Every transition of section 702 is integer
// arithmetic over that encoding, so the values are semantics and not labels.
//
// ATOM TYPE is not atom class. Sixteen types are here; the first eight index
// the inter-atom spacing table of section 764 and the rest space as one of
// those (section 761): `spacing_class` is that map. SPACE is the one type that
// is not a noad: in tex.web an explicit math space is a glue node among the
// noads, which both passes step over (sections 730 and 761). It is an atom
// type here because it is one in the reference, and it has three fields that
// mean nothing and no class.
//
// A FIELD is a tagged value: section 681's empty, math_char, sub_box and
// sub_mlist, and two tex.web has no need of — MATH_TEXT, prose that cannot be
// packed until the style (and so the size) is known, and MU_GLUE, the amount a
// SPACE carries.
//
// REPRESENTATION. The reference has frozen classes and tuples of them. Here an
// item is one trivially copyable value and a list is a contiguous run of them
// in a `MathList`, which also holds the bytes of every MATH_TEXT. A field that
// is a sublist or prose names a span of that storage by 32-bit index, never by
// pointer. A list is built bottom-up, as a NodeArena's boxes are: build the
// inner list in a scratch vector, `add_items` it, and put the Field that names
// it into an item of the outer list.
//
// A SUB_BOX names a BOX node by its index in the NodeArena the list will be
// converted into; the MathList does not own the box.
//
// Everything in a Field or a MathItem may have been written by a host through
// the public API. The conversion checks every span against the list, every tag
// against its enum and the nesting against a bound before using it.
//
// Not here, as in the reference: \mathchoice (the choice_node of section
// 689), style nodes inside a list (section 688's style_node: a list is
// converted at one style), \halign, delimiters around a fraction (section
// 748), a radical's degree, and anything extensible. ACCENT is declared,
// because the spacing map needs it, and the conversion refuses it (section
// 738's make_math_accent walks a width-fitting chain nothing here implements).

#pragma once

#include "fontmetrics.h"
#include "nodes.h"
#include "units.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ImTypeset::detail
{

// --- Styles (tex.web sections 688, 699, 702, 703) -------------------------------

// The eight math styles, tex.web's display_style, text_style, script_style and
// script_script_style and each plus `cramped` (section 688): even is
// uncramped, odd is cramped; larger is smaller.
enum class Style : uint8_t {
    DISPLAY = 0,
    DISPLAY_CRAMPED = 1,
    TEXT = 2,
    TEXT_CRAMPED = 3,
    SCRIPT = 4,
    SCRIPT_CRAMPED = 5,
    SCRIPT_SCRIPT = 6,
    SCRIPT_SCRIPT_CRAMPED = 7,
};

constexpr int STYLE_COUNT = 8;

// The three font sizes a formula is set from: tex.web's text_size, script_size
// and script_script_size (section 699), with its size codes. `math_size_index`
// is the position, 0 to 2, for indexing a table.
enum class MathSize : uint8_t {
    TEXT = 0,
    SCRIPT = 16,
    SCRIPT_SCRIPT = 32,
};

constexpr int MATH_SIZE_COUNT = 3;

constexpr int math_size_index(MathSize size)
{
    return static_cast<int>(size) / 16;
}

// Section 702: cramped_style(#) == 2*(# div 2)+cramped. Cramp without resizing.
constexpr Style cramped_style(Style style)
{
    return static_cast<Style>(2 * (static_cast<int>(style) / 2) + 1);
}

// Section 702: sup_style(#) == 2*(# div 4)+script_style+(# mod 2). Smaller,
// cramp kept.
constexpr Style sup_style(Style style)
{
    const int s = static_cast<int>(style);
    return static_cast<Style>(2 * (s / 4) + static_cast<int>(Style::SCRIPT) + (s % 2));
}

// Section 702: sub_style(#) == 2*(# div 4)+script_style+cramped. A subscript is
// always cramped and a superscript is not.
constexpr Style sub_style(Style style)
{
    return static_cast<Style>(2 * (static_cast<int>(style) / 4) + static_cast<int>(Style::SCRIPT) +
                              1);
}

// Section 702: num_style(#) == #+2-2*(# div 6). Smaller unless already
// scriptscript.
constexpr Style num_style(Style style)
{
    const int s = static_cast<int>(style);
    return static_cast<Style>(s + 2 - 2 * (s / 6));
}

// Section 702: denom_style(#) == 2*(# div 2)+cramped+2-2*(# div 6). Smaller and
// cramped.
constexpr Style denom_style(Style style)
{
    const int s = static_cast<int>(style);
    return static_cast<Style>(2 * (s / 2) + 1 + 2 - 2 * (s / 6));
}

// Section 703's cur_size as a function of cur_style: display and text are both
// set from the text face.
constexpr MathSize size_for(Style style)
{
    const int s = static_cast<int>(style);
    return s < static_cast<int>(Style::SCRIPT)
               ? MathSize::TEXT
               : static_cast<MathSize>(16 * ((s - static_cast<int>(Style::TEXT)) / 2));
}

// Section 749's cur_style<text_style: display or cramped display.
constexpr bool is_display(Style style)
{
    return static_cast<int>(style) < static_cast<int>(Style::TEXT);
}

// Whether a style has section 688's `cramped` added: something sits above, so
// scripts must not rise. tex.web asks it as odd(cur_style), in section 758.
constexpr bool is_cramped(Style style)
{
    return (static_cast<int>(style) % 2) != 0;
}

// --- Atoms (tex.web sections 681 to 687) ----------------------------------------

// What an atom is. The first eight are section 682's ord_noad..inner_noad, in
// its order. RADICAL is section 683's radical_noad; OVER, UNDER, VCENTER and
// ACCENT are section 687's noads of those names. LEFT and RIGHT are section
// 687's left_noad and right_noad: a delimiter to be sized to the list it
// stands in, held in the nucleus (tex.web holds it in the noad's delimiter
// field); an empty nucleus is the null delimiter. The values past INNER are
// not tex.web's type codes, and a fraction is not an AtomType at all
// (MathItem::is_fraction).
enum class AtomType : uint8_t {
    ORD = 0,
    OP,
    BIN,
    REL,
    OPEN,
    CLOSE,
    PUNCT,
    INNER,
    OVER,
    UNDER,
    VCENTER,
    RADICAL,
    ACCENT,
    LEFT,
    RIGHT,
    SPACE,
};

constexpr int ATOM_TYPE_COUNT = 16;

// The row or column an atom uses in section 764's table, 0 to 7: the t that
// section 761 sets for each kind of noad, less ord_noad. OVER, UNDER, VCENTER,
// RADICAL and ACCENT space as ORD, and the boundaries as OPEN and CLOSE, which
// is what make_left_right returns (section 762). -1 for SPACE, which is a node
// among the atoms and has no class, and for a value that is not an AtomType.
//
// A fraction is not asked here. The conversion spaces it as INNER, as the
// reference does; section 761 of the tex.web this was checked against (line
// 14996) leaves a fraction_noad's t at ord_noad. docs/divergences.md has it.
constexpr int spacing_class(AtomType type)
{
    switch (type) {
    case AtomType::ORD:
    case AtomType::OVER:
    case AtomType::UNDER:
    case AtomType::VCENTER:
    case AtomType::RADICAL:
    case AtomType::ACCENT:
        return 0;
    case AtomType::OP:
        return 1;
    case AtomType::BIN:
        return 2;
    case AtomType::REL:
        return 3;
    case AtomType::OPEN:
    case AtomType::LEFT:
        return 4;
    case AtomType::CLOSE:
    case AtomType::RIGHT:
        return 5;
    case AtomType::PUNCT:
        return 6;
    case AtomType::INNER:
        return 7;
    case AtomType::SPACE:
        break;
    }
    return -1;
}

// Where a big operator's scripts go: section 682's op_noad subtypes normal,
// limits and no_limits, with its values. DEFAULT becomes LIMITS in display
// style and is left alone otherwise (section 749).
enum class Limits : uint8_t { DEFAULT = 0, LIMITS, NO_LIMITS };

// A glue spec in math units, before it is scaled by cur_mu (section 716):
// eighteen to the quad of the current size. Whole math units and finite
// orders only; tex.web's mu_glue is an ordinary glue specification, in scaled
// math units with any order of stretch and shrink.
struct MuGlue {
    Scaled width = 0;
    Scaled stretch = 0;
    Scaled shrink = 0;

    friend constexpr bool operator==(const MuGlue& a, const MuGlue& b)
    {
        return a.width == b.width && a.stretch == b.stretch && a.shrink == b.shrink;
    }
};

// plain.tex's \thinmuskip, \medmuskip and \thickmuskip. Macro-package values,
// not in tex.web, which leaves the three parameters zero.
constexpr MuGlue THIN_MU = {3, 0, 0};
constexpr MuGlue MED_MU = {4, 2, 4};
constexpr MuGlue THICK_MU = {5, 5, 0};
// \! and \quad, \qquad as eighteen and thirty-six math units. The last two are
// not quite plain TeX's, which are an em of the font outside the formula and do
// not shrink in a script.
constexpr MuGlue NEGATIVE_THIN_MU = {-3, 0, 0};
constexpr MuGlue QUAD_MU = {18, 0, 0};
constexpr MuGlue QQUAD_MU = {36, 0, 0};

enum class FieldType : uint8_t {
    EMPTY = 0,
    // Section 681's math_char: one glyph of the math face. The codepoint may be
    // a synthetic one src/fontlib minted for a glyph the font does not encode.
    MATH_CHAR,
    // Section 681's sub_box: material already packed, used as it stands.
    SUB_BOX,
    // Section 681's sub_mlist: a nested formula, converted at the inherited
    // style.
    SUB_LIST,
    // Upright prose (\text{...}): UTF-8, set in a prose face at the size of the
    // style it lands in, with that face's kerning and one interword glue per
    // white-space character, leading and trailing ones included.
    MATH_TEXT,
    // Legal only as the nucleus of a SPACE atom.
    MU_GLUE,
};

constexpr int FIELD_TYPE_COUNT = 6;

// A run of a MathList's items, or of its text bytes.
struct MathSpan {
    uint32_t first = 0;
    uint32_t count = 0;
};

// One of an item's slots, in sixteen bytes. Which members mean something
// follows `type`:
//
//   MATH_CHAR   codepoint
//   SUB_BOX     box(): the index of a BOX node in the NodeArena converted into
//   SUB_LIST    span: items of the MathList
//   MATH_TEXT   span: bytes of the MathList's text; face
//   MU_GLUE     mu()
//
// The box index and the three numbers of a MuGlue share the storage of
// `codepoint` and `span`; read them through the accessors.
struct Field {
    FieldType type = FieldType::EMPTY;
    Face face = Face::PROSE;
    uint32_t codepoint = 0;
    MathSpan span;

    bool empty() const { return type == FieldType::EMPTY; }

    uint32_t box() const { return codepoint; }

    // The glue of a MU_GLUE field. Each component is held in 32 bits, within
    // plus or minus MAX_DIMEN: math_glue (src/math_mlist.h) brings a component
    // inside that before it multiplies, so nothing a conversion reads is lost.
    MuGlue mu() const
    {
        MuGlue out;
        out.width = signed_of(codepoint);
        out.stretch = signed_of(span.first);
        out.shrink = signed_of(span.count);
        return out;
    }

    static Field math_char(uint32_t codepoint)
    {
        Field field;
        field.type = FieldType::MATH_CHAR;
        field.codepoint = codepoint;
        return field;
    }

    // `box_node` must be a BOX node of the arena the list is converted into.
    static Field sub_box(const Node& box_node)
    {
        return sub_box_index(box_node.is(NodeType::BOX) ? box_node.u : ~uint32_t(0));
    }

    // The same by the box's index in that arena.
    static Field sub_box_index(uint32_t index)
    {
        Field field;
        field.type = FieldType::SUB_BOX;
        field.codepoint = index;
        return field;
    }

    static Field sub_list(MathSpan items)
    {
        Field field;
        field.type = FieldType::SUB_LIST;
        field.span = items;
        return field;
    }

    static Field mu_glue(const MuGlue& mu)
    {
        Field field;
        field.type = FieldType::MU_GLUE;
        field.codepoint = bits_of(clamp_dimen(mu.width));
        field.span.first = bits_of(clamp_dimen(mu.stretch));
        field.span.count = bits_of(clamp_dimen(mu.shrink));
        return field;
    }

private:
    // A value within 32 bits as its two's-complement bit pattern, and back.
    static uint32_t bits_of(Scaled value)
    {
        const int32_t narrow = static_cast<int32_t>(value);
        uint32_t bits = 0;
        std::memcpy(&bits, &narrow, sizeof bits);
        return bits;
    }
    static Scaled signed_of(uint32_t bits)
    {
        int32_t narrow = 0;
        std::memcpy(&narrow, &bits, sizeof narrow);
        return narrow;
    }
};

static_assert(sizeof(Field) == 16, "three fields and a header make a 64-byte item");

// Anything that can appear in a math list: an atom (tex.web's noad, sections
// 681 and 682), or a fraction (section 683's fraction_noad), which reuses an
// atom's fields under other names, as it does there (numerator is supscr,
// denominator is subscr).
//
// For a fraction `kind`, `limits` and `nucleus` mean nothing. `has_thickness`
// false is tex.web's default_code: use the font's default rule thickness
// (section 743). A thickness of exactly zero is \atop, a stacked pair with no
// rule between them, and sections 743, 744 and 747 all branch on that.
struct MathItem {
    bool is_fraction = false;
    AtomType kind = AtomType::ORD;
    Limits limits = Limits::DEFAULT;
    bool has_thickness = false;
    Scaled thickness = 0;
    Field nucleus;
    Field sup; // a fraction's numerator
    Field sub; // a fraction's denominator

    const Field& numerator() const { return sup; }
    const Field& denominator() const { return sub; }

    static MathItem atom(AtomType kind, const Field& nucleus = Field(), const Field& sup = Field(),
                         const Field& sub = Field(), Limits limits = Limits::DEFAULT)
    {
        MathItem item;
        item.kind = kind;
        item.limits = limits;
        item.nucleus = nucleus;
        item.sup = sup;
        item.sub = sub;
        return item;
    }

    // A fraction with the font's default rule thickness.
    static MathItem fraction(const Field& numerator, const Field& denominator)
    {
        MathItem item;
        item.is_fraction = true;
        item.kind = AtomType::INNER;
        item.sup = numerator;
        item.sub = denominator;
        return item;
    }

    // A fraction with a stated rule thickness; zero is \atop.
    static MathItem fraction(const Field& numerator, const Field& denominator, Scaled thickness)
    {
        MathItem item = fraction(numerator, denominator);
        item.has_thickness = true;
        item.thickness = thickness;
        return item;
    }

    // An explicit space of so many math units: \mskip. Transparent to
    // everything else: it does not stop a BIN from binding what is on its far
    // side and the spacing table does not see it.
    static MathItem space(const MuGlue& mu) { return atom(AtomType::SPACE, Field::mu_glue(mu)); }
};

static_assert(sizeof(MathItem) == 64, "an item is one cache line");

// A list somebody else owns: a vector, or a span of a MathList.
struct MathView {
    const MathItem* data = nullptr;
    size_t size = 0;

    MathView() = default;
    MathView(const MathItem* items, size_t count)
        : data(items)
        , size(count)
    {
    }
    MathView(const std::vector<MathItem>& items)
        : data(items.data())
        , size(items.size())
    {
    }

    const MathItem* begin() const { return data; }
    const MathItem* end() const { return data + size; }
    bool empty() const { return size == 0; }
    const MathItem& operator[](size_t index) const { return data[index]; }
};

// Storage for the items of nested lists and for prose. See REPRESENTATION.
// Not thread-safe; one list belongs to one piece of work at a time.
class MathList
{
public:
    // Copies a list into contiguous storage and returns where it is. `items`
    // may view this list's own storage. Lists are indexed by 32 bits.
    MathSpan add_items(MathView items)
    {
        MathSpan span;
        span.first = static_cast<uint32_t>(_items.size());
        span.count = static_cast<uint32_t>(items.size);
        if (items.size != 0) {
            // `items` may point into _items, which the insertion can move, so
            // the source is found again after the storage has grown.
            const std::less<const MathItem*> before;
            const MathItem* const begin = _items.data();
            const bool own = !before(items.data, begin) && before(items.data, begin + span.first);
            const size_t offset = own ? static_cast<size_t>(items.data - begin) : 0;
            // Grown by doubling: an exact reserve on every call would copy
            // the whole list each time.
            const size_t needed = _items.size() + items.size;
            if (needed > _items.capacity())
                _items.reserve(needed > 2 * _items.capacity() ? needed : 2 * _items.capacity());
            const MathItem* source = own ? _items.data() + offset : items.data;
            _items.insert(_items.end(), source, source + items.size);
        }
        return span;
    }

    // A nested formula as a field.
    Field sub_list(MathView items) { return Field::sub_list(add_items(items)); }

    // Prose as a field. The bytes are copied.
    Field text(std::string_view utf8, Face face = Face::PROSE)
    {
        Field field;
        field.type = FieldType::MATH_TEXT;
        field.face = face;
        field.span.first = static_cast<uint32_t>(_text.size());
        field.span.count = static_cast<uint32_t>(utf8.size());
        _text.append(utf8.data(), utf8.size());
        return field;
    }

    // The items a span names. False, with `out` empty, for a span that reaches
    // outside this list.
    bool view(MathSpan span, MathView* out) const
    {
        *out = MathView();
        if (span.first > _items.size() || span.count > _items.size() - span.first)
            return false;
        *out = MathView(_items.data() + span.first, span.count);
        return true;
    }

    // The bytes a MATH_TEXT field names; the same check.
    bool text_of(MathSpan span, std::string_view* out) const
    {
        *out = std::string_view();
        if (span.first > _text.size() || span.count > _text.size() - span.first)
            return false;
        *out = std::string_view(_text.data() + span.first, span.count);
        return true;
    }

    size_t item_count() const { return _items.size(); }

    // Drops everything and keeps the capacity.
    void clear()
    {
        _items.clear();
        _text.clear();
    }

private:
    std::vector<MathItem> _items;
    std::string _text;
};

} // namespace ImTypeset::detail
