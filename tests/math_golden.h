// Reader for the math records of tools/oracle/FORMAT-math.md, over
// core_golden.h: math lists, the `fonts` a case names, and the `q_*` records
// that hold every answer the reference's fonts gave during one conversion.
//
// Three ways to give a case its fonts, each a `Fonts`:
//
//   RecordedFonts  faces that know only the case's own q_* records. A question
//                  they have no answer for is one the reference did not ask;
//                  it is counted (`misses`) and answered with nothing.
//   FakeFonts      the reference tests' fakes, restated from FORMAT-math.md
//                  section 4.3: `fake`, `chained`, and the prose fake.
//   LibraryFonts   a FontLibrary over tests/data/fonts: the port's own reader.
//
// The math tests, the formula-parser tests and the public-API tests all read
// their goldens through this.

#pragma once

#include "core_golden.h"

#include "fontlib.h"
#include "fontmetrics.h"
#include "math_atoms.h"
#include "math_mlist.h"
#include "shipout.h"
#include "style.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace math_golden
{

using ImTypeset::detail::AtomType;
using ImTypeset::detail::Face;
using ImTypeset::detail::Field;
using ImTypeset::detail::FontKey;
using ImTypeset::detail::FontLibrary;
using ImTypeset::detail::FontMetrics;
using ImTypeset::detail::GlyphBBox;
using ImTypeset::detail::GlyphVariant;
using ImTypeset::detail::GlyphVariants;
using ImTypeset::detail::Limits;
using ImTypeset::detail::MathError;
using ImTypeset::detail::MathFontMetrics;
using ImTypeset::detail::MathFonts;
using ImTypeset::detail::MathItem;
using ImTypeset::detail::MathList;
using ImTypeset::detail::MathParams;
using ImTypeset::detail::MathResolver;
using ImTypeset::detail::MathSize;
using ImTypeset::detail::MetricsResolver;
using ImTypeset::detail::MuGlue;
using ImTypeset::detail::Node;
using ImTypeset::detail::Scaled;
using ImTypeset::detail::Style;

// --- Tokens -----------------------------------------------------------------------

inline constexpr const char* const STYLE_TOKENS[] = {
    "display", "display_cramped", "text",          "text_cramped",
    "script",  "script_cramped",  "script_script", "script_script_cramped"};
inline constexpr const char* const MATH_SIZE_TOKENS[] = {"text", "script", "script_script"};
inline constexpr const char* const ATOM_TYPE_TOKENS[] = {
    "ord",  "op",    "bin",     "rel",     "open",   "close", "punct", "inner",
    "over", "under", "vcenter", "radical", "accent", "left",  "right", "space"};
inline constexpr const char* const LIMITS_TOKENS[] = {"default", "limits", "no_limits"};

inline Style style(std::string_view token)
{
    return static_cast<Style>(core_golden::token_index(token, STYLE_TOKENS, "Style"));
}
inline const char* token(Style v)
{
    return STYLE_TOKENS[static_cast<int>(v) & 7];
}
inline MathSize math_size(std::string_view token)
{
    return static_cast<MathSize>(16 *
                                 core_golden::token_index(token, MATH_SIZE_TOKENS, "MathSize"));
}
inline const char* token(MathSize v)
{
    return MATH_SIZE_TOKENS[ImTypeset::detail::math_size_index(v) % 3];
}
inline AtomType atom_type(std::string_view token)
{
    return static_cast<AtomType>(core_golden::token_index(token, ATOM_TYPE_TOKENS, "AtomType"));
}
inline const char* token(AtomType v)
{
    return ATOM_TYPE_TOKENS[static_cast<int>(v) & 15];
}
inline Limits limits(std::string_view token)
{
    return static_cast<Limits>(core_golden::token_index(token, LIMITS_TOKENS, "Limits"));
}

// The reference's slug for an error (FORMAT-math.md section 4.1). MALFORMED is
// its TypeError; TOO_DEEP and TOO_LARGE have no counterpart there.
inline const char* slug(MathError error)
{
    switch (error) {
    case MathError::NONE:
        return "none";
    case MathError::DELIMITER_TOO_SMALL:
        return "delimiter-too-small";
    case MathError::MISSING_GLYPH:
        return "missing-glyph";
    case MathError::TEXT_FACE_UNAVAILABLE:
        return "text-face-unavailable";
    case MathError::NOT_IMPLEMENTED:
        return "not-implemented";
    case MathError::MALFORMED:
        return "malformed-field";
    case MathError::TOO_DEEP:
        return "too-deep";
    case MathError::TOO_LARGE:
        return "too-large";
    }
    return "?";
}

// --- Math lists -------------------------------------------------------------------

inline std::vector<MathItem> read_items(core_golden::Reader& reader, core_golden::Context& context,
                                        MathList& list, uint32_t count, int depth);

// One field record, with what it brackets. A SUB_BOX's box is read into
// `context.arena`, which is therefore the arena the list must be converted in.
inline Field read_field(core_golden::Reader& reader, core_golden::Context& context, MathList& list,
                        int depth)
{
    const golden::Record& record = reader.take();
    golden::Cursor cursor(record);
    if (record.is("empty")) {
        cursor.expect_done();
        return Field();
    }
    if (record.is("mathchar")) {
        const Field field = Field::math_char(cursor.u32());
        cursor.expect_done();
        return field;
    }
    if (record.is("mathtext")) {
        Face face = Face::PROSE;
        const std::string face_name = cursor.str();
        if (!ImTypeset::detail::face_from_value(face_name, &face))
            record.error(0, "not a Face value");
        const std::string text = cursor.str();
        cursor.expect_done();
        return list.text(text, face);
    }
    if (record.is("muglue")) {
        MuGlue mu;
        mu.width = cursor.i64();
        mu.stretch = cursor.i64();
        mu.shrink = cursor.i64();
        cursor.expect_done();
        return Field::mu_glue(mu);
    }
    if (record.is("subbox")) {
        cursor.expect_done();
        if (!reader.next_is("box")) {
            check::fail(record.file, record.line, "`subbox` is not followed by a box");
            return Field();
        }
        return Field::sub_box(core_golden::read_node(reader, context));
    }
    if (record.is("sublist")) {
        const uint32_t count = cursor.u32();
        cursor.expect_done();
        const std::vector<MathItem> items = read_items(reader, context, list, count, depth + 1);
        reader.take("endsublist");
        return list.sub_list(items);
    }
    check::fail(record.file, record.line,
                check::format("`%.*s` is not a field record", static_cast<int>(record.tag.size()),
                              record.tag.data()));
    return Field();
}

inline std::vector<MathItem> read_items(core_golden::Reader& reader, core_golden::Context& context,
                                        MathList& list, uint32_t count, int depth)
{
    std::vector<MathItem> items;
    if (depth > 4096) {
        check::fail(__FILE__, __LINE__, "math list records nest implausibly deep");
        return items;
    }
    for (uint32_t i = 0; i < count && !reader.done(); ++i) {
        const golden::Record& record = reader.take();
        golden::Cursor cursor(record);
        if (record.is("atom")) {
            const AtomType kind = atom_type(cursor.raw());
            const Limits where = limits(cursor.raw());
            cursor.expect_done();
            const Field nucleus = read_field(reader, context, list, depth);
            const Field sup = read_field(reader, context, list, depth);
            const Field sub = read_field(reader, context, list, depth);
            items.push_back(MathItem::atom(kind, nucleus, sup, sub, where));
        } else if (record.is("fraction")) {
            const bool has_thickness = cursor.boolean();
            const Scaled thickness = cursor.i64();
            cursor.expect_done();
            const Field numerator = read_field(reader, context, list, depth);
            const Field denominator = read_field(reader, context, list, depth);
            items.push_back(has_thickness ? MathItem::fraction(numerator, denominator, thickness)
                                          : MathItem::fraction(numerator, denominator));
        } else {
            check::fail(record.file, record.line, "expected an `atom` or a `fraction` record");
            break;
        }
    }
    return items;
}

// `mlist <n>` ... `endmlist`: the top-level items, with every sublist and all
// prose in `list`.
inline std::vector<MathItem> read_mlist(core_golden::Reader& reader, core_golden::Context& context,
                                        MathList& list)
{
    const uint32_t count = reader.take("mlist").u32(0);
    std::vector<MathItem> items = read_items(reader, context, list, count, 0);
    reader.take("endmlist");
    return items;
}

// A string as the oracle writes one: golden.h's percent-encoding, and '#' as
// well, which the oracle encodes so that no record can begin a comment.
inline std::string q(std::string_view text)
{
    if (text.empty())
        return "%";
    std::string out;
    for (const char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c <= 0x20 || c >= 0x7f || c == '%' || c == '#')
            out += check::format("%%%02X", static_cast<unsigned>(c));
        else
            out += ch;
    }
    return out;
}

inline void write_items(const core_golden::Context& context, const MathList& list,
                        ImTypeset::detail::MathView items, std::vector<std::string>& out,
                        int depth);

// Appends the record lines of one field, as the oracle writes them. A span that
// names nothing is written as a line no golden holds.
inline void write_field(const core_golden::Context& context, const MathList& list,
                        const Field& field, std::vector<std::string>& out, int depth)
{
    using ImTypeset::detail::FieldType;
    switch (field.type) {
    case FieldType::EMPTY:
        out.push_back("empty");
        return;
    case FieldType::MATH_CHAR:
        out.push_back(check::format("mathchar %u", field.codepoint));
        return;
    case FieldType::MATH_TEXT: {
        std::string_view text;
        if (!list.text_of(field.span, &text)) {
            out.push_back("<mathtext outside the list>");
            return;
        }
        out.push_back("mathtext " + q(ImTypeset::detail::face_value(field.face)) + " " + q(text));
        return;
    }
    case FieldType::MU_GLUE: {
        const MuGlue mu = field.mu();
        out.push_back(check::format("muglue %" PRId64 " %" PRId64 " %" PRId64, mu.width, mu.stretch,
                                    mu.shrink));
        return;
    }
    case FieldType::SUB_BOX: {
        out.push_back("subbox");
        if (field.box() >= context.arena.box_count()) {
            out.push_back("<box outside the arena>");
            return;
        }
        Node node;
        node.type = ImTypeset::detail::NodeType::BOX;
        node.u = field.box();
        core_golden::write_node(context, node, out);
        return;
    }
    case FieldType::SUB_LIST: {
        ImTypeset::detail::MathView items;
        if (!list.view(field.span, &items) || depth > 4096) {
            out.push_back("<sublist outside the list>");
            return;
        }
        out.push_back(check::format("sublist %zu", items.size));
        write_items(context, list, items, out, depth + 1);
        out.push_back("endsublist");
        return;
    }
    }
    out.push_back("<not a field>");
}

inline void write_items(const core_golden::Context& context, const MathList& list,
                        ImTypeset::detail::MathView items, std::vector<std::string>& out, int depth)
{
    for (const MathItem& item : items) {
        if (item.is_fraction) {
            out.push_back(check::format("fraction %d %" PRId64, item.has_thickness ? 1 : 0,
                                        item.has_thickness ? item.thickness : Scaled(0)));
            write_field(context, list, item.numerator(), out, depth);
            write_field(context, list, item.denominator(), out, depth);
        } else {
            out.push_back(check::format("atom %s %s", token(item.kind),
                                        LIMITS_TOKENS[static_cast<int>(item.limits) % 3]));
            write_field(context, list, item.nucleus, out, depth);
            write_field(context, list, item.sup, out, depth);
            write_field(context, list, item.sub, out, depth);
        }
    }
}

// `mlist <n>` ... `endmlist` for a list, as the oracle writes it.
inline std::vector<std::string> mlist_lines(const core_golden::Context& context,
                                            const MathList& list, ImTypeset::detail::MathView items)
{
    std::vector<std::string> out;
    out.push_back(check::format("mlist %zu", items.size));
    write_items(context, list, items, out, 0);
    out.push_back("endmlist");
    return out;
}

// --- Parameters ---------------------------------------------------------------------

// The 27 values of a `params` record, in the order of style.golden's
// `param_fields`, starting at the cursor.
inline MathParams read_params_fields(golden::Cursor& cursor)
{
    MathParams p;
    double* const fields[] = {
        &p.math_x_height,
        &p.math_quad,
        &p.num1,
        &p.num2,
        &p.num3,
        &p.denom1,
        &p.denom2,
        &p.sup1,
        &p.sup2,
        &p.sup3,
        &p.sub1,
        &p.sub2,
        &p.sup_drop,
        &p.sub_drop,
        &p.delim1,
        &p.delim2,
        &p.axis_height,
        &p.default_rule_thickness,
        &p.big_op_spacing1,
        &p.big_op_spacing2,
        &p.big_op_spacing3,
        &p.big_op_spacing4,
        &p.big_op_spacing5,
        &p.radical_rule_thickness,
        &p.radical_vertical_gap,
        &p.radical_display_style_vertical_gap,
        &p.script_scale,
        &p.script_script_scale,
        &p.display_operator_min_height,
    };
    for (double* field : fields)
        *field = cursor.f64();
    return p;
}

inline constexpr const char* const PARAM_FIELDS[] = {
    "math_x_height",
    "math_quad",
    "num1",
    "num2",
    "num3",
    "denom1",
    "denom2",
    "sup1",
    "sup2",
    "sup3",
    "sub1",
    "sub2",
    "sup_drop",
    "sub_drop",
    "delim1",
    "delim2",
    "axis_height",
    "default_rule_thickness",
    "big_op_spacing1",
    "big_op_spacing2",
    "big_op_spacing3",
    "big_op_spacing4",
    "big_op_spacing5",
    "radical_rule_thickness",
    "radical_vertical_gap",
    "radical_display_style_vertical_gap",
    "script_scale",
    "script_script_scale",
    "display_operator_min_height",
};

// Whether two parameter sets are the same to the last bit.
inline bool same_params(const MathParams& a, const MathParams& b)
{
    return a.math_x_height == b.math_x_height && a.math_quad == b.math_quad && a.num1 == b.num1 &&
           a.num2 == b.num2 && a.num3 == b.num3 && a.denom1 == b.denom1 && a.denom2 == b.denom2 &&
           a.sup1 == b.sup1 && a.sup2 == b.sup2 && a.sup3 == b.sup3 && a.sub1 == b.sub1 &&
           a.sub2 == b.sub2 && a.sup_drop == b.sup_drop && a.sub_drop == b.sub_drop &&
           a.delim1 == b.delim1 && a.delim2 == b.delim2 && a.axis_height == b.axis_height &&
           a.default_rule_thickness == b.default_rule_thickness &&
           a.big_op_spacing1 == b.big_op_spacing1 && a.big_op_spacing2 == b.big_op_spacing2 &&
           a.big_op_spacing3 == b.big_op_spacing3 && a.big_op_spacing4 == b.big_op_spacing4 &&
           a.big_op_spacing5 == b.big_op_spacing5 &&
           a.radical_rule_thickness == b.radical_rule_thickness &&
           a.radical_vertical_gap == b.radical_vertical_gap &&
           a.radical_display_style_vertical_gap == b.radical_display_style_vertical_gap &&
           a.script_scale == b.script_scale && a.script_script_scale == b.script_script_scale &&
           a.display_operator_min_height == b.display_operator_min_height;
}

// The `font-params` case that opens mlist.golden and formulas.golden:
// MathParams for every key a `q_params` in the file names.
struct ParamsTable {
    std::map<uint64_t, MathParams> by_key;

    void read(const golden::File& file, const core_golden::Case& c, core_golden::Context& context)
    {
        core_golden::Reader reader(file, c);
        while (!reader.done()) {
            const golden::Record& record = reader.take("params");
            golden::Cursor cursor(record);
            const FontKey key = context.faces.key(cursor.str());
            by_key[key.bits] = read_params_fields(cursor);
            cursor.expect_done();
        }
    }

    const MathParams* find(FontKey key) const
    {
        const auto found = by_key.find(key.bits);
        return found == by_key.end() ? nullptr : &found->second;
    }
};

// --- Fonts --------------------------------------------------------------------------

// What a case's conversion is handed: the math face by size, and how a key
// that is not a math key resolves (empty for `none`).
class Fonts
{
public:
    virtual ~Fonts() = default;
    virtual MathResolver math() = 0;
    virtual MetricsResolver text() = 0;
};

// One face that knows only what one case recorded.
class RecordedFace : public MathFontMetrics
{
public:
    RecordedFace(FontKey key, std::string name, long* misses, std::string* first_miss)
        : _key(key)
        , _name(std::move(name))
        , _misses(misses)
        , _first_miss(first_miss)
    {
    }

    Face face() const override { return static_cast<Face>(_key.face()); }
    double size() const override { return miss("size"); }
    double ascent() const override { return miss("ascent"); }
    double descent() const override { return miss("descent"); }
    double x_height() const override { return miss("x_height"); }
    double em() const override { return miss("em"); }

    double space_width() const override { return _has_space ? _space : miss("space_width"); }

    bool has(uint32_t codepoint) const override
    {
        const auto found = _has.find(codepoint);
        if (found == _has.end()) {
            miss("has", codepoint);
            return false;
        }
        return found->second;
    }

    double advance(uint32_t codepoint) const override
    {
        const auto found = _advance.find(codepoint);
        return found == _advance.end() ? miss("advance", codepoint) : found->second;
    }

    GlyphBBox bbox(uint32_t codepoint) const override
    {
        const auto found = _bbox.find(codepoint);
        if (found == _bbox.end()) {
            miss("bbox", codepoint);
            return GlyphBBox();
        }
        return found->second;
    }

    double kern(uint32_t codepoint, uint32_t following) const override
    {
        const auto found = _kern.find(std::make_pair(codepoint, following));
        return found == _kern.end() ? miss("kern", codepoint, following) : found->second;
    }

    const MathParams& math() const override
    {
        if (_params == nullptr) {
            miss("math");
            return _no_params;
        }
        return *_params;
    }

    double italic_correction(uint32_t codepoint) const override
    {
        const auto found = _italic.find(codepoint);
        return found == _italic.end() ? miss("italic_correction", codepoint) : found->second;
    }

    GlyphVariants vertical_variants(uint32_t codepoint) const override
    {
        const auto found = _variants.find(codepoint);
        if (found == _variants.end()) {
            miss("vertical_variants", codepoint);
            return GlyphVariants();
        }
        return GlyphVariants{found->second.data(), found->second.size()};
    }

    // The tables, filled by RecordedFonts::read.
    bool _has_space = false;
    double _space = 0.0;
    const MathParams* _params = nullptr;
    std::map<uint32_t, bool> _has;
    std::map<uint32_t, double> _advance;
    std::map<uint32_t, GlyphBBox> _bbox;
    std::map<uint32_t, double> _italic;
    std::map<std::pair<uint32_t, uint32_t>, double> _kern;
    std::map<uint32_t, std::vector<GlyphVariant>> _variants;

private:
    double miss(const char* what, uint32_t a = 0, uint32_t b = 0) const
    {
        if ((*_misses)++ == 0)
            *_first_miss = check::format("%s asked for %s(%u, %u)", _name.c_str(), what, a, b);
        return 0.0;
    }

    FontKey _key;
    std::string _name;
    long* _misses;
    std::string* _first_miss;
    MathParams _no_params;
};

class RecordedFonts : public Fonts
{
public:
    // `text_mode` is the case's `fonts` text token: none, real, fake, broken.
    explicit RecordedFonts(std::string text_mode)
        : _text_mode(std::move(text_mode))
    {
    }

    // Consumes the run of q_* records at the reader's position.
    void read(core_golden::Reader& reader, core_golden::Context& context, const ParamsTable& params)
    {
        _faces_table = &context.faces;
        while (!reader.done() && reader.peek().tag.substr(0, 2) == "q_") {
            const golden::Record& record = reader.take();
            golden::Cursor cursor(record);
            const FontKey key = context.faces.key(cursor.str());
            RecordedFace& face = this->face(key);
            if (record.is("q_params")) {
                face._params = params.find(key);
                if (face._params == nullptr)
                    record.error(0, "no `params` record for this key in the font-params case");
            } else if (record.is("q_space")) {
                face._has_space = true;
                face._space = cursor.f64();
            } else if (record.is("q_has")) {
                const uint32_t codepoint = cursor.u32();
                face._has[codepoint] = cursor.boolean();
            } else if (record.is("q_advance")) {
                const uint32_t codepoint = cursor.u32();
                face._advance[codepoint] = cursor.f64();
            } else if (record.is("q_bbox")) {
                const uint32_t codepoint = cursor.u32();
                GlyphBBox box;
                box.x_min = cursor.f64();
                box.y_min = cursor.f64();
                box.x_max = cursor.f64();
                box.y_max = cursor.f64();
                face._bbox[codepoint] = box;
            } else if (record.is("q_italic")) {
                const uint32_t codepoint = cursor.u32();
                face._italic[codepoint] = cursor.f64();
            } else if (record.is("q_kern")) {
                const uint32_t codepoint = cursor.u32();
                const uint32_t following = cursor.u32();
                face._kern[std::make_pair(codepoint, following)] = cursor.f64();
            } else if (record.is("q_variants")) {
                const uint32_t codepoint = cursor.u32();
                const uint32_t count = cursor.u32();
                std::vector<GlyphVariant> chain;
                for (uint32_t i = 0; i < count && !cursor.done(); ++i) {
                    GlyphVariant variant;
                    (void)cursor.str(); // the glyph's name
                    variant.codepoint = cursor.u32();
                    variant.extent = cursor.f64();
                    chain.push_back(variant);
                }
                face._variants[codepoint] = std::move(chain);
            } else {
                record.error(0, "not a q_* record this reader knows");
                continue;
            }
            cursor.expect_done();
        }
    }

    MathResolver math() override { return MathResolver{&RecordedFonts::resolve_math, this}; }

    MetricsResolver text() override
    {
        if (_text_mode == "none")
            return MetricsResolver();
        return MetricsResolver{&RecordedFonts::resolve_text, this};
    }

    // How many questions the port asked that the reference did not, and the
    // first of them.
    long misses = 0;
    std::string first_miss;

private:
    RecordedFace& face(FontKey key)
    {
        std::unique_ptr<RecordedFace>& slot = _faces[key.bits];
        if (!slot) {
            std::string name = _faces_table != nullptr ? _faces_table->text(key) : std::string("?");
            slot = std::make_unique<RecordedFace>(key, std::move(name), &misses, &first_miss);
        }
        return *slot;
    }

    static const MathFontMetrics* resolve_math(void* user, Scaled size)
    {
        RecordedFonts& self = *static_cast<RecordedFonts*>(user);
        return &self.face(ImTypeset::detail::font_key(Face::MATH, size));
    }

    static const FontMetrics* resolve_text(void* user, FontKey key)
    {
        RecordedFonts& self = *static_cast<RecordedFonts*>(user);
        if (self._text_mode == "broken")
            return nullptr;
        return &self.face(key);
    }

    std::string _text_mode;
    const core_golden::FaceTable* _faces_table = nullptr;
    std::map<uint64_t, std::unique_ptr<RecordedFace>> _faces;
};

// --- The reference tests' fakes (FORMAT-math.md section 4.3) ------------------------

inline MathParams fake_params(int64_t n)
{
    using ImTypeset::detail::floor_div;
    MathParams p;
    p.math_x_height = static_cast<double>(floor_div(n, 2));
    p.math_quad = static_cast<double>(n);
    p.num1 = static_cast<double>(floor_div(n * 7, 10));
    p.num2 = static_cast<double>(floor_div(n * 4, 10));
    p.num3 = static_cast<double>(floor_div(n * 45, 100));
    p.denom1 = static_cast<double>(floor_div(n * 7, 10));
    p.denom2 = static_cast<double>(floor_div(n * 35, 100));
    p.sup1 = static_cast<double>(floor_div(n * 4, 10));
    p.sup2 = static_cast<double>(floor_div(n * 36, 100));
    p.sup3 = static_cast<double>(floor_div(n * 29, 100));
    p.sub1 = static_cast<double>(floor_div(n * 15, 100));
    p.sub2 = static_cast<double>(floor_div(n * 25, 100));
    p.sup_drop = static_cast<double>(floor_div(n * 25, 100));
    p.sub_drop = static_cast<double>(floor_div(n * 20, 100));
    p.delim1 = static_cast<double>(floor_div(n * 24, 10));
    p.delim2 = static_cast<double>(n);
    p.axis_height = static_cast<double>(floor_div(n, 4));
    p.default_rule_thickness = static_cast<double>(floor_div(n, 20));
    p.big_op_spacing1 = static_cast<double>(floor_div(n, 5));
    p.big_op_spacing2 = static_cast<double>(floor_div(n, 6));
    p.big_op_spacing3 = static_cast<double>(floor_div(n, 9));
    p.big_op_spacing4 = static_cast<double>(floor_div(n * 6, 10));
    p.big_op_spacing5 = static_cast<double>(floor_div(n, 10));
    p.radical_rule_thickness = static_cast<double>(floor_div(n, 20));
    p.radical_vertical_gap = static_cast<double>(floor_div(n, 20) + floor_div(floor_div(n, 20), 4));
    p.radical_display_style_vertical_gap =
        static_cast<double>(floor_div(n, 20) + floor_div(floor_div(n, 2), 4));
    p.script_scale = 0.7;
    p.script_script_scale = 0.5;
    p.display_operator_min_height = static_cast<double>(floor_div(n * 13, 10));
    return p;
}

// `fake`, and `chained` when `chained` is set: the fake with variant chains for
// the parentheses and the surd, a tall bracket, and two slanted letters.
class FakeMath : public MathFontMetrics
{
public:
    FakeMath(double size, bool chained)
        : _size(size)
        , _chained(chained)
        , _params(fake_params(static_cast<int64_t>(size)))
    {
        if (!chained)
            return;
        static const uint32_t chains[3][4] = {
            {0x28, 0xE001, 0xE002, 0xE003},
            {0x29, 0xE011, 0xE012, 0xE013},
            {0x221A, 0xE021, 0xE022, 0xE023},
        };
        for (const auto& chain : chains) {
            std::vector<GlyphVariant> links;
            for (int i = 0; i < 4; ++i) {
                GlyphVariant variant;
                variant.codepoint = chain[i];
                variant.extent = (SHAPES[i][0] + SHAPES[i][1]) * _size;
                links.push_back(variant);
                _shape[chain[i]] = i;
            }
            _chains[chain[0]] = std::move(links);
        }
    }

    Face face() const override { return Face::MATH; }
    double size() const override { return _size; }
    double ascent() const override { return _size * 0.8; }
    double descent() const override { return _size * 0.2; }
    double x_height() const override { return _size / 2; }
    double em() const override { return _size; }
    double space_width() const override { return _size / 3; }
    bool has(uint32_t) const override { return true; }
    double advance(uint32_t) const override { return _size / 2; }
    double kern(uint32_t, uint32_t) const override { return 0.0; }
    const MathParams& math() const override { return _params; }

    GlyphBBox bbox(uint32_t codepoint) const override
    {
        GlyphBBox box;
        box.x_max = _size * 0.45;
        if (_chained && codepoint == 0x5B) {
            box.y_min = -0.0 * _size;
            box.y_max = 1.0 * _size;
            return box;
        }
        const auto shape = _shape.find(codepoint);
        if (shape != _shape.end()) {
            box.y_min = -SHAPES[shape->second][1] * _size;
            box.y_max = SHAPES[shape->second][0] * _size;
            return box;
        }
        box.y_max = _size * 0.4;
        return box;
    }

    double italic_correction(uint32_t codepoint) const override
    {
        if (!_chained)
            return 0.0;
        if (codepoint == 'f')
            return 0.1 * _size;
        if (codepoint == 'V')
            return 0.15 * _size;
        return 0.0 * _size;
    }

    GlyphVariants vertical_variants(uint32_t codepoint) const override
    {
        const auto found = _chains.find(codepoint);
        if (found == _chains.end())
            return GlyphVariants();
        return GlyphVariants{found->second.data(), found->second.size()};
    }

private:
    // (height, depth) of the four links of a chain, in ems.
    static constexpr double SHAPES[4][2] = {
        {0.75, 0.25}, {1.25, 0.75}, {1.75, 1.25}, {2.775, 2.275}};

    double _size;
    bool _chained;
    MathParams _params;
    std::map<uint32_t, int> _shape;
    std::map<uint32_t, std::vector<GlyphVariant>> _chains;
};

// The prose fake, `Latin` variant: it has every codepoint below U+0250.
class FakeProse : public FontMetrics
{
public:
    FakeProse(Face face, double size)
        : _face(face)
        , _size(size)
    {
    }

    Face face() const override { return _face; }
    double size() const override { return _size; }
    double ascent() const override { return _size * 0.8; }
    double descent() const override { return _size * 0.2; }
    double x_height() const override { return _size / 2; }
    double em() const override { return _size; }
    double space_width() const override { return _size / 4; }
    bool has(uint32_t codepoint) const override { return codepoint < 0x250; }
    double advance(uint32_t) const override { return _size / 2; }
    GlyphBBox bbox(uint32_t) const override
    {
        GlyphBBox box;
        box.y_min = -_size * 0.2;
        box.x_max = _size * 0.45;
        box.y_max = _size * 0.7;
        return box;
    }
    double kern(uint32_t codepoint, uint32_t following) const override
    {
        return codepoint == 'A' && following == 'V' ? -_size / 10 : 0.0;
    }

private:
    Face _face;
    double _size;
};

class FakeFonts : public Fonts
{
public:
    // `math_mode` is `fake` or `chained`; `text_mode` is none, fake or broken.
    FakeFonts(std::string math_mode, std::string text_mode)
        : _chained(math_mode == "chained")
        , _text_mode(std::move(text_mode))
    {
    }

    MathResolver math() override { return MathResolver{&FakeFonts::resolve_math, this}; }

    MetricsResolver text() override
    {
        if (_text_mode == "none")
            return MetricsResolver();
        return MetricsResolver{&FakeFonts::resolve_text, this};
    }

private:
    static const MathFontMetrics* resolve_math(void* user, Scaled size)
    {
        FakeFonts& self = *static_cast<FakeFonts*>(user);
        std::unique_ptr<FakeMath>& slot = self._math[size];
        if (!slot)
            slot = std::make_unique<FakeMath>(static_cast<double>(size), self._chained);
        return slot.get();
    }

    static const FontMetrics* resolve_text(void* user, FontKey key)
    {
        FakeFonts& self = *static_cast<FakeFonts*>(user);
        if (self._text_mode == "broken" || key.face() >= ImTypeset::detail::FACE_COUNT)
            return nullptr;
        std::unique_ptr<FakeProse>& slot = self._prose[key.bits];
        if (!slot) {
            slot = std::make_unique<FakeProse>(static_cast<Face>(key.face()),
                                               static_cast<double>(key.size()));
        }
        return slot.get();
    }

    bool _chained;
    std::string _text_mode;
    std::map<Scaled, std::unique_ptr<FakeMath>> _math;
    std::map<uint64_t, std::unique_ptr<FakeProse>> _prose;
};

// --- The port's own font reader ---------------------------------------------------------

// Registers the reference's default files, from tests/data/fonts, by role.
// CODE_BOLD is left to its stand-in, as the reference leaves it.
inline bool register_reference_fonts(FontLibrary& library)
{
    static const struct {
        Face face;
        const char* file;
    } defaults[] = {
        {Face::PROSE, "Roboto-Regular.ttf"},
        {Face::PROSE_BOLD, "Roboto-Bold.ttf"},
        {Face::PROSE_ITALIC, "Roboto-RegularItalic.ttf"},
        {Face::PROSE_BOLD_ITALIC, "Roboto-BoldItalic.ttf"},
        {Face::CODE, "Inconsolata-Medium.ttf"},
        {Face::MATH, "latinmodern-math.otf"},
    };
    bool ok = true;
    for (const auto& entry : defaults) {
        std::string why;
        const std::string path = golden::data_path(std::string("fonts/") + entry.file);
        if (!library.register_font(entry.face, path.c_str(), &why)) {
            check::fail(__FILE__, __LINE__, std::string(entry.file) + ": " + why);
            ok = false;
        }
    }
    return ok;
}

class LibraryFonts : public Fonts
{
public:
    explicit LibraryFonts(FontLibrary& library)
        : _library(&library)
    {
    }

    MathResolver math() override { return ImTypeset::detail::fontlib_math_metrics(*_library); }
    MetricsResolver text() override { return ImTypeset::detail::fontlib_metrics(*_library); }

private:
    FontLibrary* _library;
};

// --- Draw commands ------------------------------------------------------------------

// A shipped command as the golden reader holds one, so the port's output can
// be written as the oracle writes it.
inline core_golden::Cmd as_golden(const ImTypeset::detail::DrawCmd& cmd)
{
    using ImTypeset::detail::CmdType;
    core_golden::Cmd out;
    out.x = cmd.x;
    out.y = cmd.y;
    switch (cmd.type) {
    case CmdType::GLYPH:
        out.kind = core_golden::Cmd::GLYPH;
        out.font = cmd.font();
        out.codepoint = cmd.codepoint;
        out.index = cmd.index;
        break;
    case CmdType::RULE_RECT:
        out.kind = core_golden::Cmd::RULERECT;
        out.width = cmd.width;
        out.height = cmd.height;
        break;
    case CmdType::SET_GLUE:
    case CmdType::ATOM:
        out.kind = cmd.type == CmdType::ATOM ? core_golden::Cmd::ATOM : core_golden::Cmd::SETGLUE;
        out.width = cmd.width;
        out.natural = cmd.natural();
        out.height = cmd.height;
        out.depth = cmd.depth;
        out.index = cmd.index;
        break;
    case CmdType::LINE:
        out.kind = core_golden::Cmd::LINE;
        out.width = cmd.width;
        out.height = cmd.height;
        out.depth = cmd.depth;
        out.start = cmd.start();
        out.end = cmd.end;
        break;
    }
    return out;
}

inline std::vector<std::string> cmd_lines(const core_golden::Context& context,
                                          const std::vector<ImTypeset::detail::DrawCmd>& cmds)
{
    std::vector<core_golden::Cmd> converted;
    converted.reserve(cmds.size());
    for (const ImTypeset::detail::DrawCmd& cmd : cmds)
        converted.push_back(as_golden(cmd));
    std::vector<std::string> out;
    core_golden::write_cmds(context, converted, out);
    return out;
}

} // namespace math_golden
