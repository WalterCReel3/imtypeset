// tests/golden/doc (tools/oracle/FORMAT-doc.md) through the Document of
// include/imtypeset.h: what the reference answers above one block and across
// several, asked of the public calls.
//
// The reference's blocks are described through ImTypesetBlock and its
// companions, as tests/test_api_document.cpp describes tests/golden/blocks,
// and laid out under the reference's rules (tests/document_reference.h); every
// length goes in as float pixels and is held to the golden's scaled points
// exactly. Every layout is first held to the golden's commands, exactly; the
// questions are asked only of a layout that is the reference's.
//
// TWO SPACES. The golden speaks reading-order indices and beside each the
// (block, byte) it comes to (`imap`); the product speaks (block, byte) only.
// A caret is compared as THE FIRST BYTE OF TEXT AT OR AFTER IT, counted over
// the document: two carets with nothing but items the text does not hold
// between them (a marker, a line's fill, the end of one block and the start
// of the next) are one position, in both spaces. Nothing else is folded.
//
// A CODE BLOCK's bytes are, in the golden, into its tab-expanded text, and in
// the product into the source the host handed in (the lead's ruling,
// .concerns/api-document--lead--notes.md). `map_tabs` goes between the two by
// the reference's expansion and checks itself against the golden's text.
//
// PIXELS. The golden's points and rectangles are scaled points; the calls take
// and give float pixels. A point is handed over as the float nearest it, and
// how many of them come back as the same scaled point is counted. A rectangle
// is compared with the float its scaled points come to, exactly: no tolerance.
//
// WHERE THE PRODUCT HAS ANOTHER RULE than the reference (a point between two
// blocks, a marker, what joins two blocks in a copy), the product's answer is
// asserted where it can be stated, and the case is counted under a reason; the
// counts are printed and held to minimums so a replay that shrank cannot pass.

#include "blocks_golden.h"
#include "core_golden.h"
#include "document_reference.h"
#include "golden.h"

#include "imtypeset.h"

#include "context.h"
#include "imtypeset_document.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace ImTypeset::detail;

namespace
{

const struct {
    ImTypesetFace face;
    const char* file;
} DEFAULT_FONTS[] = {
    {ImTypesetFace_Regular, "Roboto-Regular.ttf"},
    {ImTypesetFace_Bold, "Roboto-Bold.ttf"},
    {ImTypesetFace_Italic, "Roboto-RegularItalic.ttf"},
    {ImTypesetFace_BoldItalic, "Roboto-BoldItalic.ttf"},
    {ImTypesetFace_Code, "Inconsolata-Medium.ttf"},
    {ImTypesetFace_Math, "latinmodern-math.otf"},
};

struct Stage {
    Stage()
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(800.0f, 600.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        if (!IMTYPESET_CHECKVERSION())
            FAIL("IMTYPESET_CHECKVERSION: %s", ImTypeset::GetLastError()->Message);
        ctx = ImTypeset::CreateContext();
        if (ctx == nullptr) {
            FAIL("CreateContext: %s", ImTypeset::GetLastError()->Message);
            return;
        }
        for (const auto& entry : DEFAULT_FONTS) {
            const std::string path = golden::data_path(std::string("fonts/") + entry.file);
            if (ImTypeset::AddFontFromFileTTF(entry.face, path.c_str()) == nullptr)
                FAIL("%s: %s", entry.file, ImTypeset::GetLastError()->Message);
        }
        ImTypeset::GetStyle().EmSize = 16.0f;
        // The goldens are the reference's: its departures from tex.web stay in.
        ctx->tex_rules = TexRules::reference();
    }
    ~Stage()
    {
        ImTypeset::DestroyContext();
        ImGui::DestroyContext();
    }
    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;

    ImTypesetContext* ctx = nullptr;
};

struct Frame {
    Frame() { ImGui::NewFrame(); }
    ~Frame() { ImGui::Render(); }
};

// What a host assembles before it hands a document over.
struct Content {
    std::string text;
    std::vector<ImTypesetBlock> blocks;
    std::vector<ImTypesetRun> runs;
    std::vector<ImTypesetBlockStyle> styles;

    ImTypesetBlock& add(ImTypesetBlockKind kind, const std::string& body, int style)
    {
        ImTypesetBlock block;
        block.Kind = kind;
        block.Style = style;
        block.TextStart = static_cast<int>(text.size());
        text += body;
        block.TextEnd = static_cast<int>(text.size());
        block.RunStart = static_cast<int>(runs.size());
        blocks.push_back(block);
        return blocks.back();
    }
    ImTypesetRun& run(int start, int end)
    {
        ImTypesetRun r;
        r.Start = start;
        r.End = end;
        runs.push_back(r);
        ++blocks.back().RunCount;
        return runs.back();
    }
    void marker(const std::string& body)
    {
        blocks.back().MarkerStart = static_cast<int>(text.size());
        text += body;
        blocks.back().MarkerEnd = static_cast<int>(text.size());
    }
    ImTypesetDocumentContent view() const
    {
        ImTypesetDocumentContent out;
        out.Text = text.data();
        out.TextSize = static_cast<int>(text.size());
        out.Blocks = blocks.data();
        out.BlockCount = static_cast<int>(blocks.size());
        out.Runs = runs.data();
        out.RunCount = static_cast<int>(runs.size());
        out.Styles = styles.data();
        out.StyleCount = static_cast<int>(styles.size());
        return out;
    }
};

// Everything that is counted, by what it is. Printed whole.
struct Tally {
    std::map<std::string, int> n;
    // Lengths of the description held to the golden's scaled points, by ==.
    int lengths = 0;
    // The furthest a point's float pixels are from its scaled points.
    Scaled furthest = 0;
    // And the furthest a point asked instead of one that crossed a block's edge is.
    Scaled furthest_across = 0;

    int of(const char* what) const
    {
        const auto found = n.find(what);
        return found == n.end() ? 0 : found->second;
    }
    void print_precision() const
    {
        std::printf("  %d lengths of the description held to the reference's scaled points by "
                    "==\n",
                    lengths);
        std::printf("  a point's float pixels are at most %lld sp from its scaled points; and the "
                    "points moved back across a block's edge are at most %lld sp from the "
                    "golden's\n",
                    static_cast<long long>(furthest), static_cast<long long>(furthest_across));
    }
    void print(const char* title) const
    {
        std::printf("%s\n", title);
        for (const auto& entry : n)
            std::printf("  %6d  %s\n", entry.second, entry.first.c_str());
    }
};

// --- The `blocks` group of FORMAT-doc.md §2.1 ---------------------------------------------

struct Piece {
    enum Kind { RUN, HARDBREAK, FORMULA };
    Kind kind = RUN;
    int start = 0;
    int end = 0;
    FontSpec font;
    bool breakable = true;
};

struct Link {
    int start = 0;
    int end = 0;
};

struct DocBlockIn {
    // The fields FORMAT-blocks.md's group shares, where its reader keeps them.
    blocks_golden::BlockIn in;
    int nest = -1;
    bool numbered = false;
    bool has_formula = false;
    // The block's text as FORMAT-doc.md §1 defines it.
    std::string content;
    std::string language;
    std::vector<Piece> pieces;
    std::vector<Link> links;
    // CODE: content byte -> source byte, with one more entry for the end. A
    // tab's spaces all have the tab's byte.
    std::vector<int> raw_of;
    // A code block a widget stands in for: its height, and what it copies as.
    bool hosted = false;
    std::string host_text;

    bool is_code() const { return in.type == BlockType::CODE && !hosted; }
    int size() const { return static_cast<int>(hosted ? host_text.size() : content.size()); }
    // A content byte as the product's offset.
    int offset(int byte) const
    {
        if (!is_code())
            return byte;
        const int at = std::min(std::max(byte, 0), static_cast<int>(content.size()));
        return raw_of[static_cast<size_t>(at)];
    }
    // The product's offset as a content byte: the first that comes from it.
    int byte_of(int offset_in) const
    {
        if (!is_code())
            return offset_in;
        const auto found = std::lower_bound(raw_of.begin(), raw_of.end(), offset_in);
        return static_cast<int>(found - raw_of.begin());
    }
    // False for a byte inside a tab's spaces: no offset of the source is there.
    bool expressible(int byte) const
    {
        if (!is_code() || byte <= 0 || byte >= static_cast<int>(content.size()))
            return true;
        return raw_of[static_cast<size_t>(byte)] != raw_of[static_cast<size_t>(byte) - 1];
    }
    // What the product copies for content bytes [a, b).
    std::string product_slice(int a, int b) const
    {
        if (hosted)
            return host_text;
        if (a >= b)
            return std::string();
        if (!is_code())
            return content.substr(static_cast<size_t>(a), static_cast<size_t>(b - a));
        const int from = offset(a);
        return in.source.substr(static_cast<size_t>(from), static_cast<size_t>(offset(b) - from));
    }
};

// The reference's tab expansion (FORMAT-blocks.md §3.2), keeping bytes: a tab
// is spaces to the next multiple of TAB_WIDTH columns, counted in characters.
void map_tabs(DocBlockIn& block)
{
    const std::string& raw = block.in.source;
    std::string expanded;
    std::vector<int> raw_of;
    uint32_t column = 0;
    for (size_t at = 0; at < raw.size();) {
        const unsigned char c = static_cast<unsigned char>(raw[at]);
        size_t next = at + 1;
        while (next < raw.size() && (static_cast<unsigned char>(raw[next]) & 0xC0) == 0x80)
            ++next;
        if (c == '\n') {
            expanded += '\n';
            raw_of.push_back(static_cast<int>(at));
            column = 0;
        } else if (c == '\t') {
            const uint32_t spaces = TAB_WIDTH - column % TAB_WIDTH;
            for (uint32_t s = 0; s < spaces; ++s) {
                expanded += ' ';
                raw_of.push_back(static_cast<int>(at));
            }
            column += spaces;
        } else {
            for (size_t k = at; k < next; ++k) {
                expanded += raw[k];
                raw_of.push_back(static_cast<int>(k));
            }
            ++column;
        }
        at = next;
    }
    const size_t size = block.content.size();
    if (!CHECK(expanded.size() >= size && expanded.compare(0, size, block.content) == 0)) {
        raw_of.assign(size + 1, 0);
        block.raw_of = raw_of;
        return;
    }
    raw_of.resize(size);
    raw_of.push_back(size == 0 ? 0 : raw_of.back() + 1);
    block.raw_of = raw_of;
}

struct BlocksIn {
    blocks_golden::ThemeIn theme;
    Scaled decor = 0;
    std::vector<DocBlockIn> blocks;
};

BlocksIn read_doc_blocks(core_golden::Reader& reader)
{
    BlocksIn out;
    out.theme = blocks_golden::read_theme(reader);
    out.decor = reader.take("decor").i64(0);
    const uint32_t count = reader.take("blocks").u32(0);
    while (!reader.done() && !reader.next_is("endblocks")) {
        DocBlockIn block;
        const golden::Record& open = reader.take();
        block.in.line = open.line;
        golden::Cursor cursor(open);
        if (cursor.u32() != out.blocks.size())
            check::fail(open.file, open.line, "blocks are not numbered in order");
        if (open.is("text")) {
            block.in.type = BlockType::TEXT;
            block.in.kind = std::string(cursor.raw());
            if (!blocks_golden::align_of(cursor.raw(), &block.in.align))
                check::fail(open.file, open.line, "not an Align");
            block.in.font = cursor.str();
            block.in.left_indent = cursor.i64();
            block.in.first_line_indent = cursor.i64();
            block.in.quote_depth = cursor.u32();
            block.in.space_above = cursor.i64();
            block.in.space_below = cursor.i64();
            cursor.expect_done();
            blocks_golden::read_style(reader, block.in);
            if (reader.next_is("marker")) {
                golden::Cursor marker(reader.take());
                block.in.marker_font = blocks_golden::spec_of(marker.str());
                block.in.marker_text = marker.str();
                block.numbered = marker.boolean();
                marker.expect_done();
                block.in.has_marker = true;
            }
            block.nest = reader.take("nest").i32(0);
            block.content = reader.take("content").str(2);
            while (!reader.done() && reader.next_is("piece")) {
                golden::Cursor piece(reader.take());
                const std::string_view what = piece.raw();
                Piece one;
                one.start = piece.i32();
                one.end = piece.i32();
                piece.i32();
                piece.i32();
                if (what == "run") {
                    one.font = blocks_golden::spec_of(piece.str());
                    one.breakable = piece.boolean();
                } else if (what == "hardbreak") {
                    one.kind = Piece::HARDBREAK;
                } else if (what == "formula") {
                    one.kind = Piece::FORMULA;
                    block.has_formula = true;
                    piece.boolean();
                    piece.str();
                } else {
                    check::fail(open.file, open.line, "a piece that is no run, break or formula");
                }
                piece.expect_done();
                block.pieces.push_back(one);
            }
            while (!reader.done() && reader.next_is("link")) {
                const golden::Record& link = reader.take();
                block.links.push_back(Link{link.i32(1), link.i32(2)});
            }
            while (!reader.done() && reader.next_is("formula"))
                reader.take();
            reader.take("endtext");
        } else if (open.is("code")) {
            block.in.type = BlockType::CODE;
            block.in.font = cursor.str();
            block.in.left_indent = cursor.i64();
            block.in.space_above = cursor.i64();
            block.in.space_below = cursor.i64();
            block.in.line_count = cursor.u32();
            block.in.source = cursor.str();
            cursor.expect_done();
            blocks_golden::read_style(reader, block.in);
            block.nest = reader.take("nest").i32(0);
            block.language = reader.take("language").str(0);
            block.content = reader.take("content").str(2);
            reader.take("endcode");
            map_tabs(block);
        } else if (open.is("rule")) {
            block.in.type = BlockType::RULE;
            block.in.left_indent = cursor.i64();
            block.in.thickness = cursor.i64();
            block.in.space_above = cursor.i64();
            block.in.space_below = cursor.i64();
            cursor.expect_done();
            blocks_golden::read_style(reader, block.in);
            block.nest = reader.take("nest").i32(0);
            reader.take("endrule");
        } else {
            check::fail(open.file, open.line, "expected a `text`, `code` or `rule` group");
            break;
        }
        out.blocks.push_back(std::move(block));
    }
    reader.take("endblocks");
    if (out.blocks.size() != count)
        check::fail(__FILE__, __LINE__, "`blocks` does not count its groups");
    return out;
}

using document_reference::px_float;

bool space_at(const std::string& text, bool at_end)
{
    if (text.empty())
        return false;
    std::vector<uint32_t> cps;
    decode_utf8(text, cps);
    return is_space(at_end ? cps.back() : cps.front());
}

bool all_space(const std::string& text)
{
    std::vector<uint32_t> cps;
    decode_utf8(text, cps);
    for (const uint32_t c : cps) {
        if (!is_space(c))
            return false;
    }
    return true;
}

// The golden's blocks as the public structs. Returns why they cannot be
// described, or null. `style` is the document's.
const char* describe(const BlocksIn& doc, Content& content, ImTypesetStyle& style)
{
    const blocks_golden::ThemeIn& theme = doc.theme;
    if (theme.composition.regular != Face::PROSE || theme.composition.code != Face::CODE)
        return "a theme whose stand-in faces are not the defaults";
    const double ppi = theme.px_per_inch;
    // The document's size: the body's, which most blocks have; every block
    // says its own as a multiple of it.
    std::map<Scaled, int> sizes;
    for (const DocBlockIn& block : doc.blocks) {
        if (block.in.type != BlockType::RULE)
            ++sizes[blocks_golden::spec_of(block.in.font).size];
    }
    Scaled base = 12 * UNITY;
    int most = 0;
    for (const auto& entry : sizes) {
        if (entry.second > most) {
            most = entry.second;
            base = entry.first;
        }
    }
    style.PixelsPerInch = static_cast<float>(ppi);
    style.EmSize = px_float(base, ppi);
    style.Spacing =
        theme.composition.spacing == Spacing::FRENCH
            ? ImTypesetSpacing_French
            : (theme.composition.spacing == Spacing::NONFRENCH ? ImTypesetSpacing_TeX
                                                               : ImTypesetSpacing_Sentence);
    for (size_t i = 0; i < doc.blocks.size(); ++i) {
        const DocBlockIn& block = doc.blocks[i];
        const blocks_golden::BlockIn& in = block.in;
        if (in.space_above < 0 || in.space_below < 0)
            return "a negative space, which the header does not take";
        ImTypesetBlockStyle own;
        own.SpaceAbove = px_float(in.space_above, ppi);
        own.SpaceBelow = px_float(in.space_below, ppi);
        own.MarkerGap = px_float(theme.marker_gap, ppi);
        if (in.type == BlockType::RULE || block.hosted) {
            content.styles.push_back(own);
            ImTypesetBlock& out =
                content.add(block.hosted ? ImTypesetBlockKind_Host : ImTypesetBlockKind_Rule,
                            block.hosted ? block.host_text : std::string(), static_cast<int>(i));
            out.LeftIndent = px_float(in.left_indent, ppi);
            out.Extent = px_float(in.thickness, ppi);
            if (!block.hosted && in.thickness <= 0)
                return "a rule of no thickness, which the header reads as the default";
            continue;
        }
        const FontSpec font = blocks_golden::spec_of(in.font);
        own.Face = static_cast<ImTypesetFace>(font.face) + 1;
        own.Scale = static_cast<float>(static_cast<double>(font.size) / static_cast<double>(base));
        own.LineHeight = static_cast<float>(static_cast<double>(in.interline.baselineskip.width) /
                                            static_cast<double>(font.size));
        own.Align = in.align == Align::LEFT     ? ImTypesetAlign_Left
                    : in.align == Align::CENTER ? ImTypesetAlign_Center
                    : in.align == Align::RIGHT  ? ImTypesetAlign_Right
                                                : ImTypesetAlign_Justify;
        own.FirstLineIndent = px_float(in.first_line_indent, ppi);
        if (in.has_marker)
            own.MarkerFace = static_cast<ImTypesetFace>(in.marker_font.face) + 1;
        if (in.has_marker && in.marker_font.size != font.size)
            return "a marker of another size than its block";
        content.styles.push_back(own);

        if (in.type == BlockType::CODE) {
            ImTypesetBlock& out =
                content.add(ImTypesetBlockKind_Code, in.source, static_cast<int>(i));
            out.LeftIndent = px_float(in.left_indent, ppi);
            continue;
        }
        bool any_hard = false;
        for (const Piece& piece : block.pieces)
            any_hard = any_hard || piece.kind == Piece::HARDBREAK;
        bool nothing_yet = true;
        bool after_space = false;
        bool line_start = false;
        for (const Piece& piece : block.pieces) {
            const std::string text = block.content.substr(
                static_cast<size_t>(piece.start), static_cast<size_t>(piece.end - piece.start));
            if (piece.kind == Piece::HARDBREAK) {
                if (after_space)
                    return "white space before a hard break, which the API drops";
                if (text != "\n")
                    return "a hard break that is not one newline of the text";
                line_start = true;
                after_space = false;
                continue;
            }
            if (piece.kind == Piece::FORMULA) {
                nothing_yet = false;
                line_start = false;
                after_space = false;
                continue;
            }
            if (block.has_formula && text.find('$') != std::string::npos)
                return "a dollar that is text beside a formula";
            if (any_hard && text.find_first_of("\r\n") != std::string::npos)
                return "a newline inside a run beside hard breaks";
            if (text.empty())
                continue;
            if (space_at(text, false) && (nothing_yet || after_space || line_start))
                return "white space the API collapses and the reference's raw runs do not";
            if (!all_space(text)) {
                nothing_yet = false;
                line_start = false;
            }
            after_space = space_at(text, true);
        }
        if (after_space)
            return "white space the API collapses and the reference's raw runs do not";
        ImTypesetBlock& out =
            content.add(in.has_marker ? ImTypesetBlockKind_ListItem : ImTypesetBlockKind_Paragraph,
                        block.content, static_cast<int>(i));
        out.LeftIndent = px_float(in.left_indent, ppi);
        out.QuoteDepth = static_cast<int>(in.quote_depth);
        if (any_hard)
            out.Flags |= ImTypesetBlockFlags_HardBreaks;
        if (block.has_formula)
            out.Flags |= ImTypesetBlockFlags_Math;
        for (const Piece& piece : block.pieces) {
            if (piece.kind != Piece::RUN || piece.start == piece.end)
                continue;
            ImTypesetRun& run = content.run(piece.start, piece.end);
            run.Face = static_cast<ImTypesetFace>(piece.font.face) + 1;
            if (piece.font.size != font.size)
                run.Scale = static_cast<float>(static_cast<double>(piece.font.size) /
                                               static_cast<double>(font.size));
            if (!piece.breakable)
                run.Flags |= ImTypesetRunFlags_NoBreak;
        }
        if (in.has_marker)
            content.marker(in.marker_text);
    }
    return nullptr;
}

// --- A layout section ----------------------------------------------------------------------

struct BSpan {
    int64_t start = 0;
    int64_t end = 0;
    Scaled x = 0;
    Scaled y = 0;
    Scaled width = 0;
    Scaled height = 0;
};

struct SepRow {
    int64_t index = 0;
    int block = 0;
    int byte = 0;
    std::string text;
};

struct RectRow {
    Scaled x = 0;
    Scaled y = 0;
    Scaled width = 0;
    Scaled height = 0;
};

struct RangeRow {
    int line = 0;
    int64_t start = 0;
    int64_t end = 0;
    bool has_span = true;
    int span[4] = {-1, -1, -1, -1};
    std::string copy;
    bool has_rects = false;
    std::vector<RectRow> rects;
};

struct DragRow {
    int line = 0;
    Scaled ax = 0, ay = 0, bx = 0, by = 0;
    std::string granularity;
    int64_t start = 0;
    int64_t end = 0;
};

struct ProbeRow {
    int line = 0;
    Scaled x = 0, y = 0;
    int64_t hit = 0;
    int block_at = -1;
    bool has_units = false;
    int64_t word[2] = {0, 0};
    int64_t line_range[2] = {0, 0};
};

// A stretch of one block's text with the rectangles the reference gives it: a
// find hit, a link, a colour span.
struct SpanRow {
    int line = 0;
    int block = 0;
    int a = 0;
    int b = 0;
    int number = 0;
    bool has_mark = false;
    std::vector<RectRow> rects;
};

struct FillRow {
    RectRow rect;
    uint32_t colour = 0;
};

struct HostedRow {
    int block = 0;
    Scaled height = 0;
    float height_px = 0.0f;
    std::string text;
};

struct Stacking {
    std::vector<HostedRow> hosted;
    Scaled height = 0;
    int64_t length = 0;
    std::vector<BSpan> bspan;
    std::vector<int> built;
    bool has_built = false;
};

struct LayoutIn {
    std::string name;
    int line = 0;
    Scaled width = 0;
    Stacking now;
    bool has_resized = false;
    Stacking resized;
    std::vector<std::string> cmds;
    // Per index: the block that owns it, its byte in that block's text (-1
    // for an item the text does not hold) and how many bytes it is.
    std::vector<int> block_of;
    std::vector<int> byte;
    std::vector<int> length;
    std::vector<SepRow> seps;
    std::vector<DragRow> drags;
    std::vector<ProbeRow> probes;
    std::vector<RangeRow> ranges;
    std::vector<SpanRow> matches;
    std::vector<SpanRow> linkareas;
    std::vector<SpanRow> colours;
    std::vector<FillRow> fills;
    std::vector<std::string> blocktext;
    std::vector<std::string> copyall;
    std::string selectall;
    bool has_selectall = false;

    // --- Derived
    // Per block: where its text starts among the document's bytes.
    std::vector<int64_t> prefix;
    // Per index and one more: the first byte of text at or after it.
    std::vector<int64_t> caret;
};

// Reads from `layout` to `endlayout`. `blocks` gives the sizes of the texts.
LayoutIn read_layout(core_golden::Reader& reader, core_golden::Context& context,
                     const std::vector<DocBlockIn>& blocks)
{
    LayoutIn out;
    const golden::Record& open = reader.take("layout");
    out.name = open.str(0);
    out.width = open.i64(1);
    out.line = open.line;
    Stacking* stacking = &out.now;
    // Where the `rect` rows that follow go.
    std::vector<RectRow>* rects_to = nullptr;
    int colours_block = 0;
    while (!reader.done() && !reader.next_is("endlayout")) {
        if (reader.next_is("cmds")) {
            const size_t first = reader.position();
            core_golden::read_cmds(reader, context);
            out.cmds = reader.lines(first, reader.position());
            continue;
        }
        const golden::Record& r = reader.take();
        if (r.is("size")) {
            stacking->height = r.i64(0);
            stacking->length = r.i64(1);
        } else if (r.is("bspan")) {
            stacking->bspan.push_back(
                BSpan{r.i64(1), r.i64(2), r.i64(3), r.i64(4), r.i64(5), r.i64(6)});
        } else if (r.is("hosted")) {
            HostedRow row;
            row.block = r.i32(0);
            row.height_px = static_cast<float>(r.f64(3));
            row.height = r.i64(4);
            row.text = r.str(9);
            stacking->hosted.push_back(row);
        } else if (r.is("built")) {
            stacking->has_built = true;
            for (size_t f = 1; f < r.size(); ++f)
                stacking->built.push_back(r.i32(f));
        } else if (r.is("resized")) {
            out.has_resized = true;
            stacking = &out.resized;
        } else if (r.is("endresized")) {
            stacking = &out.now;
        } else if (r.is("imap")) {
            const int block = r.i32(0);
            const int64_t start = r.i64(1);
            const int64_t count = r.i64(2);
            if (static_cast<int64_t>(out.byte.size()) != start)
                check::fail(r.file, r.line, "`imap` rows do not tile the indices");
            for (int64_t k = 0; k < count; ++k) {
                out.block_of.push_back(block);
                out.byte.push_back(r.i32(static_cast<size_t>(3 + 2 * k)));
                out.length.push_back(r.i32(static_cast<size_t>(4 + 2 * k)));
            }
        } else if (r.is("unmapped")) {
            if (r.i64(0) != 0)
                check::fail(r.file, r.line, "a block the reference could not map");
        } else if (r.is("sep")) {
            out.seps.push_back(SepRow{r.i64(0), r.i32(1), r.i32(2), r.str(3)});
        } else if (r.is("drag")) {
            DragRow row;
            row.line = r.line;
            row.ax = r.i64(0);
            row.ay = r.i64(1);
            row.bx = r.i64(2);
            row.by = r.i64(3);
            row.granularity = std::string(r.raw(4));
            row.start = r.i64(5);
            row.end = r.i64(6);
            out.drags.push_back(row);
        } else if (r.is("probe")) {
            ProbeRow row;
            row.line = r.line;
            row.x = r.i64(0);
            row.y = r.i64(1);
            row.hit = r.i64(2);
            row.block_at = r.i32(3);
            if (r.size() >= 10) {
                row.has_units = true;
                row.word[0] = r.i64(6);
                row.word[1] = r.i64(7);
                row.line_range[0] = r.i64(8);
                row.line_range[1] = r.i64(9);
            }
            out.probes.push_back(row);
        } else if (r.is("range")) {
            RangeRow row;
            row.line = r.line;
            row.start = r.i64(0);
            row.end = r.i64(1);
            for (size_t k = 0; k < 4; ++k)
                row.span[k] = r.i32(2 + k);
            out.ranges.push_back(row);
            rects_to = &out.ranges.back().rects;
        } else if (r.is("match")) {
            SpanRow row;
            row.line = r.line;
            row.block = r.i32(0);
            row.a = r.i32(3);
            row.b = r.i32(4);
            out.matches.push_back(row);
            rects_to = &out.matches.back().rects;
        } else if (r.is("linkarea")) {
            SpanRow row;
            row.line = r.line;
            row.block = r.i32(0);
            row.number = r.i32(1);
            out.linkareas.push_back(row);
            rects_to = &out.linkareas.back().rects;
        } else if (r.is("colours")) {
            colours_block = r.i32(0);
        } else if (r.is("colour")) {
            SpanRow row;
            row.line = r.line;
            row.block = colours_block;
            row.has_mark = r.raw(3) != "-1";
            row.a = r.i32(4);
            row.b = r.i32(5);
            out.colours.push_back(row);
        } else if (r.is("fill")) {
            out.fills.push_back(FillRow{RectRow{r.i64(0), r.i64(1), r.i64(2), r.i64(3)}, r.u32(4)});
        } else if (r.is("copy")) {
            if (!out.ranges.empty())
                out.ranges.back().copy = r.str(0);
        } else if (r.is("rects")) {
            if (!out.ranges.empty())
                out.ranges.back().has_rects = true;
        } else if (r.is("rect")) {
            if (rects_to != nullptr)
                rects_to->push_back(RectRow{r.i64(0), r.i64(1), r.i64(2), r.i64(3)});
        } else if (r.is("blocktext")) {
            out.blocktext.push_back(r.str(1));
        } else if (r.is("copyall")) {
            out.copyall.push_back(r.str(1));
        } else if (r.is("selectall")) {
            out.has_selectall = true;
            out.selectall = r.str(r.size() - 1);
        }
        // unit, lead, gap, composed, atomtext, original, edge_hosts and the
        // counting rows: not needed to ask the product anything.
    }
    reader.take("endlayout");

    out.prefix.assign(blocks.size() + 1, 0);
    for (size_t b = 0; b < blocks.size(); ++b) {
        // A widget's text is no part of the reference's index space.
        const int size = blocks[b].hosted ? 0 : static_cast<int>(blocks[b].content.size());
        out.prefix[b + 1] = out.prefix[b] + size;
    }
    out.caret.assign(out.byte.size() + 1, out.prefix.back());
    for (size_t i = out.byte.size(); i-- > 0;) {
        out.caret[i] = out.byte[i] >= 0
                           ? out.prefix[static_cast<size_t>(out.block_of[i])] + out.byte[i]
                           : out.caret[i + 1];
    }
    return out;
}

// --- One case, described and laid out ----------------------------------------------------

struct Replayed {
    BlocksIn doc;
    Content content;
    ImTypesetStyle style;
    ResolvedStyle resolved;
    ImTypesetDocument* document = nullptr;
    double ppi = 96.0;
    std::map<std::string, float> widths;
    bool links_styled = false;

    Replayed() = default;
    Replayed(const Replayed&) = delete;
    Replayed& operator=(const Replayed&) = delete;
    ~Replayed() { ImTypeset::DestroyDocument(document); }

    // A point as the float pixels nearest it. `seen_y` is the height the
    // product reads back from them, which is not always `y`.
    ImVec2 point(Scaled x, Scaled y, Tally& tally, Scaled* seen_y) const
    {
        const ImVec2 out(px_float(x, ppi), px_float(y, ppi));
        const Scaled back_x = sp_from_px(static_cast<double>(out.x), ppi);
        *seen_y = sp_from_px(static_cast<double>(out.y), ppi);
        ++tally.n["points handed over as float pixels"];
        if (back_x == x && *seen_y == y)
            ++tally.n["points whose float pixels are the same scaled point"];
        const Scaled apart =
            std::max(back_x > x ? back_x - x : x - back_x, *seen_y > y ? *seen_y - y : y - *seen_y);
        tally.furthest = std::max(tally.furthest, apart);
        return out;
    }
    // A product position as a byte of the document's text (see TWO SPACES).
    int64_t caret(const LayoutIn& layout, const ImTypesetPosition& position) const
    {
        if (position.Block < 0)
            return 0;
        if (static_cast<size_t>(position.Block) >= doc.blocks.size())
            return layout.prefix.back();
        const size_t b = static_cast<size_t>(position.Block);
        if (doc.blocks[b].hosted)
            return layout.prefix[b];
        return layout.prefix[b] + doc.blocks[b].byte_of(position.Offset);
    }
};

// Reads a case's INPUT rows and its `blocks`, and hands them to a document.
// Returns why it could not, or null; the reader is left at the first layout.
const char* open_case(Stage& stage, core_golden::Reader& reader, Replayed& out)
{
    struct Fence {
        std::string language;
        float height_px;
        std::string text;
    };
    std::vector<Fence> fences;
    while (!reader.done() && !reader.next_is("expect")) {
        const golden::Record& r = reader.take();
        if (r.is("width"))
            out.widths[r.str(0)] = static_cast<float>(r.f64(1));
        else if (r.is("links"))
            out.links_styled = r.boolean(0);
        else if (r.is("hostfence"))
            fences.push_back(Fence{r.str(0), static_cast<float>(r.f64(1)), r.str(2)});
    }
    reader.take("expect");
    out.doc = read_doc_blocks(reader);
    // The code blocks a widget claims: by language.
    for (DocBlockIn& block : out.doc.blocks) {
        if (block.in.type != BlockType::CODE)
            continue;
        for (const Fence& fence : fences) {
            if (fence.language != block.language)
                continue;
            block.hosted = true;
            block.host_text = fence.text;
            block.in.thickness =
                sp_from_px(static_cast<double>(fence.height_px), out.doc.theme.px_per_inch);
        }
    }
    out.ppi = out.doc.theme.px_per_inch;
    if (const char* why = describe(out.doc, out.content, out.style))
        return why;
    out.document = ImTypeset::CreateDocument();
    if (out.document == nullptr)
        return "no document";
    if (const char* why = document_reference::use_reference(out.document, out.doc.theme))
        return why;
    const ImTypesetDocumentContent view = out.content.view();
    CHECK(ImTypeset::SetDocumentBlocks(out.document, &view));
    CHECK_EQ(ImTypeset::GetDocumentBlockCount(out.document),
             static_cast<int>(out.doc.blocks.size()));
    out.resolved = resolve_style(*stage.ctx, out.style);
    return nullptr;
}

// Lays the document out at a layout's width and holds it to the golden's
// commands and height. False if it is not the reference's layout: nothing is
// then asked of it.
bool lay_out(Replayed& replay, const LayoutIn& layout, core_golden::Context& context, Tally& tally)
{
    const auto found = replay.widths.find(layout.name);
    if (!CHECK(found != replay.widths.end()))
        return false;
    const float width_px = found->second;
    if (resolve_width(width_px, replay.resolved) != layout.width) {
        ++tally.n["SKIPPED layouts: a width that is not a float number of pixels"];
        return false;
    }
    CHECK(ImTypeset::LayoutDocument(replay.document, width_px, &replay.style));
    if (!ImTypeset::LayoutDocumentWindow(replay.document, 0.0f, 1.0e9f))
        FAIL("LayoutDocumentWindow: %s", ImTypeset::GetLastError()->Message);
    Document& inner = block_document(*replay.document);
    if (!CHECK(inner.settled()))
        return false;
    document_reference::hold_theme(*replay.document, replay.doc.theme, tally.lengths);
    for (size_t b = 0; b < replay.doc.blocks.size(); ++b)
        document_reference::hold_block(*replay.document, b, replay.doc.blocks[b].in,
                                       replay.doc.blocks[b].hosted, replay.doc.theme,
                                       tally.lengths);
    std::vector<DrawCmd> all;
    std::vector<BlockSpan> spans;
    if (!CHECK(inner.flatten(all, &spans)) || !CHECK_EQ(spans.size(), replay.doc.blocks.size()))
        return false;
    // A widget's block is, in the block layer, a rule that gives it its
    // height and is never drawn (src/imtypeset_document.cpp `draw_block`);
    // the reference's has no commands at all. Its commands are set aside.
    std::vector<bool> aside(all.size(), false);
    for (size_t b = 0; b < spans.size(); ++b) {
        if (!replay.doc.blocks[b].hosted)
            continue;
        for (size_t k = spans[b].cmd_start; k < spans[b].cmd_end && k < all.size(); ++k)
            aside[k] = true;
        tally.n["commands of widgets' blocks set aside (the block layer's, never drawn)"] +=
            static_cast<int>(spans[b].cmd_end - spans[b].cmd_start);
    }
    std::vector<DrawCmd> flat;
    for (size_t k = 0; k < all.size(); ++k) {
        if (!aside[k])
            flat.push_back(all[k]);
    }
    bool same = inner.height() == layout.now.height;
    // The indices each block owns: `bspan`.
    for (size_t b = 0; b < spans.size() && b < layout.now.bspan.size(); ++b) {
        same =
            CHECK_EQ(static_cast<int64_t>(spans[b].index_start), layout.now.bspan[b].start) && same;
        same = CHECK_EQ(static_cast<int64_t>(spans[b].index_end), layout.now.bspan[b].end) && same;
    }
    CHECK_EQ(inner.height(), layout.now.height);
    if (!layout.cmds.empty())
        same = CHECK_LINES_EQ(layout.cmds, blocks_golden::written(context, flat),
                              "the document, described through the public header and flattened") &&
               same;
    if (same) {
        ++tally.n["layouts whose commands and height are the reference's, exactly"];
        tally.n["commands compared"] += static_cast<int>(flat.size());
    }
    return same;
}

// --- Questions -------------------------------------------------------------------------------

// The block a height is in by the golden's rectangles; -1 between two blocks;
// -2 above the document or below it. `lower` is the first block that ends
// below the height (the product's rule for a point between two).
int block_by_height(const Stacking& stacking, Scaled y, int* lower)
{
    *lower = -1;
    if (y < 0 || y >= stacking.height)
        return -2;
    for (size_t b = 0; b < stacking.bspan.size(); ++b) {
        const BSpan& span = stacking.bspan[b];
        if (y < span.y + span.height) {
            *lower = static_cast<int>(b);
            return y >= span.y ? static_cast<int>(b) : -1;
        }
    }
    return -2;
}

ImTypesetGranularity granularity_of(const std::string& token)
{
    return token == "word"   ? ImTypesetGranularity_Word
           : token == "line" ? ImTypesetGranularity_Line
                             : ImTypesetGranularity_Character;
}

bool same_position(const ImTypesetPosition& a, const ImTypesetPosition& b)
{
    return a.Block == b.Block && a.Offset == b.Offset;
}

void ask_probes(Replayed& replay, const LayoutIn& layout, Tally& tally)
{
    for (const ProbeRow& probe : layout.probes) {
        CHECK_CONTEXT("probe (line %d)", probe.line);
        ++tally.n["probe: asked of HitTestDocument"];
        Scaled seen_y = 0;
        ImVec2 at = replay.point(probe.x, probe.y, tally, &seen_y);
        int lower = -1;
        int lower_exact = -1;
        // Where the point is as the product reads its float pixels back.
        int where = block_by_height(layout.now, seen_y, &lower);
        const int where_exact = block_by_height(layout.now, probe.y, &lower_exact);
        if (where != where_exact) {
            // FLOAT PIXELS AT A BOUNDARY, the one tolerance of this file. The
            // golden's point is on a block's first or last scaled point and
            // the float nearest it reads back on the other side of the edge.
            // The product is first held to its own rule for the point it
            // gets; then it is asked the nearest float that reads back on the
            // golden's side, and how far that is from the golden's point is
            // counted and printed. Only the height moves, and stays within
            // the same line.
            const ImTypesetDocumentHit across = ImTypeset::HitTestDocument(replay.document, at);
            CHECK_EQ(across.Position.Block, where >= 0 ? where : lower);
            CHECK(across.InBlock == (where >= 0));
            const float toward = seen_y > probe.y ? -1.0e9f : 1.0e9f;
            for (int step = 0; step < 4 && where != where_exact; ++step) {
                at.y = std::nextafter(at.y, toward);
                seen_y = sp_from_px(static_cast<double>(at.y), replay.ppi);
                where = block_by_height(layout.now, seen_y, &lower);
            }
            if (!CHECK_EQ(where, where_exact))
                continue;
            ++tally.n["probe: asked one float away, the nearest that is on the golden's side of a "
                      "block's edge"];
            tally.furthest_across = std::max(
                tally.furthest_across, seen_y > probe.y ? seen_y - probe.y : probe.y - seen_y);
        }
        const ImTypesetDocumentHit hit = ImTypeset::HitTestDocument(replay.document, at);
        const int64_t expected = layout.caret[static_cast<size_t>(probe.hit)];
        const int64_t actual = replay.caret(layout, hit.Position);
        const ImTypesetDocumentRange word =
            ImTypeset::SelectDocumentRange(replay.document, at, at, ImTypesetGranularity_Word);
        const ImTypesetDocumentRange line =
            ImTypeset::SelectDocumentRange(replay.document, at, at, ImTypesetGranularity_Line);
        const bool units_agree =
            probe.has_units &&
            replay.caret(layout, word.Start) == layout.caret[static_cast<size_t>(probe.word[0])] &&
            replay.caret(layout, word.End) == layout.caret[static_cast<size_t>(probe.word[1])] &&
            replay.caret(layout, line.Start) ==
                layout.caret[static_cast<size_t>(probe.line_range[0])] &&
            replay.caret(layout, line.End) ==
                layout.caret[static_cast<size_t>(probe.line_range[1])];
        if (where == -1) {
            // PRODUCT RULE: between two blocks a point is the lower block's.
            // The reference gives it to the nearest text, above or below.
            ++tally.n["probe: REFERENCE-ONLY, a point between two blocks (the product gives it to "
                      "the lower block; asserted)"];
            CHECK_EQ(hit.Position.Block, lower);
            CHECK(!hit.InBlock);
            if (actual == expected)
                ++tally.n["probe:   ...of which the caret is the reference's all the same"];
            continue;
        }
        if (where >= 0) {
            CHECK_EQ(probe.block_at, where);
            CHECK_EQ(hit.Position.Block, where);
            CHECK(hit.InBlock);
        } else {
            CHECK(!hit.InBlock);
            CHECK_EQ(hit.Position.Block,
                     probe.y < 0 ? 0 : static_cast<int>(replay.doc.blocks.size()) - 1);
        }
        const DocBlockIn& block = replay.doc.blocks[static_cast<size_t>(hit.Position.Block)];
        if (block.hosted || block.in.type == BlockType::RULE) {
            // PRODUCT RULE: a block without text answers its own top or
            // bottom. The reference answers the nearest text's boundary.
            ++tally.n["probe: REFERENCE-ONLY, a point in a block without text (the product "
                      "answers that block; asserted)"];
            CHECK(hit.Position.Offset == 0 || hit.Position.Offset == block.size());
            if (actual == expected)
                ++tally.n["probe:   ...of which the caret is the reference's all the same"];
            continue;
        }
        if (CHECK_EQ(actual, expected))
            ++tally.n["probe: caret equal to the reference's `hit`"];
        if (!probe.has_units)
            continue;
        ++tally.n["probe: word and line asked of SelectDocumentRange"];
        if (CHECK(units_agree))
            ++tally.n["probe: word and line equal to the reference's `word_at`, `line_at`"];
    }
}

void ask_drags(Replayed& replay, const LayoutIn& layout, Tally& tally)
{
    for (const DragRow& drag : layout.drags) {
        CHECK_CONTEXT("drag (line %d)", drag.line);
        ++tally.n["drag: asked of SelectDocumentRange"];
        Scaled seen_ay = 0;
        Scaled seen_by = 0;
        const ImVec2 a = replay.point(drag.ax, drag.ay, tally, &seen_ay);
        const ImVec2 b = replay.point(drag.bx, drag.by, tally, &seen_by);
        const ImTypesetGranularity granularity = granularity_of(drag.granularity);
        const ImTypesetDocumentRange got =
            ImTypeset::SelectDocumentRange(replay.document, a, b, granularity);
        const bool equal =
            replay.caret(layout, got.Start) == layout.caret[static_cast<size_t>(drag.start)] &&
            replay.caret(layout, got.End) == layout.caret[static_cast<size_t>(drag.end)];
        int lower_a = -1;
        int lower_b = -1;
        int unused = -1;
        const int where_a = block_by_height(layout.now, seen_ay, &lower_a);
        const int where_b = block_by_height(layout.now, seen_by, &lower_b);
        if (where_a != block_by_height(layout.now, drag.ay, &unused) ||
            where_b != block_by_height(layout.now, drag.by, &unused)) {
            ++tally.n["drag: NOT COMPARED, float pixels put an end across a block's edge"];
            tally.furthest_across =
                std::max(tally.furthest_across,
                         std::max(seen_ay > drag.ay ? seen_ay - drag.ay : drag.ay - seen_ay,
                                  seen_by > drag.by ? seen_by - drag.by : drag.by - seen_by));
            continue;
        }
        if (where_a == -1 || where_b == -1) {
            ++tally.n["drag: REFERENCE-ONLY, an end between two blocks (the product gives it to "
                      "the lower block; asserted)"];
            if (equal)
                ++tally.n["drag:   ...of which the range is the reference's all the same"];
            // The product's rule, stated by its own hit test: each end is the
            // lower block's, and a character drag is the two hits in order.
            const ImTypesetDocumentHit hit_a = ImTypeset::HitTestDocument(replay.document, a);
            const ImTypesetDocumentHit hit_b = ImTypeset::HitTestDocument(replay.document, b);
            if (where_a == -1)
                CHECK_EQ(hit_a.Position.Block, lower_a);
            if (where_b == -1)
                CHECK_EQ(hit_b.Position.Block, lower_b);
            if (granularity == ImTypesetGranularity_Character) {
                CHECK((same_position(got.Start, hit_a.Position) &&
                       same_position(got.End, hit_b.Position)) ||
                      (same_position(got.Start, hit_b.Position) &&
                       same_position(got.End, hit_a.Position)));
            }
            continue;
        }
        const int block_a = where_a >= 0 ? where_a : (drag.ay < 0 ? 0 : lower_a);
        const int block_b = where_b >= 0 ? where_b : (drag.by < 0 ? 0 : lower_b);
        bool textless = false;
        for (const int which : {block_a, block_b}) {
            if (which < 0)
                continue;
            const DocBlockIn& block = replay.doc.blocks[static_cast<size_t>(which)];
            textless = textless || block.hosted || block.in.type == BlockType::RULE;
        }
        if (textless) {
            ++tally.n["drag: REFERENCE-ONLY, an end in a block without text"];
            continue;
        }
        const std::string key = "drag: " + drag.granularity;
        ++tally.n[key + ", both ends in blocks"];
        if (equal) {
            ++tally.n[key + ", range equal to the reference's"];
        } else {
            FAIL("%s drag: the product selects (%d, %d)-(%d, %d), bytes %lld to %lld of the "
                 "document; the reference [%lld, %lld), bytes %lld to %lld",
                 drag.granularity.c_str(), got.Start.Block, got.Start.Offset, got.End.Block,
                 got.End.Offset, static_cast<long long>(replay.caret(layout, got.Start)),
                 static_cast<long long>(replay.caret(layout, got.End)),
                 static_cast<long long>(drag.start), static_cast<long long>(drag.end),
                 static_cast<long long>(layout.caret[static_cast<size_t>(drag.start)]),
                 static_cast<long long>(layout.caret[static_cast<size_t>(drag.end)]));
        }
    }
}

// The reference's indices of block `b` as the product maps them to bytes:
// each item's byte, an item the text does not hold taking the next one's, and
// one entry more for the end (src/imtypeset.cpp `map_index_bytes`). For a code
// block the bytes are the source's.
std::vector<int> index_bytes(const Replayed& replay, const LayoutIn& layout, size_t b,
                             const BSpan& span)
{
    const DocBlockIn& block = replay.doc.blocks[b];
    const size_t count = static_cast<size_t>(span.end - span.start);
    std::vector<int> out(count + 1, block.offset(block.size()));
    for (size_t k = count; k-- > 0;) {
        const int byte = layout.byte[static_cast<size_t>(span.start) + k];
        out[k] = byte >= 0 ? block.offset(byte) : out[k + 1];
    }
    return out;
}

// src/imtypeset.cpp `index_at_byte`, over the golden's map: the index a
// product offset names.
int64_t index_named(const std::vector<int>& bytes, int offset_in)
{
    const auto found = std::lower_bound(bytes.begin(), bytes.end(), offset_in);
    if (found == bytes.end())
        return static_cast<int64_t>(bytes.size()) - 1;
    const auto last = std::upper_bound(found, bytes.end(), *found) - 1;
    return last - bytes.begin();
}

std::string escaped(const std::string& text)
{
    std::string out;
    for (const char c : text) {
        if (c == '\n')
            out += "\\n";
        else if (c == '\t')
            out += "\\t";
        else
            out += c;
    }
    return out;
}

struct BlockPart {
    size_t block = 0;
    // The reference's indices of this block that the range holds.
    int64_t start = 0;
    int64_t end = 0;
    // Its text among them, as content bytes; `has_text` false if none.
    bool has_text = false;
    int a = 0;
    int b = 0;
    int64_t first_index = 0;
    int64_t last_index = 0;
};

// One `range`: copy and rectangles. `rects` false where the golden has none.
void ask_range(Replayed& replay, const LayoutIn& layout, const Stacking& stacking,
               const RangeRow& range, Tally& tally)
{
    CHECK_CONTEXT("range [%lld, %lld) (line %d)", static_cast<long long>(range.start),
                  static_cast<long long>(range.end), range.line);
    const std::vector<DocBlockIn>& blocks = replay.doc.blocks;
    ++tally.n["range: read"];
    std::vector<BlockPart> parts;
    for (size_t b = 0; b < stacking.bspan.size(); ++b) {
        const BSpan& span = stacking.bspan[b];
        BlockPart part;
        part.block = b;
        part.start = std::max(range.start, span.start);
        part.end = std::min(range.end, span.end);
        if (part.start >= part.end)
            continue;
        for (int64_t i = part.start; i < part.end; ++i) {
            const int byte = layout.byte[static_cast<size_t>(i)];
            if (byte < 0)
                continue;
            if (!part.has_text) {
                part.a = byte;
                part.first_index = i;
            }
            part.has_text = true;
            part.b = byte + layout.length[static_cast<size_t>(i)];
            part.last_index = i;
        }
        parts.push_back(part);
    }
    const BlockPart* first = nullptr;
    const BlockPart* last = nullptr;
    for (const BlockPart& part : parts) {
        if (!part.has_text)
            continue;
        if (first == nullptr)
            first = &part;
        last = &part;
    }
    if (first == nullptr) {
        // Nothing a (block, byte) range can name: an empty range, or only a
        // marker or a line's fill.
        ++tally.n[range.start == range.end
                      ? "range: REFERENCE-ONLY, empty (nothing to ask)"
                      : "range: REFERENCE-ONLY, it holds no text (a marker or a line's fill "
                        "alone), which no (block, byte) range names"];
        CHECK_EQ(range.span[0], -1);
        return;
    }
    if (range.has_span) {
        CHECK_EQ(range.span[0], static_cast<int>(first->block));
        CHECK_EQ(range.span[1], first->a);
        CHECK_EQ(range.span[2], static_cast<int>(last->block));
        CHECK_EQ(range.span[3], last->b);
    }
    if (!blocks[first->block].expressible(first->a) || !blocks[last->block].expressible(last->b)) {
        ++tally.n["range: REFERENCE-ONLY, an end inside a tab's spaces (the product's offsets are "
                  "the source's)"];
        return;
    }
    ImTypesetDocumentRange asked;
    asked.Start.Block = static_cast<int>(first->block);
    asked.Start.Offset = blocks[first->block].offset(first->a);
    asked.End.Block = static_cast<int>(last->block);
    asked.End.Offset = blocks[last->block].offset(last->b);
    ++tally.n["range: asked of AppendDocumentRangeText"];

    // --- The copy, by the product's rule: each block's bytes of the range,
    // a widget's text whole, joined by a blank line.
    std::string product;
    for (size_t b = first->block; b <= last->block; ++b) {
        const int a = b == first->block ? first->a : 0;
        const int e = b == last->block ? last->b : static_cast<int>(blocks[b].content.size());
        const std::string slice = blocks[b].product_slice(a, e);
        if (slice.empty())
            continue;
        if (!product.empty())
            product += "\n\n";
        product += slice;
    }
    ImGuiTextBuffer buffer;
    ImTypeset::AppendDocumentRangeText(replay.document, asked, &buffer);
    const std::string copied(buffer.c_str());
    if (CHECK_EQ(copied, product))
        ++tally.n["range: copy is each block's bytes of the range, joined by a blank line (the "
                  "product's rule)"];

    // --- The reference's string, put together from the SAME bytes of each
    // block (tab-expanded for code) by the reference's rule: a `sep` row's
    // text between two pieces of selected text, a marker's glyphs as text.
    std::string reference;
    int64_t written_to = -1;
    bool wrote = false;
    bool rebuilt = true;
    bool markers = false;
    const auto emit = [&](const std::string& piece, int64_t from, int64_t to) {
        if (piece.empty())
            return;
        if (wrote) {
            for (const SepRow& sep : layout.seps) {
                if (sep.index > written_to && sep.index <= from)
                    reference += sep.text;
            }
        }
        reference += piece;
        written_to = to;
        wrote = true;
    };
    for (const BlockPart& part : parts) {
        const DocBlockIn& block = blocks[part.block];
        const BSpan& span = stacking.bspan[part.block];
        if (block.in.has_marker) {
            // `\hss`, then one index per character of the marker.
            std::vector<std::string> glyphs;
            size_t at = 0;
            const std::string& m = block.in.marker_text;
            while (at < m.size()) {
                size_t next = at + 1;
                while (next < m.size() && (static_cast<unsigned char>(m[next]) & 0xC0) == 0x80)
                    ++next;
                glyphs.push_back(m.substr(at, next - at));
                at = next;
            }
            std::string piece;
            int64_t from = -1;
            int64_t to = -1;
            for (size_t g = 0; g < glyphs.size(); ++g) {
                const int64_t index = span.start + 1 + static_cast<int64_t>(g);
                if (index < part.start || index >= part.end)
                    continue;
                if (index >= static_cast<int64_t>(layout.byte.size()) ||
                    layout.byte[static_cast<size_t>(index)] >= 0)
                    rebuilt = false;
                piece += glyphs[g];
                if (from < 0)
                    from = index;
                to = index;
            }
            markers = markers || !piece.empty();
            emit(piece, from, to);
        }
        if (!part.has_text)
            continue;
        // Inside the block the text's own newlines are the separators; a
        // `sep` row there that writes more than the newline (a list's
        // indent) replaces it.
        std::string text =
            block.content.substr(static_cast<size_t>(part.a), static_cast<size_t>(part.b - part.a));
        for (size_t s = layout.seps.size(); s-- > 0;) {
            const SepRow& sep = layout.seps[s];
            if (sep.block != static_cast<int>(part.block) || sep.index <= part.first_index ||
                sep.index > part.last_index || sep.byte <= part.a || sep.text == "\n")
                continue;
            const size_t newline = static_cast<size_t>(sep.byte - 1 - part.a);
            if (newline < text.size() && text[newline] == '\n')
                text.replace(newline, 1, sep.text);
            else
                rebuilt = false;
        }
        emit(text, part.first_index, part.last_index);
    }
    if (rebuilt && reference == range.copy) {
        ++tally.n["range: the reference's string is those same bytes under ITS joining rule "
                  "(separators, markers, tab expansion)"];
    } else {
        ++tally.n["range: UNEXPLAINED, the reference's string is not rebuilt from the product's "
                  "bytes"];
        FAIL("the reference copies \"%s\"; from the product's bytes and the `sep` rows comes "
             "\"%s\"",
             escaped(range.copy).c_str(), escaped(reference).c_str());
    }
    if (copied == range.copy) {
        ++tally.n["range: copy equal to the reference's string, byte for byte"];
    } else {
        bool tabs = false;
        bool widget = false;
        for (size_t b = first->block; b <= last->block; ++b) {
            tabs = tabs ||
                   (blocks[b].is_code() && blocks[b].in.source.find('\t') != std::string::npos);
            widget = widget || blocks[b].hosted;
        }
        ++tally.n[std::string("range: copy differs from the reference's string by the product's "
                              "rule: ") +
                  (markers  ? "a marker is not copied"
                   : widget ? "a widget's text and what joins it"
                   : tabs   ? "a tab is copied as a tab"
                            : "what joins two blocks (a blank line, no list indent)")];
    }

    // --- The rectangles, block by block.
    if (!range.has_rects)
        return;
    ++tally.n["range: asked of GetDocumentRangeRects"];
    ImVector<ImVec4> got;
    ImTypeset::GetDocumentRangeRects(replay.document, asked, &got);
    size_t expected_seen = 0;
    for (const BlockPart& part : parts) {
        const BSpan& span = stacking.bspan[part.block];
        const DocBlockIn& block = blocks[part.block];
        std::vector<ImVec4> expected;
        for (const RectRow& rect : range.rects) {
            if (rect.y < span.y || rect.y >= span.y + span.height)
                continue;
            expected.push_back(ImVec4(px_float(rect.x, replay.ppi), px_float(rect.y, replay.ppi),
                                      px_float(rect.x + rect.width, replay.ppi),
                                      px_float(rect.y + rect.height, replay.ppi)));
        }
        expected_seen += expected.size();
        std::vector<ImVec4> actual;
        const float top = px_float(span.y, replay.ppi);
        const float bottom = px_float(span.y + span.height, replay.ppi);
        for (const ImVec4& rect : got) {
            if (rect.y >= top && rect.y < bottom)
                actual.push_back(rect);
        }
        // The indices the product's bytes name in this block, against the
        // reference's: where they are not the same items the rectangles are
        // not of the same selection.
        int64_t named_start = part.start;
        int64_t named_end = part.start;
        const bool in_asked = part.block >= first->block && part.block <= last->block;
        if (in_asked && !block.hosted && block.in.type != BlockType::RULE) {
            const std::vector<int> bytes = index_bytes(replay, layout, part.block, span);
            const int a = part.block == first->block ? asked.Start.Offset : 0;
            const int e = part.block == last->block ? asked.End.Offset : block.offset(block.size());
            if (a < e) {
                named_start = span.start + index_named(bytes, a);
                named_end = span.start + index_named(bytes, e);
            }
        }
        ++tally.n["rects: blocks of ranges"];
        if (!part.has_text || !in_asked) {
            // Only items the text does not hold: the product selects nothing.
            ++tally.n["rects: REFERENCE-ONLY, the range's part in this block holds no text (the "
                      "product has no rectangle there; asserted)"];
            CHECK(actual.empty());
            continue;
        }
        if (named_start != part.start || named_end != part.end) {
            const bool marker = block.in.has_marker && part.start < named_start;
            ++tally.n[std::string("rects: REFERENCE-ONLY, ") +
                      (marker ? "the reference's range holds the marker, which the product never "
                                "selects"
                       : part.end < named_end
                           ? "the reference's range stops before a line's fill that the "
                             "product's bytes take in"
                       : part.end > named_end
                           ? "the reference's range ends in items the product's bytes "
                             "do not name"
                           : "the reference's range starts in items the product's "
                             "bytes do not name")];
            continue;
        }
        bool equal = expected.size() == actual.size();
        for (size_t k = 0; equal && k < expected.size(); ++k) {
            equal = expected[k].x == actual[k].x && expected[k].y == actual[k].y &&
                    expected[k].z == actual[k].z && expected[k].w == actual[k].w;
        }
        if (equal) {
            ++tally.n["rects: blocks whose rectangles are the reference's, exactly in float "
                      "pixels"];
            tally.n["rects: rectangles compared equal"] += static_cast<int>(expected.size());
        } else {
            std::string text = "block " + std::to_string(part.block) + ": the reference has";
            for (const ImVec4& rect : expected)
                text += check::format(" (%a %a %a %a)", static_cast<double>(rect.x),
                                      static_cast<double>(rect.y), static_cast<double>(rect.z),
                                      static_cast<double>(rect.w));
            text += "; the product";
            for (const ImVec4& rect : actual)
                text += check::format(" (%a %a %a %a)", static_cast<double>(rect.x),
                                      static_cast<double>(rect.y), static_cast<double>(rect.z),
                                      static_cast<double>(rect.w));
            FAIL("%s", text.c_str());
        }
    }
    // Every rectangle of the reference's is some block's.
    CHECK_EQ(expected_seen, range.rects.size());
}

// `blocktext`: the copy of each block's own range.
void ask_blocktexts(Replayed& replay, const LayoutIn& layout, Tally& tally)
{
    const std::vector<DocBlockIn>& blocks = replay.doc.blocks;
    if (!CHECK_EQ(layout.blocktext.size(), blocks.size()))
        return;
    for (size_t b = 0; b < blocks.size(); ++b) {
        CHECK_CONTEXT("blocktext %zu", b);
        const DocBlockIn& block = blocks[b];
        ImTypesetDocumentRange own;
        own.Start = ImTypesetPosition{static_cast<int>(b), 0};
        own.End = ImTypesetPosition{static_cast<int>(b), block.offset(block.size())};
        ImGuiTextBuffer buffer;
        ImTypeset::AppendDocumentRangeText(replay.document, own, &buffer);
        const std::string copied(buffer.c_str());
        ++tally.n["blocktext: asked of AppendDocumentRangeText"];
        const std::string expected = block.in.type == BlockType::RULE && !block.hosted
                                         ? std::string()
                                         : block.product_slice(0, block.size());
        CHECK_EQ(copied, expected);
        if (copied == layout.blocktext[b]) {
            ++tally.n["blocktext: equal to the reference's, byte for byte"];
            continue;
        }
        // What the reference has more: the marker and a space, tabs as
        // spaces; a widget's text less its last newline.
        std::string reference = block.hosted ? block.host_text : block.content;
        if (block.hosted && !reference.empty() && reference.back() == '\n')
            reference.pop_back();
        if (block.in.has_marker)
            reference = block.in.marker_text + " " + reference;
        if (CHECK_EQ(reference, layout.blocktext[b]))
            ++tally.n[std::string("blocktext: differs by the product's rule: ") +
                      (block.in.has_marker ? "a marker is not copied"
                       : block.hosted      ? "a widget's text keeps its last newline"
                                           : "a tab is copied as a tab")];
    }
}

// `bspan` against GetDocumentBlockGeometry.
void ask_geometry(Replayed& replay, const Stacking& stacking, Tally& tally)
{
    for (size_t b = 0; b < stacking.bspan.size(); ++b) {
        CHECK_CONTEXT("bspan %zu", b);
        const BSpan& span = stacking.bspan[b];
        const ImTypesetBlockGeometry geometry =
            ImTypeset::GetDocumentBlockGeometry(replay.document, static_cast<int>(b));
        ++tally.n["bspan: asked of GetDocumentBlockGeometry"];
        CHECK(geometry.LaidOut);
        const bool vertical = geometry.FullMin.y == px_float(span.y, replay.ppi) &&
                              geometry.FullMax.y == px_float(span.y + span.height, replay.ppi);
        if (CHECK(vertical))
            ++tally.n["bspan: top and bottom equal to the reference's, in float pixels"];
        CHECK_EQ(geometry.FullMax.x, px_float(span.x + span.width, replay.ppi));
        // PRODUCT RULE: the full rectangle starts at the block's indent. The
        // reference's starts at the document's left edge.
        if (geometry.FullMin.x == px_float(span.x, replay.ppi))
            ++tally.n["bspan: left edge equal to the reference's too"];
        else
            ++tally.n["bspan: REFERENCE-ONLY left edge (the product's rectangle starts at the "
                      "block's indent)"];
    }
}

// --- selection.golden -------------------------------------------------------------------

} // namespace

TEST(selection_and_copy_across_blocks_through_the_public_calls)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    golden::File file;
    REQUIRE(file.load(golden::path("doc/selection.golden")));
    Tally tally;
    for (const core_golden::Case& c : core_golden::cases(file)) {
        if (c.kind != "doc")
            continue;
        CHECK_CONTEXT("case %s (line %d)", c.name.c_str(), c.line);
        ++tally.n["cases"];
        core_golden::Context context;
        core_golden::Reader reader(file, c);
        Replayed replay;
        if (const char* why = open_case(stage, reader, replay)) {
            ++tally.n[std::string("SKIPPED cases, not expressible: ") + why];
            continue;
        }
        ++tally.n["cases described through the public header"];
        while (!reader.done() && reader.next_is("layout")) {
            const LayoutIn layout = read_layout(reader, context, replay.doc.blocks);
            CHECK_CONTEXT("layout %s (line %d)", layout.name.c_str(), layout.line);
            ++tally.n["layouts"];
            if (!lay_out(replay, layout, context, tally))
                continue;
            ask_geometry(replay, layout.now, tally);
            ask_probes(replay, layout, tally);
            ask_drags(replay, layout, tally);
            for (const RangeRow& range : layout.ranges)
                ask_range(replay, layout, layout.now, range, tally);
            ask_blocktexts(replay, layout, tally);
        }
    }
    tally.print("doc/selection.golden through the public Document calls:");
    tally.print_precision();
    // A replay that quietly shrank must not read as a pass.
    CHECK_EQ(tally.of("cases"), 7);
    CHECK_EQ(tally.of("layouts"), 10);
    CHECK_EQ(tally.of("probe: asked of HitTestDocument"), 478);
    CHECK_EQ(tally.of("drag: asked of SelectDocumentRange"), 1167);
    CHECK_EQ(tally.of("range: read"), 556);
    CHECK_EQ(tally.of("layouts whose commands and height are the reference's, exactly"), 10);
    CHECK(tally.of("probe: caret equal to the reference's `hit`") >= 387);
    CHECK(tally.of("drag: character, range equal to the reference's") >= 161);
    CHECK(tally.of("drag: word, range equal to the reference's") >= 163);
    CHECK(tally.of("drag: line, range equal to the reference's") >= 159);
    CHECK(tally.of("range: asked of AppendDocumentRangeText") >= 428);
    CHECK(tally.of("range: copy equal to the reference's string, byte for byte") >= 250);
    CHECK(tally.of("rects: blocks whose rectangles are the reference's, exactly in float "
                   "pixels") >= 703);
}

// --- hosts.golden ------------------------------------------------------------------------
//
// A code block a widget claims is described as ImTypesetBlockKind_Host with
// the widget's height as its Extent and the widget's text as its text; it
// keeps the code block's indent and spaces, as the reference's does.

// Sets every widget to the heights of `stacking` and settles the document.
void set_heights(Replayed& replay, const LayoutIn& layout, const Stacking& stacking)
{
    // The style first: an edit before any LayoutDocument resolves the blocks
    // against GetStyle().
    const auto width = replay.widths.find(layout.name);
    if (!CHECK(width != replay.widths.end()))
        return;
    CHECK(ImTypeset::LayoutDocument(replay.document, width->second, &replay.style));
    for (const HostedRow& row : stacking.hosted) {
        if (!CHECK(row.block >= 0 && static_cast<size_t>(row.block) < replay.doc.blocks.size()))
            continue;
        DocBlockIn& block = replay.doc.blocks[static_cast<size_t>(row.block)];
        CHECK(block.hosted);
        CHECK_EQ(block.host_text, row.text);
        CHECK_EQ(sp_from_px(static_cast<double>(row.height_px), replay.ppi), row.height);
        block.in.thickness = row.height;
        CHECK(ImTypeset::SetDocumentBlockExtent(replay.document, row.block, row.height_px));
    }
    ImTypeset::LayoutDocumentWindow(replay.document, 0.0f, 1.0e9f);
}

TEST(host_blocks_stack_hit_copy_and_resize_as_the_references_do)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    golden::File file;
    REQUIRE(file.load(golden::path("doc/hosts.golden")));
    Tally tally;
    for (const core_golden::Case& c : core_golden::cases(file)) {
        if (c.kind != "host")
            continue;
        CHECK_CONTEXT("case %s (line %d)", c.name.c_str(), c.line);
        ++tally.n["cases"];
        core_golden::Context context;
        core_golden::Reader reader(file, c);
        Replayed replay;
        if (const char* why = open_case(stage, reader, replay)) {
            ++tally.n[std::string("SKIPPED cases, not expressible: ") + why];
            continue;
        }
        ++tally.n["cases described through the public header"];
        while (!reader.done() && reader.next_is("layout")) {
            const LayoutIn layout = read_layout(reader, context, replay.doc.blocks);
            CHECK_CONTEXT("layout %s (line %d)", layout.name.c_str(), layout.line);
            ++tally.n["layouts"];
            tally.n["widgets"] += static_cast<int>(layout.now.hosted.size());
            set_heights(replay, layout, layout.now);
            if (!lay_out(replay, layout, context, tally))
                continue;
            ask_geometry(replay, layout.now, tally);
            ask_probes(replay, layout, tally);
            for (const RangeRow& range : layout.ranges)
                ask_range(replay, layout, layout.now, range, tally);
            ask_blocktexts(replay, layout, tally);

            // `copyall 0`: every index, which is what a range from the first
            // text to the last names. A widget before the first text or
            // after the last is not in it, in the reference or the product.
            if (CHECK_EQ(layout.copyall.size(), size_t{2})) {
                RangeRow all;
                all.line = layout.line;
                all.start = 0;
                all.end = layout.now.length;
                all.has_span = false;
                all.copy = layout.copyall[0];
                if (all.end > 0)
                    ask_range(replay, layout, layout.now, all, tally);
            }
            // Select-all: the product's range from the first block's start
            // to the last block's end takes a widget at either edge with it,
            // which the reference does only under a flag its view keeps
            // (`copyall 1`, `selectall`).
            if (!replay.doc.blocks.empty()) {
                const std::vector<DocBlockIn>& blocks = replay.doc.blocks;
                ImTypesetDocumentRange everything;
                everything.Start = ImTypesetPosition{0, 0};
                everything.End = ImTypesetPosition{static_cast<int>(blocks.size()) - 1,
                                                   blocks.back().offset(blocks.back().size())};
                std::string expected;
                for (const DocBlockIn& block : blocks) {
                    const std::string slice = block.in.type == BlockType::RULE && !block.hosted
                                                  ? std::string()
                                                  : block.product_slice(0, block.size());
                    if (slice.empty())
                        continue;
                    if (!expected.empty())
                        expected += "\n\n";
                    expected += slice;
                }
                ImGuiTextBuffer buffer;
                ImTypeset::AppendDocumentRangeText(replay.document, everything, &buffer);
                const std::string copied(buffer.c_str());
                ++tally.n["select-all: asked of AppendDocumentRangeText"];
                if (CHECK_EQ(copied, expected))
                    ++tally.n["select-all: every block's text, widgets at the edges included, "
                              "joined by a blank line (the product's rule)"];
                if (layout.copyall.size() == 2 && copied == layout.copyall[1])
                    ++tally.n["select-all: equal to the reference's `copyall 1`, byte for byte"];
                else
                    ++tally.n["select-all: differs from the reference's `copyall 1` by the "
                              "product's rules (markers, what joins blocks, a widget's last "
                              "newline)"];
                if (layout.has_selectall && layout.copyall.size() == 2)
                    CHECK_EQ(layout.selectall, layout.copyall[1]);
            }

            if (!layout.has_resized)
                continue;
            // The widgets change height: nothing else is laid out again.
            const ImTypesetDocumentStats before = ImTypeset::GetDocumentStats(replay.document);
            set_heights(replay, layout, layout.resized);
            const ImTypesetDocumentStats after = ImTypeset::GetDocumentStats(replay.document);
            ++tally.n["resize: replayed through SetDocumentBlockExtent"];
            Document& inner = block_document(*replay.document);
            if (CHECK_EQ(inner.height(), layout.resized.height))
                ++tally.n["resize: height equal to the reference's"];
            ask_geometry(replay, layout.resized, tally);
            // The reference composed the widgets' units alone (`built`).
            CHECK(layout.resized.has_built);
            CHECK_EQ(layout.resized.built.size(), layout.resized.hosted.size());
            CHECK_EQ(after.MaterialBuilds, before.MaterialBuilds);
            if (CHECK(after.BlocksLaidOut - before.BlocksLaidOut <= layout.resized.built.size()))
                ++tally.n["resize: no block but the widgets laid out again, as the reference's "
                          "`built`"];
            tally.n["resize: blocks the product laid out again, in all"] +=
                static_cast<int>(after.BlocksLaidOut - before.BlocksLaidOut);
            tally.n["resize: units the reference built again, in all"] +=
                static_cast<int>(layout.resized.built.size());
        }
    }
    tally.print("doc/hosts.golden through the public Document calls:");
    tally.print_precision();
    CHECK_EQ(tally.of("cases"), 7);
    CHECK_EQ(tally.of("layouts"), 8);
    CHECK_EQ(tally.of("widgets"), 17);
    CHECK_EQ(tally.of("probe: asked of HitTestDocument"), 34);
    // The golden's ranges, and `copyall 0` of each layout that has any index.
    CHECK_EQ(tally.of("range: read"), 217 + 7);
    CHECK_EQ(tally.of("resize: replayed through SetDocumentBlockExtent"), 4);
    CHECK_EQ(tally.of("layouts whose commands and height are the reference's, exactly"), 8);
    CHECK(tally.of("range: asked of AppendDocumentRangeText") >= 180);
}

// --- decorations.golden ------------------------------------------------------------------
//
// The product has no find, no link and no colour span: a host gives marks as
// (block, byte) ranges and they are drawn. What the golden holds that a
// public call answers is WHERE such a range is: the rectangles of each find
// hit (`match`), of each link (`linkarea`), and of each marked colour span
// (`fill`), against GetDocumentRangeRects() of the recorded bytes.
//
// NOT REPLAYED: the search itself (`found`: the product has none); the ink
// ranges (`decoinks`, `roleinks`, `inks`, `runs`), which are index ranges the
// product computes only inside DrawDocument; and what reaches the draw list
// (`rectsfilled`, `glyphinks`).

std::vector<ImVec4> rects_of(Replayed& replay, int block, int a, int b)
{
    const DocBlockIn& in = replay.doc.blocks[static_cast<size_t>(block)];
    ImTypesetDocumentRange range;
    range.Start = ImTypesetPosition{block, in.offset(a)};
    range.End = ImTypesetPosition{block, in.offset(b)};
    ImVector<ImVec4> got;
    ImTypeset::GetDocumentRangeRects(replay.document, range, &got);
    return std::vector<ImVec4>(got.begin(), got.end());
}

bool same_rect(const ImVec4& got, const RectRow& rect, double ppi)
{
    return got.x == px_float(rect.x, ppi) && got.y == px_float(rect.y, ppi) &&
           got.z == px_float(rect.x + rect.width, ppi) &&
           got.w == px_float(rect.y + rect.height, ppi);
}

bool same_rects(const std::vector<ImVec4>& got, const std::vector<RectRow>& rects, double ppi)
{
    if (got.size() != rects.size())
        return false;
    for (size_t k = 0; k < got.size(); ++k) {
        if (!same_rect(got[k], rects[k], ppi))
            return false;
    }
    return true;
}

TEST(find_hits_links_and_marks_are_where_the_reference_has_them)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    golden::File file;
    REQUIRE(file.load(golden::path("doc/decorations.golden")));
    Tally tally;
    for (const core_golden::Case& c : core_golden::cases(file)) {
        if (c.kind != "deco")
            continue;
        CHECK_CONTEXT("case %s (line %d)", c.name.c_str(), c.line);
        ++tally.n["cases"];
        core_golden::Context context;
        core_golden::Reader reader(file, c);
        Replayed replay;
        if (const char* why = open_case(stage, reader, replay)) {
            ++tally.n[std::string("SKIPPED cases, not expressible: ") + why];
            continue;
        }
        while (!reader.done() && reader.next_is("layout")) {
            const LayoutIn layout = read_layout(reader, context, replay.doc.blocks);
            CHECK_CONTEXT("layout %s (line %d)", layout.name.c_str(), layout.line);
            ++tally.n["layouts"];
            if (!lay_out(replay, layout, context, tally))
                continue;
            ask_geometry(replay, layout.now, tally);
            const std::vector<DocBlockIn>& blocks = replay.doc.blocks;

            for (const SpanRow& match : layout.matches) {
                CHECK_CONTEXT("match (line %d)", match.line);
                ++tally.n["match: asked of GetDocumentRangeRects"];
                const DocBlockIn& block = blocks[static_cast<size_t>(match.block)];
                if (!block.expressible(match.a) || !block.expressible(match.b)) {
                    ++tally.n["match: REFERENCE-ONLY, an end inside a tab's spaces"];
                    continue;
                }
                if (CHECK(same_rects(rects_of(replay, match.block, match.a, match.b), match.rects,
                                     replay.ppi))) {
                    ++tally.n["match: rectangles equal to the reference's, exactly in float "
                              "pixels"];
                    tally.n["rectangles compared equal"] += static_cast<int>(match.rects.size());
                }
            }

            // Per block, the product's rectangles of each link.
            std::vector<std::vector<std::vector<ImVec4>>> link_rects(blocks.size());
            for (const SpanRow& area : layout.linkareas) {
                CHECK_CONTEXT("linkarea (line %d)", area.line);
                ++tally.n["linkarea: asked of GetDocumentRangeRects"];
                const DocBlockIn& block = blocks[static_cast<size_t>(area.block)];
                if (!CHECK(area.number >= 0 &&
                           static_cast<size_t>(area.number) < block.links.size()))
                    continue;
                const Link& link = block.links[static_cast<size_t>(area.number)];
                const std::vector<ImVec4> got = rects_of(replay, area.block, link.start, link.end);
                if (CHECK(same_rects(got, area.rects, replay.ppi))) {
                    ++tally.n["linkarea: rectangles equal to the reference's, exactly in float "
                              "pixels"];
                    tally.n["rectangles compared equal"] += static_cast<int>(area.rects.size());
                }
                link_rects[static_cast<size_t>(area.block)].push_back(got);
            }

            // `fills`, in the order the golden has them: per block, each
            // marked span in span order, then each link's underlines (when
            // links are styled). FORMAT-doc.md §4 says the underlines come
            // first; the rows do not.
            size_t fill = 0;
            for (size_t b = 0; b < blocks.size(); ++b) {
                CHECK_CONTEXT("fills of block %zu", b);
                for (const SpanRow& colour : layout.colours) {
                    if (colour.block != static_cast<int>(b) || !colour.has_mark)
                        continue;
                    CHECK_CONTEXT("colour (line %d)", colour.line);
                    ++tally.n["fill: a mark's range asked of GetDocumentRangeRects"];
                    const std::vector<ImVec4> got =
                        rects_of(replay, colour.block, colour.a, colour.b);
                    bool equal = fill + got.size() <= layout.fills.size();
                    for (size_t k = 0; equal && k < got.size(); ++k)
                        equal = same_rect(got[k], layout.fills[fill + k].rect, replay.ppi);
                    fill += got.size();
                    if (CHECK(equal)) {
                        ++tally.n["fill: a mark's rectangles equal to the reference's fills, "
                                  "exactly in float pixels"];
                        tally.n["rectangles compared equal"] += static_cast<int>(got.size());
                    }
                }
                if (replay.links_styled) {
                    for (const std::vector<ImVec4>& rects : link_rects[b]) {
                        for (const ImVec4& rect : rects) {
                            if (!CHECK(fill < layout.fills.size()))
                                break;
                            // An underline: as wide as the link's line. Its
                            // height is the baseline's, which no rectangle
                            // of a range says: left and right are compared.
                            const RectRow& under = layout.fills[fill++].rect;
                            ++tally.n["fill: a link's underline, left and right compared with "
                                      "the link's rectangle (its height is not: REFERENCE-ONLY)"];
                            CHECK_EQ(rect.x, px_float(under.x, replay.ppi));
                            CHECK_EQ(rect.z, px_float(under.x + under.width, replay.ppi));
                        }
                    }
                }
            }
            CHECK_EQ(fill, layout.fills.size());
            tally.n["fill: rows accounted for"] += static_cast<int>(fill);
        }
    }
    tally.print("doc/decorations.golden through the public Document calls:");
    tally.print_precision();
    CHECK_EQ(tally.of("cases"), 3);
    CHECK_EQ(tally.of("layouts"), 4);
    CHECK_EQ(tally.of("match: asked of GetDocumentRangeRects"), 34);
    CHECK_EQ(tally.of("linkarea: asked of GetDocumentRangeRects"), 10);
    CHECK_EQ(tally.of("fill: rows accounted for"), 49);
    CHECK_EQ(tally.of("layouts whose commands and height are the reference's, exactly"), 4);
    CHECK_EQ(tally.of("match: rectangles equal to the reference's, exactly in float pixels"), 34);
}

// --- stream.golden -------------------------------------------------------------------------
//
// The reference's markdown stream is not ported, and a stream case carries no
// `blocks` group: an `sblock` row says a block's sort, alignment and text and
// nothing of its fonts, indents or spaces. So the reference's LAYOUT of a
// step cannot be described through the header, and its `unit` digests, its
// heights and its `settled` rows are NOT compared.
//
// What is replayed is the SEQUENCE OF EDITS: each step's blocks, as text, are
// handed to the product by the smallest edit that makes them (AppendDocumentText
// for a last block that grew, ReplaceDocumentBlocks otherwise), under the
// product's own rules. Two properties of the reference's are then asked of
// the product:
//   - `stable`: no block before the edit moves or changes. A block's layout is
//     held to its own a step earlier by FORMAT-doc.md §6's digest of its
//     commands in its own coordinates, and its top to its top;
//   - `built`: a step lays out no more blocks than the edit names, however
//     many blocks the document has by then.

struct StreamBlock {
    std::string sort;
    std::string align;
    std::string text;

    bool operator==(const StreamBlock& other) const
    {
        return sort == other.sort && align == other.align && text == other.text;
    }
};

uint64_t fnv_value(uint64_t hash, int64_t value)
{
    uint64_t bits = static_cast<uint64_t>(value);
    for (int byte = 0; byte < 8; ++byte) {
        hash ^= bits & 0xFFu;
        hash *= 0x100000001b3ull;
        bits >>= 8;
    }
    return hash;
}

// FORMAT-doc.md §6: FNV-1a over a unit's commands in its own coordinates.
uint64_t digest_of(const core_golden::Context& context, const std::vector<DrawCmd>& cmds,
                   const BlockSpan& span)
{
    uint64_t hash = 0xcbf29ce484222325ull;
    for (size_t k = span.cmd_start; k < span.cmd_end && k < cmds.size(); ++k) {
        const DrawCmd& cmd = cmds[k];
        const int64_t y = cmd.y - span.top;
        const int64_t index = static_cast<int64_t>(cmd.index) - span.index_start;
        switch (cmd.type) {
        case CmdType::GLYPH:
            hash = fnv_value(hash, 1);
            for (const char c : context.faces.text(cmd.font()))
                hash = fnv_value(hash, static_cast<unsigned char>(c));
            for (const int64_t v : {static_cast<int64_t>(cmd.codepoint), cmd.x, y, index})
                hash = fnv_value(hash, v);
            break;
        case CmdType::RULE_RECT:
            for (const int64_t v : {int64_t{2}, cmd.x, y, cmd.width, cmd.height})
                hash = fnv_value(hash, v);
            break;
        case CmdType::SET_GLUE:
        case CmdType::ATOM:
            for (const int64_t v : {int64_t{cmd.type == CmdType::ATOM ? 4 : 3}, cmd.x, y, cmd.width,
                                    cmd.natural(), cmd.height, cmd.depth, index})
                hash = fnv_value(hash, v);
            break;
        case CmdType::LINE:
            for (const int64_t v : {int64_t{5}, cmd.x, y, cmd.width, cmd.height, cmd.depth, index,
                                    static_cast<int64_t>(cmd.end) - span.index_start})
                hash = fnv_value(hash, v);
            break;
        }
    }
    return hash;
}

void add_stream_block(Content& content, const StreamBlock& block)
{
    // The styles the document was given: one per alignment.
    const int style = block.align == "left"     ? 0
                      : block.align == "center" ? 1
                      : block.align == "right"  ? 2
                                                : 3;
    if (block.sort == "code") {
        content.add(ImTypesetBlockKind_Code, block.text, style);
    } else if (block.sort == "rule") {
        content.add(ImTypesetBlockKind_Rule, std::string(), style);
    } else if (block.sort == "table") {
        // Tables are not laid out: a rectangle stands in for one.
        content.add(ImTypesetBlockKind_Host, block.text, style).Extent = 40.0f;
    } else {
        content.add(ImTypesetBlockKind_Paragraph, block.text, style);
    }
}

TEST(a_stream_of_edits_moves_nothing_before_the_edit_and_lays_out_only_what_it_names)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    // The product's own rules: nothing here is the reference's layout.
    stage.ctx->tex_rules = TexRules();
    Frame frame;
    golden::File file;
    REQUIRE(file.load(golden::path("doc/stream.golden")));
    Tally tally;
    const std::vector<golden::Record>& records = file.records();
    for (const core_golden::Case& c : core_golden::cases(file)) {
        if (c.kind != "stream") {
            ++tally.n[std::string("NOT REPLAYED cases of kind `") + c.kind +
                      "` (the line breaker's own property, on a node list)"];
            continue;
        }
        CHECK_CONTEXT("case %s (line %d)", c.name.c_str(), c.line);
        ++tally.n["cases"];
        core_golden::Context context;
        float width_px = 400.0f;
        ImTypesetDocument* document = ImTypeset::CreateDocument();
        REQUIRE(document != nullptr);
        {
            Content styles;
            for (const ImTypesetAlign align : {ImTypesetAlign_Left, ImTypesetAlign_Center,
                                               ImTypesetAlign_Right, ImTypesetAlign_Justify}) {
                ImTypesetBlockStyle style;
                style.Align = align;
                style.SpaceBelow = 8.0f;
                styles.styles.push_back(style);
            }
            const ImTypesetDocumentContent view = styles.view();
            CHECK(ImTypeset::SetDocumentBlocks(document, &view));
        }
        std::vector<StreamBlock> now;
        std::vector<uint64_t> digests;
        std::vector<Scaled> tops;
        int most_laid_out = 0;
        for (size_t at = c.first; at < c.end; ++at) {
            const golden::Record& r = records[at];
            if (r.is("width")) {
                width_px = static_cast<float>(r.f64(1));
                continue;
            }
            if (!r.is("step"))
                continue;
            CHECK_CONTEXT("step %lld (line %d)", static_cast<long long>(r.i64(0)), r.line);
            ++tally.n["steps"];
            const size_t count = static_cast<size_t>(r.i64(3) + r.i64(4));
            std::vector<StreamBlock> next = now;
            next.resize(count);
            int64_t committed_before = 0;
            size_t built = 0;
            for (size_t in = at + 1; in < c.end && !records[in].is("endstep"); ++in) {
                const golden::Record& row = records[in];
                if (row.is("sblock")) {
                    const size_t block = static_cast<size_t>(row.i64(0));
                    if (!CHECK(block < next.size()))
                        continue;
                    next[block] =
                        StreamBlock{std::string(row.raw(2)), std::string(row.raw(4)), row.str(6)};
                } else if (row.is("stable")) {
                    committed_before = row.i64(0);
                    // The reference's property, as the golden records it.
                    CHECK_EQ(row.i64(2), int64_t{0});
                } else if (row.is("built")) {
                    built = static_cast<size_t>(row.i64(0));
                }
            }
            tally.n["units the reference built, in all"] += static_cast<int>(built);

            // The smallest edit from `now` to `next`.
            size_t lo = 0;
            while (lo < now.size() && lo < next.size() && now[lo] == next[lo])
                ++lo;
            size_t old_end = now.size();
            size_t new_end = next.size();
            while (old_end > lo && new_end > lo && now[old_end - 1] == next[new_end - 1]) {
                --old_end;
                --new_end;
            }
            size_t named = new_end - lo;
            const ImTypesetDocumentStats before = ImTypeset::GetDocumentStats(document);
            if (lo == old_end && lo == new_end) {
                ++tally.n["steps that change no block's text (the reference rebuilt an object; "
                          "nothing to hand over)"];
            } else {
                // The reference never re-describes a block that had committed.
                CHECK(static_cast<int64_t>(lo) >= committed_before);
                bool appended = false;
                if (old_end == now.size() && new_end == next.size() && old_end - lo == 1 &&
                    new_end - lo == 1 && now[lo].sort == next[lo].sort &&
                    now[lo].align == next[lo].align &&
                    (next[lo].sort == "text" || next[lo].sort == "code") &&
                    next[lo].text.size() > now[lo].text.size() &&
                    next[lo].text.compare(0, now[lo].text.size(), now[lo].text) == 0) {
                    const std::string& text = next[lo].text;
                    appended = ImTypeset::AppendDocumentText(
                        document, text.data() + now[lo].text.size(), text.data() + text.size());
                    if (appended)
                        ++tally.n["edits: AppendDocumentText"];
                    else
                        ++tally.n["edits: AppendDocumentText refused, replaced instead"];
                }
                if (!appended) {
                    Content content;
                    for (size_t b = lo; b < new_end; ++b)
                        add_stream_block(content, next[b]);
                    const ImTypesetDocumentContent view = content.view();
                    CHECK(ImTypeset::ReplaceDocumentBlocks(document, static_cast<int>(lo),
                                                           static_cast<int>(old_end - lo), &view));
                    ++tally.n["edits: ReplaceDocumentBlocks"];
                }
            }
            CHECK(ImTypeset::LayoutDocument(document, width_px));
            ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e9f);
            const ImTypesetDocumentStats after = ImTypeset::GetDocumentStats(document);
            CHECK_EQ(after.Blocks, static_cast<int>(next.size()));
            const size_t laid_out = static_cast<size_t>(after.BlocksLaidOut - before.BlocksLaidOut);
            tally.n["blocks the product laid out, in all"] += static_cast<int>(laid_out);
            most_laid_out = std::max(most_laid_out, static_cast<int>(laid_out));
            if (!CHECK(laid_out <= named))
                FAIL("the edit names %zu block(s) of %zu and %zu were laid out", named, next.size(),
                     laid_out);
            else
                ++tally.n["steps that laid out no more blocks than the edit names"];

            // Nothing before the edit moved or changed.
            Document& inner = block_document(*document);
            std::vector<DrawCmd> cmds;
            std::vector<BlockSpan> spans;
            if (!CHECK(inner.settled()) || !CHECK(inner.flatten(cmds, &spans)) ||
                !CHECK_EQ(spans.size(), next.size()))
                break;
            std::vector<uint64_t> digests_now(spans.size());
            std::vector<Scaled> tops_now(spans.size());
            bool held = true;
            for (size_t b = 0; b < spans.size(); ++b) {
                digests_now[b] = digest_of(context, cmds, spans[b]);
                tops_now[b] = spans[b].top;
                if (b >= lo || b >= digests.size())
                    continue;
                ++tally.n["stable: blocks before the edit, compared with themselves a step "
                          "earlier"];
                if (digests_now[b] != digests[b] || tops_now[b] != tops[b]) {
                    held = false;
                    FAIL("block %zu, before the edit at %zu, is not what it was a step earlier", b,
                         lo);
                }
            }
            if (held)
                ++tally.n["stable: steps in which no block before the edit moved or changed"];
            digests.swap(digests_now);
            tops.swap(tops_now);
            now.swap(next);
        }
        ImTypeset::DestroyDocument(document);
        tally.n["blocks at the end of the cases, in all"] += static_cast<int>(now.size());
        std::printf("  %s: %zu blocks at the end; the most blocks one step laid out: %d\n",
                    c.name.c_str(), now.size(), most_laid_out);
    }
    tally.print("doc/stream.golden as edits through the public Document calls (the product's "
                "rules):");
    CHECK_EQ(tally.of("cases"), 9);
    CHECK_EQ(tally.of("steps"), 1583);
    CHECK_EQ(tally.of("stable: steps in which no block before the edit moved or changed"), 1583);
    CHECK(tally.of("stable: blocks before the edit, compared with themselves a step earlier") >
          10000);
}

// --- The tab of a code block ------------------------------------------------------------
//
// The goldens' code bytes are tab-expanded and the product's are the source's,
// so no golden range can start AT a tab in the product's terms and say so:
// the four spaces share the tab's offset. This is the product's own rule,
// under its own rules (no hook): the tab's byte is the whole tab.

TEST(a_tab_in_a_code_block_is_selected_whole_by_its_one_byte)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    Content content;
    content.add(ImTypesetBlockKind_Code, "ab\n\tb", -1);
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    REQUIRE(document != nullptr);
    const ImTypesetDocumentContent view = content.view();
    CHECK(ImTypeset::SetDocumentBlocks(document, &view));
    CHECK(ImTypeset::LayoutDocument(document, 400.0f));
    CHECK(ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e9f));
    const auto rects = [&](int from, int to) {
        ImVector<ImVec4> out;
        const ImTypesetDocumentRange range = {{0, from}, {0, to}};
        ImTypeset::GetDocumentRangeRects(document, range, &out);
        return out;
    };
    const ImVector<ImVec4> first = rects(0, 1);  // "a"
    const ImVector<ImVec4> tab = rects(3, 4);    // the tab
    const ImVector<ImVec4> second = rects(3, 5); // the tab and "b"
    if (CHECK_EQ(first.Size, 1) && CHECK_EQ(tab.Size, 1) && CHECK_EQ(second.Size, 1)) {
        const float advance = first[0].z - first[0].x;
        CHECK(advance > 0.0f);
        // Both lines start at the block's left edge, and the tab is four columns.
        CHECK_EQ(tab[0].x, first[0].x);
        CHECK_EQ(second[0].x, first[0].x);
        CHECK_NEAR(tab[0].z - tab[0].x, 4.0f * advance, 0.01f);
        CHECK_NEAR(second[0].z - second[0].x, 5.0f * advance, 0.01f);
    }
    ImGuiTextBuffer buffer;
    const ImTypesetDocumentRange whole_tab = {{0, 3}, {0, 4}};
    ImTypeset::AppendDocumentRangeText(document, whole_tab, &buffer);
    CHECK_EQ(std::string(buffer.c_str()), std::string("\t"));
    ImTypeset::DestroyDocument(document);
}
