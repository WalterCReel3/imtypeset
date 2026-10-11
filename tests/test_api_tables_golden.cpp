// The reference's table goldens through the Document of include/imtypeset.h:
//   - tests/golden/doc-tables/selection.golden (tools/oracle/FORMAT-doc-tables.md):
//     a table among other blocks -- its commands, its geometry, hits, drags,
//     ranges, copies, find hits and links through its cells;
//   - tests/golden/tables/{widths,shapes,demo}.golden (FORMAT-tables.md §3): one
//     table alone, described from its cells' events, at every recorded width;
//   - tests/golden/doc-tables/stream.golden: a table that grows a step at a time.
//
// The blocks are described through ImTypesetBlock, ImTypesetCell and
// ImTypesetTableColumn and laid out under the reference's rules
// (tests/document_reference.h); every length goes in as float pixels and is
// held to the golden's scaled points by ==. A layout is first held to the
// golden's commands, exactly; questions are asked only of a layout that is the
// reference's.
//
// A TABLE'S TEXT is its cells joined by a tab and its rows by a newline: what
// include/imtypeset.h tells a host to write. A cell's text is the golden's
// `content`.
//
// TWO SPACES. The golden speaks reading-order indices; the product speaks
// (block, byte offset into the block's text). The reference's OWN answers are
// indices and are compared as indices where the product's internals have one
// (the selection model the public call asks, the table layout's row and cell
// index ranges). A public position is then held to the index by THE PRODUCT'S
// CONVENTION, derived here from the golden's `tmap` (`derive`):
//   - an index that came from a character of a cell is that character's byte
//     of the block's text;
//   - an index the text does not hold is, when a character of its cell comes
//     after it, that character's byte, and otherwise the END of its cell's
//     text.
// This is not the dump script's convention (FORMAT-doc-tables.md §0: its
// `<pos>` and `<span>` resolve a boundary forward to the item after it, with
// byte -1 where the text does not hold that item). The two are compared and
// the number of places they differ is printed; neither is failed by the other.
// Outside a table a caret is compared as tests/test_api_document_golden.cpp
// compares it: the first byte of text at or after it, counted over the
// document.
//
// PIXELS. As tests/test_api_document_golden.cpp: a point is handed over as the
// float nearest its scaled points; a rectangle is compared with the float its
// scaled points come to, exactly. No tolerance anywhere.

#include "blocks_golden.h"
#include "core_golden.h"
#include "document_reference.h"
#include "golden.h"
#include "tables_golden.h"

#include "imtypeset.h"

#include "context.h"
#include "imtypeset_document.h"

#include <algorithm>
#include <cinttypes>
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
    std::vector<ImTypesetCell> cells;
    std::vector<ImTypesetTableColumn> columns;

    ImTypesetBlock& add(ImTypesetBlockKind kind, const std::string& body, int style)
    {
        ImTypesetBlock block;
        block.Kind = kind;
        block.Style = style;
        block.TextStart = static_cast<int>(text.size());
        text += body;
        block.TextEnd = static_cast<int>(text.size());
        block.RunStart = static_cast<int>(runs.size());
        block.CellStart = static_cast<int>(cells.size());
        block.ColumnStart = static_cast<int>(columns.size());
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
    void cell(int start, int end, bool row_start)
    {
        ImTypesetCell c;
        c.TextStart = start;
        c.TextEnd = end;
        c.Flags = row_start ? ImTypesetCellFlags_RowStart : ImTypesetCellFlags_None;
        cells.push_back(c);
        ++blocks.back().CellCount;
    }
    ImTypesetTableColumn& column(ImTypesetAlign align)
    {
        ImTypesetTableColumn c;
        c.Align = align;
        columns.push_back(c);
        ++blocks.back().ColumnCount;
        return columns.back();
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
        out.Cells = cells.data();
        out.CellCount = static_cast<int>(cells.size());
        out.Columns = columns.data();
        out.ColumnCount = static_cast<int>(columns.size());
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
    void print(const char* title) const
    {
        std::printf("%s\n", title);
        for (const auto& entry : n)
            std::printf("  %6d  %s\n", entry.second, entry.first.c_str());
        std::printf("  %d lengths of the description held to the reference's scaled points by "
                    "==\n",
                    lengths);
        std::printf("  a point's float pixels are at most %lld sp from its scaled points; the "
                    "points moved back across a block's edge are at most %lld sp from the "
                    "golden's\n",
                    static_cast<long long>(furthest), static_cast<long long>(furthest_across));
    }
};

using document_reference::px_float;

// --- The `blocks` group of FORMAT-doc.md §2.1 and FORMAT-doc-tables.md §2.1 ----------------

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

// One cell of a table: its text, and where that is in the block's text.
struct CellIn {
    int line = 0;
    int row = 0;
    int column = 0;
    std::string content;
    std::vector<Piece> pieces;
    std::vector<Link> links;
    bool has_formula = false;
    int start = 0;
    int end = 0;
    // How many links the block's cells before this one hold.
    int links_before = 0;
};

struct DocBlockIn {
    // The fields FORMAT-blocks.md's group shares, where its reader keeps them.
    blocks_golden::BlockIn in;
    int nest = -1;
    bool numbered = false;
    bool has_formula = false;
    // The block's text: FORMAT-doc.md §1's, and for a table its cells joined
    // by a tab and its rows by a newline.
    std::string content;
    std::string language;
    std::vector<Piece> pieces;
    std::vector<Link> links;
    // CODE: content byte -> source byte, with one more entry for the end.
    std::vector<int> raw_of;
    // TABLE
    bool is_table = false;
    uint32_t columns = 0;
    bool has_header = false;
    std::vector<ColumnAlign> aligns;
    // Row after row; `row_first` has one entry more than there are rows.
    std::vector<CellIn> cells;
    std::vector<size_t> row_first;

    bool is_code() const { return in.type == BlockType::CODE; }
    int size() const { return static_cast<int>(content.size()); }
    size_t rows() const { return row_first.empty() ? 0 : row_first.size() - 1; }
    const CellIn* cell(int row, int column) const
    {
        if (row < 0 || column < 0 || static_cast<size_t>(row) >= rows())
            return nullptr;
        const size_t first = row_first[static_cast<size_t>(row)];
        const size_t count = row_first[static_cast<size_t>(row) + 1] - first;
        return static_cast<size_t>(column) < count ? &cells[first + static_cast<size_t>(column)]
                                                   : nullptr;
    }
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
    // What the product copies for content bytes [a, b) of a block that is not
    // a table.
    std::string product_slice(int a, int b) const
    {
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

// `piece` rows, then `link` and `formula` rows: of a text block or of a cell.
// A link's fields are <number> <byte_start> <byte_end> ...
void read_pieces(core_golden::Reader& reader, std::vector<Piece>& pieces, std::vector<Link>& links,
                 bool& has_formula)
{
    while (!reader.done() && reader.next_is("piece")) {
        const golden::Record& record = reader.take();
        golden::Cursor piece(record);
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
            has_formula = true;
            piece.boolean();
            piece.str();
        } else {
            check::fail(record.file, record.line, "a piece that is no run, break or formula");
        }
        piece.expect_done();
        pieces.push_back(one);
    }
    while (!reader.done() && reader.next_is("link")) {
        const golden::Record& link = reader.take();
        if (link.i64(0) != static_cast<int64_t>(links.size()))
            check::fail(link.file, link.line, "links are not numbered in order");
        links.push_back(Link{link.i32(1), link.i32(2)});
    }
    while (!reader.done() && reader.next_is("formula"))
        reader.take();
}

// Joins a table's cells into its block's text and says where each is.
void join_cells(DocBlockIn& block)
{
    block.content.clear();
    int links = 0;
    for (size_t r = 0; r < block.rows(); ++r) {
        if (r != 0)
            block.content += '\n';
        for (size_t k = block.row_first[r]; k < block.row_first[r + 1]; ++k) {
            CellIn& cell = block.cells[k];
            if (k != block.row_first[r])
                block.content += '\t';
            cell.start = static_cast<int>(block.content.size());
            block.content += cell.content;
            cell.end = static_cast<int>(block.content.size());
            cell.links_before = links;
            links += static_cast<int>(cell.links.size());
            block.has_formula = block.has_formula || cell.has_formula;
        }
    }
}

// `cell` ... `endcell` of FORMAT-doc-tables.md §2.1.
CellIn read_cell(core_golden::Reader& reader)
{
    CellIn cell;
    const golden::Record& open = reader.take("cell");
    cell.line = open.line;
    cell.row = open.i32(1);
    cell.column = open.i32(2);
    const golden::Record& content = reader.take("content");
    cell.content = content.str(2);
    if (content.i64(0) != static_cast<int64_t>(cell.content.size()))
        check::fail(content.file, content.line, "`content` does not count its bytes");
    read_pieces(reader, cell.pieces, cell.links, cell.has_formula);
    reader.take("endcell");
    return cell;
}

struct BlocksIn {
    blocks_golden::ThemeIn theme;
    // The rule under a header, and the padding left and right of a cell.
    Scaled table_rule = 0;
    Scaled cell_padding = 0;
    std::vector<DocBlockIn> blocks;
};

BlocksIn read_doc_blocks(core_golden::Reader& reader)
{
    BlocksIn out;
    out.theme = blocks_golden::read_theme(reader);
    out.table_rule = reader.take("decor").i64(0);
    out.cell_padding = reader.take("tabledecor").i64(0);
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
            read_pieces(reader, block.pieces, block.links, block.has_formula);
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
        } else if (open.is("table")) {
            block.in.type = BlockType::TABLE;
            block.is_table = true;
            block.columns = cursor.u32();
            const uint32_t rows = cursor.u32();
            block.has_header = cursor.boolean();
            block.in.font = cursor.str();
            block.in.left_indent = cursor.i64();
            block.in.space_above = cursor.i64();
            block.in.space_below = cursor.i64();
            cursor.expect_done();
            blocks_golden::read_style(reader, block.in);
            block.nest = reader.take("nest").i32(0);
            golden::Cursor aligns(reader.take("aligns"));
            const uint32_t align_count = aligns.u32();
            for (uint32_t i = 0; i < align_count && !aligns.done(); ++i)
                block.aligns.push_back(static_cast<ColumnAlign>(core_golden::token_index(
                    aligns.raw(), tables_golden::COLUMN_ALIGN_TOKENS, "ColumnAlign")));
            aligns.expect_done();
            block.row_first.push_back(0);
            while (!reader.done() && reader.next_is("row")) {
                const golden::Record& row = reader.take();
                const uint32_t cell_count = row.u32(1);
                for (uint32_t c = 0; c < cell_count && !reader.done(); ++c) {
                    CellIn cell = read_cell(reader);
                    if (cell.row != static_cast<int>(block.rows()) ||
                        cell.column != static_cast<int>(c))
                        check::fail(row.file, row.line, "cells are not numbered in order");
                    block.cells.push_back(std::move(cell));
                }
                block.row_first.push_back(block.cells.size());
            }
            reader.take("endtable");
            if (block.rows() != rows)
                check::fail(open.file, open.line, "`table` does not count its rows");
            join_cells(block);
        } else {
            check::fail(open.file, open.line, "expected a `text`, `code`, `rule` or `table` group");
            break;
        }
        out.blocks.push_back(std::move(block));
    }
    reader.take("endblocks");
    if (out.blocks.size() != count)
        check::fail(__FILE__, __LINE__, "`blocks` does not count its groups");
    return out;
}

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

// Why the pieces of a text (a block's, a cell's) cannot be said as runs of
// it, or null. `any_hard` is set when there is a hard break among them.
const char* pieces_expressible(const std::string& content, const std::vector<Piece>& pieces,
                               bool has_formula, bool* any_hard)
{
    *any_hard = false;
    for (const Piece& piece : pieces)
        *any_hard = *any_hard || piece.kind == Piece::HARDBREAK;
    bool nothing_yet = true;
    bool after_space = false;
    bool line_start = false;
    for (const Piece& piece : pieces) {
        const std::string text = content.substr(static_cast<size_t>(piece.start),
                                                static_cast<size_t>(piece.end - piece.start));
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
        if (has_formula && text.find('$') != std::string::npos)
            return "a dollar that is text beside a formula";
        if (*any_hard && text.find_first_of("\r\n") != std::string::npos)
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
    return nullptr;
}

// The runs of one text, whose first byte is `base` of the block's: a run per
// `piece run`, cut where a link starts or ends. A link is a run's Tag:
// `tag_base` + its number + 1.
void add_runs(Content& content, int base, const std::vector<Piece>& pieces,
              const std::vector<Link>& links, int tag_base, const FontSpec& font)
{
    for (const Piece& piece : pieces) {
        if (piece.kind != Piece::RUN || piece.start == piece.end)
            continue;
        std::vector<int> cuts = {piece.start, piece.end};
        for (const Link& link : links) {
            for (const int edge : {link.start, link.end}) {
                if (edge > piece.start && edge < piece.end)
                    cuts.push_back(edge);
            }
        }
        std::sort(cuts.begin(), cuts.end());
        cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
        for (size_t k = 0; k + 1 < cuts.size(); ++k) {
            ImTypesetRun& run = content.run(base + cuts[k], base + cuts[k + 1]);
            run.Face = static_cast<ImTypesetFace>(piece.font.face) + 1;
            if (piece.font.size != font.size)
                run.Scale = static_cast<float>(static_cast<double>(piece.font.size) /
                                               static_cast<double>(font.size));
            if (!piece.breakable)
                run.Flags |= ImTypesetRunFlags_NoBreak;
            for (size_t l = 0; l < links.size(); ++l) {
                if (links[l].start <= cuts[k] && cuts[k + 1] <= links[l].end)
                    run.Tag = tag_base + static_cast<int>(l) + 1;
            }
        }
    }
}

ImTypesetAlign column_align(ColumnAlign align)
{
    return align == ColumnAlign::CENTER  ? ImTypesetAlign_Center
           : align == ColumnAlign::RIGHT ? ImTypesetAlign_Right
                                         : ImTypesetAlign_Left;
}

// A table block of `block`'s cells, runs and columns, added to `content`.
// Returns why it cannot be said, or null.
const char* describe_table(const DocBlockIn& block, Content& content, int style, double ppi,
                           ImTypesetBlockFlags flags)
{
    for (const CellIn& cell : block.cells) {
        bool hard = false;
        if (const char* why =
                pieces_expressible(cell.content, cell.pieces, block.has_formula, &hard))
            return why;
        if (hard)
            return "a hard break in a cell, which the API has none of";
        if (cell.content.find_first_of("\t\n") != std::string::npos)
            return "a tab or a newline in a cell's text";
    }
    const FontSpec font = blocks_golden::spec_of(block.in.font);
    ImTypesetBlock& out = content.add(ImTypesetBlockKind_Table, block.content, style);
    out.LeftIndent = px_float(block.in.left_indent, ppi);
    out.Flags |= flags;
    if (block.has_header)
        out.Flags |= ImTypesetBlockFlags_TableHeader;
    if (block.has_formula)
        out.Flags |= ImTypesetBlockFlags_Math;
    for (size_t r = 0; r < block.rows(); ++r) {
        for (size_t k = block.row_first[r]; k < block.row_first[r + 1]; ++k) {
            const CellIn& cell = block.cells[k];
            content.cell(cell.start, cell.end, k == block.row_first[r]);
            add_runs(content, cell.start, cell.pieces, cell.links, cell.links_before, font);
        }
    }
    for (uint32_t c = 0; c < block.columns; ++c)
        content.column(column_align(c < block.aligns.size() ? block.aligns[c] : ColumnAlign::LEFT));
    return nullptr;
}

// The style of a block that is not a rule, as a multiple of the document's.
ImTypesetBlockStyle style_of(const blocks_golden::BlockIn& in, const blocks_golden::ThemeIn& theme,
                             Scaled base)
{
    const double ppi = theme.px_per_inch;
    ImTypesetBlockStyle own;
    own.SpaceAbove = px_float(in.space_above, ppi);
    own.SpaceBelow = px_float(in.space_below, ppi);
    own.MarkerGap = px_float(theme.marker_gap, ppi);
    if (in.type == BlockType::RULE)
        return own;
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
    return own;
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
        content.styles.push_back(style_of(in, theme, base));
        if (in.type == BlockType::RULE) {
            ImTypesetBlock& out =
                content.add(ImTypesetBlockKind_Rule, std::string(), static_cast<int>(i));
            out.LeftIndent = px_float(in.left_indent, ppi);
            out.Extent = px_float(in.thickness, ppi);
            if (in.thickness <= 0)
                return "a rule of no thickness, which the header reads as the default";
            continue;
        }
        const FontSpec font = blocks_golden::spec_of(in.font);
        if (in.has_marker && in.marker_font.size != font.size)
            return "a marker of another size than its block";
        if (in.type == BlockType::CODE) {
            ImTypesetBlock& out =
                content.add(ImTypesetBlockKind_Code, in.source, static_cast<int>(i));
            out.LeftIndent = px_float(in.left_indent, ppi);
            continue;
        }
        if (block.is_table) {
            if (const char* why = describe_table(block, content, static_cast<int>(i), ppi, 0))
                return why;
            continue;
        }
        bool any_hard = false;
        if (const char* why =
                pieces_expressible(block.content, block.pieces, block.has_formula, &any_hard))
            return why;
        ImTypesetBlock& out =
            content.add(in.has_marker ? ImTypesetBlockKind_ListItem : ImTypesetBlockKind_Paragraph,
                        block.content, static_cast<int>(i));
        out.LeftIndent = px_float(in.left_indent, ppi);
        out.QuoteDepth = static_cast<int>(in.quote_depth);
        if (any_hard)
            out.Flags |= ImTypesetBlockFlags_HardBreaks;
        if (block.has_formula)
            out.Flags |= ImTypesetBlockFlags_Math;
        add_runs(content, 0, block.pieces, block.links, 0, font);
        if (in.has_marker)
            content.marker(in.marker_text);
    }
    return nullptr;
}

// The table options of a golden's theme: a golden zero is said as below zero,
// which the header reads as none.
void table_options(ImTypesetDocumentOptions& options, Scaled cell_padding, Scaled rule, double ppi)
{
    options.TableCellPadding = cell_padding > 0 ? px_float(cell_padding, ppi) : -1.0f;
    options.TableRuleThickness = rule > 0 ? px_float(rule, ppi) : -1.0f;
}

// What a table's description came to, against the golden's: beside
// document_reference::hold_block, which holds its indent, spaces, size,
// leading and strut.
void hold_table(const ImTypesetDocument& document, size_t number, const DocBlockIn& block,
                Scaled cell_padding, Scaled rule, int& lengths)
{
    CHECK_CONTEXT("table %zu (line %d)", number, block.in.line);
    if (!CHECK(number < document.blocks.size()))
        return;
    const TableCells& table = document.doc.block(number).table;
    lengths += 2;
    CHECK_EQ(table.cell_padding, cell_padding);
    CHECK_EQ(table.rule_thickness, rule);
    CHECK(table.has_header == block.has_header);
    CHECK_EQ(table.columns(), static_cast<size_t>(block.columns));
    CHECK_EQ(table.rows(), block.rows());
    CHECK_EQ(table.cells(), block.cells.size());
    for (size_t c = 0; c < table.aligns.size(); ++c)
        CHECK(table.aligns[c] == (c < block.aligns.size() ? block.aligns[c] : ColumnAlign::LEFT));
}

// --- A layout section of FORMAT-doc-tables.md §3 -------------------------------------------

struct BSpan {
    int64_t start = 0;
    int64_t end = 0;
    Scaled x = 0;
    Scaled y = 0;
    Scaled width = 0;
    Scaled height = 0;
};

// A position as the dump script writes it.
struct Pos {
    int block = -1;
    int row = -1;
    int column = -1;
    int byte = -1;

    bool operator==(const Pos& other) const
    {
        return block == other.block && row == other.row && column == other.column &&
               byte == other.byte;
    }
};

Pos read_pos(const golden::Record& r, size_t at)
{
    return Pos{r.i32(at), r.i32(at + 1), r.i32(at + 2), r.i32(at + 3)};
}

struct SepRow {
    int64_t index = 0;
    Pos pos;
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
    Pos first;
    Pos last;
    std::string copy;
    std::vector<RectRow> rects;
};

struct DragRow {
    int line = 0;
    Scaled ax = 0, ay = 0, bx = 0, by = 0;
    std::string granularity;
    int64_t start = 0;
    int64_t end = 0;
    Pos first;
    Pos last;
};

struct ProbeRow {
    int line = 0;
    Scaled x = 0, y = 0;
    int64_t hit = 0;
    int block_at = -1;
    Pos pos;
    int64_t word[2] = {0, 0};
    int64_t line_range[2] = {0, 0};
};

struct MatchRow {
    int line = 0;
    int block = 0;
    int row = -1;
    int column = -1;
    int a = 0;
    int b = 0;
    std::vector<RectRow> rects;
};

struct LinkRow {
    int line = 0;
    int block = 0;
    int number = 0;
    int row = -1;
    int column = -1;
    std::vector<RectRow> rects;
};

struct TGeom {
    int line = 0;
    int block = 0;
    Scaled left = 0;
    uint32_t rows = 0;
    std::vector<Scaled> widths;
};

struct TRow {
    int line = 0;
    int block = 0;
    int row = 0;
    int64_t start = 0;
    int64_t end = 0;
    bool wrapped = false;
    Scaled top = 0;
    Scaled height = 0;
};

struct TCell {
    int line = 0;
    int block = 0;
    int row = 0;
    int column = 0;
    int64_t start = 0;
    int64_t end = 0;
    std::vector<RectRow> rects;
};

struct LayoutIn {
    std::string name;
    int line = 0;
    Scaled width = 0;
    Scaled height = 0;
    int64_t length = 0;
    std::vector<BSpan> bspan;
    std::vector<std::string> cmds;
    // Per index: the block that owns it; in a table its row and column (-1
    // elsewhere); its byte in the block's text or the cell's (-1 for an item
    // the text does not hold); how many bytes it is.
    std::vector<int> block_of;
    std::vector<int> row;
    std::vector<int> column;
    std::vector<int> byte;
    std::vector<int> length_of;
    std::vector<SepRow> seps;
    std::vector<SepRow> atomtexts;
    std::vector<SepRow> originals;
    std::vector<DragRow> drags;
    std::vector<ProbeRow> probes;
    std::vector<RangeRow> ranges;
    std::vector<MatchRow> matches;
    std::vector<LinkRow> linkareas;
    std::vector<TGeom> tgeoms;
    std::vector<TRow> trows;
    std::vector<TCell> tcells;
    std::vector<std::string> blocktext;
    int founds = 0;

    // --- Derived (`derive`)
    // Per index: its byte of the BLOCK's text, for an item the text holds; -1
    // otherwise.
    std::vector<int> sourced;
    // Per index of a table: the offset the product's convention gives it. -1
    // for an index of another block.
    std::vector<int> offset;
    // Per block: where its text starts among the document's bytes.
    std::vector<int64_t> prefix;
    // Per index and one more: the first byte of text at or after it, counted
    // over the document; for a table's index, its offset by the convention.
    std::vector<int64_t> caret;
};

// THE PRODUCT'S CONVENTION for a table's indices, from the golden's `tmap`:
// see the top of the file. A cell a row lacks is where the cell before it
// ended (src/imtypeset_document.cpp `map_table_indices`).
void derive(LayoutIn& out, const std::vector<DocBlockIn>& blocks)
{
    const size_t count = out.byte.size();
    out.sourced.assign(count, -1);
    out.offset.assign(count, -1);
    for (size_t i = 0; i < count; ++i) {
        const DocBlockIn& block = blocks[static_cast<size_t>(out.block_of[i])];
        if (out.byte[i] < 0)
            continue;
        if (!block.is_table) {
            out.sourced[i] = out.byte[i];
        } else if (const CellIn* cell = block.cell(out.row[i], out.column[i])) {
            out.sourced[i] = cell->start + out.byte[i];
        }
    }
    for (size_t i = count; i-- > 0;) {
        const DocBlockIn& block = blocks[static_cast<size_t>(out.block_of[i])];
        if (!block.is_table)
            continue;
        if (out.sourced[i] >= 0) {
            out.offset[i] = out.sourced[i];
            continue;
        }
        const bool same_cell = i + 1 < count && out.block_of[i + 1] == out.block_of[i] &&
                               out.row[i + 1] == out.row[i] && out.column[i + 1] == out.column[i];
        if (same_cell) {
            // The character after it in its cell, or what that one came to.
            out.offset[i] = out.offset[i + 1];
            continue;
        }
        // The last item of its cell: the end of the cell's text.
        int end = 0;
        for (int c = out.column[i]; c >= 0; --c) {
            if (const CellIn* cell = block.cell(out.row[i], c)) {
                end = cell->end;
                break;
            }
        }
        out.offset[i] = end;
    }
    out.prefix.assign(blocks.size() + 1, 0);
    for (size_t b = 0; b < blocks.size(); ++b)
        out.prefix[b + 1] = out.prefix[b] + static_cast<int64_t>(blocks[b].content.size());
    out.caret.assign(count + 1, out.prefix.back());
    for (size_t i = count; i-- > 0;) {
        const int64_t before = out.prefix[static_cast<size_t>(out.block_of[i])];
        if (out.offset[i] >= 0)
            out.caret[i] = before + out.offset[i];
        else
            out.caret[i] = out.sourced[i] >= 0 ? before + out.sourced[i] : out.caret[i + 1];
    }
}

// Reads from `layout` to `endlayout`.
LayoutIn read_layout(core_golden::Reader& reader, core_golden::Context& context,
                     const std::vector<DocBlockIn>& blocks)
{
    LayoutIn out;
    const golden::Record& open = reader.take("layout");
    out.name = open.str(0);
    out.width = open.i64(1);
    out.line = open.line;
    // Where the `rect` rows that follow go.
    std::vector<RectRow>* rects_to = nullptr;
    while (!reader.done() && !reader.next_is("endlayout")) {
        if (reader.next_is("cmds")) {
            const size_t first = reader.position();
            core_golden::read_cmds(reader, context);
            out.cmds = reader.lines(first, reader.position());
            continue;
        }
        const golden::Record& r = reader.take();
        if (r.is("size")) {
            out.height = r.i64(0);
            out.length = r.i64(1);
        } else if (r.is("bspan")) {
            out.bspan.push_back(BSpan{r.i64(1), r.i64(2), r.i64(3), r.i64(4), r.i64(5), r.i64(6)});
        } else if (r.is("imap") || r.is("tmap")) {
            const bool table = r.is("tmap");
            const int block = r.i32(0);
            const int64_t start = r.i64(1);
            const int64_t count = r.i64(2);
            if (static_cast<int64_t>(out.byte.size()) != start)
                check::fail(r.file, r.line, "the map rows do not tile the indices");
            const size_t step = table ? 4 : 2;
            if (r.size() != 3 + step * static_cast<size_t>(count))
                check::fail(r.file, r.line, "a map row does not count its entries");
            for (size_t k = 0; k < static_cast<size_t>(count); ++k) {
                const size_t at = 3 + step * k;
                out.block_of.push_back(block);
                out.row.push_back(table ? r.i32(at) : -1);
                out.column.push_back(table ? r.i32(at + 1) : -1);
                out.byte.push_back(r.i32(at + step - 2));
                out.length_of.push_back(r.i32(at + step - 1));
            }
        } else if (r.is("unmapped")) {
            if (r.i64(0) != 0)
                check::fail(r.file, r.line, "a block the reference could not map");
        } else if (r.is("sep")) {
            out.seps.push_back(SepRow{r.i64(0), read_pos(r, 1), r.str(5)});
        } else if (r.is("atomtext")) {
            out.atomtexts.push_back(SepRow{r.i64(0), read_pos(r, 1), r.str(5)});
        } else if (r.is("original")) {
            out.originals.push_back(SepRow{r.i64(0), read_pos(r, 1), r.str(5)});
        } else if (r.is("tgeom")) {
            TGeom row;
            row.line = r.line;
            row.block = r.i32(0);
            row.left = r.i64(1);
            row.rows = r.u32(2);
            const uint32_t columns = r.u32(3);
            for (uint32_t c = 0; c < columns; ++c)
                row.widths.push_back(r.i64(4 + c));
            out.tgeoms.push_back(row);
        } else if (r.is("trow")) {
            TRow row;
            row.line = r.line;
            row.block = r.i32(0);
            row.row = r.i32(1);
            row.start = r.i64(2);
            row.end = r.i64(3);
            row.wrapped = r.boolean(4);
            row.top = r.i64(5);
            row.height = r.i64(6);
            out.trows.push_back(row);
        } else if (r.is("tcell")) {
            TCell row;
            row.line = r.line;
            row.block = r.i32(0);
            row.row = r.i32(1);
            row.column = r.i32(2);
            row.start = r.i64(3);
            row.end = r.i64(4);
            out.tcells.push_back(row);
            rects_to = &out.tcells.back().rects;
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
            row.first = read_pos(r, 7);
            row.last = read_pos(r, 11);
            out.drags.push_back(row);
        } else if (r.is("probe")) {
            ProbeRow row;
            row.line = r.line;
            row.x = r.i64(0);
            row.y = r.i64(1);
            row.hit = r.i64(2);
            row.block_at = r.i32(3);
            row.pos = read_pos(r, 4);
            row.word[0] = r.i64(8);
            row.word[1] = r.i64(9);
            row.line_range[0] = r.i64(10);
            row.line_range[1] = r.i64(11);
            out.probes.push_back(row);
        } else if (r.is("range")) {
            RangeRow row;
            row.line = r.line;
            row.start = r.i64(0);
            row.end = r.i64(1);
            row.first = read_pos(r, 2);
            row.last = read_pos(r, 6);
            out.ranges.push_back(row);
            rects_to = &out.ranges.back().rects;
        } else if (r.is("match")) {
            MatchRow row;
            row.line = r.line;
            row.block = r.i32(0);
            row.row = r.i32(1);
            row.column = r.i32(2);
            row.a = r.i32(5);
            row.b = r.i32(6);
            out.matches.push_back(row);
            rects_to = &out.matches.back().rects;
        } else if (r.is("found")) {
            ++out.founds;
        } else if (r.is("linkarea")) {
            LinkRow row;
            row.line = r.line;
            row.block = r.i32(0);
            row.number = r.i32(1);
            row.row = r.i32(2);
            row.column = r.i32(3);
            out.linkareas.push_back(row);
            rects_to = &out.linkareas.back().rects;
        } else if (r.is("fill") || r.is("ink") || r.is("lrun")) {
            // The reference's decoration of its own view: not a public call's.
        } else if (r.is("copy")) {
            if (!out.ranges.empty())
                out.ranges.back().copy = r.str(0);
        } else if (r.is("rect")) {
            if (rects_to != nullptr)
                rects_to->push_back(RectRow{r.i64(0), r.i64(1), r.i64(2), r.i64(3)});
        } else if (r.is("blocktext")) {
            out.blocktext.push_back(r.str(1));
        }
        // unit, lead and the counting rows: not needed to ask the product anything.
    }
    reader.take("endlayout");
    if (static_cast<int64_t>(out.byte.size()) != out.length)
        check::fail(open.file, open.line, "the map rows do not cover the layout's indices");
    derive(out, blocks);
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
    std::vector<BlockSpan> spans;

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
    // A product position as a byte of the document's text.
    int64_t caret(const LayoutIn& layout, const ImTypesetPosition& position) const
    {
        if (position.Block < 0)
            return 0;
        if (static_cast<size_t>(position.Block) >= doc.blocks.size())
            return layout.prefix.back();
        const size_t b = static_cast<size_t>(position.Block);
        return layout.prefix[b] + doc.blocks[b].byte_of(position.Offset);
    }
    bool in_table(const LayoutIn& layout, int64_t index) const
    {
        return index >= 0 && index < layout.length &&
               doc.blocks[static_cast<size_t>(layout.block_of[static_cast<size_t>(index)])]
                   .is_table;
    }
    // Whether a product position is the reference's index. In a table, when
    // the position is in that table, the offset the convention gives the
    // index, exactly. Otherwise the same caret: nothing but items no text
    // holds lies between the two (the end of one block and the start of the
    // next are one caret).
    bool same_place(const LayoutIn& layout, int64_t index, const ImTypesetPosition& position) const
    {
        if (in_table(layout, index) &&
            position.Block == layout.block_of[static_cast<size_t>(index)])
            return position.Offset == layout.offset[static_cast<size_t>(index)];
        return caret(layout, position) == layout.caret[static_cast<size_t>(index)];
    }
};

// Reads a case's INPUT rows and its `blocks`, and hands them to a document.
// Returns why it could not, or null; the reader is left at the first layout.
const char* open_case(Stage& stage, core_golden::Reader& reader, Replayed& out)
{
    while (!reader.done() && !reader.next_is("expect")) {
        const golden::Record& r = reader.take();
        if (r.is("width"))
            out.widths[r.str(0)] = static_cast<float>(r.f64(1));
    }
    reader.take("expect");
    out.doc = read_doc_blocks(reader);
    out.ppi = out.doc.theme.px_per_inch;
    if (const char* why = describe(out.doc, out.content, out.style))
        return why;
    out.document = ImTypeset::CreateDocument();
    if (out.document == nullptr)
        return "no document";
    if (const char* why = document_reference::use_reference(out.document, out.doc.theme))
        return why;
    ImTypesetDocumentOptions options;
    options.QuoteIndent = px_float(out.doc.theme.theme.quote_indent, out.ppi);
    options.QuoteBarThickness = px_float(out.doc.theme.theme.quote_rule_thickness, out.ppi);
    table_options(options, out.doc.cell_padding, out.doc.table_rule, out.ppi);
    CHECK(ImTypeset::SetDocumentOptions(out.document, &options));
    const ImTypesetDocumentContent view = out.content.view();
    CHECK(ImTypeset::SetDocumentBlocks(out.document, &view));
    CHECK_EQ(ImTypeset::GetDocumentBlockCount(out.document),
             static_cast<int>(out.doc.blocks.size()));
    for (size_t b = 0; b < out.doc.blocks.size(); ++b) {
        const ImTypesetErrorInfo* error =
            ImTypeset::GetDocumentBlockError(out.document, static_cast<int>(b));
        if (error != nullptr && error->Code != ImTypesetError_None)
            FAIL("block %zu is described with an error: %s", b, error->Message);
    }
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
    for (size_t b = 0; b < replay.doc.blocks.size(); ++b) {
        document_reference::hold_block(*replay.document, b, replay.doc.blocks[b].in, false,
                                       replay.doc.theme, tally.lengths);
        if (replay.doc.blocks[b].is_table)
            hold_table(*replay.document, b, replay.doc.blocks[b], replay.doc.cell_padding,
                       replay.doc.table_rule, tally.lengths);
    }
    std::vector<DrawCmd> flat;
    replay.spans.clear();
    if (!CHECK(inner.flatten(flat, &replay.spans)) ||
        !CHECK_EQ(replay.spans.size(), replay.doc.blocks.size()))
        return false;
    bool same = inner.height() == layout.height;
    for (size_t b = 0; b < replay.spans.size() && b < layout.bspan.size(); ++b) {
        same = CHECK_EQ(static_cast<int64_t>(replay.spans[b].index_start), layout.bspan[b].start) &&
               same;
        same =
            CHECK_EQ(static_cast<int64_t>(replay.spans[b].index_end), layout.bspan[b].end) && same;
    }
    CHECK_EQ(inner.height(), layout.height);
    same = CHECK(!layout.cmds.empty()) && same;
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
int block_by_height(const LayoutIn& layout, Scaled y, int* lower)
{
    *lower = -1;
    if (y < 0 || y >= layout.height)
        return -2;
    for (size_t b = 0; b < layout.bspan.size(); ++b) {
        const BSpan& span = layout.bspan[b];
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

// The selection model the public calls ask of a block: the one in the view
// the document keeps of it, made by the public call before this is asked.
const SelectionModel* model_of(Replayed& replay, size_t block)
{
    const BlockLayout& layout = block_document(*replay.document).layout(block);
    for (const std::unique_ptr<DocView>& held : replay.document->views) {
        if (held->block == static_cast<int>(block) && held->serial == layout.serial &&
            held->p.has_model)
            return &held->p.model;
    }
    return nullptr;
}

// An index of a table as (block, row, column, byte of the cell's text) by the
// product's convention. False for an index that is no table's.
bool convention_pos(const Replayed& replay, const LayoutIn& layout, int64_t index, Pos* out)
{
    if (!replay.in_table(layout, index))
        return false;
    const size_t i = static_cast<size_t>(index);
    const DocBlockIn& block = replay.doc.blocks[static_cast<size_t>(layout.block_of[i])];
    const CellIn* cell = block.cell(layout.row[i], layout.column[i]);
    out->block = layout.block_of[i];
    out->row = layout.row[i];
    out->column = layout.column[i];
    out->byte = cell != nullptr ? layout.offset[i] - cell->start : -1;
    return true;
}

// Counts whether the script's position for an index is the convention's.
void count_conventions(const Replayed& replay, const LayoutIn& layout, int64_t index,
                       const Pos& script, const char* what, Tally& tally)
{
    Pos mine;
    if (!convention_pos(replay, layout, index, &mine))
        return;
    ++tally.n[std::string("conventions: ") + what + ", positions in a table compared"];
    if (!(mine == script))
        ++tally.n[std::string("conventions: ") + what +
                  ", the script's forward position is NOT the product's (counted, not failed)"];
}

void ask_probes(Replayed& replay, const LayoutIn& layout, Tally& tally)
{
    const std::vector<DocBlockIn>& blocks = replay.doc.blocks;
    for (const ProbeRow& probe : layout.probes) {
        CHECK_CONTEXT("probe (line %d)", probe.line);
        ++tally.n["probe: asked of HitTestDocument"];
        Scaled seen_y = 0;
        ImVec2 at = replay.point(probe.x, probe.y, tally, &seen_y);
        int lower = -1;
        int lower_exact = -1;
        // Where the point is as the product reads its float pixels back.
        int where = block_by_height(layout, seen_y, &lower);
        const int where_exact = block_by_height(layout, probe.y, &lower_exact);
        if (where != where_exact) {
            // FLOAT PIXELS AT A BLOCK'S EDGE: the golden's point is on a
            // block's first or last scaled point and the float nearest it
            // reads back on the other side. The product is held to its own
            // rule for the point it gets, then asked the nearest float that
            // reads back on the golden's side; how far that is from the
            // golden's point is counted and printed.
            const ImTypesetDocumentHit across = ImTypeset::HitTestDocument(replay.document, at);
            CHECK_EQ(across.Position.Block, where >= 0 ? where : lower);
            CHECK(across.InBlock == (where >= 0));
            const float toward = seen_y > probe.y ? -1.0e9f : 1.0e9f;
            for (int step = 0; step < 4 && where != where_exact; ++step) {
                at.y = std::nextafter(at.y, toward);
                seen_y = sp_from_px(static_cast<double>(at.y), replay.ppi);
                where = block_by_height(layout, seen_y, &lower);
            }
            if (!CHECK_EQ(where, where_exact))
                continue;
            ++tally.n["probe: asked one float away, the nearest that is on the golden's side of a "
                      "block's edge"];
            tally.furthest_across = std::max(
                tally.furthest_across, seen_y > probe.y ? seen_y - probe.y : probe.y - seen_y);
        }
        const ImTypesetDocumentHit hit = ImTypeset::HitTestDocument(replay.document, at);
        if (where == -1) {
            // PRODUCT RULE: between two blocks a point is the lower block's.
            // The reference gives it to the nearest text, above or below.
            ++tally.n["probe: REFERENCE-ONLY, a point between two blocks (the product gives it to "
                      "the lower block; asserted)"];
            CHECK_EQ(hit.Position.Block, lower);
            CHECK(!hit.InBlock);
            if (replay.same_place(layout, probe.hit, hit.Position))
                ++tally.n["probe:   ...of which the position is the reference's all the same"];
            continue;
        }
        if (where >= 0) {
            CHECK_EQ(probe.block_at, where);
            CHECK_EQ(hit.Position.Block, where);
            CHECK(hit.InBlock);
        } else {
            CHECK(!hit.InBlock);
            CHECK_EQ(hit.Position.Block, probe.y < 0 ? 0 : static_cast<int>(blocks.size()) - 1);
        }
        if (hit.Position.Block < 0 || static_cast<size_t>(hit.Position.Block) >= blocks.size())
            continue;
        const size_t b = static_cast<size_t>(hit.Position.Block);
        const DocBlockIn& block = blocks[b];
        if (block.in.type == BlockType::RULE) {
            // PRODUCT RULE: a block without text answers its own top or
            // bottom. The reference answers the nearest text's boundary.
            ++tally.n["probe: REFERENCE-ONLY, a point in a block without text (the product "
                      "answers that block; asserted)"];
            CHECK(hit.Position.Offset == 0 || hit.Position.Offset == block.size());
            continue;
        }
        const std::string sort = block.is_table ? "in a table" : "in another block";
        const BSpan& span = layout.bspan[b];
        const ImTypesetDocumentRange word =
            ImTypeset::SelectDocumentRange(replay.document, at, at, ImTypesetGranularity_Word);
        const ImTypesetDocumentRange line =
            ImTypeset::SelectDocumentRange(replay.document, at, at, ImTypesetGranularity_Line);

        // --- The reference's own answers, as indices: asked of the model the
        // public calls just asked, AT THE GOLDEN'S SCALED POINT, which a float
        // of pixels does not always carry (a point at the middle of a glyph
        // is a tie that one scaled point decides).
        const SelectionModel* model = model_of(replay, b);
        if (!CHECK(model != nullptr))
            continue;
        const Scaled top = replay.spans[b].top;
        const Point exact{probe.x, probe.y - top};
        const Point seen{sp_from_px(static_cast<double>(at.x), replay.ppi), seen_y - top};
        ++tally.n["probe: " + sort + ", the product's model asked at the golden's scaled point"];
        if (CHECK_EQ(span.start + model->hit(exact), probe.hit))
            ++tally.n["probe: " + sort + ", the model's index equal to the reference's `hit`"];
        const Selection w = model->select_at(exact, exact, Granularity::WORD);
        const Selection l = model->select_at(exact, exact, Granularity::LINE);
        const bool words = CHECK_EQ(span.start + w.start, probe.word[0]) &&
                           CHECK_EQ(span.start + w.end, probe.word[1]);
        const bool lines = CHECK_EQ(span.start + l.start, probe.line_range[0]) &&
                           CHECK_EQ(span.start + l.end, probe.line_range[1]);
        if (words && lines)
            ++tally.n["probe: " + sort +
                      ", the model's word and line equal to the reference's `word_at`, "
                      "`line_at`, as indices"];

        // --- The public answers, by the product's convention: of the index
        // the model gives AT THE POINT THE FLOAT PIXELS READ BACK AS. Where
        // that is the golden's point, or the answer is the same, this holds
        // the public call to the reference; elsewhere to the model, which the
        // lines above hold to the reference.
        const int64_t seen_hit = span.start + model->hit(seen);
        const Selection seen_w = model->select_at(seen, seen, Granularity::WORD);
        const Selection seen_l = model->select_at(seen, seen, Granularity::LINE);
        const bool moved = seen_hit != probe.hit || span.start + seen_w.start != probe.word[0] ||
                           span.start + seen_w.end != probe.word[1] ||
                           span.start + seen_l.start != probe.line_range[0] ||
                           span.start + seen_l.end != probe.line_range[1];
        if (moved)
            ++tally.n["probe: " + sort +
                      ", float pixels read back as a point with another answer (the public call "
                      "is held to the model there)"];
        if (replay.same_place(layout, seen_hit, hit.Position)) {
            ++tally.n["probe: " + sort + ", public position is the index's by the convention"];
            if (!moved)
                ++tally.n["probe: " + sort + ", public position is the reference's `hit`"];
        } else {
            FAIL("HitTestDocument answers (%d, %d); the index %lld is offset %d of block %d by "
                 "the product's convention",
                 hit.Position.Block, hit.Position.Offset, static_cast<long long>(seen_hit),
                 seen_hit < layout.length ? layout.offset[static_cast<size_t>(seen_hit)] : -1,
                 seen_hit < layout.length ? layout.block_of[static_cast<size_t>(seen_hit)] : -1);
        }
        if (replay.same_place(layout, span.start + seen_w.start, word.Start) &&
            replay.same_place(layout, span.start + seen_w.end, word.End) &&
            replay.same_place(layout, span.start + seen_l.start, line.Start) &&
            replay.same_place(layout, span.start + seen_l.end, line.End)) {
            ++tally.n["probe: " + sort +
                      ", public word and line (SelectDocumentRange) are the indices' by the "
                      "convention"];
            if (!moved)
                ++tally.n["probe: " + sort + ", public word and line are the reference's"];
        } else {
            FAIL("SelectDocumentRange: word (%d, %d)-(%d, %d), line (%d, %d)-(%d, %d); the "
                 "model's word [%lld, %lld), line [%lld, %lld)",
                 word.Start.Block, word.Start.Offset, word.End.Block, word.End.Offset,
                 line.Start.Block, line.Start.Offset, line.End.Block, line.End.Offset,
                 static_cast<long long>(span.start + seen_w.start),
                 static_cast<long long>(span.start + seen_w.end),
                 static_cast<long long>(span.start + seen_l.start),
                 static_cast<long long>(span.start + seen_l.end));
        }
        if (!block.is_table) {
            CHECK_EQ(hit.Row, -1);
            CHECK_EQ(hit.Column, -1);
            continue;
        }
        // --- The cell (docs/decisions.md D60). The reference has no answer
        // for which cell a point is in: its `hit` is an index, and the cell
        // of that index is the NEXT cell for a point in the far half of a
        // cell's blank. The product's Row and Column are the cell whose
        // rectangle holds the point, and that is what is asserted, for every
        // probe, against rectangles built from the golden alone: the column
        // edges are `tgeom`'s left plus the reference's widths, the rows are
        // `trow`'s top and height. A point between two rows is the nearer
        // row's (the upper when as near to both), one beside the table the
        // first or last column's, and one in a column its row has no cell
        // for the row's last cell's: the product's rule, written out here.
        const TGeom* geom = nullptr;
        for (const TGeom& candidate : layout.tgeoms) {
            if (candidate.block == static_cast<int>(b))
                geom = &candidate;
        }
        if (!CHECK(geom != nullptr) || !CHECK(!geom->widths.empty()))
            continue;
        const Scaled seen_x = sp_from_px(static_cast<double>(at.x), replay.ppi);
        const TRow* in_row = nullptr;
        for (const TRow& row : layout.trows) {
            if (row.block != static_cast<int>(b))
                continue;
            if (in_row == nullptr) {
                in_row = &row;
                continue;
            }
            // Rows come top to bottom. The later row is taken once the point
            // is at or below its top, or nearer its top than the last scaled
            // point of the row above.
            const Scaled above_last = in_row->top + in_row->height - 1;
            if (seen_y >= row.top ||
                (seen_y > above_last && row.top - seen_y < seen_y - above_last))
                in_row = &row;
        }
        if (!CHECK(in_row != nullptr))
            continue;
        int in_column = 0;
        Scaled edge = geom->left + geom->widths[0];
        while (static_cast<size_t>(in_column) + 1 < geom->widths.size() && seen_x >= edge)
            edge += geom->widths[static_cast<size_t>(++in_column)];
        const int have = static_cast<int>(block.row_first[static_cast<size_t>(in_row->row) + 1] -
                                          block.row_first[static_cast<size_t>(in_row->row)]);
        if (!CHECK(have > 0))
            continue;
        const int cell_column = std::min(in_column, have - 1);
        const CellIn* under = block.cell(in_row->row, cell_column);
        if (!CHECK(under != nullptr))
            continue;
        const bool in_rect = seen_y >= in_row->top && seen_y < in_row->top + in_row->height &&
                             seen_x >= geom->left && seen_x < edge && in_column == cell_column;
        ++tally.n["probe: in a table, Row and Column held to the cell under the point"];
        if (in_rect)
            ++tally.n["probe: in a table, the point is inside a cell's rectangle"];
        // Row and Column are the same under either set of rules.
        CHECK_EQ(hit.Row, in_row->row);
        CHECK_EQ(hit.Column, cell_column);

        // --- The public position under the product's rule: a byte of that
        // cell's own text, whose cell (GetDocumentTableCell) is that cell,
        // and which names itself back through GetDocumentTablePosition.
        replay.document->product_table_hits = true;
        const ImTypesetDocumentHit product = ImTypeset::HitTestDocument(replay.document, at);
        replay.document->product_table_hits = false;
        CHECK_EQ(product.Position.Block, hit.Position.Block);
        const ImTypesetCellPosition cell =
            ImTypeset::GetDocumentTableCell(replay.document, product.Position);
        const bool rows =
            CHECK_EQ(product.Row, in_row->row) && CHECK_EQ(product.Column, cell_column);
        const bool cells = CHECK_EQ(cell.Row, in_row->row) && CHECK_EQ(cell.Column, cell_column) &&
                           CHECK(cell.Offset >= 0) &&
                           CHECK(cell.Offset <= under->end - under->start) &&
                           CHECK_EQ(product.Position.Offset, under->start + cell.Offset);
        if (rows && cells)
            ++tally.n["probe: in a table, the product's position is a byte of the cell under "
                      "the point"];
        const ImTypesetPosition back = ImTypeset::GetDocumentTablePosition(
            replay.document, product.Position.Block, cell.Row, cell.Column, cell.Offset);
        CHECK(same_position(back, product.Position));
        // How often the two rules give one position, and how often the
        // reference's index is in another cell than the point: counted, and
        // the counts are pinned by the test.
        if (same_position(product.Position, hit.Position))
            ++tally.n["probe: in a table, the product's position is the reference's index's"];
        const int64_t in_cell = std::min(std::max(seen_hit, span.start), span.end - 1);
        Pos reference_cell;
        if (convention_pos(replay, layout, in_cell, &reference_cell) &&
            (reference_cell.row != in_row->row || reference_cell.column != cell_column)) {
            ++tally.n["probe: in a table, the reference's index is in ANOTHER cell than the "
                      "point (the product answers the point's)"];
            if (in_rect)
                ++tally.n["probe:   ...of which the point is inside a cell's rectangle"];
        }
        if (!moved)
            count_conventions(replay, layout, probe.hit, probe.pos, "a probe's `<pos>`", tally);
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
        const bool equal = replay.same_place(layout, drag.start, got.Start) &&
                           replay.same_place(layout, drag.end, got.End);
        int lower_a = -1;
        int lower_b = -1;
        int unused = -1;
        const int where_a = block_by_height(layout, seen_ay, &lower_a);
        const int where_b = block_by_height(layout, seen_by, &lower_b);
        if (where_a != block_by_height(layout, drag.ay, &unused) ||
            where_b != block_by_height(layout, drag.by, &unused)) {
            ++tally.n["drag: NOT COMPARED, float pixels put an end across a block's edge"];
            continue;
        }
        if (where_a == -1 || where_b == -1) {
            ++tally.n["drag: REFERENCE-ONLY, an end between two blocks (the product gives it to "
                      "the lower block)"];
            if (equal)
                ++tally.n["drag:   ...of which the range is the reference's all the same"];
            continue;
        }
        const int block_a = where_a >= 0 ? where_a : (drag.ay < 0 ? 0 : lower_a);
        const int block_b = where_b >= 0 ? where_b : (drag.by < 0 ? 0 : lower_b);
        bool textless = false;
        bool table = false;
        for (const int which : {block_a, block_b}) {
            if (which < 0)
                continue;
            const DocBlockIn& block = replay.doc.blocks[static_cast<size_t>(which)];
            textless = textless || block.in.type == BlockType::RULE;
            table = table || block.is_table;
        }
        if (textless) {
            ++tally.n["drag: REFERENCE-ONLY, an end in a block without text"];
            continue;
        }
        const std::string key =
            "drag: " + drag.granularity + (table ? ", an end in a table" : ", no end in a table");
        ++tally.n[key + ", compared"];
        const bool exact = sp_from_px(static_cast<double>(a.x), replay.ppi) == drag.ax &&
                           sp_from_px(static_cast<double>(b.x), replay.ppi) == drag.bx &&
                           seen_ay == drag.ay && seen_by == drag.by;
        if (equal) {
            ++tally.n[key + ", range equal to the reference's"];
        } else if (!exact) {
            // The float pixels of an end read back as another scaled point,
            // and the answer there is another (a tie at the middle of a
            // glyph). The range is then held to what the product answers at
            // each end alone, which the probes hold to the reference.
            const ImTypesetDocumentRange at_a =
                ImTypeset::SelectDocumentRange(replay.document, a, a, granularity);
            const ImTypesetDocumentRange at_b =
                ImTypeset::SelectDocumentRange(replay.document, b, b, granularity);
            const auto less = [](const ImTypesetPosition& p, const ImTypesetPosition& q) {
                return p.Block < q.Block || (p.Block == q.Block && p.Offset < q.Offset);
            };
            ImTypesetDocumentRange joined;
            if (granularity == ImTypesetGranularity_Character) {
                joined.Start = less(at_b.Start, at_a.Start) ? at_b.Start : at_a.Start;
                joined.End = less(at_b.Start, at_a.Start) ? at_a.Start : at_b.Start;
            } else {
                joined.Start = less(at_b.Start, at_a.Start) ? at_b.Start : at_a.Start;
                joined.End = less(at_a.End, at_b.End) ? at_b.End : at_a.End;
            }
            if (CHECK(same_position(got.Start, joined.Start)) &&
                CHECK(same_position(got.End, joined.End)))
                ++tally.n[key + ", NOT the reference's: float pixels read an end back as another "
                                "scaled point (the range is what the product answers at each "
                                "end; asserted)"];
            continue;
        } else {
            FAIL("%s drag: the product selects (%d, %d)-(%d, %d); the reference [%lld, %lld), "
                 "which the product's convention puts at offsets %d and %d of blocks %d and %d",
                 drag.granularity.c_str(), got.Start.Block, got.Start.Offset, got.End.Block,
                 got.End.Offset, static_cast<long long>(drag.start),
                 static_cast<long long>(drag.end),
                 drag.start < layout.length ? layout.offset[static_cast<size_t>(drag.start)] : -1,
                 drag.end < layout.length ? layout.offset[static_cast<size_t>(drag.end)] : -1,
                 drag.start < layout.length ? layout.block_of[static_cast<size_t>(drag.start)] : -1,
                 drag.end < layout.length ? layout.block_of[static_cast<size_t>(drag.end)] : -1);
        }
        count_conventions(replay, layout, drag.start, drag.first, "a drag's `<span>` start", tally);
        // The script's end is its last sourced item's END byte; the product's
        // is the position of the range's end.
        count_conventions(replay, layout, drag.end, drag.last, "a drag's `<span>` end", tally);
    }
}

// A block's indices as the product maps them to offsets of its text, with one
// entry more for the end: a table's by the convention; elsewhere each item's
// byte, an item the text does not hold taking the next one's (src/imtypeset.cpp
// `map_index_bytes`).
std::vector<int> index_bytes(const Replayed& replay, const LayoutIn& layout, size_t b)
{
    const DocBlockIn& block = replay.doc.blocks[b];
    const BSpan& span = layout.bspan[b];
    const size_t count = static_cast<size_t>(span.end - span.start);
    std::vector<int> out(count + 1, block.offset(block.size()));
    for (size_t k = count; k-- > 0;) {
        const size_t i = static_cast<size_t>(span.start) + k;
        if (block.is_table)
            out[k] = layout.offset[i];
        else
            out[k] = layout.byte[i] >= 0 ? block.offset(layout.byte[i]) : out[k + 1];
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

ImVec4 rect_px(const RectRow& rect, double ppi)
{
    return ImVec4(px_float(rect.x, ppi), px_float(rect.y, ppi), px_float(rect.x + rect.width, ppi),
                  px_float(rect.y + rect.height, ppi));
}

bool same_rects(const std::vector<ImVec4>& got, const std::vector<ImVec4>& expected)
{
    if (got.size() != expected.size())
        return false;
    for (size_t k = 0; k < got.size(); ++k) {
        if (got[k].x != expected[k].x || got[k].y != expected[k].y || got[k].z != expected[k].z ||
            got[k].w != expected[k].w)
            return false;
    }
    return true;
}

std::string rects_text(const std::vector<ImVec4>& expected, const std::vector<ImVec4>& actual)
{
    std::string text = "the reference has";
    for (const ImVec4& rect : expected)
        text += check::format(" (%a %a %a %a)", static_cast<double>(rect.x),
                              static_cast<double>(rect.y), static_cast<double>(rect.z),
                              static_cast<double>(rect.w));
    text += "; the product";
    for (const ImVec4& rect : actual)
        text += check::format(" (%a %a %a %a)", static_cast<double>(rect.x),
                              static_cast<double>(rect.y), static_cast<double>(rect.z),
                              static_cast<double>(rect.w));
    return text;
}

std::vector<ImVec4> rects_of(Replayed& replay, const ImTypesetDocumentRange& range)
{
    ImVector<ImVec4> got;
    ImTypeset::GetDocumentRangeRects(replay.document, range, &got);
    return std::vector<ImVec4>(got.begin(), got.end());
}

std::vector<ImVec4> rects_px(const std::vector<RectRow>& rects, double ppi)
{
    std::vector<ImVec4> out;
    for (const RectRow& rect : rects)
        out.push_back(rect_px(rect, ppi));
    return out;
}

// A stretch of one text the range holds: a block's, or one cell's.
struct Segment {
    int row = -1;
    int column = -1;
    // Bytes of the BLOCK's text.
    int a = 0;
    int b = 0;
    int64_t first_index = 0;
    int64_t last_index = 0;
};

struct BlockPart {
    size_t block = 0;
    // The reference's indices of this block that the range holds.
    int64_t start = 0;
    int64_t end = 0;
    std::vector<Segment> segments;

    bool has_text() const { return !segments.empty(); }
};

// The seps between two pieces of selected text: after index `from`, up to
// index `to` (the reference's `text`, FORMAT-doc-tables.md §3.2).
std::string separators(const LayoutIn& layout, int64_t from, int64_t to)
{
    std::string out;
    for (const SepRow& sep : layout.seps) {
        if (sep.index > from && sep.index <= to)
            out += sep.text;
    }
    return out;
}

// One `range`: copy and rectangles.
void ask_range(Replayed& replay, const LayoutIn& layout, const RangeRow& range, Tally& tally)
{
    CHECK_CONTEXT("range [%lld, %lld) (line %d)", static_cast<long long>(range.start),
                  static_cast<long long>(range.end), range.line);
    const std::vector<DocBlockIn>& blocks = replay.doc.blocks;
    ++tally.n["range: read"];
    std::vector<BlockPart> parts;
    bool table = false;
    for (size_t b = 0; b < layout.bspan.size(); ++b) {
        const BSpan& span = layout.bspan[b];
        BlockPart part;
        part.block = b;
        part.start = std::max(range.start, span.start);
        part.end = std::min(range.end, span.end);
        if (part.start >= part.end)
            continue;
        table = table || blocks[b].is_table;
        for (int64_t i = part.start; i < part.end; ++i) {
            const size_t at = static_cast<size_t>(i);
            if (layout.sourced[at] < 0)
                continue;
            if (part.segments.empty() || part.segments.back().row != layout.row[at] ||
                part.segments.back().column != layout.column[at]) {
                Segment segment;
                segment.row = layout.row[at];
                segment.column = layout.column[at];
                segment.a = layout.sourced[at];
                segment.first_index = i;
                part.segments.push_back(segment);
            }
            part.segments.back().b = layout.sourced[at] + layout.length_of[at];
            part.segments.back().last_index = i;
        }
        parts.push_back(part);
    }
    const BlockPart* first = nullptr;
    const BlockPart* last = nullptr;
    for (const BlockPart& part : parts) {
        if (!part.has_text())
            continue;
        if (first == nullptr)
            first = &part;
        last = &part;
    }
    if (first == nullptr) {
        // Nothing a (block, byte) range can name: an empty range, or only
        // items the text does not hold.
        ++tally.n[range.start == range.end
                      ? "range: REFERENCE-ONLY, empty (nothing to ask)"
                      : "range: REFERENCE-ONLY, it holds no text (a marker, a line's fill or an "
                        "empty cell alone), which no (block, byte) range names"];
        CHECK_EQ(range.first.block, -1);
        return;
    }
    const std::string sort = table ? "through a table" : "outside any table";
    // The script's `<span>` is the first and the last sourced item, read here
    // off the same `tmap`.
    {
        const Segment& head = first->segments.front();
        const Segment& tail = last->segments.back();
        const CellIn* head_cell = blocks[first->block].cell(head.row, head.column);
        const CellIn* tail_cell = blocks[last->block].cell(tail.row, tail.column);
        const Pos head_pos{static_cast<int>(first->block), head.row, head.column,
                           head.a - (head_cell != nullptr ? head_cell->start : 0)};
        const Pos tail_pos{static_cast<int>(last->block), tail.row, tail.column,
                           tail.b - (tail_cell != nullptr ? tail_cell->start : 0)};
        CHECK(head_pos == range.first);
        CHECK(tail_pos == range.last);
    }
    const int a = first->segments.front().a;
    const int e = last->segments.back().b;
    if (!blocks[first->block].expressible(a) || !blocks[last->block].expressible(e)) {
        ++tally.n["range: REFERENCE-ONLY, an end inside a tab's spaces of a code block (the "
                  "product's offsets are the source's)"];
        return;
    }
    ImTypesetDocumentRange asked;
    asked.Start.Block = static_cast<int>(first->block);
    asked.Start.Offset = blocks[first->block].offset(a);
    asked.End.Block = static_cast<int>(last->block);
    asked.End.Offset = blocks[last->block].offset(e);
    ++tally.n["range: " + sort + ", asked of AppendDocumentRangeText"];

    // --- The reference's string, put together from the bytes of each block
    // and cell by the reference's rule: a `sep` row's text between two pieces
    // of selected text, a marker's glyphs as text. And the product's: the same
    // within a block, a blank line between two blocks, no marker.
    std::string reference;
    std::string product;
    int64_t written_to = -1;
    bool wrote = false;
    bool rebuilt = true;
    bool markers = false;
    const auto emit = [&](const std::string& piece, int64_t from, int64_t to) {
        if (piece.empty())
            return;
        if (wrote)
            reference += separators(layout, written_to, from);
        reference += piece;
        written_to = to;
        wrote = true;
    };
    for (const BlockPart& part : parts) {
        const DocBlockIn& block = blocks[part.block];
        const BSpan& span = layout.bspan[part.block];
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
                if (index >= layout.length || layout.byte[static_cast<size_t>(index)] >= 0)
                    rebuilt = false;
                piece += glyphs[g];
                if (from < 0)
                    from = index;
                to = index;
            }
            markers = markers || !piece.empty();
            emit(piece, from, to);
        }
        if (!part.has_text())
            continue;
        std::string own;
        if (block.is_table) {
            for (size_t s = 0; s < part.segments.size(); ++s) {
                const Segment& segment = part.segments[s];
                const std::string piece = block.content.substr(
                    static_cast<size_t>(segment.a), static_cast<size_t>(segment.b - segment.a));
                if (s != 0)
                    own += separators(layout, part.segments[s - 1].last_index, segment.first_index);
                own += piece;
                emit(piece, segment.first_index, segment.last_index);
            }
        } else {
            const Segment& segment = part.segments.front();
            // Inside the block the text's own newlines are the separators; a
            // `sep` row there that writes more than the newline (a list's
            // indent) replaces it.
            std::string text = block.content.substr(static_cast<size_t>(segment.a),
                                                    static_cast<size_t>(segment.b - segment.a));
            for (size_t s = layout.seps.size(); s-- > 0;) {
                const SepRow& sep = layout.seps[s];
                if (sep.pos.block != static_cast<int>(part.block) ||
                    sep.index <= segment.first_index || sep.index > segment.last_index ||
                    sep.pos.byte <= segment.a || sep.text == "\n")
                    continue;
                const size_t newline = static_cast<size_t>(sep.pos.byte - 1 - segment.a);
                if (newline < text.size() && text[newline] == '\n')
                    text.replace(newline, 1, sep.text);
                else
                    rebuilt = false;
            }
            emit(text, segment.first_index, segment.last_index);
            own = block.product_slice(segment.a, segment.b);
        }
        if (own.empty())
            continue;
        if (!product.empty())
            product += "\n\n";
        product += own;
    }
    if (rebuilt && reference == range.copy) {
        ++tally.n["range: the reference's `copy` is rebuilt from the cells' bytes and the `sep` "
                  "rows (the reading of the golden holds)"];
    } else {
        ++tally.n["range: UNEXPLAINED, the reference's `copy` is not rebuilt from the `tmap` and "
                  "the `sep` rows"];
        FAIL("the reference copies \"%s\"; from the map's bytes and the `sep` rows comes \"%s\"",
             escaped(range.copy).c_str(), escaped(reference).c_str());
    }
    ImGuiTextBuffer buffer;
    ImTypeset::AppendDocumentRangeText(replay.document, asked, &buffer);
    const std::string copied(buffer.c_str());
    if (copied == range.copy) {
        ++tally.n["range: " + sort + ", copy equal to the reference's `copy`, byte for byte"];
    } else if (first->block != last->block || markers) {
        // PRODUCT RULE: a blank line between two blocks whatever they are (the
        // reference: one newline before a list item or a table in one, and
        // the list's indent), and no marker.
        if (CHECK_EQ(copied, product))
            ++tally.n["range: " + sort +
                      ", copy differs from the reference's by the product's rule between blocks "
                      "(a blank line, no marker); equal to that rule's string"];
    } else {
        FAIL("AppendDocumentRangeText of (%d, %d)-(%d, %d) gives \"%s\"; the reference copies "
             "\"%s\"",
             asked.Start.Block, asked.Start.Offset, asked.End.Block, asked.End.Offset,
             escaped(copied).c_str(), escaped(range.copy).c_str());
    }

    // --- The rectangles, block by block.
    const std::vector<ImVec4> got = rects_of(replay, asked);
    size_t expected_seen = 0;
    for (const BlockPart& part : parts) {
        const BSpan& span = layout.bspan[part.block];
        const DocBlockIn& block = blocks[part.block];
        const std::string where = block.is_table ? "rects: a table's part of a range"
                                                 : "rects: another block's part of a range";
        std::vector<ImVec4> expected;
        for (const RectRow& rect : range.rects) {
            if (rect.y >= span.y && rect.y < span.y + span.height)
                expected.push_back(rect_px(rect, replay.ppi));
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
        if (in_asked && block.in.type != BlockType::RULE) {
            const std::vector<int> bytes = index_bytes(replay, layout, part.block);
            const int from = part.block == first->block ? asked.Start.Offset : 0;
            const int to =
                part.block == last->block ? asked.End.Offset : block.offset(block.size());
            if (from < to) {
                named_start = span.start + index_named(bytes, from);
                named_end = span.start + index_named(bytes, to);
            }
        }
        ++tally.n[where];
        if (!part.has_text() || !in_asked) {
            ++tally.n[where + ", REFERENCE-ONLY: it holds no text (the product has no rectangle "
                              "there; asserted)"];
            CHECK(actual.empty());
            continue;
        }
        if (block.is_table && (named_start != part.start || named_end != part.end)) {
            // In a table the items without a byte are glue that places a
            // cell: selecting them or not, the rectangles are the same, and
            // they are held to the reference's below.
            ++tally.n[where + ", the product's bytes name other glue than the reference's range "
                              "(compared all the same)"];
        } else if (named_start != part.start || named_end != part.end) {
            const bool marker = block.in.has_marker && part.start < named_start;
            ++tally.n[where + ", REFERENCE-ONLY: " +
                      (marker ? "the reference's range holds the marker, which the product never "
                                "selects"
                       : part.end < named_end
                           ? "the reference's range stops before items without text that the "
                             "product's end byte takes in"
                       : part.end > named_end
                           ? "the reference's range ends in items the product's bytes do not name"
                           : "the reference's range starts in items the product's bytes do not "
                             "name")];
            if (same_rects(actual, expected))
                ++tally.n[where + ", REFERENCE-ONLY:   ...of which the rectangles are the "
                                  "reference's all the same"];
            continue;
        }
        if (same_rects(actual, expected)) {
            ++tally.n[where + ", rectangles equal to the reference's, exactly in float pixels"];
            tally.n["rects: rectangles compared equal"] += static_cast<int>(expected.size());
        } else {
            FAIL("block %zu: %s", part.block, rects_text(expected, actual).c_str());
        }
    }
    // Every rectangle of the reference's is some block's.
    CHECK_EQ(expected_seen, range.rects.size());
    count_conventions(replay, layout, range.start, range.first, "a range's `<span>` start", tally);
    count_conventions(replay, layout, range.end, range.last, "a range's `<span>` end", tally);
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
        const std::string sort = block.is_table ? "blocktext: a table" : "blocktext: another block";
        ++tally.n[sort + ", asked of AppendDocumentRangeText"];
        if (copied == layout.blocktext[b]) {
            ++tally.n[sort + ", equal to the reference's, byte for byte"];
            continue;
        }
        if (block.is_table) {
            FAIL("the table copies as \"%s\"; the reference's `blocktext` is \"%s\"",
                 escaped(copied).c_str(), escaped(layout.blocktext[b]).c_str());
            continue;
        }
        // What the reference has more: the marker and a space, tabs as spaces.
        CHECK_EQ(copied, block.in.type == BlockType::RULE ? std::string()
                                                          : block.product_slice(0, block.size()));
        std::string reference = block.content;
        if (block.in.has_marker)
            reference = block.in.marker_text + " " + reference;
        if (CHECK_EQ(reference, layout.blocktext[b]))
            ++tally
                  .n[sort + ", differs by the product's rule: " +
                     (block.in.has_marker ? "a marker is not copied" : "a tab is copied as a tab")];
    }
}

// `bspan` against GetDocumentBlockGeometry.
void ask_geometry(Replayed& replay, const LayoutIn& layout, Tally& tally)
{
    for (size_t b = 0; b < layout.bspan.size(); ++b) {
        CHECK_CONTEXT("bspan %zu", b);
        const BSpan& span = layout.bspan[b];
        const ImTypesetBlockGeometry geometry =
            ImTypeset::GetDocumentBlockGeometry(replay.document, static_cast<int>(b));
        ++tally.n["bspan: asked of GetDocumentBlockGeometry"];
        CHECK(geometry.LaidOut);
        const bool vertical = geometry.FullMin.y == px_float(span.y, replay.ppi) &&
                              geometry.FullMax.y == px_float(span.y + span.height, replay.ppi);
        if (CHECK(vertical))
            ++tally.n["bspan: top and bottom equal to the reference's, in float pixels"];
        CHECK_EQ(geometry.FullMax.x, px_float(span.x + span.width, replay.ppi));
    }
}

// `tgeom`, `trow`, `tcell`: the table layout's own numbers against the
// golden's, exactly; the public geometry against those numbers, as the float
// each comes to; and each cell's rectangles through GetDocumentRangeRects.
void ask_tables(Replayed& replay, const LayoutIn& layout, Tally& tally)
{
    const std::vector<DocBlockIn>& blocks = replay.doc.blocks;
    Document& inner = block_document(*replay.document);
    const double ppi = replay.ppi;
    for (const TGeom& geom : layout.tgeoms) {
        CHECK_CONTEXT("tgeom (line %d)", geom.line);
        const size_t b = static_cast<size_t>(geom.block);
        if (!CHECK(b < blocks.size()) || !CHECK(blocks[b].is_table))
            continue;
        ++tally.n["tgeom: tables"];
        const TableLayout& table = inner.layout(b).table;
        const ImTypesetTableGeometry got =
            ImTypeset::GetDocumentTableGeometry(replay.document, geom.block);
        CHECK(got.LaidOut);
        CHECK_EQ(got.RowCount, static_cast<int>(geom.rows));
        CHECK_EQ(got.ColumnCount, static_cast<int>(geom.widths.size()));
        CHECK_EQ(got.CellCount, static_cast<int>(blocks[b].cells.size()));
        // The widths are the reference's own (the cell boxes').
        if (CHECK_EQ(tables_golden::counted("widths", table.columns),
                     tables_golden::counted("widths", geom.widths)) &&
            CHECK_EQ(tables_golden::counted("widths", table.solve.columns),
                     tables_golden::counted("widths", geom.widths)))
            ++tally.n["tgeom: column widths equal to the reference's, in scaled points"];
        // `left` is the script's arithmetic over the reference's own objects:
        // the x of the `line` command the row box ships, plus the table's
        // left indent. Both are the reference's, so it is held exactly.
        if (CHECK_EQ(table.indent, geom.left))
            ++tally.n["tgeom: left edge equal to the golden's `left`, in scaled points"];
        CHECK_EQ(got.Min.x, px_float(table.indent, ppi));
        CHECK_EQ(got.Min.y, px_float(replay.spans[b].top, ppi));
        Scaled left = table.indent;
        bool columns = true;
        for (size_t c = 0; c < table.columns.size(); ++c) {
            const ImTypesetTableColumnGeometry column =
                ImTypeset::GetDocumentTableColumn(replay.document, geom.block, static_cast<int>(c));
            columns = CHECK_EQ(column.Min, px_float(left, ppi)) && columns;
            left += table.columns[c];
            columns = CHECK_EQ(column.Max, px_float(left, ppi)) && columns;
            if (c < table.widest.size())
                columns = CHECK_EQ(column.Natural, px_float(table.widest[c], ppi)) && columns;
            ++tally.n["tgeom: columns asked of GetDocumentTableColumn"];
        }
        CHECK_EQ(got.Max.x, px_float(left, ppi));
        if (columns)
            ++tally.n["tgeom: tables whose public column edges are the layout's, as floats"];
    }
    for (const TRow& row : layout.trows) {
        CHECK_CONTEXT("trow (line %d)", row.line);
        const size_t b = static_cast<size_t>(row.block);
        if (!CHECK(b < blocks.size()) || !CHECK(blocks[b].is_table))
            continue;
        ++tally.n["trow: rows"];
        const TableLayout& table = inner.layout(b).table;
        const size_t r = static_cast<size_t>(row.row);
        if (!CHECK(r < table.rows.size()) || !CHECK(r + 1 < table.row_index.size()))
            continue;
        const int64_t base = layout.bspan[b].start;
        if (CHECK_EQ(base + table.row_index[r], row.start) &&
            CHECK_EQ(base + table.row_index[r + 1], row.end))
            ++tally.n["trow: index range equal to the reference's"];
        const Scaled top = replay.spans[b].top + table.row_top[r];
        const Scaled height = table.rows[r].dims.height + table.rows[r].dims.depth;
        // `top` and `height` are the script's arithmetic over the `line`
        // command the reference ships for the row box (y - height, height +
        // depth), and `wrapped` is whether a cell box of the row is a
        // vertical list: derived from the reference's objects with nothing
        // chosen, so each is held exactly, in scaled points.
        if (CHECK_EQ(top, row.top) && CHECK_EQ(height, row.height))
            ++tally.n["trow: top and height equal to the golden's, in scaled points"];
        if (CHECK(table.rows[r].wrapped == row.wrapped))
            ++tally.n["trow: wrapped equal to the golden's"];
        const ImTypesetTableRowGeometry got =
            ImTypeset::GetDocumentTableRow(replay.document, row.block, row.row);
        const bool same = CHECK_EQ(got.Min, px_float(top, ppi)) &&
                          CHECK_EQ(got.Max, px_float(top + height, ppi)) &&
                          CHECK(got.Wrapped == table.rows[r].wrapped) &&
                          CHECK_EQ(got.CellStart, static_cast<int>(blocks[b].row_first[r])) &&
                          CHECK_EQ(got.CellCount, static_cast<int>(blocks[b].row_first[r + 1] -
                                                                   blocks[b].row_first[r]));
        if (same)
            ++tally.n["trow: GetDocumentTableRow is the layout's, as floats"];
    }
    for (const TCell& cell : layout.tcells) {
        CHECK_CONTEXT("tcell (line %d)", cell.line);
        const size_t b = static_cast<size_t>(cell.block);
        if (!CHECK(b < blocks.size()) || !CHECK(blocks[b].is_table))
            continue;
        ++tally.n["tcell: cells"];
        const DocBlockIn& block = blocks[b];
        const TableLayout& table = inner.layout(b).table;
        const size_t k =
            static_cast<size_t>(cell.row) * block.columns + static_cast<size_t>(cell.column);
        const int64_t base = layout.bspan[b].start;
        if (CHECK(k + 1 < table.cell_index.size()) &&
            CHECK_EQ(base + table.cell_index[k], cell.start) &&
            CHECK_EQ(base + table.cell_index[k + 1], cell.end))
            ++tally.n["tcell: index range equal to the reference's"];
        const CellIn* in = block.cell(cell.row, cell.column);
        if (in == nullptr) {
            ++tally.n["tcell: REFERENCE-ONLY, a cell its row lacks"];
            continue;
        }
        // The position of each end of the cell, and back.
        const ImTypesetPosition start = ImTypeset::GetDocumentTablePosition(
            replay.document, cell.block, cell.row, cell.column, 0);
        const ImTypesetPosition end = ImTypeset::GetDocumentTablePosition(
            replay.document, cell.block, cell.row, cell.column, in->end - in->start);
        CHECK_EQ(start.Offset, in->start);
        CHECK_EQ(end.Offset, in->end);
        for (const ImTypesetPosition& position : {start, end}) {
            const ImTypesetCellPosition back =
                ImTypeset::GetDocumentTableCell(replay.document, position);
            CHECK_EQ(back.Row, cell.row);
            CHECK_EQ(back.Column, cell.column);
            CHECK_EQ(back.Offset, position.Offset - in->start);
        }
        const ImTypesetDocumentRange whole = {start, end};
        const std::vector<ImVec4> got = rects_of(replay, whole);
        const std::vector<ImVec4> expected = rects_px(cell.rects, ppi);
        ++tally.n["tcell: rects, the cell's bytes asked of GetDocumentRangeRects"];
        if (in->start == in->end) {
            // No byte range names an empty cell.
            if (CHECK(got.empty()) && CHECK(expected.empty()))
                ++tally.n["tcell: rects, an empty cell has none, in the reference and the product"];
            continue;
        }
        // The cell's bytes do not always name the cell's whole index range:
        // the glue that places it in its column has no byte. Counted; the
        // rectangles are held to the reference's all the same.
        const std::vector<int> bytes = index_bytes(replay, layout, b);
        const int64_t named_start = base + index_named(bytes, in->start);
        const int64_t named_end = base + index_named(bytes, in->end);
        if (named_start != cell.start || named_end != cell.end)
            ++tally.n["tcell: rects, cells whose bytes name fewer indices than the reference's "
                      "range (the glue that places the cell)"];
        if (same_rects(got, expected)) {
            ++tally.n["tcell: rects equal to the reference's, exactly in float pixels"];
            tally.n["rects: rectangles compared equal"] += static_cast<int>(expected.size());
        } else {
            FAIL("bytes [%d, %d) of the block name indices [%lld, %lld), the cell's are [%lld, "
                 "%lld): %s",
                 in->start, in->end, static_cast<long long>(named_start),
                 static_cast<long long>(named_end), static_cast<long long>(cell.start),
                 static_cast<long long>(cell.end), rects_text(expected, got).c_str());
        }
    }
    // `atomtext` and `original`: what a copy writes for an item is the bytes
    // the `tmap` gives it, which is what the ranges' copies are rebuilt from.
    for (const std::vector<SepRow>* rows : {&layout.atomtexts, &layout.originals}) {
        for (const SepRow& row : *rows) {
            const size_t i = static_cast<size_t>(row.index);
            if (!CHECK(row.index >= 0 && row.index < layout.length))
                continue;
            const DocBlockIn& block = blocks[static_cast<size_t>(layout.block_of[i])];
            ++tally.n[rows == &layout.atomtexts ? "atomtext: rows" : "original: rows"];
            if (CHECK(layout.sourced[i] >= 0) &&
                CHECK_EQ(block.content.substr(static_cast<size_t>(layout.sourced[i]),
                                              static_cast<size_t>(layout.length_of[i])),
                         row.text))
                ++tally.n[rows == &layout.atomtexts
                              ? "atomtext: the text is the item's bytes of its cell or block"
                              : "original: the text is the item's bytes of its cell or block"];
        }
    }
}

// A stretch of a block's or a cell's text as a document range.
ImTypesetDocumentRange range_of(const DocBlockIn& block, int number, int row, int column, int a,
                                int b)
{
    const CellIn* cell = block.cell(row, column);
    const int base = cell != nullptr ? cell->start : 0;
    return ImTypesetDocumentRange{{number, block.offset(base + a)},
                                  {number, block.offset(base + b)}};
}

// `match`: where a find hit is. The product has no search; a host's is over
// the text it handed in, and the hit's bytes are what it marks.
void ask_matches(Replayed& replay, const LayoutIn& layout, Tally& tally)
{
    const std::vector<DocBlockIn>& blocks = replay.doc.blocks;
    for (const MatchRow& match : layout.matches) {
        CHECK_CONTEXT("match (line %d)", match.line);
        const size_t b = static_cast<size_t>(match.block);
        if (!CHECK(b < blocks.size()))
            continue;
        const DocBlockIn& block = blocks[b];
        const std::string sort = block.is_table ? "match: in a cell" : "match: in another block";
        ++tally.n[sort + ", asked of GetDocumentRangeRects"];
        if (block.is_table != (match.row >= 0)) {
            FAIL("a match's row does not say whether its block is a table");
            continue;
        }
        const CellIn* cell = block.cell(match.row, match.column);
        const int base = cell != nullptr ? cell->start : 0;
        if (!block.expressible(base + match.a) || !block.expressible(base + match.b)) {
            ++tally.n[sort + ", REFERENCE-ONLY: an end inside a tab's spaces"];
            continue;
        }
        const ImTypesetDocumentRange range =
            range_of(block, match.block, match.row, match.column, match.a, match.b);
        const std::vector<ImVec4> got = rects_of(replay, range);
        const std::vector<ImVec4> expected = rects_px(match.rects, replay.ppi);
        if (same_rects(got, expected)) {
            ++tally.n[sort + ", rectangles equal to the reference's, exactly in float pixels"];
            tally.n["rects: rectangles compared equal"] += static_cast<int>(expected.size());
            continue;
        }
        // Which items the bytes name, to say what the difference is of.
        const std::vector<int> bytes = index_bytes(replay, layout, b);
        const int64_t base_index = layout.bspan[b].start;
        FAIL("bytes [%d, %d) of the cell's text name indices [%lld, %lld) by the product's "
             "convention: %s",
             match.a, match.b,
             static_cast<long long>(base_index + index_named(bytes, range.Start.Offset)),
             static_cast<long long>(base_index + index_named(bytes, range.End.Offset)),
             rects_text(expected, got).c_str());
    }
}

// `linkarea`: a link is a run with a Tag. Its rectangles are its bytes'; the
// tag under the middle of each is the link's.
void ask_links(Replayed& replay, const LayoutIn& layout, Tally& tally)
{
    const std::vector<DocBlockIn>& blocks = replay.doc.blocks;
    for (const LinkRow& area : layout.linkareas) {
        CHECK_CONTEXT("linkarea (line %d)", area.line);
        const size_t b = static_cast<size_t>(area.block);
        if (!CHECK(b < blocks.size()))
            continue;
        const DocBlockIn& block = blocks[b];
        const std::string sort = block.is_table ? "linkarea: in a cell"
                                                : "linkarea: in another "
                                                  "block";
        ++tally.n[sort + ", asked of GetDocumentRangeRects"];
        const Link* link = nullptr;
        int row = -1;
        int column = -1;
        if (block.is_table) {
            for (const CellIn& cell : block.cells) {
                const int local = area.number - cell.links_before;
                if (local >= 0 && static_cast<size_t>(local) < cell.links.size()) {
                    link = &cell.links[static_cast<size_t>(local)];
                    row = cell.row;
                    column = cell.column;
                }
            }
            // `row` and `column` are the script's arithmetic: the cell box
            // that holds the first index of the link's first run, which is
            // the cell whose links the link is counted among. Held exactly.
            if (CHECK_EQ(row, area.row) && CHECK_EQ(column, area.column))
                ++tally.n["linkarea: in a cell, the cell is the golden's `row`, `column`"];
        } else if (area.number >= 0 && static_cast<size_t>(area.number) < block.links.size()) {
            link = &block.links[static_cast<size_t>(area.number)];
        }
        if (!CHECK(link != nullptr))
            continue;
        const ImTypesetDocumentRange range =
            range_of(block, area.block, row, column, link->start, link->end);
        const std::vector<ImVec4> got = rects_of(replay, range);
        const std::vector<ImVec4> expected = rects_px(area.rects, replay.ppi);
        if (same_rects(got, expected)) {
            ++tally.n[sort + ", rectangles equal to the reference's, exactly in float pixels"];
            tally.n["rects: rectangles compared equal"] += static_cast<int>(expected.size());
        } else {
            FAIL("%s", rects_text(expected, got).c_str());
        }
        for (const RectRow& rect : area.rects) {
            Scaled seen_y = 0;
            const ImVec2 middle =
                replay.point(rect.x + rect.width / 2, rect.y + rect.height / 2, tally, &seen_y);
            const ImTypesetDocumentHit hit = ImTypeset::HitTestDocument(replay.document, middle);
            ++tally.n[sort + ", the middle of a rectangle asked of HitTestDocument"];
            CHECK_EQ(hit.Position.Block, area.block);
            if (CHECK(hit.Inside))
                ++tally.n[sort + ", the middle of a rectangle is Inside"];
            if (hit.Tag == area.number + 1)
                ++tally.n[sort + ", the Tag under the middle of a rectangle is the link's"];
            else
                FAIL("the Tag under the middle of a rectangle of link %d, bytes [%d, %d) of the "
                     "block, is %d: HitTestDocument answers offset %d, line %d, Inside %d",
                     area.number, range.Start.Offset, range.End.Offset, hit.Tag,
                     hit.Position.Offset, hit.Line, hit.Inside ? 1 : 0);
            if (block.is_table) {
                CHECK_EQ(hit.Row, row);
                CHECK_EQ(hit.Column, column);
            }
        }
    }
}

// The table's index-to-byte map as the document keeps it, against the
// convention read off the `tmap`: every index, exactly.
void hold_table_bytes(Replayed& replay, const LayoutIn& layout, Tally& tally)
{
    for (size_t b = 0; b < replay.doc.blocks.size(); ++b) {
        if (!replay.doc.blocks[b].is_table)
            continue;
        CHECK_CONTEXT("the index-to-byte map of table %zu", b);
        // The map is made when the block is first asked about.
        ImTypeset::GetDocumentTableGeometry(replay.document, static_cast<int>(b));
        const std::vector<uint32_t>& kept = replay.document->blocks[b].table_bytes;
        const std::vector<int> expected = index_bytes(replay, layout, b);
        ++tally.n["tmap: tables whose index-to-byte map is compared"];
        if (!CHECK_EQ(kept.size(), expected.size()))
            continue;
        size_t differ = 0;
        for (size_t k = 0; k < kept.size(); ++k) {
            if (static_cast<int64_t>(kept[k]) == static_cast<int64_t>(expected[k]))
                continue;
            if (differ++ < 5)
                FAIL("index %lld (row %d, column %d, byte %d of its cell): the product's offset "
                     "is %u, the convention's %d",
                     static_cast<long long>(layout.bspan[b].start + static_cast<int64_t>(k)),
                     k + 1 < kept.size()
                         ? layout.row[static_cast<size_t>(layout.bspan[b].start) + k]
                         : -1,
                     k + 1 < kept.size()
                         ? layout.column[static_cast<size_t>(layout.bspan[b].start) + k]
                         : -1,
                     k + 1 < kept.size()
                         ? layout.byte[static_cast<size_t>(layout.bspan[b].start) + k]
                         : -1,
                     kept[k], expected[k]);
        }
        tally.n["tmap: indices compared"] += static_cast<int>(kept.size());
        if (differ == 0)
            ++tally.n["tmap: tables whose every index is at the byte the convention gives it"];
    }
}

} // namespace

// --- doc-tables/selection.golden ---------------------------------------------------------

TEST(a_table_among_blocks_selects_hits_and_copies_as_the_reference_does)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    REQUIRE(!golden::list("doc-tables").empty());
    golden::File file;
    REQUIRE(file.load(golden::path("doc-tables/selection.golden")));
    Tally tally;
    for (const core_golden::Case& c : core_golden::cases(file)) {
        CHECK_CONTEXT("doc-tables/selection.golden case %s (line %d)", c.name.c_str(), c.line);
        if (!CHECK(c.kind == "tdoc"))
            continue;
        ++tally.n["cases"];
        core_golden::Context context;
        core_golden::Reader reader(file, c);
        Replayed replay;
        if (const char* why = open_case(stage, reader, replay)) {
            ++tally.n[std::string("SKIPPED cases, not expressible: ") + why];
            std::printf("  SKIPPED case %s: %s\n", c.name.c_str(), why);
            continue;
        }
        ++tally.n["cases described through the public header"];
        while (!reader.done() && reader.next_is("layout")) {
            const LayoutIn layout = read_layout(reader, context, replay.doc.blocks);
            CHECK_CONTEXT("layout %s (line %d)", layout.name.c_str(), layout.line);
            ++tally.n["layouts"];
            tally.n["found: needles NOT REPLAYED (the product has no search)"] += layout.founds;
            if (!lay_out(replay, layout, context, tally))
                continue;
            hold_table_bytes(replay, layout, tally);
            ask_geometry(replay, layout, tally);
            ask_tables(replay, layout, tally);
            ask_probes(replay, layout, tally);
            ask_drags(replay, layout, tally);
            for (const RangeRow& range : layout.ranges)
                ask_range(replay, layout, range, tally);
            ask_blocktexts(replay, layout, tally);
            ask_matches(replay, layout, tally);
            ask_links(replay, layout, tally);
        }
    }
    tally.print("doc-tables/selection.golden through the public Document calls:");
    // A replay that quietly shrank must not read as a pass.
    CHECK_EQ(tally.of("cases"), 9);
    CHECK_EQ(tally.of("layouts"), 15);
    CHECK_EQ(tally.of("layouts whose commands and height are the reference's, exactly"), 15);
    CHECK_EQ(tally.of("probe: asked of HitTestDocument"), 2141);
    CHECK_EQ(tally.of("drag: asked of SelectDocumentRange"), 1899);
    CHECK_EQ(tally.of("range: read"), 1837);
    CHECK_EQ(tally.of("tcell: cells"), 333);
    CHECK_EQ(tally.of("trow: rows"), 116);
    CHECK_EQ(tally.of("tgeom: tables"), 29);
    CHECK_EQ(tally.of("tmap: tables whose every index is at the byte the convention gives it"), 29);
    CHECK_EQ(tally.of("tcell: index range equal to the reference's"), 333);
    CHECK_EQ(tally.of("tcell: rects equal to the reference's, exactly in float pixels"), 309);
    CHECK_EQ(tally.of("probe: in a table, the model's index equal to the reference's `hit`"), 1848);
    CHECK_EQ(tally.of("probe: in a table, public position is the index's by the convention"), 1848);
    CHECK_EQ(tally.of("probe: in a table, public position is the reference's `hit`"), 1751);
    // The cell, by the product's rule (D60): every probe in a table, of
    // which 1,284 are inside a cell's rectangle and the rest in the space
    // between rows or beside the table. The reference's index is in another
    // cell than the point for 465 of them, 253 of those inside a rectangle.
    CHECK_EQ(tally.of("probe: in a table, Row and Column held to the cell under the point"), 1848);
    CHECK_EQ(
        tally.of("probe: in a table, the product's position is a byte of the cell under the point"),
        1848);
    CHECK_EQ(tally.of("probe: in a table, the point is inside a cell's rectangle"), 1284);
    CHECK_EQ(tally.of("probe: in a table, the product's position is the reference's index's"),
             1366);
    CHECK_EQ(tally.of("probe: in a table, the reference's index is in ANOTHER cell than the point "
                      "(the product answers the point's)"),
             465);
    CHECK_EQ(tally.of("probe:   ...of which the point is inside a cell's rectangle"), 253);
    CHECK_EQ(tally.of("tgeom: left edge equal to the golden's `left`, in scaled points"), 29);
    CHECK_EQ(tally.of("trow: top and height equal to the golden's, in scaled points"), 116);
    CHECK_EQ(tally.of("trow: wrapped equal to the golden's"), 116);
    CHECK_EQ(tally.of("linkarea: in a cell, the cell is the golden's `row`, `column`"), 19);
    CHECK_EQ(tally.of("drag: character, an end in a table, range equal to the reference's"), 473);
    CHECK_EQ(tally.of("drag: word, an end in a table, range equal to the reference's"), 481);
    CHECK_EQ(tally.of("drag: line, an end in a table, range equal to the reference's"), 480);
    CHECK_EQ(tally.of("range: through a table, asked of AppendDocumentRangeText"), 1247);
    CHECK_EQ(tally.of("range: through a table, copy equal to the reference's `copy`, byte for "
                      "byte"),
             1223);
    CHECK_EQ(tally.of("rects: a table's part of a range, rectangles equal to the reference's, "
                      "exactly in float pixels"),
             1286);
    CHECK_EQ(tally.of("match: in a cell, rectangles equal to the reference's, exactly in float "
                      "pixels"),
             115);
    CHECK_EQ(tally.of("linkarea: in a cell, rectangles equal to the reference's, exactly in float "
                      "pixels"),
             19);
}

// --- tables/{widths,shapes,demo}.golden --------------------------------------------------
//
// One table alone (FORMAT-tables.md §3). The INPUT is each cell's events: a
// `run` is a font, whether it may break and its text, and a cell's text is its
// runs' in order. An `atom` is an inline formula recorded as the box it
// became, with no source: a table that holds one cannot be said through the
// header, and its case is skipped, counted and named.
//
// The core's own replay of these files, from the recorded node lists, is
// tests/test_align.cpp. Here the same answers are asked of a document that was
// handed text.

namespace
{

struct AloneIn {
    // The theme, the style, the policy; `rows` is not read into.
    tables_golden::TableIn head;
    DocBlockIn block;
    uint32_t atoms = 0;
};

// `table` ... `endtable` of FORMAT-tables.md §3.1, keeping the runs.
void read_alone(core_golden::Reader& reader, core_golden::Context& context, AloneIn& out)
{
    tables_golden::TableIn& head = out.head;
    DocBlockIn& block = out.block;
    const golden::Record& open = reader.take("table");
    golden::Cursor cursor(open);
    block.in.line = open.line;
    block.in.type = BlockType::TABLE;
    block.is_table = true;
    block.columns = head.columns = cursor.u32();
    head.row_count = cursor.u32();
    block.has_header = head.has_header = cursor.boolean();
    block.in.left_indent = head.style.left_indent = cursor.i64();
    block.in.space_above = head.space_above = cursor.i64();
    block.in.space_below = head.space_below = cursor.i64();
    block.in.font = head.font = cursor.str();
    cursor.expect_done();
    blocks_golden::read_style(reader, block.in);
    head.style.interline = block.in.interline;
    head.style.strut = block.in.strut;

    golden::Cursor aligns(reader.take("aligns"));
    const uint32_t align_count = aligns.u32();
    for (uint32_t i = 0; i < align_count && !aligns.done(); ++i)
        block.aligns.push_back(static_cast<ColumnAlign>(core_golden::token_index(
            aligns.raw(), tables_golden::COLUMN_ALIGN_TOKENS, "ColumnAlign")));
    aligns.expect_done();

    golden::Cursor policy(reader.take("policy"));
    const std::string kind(policy.raw());
    if (kind == "widths") {
        head.policy = WidthPolicy::WIDTHS;
        const uint32_t count = policy.u32();
        for (uint32_t i = 0; i < count && !policy.done(); ++i)
            head.widths.push_back(policy.i64());
    } else if (kind == "fractions") {
        head.policy = WidthPolicy::FRACTIONS;
        const uint32_t count = policy.u32();
        for (uint32_t i = 0; i < count && !policy.done(); ++i)
            head.fractions.push_back(policy.f64());
    } else if (kind != "apportioned") {
        check::fail(open.file, open.line, "the policy is not a width policy");
    }
    policy.expect_done();

    block.row_first.push_back(0);
    while (!reader.done() && reader.next_is("row")) {
        const golden::Record& row = reader.take();
        const uint32_t cell_count = row.u32(1);
        for (uint32_t c = 0; c < cell_count && !reader.done(); ++c) {
            const golden::Record& cell_open = reader.take("cell");
            CellIn cell;
            cell.line = cell_open.line;
            cell.row = static_cast<int>(block.rows());
            cell.column = static_cast<int>(c);
            const uint32_t events = cell_open.u32(2);
            for (uint32_t e = 0; e < events && !reader.done(); ++e) {
                if (reader.next_is("atom")) {
                    reader.take();
                    core_golden::read_node(reader, context);
                    ++out.atoms;
                    continue;
                }
                golden::Cursor run(reader.take("run"));
                Piece piece;
                piece.font = blocks_golden::spec_of(run.str());
                piece.breakable = run.boolean();
                const std::string text = run.str();
                run.expect_done();
                piece.start = static_cast<int>(cell.content.size());
                cell.content += text;
                piece.end = static_cast<int>(cell.content.size());
                cell.pieces.push_back(piece);
            }
            core_golden::read_nodes(reader, context);
            blocks_golden::read_node_lines(reader, context, "unshrinkable");
            reader.take("endcell");
            block.cells.push_back(std::move(cell));
        }
        reader.take("endrow");
        block.row_first.push_back(block.cells.size());
    }
    reader.take("endtable");
    if (block.rows() != head.row_count)
        check::fail(open.file, open.line, "`table` does not count its rows");
    join_cells(block);
}

// Skips records up to and including the next one tagged `tag`.
void skip_past(core_golden::Reader& reader, std::string_view tag)
{
    while (!reader.done() && !reader.next_is(tag))
        reader.take();
    reader.take(tag);
}

// The float of pixels the document reads back as exactly `length` scaled
// points (src/units.h `sp_from_px`, which every length and the width go
// through), or a negative number if there is none among the floats nearest it.
float exact_px(Scaled length, double ppi)
{
    float px = px_float(length, ppi);
    for (int step = 0; step < 8; ++step) {
        const Scaled back = sp_from_px(static_cast<double>(px), ppi);
        if (back == length)
            return px;
        px = std::nextafter(px, back < length ? 1.0e9f : -1.0e9f);
    }
    return -1.0f;
}

struct AloneTally {
    std::map<std::string, int> n;
    int lengths = 0;
};

void replay_alone(Stage& stage, const golden::File& file, const core_golden::Case& c,
                  AloneTally& tally)
{
    core_golden::Context context;
    core_golden::Reader reader(file, c);
    reader.take("source");
    AloneIn in;
    tables_golden::read_theme(reader, context, in.head);
    read_alone(reader, context, in);
    const tables_golden::TableIn& head = in.head;
    const DocBlockIn& block = in.block;
    const double ppi = head.px_per_inch;
    ++tally.n["cases"];
    int layouts_here = 0;
    for (size_t at = reader.position(); at < c.end; ++at)
        layouts_here += file.records()[at].is("layout") ? 1 : 0;
    tally.n["layouts"] += layouts_here;

    // --- Whether the table can be said.
    const char* why = nullptr;
    Spacing spacing = Spacing::EXTRA_SPACE_ONLY;
    if (in.atoms != 0)
        why = "a cell holds an inline formula recorded as a box (`atom`), with no source";
    else if (!blocks_golden::spacing_of(head.spacing, &spacing))
        why = "a spacing the header has no word for";
    else if (head.space_above < 0 || head.space_below < 0)
        why = "a negative space, which the header does not take";
    for (size_t r = 0; why == nullptr && r < block.rows(); ++r) {
        if (block.row_first[r] == block.row_first[r + 1])
            why = "a row with no cells, which the header cannot say: a row is begun by its first "
                  "cell (ImTypesetCellFlags_RowStart)";
    }
    Content content;
    blocks_golden::ThemeIn theme;
    theme.px_per_inch = ppi;
    const FontSpec font = blocks_golden::spec_of(block.in.font);
    theme.marker_gap = font.size / 2;
    if (why == nullptr) {
        content.styles.push_back(style_of(block.in, theme, font.size));
        why = describe_table(block, content, 0, ppi, 0);
    }
    if (why == nullptr) {
        // Columns the alignments name past the longest row are not read.
        for (uint32_t col = 0; col < block.columns; ++col) {
            ImTypesetTableColumn& column = content.columns[col];
            if (head.policy == WidthPolicy::WIDTHS && col < head.widths.size()) {
                column.Width = exact_px(head.widths[col], ppi);
                if (column.Width <= 0.0f)
                    why = "a stated width in scaled points that no float of pixels reads back as";
            }
            if (head.policy == WidthPolicy::FRACTIONS && col < head.fractions.size()) {
                column.Share = static_cast<float>(head.fractions[col]);
                if (static_cast<double>(column.Share) != head.fractions[col])
                    why = "a share that no float carries";
            }
        }
        if (head.policy != WidthPolicy::APPORTIONED &&
            (head.policy == WidthPolicy::WIDTHS ? head.widths.size() : head.fractions.size()) !=
                block.columns)
            why = "a policy that does not state every column";
    }
    if (why != nullptr) {
        ++tally.n[std::string("SKIPPED cases: ") + why];
        tally.n["SKIPPED layouts, with their cases"] += layouts_here;
        std::printf("  SKIPPED case %s (%d layouts): %s\n", c.name.c_str(), layouts_here, why);
        return;
    }
    ++tally.n["cases described through the public header"];

    ImTypesetStyle style;
    style.PixelsPerInch = static_cast<float>(ppi);
    style.EmSize = px_float(font.size, ppi);
    style.Spacing =
        spacing == Spacing::FRENCH
            ? ImTypesetSpacing_French
            : (spacing == Spacing::NONFRENCH ? ImTypesetSpacing_TeX : ImTypesetSpacing_Sentence);
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    REQUIRE(document != nullptr);
    struct Owner {
        ImTypesetDocument* d;
        ~Owner() { ImTypeset::DestroyDocument(d); }
    } owner{document};
    document->reference_rules = true;
    ImTypesetDocumentOptions options;
    table_options(options, head.style.cell_padding, head.style.rule_thickness, ppi);
    CHECK(ImTypeset::SetDocumentOptions(document, &options));
    const ImTypesetDocumentContent view = content.view();
    CHECK(ImTypeset::SetDocumentBlocks(document, &view));
    {
        const ImTypesetErrorInfo* error = ImTypeset::GetDocumentBlockError(document, 0);
        if (error != nullptr && error->Code != ImTypesetError_None)
            FAIL("the table is described with an error: %s", error->Message);
    }
    const ResolvedStyle resolved = resolve_style(*stage.ctx, style);
    Document& inner = block_document(*document);
    const size_t columns = block.columns;

    // --- EXPECTED, whatever the measure: `limits`.
    std::vector<std::vector<Scaled>> natural(block.rows());
    std::vector<Scaled> widest;
    std::vector<Scaled> floors;
    reader.take("limits");
    if (block.rows() != 0) {
        for (size_t r = 0; r < block.rows(); ++r) {
            golden::Cursor record(reader.take("natural"));
            record.u32();
            const uint32_t count = record.u32();
            for (uint32_t k = 0; k < count; ++k)
                natural[r].push_back(record.i64());
            record.expect_done();
        }
        golden::Cursor wide(reader.take("widest"));
        for (uint32_t k = wide.u32(); k > 0; --k)
            widest.push_back(wide.i64());
        skip_past(reader, "floor");
        const golden::Record& floor = file.records()[reader.position() - 1];
        for (uint32_t k = 0; k < floor.u32(0); ++k)
            floors.push_back(floor.i64(1 + k));
    }
    reader.take("endlimits");

    while (!reader.done() && reader.next_is("layout")) {
        const golden::Record& open = reader.take("layout");
        const std::string run = open.str(0);
        const Scaled width = open.i64(1);
        CHECK_CONTEXT("layout %s (line %d)", run.c_str(), open.line);
        // --- The golden's records of this layout.
        std::vector<std::string> solve_lines;
        if (reader.next_is("solve")) {
            const size_t first = reader.position();
            skip_past(reader, "endsolve");
            solve_lines = reader.lines(first, reader.position());
        }
        struct RowIn {
            int line = 0;
            bool wrapped = false;
            std::string strutted;
            bool has_resumed = false;
            Scaled resumed = 0;
        };
        std::vector<RowIn> rows;
        bool headrule = false;
        Scaled bottom = 0;
        std::string vbox;
        std::vector<std::string> cmds;
        while (!reader.done() && !reader.next_is("endlayout")) {
            if (reader.next_is("cmds")) {
                const size_t first = reader.position();
                core_golden::read_cmds(reader, context);
                cmds = reader.lines(first, reader.position());
                continue;
            }
            const size_t at = reader.position();
            const golden::Record& r = reader.take();
            if (r.is("row")) {
                RowIn row;
                row.line = r.line;
                row.wrapped = r.raw(1) == "wrapped";
                if (!row.wrapped && r.raw(1) != "fits")
                    check::fail(r.file, r.line, "a row that neither fits nor wrapped");
                while (!reader.done() && !reader.next_is("endrow")) {
                    const size_t inside = reader.position();
                    const golden::Record& q = reader.take();
                    if (q.is("strutted")) {
                        row.strutted = reader.line_text(inside);
                    } else if (q.is("resumed")) {
                        row.has_resumed = true;
                        row.resumed = q.i64(0);
                    }
                }
                reader.take("endrow");
                rows.push_back(row);
            } else if (r.is("headrule")) {
                headrule = true;
            } else if (r.is("extents")) {
                CHECK_EQ(r.i64(0), int64_t{1});
                CHECK_EQ(r.i64(1), int64_t{0});
                bottom = r.i64(2);
            } else if (r.is("vbox")) {
                vbox = reader.line_text(at);
            }
        }
        reader.take("endlayout");

        // --- The product's layout at this width.
        const float width_px = exact_px(width, ppi);
        if (width_px < 0.0f) {
            ++tally.n["SKIPPED layouts: a width in scaled points that no float of pixels reads "
                      "back as"];
            std::printf("  SKIPPED layout %s of %s: %lld sp is no float of pixels\n", run.c_str(),
                        c.name.c_str(), static_cast<long long>(width));
            continue;
        }
        ++tally.n["layouts laid out through LayoutDocument"];
        CHECK(ImTypeset::LayoutDocument(document, width_px, &style));
        if (!ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e9f))
            FAIL("LayoutDocumentWindow: %s", ImTypeset::GetLastError()->Message);
        if (!CHECK(inner.settled()))
            continue;
        CHECK_EQ(resolve_width(width_px, resolved), width);
        CHECK_EQ(document->width, width);

        // The description, held to the golden's lengths.
        document_reference::hold_block(*document, 0, block.in, false, theme, tally.lengths);
        hold_table(*document, 0, block, head.style.cell_padding, head.style.rule_thickness,
                   tally.lengths);
        const TableCells& cells = inner.block(0).table;
        CHECK(cells.policy == head.policy);
        if (head.policy == WidthPolicy::WIDTHS) {
            tally.lengths += static_cast<int>(head.widths.size());
            CHECK_EQ(tables_golden::counted("widths", cells.widths),
                     tables_golden::counted("widths", head.widths));
        } else if (head.policy == WidthPolicy::FRACTIONS) {
            if (CHECK_EQ(cells.fractions.size(), head.fractions.size())) {
                for (size_t k = 0; k < head.fractions.size(); ++k)
                    CHECK_EQ(cells.fractions[k], head.fractions[k]);
            }
        }

        const BlockLayout& layout = inner.layout(0);
        const TableLayout& table = layout.table;

        // `limits`: the cells' natural widths and the columns' widest.
        {
            bool same = CHECK_EQ(cells.natural.size(), block.cells.size());
            for (size_t r = 0; same && r < block.rows(); ++r) {
                std::vector<Scaled> got(
                    cells.natural.begin() + static_cast<std::ptrdiff_t>(block.row_first[r]),
                    cells.natural.begin() + static_cast<std::ptrdiff_t>(block.row_first[r + 1]));
                same = CHECK_EQ(tables_golden::counted("natural", got),
                                tables_golden::counted("natural", natural[r])) &&
                       same;
            }
            if (block.rows() != 0)
                same = CHECK_EQ(tables_golden::counted("widest", table.widest),
                                tables_golden::counted("widest", widest)) &&
                       same;
            if (same)
                ++tally.n["limits: natural and widest equal to the reference's"];
        }

        // `solve`: the regime, every intermediate the regime has, the columns.
        if (block.rows() != 0) {
            CHECK(table.solve.refused == PolicyRefusal::NONE);
            CHECK(!table.solve.overflowed);
            if (CHECK_LINES_EQ(solve_lines,
                               tables_golden::written(
                                   table.solve, table.widest.data(),
                                   table.narrowest.empty() ? nullptr : table.narrowest.data(),
                                   columns),
                               "the solve, from the document's table layout")) {
                ++tally.n["solve: equal to the reference's, record for record"];
                ++tally.n[std::string("solve: regime ") + tables_golden::token(table.solve.regime)];
            }
        } else {
            CHECK(solve_lines.empty());
            ++tally.n["solve: a table without rows has none"];
        }

        // The rows: fits or wrapped, the strutted box, what the list resumes from.
        if (CHECK_EQ(table.rows.size(), rows.size())) {
            for (size_t r = 0; r < rows.size(); ++r) {
                CHECK_CONTEXT("row %zu (line %d)", r, rows[r].line);
                const LineDims& dims = table.rows[r].dims;
                ++tally.n["rows: compared"];
                const bool same =
                    CHECK(table.rows[r].wrapped == rows[r].wrapped) &&
                    CHECK_EQ(check::format("strutted %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64,
                                           dims.width, dims.height, dims.depth, dims.shift),
                             rows[r].strutted) &&
                    CHECK(rows[r].has_resumed == rows[r].wrapped) &&
                    (!rows[r].has_resumed || CHECK_EQ(dims.resumed, rows[r].resumed));
                if (same)
                    ++tally.n[rows[r].wrapped
                                  ? "rows: wrapped, strutted and resumed equal to the reference's"
                                  : "rows: fits, strutted equal to the reference's"];
            }
        }
        CHECK_EQ(headrule, block.has_header && head.style.rule_thickness != 0 && block.rows() != 0);

        // `extents`, `vbox`, `cmds`: the document is this one table.
        if (CHECK_EQ(layout.extent, bottom) && CHECK_EQ(inner.height(), bottom))
            ++tally.n["extents: the table's height equal to the reference's"];
        {
            Scaled box_width = 0;
            Scaled box_height = 0;
            Scaled box_depth = 0;
            if (std::sscanf(vbox.c_str(), "vbox %" SCNd64 " %" SCNd64 " %" SCNd64, &box_width,
                            &box_height, &box_depth) != 3)
                FAIL("no `vbox` record");
            CHECK_EQ(box_height + box_depth, bottom);
            if (CHECK_EQ(layout.box_width, box_width))
                ++tally.n["vbox: the table's width equal to the reference's"];
        }
        std::vector<DrawCmd> flat;
        if (CHECK(inner.flatten(flat)) &&
            CHECK_LINES_EQ(cmds, blocks_golden::written(context, flat),
                           "the table, described through the public header and flattened")) {
            ++tally.n["cmds: layouts whose commands are the reference's, exactly"];
            tally.n["cmds: commands compared"] += static_cast<int>(flat.size());
        }

        // `floor`: the longest unbreakable piece of each column, with padding.
        if (block.rows() != 0) {
            std::vector<Scaled> got;
            for (size_t col = 0; col < columns; ++col)
                got.push_back(inner.table_floor(block_composer(*document), 0, col));
            if (CHECK_EQ(tables_golden::counted("floor", got),
                         tables_golden::counted("floor", floors)))
                ++tally.n["limits: floor equal to the reference's"];
        }

        // The public geometry is these numbers, as the float each comes to.
        const ImTypesetTableGeometry geometry = ImTypeset::GetDocumentTableGeometry(document, 0);
        bool same = CHECK(geometry.LaidOut) &&
                    CHECK_EQ(geometry.RowCount, static_cast<int>(block.rows())) &&
                    CHECK_EQ(geometry.ColumnCount, static_cast<int>(columns)) &&
                    CHECK_EQ(geometry.CellCount, static_cast<int>(block.cells.size())) &&
                    CHECK_EQ(geometry.Min.x, px_float(block.in.left_indent, ppi)) &&
                    CHECK_EQ(geometry.Min.y, 0.0f);
        Scaled left = block.in.left_indent;
        for (size_t col = 0; col < columns && col < table.solve.columns.size(); ++col) {
            const ImTypesetTableColumnGeometry column =
                ImTypeset::GetDocumentTableColumn(document, 0, static_cast<int>(col));
            same = CHECK_EQ(column.Min, px_float(left, ppi)) && same;
            left += table.solve.columns[col];
            same = CHECK_EQ(column.Max, px_float(left, ppi)) && same;
            same = CHECK_EQ(column.Natural, px_float(widest[col], ppi)) && same;
            same = CHECK_EQ(column.Floor, px_float(floors[col], ppi)) && same;
        }
        if (block.rows() != 0)
            same = CHECK_EQ(geometry.Max.x, px_float(left, ppi)) && same;
        for (size_t r = 0; r < table.rows.size() && r + 1 < table.row_top.size(); ++r) {
            const ImTypesetTableRowGeometry row =
                ImTypeset::GetDocumentTableRow(document, 0, static_cast<int>(r));
            const LineDims& dims = table.rows[r].dims;
            same = CHECK_EQ(row.Min, px_float(table.row_top[r], ppi)) && same;
            same = CHECK_EQ(row.Max, px_float(table.row_top[r] + dims.height + dims.depth, ppi)) &&
                   same;
            same = CHECK(row.Wrapped == table.rows[r].wrapped) && same;
            same = CHECK_EQ(row.CellCount,
                            static_cast<int>(block.row_first[r + 1] - block.row_first[r])) &&
                   same;
        }
        if (same)
            ++tally.n["geometry: the public table, columns and rows are the layout's, as floats"];
    }
    CHECK(reader.done());
}

} // namespace

TEST(a_table_alone_is_set_as_the_reference_sets_it_through_the_public_calls)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    REQUIRE(!golden::list("tables").empty());
    int cases = 0;
    for (const char* name : {"widths", "shapes", "demo"}) {
        golden::File file;
        REQUIRE(file.load(golden::path(std::string("tables/") + name + ".golden")));
        AloneTally tally;
        for (const core_golden::Case& c : core_golden::cases(file)) {
            CHECK_CONTEXT("tables/%s.golden case %s (line %d)", name, c.name.c_str(), c.line);
            if (!CHECK(c.kind == "table"))
                continue;
            replay_alone(stage, file, c, tally);
        }
        std::printf("tables/%s.golden through the public Document calls:\n", name);
        for (const auto& entry : tally.n)
            std::printf("  %6d  %s\n", entry.second, entry.first.c_str());
        std::printf("  %d lengths of the description held to the reference's scaled points by "
                    "==\n",
                    tally.lengths);
        const auto of = [&](const char* what) {
            const auto found = tally.n.find(what);
            return found == tally.n.end() ? 0 : found->second;
        };
        cases += of("cases");
        // Every layout is accounted for: laid out, or skipped under a reason.
        CHECK_EQ(of("layouts"),
                 of("layouts laid out through LayoutDocument") +
                     of("SKIPPED layouts, with their cases") +
                     of("SKIPPED layouts: a width in scaled points that no float of pixels reads "
                        "back as"));
        CHECK_EQ(of("cmds: layouts whose commands are the reference's, exactly"),
                 of("layouts laid out through LayoutDocument"));
    }
    // tests/test_align.cpp replays the same 36 cases from their node lists.
    CHECK_EQ(cases, 36);
}

// --- doc-tables/stream.golden ------------------------------------------------------------
//
// A table that grows (FORMAT-doc-tables.md §5). Each step's blocks are handed
// to the document by the smallest edit that makes them: AppendDocumentTableCells
// when the last block is a table that only gained whole cells,
// AppendDocumentText when it is text that only grew, ReplaceDocumentBlocks
// otherwise. A table the reference has LIVE is described with
// ImTypesetBlockFlags_TableHeld; committed under `keep_streamed` it keeps the
// flag, under `reset` it is described without it.
//
// A stream case has no `blocks` group: a `cell` says its runs' fonts, and
// nothing says the table's leading and spaces or a paragraph's. They are the
// reference's Theme at the case's `themein`, and are taken from the first
// case of selection.golden, which has the same `themein` (asserted): its
// paragraph's style, its table's, its padding and rule. Every step's height
// and the final commands hold that borrowing to the reference, exactly, in
// the cases whose other blocks are plain paragraphs. Where they are not (a
// heading has a size no row here says) only the tables are compared.
//
// NOT COMPARED: `npieces`, `unit`, `built` (the reference's view lays a long
// table out in pieces of four rows: its own bookkeeping) and the digests.
// `moved`, `nwrapped`, `mode` and `totals` are the dump script's arithmetic:
// they are recomputed here from the reference's own rows and a disagreement
// is counted as a finding about the golden, not failed.

namespace
{

struct StreamBlock {
    std::string sort = "text";
    std::string kind;
    std::string align = "left";
    std::string text;
    // TABLE
    bool is_table = false;
    bool live = false;
    uint32_t columns = 0;
    uint32_t rows = 0;
    bool has_header = false;
    Scaled left_indent = 0;
    std::vector<ColumnAlign> aligns;
    std::map<std::pair<int, int>, CellIn> cells;
};

// What a step's records say of one table.
struct TableStep {
    int line = 0;
    std::string state;
    std::string mode;
    int64_t rows = 0;
    int64_t moved = 0;
    int64_t wrapped = 0;
    std::vector<Scaled> natural;
    std::vector<Scaled> stated;
    std::vector<Scaled> set;
    // The rows a `cell` group of this step is in.
    std::vector<int> changed_rows;
};

std::string cell_signature(const CellIn& cell)
{
    std::string out = check::format("%d.%d:", cell.row, cell.column) + cell.content + "|";
    for (const Piece& piece : cell.pieces)
        out += check::format("%d,%d,%d,%u,%lld,%d;", static_cast<int>(piece.kind), piece.start,
                             piece.end, static_cast<unsigned>(piece.font.face),
                             static_cast<long long>(piece.font.size), piece.breakable ? 1 : 0);
    for (const Link& link : cell.links)
        out += check::format("L%d,%d;", link.start, link.end);
    return out;
}

// A step's table as the `blocks` group of a `tdoc` case would have it.
// False if a cell below the counts has no `cell` group.
bool table_of(const StreamBlock& from, const blocks_golden::BlockIn& style, DocBlockIn& out)
{
    out = DocBlockIn();
    out.in = style;
    out.in.type = BlockType::TABLE;
    out.in.left_indent = from.left_indent;
    out.is_table = true;
    out.columns = from.columns;
    out.has_header = from.has_header;
    out.aligns = from.aligns;
    out.row_first.push_back(0);
    for (uint32_t r = 0; r < from.rows; ++r) {
        for (uint32_t c = 0; c < from.columns; ++c) {
            const auto found = from.cells.find({static_cast<int>(r), static_cast<int>(c)});
            if (found == from.cells.end())
                return false;
            out.cells.push_back(found->second);
        }
        out.row_first.push_back(out.cells.size());
    }
    join_cells(out);
    return true;
}

// Everything the description of a block depends on, to tell an edit by.
struct Described {
    std::string head;
    std::string text;
    std::vector<std::string> cells;
    bool is_table = false;

    bool operator==(const Described& other) const
    {
        return head == other.head && text == other.text && cells == other.cells;
    }
};

bool held_flag(const StreamBlock& block, bool keep)
{
    return block.live || keep;
}

Described described(const StreamBlock& block, const DocBlockIn* table, bool keep)
{
    Described out;
    out.is_table = block.is_table;
    if (!block.is_table) {
        out.head = block.sort + "|" + block.align;
        out.text = block.text;
        return out;
    }
    out.head = check::format("table|%d|%u|%d|%lld|", held_flag(block, keep) ? 1 : 0, block.columns,
                             block.has_header ? 1 : 0, static_cast<long long>(block.left_indent));
    for (const ColumnAlign align : block.aligns)
        out.head += check::format("%d,", static_cast<int>(align));
    if (table != nullptr) {
        out.text = table->content;
        for (const CellIn& cell : table->cells)
            out.cells.push_back(cell_signature(cell));
    }
    return out;
}

int align_style(const std::string& align)
{
    return align == "left" ? 0 : align == "center" ? 1 : align == "right" ? 2 : 3;
}

// The index of the table's style among the document's: after the four
// paragraph styles, one per alignment.
constexpr int TABLE_STYLE = 4;

// Adds a step's block to `content`. Returns why it cannot be said, or null.
const char* add_stream_block(Content& content, const StreamBlock& block, const DocBlockIn* table,
                             bool keep, double ppi)
{
    if (block.is_table)
        return table == nullptr
                   ? "a cell of a stream's table with no `cell` group"
                   : describe_table(*table, content, TABLE_STYLE, ppi,
                                    held_flag(block, keep) ? ImTypesetBlockFlags_TableHeld : 0);
    if (block.sort == "code")
        content.add(ImTypesetBlockKind_Code, block.text, align_style(block.align));
    else if (block.sort == "rule")
        content.add(ImTypesetBlockKind_Rule, std::string(), align_style(block.align));
    else
        content.add(ImTypesetBlockKind_Paragraph, block.text, align_style(block.align));
    return nullptr;
}

std::string widths_text(const std::vector<Scaled>& widths)
{
    return tables_golden::counted("widths", widths);
}

void replay_stream(const golden::File& file, const core_golden::Case& c, const BlocksIn& theme_case,
                   double themein, Tally& tally)
{
    core_golden::Context context;
    core_golden::Reader reader(file, c);
    const double ppi = theme_case.theme.px_per_inch;
    bool keep = true;
    float width_px = 0.0f;
    while (!reader.done() && !reader.next_is("expect")) {
        const golden::Record& r = reader.take();
        if (r.is("themein")) {
            // The borrowed lengths are this Theme's.
            CHECK_EQ(r.f64(0), themein);
        } else if (r.is("policy")) {
            keep = r.raw(0) == "keep_streamed";
            CHECK(keep || r.raw(0) == "reset");
        } else if (r.is("width")) {
            width_px = static_cast<float>(r.f64(1));
        }
    }
    reader.take("expect");
    ++tally.n["cases"];

    // The paragraph and the table of the borrowed Theme.
    const blocks_golden::BlockIn* paragraph = nullptr;
    const blocks_golden::BlockIn* table_style = nullptr;
    for (const DocBlockIn& block : theme_case.blocks) {
        if (block.is_table && table_style == nullptr)
            table_style = &block.in;
        if (block.in.type == BlockType::TEXT && block.in.kind == "paragraph" &&
            paragraph == nullptr)
            paragraph = &block.in;
    }
    REQUIRE(paragraph != nullptr && table_style != nullptr);
    const FontSpec body = blocks_golden::spec_of(paragraph->font);

    ImTypesetStyle style;
    style.PixelsPerInch = static_cast<float>(ppi);
    style.EmSize = px_float(body.size, ppi);
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    REQUIRE(document != nullptr);
    struct Owner {
        ImTypesetDocument* d;
        ~Owner() { ImTypeset::DestroyDocument(d); }
    } owner{document};
    REQUIRE(document_reference::use_reference(document, theme_case.theme) == nullptr);
    {
        ImTypesetDocumentOptions options;
        options.QuoteIndent = px_float(theme_case.theme.theme.quote_indent, ppi);
        options.QuoteBarThickness = px_float(theme_case.theme.theme.quote_rule_thickness, ppi);
        table_options(options, theme_case.cell_padding, theme_case.table_rule, ppi);
        CHECK(ImTypeset::SetDocumentOptions(document, &options));
        Content styles;
        for (const Align align : {Align::LEFT, Align::CENTER, Align::RIGHT, Align::JUSTIFY}) {
            blocks_golden::BlockIn in = *paragraph;
            in.align = align;
            styles.styles.push_back(style_of(in, theme_case.theme, body.size));
        }
        styles.styles.push_back(style_of(*table_style, theme_case.theme, body.size));
        const ImTypesetDocumentContent view = styles.view();
        CHECK(ImTypeset::SetDocumentBlocks(document, &view));
    }
    Document& inner = block_document(*document);

    std::vector<StreamBlock> now;
    std::vector<Described> said;
    std::vector<DocBlockIn> tables_now;
    // Per block: the widths the reference set it to a step earlier, and
    // whether the product had laid it out as a table.
    std::map<int, std::vector<Scaled>> set_before;
    std::map<int, bool> laid_before;
    bool plain = true;
    bool comparable = true;
    int64_t moved_total = 0;
    int64_t steps = 0;
    uint64_t rows_set_total = 0;
    uint64_t rows_times_steps = 0;
    uint64_t cells_measured_total = 0;
    int by_append = 0;
    int by_replace = 0;

    while (!reader.done() && reader.next_is("step")) {
        const golden::Record& open = reader.take("step");
        CHECK_CONTEXT("step %lld (line %d)", static_cast<long long>(open.i64(0)), open.line);
        ++steps;
        ++tally.n["steps"];
        const size_t count = static_cast<size_t>(open.i64(3) + open.i64(4));
        const Scaled height = open.i64(6);
        const int64_t length = open.i64(7);
        std::vector<StreamBlock> next = now;
        next.resize(count);
        std::map<int, TableStep> expected;
        while (!reader.done() && !reader.next_is("endstep")) {
            if (reader.next_is("cell")) {
                // A `cell` group follows the `stable` row of its block.
                const int block = static_cast<int>(file.records()[reader.position()].i64(0));
                CellIn cell = read_cell(reader);
                if (!CHECK(block >= 0 && static_cast<size_t>(block) < next.size()))
                    continue;
                expected[block].changed_rows.push_back(cell.row);
                next[static_cast<size_t>(block)].cells[{cell.row, cell.column}] = std::move(cell);
                continue;
            }
            const golden::Record& r = reader.take();
            const size_t b = r.size() > 0 && (r.is("sblock") || r.is("stable") || r.is("ttable") ||
                                              r.is("tnatural") || r.is("tstated") || r.is("tset"))
                                 ? static_cast<size_t>(r.i64(0))
                                 : 0;
            if (r.is("sblock") || r.is("stable") || r.is("ttable")) {
                if (!CHECK(b < next.size()))
                    continue;
            }
            if (r.is("sblock")) {
                StreamBlock block;
                block.sort = std::string(r.raw(2));
                block.kind = std::string(r.raw(3));
                block.align = std::string(r.raw(4));
                block.text = r.str(6);
                if (block.sort == "table")
                    check::fail(r.file, r.line, "a table as an `sblock` row");
                next[b] = block;
            } else if (r.is("stable")) {
                StreamBlock& block = next[b];
                if (!block.is_table)
                    block = StreamBlock();
                block.sort = "table";
                block.is_table = true;
                block.live = r.raw(1) == "live";
                block.columns = r.u32(2);
                block.rows = r.u32(3);
                block.has_header = r.boolean(4);
                block.left_indent = r.i64(5);
                block.aligns.clear();
                for (uint32_t k = 0; k < r.u32(6); ++k)
                    block.aligns.push_back(static_cast<ColumnAlign>(core_golden::token_index(
                        r.raw(7 + k), tables_golden::COLUMN_ALIGN_TOKENS, "ColumnAlign")));
            } else if (r.is("ttable")) {
                TableStep& t = expected[static_cast<int>(b)];
                t.line = r.line;
                t.state = std::string(r.raw(1));
                t.rows = r.i64(2);
                t.mode = std::string(r.raw(4));
                t.moved = r.i64(5);
                t.wrapped = r.i64(6);
                next[b].live = t.state == "live";
            } else if (r.is("tnatural") || r.is("tstated") || r.is("tset")) {
                TableStep& t = expected[static_cast<int>(b)];
                std::vector<Scaled>& to = r.is("tnatural")  ? t.natural
                                          : r.is("tstated") ? t.stated
                                                            : t.set;
                for (uint32_t k = 0; k < r.u32(1); ++k)
                    to.push_back(r.i64(2 + k));
            }
        }
        reader.take("endstep");

        // --- The step's blocks, as described.
        std::vector<DocBlockIn> tables_next(next.size());
        std::vector<Described> saying(next.size());
        for (size_t b = 0; b < next.size(); ++b) {
            const StreamBlock& block = next[b];
            bool have = true;
            if (block.is_table)
                have = CHECK(table_of(block, *table_style, tables_next[b]));
            else
                plain = plain && block.sort == "text" && block.kind == "paragraph";
            saying[b] = described(block, block.is_table && have ? &tables_next[b] : nullptr, keep);
        }

        // --- The smallest edit from what is said to what is to be.
        size_t lo = 0;
        while (lo < said.size() && lo < saying.size() && said[lo] == saying[lo])
            ++lo;
        size_t old_end = said.size();
        size_t new_end = saying.size();
        while (old_end > lo && new_end > lo && said[old_end - 1] == saying[new_end - 1]) {
            --old_end;
            --new_end;
        }
        const ImTypesetDocumentStats before = ImTypeset::GetDocumentStats(document);
        bool replaced_table = false;
        if (!comparable) {
            // Nothing more is handed over once a step could not be said.
        } else if (lo == old_end && lo == new_end) {
            ++tally.n["edits: none (the step changes nothing a description says)"];
        } else {
            bool appended = false;
            const bool last_alone = old_end == said.size() && new_end == saying.size() &&
                                    old_end - lo == 1 && new_end - lo == 1;
            if (last_alone && said[lo].is_table && saying[lo].is_table &&
                said[lo].head == saying[lo].head &&
                saying[lo].cells.size() > said[lo].cells.size() &&
                std::equal(said[lo].cells.begin(), said[lo].cells.end(),
                           saying[lo].cells.begin()) &&
                saying[lo].text.compare(0, said[lo].text.size(), said[lo].text) == 0) {
                // Whole cells more, and nothing else: the cells after the ones
                // the table has, with the text they are stretches of.
                Content scratch;
                const char* why = add_stream_block(scratch, next[lo], &tables_next[lo], keep, ppi);
                if (why == nullptr) {
                    const size_t had = said[lo].cells.size();
                    const int from = static_cast<int>(said[lo].text.size());
                    std::vector<ImTypesetRun> runs;
                    for (const ImTypesetRun& run : scratch.runs) {
                        if (run.Start >= from)
                            runs.push_back(run);
                    }
                    appended = ImTypeset::AppendDocumentTableCells(
                        document, scratch.text.data() + from,
                        scratch.text.data() + scratch.text.size(), scratch.cells.data() + had,
                        static_cast<int>(scratch.cells.size() - had), runs.data(),
                        static_cast<int>(runs.size()));
                    if (!appended)
                        FAIL("AppendDocumentTableCells: %s", ImTypeset::GetLastError()->Message);
                    else
                        ++tally.n["edits: AppendDocumentTableCells"];
                }
            } else if (last_alone && !said[lo].is_table && !saying[lo].is_table &&
                       said[lo].head == saying[lo].head && next[lo].sort == "text" &&
                       saying[lo].text.size() > said[lo].text.size() &&
                       saying[lo].text.compare(0, said[lo].text.size(), said[lo].text) == 0) {
                const std::string& text = saying[lo].text;
                appended = ImTypeset::AppendDocumentText(
                    document, text.data() + said[lo].text.size(), text.data() + text.size());
                if (appended)
                    ++tally.n["edits: AppendDocumentText"];
            }
            if (appended) {
                ++by_append;
            } else {
                Content content;
                const char* why = nullptr;
                for (size_t b = lo; b < new_end && why == nullptr; ++b) {
                    why = add_stream_block(content, next[b], &tables_next[b], keep, ppi);
                    replaced_table = replaced_table || next[b].is_table;
                }
                if (why != nullptr) {
                    comparable = false;
                    ++tally.n[std::string("SKIPPED cases from a step on, not expressible: ") + why];
                    std::printf("  SKIPPED case %s from step %lld on: %s\n", c.name.c_str(),
                                static_cast<long long>(open.i64(0)), why);
                } else {
                    const ImTypesetDocumentContent view = content.view();
                    CHECK(ImTypeset::ReplaceDocumentBlocks(document, static_cast<int>(lo),
                                                           static_cast<int>(old_end - lo), &view));
                    ++tally.n["edits: ReplaceDocumentBlocks"];
                    ++by_replace;
                }
            }
        }
        now.swap(next);
        tables_now.swap(tables_next);
        if (!comparable) {
            ++tally.n["SKIPPED steps, with the rest of their case"];
            continue;
        }
        said.swap(saying);
        ++tally.n["steps handed over and laid out"];
        CHECK(ImTypeset::LayoutDocument(document, width_px, &style));
        if (!ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e9f))
            FAIL("LayoutDocumentWindow: %s", ImTypeset::GetLastError()->Message);
        const ImTypesetDocumentStats after = ImTypeset::GetDocumentStats(document);
        if (CHECK_EQ(ImTypeset::GetDocumentBlockCount(document), static_cast<int>(now.size())))
            ++tally.n["steps: block count equal to the reference's"];
        if (!CHECK(inner.settled()))
            continue;
        if (plain) {
            ++tally.n["steps: height compared (every other block is a plain paragraph)"];
            std::vector<DrawCmd> flat;
            std::vector<BlockSpan> spans;
            CHECK(inner.flatten(flat, &spans));
            if (CHECK_EQ(inner.height(), height) &&
                CHECK_EQ(spans.empty() ? int64_t{0} : static_cast<int64_t>(spans.back().index_end),
                         length))
                ++tally.n["steps: height and index count equal to the reference's"];
        } else {
            ++tally.n["steps: height NOT COMPARED (a heading or another block no row describes)"];
        }

        // --- The tables of the step.
        bool settled_step = true;
        int rows_changed = 0;
        for (auto& entry : expected) {
            TableStep& t = entry.second;
            const size_t b = static_cast<size_t>(entry.first);
            if (t.line == 0)
                continue;
            CHECK_CONTEXT("table %zu (line %d)", b, t.line);
            if (!CHECK(b < now.size()) || !CHECK(now[b].is_table))
                continue;
            ++tally.n["tables of steps: compared"];
            const StreamBlock& block = now[b];
            const TableLayout& table = inner.layout(b).table;
            document_reference::hold_block(*document, b, tables_now[b].in, false, theme_case.theme,
                                           tally.lengths);
            hold_table(*document, b, tables_now[b], theme_case.cell_padding, theme_case.table_rule,
                       tally.lengths);
            CHECK_EQ(static_cast<int64_t>(table.rows.size()), t.rows);
            if (CHECK_EQ(widths_text(table.widest), widths_text(t.natural)))
                ++tally.n["tables of steps: `tnatural` equal to the reference's"];
            if (CHECK_EQ(widths_text(table.solve.columns), widths_text(t.set)) &&
                CHECK_EQ(widths_text(table.columns), widths_text(t.set)))
                ++tally.n["tables of steps: `tset` equal to the reference's"];
            // The reference's private state: what its view holds the columns
            // at. The product's is the widest each column has wanted.
            const bool flagged = held_flag(block, keep);
            if (flagged) {
                ++tally.n["tables of steps: `tstated` compared with the widths the product holds"];
                if (CHECK_EQ(widths_text(document->blocks[b].held), widths_text(t.stated)))
                    ++tally.n["tables of steps: `tstated` equal to the widths the product holds"];
            } else {
                CHECK(t.stated.empty());
            }
            // The script's `mode`, against how the block is described.
            const char* mode = block.live ? "held" : (keep ? "kept" : "plain");
            ++tally.n[t.mode == mode ? "script: `mode` is how the block is described"
                                     : "script: `mode` is NOT how the block is described (a "
                                       "finding about the golden)"];
            int64_t wrapped = 0;
            for (const TableRowLayout& row : table.rows)
                wrapped += row.wrapped ? 1 : 0;
            ++tally.n[wrapped == t.wrapped ? "script: `nwrapped` is the product's wrapped rows"
                                           : "script: `nwrapped` is NOT the product's wrapped rows "
                                             "(counted, not failed)"];
            // The script's `moved`, from the reference's own consecutive `tset`s.
            const auto earlier = set_before.find(entry.first);
            int64_t moved = -1;
            if (earlier != set_before.end() && earlier->second.size() == t.set.size()) {
                moved = 0;
                for (size_t k = 0; k < t.set.size(); ++k)
                    moved += earlier->second[k] != t.set[k] ? 1 : 0;
            }
            ++tally.n[moved == t.moved ? "script: `moved` is what consecutive `tset` rows give"
                                       : "script: `moved` is NOT what consecutive `tset` rows give "
                                         "(a finding about the golden)"];
            if (moved > 0)
                moved_total += moved;
            settled_step = settled_step && moved == 0 && laid_before[entry.first];
            std::sort(t.changed_rows.begin(), t.changed_rows.end());
            t.changed_rows.erase(std::unique(t.changed_rows.begin(), t.changed_rows.end()),
                                 t.changed_rows.end());
            rows_changed += static_cast<int>(t.changed_rows.size());
            rows_times_steps += static_cast<uint64_t>(t.rows);
            set_before[entry.first] = t.set;
            laid_before[entry.first] = true;
        }
        for (auto it = set_before.begin(); it != set_before.end();) {
            const bool gone = static_cast<size_t>(it->first) >= now.size() ||
                              !now[static_cast<size_t>(it->first)].is_table;
            if (gone) {
                laid_before.erase(it->first);
                it = set_before.erase(it);
            } else {
                ++it;
            }
        }

        // --- The work: rows set by this step.
        const uint64_t rows_set = after.TableRowsSet - before.TableRowsSet;
        rows_set_total += rows_set;
        cells_measured_total += after.TableCellsMeasured - before.TableCellsMeasured;
        if (expected.empty() || !settled_step)
            continue;
        if (replaced_table) {
            // The table was described again: every row of it is new to the
            // product, whatever the reference kept.
            ++tally.n["work: steps with no column moved whose table was REPLACED (rows set are "
                      "not held to the rows changed)"];
            tally.n["work:   ...rows set by those steps"] += static_cast<int>(rows_set);
            tally.n["work:   ...rows changed in those steps"] += rows_changed;
            continue;
        }
        ++tally.n["work: steps with no column moved and the table already laid out, not replaced"];
        if (rows_set <= static_cast<uint64_t>(rows_changed))
            ++tally.n["work: ...in which no more rows were set than had cells that changed"];
        else
            FAIL("no column moved and %d row(s) changed; the step set %llu rows", rows_changed,
                 static_cast<unsigned long long>(rows_set));
    }

    const golden::Record& final_row = reader.take("final");
    const golden::Record& totals = reader.take("totals");
    CHECK_EQ(totals.i64(0), steps);
    ++tally.n[totals.i64(2) == moved_total
                  ? "script: `totals` moved is the sum of the recomputed `moved`"
                  : "script: `totals` moved is NOT the sum of the recomputed `moved` (a finding "
                    "about the golden)"];
    std::vector<std::string> cmds;
    if (reader.next_is("cmds")) {
        const size_t first = reader.position();
        core_golden::read_cmds(reader, context);
        cmds = reader.lines(first, reader.position());
    }
    std::printf("  %s: %lld steps, %d by an append call, %d by ReplaceDocumentBlocks; rows set "
                "over the feed %llu, rows x steps %llu; cells measured %llu%s\n",
                c.name.c_str(), static_cast<long long>(steps), by_append, by_replace,
                static_cast<unsigned long long>(rows_set_total),
                static_cast<unsigned long long>(rows_times_steps),
                static_cast<unsigned long long>(cells_measured_total),
                comparable ? "" : " (cut short)");
    if (!comparable)
        return;
    if (CHECK_EQ(static_cast<int64_t>(ImTypeset::GetDocumentBlockCount(document)),
                 final_row.i64(0)))
        ++tally.n["final: block count equal to the reference's"];
    if (!plain) {
        ++tally.n["final: height and commands NOT COMPARED (a heading or another block no row "
                  "describes)"];
        return;
    }
    std::vector<DrawCmd> flat;
    std::vector<BlockSpan> spans;
    CHECK(inner.flatten(flat, &spans));
    if (CHECK_EQ(inner.height(), final_row.i64(1)) &&
        CHECK_EQ(spans.empty() ? int64_t{0} : static_cast<int64_t>(spans.back().index_end),
                 final_row.i64(2)))
        ++tally.n["final: height and index count equal to the reference's"];
    if (CHECK(!cmds.empty()) && CHECK_LINES_EQ(cmds, blocks_golden::written(context, flat),
                                               "the document at the end of the feed, flattened")) {
        ++tally.n["final: commands equal to the reference's, exactly"];
        tally.n["final: commands compared"] += static_cast<int>(flat.size());
    }
}

} // namespace

TEST(a_table_that_grows_is_set_step_by_step_as_the_reference_sets_it)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    // The Theme the stream cases are fed under: see the top of this section.
    golden::File selection;
    REQUIRE(selection.load(golden::path("doc-tables/selection.golden")));
    const std::vector<core_golden::Case> described = core_golden::cases(selection);
    REQUIRE(!described.empty());
    BlocksIn theme_case;
    double themein = 0.0;
    {
        core_golden::Reader reader(selection, described.front());
        while (!reader.done() && !reader.next_is("expect")) {
            const golden::Record& r = reader.take();
            if (r.is("themein"))
                themein = r.f64(0);
        }
        reader.take("expect");
        theme_case = read_doc_blocks(reader);
    }
    REQUIRE(themein > 0.0);

    golden::File file;
    REQUIRE(file.load(golden::path("doc-tables/stream.golden")));
    Tally tally;
    for (const core_golden::Case& c : core_golden::cases(file)) {
        CHECK_CONTEXT("doc-tables/stream.golden case %s (line %d)", c.name.c_str(), c.line);
        if (!CHECK(c.kind == "tstream"))
            continue;
        replay_stream(file, c, theme_case, themein, tally);
    }
    std::printf("doc-tables/stream.golden through the public Document calls:\n");
    for (const auto& entry : tally.n)
        std::printf("  %6d  %s\n", entry.second, entry.first.c_str());
    std::printf("  %d lengths of the description held to the reference's scaled points by ==\n",
                tally.lengths);
    CHECK_EQ(tally.of("cases"), 9);
    CHECK_EQ(tally.of("steps"), 213);
    CHECK_EQ(tally.of("tables of steps: compared"), 289);
}
