// A specimen: the same text set several ways, rendered with no display and no
// GPU by the software renderer the tests use, one small PNG per page.
//
//     example_specimen                 runs every page, writes nothing
//     example_specimen docs/images     also writes <page>.png there
//
// It also prints, for the paragraphs marked `breaks`, the text of every line,
// so that the line breaks can be held against the Python reference's.
//
// The pages are small on purpose: the PNG writer does not compress.

#include "imgui.h"
#include "imtypeset.h"

#include "support/soft_raster.h"

#include <cstdio>
#include <cstring>
#include <string>

#ifndef IMTYPESET_TEST_DATA_DIR
#define IMTYPESET_TEST_DATA_DIR "tests/data"
#endif

namespace
{

const char PROSE[] =
    "A formula written between dollar signs is set by the math engine and sits in its line "
    "as one box. The subset is small on purpose: enough for everyday output, with no macro "
    "processor behind it. Everything on this page is ordinary text, converted with math "
    "switched on, which is off unless a host asks for it.";

const char LINE_MATH[] =
    "A formula of plain letters, such as $x$ or $a + b = c$, is no taller than the words "
    "around it, and the lines keep their leading. Scripts hang from the same baseline as "
    "the text: the letter in $x^2$ and in $x_i$ stands where a letter of the sentence "
    "would, and $x_i^2$ carries both at once. A fraction is another matter. The fraction "
    "$\\frac{a}{b}$ needs more room than a line of words has, so its line is set further "
    "from its neighbours, and a fraction of fractions, "
    "$\\frac{\\frac{a}{b}}{\\frac{c}{d}}$, pushes them further again.";

const char INLINE_SUM[] =
    "Inline, a sum keeps its limits beside it, as in "
    "$\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2}$, so the line is disturbed as little as "
    "possible. An integral keeps its bounds beside it, $\\int_0^1 x^2 dx = \\frac{1}{3}$, "
    "and a fitted pair of parentheses, $\\left(\\frac{a}{b}\\right)$, is taller than a "
    "typed pair, $(\\frac{a}{b})$. The sign in $\\sqrt{x}$ and the sign in "
    "$\\sqrt{\\frac{a}{b}}$ are two outlines. Words in a formula are upright: "
    "$x = 1 \\text{ if } y > 0$.";

const char PRICES[] =
    "It cost $5 and $10, and neither is a formula; an escaped dollar, \\$ like this, is a "
    "dollar too. Matrices are refused, $\\begin{matrix} a & b \\end{matrix}$, and so are "
    "accents, $\\hat{x}$: the source is shown as typed.";

const char DISPLAY_SUM[] = "$$\n\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2}\n$$";
const char DISPLAY_INT[] = "$$\\int_0^1 x^2 dx = \\frac{1}{3}$$";
const char DISPLAY_TALL[] =
    "$$\n\\left( \\sum_{i=1}^{n} \\frac{a_i}{b_i} \\right)^2 \\le \\sqrt{n} \\quad "
    "\\text{for every } n\n$$";
const char DISPLAY_REFUSED[] = "$$\\begin{matrix} a & b \\\\ c & d \\end{matrix}$$";

const ImU32 WHITE = IM_COL32(255, 255, 255, 255);
const ImU32 GUIDE = IM_COL32(255, 96, 96, 255);
const float EM = 16.0f; // The reference theme's body size.

// A caption in small grey type, set by ImGui itself.
void caption(const char* text)
{
    ImGui::PushFont(nullptr, 13.0f);
    ImGui::TextDisabled("%s", text);
    ImGui::PopFont();
}

// A vertical hairline through the window at `x`, behind the text: where a
// margin is, so that a line ending short of it or past it can be seen.
void guide(float x, float y0, float y1)
{
    ImGui::GetWindowDrawList()->AddLine(ImVec2(x + 0.5f, y0), ImVec2(x + 0.5f, y1), GUIDE);
}

// One paragraph through the widget, `width` wide, between two margin guides.
void column(const char* label, const char* text, float width, ImTypesetAlign align,
            ImTypesetTextFlags flags = 0)
{
    ImGui::BeginChild(label, ImVec2(width, 0.0f), ImGuiChildFlags_AutoResizeY);
    caption(label);
    // The context's style with one thing changed, for this call only.
    ImTypesetStyle style = ImTypeset::GetStyle();
    style.Align = align;
    ImTypeset::TextUnformatted(text, nullptr, flags, 0.0f, &style);
    const ImVec2 lo = ImGui::GetItemRectMin();
    const ImVec2 hi = ImGui::GetItemRectMax();
    ImGui::EndChild();
    // In the parent's draw list: the child clips at its own edges.
    guide(lo.x - 1.0f, lo.y, hi.y);
    guide(lo.x + width, lo.y, hi.y);
}

// The text of each line of a laid-out paragraph, to stdout.
void print_breaks(const char* name, const char* text, float width, ImTypesetTextFlags flags)
{
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    ImTypeset::SetParagraphText(p, text, nullptr, nullptr, 0, flags);
    ImTypesetStyle style;
    style.EmSize = EM;
    ImTypeset::LayoutParagraph(p, width, &style);
    const int lines = ImTypeset::GetParagraphLineCount(p);
    std::printf("breaks %s width %g lines %d\n", name, (double)width, lines);
    for (int n = 0; n < lines; ++n) {
        // A line knows which bytes of the text are on it.
        const ImTypesetLine line = ImTypeset::GetParagraphLine(p, n);
        const int start = n == 0 ? 0 : line.TextStart;
        std::printf("  |%.*s|\n", line.TextEnd - start, text + start);
    }
    ImTypeset::DestroyParagraph(p);
}

// --- Pages ---------------------------------------------------------------------------

// ImGui's own wrapping beside the same paragraph ragged and justified, at the
// same font size (ImGui's: the height of a line).
void page_paragraph()
{
    ImTypeset::GetStyle() = ImTypesetStyle();
    ImGui::PushFont(ImTypeset::GetFont(ImTypesetFace_Regular), 19.0f);

    ImGui::BeginChild("wrapped", ImVec2(250.0f, 0.0f), ImGuiChildFlags_AutoResizeY);
    caption("ImGui::TextWrapped");
    ImGui::TextWrapped("%s", PROSE);
    ImGui::EndChild();
    ImGui::SameLine(0.0f, 20.0f);
    column("ImTypeset::Text, ragged", PROSE, 250.0f, ImTypesetAlign_Left);
    ImGui::SameLine(0.0f, 20.0f);
    column("ImTypeset::Text, justified", PROSE, 250.0f, ImTypesetAlign_Justify);

    ImGui::PopFont();
}

void page_widths()
{
    ImTypeset::GetStyle() = ImTypesetStyle();
    ImTypeset::GetStyle().EmSize = EM;
    column("justified, 170 px", PROSE, 170.0f, ImTypesetAlign_Justify);
    ImGui::SameLine(0.0f, 20.0f);
    column("justified, 240 px", PROSE, 240.0f, ImTypesetAlign_Justify);
    ImGui::SameLine(0.0f, 20.0f);
    column("justified, 330 px", PROSE, 330.0f, ImTypesetAlign_Justify);
}

// Runs: faces, a size and a colour inside one paragraph, on one baseline.
void page_faces()
{
    struct Piece {
        const char* Text;
        ImTypesetFace Face;
        float Scale;
        ImU32 Color;
        ImTypesetRunFlags Flags;
    };
    static const Piece pieces[] = {
        {"Hxg regular, ", ImTypesetFace_Default, 1.0f, 0, 0},
        {"Hxg bold, ", ImTypesetFace_Bold, 1.0f, 0, 0},
        {"Hxg italic, ", ImTypesetFace_Italic, 1.0f, 0, 0},
        {"Hxg bold italic, ", ImTypesetFace_BoldItalic, 1.0f, 0, 0},
        {"Hxg code()", ImTypesetFace_Code, 1.0f, 0, ImTypesetRunFlags_NoBreak},
        {", ", ImTypesetFace_Default, 1.0f, 0, 0},
        {"Hxg large", ImTypesetFace_Default, 1.5f, 0, 0},
        {", ", ImTypesetFace_Default, 1.0f, 0, 0},
        {"Hxg small", ImTypesetFace_Default, 0.75f, 0, 0},
        {", and ", ImTypesetFace_Default, 1.0f, 0, 0},
        {"Hxg in colour", ImTypesetFace_Bold, 1.0f, IM_COL32(176, 32, 32, 255), 0},
        {". ", ImTypesetFace_Default, 1.0f, 0, 0},
        {"Then the paragraph goes on in the regular face for long enough to wrap, with an ",
         ImTypesetFace_Default, 1.0f, 0, 0},
        {"italic phrase that runs across a line break", ImTypesetFace_Italic, 1.0f, 0, 0},
        {" and a ", ImTypesetFace_Default, 1.0f, 0, 0},
        {"code_span(that, does, not, break)", ImTypesetFace_Code, 1.0f, 0,
         ImTypesetRunFlags_NoBreak},
        {" before it ends, AV To WA.", ImTypesetFace_Default, 1.0f, 0, 0},
    };
    std::string text;
    ImVector<ImTypesetRun> runs;
    for (const Piece& piece : pieces) {
        ImTypesetRun run;
        run.Start = (int)text.size();
        text += piece.Text;
        run.End = (int)text.size();
        run.Face = piece.Face;
        run.Scale = piece.Scale;
        run.Color = piece.Color;
        run.Flags = piece.Flags;
        runs.push_back(run);
    }

    ImTypesetStyle style;
    style.EmSize = EM;
    const float width = 430.0f;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    ImTypeset::SetParagraphText(p, text.c_str(), nullptr, runs.Data, runs.Size);
    ImTypeset::LayoutParagraph(p, width, &style);
    const ImVec2 size = ImTypeset::GetParagraphSize(p);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    guide(pos.x - 1.0f, pos.y, pos.y + size.y);
    guide(pos.x + width, pos.y, pos.y + size.y);
    ImTypeset::DrawParagraph(p, ImGui::GetWindowDrawList(), pos);
    ImGui::Dummy(size);
    ImTypeset::DestroyParagraph(p);
}

void page_math_inline()
{
    ImTypeset::GetStyle() = ImTypesetStyle();
    ImTypeset::GetStyle().EmSize = EM;
    column("inline formulas", LINE_MATH, 440.0f, ImTypesetAlign_Justify, ImTypesetTextFlags_Math);
    ImGui::Spacing();
    column("inline operators, fractions, delimiters, roots", INLINE_SUM, 440.0f,
           ImTypesetAlign_Justify, ImTypesetTextFlags_Math);
}

void page_math_display()
{
    ImTypeset::GetStyle() = ImTypesetStyle();
    ImTypeset::GetStyle().EmSize = EM;
    column("display: a sum with limits", DISPLAY_SUM, 400.0f, ImTypesetAlign_Justify,
           ImTypesetTextFlags_Math);
    column("display: an integral", DISPLAY_INT, 400.0f, ImTypesetAlign_Justify,
           ImTypesetTextFlags_Math);
    column("display: fitted delimiters, a root, words", DISPLAY_TALL, 400.0f,
           ImTypesetAlign_Justify, ImTypesetTextFlags_Math);
    column("display: refused (a matrix), shown as its source", DISPLAY_REFUSED, 400.0f,
           ImTypesetAlign_Justify, ImTypesetTextFlags_Math);
}

// Formulas that are not formulas, and formulas that cannot be set, with what
// the paragraph says about each.
void page_math_refused()
{
    ImTypesetStyle style;
    style.EmSize = EM;
    const float width = 440.0f;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    ImTypeset::SetParagraphText(p, PRICES, nullptr, nullptr, 0, ImTypesetTextFlags_Math);
    ImTypeset::LayoutParagraph(p, width, &style);
    const ImVec2 size = ImTypeset::GetParagraphSize(p);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    guide(pos.x - 1.0f, pos.y, pos.y + size.y);
    guide(pos.x + width, pos.y, pos.y + size.y);
    ImTypeset::DrawParagraph(p, ImGui::GetWindowDrawList(), pos);
    ImGui::Dummy(size);

    ImGui::Spacing();
    for (int i = 0; i < ImTypeset::GetParagraphFormulaCount(p); ++i) {
        const ImTypesetFormula formula = ImTypeset::GetParagraphFormula(p, i);
        ImGui::PushFont(nullptr, 13.0f);
        ImGui::PushTextWrapPos(width);
        ImGui::TextDisabled("%.*s: %s", formula.TextEnd - formula.TextStart,
                            PRICES + formula.TextStart, formula.Refused ? formula.Reason : "set");
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
    }
    ImTypeset::DestroyParagraph(p);
}

// A drag from one point to another, by words, and what it would copy.
void page_selection()
{
    static const char text[] =
        "Select across a formula and it is taken or left as a whole: the sum "
        "$\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2}$ has no caret position inside it. Copying "
        "gives back the source between its dollar signs.";
    ImTypesetStyle style;
    style.EmSize = EM;
    style.SelectionColor = IM_COL32(120, 170, 255, 255);
    const float width = 400.0f;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    ImTypeset::SetParagraphText(p, text, nullptr, nullptr, 0, ImTypesetTextFlags_Math);
    ImTypeset::LayoutParagraph(p, width, &style);
    const ImVec2 size = ImTypeset::GetParagraphSize(p);

    // What a host does with the mouse: positions relative to the paragraph.
    const ImVec2 anchor(150.0f, 12.0f);
    const ImVec2 active(250.0f, 60.0f);
    // A range of the text, in bytes: it would be the same words at any width.
    const ImTypesetTextRange selection =
        ImTypeset::SelectParagraphRange(p, anchor, active, ImTypesetGranularity_Word);
    ImGuiTextBuffer copied;
    ImTypeset::AppendParagraphRangeText(p, selection, &copied);

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImTypeset::DrawParagraph(p, draw_list, pos, &selection);
    // The two ends of the drag.
    draw_list->AddCircleFilled(ImVec2(pos.x + anchor.x, pos.y + anchor.y), 2.5f, GUIDE);
    draw_list->AddCircleFilled(ImVec2(pos.x + active.x, pos.y + active.y), 2.5f, GUIDE);
    ImGui::Dummy(size);
    ImTypeset::DestroyParagraph(p);

    ImGui::Spacing();
    caption("a drag by words between the two dots copies:");
    ImGui::PushFont(ImTypeset::GetFont(ImTypesetFace_Code), 15.0f);
    ImGui::PushTextWrapPos(width);
    ImGui::TextUnformatted(copied.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
}

struct Page {
    const char* Name;
    int Width;
    int Height;
    void (*Body)();
};

const Page PAGES[] = {
    {"paragraph", 814, 330, page_paragraph},
    {"widths", 804, 330, page_widths},
    {"faces", 454, 150, page_faces},
    {"math-inline", 464, 400, page_math_inline},
    {"math-display", 424, 330, page_math_display},
    {"math-refused", 464, 190, page_math_refused},
    {"selection", 424, 190, page_selection},
};

bool render_page(const Page& page, soft_raster::Renderer& renderer, const char* out_dir)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)page.Width, (float)page.Height);
    soft_raster::Image image(page.Width, page.Height, WHITE);
    // More than one frame: a window draws nothing on the frame it appears,
    // and an auto-sized child takes its height from the frame before.
    for (int frame = 0; frame < 4; ++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin(page.Name, nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoMove);
        page.Body();
        ImGui::End();
        ImGui::Render();
        image.clear(WHITE);
        renderer.render(ImGui::GetDrawData(), image);
    }
    if (out_dir == nullptr)
        return true;
    const std::string path = std::string(out_dir) + "/" + page.Name + ".png";
    if (!soft_raster::write_png(path.c_str(), image)) {
        std::fprintf(stderr, "specimen: cannot write %s\n", path.c_str());
        return false;
    }
    std::printf("specimen: wrote %s (%d x %d)\n", path.c_str(), page.Width, page.Height);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    const char* out_dir = argc > 1 ? argv[1] : nullptr;
    const std::string fonts = IMTYPESET_TEST_DATA_DIR "/fonts/";

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::StyleColorsLight();
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(WHITE);
    ImGui::GetStyle().WindowPadding = ImVec2(12.0f, 10.0f);
    ImGui::GetStyle().WindowBorderSize = 0.0f;
    soft_raster::Renderer renderer;
    renderer.init();

    static const struct {
        ImTypesetFace Face;
        const char* File;
    } faces[] = {
        {ImTypesetFace_Regular, "Roboto-Regular.ttf"},
        {ImTypesetFace_Bold, "Roboto-Bold.ttf"},
        {ImTypesetFace_Italic, "Roboto-RegularItalic.ttf"},
        {ImTypesetFace_BoldItalic, "Roboto-BoldItalic.ttf"},
        {ImTypesetFace_Code, "Inconsolata-Medium.ttf"},
        {ImTypesetFace_Math, "latinmodern-math.otf"},
    };
    bool ok = IMTYPESET_CHECKVERSION() && ImTypeset::CreateContext() != nullptr;
    for (int i = 0; ok && i < IM_ARRAYSIZE(faces); ++i)
        ok = ImTypeset::AddFontFromFileTTF(faces[i].Face, (fonts + faces[i].File).c_str()) !=
             nullptr;
    if (!ok) {
        std::fprintf(stderr, "specimen: %s\n", ImTypeset::GetLastError()->Message);
        return 1;
    }
    std::printf("specimen: font loader %s, ImWchar %d bits\n", io.Fonts->FontLoaderName,
                (int)sizeof(ImWchar) * 8);

    for (const Page& page : PAGES)
        ok = render_page(page, renderer, out_dir) && ok;

    for (float width = 150.0f; width <= 500.0f; width += 10.0f) {
        print_breaks("prose", PROSE, width, 0);
        print_breaks("line-math", LINE_MATH, width, ImTypesetTextFlags_Math);
        print_breaks("inline-sum", INLINE_SUM, width, ImTypesetTextFlags_Math);
    }

    ImTypeset::DestroyContext();
    renderer.shutdown();
    ImGui::DestroyContext();
    return ok ? 0 : 1;
}
