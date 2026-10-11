// Tables in a Document: what a table costs to set, and what a row costs when
// it arrives in a table that is already set (ImTypesetBlockFlags_TableHeld).
// The second is the claim of "A TABLE THAT GROWS" in imtypeset.h: a row that
// moves no column sets that row and no other, however long the table is.
//
// Take timings from the release preset only (CLAUDE.md).

#include "bench_common.h"

#include "imtypeset.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{

const char* const WORDS[] = {"alpha", "beta",  "gamma", "delta", "epsilon", "zeta",
                             "eta",   "theta", "iota",  "kappa", "lambda",  "mu"};

// Row `r` of a four-column table: a number, two short cells and a phrase, none
// wider than the header's.
void row_text(int r, std::string& text, std::vector<ImTypesetCell>& cells, size_t base)
{
    const std::string parts[4] = {std::to_string(r % 1000), WORDS[r % 12], WORDS[(r * 7) % 12],
                                  std::string(WORDS[(r * 5) % 12]) + " and " + WORDS[r % 7]};
    for (int c = 0; c < 4; ++c) {
        text += c == 0 ? (text.size() > base ? "\n" : "") : "\t";
        ImTypesetCell cell;
        cell.TextStart = static_cast<int>(text.size() - base);
        text += parts[c];
        cell.TextEnd = static_cast<int>(text.size() - base);
        cell.Flags = c == 0 ? ImTypesetCellFlags_RowStart : 0;
        cells.push_back(cell);
    }
}

ImTypesetDocument* table_document(int rows, ImTypesetBlockFlags flags, float width)
{
    std::string text = "Number\tA wide first heading\tA wide second heading\t"
                       "The widest heading of the four, by far";
    std::vector<ImTypesetCell> cells(4);
    size_t at = 0;
    for (int c = 0; c < 4; ++c) {
        const size_t end = text.find('\t', at);
        cells[static_cast<size_t>(c)].TextStart = static_cast<int>(at);
        cells[static_cast<size_t>(c)].TextEnd =
            static_cast<int>(end == std::string::npos ? text.size() : end);
        at = end == std::string::npos ? text.size() : end + 1;
    }
    cells[0].Flags = ImTypesetCellFlags_RowStart;
    for (int r = 0; r < rows; ++r)
        row_text(r, text, cells, 0);
    ImTypesetBlock block;
    block.Kind = ImTypesetBlockKind_Table;
    block.Flags = flags | ImTypesetBlockFlags_TableHeader;
    block.TextEnd = static_cast<int>(text.size());
    block.CellCount = static_cast<int>(cells.size());
    ImTypesetDocumentContent content;
    content.Text = text.data();
    content.TextSize = static_cast<int>(text.size());
    content.Blocks = &block;
    content.BlockCount = 1;
    content.Cells = cells.data();
    content.CellCount = static_cast<int>(cells.size());
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    ImTypeset::SetDocumentBlocks(document, &content);
    ImTypeset::LayoutDocument(document, width);
    return document;
}

void bench_first_layout(int rows, float width, const char* what)
{
    char name[96];
    std::snprintf(name, sizeof name, "%s, %d rows of 4 cells: describe and lay out", what, rows);
    bench::row(name, bench::sample(rows >= 10000 ? 5 : 30, 1, [&] {
                   ImTypesetDocument* document = table_document(rows, 0, width);
                   ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e9f);
                   ImTypeset::DestroyDocument(document);
               }));
}

// One row appended to a held table of `rows` rows and the table laid out.
void bench_append(int rows, float width, bool wider)
{
    ImTypesetDocument* document = table_document(rows, ImTypesetBlockFlags_TableHeld, width);
    ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e9f);
    int next = rows;
    std::string grow = "x";
    const ImTypesetDocumentStats before = ImTypeset::GetDocumentStats(document);
    const int count = wider ? 20 : 200;
    bench::Samples samples = bench::sample(count, 0, [&] {
        std::string text;
        std::vector<ImTypesetCell> cells;
        // Offsets are into the table's whole text.
        const size_t held = static_cast<size_t>(ImTypeset::GetDocumentBlockTextSize(document, 0));
        text = "\n";
        row_text(next++, text, cells, 1);
        if (wider) {
            // A cell wider than any before it: every row is set again.
            grow += "wider ";
            text += " " + grow + grow + grow + grow + grow + grow;
            cells.back().TextEnd = static_cast<int>(text.size() - 1);
        }
        for (ImTypesetCell& cell : cells) {
            cell.TextStart += static_cast<int>(held) + 1;
            cell.TextEnd += static_cast<int>(held) + 1;
        }
        ImTypeset::AppendDocumentTableCells(document, text.data(), text.data() + text.size(),
                                            cells.data(), static_cast<int>(cells.size()));
        ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e9f);
    });
    const ImTypesetDocumentStats after = ImTypeset::GetDocumentStats(document);
    char name[128];
    char note[160];
    std::snprintf(name, sizeof name, "a row appended to a held table of %d rows%s", rows,
                  wider ? ", a column growing" : "");
    std::snprintf(note, sizeof note, "%.1f rows set and %.1f cells measured an append",
                  static_cast<double>(after.TableRowsSet - before.TableRowsSet) / count,
                  static_cast<double>(after.TableCellsMeasured - before.TableCellsMeasured) /
                      count);
    bench::row(name, samples, note);
    ImTypeset::DestroyDocument(document);
}

void bench_draw(int rows, float width)
{
    ImTypesetDocument* document = table_document(rows, 0, width);
    ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e9f);
    const float height = ImTypeset::GetDocumentSize(document).y;
    char name[96];
    std::snprintf(name, sizeof name, "a window of %.0f px in the middle of %d rows, drawn",
                  static_cast<double>(bench::WINDOW_PX), rows);
    bench::row(name, bench::sample_frames(500, 20, [&](ImDrawList* list) {
                   list->PushClipRect(ImVec2(0.0f, 0.0f), ImVec2(width, bench::WINDOW_PX));
                   ImTypeset::DrawDocument(document, list, ImVec2(0.0f, -height * 0.5f));
                   list->PopClipRect();
               }));
    ImTypeset::DestroyDocument(document);
}

} // namespace

int main()
{
    bench::create_imgui(4000.0f, 4000000.0f);
    if (ImTypeset::CreateContext() == nullptr || !bench::add_fonts()) {
        std::fprintf(stderr, "%s\n", ImTypeset::GetLastError()->Message);
        ImGui::DestroyContext();
        return 1;
    }
    bench::reference_style();
    bench::header("bench_tables");
    const float width = bench::px_of_pt(bench::MEASURE_PT);

    bench::table_head("a table set whole");
    bench_first_layout(100, width, "fitted to the measure (every row wraps)");
    bench_first_layout(1000, width, "fitted to the measure (every row wraps)");
    bench_first_layout(10000, width, "fitted to the measure (every row wraps)");
    bench_first_layout(1000, 4.0f * width, "at four times the measure (no row wraps)");
    bench_first_layout(10000, 4.0f * width, "at four times the measure (no row wraps)");

    bench::table_head("a row arriving in a held table, four times the measure wide");
    for (const int rows : {10, 100, 1000, 10000})
        bench_append(rows, 4.0f * width, false);
    bench_append(100, 16.0f * width, true);
    bench_append(1000, 16.0f * width, true);

    bench::table_head("a table drawn");
    bench_draw(100, 4.0f * width);
    bench_draw(10000, 4.0f * width);

    ImTypeset::DestroyContext();
    ImGui::DestroyContext();
    return 0;
}
