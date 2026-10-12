// What measuring code on the device costs (ImTypesetAdvances_, src/device.h):
// a document of 5,000 lines of code, as one block and as 250 blocks of twenty
// lines, which is nearer a diff, measured by the device's advances (the
// default for the Code faces) and by the font's.
//
// The rows:
//   - laid out whole, cold: the blocks handed over and every one of them laid
//     out, with nothing memoised for the size: each sample is at a size no
//     sample before it used, a hundredth of a pixel apart, so the device rows
//     ask the atlas for each character's advance once more. The atlas bakes
//     a glyph the first time a whole pixel size is met, which here is the
//     first sample of a run; the minimum of the row is without that;
//   - laid out whole, warm: the same at a size already met;
//   - a frame of a window, steady: one draw of what a window shows, nothing
//     having changed;
//   - the first frame after the display's density changed: what a window
//     moved to another display costs, with no call but the draw. Under the
//     font's advances nothing is laid out again and the row is a steady frame.

#include "bench_common.h"

namespace
{

constexpr int LINES = 5000;
constexpr int BLOCK_LINES = 20;

std::string make_text(std::vector<ImTypesetBlock>& one, std::vector<ImTypesetBlock>& many)
{
    std::string text;
    int block_start = 0;
    for (int line = 0; line < LINES; ++line) {
        char buffer[160];
        const int size =
            std::snprintf(buffer, sizeof buffer,
                          "    const value_%04d = compute(\"item %04d\", %d);  // line %d\n", line,
                          line, line * 7, line);
        text.append(buffer, static_cast<size_t>(size));
        if ((line + 1) % BLOCK_LINES == 0) {
            ImTypesetBlock block;
            block.Kind = ImTypesetBlockKind_Code;
            block.TextStart = block_start;
            block.TextEnd = static_cast<int>(text.size());
            many.push_back(block);
            block_start = block.TextEnd;
        }
    }
    ImTypesetBlock whole;
    whole.Kind = ImTypesetBlockKind_Code;
    whole.TextStart = 0;
    whole.TextEnd = static_cast<int>(text.size());
    one.push_back(whole);
    return text;
}

ImTypesetStyle style_of(ImTypesetAdvances advances, float size)
{
    ImTypesetStyle style = ImTypeset::GetStyle();
    style.EmSize = size;
    style.Advances = advances;
    return style;
}

void set_blocks(ImTypesetDocument* document, const std::string& text,
                const std::vector<ImTypesetBlock>& blocks)
{
    ImTypesetDocumentContent content;
    content.Text = text.data();
    content.TextSize = static_cast<int>(text.size());
    content.Blocks = blocks.data();
    content.BlockCount = static_cast<int>(blocks.size());
    ImTypeset::SetDocumentBlocks(document, &content);
}

void bench_one(const char* shape, const std::string& text,
               const std::vector<ImTypesetBlock>& blocks, ImTypesetAdvances advances, float* size)
{
    const float width = 4.0f * bench::px_of_pt(bench::MEASURE_PT);
    const char* const measured =
        advances == ImTypesetAdvances_Font ? "the font's advances" : "the device's advances";
    char what[160];
    ImTypesetDocument* document = ImTypeset::CreateDocument();

    std::snprintf(what, sizeof what, "%s, %s: laid out whole, cold", shape, measured);
    bench::row(what,
               bench::sample(12, 0,
                             [&]() {
                                 *size += 0.01f;
                                 const ImTypesetStyle style = style_of(advances, *size);
                                 set_blocks(document, text, blocks);
                                 ImTypeset::LayoutDocument(document, width, &style);
                                 ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e9f);
                             }),
               "a size not met before each time");

    const ImTypesetStyle style = style_of(advances, bench::EM_PX);
    std::snprintf(what, sizeof what, "%s, %s: laid out whole, warm", shape, measured);
    bench::row(what, bench::sample(12, 2, [&]() {
                   set_blocks(document, text, blocks);
                   ImTypeset::LayoutDocument(document, width, &style);
                   ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e9f);
               }));

    const float height = ImTypeset::GetDocumentSize(document).y;
    const auto draw = [&](ImDrawList* list) {
        list->PushClipRect(ImVec2(0.0f, 0.0f), ImVec2(width, bench::WINDOW_PX));
        ImTypeset::DrawDocument(document, list, ImVec2(0.0f, -height * 0.5f));
        list->PopClipRect();
    };
    std::snprintf(what, sizeof what, "%s, %s: a frame of a window, steady", shape, measured);
    bench::row(what, bench::sample_frames(2000, 50, draw));

    // Alternately on a display of density 2 and of density 1: each frame
    // measured is the first on its display.
    std::snprintf(what, sizeof what, "%s, %s: the first frame on another display", shape, measured);
    int flips = 0;
    bench::row(what,
               bench::sample_frames(200, 4,
                                    [&](ImDrawList* list) {
                                        const float density = (flips++ % 2) == 0 ? 2.0f : 1.0f;
                                        ImGui::GetIO().DisplayFramebufferScale =
                                            ImVec2(density, density);
                                        draw(list);
                                    }),
               "density 2 and 1 in turn");
    ImGui::GetIO().DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    const ImTypesetDocumentStats stats = ImTypeset::GetDocumentStats(document);
    std::printf("| %s, %s: blocks laid out over all of the above | | %llu | | | blocks | "
                "structure visits %llu |\n",
                shape, measured, static_cast<unsigned long long>(stats.BlocksLaidOut),
                static_cast<unsigned long long>(stats.StructureVisits));
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
    bench::header("bench_code");

    std::vector<ImTypesetBlock> one, many;
    const std::string text = make_text(one, many);
    char title[160];
    std::snprintf(title, sizeof title,
                  "%d lines of code, %zu bytes, through a window of %.0f px: one block, and %zu "
                  "blocks of %d lines",
                  LINES, text.size(), static_cast<double>(bench::WINDOW_PX), many.size(),
                  BLOCK_LINES);
    bench::table_head(title);
    // Sizes no row has used: each cold sample takes the next hundredth.
    float size = 17.0f;
    for (const ImTypesetAdvances advances : {ImTypesetAdvances_Default, ImTypesetAdvances_Font}) {
        bench_one("one block", text, one, advances, &size);
        bench_one("250 blocks", text, many, advances, &size);
    }

    ImTypeset::DestroyContext();
    ImGui::DestroyContext();
    return 0;
}
