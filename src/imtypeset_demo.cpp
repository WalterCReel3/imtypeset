// imtypeset: the demo window.
//
// ImTypeset::ShowDemoWindow() is to this library what ImGui::ShowDemoWindow()
// is to Dear ImGui: a window that shows what there is, and whose source is
// where to look up how each thing is called. Every section below is a function
// of a screen or less that uses only imgui.h and imtypeset.h, written to be
// copied from.
//
// Calling it: between ImGui::NewFrame() and ImGui::Render(), with an ImTypeset
// context current. It adds no font: it shows the faces the host gave, and its
// Fonts section can add more. A host with no fonts yet still gets a window,
// which says so.
//
// Leaving it out: define IMTYPESET_DISABLE_DEMO for this file and the function
// is still there to link against and draws nothing.
//
// Index of this file:
//   [SECTION] Helpers
//   [SECTION] Paragraph
//   [SECTION] Faces and sizes
//   [SECTION] Math
//   [SECTION] Selection
//   [SECTION] Inspector
//   [SECTION] Stress
//   [SECTION] Fonts
//   [SECTION] Known limits
//   [SECTION] Document
//   [SECTION] ShowDemoWindow()

#include "imgui.h"
#include "imtypeset.h"

#ifdef IMTYPESET_DISABLE_DEMO

void ImTypeset::ShowDemoWindow(bool*) {}

#else

#include <string.h> // strlen, strchr, memcpy

namespace
{

//-----------------------------------------------------------------------------
// [SECTION] Helpers
//-----------------------------------------------------------------------------

const char PROSE[] =
    "A formula written between dollar signs is set by the math engine and sits in its line "
    "as one box. The subset is small on purpose: enough for everyday output, with no macro "
    "processor behind it. Everything on this page is ordinary text, converted with math "
    "switched on, which is off unless a host asks for it.";

const char* const FACE_NAMES[ImTypesetFace_COUNT] = {
    "Default", "Regular", "Bold", "Italic", "BoldItalic", "Code", "CodeBold", "Math",
};

// The style every section starts from: the context's, at the size chosen at
// the top of the window. FontSize is in pixels as ImGui::GetFontSize() answers
// them, after ImGui's own DPI and global scale.
ImTypesetStyle g_style;
// The same size as ImGui::PushFont() wants it: before those scales.
float g_font_size = 19.0f;
// What the library did during the frame before this one.
ImTypesetStats g_frame;

// The demo's own paragraphs. A host keeps an ImTypesetParagraph* wherever it
// keeps the text it belongs to, and destroys it with DestroyParagraph() or
// lets it die with its context.
//
// A demo window does not own the context it is shown in, and a handle that
// outlives its context dangles, so these are dropped (not destroyed: they are
// already gone) when the current context is another one than last frame's, or
// one that has done less work than the last frame saw, which is a new context
// at the old address.
enum DemoSlot {
    DemoSlot_Faces,
    DemoSlot_MathDisplay,
    DemoSlot_Selection,
    DemoSlot_Inspector,
    DemoSlot_COUNT
};
ImTypesetContext* g_demo_context;
ImU64 g_context_text_builds;
// Goes up when the context is another one or the demo adds a font: whatever
// the demo has measured for itself is measured again.
int g_epoch;
ImTypesetParagraph* g_paragraphs[DemoSlot_COUNT];

// The paragraph of a slot, made on first use. `fresh` is set when it was just
// made and so has no text yet.
ImTypesetParagraph* DemoParagraph(DemoSlot slot, bool* fresh)
{
    *fresh = g_paragraphs[slot] == NULL;
    if (*fresh)
        g_paragraphs[slot] = ImTypeset::CreateParagraph();
    return g_paragraphs[slot];
}

void HelpMarker(const char* text)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// What the last library call marked [E] reported, if it reported anything.
void ShowLastError()
{
    const ImTypesetErrorInfo* error = ImTypeset::GetLastError();
    if (error->Code != ImTypesetError_None)
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", error->Message);
}

// A path without its directories.
const char* FileName(const char* path)
{
    const char* name = path;
    for (const char* p = path; *p != 0; ++p)
        if (*p == '/' || *p == '\\')
            name = p + 1;
    return name;
}

// The bytes [text, text_end) as they are, wrapped by ImGui, in the dimmed ink.
void DisabledTextWrapped(const char* text, const char* text_end)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text, text_end);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

// Hairlines down both margins of the last item, `width` apart, so that a line
// ending short of the measure or past it can be seen.
void MarginGuides(float width)
{
    const ImVec2 lo = ImGui::GetItemRectMin();
    const ImVec2 hi = ImGui::GetItemRectMax();
    const ImU32 color = IM_COL32(255, 96, 96, 160);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddLine(ImVec2(lo.x - 1.5f, lo.y), ImVec2(lo.x - 1.5f, hi.y), color);
    draw_list->AddLine(ImVec2(lo.x + width + 1.5f, lo.y), ImVec2(lo.x + width + 1.5f, hi.y), color);
}

// A paragraph object as an ImGui item: lay it out (free when nothing changed),
// reserve its rectangle, draw it. Returns where its top-left corner is, which
// is what turns a mouse position into a point for the hit-testing functions.
// `button_id`, if given, makes the item one that can be clicked and dragged.
ImVec2 ParagraphItem(ImTypesetParagraph* paragraph, float width, const ImTypesetStyle* style,
                     const ImTypesetTextRange* selection = NULL, const char* button_id = NULL)
{
    ImTypeset::LayoutParagraph(paragraph, width, style);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size = ImTypeset::GetParagraphSize(paragraph);
    if (button_id != NULL)
        ImGui::InvisibleButton(
            button_id, ImVec2(size.x > 1.0f ? size.x : 1.0f, size.y > 1.0f ? size.y : 1.0f));
    else
        ImGui::Dummy(size);
    ImTypeset::DrawParagraph(paragraph, ImGui::GetWindowDrawList(), pos, selection);
    return pos;
}

//-----------------------------------------------------------------------------
// [SECTION] Paragraph
//-----------------------------------------------------------------------------
// The widget: one call per paragraph per frame. The layout is kept by the
// context between frames and redone when the text, the width, the style or
// the fonts change.

void ShowDemoParagraph()
{
    static float column_width = 250.0f;
    ImGui::SliderFloat("Column width", &column_width, 120.0f, 400.0f, "%.0f px");

    // ImGui's own wrapping, in the same font at the same size...
    ImGui::BeginChild("wrapped", ImVec2(column_width, 0.0f), ImGuiChildFlags_AutoResizeY);
    ImGui::TextDisabled("ImGui::TextWrapped");
    ImGui::PushFont(ImTypeset::GetFont(ImTypesetFace_Regular), g_font_size);
    ImGui::TextWrapped("%s", PROSE);
    ImGui::PopFont();
    ImGui::EndChild();

    // ...beside the same words typeset ragged...
    ImGui::SameLine(0.0f, 24.0f);
    ImGui::BeginChild("ragged", ImVec2(column_width, 0.0f), ImGuiChildFlags_AutoResizeY);
    ImGui::TextDisabled("ImTypeset, ragged");
    ImTypesetStyle ragged = g_style; // A copy with one thing changed, for this call only.
    ragged.Align = ImTypesetAlign_Left;
    ImTypeset::TextUnformatted(PROSE, NULL, 0, 0.0f, &ragged);
    ImGui::EndChild();
    MarginGuides(column_width);

    // ...and justified, which is the default.
    ImGui::SameLine(0.0f, 24.0f);
    ImGui::BeginChild("justified", ImVec2(column_width, 0.0f), ImGuiChildFlags_AutoResizeY);
    ImGui::TextDisabled("ImTypeset, justified");
    ImTypeset::TextUnformatted(PROSE, NULL, 0, 0.0f, &g_style);
    ImGui::EndChild();
    MarginGuides(column_width);

    // Everything an ImTypesetStyle says about a paragraph.
    ImGui::SeparatorText("ImTypesetStyle");
    static ImTypesetStyle custom; // All zero: the defaults.
    static bool fill_window = true;
    static bool hard_breaks = false;
    static float width = 420.0f;
    ImGui::PushItemWidth(ImGui::GetFontSize() * 12.0f);
    ImGui::Combo("Align", &custom.Align, "Justify\0Left\0Center\0Right\0");
    ImGui::SameLine();
    ImGui::Combo("LineBreak", &custom.LineBreak, "Auto\0FirstFit\0TotalFit\0");
    ImGui::SameLine();
    HelpMarker("Auto is total fit for justified text and first fit for the rest. Total fit "
               "chooses every line of the paragraph at once, as TeX does; first fit fills each "
               "line in turn.");
    ImGui::Combo("Spacing", &custom.Spacing, "Sentence\0French\0TeX\0");
    ImGui::SameLine();
    ImGui::SliderFloat("LineHeight", &custom.LineHeight, 0.0f, 2.5f,
                       custom.LineHeight > 0.0f ? "%.2f em" : "0 (1.35 em)");
    ImGui::SliderFloat("FirstLineIndent", &custom.FirstLineIndent, 0.0f, 60.0f, "%.0f px");
    ImGui::SameLine();
    ImGui::SliderFloat("LeftIndent", &custom.LeftIndent, 0.0f, 80.0f, "%.0f px");
    ImGui::Checkbox("Fill the window", &fill_window);
    ImGui::SameLine();
    ImGui::BeginDisabled(fill_window);
    ImGui::SliderFloat("Width", &width, 100.0f, 800.0f, "%.0f px");
    ImGui::EndDisabled();
    ImGui::Checkbox("ImTypesetTextFlags_HardBreaks", &hard_breaks);
    ImGui::PopItemWidth();

    static const char TEXT[] =
        "Lines are chosen for the whole paragraph at once. A greedy breaker fills each line "
        "as full as it will go and leaves the last decision to chance; this one weighs every "
        "way of breaking the paragraph and keeps the one whose worst line is least bad.\n"
        "Drag the window's edge, or the width above, and watch the paragraph being set again. "
        "Mr. Knuth's rule for the space after a sentence is one of the three under Spacing.";
    ImTypesetStyle style = custom;
    style.FontSize = g_style.FontSize;
    // A wrap width of zero is the width available, as for ImGui::TextWrapped().
    const float wrap_width = fill_window ? 0.0f : width;
    const ImTypesetTextFlags flags = hard_breaks ? ImTypesetTextFlags_HardBreaks : 0;
    if (!ImTypeset::TextUnformatted(TEXT, NULL, flags, wrap_width, &style))
        ShowLastError();
    else if (!fill_window)
        MarginGuides(width);
}

//-----------------------------------------------------------------------------
// [SECTION] Faces and sizes
//-----------------------------------------------------------------------------
// A paragraph object: text and style runs in, laid out for a width, drawn into
// a draw list. Runs are how a host that has parsed its own markup says "this
// stretch is bold": byte ranges of the text with a face, a size and an ink.

void ShowDemoFaces()
{
    struct Piece {
        const char* Text;
        ImTypesetFace Face;
        float Scale;
        bool Colored;
        ImTypesetRunFlags Flags;
    };
    static const Piece PIECES[] = {
        {"Hxg regular, ", ImTypesetFace_Default, 0.0f, false, 0},
        {"Hxg bold, ", ImTypesetFace_Bold, 0.0f, false, 0},
        {"Hxg italic, ", ImTypesetFace_Italic, 0.0f, false, 0},
        {"Hxg bold italic, ", ImTypesetFace_BoldItalic, 0.0f, false, 0},
        {"Hxg code()", ImTypesetFace_Code, 0.0f, false, ImTypesetRunFlags_NoBreak},
        {", ", ImTypesetFace_Default, 0.0f, false, 0},
        {"Hxg large", ImTypesetFace_Default, 1.5f, false, 0},
        {", ", ImTypesetFace_Default, 0.0f, false, 0},
        {"Hxg small", ImTypesetFace_Default, 0.75f, false, 0},
        {", and ", ImTypesetFace_Default, 0.0f, false, 0},
        {"Hxg in colour", ImTypesetFace_Bold, 0.0f, true, 0},
        {". Then the paragraph goes on in the regular face for long enough to wrap, with an ",
         ImTypesetFace_Default, 0.0f, false, 0},
        {"italic phrase that runs across a line break", ImTypesetFace_Italic, 0.0f, false, 0},
        {" and a ", ImTypesetFace_Default, 0.0f, false, 0},
        {"code_span(that, does, not, break)", ImTypesetFace_Code, 0.0f, false,
         ImTypesetRunFlags_NoBreak},
        {" before it ends, AV To WA.", ImTypesetFace_Default, 0.0f, false, 0},
    };

    static float width = 460.0f;
    static ImVec4 color = ImVec4(0.90f, 0.35f, 0.30f, 1.0f);
    static float large = 1.5f;
    bool changed = false;
    ImGui::SliderFloat("Width##faces", &width, 160.0f, 800.0f, "%.0f px");
    changed |= ImGui::ColorEdit4("ImTypesetRun::Color", &color.x, ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    changed |= ImGui::SliderFloat("ImTypesetRun::Scale", &large, 0.5f, 3.0f, "%.2f");

    // The text is given once, and again only when it or its runs change;
    // LayoutParagraph() and DrawParagraph() are what a frame costs.
    bool fresh = false;
    ImTypesetParagraph* paragraph = DemoParagraph(DemoSlot_Faces, &fresh);
    if (fresh || changed) {
        ImGuiTextBuffer text;
        ImVector<ImTypesetRun> runs;
        for (int i = 0; i < IM_ARRAYSIZE(PIECES); ++i) {
            const Piece& piece = PIECES[i];
            ImTypesetRun run; // Zero everywhere changes nothing.
            run.Start = text.size();
            text.append(piece.Text);
            run.End = text.size();
            run.Face = piece.Face;
            run.Scale = piece.Scale == 1.5f ? large : piece.Scale;
            run.Color = piece.Colored ? ImGui::ColorConvertFloat4ToU32(color) : 0;
            run.Flags = piece.Flags;
            runs.push_back(run);
        }
        ImTypeset::SetParagraphText(paragraph, text.begin(), text.end(), runs.Data, runs.Size);
    }
    ParagraphItem(paragraph, width, &g_style);
    MarginGuides(width);
    // A run that names a face nothing was added for is set in the paragraph's
    // own face, and the paragraph says so.
    const ImTypesetErrorInfo* error = ImTypeset::GetParagraphError(paragraph);
    if (error->Code != ImTypesetError_None)
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", error->Message);

    // Sizes. A face is added once and set at any size: ImGui's atlas bakes
    // each size when it is first drawn.
    ImGui::SeparatorText("One face, several sizes");
    static const float SIZES[] = {11.0f, 14.0f, 19.0f, 26.0f, 38.0f};
    for (int i = 0; i < IM_ARRAYSIZE(SIZES); ++i) {
        ImTypesetStyle style = g_style;
        style.FontSize = SIZES[i];
        style.Align = ImTypesetAlign_Left;
        ImGui::PushID(i); // The same words five times over: five layouts, told apart by ID.
        ImTypeset::TextUnformatted("Hamburgefonstiv, AV To WA, 0123456789", NULL, 0, 0.0f, &style);
        ImGui::PopID();
    }

    // The other size. FontSize is ImGui's: the height of a line. EmSize is a
    // typographer's: the em. The same number gives two sizes of type.
    ImGui::SeparatorText("FontSize and EmSize");
    ImTypesetStyle by_line = g_style;
    by_line.Align = ImTypesetAlign_Left;
    by_line.FontSize = 20.0f;
    ImTypeset::TextUnformatted("FontSize = 20: a line of this face is 20 px tall.", NULL, 0, 0.0f,
                               &by_line);
    ImTypesetStyle by_em = by_line;
    by_em.EmSize = 20.0f; // Wins over FontSize.
    ImTypeset::TextUnformatted("EmSize = 20: the em of this face is 20 px.", NULL, 0, 0.0f, &by_em);
}

//-----------------------------------------------------------------------------
// [SECTION] Math
//-----------------------------------------------------------------------------
// ImTypesetTextFlags_Math makes the dollars of a text delimit formulas. A
// formula is set in the face added for ImTypesetFace_Math, as a box among the
// words. One that is outside the supported subset is never guessed at: its
// source is shown instead and the call says why and where.

const char* const FORMULAS[] = {
    "e^{i\\pi} + 1 = 0",
    "x_i^2",
    "x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}",
    "\\frac{\\frac{a}{b}}{\\frac{c}{d}}",
    "\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2}",
    "\\int_0^1 x^2 dx = \\frac{1}{3}",
    "a_1 + a_2 + a_3 + \\cdots + a_n = \\frac{n(a_1 + a_n)}{2}",
    "(\\frac{a}{b}) \\quad \\left(\\frac{a}{b}\\right)",
    "\\sqrt{x} \\quad \\sqrt{\\frac{a}{b}}",
    "\\int f(x) dx \\quad \\int f(x) \\, dx",
    "x = 1 \\text{ if } y > 0",
    "\\left( \\sum_{i=1}^{n} \\frac{a_i}{b_i} \\right)^2 \\le \\sqrt{n} \\quad \\text{for every } "
    "n",
    "\\frac{a+b+c+d}{\\sqrt{x}} \\quad \\frac{\\sqrt{x}}{a+b+c+d} \\quad "
    "\\frac{a+b+c+d}{\\frac{x}{y}}",
    "\\sqrt{x}^2 \\quad \\sqrt{x^2 + y^2} \\quad \\frac{1}{11111111}",
    "E = mc^2",
    "\\alpha + \\beta = \\gamma",
    "\\frac{1}{1 - x}",
    "\\lim_{n \\to \\infty} \\frac{1}{n} = 0",
    "\\sum_{n=1}^{\\infty} \\frac{1}{n^2} = \\frac{\\pi^2}{6}",
    "a^2 + b^2 = c^2",
    "\\binom{n}{k} = \\frac{n!}{k!(n-k)!}",
    "\\overline{x + y} \\quad \\underline{x + y}",
    "f'(x) \\quad \\mathbb{R} \\quad \\mathcal{L} \\quad \\mathbf{v} \\quad \\mathrm{d}x",
    // Outside the subset: refused, with a reason.
    "\\begin{matrix} a & b \\end{matrix}",
    "\\hat{x}",
};

void ShowDemoMath()
{
    if (ImTypeset::GetFont(ImTypesetFace_Math) == NULL)
        ImGui::TextWrapped("No font has been added for ImTypesetFace_Math, so every formula is "
                           "refused with that reason. Add one that carries an OpenType MATH table "
                           "(see Fonts below).");

    static char source[512] = "x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}";
    static float width = 460.0f;
    static bool replaced = false; // A row of the table below was clicked last frame.
    bool edited = ImGui::InputTextMultiline("Source", source, sizeof(source),
                                            ImVec2(0.0f, ImGui::GetTextLineHeight() * 3.5f));
    edited |= replaced;
    replaced = false;
    ImGui::SliderFloat("Width##math", &width, 100.0f, 800.0f, "%.0f px");

    // Inline, through the widget. The text changes at every keystroke, so it
    // is found again by an ID and not by its content: TextWithID().
    ImGui::SeparatorText("$...$ in a sentence");
    static ImGuiTextBuffer text;
    static const char BEFORE[] = "Set in text style, $";
    text.resize(0);
    text.append(BEFORE);
    text.append(source);
    text.append("$ is a box on the baseline among the words of its sentence, and the line "
                "breaker moves it whole.");
    if (!ImTypeset::TextWithID("inline", text.begin(), text.end(), ImTypesetTextFlags_Math, width,
                               &g_style)) {
        // False, and the text is still drawn: the formula as its source. The
        // place is a byte offset into the text that was handed in.
        const ImTypesetErrorInfo* error = ImTypeset::GetLastError();
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", error->Message);
        const int at = error->BytePos - (int)(sizeof(BEFORE) - 1);
        if (error->Code == ImTypesetError_MathRefused && at >= 0 && at <= (int)strlen(source))
            ImGui::Text("at byte %d of the source: %s", at, source + at);
    }

    // Displayed, through a paragraph object, which can be asked about each of
    // its formulas. A text that is nothing but a $$ block is centred on the
    // width whatever the style's alignment.
    ImGui::SeparatorText("$$...$$ on its own");
    bool fresh = false;
    ImTypesetParagraph* display = DemoParagraph(DemoSlot_MathDisplay, &fresh);
    if (fresh || edited) {
        text.resize(0);
        text.append("$$");
        text.append(source);
        text.append("$$");
        ImTypeset::SetParagraphText(display, text.begin(), text.end(), NULL, 0,
                                    ImTypesetTextFlags_Math);
    }
    ParagraphItem(display, width, &g_style);
    MarginGuides(width);
    for (int i = 0; i < ImTypeset::GetParagraphFormulaCount(display); ++i) {
        const ImTypesetFormula formula = ImTypeset::GetParagraphFormula(display, i);
        if (formula.Refused)
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                               "refused at byte %d (codepoint %d) of \"$$source$$\": %s",
                               formula.BytePos, formula.CodepointPos, formula.Reason);
        else
            ImGui::TextDisabled("%s formula, bytes [%d, %d): set",
                                formula.Display ? "display" : "inline", formula.TextStart,
                                formula.TextEnd);
    }
    if (ImTypeset::GetParagraphFormulaCount(display) == 0)
        ImGui::TextDisabled("no formula: the dollars do not open one (see the rules at "
                            "ImTypesetTextFlags_Math)");

    // The subset, one construct at a time: click a source to edit it above.
    ImGui::SeparatorText("The subset");
    if (ImGui::BeginTable("formulas", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("source", ImGuiTableColumnFlags_None, 3.0f);
        ImGui::TableSetupColumn("$...$", ImGuiTableColumnFlags_None, 2.0f);
        ImGui::TableSetupColumn("$$...$$", ImGuiTableColumnFlags_None, 2.0f);
        ImGui::TableHeadersRow();
        ImTypesetStyle cell = g_style;
        cell.Align = ImTypesetAlign_Left;
        for (int i = 0; i < IM_ARRAYSIZE(FORMULAS); ++i) {
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(FORMULAS[i]) && strlen(FORMULAS[i]) < sizeof(source)) {
                memcpy(source, FORMULAS[i], strlen(FORMULAS[i]) + 1);
                replaced = true;
            }
            ImGui::TableNextColumn();
            text.resize(0);
            text.appendf("$%s$", FORMULAS[i]);
            ImTypeset::TextUnformatted(text.begin(), text.end(), ImTypesetTextFlags_Math, 0.0f,
                                       &cell);
            ImGui::TableNextColumn();
            // A text that is one $$ block: display style, centred in the cell.
            text.resize(0);
            text.appendf("$$%s$$", FORMULAS[i]);
            ImTypeset::TextUnformatted(text.begin(), text.end(), ImTypesetTextFlags_Math, 0.0f,
                                       &cell);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Which dollars are not math.
    ImGui::SeparatorText("Prices are not math");
    ImTypeset::TextUnformatted(
        "It cost $5 and $10, and neither is a formula: an opening dollar must be followed at "
        "once by the formula and a closing one must follow it at once. An escaped dollar, \\$ "
        "like this, is a dollar too. Without ImTypesetTextFlags_Math, a dollar is a dollar.",
        NULL, ImTypesetTextFlags_Math, width, &g_style);
}

//-----------------------------------------------------------------------------
// [SECTION] Selection
//-----------------------------------------------------------------------------

const char SELECTABLE[] =
    "Drag across these words to select them, double-click for a word and triple-click for a "
    "line; Ctrl+C copies and Ctrl+A takes everything. A formula such as "
    "$\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2}$ is taken or left as a whole, and copies as its "
    "source between its dollars. A selection is a range of bytes of the text the host handed "
    "in, so it survives being laid out again at another width.";

void ShowDemoSelection()
{
    static float width = 460.0f;
    ImGui::SliderFloat("Width##selection", &width, 160.0f, 800.0f, "%.0f px");

    // The widget does the mouse, the keyboard and the clipboard. The range is
    // the host's to read and to set, as a bool is a checkbox's.
    ImGui::SeparatorText("ImTypeset::SelectableText");
    static ImTypesetTextRange selection = {0, 0};
    ImTypeset::SelectableText("selectable", SELECTABLE, NULL, ImTypesetTextFlags_Math, width,
                              &g_style, &selection);
    if (selection.Start < selection.End) {
        ImGui::Text("Selection: bytes [%d, %d)", selection.Start, selection.End);
        DisabledTextWrapped(SELECTABLE + selection.Start, SELECTABLE + selection.End);
    } else {
        ImGui::Text("Selection: none");
    }
    if (ImGui::Button("Select the first sentence")) {
        const char* stop = strchr(SELECTABLE, ';');
        selection.Start = 0;
        selection.End = stop != NULL ? (int)(stop - SELECTABLE) : 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
        selection.Start = selection.End = 0;
    const char* clipboard = ImGui::GetClipboardText();
    ImGui::TextDisabled("Clipboard: %.80s", clipboard != NULL ? clipboard : "");

    // The same from its parts, for a host that has its own idea of what a
    // click does: a paragraph object, a drag turned into a range at a chosen
    // granularity, and the range turned into text.
    ImGui::SeparatorText("By hand: SelectParagraphRange, AppendParagraphRangeText");
    static ImTypesetGranularity granularity = ImTypesetGranularity_Word;
    static ImTypesetTextRange range = {0, 0};
    static ImVec2 anchor;
    static ImGuiTextBuffer copied;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    ImGui::Combo("Granularity", &granularity, "Character\0Word\0Line\0");

    bool fresh = false;
    ImTypesetParagraph* paragraph = DemoParagraph(DemoSlot_Selection, &fresh);
    if (fresh) {
        ImTypeset::SetParagraphText(paragraph, SELECTABLE, NULL, NULL, 0, ImTypesetTextFlags_Math);
        range.Start = range.End = 0;
        copied.resize(0);
    }
    const ImVec2 pos = ParagraphItem(paragraph, width, &g_style, &range, "by-hand");
    if (ImGui::IsItemActive()) {
        // Points are relative to the paragraph's top-left corner.
        const ImVec2 mouse = ImGui::GetMousePos();
        const ImVec2 active = ImVec2(mouse.x - pos.x, mouse.y - pos.y);
        if (ImGui::IsItemActivated())
            anchor = active;
        const ImTypesetTextRange dragged =
            ImTypeset::SelectParagraphRange(paragraph, anchor, active, granularity);
        if (dragged.Start != range.Start || dragged.End != range.End) {
            range = dragged;
            // A copy allocates: when the range changes, not every frame.
            copied.resize(0);
            ImTypeset::AppendParagraphRangeText(paragraph, range, &copied);
        }
    }
    ImGui::Text("Range: bytes [%d, %d)", range.Start, range.End);
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy") && !copied.empty())
        ImGui::SetClipboardText(copied.c_str());
    ImGui::TextDisabled("Copies as:");
    DisabledTextWrapped(copied.begin(), copied.end());
}

//-----------------------------------------------------------------------------
// [SECTION] Inspector
//-----------------------------------------------------------------------------
// What a laid-out paragraph can be asked: its lines, where a point falls in
// it, where a caret stands. The overlay here is drawn with those answers.

void ShowDemoInspector()
{
    static const char TEXT[] =
        "Every line has a box, a baseline and the bytes of the text that are on it. A line "
        "that holds a tall formula, such as $\\frac{\\frac{a}{b}}{\\frac{c}{d}}$, is taller "
        "than its neighbours. Move the mouse over the paragraph: the caret goes to the "
        "position nearest the pointer.";
    static float width = 460.0f;
    static bool show_boxes = true;
    static bool show_baselines = true;
    static bool show_line_text = false;
    ImGui::SliderFloat("Width##inspector", &width, 160.0f, 800.0f, "%.0f px");
    ImGui::Checkbox("Line boxes", &show_boxes);
    ImGui::SameLine();
    ImGui::Checkbox("Baselines", &show_baselines);
    ImGui::SameLine();
    ImGui::Checkbox("Highlight the hovered line's text", &show_line_text);

    bool fresh = false;
    ImTypesetParagraph* paragraph = DemoParagraph(DemoSlot_Inspector, &fresh);
    if (fresh)
        ImTypeset::SetParagraphText(paragraph, TEXT, NULL, NULL, 0, ImTypesetTextFlags_Math);
    const ImVec2 pos = ParagraphItem(paragraph, width, &g_style, NULL, "inspected");
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    const int line_count = ImTypeset::GetParagraphLineCount(paragraph);
    for (int n = 0; n < line_count; ++n) {
        // Relative to the paragraph's corner, and layout's own: drawing snaps
        // each baseline to a whole pixel, so ink sits within a pixel of these.
        const ImTypesetLine line = ImTypeset::GetParagraphLine(paragraph, n);
        if (show_boxes)
            draw_list->AddRect(ImVec2(pos.x + line.Min.x, pos.y + line.Min.y),
                               ImVec2(pos.x + line.Max.x, pos.y + line.Max.y),
                               IM_COL32(80, 160, 255, 140));
        if (show_baselines)
            draw_list->AddLine(ImVec2(pos.x + line.Min.x, pos.y + line.Baseline),
                               ImVec2(pos.x + line.Max.x, pos.y + line.Baseline),
                               IM_COL32(255, 96, 96, 160));
    }

    if (hovered) {
        const ImVec2 mouse = ImGui::GetMousePos();
        const ImTypesetHit hit =
            ImTypeset::HitTestParagraph(paragraph, ImVec2(mouse.x - pos.x, mouse.y - pos.y));
        const ImVec4 caret = ImTypeset::GetParagraphCaretRect(paragraph, hit.TextOffset);
        draw_list->AddLine(ImVec2(pos.x + caret.x, pos.y + caret.y),
                           ImVec2(pos.x + caret.z, pos.y + caret.w), IM_COL32(255, 220, 64, 255),
                           2.0f);
        if (show_line_text && hit.Line >= 0) {
            const ImTypesetLine line = ImTypeset::GetParagraphLine(paragraph, hit.Line);
            const ImTypesetTextRange on_line = {line.TextStart, line.TextEnd};
            static ImVector<ImVec4>
                rects; // Kept: the call appends, and a frame should not allocate.
            rects.resize(0);
            ImTypeset::GetParagraphRangeRects(paragraph, on_line, &rects);
            for (int i = 0; i < rects.Size; ++i)
                draw_list->AddRectFilled(ImVec2(pos.x + rects[i].x, pos.y + rects[i].y),
                                         ImVec2(pos.x + rects[i].z, pos.y + rects[i].w),
                                         IM_COL32(255, 220, 64, 48));
        }
        ImGui::Text("Hit: TextOffset %d, Line %d, Inside %s", hit.TextOffset, hit.Line,
                    hit.Inside ? "true" : "false");
    } else {
        ImGui::TextDisabled("Hit: move the mouse over the paragraph");
    }

    const ImVec2 size = ImTypeset::GetParagraphSize(paragraph);
    const ImTypesetStatusFlags status = ImTypeset::GetParagraphStatus(paragraph);
    ImGui::Text("Size %.0f x %.0f, %d lines%s%s", (double)size.x, (double)size.y, line_count,
                (status & ImTypesetStatusFlags_FirstFitFallback) ? ", first-fit fallback" : "",
                (status & ImTypesetStatusFlags_DisplayMath) ? ", display math" : "");
    if (ImGui::BeginTable("lines", 5,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("line");
        ImGui::TableSetupColumn("Min");
        ImGui::TableSetupColumn("Max");
        ImGui::TableSetupColumn("Baseline");
        ImGui::TableSetupColumn("text [TextStart, TextEnd)");
        ImGui::TableHeadersRow();
        for (int n = 0; n < line_count; ++n) {
            const ImTypesetLine line = ImTypeset::GetParagraphLine(paragraph, n);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d", n);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f, %.2f", (double)line.Min.x, (double)line.Min.y);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f, %.2f", (double)line.Max.x, (double)line.Max.y);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", (double)line.Baseline);
            ImGui::TableNextColumn();
            ImGui::Text("[%d, %d)", line.TextStart, line.TextEnd);
        }
        ImGui::EndTable();
    }
}

//-----------------------------------------------------------------------------
// [SECTION] Stress
//-----------------------------------------------------------------------------
// A long document in a scrolling child, and what it costs. The counters are
// ImTypeset::GetStats(), a debug facility: a frame in which nothing changed
// lays nothing out, and draws only what is in view.

const char* const DOCUMENT[] = {
    "The first thing a reader meets in a long document is its length, and the first thing a "
    "program meets is the cost of that length. This document is the same six paragraphs over "
    "and over, each copy under its own ID so that each is a text of its own to the library.",
    "A paragraph is laid out when it is first seen and when the width it is set to changes. "
    "Scrolling changes neither, so a frame that only scrolls lays out the paragraphs that "
    "came into view and nothing else, and a frame in which nothing moved lays out nothing.",
    "What is out of view is not drawn. Here it is not submitted either: every copy is as tall "
    "as every other, so the copies in view are found by arithmetic, as ImGuiListClipper does "
    "for rows, and the rest is one empty rectangle of the right height.",
    "Formulas are part of the text, $\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2}$ for one, and are "
    "laid out with the paragraph they stand in and kept with it.",
    "Layouts that are no longer submitted are dropped once the cache is over its limits "
    "(ImTypeset::SetTextCacheLimits), so the memory held follows what has been on screen "
    "lately and not the length of the document.",
    "The end of one copy. The next begins below.",
};

void ShowDemoStress()
{
    static int copies = 100;
    static bool submit_everything = false;
    ImGui::SliderInt("Copies of the document", &copies, 1, 1000, "x%d",
                     ImGuiSliderFlags_AlwaysClamp);
    ImGui::Checkbox("Submit every paragraph and let the library cull", &submit_everything);
    ImGui::SameLine();
    HelpMarker("The simple way to show a document: call TextUnformatted() for every paragraph "
               "and rely on a clipped one drawing nothing. Each call still reads its text to "
               "find its layout (BytesHashed), and every paragraph is laid out once, so the "
               "demo holds this mode to 20 copies. Unticked, only the copies in view are "
               "submitted.");
    const int shown_copies = submit_everything && copies > 20 ? 20 : copies;

    const ImTypesetStats stats = ImTypeset::GetStats();
    ImGui::Text("Last frame: %llu layouts, %llu text builds, %llu paragraphs drawn, %llu culled, "
                "%llu vertices, %llu bytes hashed",
                (unsigned long long)g_frame.Layouts, (unsigned long long)g_frame.TextBuilds,
                (unsigned long long)g_frame.DrawCalls, (unsigned long long)g_frame.CulledCalls,
                (unsigned long long)g_frame.VerticesEmitted,
                (unsigned long long)g_frame.BytesHashed);
    ImGui::Text("Held now: %llu layouts, about %llu KiB; %llu evicted since the context was made",
                (unsigned long long)stats.CachedParagraphs,
                (unsigned long long)(stats.CachedBytes / 1024),
                (unsigned long long)stats.CacheEvictions);

    ImTypesetStyle style = g_style;
    const ImTypesetTextFlags flags = ImTypesetTextFlags_Math;
    const float gap = ImGui::GetStyle().ItemSpacing.y;
    // The scroll bar is always there so that the width does not depend on
    // whether the document overflows.
    ImGui::BeginChild("document", ImVec2(0.0f, ImGui::GetFontSize() * 24.0f),
                      ImGuiChildFlags_Borders, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    const float width = ImGui::GetContentRegionAvail().x;

    // How tall one copy is at this width and size: measured when either
    // changes, not every frame. CalcTextSize() lays the text out to answer.
    static float measured_width = -1.0f;
    static float measured_font_size = -1.0f;
    static int measured_epoch = -1;
    static float copy_height = 0.0f;
    if (measured_width != width || measured_font_size != style.FontSize ||
        measured_epoch != g_epoch) {
        copy_height = 0.0f;
        for (int k = 0; k < IM_ARRAYSIZE(DOCUMENT); ++k)
            copy_height += ImTypeset::CalcTextSize(DOCUMENT[k], NULL, width, flags, &style).y + gap;
        measured_width = width;
        measured_font_size = style.FontSize;
        measured_epoch = g_epoch;
    }

    int first = 0;
    int last = shown_copies - 1;
    if (!submit_everything && copy_height > 0.0f) {
        first = (int)(ImGui::GetScrollY() / copy_height);
        last = (int)((ImGui::GetScrollY() + ImGui::GetWindowHeight()) / copy_height);
        if (last > shown_copies - 1)
            last = shown_copies - 1;
    }
    const float top = ImGui::GetCursorPosY();
    for (int copy = first; copy <= last; ++copy) {
        ImGui::SetCursorPosY(top + (float)copy * copy_height);
        ImGui::PushID(copy);
        for (int k = 0; k < IM_ARRAYSIZE(DOCUMENT); ++k)
            ImTypeset::TextUnformatted(DOCUMENT[k], NULL, flags, width, &style);
        ImGui::PopID();
    }
    // The extent of the whole document, for the scroll bar.
    ImGui::SetCursorPosY(top + (float)shown_copies * copy_height);
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::EndChild();
    ImGui::TextDisabled("%d paragraphs in the document, copies %d to %d submitted",
                        shown_copies * IM_ARRAYSIZE(DOCUMENT), first + 1, last + 1);
}

//-----------------------------------------------------------------------------
// [SECTION] Fonts
//-----------------------------------------------------------------------------
// A host says which font file plays each role; text names roles. A font that
// cannot be used is refused with a reason and nothing changes.

void ShowDemoFonts()
{
    if (ImGui::BeginTable("faces", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("ImTypesetFace_");
        ImGui::TableSetupColumn("ImFont");
        ImGui::TableSetupColumn("sample", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (int face = ImTypesetFace_Regular; face < ImTypesetFace_COUNT; ++face) {
            ImFont* font = ImTypeset::GetFont(face);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(FACE_NAMES[face]);
            ImGui::TableNextColumn();
            if (font != NULL)
                ImGui::TextUnformatted(font->GetDebugName());
            else
                ImGui::TextDisabled("none");
            ImGui::TableNextColumn();
            if (font != NULL) {
                ImTypesetStyle style = g_style;
                style.Face = face;
                style.Align = ImTypesetAlign_Left;
                ImGui::PushID(face);
                if (face == ImTypesetFace_Math)
                    ImTypeset::TextUnformatted("$a^2 + b^2 = c^2$", NULL, ImTypesetTextFlags_Math,
                                               0.0f, &g_style);
                else
                    ImTypeset::TextUnformatted("Sphinx of black quartz, judge my vow.", NULL, 0,
                                               0.0f, &style);
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("CodeBold answers Bold's font until it has its own.");

    ImGui::SeparatorText("ImTypeset::AddFontFromFileTTF");
    static char path[512] = "";
    static int face = ImTypesetFace_Regular;
    static int face_index = 0;
    static ImGuiTextBuffer outcome;
    static bool refused = false;
    ImGui::InputTextWithHint("File", "a .ttf, .otf or .ttc, UTF-8 path", path, sizeof(path));
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    if (ImGui::BeginCombo("Role", FACE_NAMES[face])) {
        for (int n = ImTypesetFace_Regular; n < ImTypesetFace_COUNT; ++n)
            if (ImGui::Selectable(FACE_NAMES[n], n == face))
                face = n;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
    ImGui::InputInt("Face index", &face_index);
    ImGui::SameLine();
    HelpMarker("Which face of a collection (.ttc). 0 for a single font.");
    if (ImGui::Button("Add font")) {
        // Null with nothing changed when the file cannot be used, and
        // GetLastError() says what is wrong with it. A role that had a font is
        // given the new one, and every layout is redone.
        ImFont* font = ImTypeset::AddFontFromFileTTF(face, path, face_index);
        refused = font == NULL;
        outcome.resize(0);
        if (refused)
            outcome.append(ImTypeset::GetLastError()->Message);
        else
            outcome.appendf("%s is now %s", FACE_NAMES[face], FileName(path));
        if (!refused)
            ++g_epoch;
    }
    if (!outcome.empty()) {
        if (refused)
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Refused: %s", outcome.c_str());
        else
            ImGui::TextUnformatted(outcome.c_str());
    }
}

//-----------------------------------------------------------------------------
// [SECTION] Known limits
//-----------------------------------------------------------------------------
// Said with the library itself, so that each can be seen as well as read.

void ShowDemoKnownLimits()
{
    ImTypesetStyle style = g_style;
    style.Align = ImTypesetAlign_Left;
    static const char* const LIMITS[] = {
        "Word gaps. Every glyph is drawn on a whole pixel, so that type is as sharp as ImGui's "
        "own. The price is paid between the words: the gaps of a line can differ from one "
        "another by a pixel or two, because the letters of each word advance in whole pixels "
        "and the remainder falls into the gap after it.",
        "No fallback fonts. A character its face has no glyph for is looked for in the "
        "Regular, Math and Code faces and nowhere else: \xE6\xBC\xA2\xE5\xAD\x97 and "
        "\xF0\x9F\x99\x82 are drawn as replacement characters unless one of those has them. "
        "Merging another font into one added here is refused.",
        "No markup. This is a typesetter and not a markdown renderer: **asterisks** and "
        "`backquotes` "
        "are text. A host that parses markup hands over the text and an ImTypesetRun for each "
        "styled stretch, as Faces and sizes does.",
        "No hyphenation. A word is never broken, so a narrow justified column has loose lines "
        "where a long word would not fit.",
        "No tables. An ImTypesetDocument stacks paragraphs, headings, list items, code, quotes "
        "and rules and leaves a rectangle where a host wants a widget of its own; a table is one "
        "of those rectangles for now.",
        "Math is a subset. Accents, matrices, alignment and macros are refused, and a refused "
        "formula is shown as its source: $\\hat{x}$.",
        "A formula's type is as small as its style makes it: the scripts of scripts in "
        "$\\frac{\\frac{a}{b}}{\\frac{c}{d}}$ are a few pixels tall at text sizes.",
    };
    for (int i = 0; i < IM_ARRAYSIZE(LIMITS); ++i) {
        ImGui::Bullet();
        ImTypeset::TextUnformatted(LIMITS[i], NULL, ImTypesetTextFlags_Math, 0.0f, &style);
    }
}

//-----------------------------------------------------------------------------
// [SECTION] Document
//-----------------------------------------------------------------------------
// Many blocks, described once and handed over in ONE call; after that a frame
// costs what is in view, whatever the document's length. A host keeps an
// ImTypesetDocument* beside the text it shows, as this section does.

ImTypesetDocument* g_document;
int g_document_blocks = -1;
int g_document_epoch = -1;

// The blocks the document is built from, ten at a time: a heading, six
// paragraphs with a formula in them, two items of a tight list, some code.
void BuildDemoDocument(ImTypesetDocument* document, int count)
{
    static const char HEADING[] = "A heading";
    static const char ITEM[] = "An item of a list, whose marker hangs in the gutter.";
    static const char MARKER[] = "-";
    static const char CODE[] = "for (int i = 0; i < n; ++i)\n\tsum += x[i];";
    ImGuiTextBuffer text;
    ImVector<ImTypesetBlock> blocks;
    ImTypesetBlockStyle styles[4];
    styles[0].SpaceBelow = 8.0f;
    styles[1].Face = ImTypesetFace_Bold;
    styles[1].Scale = 1.4f;
    styles[1].Align = ImTypesetAlign_Left;
    styles[1].SpaceAbove = 6.0f;
    styles[1].SpaceBelow = 6.0f;
    styles[2].Align = ImTypesetAlign_Left;
    styles[2].SpaceBelow = 8.0f;
    styles[3].Face = ImTypesetFace_Code;
    styles[3].Align = ImTypesetAlign_Left;
    styles[3].SpaceBelow = 8.0f;
    ImTypesetContainer list;
    list.Kind = ImTypesetContainerKind_List;
    list.Indent = 22.0f;
    list.Flags = ImTypesetContainerFlags_Tight;

    const struct {
        ImTypesetBlockKind Kind;
        const char* Text;
        int Style;
    } PATTERN[10] = {
        {ImTypesetBlockKind_Heading, HEADING, 1}, {ImTypesetBlockKind_Paragraph, PROSE, 0},
        {ImTypesetBlockKind_Paragraph, PROSE, 0}, {ImTypesetBlockKind_ListItem, ITEM, 2},
        {ImTypesetBlockKind_ListItem, ITEM, 2},   {ImTypesetBlockKind_Paragraph, PROSE, 0},
        {ImTypesetBlockKind_Code, CODE, 3},       {ImTypesetBlockKind_Paragraph, PROSE, 0},
        {ImTypesetBlockKind_Paragraph, PROSE, 0}, {ImTypesetBlockKind_Paragraph, PROSE, 0},
    };
    for (int i = 0; i < count; ++i) {
        ImTypesetBlock block;
        block.Kind = PATTERN[i % 10].Kind;
        block.Style = PATTERN[i % 10].Style;
        block.TextStart = text.size();
        text.append(PATTERN[i % 10].Text);
        block.TextEnd = text.size();
        if (block.Kind == ImTypesetBlockKind_Paragraph)
            block.Flags = ImTypesetBlockFlags_Math;
        if (block.Kind == ImTypesetBlockKind_ListItem) {
            block.MarkerStart = text.size();
            text.append(MARKER);
            block.MarkerEnd = text.size();
            block.LeftIndent = 22.0f;
            block.Container = 1;
            block.Item = i;
        }
        blocks.push_back(block);
    }
    ImTypesetDocumentContent content;
    content.Text = text.begin();
    content.TextSize = text.size();
    content.Blocks = blocks.Data;
    content.BlockCount = blocks.Size;
    content.Styles = styles;
    content.StyleCount = IM_ARRAYSIZE(styles);
    content.Containers = &list;
    content.ContainerCount = 1;
    ImTypeset::SetDocumentBlocks(document, &content);
}

void ShowDemoDocument()
{
    static int wanted = 2000;
    static bool stream = false;
    static bool marks_on = true;
    static ImTypesetDocumentStats last;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    ImGui::SliderInt("Blocks", &wanted, 10, 10000);
    ImGui::SameLine();
    ImGui::Checkbox("Stream", &stream);
    ImGui::SameLine();
    HelpMarker("AppendDocumentText() on the last block, every frame: what a pane does while "
               "text arrives. One block is laid out again; no other is visited.");
    ImGui::SameLine();
    ImGui::Checkbox("Marks", &marks_on);
    ImGui::SameLine();
    HelpMarker("Every \"formula\" of the first paragraphs, given to the draw as (block, byte) "
               "ranges: a filled mark behind one, an underline and another ink for the next. "
               "Marks are no part of the layout and cost nothing when they change.");

    if (g_document == NULL || g_document_epoch != g_epoch) {
        if (g_document == NULL)
            g_document = ImTypeset::CreateDocument();
        g_document_blocks = -1;
        g_document_epoch = g_epoch;
    }
    if (g_document == NULL) {
        ShowLastError();
        return;
    }
    if (g_document_blocks != wanted) {
        BuildDemoDocument(g_document, wanted);
        g_document_blocks = wanted;
        last = ImTypeset::GetDocumentStats(g_document);
    }
    // The last block is whatever the pattern ends on; text arrives in one that holds text.
    if (stream && !ImTypeset::AppendDocumentText(g_document, " more"))
        stream = false;

    // Where "formula" is in PROSE, found once: a host that searches has the text.
    static int hits[4];
    static int hit_count = -1;
    if (hit_count < 0) {
        hit_count = 0;
        for (const char* at = strstr(PROSE, "formula");
             at != NULL && hit_count < IM_ARRAYSIZE(hits); at = strstr(at + 1, "formula"))
            hits[hit_count++] = (int)(at - PROSE);
    }
    ImVector<ImTypesetMark> marks;
    if (marks_on) {
        for (int block = 0; block < g_document_blocks && block < 60; ++block) {
            if (block % 10 != 1 && block % 10 != 2)
                continue;
            for (int i = 0; i < hit_count; ++i) {
                ImTypesetMark mark;
                mark.Range.Start.Block = mark.Range.End.Block = block;
                mark.Range.Start.Offset = hits[i];
                mark.Range.End.Offset = hits[i] + 7;
                mark.Place = i % 2 == 0 ? ImTypesetMarkPlace_Behind : ImTypesetMarkPlace_Under;
                mark.Color = i % 2 == 0 ? IM_COL32(255, 210, 0, 110) : IM_COL32(80, 140, 255, 255);
                marks.push_back(mark);
                if (i % 2 == 1) {
                    mark.Place = ImTypesetMarkPlace_Ink;
                    marks.push_back(mark);
                }
            }
        }
    }
    ImTypesetDocumentDraw draw;
    draw.Marks = marks.Data;
    draw.MarkCount = marks.Size;

    // The whole document in a child window that scrolls it. Drag to select
    // across blocks; Ctrl+C copies.
    ImTypeset::DocumentView("##document", g_document, ImVec2(0.0f, ImGui::GetFontSize() * 18.0f),
                            NULL, &draw, &g_style);
    ShowLastError();

    const ImTypesetDocumentStats now = ImTypeset::GetDocumentStats(g_document);
    ImGui::Text("%d blocks, %d of them never laid out at this width.", now.Blocks,
                now.BlocksDeferred);
    ImGui::Text("This frame: %d laid out, %d texts built, %d blocks visited by a pass over all.",
                (int)(now.BlocksLaidOut - last.BlocksLaidOut),
                (int)(now.MaterialBuilds - last.MaterialBuilds),
                (int)(now.StructureVisits - last.StructureVisits));
    last = now;
}

} // namespace

//-----------------------------------------------------------------------------
// [SECTION] ShowDemoWindow()
//-----------------------------------------------------------------------------

void ImTypeset::ShowDemoWindow(bool* p_open)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 20.0f, viewport->WorkPos.y + 20.0f),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(880.0f, 760.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("ImTypeset Demo", p_open)) {
        ImGui::End();
        return;
    }
    ImGui::Text("imtypeset %s, built against Dear ImGui %d, running on %s", ImTypeset::GetVersion(),
                ImTypeset::GetImGuiVersionNum(), IMGUI_VERSION);

    // Without a context there is nothing to show, and every call would only
    // answer ImTypesetError_NoContext.
    ImTypesetContext* context = ImTypeset::GetCurrentContext();
    if (context == NULL) {
        ImGui::TextWrapped("There is no current ImTypeset context. Call "
                           "ImTypeset::CreateContext() after ImGui::CreateContext().");
        ImGui::End();
        return;
    }
    const ImTypesetStats stats = ImTypeset::GetStats();
    if (context != g_demo_context || stats.TextBuilds < g_context_text_builds) {
        for (int i = 0; i < DemoSlot_COUNT; ++i)
            g_paragraphs[i] = NULL;
        g_document = NULL;
        g_demo_context = context;
        g_frame = ImTypesetStats();
        ++g_epoch;
    }

    if ((ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasTextures) == 0)
        ImGui::TextWrapped("The renderer backend does not announce "
                           "ImGuiBackendFlags_RendererHasTextures, so nothing can be typeset: "
                           "text is drawn by ImGui's own wrapping.");
    if (ImTypeset::GetFont(ImTypesetFace_Regular) == NULL)
        ImGui::TextWrapped("No font has been added for ImTypesetFace_Regular, so text is drawn "
                           "by ImGui's own wrapping until one is. See Fonts below.");

    // One size for the whole window. The widgets follow ImGui::GetFontSize()
    // when a style's FontSize is zero, so pushing a size is all it takes; the
    // demo keeps ImGui's own text at the UI's size and says the size outright.
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    ImGui::SliderFloat("Font size", &g_font_size, 10.0f, 40.0f, "%.0f px");
    ImGui::SameLine();
    HelpMarker("ImTypesetStyle::FontSize for every section below: the height of a line, as "
               "ImGui means a font size. In Roboto, 19 px is about 16 px type.");
    ImGui::PushFont(NULL, g_font_size);
    g_style = ImTypeset::GetStyle();
    g_style.FontSize = ImGui::GetFontSize();
    ImGui::PopFont();

    if (ImGui::CollapsingHeader("Paragraph"))
        ShowDemoParagraph();
    if (ImGui::CollapsingHeader("Faces and sizes"))
        ShowDemoFaces();
    if (ImGui::CollapsingHeader("Math"))
        ShowDemoMath();
    if (ImGui::CollapsingHeader("Selection"))
        ShowDemoSelection();
    if (ImGui::CollapsingHeader("Inspector"))
        ShowDemoInspector();
    if (ImGui::CollapsingHeader("Stress"))
        ShowDemoStress();
    if (ImGui::CollapsingHeader("Fonts"))
        ShowDemoFonts();
    if (ImGui::CollapsingHeader("Known limits"))
        ShowDemoKnownLimits();
    if (ImGui::CollapsingHeader("Document"))
        ShowDemoDocument();

    // What this frame did, for the next one to show.
    const ImTypesetStats now = ImTypeset::GetStats();
    g_frame.Layouts = now.Layouts - stats.Layouts;
    g_frame.TextBuilds = now.TextBuilds - stats.TextBuilds;
    g_frame.DrawCalls = now.DrawCalls - stats.DrawCalls;
    g_frame.CulledCalls = now.CulledCalls - stats.CulledCalls;
    g_frame.VerticesEmitted = now.VerticesEmitted - stats.VerticesEmitted;
    g_frame.BytesHashed = now.BytesHashed - stats.BytesHashed;
    g_context_text_builds = now.TextBuilds;
    ImGui::End();
}

#endif // IMTYPESET_DISABLE_DEMO
