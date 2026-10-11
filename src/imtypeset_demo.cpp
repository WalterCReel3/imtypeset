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
// More than one context: the window can be shown in each of them. What it
// remembers from one frame to the next (where its controls stand, the
// paragraphs and the document it keeps) it remembers for the context it was
// shown in, so two contexts have two demos that know nothing of each other,
// and a context made later starts from the defaults, whatever its address. See [SECTION] One state
// per context.
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
//   [SECTION] One state per context
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

// What every section reads. Set at the top of each ShowDemoWindow() for that
// call; nothing is carried in these from one call to the next.
//
// The style every section starts from: the context's, at the size chosen at
// the top of the window. FontSize is in pixels as ImGui::GetFontSize() answers
// them, after ImGui's own DPI and global scale.
ImTypesetStyle g_style;
// The same size as ImGui::PushFont() wants it: before those scales.
float g_font_size;
// What the library did during the frame before this one.
ImTypesetStats g_frame;

// ABOUT THE STRUCTS NAMED DemoXxxState. Each section has one, just above it:
// what the section remembers between frames. ImGui's own demo keeps such
// things in statics beside their use, and a host would keep them there too, or
// in the object the text belongs to. Here they are structs because the demo
// keeps one of each per context.

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

struct DemoParagraphState {
    float ColumnWidth = 250.0f;
    ImTypesetStyle Custom; // All zero: the defaults.
    bool FillWindow = true;
    bool HardBreaks = false;
    float Width = 420.0f;
};

void ShowDemoParagraph(DemoParagraphState* state)
{
    ImGui::SliderFloat("Column width", &state->ColumnWidth, 120.0f, 400.0f, "%.0f px");

    // ImGui's own wrapping, in the same font at the same size...
    ImGui::BeginChild("wrapped", ImVec2(state->ColumnWidth, 0.0f), ImGuiChildFlags_AutoResizeY);
    ImGui::TextDisabled("ImGui::TextWrapped");
    ImGui::PushFont(ImTypeset::GetFont(ImTypesetFace_Regular), g_font_size);
    ImGui::TextWrapped("%s", PROSE);
    ImGui::PopFont();
    ImGui::EndChild();

    // ...beside the same words typeset ragged...
    ImGui::SameLine(0.0f, 24.0f);
    ImGui::BeginChild("ragged", ImVec2(state->ColumnWidth, 0.0f), ImGuiChildFlags_AutoResizeY);
    ImGui::TextDisabled("ImTypeset, ragged");
    ImTypesetStyle ragged = g_style; // A copy with one thing changed, for this call only.
    ragged.Align = ImTypesetAlign_Left;
    ImTypeset::TextUnformatted(PROSE, NULL, 0, 0.0f, &ragged);
    ImGui::EndChild();
    MarginGuides(state->ColumnWidth);

    // ...and justified, which is the default.
    ImGui::SameLine(0.0f, 24.0f);
    ImGui::BeginChild("justified", ImVec2(state->ColumnWidth, 0.0f), ImGuiChildFlags_AutoResizeY);
    ImGui::TextDisabled("ImTypeset, justified");
    ImTypeset::TextUnformatted(PROSE, NULL, 0, 0.0f, &g_style);
    ImGui::EndChild();
    MarginGuides(state->ColumnWidth);

    // Everything an ImTypesetStyle says about a paragraph.
    ImGui::SeparatorText("ImTypesetStyle");
    ImGui::PushItemWidth(ImGui::GetFontSize() * 12.0f);
    ImGui::Combo("Align", &state->Custom.Align, "Justify\0Left\0Center\0Right\0");
    ImGui::SameLine();
    ImGui::Combo("LineBreak", &state->Custom.LineBreak, "Auto\0FirstFit\0TotalFit\0");
    ImGui::SameLine();
    HelpMarker("Auto is total fit for justified text and first fit for the rest. Total fit "
               "chooses every line of the paragraph at once, as TeX does; first fit fills each "
               "line in turn.");
    ImGui::Combo("Spacing", &state->Custom.Spacing, "Sentence\0French\0TeX\0");
    ImGui::SameLine();
    ImGui::SliderFloat("LineHeight", &state->Custom.LineHeight, 0.0f, 2.5f,
                       state->Custom.LineHeight > 0.0f ? "%.2f em" : "0 (1.35 em)");
    ImGui::SliderFloat("FirstLineIndent", &state->Custom.FirstLineIndent, 0.0f, 60.0f, "%.0f px");
    ImGui::SameLine();
    ImGui::SliderFloat("LeftIndent", &state->Custom.LeftIndent, 0.0f, 80.0f, "%.0f px");
    ImGui::Checkbox("Fill the window", &state->FillWindow);
    ImGui::SameLine();
    ImGui::BeginDisabled(state->FillWindow);
    ImGui::SliderFloat("Width", &state->Width, 100.0f, 800.0f, "%.0f px");
    ImGui::EndDisabled();
    ImGui::Checkbox("ImTypesetTextFlags_HardBreaks", &state->HardBreaks);
    ImGui::PopItemWidth();

    static const char TEXT[] =
        "Lines are chosen for the whole paragraph at once. A greedy breaker fills each line "
        "as full as it will go and leaves the last decision to chance; this one weighs every "
        "way of breaking the paragraph and keeps the one whose worst line is least bad.\n"
        "Drag the window's edge, or the width above, and watch the paragraph being set again. "
        "Mr. Knuth's rule for the space after a sentence is one of the three under Spacing.";
    ImTypesetStyle style = state->Custom;
    style.FontSize = g_style.FontSize;
    // A wrap state->Width of zero is the state->Width available, as for ImGui::TextWrapped().
    const float wrap_width = state->FillWindow ? 0.0f : state->Width;
    const ImTypesetTextFlags flags = state->HardBreaks ? ImTypesetTextFlags_HardBreaks : 0;
    if (!ImTypeset::TextUnformatted(TEXT, NULL, flags, wrap_width, &style))
        ShowLastError();
    else if (!state->FillWindow)
        MarginGuides(state->Width);
}

//-----------------------------------------------------------------------------
// [SECTION] Faces and sizes
//-----------------------------------------------------------------------------
// A paragraph object: text and style runs in, laid out for a width, drawn into
// a draw list. Runs are how a host that has parsed its own markup says "this
// stretch is bold": byte ranges of the text with a face, a size and an ink.

struct DemoFacesState {
    // A host keeps an ImTypesetParagraph* wherever it keeps the text it
    // belongs to, and destroys it with DestroyParagraph() or lets it die with
    // its context.
    ImTypesetParagraph* Paragraph = NULL;
    float Width = 460.0f;
    ImVec4 Color = ImVec4(0.90f, 0.35f, 0.30f, 1.0f);
    float Large = 1.5f;
};

void ShowDemoFaces(DemoFacesState* state)
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

    bool changed = false;
    ImGui::SliderFloat("Width##faces", &state->Width, 160.0f, 800.0f, "%.0f px");
    changed |=
        ImGui::ColorEdit4("ImTypesetRun::Color", &state->Color.x, ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    changed |= ImGui::SliderFloat("ImTypesetRun::Scale", &state->Large, 0.5f, 3.0f, "%.2f");

    // The text is given once, and again only when it or its runs change;
    // LayoutParagraph() and DrawParagraph() are what a frame costs.
    const bool fresh = state->Paragraph == NULL;
    if (fresh)
        state->Paragraph = ImTypeset::CreateParagraph();
    ImTypesetParagraph* paragraph = state->Paragraph;
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
            run.Scale = piece.Scale == 1.5f ? state->Large : piece.Scale;
            run.Color = piece.Colored ? ImGui::ColorConvertFloat4ToU32(state->Color) : 0;
            run.Flags = piece.Flags;
            runs.push_back(run);
        }
        ImTypeset::SetParagraphText(paragraph, text.begin(), text.end(), runs.Data, runs.Size);
    }
    ParagraphItem(paragraph, state->Width, &g_style);
    MarginGuides(state->Width);
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

struct DemoMathState {
    char Source[512] = "x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}";
    float Width = 460.0f;
    bool Replaced = false; // A row of the table was clicked last frame.
    ImTypesetParagraph* Display = NULL;
};

void ShowDemoMath(DemoMathState* state)
{
    if (ImTypeset::GetFont(ImTypesetFace_Math) == NULL)
        ImGui::TextWrapped("No font has been added for ImTypesetFace_Math, so every formula is "
                           "refused with that reason. Add one that carries an OpenType MATH table "
                           "(see Fonts below).");

    char* source = state->Source;
    bool edited = ImGui::InputTextMultiline("Source", source, sizeof(state->Source),
                                            ImVec2(0.0f, ImGui::GetTextLineHeight() * 3.5f));
    edited |= state->Replaced;
    state->Replaced = false;
    ImGui::SliderFloat("Width##math", &state->Width, 100.0f, 800.0f, "%.0f px");

    // Inline, through the widget. The text changes at every keystroke, so it
    // is found again by an ID and not by its content: TextWithID().
    ImGui::SeparatorText("$...$ in a sentence");
    static ImGuiTextBuffer text; // Scratch: emptied before each use.
    static const char BEFORE[] = "Set in text style, $";
    text.resize(0);
    text.append(BEFORE);
    text.append(source);
    text.append("$ is a box on the baseline among the words of its sentence, and the line "
                "breaker moves it whole.");
    if (!ImTypeset::TextWithID("inline", text.begin(), text.end(), ImTypesetTextFlags_Math,
                               state->Width, &g_style)) {
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
    // state->Width whatever the style's alignment.
    ImGui::SeparatorText("$$...$$ on its own");
    const bool fresh = state->Display == NULL;
    if (fresh)
        state->Display = ImTypeset::CreateParagraph();
    ImTypesetParagraph* display = state->Display;
    if (fresh || edited) {
        text.resize(0);
        text.append("$$");
        text.append(source);
        text.append("$$");
        ImTypeset::SetParagraphText(display, text.begin(), text.end(), NULL, 0,
                                    ImTypesetTextFlags_Math);
    }
    ParagraphItem(display, state->Width, &g_style);
    MarginGuides(state->Width);
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
            if (ImGui::Selectable(FORMULAS[i]) && strlen(FORMULAS[i]) < sizeof(state->Source)) {
                memcpy(source, FORMULAS[i], strlen(FORMULAS[i]) + 1);
                state->Replaced = true;
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
        NULL, ImTypesetTextFlags_Math, state->Width, &g_style);
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

struct DemoSelectionState {
    float Width = 460.0f;
    ImTypesetTextRange Selection = {0, 0}; // The widget's.
    // By hand.
    ImTypesetParagraph* Paragraph = NULL;
    ImTypesetGranularity Granularity = ImTypesetGranularity_Word;
    ImTypesetTextRange Range = {0, 0};
    ImVec2 Anchor;
    ImGuiTextBuffer Copied;
};

void ShowDemoSelection(DemoSelectionState* state)
{
    ImGui::SliderFloat("Width##selection", &state->Width, 160.0f, 800.0f, "%.0f px");

    // The widget does the mouse, the keyboard and the clipboard. The range is
    // the host's to read and to set, as a bool is a checkbox's.
    ImGui::SeparatorText("ImTypeset::SelectableText");
    ImTypesetTextRange& selection = state->Selection;
    ImTypeset::SelectableText("selectable", SELECTABLE, NULL, ImTypesetTextFlags_Math, state->Width,
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
    // state->Granularity, and the range turned into text.
    ImGui::SeparatorText("By hand: SelectParagraphRange, AppendParagraphRangeText");
    ImTypesetTextRange& range = state->Range;
    ImGuiTextBuffer& copied = state->Copied;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    ImGui::Combo("Granularity", &state->Granularity, "Character\0Word\0Line\0");

    const bool fresh = state->Paragraph == NULL;
    if (fresh)
        state->Paragraph = ImTypeset::CreateParagraph();
    ImTypesetParagraph* paragraph = state->Paragraph;
    if (fresh) {
        ImTypeset::SetParagraphText(paragraph, SELECTABLE, NULL, NULL, 0, ImTypesetTextFlags_Math);
        range.Start = range.End = 0;
        copied.resize(0);
    }
    const ImVec2 pos = ParagraphItem(paragraph, state->Width, &g_style, &range, "by-hand");
    if (ImGui::IsItemActive()) {
        // Points are relative to the paragraph's top-left corner.
        const ImVec2 mouse = ImGui::GetMousePos();
        const ImVec2 active = ImVec2(mouse.x - pos.x, mouse.y - pos.y);
        if (ImGui::IsItemActivated())
            state->Anchor = active;
        const ImTypesetTextRange dragged =
            ImTypeset::SelectParagraphRange(paragraph, state->Anchor, active, state->Granularity);
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

struct DemoInspectorState {
    ImTypesetParagraph* Paragraph = NULL;
    float Width = 460.0f;
    bool ShowBoxes = true;
    bool ShowBaselines = true;
    bool ShowLineText = false;
};

void ShowDemoInspector(DemoInspectorState* state)
{
    static const char TEXT[] =
        "Every line has a box, a baseline and the bytes of the text that are on it. A line "
        "that holds a tall formula, such as $\\frac{\\frac{a}{b}}{\\frac{c}{d}}$, is taller "
        "than its neighbours. Move the mouse over the paragraph: the caret goes to the "
        "position nearest the pointer.";
    ImGui::SliderFloat("Width##inspector", &state->Width, 160.0f, 800.0f, "%.0f px");
    ImGui::Checkbox("Line boxes", &state->ShowBoxes);
    ImGui::SameLine();
    ImGui::Checkbox("Baselines", &state->ShowBaselines);
    ImGui::SameLine();
    ImGui::Checkbox("Highlight the hovered line's text", &state->ShowLineText);

    if (state->Paragraph == NULL) {
        state->Paragraph = ImTypeset::CreateParagraph();
        ImTypeset::SetParagraphText(state->Paragraph, TEXT, NULL, NULL, 0, ImTypesetTextFlags_Math);
    }
    ImTypesetParagraph* paragraph = state->Paragraph;
    const ImVec2 pos = ParagraphItem(paragraph, state->Width, &g_style, NULL, "inspected");
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    const int line_count = ImTypeset::GetParagraphLineCount(paragraph);
    for (int n = 0; n < line_count; ++n) {
        // Relative to the paragraph's corner, and layout's own: drawing snaps
        // each baseline to a whole pixel, so ink sits within a pixel of these.
        const ImTypesetLine line = ImTypeset::GetParagraphLine(paragraph, n);
        if (state->ShowBoxes)
            draw_list->AddRect(ImVec2(pos.x + line.Min.x, pos.y + line.Min.y),
                               ImVec2(pos.x + line.Max.x, pos.y + line.Max.y),
                               IM_COL32(80, 160, 255, 140));
        if (state->ShowBaselines)
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
        if (state->ShowLineText && hit.Line >= 0) {
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

struct DemoStressState {
    int Copies = 100;
    bool SubmitEverything = false;
    // How tall one copy is, and what it was measured at.
    float CopyHeight = 0.0f;
    float MeasuredWidth = -1.0f;
    float MeasuredFontSize = -1.0f;
    int MeasuredFonts = -1;
};

// `fonts_added` is how many fonts the demo has added to the context: what the
// section has measured, it measures again when that changes.
void ShowDemoStress(DemoStressState* state, int fonts_added)
{
    ImGui::SliderInt("Copies of the document", &state->Copies, 1, 1000, "x%d",
                     ImGuiSliderFlags_AlwaysClamp);
    ImGui::Checkbox("Submit every paragraph and let the library cull", &state->SubmitEverything);
    ImGui::SameLine();
    HelpMarker("The simple way to show a document: call TextUnformatted() for every paragraph "
               "and rely on a clipped one drawing nothing. Each call still reads its text to "
               "find its layout (BytesHashed), and every paragraph is laid out once, so the "
               "demo holds this mode to 20 copies. Unticked, only the copies in view are "
               "submitted.");
    const int shown_copies = state->SubmitEverything && state->Copies > 20 ? 20 : state->Copies;

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
    if (state->MeasuredWidth != width || state->MeasuredFontSize != style.FontSize ||
        state->MeasuredFonts != fonts_added) {
        state->CopyHeight = 0.0f;
        for (int k = 0; k < IM_ARRAYSIZE(DOCUMENT); ++k)
            state->CopyHeight +=
                ImTypeset::CalcTextSize(DOCUMENT[k], NULL, width, flags, &style).y + gap;
        state->MeasuredWidth = width;
        state->MeasuredFontSize = style.FontSize;
        state->MeasuredFonts = fonts_added;
    }

    int first = 0;
    int last = shown_copies - 1;
    if (!state->SubmitEverything && state->CopyHeight > 0.0f) {
        first = (int)(ImGui::GetScrollY() / state->CopyHeight);
        last = (int)((ImGui::GetScrollY() + ImGui::GetWindowHeight()) / state->CopyHeight);
        if (last > shown_copies - 1)
            last = shown_copies - 1;
    }
    const float top = ImGui::GetCursorPosY();
    for (int copy = first; copy <= last; ++copy) {
        ImGui::SetCursorPosY(top + (float)copy * state->CopyHeight);
        ImGui::PushID(copy);
        for (int k = 0; k < IM_ARRAYSIZE(DOCUMENT); ++k)
            ImTypeset::TextUnformatted(DOCUMENT[k], NULL, flags, width, &style);
        ImGui::PopID();
    }
    // The extent of the whole document, for the scroll bar.
    ImGui::SetCursorPosY(top + (float)shown_copies * state->CopyHeight);
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

struct DemoFontsState {
    char Path[512] = "";
    int Face = ImTypesetFace_Regular;
    int FaceIndex = 0;
    ImGuiTextBuffer Outcome;
    bool Refused = false;
    int Added = 0; // How many fonts the demo has added to the context.
};

void ShowDemoFonts(DemoFontsState* state)
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
    char* path = state->Path;
    ImGuiTextBuffer& outcome = state->Outcome;
    ImGui::InputTextWithHint("File", "a .ttf, .otf or .ttc, UTF-8 path", path, sizeof(state->Path));
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    if (ImGui::BeginCombo("Role", FACE_NAMES[state->Face])) {
        for (int n = ImTypesetFace_Regular; n < ImTypesetFace_COUNT; ++n)
            if (ImGui::Selectable(FACE_NAMES[n], n == state->Face))
                state->Face = n;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
    ImGui::InputInt("Face index", &state->FaceIndex);
    ImGui::SameLine();
    HelpMarker("Which face of a collection (.ttc). 0 for a single font.");
    if (ImGui::Button("Add font")) {
        // Null with nothing changed when the file cannot be used, and
        // GetLastError() says what is wrong with it. A role that had a font is
        // given the new one, and every layout is redone.
        ImFont* font = ImTypeset::AddFontFromFileTTF(state->Face, path, state->FaceIndex);
        state->Refused = font == NULL;
        outcome.resize(0);
        if (state->Refused)
            outcome.append(ImTypeset::GetLastError()->Message);
        else
            outcome.appendf("%s is now %s", FACE_NAMES[state->Face], FileName(path));
        if (!state->Refused)
            ++state->Added;
    }
    if (!outcome.empty()) {
        if (state->Refused)
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
        "own, and the rounding has to go somewhere. A word is held to the width layout gave it "
        "and placed from the word before it, so a space between two words is drawn within a "
        "pixel and a half of the width it was laid out with. The price is paid inside the "
        "words: about one pair of neighbouring letters in eleven stands more than half a pixel "
        "from its laid-out distance. A justified line whose spaces are nearly closed was laid "
        "out that way: the line breaker shrinks the spaces of a line to make it fit, and how "
        "far it may on a screen has not been tuned.",
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

struct DemoDocumentState {
    ImTypesetDocument* Document = NULL;
    int Blocks = -1;     // How many it was built with.
    int BuiltFonts = -1; // And with how many fonts added by the demo.
    int Wanted = 2000;
    bool Stream = false;
    bool MarksOn = true;
    ImTypesetDocumentStats Last = ImTypesetDocumentStats();
};

// `fonts_added`: as for ShowDemoStress().
void ShowDemoDocument(DemoDocumentState* state, int fonts_added)
{
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    ImGui::SliderInt("Blocks", &state->Wanted, 10, 10000);
    ImGui::SameLine();
    ImGui::Checkbox("Stream", &state->Stream);
    ImGui::SameLine();
    HelpMarker("AppendDocumentText() on the last block, every frame: what a pane does while "
               "text arrives. One block is laid out again; no other is visited.");
    ImGui::SameLine();
    ImGui::Checkbox("Marks", &state->MarksOn);
    ImGui::SameLine();
    HelpMarker("Every \"formula\" of the first paragraphs, given to the draw as (block, byte) "
               "ranges: a filled mark behind one, an underline and another ink for the next. "
               "Marks are no part of the layout and cost nothing when they change.");

    if (state->Document == NULL || state->BuiltFonts != fonts_added) {
        if (state->Document == NULL)
            state->Document = ImTypeset::CreateDocument();
        state->Blocks = -1;
        state->BuiltFonts = fonts_added;
    }
    if (state->Document == NULL) {
        ShowLastError();
        return;
    }
    if (state->Blocks != state->Wanted) {
        BuildDemoDocument(state->Document, state->Wanted);
        state->Blocks = state->Wanted;
        state->Last = ImTypeset::GetDocumentStats(state->Document);
    }
    // The last block is whatever the pattern ends on; text arrives in one that holds text.
    if (state->Stream && !ImTypeset::AppendDocumentText(state->Document, " more"))
        state->Stream = false;

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
    if (state->MarksOn) {
        for (int block = 0; block < state->Blocks && block < 60; ++block) {
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
    ImTypeset::DocumentView("##document", state->Document,
                            ImVec2(0.0f, ImGui::GetFontSize() * 18.0f), NULL, &draw, &g_style);
    ShowLastError();

    const ImTypesetDocumentStats now = ImTypeset::GetDocumentStats(state->Document);
    ImGui::Text("%d blocks, %d of them never laid out at this width.", now.Blocks,
                now.BlocksDeferred);
    ImGui::Text("This frame: %d laid out, %d texts built, %d blocks visited by a pass over all.",
                (int)(now.BlocksLaidOut - state->Last.BlocksLaidOut),
                (int)(now.MaterialBuilds - state->Last.MaterialBuilds),
                (int)(now.StructureVisits - state->Last.StructureVisits));
    state->Last = now;
}

//-----------------------------------------------------------------------------
// [SECTION] One state per context
//-----------------------------------------------------------------------------
// A host owns its context and knows when it destroys it, so it knows when the
// paragraphs and documents it keeps are gone. A demo window does not: it is
// shown in whatever context is current, by a host that may have several and
// may destroy one between two frames without saying so. A handle that outlives
// its context dangles, and a context made later can stand at the address of a
// dead one, so an address does not say which context a handle belongs to.
// ImTypeset::GetContextSerial() does: a number no other context has had.
//
// So everything the demo remembers is in a DemoState, found by the serial of
// the context the window is shown in. A handle is used only in the context it
// was made in, and the demo never destroys one: it dies with its context. Code
// of a host's that keeps handles without owning the context does the same.

struct DemoState {
    ImU64 Serial = 0;   // ImTypeset::GetContextSerial() of its context.
    ImU64 LastCall = 0; // Which call of ShowDemoWindow() last used it.

    float FontSize = 19.0f;                           // Chosen at the top of the window.
    ImTypesetStats LastFrameStats = ImTypesetStats(); // What the library did in its last frame.
    DemoParagraphState Paragraph;
    DemoFacesState Faces;
    DemoMathState Math;
    DemoSelectionState Selection;
    DemoInspectorState Inspector;
    DemoStressState Stress;
    DemoFontsState Fonts;
    DemoDocumentState Document;
};

// The states of the contexts the window was last shown in, DEMO_STATES_MAX at
// the most, so that a host making contexts one after another does not make
// the demo grow. A context that comes back after that many others starts from
// the defaults; what the demo made in it before is still its own and dies
// with it. The states themselves are freed with the program.
const int DEMO_STATES_MAX = 8;
struct DemoStates {
    ImVector<DemoState*> All;
    ImU64 Calls = 0;
    ~DemoStates()
    {
        for (int i = 0; i < All.Size; ++i)
            IM_DELETE(All[i]);
    }
};
DemoStates g_states;

// The state of the current context, made if there is none. Null with no
// current context.
DemoState* DemoStateOfCurrentContext()
{
    const ImU64 serial = ImTypeset::GetContextSerial();
    if (serial == 0)
        return NULL;
    int found = -1;
    int oldest = 0;
    for (int i = 0; i < g_states.All.Size; ++i) {
        if (g_states.All[i]->Serial == serial)
            found = i;
        if (g_states.All[i]->LastCall < g_states.All[oldest]->LastCall)
            oldest = i;
    }
    if (found < 0) {
        if (g_states.All.Size < DEMO_STATES_MAX) {
            found = g_states.All.Size;
            g_states.All.push_back(NULL);
        } else {
            found = oldest;
            IM_DELETE(g_states.All[found]);
        }
        g_states.All[found] = IM_NEW(DemoState)();
        g_states.All[found]->Serial = serial;
    }
    g_states.All[found]->LastCall = ++g_states.Calls;
    return g_states.All[found];
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
    DemoState* demo = DemoStateOfCurrentContext();
    if (demo == NULL) {
        ImGui::TextWrapped("There is no current ImTypeset context. Call "
                           "ImTypeset::CreateContext() after ImGui::CreateContext().");
        ImGui::End();
        return;
    }
    const ImTypesetStats stats = ImTypeset::GetStats();
    g_frame = demo->LastFrameStats;

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
    ImGui::SliderFloat("Font size", &demo->FontSize, 10.0f, 40.0f, "%.0f px");
    ImGui::SameLine();
    HelpMarker("ImTypesetStyle::FontSize for every section below: the height of a line, as "
               "ImGui means a font size. In Roboto, 19 px is about 16 px type.");
    g_font_size = demo->FontSize;
    ImGui::PushFont(NULL, g_font_size);
    g_style = ImTypeset::GetStyle();
    g_style.FontSize = ImGui::GetFontSize();
    ImGui::PopFont();

    if (ImGui::CollapsingHeader("Paragraph"))
        ShowDemoParagraph(&demo->Paragraph);
    if (ImGui::CollapsingHeader("Faces and sizes"))
        ShowDemoFaces(&demo->Faces);
    if (ImGui::CollapsingHeader("Math"))
        ShowDemoMath(&demo->Math);
    if (ImGui::CollapsingHeader("Selection"))
        ShowDemoSelection(&demo->Selection);
    if (ImGui::CollapsingHeader("Inspector"))
        ShowDemoInspector(&demo->Inspector);
    if (ImGui::CollapsingHeader("Stress"))
        ShowDemoStress(&demo->Stress, demo->Fonts.Added);
    if (ImGui::CollapsingHeader("Fonts"))
        ShowDemoFonts(&demo->Fonts);
    if (ImGui::CollapsingHeader("Known limits"))
        ShowDemoKnownLimits();
    if (ImGui::CollapsingHeader("Document"))
        ShowDemoDocument(&demo->Document, demo->Fonts.Added);

    // What this frame did, for the next one to show.
    const ImTypesetStats now = ImTypeset::GetStats();
    demo->LastFrameStats.Layouts = now.Layouts - stats.Layouts;
    demo->LastFrameStats.TextBuilds = now.TextBuilds - stats.TextBuilds;
    demo->LastFrameStats.DrawCalls = now.DrawCalls - stats.DrawCalls;
    demo->LastFrameStats.CulledCalls = now.CulledCalls - stats.CulledCalls;
    demo->LastFrameStats.VerticesEmitted = now.VerticesEmitted - stats.VerticesEmitted;
    demo->LastFrameStats.BytesHashed = now.BytesHashed - stats.BytesHashed;
    ImGui::End();
}

#endif // IMTYPESET_DISABLE_DEMO
