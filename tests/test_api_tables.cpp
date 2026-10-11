// Tables through the public Document: what a host describes (cells as
// stretches of the block's text, columns, a header), and what it can then ask
// (geometry, the cell at a position, hits, selections, marks, copies), a table
// that grows a row at a time with its columns held, and hostile descriptions.
//
// How a table is SET is held to the reference elsewhere: tests/test_align.cpp
// replays tests/golden/tables against the core, and
// tests/test_api_tables_golden.cpp replays the goldens through the calls used
// here. This file is what the reference has no word for: the public
// description, positions as (block, byte), the work counters, the limits.

#include "golden.h"

#include "imtypeset.h"

#include "imtypeset_document.h"

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

// A document's content under construction. A table's text is its cells joined
// by a tab and its rows by a newline: what a copy of it gives.
struct Content {
    std::string text;
    std::vector<ImTypesetBlock> blocks;
    std::vector<ImTypesetRun> runs;
    std::vector<ImTypesetCell> cells;
    std::vector<ImTypesetTableColumn> columns;
    std::vector<ImTypesetBlockStyle> styles;

    ImTypesetBlock& paragraph(const std::string& body)
    {
        ImTypesetBlock block;
        block.TextStart = static_cast<int>(text.size());
        text += body;
        block.TextEnd = static_cast<int>(text.size());
        block.RunStart = static_cast<int>(runs.size());
        blocks.push_back(block);
        return blocks.back();
    }
    ImTypesetBlock& table(const std::vector<std::vector<std::string>>& rows,
                          ImTypesetBlockFlags flags = ImTypesetBlockFlags_TableHeader)
    {
        ImTypesetBlock block;
        block.Kind = ImTypesetBlockKind_Table;
        block.Flags = flags;
        block.TextStart = static_cast<int>(text.size());
        block.CellStart = static_cast<int>(cells.size());
        block.ColumnStart = static_cast<int>(columns.size());
        block.RunStart = static_cast<int>(runs.size());
        const size_t base = text.size();
        for (size_t r = 0; r < rows.size(); ++r) {
            if (r != 0)
                text += '\n';
            for (size_t c = 0; c < rows[r].size(); ++c) {
                if (c != 0)
                    text += '\t';
                ImTypesetCell cell;
                cell.TextStart = static_cast<int>(text.size() - base);
                text += rows[r][c];
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
    ImTypesetTableColumn& column(ImTypesetAlign align = ImTypesetAlign_Left)
    {
        ImTypesetTableColumn c;
        c.Align = align;
        columns.push_back(c);
        ++blocks.back().ColumnCount;
        return columns.back();
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
    ImTypesetDocumentContent view() const
    {
        ImTypesetDocumentContent out;
        out.Text = text.data();
        out.TextSize = static_cast<int>(text.size());
        out.Blocks = blocks.data();
        out.BlockCount = static_cast<int>(blocks.size());
        out.Runs = runs.data();
        out.RunCount = static_cast<int>(runs.size());
        out.Cells = cells.data();
        out.CellCount = static_cast<int>(cells.size());
        out.Columns = columns.data();
        out.ColumnCount = static_cast<int>(columns.size());
        out.Styles = styles.data();
        out.StyleCount = static_cast<int>(styles.size());
        return out;
    }
};

ImTypesetDocument* document_of(const Content& content, float width = 600.0f, bool expect_ok = true)
{
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    if (document == nullptr)
        return nullptr;
    const ImTypesetDocumentContent view = content.view();
    const bool ok = ImTypeset::SetDocumentBlocks(document, &view);
    if (expect_ok && !ok)
        FAIL("SetDocumentBlocks: %s", ImTypeset::GetLastError()->Message);
    ImTypeset::LayoutDocument(document, width);
    return document;
}

std::string copied(ImTypesetDocument* document, ImTypesetPosition start, ImTypesetPosition end)
{
    ImGuiTextBuffer buffer;
    const ImTypesetDocumentRange range = {start, end};
    ImTypeset::AppendDocumentRangeText(document, range, &buffer);
    return std::string(buffer.c_str());
}

std::string whole(ImTypesetDocument* document, int block)
{
    return copied(document, ImTypesetPosition{block, 0},
                  ImTypesetPosition{block, ImTypeset::GetDocumentBlockTextSize(document, block)});
}

ImTypesetPosition at(ImTypesetDocument* document, int block, int row, int column, int offset = 0)
{
    return ImTypeset::GetDocumentTablePosition(document, block, row, column, offset);
}

// The middle of a cell's first line, in document coordinates.
ImVec2 centre_of(ImTypesetDocument* document, int block, int row, int column)
{
    const ImTypesetTableColumnGeometry c =
        ImTypeset::GetDocumentTableColumn(document, block, column);
    const ImTypesetTableRowGeometry r = ImTypeset::GetDocumentTableRow(document, block, row);
    return ImVec2((c.Min + c.Max) * 0.5f, r.Min + 6.0f);
}

int vertices_of(ImTypesetDocument* document, const ImTypesetDocumentDraw* draw = nullptr)
{
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    ImTypeset::DrawDocumentToBuffers(document, ImVec2(0.0f, 0.0f),
                                     ImVec4(-100.0f, -100.0f, 4000.0f, 100000.0f), &vertices,
                                     &indices, draw);
    return vertices.Size;
}

const std::vector<std::vector<std::string>> UNITS = {
    {"Quantity", "Unit", "Note"},
    {"mass", "kg", "the SI base unit"},
    {"length", "m", ""},
    {"time", "s", "caesium"},
};

} // namespace

// --- Description and geometry ---------------------------------------------------------------

TEST(a_table_is_laid_out_with_its_rows_columns_and_rule)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Content content;
    content.paragraph("Before the table.");
    content.table(UNITS);
    content.column();
    content.column(ImTypesetAlign_Center);
    content.column(ImTypesetAlign_Right);
    content.paragraph("After it.");
    ImTypesetDocument* document = document_of(content);
    REQUIRE(document != nullptr);
    REQUIRE(ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e6f));

    const ImTypesetTableGeometry table = ImTypeset::GetDocumentTableGeometry(document, 1);
    CHECK(table.LaidOut);
    CHECK_EQ(table.RowCount, 4);
    CHECK_EQ(table.ColumnCount, 3);
    CHECK_EQ(table.CellCount, 12);
    CHECK(!table.Stated);
    // It fits, so it is scaled up to the document's width exactly.
    CHECK_EQ(table.Min.x, 0.0f);
    CHECK_NEAR(table.Max.x, 600.0f, 1.0e-3f);
    const ImTypesetBlockGeometry block = ImTypeset::GetDocumentBlockGeometry(document, 1);
    CHECK_EQ(table.Min.y, block.FullMin.y);
    CHECK_EQ(table.Max.y, block.FullMax.y);
    CHECK(block.FullMin.y >= ImTypeset::GetDocumentBlockGeometry(document, 0).FullMax.y);
    CHECK(ImTypeset::GetDocumentBlockGeometry(document, 2).FullMin.y >= block.FullMax.y);

    // Columns tile the table; each is at least what its cells want.
    float edge = table.Min.x;
    for (int c = 0; c < 3; ++c) {
        CHECK_CONTEXT("column %d", c);
        const ImTypesetTableColumnGeometry column =
            ImTypeset::GetDocumentTableColumn(document, 1, c);
        CHECK_EQ(column.Min, edge);
        CHECK(column.Max - column.Min >= column.Natural);
        CHECK(column.Natural >= column.Floor);
        CHECK(column.Floor > 0.0f);
        edge = column.Max;
    }
    CHECK_EQ(edge, table.Max.x);
    // Rows go down the table and do not overlap; the rule lies under row 0.
    float bottom = table.Min.y;
    for (int r = 0; r < 4; ++r) {
        CHECK_CONTEXT("row %d", r);
        const ImTypesetTableRowGeometry row = ImTypeset::GetDocumentTableRow(document, 1, r);
        CHECK(row.Min >= bottom);
        CHECK(row.Max > row.Min);
        CHECK_EQ(row.CellStart, 3 * r);
        CHECK_EQ(row.CellCount, 3);
        CHECK(!row.Wrapped);
        if (r == 1)
            CHECK(row.Min > bottom); // the rule's thickness
        bottom = row.Max;
    }
    CHECK_EQ(bottom, table.Max.y);
    // Not a table, not a column, not a row.
    CHECK_EQ(ImTypeset::GetDocumentTableGeometry(document, 0).RowCount, 0);
    CHECK_EQ(ImTypeset::GetDocumentTableColumn(document, 1, 3).Max, 0.0f);
    CHECK_EQ(ImTypeset::GetDocumentTableRow(document, 1, 4).Max, 0.0f);
    CHECK_EQ(ImTypeset::GetDocumentTableRow(document, 1, -1).Max, 0.0f);

    // It draws: glyphs and one rule.
    Frame frame;
    CHECK(vertices_of(document) > 4 * 40);
    ImTypeset::DestroyDocument(document);
}

TEST(a_copy_of_a_table_is_its_cells_with_tabs_and_its_rows_with_newlines)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Content content;
    content.paragraph("Before the table.");
    content.table(UNITS);
    content.paragraph("After it.");
    ImTypesetDocument* document = document_of(content);
    REQUIRE(document != nullptr);

    const std::string table = "Quantity\tUnit\tNote\nmass\tkg\tthe SI base unit\nlength\tm\t\n"
                              "time\ts\tcaesium";
    CHECK_EQ(whole(document, 1), table);
    // The block's text, as given, is the same string.
    CHECK_EQ(ImTypeset::GetDocumentBlockTextSize(document, 1), static_cast<int>(table.size()));
    // Across its edges: the block separator, once each side.
    CHECK_EQ(copied(document, ImTypesetPosition{0, 0}, ImTypesetPosition{2, 9}),
             "Before the table.\n\n" + table + "\n\nAfter it.");
    // From the middle of one cell to the middle of another, in reading order.
    CHECK_EQ(copied(document, at(document, 1, 1, 0, 2), at(document, 1, 1, 2, 3)), "ss\tkg\tthe");
    CHECK_EQ(copied(document, at(document, 1, 1, 1, 0), at(document, 1, 2, 0, 3)),
             "kg\tthe SI base unit\nlen");
    // An empty cell at either end of a range keeps its tab; a range that ends
    // at the very start of a cell does not reach it.
    CHECK_EQ(copied(document, at(document, 1, 2, 1, 0), at(document, 1, 2, 2, 0)), "m\t");
    CHECK_EQ(copied(document, at(document, 1, 2, 2, 0), at(document, 1, 3, 0, 2)), "\nti");
    CHECK_EQ(copied(document, at(document, 1, 1, 0, 0), at(document, 1, 1, 1, 0)), "mass");
    // One cell whole, and an empty range.
    CHECK_EQ(copied(document, at(document, 1, 3, 2, 0), at(document, 1, 3, 2, 99)), "caesium");
    CHECK_EQ(copied(document, at(document, 1, 1, 1, 1), at(document, 1, 1, 1, 1)), "");
    ImTypeset::DestroyDocument(document);
}

TEST(a_position_names_its_cell_and_a_cell_its_position)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Content content;
    content.table(UNITS);
    ImTypesetDocument* document = document_of(content);
    REQUIRE(document != nullptr);
    int cell = 0;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 3; ++c, ++cell) {
            CHECK_CONTEXT("row %d column %d", r, c);
            const int length =
                static_cast<int>(UNITS[static_cast<size_t>(r)][static_cast<size_t>(c)].size());
            for (int offset = 0; offset <= length; ++offset) {
                const ImTypesetPosition position = at(document, 0, r, c, offset);
                const ImTypesetCellPosition back =
                    ImTypeset::GetDocumentTableCell(document, position);
                CHECK_EQ(back.Row, r);
                CHECK_EQ(back.Column, c);
                CHECK_EQ(back.Cell, cell);
                CHECK_EQ(back.Offset, offset);
            }
            // Past the cell's end is its end.
            CHECK_EQ(at(document, 0, r, c, 1000).Offset, at(document, 0, r, c, length).Offset);
            // The caret at a cell's end stands in that cell's row, right of its start.
            const ImVec4 first =
                ImTypeset::GetDocumentCaretRect(document, at(document, 0, r, c, 0));
            const ImVec4 last =
                ImTypeset::GetDocumentCaretRect(document, at(document, 0, r, c, length));
            const ImTypesetTableRowGeometry row = ImTypeset::GetDocumentTableRow(document, 0, r);
            const ImTypesetTableColumnGeometry column =
                ImTypeset::GetDocumentTableColumn(document, 0, c);
            CHECK(first.y >= row.Min - 0.01f && first.w <= row.Max + 0.01f);
            CHECK(last.y >= row.Min - 0.01f && last.w <= row.Max + 0.01f);
            CHECK(first.x >= column.Min - 0.01f && last.x <= column.Max + 0.01f);
            if (length > 0)
                CHECK(last.x > first.x);
        }
    }
    // Not a table's position.
    CHECK_EQ(ImTypeset::GetDocumentTableCell(document, ImTypesetPosition{5, 0}).Row, -1);
    ImTypeset::DestroyDocument(document);
}

TEST(a_hit_in_a_cell_says_its_row_column_and_byte)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Content content;
    content.paragraph("Before the table.");
    content.table(UNITS);
    content.column();
    content.column();
    content.column();
    ImTypesetDocument* document = document_of(content);
    REQUIRE(document != nullptr);
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 3; ++c) {
            CHECK_CONTEXT("row %d column %d", r, c);
            const ImTypesetDocumentHit hit =
                ImTypeset::HitTestDocument(document, centre_of(document, 1, r, c));
            CHECK_EQ(hit.Position.Block, 1);
            CHECK_EQ(hit.Row, r);
            CHECK_EQ(hit.Column, c);
            CHECK(hit.InBlock);
            const ImTypesetCellPosition cell =
                ImTypeset::GetDocumentTableCell(document, hit.Position);
            CHECK_EQ(cell.Row, r);
            CHECK_EQ(cell.Column, c);
        }
    }
    // On a character: the caret is at it or just after it.
    const ImVec4 caret = ImTypeset::GetDocumentCaretRect(document, at(document, 1, 1, 2, 4));
    const ImTypesetDocumentHit on =
        ImTypeset::HitTestDocument(document, ImVec2(caret.x + 1.0f, (caret.y + caret.w) * 0.5f));
    CHECK_EQ(on.Row, 1);
    CHECK_EQ(on.Column, 2);
    CHECK(on.Inside);
    CHECK_EQ(ImTypeset::GetDocumentTableCell(document, on.Position).Offset, 4);
    // A paragraph's hit has no cell.
    const ImTypesetDocumentHit above = ImTypeset::HitTestDocument(document, ImVec2(5.0f, 5.0f));
    CHECK_EQ(above.Position.Block, 0);
    CHECK_EQ(above.Row, -1);
    CHECK_EQ(above.Column, -1);
    ImTypeset::DestroyDocument(document);
}

TEST(cells_take_runs_faces_colours_links_and_formulas)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Content content;
    ImTypesetBlock& block =
        content.table({{"Name", "Value"},
                       {"a link here", "$\\frac{\\frac{a}{b}}{\\frac{c}{d}}$"},
                       {"bold and red", "plain"}},
                      ImTypesetBlockFlags_TableHeader | ImTypesetBlockFlags_Math);
    (void)block;
    const std::string& text = content.text;
    const int link = static_cast<int>(text.find("link"));
    const int bold = static_cast<int>(text.find("bold"));
    const int red = static_cast<int>(text.find("red"));
    ImTypesetRun& header = content.run(0, 4);
    header.Face = ImTypesetFace_Bold;
    ImTypesetRun& linked = content.run(link, link + 4);
    linked.Tag = 7;
    linked.Color = IM_COL32(0, 0, 0xEE, 0xFF);
    content.run(bold, bold + 4).Face = ImTypesetFace_Bold;
    content.run(red, red + 3).Color = IM_COL32(0xEE, 0, 0, 0xFF);
    ImTypesetDocument* document = document_of(content);
    REQUIRE(document != nullptr);
    REQUIRE(ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e6f));
    CHECK_EQ(ImTypeset::GetDocumentBlockError(document, 0)->Code,
             static_cast<int>(ImTypesetError_None));

    // The link's tag under the mouse, and none beside it.
    const ImVec4 l = ImTypeset::GetDocumentCaretRect(document, ImTypesetPosition{0, link + 1});
    const ImTypesetDocumentHit on_link =
        ImTypeset::HitTestDocument(document, ImVec2(l.x + 1.0f, (l.y + l.w) * 0.5f));
    CHECK_EQ(on_link.Tag, 7);
    const ImVec4 p = ImTypeset::GetDocumentCaretRect(document, ImTypesetPosition{0, link - 5});
    CHECK_EQ(ImTypeset::HitTestDocument(document, ImVec2(p.x + 1.0f, (p.y + p.w) * 0.5f)).Tag, 0);

    // The formula copies as its source, whole or not at all.
    CHECK_EQ(whole(document, 0),
             "Name\tValue\na link here\t$\\frac{\\frac{a}{b}}{\\frac{c}{d}}$\nbold and red\tplain");
    const ImTypesetPosition formula = at(document, 0, 1, 1, 0);
    CHECK_EQ(copied(document, formula, ImTypesetPosition{0, formula.Offset + 1}),
             "$\\frac{\\frac{a}{b}}{\\frac{c}{d}}$");
    // The row holding the fraction is taller than a row of text.
    const ImTypesetTableRowGeometry tall = ImTypeset::GetDocumentTableRow(document, 0, 1);
    const ImTypesetTableRowGeometry plain = ImTypeset::GetDocumentTableRow(document, 0, 2);
    CHECK(tall.Max - tall.Min > plain.Max - plain.Min + 1.0f);

    // Colours reach the vertices: the link's, the red word's, and the ink.
    Frame frame;
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    REQUIRE(ImTypeset::DrawDocumentToBuffers(
        document, ImVec2(0.0f, 0.0f), ImVec4(0.0f, 0.0f, 4000.0f, 4000.0f), &vertices, &indices));
    int blue = 0;
    int reds = 0;
    int ink = 0;
    for (const ImDrawVert& v : vertices) {
        blue += v.col == IM_COL32(0, 0, 0xEE, 0xFF) ? 1 : 0;
        reds += v.col == IM_COL32(0xEE, 0, 0, 0xFF) ? 1 : 0;
        ink += v.col == IM_COL32(0x11, 0x22, 0x33, 0xFF) ? 1 : 0;
    }
    CHECK_EQ(blue, 4 * 4);
    CHECK_EQ(reds, 3 * 4);
    CHECK(ink > 4 * 20);
    ImTypeset::DestroyDocument(document);
}

// --- Widths ---------------------------------------------------------------------------------

TEST(a_table_too_wide_for_the_width_wraps_its_cells)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Content content;
    content.table({{"Term", "Meaning"},
                   {"glue", "Space that can stretch and shrink by stated amounts, which is how a "
                            "line is made to fit its measure without any letter being moved."},
                   {"kern", "Space that cannot."}});
    ImTypesetDocument* document = document_of(content, 320.0f);
    REQUIRE(document != nullptr);
    const ImTypesetTableGeometry table = ImTypeset::GetDocumentTableGeometry(document, 0);
    CHECK_NEAR(table.Max.x - table.Min.x, 320.0f, 1.0e-3f);
    const ImTypesetTableRowGeometry wrapped = ImTypeset::GetDocumentTableRow(document, 0, 1);
    const ImTypesetTableRowGeometry single = ImTypeset::GetDocumentTableRow(document, 0, 2);
    CHECK(wrapped.Wrapped);
    CHECK(!single.Wrapped);
    CHECK(wrapped.Max - wrapped.Min > 3.0f * (single.Max - single.Min));
    // The narrow column kept its longest word and gave the rest away.
    const ImTypesetTableColumnGeometry term = ImTypeset::GetDocumentTableColumn(document, 0, 0);
    const ImTypesetTableColumnGeometry meaning = ImTypeset::GetDocumentTableColumn(document, 0, 1);
    CHECK(term.Max - term.Min >= term.Floor - 0.01f);
    CHECK(meaning.Max - meaning.Min < meaning.Natural);
    // A wrapped cell copies with a space where its lines were broken.
    CHECK_EQ(copied(document, at(document, 0, 1, 1, 0), at(document, 0, 1, 1, 37)),
             "Space that can stretch and shrink by ");
    // Its selection is a rectangle a line.
    ImVector<ImVec4> rects;
    const ImTypesetDocumentRange cell = {at(document, 0, 1, 1, 0), at(document, 0, 1, 1, 1000)};
    CHECK(ImTypeset::GetDocumentRangeRects(document, cell, &rects) >= 3);
    for (const ImVec4& rect : rects) {
        CHECK(rect.x >= meaning.Min - 0.01f && rect.z <= meaning.Max + 0.01f);
        CHECK(rect.y >= wrapped.Min - 0.01f && rect.w <= wrapped.Max + 0.01f);
    }
    ImTypeset::DestroyDocument(document);
}

TEST(stated_widths_and_shares_are_what_the_columns_are)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Content content;
    content.table(UNITS);
    content.column().Width = 100.0f;
    content.column().Width = 60.0f;
    content.column().Width = 200.0f;
    ImTypesetDocument* document = document_of(content, 600.0f);
    REQUIRE(document != nullptr);
    const float stated[3] = {100.0f, 60.0f, 200.0f};
    CHECK(ImTypeset::GetDocumentTableGeometry(document, 0).Stated);
    for (int c = 0; c < 3; ++c) {
        const ImTypesetTableColumnGeometry column =
            ImTypeset::GetDocumentTableColumn(document, 0, c);
        CHECK_NEAR(column.Max - column.Min, stated[c], 1.0e-3f);
    }
    // Restated each frame, as a drag does: only the rows are set again.
    const ImTypesetDocumentStats before = ImTypeset::GetDocumentStats(document);
    ImTypesetTableColumn dragged[3];
    dragged[0].Width = 130.0f;
    dragged[1].Width = 60.0f;
    dragged[2].Width = 200.0f;
    REQUIRE(ImTypeset::SetDocumentTableColumns(document, 0, dragged, 3));
    CHECK_NEAR(ImTypeset::GetDocumentTableColumn(document, 0, 0).Max, 130.0f, 1.0e-3f);
    const ImTypesetDocumentStats after = ImTypeset::GetDocumentStats(document);
    CHECK_EQ(after.TableCellsMeasured, before.TableCellsMeasured);
    CHECK_EQ(after.TableRowsSet, before.TableRowsSet + 4);
    CHECK_EQ(after.StructureVisits, before.StructureVisits);
    // Said again unchanged: nothing is set.
    REQUIRE(ImTypeset::SetDocumentTableColumns(document, 0, dragged, 3));
    ImTypeset::GetDocumentTableGeometry(document, 0);
    CHECK_EQ(ImTypeset::GetDocumentStats(document).TableRowsSet, after.TableRowsSet);

    // Shares of the width.
    ImTypesetTableColumn shares[3];
    shares[0].Share = 1.0f;
    shares[1].Share = 2.0f;
    shares[2].Share = 1.0f;
    REQUIRE(ImTypeset::SetDocumentTableColumns(document, 0, shares, 3));
    CHECK_NEAR(ImTypeset::GetDocumentTableColumn(document, 0, 0).Max, 150.0f, 0.01f);
    CHECK_NEAR(ImTypeset::GetDocumentTableColumn(document, 0, 1).Max, 450.0f, 0.01f);
    CHECK_NEAR(ImTypeset::GetDocumentTableColumn(document, 0, 2).Max, 600.0f, 0.01f);
    CHECK(!ImTypeset::GetDocumentTableGeometry(document, 0).Stated);
    // Not every column stating one: the library chooses.
    shares[1].Share = 0.0f;
    REQUIRE(ImTypeset::SetDocumentTableColumns(document, 0, shares, 3));
    CHECK(ImTypeset::GetDocumentTableColumn(document, 0, 0).Max < 300.0f);
    CHECK(!ImTypeset::SetDocumentTableColumns(document, 5, shares, 3));
    ImTypeset::DestroyDocument(document);
}

// --- Marks ----------------------------------------------------------------------------------

TEST(a_mark_in_a_table_marks_what_it_covers_of_each_cell)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Content content;
    content.table(UNITS);
    ImTypesetDocument* document = document_of(content);
    REQUIRE(document != nullptr);
    Frame frame;
    const int plain = vertices_of(document);
    // A find hit inside one cell: "base" in row 1, column 2.
    const ImU32 yellow = IM_COL32(0xFF, 0xEE, 0x00, 0xFF);
    ImTypesetMark mark;
    mark.Range.Start = at(document, 0, 1, 2, 7);
    mark.Range.End = at(document, 0, 1, 2, 11);
    mark.Color = yellow;
    mark.Place = ImTypesetMarkPlace_Behind;
    ImTypesetDocumentDraw draw;
    draw.Marks = &mark;
    draw.MarkCount = 1;
    CHECK_EQ(vertices_of(document, &draw), plain + 4);
    ImVector<ImVec4> rects;
    REQUIRE_EQ(ImTypeset::GetDocumentRangeRects(document, mark.Range, &rects), 1);
    const ImTypesetTableColumnGeometry column = ImTypeset::GetDocumentTableColumn(document, 0, 2);
    const ImTypesetTableRowGeometry row = ImTypeset::GetDocumentTableRow(document, 0, 1);
    CHECK(rects[0].x > column.Min && rects[0].z < column.Max);
    CHECK(rects[0].y >= row.Min - 0.01f && rects[0].w <= row.Max + 0.01f);

    // Ink over the same four letters.
    mark.Place = ImTypesetMarkPlace_Ink;
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    REQUIRE(ImTypeset::DrawDocumentToBuffers(document, ImVec2(0.0f, 0.0f),
                                             ImVec4(0.0f, 0.0f, 4000.0f, 4000.0f), &vertices,
                                             &indices, &draw));
    int marked = 0;
    for (const ImDrawVert& v : vertices)
        marked += v.col == yellow ? 1 : 0;
    CHECK_EQ(marked, 4 * 4);

    // A mark from one cell into the next covers the end of one and the start
    // of the other: two rectangles on one row.
    ImTypesetDocumentRange across = {at(document, 0, 1, 0, 2), at(document, 0, 1, 1, 1)};
    rects.clear();
    const int count = ImTypeset::GetDocumentRangeRects(document, across, &rects);
    CHECK(count >= 1);
    // A selection drawn over the whole table adds rectangles and no glyph.
    ImTypesetDocumentDraw selected;
    selected.Selection.Start = ImTypesetPosition{0, 0};
    selected.Selection.End = ImTypesetPosition{0, ImTypeset::GetDocumentBlockTextSize(document, 0)};
    CHECK(vertices_of(document, &selected) > plain);
    ImTypeset::DestroyDocument(document);
}

// --- A table that grows ----------------------------------------------------------------------

namespace
{

// Appends one row of `cells` to the last block of `document`, a table whose
// text is `text` so far.
bool append_row(ImTypesetDocument* document, std::string& text,
                const std::vector<std::string>& cells)
{
    std::string more = text.empty() ? "" : "\n";
    std::vector<ImTypesetCell> added;
    for (size_t c = 0; c < cells.size(); ++c) {
        if (c != 0)
            more += '\t';
        ImTypesetCell cell;
        cell.TextStart = static_cast<int>(text.size() + more.size());
        more += cells[c];
        cell.TextEnd = static_cast<int>(text.size() + more.size());
        cell.Flags = c == 0 ? ImTypesetCellFlags_RowStart : 0;
        added.push_back(cell);
    }
    text += more;
    return ImTypeset::AppendDocumentTableCells(document, more.data(), more.data() + more.size(),
                                               added.data(), static_cast<int>(added.size()));
}

} // namespace

TEST(a_row_appended_to_a_held_table_sets_that_row_and_no_other)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Content content;
    content.paragraph("A stream.");
    content.table({{"Step", "Result", "Remarks"}},
                  ImTypesetBlockFlags_TableHeader | ImTypesetBlockFlags_TableHeld);
    ImTypesetDocument* document = document_of(content, 700.0f);
    REQUIRE(document != nullptr);
    std::string text = "Step\tResult\tRemarks";
    REQUIRE(ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e6f));
    const ImTypesetTableGeometry first = ImTypeset::GetDocumentTableGeometry(document, 1);
    CHECK(first.Stated);
    // Held: as wide as its cells want, not the document.
    CHECK(first.Max.x < 400.0f);
    float widths[3];
    for (int c = 0; c < 3; ++c) {
        const ImTypesetTableColumnGeometry column =
            ImTypeset::GetDocumentTableColumn(document, 1, c);
        widths[c] = column.Max - column.Min;
        CHECK_NEAR(widths[c], column.Natural, 1.0e-3f);
    }

    // Rows no wider than what is there: nothing moves, one row is set each.
    ImTypesetDocumentStats before = ImTypeset::GetDocumentStats(document);
    for (int r = 0; r < 200; ++r) {
        CHECK_CONTEXT("row %d", r);
        REQUIRE(append_row(document, text, {"n", "ok", "fine"}));
        REQUIRE(ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e6f));
        const ImTypesetDocumentStats now = ImTypeset::GetDocumentStats(document);
        CHECK_EQ(now.TableRowsSet, before.TableRowsSet + 1);
        CHECK_EQ(now.TableCellsMeasured, before.TableCellsMeasured + 3);
        CHECK_EQ(now.StructureVisits, before.StructureVisits);
        CHECK_EQ(now.MaterialBuilds, before.MaterialBuilds + 1);
        before = now;
    }
    for (int c = 0; c < 3; ++c) {
        const ImTypesetTableColumnGeometry column =
            ImTypeset::GetDocumentTableColumn(document, 1, c);
        CHECK_EQ(column.Max - column.Min, widths[c]);
    }
    CHECK_EQ(ImTypeset::GetDocumentTableGeometry(document, 1).RowCount, 201);

    // A wider cell arrives: its column grows, the others keep their widths,
    // and every row is set again, once.
    REQUIRE(append_row(document, text, {"n", "a much wider result than before", "fine"}));
    REQUIRE(ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e6f));
    ImTypesetDocumentStats now = ImTypeset::GetDocumentStats(document);
    CHECK_EQ(now.TableRowsSet, before.TableRowsSet + 202);
    CHECK_EQ(now.TableCellsMeasured, before.TableCellsMeasured + 3);
    const ImTypesetTableColumnGeometry grown = ImTypeset::GetDocumentTableColumn(document, 1, 1);
    CHECK(grown.Max - grown.Min > widths[1] + 50.0f);
    CHECK_EQ(ImTypeset::GetDocumentTableColumn(document, 1, 0).Max, widths[0]);
    const ImTypesetTableColumnGeometry third = ImTypeset::GetDocumentTableColumn(document, 1, 2);
    CHECK_NEAR(third.Max - third.Min, widths[2], 1.0e-3f);
    before = now;

    // Never narrower: a short row after the wide one moves nothing.
    REQUIRE(append_row(document, text, {"n", "ok", "fine"}));
    REQUIRE(ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e6f));
    now = ImTypeset::GetDocumentStats(document);
    CHECK_EQ(now.TableRowsSet, before.TableRowsSet + 1);
    CHECK_EQ(ImTypeset::GetDocumentTableColumn(document, 1, 1).Max, grown.Max);
    before = now;

    // Text arriving in the last cell: that cell is measured, its row is set.
    REQUIRE(ImTypeset::AppendDocumentText(document, " and"));
    text += " and";
    REQUIRE(ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e6f));
    now = ImTypeset::GetDocumentStats(document);
    CHECK_EQ(now.TableRowsSet, before.TableRowsSet + 1);
    CHECK_EQ(now.TableCellsMeasured, before.TableCellsMeasured + 1);
    CHECK_EQ(whole(document, 1), text);

    // A cell arriving in the row that is open goes on with it.
    ImTypesetCell late;
    late.TextStart = static_cast<int>(text.size()) + 1;
    late.TextEnd = late.TextStart + 5;
    REQUIRE(ImTypeset::AppendDocumentTableCells(document, "\tlater", nullptr, &late, 1));
    text += "\tlater";
    CHECK_EQ(ImTypeset::GetDocumentTableGeometry(document, 1).ColumnCount, 4);
    // Every earlier row now lacks a cell, and a copy gives the cells a row has.
    CHECK_EQ(whole(document, 1), text);
    CHECK_EQ(ImTypeset::GetDocumentTableGeometry(document, 1).RowCount, 203);
    CHECK_EQ(ImTypeset::GetDocumentTableRow(document, 1, 202).CellCount, 4);

    // The paragraph above never moved and the document's height is the sum.
    CHECK_EQ(ImTypeset::GetDocumentBlockGeometry(document, 0).FullMin.y, 0.0f);
    ImTypeset::DestroyDocument(document);
}

TEST(a_table_that_is_not_held_is_refitted_and_a_held_one_keeps_its_widths_over_a_replace)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    // Not held: the columns fill the width, so a longer cell moves them all.
    {
        Content content;
        content.table({{"a", "b"}});
        ImTypesetDocument* document = document_of(content, 400.0f);
        REQUIRE(document != nullptr);
        std::string text = "a\tb";
        const float before = ImTypeset::GetDocumentTableColumn(document, 0, 0).Max;
        CHECK_NEAR(ImTypeset::GetDocumentTableGeometry(document, 0).Max.x, 400.0f, 1.0e-3f);
        REQUIRE(append_row(document, text, {"a", "considerably longer"}));
        CHECK(ImTypeset::GetDocumentTableColumn(document, 0, 0).Max < before - 20.0f);
        ImTypeset::DestroyDocument(document);
    }
    // Held, and the block described again each time (a host that parses its
    // source again as it arrives): the widths are kept across the replace,
    // also when the new text wants less.
    {
        Content wide;
        wide.table({{"a wide heading here", "b"}},
                   ImTypesetBlockFlags_TableHeader | ImTypesetBlockFlags_TableHeld);
        ImTypesetDocument* document = document_of(wide, 600.0f);
        REQUIRE(document != nullptr);
        const float held = ImTypeset::GetDocumentTableColumn(document, 0, 0).Max;
        Content narrow;
        narrow.table({{"a", "b"}, {"c", "d"}},
                     ImTypesetBlockFlags_TableHeader | ImTypesetBlockFlags_TableHeld);
        const ImTypesetDocumentContent view = narrow.view();
        REQUIRE(ImTypeset::ReplaceDocumentBlocks(document, 0, 1, &view));
        CHECK_EQ(ImTypeset::GetDocumentTableColumn(document, 0, 0).Max, held);
        // Without the flag the table is fitted to the width: it settles.
        Content settled;
        settled.table({{"a", "b"}, {"c", "d"}});
        const ImTypesetDocumentContent closed = settled.view();
        REQUIRE(ImTypeset::ReplaceDocumentBlocks(document, 0, 1, &closed));
        CHECK_NEAR(ImTypeset::GetDocumentTableGeometry(document, 0).Max.x, 600.0f, 1.0e-3f);
        CHECK(!ImTypeset::GetDocumentTableGeometry(document, 0).Stated);
        // Another number of columns starts afresh.
        Content three;
        three.table({{"a", "b", "c"}},
                    ImTypesetBlockFlags_TableHeader | ImTypesetBlockFlags_TableHeld);
        const ImTypesetDocumentContent other = three.view();
        REQUIRE(ImTypeset::ReplaceDocumentBlocks(document, 0, 1, &other));
        CHECK(ImTypeset::GetDocumentTableColumn(document, 0, 0).Max < held);
        ImTypeset::DestroyDocument(document);
    }
    // Held widths that stop fitting: the table is fitted to the width.
    {
        Content content;
        content.table({{"a rather long heading", "and another long heading"}},
                      ImTypesetBlockFlags_TableHeld);
        ImTypesetDocument* document = document_of(content, 600.0f);
        REQUIRE(document != nullptr);
        CHECK(ImTypeset::GetDocumentTableGeometry(document, 0).Stated);
        ImTypeset::LayoutDocument(document, 150.0f);
        const ImTypesetTableGeometry squeezed = ImTypeset::GetDocumentTableGeometry(document, 0);
        CHECK(!squeezed.Stated);
        CHECK_NEAR(squeezed.Max.x, 150.0f, 1.0e-3f);
        CHECK(ImTypeset::GetDocumentTableRow(document, 0, 0).Wrapped);
        ImTypeset::LayoutDocument(document, 600.0f);
        CHECK(ImTypeset::GetDocumentTableGeometry(document, 0).Stated);
        ImTypeset::DestroyDocument(document);
    }
}

// --- Lazy layout -----------------------------------------------------------------------------

TEST(a_table_out_of_view_costs_nothing_and_a_steady_frame_sets_nothing)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Content content;
    for (int i = 0; i < 400; ++i) {
        content.paragraph("A paragraph of the document, number " + std::to_string(i) + ".");
        if (i % 4 == 3)
            content.table(UNITS);
    }
    ImTypesetDocument* document = document_of(content, 500.0f);
    REQUIRE(document != nullptr);
    Frame frame;
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    const ImVec4 window(0.0f, 0.0f, 500.0f, 300.0f);
    REQUIRE(ImTypeset::DrawDocumentToBuffers(document, ImVec2(0.0f, 0.0f), window, &vertices,
                                             &indices));
    const ImTypesetDocumentStats first = ImTypeset::GetDocumentStats(document);
    // 100 tables of 12 cells; the window shows a few.
    CHECK(first.TableCellsMeasured <= 4 * 12);
    CHECK(first.TableRowsSet <= 4 * 4);
    CHECK(first.BlocksDeferred > 450);
    for (int i = 0; i < 5; ++i) {
        vertices.resize(0);
        indices.resize(0);
        REQUIRE(ImTypeset::DrawDocumentToBuffers(document, ImVec2(0.0f, 0.0f), window, &vertices,
                                                 &indices));
    }
    const ImTypesetDocumentStats steady = ImTypeset::GetDocumentStats(document);
    CHECK_EQ(steady.TableCellsMeasured, first.TableCellsMeasured);
    CHECK_EQ(steady.TableRowsSet, first.TableRowsSet);
    CHECK_EQ(steady.BlocksLaidOut, first.BlocksLaidOut);
    // A new width sets the tables in view again and measures no cell.
    ImTypeset::LayoutDocument(document, 420.0f);
    vertices.resize(0);
    indices.resize(0);
    REQUIRE(ImTypeset::DrawDocumentToBuffers(document, ImVec2(0.0f, 0.0f), window, &vertices,
                                             &indices));
    const ImTypesetDocumentStats resized = ImTypeset::GetDocumentStats(document);
    CHECK_EQ(resized.TableCellsMeasured, first.TableCellsMeasured);
    CHECK(resized.TableRowsSet > first.TableRowsSet);
    ImTypeset::DestroyDocument(document);
}

// --- In a list --------------------------------------------------------------------------------

TEST(a_table_in_a_list_item_starts_at_the_items_indent)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Content content;
    ImTypesetContainer list;
    list.Kind = ImTypesetContainerKind_List;
    list.Indent = 24.0f;
    ImTypesetBlock& item = content.paragraph("An item with a table in it:");
    item.Kind = ImTypesetBlockKind_ListItem;
    item.Container = 1;
    item.LeftIndent = 24.0f;
    const size_t marker = content.text.size();
    content.text += "1.";
    content.blocks.back().MarkerStart = static_cast<int>(marker);
    content.blocks.back().MarkerEnd = static_cast<int>(marker) + 2;
    ImTypesetBlock& table = content.table(UNITS);
    table.Container = 1;
    table.LeftIndent = 24.0f;
    ImTypesetDocumentContent view = content.view();
    view.Containers = &list;
    view.ContainerCount = 1;
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    REQUIRE(document != nullptr);
    REQUIRE(ImTypeset::SetDocumentBlocks(document, &view));
    ImTypeset::LayoutDocument(document, 500.0f);
    const ImTypesetTableGeometry geometry = ImTypeset::GetDocumentTableGeometry(document, 1);
    CHECK_NEAR(geometry.Min.x, 24.0f, 1.0e-3f);
    CHECK_NEAR(geometry.Max.x, 500.0f, 1.0e-3f);
    CHECK_NEAR(ImTypeset::GetDocumentTableColumn(document, 1, 0).Min, 24.0f, 1.0e-3f);
    CHECK_EQ(whole(document, 1).substr(0, 13), "Quantity\tUnit");
    ImTypeset::DestroyDocument(document);
}

// --- What is wrong with a description ---------------------------------------------------------

TEST(ragged_rows_empty_cells_and_empty_tables_are_set)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Content content;
    content.table({{"a", "b", "c"}, {"only one"}, {"", "", ""}, {"two", "cells"}});
    content.table({});
    content.table({{""}});
    content.paragraph("After.");
    ImTypesetDocument* document = document_of(content);
    REQUIRE(document != nullptr);
    REQUIRE(ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e6f));
    const ImTypesetTableGeometry ragged = ImTypeset::GetDocumentTableGeometry(document, 0);
    CHECK_EQ(ragged.RowCount, 4);
    CHECK_EQ(ragged.ColumnCount, 3);
    CHECK_EQ(ragged.CellCount, 9);
    CHECK_EQ(ImTypeset::GetDocumentTableRow(document, 0, 1).CellCount, 1);
    // A copy keeps a tab for an empty cell and has none for a cell a row lacks.
    CHECK_EQ(whole(document, 0), "a\tb\tc\nonly one\n\t\t\ntwo\tcells");
    // A hit in a column the row has no cell for is in that row.
    const ImTypesetDocumentHit hit =
        ImTypeset::HitTestDocument(document, centre_of(document, 0, 1, 2));
    CHECK_EQ(hit.Row, 1);
    CHECK_EQ(hit.Column, 2);
    CHECK_EQ(ImTypeset::GetDocumentTableCell(document, hit.Position).Row, 1);
    // A table with no rows takes no room; one empty cell is one row.
    const ImTypesetTableGeometry none = ImTypeset::GetDocumentTableGeometry(document, 1);
    CHECK(none.LaidOut);
    CHECK_EQ(none.RowCount, 0);
    const ImTypesetBlockGeometry empty = ImTypeset::GetDocumentBlockGeometry(document, 1);
    CHECK_EQ(empty.FullMax.y, empty.FullMin.y);
    CHECK_EQ(whole(document, 1), "");
    CHECK_EQ(ImTypeset::GetDocumentTableGeometry(document, 2).RowCount, 1);
    CHECK(ImTypeset::GetDocumentTableRow(document, 2, 0).Max >
          ImTypeset::GetDocumentTableRow(document, 2, 0).Min);
    Frame frame;
    CHECK(vertices_of(document) > 0);
    ImTypeset::DestroyDocument(document);
}

TEST(a_description_that_is_wrong_is_cut_to_what_is_right_and_said)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    // Cells out of order, overlapping and outside the text.
    {
        Content content;
        content.table({{"abcdef", "ghi"}});
        content.cells[1].TextStart = 3; // overlaps the first
        ImTypesetCell beyond;
        beyond.TextStart = 50;
        beyond.TextEnd = 90;
        content.cells.push_back(beyond);
        ++content.blocks.back().CellCount;
        ImTypesetDocument* document = document_of(content, 400.0f, false);
        REQUIRE(document != nullptr);
        CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_None));
        const ImTypesetDocumentContent view = content.view();
        CHECK(!ImTypeset::SetDocumentBlocks(document, &view));
        CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_DescriptionCut));
        ImTypeset::LayoutDocument(document, 400.0f);
        CHECK_EQ(ImTypeset::GetDocumentTableGeometry(document, 0).CellCount, 3);
        CHECK_EQ(whole(document, 0), "abcdef\tghi\t");
        ImTypeset::DestroyDocument(document);
    }
    // Cells and columns that are not in the arrays given.
    {
        Content content;
        content.table({{"a", "b"}});
        content.blocks.back().CellCount = 40;
        content.blocks.back().ColumnStart = -3;
        content.blocks.back().ColumnCount = 2;
        ImTypesetDocument* document = ImTypeset::CreateDocument();
        REQUIRE(document != nullptr);
        const ImTypesetDocumentContent view = content.view();
        CHECK(!ImTypeset::SetDocumentBlocks(document, &view));
        CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_DescriptionCut));
        ImTypeset::LayoutDocument(document, 400.0f);
        CHECK_EQ(ImTypeset::GetDocumentTableGeometry(document, 0).RowCount, 0);
        ImTypeset::DestroyDocument(document);
    }
    // Widths and shares that are not numbers.
    {
        Content content;
        content.table(UNITS);
        content.column().Width = std::nanf("");
        content.column().Width = INFINITY;
        content.column().Width = -5.0f;
        ImTypesetDocument* document = document_of(content, 400.0f);
        REQUIRE(document != nullptr);
        const ImTypesetTableGeometry geometry = ImTypeset::GetDocumentTableGeometry(document, 0);
        CHECK(!geometry.Stated);
        CHECK_NEAR(geometry.Max.x, 400.0f, 1.0e-3f);
        ImTypesetTableColumn shares[3];
        shares[0].Share = 1.0e30f;
        shares[1].Share = 1.0e-30f;
        shares[2].Share = std::nanf("");
        REQUIRE(ImTypeset::SetDocumentTableColumns(document, 0, shares, 3));
        CHECK(std::isfinite(ImTypeset::GetDocumentTableGeometry(document, 0).Max.x));
        shares[2].Share = 1.0e-30f;
        REQUIRE(ImTypeset::SetDocumentTableColumns(document, 0, shares, 3));
        CHECK(std::isfinite(ImTypeset::GetDocumentTableGeometry(document, 0).Max.x));
        REQUIRE(ImTypeset::SetDocumentTableColumns(document, 0, nullptr, 0));
        CHECK_NEAR(ImTypeset::GetDocumentTableGeometry(document, 0).Max.x, 400.0f, 1.0e-3f);
        ImTypeset::DestroyDocument(document);
    }
    // Appending to what is not a table, and text to a table with no cell at its end.
    {
        Content content;
        content.paragraph("Text.");
        ImTypesetDocument* document = document_of(content);
        REQUIRE(document != nullptr);
        ImTypesetCell cell;
        cell.TextEnd = 1;
        CHECK(!ImTypeset::AppendDocumentTableCells(document, "x", nullptr, &cell, 1));
        CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_InvalidArgument));
        Content table;
        table.table({});
        const ImTypesetDocumentContent view = table.view();
        REQUIRE(ImTypeset::ReplaceDocumentBlocks(document, 1, 0, &view));
        CHECK(!ImTypeset::AppendDocumentText(document, "x"));
        CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_InvalidArgument));
        // A first row appended to an empty table gives it a height.
        std::string text;
        const float before = ImTypeset::GetDocumentSize(document).y;
        REQUIRE(append_row(document, text, {"a", "b"}));
        REQUIRE(ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e6f));
        CHECK(ImTypeset::GetDocumentSize(document).y > before + 10.0f);
        REQUIRE(ImTypeset::AppendDocumentText(document, "c"));
        CHECK_EQ(whole(document, 1), "a\tbc");
        ImTypeset::DestroyDocument(document);
    }
}

// --- Limits ------------------------------------------------------------------------------------

TEST(a_table_past_its_limits_is_cut_at_them)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    // One row of 3,000 cells: 1,024 are kept.
    {
        Content content;
        std::vector<std::string> row(3000, "x");
        content.table({row});
        ImTypesetDocument* document = ImTypeset::CreateDocument();
        REQUIRE(document != nullptr);
        const ImTypesetDocumentContent view = content.view();
        CHECK(!ImTypeset::SetDocumentBlocks(document, &view));
        CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_DescriptionCut));
        ImTypeset::LayoutDocument(document, 800.0f);
        const ImTypesetTableGeometry geometry = ImTypeset::GetDocumentTableGeometry(document, 0);
        CHECK_EQ(geometry.ColumnCount, 1024);
        CHECK_EQ(geometry.CellCount, 1024);
        CHECK(std::isfinite(geometry.Max.x));
        ImTypeset::DestroyDocument(document);
    }
    // 300,000 one-character cells in rows of four: 262,144 are kept, and the
    // table is laid out once.
    {
        std::string text;
        std::vector<ImTypesetCell> cells;
        const int wanted = 300000;
        text.reserve(static_cast<size_t>(wanted) * 2);
        cells.reserve(static_cast<size_t>(wanted));
        for (int k = 0; k < wanted; ++k) {
            if (k != 0)
                text += k % 4 == 0 ? '\n' : '\t';
            ImTypesetCell cell;
            cell.TextStart = static_cast<int>(text.size());
            text += static_cast<char>('a' + k % 26);
            cell.TextEnd = static_cast<int>(text.size());
            cell.Flags = k % 4 == 0 ? ImTypesetCellFlags_RowStart : 0;
            cells.push_back(cell);
        }
        ImTypesetBlock block;
        block.Kind = ImTypesetBlockKind_Table;
        block.TextEnd = static_cast<int>(text.size());
        block.CellCount = wanted;
        ImTypesetDocumentContent view;
        view.Text = text.data();
        view.TextSize = static_cast<int>(text.size());
        view.Blocks = &block;
        view.BlockCount = 1;
        view.Cells = cells.data();
        view.CellCount = wanted;
        ImTypesetDocument* document = ImTypeset::CreateDocument();
        REQUIRE(document != nullptr);
        CHECK(!ImTypeset::SetDocumentBlocks(document, &view));
        CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_DescriptionCut));
        ImTypeset::LayoutDocument(document, 400.0f);
        const ImTypesetTableGeometry geometry = ImTypeset::GetDocumentTableGeometry(document, 0);
        CHECK_EQ(geometry.CellCount, 262144);
        CHECK_EQ(geometry.RowCount, 65536);
        const ImTypesetDocumentStats stats = ImTypeset::GetDocumentStats(document);
        CHECK_EQ(stats.TableCellsMeasured, ImU64(262144));
        CHECK_EQ(stats.TableRowsSet, ImU64(65536));
        // The last cell kept is found in a handful of steps, and is where it says.
        const ImTypesetCellPosition last = ImTypeset::GetDocumentTableCell(
            document, ImTypesetPosition{0, cells[262143].TextStart});
        CHECK_EQ(last.Row, 65535);
        CHECK_EQ(last.Column, 3);
        // A window near the end draws a screenful, not the table.
        Frame frame;
        ImVector<ImDrawVert> vertices;
        ImVector<ImDrawIdx> indices;
        const float bottom = geometry.Max.y;
        REQUIRE(ImTypeset::DrawDocumentToBuffers(document, ImVec2(0.0f, 300.0f - bottom),
                                                 ImVec4(0.0f, 0.0f, 400.0f, 300.0f), &vertices,
                                                 &indices));
        CHECK(vertices.Size > 0);
        CHECK(vertices.Size < 4 * 4 * 40);
        ImTypeset::DestroyDocument(document);
    }
}
