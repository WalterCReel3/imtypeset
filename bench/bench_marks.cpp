// What marks cost a frame: a code block of 600 lines with 1,881 marks on it
// (three colour spans a line, as a syntax highlighter hands them over, and a
// few more), drawn through a window that shows some forty of the lines.
//
// The rows are one draw each: with no marks; with every mark handed over,
// sorted as a highlighter gives them; with the same marks in no order; and
// with marks that are filled behind the text. What is measured is the draw
// alone, warm: the block is laid out and its glyphs are in the atlas.

#include "bench_common.h"

#include <cstdint>

namespace
{

constexpr int LINES = 600;
constexpr int MARKS = 1881;

struct Sample {
    std::string text;
    std::vector<ImTypesetMark> marks;
};

// Lines of made-up code, each with three stretches a highlighter would
// colour, and the marks over them in the order they stand in the text.
Sample make_sample(ImTypesetMarkPlace place)
{
    Sample out;
    const ImU32 colours[3] = {IM_COL32(86, 156, 214, 255), IM_COL32(206, 145, 120, 255),
                              IM_COL32(106, 153, 85, 255)};
    for (int line = 0; line < LINES; ++line) {
        char buffer[160];
        const int offset = static_cast<int>(out.text.size());
        const int size =
            std::snprintf(buffer, sizeof buffer,
                          "    const value_%03d = compute(\"item %03d\", %d);  // line %d\n", line,
                          line, line * 7, line);
        out.text.append(buffer, static_cast<size_t>(size));
        const int spans[4][2] = {{4, 9}, {32, 42}, {size - 12, size - 1}, {10, 19}};
        // Three a line, and a fourth on enough lines to make the count.
        const int count = line < MARKS - 3 * LINES ? 4 : 3;
        int order[4] = {0, 3, 1, 2};
        for (int k = 0; k < 4; ++k) {
            const int s = order[k];
            if (s == 3 && count == 3)
                continue;
            ImTypesetMark mark;
            mark.Range.Start.Block = 0;
            mark.Range.Start.Offset = offset + spans[s][0];
            mark.Range.End.Block = 0;
            mark.Range.End.Offset = offset + spans[s][1];
            mark.Color = colours[s % 3];
            mark.Place = place;
            out.marks.push_back(mark);
        }
    }
    return out;
}

ImTypesetDocument* code_document(const std::string& text, float width)
{
    ImTypesetBlock block;
    block.Kind = ImTypesetBlockKind_Code;
    block.TextStart = 0;
    block.TextEnd = static_cast<int>(text.size());
    ImTypesetDocumentContent content;
    content.Text = text.data();
    content.TextSize = static_cast<int>(text.size());
    content.Blocks = &block;
    content.BlockCount = 1;
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    ImTypeset::SetDocumentBlocks(document, &content);
    ImTypeset::LayoutDocument(document, width);
    ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e9f);
    return document;
}

void bench_draw(const char* what, ImTypesetDocument* document, float width,
                const std::vector<ImTypesetMark>& marks, const char* note)
{
    const float height = ImTypeset::GetDocumentSize(document).y;
    ImTypesetDocumentDraw draw;
    draw.Marks = marks.empty() ? nullptr : marks.data();
    draw.MarkCount = static_cast<int>(marks.size());
    bench::row(what,
               bench::sample_frames(
                   2000, 50,
                   [&](ImDrawList* list) {
                       list->PushClipRect(ImVec2(0.0f, 0.0f), ImVec2(width, bench::WINDOW_PX));
                       ImTypeset::DrawDocument(document, list, ImVec2(0.0f, -height * 0.5f), &draw);
                       list->PopClipRect();
                   }),
               note);
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
    bench::header("bench_marks");
    const float width = 4.0f * bench::px_of_pt(bench::MEASURE_PT);

    const Sample ink = make_sample(ImTypesetMarkPlace_Ink);
    const Sample behind = make_sample(ImTypesetMarkPlace_Behind);
    // The same marks in an order that is no order: a fixed shuffle.
    std::vector<ImTypesetMark> shuffled = ink.marks;
    uint32_t state = 12345;
    for (size_t i = shuffled.size(); i > 1; --i) {
        state = state * 1664525u + 1013904223u;
        std::swap(shuffled[i - 1], shuffled[(state >> 8) % i]);
    }

    ImTypesetDocument* document = code_document(ink.text, width);
    char title[128];
    std::snprintf(title, sizeof title,
                  "a code block of %d lines through a window of %.0f px, %zu marks", LINES,
                  static_cast<double>(bench::WINDOW_PX), ink.marks.size());
    bench::table_head(title);
    bench_draw("drawn with no marks", document, width, {}, "");
    bench_draw("drawn with every mark, Ink, in text order", document, width, ink.marks, "");
    bench_draw("drawn with every mark, Ink, shuffled", document, width, shuffled,
               "the same picture");
    bench_draw("drawn with every mark, Behind, in text order", document, width, behind.marks, "");
    ImTypeset::DestroyDocument(document);

    ImTypeset::DestroyContext();
    ImGui::DestroyContext();
    return 0;
}
