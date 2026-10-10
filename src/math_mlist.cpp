// See math_mlist.h for the map of tex.web's parts 35 and 36 onto this file and
// for where it departs from them. Section numbers are tex.web's.
//
// HOW THE LISTS ARE HELD. The reference builds a Python list per atom and
// returns it. Here every list under construction lives on one stack, `_nodes`:
// a function that "returns nodes" pushes them, and its caller reads them as
// the run from where the stack stood before the call. Packing such a run
// copies it into the arena as the box's contents and cuts the stack back. A
// box in hand is a `Box` value whose contents are already in the arena, as a
// box in the reference is a value holding a tuple; it becomes a node
// (`node_of`) only when it is put in a list.
//
// A NodeView of the stack is dead as soon as anything is pushed. Every view
// below is taken, used and dropped before the next push.
//
// ERRORS are sticky: the first one is kept, every function returns early once
// one is set, and the caller of the whole conversion discards the result. The
// order in which fields are converted is the reference's, so the first error
// met is the exception the reference would have raised.

#include "math_mlist.h"

#include "fontlib.h"
#include "unicode_tables.h"

#include <algorithm>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>

namespace ImTypeset::detail
{

namespace
{

// The reference's round() of a metric: half to even, into scaled points.
Scaled rounded(double value)
{
    return round_to_scaled(value);
}

void append_utf8(std::string& out, uint32_t codepoint)
{
    if (codepoint < 0x80) {
        out += static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
        out += static_cast<char>(0xC0 | (codepoint >> 6));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        out += static_cast<char>(0xE0 | (codepoint >> 12));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07));
        out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
}

std::string formatted(const char* format, ...) IM_FMTARGS(1);

std::string formatted(const char* format, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    if (written <= 0)
        return std::string();
    const size_t length = static_cast<size_t>(written);
    return std::string(buffer, length < sizeof buffer ? length : sizeof buffer - 1);
}

const MathFontMetrics* library_math_resolver(void* user, Scaled size)
{
    return static_cast<FontLibrary*>(user)->math_metrics(static_cast<double>(size));
}

bool is_boundary(AtomType kind)
{
    return kind == AtomType::LEFT || kind == AtomType::RIGHT;
}

// One atom after the first pass: what it spaces as, and where on the stack
// what it became is (tex.web's new_hlist, section 725, which it keeps in the
// noad). `kind` is -1 for a node that is not a noad, an explicit space, which
// the second pass links in without consulting the table (section 761's
// glue_node case).
struct Translated {
    int kind = -1;
    size_t first = 0;
    size_t count = 0;
    // The boundary atom whose delimiter is still to be sized, or null.
    const MathItem* boundary = nullptr;
};

class Converter
{
public:
    Converter(const MathList& list, MathFonts& fonts, NodeArena& arena)
        : _list(list)
        , _fonts(fonts)
        , _arena(arena)
    {
    }

    // tex.web's mlist_to_hlist (section 726). Pushes the hlist of `items` at
    // `style`.
    void mlist_to_hlist(MathView items, Style style);

    const MathStatus& status() const { return _status; }
    const std::vector<Node>& nodes() const { return _nodes; }

    void check_measure()
    {
        if (_fonts.unmeasured())
            fail(MathError::TEXT_FACE_UNAVAILABLE);
    }

private:
    bool failed() const { return _status.error != MathError::NONE; }

    void fail(MathError error)
    {
        if (!failed())
            _status.error = error;
    }

    // --- The stack and the arena

    NodeView top(size_t base) const { return NodeView(_nodes.data() + base, _nodes.size() - base); }

    PackResult hpack_view(NodeView nodes, Scaled target = 0, PackMode mode = PackMode::ADDITIONAL)
    {
        const PackResult packed = hpack(_arena, nodes, target, mode, _fonts.measure());
        if (packed.error != PackError::NONE)
            fail(MathError::MALFORMED);
        check_measure();
        return packed;
    }

    // A list copied into the arena as a box's contents. A list already as deep
    // as the arena allows is refused: the box around it would be one deeper.
    NodeSpan store(NodeView nodes)
    {
        for (const Node& node : nodes) {
            if (_arena.nesting(node) >= NodeArena::MAX_NESTING) {
                fail(MathError::TOO_DEEP);
                return NodeSpan();
            }
        }
        return _arena.add_nodes(nodes);
    }

    // hpack at natural width of the stack from `base`, which is cut back.
    Box hpack_from(size_t base)
    {
        Box box = hpack_view(top(base)).box;
        box.contents = store(top(base));
        _nodes.resize(base);
        return box;
    }

    Box vpack_from(size_t base)
    {
        const PackResult packed = vpack(_arena, top(base));
        if (packed.error != PackError::NONE)
            fail(MathError::MALFORMED);
        Box box = packed.box;
        box.contents = store(top(base));
        _nodes.resize(base);
        return box;
    }

    // Every box the conversion makes is stored here, so here is where one is
    // held to the bound every stored dimension has. Glue and kerns are bounded
    // one by one, and a list of them is as wide as it is long.
    Node node_of(const Box& box)
    {
        const Scaled outside = dimension_outside_max_dimen(box);
        if (outside != 0) {
            if (!failed()) {
                _status.error = MathError::TOO_LARGE;
                _status.needed = outside;
                _status.largest = MAX_DIMEN;
            }
            return _arena.add_box(Box());
        }
        return _arena.add_box(box);
    }
    // Every node the conversion puts in a list goes through here, so here is
    // where a kern and a rule are held to the same bound. A box's stated
    // dimensions do not bound what is inside it: section 750 states the height
    // and depth of a stack of limits, and section 747 a fraction's, and in
    // both a font whose parameters are out of all proportion can make two
    // kerns, or a kern and a rule, that cancel. Glue is bounded where it is
    // made (from_mu, interword_glue) and a character holds no dimension.
    void push(const Node& node)
    {
        if (node.type == NodeType::KERN || node.type == NodeType::RULE) {
            const bool rule = node.type == NodeType::RULE;
            for (const Scaled dimension : {node.a, node.b, node.c}) {
                if ((rule && dimension == RUNNING) ||
                    (dimension >= -MAX_DIMEN && dimension <= MAX_DIMEN))
                    continue;
                if (!failed()) {
                    _status.error = MathError::TOO_LARGE;
                    _status.needed = dimension;
                    _status.largest = MAX_DIMEN;
                    _status.inside = true;
                }
                _nodes.push_back(kern(0));
                return;
            }
        }
        _nodes.push_back(node);
    }
    void push(const Box& box) { _nodes.push_back(node_of(box)); }

    // The Box a SUB_BOX field names, by value. False for an index the arena
    // does not hold.
    bool sub_box(const Field& field, Box* out)
    {
        if (field.box() >= _arena.box_count()) {
            fail(MathError::MALFORMED);
            return false;
        }
        Node node;
        node.type = NodeType::BOX;
        node.u = field.box();
        *out = _arena.box(node);
        return true;
    }

    bool sub_list(const Field& field, MathView* out)
    {
        if (!_list.view(field.span, out)) {
            fail(MathError::MALFORMED);
            return false;
        }
        return true;
    }

    // --- Sections 720, 721, and prose

    Scaled translate_nucleus(const Field& field, Style style, bool has_subscript);
    Box clean_box(const Field& field, Style style);
    Box clean_box(const Box& box);
    Box clean_pack(size_t base);
    Box simplified(Box box) const;
    Box text_box(const Field& field, Style style);

    // --- Sections 706 to 709, 737, 762

    bool delimiter_box(uint32_t codepoint, Style style, Scaled needed, const Scaled* at_least,
                       Box* box_out, MathSize* size_out);
    Box make_left_right(const MathItem& atom, Style style, Scaled max_d, Scaled max_h);
    void make_radical(const MathItem& atom, Style style);
    Box rebox(Box box, Scaled width);

    // --- Sections 749 to 751

    struct Op {
        Scaled delta = 0;
        bool finished = false;
    };
    Op make_op(const MathItem& atom, Style style);
    Box stack_limits(const MathItem& atom, const Box* operator_box, Scaled delta, Style style);

    // --- Sections 734 to 736, 743 to 747, 756 to 759

    void make_scripts(size_t base, const MathItem& atom, Scaled delta, Style style);
    void make_fraction(const MathItem& fraction, Style style);
    Box overbar(const Box& box, Scaled gap, Scaled thickness);
    void make_over(const MathItem& atom, Style style);
    void make_under(const MathItem& atom, Style style);
    void make_vcenter(const MathItem& atom, Style style);

    // --- Sections 727 to 733, 760 to 766

    void translate(const MathItem& item, Style style);
    Node math_space(const MathItem& atom, Style style);
    void size_boundaries(size_t first, Style style);
    bool inter_atom_glue(AtomType left, AtomType right, Style style, GlueSpec* spec);

    const MathList& _list;
    MathFonts& _fonts;
    NodeArena& _arena;
    std::vector<Node> _nodes;
    std::vector<Translated> _translated;
    std::vector<uint32_t> _codepoints;
    MathStatus _status;
    int _depth = 0;
    uint32_t _items = 0;
};

// A nucleus as nodes, pushed, and tex.web's delta: the italic correction
// (sections 754 and 755). A math character is followed by a kern of its italic
// correction, which is what leans a script off the end of an integral sign.
// The function has no name in tex.web; it is the case statement of section
// 754.
//
// `has_subscript` is whether the atom whose nucleus this is has a subscript,
// which is what section 755 asks.
//
// Three departures from those sections, the reference's, the first two of
// them switches (MathFonts::rules, src/tex_rules.h):
//
//   - Section 755 (tex.web line 14868) appends the kern only when the atom
//     has no subscript, and then clears delta; with a subscript it appends
//     nothing and delta goes to make_scripts, which sets the superscript that
//     far right of the subscript (section 759). The reference appends the
//     kern whenever the correction is not zero and returns delta all the
//     same, so a subscript sits one correction further right than tex.web
//     puts it and a superscript over a subscript two where tex.web has one.
//     italic_kern_only_without_subscript is the section.
//   - A sublist is converted and left on the stack as it is. Section 754
//     (line 14849) packs it into one box, and delta is zero for it there too.
//     sublist_nucleus_packed is the section.
//   - There is no math_text_char, so no case in which section 755 drops the
//     correction, and none of section 752's kerns and ligatures between
//     adjacent characters.
Scaled Converter::translate_nucleus(const Field& field, Style style, bool has_subscript)
{
    switch (field.type) {
    case FieldType::EMPTY:
        return 0;
    case FieldType::MATH_CHAR: {
        const Scaled delta = rounded(_fonts.face(style).italic_correction(field.codepoint));
        push(char_node(_fonts.key(style), field.codepoint));
        if (_fonts.rules.italic_kern_only_without_subscript) {
            if (has_subscript)
                return delta;
            if (delta != 0)
                push(kern(delta));
            return 0;
        }
        if (delta != 0)
            push(kern(delta));
        return delta;
    }
    case FieldType::SUB_BOX: {
        Box box;
        if (sub_box(field, &box))
            push(box);
        return 0;
    }
    case FieldType::SUB_LIST: {
        MathView items;
        if (!sub_list(field, &items))
            return 0;
        const size_t base = _nodes.size();
        mlist_to_hlist(items, style);
        if (_fonts.rules.sublist_nucleus_packed && !failed()) {
            const Box packed = hpack_from(base);
            push(packed);
        }
        return 0;
    }
    case FieldType::MATH_TEXT: {
        const Box box = text_box(field, style);
        if (!failed())
            push(box);
        return 0;
    }
    case FieldType::MU_GLUE:
        break;
    }
    // A MU_GLUE is the nucleus of a SPACE atom and of nothing else; any other
    // value is not a FieldType.
    fail(MathError::MALFORMED);
    return 0;
}

// Section 720's clean_box: a field, converted at a given style and packed into
// one box. What was converted is always packed; section 720 (lines 14194 to
// 14196) takes a result that is already a single unshifted box as it stands,
// which is what happens under TexRules::sublist_nucleus_packed (clean_pack).
// The dimensions are the same and the reference's box is one level deeper.
Box Converter::clean_box(const Field& field, Style style)
{
    if (failed())
        return Box();
    const size_t base = _nodes.size();
    switch (field.type) {
    case FieldType::EMPTY:
        break;
    case FieldType::MATH_CHAR:
        // Section 720 converts a new noad holding the character and nothing
        // else: no subscript.
        translate_nucleus(field, style, false);
        break;
    case FieldType::SUB_BOX: {
        Box box;
        return sub_box(field, &box) ? clean_box(box) : Box();
    }
    case FieldType::SUB_LIST: {
        MathView items;
        if (sub_list(field, &items))
            mlist_to_hlist(items, style);
        break;
    }
    case FieldType::MATH_TEXT:
        return text_box(field, style);
    default:
        fail(MathError::MALFORMED);
        break;
    }
    if (failed()) {
        _nodes.resize(base);
        return Box();
    }
    return clean_pack(base);
}

// clean_box of material that is already a box (section 720's sub_box case):
// used as it stands unless it is shifted, in which case it is wrapped so the
// shift is inside the result. Section 721 is applied to a box used as it
// stands too; the reference leaves such a box alone, and so does this unless
// TexRules::sublist_nucleus_packed.
Box Converter::clean_box(const Box& box)
{
    if (failed())
        return Box();
    if (box.shift_amount == 0)
        return _fonts.rules.sublist_nucleus_packed ? simplified(box) : box;
    const size_t base = _nodes.size();
    push(box);
    return clean_pack(base);
}

// The pack that ends clean_box (section 720's hpack), with section 721, which
// simplifies a trivial box: the italic-correction kern after a lone character
// is dropped without narrowing the box. The width still includes the
// correction, so a later rebox can put the glyph back where it belongs.
//
// Under TexRules::sublist_nucleus_packed a list that is one unshifted box is
// not packed again: that box is the result (section 720's "already clean"),
// simplified as section 721 simplifies any result.
Box Converter::clean_pack(size_t base)
{
    if (_fonts.rules.sublist_nucleus_packed && _nodes.size() - base == 1 &&
        _nodes[base].is(NodeType::BOX)) {
        const Box only = _arena.box(_nodes[base]);
        if (only.shift_amount == 0) {
            _nodes.resize(base);
            return simplified(only);
        }
    }
    Box box = hpack_view(top(base)).box;
    const NodeView nodes = top(base);
    const bool trivial =
        nodes.size() == 2 && nodes[0].is(NodeType::CHAR) && nodes[1].is(NodeType::KERN);
    box.contents = store(trivial ? nodes.sub(0, 1) : nodes);
    _nodes.resize(base);
    return box;
}

// Section 721 for a box that already exists: a list that is one character and
// the kern after it loses the kern, and the box keeps its width. The box's
// list is in the arena and is not written; the result names its first node.
Box Converter::simplified(Box box) const
{
    if (box.kind != BoxKind::HLIST || box.contents.count != 2)
        return box;
    const NodeView nodes = _arena.view(box.contents);
    if (nodes.size() == 2 && nodes[0].is(NodeType::CHAR) && nodes[1].is(NodeType::KERN))
        box.contents.count = 1;
    return box;
}

// Not tex.web's, where text in a formula is an hbox the caller made in
// horizontal mode and hands over as a sub_box.
//
// Prose inside a formula, packed at the size of the style it landed in: the
// face's own pair kerning and an interword glue for each white-space
// character, every one kept, leading and trailing too, because here they are
// the request. The glue is interword_glue at its defaults — a neutral space
// factor — and keeps a stretch and shrink that do nothing, the box being
// packed to its natural width.
Box Converter::text_box(const Field& field, Style style)
{
    if (failed())
        return Box();
    _status.face = field.face;
    _status.text = field.span;
    if (!_fonts.text()) {
        fail(MathError::TEXT_FACE_UNAVAILABLE);
        return Box();
    }
    std::string_view text;
    if (!_list.text_of(field.span, &text)) {
        fail(MathError::MALFORMED);
        return Box();
    }
    const FontKey key = font_key(field.face, _fonts.size(size_for(style)));
    const FontMetrics* metrics = _fonts.text()(key);
    if (metrics == nullptr) {
        fail(MathError::TEXT_FACE_UNAVAILABLE);
        return Box();
    }
    const Scaled space = rounded(metrics->space_width());
    _codepoints.clear();
    decode_utf8(text, _codepoints);
    const size_t base = _nodes.size();
    for (size_t i = 0; i < _codepoints.size(); ++i) {
        const uint32_t character = _codepoints[i];
        if (py_isspace(character)) {
            push(glue(interword_glue(space)));
            continue;
        }
        if (!metrics->has(character)) {
            _status.codepoint = character;
            fail(MathError::MISSING_GLYPH);
            _nodes.resize(base);
            return Box();
        }
        push(char_node(key, character));
        if (i + 1 < _codepoints.size() && !py_isspace(_codepoints[i + 1])) {
            const Scaled pair = rounded(metrics->kern(character, _codepoints[i + 1]));
            if (pair != 0)
                push(kern(pair));
        }
    }
    return hpack_from(base);
}

// The smallest variant of a delimiter whose height plus depth is `needed` or
// more, with the size of the font it was found in.
//
// var_delimiter's search (sections 706 to 708) without its extensible half.
// Two loops, as there: the outer runs from the font of the current size up to
// the text font, so a delimiter in a subscript is tried at script size first
// and borrows a larger font only if it must; the inner follows the variant
// chain. The first character tall enough wins, and the largest seen so far is
// remembered in case none is (section 708).
//
// The box is char_box's (section 709): the one character, with its italic
// correction in the width, unshifted.
//
// `at_least`, if given, is what the largest variant must reach to be used when
// none reaches `needed`; without it nothing short is ever returned. tex.web
// would settle for the largest whatever it was (section 706).
//
// Besides that, this is not all of var_delimiter, and so does not have its
// name: a character with an extensible recipe is not built (section 708's
// ext_tag, sections 710 to 714); a delimiter has one character and not a small
// and a large one in two families (section 706's two attempts); the variant
// chain is the OpenType MATH table's and not a TFM character list; and the box
// is not centred on the axis here, which section 706 does last and the two
// callers do for themselves.
bool Converter::delimiter_box(uint32_t codepoint, Style style, Scaled needed,
                              const Scaled* at_least, Box* box_out, MathSize* size_out)
{
    if (failed())
        return false;
    const Scaled floor = at_least == nullptr ? needed : std::min(*at_least, needed);
    Scaled largest = 0;
    bool have_fallback = false;
    Box fallback;
    Node fallback_char;
    MathSize fallback_size = MathSize::TEXT;

    int size = static_cast<int>(size_for(style));
    for (;;) {
        const MathSize at = static_cast<MathSize>(size);
        const MathFontMetrics& face = _fonts.face(at);
        const FontKey key = _fonts.key(at);
        const GlyphVariants chain = face.vertical_variants(codepoint);
        const size_t candidates = chain.empty() ? 1 : chain.size;
        for (size_t i = 0; i < candidates; ++i) {
            const uint32_t candidate = chain.empty() ? codepoint : chain[i].codepoint;
            const CharDimens dimens = _fonts.measure(key, candidate);
            const Scaled extent = dimens.height + dimens.depth;
            if (extent < needed && extent <= largest)
                continue;
            Box box;
            box.kind = BoxKind::HLIST;
            box.width = dimens.width + rounded(face.italic_correction(candidate));
            box.height = dimens.height;
            box.depth = dimens.depth;
            const Node character = char_node(key, candidate);
            if (extent >= needed) {
                box.contents = _arena.add_nodes(NodeView(&character, 1));
                *box_out = box;
                *size_out = at;
                return true;
            }
            largest = extent;
            have_fallback = true;
            fallback = box;
            fallback_char = character;
            fallback_size = at;
        }
        if (at == MathSize::TEXT)
            break;
        size -= 16;
    }
    if (have_fallback && largest >= floor) {
        fallback.contents = _arena.add_nodes(NodeView(&fallback_char, 1));
        *box_out = fallback;
        *size_out = fallback_size;
        return true;
    }
    if (!failed()) {
        _status.codepoint = codepoint;
        _status.needed = needed;
        _status.largest = largest;
        fail(MathError::DELIMITER_TOO_SMALL);
    }
    return false;
}

// make_left_right (section 762): one delimiter of a \left ... \right.
//
// delta1 is the content's greater reach from the axis, up or down, so the
// delimiter is symmetric about the axis even when the content is not. It must
// cover delimiter_factor thousandths of twice that, and may not fall short of
// twice that by more than delimiter_shortfall. The division by 500 before the
// multiplication is the section's; it floors, as the reference's does, where
// Pascal's truncates. The two differ only for a negative delta1, which takes
// content of negative height and depth.
//
// The smallest variant satisfying both is taken. When none does the largest
// is, provided it covers LARGEST_VARIANT_MUST_COVER of twice delta1 (rounded
// up), and the formula is refused below that. tex.web has no such floor.
//
// The box comes back centred on the axis, var_delimiter's last act (section
// 706), which is applied to the null delimiter too. The section returns the
// type the boundary spaces as, open_noad or close_noad; the first pass here
// has settled that already. delta, delta1 and delta2 are its names.
Box Converter::make_left_right(const MathItem& atom, Style style, Scaled max_d, Scaled max_h)
{
    const MathParams& params = _fonts.params(style);
    const Scaled axis = rounded(params.axis_height);
    Scaled delta2 = max_d + axis;
    Scaled delta1 = max_h + max_d - delta2;
    if (delta2 > delta1)
        delta1 = delta2;
    Scaled delta = floor_div(delta1, 500) * DELIMITER_FACTOR;
    delta2 = delta1 + delta1 - _fonts.delimiter_shortfall;
    if (delta < delta2)
        delta = delta2;
    const Scaled covering = ceil_div(2 * delta1 * LARGEST_VARIANT_MUST_COVER_NUMERATOR,
                                     LARGEST_VARIANT_MUST_COVER_DENOMINATOR);

    Box box;
    switch (atom.nucleus.type) {
    case FieldType::EMPTY:
        box.kind = BoxKind::HLIST;
        box.width = rounded(params.math_quad * NULL_DELIMITER_SPACE_QUADS);
        break;
    case FieldType::MATH_CHAR: {
        MathSize found_at = MathSize::TEXT;
        if (!delimiter_box(atom.nucleus.codepoint, style, delta, &covering, &box, &found_at))
            return Box();
        break;
    }
    default:
        // A delimiter is one math character or nothing.
        fail(MathError::MALFORMED);
        return Box();
    }
    box.shift_amount = half(box.height - box.depth) - axis;
    return box;
}

// make_radical (section 737): a surd, a rule, and the radicand under it.
//
// The radicand is set cramped. The clearance between it and the rule is larger
// in display style; tex.web computes both values (from the rule thickness and
// the x-height) and the font states both results (RadicalVerticalGap and
// RadicalDisplayStyleVerticalGap), which are what is used. The surd is the
// smallest variant covering radicand, clearance and rule, and what it has to
// spare is split: the actual clearance is clr plus half the excess.
//
// The rule is as thick as the font the surd was found in says, not as the
// current size's: they differ when a radical in a script outgrows the script
// font's chain and borrows a text-size surd.
//
// The glyph hangs from the top of its ink (math_mlist.h). The result is one
// box, as it is in the section, so scripts hang off the whole radical.
//
// A radical is refused when no surd covers it and does not get the
// largest-variant allowance a \left pair has.
void Converter::make_radical(const MathItem& atom, Style style)
{
    const MathParams& params = _fonts.params(style);
    const Box radicand = clean_box(atom.nucleus, cramped_style(style));
    if (failed())
        return;
    Scaled thickness = rounded(params.radical_rule_thickness);
    Scaled clearance = is_display(style) ? rounded(params.radical_display_style_vertical_gap)
                                         : rounded(params.radical_vertical_gap);

    const Scaled covered = radicand.height + radicand.depth + clearance;
    Box surd;
    MathSize found_at = MathSize::TEXT;
    if (!delimiter_box(SURD, style, covered + thickness, nullptr, &surd, &found_at))
        return;
    thickness = rounded(_fonts.face(found_at).math().radical_rule_thickness);
    const Scaled excess = (surd.height + surd.depth - thickness) - covered;
    if (excess > 0)
        clearance += half(excess);
    const Scaled rule_top = radicand.height + clearance + thickness;
    surd.shift_amount = surd.height - rule_top;

    const Box barred = overbar(radicand, clearance, thickness);
    const size_t base = _nodes.size();
    push(surd);
    push(barred);
    const Box whole = hpack_from(base);
    push(whole);
}

// Section 715's rebox: re-centre a box in a new width, with \hss either side
// (tex.web's ss_glue).
//
// A vlist is wrapped, not unwrapped: section 715 hpacks it first, so the
// centring applies to the whole thing. A single character gets an explicit
// kern first, because clean_box folded an italic correction into the width and
// centring the glyph without compensating would push it half a correction to
// the left.
Box Converter::rebox(Box box, Scaled width)
{
    if (failed())
        return Box();
    if (box.width == width || box.contents.count == 0) {
        box.width = width;
        return box;
    }
    const Node hss = glue(GlueSpec{0, UNITY, GlueOrder::FIL, UNITY, GlueOrder::FIL});
    const size_t base = _nodes.size();
    push(hss);
    if (box.kind == BoxKind::VLIST) {
        push(box);
    } else {
        const NodeView contents = _arena.view(box.contents);
        // Copied by index: the arena is not written while this runs, and the
        // stack is not what the view names.
        for (size_t i = 0; i < contents.size(); ++i) {
            const Node node = contents[i];
            push(node);
        }
    }
    if (_nodes.size() - base == 2 && _nodes[base + 1].is(NodeType::CHAR)) {
        const Node character = _nodes[base + 1];
        const Scaled advance = _fonts.measure(character.font(), character.codepoint()).width;
        if (advance != box.width)
            push(kern(box.width - advance));
    }
    push(hss);
    Box out = hpack_view(top(base), width, PackMode::EXACTLY).box;
    out.contents = store(top(base));
    _nodes.resize(base);
    return out;
}

// make_op (section 749): enlarge, centre on the axis, and stack the limits if
// they belong there. Pushes the material. `finished` says the limits were
// stacked (the section's subtype(q) = limits on return), in which case the
// caller must not call make_scripts: they are already in the box. `op.delta`
// is the value the section returns.
//
// "Larger" is one step along the font's vertical variant chain where the
// section takes the TFM successor (rem_byte of a list_tag character): the same
// rule over the other font format.
Converter::Op Converter::make_op(const MathItem& atom, Style style)
{
    Op op;
    Limits limits = atom.limits;
    if (limits == Limits::DEFAULT && is_display(style))
        limits = Limits::LIMITS;

    const MathFontMetrics& face = _fonts.face(style);
    bool boxed = false;
    Box operator_box;

    if (atom.nucleus.type == FieldType::MATH_CHAR) {
        uint32_t codepoint = atom.nucleus.codepoint;
        if (is_display(style)) {
            // Exactly one step along the chain. Not a search: var_delimiter
            // walks the chain looking for a fit and make_op does not.
            const GlyphVariants chain = face.vertical_variants(codepoint);
            if (chain.size > 1)
                codepoint = chain[1].codepoint;
        }
        op.delta = rounded(face.italic_correction(codepoint));
        operator_box = clean_box(Field::math_char(codepoint), style);
        if (failed())
            return op;
        // The italic correction is removed when a subscript sits to the right
        // of the operator and not beneath it.
        if (!atom.sub.empty() && limits != Limits::LIMITS)
            operator_box.width -= op.delta;
        operator_box.shift_amount =
            half(operator_box.height - operator_box.depth) - rounded(face.math().axis_height);
        boxed = true;
    }

    if (limits == Limits::LIMITS) {
        const Box stacked = stack_limits(atom, boxed ? &operator_box : nullptr, op.delta, style);
        if (!failed())
            push(stacked);
        op.finished = true;
        return op;
    }

    if (boxed)
        push(operator_box);
    else
        translate_nucleus(atom.nucleus, style, !atom.sub.empty());
    return op;
}

// Sections 750 and 751: the upper limit, the operator and the lower limit in a
// vlist. Part of make_op in tex.web and without a name of its own there.
//
// Built by hand and not by vpack, as the section does, because the height and
// depth are stated: the vlist's baseline has to stay the operator's own,
// already axis-centred baseline, and a vpack would put it on the last box. The
// big_op_spacing5 kerns are padding that counts towards the height and depth.
//
// `operator_box` is the operator if make_op boxed it; otherwise the atom's
// nucleus is cleaned here.
Box Converter::stack_limits(const MathItem& atom, const Box* operator_box, Scaled delta,
                            Style style)
{
    const MathParams& params = _fonts.params(style);
    Box upper = clean_box(atom.sup, sup_style(style));
    Box middle =
        operator_box != nullptr ? clean_box(*operator_box) : clean_box(atom.nucleus, style);
    Box lower = clean_box(atom.sub, sub_style(style));
    if (failed())
        return Box();

    const Scaled width = std::max(upper.width, std::max(middle.width, lower.width));
    upper = rebox(upper, width);
    upper.shift_amount = half(delta);
    middle = rebox(middle, width);
    lower = rebox(lower, width);
    lower.shift_amount = -half(delta);
    if (failed())
        return Box();

    const Scaled padding = rounded(params.big_op_spacing5);
    Scaled height = middle.height;
    Scaled depth = middle.depth;
    const size_t base = _nodes.size();

    if (!atom.sup.empty()) {
        // Section 751: a minimum baseline distance, floored by a minimum gap.
        const Scaled rise = std::max(rounded(params.big_op_spacing3) - upper.depth,
                                     rounded(params.big_op_spacing1));
        push(kern(padding));
        push(upper);
        push(kern(rise));
        height += padding + upper.height + upper.depth + rise;
    }

    push(middle);

    if (!atom.sub.empty()) {
        const Scaled drop = std::max(rounded(params.big_op_spacing4) - lower.height,
                                     rounded(params.big_op_spacing2));
        push(kern(drop));
        push(lower);
        push(kern(padding));
        depth += padding + lower.height + lower.depth + drop;
    }

    Box out;
    out.kind = BoxKind::VLIST;
    out.contents = store(top(base));
    out.width = width;
    out.height = height;
    out.depth = depth;
    _nodes.resize(base);
    return out;
}

// make_scripts (sections 756 to 759): attach a subscript, a superscript, or
// both, to the nucleus on the stack from `base`.
//
// The top of a lone subscript may not rise above four fifths of the x-height
// (section 757), and the bottom of a superscript may not fall below one
// quarter of it (section 758). When both are present they must be at least
// four rule thicknesses apart, and if they are not the subscript drops and
// then both rise together (section 759).
void Converter::make_scripts(size_t base, const MathItem& atom, Scaled delta, Style style)
{
    const MathParams& params = _fonts.params(style);
    Scaled x_height = rounded(params.math_x_height);
    if (x_height < 0)
        x_height = -x_height;
    const Style smaller = static_cast<int>(style) < static_cast<int>(Style::SCRIPT)
                              ? Style::SCRIPT
                              : Style::SCRIPT_SCRIPT;
    const MathParams& smaller_params = _fonts.params(smaller);

    // shift_up, shift_down and (below) clr under the name `clearance` are
    // the section's own variables.
    Scaled shift_up = 0;
    Scaled shift_down = 0;
    // Section 756 asks is_char_node of the nucleus's translation, and a
    // nucleus that is a list has been packed into a box by then (section 754),
    // so only a nucleus that is one character has no box to measure and starts
    // both shifts at zero. A group's nodes are not packed here as the section
    // packs them; the box measured below is the same box either way.
    const bool bare_character = atom.nucleus.type == FieldType::MATH_CHAR && _nodes.size() > base &&
                                _nodes[base].is(NodeType::CHAR);
    if (!bare_character) {
        const Box enclosing = hpack_view(top(base)).box;
        shift_up = enclosing.height - rounded(smaller_params.sup_drop);
        shift_down = enclosing.depth + rounded(smaller_params.sub_drop);
    }
    if (failed())
        return;

    Box attached;
    if (atom.sup.empty()) {
        // Section 757.
        Box script = clean_box(atom.sub, sub_style(style));
        script.width += SCRIPT_SPACE;
        shift_down = std::max(
            shift_down, std::max(rounded(params.sub1), script.height - floor_div(x_height * 4, 5)));
        script.shift_amount = shift_down;
        attached = script;
    } else {
        // Section 758.
        Box upper = clean_box(atom.sup, sup_style(style));
        upper.width += SCRIPT_SPACE;
        Scaled clearance = 0;
        if (is_cramped(style))
            clearance = rounded(params.sup3);
        else if (is_display(style))
            clearance = rounded(params.sup1);
        else
            clearance = rounded(params.sup2);
        shift_up = std::max(shift_up, std::max(clearance, upper.depth + floor_div(x_height, 4)));

        if (atom.sub.empty()) {
            upper.shift_amount = -shift_up;
            attached = upper;
        } else {
            // Section 759.
            Box lower = clean_box(atom.sub, sub_style(style));
            lower.width += SCRIPT_SPACE;
            shift_down = std::max(shift_down, rounded(params.sub2));
            const Scaled gap = 4 * rounded(params.default_rule_thickness) -
                               ((shift_up - upper.depth) - (lower.height - shift_down));
            if (gap > 0) {
                shift_down += gap;
                const Scaled lift = floor_div(x_height * 4, 5) - (shift_up - upper.depth);
                if (lift > 0) {
                    shift_up += lift;
                    shift_down -= lift;
                }
            }
            if (failed())
                return;
            // The superscript sits delta to the right of the subscript, which
            // is what makes an integral's bounds lean with the glyph. After a
            // math character both are already one delta to the right under
            // the reference's rules, behind the kern translate_nucleus
            // appended, which section 755 does not append.
            upper.shift_amount = delta;
            const size_t stack = _nodes.size();
            push(upper);
            push(kern((shift_up - upper.depth) - (lower.height - shift_down)));
            push(lower);
            attached = vpack_from(stack);
            attached.shift_amount = shift_down;
        }
    }
    if (failed())
        return;
    push(attached);
}

// make_fraction (sections 743 to 747): numerator over denominator, with the
// rule centred on the axis.
//
// The three shift pairs are the ones section 744 selects between: display
// takes num1/denom1, non-display with a rule num2/denom2, and \atop — a
// thickness of zero — num3/denom2.
//
// Section 748, which puts the fraction between its two delimiters, is left
// out: the result is the vlist of section 747 itself.
void Converter::make_fraction(const MathItem& fraction, Style style)
{
    const MathParams& params = _fonts.params(style);
    const Scaled rule_thickness = rounded(params.default_rule_thickness);
    const Scaled thickness =
        fraction.has_thickness ? clamp_dimen(fraction.thickness) : rule_thickness;

    Box numerator = clean_box(fraction.numerator(), num_style(style));
    Box denominator = clean_box(fraction.denominator(), denom_style(style));
    if (failed())
        return;
    if (numerator.width < denominator.width)
        numerator = rebox(numerator, denominator.width);
    else
        denominator = rebox(denominator, numerator.width);
    if (failed())
        return;

    Scaled shift_up = 0;
    Scaled shift_down = 0;
    if (is_display(style)) {
        shift_up = rounded(params.num1);
        shift_down = rounded(params.denom1);
    } else {
        shift_down = rounded(params.denom2);
        shift_up = thickness != 0 ? rounded(params.num2) : rounded(params.num3);
    }

    const Scaled axis = rounded(params.axis_height);
    const size_t base = _nodes.size();
    if (thickness == 0) {
        // Section 745. The gap between the two boxes must reach clr; any
        // deficiency is split evenly so the pair stays centred where it was.
        const Scaled clearance = is_display(style) ? 7 * rule_thickness : 3 * rule_thickness;
        const Scaled shortage =
            half(clearance - ((shift_up - numerator.depth) - (denominator.height - shift_down)));
        if (shortage > 0) {
            shift_up += shortage;
            shift_down += shortage;
        }
        push(numerator);
        push(kern((shift_up - numerator.depth) - (denominator.height - shift_down)));
        push(denominator);
    } else {
        // Section 746. The rule straddles the axis: its edges are at axis
        // plus and minus half its thickness.
        const Scaled clearance = is_display(style) ? 3 * thickness : thickness;
        const Scaled half_rule = half(thickness);
        const Scaled above = clearance - ((shift_up - numerator.depth) - (axis + half_rule));
        const Scaled below = clearance - ((axis - half_rule) - (denominator.height - shift_down));
        if (above > 0)
            shift_up += above;
        if (below > 0)
            shift_down += below;
        push(numerator);
        push(kern((shift_up - numerator.depth) - (axis + half_rule)));
        // Section 704's fraction_rule: height the thickness, depth zero, width
        // running so it spans whatever box encloses it.
        push(rule(RUNNING, thickness, 0));
        push(kern((axis - half_rule) - (denominator.height - shift_down)));
        push(denominator);
    }

    // Section 747: a vlist whose height and depth are stated, not packed.
    Box out;
    out.kind = BoxKind::VLIST;
    out.contents = store(top(base));
    out.width = numerator.width;
    out.height = shift_up + numerator.height;
    out.depth = denominator.depth + shift_down;
    _nodes.resize(base);
    if (!failed())
        push(out);
}

// Section 705's overbar: a rule above a box, with a gap under it and air above.
Box Converter::overbar(const Box& box, Scaled gap, Scaled thickness)
{
    const size_t base = _nodes.size();
    push(kern(thickness));
    push(rule(RUNNING, thickness, 0));
    push(kern(gap));
    push(box);
    return vpack_from(base);
}

// make_over (section 734): a bar over the nucleus, three rule thicknesses
// clear of it.
void Converter::make_over(const MathItem& atom, Style style)
{
    const Scaled thickness = rounded(_fonts.params(style).default_rule_thickness);
    const Box inner = clean_box(atom.nucleus, cramped_style(style));
    if (failed())
        return;
    const Box barred = overbar(inner, 3 * thickness, thickness);
    push(barred);
}

// make_under (section 735): a bar under the nucleus. The height of the result
// is the nucleus's own and not the vlist's, which is what stops an underlined
// term pushing the line above it up.
//
// A departure, the reference's: section 735 (tex.web line 14449) adds one
// default_rule_thickness to the height plus depth of the vlist before it
// takes the height off, so that there is that much clearance below the rule.
// The reference does not add it, and its box is one rule thickness shallower
// than tex.web makes it. TexRules::underline_clearance adds it; `delta` is
// the section's name.
void Converter::make_under(const MathItem& atom, Style style)
{
    const Scaled thickness = rounded(_fonts.params(style).default_rule_thickness);
    const Box inner = clean_box(atom.nucleus, style);
    if (failed())
        return;
    const size_t base = _nodes.size();
    push(inner);
    push(kern(3 * thickness));
    push(rule(RUNNING, thickness, 0));
    Box stacked = vpack_from(base);
    const Scaled delta =
        stacked.height + stacked.depth + (_fonts.rules.underline_clearance ? thickness : 0);
    stacked.height = inner.height;
    stacked.depth = delta - inner.height;
    push(stacked);
}

// make_vcenter (section 736): re-hang a box so its vertical middle is on the
// axis. The section accepts only a vbox, and stops with confusion otherwise;
// here any nucleus is cleaned into a box first.
void Converter::make_vcenter(const MathItem& atom, Style style)
{
    const Scaled axis = rounded(_fonts.params(style).axis_height);
    Box box = clean_box(atom.nucleus, style);
    if (failed())
        return;
    const Scaled total = box.height + box.depth;
    box.height = axis + half(total);
    box.depth = total - box.height;
    push(box);
}

// One atom's new_hlist: section 733's dispatch on the kind of noad, then
// section 754, its nucleus with scripts attached if it has any. Pushed.
//
// Section 733 calls make_ord for an ord_noad (sections 752 and 753: kerns and
// ligatures between adjacent math characters, from the font). That is not
// here; an ORD goes straight to its nucleus.
void Converter::translate(const MathItem& item, Style style)
{
    if (item.is_fraction) {
        make_fraction(item, style);
        return;
    }
    if (item.kind == AtomType::ACCENT) {
        fail(MathError::NOT_IMPLEMENTED);
        return;
    }

    const size_t base = _nodes.size();
    Scaled delta = 0;
    switch (item.kind) {
    case AtomType::OP: {
        const Op op = make_op(item, style);
        if (op.finished)
            return;
        delta = op.delta;
        break;
    }
    case AtomType::RADICAL:
        make_radical(item, style);
        break;
    case AtomType::OVER:
        make_over(item, style);
        break;
    case AtomType::UNDER:
        make_under(item, style);
        break;
    case AtomType::VCENTER:
        make_vcenter(item, style);
        break;
    case AtomType::ORD:
    case AtomType::BIN:
    case AtomType::REL:
    case AtomType::OPEN:
    case AtomType::CLOSE:
    case AtomType::PUNCT:
    case AtomType::INNER:
        delta = translate_nucleus(item.nucleus, style, !item.sub.empty());
        break;
    default:
        // LEFT, RIGHT and SPACE are placed by the first pass and never reach
        // here; anything else is not an AtomType.
        fail(MathError::MALFORMED);
        break;
    }
    if (failed())
        return;
    if (item.sup.empty() && item.sub.empty())
        return;
    make_scripts(base, item, delta, style);
}

// An explicit space as glue, its math units scaled by cur_mu (sections 716 and
// 732). tex.web has a kern form as well (math_kern, section 717); one form is
// enough, since a spec with no stretch or shrink sets exactly as a kern would
// in a box packed to its natural width.
Node Converter::math_space(const MathItem& atom, Style style)
{
    if (atom.nucleus.type != FieldType::MU_GLUE) {
        fail(MathError::MALFORMED);
        return glue(GlueSpec());
    }
    return glue(from_mu(atom.nucleus.mu(), _fonts.mu(style)));
}

// Gives each LEFT and RIGHT of the level starting at `_translated[first]` its
// delimiter, sized to the list it stands in.
//
// tex.web does this in the second pass, once max_h and max_d are known
// (sections 760 and 761); every atom has been translated by the time this
// runs, so it is the same moment. max_h and max_d are what section 727
// gathers at its label check_dimensions: each noad's hlist, packed, and
// nothing else — an explicit space contributes no height, and the boundaries
// themselves have no hlist yet. Both delimiters are sized from the same two
// numbers at the same style, so they are consistent (section 762).
void Converter::size_boundaries(size_t first, Style style)
{
    Scaled max_h = 0;
    Scaled max_d = 0;
    for (size_t i = first; i < _translated.size(); ++i) {
        const Translated& translated = _translated[i];
        if (translated.kind < 0 || translated.boundary != nullptr)
            continue;
        const Box packed =
            hpack_view(NodeView(_nodes.data() + translated.first, translated.count)).box;
        max_h = std::max(max_h, packed.height);
        max_d = std::max(max_d, packed.depth);
    }
    for (size_t i = first; i < _translated.size() && !failed(); ++i) {
        const MathItem* atom = _translated[i].boundary;
        if (atom == nullptr)
            continue;
        const Box box = make_left_right(*atom, style, max_d, max_h);
        if (failed())
            return;
        _nodes[_translated[i].first] = node_of(box);
    }
}

// The space between two adjacent atoms (section 766, which has no name for
// it). False for none at all. The three amounts are plain.tex's thin, medium
// and thick math skips; tex.web reads them from parameters (thin_mu_skip and
// its neighbours, section 224).
bool Converter::inter_atom_glue(AtomType left, AtomType right, Style style, GlueSpec* spec)
{
    const int row = spacing_class(left);
    const int column = spacing_class(right);
    if (row < 0 || column < 0)
        return false;
    const char code = MATH_SPACING[8 * row + column];
    // An impossible pair: the first pass's BIN conversion prevents it.
    if (!IMTYPESET_CONTRACT(code != '*', "mlist_to_hlist: an impossible pair of atoms"))
        return false;
    if (code == '0')
        return false;
    // A conditional space is suppressed in script styles.
    const bool conditional = code == '1' || code == '3' || code == '4';
    if (conditional && static_cast<int>(style) >= static_cast<int>(Style::SCRIPT))
        return false;
    const MuGlue& mu_glue = code == '3' ? MED_MU : (code == '4' ? THICK_MU : THIN_MU);
    *spec = from_mu(mu_glue, _fonts.mu(style));
    return true;
}

void Converter::mlist_to_hlist(MathView items, Style style)
{
    if (failed())
        return;
    if (_depth >= MAX_MATH_NESTING) {
        fail(MathError::TOO_DEEP);
        return;
    }
    ++_depth;
    const size_t node_base = _nodes.size();
    const size_t level = _translated.size();

    // The first pass (sections 726 to 729): translate each atom, settling
    // whether each BIN is really binary (section 728).
    //
    // `previous` is tex.web's r_type, which starts as op_noad: that is what
    // makes a leading BIN become ORD, there being nothing to its left to bind.
    // `last` is r, the most recent noad: an explicit space is a node and not a
    // noad, so it neither becomes r nor changes r_type, and a BIN binds
    // straight through it.
    //
    // A boundary is OPEN or CLOSE from the start. tex.web keeps left_noad and
    // right_noad apart until the second pass but lists them beside open_noad
    // and close_noad in both of section 728's tests, so nothing in the first
    // pass can tell the difference.
    AtomType previous = AtomType::OP;
    bool have_last = false;
    size_t last = 0;
    bool have_boundary = false;
    for (const MathItem& item : items) {
        if (failed())
            break;
        if (++_items > MAX_MATH_ITEMS) {
            fail(MathError::TOO_LARGE);
            break;
        }
        if (!item.is_fraction && item.kind == AtomType::SPACE) {
            Translated space;
            space.first = _nodes.size();
            space.count = 1;
            push(math_space(item, style));
            _translated.push_back(space);
            continue;
        }
        // A fraction spaces as INNER, the reference's choice. Section 761 of
        // the tex.web this was checked against (line 14996) sets no type for a
        // fraction_noad, which leaves it spacing as an ord_noad.
        AtomType kind = AtomType::INNER;
        if (!item.is_fraction) {
            kind = item.kind;
            if (kind == AtomType::LEFT)
                kind = AtomType::OPEN;
            else if (kind == AtomType::RIGHT)
                kind = AtomType::CLOSE;
        }
        if (kind == AtomType::BIN &&
            (previous == AtomType::BIN || previous == AtomType::OP || previous == AtomType::REL ||
             previous == AtomType::OPEN || previous == AtomType::PUNCT)) {
            kind = AtomType::ORD;
        } else if ((kind == AtomType::REL || kind == AtomType::CLOSE || kind == AtomType::PUNCT) &&
                   have_last && _translated[last].kind == static_cast<int>(AtomType::BIN)) {
            _translated[last].kind = static_cast<int>(AtomType::ORD);
        }
        have_last = true;
        last = _translated.size();

        Translated translated;
        translated.kind = static_cast<int>(kind);
        translated.first = _nodes.size();
        if (!item.is_fraction && is_boundary(item.kind)) {
            // Sized below, once the height and depth of the whole list are
            // known. The slot is kept so the delimiter lands in its place.
            translated.boundary = &item;
            translated.count = 1;
            push(penalty(0));
            have_boundary = true;
        } else {
            translate(item, style);
            translated.count = _nodes.size() - translated.first;
        }
        _translated.push_back(translated);
        previous = kind;
    }
    // Section 729: a list ending in a BIN has nothing for it to bind.
    if (have_last && _translated[last].kind == static_cast<int>(AtomType::BIN))
        _translated[last].kind = static_cast<int>(AtomType::ORD);
    if (have_boundary && !failed())
        size_boundaries(level, style);

    if (failed()) {
        _nodes.resize(node_base);
        _translated.resize(level);
        --_depth;
        return;
    }

    // The second pass (sections 760, 761 and 766): the table's glue between
    // the atoms. Built above the first pass's nodes and then moved down over
    // them. `left` is tex.web's r_type and `right` its t. Section 767's
    // penalties after a BIN and a REL are not made.
    const size_t out_base = _nodes.size();
    bool have_previous = false;
    AtomType left = AtomType::ORD;
    for (size_t i = level; i < _translated.size(); ++i) {
        const Translated translated = _translated[i];
        if (translated.kind >= 0) {
            const AtomType right = static_cast<AtomType>(translated.kind);
            GlueSpec spec;
            if (have_previous && inter_atom_glue(left, right, style, &spec))
                push(glue(spec));
            left = right;
            have_previous = true;
        }
        // A node that is not a noad is linked in as it stands and r_type is
        // left alone, so the atoms either side still space against each other.
        for (size_t j = 0; j < translated.count; ++j) {
            const Node node = _nodes[translated.first + j];
            push(node);
        }
    }
    _nodes.erase(_nodes.begin() + static_cast<std::ptrdiff_t>(node_base),
                 _nodes.begin() + static_cast<std::ptrdiff_t>(out_base));
    _translated.resize(level);
    --_depth;
}

} // namespace

// --- Messages -------------------------------------------------------------------------

std::string python_repr(const uint32_t* codepoints, size_t count)
{
    bool single = false;
    bool twice = false;
    for (size_t i = 0; i < count; ++i) {
        single = single || codepoints[i] == '\'';
        twice = twice || codepoints[i] == '"';
    }
    const char quote = single && !twice ? '"' : '\'';
    std::string out(1, quote);
    for (size_t i = 0; i < count; ++i) {
        const uint32_t codepoint = codepoints[i];
        if (codepoint == '\\')
            out += "\\\\";
        else if (codepoint == static_cast<uint32_t>(quote))
            out += std::string("\\") + quote;
        else if (codepoint == '\t')
            out += "\\t";
        else if (codepoint == '\n')
            out += "\\n";
        else if (codepoint == '\r')
            out += "\\r";
        else if (py_isprintable(codepoint))
            append_utf8(out, codepoint);
        else if (codepoint < 0x100)
            out += formatted("\\x%02x", codepoint);
        else if (codepoint < 0x10000)
            out += formatted("\\u%04x", codepoint);
        else
            out += formatted("\\U%08x", codepoint);
    }
    out += quote;
    return out;
}

std::string python_char_repr(uint32_t codepoint)
{
    return python_repr(&codepoint, 1);
}

Scaled dimension_outside_max_dimen(const Box& box)
{
    for (const Scaled dimension : {box.width, box.height, box.depth, box.shift_amount}) {
        if (dimension > MAX_DIMEN || dimension < -MAX_DIMEN)
            return dimension;
    }
    return 0;
}

std::string math_error_message(const MathStatus& status)
{
    switch (status.error) {
    case MathError::NONE:
        return std::string();
    case MathError::DELIMITER_TOO_SMALL:
        return formatted("U+%04X has no variant %" PRId64 " sp tall (the largest is %" PRId64
                         " sp), and delimiters are not assembled from pieces here",
                         status.codepoint, status.needed, status.largest);
    case MathError::MISSING_GLYPH:
        return std::string(face_value(status.face)) + " has no glyph for " +
               python_char_repr(status.codepoint) +
               formatted(" (U+%04X), and a formula is not patched from another face",
                         status.codepoint);
    case MathError::TEXT_FACE_UNAVAILABLE:
        return "the formula holds prose and these fonts cannot measure prose";
    case MathError::NOT_IMPLEMENTED:
        return "ACCENT atoms are not supported: an accent walks a width-fitting chain, which "
               "is not implemented";
    case MathError::MALFORMED:
        return "the math list is malformed: a field is where it cannot be, or names nothing";
    case MathError::TOO_DEEP:
        return "the math list nests too deeply";
    case MathError::TOO_LARGE:
        if (status.largest > 0 && status.inside)
            return "a space or a rule inside it is larger than the largest dimension there is";
        if (status.largest > 0)
            return "its box is wider or taller than the largest dimension a box can have";
        return "the math list is too large";
    }
    return std::string();
}

// --- Fonts ----------------------------------------------------------------------------

MathResolver fontlib_math_metrics(FontLibrary& library)
{
    return MathResolver{&library_math_resolver, &library};
}

MathFonts::MathFonts()
    : _cache(MetricsResolver{&MathFonts::metrics_for, this})
{
}

bool MathFonts::init(Scaled base_size, MathResolver resolve, MetricsResolver text, std::string* why)
{
    _ready = false;
    _unmeasured = false;
    _text = text;
    _last_text_key = ~uint64_t(0);
    _last_text_metrics = nullptr;
    _cache.clear();
    for (int i = 0; i < MATH_SIZE_COUNT; ++i) {
        _sizes[i] = 0;
        _faces[i] = nullptr;
        _keys[i] = FontKey();
    }

    if (base_size <= 0 || base_size > MAX_DIMEN) {
        if (why != nullptr)
            *why = "the base size of a formula must be positive and no more than MAX_DIMEN";
        return false;
    }
    const MathFontMetrics* text_face = resolve(base_size);
    if (text_face == nullptr) {
        if (why != nullptr)
            *why = "there is no math face at the base size";
        return false;
    }
    const MathParams& scales = text_face->math();
    const double base = static_cast<double>(base_size);
    const Scaled sizes[MATH_SIZE_COUNT] = {
        base_size,
        round_to_scaled(base * scales.script_scale),
        round_to_scaled(base * scales.script_script_scale),
    };
    for (int i = 0; i < MATH_SIZE_COUNT; ++i) {
        if (sizes[i] <= 0 || sizes[i] > MAX_DIMEN) {
            if (why != nullptr)
                *why = "the math face's script percentages do not give a usable size";
            return false;
        }
        const MathFontMetrics* face = i == 0 ? text_face : resolve(sizes[i]);
        if (face == nullptr) {
            if (why != nullptr)
                *why = "there is no math face at a script size";
            return false;
        }
        _sizes[i] = sizes[i];
        _faces[i] = face;
        _keys[i] = font_key(Face::MATH, sizes[i]);
    }
    _ready = true;
    return true;
}

// Section 703's cur_mu. The section divides with x_over_n (section 106),
// which truncates toward zero, a quad that is a whole number of scaled points:
// that is TexRules::mu_truncates, the quad being rounded as every font
// parameter is. Otherwise this is the reference's round(), half to even, of
// the unrounded quad over eighteen.
Scaled MathFonts::mu(Style style) const
{
    if (rules.mu_truncates)
        return round_to_scaled(params(style).math_quad) / 18;
    return round_to_scaled(params(style).math_quad / 18.0);
}

const FontMetrics* MathFonts::resolve_key(FontKey key)
{
    // The later size wins where two sizes share a key, as in the reference's
    // table; the resolver gave the same face for both.
    for (int i = MATH_SIZE_COUNT - 1; i >= 0; --i) {
        if (_faces[i] != nullptr && key == _keys[i])
            return _faces[i];
    }
    if (key.bits != _last_text_key) {
        _last_text_key = key.bits;
        _last_text_metrics = _text(key);
    }
    return _last_text_metrics;
}

const FontMetrics* MathFonts::metrics_for(void* user, FontKey key)
{
    return static_cast<MathFonts*>(user)->resolve_key(key);
}

CharDimens MathFonts::measure_thunk(void* user, FontKey font, uint32_t codepoint)
{
    return static_cast<MathFonts*>(user)->measure(font, codepoint);
}

CharDimens MathFonts::measure(FontKey font, uint32_t codepoint)
{
    if (resolve_key(font) == nullptr) {
        _unmeasured = true;
        return CharDimens();
    }
    return _cache(font, codepoint);
}

CharMeasure MathFonts::measure()
{
    return CharMeasure{&MathFonts::measure_thunk, this};
}

// --- The conversion ------------------------------------------------------------------

MathStatus mlist_to_hlist(const MathList& list, MathView items, Style style, MathFonts& fonts,
                          NodeArena& arena, std::vector<Node>& out)
{
    MathStatus status;
    if (!fonts.ready() || static_cast<int>(style) >= STYLE_COUNT) {
        status.error = MathError::MALFORMED;
        return status;
    }
    fonts.clear_unmeasured();
    const NodeArena::Mark mark = arena.mark();
    Converter converter(list, fonts, arena);
    converter.mlist_to_hlist(items, style);
    converter.check_measure();
    status = converter.status();
    if (!status.ok()) {
        arena.rewind(mark);
        return status;
    }
    out.insert(out.end(), converter.nodes().begin(), converter.nodes().end());
    return status;
}

MathStatus mlist_to_hlist(const MathList& list, MathSpan items, Style style, MathFonts& fonts,
                          NodeArena& arena, std::vector<Node>& out)
{
    MathView view;
    if (!list.view(items, &view)) {
        MathStatus status;
        status.error = MathError::MALFORMED;
        return status;
    }
    return mlist_to_hlist(list, view, style, fonts, arena, out);
}

} // namespace ImTypeset::detail
