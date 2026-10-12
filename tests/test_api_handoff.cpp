// What the host that draws through this library alone asked next, each pinned
// through the public header: how tall a table with a header is and why, which
// glyph quads a draw under a clip rectangle emits, a mark set culled to what
// the clip shows, text appended to a cell that is not the table's last, and a
// role's font metrics.
//
// src/context.h is included for the tests that set the reference's rules
// beside the product's, and src/imtypeset_document.h for heights in scaled
// points, where "equal" can be held to the unit.

#include "golden.h"

#include "imtypeset.h"

#include "context.h"
#include "imtypeset_document.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace ImTypeset::detail;

namespace
{

const struct {
    ImTypesetFace face;
    const char* file;
} FONTS[] = {
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
        for (const auto& entry : FONTS) {
            const std::string path = golden::data_path(std::string("fonts/") + entry.file);
            if (ImTypeset::AddFontFromFileTTF(entry.face, path.c_str()) == nullptr)
                FAIL("%s: %s", entry.file, ImTypeset::GetLastError()->Message);
        }
        ImTypeset::GetStyle().EmSize = 16.0f;
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

// A table described from rows of cells, as a host describes one: the block's
// text is the cells joined by a tab and the rows by a newline.
struct TableText {
    std::string text;
    std::vector<ImTypesetCell> cells;

    void row(const std::vector<std::string>& given)
    {
        if (!cells.empty())
            text += '\n';
        for (size_t c = 0; c < given.size(); ++c) {
            if (c != 0)
                text += '\t';
            ImTypesetCell cell;
            cell.TextStart = static_cast<int>(text.size());
            text += given[c];
            cell.TextEnd = static_cast<int>(text.size());
            cell.Flags = c == 0 ? ImTypesetCellFlags_RowStart : 0;
            cells.push_back(cell);
        }
    }
};

ImTypesetDocument* table_document(const TableText& table, ImTypesetBlockFlags flags,
                                  float width = 684.0f, float rule = 0.0f)
{
    ImTypesetBlock block;
    block.Kind = ImTypesetBlockKind_Table;
    block.Flags = flags;
    block.TextStart = 0;
    block.TextEnd = static_cast<int>(table.text.size());
    block.CellStart = 0;
    block.CellCount = static_cast<int>(table.cells.size());
    ImTypesetDocumentContent content;
    content.Text = table.text.data();
    content.TextSize = static_cast<int>(table.text.size());
    content.Blocks = &block;
    content.BlockCount = 1;
    content.Cells = table.cells.data();
    content.CellCount = static_cast<int>(table.cells.size());
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    if (document == nullptr)
        return nullptr;
    if (rule != 0.0f) {
        ImTypesetDocumentOptions options;
        options.TableRuleThickness = rule;
        ImTypeset::SetDocumentOptions(document, &options);
    }
    ImTypeset::SetDocumentBlocks(document, &content);
    ImTypeset::LayoutDocument(document, width);
    return document;
}

// The first table of the reference's demo/tables.md, its code spans as plain
// text: a header and four rows, none of which wraps at 684 px.
TableText demo_table()
{
    TableText table;
    table.row({"Delimiter row", "The column is set"});
    table.row({"---", "flush left"});
    table.row({":---", "flush left"});
    table.row({":---:", "centred"});
    table.row({"---:", "flush right"});
    return table;
}

Scaled extent_of(ImTypesetDocument* document)
{
    ImTypeset::GetDocumentBlockGeometry(document, 0);
    return block_document(*document).layout(0).extent;
}

struct Quad {
    float x0, y0, x1, y1;
};

bool same_quad(const Quad& a, const Quad& b)
{
    return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}

// What a draw of the whole document under `clip` emits, a quad at a time.
std::vector<Quad> quads_under(ImTypesetDocument* document, const ImVec4& clip)
{
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    ImTypeset::DrawDocumentToBuffers(document, ImVec2(0.0f, 0.0f), clip, &vertices, &indices);
    std::vector<Quad> out;
    for (int v = 0; v + 3 < vertices.Size; v += 4) {
        Quad quad = {vertices[v].pos.x, vertices[v].pos.y, vertices[v].pos.x, vertices[v].pos.y};
        for (int k = 1; k < 4; ++k) {
            quad.x0 = std::min(quad.x0, vertices[v + k].pos.x);
            quad.y0 = std::min(quad.y0, vertices[v + k].pos.y);
            quad.x1 = std::max(quad.x1, vertices[v + k].pos.x);
            quad.y1 = std::max(quad.y1, vertices[v + k].pos.y);
        }
        out.push_back(quad);
    }
    return out;
}

ImTypesetDocument* code_document(const std::string& text, ImTypesetBlockFlags flags, float pitch)
{
    ImTypesetBlock block;
    block.Kind = ImTypesetBlockKind_Code;
    block.Flags = flags;
    block.TextStart = 0;
    block.TextEnd = static_cast<int>(text.size());
    ImTypesetBlockStyle style;
    style.LineHeight = pitch / 16.0f;
    ImTypesetDocumentContent content;
    content.Text = text.data();
    content.TextSize = static_cast<int>(text.size());
    content.Blocks = &block;
    content.BlockCount = 1;
    content.Styles = &style;
    content.StyleCount = 1;
    ImTypesetDocumentOptions options;
    options.CodeOverflow = ImTypesetCodeOverflow_Grow;
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    if (document == nullptr)
        return nullptr;
    ImTypeset::SetDocumentOptions(document, &options);
    ImTypeset::SetDocumentBlocks(document, &content);
    ImTypeset::LayoutDocument(document, 600.0f);
    return document;
}

} // namespace

// --- H8: how tall a table with a header is ------------------------------------------------

// The reference stacks a table's boxes (header, rule, rows) as it stacks
// lines: each a \baselineskip under the one before, the rule included
// (docs/divergences.md, T20). Measured through the pinned reference on
// demo/tables.md at 684 px, the first table is 6,393,984 sp: six leadings of
// 1,065,664 sp for a header and four rows. The sum of the heights of the boxes
// `_table_boxes` returns, which is never what the reference draws, is
// 5,375,683 sp: one leading less, plus the rule's own 47,363 sp.
TEST(a_table_with_a_header_is_a_leading_taller_than_its_rows_under_either_set_of_rules)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const Scaled leading = 1065664;
    const TableText table = demo_table();
    for (int reference = 0; reference < 2; ++reference) {
        CHECK_CONTEXT("%s rules", reference != 0 ? "reference" : "product");
        stage.ctx->tex_rules = reference != 0 ? TexRules::reference() : TexRules::product();
        ImTypesetDocument* headed = table_document(table, ImTypesetBlockFlags_TableHeader);
        ImTypesetDocument* plain = table_document(table, 0);
        REQUIRE(headed != nullptr && plain != nullptr);
        CHECK_EQ(extent_of(plain), 5 * leading);
        // The reference's own figure for this table at this width.
        CHECK_EQ(extent_of(headed), Scaled(6393984));
        CHECK_EQ(extent_of(headed), 6 * leading);
        const ImTypesetBlockGeometry geometry = ImTypeset::GetDocumentBlockGeometry(headed, 0);
        CHECK_NEAR(geometry.FullMax.y - geometry.FullMin.y, 129.6f, 1.0e-3f);
        // The first row under the header starts two leadings under the
        // header's top; every later row one leading under the row before.
        const ImTypesetTableRowGeometry header = ImTypeset::GetDocumentTableRow(headed, 0, 0);
        const ImTypesetTableRowGeometry first = ImTypeset::GetDocumentTableRow(headed, 0, 1);
        const ImTypesetTableRowGeometry second = ImTypeset::GetDocumentTableRow(headed, 0, 2);
        CHECK_NEAR(first.Min - header.Min, 43.2f, 1.0e-3f);
        CHECK_NEAR(second.Min - first.Min, 21.6f, 1.0e-3f);
        ImTypeset::DestroyDocument(headed);
        ImTypeset::DestroyDocument(plain);
    }
}

// The rule is placed by its baseline, a leading under the header's, so how
// thick it is moves nothing until it is thicker than the room above it.
TEST(the_rule_under_a_header_takes_the_same_height_whatever_its_thickness)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const TableText table = demo_table();
    ImTypesetDocument* usual = table_document(table, ImTypesetBlockFlags_TableHeader);
    REQUIRE(usual != nullptr);
    const Scaled expected = extent_of(usual);
    for (const float rule : {0.96f, 2.0f, 5.0f}) {
        CHECK_CONTEXT("TableRuleThickness %g", static_cast<double>(rule));
        ImTypesetDocument* document =
            table_document(table, ImTypesetBlockFlags_TableHeader, 684.0f, rule);
        REQUIRE(document != nullptr);
        CHECK_EQ(extent_of(document), expected);
        ImTypeset::DestroyDocument(document);
    }
    // No rule at all: the header is a row like the others.
    ImTypesetDocument* none = table_document(table, ImTypesetBlockFlags_TableHeader, 684.0f, -1.0f);
    REQUIRE(none != nullptr);
    CHECK_EQ(extent_of(none), Scaled(5 * 1065664));
    ImTypeset::DestroyDocument(none);
    ImTypeset::DestroyDocument(usual);
}

// --- H11: which quads a draw under a clip rectangle emits ---------------------------------

// A row of code with glyphs whose bitmaps reach outside the font's cell
// (U+2502, a box-drawing bar, is taller than the cell it joins across) and one
// set from another face. Under clip rectangles whose edges sweep through the
// rows, a quad is emitted exactly when some of it is inside the rectangle:
// whether its pen is, or its line's box, does not decide.
TEST(a_glyph_quad_is_emitted_exactly_when_part_of_it_is_inside_the_clip_rectangle)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    REQUIRE(ImTypeset::FaceHasGlyph(ImTypesetFace_Code, 0x2502));
    const std::string text = "line 0\nline 1\nline 2\n"
                             "x \xE4\xB8\xAD \xE2\x94\x82 \xE2\x8C\xA0 \xE2\x9F\xA8 \xD0\x96 y\n"
                             "line 4\nline 5";
    for (const bool fixed : {true, false}) {
        CHECK_CONTEXT("%s", fixed ? "FixedLineHeight" : "lines as tall as they are");
        ImTypesetDocument* document =
            code_document(text, fixed ? ImTypesetBlockFlags_FixedLineHeight : 0, 20.0f);
        REQUIRE(document != nullptr);
        Frame frame;
        const std::vector<Quad> whole =
            quads_under(document, ImVec4(-100.0f, -1000.0f, 4000.0f, 4000.0f));
        REQUIRE(whole.size() > size_t(30));
        // The premise: some quad starts above the top of the row it is on.
        const ImTypesetLine row = ImTypeset::GetDocumentBlockLine(document, 0, 3);
        int above_its_row = 0;
        for (const Quad& quad : whole)
            above_its_row += quad.y0 < row.Min.y && quad.y1 > row.Min.y && quad.y1 <= row.Max.y + 4;
        if (fixed)
            CHECK(above_its_row >= 1);
        const float height = ImTypeset::GetDocumentSize(document).y;
        int emitted = 0;
        for (float edge = -4.0f; edge <= height + 4.0f; edge += 0.5f) {
            CHECK_CONTEXT("edge %g", static_cast<double>(edge));
            // The same edge as a clip's bottom and as another's top.
            const ImVec4 upper(-100.0f, -1000.0f, 4000.0f, edge);
            const ImVec4 lower(-100.0f, edge, 4000.0f, 4000.0f);
            for (const ImVec4& clip : {upper, lower}) {
                const std::vector<Quad> got = quads_under(document, clip);
                size_t at = 0;
                for (const Quad& quad : whole) {
                    const bool inside = quad.y0 < clip.w && quad.y1 > clip.y;
                    const bool found = at < got.size() && same_quad(got[at], quad);
                    if (found)
                        ++at;
                    if (inside != found)
                        FAIL("quad (%g, %g)-(%g, %g): %s the clip (%g to %g) and %s",
                             static_cast<double>(quad.x0), static_cast<double>(quad.y0),
                             static_cast<double>(quad.x1), static_cast<double>(quad.y1),
                             inside ? "inside" : "outside", static_cast<double>(clip.y),
                             static_cast<double>(clip.w), found ? "emitted" : "not emitted");
                    emitted += found;
                }
                // Nothing else was emitted.
                CHECK_EQ(at, got.size());
            }
        }
        CHECK(emitted > 0);
        ImTypeset::DestroyDocument(document);
    }
}

// --- H10: marks culled to what the clip rectangle shows ------------------------------------

namespace
{

// A document of several kinds of block, long enough that a window shows a
// small part: paragraphs, a code block of 300 lines, a table, a paragraph.
struct Mixed {
    std::string text;
    std::vector<ImTypesetBlock> blocks;
    std::vector<ImTypesetCell> cells;

    void add(ImTypesetBlockKind kind, const std::string& body, ImTypesetBlockFlags flags = 0)
    {
        ImTypesetBlock block;
        block.Kind = kind;
        block.Flags = flags;
        block.TextStart = static_cast<int>(text.size());
        text += body;
        block.TextEnd = static_cast<int>(text.size());
        blocks.push_back(block);
    }
};

ImTypesetDocument* mixed_document(Mixed& mixed)
{
    const std::string prose = "The quick brown fox jumps over the lazy dog and then, having "
                              "thought better of it, walks back the way it came. ";
    mixed.add(ImTypesetBlockKind_Paragraph, prose + prose + prose);
    mixed.add(ImTypesetBlockKind_Paragraph,
              prose + "A formula, $a^2 + b^2 = c^2$, and more. " + prose, ImTypesetBlockFlags_Math);
    std::string code;
    for (int line = 0; line < 300; ++line) {
        char buffer[96];
        std::snprintf(buffer, sizeof buffer, "\tvalue_%03d = compute(\"item\", %d)  # line %d\n",
                      line, line * 7, line);
        code += buffer;
    }
    mixed.add(ImTypesetBlockKind_Code, code);
    {
        TableText table;
        for (int row = 0; row < 40; ++row)
            table.row({"row " + std::to_string(row), "a cell with several words in it",
                       std::to_string(row * row)});
        ImTypesetBlock block;
        block.Kind = ImTypesetBlockKind_Table;
        block.Flags = ImTypesetBlockFlags_TableHeader;
        block.TextStart = static_cast<int>(mixed.text.size());
        block.CellStart = 0;
        block.CellCount = static_cast<int>(table.cells.size());
        mixed.text += table.text;
        block.TextEnd = static_cast<int>(mixed.text.size());
        mixed.blocks.push_back(block);
        mixed.cells = table.cells;
    }
    mixed.add(ImTypesetBlockKind_Code, code, ImTypesetBlockFlags_FixedLineHeight);
    mixed.add(ImTypesetBlockKind_Paragraph, prose + prose);
    ImTypesetDocumentContent content;
    content.Text = mixed.text.data();
    content.TextSize = static_cast<int>(mixed.text.size());
    content.Blocks = mixed.blocks.data();
    content.BlockCount = static_cast<int>(mixed.blocks.size());
    content.Cells = mixed.cells.data();
    content.CellCount = static_cast<int>(mixed.cells.size());
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    if (document == nullptr)
        return nullptr;
    ImTypeset::SetDocumentBlocks(document, &content);
    ImTypeset::LayoutDocument(document, 520.0f);
    return document;
}

// Marks all over a document, from a fixed sequence: short ones, ones that
// overlap, ones across lines and across blocks, in the three places.
std::vector<ImTypesetMark> scattered_marks(ImTypesetDocument* document, int count, uint32_t seed,
                                           bool sorted)
{
    std::vector<ImTypesetMark> marks;
    uint32_t state = seed;
    const auto next = [&](uint32_t below) {
        state = state * 1664525u + 1013904223u;
        return static_cast<int>((state >> 8) % below);
    };
    const int blocks = ImTypeset::GetDocumentBlockCount(document);
    for (int k = 0; k < count; ++k) {
        ImTypesetMark mark;
        const int block = next(static_cast<uint32_t>(blocks));
        const int size = std::max(ImTypeset::GetDocumentBlockTextSize(document, block), 1);
        mark.Range.Start.Block = block;
        mark.Range.Start.Offset = next(static_cast<uint32_t>(size));
        mark.Range.End.Block = block;
        const int kind = next(20);
        const int length = kind == 0 ? 400 : kind == 1 ? 90 : 1 + next(12);
        mark.Range.End.Offset = mark.Range.Start.Offset + length;
        if (kind == 2 && block + 1 < blocks) {
            mark.Range.End.Block = block + 1;
            mark.Range.End.Offset = next(40);
        }
        mark.Color = IM_COL32(40 + next(200), 40 + next(200), 40 + next(200), 255);
        mark.Place = static_cast<ImTypesetMarkPlace>(next(3));
        marks.push_back(mark);
    }
    if (sorted)
        std::stable_sort(marks.begin(), marks.end(),
                         [](const ImTypesetMark& a, const ImTypesetMark& b) {
                             return a.Range.Start.Block < b.Range.Start.Block ||
                                    (a.Range.Start.Block == b.Range.Start.Block &&
                                     a.Range.Start.Offset < b.Range.Start.Offset);
                         });
    return marks;
}

struct Drawn {
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
};

void draw_marked(ImTypesetDocument* document, const std::vector<ImTypesetMark>& marks, float scroll,
                 float window, Drawn& out)
{
    ImTypesetDocumentDraw draw;
    draw.Marks = marks.data();
    draw.MarkCount = static_cast<int>(marks.size());
    ImTypeset::DrawDocumentToBuffers(document, ImVec2(0.0f, -scroll),
                                     ImVec4(0.0f, 0.0f, 600.0f, window), &out.vertices,
                                     &out.indices, &draw);
}

// The quads of a drawing that have some part inside the window, as their
// vertices: a draw emits a mark's rectangles on lines the window does not
// show, when it works the mark out at all, and those show nothing.
std::vector<ImDrawVert> shown_quads(const Drawn& drawn, float window)
{
    std::vector<ImDrawVert> out;
    for (int v = 0; v + 3 < drawn.vertices.Size; v += 4) {
        float top = drawn.vertices[v].pos.y;
        float bottom = top;
        for (int k = 1; k < 4; ++k) {
            top = std::min(top, drawn.vertices[v + k].pos.y);
            bottom = std::max(bottom, drawn.vertices[v + k].pos.y);
        }
        if (top < window && bottom > 0.0f)
            out.insert(out.end(), drawn.vertices.Data + v, drawn.vertices.Data + v + 4);
    }
    return out;
}

bool same_drawing(const Drawn& a, const Drawn& b, float window)
{
    const std::vector<ImDrawVert> left = shown_quads(a, window);
    const std::vector<ImDrawVert> right = shown_quads(b, window);
    if (left.size() != right.size())
        return false;
    for (size_t v = 0; v < left.size(); ++v) {
        if (left[v].pos.x != right[v].pos.x || left[v].pos.y != right[v].pos.y ||
            left[v].col != right[v].col || left[v].uv.x != right[v].uv.x ||
            left[v].uv.y != right[v].uv.y)
            return false;
    }
    return true;
}

} // namespace

// Whatever order the marks come in and wherever the window is, the draw that
// looks only for the marks on the lines it shows emits, inside the window,
// what the draw that works every mark out emits, vertex for vertex, and looks
// at far fewer.
TEST(marks_culled_to_the_clip_draw_what_every_mark_worked_out_draws)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Mixed mixed;
    ImTypesetDocument* document = mixed_document(mixed);
    REQUIRE(document != nullptr);
    ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e9f);
    const float height = ImTypeset::GetDocumentSize(document).y;
    REQUIRE(height > 8000.0f);
    Frame frame;
    for (int variant = 0; variant < 4; ++variant) {
        const bool sorted = (variant & 1) == 0;
        const int count = variant < 2 ? 3000 : 40;
        CHECK_CONTEXT("%d marks, %s", count, sorted ? "sorted by start" : "in no order");
        const std::vector<ImTypesetMark> marks =
            scattered_marks(document, count, 77u + static_cast<uint32_t>(variant), sorted);
        uint64_t culled_looks = 0;
        uint64_t whole_looks = 0;
        int coloured = 0;
        for (float scroll = -50.0f; scroll < height + 50.0f; scroll += 173.0f) {
            for (const float window : {40.0f, 300.0f}) {
                CHECK_CONTEXT("scroll %g, a window of %g", static_cast<double>(scroll),
                              static_cast<double>(window));
                Drawn culled;
                Drawn whole;
                document->marks.every_mark = false;
                draw_marked(document, marks, scroll, window, culled);
                culled_looks += document->marks.looked_at;
                document->marks.every_mark = true;
                draw_marked(document, marks, scroll, window, whole);
                whole_looks += document->marks.looked_at;
                document->marks.every_mark = false;
                CHECK(same_drawing(culled, whole, window));
                // The marks do something: a draw without them differs.
                Drawn bare;
                draw_marked(document, {}, scroll, window, bare);
                coloured += !same_drawing(bare, whole, window);
            }
        }
        CHECK(coloured > 20);
        if (count > 100) {
            CHECK(culled_looks * 4 < whole_looks);
            std::printf("marks: %d %s: %llu looked at where every mark of a shown block is %llu\n",
                        count, sorted ? "sorted" : "unsorted",
                        static_cast<unsigned long long>(culled_looks),
                        static_cast<unsigned long long>(whole_looks));
        }
    }
    ImTypeset::DestroyDocument(document);
}

// The order is made once for a set of marks, however many frames draw it and
// whether or not it came sorted, and again when a range changes.
TEST(the_order_of_a_mark_set_is_made_once_and_again_only_when_a_range_changes)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Mixed mixed;
    ImTypesetDocument* document = mixed_document(mixed);
    REQUIRE(document != nullptr);
    Frame frame;
    for (const bool sorted : {true, false}) {
        CHECK_CONTEXT("%s", sorted ? "sorted by start" : "in no order");
        std::vector<ImTypesetMark> marks = scattered_marks(document, 2000, 5u, sorted);
        const uint64_t before = document->marks.made;
        Drawn out;
        for (int f = 0; f < 50; ++f)
            draw_marked(document, marks, 100.0f * static_cast<float>(f), 300.0f, out);
        CHECK_EQ(document->marks.made, before + 1);
        // A colour is not a range: nothing to order again.
        marks[10].Color ^= 0x00FFFFFFu;
        draw_marked(document, marks, 0.0f, 300.0f, out);
        CHECK_EQ(document->marks.made, before + 1);
        // A copy of the same marks elsewhere in memory is the same set.
        const std::vector<ImTypesetMark> copy = marks;
        draw_marked(document, copy, 0.0f, 300.0f, out);
        CHECK_EQ(document->marks.made, before + 1);
        marks[1000].Range.End.Offset += 1;
        draw_marked(document, marks, 0.0f, 300.0f, out);
        CHECK_EQ(document->marks.made, before + 2);
        marks.pop_back();
        draw_marked(document, marks, 0.0f, 300.0f, out);
        CHECK_EQ(document->marks.made, before + 3);
    }
    ImTypeset::DestroyDocument(document);
}

// --- A bar under a line stands where the host put it ------------------------------------------

namespace
{

const ImU32 BAR = IM_COL32(255, 0, 255, 255);

// The quads of colour BAR that a draw of `document`, scrolled by `scroll`
// under a clip rectangle from `clip_top` to `clip_bottom`, emits with some
// part between 0 and `window`.
std::vector<Quad> bars_shown(ImTypesetDocument* document, const std::vector<ImTypesetMark>& marks,
                             float scroll, float clip_top, float clip_bottom, float window)
{
    ImTypesetDocumentDraw draw;
    draw.Marks = marks.data();
    draw.MarkCount = static_cast<int>(marks.size());
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    ImTypeset::DrawDocumentToBuffers(document, ImVec2(0.0f, -scroll),
                                     ImVec4(0.0f, clip_top, 600.0f, clip_bottom), &vertices,
                                     &indices, &draw);
    std::vector<Quad> out;
    for (int v = 0; v + 3 < vertices.Size; v += 4) {
        if (vertices[v].col != BAR)
            continue;
        Quad quad = {vertices[v].pos.x, vertices[v].pos.y, vertices[v].pos.x, vertices[v].pos.y};
        for (int k = 1; k < 4; ++k) {
            quad.x0 = std::min(quad.x0, vertices[v + k].pos.x);
            quad.y0 = std::min(quad.y0, vertices[v + k].pos.y);
            quad.x1 = std::max(quad.x1, vertices[v + k].pos.x);
            quad.y1 = std::max(quad.y1, vertices[v + k].pos.y);
        }
        if (quad.y0 < window && quad.y1 > 0.0f)
            out.push_back(quad);
    }
    std::sort(out.begin(), out.end(), [](const Quad& a, const Quad& b) {
        return a.y0 < b.y0 || (a.y0 == b.y0 && a.x0 < b.x0);
    });
    return out;
}

// Under a window moved a pixel at a time down `document`, the bars a draw
// emits inside the window are the bars a draw under a clip rectangle that
// shows the whole document emits there. `shown` counts the windows that had
// a bar in them.
void check_bars_under_every_window(ImTypesetDocument* document,
                                   const std::vector<ImTypesetMark>& marks, float from, float to,
                                   float window, int& shown)
{
    for (float scroll = from; scroll < to; scroll += 1.0f) {
        CHECK_CONTEXT("scroll %g", static_cast<double>(scroll));
        const std::vector<Quad> all = bars_shown(document, marks, scroll, -1.0e6f, 1.0e6f, window);
        const std::vector<Quad> seen = bars_shown(document, marks, scroll, 0.0f, window, window);
        shown += !all.empty();
        CHECK_EQ(seen.size(), all.size());
        if (seen.size() != all.size())
            continue;
        for (size_t k = 0; k < all.size(); ++k)
            CHECK(same_quad(seen[k], all[k]));
    }
}

ImTypesetMark bar_under(int block, int start, int end, float thickness, float offset)
{
    ImTypesetMark mark;
    mark.Range.Start.Block = block;
    mark.Range.Start.Offset = start;
    mark.Range.End.Block = block;
    mark.Range.End.Offset = end;
    mark.Color = BAR;
    mark.Place = ImTypesetMarkPlace_Under;
    mark.Thickness = thickness;
    mark.Offset = offset;
    return mark;
}

} // namespace

// An Under mark's bar is below its line's baseline by the Offset the host
// gave, which can put the bar inside a window that the line itself is wholly
// above. The bar is drawn there whether the block has few marks or many, and
// whether the line is in a block the window shows or in one above it.
TEST(a_bar_under_a_line_above_the_window_is_drawn_where_it_hangs_into_the_window)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    // Thickness and Offset, in pixels: both left to the library, a thick bar
    // at the library's offset, and bars a host hangs from just under the line
    // to several lines under it.
    const float bars[][2] = {{0.0f, 0.0f},  {5.0f, 0.0f},  {2.0f, 12.0f},
                             {0.0f, 30.0f}, {2.0f, 30.0f}, {2.0f, 75.0f}};
    for (const auto& bar : bars) {
        const float thickness = bar[0];
        const float offset = bar[1];
        CHECK_CONTEXT("thickness %g, offset %g", static_cast<double>(thickness),
                      static_cast<double>(offset));
        // One code block of 120 lines; a bar under a word of each of `count`
        // lines in a row.
        for (const int count : {3, 40}) {
            CHECK_CONTEXT("a code block, %d marks", count);
            std::string code;
            std::vector<ImTypesetMark> marks;
            for (int line = 0; line < 120; ++line) {
                const int start = static_cast<int>(code.size());
                code += "value = compute(item)  # line " + std::to_string(line) + "\n";
                if (line >= 40 && line < 40 + count)
                    marks.push_back(bar_under(0, start + 8, start + 15, thickness, offset));
            }
            ImTypesetDocument* document = code_document(code, 0, 20.0f);
            REQUIRE(document != nullptr);
            ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e9f);
            int shown = 0;
            check_bars_under_every_window(document, marks, 700.0f, 1800.0f, 30.0f, shown);
            CHECK(shown > 60);
            ImTypeset::DestroyDocument(document);
        }
        // Paragraphs of one line each, a bar under a word of each: the bar of
        // a paragraph's line hangs over the paragraphs after it.
        {
            CHECK_CONTEXT("paragraphs");
            Mixed mixed;
            std::vector<ImTypesetMark> marks;
            for (int block = 0; block < 60; ++block) {
                mixed.add(ImTypesetBlockKind_Paragraph,
                          "Paragraph " + std::to_string(block) + " is one line long.");
                marks.push_back(bar_under(block, 0, 9, thickness, offset));
            }
            ImTypesetDocumentContent content;
            content.Text = mixed.text.data();
            content.TextSize = static_cast<int>(mixed.text.size());
            content.Blocks = mixed.blocks.data();
            content.BlockCount = static_cast<int>(mixed.blocks.size());
            ImTypesetDocument* document = ImTypeset::CreateDocument();
            REQUIRE(document != nullptr);
            ImTypeset::SetDocumentBlocks(document, &content);
            ImTypeset::LayoutDocument(document, 520.0f);
            ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e9f);
            const float height = ImTypeset::GetDocumentSize(document).y;
            int shown = 0;
            check_bars_under_every_window(document, marks, 200.0f, height - 200.0f, 30.0f, shown);
            CHECK(shown > 100);
            ImTypeset::DestroyDocument(document);
        }
        // A table of 60 rows, a bar under the first cell of each.
        {
            CHECK_CONTEXT("a table");
            TableText table;
            std::vector<ImTypesetMark> marks;
            for (int row = 0; row < 60; ++row) {
                const int start = static_cast<int>(table.text.size()) + (row > 0 ? 1 : 0);
                table.row({"row " + std::to_string(row), "a cell", std::to_string(row * row)});
                marks.push_back(bar_under(0, start, start + 3, thickness, offset));
            }
            ImTypesetBlock block;
            block.Kind = ImTypesetBlockKind_Table;
            block.TextStart = 0;
            block.TextEnd = static_cast<int>(table.text.size());
            block.CellStart = 0;
            block.CellCount = static_cast<int>(table.cells.size());
            ImTypesetDocumentContent content;
            content.Text = table.text.data();
            content.TextSize = static_cast<int>(table.text.size());
            content.Blocks = &block;
            content.BlockCount = 1;
            content.Cells = table.cells.data();
            content.CellCount = static_cast<int>(table.cells.size());
            ImTypesetDocument* document = ImTypeset::CreateDocument();
            REQUIRE(document != nullptr);
            ImTypeset::SetDocumentBlocks(document, &content);
            ImTypeset::LayoutDocument(document, 520.0f);
            ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e9f);
            const float height = ImTypeset::GetDocumentSize(document).y;
            int shown = 0;
            check_bars_under_every_window(document, marks, 200.0f, height - 200.0f, 30.0f, shown);
            CHECK(shown > 100);
            ImTypeset::DestroyDocument(document);
        }
    }
}

// --- H9: a table row that arrives in pieces -------------------------------------------------

namespace
{

typedef std::vector<std::vector<std::string>> Rows;

// Row `r` of a table of three columns as its source would be written, one
// line of cells between bars. The cells differ in length from row to row.
std::string source_row(int r)
{
    const char* const words[] = {"alpha", "beta", "gamma delta", "epsilon", "zeta eta theta"};
    return "| item " + std::to_string(r) + " | " + words[r % 5] + " and " + words[(r * 3 + 1) % 5] +
           " | " + std::to_string(r * 37 % 1000) + " |\n";
}

std::string trimmed(const std::string& text)
{
    const size_t first = text.find_first_not_of(' ');
    if (first == std::string::npos)
        return std::string();
    return text.substr(first, text.find_last_not_of(' ') - first + 1);
}

// What a host's parser makes of the source that has arrived: the rows that
// are complete, each of `columns` cells, and the row being written. With
// `padded` that row has `columns` cells too, the ones not yet written empty;
// without it, the cells it has, down to the one being written.
Rows parsed(const std::string& source, size_t columns, bool padded)
{
    Rows rows;
    size_t at = 0;
    while (at < source.size()) {
        const size_t line_end = source.find('\n', at);
        const bool complete = line_end != std::string::npos;
        const std::string line = source.substr(at, complete ? line_end - at : std::string::npos);
        at = complete ? line_end + 1 : source.size();
        std::vector<std::string> cells;
        size_t bar = line.find('|');
        while (bar != std::string::npos) {
            const size_t next = line.find('|', bar + 1);
            if (next == std::string::npos) {
                if (!complete && bar + 1 < line.size())
                    cells.push_back(trimmed(line.substr(bar + 1)));
                break;
            }
            cells.push_back(trimmed(line.substr(bar + 1, next - bar - 1)));
            bar = next;
        }
        if (complete || padded)
            cells.resize(columns);
        if (!cells.empty())
            rows.push_back(cells);
    }
    return rows;
}

// The block's text for rows of cells, and the cells: a tab between cells, a
// newline between rows.
TableText text_of(const Rows& rows)
{
    TableText out;
    for (const std::vector<std::string>& row : rows)
        out.row(row);
    return out;
}

struct Streamed {
    ImTypesetDocument* document = nullptr;
    Rows rows;
    std::string text;
    int replaces = 0;
    int appends = 0;

    void replace(const Rows& now)
    {
        const TableText table = text_of(now);
        ImTypesetBlock block;
        block.Kind = ImTypesetBlockKind_Table;
        block.Flags = ImTypesetBlockFlags_TableHeader | ImTypesetBlockFlags_TableHeld;
        block.TextStart = 0;
        block.TextEnd = static_cast<int>(table.text.size());
        block.CellStart = 0;
        block.CellCount = static_cast<int>(table.cells.size());
        ImTypesetDocumentContent content;
        content.Text = table.text.data();
        content.TextSize = static_cast<int>(table.text.size());
        content.Blocks = &block;
        content.BlockCount = 1;
        content.Cells = table.cells.data();
        content.CellCount = static_cast<int>(table.cells.size());
        if (document == nullptr) {
            document = ImTypeset::CreateDocument();
            ImTypeset::SetDocumentBlocks(document, &content);
        } else {
            ImTypeset::ReplaceDocumentBlocks(document, 0, 1, &content);
            ++replaces;
        }
        rows = now;
        text = table.text;
    }

    // Brings the table to `now` by appending, if `now` is what the table holds
    // with more at its end: more text in its last cell, further cells in its
    // last row, further rows. False, with nothing done, if it is not.
    bool append(const Rows& now)
    {
        if (document == nullptr || rows.empty() || now.size() < rows.size())
            return false;
        const size_t last = rows.size() - 1;
        for (size_t r = 0; r < last; ++r) {
            if (rows[r] != now[r])
                return false;
        }
        const std::vector<std::string>& had = rows[last];
        const std::vector<std::string>& has = now[last];
        if (had.empty() || has.size() < had.size())
            return false;
        for (size_t c = 0; c + 1 < had.size(); ++c) {
            if (had[c] != has[c])
                return false;
        }
        const std::string& old_cell = had.back();
        const std::string& new_cell = has[had.size() - 1];
        if (new_cell.compare(0, old_cell.size(), old_cell) != 0)
            return false;
        if (new_cell.size() > old_cell.size()) {
            const std::string more = new_cell.substr(old_cell.size());
            if (!ImTypeset::AppendDocumentText(document, more.data(), more.data() + more.size()))
                return false;
            text += more;
            ++appends;
        }
        // Further cells of the last row, then further rows: one call.
        std::string more;
        std::vector<ImTypesetCell> cells;
        const auto add = [&](const std::string& cell, bool row_start) {
            more += row_start ? '\n' : '\t';
            ImTypesetCell given;
            given.TextStart = static_cast<int>(text.size() + more.size());
            more += cell;
            given.TextEnd = static_cast<int>(text.size() + more.size());
            given.Flags = row_start ? ImTypesetCellFlags_RowStart : 0;
            cells.push_back(given);
        };
        for (size_t c = had.size(); c < has.size(); ++c)
            add(has[c], false);
        for (size_t r = rows.size(); r < now.size(); ++r) {
            for (size_t c = 0; c < now[r].size(); ++c)
                add(now[r][c], c == 0);
        }
        if (!cells.empty()) {
            if (!ImTypeset::AppendDocumentTableCells(document, more.data(),
                                                     more.data() + more.size(), cells.data(),
                                                     static_cast<int>(cells.size())))
                return false;
            text += more;
            ++appends;
        }
        rows = now;
        return true;
    }
};

struct StreamCost {
    ImU64 cells_measured = 0;
    ImU64 rows_set = 0;
    int replaces = 0;
    int appends = 0;
    ImTypesetDocument* document = nullptr;
};

// A table of `count` rows under a header, arriving `piece` bytes of its
// source at a time and laid out after each piece, as a frame would.
StreamCost stream_table(int count, size_t piece, bool padded)
{
    std::string source = "| name | what it holds | number |\n";
    for (int r = 0; r < count; ++r)
        source += source_row(r);
    Streamed stream;
    for (size_t arrived = std::min(piece, source.size());;
         arrived = std::min(arrived + piece, source.size())) {
        const Rows now = parsed(source.substr(0, arrived), 3, padded);
        if (!now.empty() && now != stream.rows && !stream.append(now))
            stream.replace(now);
        if (stream.document != nullptr) {
            ImTypeset::LayoutDocument(stream.document, 2000.0f);
            ImTypeset::LayoutDocumentWindow(stream.document, 0.0f, 1.0e9f);
        }
        if (arrived == source.size())
            break;
    }
    StreamCost cost;
    const ImTypesetDocumentStats stats = ImTypeset::GetDocumentStats(stream.document);
    cost.cells_measured = stats.TableCellsMeasured;
    cost.rows_set = stats.TableRowsSet;
    cost.replaces = stream.replaces;
    cost.appends = stream.appends;
    cost.document = stream.document;
    return cost;
}

} // namespace

// A row that arrives in pieces costs its own cells when the host describes it
// without the cells that are not written yet: the cell being written is then
// the table's last, AppendDocumentText() grows it, and AppendDocumentTableCells()
// with a first cell that is no row's start adds the next one to the row.
// Described with the unwritten cells as empty ones, the cell being written is
// not the last, nothing can be appended and every piece is a replace of the
// table, which measures every cell again.
TEST(a_row_arriving_in_pieces_costs_its_cells_when_described_without_the_unwritten_ones)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const int rows = 200;
    const StreamCost whole = stream_table(rows, 1u << 20, false);
    const StreamCost padded = stream_table(rows, 7, true);
    const StreamCost pieces = stream_table(rows, 7, false);
    REQUIRE(whole.document != nullptr && padded.document != nullptr && pieces.document != nullptr);
    std::printf("  a table of %d rows, 7 bytes of source a piece:\n"
                "    unfinished row padded with empty cells: %d appends, %d replaces, %llu cells "
                "measured, %llu rows set\n"
                "    unfinished row as the cells it has:     %d appends, %d replaces, %llu cells "
                "measured, %llu rows set\n"
                "    (given whole in one call: %llu cells measured, %llu rows set)\n",
                rows, padded.appends, padded.replaces,
                static_cast<unsigned long long>(padded.cells_measured),
                static_cast<unsigned long long>(padded.rows_set), pieces.appends, pieces.replaces,
                static_cast<unsigned long long>(pieces.cells_measured),
                static_cast<unsigned long long>(pieces.rows_set),
                static_cast<unsigned long long>(whole.cells_measured),
                static_cast<unsigned long long>(whole.rows_set));
    // Without padding nothing is replaced, and a piece measures the cell it
    // grew or added: one a call, whatever the table holds.
    CHECK_EQ(pieces.replaces, 0);
    CHECK(pieces.cells_measured <= static_cast<ImU64>(pieces.appends) + 3 * (rows + 1));
    // With it, the cost grows with the table at every piece.
    CHECK(padded.replaces > rows);
    CHECK(padded.cells_measured > 20 * pieces.cells_measured);

    // The three tables are one table: the same text, rows and geometry.
    const ImTypesetTableGeometry expected = ImTypeset::GetDocumentTableGeometry(whole.document, 0);
    REQUIRE_EQ(expected.RowCount, rows + 1);
    for (const StreamCost* other : {&padded, &pieces}) {
        CHECK_CONTEXT("%s", other == &padded ? "padded" : "pieces");
        const ImTypesetTableGeometry got = ImTypeset::GetDocumentTableGeometry(other->document, 0);
        CHECK_EQ(got.RowCount, expected.RowCount);
        CHECK_EQ(got.ColumnCount, expected.ColumnCount);
        CHECK_EQ(got.CellCount, expected.CellCount);
        CHECK_EQ(got.Max.y - got.Min.y, expected.Max.y - expected.Min.y);
        for (int c = 0; c < expected.ColumnCount; ++c) {
            const ImTypesetTableColumnGeometry a =
                ImTypeset::GetDocumentTableColumn(whole.document, 0, c);
            const ImTypesetTableColumnGeometry b =
                ImTypeset::GetDocumentTableColumn(other->document, 0, c);
            CHECK_EQ(a.Min, b.Min);
            CHECK_EQ(a.Max, b.Max);
        }
        for (int r = 0; r < expected.RowCount; r += 17) {
            const ImTypesetTableRowGeometry a =
                ImTypeset::GetDocumentTableRow(whole.document, 0, r);
            const ImTypesetTableRowGeometry b =
                ImTypeset::GetDocumentTableRow(other->document, 0, r);
            CHECK_EQ(a.Min, b.Min);
            CHECK_EQ(a.Max, b.Max);
            CHECK_EQ(a.CellCount, b.CellCount);
        }
        ImGuiTextBuffer a_text;
        ImGuiTextBuffer b_text;
        const ImTypesetDocumentRange all = {{0, 0}, {0, 1 << 30}};
        ImTypeset::AppendDocumentRangeText(whole.document, all, &a_text);
        ImTypeset::AppendDocumentRangeText(other->document, all, &b_text);
        CHECK(a_text.size() > rows * 10);
        CHECK(std::string(a_text.c_str()) == std::string(b_text.c_str()));
    }
    ImTypeset::DestroyDocument(whole.document);
    ImTypeset::DestroyDocument(padded.document);
    ImTypeset::DestroyDocument(pieces.document);
}

// --- H7: a role's font at a size ------------------------------------------------------------

// The reference's own answers (tests/golden/font, `FM` and `CH` of every role
// at every size it was dumped at) are what the query answers, as the float
// each double comes to. The line gap is not among the reference's answers: it
// is held to the font file's hhea table as the raw goldens record it, scaled
// the way every other length is.
TEST(font_metrics_and_advances_are_the_references_as_floats)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const struct {
        const char* role;
        ImTypesetFace face;
        const char* file;
    } roles[] = {
        {"prose", ImTypesetFace_Regular, "Roboto-Regular.ttf"},
        {"prose-bold", ImTypesetFace_Bold, "Roboto-Bold.ttf"},
        {"prose-italic", ImTypesetFace_Italic, "Roboto-RegularItalic.ttf"},
        {"prose-bold-italic", ImTypesetFace_BoldItalic, "Roboto-BoldItalic.ttf"},
        {"code", ImTypesetFace_Code, "Inconsolata-Medium.ttf"},
        // No file of its own here, as in the reference: Bold's.
        {"code-bold", ImTypesetFace_CodeBold, "Roboto-Bold.ttf"},
        {"math", ImTypesetFace_Math, "latinmodern-math.otf"},
    };
    int sizes = 0;
    int advances = 0;
    int not_askable = 0;
    for (const auto& role : roles) {
        CHECK_CONTEXT("%s", role.role);
        golden::File raw;
        REQUIRE(raw.load(golden::path(std::string("font/") + role.file + ".raw.golden")));
        double units_per_em = 0.0;
        double line_gap = 0.0;
        for (const golden::Record& r : raw) {
            if (r.is("HEAD"))
                units_per_em = static_cast<double>(r.i64(0));
            else if (r.is("HHEA"))
                line_gap = static_cast<double>(r.i64(2));
        }
        REQUIRE(units_per_em > 0.0);
        golden::File answers;
        REQUIRE(answers.load(golden::path(std::string("font/") + role.role + ".answers.golden")));
        float size = 0.0f;
        bool askable = false;
        for (const golden::Record& r : answers) {
            CHECK_CONTEXT("line %d", r.line);
            if (r.is("SIZE")) {
                // A size the reference was asked at that a float does not hold
                // (14.4) cannot be asked for here, where a size is a float:
                // its records are passed over, and counted.
                size = static_cast<float>(r.f64(0));
                askable = static_cast<double>(size) == r.f64(0);
                not_askable += !askable;
            } else if (!askable) {
                continue;
            } else if (r.is("FM")) {
                ImTypesetFontMetrics got;
                REQUIRE(ImTypeset::GetFontMetrics(role.face, size, &got));
                CHECK_EQ(got.Ascent, static_cast<float>(r.f64(0)));
                CHECK_EQ(got.Descent, static_cast<float>(r.f64(1)));
                CHECK_EQ(got.XHeight, static_cast<float>(r.f64(2)));
                CHECK_EQ(got.EmSize, static_cast<float>(r.f64(3)));
                CHECK_EQ(got.SpaceAdvance, static_cast<float>(r.f64(4)));
                CHECK_EQ(got.FontSize, static_cast<float>(r.f64(0) + r.f64(1)));
                CHECK_EQ(got.LineGap,
                         static_cast<float>(line_gap * (static_cast<double>(size) / units_per_em)));
                ++sizes;
            } else if (r.is("CH")) {
                const unsigned int codepoint = r.u32(0);
                CHECK_EQ(ImTypeset::GetGlyphAdvance(role.face, size, codepoint),
                         static_cast<float>(r.f64(2)));
                CHECK_EQ(ImTypeset::FaceHasGlyph(role.face, codepoint), r.boolean(1));
                ++advances;
            }
        }
    }
    std::printf("  font metrics: %d (role, size) answers and %d advances equal to the "
                "reference's; %d (role, size) not asked, the size being no float\n",
                sizes, advances, not_askable);
    CHECK(sizes >= 7 * 4);
    CHECK(advances > 7 * 4 * 100);
}

// The size ImGui means and the em: the ratio the metrics give is the one the
// library itself uses to turn ImTypesetStyle::FontSize into an em, so a host
// that sizes by one and a host that sizes by the other set the same lines.
TEST(the_em_for_an_imgui_size_follows_from_the_metrics_at_any_size)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const std::string text = "Sizing a face by its height and by its em gives the same lines "
                             "when the em is the one the metrics say.";
    for (const float height : {13.0f, 16.0f, 19.0f, 24.0f}) {
        CHECK_CONTEXT("height %g", static_cast<double>(height));
        ImTypesetFontMetrics probe;
        REQUIRE(ImTypeset::GetFontMetrics(ImTypesetFace_Regular, 1024.0f, &probe));
        const float em =
            static_cast<float>(static_cast<double>(height) * static_cast<double>(probe.EmSize) /
                               static_cast<double>(probe.FontSize));
        ImTypesetFontMetrics at;
        REQUIRE(ImTypeset::GetFontMetrics(ImTypesetFace_Regular, em, &at));
        // To the float: a height of 16 is an em of 13.653 in Roboto.
        CHECK_NEAR(at.FontSize, height, height * 2.0e-7f);
        ImTypesetStyle by_height;
        by_height.FontSize = height;
        ImTypesetStyle by_em;
        by_em.EmSize = em;
        ImTypesetParagraph* a = ImTypeset::CreateParagraph();
        ImTypesetParagraph* b = ImTypeset::CreateParagraph();
        REQUIRE(a != nullptr && b != nullptr);
        ImTypeset::SetParagraphText(a, text.data(), text.data() + text.size());
        ImTypeset::SetParagraphText(b, text.data(), text.data() + text.size());
        REQUIRE(ImTypeset::LayoutParagraph(a, 300.0f, &by_height));
        REQUIRE(ImTypeset::LayoutParagraph(b, 300.0f, &by_em));
        REQUIRE_EQ(ImTypeset::GetParagraphLineCount(a), ImTypeset::GetParagraphLineCount(b));
        for (int line = 0; line < ImTypeset::GetParagraphLineCount(a); ++line) {
            const ImTypesetLine la = ImTypeset::GetParagraphLine(a, line);
            const ImTypesetLine lb = ImTypeset::GetParagraphLine(b, line);
            CHECK_EQ(la.TextStart, lb.TextStart);
            CHECK_NEAR(la.Max.x, lb.Max.x, 1.0e-3f);
            CHECK_NEAR(la.Baseline, lb.Baseline, 1.0e-3f);
        }
        ImTypeset::DestroyParagraph(a);
        ImTypeset::DestroyParagraph(b);
    }
}

TEST(a_metrics_query_refuses_what_it_cannot_answer_and_says_why)
{
    ImTypesetFontMetrics got;
    got.Ascent = 5.0f;
    // No context.
    CHECK(!ImTypeset::GetFontMetrics(ImTypesetFace_Regular, 16.0f, &got));
    CHECK_EQ(ImTypeset::GetLastError()->Code, ImTypesetError_NoContext);
    CHECK_EQ(got.Ascent, 0.0f);
    CHECK_EQ(ImTypeset::GetGlyphAdvance(ImTypesetFace_Regular, 16.0f, 'a'), 0.0f);
    {
        // No ImGui context is needed: a font registered and none current.
        Stage stage;
        REQUIRE(stage.ctx != nullptr);
        ImGuiContext* imgui = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(nullptr);
        CHECK(ImTypeset::GetFontMetrics(ImTypesetFace_Regular, 16.0f, &got));
        CHECK(got.Ascent > 0.0f);
        CHECK(ImTypeset::GetGlyphAdvance(ImTypesetFace_Regular, 16.0f, 'a') > 0.0f);
        ImGui::SetCurrentContext(imgui);

        const float bad_sizes[] = {0.0f, -16.0f, 2.0e6f, std::nanf(""), HUGE_VALF, -HUGE_VALF};
        for (const float size : bad_sizes) {
            CHECK_CONTEXT("size %g", static_cast<double>(size));
            got.Ascent = 5.0f;
            CHECK(!ImTypeset::GetFontMetrics(ImTypesetFace_Regular, size, &got));
            CHECK_EQ(ImTypeset::GetLastError()->Code, ImTypesetError_InvalidArgument);
            CHECK_EQ(got.Ascent, 0.0f);
            CHECK_EQ(ImTypeset::GetGlyphAdvance(ImTypesetFace_Regular, size, 'a'), 0.0f);
        }
        for (const ImTypesetFace face :
             {ImTypesetFace(-3), ImTypesetFace(ImTypesetFace_COUNT), ImTypesetFace(99)}) {
            CHECK(!ImTypeset::GetFontMetrics(face, 16.0f, &got));
            CHECK_EQ(ImTypeset::GetLastError()->Code, ImTypesetError_InvalidArgument);
        }
        CHECK(!ImTypeset::GetFontMetrics(ImTypesetFace_Regular, 16.0f, nullptr));
        CHECK_EQ(ImTypeset::GetLastError()->Code, ImTypesetError_InvalidArgument);
        // A caller compiled against another header: refused, not written to.
        got.Ascent = 5.0f;
        CHECK(!ImTypeset::GetFontMetrics(ImTypesetFace_Regular, 16.0f, &got, sizeof got + 4));
        CHECK_EQ(ImTypeset::GetLastError()->Code, ImTypesetError_VersionMismatch);
        CHECK_EQ(got.Ascent, 5.0f);
        // The largest size answers finite numbers; a codepoint that is none
        // answers the missing glyph's advance.
        CHECK(ImTypeset::GetFontMetrics(ImTypesetFace_Math, 1.0e6f, &got));
        CHECK(std::isfinite(got.Ascent) && std::isfinite(got.FontSize) && got.FontSize > 1.0e5f);
        CHECK_EQ(ImTypeset::GetGlyphAdvance(ImTypesetFace_Regular, 16.0f, 0xFFFFFFFFu),
                 ImTypeset::GetGlyphAdvance(ImTypesetFace_Regular, 16.0f, 0x10FFFE));
        // A role whose font was taken away.
        {
            Frame frame;
        }
        REQUIRE(ImTypeset::RemoveFont(ImTypesetFace_Italic));
        CHECK(!ImTypeset::GetFontMetrics(ImTypesetFace_Italic, 16.0f, &got));
        CHECK_EQ(ImTypeset::GetLastError()->Code, ImTypesetError_NoFont);
    }
}

// --- H12: what a binding can hand over without ending the process ---------------------------

// An empty ID is one ImGui refuses by assertion, and a binding hands a host's
// string straight through: none and an empty one are the view's own ID.
TEST(a_document_view_with_no_id_or_an_empty_one_is_drawn_under_its_own)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const TableText table = demo_table();
    ImTypesetDocument* document = table_document(table, ImTypesetBlockFlags_TableHeader);
    REQUIRE(document != nullptr);
    Frame frame;
    ImGui::SetNextWindowSize(ImVec2(700.0f, 500.0f));
    ImGui::Begin("host");
    for (const char* id : {static_cast<const char*>(nullptr), "", "named"}) {
        CHECK_CONTEXT("id %s", id == nullptr ? "(null)" : id);
        ImGui::PushID(id == nullptr ? 0 : id[0] == '\0' ? 1 : 2);
        CHECK(ImTypeset::DocumentView(id, document, ImVec2(0.0f, 140.0f)));
        CHECK_EQ(ImTypeset::GetLastError()->Code, ImTypesetError_None);
        ImGui::PopID();
    }
    ImGui::End();
    ImTypeset::DestroyDocument(document);
}

// The demo's two entry points outside a frame, and with no ImGui context at
// all, return without touching ImGui: before the first frame ImGui::Begin()
// asserts and the current window is not there to read.
TEST(the_demo_draws_nothing_outside_a_frame)
{
    // No ImGui context.
    ImTypeset::ShowDemoWindow();
    ImTypeset::ShowDemoSection(ImTypesetDemoSection_Math);
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    // Before the first frame.
    ImTypeset::ShowDemoWindow();
    ImTypeset::ShowDemoSection(ImTypesetDemoSection_Math);
    {
        Frame frame;
        ImGui::Begin("host");
        ImTypeset::ShowDemoSection(ImTypesetDemoSection_Fonts);
        ImGui::End();
    }
    // After Render().
    ImTypeset::ShowDemoWindow();
    ImTypeset::ShowDemoSection(ImTypesetDemoSection_Math);
    CHECK(ImGui::GetDrawData() != nullptr);
}
