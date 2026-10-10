// Reader and writer for the records of tools/oracle/FORMAT.md: nodes, glue
// specs, CharMeasure tables, pack results, line-break parameters and results,
// and ship-out commands. The tests of pack, linebreak, shipout, and the draw,
// math and selection tests that reuse the same records, read their goldens
// through this.
//
// Reading turns records into the port's own structures (src/nodes.h) or, for
// things whose types belong to modules this header must not depend on (pack
// results, break parameters, draw commands), into plain structs holding what
// the record holds, with enum fields left as the golden's tokens.
//
// Writing produces the record text again, token for token as the oracle writes
// it, including Python's float.hex() spelling. So a test has two ways to
// compare: structurally (deep_equal, field by field), or by writing what the
// port produced and comparing lines with what the golden holds, which is the
// one that tells a reader what differed. `first_difference` does the latter.
//
// Font keys. The golden's "face@size" strings become FontKeys through a
// FaceTable, which gives each face name an id. A fresh table knows the
// reference's seven faces in the order of its Face enum (prose 0, prose-bold 1,
// prose-italic 2, prose-bold-italic 3, code 4, code-bold 5, math 6) and gives
// any other name ("fake", "big") the next free id. The core never interprets
// the id, so any consistent assignment serves its tests.

#pragma once

#include "golden.h"

#include "nodes.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace core_golden
{

using ImTypeset::detail::Box;
using ImTypeset::detail::BoxKind;
using ImTypeset::detail::Disc;
using ImTypeset::detail::FontKey;
using ImTypeset::detail::GlueOrder;
using ImTypeset::detail::GlueSign;
using ImTypeset::detail::GlueSpec;
using ImTypeset::detail::KernKind;
using ImTypeset::detail::Node;
using ImTypeset::detail::NodeArena;
using ImTypeset::detail::NodeType;
using ImTypeset::detail::NodeView;
using ImTypeset::detail::Scaled;

// --- Tokens -----------------------------------------------------------------------

// Python's float.hex(): "0x1.8000000000000p+1", "-0x0.0p+0", thirteen hex
// digits always, "inf", "-inf", "nan". printf's %a trims trailing zeros and is
// not the same text.
inline std::string hex_float(double value)
{
    if (value != value)
        return "nan";
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    const bool negative = (bits >> 63) != 0;
    const int exponent = static_cast<int>((bits >> 52) & 0x7FF);
    const uint64_t mantissa = bits & ((uint64_t(1) << 52) - 1);
    if (exponent == 0x7FF)
        return negative ? "-inf" : "inf";
    if (exponent == 0 && mantissa == 0)
        return negative ? "-0x0.0p+0" : "0x0.0p+0";
    const int lead = exponent == 0 ? 0 : 1;
    const int power = exponent == 0 ? -1022 : exponent - 1023;
    return check::format("%s0x%d.%013" PRIx64 "p%+d", negative ? "-" : "", lead, mantissa, power);
}

// An enum's token is the lower-case name of the reference's member. `tokens` is
// the list in the port's enumerator order; the answer is the position, or 0 and
// a failure for a token that is not in it.
template<size_t N>
inline int token_index(std::string_view token, const char* const (&tokens)[N], const char* what)
{
    for (size_t i = 0; i < N; ++i) {
        if (token == tokens[i])
            return static_cast<int>(i);
    }
    check::fail(__FILE__, __LINE__,
                check::format("'%.*s' is not a %s token", static_cast<int>(token.size()),
                              token.data(), what));
    return 0;
}

inline constexpr const char* const GLUE_ORDER_TOKENS[] = {"normal", "fil", "fill", "filll"};
inline constexpr const char* const GLUE_SIGN_TOKENS[] = {"normal", "stretching", "shrinking"};
inline constexpr const char* const KERN_KIND_TOKENS[] = {"normal", "explicit", "accent"};
inline constexpr const char* const BOX_KIND_TOKENS[] = {"hlist", "vlist"};
// The tokens of enums that belong to pack and linebreak, in the reference's
// member order, for those modules' tests to index their own enums with.
inline constexpr const char* const PACK_MODE_TOKENS[] = {"exactly", "additional"};
inline constexpr const char* const PACK_FITNESS_TOKENS[] = {
    "exact", "loose", "underfull", "tight", "overfull", "infinite_glue", "empty"};
inline constexpr const char* const BREAK_PASS_TOKENS[] = {"first", "second", "emergency",
                                                          "first_fit"};
inline constexpr const char* const BREAK_KIND_TOKENS[] = {"glue", "penalty", "kern",
                                                          "math", "disc",    "paragraph_end"};

inline GlueOrder glue_order(std::string_view token)
{
    return static_cast<GlueOrder>(token_index(token, GLUE_ORDER_TOKENS, "GlueOrder"));
}
// A value outside its enum has no token: it prints as one no golden holds, so
// a corrupt field fails the comparison it is part of.
template<size_t N>
inline const char* token_at(const char* const (&tokens)[N], unsigned value)
{
    return value < N ? tokens[value] : "<not-a-member>";
}
inline const char* token(GlueOrder v)
{
    return token_at(GLUE_ORDER_TOKENS, static_cast<unsigned>(v));
}
inline const char* token(GlueSign v)
{
    return token_at(GLUE_SIGN_TOKENS, static_cast<unsigned>(v));
}
inline const char* token(KernKind v)
{
    return token_at(KERN_KIND_TOKENS, static_cast<unsigned>(v));
}
inline const char* token(BoxKind v)
{
    return token_at(BOX_KIND_TOKENS, static_cast<unsigned>(v));
}

inline std::string i64_text(int64_t v)
{
    return check::format("%" PRId64, v);
}

// --- Font keys --------------------------------------------------------------------

class FaceTable
{
public:
    FaceTable()
        : _names{"prose", "prose-bold", "prose-italic", "prose-bold-italic",
                 "code",  "code-bold",  "math"}
    {
    }

    // The id of a face name, assigning the next free one to a name not seen.
    uint32_t id(std::string_view name)
    {
        for (size_t i = 0; i < _names.size(); ++i) {
            if (_names[i] == name)
                return static_cast<uint32_t>(i);
        }
        _names.emplace_back(name);
        return static_cast<uint32_t>(_names.size() - 1);
    }

    // The name of an id; "?" for one this table never gave out.
    const char* name(uint32_t face) const
    {
        return face < _names.size() ? _names[face].c_str() : "?";
    }

    // "face@size" to a key, splitting at the last '@' as the reference does. A
    // string that is not of that form is a failure and answers the zero key.
    FontKey key(const std::string& text)
    {
        const size_t at = text.rfind('@');
        if (at == std::string::npos || at == 0 || at + 1 == text.size()) {
            check::fail(__FILE__, __LINE__, "not a font key: '" + text + "'");
            return FontKey{};
        }
        char* end = nullptr;
        const long long size = std::strtoll(text.c_str() + at + 1, &end, 10);
        if (end != text.c_str() + text.size() || size < 0 || size > FontKey::MAX_SIZE) {
            check::fail(__FILE__, __LINE__, "font key size is not usable: '" + text + "'");
            return FontKey{};
        }
        return FontKey::make(id(std::string_view(text).substr(0, at)), static_cast<Scaled>(size));
    }

    std::string text(FontKey key) const
    {
        return std::string(name(key.face())) + "@" + i64_text(key.size());
    }

private:
    std::vector<std::string> _names;
};

// What a case's node records are read into, and written back from.
struct Context {
    NodeArena arena;
    FaceTable faces;
};

// --- Walking a file ---------------------------------------------------------------

// One `case ... endcase` of a golden file: [first, end) are the records
// strictly between the two.
struct Case {
    std::string name;
    std::string kind;
    size_t first = 0;
    size_t end = 0;
    int line = 0;
};

// The cases of a file, in order. Checks the header record, and that every case
// is closed; a file that is not well formed is a failure and yields the cases
// read so far.
inline std::vector<Case> cases(const golden::File& file, std::string_view expected_id = {})
{
    std::vector<Case> out;
    const std::vector<golden::Record>& records = file.records();
    if (records.empty() || !records[0].is("golden")) {
        check::fail(file.name().c_str(), 0, "golden file has no `golden` header record");
        return out;
    }
    if (records[0].i64(0) != 1)
        check::fail(file.name().c_str(), records[0].line, "golden format version is not 1");
    if (!expected_id.empty() && records[0].str(1) != expected_id)
        check::fail(file.name().c_str(), records[0].line, "golden file id is not the one expected");
    size_t i = 1;
    while (i < records.size()) {
        const golden::Record& open = records[i];
        if (!open.is("case")) {
            check::fail(file.name().c_str(), open.line, "expected a `case` record");
            return out;
        }
        Case c;
        c.name = open.str(0);
        c.kind = std::string(open.raw(1));
        c.line = open.line;
        c.first = i + 1;
        size_t j = c.first;
        while (j < records.size() && !records[j].is("endcase") && !records[j].is("case"))
            ++j;
        if (j == records.size() || !records[j].is("endcase")) {
            check::fail(file.name().c_str(), open.line, "case has no `endcase`");
            return out;
        }
        c.end = j;
        out.push_back(std::move(c));
        i = j + 1;
    }
    return out;
}

// A position in one case's records.
class Reader
{
public:
    Reader(const golden::File& file, const Case& c)
        : _file(&file)
        , _next(c.first)
        , _end(c.end)
    {
        _missing.file = file.name().c_str();
    }

    bool done() const { return _next >= _end; }
    size_t position() const { return _next; }
    size_t end() const { return _end; }

    // The next record without consuming it. At the end of the case this is an
    // empty record with an empty tag, so `peek().is("x")` is simply false.
    const golden::Record& peek() const { return done() ? _missing : _file->records()[_next]; }
    bool next_is(std::string_view tag) const { return peek().is(tag); }

    const golden::Record& take()
    {
        if (done()) {
            check::fail(_file->name().c_str(), 0, "read past the end of a golden case");
            return _missing;
        }
        return _file->records()[_next++];
    }

    // Consumes the next record, which must have this tag; a failure if not.
    const golden::Record& take(std::string_view tag)
    {
        const golden::Record& record = take();
        if (!record.is(tag)) {
            check::fail(record.file, record.line,
                        check::format("expected a `%.*s` record, found `%.*s`",
                                      static_cast<int>(tag.size()), tag.data(),
                                      static_cast<int>(record.tag.size()), record.tag.data()));
        }
        return record;
    }

    // The text of record `index` as the file holds it, tokens joined by single
    // spaces: what the writers below produce for the same data.
    std::string line_text(size_t index) const
    {
        const golden::Record& record = _file->records()[index];
        std::string out(record.tag);
        for (const std::string_view field : record.fields) {
            out += ' ';
            out += field;
        }
        return out;
    }

    // The text of the records [first, end), one per element.
    std::vector<std::string> lines(size_t first, size_t end) const
    {
        std::vector<std::string> out;
        for (size_t i = first; i < end && i < _end; ++i)
            out.push_back(line_text(i));
        return out;
    }

private:
    const golden::File* _file;
    size_t _next;
    size_t _end;
    golden::Record _missing;
};

// The index of the first line at which two texts differ, or -1 if they are the
// same. On a difference, reports both lines (and a missing one as "<end>").
inline long first_difference(const std::vector<std::string>& expected,
                             const std::vector<std::string>& actual, const char* file, int line,
                             const char* what)
{
    const size_t n = expected.size() < actual.size() ? expected.size() : actual.size();
    size_t i = 0;
    while (i < n && expected[i] == actual[i])
        ++i;
    check::count_check();
    if (i == expected.size() && i == actual.size())
        return -1;
    check::fail(file, line,
                check::format("%s differs at line %zu of %zu (port wrote %zu)\n"
                              "    reference: %s\n"
                              "    port:      %s",
                              what, i, expected.size(), actual.size(),
                              i < expected.size() ? expected[i].c_str() : "<end>",
                              i < actual.size() ? actual[i].c_str() : "<end>"));
    return static_cast<long>(i);
}

// As a CHECK: an expression that is true when the two texts are the same.
inline bool lines_equal(const std::vector<std::string>& expected,
                        const std::vector<std::string>& actual, const char* file, int line,
                        const char* what)
{
    return first_difference(expected, actual, file, line, what) < 0;
}

#define CHECK_LINES_EQ(expected, actual, what) \
    ::core_golden::lines_equal((expected), (actual), __FILE__, __LINE__, (what))

// --- Glue specs -------------------------------------------------------------------

// Five fields starting at `cursor`: width stretch stretch_order shrink shrink_order.
inline GlueSpec read_spec_fields(golden::Cursor& cursor)
{
    GlueSpec spec;
    spec.width = cursor.i64();
    spec.stretch = cursor.i64();
    spec.stretch_order = glue_order(cursor.raw());
    spec.shrink = cursor.i64();
    spec.shrink_order = glue_order(cursor.raw());
    return spec;
}

inline std::string spec_fields(const GlueSpec& spec)
{
    return check::format("%" PRId64 " %" PRId64 " %s %" PRId64 " %s", spec.width, spec.stretch,
                         token(spec.stretch_order), spec.shrink, token(spec.shrink_order));
}

// `spec <role> ...`, which must be the next record and carry this role.
inline GlueSpec read_spec(Reader& reader, std::string_view role)
{
    const golden::Record& record = reader.take("spec");
    if (record.raw(0) != role)
        record.error(0, "spec record has another role than the one expected");
    golden::Cursor cursor(record, 1);
    const GlueSpec spec = read_spec_fields(cursor);
    cursor.expect_done();
    return spec;
}

inline std::string write_spec(std::string_view role, const GlueSpec& spec)
{
    return "spec " + std::string(role) + " " + spec_fields(spec);
}

// The `spec baselineskip` / `spec lineskip` / `lineskiplimit` triple.
struct Skip {
    GlueSpec baselineskip;
    GlueSpec lineskip;
    Scaled lineskiplimit = 0;
};

inline Skip read_skip(Reader& reader)
{
    Skip skip;
    skip.baselineskip = read_spec(reader, "baselineskip");
    skip.lineskip = read_spec(reader, "lineskip");
    skip.lineskiplimit = reader.take("lineskiplimit").i64(0);
    return skip;
}

// --- Nodes ------------------------------------------------------------------------

inline bool is_node_tag(std::string_view tag)
{
    return tag == "char" || tag == "rchar" || tag == "box" || tag == "rule" || tag == "glue" ||
           tag == "kern" || tag == "penalty" || tag == "disc" || tag == "mathon" ||
           tag == "mathoff";
}

// Reads one node record, with the records it brackets, into `context`. A record
// that is not a node is a failure and answers a zero penalty; nesting past the
// arena's limit is a failure.
inline Node read_node(Reader& reader, Context& context, int depth = 0)
{
    using namespace ImTypeset::detail;
    const golden::Record& record = reader.take();
    golden::Cursor cursor(record);
    if (depth > NodeArena::MAX_NESTING) {
        check::fail(record.file, record.line, "node records nest deeper than the arena allows");
        return penalty(0);
    }
    if (record.is("char")) {
        const FontKey font = context.faces.key(cursor.str());
        const Node node = char_node(font, cursor.u32());
        cursor.expect_done();
        return node;
    }
    if (record.is("rchar")) {
        const FontKey font = context.faces.key(cursor.str());
        const uint32_t codepoint = cursor.u32();
        const Node node = replaced_char(font, codepoint, cursor.u32());
        cursor.expect_done();
        return node;
    }
    if (record.is("rule")) {
        const Scaled width = cursor.i64();
        const Scaled height = cursor.i64();
        const Node node = rule(width, height, cursor.i64());
        cursor.expect_done();
        return node;
    }
    if (record.is("glue")) {
        const Node node = glue(read_spec_fields(cursor));
        cursor.expect_done();
        return node;
    }
    if (record.is("kern")) {
        const Scaled width = cursor.i64();
        const Node node = kern(
            width, static_cast<KernKind>(token_index(cursor.raw(), KERN_KIND_TOKENS, "KernKind")));
        cursor.expect_done();
        return node;
    }
    if (record.is("penalty")) {
        const Node node = penalty(cursor.i64());
        cursor.expect_done();
        return node;
    }
    if (record.is("mathon") || record.is("mathoff")) {
        const Scaled width = cursor.i64();
        cursor.expect_done();
        return record.is("mathon") ? math_on(width) : math_off(width);
    }
    if (record.is("box")) {
        Box box;
        box.kind = static_cast<BoxKind>(token_index(cursor.raw(), BOX_KIND_TOKENS, "BoxKind"));
        box.width = cursor.i64();
        box.height = cursor.i64();
        box.depth = cursor.i64();
        box.shift_amount = cursor.i64();
        box.glue_set = cursor.f64();
        box.glue_sign =
            static_cast<GlueSign>(token_index(cursor.raw(), GLUE_SIGN_TOKENS, "GlueSign"));
        box.glue_order = glue_order(cursor.raw());
        const uint32_t count = cursor.u32();
        cursor.expect_done();
        std::vector<Node> children;
        for (uint32_t i = 0; i < count && !reader.done(); ++i)
            children.push_back(read_node(reader, context, depth + 1));
        reader.take("endbox");
        return context.arena.add_box(box, children);
    }
    if (record.is("disc")) {
        const int replace_count = cursor.i32();
        const uint32_t npre = cursor.u32();
        const uint32_t npost = cursor.u32();
        cursor.expect_done();
        std::vector<Node> pre;
        std::vector<Node> post;
        for (uint32_t i = 0; i < npre && !reader.done(); ++i)
            pre.push_back(read_node(reader, context, depth + 1));
        for (uint32_t i = 0; i < npost && !reader.done(); ++i)
            post.push_back(read_node(reader, context, depth + 1));
        reader.take("enddisc");
        return context.arena.add_disc(pre, post, replace_count);
    }
    check::fail(record.file, record.line,
                check::format("`%.*s` is not a node record", static_cast<int>(record.tag.size()),
                              record.tag.data()));
    return penalty(0);
}

// Reads `nodes <name> <n>` ... `endnodes`. The list's name goes to `name` if
// that is given.
inline std::vector<Node> read_nodes(Reader& reader, Context& context, std::string* name = nullptr)
{
    const golden::Record& open = reader.take("nodes");
    if (name != nullptr)
        *name = std::string(open.raw(0));
    const uint32_t count = open.u32(1);
    std::vector<Node> nodes;
    for (uint32_t i = 0; i < count && !reader.done(); ++i)
        nodes.push_back(read_node(reader, context));
    reader.take("endnodes");
    return nodes;
}

// Appends the record lines of one node, as the oracle writes them.
inline void write_node(const Context& context, const Node& node, std::vector<std::string>& out)
{
    switch (node.type) {
    case NodeType::CHAR:
        if (node.is_replaced()) {
            out.push_back("rchar " + golden::percent_encode(context.faces.text(node.font())) +
                          check::format(" %u %u", node.codepoint(), node.original()));
        } else {
            out.push_back("char " + golden::percent_encode(context.faces.text(node.font())) +
                          check::format(" %u", node.codepoint()));
        }
        break;
    case NodeType::BOX: {
        const Box& box = context.arena.box(node);
        const NodeView contents = context.arena.view(box.contents);
        out.push_back(check::format(
            "box %s %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %s %s %s %zu", token(box.kind),
            box.width, box.height, box.depth, box.shift_amount, hex_float(box.glue_set).c_str(),
            token(box.glue_sign), token(box.glue_order), contents.size()));
        for (const Node& child : contents)
            write_node(context, child, out);
        out.push_back("endbox");
        break;
    }
    case NodeType::RULE:
        out.push_back(check::format("rule %" PRId64 " %" PRId64 " %" PRId64, node.width(),
                                    node.height(), node.depth()));
        break;
    case NodeType::DISC: {
        const Disc& disc = context.arena.disc(node);
        const NodeView pre = context.arena.view(disc.pre_break);
        const NodeView post = context.arena.view(disc.post_break);
        out.push_back(
            check::format("disc %d %zu %zu", disc.replace_count, pre.size(), post.size()));
        for (const Node& child : pre)
            write_node(context, child, out);
        for (const Node& child : post)
            write_node(context, child, out);
        out.push_back("enddisc");
        break;
    }
    case NodeType::MATH_ON:
        out.push_back(check::format("mathon %" PRId64, node.width()));
        break;
    case NodeType::MATH_OFF:
        out.push_back(check::format("mathoff %" PRId64, node.width()));
        break;
    case NodeType::GLUE:
        out.push_back("glue " + spec_fields(node.spec()));
        break;
    case NodeType::KERN:
        out.push_back(check::format("kern %" PRId64 " %s", node.width(), token(node.kern_kind())));
        break;
    case NodeType::PENALTY:
        out.push_back(check::format("penalty %" PRId64, node.penalty()));
        break;
    }
}

inline void write_nodes(const Context& context, std::string_view name, NodeView nodes,
                        std::vector<std::string>& out)
{
    out.push_back(
        check::format("nodes %.*s %zu", static_cast<int>(name.size()), name.data(), nodes.size()));
    for (const Node& node : nodes)
        write_node(context, node, out);
    out.push_back("endnodes");
}

inline std::vector<std::string> node_lines(const Context& context, const Node& node)
{
    std::vector<std::string> out;
    write_node(context, node, out);
    return out;
}

// --- CharMeasure answers ----------------------------------------------------------

// The `measure` records of a case: what the reference's CharMeasure answered
// for each (font, codepoint) in the case's inputs. A case with none was run
// with no measure at all, which `present` says.
struct MeasureTable {
    struct Entry {
        FontKey font;
        uint32_t codepoint = 0;
        Scaled width = 0;
        Scaled height = 0;
        Scaled depth = 0;
    };

    std::vector<Entry> entries;
    bool present = false;

    // The entry for a character, or null: a lookup the golden has no answer
    // for means the port measured something the reference did not.
    const Entry* find(FontKey font, uint32_t codepoint) const
    {
        // Sorted by font name then codepoint in the file, which is not the
        // order of the keys; cases are small, so this is a scan.
        for (const Entry& entry : entries) {
            if (entry.font == font && entry.codepoint == codepoint)
                return &entry;
        }
        return nullptr;
    }
};

// Reads the run of `measure` records at the reader's position, if any.
inline MeasureTable read_measure(Reader& reader, Context& context)
{
    MeasureTable table;
    while (reader.next_is("measure")) {
        const golden::Record& record = reader.take();
        golden::Cursor cursor(record);
        MeasureTable::Entry entry;
        entry.font = context.faces.key(cursor.str());
        entry.codepoint = cursor.u32();
        entry.width = cursor.i64();
        entry.height = cursor.i64();
        entry.depth = cursor.i64();
        cursor.expect_done();
        table.entries.push_back(entry);
        table.present = true;
    }
    return table;
}

// --- Pack results -----------------------------------------------------------------

// `packed <fitness> <natural> <excess> <badness> <overfull_by>` and the box
// record after it.
struct Packed {
    std::string fitness; // a PACK_FITNESS_TOKENS token
    Scaled natural = 0;
    Scaled excess = 0;
    int64_t badness = 0;
    Scaled overfull_by = 0;
    Node box;
};

inline Packed read_packed(Reader& reader, Context& context)
{
    const golden::Record& record = reader.take("packed");
    golden::Cursor cursor(record);
    Packed packed;
    packed.fitness = std::string(cursor.raw());
    packed.natural = cursor.i64();
    packed.excess = cursor.i64();
    packed.badness = cursor.i64();
    packed.overfull_by = cursor.i64();
    cursor.expect_done();
    if (reader.next_is("box"))
        packed.box = read_node(reader, context);
    else
        check::fail(record.file, record.line, "`packed` is not followed by a box");
    return packed;
}

inline void write_packed(const Context& context, std::string_view fitness, Scaled natural,
                         Scaled excess, int64_t badness, Scaled overfull_by, const Node& box,
                         std::vector<std::string>& out)
{
    out.push_back(check::format("packed %.*s %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64,
                                static_cast<int>(fitness.size()), fitness.data(), natural, excess,
                                badness, overfull_by));
    write_node(context, box, out);
}

// --- Line breaking ----------------------------------------------------------------

// `shape <n> <width> <indent> ...`: one (width, indent) per LineSpec.
struct Shape {
    struct Line {
        Scaled width = 0;
        Scaled indent = 0;
    };
    std::vector<Line> lines;
};

inline Shape read_shape(Reader& reader)
{
    const golden::Record& record = reader.take("shape");
    golden::Cursor cursor(record);
    Shape shape;
    const uint32_t count = cursor.u32();
    for (uint32_t i = 0; i < count && !cursor.done(); ++i) {
        Shape::Line line;
        line.width = cursor.i64();
        line.indent = cursor.i64();
        shape.lines.push_back(line);
    }
    cursor.expect_done();
    return shape;
}

// The `params` record and the three `spec` records after it: the fields of the
// reference's LineBreakParams, under its names.
struct Params {
    int64_t pretolerance = 0;
    int64_t tolerance = 0;
    int64_t line_penalty = 0;
    int64_t adj_demerits = 0;
    int64_t double_hyphen_demerits = 0;
    int64_t final_hyphen_demerits = 0;
    int64_t hyphen_penalty = 0;
    int64_t ex_hyphen_penalty = 0;
    Scaled emergency_stretch = 0;
    bool first_fit = false;
    GlueSpec left_skip;
    GlueSpec right_skip;
    GlueSpec par_fill_skip;
};

inline Params read_params(Reader& reader)
{
    const golden::Record& record = reader.take("params");
    golden::Cursor cursor(record);
    Params params;
    params.pretolerance = cursor.i64();
    params.tolerance = cursor.i64();
    params.line_penalty = cursor.i64();
    params.adj_demerits = cursor.i64();
    params.double_hyphen_demerits = cursor.i64();
    params.final_hyphen_demerits = cursor.i64();
    params.hyphen_penalty = cursor.i64();
    params.ex_hyphen_penalty = cursor.i64();
    params.emergency_stretch = cursor.i64();
    params.first_fit = cursor.boolean();
    cursor.expect_done();
    params.left_skip = read_spec(reader, "left_skip");
    params.right_skip = read_spec(reader, "right_skip");
    params.par_fill_skip = read_spec(reader, "par_fill_skip");
    return params;
}

// `paragraph <nlines> <total_demerits> <pass> <prepared_length>` and the
// `breaks` record after it. Positions index the prepared list (FORMAT.md).
struct ParagraphResult {
    struct Break {
        int64_t position = 0;
        std::string kind; // a BREAK_KIND_TOKENS token
    };

    int64_t line_count = 0;
    int64_t total_demerits = 0;
    std::string pass; // a BREAK_PASS_TOKENS token
    int64_t prepared_length = 0;
    std::vector<Break> breaks;
};

inline ParagraphResult read_paragraph(Reader& reader)
{
    ParagraphResult result;
    {
        const golden::Record& record = reader.take("paragraph");
        golden::Cursor cursor(record);
        result.line_count = cursor.i64();
        result.total_demerits = cursor.i64();
        result.pass = std::string(cursor.raw());
        result.prepared_length = cursor.i64();
        cursor.expect_done();
    }
    const golden::Record& record = reader.take("breaks");
    golden::Cursor cursor(record);
    const uint32_t count = cursor.u32();
    for (uint32_t i = 0; i < count && !cursor.done(); ++i) {
        ParagraphResult::Break b;
        b.position = cursor.i64();
        b.kind = std::string(cursor.raw());
        result.breaks.push_back(std::move(b));
    }
    cursor.expect_done();
    return result;
}

// --- Draw commands ----------------------------------------------------------------

// One ship-out command as the golden holds it. Which fields are meaningful
// follows the record: glyph (font codepoint x y index), rulerect (x y width
// height), setglue and atom (x y width natural height depth index), line (x y
// width height depth start end).
struct Cmd {
    enum Kind { GLYPH, RULERECT, SETGLUE, ATOM, LINE };

    Kind kind = GLYPH;
    FontKey font;
    uint32_t codepoint = 0;
    Scaled x = 0;
    Scaled y = 0;
    Scaled width = 0;
    Scaled natural = 0;
    Scaled height = 0;
    Scaled depth = 0;
    int64_t index = 0;
    int64_t start = 0;
    int64_t end = 0;
};

inline bool is_cmd_tag(std::string_view tag)
{
    return tag == "glyph" || tag == "rulerect" || tag == "setglue" || tag == "atom" ||
           tag == "line";
}

// Reads `cmds <n>` ... `endcmds`.
inline std::vector<Cmd> read_cmds(Reader& reader, Context& context)
{
    std::vector<Cmd> out;
    const uint32_t count = reader.take("cmds").u32(0);
    for (uint32_t i = 0; i < count && !reader.done(); ++i) {
        const golden::Record& record = reader.take();
        golden::Cursor cursor(record);
        Cmd cmd;
        if (record.is("glyph")) {
            cmd.kind = Cmd::GLYPH;
            cmd.font = context.faces.key(cursor.str());
            cmd.codepoint = cursor.u32();
            cmd.x = cursor.i64();
            cmd.y = cursor.i64();
            cmd.index = cursor.i64();
        } else if (record.is("rulerect")) {
            cmd.kind = Cmd::RULERECT;
            cmd.x = cursor.i64();
            cmd.y = cursor.i64();
            cmd.width = cursor.i64();
            cmd.height = cursor.i64();
        } else if (record.is("setglue") || record.is("atom")) {
            cmd.kind = record.is("atom") ? Cmd::ATOM : Cmd::SETGLUE;
            cmd.x = cursor.i64();
            cmd.y = cursor.i64();
            cmd.width = cursor.i64();
            cmd.natural = cursor.i64();
            cmd.height = cursor.i64();
            cmd.depth = cursor.i64();
            cmd.index = cursor.i64();
        } else if (record.is("line")) {
            cmd.kind = Cmd::LINE;
            cmd.x = cursor.i64();
            cmd.y = cursor.i64();
            cmd.width = cursor.i64();
            cmd.height = cursor.i64();
            cmd.depth = cursor.i64();
            cmd.start = cursor.i64();
            cmd.end = cursor.i64();
        } else {
            check::fail(record.file, record.line, "not a draw-command record");
        }
        cursor.expect_done();
        out.push_back(cmd);
    }
    reader.take("endcmds");
    return out;
}

inline std::string write_cmd(const Context& context, const Cmd& cmd)
{
    switch (cmd.kind) {
    case Cmd::GLYPH:
        return "glyph " + golden::percent_encode(context.faces.text(cmd.font)) +
               check::format(" %u %" PRId64 " %" PRId64 " %" PRId64, cmd.codepoint, cmd.x, cmd.y,
                             cmd.index);
    case Cmd::RULERECT:
        return check::format("rulerect %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64, cmd.x, cmd.y,
                             cmd.width, cmd.height);
    case Cmd::SETGLUE:
    case Cmd::ATOM:
        return check::format("%s %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64
                             " %" PRId64 " %" PRId64,
                             cmd.kind == Cmd::ATOM ? "atom" : "setglue", cmd.x, cmd.y, cmd.width,
                             cmd.natural, cmd.height, cmd.depth, cmd.index);
    case Cmd::LINE:
        return check::format("line %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64
                             " %" PRId64 " %" PRId64,
                             cmd.x, cmd.y, cmd.width, cmd.height, cmd.depth, cmd.start, cmd.end);
    }
    return std::string();
}

inline void write_cmds(const Context& context, const std::vector<Cmd>& cmds,
                       std::vector<std::string>& out)
{
    out.push_back(check::format("cmds %zu", cmds.size()));
    for (const Cmd& cmd : cmds)
        out.push_back(write_cmd(context, cmd));
    out.push_back("endcmds");
}

} // namespace core_golden
