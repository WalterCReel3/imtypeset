// A document shown in columns, through the public header.
//
// What is held to the reference -- where a column is cut and what it ships --
// is tests/test_vsplit.cpp, tests/test_columns.cpp and, through this header,
// tests/test_api_document.cpp's replay of tests/golden/tables/columns.golden.
// THE REFERENCE DRAWS COLUMNS AND NOTHING ELSE: it never hit-tests, selects or
// copies in them, and it does not balance. Everything here stands on a
// decision (docs/decisions.md D58) and is tested on its own terms: against the
// same document shown uncut, which is what a column is a stretch of. Only the
// public header is included.

#include "golden.h"

#include "imtypeset.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace
{

const struct {
    ImTypesetFace face;
    const char* file;
} FONTS[] = {
    {ImTypesetFace_Regular, "Roboto-Regular.ttf"},
    {ImTypesetFace_Bold, "Roboto-Bold.ttf"},
    {ImTypesetFace_Italic, "Roboto-RegularItalic.ttf"},
    {ImTypesetFace_Code, "Inconsolata-Medium.ttf"},
    {ImTypesetFace_Math, "latinmodern-math.otf"},
};

struct Stage {
    Stage()
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1200.0f, 800.0f);
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
        ImTypeset::GetStyle().Color = IM_COL32(0x11, 0x22, 0x33, 0xFF);
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

struct Content {
    std::string text;
    std::vector<ImTypesetBlock> blocks;
    std::vector<ImTypesetCell> cells;
    std::vector<ImTypesetBlockStyle> styles;

    ImTypesetBlock& add(ImTypesetBlockKind kind, const std::string& body, int style = 0)
    {
        ImTypesetBlock block;
        block.Kind = kind;
        block.Style = style;
        block.TextStart = static_cast<int>(text.size());
        text += body;
        block.TextEnd = static_cast<int>(text.size());
        blocks.push_back(block);
        return blocks.back();
    }
    ImTypesetBlock& table(int rows, int columns)
    {
        ImTypesetBlock block;
        block.Kind = ImTypesetBlockKind_Table;
        block.Flags = ImTypesetBlockFlags_TableHeader;
        block.TextStart = static_cast<int>(text.size());
        block.CellStart = static_cast<int>(cells.size());
        const size_t base = text.size();
        for (int r = 0; r < rows; ++r) {
            if (r != 0)
                text += '\n';
            for (int c = 0; c < columns; ++c) {
                if (c != 0)
                    text += '\t';
                ImTypesetCell cell;
                cell.TextStart = static_cast<int>(text.size() - base);
                text += "r" + std::to_string(r) + "c" + std::to_string(c);
                cell.TextEnd = static_cast<int>(text.size() - base);
                cell.Flags = c == 0 ? ImTypesetCellFlags_RowStart : 0;
                cells.push_back(cell);
            }
        }
        block.TextEnd = static_cast<int>(text.size());
        block.CellCount = static_cast<int>(cells.size()) - block.CellStart;
        blocks.push_back(block);
        return blocks.back();
    }
    ImTypesetDocumentContent view() const
    {
        ImTypesetDocumentContent out;
        out.Text = text.data();
        out.TextSize = static_cast<int>(text.size());
        out.Blocks = blocks.data();
        out.BlockCount = static_cast<int>(blocks.size());
        out.Cells = cells.data();
        out.CellCount = static_cast<int>(cells.size());
        out.Styles = styles.data();
        out.StyleCount = static_cast<int>(styles.size());
        return out;
    }
};

const char* const SENTENCE =
    "Stored letters and other symbols are retrieved and ordered according to a language's "
    "orthography for visual display.";

// `count` paragraphs of differing lengths, a heading before every tenth.
Content prose(int count, bool headings = false)
{
    Content content;
    ImTypesetBlockStyle body;
    body.SpaceBelow = 8.0f;
    content.styles.push_back(body);
    ImTypesetBlockStyle head;
    head.Face = ImTypesetFace_Bold;
    head.Scale = 1.4f;
    head.SpaceAbove = 6.0f;
    head.SpaceBelow = 6.0f;
    head.Align = ImTypesetAlign_Left;
    content.styles.push_back(head);
    for (int i = 0; i < count; ++i) {
        if (headings && i % 10 == 0)
            content.add(ImTypesetBlockKind_Heading, "Heading " + std::to_string(i / 10), 1);
        std::string body_text = std::to_string(i) + " " + SENTENCE;
        for (int extra = 0; extra < i % 3; ++extra)
            body_text += std::string(" ") + SENTENCE;
        content.add(ImTypesetBlockKind_Paragraph, body_text);
    }
    return content;
}

struct Shown {
    explicit Shown(const Content& content, float width = 260.0f)
    {
        handle = ImTypeset::CreateDocument();
        const ImTypesetDocumentContent view = content.view();
        CHECK(ImTypeset::SetDocumentBlocks(handle, &view));
        CHECK(ImTypeset::LayoutDocument(handle, width));
    }
    ~Shown() { ImTypeset::DestroyDocument(handle); }
    Shown(const Shown&) = delete;
    Shown& operator=(const Shown&) = delete;
    operator ImTypesetDocument*() const { return handle; }

    ImTypesetDocument* handle = nullptr;
};

void in_order(ImTypesetDocument* document, float height, float gutter = 0.0f)
{
    ImTypesetColumns columns;
    columns.Height = height;
    columns.Gutter = gutter;
    CHECK(ImTypeset::SetDocumentColumns(document, &columns));
}

void balanced(ImTypesetDocument* document, int count, float gutter = 0.0f)
{
    ImTypesetColumns columns;
    columns.Count = count;
    columns.Gutter = gutter;
    columns.Flags = ImTypesetColumnFlags_Balance;
    CHECK(ImTypeset::SetDocumentColumns(document, &columns));
}

bool same(const ImTypesetPosition& a, const ImTypesetPosition& b)
{
    return a.Block == b.Block && a.Offset == b.Offset;
}

bool before(const ImTypesetPosition& a, const ImTypesetPosition& b)
{
    return a.Block < b.Block || (a.Block == b.Block && a.Offset < b.Offset);
}

std::string copied(ImTypesetDocument* document, ImTypesetPosition start, ImTypesetPosition end)
{
    ImGuiTextBuffer buffer;
    const ImTypesetDocumentRange range = {start, end};
    ImTypeset::AppendDocumentRangeText(document, range, &buffer);
    return std::string(buffer.c_str());
}

struct Buffers {
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
};

// Every line of every block of a document shown uncut: the block, the line,
// its rectangle in the uncut column, and the text it holds.
struct UncutLine {
    int block = 0;
    int line = 0;
    ImVec2 min;
    ImVec2 max;
    int text_start = 0;
    int text_end = 0;
};

std::vector<UncutLine> uncut_lines(ImTypesetDocument* document)
{
    std::vector<UncutLine> out;
    const int blocks = ImTypeset::GetDocumentBlockCount(document);
    for (int b = 0; b < blocks; ++b) {
        const ImTypesetBlockGeometry geometry = ImTypeset::GetDocumentBlockGeometry(document, b);
        for (int l = 0; l < geometry.LineCount; ++l) {
            const ImTypesetLine line = ImTypeset::GetDocumentBlockLine(document, b, l);
            UncutLine entry;
            entry.block = b;
            entry.line = l;
            entry.min = ImVec2(geometry.FullMin.x + line.Min.x, geometry.FullMin.y + line.Min.y);
            entry.max = ImVec2(geometry.FullMin.x + line.Max.x, geometry.FullMin.y + line.Max.y);
            entry.text_start = line.TextStart;
            entry.text_end = line.TextEnd;
            out.push_back(entry);
        }
    }
    return out;
}

} // namespace

TEST(columns_tile_the_document_in_reading_order)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    const Content content = prose(60, true);
    Shown document(content);
    CHECK_EQ(ImTypeset::GetDocumentColumnCount(document), 1);
    {
        // Shown uncut, a document is its one column.
        const ImTypesetColumnGeometry whole = ImTypeset::GetDocumentColumn(document, 0);
        CHECK(whole.Exists);
        CHECK(same(whole.Start, ImTypesetPosition{0, 0}));
        CHECK(!ImTypeset::GetDocumentColumn(document, 1).Exists);
        const ImVec2 point(31.0f, 977.0f);
        const ImVec2 there = ImTypeset::GetDocumentColumnPos(document, point);
        CHECK(there.x == point.x && there.y == point.y);
    }

    const float height = 400.0f;
    const float gutter = 24.0f;
    in_order(document, height, gutter);
    CHECK(ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e9f));
    const int count = ImTypeset::GetDocumentColumnCount(document);
    REQUIRE(count > 5);
    const ImVec2 size = ImTypeset::GetDocumentSize(document);
    CHECK_EQ(size.y, height);
    CHECK_EQ(size.x, std::ceil(260.0f * static_cast<float>(count) +
                               gutter * static_cast<float>(count - 1)));

    const ImTypesetPosition end = {static_cast<int>(content.blocks.size()) - 1,
                                   content.blocks.back().TextEnd - content.blocks.back().TextStart};
    ImTypesetPosition at = {0, 0};
    int overfull = 0;
    for (int k = 0; k < count; ++k) {
        CHECK_CONTEXT("column %d", k);
        const ImTypesetColumnGeometry column = ImTypeset::GetDocumentColumn(document, k);
        REQUIRE(column.Exists);
        // Where it is on the page.
        CHECK_NEAR(column.Min.x, static_cast<float>(k) * (260.0f + gutter), 0.001);
        CHECK_NEAR(column.Max.x, column.Min.x + 260.0f, 0.001);
        CHECK_EQ(column.Min.y, 0.0f);
        CHECK_EQ(column.Max.y, height);
        // What it holds follows what the column before held.
        CHECK(same(column.Start, at));
        CHECK(before(column.Start, column.End));
        at = column.End;
        // It is full, to within a line and a gap, and nothing sticks out but
        // a descender.
        CHECK(column.ContentMin.y >= 0.0f);
        CHECK(column.ContentMax.y <= height + 8.0f);
        if (k + 1 < count)
            CHECK(column.ContentMax.y > height - 60.0f);
        overfull += column.Overfull ? 1 : 0;
    }
    CHECK(same(at, end));
    CHECK_EQ(overfull, 0);
    CHECK(!ImTypeset::GetDocumentColumn(document, count).Exists);
    CHECK(!ImTypeset::GetDocumentColumn(document, -1).Exists);

    // No column ends on a heading: the block a column ends before is never
    // the one after a heading.
    for (int k = 0; k + 1 < count; ++k) {
        const ImTypesetColumnGeometry column = ImTypeset::GetDocumentColumn(document, k);
        if (column.End.Offset != 0 || column.End.Block == 0)
            continue;
        CHECK(content.blocks[static_cast<size_t>(column.End.Block) - 1].Kind !=
              ImTypesetBlockKind_Heading);
    }
}

TEST(every_line_is_found_in_its_column_where_the_uncut_document_has_it)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    const Content content = prose(40, true);
    Shown document(content);
    CHECK(ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e9f));
    const std::vector<UncutLine> lines = uncut_lines(document);
    REQUIRE(lines.size() > 100);
    // What a point in the middle of each line hits, uncut.
    std::vector<ImTypesetDocumentHit> uncut_hits;
    for (const UncutLine& line : lines)
        uncut_hits.push_back(ImTypeset::HitTestDocument(
            document, ImVec2((line.min.x + line.max.x) * 0.5f, (line.min.y + line.max.y) * 0.5f)));

    for (int mode = 0; mode < 2; ++mode) {
        CHECK_CONTEXT("%s", mode == 0 ? "in order" : "balanced");
        if (mode == 0)
            in_order(document, 300.0f, 20.0f);
        else
            balanced(document, 4, 20.0f);
        const int count = ImTypeset::GetDocumentColumnCount(document);
        REQUIRE(count >= 4);
        std::vector<ImTypesetColumnGeometry> columns;
        for (int k = 0; k < count; ++k)
            columns.push_back(ImTypeset::GetDocumentColumn(document, k));
        int last_column = 0;
        int moved = 0;
        for (size_t i = 0; i < lines.size(); ++i) {
            const UncutLine& line = lines[i];
            CHECK_CONTEXT("block %d line %d", line.block, line.line);
            const ImVec2 middle((line.min.x + line.max.x) * 0.5f, (line.min.y + line.max.y) * 0.5f);
            const ImVec2 shown = ImTypeset::GetDocumentColumnPos(document, middle);
            // Which column it landed in, by its place on the page.
            int k = 0;
            while (k + 1 < count && shown.x >= columns[static_cast<size_t>(k) + 1].Min.x)
                ++k;
            const ImTypesetColumnGeometry& column = columns[static_cast<size_t>(k)];
            CHECK(shown.x >= column.Min.x && shown.x < column.Max.x);
            CHECK(shown.y >= column.ContentMin.y && shown.y < column.ContentMax.y);
            // Lines go down a column and then to the next, never back.
            CHECK(k >= last_column);
            moved += k > last_column ? 1 : 0;
            last_column = k;
            // The point hits what it hit uncut.
            const ImTypesetDocumentHit hit = ImTypeset::HitTestDocument(document, shown);
            CHECK(same(hit.Position, uncut_hits[i].Position));
            CHECK_EQ(hit.Line, uncut_hits[i].Line);
            CHECK_EQ(hit.Inside, uncut_hits[i].Inside);
            // And the line's first character is where its column says.
            const ImTypesetPosition first = {line.block, line.text_start};
            CHECK(!before(first, column.Start));
            CHECK(before(first, column.End));
            // The caret before it is drawn on that line of that column.
            const ImVec4 caret = ImTypeset::GetDocumentCaretRect(document, first);
            CHECK(caret.x >= column.Min.x - 1.0f && caret.x <= column.Max.x + 1.0f);
            CHECK(std::fabs((caret.y + caret.w) * 0.5f - shown.y) < 1.0f);
        }
        CHECK_EQ(moved, count - 1);
        CHECK_EQ(last_column, count - 1);
    }
}

TEST(a_drag_across_columns_selects_in_reading_order_and_copies_what_the_uncut_document_does)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    const Content content = prose(40);
    Shown document(content);
    in_order(document, 320.0f, 16.0f);
    const int count = ImTypeset::GetDocumentColumnCount(document);
    REQUIRE(count >= 4);
    const ImTypesetColumnGeometry first = ImTypeset::GetDocumentColumn(document, 0);
    const ImTypesetColumnGeometry second = ImTypeset::GetDocumentColumn(document, 1);
    const ImTypesetColumnGeometry third = ImTypeset::GetDocumentColumn(document, 2);

    // From the middle of column 0 to the middle of column 2.
    const ImVec2 a(first.Min.x + 100.0f, 150.0f);
    const ImVec2 b(third.Min.x + 100.0f, 150.0f);
    const ImTypesetDocumentRange range = ImTypeset::SelectDocumentRange(document, a, b);
    CHECK(before(range.Start, range.End));
    CHECK(same(range.Start, ImTypeset::HitTestDocument(document, a).Position));
    CHECK(same(range.End, ImTypeset::HitTestDocument(document, b).Position));
    // It holds the rest of column 0, all of column 1 and the head of column 2.
    CHECK(before(range.Start, first.End));
    CHECK(before(range.Start, second.Start));
    CHECK(!before(range.End, second.End));
    CHECK(!before(range.End, third.Start));
    // Dragged the other way it is the same range.
    const ImTypesetDocumentRange back = ImTypeset::SelectDocumentRange(document, b, a);
    CHECK(same(back.Start, range.Start) && same(back.End, range.End));

    // Its rectangles: every one in one column, some in each of the three,
    // and those of column 1 reach from its first line to its last.
    ImVector<ImVec4> rects;
    const int rect_count = ImTypeset::GetDocumentRangeRects(document, range, &rects);
    REQUIRE(rect_count > 10);
    int per_column[3] = {0, 0, 0};
    float second_top = 1.0e9f;
    float second_bottom = -1.0e9f;
    for (const ImVec4& rect : rects) {
        int k = -1;
        for (int c = 0; c < 3; ++c) {
            const ImTypesetColumnGeometry& column = c == 0 ? first : (c == 1 ? second : third);
            if (rect.x >= column.Min.x - 0.5f && rect.z <= column.Max.x + 0.5f)
                k = c;
        }
        if (!CHECK(k >= 0))
            continue;
        ++per_column[k];
        CHECK(rect.y >= 0.0f && rect.w <= 320.0f + 8.0f);
        if (k == 1) {
            second_top = std::min(second_top, rect.y);
            second_bottom = std::max(second_bottom, rect.w);
        }
    }
    CHECK(per_column[0] > 0 && per_column[1] > 0 && per_column[2] > 0);
    CHECK(std::fabs(second_top - second.ContentMin.y) < 1.0f);
    CHECK(std::fabs(second_bottom - second.ContentMax.y) < 6.0f);
    // In column 0 nothing is selected above the anchor, in column 2 nothing
    // below the active end.
    for (const ImVec4& rect : rects) {
        if (rect.z <= first.Max.x + 0.5f)
            CHECK(rect.w > a.y - 1.0f);
        if (rect.x >= third.Min.x - 0.5f)
            CHECK(rect.y < b.y + 1.0f);
    }

    // The copy, the word and line selections: the uncut document's.
    const std::string in_columns = copied(document, range.Start, range.End);
    const ImTypesetDocumentRange words =
        ImTypeset::SelectDocumentRange(document, a, b, ImTypesetGranularity_Word);
    const ImTypesetDocumentRange whole_lines =
        ImTypeset::SelectDocumentRange(document, a, b, ImTypesetGranularity_Line);
    CHECK(!before(range.Start, words.Start) && !before(words.End, range.End));
    CHECK(!before(words.Start, whole_lines.Start) && !before(whole_lines.End, words.End));
    const std::string words_in_columns = copied(document, words.Start, words.End);

    CHECK(ImTypeset::SetDocumentColumns(document, nullptr));
    CHECK_EQ(ImTypeset::GetDocumentColumnCount(document), 1);
    const std::string uncut = copied(document, range.Start, range.End);
    CHECK_EQ(in_columns, uncut);
    CHECK(in_columns.size() > 500);
    CHECK_EQ(words_in_columns, copied(document, words.Start, words.End));
    // The same rectangles, uncut: as many, each as large.
    ImVector<ImVec4> uncut_rects;
    CHECK_EQ(ImTypeset::GetDocumentRangeRects(document, range, &uncut_rects), rect_count);
    for (int i = 0; i < rect_count && i < uncut_rects.Size; ++i) {
        CHECK(std::fabs((rects[i].z - rects[i].x) - (uncut_rects[i].z - uncut_rects[i].x)) < 0.01f);
        CHECK(std::fabs((rects[i].w - rects[i].y) - (uncut_rects[i].w - uncut_rects[i].y)) < 0.01f);
    }
}

TEST(a_point_beside_a_columns_lines_is_the_nearest_of_that_column)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    const Content content = prose(40);
    Shown document(content);
    const float gutter = 30.0f;
    in_order(document, 320.0f, gutter);
    REQUIRE(ImTypeset::GetDocumentColumnCount(document) >= 4);
    const ImTypesetColumnGeometry first = ImTypeset::GetDocumentColumn(document, 0);
    const ImTypesetColumnGeometry second = ImTypeset::GetDocumentColumn(document, 1);

    // Below the last line of a column: that line, not the next column's first.
    const ImTypesetDocumentHit below =
        ImTypeset::HitTestDocument(document, ImVec2(first.Max.x - 1.0f, 5000.0f));
    CHECK(before(below.Position, first.End) || same(below.Position, first.End));
    CHECK(!before(below.Position, first.Start));
    CHECK(below.Position.Block == first.End.Block || below.Position.Block == first.End.Block - 1);
    // Above the first line of column 1: its start.
    const ImTypesetDocumentHit above =
        ImTypeset::HitTestDocument(document, ImVec2(second.Min.x, -5000.0f));
    CHECK(same(above.Position, second.Start));
    // In the gutter after column 0: column 0's line at that height, at its end.
    const ImTypesetDocumentHit in_gutter =
        ImTypeset::HitTestDocument(document, ImVec2(first.Max.x + gutter * 0.5f, 100.0f));
    const ImTypesetDocumentHit at_edge =
        ImTypeset::HitTestDocument(document, ImVec2(first.Max.x - 0.5f, 100.0f));
    CHECK(before(in_gutter.Position, first.End));
    CHECK_EQ(in_gutter.Position.Block, at_edge.Position.Block);
    CHECK(!before(in_gutter.Position, at_edge.Position));
    CHECK(in_gutter.Position.Offset - at_edge.Position.Offset <= 2);
    CHECK(!in_gutter.Inside);
    // Left of everything and right of everything.
    CHECK(before(ImTypeset::HitTestDocument(document, ImVec2(-1.0e6f, 10.0f)).Position, first.End));
    const int count = ImTypeset::GetDocumentColumnCount(document);
    const ImTypesetColumnGeometry last = ImTypeset::GetDocumentColumn(document, count - 1);
    const ImTypesetDocumentHit far = ImTypeset::HitTestDocument(document, ImVec2(1.0e7f, 10.0f));
    CHECK(!before(far.Position, last.Start));
    // Points that are not numbers are the page's corner.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(same(ImTypeset::HitTestDocument(document, ImVec2(nan, nan)).Position,
               ImTypesetPosition{0, 0}));
}

TEST(what_columns_draw_is_what_the_uncut_document_draws_each_column_in_its_rectangle)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    const Content content = prose(30, true);
    Shown document(content);
    const ImVec4 everything(-1.0e6f, -1.0e6f, 1.0e6f, 1.0e6f);
    Buffers uncut;
    REQUIRE(ImTypeset::DrawDocumentToBuffers(document, ImVec2(0.0f, 0.0f), everything,
                                             &uncut.vertices, &uncut.indices));
    REQUIRE(uncut.vertices.Size > 4000);

    for (int mode = 0; mode < 2; ++mode) {
        CHECK_CONTEXT("%s", mode == 0 ? "in order" : "balanced");
        const float gutter = 40.0f;
        if (mode == 0)
            in_order(document, 300.0f, gutter);
        else
            balanced(document, 3, gutter);
        const int count = ImTypeset::GetDocumentColumnCount(document);
        Buffers all;
        REQUIRE(ImTypeset::DrawDocumentToBuffers(document, ImVec2(0.0f, 0.0f), everything,
                                                 &all.vertices, &all.indices));
        // Nothing is lost at a cut and nothing drawn twice.
        CHECK_EQ(all.vertices.Size, uncut.vertices.Size);
        CHECK_EQ(all.indices.Size, uncut.indices.Size);
        // The same glyphs: the same texture coordinates, however placed.
        std::vector<std::pair<float, float>> a;
        std::vector<std::pair<float, float>> b;
        for (const ImDrawVert& v : uncut.vertices)
            a.push_back({v.uv.x, v.uv.y});
        for (const ImDrawVert& v : all.vertices)
            b.push_back({v.uv.x, v.uv.y});
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        CHECK(a == b);

        // Each column alone, under a clip that is its rectangle: what it
        // draws is inside it, and the columns together draw all of it.
        int total = 0;
        for (int k = 0; k < count; ++k) {
            CHECK_CONTEXT("column %d", k);
            const ImTypesetColumnGeometry column = ImTypeset::GetDocumentColumn(document, k);
            Buffers one;
            const ImVec4 clip(column.Min.x - gutter * 0.5f, -1.0e6f, column.Max.x + gutter * 0.5f,
                              1.0e6f);
            REQUIRE(ImTypeset::DrawDocumentToBuffers(document, ImVec2(0.0f, 0.0f), clip,
                                                     &one.vertices, &one.indices));
            total += one.vertices.Size;
            for (const ImDrawVert& v : one.vertices) {
                if (!(v.pos.x >= column.Min.x - 2.0f && v.pos.x <= column.Max.x + 2.0f &&
                      v.pos.y >= column.ContentMin.y - 4.0f &&
                      v.pos.y <= column.ContentMax.y + 4.0f)) {
                    FAIL("a vertex at (%g, %g) outside its column", static_cast<double>(v.pos.x),
                         static_cast<double>(v.pos.y));
                    break;
                }
            }
        }
        CHECK_EQ(total, uncut.vertices.Size);
    }

    // A selection and a mark over a cut are drawn on both sides of it.
    in_order(document, 300.0f, 40.0f);
    const ImTypesetColumnGeometry first = ImTypeset::GetDocumentColumn(document, 0);
    const ImTypesetColumnGeometry second = ImTypeset::GetDocumentColumn(document, 1);
    ImTypesetDocumentDraw draw;
    draw.Selection.Start = ImTypeset::HitTestDocument(document, ImVec2(50.0f, 250.0f)).Position;
    draw.Selection.End =
        ImTypeset::HitTestDocument(document, ImVec2(second.Min.x + 50.0f, 60.0f)).Position;
    ImTypesetMark mark;
    mark.Range = draw.Selection;
    mark.Place = ImTypesetMarkPlace_Behind;
    mark.Color = IM_COL32(1, 2, 3, 255);
    draw.Marks = &mark;
    draw.MarkCount = 1;
    Buffers with;
    REQUIRE(ImTypeset::DrawDocumentToBuffers(document, ImVec2(0.0f, 0.0f), everything,
                                             &with.vertices, &with.indices, &draw));
    int marked[2] = {0, 0};
    for (const ImDrawVert& v : with.vertices) {
        if (v.col != IM_COL32(1, 2, 3, 255))
            continue;
        ++marked[v.pos.x > first.Max.x + 1.0f ? 1 : 0];
        if (v.pos.x <= first.Max.x + 1.0f)
            CHECK(v.pos.y > 230.0f);
        else
            CHECK(v.pos.y < 90.0f && v.pos.x < second.Max.x + 1.0f);
    }
    CHECK(marked[0] >= 4 && marked[1] >= 4);
    CHECK(with.vertices.Size > uncut.vertices.Size);
}

TEST(what_cannot_be_cut_and_is_taller_than_a_column_stands_alone_and_says_so)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    Content content;
    ImTypesetBlockStyle body;
    body.SpaceBelow = 8.0f;
    content.styles.push_back(body);
    for (int i = 0; i < 6; ++i)
        content.add(ImTypesetBlockKind_Paragraph, SENTENCE);
    content.add(ImTypesetBlockKind_Host, "[a widget]").Extent = 500.0f; // block 6
    for (int i = 0; i < 6; ++i)
        content.add(ImTypesetBlockKind_Paragraph, SENTENCE);
    // A code block and a table, each taller than a column: both are cut
    // between their lines and their rows.
    std::string code;
    for (int i = 0; i < 40; ++i)
        code += "line " + std::to_string(i) + "\n";
    content.add(ImTypesetBlockKind_Code, code); // block 13
    content.table(40, 3);                       // block 14
    Shown document(content);
    const float height = 200.0f;
    in_order(document, height, 10.0f);
    CHECK(ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e9f));
    const int count = ImTypeset::GetDocumentColumnCount(document);
    REQUIRE(count > 8);

    int overfull = 0;
    int host_column = -1;
    int code_columns = 0;
    int table_columns = 0;
    for (int k = 0; k < count; ++k) {
        CHECK_CONTEXT("column %d", k);
        const ImTypesetColumnGeometry column = ImTypeset::GetDocumentColumn(document, k);
        if (column.Overfull) {
            ++overfull;
            host_column = k;
            // The host block alone, reaching past the foot.
            CHECK_EQ(column.Start.Block, 6);
            CHECK_EQ(column.End.Block, 7);
            CHECK(column.ContentMax.y >= 500.0f);
            CHECK(column.ContentMax.y > column.Max.y);
        } else {
            CHECK(column.ContentMax.y <= height + 8.0f);
        }
        code_columns += column.Start.Block <= 13 && column.End.Block >= 13 &&
                                !(column.End.Block == 13 && column.End.Offset == 0)
                            ? 1
                            : 0;
        table_columns += column.End.Block == 14 || column.Start.Block == 14 ? 1 : 0;
    }
    CHECK_EQ(overfull, 1);
    REQUIRE(host_column > 0);
    CHECK(code_columns >= 3);
    CHECK(table_columns >= 3);

    // Where a host draws its widget: the block's corner, taken to the page.
    const ImTypesetBlockGeometry host = ImTypeset::GetDocumentBlockGeometry(document, 6);
    const ImVec2 corner = ImTypeset::GetDocumentColumnPos(document, host.FullMin);
    const ImTypesetColumnGeometry holder = ImTypeset::GetDocumentColumn(document, host_column);
    CHECK_NEAR(corner.x, holder.Min.x, 0.001);
    CHECK_NEAR(corner.y, holder.ContentMin.y, 0.001);
    // A point in it hits it.
    const ImTypesetDocumentHit hit =
        ImTypeset::HitTestDocument(document, ImVec2(corner.x + 5.0f, corner.y + 20.0f));
    CHECK_EQ(hit.Position.Block, 6);

    // A row of the table in a later column: the hit names its row and column,
    // and the row's place in the uncut column goes to that page column.
    const ImTypesetTableGeometry table = ImTypeset::GetDocumentTableGeometry(document, 14);
    REQUIRE_EQ(table.RowCount, 40);
    const ImTypesetTableRowGeometry row = ImTypeset::GetDocumentTableRow(document, 14, 30);
    const ImTypesetTableColumnGeometry cell_column =
        ImTypeset::GetDocumentTableColumn(document, 14, 1);
    const ImVec2 in_cell = ImTypeset::GetDocumentColumnPos(
        document, ImVec2((cell_column.Min + cell_column.Max) * 0.5f, (row.Min + row.Max) * 0.5f));
    const ImTypesetDocumentHit cell = ImTypeset::HitTestDocument(document, in_cell);
    CHECK_EQ(cell.Position.Block, 14);
    CHECK_EQ(cell.Row, 30);
    CHECK_EQ(cell.Column, 1);
    CHECK(in_cell.x > holder.Max.x);
    CHECK(in_cell.y >= 0.0f && in_cell.y < height + 8.0f);

    // Balanced, the page is as tall as what cannot be cut.
    balanced(document, 4, 10.0f);
    CHECK_EQ(ImTypeset::GetDocumentColumnCount(document), 4);
    CHECK(ImTypeset::GetDocumentSize(document).y >= 500.0f);
    int tall = 0;
    for (int k = 0; k < 4; ++k)
        tall += ImTypeset::GetDocumentColumn(document, k).ContentMax.y >= 500.0f ? 1 : 0;
    CHECK(tall >= 1);
}

TEST(ten_thousand_blocks_in_columns_cost_what_the_columns_asked_for_hold)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    const Content content = prose(10000);
    Shown document(content, 300.0f);
    const ImTypesetDocumentStats start = ImTypeset::GetDocumentStats(document);
    CHECK_EQ(start.BlocksLaidOut, ImU64(0));

    // IN ORDER. A frame that shows the first three columns.
    const float height = 600.0f;
    const float gutter = 20.0f;
    in_order(document, height, gutter);
    Buffers buffers;
    const ImVec4 three(0.0f, 0.0f, 3.0f * 300.0f + 2.0f * gutter, height);
    REQUIRE(ImTypeset::DrawDocumentToBuffers(document, ImVec2(0.0f, 0.0f), three, &buffers.vertices,
                                             &buffers.indices));
    const ImTypesetDocumentStats first = ImTypeset::GetDocumentStats(document);
    const int held = ImTypeset::GetDocumentColumn(document, 2).End.Block + 1;
    std::printf("columns, 10,000 blocks, in order: a frame of 3 columns laid out %llu blocks "
                "(the columns hold %d), cut %llu columns; %d columns estimated\n",
                static_cast<unsigned long long>(first.BlocksLaidOut), held,
                static_cast<unsigned long long>(first.ColumnsCut),
                ImTypeset::GetDocumentColumnCount(document));
    CHECK(first.BlocksLaidOut >= ImU64(held) - 1);
    CHECK(first.BlocksLaidOut <= ImU64(held) + 12);
    CHECK(first.BlocksLaidOut < 120);
    CHECK(first.ColumnsCut <= 5);
    CHECK_EQ(first.BalanceHeightsTried, ImU64(0));
    CHECK_EQ(first.BlocksDeferred, 10000 - static_cast<int>(first.BlocksLaidOut));
    CHECK(buffers.vertices.Size > 1000);
    // The next frame costs nothing, and neither do the questions a view asks.
    buffers.vertices.resize(0);
    buffers.indices.resize(0);
    REQUIRE(ImTypeset::DrawDocumentToBuffers(document, ImVec2(0.0f, 0.0f), three, &buffers.vertices,
                                             &buffers.indices));
    ImTypeset::HitTestDocument(document, ImVec2(400.0f, 300.0f));
    const int estimated = ImTypeset::GetDocumentColumnCount(document);
    ImTypeset::GetDocumentSize(document);
    const ImTypesetDocumentStats second = ImTypeset::GetDocumentStats(document);
    CHECK_EQ(second.BlocksLaidOut, first.BlocksLaidOut);
    CHECK_EQ(second.ColumnsCut, first.ColumnsCut);
    CHECK(estimated > 500);

    // Scrolled ten columns to the right: the columns passed over are cut too.
    const float pitch = 300.0f + gutter;
    const ImVec4 further(10.0f * pitch, 0.0f, 13.0f * pitch, height);
    REQUIRE(ImTypeset::DrawDocumentToBuffers(document, ImVec2(0.0f, 0.0f), further,
                                             &buffers.vertices, &buffers.indices));
    const ImTypesetDocumentStats third = ImTypeset::GetDocumentStats(document);
    const int held_further = ImTypeset::GetDocumentColumn(document, 12).End.Block + 1;
    std::printf("columns: a frame of columns 10 to 12 brought it to %llu blocks laid out (they "
                "end at block %d), %llu columns cut\n",
                static_cast<unsigned long long>(third.BlocksLaidOut), held_further,
                static_cast<unsigned long long>(third.ColumnsCut));
    CHECK(third.BlocksLaidOut <= ImU64(held_further) + 12);
    CHECK(third.BlocksLaidOut > second.BlocksLaidOut);
    CHECK(third.ColumnsCut >= 13 && third.ColumnsCut <= 15);

    // Text arriving in the last block lays nothing out until it is shown.
    CHECK(ImTypeset::AppendDocumentText(document, " and more"));
    REQUIRE(ImTypeset::DrawDocumentToBuffers(document, ImVec2(0.0f, 0.0f), three, &buffers.vertices,
                                             &buffers.indices));
    const ImTypesetDocumentStats fourth = ImTypeset::GetDocumentStats(document);
    CHECK_EQ(fourth.BlocksLaidOut, third.BlocksLaidOut);
    CHECK_EQ(fourth.ColumnsCut, third.ColumnsCut);

    // The last column costs all of it.
    CHECK(ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e9f));
    const ImTypesetDocumentStats all = ImTypeset::GetDocumentStats(document);
    const int count = ImTypeset::GetDocumentColumnCount(document);
    std::printf("columns: all of it is %d columns (%d estimated after three), %llu blocks laid "
                "out, %llu columns cut\n",
                count, estimated, static_cast<unsigned long long>(all.BlocksLaidOut),
                static_cast<unsigned long long>(all.ColumnsCut));
    CHECK_EQ(all.BlocksLaidOut, ImU64(10000));
    CHECK_EQ(all.BlocksDeferred, 0);
    CHECK(std::abs(count - estimated) < estimated / 4);
    // Text arriving at the end then cuts the last column again, and no other.
    CHECK(ImTypeset::AppendDocumentText(document, " and more"));
    CHECK(ImTypeset::GetDocumentColumn(document, count - 1).Exists);
    const ImTypesetDocumentStats appended = ImTypeset::GetDocumentStats(document);
    CHECK_EQ(appended.BlocksLaidOut, ImU64(10001));
    CHECK(appended.ColumnsCut - all.ColumnsCut <= 2);

    // Shown uncut again and in columns again: nothing is laid out for either.
    CHECK(ImTypeset::SetDocumentColumns(document, nullptr));
    REQUIRE(ImTypeset::DrawDocumentToBuffers(document, ImVec2(0.0f, 0.0f), three, &buffers.vertices,
                                             &buffers.indices));
    CHECK_EQ(ImTypeset::GetDocumentStats(document).BlocksLaidOut, ImU64(10001));

    // BALANCED needs every height: a fresh document, one question.
    Shown other(content, 300.0f);
    balanced(other, 6, gutter);
    CHECK_EQ(ImTypeset::GetDocumentColumnCount(other), 6);
    const ImTypesetDocumentStats level = ImTypeset::GetDocumentStats(other);
    std::printf("columns, 10,000 blocks, balanced into 6: %llu blocks laid out by the first "
                "question, %llu heights tried, %llu columns cut; the page is %g px tall\n",
                static_cast<unsigned long long>(level.BlocksLaidOut),
                static_cast<unsigned long long>(level.BalanceHeightsTried),
                static_cast<unsigned long long>(level.ColumnsCut),
                static_cast<double>(ImTypeset::GetDocumentSize(other).y));
    CHECK_EQ(level.BlocksLaidOut, ImU64(10000));
    CHECK(level.BalanceHeightsTried > 10 && level.BalanceHeightsTried < 64);
    CHECK_EQ(level.ColumnsCut, ImU64(6));
    float tallest = 0.0f;
    float shortest = 1.0e9f;
    for (int k = 0; k < 6; ++k) {
        const ImTypesetColumnGeometry column = ImTypeset::GetDocumentColumn(other, k);
        CHECK(column.Exists && !column.Overfull);
        tallest = std::max(tallest, column.ContentMax.y);
        if (k < 5)
            shortest = std::min(shortest, column.ContentMax.y);
    }
    CHECK_EQ(std::ceil(tallest), ImTypeset::GetDocumentSize(other).y);
    CHECK(tallest - shortest < 60.0f);
    // An edit searches again and lays out the one block.
    CHECK(ImTypeset::AppendDocumentText(other, " and more"));
    CHECK_EQ(ImTypeset::GetDocumentColumnCount(other), 6);
    ImTypeset::GetDocumentSize(other);
    const ImTypesetDocumentStats again = ImTypeset::GetDocumentStats(other);
    CHECK_EQ(again.BlocksLaidOut, ImU64(10001));
    CHECK(again.BalanceHeightsTried > level.BalanceHeightsTried);
}

TEST(one_document_is_shown_in_columns_at_any_width_and_uncut_again)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    const Content content = prose(50);
    Shown document(content, 400.0f);
    const ImTypesetPosition somewhere = {17, 40};
    CHECK(ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e9f));
    const ImTypesetDocumentRange range = {{3, 10}, {31, 5}};
    const std::string text = copied(document, range.Start, range.End);

    int last_count = 0;
    for (const float width : {400.0f, 250.0f, 180.0f}) {
        CHECK_CONTEXT("width %g", static_cast<double>(width));
        CHECK(ImTypeset::LayoutDocument(document, width));
        in_order(document, 500.0f);
        const int count = ImTypeset::GetDocumentColumnCount(document);
        CHECK(ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e9f));
        const int exact = ImTypeset::GetDocumentColumnCount(document);
        // Narrower columns are more columns.
        CHECK(exact > last_count);
        last_count = exact;
        CHECK(count >= 1);
        // The default gutter is one line of the style.
        const ImTypesetColumnGeometry second = ImTypeset::GetDocumentColumn(document, 1);
        CHECK(second.Min.x > width + 10.0f && second.Min.x < width + 40.0f);
        // A position means what it meant: the caret is in the column that
        // holds it, and a copy is the same text.
        const ImVec4 caret = ImTypeset::GetDocumentCaretRect(document, somewhere);
        bool found = false;
        for (int k = 0; k < exact && !found; ++k) {
            const ImTypesetColumnGeometry column = ImTypeset::GetDocumentColumn(document, k);
            if (before(somewhere, column.Start) || !before(somewhere, column.End))
                continue;
            found = true;
            CHECK(caret.x >= column.Min.x && caret.x <= column.Max.x);
            CHECK(caret.y >= column.ContentMin.y - 1.0f && caret.w <= column.ContentMax.y + 1.0f);
            const ImTypesetDocumentHit hit = ImTypeset::HitTestDocument(
                document, ImVec2(caret.x + 0.5f, (caret.y + caret.w) * 0.5f));
            CHECK_EQ(hit.Position.Block, somewhere.Block);
            CHECK(std::abs(hit.Position.Offset - somewhere.Offset) <= 1);
        }
        CHECK(found);
        CHECK_EQ(copied(document, range.Start, range.End), text);
        // A gutter of none, and one stated.
        in_order(document, 500.0f, -1.0f);
        CHECK_NEAR(ImTypeset::GetDocumentColumn(document, 1).Min.x, width, 0.001);
        in_order(document, 500.0f, 33.0f);
        CHECK_NEAR(ImTypeset::GetDocumentColumn(document, 1).Min.x, width + 33.0f, 0.001);
    }
    CHECK(ImTypeset::SetDocumentColumns(document, nullptr));
    CHECK_EQ(ImTypeset::GetDocumentColumnCount(document), 1);
    CHECK_EQ(ImTypeset::GetDocumentSize(document).x, 180.0f);
    CHECK_EQ(copied(document, range.Start, range.End), text);
}

TEST(columns_nobody_vetted_are_cut_to_what_can_be_shown)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    const Content content = prose(20);
    Shown document(content);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    Buffers buffers;
    const ImVec4 clip(0.0f, 0.0f, 2000.0f, 2000.0f);

    CHECK(!ImTypeset::SetDocumentColumns(nullptr, nullptr));
    CHECK_EQ(ImTypeset::GetDocumentColumnCount(nullptr), 1);
    CHECK(!ImTypeset::GetDocumentColumn(nullptr, 0).Exists);

    struct Case {
        int count;
        float height;
        float gutter;
        int flags;
        bool several; // whether there is then more than one column
    };
    const Case cases[] = {
        {0, 0.0f, 0.0f, 0, false},
        {5, 0.0f, 0.0f, 0, false}, // no height and no view to lend one
        {1, 0.0f, 0.0f, ImTypesetColumnFlags_Balance, false},
        {-7, -50.0f, 0.0f, ImTypesetColumnFlags_Balance, false},
        {0, nan, nan, 0, false},
        {0, inf, inf, 0, false},
        {0, 1.0e-30f, -inf, 0, true},    // a column of one scaled point: a line each
        {0, 1.0e30f, 1.0e30f, 0, false}, // one column taller than anything: all of it
        {2000000000, 0.0f, 1.0e30f, ImTypesetColumnFlags_Balance, true},
        {3, nan, -5.0f, ImTypesetColumnFlags_Balance | 0x7FFFFFF0, true},
    };
    for (const Case& c : cases) {
        CHECK_CONTEXT("count %d height %g gutter %g flags %d", c.count,
                      static_cast<double>(c.height), static_cast<double>(c.gutter), c.flags);
        ImTypesetColumns columns;
        columns.Count = c.count;
        columns.Height = c.height;
        columns.Gutter = c.gutter;
        columns.Flags = c.flags;
        CHECK(ImTypeset::SetDocumentColumns(document, &columns));
        const int count = ImTypeset::GetDocumentColumnCount(document);
        CHECK(count >= 1);
        CHECK_EQ(count > 1, c.several);
        CHECK(ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e9f));
        const ImVec2 size = ImTypeset::GetDocumentSize(document);
        CHECK(std::isfinite(size.x) && std::isfinite(size.y));
        buffers.vertices.resize(0);
        buffers.indices.resize(0);
        CHECK(ImTypeset::DrawDocumentToBuffers(document, ImVec2(0.0f, 0.0f), clip,
                                               &buffers.vertices, &buffers.indices));
        CHECK(buffers.vertices.Size > 0);
        ImTypeset::HitTestDocument(document, ImVec2(inf, -inf));
        ImTypeset::HitTestDocument(document, ImVec2(1.0e30f, 1.0e30f));
        ImTypeset::GetDocumentColumnPos(document, ImVec2(nan, inf));
        const int exact = ImTypeset::GetDocumentColumnCount(document);
        CHECK(ImTypeset::GetDocumentColumn(document, exact - 1).Exists);
        CHECK(!ImTypeset::GetDocumentColumn(document, exact).Exists);
        CHECK(!ImTypeset::GetDocumentColumn(document, 0x7FFFFFFF).Exists);
    }

    // A document of nothing, in columns.
    Content nothing;
    Shown empty(nothing);
    in_order(empty, 100.0f);
    CHECK_EQ(ImTypeset::GetDocumentColumnCount(empty), 1);
    CHECK(!ImTypeset::GetDocumentColumn(empty, 0).Exists);
    CHECK(ImTypeset::DrawDocumentToBuffers(empty, ImVec2(0.0f, 0.0f), clip, &buffers.vertices,
                                           &buffers.indices));
    ImTypeset::HitTestDocument(empty, ImVec2(5.0f, 5.0f));
    balanced(empty, 3);
    CHECK_EQ(ImTypeset::GetDocumentColumnCount(empty), 3);
    CHECK(ImTypeset::GetDocumentColumn(empty, 2).Exists);
    CHECK_EQ(ImTypeset::GetDocumentSize(empty).y, 0.0f);
}

TEST(the_view_shows_columns_across_its_width_and_selects_across_them)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const Content content = prose(200);
    Shown document(content);
    ImTypesetColumns columns;
    columns.Count = 3;
    columns.Gutter = 20.0f;
    CHECK(ImTypeset::SetDocumentColumns(document, &columns));
    ImGuiIO& io = ImGui::GetIO();
    ImTypesetDocumentRange selection = {{0, 0}, {0, 0}};
    int drawn = 0;
    for (int f = 0; f < 6; ++f) {
        // A drag from the first column into the third.
        io.AddMousePosEvent(f < 2 ? 60.0f : 560.0f, f < 2 ? 200.0f : 120.0f);
        if (f == 1)
            io.AddMouseButtonEvent(0, true);
        if (f == 4)
            io.AddMouseButtonEvent(0, false);
        Frame frame;
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(700.0f, 500.0f));
        ImGui::Begin("columns", nullptr, ImGuiWindowFlags_NoDecoration);
        CHECK(ImTypeset::DocumentView("##view", document, ImVec2(660.0f, 420.0f), &selection));
        drawn = ImGui::GetWindowDrawList()->VtxBuffer.Size;
        ImGui::End();
    }
    (void)drawn;
    // Three columns across what the view has: each a third of it less the
    // gutters, as tall as the view, and the rest to the right.
    const ImTypesetColumnGeometry first = ImTypeset::GetDocumentColumn(document, 0);
    const ImTypesetColumnGeometry third = ImTypeset::GetDocumentColumn(document, 2);
    const float width = first.Max.x - first.Min.x;
    CHECK(width > 180.0f && width < 215.0f);
    CHECK(third.Max.x <= 660.0f);
    CHECK(first.Max.y > 350.0f && first.Max.y <= 420.0f);
    CHECK(ImTypeset::GetDocumentColumnCount(document) > 10);
    const ImTypesetDocumentStats stats = ImTypeset::GetDocumentStats(document);
    CHECK(stats.BlocksLaidOut < 60);
    // The drag selected from column 0 into column 2, through all of column 1.
    const ImTypesetColumnGeometry second = ImTypeset::GetDocumentColumn(document, 1);
    CHECK(before(selection.Start, first.End));
    CHECK(!before(selection.End, third.Start));
    CHECK(before(selection.Start, second.Start) && !before(selection.End, second.End));

    // Balanced in a view: Count columns, scrolled down.
    columns.Flags = ImTypesetColumnFlags_Balance;
    CHECK(ImTypeset::SetDocumentColumns(document, &columns));
    for (int f = 0; f < 2; ++f) {
        Frame frame;
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(700.0f, 500.0f));
        ImGui::Begin("columns", nullptr, ImGuiWindowFlags_NoDecoration);
        CHECK(ImTypeset::DocumentView("##view", document, ImVec2(660.0f, 420.0f), &selection));
        ImGui::End();
    }
    CHECK_EQ(ImTypeset::GetDocumentColumnCount(document), 3);
    CHECK(ImTypeset::GetDocumentSize(document).y > 2000.0f);
}
