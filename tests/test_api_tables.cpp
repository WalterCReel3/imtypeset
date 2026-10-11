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
    // A hit in a column the row has no cell for is in that row, and in the
    // last cell the row has, at its end: the cell the position names is the
    // cell the hit names.
    const ImTypesetDocumentHit hit =
        ImTypeset::HitTestDocument(document, centre_of(document, 0, 1, 2));
    CHECK_EQ(hit.Row, 1);
    CHECK_EQ(hit.Column, 0);
    const ImTypesetCellPosition lacking = ImTypeset::GetDocumentTableCell(document, hit.Position);
    CHECK_EQ(lacking.Row, 1);
    CHECK_EQ(lacking.Column, 0);
    CHECK_EQ(lacking.Offset, 8);
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

// --- Positions in cells: the product's rule (docs/decisions.md D60) --------------------------

namespace
{

// One table for the tests of where a point, a position and a caret are: a
// left, a right-aligned and a centred column; cells shorter than their
// columns, empty cells, and a row with one cell.
void positions_table(Content& content, ImTypesetBlockFlags flags = 0)
{
    content.table({{"ab", "cd", "ef"}, {"x", "12", "z"}, {"", "q", ""}, {"", "", "w"}, {"only"}},
                  flags);
    content.column();
    content.column(ImTypesetAlign_Right);
    content.column(ImTypesetAlign_Center);
}

struct CellRect {
    float x0 = 0.0f;
    float x1 = 0.0f;
    float y0 = 0.0f;
    float y1 = 0.0f;
};

CellRect rect_of(ImTypesetDocument* document, int block, int row, int column)
{
    const ImTypesetTableColumnGeometry c =
        ImTypeset::GetDocumentTableColumn(document, block, column);
    const ImTypesetTableRowGeometry r = ImTypeset::GetDocumentTableRow(document, block, row);
    return CellRect{c.Min, c.Max, r.Min, r.Max};
}

// Points of a rectangle: a pixel inside each edge, and across and down it.
std::vector<ImVec2> points_in(const CellRect& rect)
{
    std::vector<ImVec2> out;
    const float xs[] = {rect.x0 + 1.0f, rect.x0 + (rect.x1 - rect.x0) * 0.25f,
                        (rect.x0 + rect.x1) * 0.5f, rect.x0 + (rect.x1 - rect.x0) * 0.75f,
                        rect.x1 - 1.0f};
    const float ys[] = {rect.y0 + 1.0f, (rect.y0 + rect.y1) * 0.5f, rect.y1 - 1.0f};
    for (const float y : ys) {
        for (const float x : xs)
            out.push_back(ImVec2(x, y));
    }
    return out;
}

// What every point of every cell of a table must answer: the cell (row,
// column, and the cell its position names), a byte of that cell's text, a
// caret inside the cell's rectangle and no taller than the row, and the same
// position again when the caret's own middle is asked. `cells_of_row[r]` is
// how many cells row r has. Returns how many points were asked.
int hold_cells(ImTypesetDocument* document, int block, const std::vector<int>& cells_of_row,
               int columns)
{
    int asked = 0;
    for (int r = 0; r < static_cast<int>(cells_of_row.size()); ++r) {
        for (int c = 0; c < columns; ++c) {
            // A column the row has no cell for answers the row's last cell.
            const int cell_column = std::min(c, cells_of_row[static_cast<size_t>(r)] - 1);
            const CellRect rect = rect_of(document, block, r, c);
            const CellRect own = rect_of(document, block, r, cell_column);
            const ImTypesetPosition first = at(document, block, r, cell_column, 0);
            const ImTypesetPosition last = at(document, block, r, cell_column, 1 << 20);
            for (const ImVec2& point : points_in(rect)) {
                CHECK_CONTEXT("row %d column %d at (%.2f, %.2f)", r, c,
                              static_cast<double>(point.x), static_cast<double>(point.y));
                ++asked;
                const ImTypesetDocumentHit hit = ImTypeset::HitTestDocument(document, point);
                CHECK_EQ(hit.Position.Block, block);
                CHECK(hit.InBlock);
                CHECK_EQ(hit.Row, r);
                CHECK_EQ(hit.Column, cell_column);
                CHECK(hit.Position.Offset >= first.Offset);
                CHECK(hit.Position.Offset <= last.Offset);
                const ImTypesetCellPosition named =
                    ImTypeset::GetDocumentTableCell(document, hit.Position);
                CHECK_EQ(named.Row, r);
                CHECK_EQ(named.Column, cell_column);
                // The caret: a line of no width, inside the cell.
                const ImVec4 caret = ImTypeset::GetDocumentCaretRect(document, hit.Position);
                CHECK_EQ(caret.x, caret.z);
                CHECK(caret.w > caret.y);
                CHECK(caret.x >= own.x0);
                CHECK(caret.x <= own.x1);
                CHECK(caret.y >= own.y0 - 0.01f);
                CHECK(caret.w <= own.y1 + 0.01f);
                // And asked where the caret stands, the same position.
                const ImTypesetDocumentHit again = ImTypeset::HitTestDocument(
                    document, ImVec2(caret.x, (caret.y + caret.w) * 0.5f));
                CHECK_EQ(again.Position.Block, hit.Position.Block);
                CHECK_EQ(again.Position.Offset, hit.Position.Offset);
                CHECK_EQ(again.Row, hit.Row);
                CHECK_EQ(again.Column, hit.Column);
            }
        }
    }
    return asked;
}

} // namespace

TEST(a_point_in_a_cells_rectangle_answers_that_cell_and_its_caret_answers_the_same_position)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Content content;
    content.paragraph("Before the table.");
    positions_table(content);
    content.paragraph("After the table.");
    ImTypesetDocument* document = document_of(content);
    REQUIRE(document != nullptr);
    for (int r = 0; r < 5; ++r)
        CHECK(!ImTypeset::GetDocumentTableRow(document, 1, r).Wrapped);
    CHECK_EQ(hold_cells(document, 1, {3, 3, 3, 3, 1}, 3), 5 * 3 * 15);

    // The blank of a cell is the cell's: its far end answers the end of its
    // text, not the next cell and not the next row.
    const CellRect left = rect_of(document, 1, 0, 0);
    const ImTypesetDocumentHit blank =
        ImTypeset::HitTestDocument(document, ImVec2(left.x1 - 1.0f, (left.y0 + left.y1) * 0.5f));
    CHECK_EQ(blank.Position.Offset, at(document, 1, 0, 0, 2).Offset);
    const CellRect last = rect_of(document, 1, 0, 2);
    const ImTypesetDocumentHit edge =
        ImTypeset::HitTestDocument(document, ImVec2(last.x1 - 1.0f, (last.y0 + last.y1) * 0.5f));
    CHECK_EQ(edge.Row, 0);
    CHECK_EQ(edge.Column, 2);
    CHECK_EQ(edge.Position.Offset, at(document, 1, 0, 2, 2).Offset);
    // The blank BEFORE a right-aligned cell's text is its start.
    const CellRect right = rect_of(document, 1, 0, 1);
    const ImTypesetDocumentHit before =
        ImTypeset::HitTestDocument(document, ImVec2(right.x0 + 1.0f, (right.y0 + right.y1) * 0.5f));
    CHECK_EQ(before.Position.Offset, at(document, 1, 0, 1, 0).Offset);
    ImTypeset::DestroyDocument(document);
}

TEST(a_caret_at_a_cells_end_stands_at_the_end_of_its_text_whatever_the_alignment)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Content content;
    positions_table(content);
    ImTypesetDocument* document = document_of(content);
    REQUIRE(document != nullptr);
    const float padding = 8.0f; // the default: half an em of 16 px
    for (int r = 0; r < 2; ++r) {
        CHECK_CONTEXT("row %d", r);
        const CellRect l = rect_of(document, 0, r, 0);
        const CellRect m = rect_of(document, 0, r, 1);
        const CellRect c = rect_of(document, 0, r, 2);
        const ImVec4 l0 = ImTypeset::GetDocumentCaretRect(document, at(document, 0, r, 0, 0));
        const ImVec4 l1 = ImTypeset::GetDocumentCaretRect(document, at(document, 0, r, 0, 99));
        const ImVec4 m0 = ImTypeset::GetDocumentCaretRect(document, at(document, 0, r, 1, 0));
        const ImVec4 m1 = ImTypeset::GetDocumentCaretRect(document, at(document, 0, r, 1, 99));
        const ImVec4 c0 = ImTypeset::GetDocumentCaretRect(document, at(document, 0, r, 2, 0));
        const ImVec4 c1 = ImTypeset::GetDocumentCaretRect(document, at(document, 0, r, 2, 99));
        // Left: the text starts after the padding.
        CHECK_NEAR(l0.x, l.x0 + padding, 0.01f);
        CHECK(l1.x > l0.x);
        CHECK(l1.x < l.x1 - padding);
        // Right: the text ENDS before the padding, and its end is its own.
        CHECK_NEAR(m1.x, m.x1 - padding, 0.01f);
        CHECK(m0.x < m1.x);
        CHECK(m0.x > m.x0 + padding);
        // Centred: as far from one edge as from the other.
        CHECK(c0.x < c1.x);
        CHECK_NEAR(c0.x - c.x0, c.x1 - c1.x, 0.01f);
    }
    // An empty cell: where its text would start.
    const CellRect el = rect_of(document, 0, 2, 0);
    const CellRect er = rect_of(document, 0, 3, 1);
    CHECK_NEAR(ImTypeset::GetDocumentCaretRect(document, at(document, 0, 2, 0, 0)).x,
               el.x0 + padding, 0.01f);
    CHECK_NEAR(ImTypeset::GetDocumentCaretRect(document, at(document, 0, 3, 1, 0)).x,
               er.x1 - padding, 0.01f);
    ImTypeset::DestroyDocument(document);
}

TEST(in_a_wrapped_row_a_caret_is_one_line_tall_and_every_point_answers_its_cell)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Content content;
    content.table(
        {{"one two three four five six seven eight nine ten", "12", "a b c d e f g h i j k l"},
         {"short", "3", ""},
         {"again a cell with several words in it to break", "", "x"}},
        0);
    content.column();
    content.column(ImTypesetAlign_Right);
    content.column(ImTypesetAlign_Center);
    ImTypesetDocument* document = document_of(content, 260.0f);
    REQUIRE(document != nullptr);
    const ImTypesetTableRowGeometry row0 = ImTypeset::GetDocumentTableRow(document, 0, 0);
    const ImTypesetTableRowGeometry row2 = ImTypeset::GetDocumentTableRow(document, 0, 2);
    REQUIRE(row0.Wrapped);
    REQUIRE(row2.Wrapped);
    const float line = 21.6f; // 1.35 x 16 px, the default leading
    REQUIRE(row0.Max - row0.Min > 2.5f * line);
    CHECK(hold_cells(document, 0, {3, 3, 3}, 3) == 3 * 3 * 15);

    // Every caret of the first cell: one line tall, and the lines it is
    // found on are several and go down the cell.
    const int size = at(document, 0, 0, 0, 1 << 20).Offset;
    float lowest = -100.0f;
    int lines = 0;
    for (int byte = 0; byte <= size; ++byte) {
        CHECK_CONTEXT("byte %d", byte);
        const ImVec4 caret = ImTypeset::GetDocumentCaretRect(document, ImTypesetPosition{0, byte});
        CHECK_NEAR(caret.w - caret.y, line, 0.01f);
        CHECK(caret.y >= row0.Min - 0.01f);
        CHECK(caret.w <= row0.Max + 0.01f);
        CHECK(caret.y >= lowest - 0.01f);
        if (caret.y > lowest + 1.0f) {
            ++lines;
            lowest = caret.y;
        }
    }
    CHECK(lines >= 3);
    // A cell that fits, in a wrapped row: one line, at the row's top.
    const ImVec4 fits = ImTypeset::GetDocumentCaretRect(document, at(document, 0, 0, 1, 2));
    CHECK_NEAR(fits.y, row0.Min, 0.01f);
    CHECK_NEAR(fits.w - fits.y, line, 0.01f);
    const CellRect middle = rect_of(document, 0, 0, 1);
    CHECK_NEAR(fits.x, middle.x1 - 8.0f, 0.01f);
    // Below the last line of a cell shorter than its row: still that cell,
    // on its last line.
    const ImTypesetDocumentHit below = ImTypeset::HitTestDocument(
        document, ImVec2((middle.x0 + middle.x1) * 0.5f, row0.Max - 1.0f));
    CHECK_EQ(below.Row, 0);
    CHECK_EQ(below.Column, 1);
    ImTypeset::DestroyDocument(document);
}

TEST(cells_written_with_nothing_between_them_keep_hit_and_caret_a_fixed_point)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    // "abcdef" as two cells, [0, 3) and [3, 6), then a row whose cells touch
    // and whose middle cell is empty: "ghij" as [6, 8), [8, 8), [8, 10).
    Content content;
    ImTypesetBlock block;
    block.Kind = ImTypesetBlockKind_Table;
    block.TextStart = 0;
    content.text = "abcdefghij";
    block.TextEnd = 10;
    const int bounds[][3] = {{0, 3, 1}, {3, 6, 0}, {6, 8, 1}, {8, 8, 0}, {8, 10, 0}};
    for (const auto& bound : bounds) {
        ImTypesetCell cell;
        cell.TextStart = bound[0];
        cell.TextEnd = bound[1];
        cell.Flags = bound[2] != 0 ? ImTypesetCellFlags_RowStart : 0;
        content.cells.push_back(cell);
    }
    block.CellCount = 5;
    content.blocks.push_back(block);
    content.column();
    content.column();
    content.column();
    ImTypesetDocument* document = document_of(content);
    REQUIRE(document != nullptr);

    // The byte two cells share is the later cell's start: the header's rule.
    CHECK_EQ(ImTypeset::GetDocumentTableCell(document, ImTypesetPosition{0, 3}).Column, 1);
    const CellRect first = rect_of(document, 0, 0, 0);
    const CellRect second = rect_of(document, 0, 0, 1);
    const float y = (first.y0 + first.y1) * 0.5f;
    // The blank after "abc" is the first cell's. Its end is not a position of
    // its own, so the caret goes to the last that is: before the "c".
    const ImTypesetDocumentHit blank =
        ImTypeset::HitTestDocument(document, ImVec2(first.x1 - 1.0f, y));
    CHECK_EQ(blank.Row, 0);
    CHECK_EQ(blank.Column, 0);
    CHECK_EQ(blank.Position.Offset, 2);
    const ImVec4 caret = ImTypeset::GetDocumentCaretRect(document, blank.Position);
    CHECK(caret.x > first.x0);
    CHECK(caret.x < first.x1);
    const ImTypesetDocumentHit again =
        ImTypeset::HitTestDocument(document, ImVec2(caret.x, (caret.y + caret.w) * 0.5f));
    CHECK_EQ(again.Position.Offset, 2);
    CHECK_EQ(again.Column, 0);
    // The shared byte's caret is in the later cell, and a hit there finds it.
    const ImVec4 shared = ImTypeset::GetDocumentCaretRect(document, ImTypesetPosition{0, 3});
    CHECK(shared.x >= second.x0);
    CHECK(shared.x < second.x1);
    const ImTypesetDocumentHit there =
        ImTypeset::HitTestDocument(document, ImVec2(shared.x, (shared.y + shared.w) * 0.5f));
    CHECK_EQ(there.Position.Offset, 3);
    CHECK_EQ(there.Column, 1);

    // Every point of every cell that has a position of its own: the whole
    // round trip. The empty cell that touches both neighbours has none: a
    // point in it says its row and column, and the position is the byte it
    // shares, which names the cell after it.
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < (r == 0 ? 2 : 3); ++c) {
            const bool empty = r == 1 && c == 1;
            for (const ImVec2& point : points_in(rect_of(document, 0, r, c))) {
                CHECK_CONTEXT("row %d column %d at x %.2f", r, c, static_cast<double>(point.x));
                const ImTypesetDocumentHit hit = ImTypeset::HitTestDocument(document, point);
                CHECK_EQ(hit.Row, r);
                CHECK_EQ(hit.Column, c);
                const ImTypesetCellPosition named =
                    ImTypeset::GetDocumentTableCell(document, hit.Position);
                CHECK_EQ(named.Row, r);
                CHECK_EQ(named.Column, empty ? 2 : c);
                if (empty) {
                    CHECK_EQ(hit.Position.Offset, 8);
                    continue;
                }
                const ImVec4 at_hit = ImTypeset::GetDocumentCaretRect(document, hit.Position);
                const ImTypesetDocumentHit back = ImTypeset::HitTestDocument(
                    document, ImVec2(at_hit.x, (at_hit.y + at_hit.w) * 0.5f));
                CHECK_EQ(back.Position.Offset, hit.Position.Offset);
                CHECK_EQ(back.Row, r);
                CHECK_EQ(back.Column, c);
            }
        }
    }
    ImTypeset::DestroyDocument(document);
}

// D61: under the product's rules a range through a table is highlighted cell
// by cell, whether the row is set on one line or wrapped, and whether the
// range starts at the row's start or inside a cell.
TEST(a_range_through_a_table_is_one_rectangle_a_cell_inside_that_cells_text)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Content content;
    positions_table(content);
    ImTypesetDocument* document = document_of(content);
    REQUIRE(document != nullptr);
    const float padding = 8.0f;
    const auto rects_of = [&](ImTypesetPosition start, ImTypesetPosition end) {
        ImVector<ImVec4> out;
        const ImTypesetDocumentRange range = {start, end};
        ImTypeset::GetDocumentRangeRects(document, range, &out);
        return out;
    };
    // Rows 0 and 1 whole, and from inside the first cell of row 0: the same
    // number of rectangles a row, each inside one cell's text area.
    for (const int start_byte : {0, 1}) {
        CHECK_CONTEXT("from byte %d", start_byte);
        const ImVector<ImVec4> got =
            rects_of(at(document, 0, 0, 0, start_byte), at(document, 0, 1, 2, 99));
        REQUIRE(got.Size == 6);
        for (int k = 0; k < got.Size; ++k) {
            CHECK_CONTEXT("rectangle %d", k);
            const CellRect cell = rect_of(document, 0, k / 3, k % 3);
            CHECK(got[k].x >= cell.x0 + padding - 0.01f);
            CHECK(got[k].z <= cell.x1 - padding + 0.01f);
            CHECK(got[k].z > got[k].x);
            CHECK(got[k].y >= cell.y0 - 0.01f);
            CHECK(got[k].w <= cell.y1 + 0.01f);
        }
        // The right-aligned cell's text ends at its padding, and so does
        // its rectangle; the blank before it is not highlighted.
        const CellRect middle = rect_of(document, 0, 0, 1);
        CHECK_NEAR(got[1].z, middle.x1 - padding, 0.01f);
        CHECK(got[1].x > middle.x0 + 50.0f);
        // The left cell's rectangle ends with its text.
        const ImVec4 end_of_first =
            ImTypeset::GetDocumentCaretRect(document, at(document, 0, 0, 0, 99));
        CHECK_NEAR(got[0].z, end_of_first.x, 0.01f);
    }
    // One cell's text: the caret at its start to the caret at its end.
    const ImVector<ImVec4> one = rects_of(at(document, 0, 1, 1, 0), at(document, 0, 1, 1, 99));
    REQUIRE(one.Size == 1);
    CHECK_NEAR(one[0].x, ImTypeset::GetDocumentCaretRect(document, at(document, 0, 1, 1, 0)).x,
               0.01f);
    CHECK_NEAR(one[0].z, ImTypeset::GetDocumentCaretRect(document, at(document, 0, 1, 1, 99)).x,
               0.01f);
    // An empty cell has no rectangle.
    CHECK_EQ(rects_of(at(document, 0, 2, 0, 0), at(document, 0, 2, 1, 0)).Size, 0);
    ImTypeset::DestroyDocument(document);
}

// --- FixedLineHeight on a table (docs/decisions.md D62) ---------------------------------------

namespace
{

// Three rows whose first and last wrap at 260 px, with no header: nothing
// lies between two rows but what the leading leaves, which with the flag is
// nothing.
void wrapping_table(Content& content, ImTypesetBlockFlags flags)
{
    content.table(
        {{"one two three four five six seven eight nine ten", "12", "a b c d e f g h i j k l"},
         {"short", "3", ""},
         {"again a cell with several words in it to break", "", "x"}},
        flags);
    content.column();
    content.column(ImTypesetAlign_Right);
    content.column(ImTypesetAlign_Center);
}

} // namespace

TEST(fixed_line_height_on_a_table_is_per_line_and_a_wrapped_row_is_its_lines_tall)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Content content;
    wrapping_table(content, ImTypesetBlockFlags_FixedLineHeight);
    content.paragraph("After the table.");
    ImTypesetDocument* document = document_of(content, 260.0f);
    REQUIRE(document != nullptr);
    const float pitch = 21.6f; // 1.35 x 16 px
    const ImTypesetTableGeometry table = ImTypeset::GetDocumentTableGeometry(document, 0);
    const ImTypesetBlockGeometry block = ImTypeset::GetDocumentBlockGeometry(document, 0);
    const ImTypesetBlockGeometry after = ImTypeset::GetDocumentBlockGeometry(document, 1);
    ImTypesetTableRowGeometry rows[3];
    int lines = 0;
    for (int r = 0; r < 3; ++r) {
        CHECK_CONTEXT("row %d", r);
        rows[r] = ImTypeset::GetDocumentTableRow(document, 0, r);
        // A row is a whole number of lines of the pitch, one for a row that
        // fits and several for one that wraps.
        const float tall = (rows[r].Max - rows[r].Min) / pitch;
        const int count = static_cast<int>(std::lround(tall));
        CHECK_NEAR(tall, static_cast<float>(count), 1.0e-3f);
        CHECK(rows[r].Wrapped ? count >= 2 : count == 1);
        lines += count;
    }
    REQUIRE(rows[0].Wrapped);
    REQUIRE(!rows[1].Wrapped);
    REQUIRE(rows[2].Wrapped);
    // The rows tile the table, and the table is its lines tall: the block's
    // extent, the table's rectangle and the next block's top all say so.
    CHECK_NEAR(rows[0].Min, table.Min.y, 1.0e-3f);
    CHECK_NEAR(rows[1].Min, rows[0].Max, 1.0e-3f);
    CHECK_NEAR(rows[2].Min, rows[1].Max, 1.0e-3f);
    CHECK_NEAR(table.Max.y, rows[2].Max, 1.0e-3f);
    CHECK_NEAR(table.Max.y - table.Min.y, pitch * static_cast<float>(lines), 1.0e-2f);
    CHECK_NEAR(block.FullMax.y - block.FullMin.y, pitch * static_cast<float>(lines), 1.0e-2f);
    CHECK(after.FullMin.y >= table.Max.y - 1.0e-3f);
    // Every caret of the last row's first cell is one pitch tall, on a
    // multiple of the pitch, and inside the row: its lines are in the row's
    // box, the last as much as the first.
    const int first = at(document, 0, 2, 0, 0).Offset;
    const int last = at(document, 0, 2, 0, 1 << 20).Offset;
    float lowest = 0.0f;
    for (int byte = first; byte <= last; ++byte) {
        CHECK_CONTEXT("byte %d", byte);
        const ImVec4 caret = ImTypeset::GetDocumentCaretRect(document, ImTypesetPosition{0, byte});
        CHECK_NEAR(caret.w - caret.y, pitch, 1.0e-3f);
        const float down = (caret.y - rows[2].Min) / pitch;
        CHECK_NEAR(down, std::round(down), 1.0e-3f);
        CHECK(caret.y >= rows[2].Min - 1.0e-3f);
        CHECK(caret.w <= rows[2].Max + 1.0e-3f);
        lowest = std::max(lowest, caret.w);
    }
    CHECK_NEAR(lowest, rows[2].Max, 1.0e-3f);
    CHECK(hold_cells(document, 0, {3, 3, 3}, 3) == 3 * 3 * 15);
    // A point on the last line of the last row is in the table and on a line.
    const ImTypesetDocumentHit low =
        ImTypeset::HitTestDocument(document, ImVec2(20.0f, rows[2].Max - pitch * 0.5f));
    CHECK_EQ(low.Position.Block, 0);
    CHECK(low.InBlock);
    CHECK(low.Inside);
    CHECK_EQ(low.Row, 2);
    ImTypeset::DestroyDocument(document);

    // Without the flag the same table is no shorter: the flag changes what a
    // tall line does to its neighbours, not how many lines there are.
    Content loose;
    wrapping_table(loose, 0);
    ImTypesetDocument* other = document_of(loose, 260.0f);
    REQUIRE(other != nullptr);
    const ImTypesetTableGeometry unfixed = ImTypeset::GetDocumentTableGeometry(other, 0);
    CHECK_NEAR(unfixed.Max.y - unfixed.Min.y, pitch * static_cast<float>(lines), 1.0e-2f);
    ImTypeset::DestroyDocument(other);
}

TEST(ink_that_overhangs_a_fixed_line_is_drawn_when_only_the_overhang_is_in_the_clip)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    // A formula taller than the line it is in: with the flag the line's box
    // stays one pitch and the fraction stands above and below it.
    Content content;
    content.paragraph("x $\\frac{\\frac{a}{b}}{\\frac{c}{d}}$ y").Flags =
        ImTypesetBlockFlags_FixedLineHeight | ImTypesetBlockFlags_Math;
    ImTypesetBlockStyle tight;
    tight.LineHeight = 0.5f;
    content.styles.push_back(tight);
    content.blocks.back().Style = 0;
    ImTypesetDocument* document = document_of(content);
    REQUIRE(document != nullptr);
    const float origin = 100.0f;
    const ImTypesetBlockGeometry block = ImTypeset::GetDocumentBlockGeometry(document, 0);
    CHECK_NEAR(block.FullMax.y - block.FullMin.y, 8.0f, 1.0e-3f);
    const float box_top = origin + block.FullMin.y;
    const float box_bottom = origin + block.FullMax.y;
    struct Drawn {
        int quads = 0;
        int above = 0; // quads that reach above `limit_above`
        int below = 0; // quads that reach below `limit_below`
        float top = 1.0e9f;
        float bottom = -1.0e9f;
    };
    const float limit_above = box_top - 4.0f;
    const float limit_below = box_bottom + 4.0f;
    const auto draw = [&](float clip_top, float clip_bottom) {
        ImVector<ImDrawVert> vertices;
        ImVector<ImDrawIdx> indices;
        ImTypeset::DrawDocumentToBuffers(document, ImVec2(0.0f, origin),
                                         ImVec4(0.0f, clip_top, 600.0f, clip_bottom), &vertices,
                                         &indices, nullptr);
        Drawn out;
        for (int v = 0; v + 3 < vertices.Size; v += 4) {
            float top = vertices[v].pos.y;
            float bottom = top;
            for (int k = 1; k < 4; ++k) {
                top = std::min(top, vertices[v + k].pos.y);
                bottom = std::max(bottom, vertices[v + k].pos.y);
            }
            ++out.quads;
            out.above += top < limit_above ? 1 : 0;
            out.below += bottom > limit_below ? 1 : 0;
            out.top = std::min(out.top, top);
            out.bottom = std::max(out.bottom, bottom);
        }
        return out;
    };
    const Drawn whole = draw(0.0f, 400.0f);
    // The ink overhangs the box by more than the two pixels every line's box
    // is grown by, above and below.
    REQUIRE(whole.above > 0);
    REQUIRE(whole.below > 0);
    // A clip that ends 4 px above the box, and one that starts 4 px below
    // it, each show the quads that reach it: the topmost and the lowest ink
    // are where the whole draw has them.
    const Drawn over = draw(0.0f, limit_above);
    CHECK(over.quads >= whole.above);
    CHECK_EQ(over.top, whole.top);
    const Drawn under = draw(limit_below, 400.0f);
    CHECK(under.quads >= whole.below);
    CHECK_EQ(under.bottom, whole.bottom);
    // A clip further off than ink is looked for (four ems past the block's
    // box) visits nothing.
    CHECK_EQ(draw(0.0f, box_top - 80.0f).quads, 0);
    CHECK_EQ(draw(box_bottom + 80.0f, 400.0f).quads, 0);
    ImTypeset::DestroyDocument(document);
}

// The widths a held table keeps are lengths of the type they were measured
// in: a change of size, of face, of the fonts or of the padding starts them
// afresh, and a table re-described under the same style keeps them.
TEST(held_widths_do_not_outlive_the_style_they_were_measured_under)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const auto build = [](Content& content) {
        content.table({{"Quantity", "Value"}, {"a rather long first cell", "12"}},
                      ImTypesetBlockFlags_TableHeader | ImTypesetBlockFlags_TableHeld);
        content.column();
        content.column(ImTypesetAlign_Right);
    };
    const auto widths_of = [](ImTypesetDocument* document) {
        std::vector<float> out;
        for (int c = 0; c < 2; ++c) {
            const ImTypesetTableColumnGeometry column =
                ImTypeset::GetDocumentTableColumn(document, 0, c);
            out.push_back(column.Max - column.Min);
        }
        return out;
    };
    Content content;
    build(content);
    ImTypesetDocument* document = document_of(content, 600.0f);
    REQUIRE(document != nullptr);
    const std::vector<float> large = widths_of(document);
    REQUIRE(ImTypeset::GetDocumentTableGeometry(document, 0).Stated);

    // Half the size: the columns are what a table first seen at that size has.
    ImTypesetStyle small = ImTypeset::GetStyle();
    small.EmSize = 8.0f;
    REQUIRE(ImTypeset::LayoutDocument(document, 600.0f, &small));
    const std::vector<float> shrunk = widths_of(document);
    ImTypesetDocument* fresh = ImTypeset::CreateDocument();
    REQUIRE(fresh != nullptr);
    const ImTypesetDocumentContent view = content.view();
    REQUIRE(ImTypeset::SetDocumentBlocks(fresh, &view));
    REQUIRE(ImTypeset::LayoutDocument(fresh, 600.0f, &small));
    const std::vector<float> expected = widths_of(fresh);
    for (size_t c = 0; c < 2; ++c) {
        CHECK_CONTEXT("column %d", static_cast<int>(c));
        CHECK_EQ(shrunk[c], expected[c]);
        CHECK(shrunk[c] < large[c] * 0.75f);
    }
    // Back at the first size they are the first widths again, not wider.
    const ImTypesetStyle normal = ImTypeset::GetStyle();
    REQUIRE(ImTypeset::LayoutDocument(document, 600.0f, &normal));
    const std::vector<float> again = widths_of(document);
    CHECK_EQ(again[0], large[0]);
    CHECK_EQ(again[1], large[1]);

    // A change of padding starts them afresh as well: the padding is in them.
    ImTypesetDocumentOptions options;
    options.TableCellPadding = 2.0f;
    REQUIRE(ImTypeset::SetDocumentOptions(document, &options));
    const std::vector<float> tight = widths_of(document);
    CHECK_NEAR(tight[0], large[0] - 12.0f, 0.01f);
    CHECK_NEAR(tight[1], large[1] - 12.0f, 0.01f);

    // Held for what they are for: a narrower table described in its place,
    // under the same style, keeps the widths.
    Content narrower;
    narrower.table({{"Q", "V"}, {"a", "1"}},
                   ImTypesetBlockFlags_TableHeader | ImTypesetBlockFlags_TableHeld);
    narrower.column();
    narrower.column(ImTypesetAlign_Right);
    const ImTypesetDocumentContent replaced = narrower.view();
    REQUIRE(ImTypeset::ReplaceDocumentBlocks(document, 0, 1, &replaced));
    const std::vector<float> kept = widths_of(document);
    CHECK_EQ(kept[0], tight[0]);
    CHECK_EQ(kept[1], tight[1]);
    ImTypeset::DestroyDocument(fresh);
    ImTypeset::DestroyDocument(document);
}

// What a table is taken to be tall before it is laid out has the rule under
// its header in it: a view scrolled to the end of a document that ends in a
// table reaches the last row the first time.
TEST(a_table_not_yet_laid_out_is_estimated_with_its_header_rule)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    for (const bool header : {true, false}) {
        CHECK_CONTEXT("header %d", header ? 1 : 0);
        Content content;
        content.paragraph("Before the table.");
        content.table(UNITS, header ? ImTypesetBlockFlags_TableHeader : 0);
        content.column();
        content.column();
        content.column();
        ImTypesetDocument* document = document_of(content);
        REQUIRE(document != nullptr);
        // Nothing has been laid out: no draw, no question about a block.
        const ImVec2 estimated = ImTypeset::GetDocumentSize(document);
        CHECK_EQ(ImTypeset::GetDocumentStats(document).BlocksLaidOut, 0);
        REQUIRE(ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e6f));
        const ImVec2 exact = ImTypeset::GetDocumentSize(document);
        const ImTypesetTableRowGeometry last = ImTypeset::GetDocumentTableRow(document, 1, 3);
        // A size is whole pixels, rounded up.
        CHECK(exact.y >= last.Max);
        CHECK(exact.y < last.Max + 1.0f);
        // Rows of one line of plain text: the estimate is the height.
        CHECK_EQ(estimated.y, exact.y);
        ImTypeset::DestroyDocument(document);
    }
}

// --- A row appended costs a row
// -------------------------------------------------------------------

namespace
{

// One row of four cells appended to the last block, the second cell in a
// colour and, every seventh row, the last cell long enough to wrap in a
// table fitted to 600 px.
bool append_counted_row(ImTypesetDocument* document, std::string& text, int r,
                        std::vector<ImTypesetCell>* all_cells, std::vector<ImTypesetRun>* all_runs)
{
    const std::string cells[4] = {
        "row " + std::to_string(r), "v" + std::to_string(r % 13), "ok",
        r % 7 == 3 ? "a remark long enough to be broken when the table is fitted to the width"
                   : "fine"};
    std::string more = text.empty() ? "" : "\n";
    std::vector<ImTypesetCell> added;
    ImTypesetRun run;
    for (int c = 0; c < 4; ++c) {
        if (c != 0)
            more += '\t';
        ImTypesetCell cell;
        cell.TextStart = static_cast<int>(text.size() + more.size());
        more += cells[c];
        cell.TextEnd = static_cast<int>(text.size() + more.size());
        cell.Flags = c == 0 ? ImTypesetCellFlags_RowStart : 0;
        added.push_back(cell);
        if (c == 1) {
            run.Start = cell.TextStart;
            run.End = cell.TextEnd;
            run.Color = IM_COL32(200, 40 + r % 100, 40, 255);
            run.Tag = r + 1;
        }
    }
    text += more;
    if (all_cells != nullptr)
        all_cells->insert(all_cells->end(), added.begin(), added.end());
    if (all_runs != nullptr)
        all_runs->push_back(run);
    return ImTypeset::AppendDocumentTableCells(document, more.data(), more.data() + more.size(),
                                               added.data(), 4, &run, 1);
}

ImTypesetDocument* empty_table(ImTypesetBlockFlags flags, float width)
{
    Content content;
    content.table({}, flags);
    content.column();
    content.column(ImTypesetAlign_Right);
    content.column(ImTypesetAlign_Center);
    content.column();
    return document_of(content, width);
}

} // namespace

// What is kept beside a table that grew a row at a time -- its lines, its
// colours, the byte each index stands for -- is what a table described whole
// has, at every size it is asked at; and so is everything a host can ask.
TEST(a_table_grown_a_row_at_a_time_is_the_table_described_whole)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    for (const bool held : {true, false}) {
        CHECK_CONTEXT("held %d", held ? 1 : 0);
        const ImTypesetBlockFlags flags =
            ImTypesetBlockFlags_TableHeader | (held ? ImTypesetBlockFlags_TableHeld : 0);
        // Held, the table is as wide as its cells and no row wraps; fitted to
        // 600 px, the long remarks wrap.
        const float width = held ? 2000.0f : 600.0f;
        ImTypesetDocument* grown = empty_table(flags, width);
        REQUIRE(grown != nullptr);
        std::string text;
        std::vector<ImTypesetCell> cells;
        std::vector<ImTypesetRun> runs;
        for (int r = 0; r < 60; ++r) {
            CHECK_CONTEXT("row %d", r);
            REQUIRE(append_counted_row(grown, text, r, &cells, &runs));
            // Text arriving in the last cell, now and then.
            if (r % 5 == 4) {
                REQUIRE(ImTypeset::AppendDocumentText(grown, " more"));
                text += " more";
                cells.back().TextEnd += 5;
            }
            // A question each step, so that what is kept is kept up.
            REQUIRE(ImTypeset::LayoutDocumentWindow(grown, 0.0f, 1.0e6f));
            ImTypeset::GetDocumentCaretRect(grown, at(grown, 0, r, 1, 1));
            if (r % 10 != 9)
                continue;

            // The same table, described whole.
            ImTypesetBlock block;
            block.Kind = ImTypesetBlockKind_Table;
            block.Flags = flags;
            block.TextEnd = static_cast<int>(text.size());
            block.CellCount = static_cast<int>(cells.size());
            block.RunCount = static_cast<int>(runs.size());
            block.ColumnCount = 4;
            ImTypesetTableColumn columns[4];
            columns[1].Align = ImTypesetAlign_Right;
            columns[2].Align = ImTypesetAlign_Center;
            ImTypesetDocumentContent content;
            content.Text = text.data();
            content.TextSize = static_cast<int>(text.size());
            content.Blocks = &block;
            content.BlockCount = 1;
            content.Cells = cells.data();
            content.CellCount = static_cast<int>(cells.size());
            content.Runs = runs.data();
            content.RunCount = static_cast<int>(runs.size());
            content.Columns = columns;
            content.ColumnCount = 4;
            ImTypesetDocument* whole_table = ImTypeset::CreateDocument();
            REQUIRE(whole_table != nullptr);
            REQUIRE(ImTypeset::SetDocumentBlocks(whole_table, &content));
            REQUIRE(ImTypeset::LayoutDocument(whole_table, width));
            REQUIRE(ImTypeset::LayoutDocumentWindow(whole_table, 0.0f, 1.0e6f));
            // A held table's widths are the widest so far in both; a fitted
            // one's are the fit of the same cells.
            ImTypeset::GetDocumentCaretRect(whole_table, at(whole_table, 0, r, 1, 1));

            // What is kept beside the layout, exactly.
            const DocBlock& a = grown->blocks[0];
            const DocBlock& b = whole_table->blocks[0];
            const BlockLayout& la = block_document(*grown).layout(0);
            const BlockLayout& lb = block_document(*whole_table).layout(0);
            REQUIRE_EQ(la.cmds.size(), lb.cmds.size());
            CHECK_EQ(la.item_count, lb.item_count);
            CHECK_EQ(la.extent, lb.extent);
            CHECK(a.table_bytes == b.table_bytes);
            if (CHECK_EQ(a.lines.spans.size(), b.lines.spans.size())) {
                for (size_t k = 0; k < a.lines.spans.size(); ++k) {
                    const LineSpan& x = a.lines.spans[k];
                    const LineSpan& y = b.lines.spans[k];
                    if (!(x.first == y.first && x.end == y.end && x.top == y.top &&
                          x.bottom == y.bottom && x.bounded == y.bounded)) {
                        FAIL("line span %d differs", static_cast<int>(k));
                        break;
                    }
                }
            }
            CHECK(a.lines.sorted == b.lines.sorted);
            if (CHECK_EQ(a.inks.size(), b.inks.size())) {
                for (size_t k = 0; k < a.inks.size(); ++k) {
                    if (!(a.inks[k].start == b.inks[k].start && a.inks[k].end == b.inks[k].end &&
                          a.inks[k].colour == b.inks[k].colour)) {
                        FAIL("ink range %d differs", static_cast<int>(k));
                        break;
                    }
                }
            }
            CHECK_EQ(a.inks.size(), static_cast<size_t>(r + 1));

            // And what a host asks: the copy, a hit, a caret, a range's
            // rectangles, the tag under a point, what is drawn.
            CHECK_EQ(whole(grown, 0), whole(whole_table, 0));
            CHECK_EQ(vertices_of(grown), vertices_of(whole_table));
            for (const int row : {0, r / 2, r}) {
                for (int c = 0; c < 4; ++c) {
                    const ImVec2 point = centre_of(grown, 0, row, c);
                    const ImVec2 other = centre_of(whole_table, 0, row, c);
                    CHECK_EQ(point.x, other.x);
                    CHECK_EQ(point.y, other.y);
                    const ImTypesetDocumentHit ha = ImTypeset::HitTestDocument(grown, point);
                    const ImTypesetDocumentHit hb = ImTypeset::HitTestDocument(whole_table, point);
                    CHECK_EQ(ha.Position.Offset, hb.Position.Offset);
                    CHECK_EQ(ha.Row, row);
                    CHECK_EQ(ha.Column, c);
                    CHECK_EQ(ha.Tag, hb.Tag);
                    const ImVec4 ca = ImTypeset::GetDocumentCaretRect(grown, ha.Position);
                    const ImVec4 cb = ImTypeset::GetDocumentCaretRect(whole_table, hb.Position);
                    CHECK(ca.x == cb.x && ca.y == cb.y && ca.w == cb.w);
                }
                ImVector<ImVec4> ra;
                ImVector<ImVec4> rb;
                const ImTypesetDocumentRange range_a = {at(grown, 0, row, 0, 0),
                                                        at(grown, 0, row, 3, 1 << 20)};
                ImTypeset::GetDocumentRangeRects(grown, range_a, &ra);
                ImTypeset::GetDocumentRangeRects(whole_table, range_a, &rb);
                if (CHECK_EQ(ra.Size, rb.Size)) {
                    for (int k = 0; k < ra.Size; ++k)
                        CHECK(ra[k].x == rb[k].x && ra[k].y == rb[k].y && ra[k].z == rb[k].z &&
                              ra[k].w == rb[k].w);
                }
            }
            ImTypeset::DestroyDocument(whole_table);
        }
        ImTypeset::DestroyDocument(grown);
    }
}

// The work of an append, by the counters: the rows a layout goes over and
// the rows whose lines and index map are made again beside it do not grow
// with the table. (What is typeset was already held to one row an append.)
TEST(a_row_appended_goes_over_that_row_and_not_the_table)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    for (const bool held : {true, false}) {
        CHECK_CONTEXT("held %d", held ? 1 : 0);
        // Held and wide: no row wraps and no column moves once the first
        // rows have said how wide the cells are. Fitted and narrow: the
        // widths are solved again each time, from the longest words too.
        ImTypesetDocument* document = empty_table(ImTypesetBlockFlags_TableHeader |
                                                      (held ? ImTypesetBlockFlags_TableHeld : 0),
                                                  held ? 2000.0f : 500.0f);
        REQUIRE(document != nullptr);
        std::string text;
        // The widest of everything first, so that no later row moves a column.
        for (int r = 3; r < 3 + 14; ++r)
            REQUIRE(append_counted_row(document, text, r, nullptr, nullptr));
        REQUIRE(append_counted_row(document, text, 9999, nullptr, nullptr));
        REQUIRE(ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e6f));
        ImTypeset::GetDocumentCaretRect(document, ImTypesetPosition{0, 0});
        DocumentComposer& composer = block_composer(*document);
        const int rows = 3000;
        uint64_t worst_visited = 0;
        uint64_t worst_synced = 0;
        uint64_t set = 0;
        const uint64_t visited_at_start = composer.table_rows_visited;
        const uint64_t synced_at_start = document->table_rows_synced;
        for (int r = 0; r < rows; ++r) {
            const uint64_t visited = composer.table_rows_visited;
            const uint64_t synced = document->table_rows_synced;
            const uint64_t rows_set = composer.table_rows_set;
            REQUIRE(append_counted_row(document, text, 100 + r % 800, nullptr, nullptr));
            REQUIRE(ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e6f));
            worst_visited = std::max(worst_visited, composer.table_rows_visited - visited);
            worst_synced = std::max(worst_synced, document->table_rows_synced - synced);
            set += composer.table_rows_set - rows_set;
        }
        const uint64_t visited = composer.table_rows_visited - visited_at_start;
        const uint64_t synced = document->table_rows_synced - synced_at_start;
        std::printf("  %s table, %d rows appended one at a time: %llu rows set, %llu rows gone "
                    "over by layout (worst append %llu), %llu rows indexed again (worst %llu)\n",
                    held ? "held" : "fitted", rows, static_cast<unsigned long long>(set),
                    static_cast<unsigned long long>(visited),
                    static_cast<unsigned long long>(worst_visited),
                    static_cast<unsigned long long>(synced),
                    static_cast<unsigned long long>(worst_synced));
        // One row set an append, a handful gone over, one indexed.
        CHECK_EQ(set, static_cast<uint64_t>(rows));
        CHECK(worst_visited <= 6);
        CHECK(visited <= static_cast<uint64_t>(rows) * 6);
        CHECK_EQ(worst_synced, uint64_t(1));
        CHECK_EQ(synced, static_cast<uint64_t>(rows));
        CHECK_EQ(ImTypeset::GetDocumentTableGeometry(document, 0).RowCount, rows + 15);
        ImTypeset::DestroyDocument(document);
    }
}
