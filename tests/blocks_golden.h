// Reader for the records of tools/oracle/FORMAT-blocks.md: the INPUT of a
// `blocks` case (theme, metrics answers, one group per block) and the pieces of
// its EXPECTED `compose` groups that are new there (`lines`, `stack`,
// `extents`, `units`). The lexical layer is tests/golden.h's and the node,
// spec, measure, packed, break and command records are tests/core_golden.h's;
// the run, marker and metrics records are FORMAT-para.md's, read here as
// tests/test_paragraph.cpp reads them.
//
// Reading gives plain structs holding what the records hold, plus the text of
// the records a test compares line for line. Writing gives what the port
// produced in the oracle's spelling, so a difference is shown as two lines.

#pragma once

#include "core_golden.h"

#include "blocks.h"
#include "fontmetrics.h"
#include "linebreak.h"
#include "paragraph.h"
#include "shipout.h"
#include "style.h"

#include <cinttypes>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace blocks_golden
{

using namespace ImTypeset::detail;

// --- Tokens -----------------------------------------------------------------------------

inline bool spacing_of(std::string_view token, Spacing* out)
{
    if (token == "nonfrench")
        *out = Spacing::NONFRENCH;
    else if (token == "french")
        *out = Spacing::FRENCH;
    else if (token == "extra_space_only")
        *out = Spacing::EXTRA_SPACE_ONLY;
    else
        return false;
    return true;
}

inline bool align_of(std::string_view token, Align* out)
{
    if (token == "justify")
        *out = Align::JUSTIFY;
    else if (token == "left")
        *out = Align::LEFT;
    else if (token == "center")
        *out = Align::CENTER;
    else if (token == "right")
        *out = Align::RIGHT;
    else
        return false;
    return true;
}

// A font key as the goldens spell it, which must be one the port can hold.
inline FontSpec spec_of(const std::string& key)
{
    FontSpec spec;
    if (!parse_font_key_text(key, &spec))
        check::fail(__FILE__, __LINE__, check::format("'%s' is not a font key", key.c_str()));
    return spec;
}

inline Face face_of(const std::string& name)
{
    return spec_of(name + "@0").face;
}

// --- Metrics answered from a case's records -----------------------------------------------

struct Recorded;

// One face at one size, answering only what the reference was asked.
class RecordedMetrics : public FontMetrics
{
public:
    RecordedMetrics(Recorded& owner, FontKey key)
        : _owner(&owner)
        , _key(key)
    {
    }
    Face face() const override { return static_cast<Face>(_key.face()); }
    double size() const override { return unasked("size"); }
    double ascent() const override { return unasked("ascent"); }
    double descent() const override { return unasked("descent"); }
    double x_height() const override { return unasked("x_height"); }
    double em() const override { return unasked("em"); }
    double space_width() const override;
    bool has(uint32_t codepoint) const override;
    double advance(uint32_t) const override { return unasked("advance"); }
    GlyphBBox bbox(uint32_t) const override
    {
        unasked("bbox");
        return GlyphBBox();
    }
    double kern(uint32_t codepoint, uint32_t following) const override;

private:
    double unasked(const char* what) const;

    Recorded* _owner;
    FontKey _key;
};

// The `space_width`, `has` and `kern` records of a case. `misses` counts the
// questions it has no record for: the port asking what the reference did not.
struct Recorded {
    std::map<uint64_t, double> space_widths;
    std::map<std::pair<uint64_t, uint32_t>, bool> has;
    std::map<std::tuple<uint64_t, uint32_t, uint32_t>, double> kerns;
    std::map<uint64_t, std::unique_ptr<RecordedMetrics>> faces;
    int misses = 0;
    int asked = 0;

    void miss(const char* what, FontKey key, uint32_t a = 0, uint32_t b = 0)
    {
        ++misses;
        check::fail(
            __FILE__, __LINE__,
            check::format("the port asked %s of %s (U+%04X, U+%04X), which the reference did not",
                          what, font_key_text(static_cast<Face>(key.face()), key.size()).c_str(), a,
                          b));
    }

    static const FontMetrics* resolve(void* user, FontKey key)
    {
        Recorded& self = *static_cast<Recorded*>(user);
        FontSpec spec;
        if (!parse_font_key(key, &spec))
            return nullptr;
        std::unique_ptr<RecordedMetrics>& slot = self.faces[key.bits];
        if (!slot)
            slot.reset(new RecordedMetrics(self, key));
        return slot.get();
    }

    MetricsResolver resolver() { return MetricsResolver{&resolve, this}; }

    // Reads the run of `space_width`, `has` and `kern` records at the reader.
    void read(core_golden::Reader& reader, core_golden::Context& context)
    {
        for (;;) {
            if (reader.next_is("space_width")) {
                golden::Cursor cursor(reader.take());
                const FontKey key = context.faces.key(cursor.str());
                space_widths[key.bits] = cursor.f64();
                cursor.expect_done();
            } else if (reader.next_is("has")) {
                golden::Cursor cursor(reader.take());
                const FontKey key = context.faces.key(cursor.str());
                const uint32_t codepoint = cursor.u32();
                has[{key.bits, codepoint}] = cursor.boolean();
                cursor.expect_done();
            } else if (reader.next_is("kern")) {
                golden::Cursor cursor(reader.take());
                const FontKey key = context.faces.key(cursor.str());
                const uint32_t codepoint = cursor.u32();
                const uint32_t following = cursor.u32();
                kerns[std::make_tuple(key.bits, codepoint, following)] = cursor.f64();
                cursor.expect_done();
            } else {
                return;
            }
        }
    }
};

inline double RecordedMetrics::unasked(const char* what) const
{
    _owner->miss(what, _key);
    return 0.0;
}

inline double RecordedMetrics::space_width() const
{
    ++_owner->asked;
    const auto found = _owner->space_widths.find(_key.bits);
    if (found == _owner->space_widths.end()) {
        _owner->miss("space_width", _key);
        return 0.0;
    }
    return found->second;
}

inline bool RecordedMetrics::has(uint32_t codepoint) const
{
    ++_owner->asked;
    const auto found = _owner->has.find({_key.bits, codepoint});
    if (found == _owner->has.end()) {
        _owner->miss("has", _key, codepoint);
        return false;
    }
    return found->second;
}

inline double RecordedMetrics::kern(uint32_t codepoint, uint32_t following) const
{
    ++_owner->asked;
    const auto found = _owner->kerns.find(std::make_tuple(_key.bits, codepoint, following));
    if (found == _owner->kerns.end()) {
        _owner->miss("kern", _key, codepoint, following);
        return 0.0;
    }
    return found->second;
}

// The `measure` records as a CharMeasure. A document's table is hundreds of
// entries and is asked for every character of every packing, so it is indexed.
struct TableMeasure {
    std::map<std::pair<uint64_t, uint32_t>, CharDimens> entries;
    int misses = 0;

    explicit TableMeasure(const core_golden::MeasureTable& table)
    {
        for (const core_golden::MeasureTable::Entry& entry : table.entries)
            entries[{entry.font.bits, entry.codepoint}] =
                CharDimens{entry.width, entry.height, entry.depth};
    }

    static CharDimens answer(void* user, FontKey font, uint32_t codepoint)
    {
        TableMeasure& self = *static_cast<TableMeasure*>(user);
        const auto found = self.entries.find({font.bits, codepoint});
        if (found == self.entries.end()) {
            ++self.misses;
            return CharDimens{};
        }
        return found->second;
    }

    CharMeasure measure() { return CharMeasure{&TableMeasure::answer, this}; }
};

// --- INPUT --------------------------------------------------------------------------------

// The `theme` and `faces` records.
struct ThemeIn {
    double px_per_inch = 0.0;
    Composition composition;
    Scaled marker_gap = 0;
    DocumentTheme theme;
    // Informational: both are already inside the blocks' own values.
    Scaled list_indent = 0;
    Scaled rule_thickness = 0;
};

inline ThemeIn read_theme(core_golden::Reader& reader)
{
    ThemeIn out;
    golden::Cursor theme(reader.take("theme"));
    out.px_per_inch = theme.f64();
    if (!spacing_of(theme.raw(), &out.composition.spacing))
        check::fail(__FILE__, __LINE__, "the theme's spacing is not a Spacing");
    out.marker_gap = theme.i64();
    out.theme.quote_indent = theme.i64();
    out.theme.quote_rule_thickness = theme.i64();
    out.list_indent = theme.i64();
    out.rule_thickness = theme.i64();
    theme.expect_done();
    golden::Cursor faces(reader.take("faces"));
    out.composition.regular = face_of(faces.str());
    if (face_of(faces.str()) != Face::MATH)
        check::fail(__FILE__, __LINE__, "the theme's second face is not the math face");
    out.composition.code = face_of(faces.str());
    faces.expect_done();
    return out;
}

// One `run` or `hardbreak` record.
struct Event {
    bool hard_break = false;
    FontSpec font;
    bool breakable = true;
    std::string text;
};

// One block group: what it says, and the text of its derived node lists.
struct BlockIn {
    BlockType type = BlockType::TEXT;
    int line = 0;
    // TEXT: the role token, and the style's own font (both informational).
    std::string kind;
    std::string font;
    Align align = Align::JUSTIFY;
    Scaled left_indent = 0;
    Scaled first_line_indent = 0;
    uint32_t quote_depth = 0;
    Scaled space_above = 0;
    Scaled space_below = 0;
    InterlineSkip interline;
    Strut strut;
    std::vector<Event> events;
    bool has_marker = false;
    FontSpec marker_font;
    std::string marker_text;
    // CODE
    std::string source;
    uint32_t line_count = 0;
    // RULE
    Scaled thickness = 0;
    // The records of `nodes material`, `nodes marker`, `nodes unshrinkable`,
    // and of each `nodes line`, as the file holds them.
    std::vector<std::string> material;
    std::vector<std::string> marker;
    std::vector<std::string> unshrinkable;
    std::vector<std::vector<std::string>> code_lines;
};

inline void read_style(core_golden::Reader& reader, BlockIn& out)
{
    const core_golden::Skip skip = core_golden::read_skip(reader);
    out.interline.baselineskip = skip.baselineskip;
    out.interline.lineskip = skip.lineskip;
    out.interline.lineskiplimit = skip.lineskiplimit;
    golden::Cursor strut(reader.take("strut"));
    out.strut.height = strut.i64();
    out.strut.depth = strut.i64();
    strut.expect_done();
}

// Reads one `nodes <name> <n>` ... `endnodes` and answers its records' text.
inline std::vector<std::string> read_node_lines(core_golden::Reader& reader,
                                                core_golden::Context& context,
                                                std::string_view expected_name)
{
    std::string name;
    const size_t first = reader.position();
    core_golden::read_nodes(reader, context, &name);
    if (name != expected_name)
        check::fail(__FILE__, __LINE__,
                    check::format("expected `nodes %.*s`, found `nodes %s`",
                                  static_cast<int>(expected_name.size()), expected_name.data(),
                                  name.c_str()));
    return reader.lines(first, reader.position());
}

// Reads `blocks <n>` ... `endblocks`.
inline std::vector<BlockIn> read_blocks(core_golden::Reader& reader, core_golden::Context& context)
{
    std::vector<BlockIn> out;
    const uint32_t count = reader.take("blocks").u32(0);
    while (!reader.done() && !reader.next_is("endblocks")) {
        BlockIn block;
        const golden::Record& open = reader.take();
        block.line = open.line;
        golden::Cursor cursor(open);
        if (cursor.u32() != out.size())
            check::fail(open.file, open.line, "blocks are not numbered in order");
        if (open.is("text")) {
            block.type = BlockType::TEXT;
            block.kind = std::string(cursor.raw());
            if (!align_of(cursor.raw(), &block.align))
                check::fail(open.file, open.line, "not an Align");
            block.font = cursor.str();
            block.left_indent = cursor.i64();
            block.first_line_indent = cursor.i64();
            block.quote_depth = cursor.u32();
            block.space_above = cursor.i64();
            block.space_below = cursor.i64();
            cursor.expect_done();
            read_style(reader, block);
            const uint32_t runs = reader.take("runs").u32(0);
            while (!reader.done() && !reader.next_is("endruns")) {
                Event event;
                if (reader.next_is("hardbreak")) {
                    reader.take();
                    event.hard_break = true;
                } else {
                    golden::Cursor run(reader.take("run"));
                    event.font = spec_of(run.str());
                    event.breakable = run.boolean();
                    event.text = run.str();
                    run.expect_done();
                }
                block.events.push_back(event);
            }
            reader.take("endruns");
            if (block.events.size() != runs)
                check::fail(open.file, open.line, "`runs` does not count its records");
            if (reader.next_is("marker")) {
                golden::Cursor marker(reader.take());
                block.marker_font = spec_of(marker.str());
                block.marker_text = marker.str();
                marker.expect_done();
                block.has_marker = true;
            }
            block.material = read_node_lines(reader, context, "material");
            if (block.has_marker)
                block.marker = read_node_lines(reader, context, "marker");
            if (block.align != Align::JUSTIFY)
                block.unshrinkable = read_node_lines(reader, context, "unshrinkable");
            reader.take("endtext");
        } else if (open.is("code")) {
            block.type = BlockType::CODE;
            block.font = cursor.str();
            block.left_indent = cursor.i64();
            block.space_above = cursor.i64();
            block.space_below = cursor.i64();
            block.line_count = cursor.u32();
            block.source = cursor.str();
            cursor.expect_done();
            read_style(reader, block);
            for (uint32_t l = 0; l < block.line_count && !reader.done(); ++l)
                block.code_lines.push_back(read_node_lines(reader, context, "line"));
            reader.take("endcode");
        } else if (open.is("rule")) {
            block.type = BlockType::RULE;
            block.left_indent = cursor.i64();
            block.thickness = cursor.i64();
            block.space_above = cursor.i64();
            block.space_below = cursor.i64();
            cursor.expect_done();
            read_style(reader, block);
            reader.take("endrule");
        } else {
            check::fail(open.file, open.line, "expected a `text`, `code` or `rule` group");
            break;
        }
        out.push_back(std::move(block));
    }
    reader.take("endblocks");
    if (out.size() != count)
        check::fail(__FILE__, __LINE__, "`blocks` does not count its groups");
    return out;
}

// The index one past the record that closes the group opened at `open`
// (`stack` ... `endstack`, nested), or `end`.
inline size_t group_end(const golden::File& file, size_t open, size_t end, std::string_view begin,
                        std::string_view close)
{
    int depth = 0;
    for (size_t i = open; i < end; ++i) {
        const golden::Record& record = file.records()[i];
        if (record.is(begin))
            ++depth;
        else if (record.is(close) && --depth == 0)
            return i + 1;
    }
    return end;
}

// --- EXPECTED: `units` ----------------------------------------------------------------------

struct UnitRecord {
    uint32_t first_block = 0;
    uint32_t block_count = 0;
    Scaled top = 0;
    uint32_t cmd_start = 0;
    uint32_t cmd_end = 0;
    uint32_t index_start = 0;
    uint32_t index_end = 0;
    struct Span {
        uint32_t block = 0;
        uint32_t index_start = 0;
        uint32_t index_end = 0;
        std::string role;
    };
    std::vector<Span> spans;
};

inline std::vector<UnitRecord> read_units(core_golden::Reader& reader)
{
    std::vector<UnitRecord> out;
    const uint32_t count = reader.take("units").u32(0);
    for (uint32_t u = 0; u < count && !reader.done(); ++u) {
        UnitRecord unit;
        golden::Cursor cursor(reader.take("unit"));
        unit.first_block = cursor.u32();
        unit.block_count = cursor.u32();
        unit.top = cursor.i64();
        unit.cmd_start = cursor.u32();
        unit.cmd_end = cursor.u32();
        unit.index_start = cursor.u32();
        unit.index_end = cursor.u32();
        cursor.expect_done();
        for (uint32_t s = 0; s < unit.block_count && !reader.done(); ++s) {
            golden::Cursor span(reader.take("span"));
            UnitRecord::Span one;
            one.block = span.u32();
            one.index_start = span.u32();
            one.index_end = span.u32();
            one.role = std::string(span.raw());
            span.expect_done();
            unit.spans.push_back(one);
        }
        out.push_back(std::move(unit));
    }
    return out;
}

// --- Writing what the port produced as the oracle writes it -----------------------------------

inline const char* token(BreakPass pass)
{
    return core_golden::BREAK_PASS_TOKENS[static_cast<int>(pass)];
}

inline const char* token(BreakKind kind)
{
    return core_golden::BREAK_KIND_TOKENS[static_cast<int>(kind)];
}

inline const char* token(PackFitness fitness)
{
    return core_golden::PACK_FITNESS_TOKENS[static_cast<int>(fitness)];
}

// The `paragraph`, `breaks` and `packed`/`box` records of a broken paragraph.
inline std::vector<std::string> written(const core_golden::Context& context,
                                        const Paragraph& paragraph)
{
    std::vector<std::string> out;
    out.push_back(check::format("paragraph %zu %lld %s %u", paragraph.lines.size(),
                                static_cast<long long>(paragraph.total_demerits),
                                token(paragraph.pass_used), paragraph.prepared_length));
    std::string breaks = check::format("breaks %zu", paragraph.breaks.size());
    for (const Break& b : paragraph.breaks)
        breaks += check::format(" %u %s", b.position, token(b.kind));
    out.push_back(breaks);
    for (const BrokenLine& line : paragraph.lines) {
        core_golden::write_packed(context, token(line.packed.fitness), line.packed.natural,
                                  line.packed.excess, line.packed.badness, line.packed.overfull_by,
                                  line.box, out);
    }
    return out;
}

inline core_golden::Cmd as_golden(const DrawCmd& cmd)
{
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

// The `cmds` group.
inline std::vector<std::string> written(const core_golden::Context& context,
                                        const std::vector<DrawCmd>& cmds)
{
    std::vector<core_golden::Cmd> converted;
    converted.reserve(cmds.size());
    for (const DrawCmd& cmd : cmds)
        converted.push_back(as_golden(cmd));
    std::vector<std::string> out;
    core_golden::write_cmds(context, converted, out);
    return out;
}

inline bool same_params(const core_golden::Params& p, const LineBreakParams& q)
{
    return p.pretolerance == q.pretolerance && p.tolerance == q.tolerance &&
           p.line_penalty == q.line_penalty && p.adj_demerits == q.adj_demerits &&
           p.double_hyphen_demerits == q.double_hyphen_demerits &&
           p.final_hyphen_demerits == q.final_hyphen_demerits &&
           p.hyphen_penalty == q.hyphen_penalty && p.ex_hyphen_penalty == q.ex_hyphen_penalty &&
           p.emergency_stretch == q.emergency_stretch && p.first_fit == q.first_fit &&
           p.left_skip == q.left_skip && p.right_skip == q.right_skip &&
           p.par_fill_skip == q.par_fill_skip;
}

inline bool same_shape(const core_golden::Shape& expected, const BlockShape& actual)
{
    if (expected.lines.size() != actual.count)
        return false;
    for (size_t i = 0; i < actual.count; ++i) {
        if (expected.lines[i].width != actual.lines[i].width ||
            expected.lines[i].indent != actual.lines[i].indent)
            return false;
    }
    return true;
}

// The seven fields a `barred` and an `inner` record give of a box.
inline std::string box_header(const Box& box)
{
    return check::format("%" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %s %s %s", box.width,
                         box.height, box.depth, box.shift_amount,
                         core_golden::hex_float(box.glue_set).c_str(),
                         core_golden::token(box.glue_sign), core_golden::token(box.glue_order));
}

// A stacking's trace as the `stack` ... `endstack` records.
inline std::vector<std::string> written(const core_golden::Context& context,
                                        const std::vector<StackEvent>& trace)
{
    std::vector<std::string> out;
    for (const StackEvent& event : trace) {
        switch (event.kind) {
        case StackEvent::STACK:
            out.push_back(check::format("stack %" PRId64 " %" PRId64, event.a, event.b));
            break;
        case StackEvent::RUN:
            out.push_back(
                check::format("srun %" PRId64 " %" PRId64 " %" PRId64, event.a, event.b, event.c));
            break;
        case StackEvent::GAP:
            out.push_back(check::format("gap %" PRId64 " %" PRId64 " %" PRId64 " %s", event.a,
                                        event.b, event.c, event.d != 0 ? "list-empty" : "sum"));
            break;
        case StackEvent::KERN:
        case StackEvent::GLUE:
            core_golden::write_node(context, event.node, out);
            break;
        case StackEvent::APPEND:
            out.push_back(check::format("append %" PRId64 " %" PRId64, event.a, event.b));
            break;
        case StackEvent::REF:
            out.push_back(check::format("ref %" PRId64 " %" PRId64, event.a, event.b));
            break;
        case StackEvent::BARRED: {
            const Box* const box = context.arena.find_box(event.node);
            if (box == nullptr) {
                out.push_back("barred <not-a-box>");
                break;
            }
            out.push_back("barred " + box_header(*box));
            const NodeView contents = context.arena.view(box->contents);
            for (size_t i = 0; i < contents.size(); ++i) {
                const Box* const inner = i == 3 ? context.arena.find_box(contents[i]) : nullptr;
                if (inner != nullptr)
                    out.push_back("inner " + box_header(*inner));
                else
                    core_golden::write_node(context, contents[i], out);
            }
            break;
        }
        case StackEvent::END_BARRED:
            out.push_back("endbarred");
            break;
        case StackEvent::END_RUN:
            out.push_back("endsrun");
            break;
        case StackEvent::END_STACK:
            out.push_back("endstack");
            break;
        }
    }
    return out;
}

} // namespace blocks_golden
