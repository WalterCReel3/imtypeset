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
// One section at a time: ImTypeset::ShowDemoSection() submits the contents of
// one section into whatever window is current, for a host that has pages of
// its own. See [SECTION] ShowDemoSection().
//
// Leaving it out: define IMTYPESET_DISABLE_DEMO for this file and the functions
// are still there to link against and draw nothing.
//
// Index of this file:
//   [SECTION] Helpers
//   [SECTION] Paragraph
//   [SECTION] Faces and sizes
//   [SECTION] Specimen
//   [SECTION] Math
//   [SECTION] Formula gallery
//   [SECTION] Selection
//   [SECTION] Inspector
//   [SECTION] Stress
//   [SECTION] Fonts
//   [SECTION] Known limits
//   [SECTION] Document
//   [SECTION] One state per context
//   [SECTION] ShowDemoWindow()
//   [SECTION] ShowDemoSection()

#include "imgui.h"
#include "imtypeset.h"

#ifdef IMTYPESET_DISABLE_DEMO

void ImTypeset::ShowDemoWindow(bool*) {}
void ImTypeset::ShowDemoSection(ImTypesetDemoSection) {}
const char* ImTypeset::GetDemoSectionName(ImTypesetDemoSection)
{
    return "";
}

#else

#include <math.h>   // floor
#include <stdio.h>  // snprintf
#include <string.h> // strlen, strchr, strstr, memcpy, memcmp

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

// A length in pixels put on the pixel grid, a half going up: floor(value + 0.5)
// in double, which is how the library snaps a coordinate. `(float)(int)(v +
// 0.5f)` is not that: it truncates, so it rounds a negative length the other
// way.
float SnapPx(float value)
{
    return (float)floor((double)value + 0.5);
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

// ABOUT KEEPING A MEASUREMENT. CalcTextSize() lays its text out to answer, so
// a section that wants the widths of twenty texts asks once and keeps the
// answers, with what they were taken at: the style, the fonts, and whatever
// numbers of its own they depend on. The library has no "the fonts changed"
// to ask for, so the fonts are told apart by the ImFont each role answers.
struct DemoSetting {
    bool Taken = false;
    ImTypesetStyle Style;
    ImFont* Fonts[ImTypesetFace_COUNT] = {};
    int FontsAdded = 0; // By the demo's Fonts section: a font replaced at the same address.
    float A = 0.0f;
    float B = 0.0f;
};

// Whether `kept` was taken at the present style, fonts and numbers. If it was
// not, it now is: the caller measures again.
bool SameSetting(DemoSetting* kept, int fonts_added, float a = 0.0f, float b = 0.0f)
{
    bool same = kept->Taken && kept->FontsAdded == fonts_added && kept->A == a && kept->B == b &&
                memcmp(&kept->Style, &g_style, sizeof(ImTypesetStyle)) == 0;
    for (int face = 0; face < ImTypesetFace_COUNT; ++face) {
        ImFont* font = ImTypeset::GetFont(face);
        same = same && kept->Fonts[face] == font;
        kept->Fonts[face] = font;
    }
    kept->Taken = true;
    kept->Style = g_style;
    kept->FontsAdded = fonts_added;
    kept->A = a;
    kept->B = b;
    return same;
}

// Text in ImGui's own font and the dimmed ink, with its right end at `right`
// (a screen x) or centred on it, at the height `y`. It is drawn, not
// submitted: the caller reserves the room.
void DimmedLabel(float x, float y, const char* text, bool centred)
{
    const float width = ImGui::CalcTextSize(text).x;
    const float left = centred ? x - width * 0.5f : x - width;
    ImGui::GetWindowDrawList()->AddText(ImVec2((float)(int)left, (float)(int)y),
                                        ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
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

    // The two sizes. FontSize is ImGui's: the height of a line. EmSize is a
    // typographer's: the em. The same number gives two sizes of type. (One
    // face up a scale of sizes is in the Specimen section.)
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
// [SECTION] Specimen
//-----------------------------------------------------------------------------
// The faces a host has added, arranged to be compared: one line in each at one
// size, one face up a scale of sizes, and pairs of letters set with and
// without their kerning. A row is set at its natural width and never broken:
// it is a row of a chart, and what is read off it is how wide it came.
//
// The numbers are a paragraph object's: its line says where the content starts
// and ends as layout has it, in fractions of a pixel. CalcTextSize() and the
// widgets answer in whole pixels, rounded up, which is right for reserving
// room and too coarse to read a kern of a pixel or two off.

const char PANGRAM[] = "The quick brown fox jumps over the lazy dog";
const char LADDER_TEXT[] = "Hamburgefonstiv";
// A modular scale: a major third, 1.25 a step, around a 16 px body, two steps
// below it and three above.
const int LADDER_STEPS = 6;
const float LADDER_BODY = 16.0f;
const float LADDER_RATIO = 1.25f;
// Six pairs most text faces pull together and one, "rt", that Roboto and
// others push apart: kerning is not only a matter of tightening.
const char* const KERN_PAIRS[][2] = {
    {"A", "V"}, {"T", "o"}, {"W", "a"}, {"Y", "o"}, {"P", "."}, {"L", "T"}, {"r", "t"},
};

struct DemoSpecimenState {
    float FaceSize = 24.0f; // The em the faces are compared at, in pixels.
    float KernSize = 44.0f; // The em the pairs are set at: a kern is some hundredths of it.
    bool Guides = true;
    ImTypesetParagraph* Scratch = NULL; // What the rows are measured with.
    DemoSetting Measured;
    ImVec2 Faces[ImTypesetFace_COUNT];
    ImVec2 Ladder[LADDER_STEPS];
    ImVec2 Pairs[IM_ARRAYSIZE(KERN_PAIRS)][3]; // The pair, its left letter, its right letter.
};

// The style of one row: a face at an em, ragged right.
ImTypesetStyle SpecimenStyle(ImTypesetFace face, float em_size)
{
    ImTypesetStyle style = g_style;
    style.Face = face;
    style.EmSize = em_size; // Wins over FontSize.
    style.Align = ImTypesetAlign_Left;
    return style;
}

// How wide `text` sets on one line in `style`, and how tall that line is: the
// extent of the one line of a paragraph laid out with room to spare.
ImVec2 SpecimenMeasure(ImTypesetParagraph* scratch, const char* text, const ImTypesetStyle& style)
{
    ImTypeset::SetParagraphText(scratch, text);
    ImTypeset::LayoutParagraph(scratch, 4096.0f, &style);
    const ImTypesetLine line = ImTypeset::GetParagraphLine(scratch, 0);
    return ImVec2(line.Max.x - line.Min.x, ImTypeset::GetParagraphSize(scratch).y);
}

// One sample: `text` in a face at an em, on one line. `size` is what
// SpecimenMeasure() answered for it, and the wrap width is a little more than
// that, so the line is never broken whatever room the window has. Returns
// where it was put.
ImVec2 SpecimenSample(const char* text, ImTypesetFace face, float em_size, const ImVec2& size)
{
    const ImTypesetStyle style = SpecimenStyle(face, em_size);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImTypeset::TextUnformatted(text, NULL, 0, (float)(int)size.x + 3.0f, &style);
    return pos;
}

// A label in a gutter `gutter` wide: right-aligned, so that labels of any
// length end at one x, and centred on a row `height` tall.
void SpecimenLabel(const char* label, float gutter, float height)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    DimmedLabel(pos.x + gutter, pos.y + (height - ImGui::GetTextLineHeight()) * 0.5f, label, false);
    ImGui::Dummy(ImVec2(gutter, 1.0f));
}

// A number at the end of a row `height` tall, centred on it.
void SpecimenNumber(float height, const char* fmt, ...) IM_FMTARGS(2);
void SpecimenNumber(float height, const char* fmt, ...)
{
    const float lower = (height - ImGui::GetTextLineHeight()) * 0.5f;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (lower > 0.0f ? lower : 0.0f));
    va_list args;
    va_start(args, fmt);
    ImGui::TextDisabledV(fmt, args);
    va_end(args);
}

// `fonts_added`: as for ShowDemoStress().
void ShowDemoSpecimen(DemoSpecimenState* state, int fonts_added)
{
    static const char* const LABELS[ImTypesetFace_COUNT] = {
        "", "Regular", "Bold", "Italic", "Bold Italic", "Code", "Code Bold", "Math",
    };
    if (ImTypeset::GetFont(ImTypesetFace_Regular) == NULL) {
        ImGui::TextWrapped("No font has been added for ImTypesetFace_Regular: there is nothing "
                           "to show a specimen of. See Fonts.");
        return;
    }

    // The body is the third rung, and is itself on the scale.
    float ladder[LADDER_STEPS];
    ladder[2] = LADDER_BODY;
    for (int i = 1; i >= 0; --i)
        ladder[i] = ladder[i + 1] / LADDER_RATIO;
    for (int i = 3; i < LADDER_STEPS; ++i)
        ladder[i] = ladder[i - 1] * LADDER_RATIO;

    // Which roles get a row. CodeBold answers Bold's font until it has one of
    // its own, and a second row of the same face under another name would say
    // nothing.
    bool shown[ImTypesetFace_COUNT] = {};
    int absent = 0;
    for (int face = ImTypesetFace_Regular; face < ImTypesetFace_COUNT; ++face) {
        ImFont* font = ImTypeset::GetFont(face);
        if (face == ImTypesetFace_CodeBold)
            shown[face] = font != NULL && font != ImTypeset::GetFont(ImTypesetFace_Bold);
        else
            shown[face] = font != NULL;
        absent += font == NULL ? 1 : 0;
    }

    // Measure, when anything a measurement depends on has changed.
    const bool fresh = state->Scratch == NULL;
    if (fresh)
        state->Scratch = ImTypeset::CreateParagraph();
    ImTypesetParagraph* scratch = state->Scratch;
    if (!SameSetting(&state->Measured, fonts_added, state->FaceSize, state->KernSize) || fresh) {
        for (int face = ImTypesetFace_Regular; face < ImTypesetFace_COUNT; ++face)
            state->Faces[face] = shown[face] ? SpecimenMeasure(scratch, PANGRAM,
                                                               SpecimenStyle(face, state->FaceSize))
                                             : ImVec2(0.0f, 0.0f);
        for (int i = 0; i < LADDER_STEPS; ++i)
            state->Ladder[i] = SpecimenMeasure(scratch, LADDER_TEXT,
                                               SpecimenStyle(ImTypesetFace_Regular, ladder[i]));
        const ImTypesetStyle style = SpecimenStyle(ImTypesetFace_Regular, state->KernSize);
        static ImGuiTextBuffer pair; // Scratch: emptied before each use.
        for (int i = 0; i < IM_ARRAYSIZE(KERN_PAIRS); ++i) {
            pair.resize(0);
            pair.appendf("%s%s", KERN_PAIRS[i][0], KERN_PAIRS[i][1]);
            state->Pairs[i][0] = SpecimenMeasure(scratch, pair.c_str(), style);
            state->Pairs[i][1] = SpecimenMeasure(scratch, KERN_PAIRS[i][0], style);
            state->Pairs[i][2] = SpecimenMeasure(scratch, KERN_PAIRS[i][1], style);
        }
    }
    const ImGuiTableFlags chart = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;
    char label[32];

    // The faces. A role is a file, not an effect: Bold is the font added for
    // ImTypesetFace_Bold, drawn as a bold.
    ImGui::SeparatorText("Every face at one size");
    ImTypeset::TextUnformatted(
        "Every role a font has been added for, in one line at one size. A role is a font file "
        "and not an effect applied to one: bold is the face added for ImTypesetFace_Bold, drawn "
        "as a bold, and italic a drawn italic. Faces of one family share their heights, so what "
        "separates them here is how wide each sets the line, which is the number at the end of "
        "its row; a code face or a math face usually differs in the height of its lower case "
        "as well.",
        NULL, 0, 0.0f, &g_style);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    ImGui::SliderFloat("ImTypesetStyle::EmSize##faces", &state->FaceSize, 10.0f, 48.0f, "%.0f px");
    float gutter = 0.0f;
    for (int face = ImTypesetFace_Regular; face < ImTypesetFace_COUNT; ++face)
        if (shown[face] && ImGui::CalcTextSize(LABELS[face]).x > gutter)
            gutter = ImGui::CalcTextSize(LABELS[face]).x;
    if (ImGui::BeginTable("faces", 3, chart)) {
        ImGui::TableSetupColumn("role", ImGuiTableColumnFlags_WidthFixed, gutter);
        ImGui::TableSetupColumn("sample");
        ImGui::TableSetupColumn("width");
        for (int face = ImTypesetFace_Regular; face < ImTypesetFace_COUNT; ++face) {
            if (!shown[face])
                continue;
            const ImVec2 size = state->Faces[face];
            ImGui::PushID(face);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            SpecimenLabel(LABELS[face], gutter, size.y);
            ImGui::TableNextColumn();
            SpecimenSample(PANGRAM, face, state->FaceSize, size);
            ImGui::TableNextColumn();
            SpecimenNumber(size.y, "%.2f px", (double)size.x);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (absent > 0)
        ImGui::TextDisabled("%d role%s no font: no row for %s.", absent,
                            absent == 1 ? " has" : "s have", absent == 1 ? "it" : "them");

    // Sizes. A face is added once and set at any size: ImGui's atlas bakes
    // each size when it is first drawn.
    ImGui::SeparatorText("One face up a scale of sizes");
    ImTypeset::TextUnformatted(
        "Not round numbers: a modular scale, a major third of 1.25 a step from a 16 px body, "
        "two steps below it and three above. Each row's size is its ImTypesetStyle::EmSize. A "
        "face is added once and set at any size.",
        NULL, 0, 0.0f, &g_style);
    gutter = ImGui::CalcTextSize("00.0 px").x;
    if (ImGui::BeginTable("ladder", 3, chart)) {
        ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthFixed, gutter);
        ImGui::TableSetupColumn("sample");
        ImGui::TableSetupColumn("width");
        for (int i = 0; i < LADDER_STEPS; ++i) {
            const ImVec2 size = state->Ladder[i];
            if (ladder[i] == (float)(int)ladder[i])
                snprintf(label, sizeof(label), "%d px", (int)ladder[i]);
            else
                snprintf(label, sizeof(label), "%.1f px", (double)ladder[i]);
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            SpecimenLabel(label, gutter, size.y);
            ImGui::TableNextColumn();
            SpecimenSample(LADDER_TEXT, ImTypesetFace_Regular, ladder[i], size);
            ImGui::TableNextColumn();
            SpecimenNumber(size.y, "%.2f px", (double)size.x);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Kerning. The library has no switch that turns it off and no call that
    // answers a pair's kern, and neither is needed to show it: a text of two
    // letters is set with their kern, and two texts of one letter each,
    // placed by their widths, are set without. The widths are layout's and
    // the ink is on whole pixels, so the second letter is put on the pixel
    // its own width asks for.
    ImGui::SeparatorText("Kerning, which ImGui's own text cannot do");
    ImTypeset::TextUnformatted(
        "Each pair is set twice in the Regular face. On the left as one text, with the kern the "
        "font gives the pair; on the right letter by letter, each letter a text of its own "
        "placed after the width of the one before, which is what a renderer that measures one "
        "glyph at a time produces, and what ImGui::Text() produces. The number in the gutter is "
        "the difference between the two widths: the pair's kern at this size. A pair the face "
        "does not kern says +0.00. Where the number is positive the kern pushes the letters "
        "apart, and it is the letter-by-letter setting that is too tight. Ink is drawn on "
        "whole pixels, so on screen the two settings differ by the kern to the nearest pixel.",
        NULL, 0, 0.0f, &g_style);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    ImGui::SliderFloat("ImTypesetStyle::EmSize##kern", &state->KernSize, 16.0f, 96.0f, "%.0f px");
    ImGui::SameLine();
    ImGui::Checkbox("Mark where each setting ends", &state->Guides);
    gutter = ImGui::CalcTextSize("+00.00 px").x;
    float column = 0.0f;
    for (int i = 0; i < IM_ARRAYSIZE(KERN_PAIRS); ++i) {
        const float apart = state->Pairs[i][1].x + state->Pairs[i][2].x;
        column = state->Pairs[i][0].x > column ? state->Pairs[i][0].x : column;
        column = apart > column ? apart : column;
    }
    column += state->KernSize * 0.4f;
    const float heading = ImGui::CalcTextSize("letter by letter").x;
    column = heading > column ? heading : column;
    if (ImGui::BeginTable("kerning", 4, chart)) {
        ImGui::TableSetupColumn("kern", ImGuiTableColumnFlags_WidthFixed, gutter);
        ImGui::TableSetupColumn("kerned", ImGuiTableColumnFlags_WidthFixed, column);
        ImGui::TableSetupColumn("letter by letter", ImGuiTableColumnFlags_WidthFixed, column);
        ImGui::TableSetupColumn("widths");
        ImGui::TableHeadersRow();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const ImU32 guide = IM_COL32(255, 96, 96, 200);
        static ImGuiTextBuffer pair; // Scratch: emptied before each use.
        for (int i = 0; i < IM_ARRAYSIZE(KERN_PAIRS); ++i) {
            const ImVec2 kerned = state->Pairs[i][0];
            const ImVec2 left = state->Pairs[i][1];
            const ImVec2 right = state->Pairs[i][2];
            const float apart = left.x + right.x;
            snprintf(label, sizeof(label), "%+.2f px", (double)(kerned.x - apart));
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            SpecimenLabel(label, gutter, kerned.y);

            ImGui::TableNextColumn();
            pair.resize(0);
            pair.appendf("%s%s", KERN_PAIRS[i][0], KERN_PAIRS[i][1]);
            ImVec2 pos =
                SpecimenSample(pair.c_str(), ImTypesetFace_Regular, state->KernSize, kerned);
            if (state->Guides)
                draw_list->AddLine(ImVec2(pos.x + kerned.x, pos.y),
                                   ImVec2(pos.x + kerned.x, pos.y + kerned.y), guide);

            // The same two letters, each a text of its own: the second is put
            // where the first one's width ends.
            ImGui::TableNextColumn();
            ImGui::PushID("left");
            pos = SpecimenSample(KERN_PAIRS[i][0], ImTypesetFace_Regular, state->KernSize, left);
            ImGui::PopID();
            ImGui::SetCursorScreenPos(ImVec2(pos.x + SnapPx(left.x), pos.y));
            ImGui::PushID("right");
            SpecimenSample(KERN_PAIRS[i][1], ImTypesetFace_Regular, state->KernSize, right);
            ImGui::PopID();
            if (state->Guides)
                draw_list->AddLine(ImVec2(pos.x + apart, pos.y),
                                   ImVec2(pos.x + apart, pos.y + kerned.y), guide);

            ImGui::TableNextColumn();
            SpecimenNumber(kerned.y, "%.2f px, %.2f px", (double)kerned.x, (double)apart);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    DisabledTextWrapped("Not here: a face's ascent, descent and x-height, which "
                        "ImTypeset::GetFontMetrics() answers; which characters it has a glyph "
                        "for, which ImTypeset::FaceHasGlyph() answers; and an overlay of its "
                        "boxes.",
                        NULL);
}

//-----------------------------------------------------------------------------
// [SECTION] Math
//-----------------------------------------------------------------------------
// ImTypesetTextFlags_Math makes the dollars of a text delimit formulas. A
// formula is set in the face added for ImTypesetFace_Math, as a box among the
// words. One that is outside the supported subset is never guessed at: its
// source is shown instead and the call says why and where.

// Sources, each with what it shows. Which of them the library sets is asked
// of it and not written down here: ShowDemoMath() puts every one through
// CalcTextSize() and shows it in the table if it set and in the list of
// refusals, with the reason it was given, if it did not.
struct DemoCandidate {
    const char* What;
    const char* Source;
};
const DemoCandidate CANDIDATES[] = {
    {"superscript", "x^2"},
    {"subscript", "x_i"},
    {"both scripts", "x_i^2"},
    {"a braced script", "x^{a+b}"},
    {"a script on a script", "e^{x^2}"},
    {"primes", "f'(x) + g''(x)"},
    {"fraction", "\\frac{a}{b}"},
    {"fraction in a fraction", "\\frac{1}{1 + \\frac{1}{x}}"},
    {"a fraction of fractions", "\\frac{\\frac{a}{b}}{\\frac{c}{d}}"},
    {"a binomial coefficient", "\\binom{n}{k} = \\frac{n!}{k!(n-k)!}"},
    {"Greek, lower case", "\\alpha \\beta \\gamma \\pi \\omega"},
    {"Greek, capitals", "\\Gamma \\Delta \\Sigma \\Omega"},
    {"sum with limits", "\\sum_{i=1}^{n} a_i"},
    {"product with limits", "\\prod_{k=1}^{n} k"},
    {"integral with bounds", "\\int_a^b f(x) dx"},
    {"limits beside, on request", "\\sum\\nolimits_{i=1}^{n} a_i"},
    {"limits stacked, on request", "\\int\\limits_a^b f"},
    {"relations", "a \\le b \\ne c \\approx d"},
    {"membership", "x \\in A \\subset B"},
    {"arrows", "a \\to b \\Rightarrow c"},
    {"binary operators", "a + b - c \\times d \\cdot e \\pm f"},
    {"fixed-size delimiters", "(a + b)[c]\\{d\\}"},
    {"delimiters sized to their content", "\\left( \\frac{a}{b} \\right)"},
    {"brackets, sized", "\\left[ \\frac{a}{b} \\right]"},
    {"angle brackets, sized", "\\left\\langle \\frac{a}{b} \\right\\rangle"},
    {"a brace on one side only", "\\left\\{ \\frac{x}{2} \\right."},
    {"a sized pair with scripts", "\\left. \\frac{x^2}{2} \\right\\rbrack_0^1"},
    {"parentheses, typed and fitted", "(\\frac{a}{b}) \\quad \\left(\\frac{a}{b}\\right)"},
    {"a square root", "\\sqrt{x^2 + y^2}"},
    {"a root over a fraction", "\\sqrt{\\frac{a}{b}}"},
    {"two root signs of the font's several", "\\sqrt{x} \\quad \\sqrt{\\frac{a}{b}}"},
    {"explicit spaces", "a \\, b \\; c \\quad d"},
    {"a thin space before the differential", "\\int_0^1 f(x) \\, dx"},
    {"the differential, against and a thin space off", "\\int f(x) dx \\quad \\int f(x) \\, dx"},
    {"a negative thin space", "\\int\\!\\!\\int f"},
    {"words inside a formula", "x = 1 \\text{ if } y > 0"},
    {"words in a subscript", "v_{\\text{max}} \\le c"},
    {"overline and underline", "\\overline{x + y} \\underline{z}"},
    {"named functions", "\\sin x + \\log y"},
    {"a limit", "\\lim_{n \\to \\infty} a_n"},
    {"an operator name", "\\operatorname{rank} A"},
    {"upright letters", "\\mathrm{d}x"},
    {"alphabets", "\\mathbb{R} \\mathcal{L} \\mathbf{v}"},
    {"dots on the baseline", "a_1, \\ldots, a_n"},
    {"dots on the axis", "a_1 + \\cdots + a_n"},
    {"ordinary symbols", "\\infty \\partial \\nabla \\forall \\exists"},
    {"symbols typed as themselves", "\xE2\x88\x9E \xE2\x89\xA4 \xE2\x88\x91"},
    {"Euler's identity", "e^{i\\pi} + 1 = 0"},
    {"the quadratic formula", "x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}"},
    {"a sum and its closed form", "\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2}"},
    {"a long one, for the line breaker",
     "a_1 + a_2 + a_3 + \\cdots + a_n = \\frac{n(a_1 + a_n)}{2}"},
    {"all of it at once",
     "\\left( \\sum_{i=1}^{n} \\frac{a_i}{b_i} \\right)^2 \\le \\sqrt{n} \\quad "
     "\\text{for every } n"},
    {"bars over parts of unequal width",
     "\\frac{a+b+c+d}{\\sqrt{x}} \\quad \\frac{\\sqrt{x}}{a+b+c+d} \\quad "
     "\\frac{a+b+c+d}{\\frac{x}{y}}"},
    {"rules and what they cover",
     "\\sqrt{x}^2 \\quad \\sqrt{x^2 + y^2} \\quad \\frac{1}{11111111}"},
    // Outside the subset. The first two are in it and are still refused: what
    // stands between the delimiters, or under the root, is far taller than
    // the largest ready-made sign a math font has.
    {"content far taller than the largest parenthesis",
     "\\left( 1 + \\frac{1}{1 + \\frac{1}{1 + \\frac{1}{1 + \\frac{1}{1 + \\frac{1}{x}}}}} "
     "\\right)"},
    {"roots nested past the largest radical sign",
     "\\sqrt{\\sqrt{\\sqrt{\\sqrt{\\sqrt{\\sqrt{x}}}}}}"},
    {"a root with a degree", "\\sqrt[3]{x}"},
    {"a delimiter of a named size", "\\big( x \\big)"},
    {"a brace that stretches sideways", "\\overbrace{a + b}"},
    {"a matrix", "\\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix}"},
    {"cases", "\\begin{cases} a \\\\ b \\end{cases}"},
    {"an environment", "\\begin{align} a &= b \\end{align}"},
    {"a column separator", "a & b"},
    {"a macro definition", "\\newcommand{\\R}{\\mathbb{R}}"},
    {"a plain TeX definition", "\\def\\half{\\frac{1}{2}}"},
    {"an accent", "\\hat{x}"},
    {"a change of style", "\\displaystyle\\sum_i a_i"},
    {"an infix fraction", "a \\over b"},
    {"a command nobody listed", "\\frobnicate{x}"},
    {"a script with nothing in it", "x^"},
    {"a brace left open", "\\frac{a}{b"},
};

// A reply as it would arrive from somewhere, a few bytes at a time: three
// paragraphs with a blank line between them, the second a display block.
const char STREAM_SOURCE[] =
    "The roots are $x = \\frac{-b \\pm \\sqrt{d}}{2a}$, where $d = b^2 - 4ac$ is the "
    "discriminant. Until a closing dollar sign has arrived the source is shown as text, and "
    "the words before it do not move when it closes.\n\n"
    "$$\n\\sum_{k=0}^{\\infty} \\frac{x^k}{k!} = e^x\n$$\n\n"
    "A display block is open from its first line, and is shown as its source until the line "
    "that closes it has come.";
const int STREAM_PARAGRAPHS = 3;
const int STREAM_PIECE = 3; // Bytes in one piece: few enough to stop inside a formula.
const float STREAM_PIECES_PER_SECOND = 12.0f;

struct DemoMathState {
    char Source[512] = "x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}";
    float Width = 460.0f;
    bool Replaced = false; // A row of the table was clicked last frame.
    ImTypesetParagraph* Display = NULL;

    // Where a formula can go.
    ImTypesetDocument* Places = NULL;
    int PlacesFonts = -1; // How many fonts the demo had added when it was built.
    bool PlacesDebug = false;

    // The stream.
    ImTypesetParagraph* Stream[STREAM_PARAGRAPHS] = {};
    int StreamFed = -1;   // Pieces handed over so far; -1 before the first frame.
    int StreamShown = -1; // Bytes the paragraphs were last given.
    bool StreamPlaying = false;
    float StreamDue = 0.0f; // Pieces owed by the clock, less than one.

    // The candidates: which of them set, and what the others were told.
    DemoSetting Sorted;
    // The widest formula of the subset as it is set in text and on a line of
    // its own, in pixels, under the setting `Sorted` holds.
    float WidestInline = 0.0f;
    float WidestDisplay = 0.0f;
    bool Sets[IM_ARRAYSIZE(CANDIDATES)] = {};
    ImGuiTextBuffer Reasons[IM_ARRAYSIZE(CANDIDATES)];
};

// Where a formula can go, as a document: a heading, two items of a list, a
// quotation, and a paragraph with a tagged, coloured run straight after a
// formula. Blocks that say ImTypesetBlockFlags_Math have their dollars looked
// for, exactly as a text with ImTypesetTextFlags_Math has.
void BuildDemoPlaces(ImTypesetDocument* document)
{
    static const char LINK[] = "is the theorem of Pythagoras";
    static const struct {
        ImTypesetBlockKind Kind;
        int Style;
        int Container;
        const char* Text;
    } PLACES[] = {
        {ImTypesetBlockKind_Heading, 1, 0, "In a heading: $E = mc^2$"},
        {ImTypesetBlockKind_ListItem, 2, 1,
         "In a list item, $\\alpha + \\beta = \\gamma$, with words after it."},
        {ImTypesetBlockKind_ListItem, 2, 1,
         "In a second item with a taller one, $\\frac{1}{1 - x}$, which opens its own gap."},
        {ImTypesetBlockKind_Paragraph, 0, 2,
         "In a quotation: $\\lim_{n \\to \\infty} \\frac{1}{n} = 0$."},
        {ImTypesetBlockKind_Paragraph, 0, 0,
         "A run straight after a formula keeps its own colour and its own tag: "
         "$a^2 + b^2 = c^2$ is the theorem of Pythagoras, and the formula before it is no part "
         "of the run."},
    };
    ImGuiTextBuffer text;
    ImVector<ImTypesetBlock> blocks;
    ImTypesetRun link;
    link.Color = IM_COL32(110, 170, 255, 255);
    link.Tag = 1;
    ImTypesetBlockStyle styles[3];
    styles[0].SpaceBelow = 8.0f;
    styles[1].Face = ImTypesetFace_Bold;
    styles[1].Scale = 1.25f;
    styles[1].Align = ImTypesetAlign_Left;
    styles[1].SpaceBelow = 8.0f;
    styles[2].Align = ImTypesetAlign_Left;
    styles[2].SpaceBelow = 8.0f;
    ImTypesetContainer containers[2];
    containers[0].Kind = ImTypesetContainerKind_List;
    containers[0].Indent = 22.0f;
    containers[1].Kind = ImTypesetContainerKind_Quote;

    for (int i = 0; i < IM_ARRAYSIZE(PLACES); ++i) {
        ImTypesetBlock block;
        block.Kind = PLACES[i].Kind;
        block.Flags = ImTypesetBlockFlags_Math;
        block.Level = 3;
        block.Style = PLACES[i].Style;
        block.Container = PLACES[i].Container;
        block.TextStart = text.size();
        text.append(PLACES[i].Text);
        block.TextEnd = text.size();
        if (block.Kind == ImTypesetBlockKind_ListItem) {
            block.MarkerStart = text.size();
            text.append("-");
            block.MarkerEnd = text.size();
            block.LeftIndent = 22.0f;
            block.Item = i;
        }
        if (block.Container == 2)
            block.LeftIndent = 18.0f;
        // A run's offsets are bytes of its block's text.
        const char* at = strstr(PLACES[i].Text, LINK);
        if (at != NULL) {
            link.Start = (int)(at - PLACES[i].Text);
            link.End = link.Start + (int)(sizeof(LINK) - 1);
            block.RunStart = 0;
            block.RunCount = 1;
        }
        blocks.push_back(block);
    }
    ImTypesetDocumentContent content;
    content.Text = text.begin();
    content.TextSize = text.size();
    content.Blocks = blocks.Data;
    content.BlockCount = blocks.Size;
    content.Runs = &link;
    content.RunCount = 1;
    content.Styles = styles;
    content.StyleCount = IM_ARRAYSIZE(styles);
    content.Containers = containers;
    content.ContainerCount = IM_ARRAYSIZE(containers);
    ImTypeset::SetDocumentBlocks(document, &content);
}

// Text that arrives a piece at a time. Each paragraph of the reply is a
// paragraph object given, whenever a piece arrives, as much of its text as
// has come; the library is asked what the dollars in it came to so far.
void ShowDemoMathStream(DemoMathState* state)
{
    const int size = (int)(sizeof(STREAM_SOURCE) - 1);
    const int pieces = (size + STREAM_PIECE - 1) / STREAM_PIECE;
    if (state->StreamFed < 0) {
        // It opens stopped a little way inside its first formula, so that an
        // unclosed dollar is on screen as text.
        const char* dollar = strchr(STREAM_SOURCE, '$');
        state->StreamFed = dollar != NULL ? (int)(dollar - STREAM_SOURCE + 8) / STREAM_PIECE : 0;
    }
    if (ImGui::Button("Restart")) {
        state->StreamFed = 0;
        state->StreamPlaying = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Step")) {
        state->StreamFed += 1;
        state->StreamPlaying = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(state->StreamPlaying ? "Pause" : "Play")) {
        state->StreamPlaying = !state->StreamPlaying;
        state->StreamDue = 0.0f;
        if (state->StreamPlaying && state->StreamFed >= pieces)
            state->StreamFed = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Finish")) {
        state->StreamFed = pieces;
        state->StreamPlaying = false;
    }
    if (state->StreamPlaying) {
        state->StreamDue += ImGui::GetIO().DeltaTime * STREAM_PIECES_PER_SECOND;
        const int due = (int)state->StreamDue;
        state->StreamDue -= (float)due;
        state->StreamFed += due;
    }
    if (state->StreamFed >= pieces) {
        state->StreamFed = pieces;
        state->StreamPlaying = false;
    }
    const int arrived =
        state->StreamFed * STREAM_PIECE < size ? state->StreamFed * STREAM_PIECE : size;

    // Where each paragraph of the reply is in it: a blank line ends one.
    int starts[STREAM_PARAGRAPHS + 1];
    int count = 0;
    starts[count++] = 0;
    for (const char* gap = strstr(STREAM_SOURCE, "\n\n"); gap != NULL && count < STREAM_PARAGRAPHS;
         gap = strstr(gap + 2, "\n\n"))
        starts[count++] = (int)(gap - STREAM_SOURCE) + 2;
    starts[count] = size;

    bool fresh = false;
    for (int k = 0; k < count; ++k) {
        if (state->Stream[k] == NULL) {
            state->Stream[k] = ImTypeset::CreateParagraph();
            fresh = true;
        }
    }
    if (fresh || arrived != state->StreamShown) {
        for (int k = 0; k < count; ++k) {
            const int end = arrived < starts[k + 1] ? arrived : starts[k + 1];
            const int start = starts[k] < end ? starts[k] : end;
            ImTypeset::SetParagraphText(state->Stream[k], STREAM_SOURCE + start,
                                        STREAM_SOURCE + end, NULL, 0, ImTypesetTextFlags_Math);
        }
        state->StreamShown = arrived;
    }

    int set = 0;
    int refused = 0;
    int loose = 0;
    for (int k = 0; k < count && starts[k] < arrived; ++k) {
        ImTypesetParagraph* paragraph = state->Stream[k];
        ParagraphItem(paragraph, state->Width, &g_style);
        // Every dollar of what has come is in a formula or is standing in the
        // text as a character. A formula's range has its delimiters in it.
        const int end = arrived < starts[k + 1] ? arrived : starts[k + 1];
        for (int at = starts[k]; at < end; ++at)
            loose += STREAM_SOURCE[at] == '$' ? 1 : 0;
        for (int i = 0; i < ImTypeset::GetParagraphFormulaCount(paragraph); ++i) {
            const ImTypesetFormula formula = ImTypeset::GetParagraphFormula(paragraph, i);
            if (formula.Refused)
                ++refused;
            else
                ++set;
            for (int at = formula.TextStart; at < formula.TextEnd; ++at)
                loose -= STREAM_SOURCE[starts[k] + at] == '$' ? 1 : 0;
        }
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::Text("Piece %d of %d. Formulas set: %d. Refused so far: %d. Dollar signs standing as "
                "text: %d.",
                state->StreamFed, pieces, set, refused, loose);
    ImGui::TextDisabled("A display block whose closing line has not come counts as refused "
                        "until it has.");
    ImGui::PopTextWrapPos();
}

// `fonts_added`: as for ShowDemoStress().
void ShowDemoMath(DemoMathState* state, int fonts_added)
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

    // In running text. A formula is a box in its line: a tall one opens the
    // gaps above and below its own line and no others, and the line breaker
    // moves it whole.
    ImGui::SeparatorText("In a paragraph");
    ImTypeset::TextUnformatted(
        "A formula of plain letters, such as $x$ or $a + b = c$, is no taller than the words "
        "around it, and the lines keep their leading. Scripts hang from the same baseline as "
        "the text: the letter in $x^2$ and in $x_i$ stands where a letter of the sentence "
        "would, and $x_i^2$ carries both at once. A fraction is another matter. The fraction "
        "$\\frac{a}{b}$ needs more room than a line of words has, so its line is set further "
        "from its neighbours, and a fraction of fractions, "
        "$\\frac{\\frac{a}{b}}{\\frac{c}{d}}$, pushes them further again. Only the gaps "
        "touching the formula's own line change.",
        NULL, ImTypesetTextFlags_Math, state->Width, &g_style);
    MarginGuides(state->Width);
    ImTypeset::TextUnformatted(
        "A formula is never broken across lines. Drag the width above and watch this one: the "
        "sum of an arithmetic progression, "
        "$a_1 + a_2 + a_3 + \\cdots + a_n = \\frac{n(a_1 + a_n)}{2}$, moves to the next line "
        "whole when it no longer fits, and the line it left is stretched to the margin like "
        "any other. At a width narrower than the formula itself it overhangs; it is not "
        "scaled and not split.",
        NULL, ImTypesetTextFlags_Math, state->Width, &g_style);
    MarginGuides(state->Width);

    // In the blocks of a document.
    ImGui::SeparatorText("Where a formula can go");
    ImGui::Checkbox("ImTypesetDocumentDrawFlags_Debug", &state->PlacesDebug);
    ImGui::SameLine();
    HelpMarker("Line boxes, baselines, and glue coloured by whether it was stretched or "
               "squeezed. A document can be drawn with them; a paragraph and the widgets "
               "cannot.");
    if (state->Places == NULL || state->PlacesFonts != fonts_added) {
        if (state->Places == NULL)
            state->Places = ImTypeset::CreateDocument();
        if (state->Places != NULL)
            BuildDemoPlaces(state->Places);
        state->PlacesFonts = fonts_added;
    }
    if (state->Places != NULL) {
        ImTypesetDocument* places = state->Places;
        ImTypeset::LayoutDocument(places, state->Width, &g_style);
        // Five blocks: all of it is laid out, so that its height is its own
        // and not an estimate.
        ImTypeset::LayoutDocumentWindow(places, 0.0f, 100000.0f);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImTypeset::GetDocumentSize(places));
        ImTypesetDocumentDraw draw;
        draw.Flags = state->PlacesDebug ? ImTypesetDocumentDrawFlags_Debug : 0;
        ImTypeset::DrawDocument(places, ImGui::GetWindowDrawList(), pos, &draw);
        ImGui::PushTextWrapPos(0.0f);
        if (ImGui::IsItemHovered()) {
            const ImVec2 mouse = ImGui::GetMousePos();
            const ImTypesetDocumentHit hit =
                ImTypeset::HitTestDocument(places, ImVec2(mouse.x - pos.x, mouse.y - pos.y));
            ImGui::Text("Under the pointer: block %d, byte %d, ImTypesetRun::Tag %d",
                        hit.Position.Block, hit.Position.Offset, hit.Tag);
        } else {
            ImGui::TextDisabled("Under the pointer: move the mouse over the blocks");
        }
        ImGui::TextDisabled(
            "Not here: a table cell. The table that ends the Document section has one.");
        ImGui::PopTextWrapPos();
    }

    ImGui::SeparatorText("In text that is still arriving");
    ShowDemoMathStream(state);

    // The subset, one construct at a time. Every candidate is put to the
    // library once, and again when the style or the fonts change: an
    // unwrapped CalcTextSize() lays it out, and the last error says whether a
    // formula in it was refused and why.
    ImTypesetStyle cell = g_style;
    cell.Align = ImTypesetAlign_Left;
    if (!SameSetting(&state->Sorted, fonts_added)) {
        state->WidestInline = 0.0f;
        state->WidestDisplay = 0.0f;
        for (int i = 0; i < IM_ARRAYSIZE(CANDIDATES); ++i) {
            text.resize(0);
            text.appendf("$%s$", CANDIDATES[i].Source);
            const ImVec2 size = ImTypeset::CalcTextSize(text.begin(), text.end(), 0.0f,
                                                        ImTypesetTextFlags_Math, &cell);
            const ImTypesetErrorInfo* error = ImTypeset::GetLastError();
            state->Sets[i] = error->Code != ImTypesetError_MathRefused;
            state->Reasons[i].resize(0);
            if (!state->Sets[i]) {
                state->Reasons[i].append(error->Message);
                continue;
            }
            if (size.x > state->WidestInline)
                state->WidestInline = size.x;
            // A text that is one $$ block is centred, and the size of centred
            // text is the width it was laid out to: its own width is its
            // line's, in a paragraph laid out wider than any formula.
            text.resize(0);
            text.appendf("$$%s$$", CANDIDATES[i].Source);
            if (ImTypesetParagraph* alone = ImTypeset::CreateParagraph()) {
                ImTypeset::SetParagraphText(alone, text.begin(), text.end(), NULL, 0,
                                            ImTypesetTextFlags_Math);
                ImTypeset::LayoutParagraph(alone, 100000.0f, &cell);
                if (ImTypeset::GetParagraphLineCount(alone) > 0) {
                    const ImTypesetLine line = ImTypeset::GetParagraphLine(alone, 0);
                    if (line.Max.x - line.Min.x > state->WidestDisplay)
                        state->WidestDisplay = line.Max.x - line.Min.x;
                }
                ImTypeset::DestroyParagraph(alone);
            }
        }
    }
    int sets = 0;
    for (int i = 0; i < IM_ARRAYSIZE(CANDIDATES); ++i)
        sets += state->Sets[i] ? 1 : 0;
    ImTypesetStyle code = cell;
    code.Face = ImTypesetFace_Code;

    ImGui::SeparatorText("The subset, one construct at a time");
    ImGui::TextDisabled("%d of %d sources set. Click a source to edit it above.", sets,
                        IM_ARRAYSIZE(CANDIDATES));
    // A formula is not broken, so each of its two columns is as wide as the
    // widest formula in it and the other two share what is left. A column of
    // a stated width takes it when the table is first made: the widths are
    // part of the table's ID, so that at another size it is another table.
    ImGui::PushID((int)state->WidestInline);
    ImGui::PushID((int)state->WidestDisplay);
    if (ImGui::BeginTable("formulas", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("source", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("$...$", ImGuiTableColumnFlags_WidthFixed,
                                state->WidestInline + 1.0f);
        ImGui::TableSetupColumn("$$...$$", ImGuiTableColumnFlags_WidthFixed,
                                state->WidestDisplay + 1.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < IM_ARRAYSIZE(CANDIDATES); ++i) {
            if (!state->Sets[i])
                continue;
            const char* candidate = CANDIDATES[i].Source;
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(CANDIDATES[i].What);
            ImGui::PopTextWrapPos();
            ImGui::TableNextColumn();
            // The source as it was typed, in the code face: without
            // ImTypesetTextFlags_Math a dollar or a backslash is a character.
            // A typeset text is an item like any other, so it can be clicked.
            ImTypeset::TextUnformatted(candidate, NULL, 0, 0.0f, &code);
            if (ImGui::IsItemHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(),
                                                    ImGui::GetItemRectMax(),
                                                    ImGui::GetColorU32(ImGuiCol_TextDisabled));
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                    strlen(candidate) < sizeof(state->Source)) {
                    memcpy(source, candidate, strlen(candidate) + 1);
                    state->Replaced = true;
                }
            }
            ImGui::TableNextColumn();
            text.resize(0);
            text.appendf("$%s$", candidate);
            ImTypeset::TextUnformatted(text.begin(), text.end(), ImTypesetTextFlags_Math, 0.0f,
                                       &cell);
            ImGui::TableNextColumn();
            // A text that is one $$ block: display style, centred in the cell.
            text.resize(0);
            text.appendf("$$%s$$", candidate);
            ImTypeset::TextUnformatted(text.begin(), text.end(), ImTypesetTextFlags_Math, 0.0f,
                                       &cell);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
    ImGui::PopID();

    // What cannot be set whole is never set as a damaged formula.
    ImGui::SeparatorText("Refused, and why");
    ImTypeset::TextUnformatted(
        "Whatever cannot be set whole is never set as a damaged formula. Its source is shown "
        "as it was typed, in the code face with its dollar signs, and the call that laid it "
        "out says why. Matrices are left out, and everything else that needs alignment with "
        "them; accents are not built; and a delimiter or a root sign is chosen from the sizes "
        "the font carries and never assembled from pieces, so content far taller than the "
        "largest is refused, where a sign that does not reach would be a damaged formula. The "
        "first two below are of that kind. The reasons are the library's, given when this "
        "list was made.",
        NULL, 0, 0.0f, &g_style);
    if (ImGui::BeginTable("refused", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_None, 2.0f);
        ImGui::TableSetupColumn("$...$, as it is shown", ImGuiTableColumnFlags_None, 4.0f);
        ImGui::TableSetupColumn("ImTypesetErrorInfo::Message", ImGuiTableColumnFlags_None, 4.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < IM_ARRAYSIZE(CANDIDATES); ++i) {
            if (state->Sets[i])
                continue;
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(CANDIDATES[i].What);
            ImGui::PopTextWrapPos();
            ImGui::TableNextColumn();
            text.resize(0);
            text.appendf("$%s$", CANDIDATES[i].Source);
            ImTypeset::TextUnformatted(text.begin(), text.end(), ImTypesetTextFlags_Math, 0.0f,
                                       &cell);
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.3f, 1.0f));
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(state->Reasons[i].c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
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
// [SECTION] Formula gallery
//-----------------------------------------------------------------------------
// Plates, each one chosen for a rule of math mode it makes visible, and most
// of them the same material set twice under two rules, side by side: a
// reader compares two things faster than a picture with a memory.
//
// A panel is a paragraph object whose whole text is the source of one formula,
// under one run that says so: ImTypesetRunFlags_DisplayFormula sets it in
// display style and ImTypesetRunFlags_Formula in text style, where it stands,
// with no dollars and no ImTypesetTextFlags_Math. The paragraph is then as wide
// as the formula, and its one line says where the baseline is, which is what
// puts the panels of a plate on one baseline.

struct DemoPanel {
    const char* Source;
    bool Display; // Display style, or text style.
    const char* Label;
};
const int PLATE_PANELS = 2;
struct DemoPlate {
    const char* Title;
    const char* Caption;
    DemoPanel Panels[PLATE_PANELS]; // A null Source ends them.
};
const DemoPlate PLATES[] = {
    {"A big operator, twice",
     "The large sigma is a different outline and not the small one scaled up: a glyph the math "
     "font keeps for display style and reaches through its chain of size variants. The limits "
     "move with it: stacked in display style, set to the right in text style.",
     {{"\\sum_{i=1}^n", true, "display style"}, {"\\sum_{i=1}^n", false, "text style"}}},
    {"Euler's identity",
     "The control: five atoms, one superscript, nothing nested. No space was typed into it, and "
     "the gap at the equals sign is wider than the one at the plus because a relation is not a "
     "binary operator.",
     {{"e^{i\\pi} + 1 = 0", true, ""}, {NULL, false, ""}}},
    {"Schr\xC3\xB6"
     "dinger's equation",
     "Two fractions inside one expression, and a reclassification in the middle: the minus "
     "after the equals sign has nothing on its left to bind, so it is set as an ordinary symbol "
     "and hugs the fraction. The later plus, which really is binary, keeps its space.",
     {{"i\\hbar\\frac{\\partial\\psi}{\\partial t} = "
       "-\\frac{\\hbar^2}{2m}\\nabla^2\\psi + V\\psi",
       true, ""},
      {NULL, false, ""}}},
    {"The Basel problem",
     "Both limits stacked over the operator, because the style is display and a sum takes its "
     "limits above and below there; which is why the text-style sigma of the first plate sets "
     "its bounds to the right instead.",
     {{"\\sum_{n=1}^\\infty \\frac{1}{n^2} = \\frac{\\pi^2}{6}", true, ""}, {NULL, false, ""}}},
    {"Cauchy's integral formula",
     "The plate before, the other way round: an integral keeps its bound beside the sign in "
     "display style too, where a sum stacks them. The parentheses are typed as themselves and "
     "stay the size of the type; nothing here is asked to grow.",
     {{"f(a) = \\frac{1}{2\\pi i} \\oint_\\gamma \\frac{f(z)}{z-a} dz", true, ""},
      {NULL, false, ""}}},
    {"A continued fraction, four deep",
     "The golden ratio, and the only plate that shows all four math styles at once. Each bar "
     "drops one step: display, text, script, script-script. Then it stops, so the fourth bar is "
     "set at the size of the third and three sizes of type serve any depth. Watch the plus "
     "signs as well: in the script styles the space around a binary operator is dropped, so "
     "the deep ones close up.",
     {{"\\varphi = 1 + \\frac{1}{1+\\frac{1}{1+\\frac{1}{1+\\frac{1}{1+1}}}}", true, ""},
      {NULL, false, ""}}},
    {"Scripts stack, and the two directions are not mirror images",
     "The same four letters, hung upward and downward. A subscript is always cramped and a "
     "superscript is not, and a cramped style holds its own scripts down, so the left tower "
     "rises further than the right one drops. Both reach the smallest size at the third "
     "letter.",
     {{"x^{y^{z^w}}", true, "superscripts"}, {"x_{y_{z_w}}", true, "subscripts"}}},
    {"The spacing table, on and off",
     "The same five glyphs, set twice. On the left the plus is a binary operator and the equals "
     "sign a relation, so the one is given a medium space on each side and the other a thick "
     "one. On the right each sign is written in braces, {+} and {=}, which makes it an ordinary "
     "symbol, and no space is asked for. Neither source has a space in it.",
     {{"a+b=c", true, "classified"}, {"a{+}b{=}c", true, "all ordinary"}}},
    {"A minus sign is only binary in context",
     "On the right something precedes it, so it is a binary operator and takes a medium space "
     "on each side. On the left nothing does: it is set as an ordinary symbol, and the space is "
     "not narrowed, it is never asked for.",
     {{"-x", true, "nothing before it"}, {"a-x", true, "something before it"}}},
    {"A radical, sized to its radicand",
     "The root sign is chosen, never stretched: the font carries several, and the smallest that "
     "covers the radicand, the clearance above it and the rule is the one set. So the two signs "
     "here are different outlines, and the rule meets each at the top of its stroke.",
     {{"\\sqrt{a^2+b^2}", true, "one line"}, {"\\sqrt{\\frac{a^2+b^2}{c^2}}", true, "a fraction"}}},
    {"Parentheses, fixed and fitted",
     "The same fraction between the same two characters. On the left they are typed as "
     "themselves and stay the size of the type. On the right they are a \\left ... \\right "
     "pair: what stands between them is measured and the smallest parenthesis in the font that "
     "covers it is set, centred on the axis.",
     {{"(\\frac{a^2+b^2}{c^2})", true, "fixed"},
      {"\\left(\\frac{a^2+b^2}{c^2}\\right)", true, "fitted"}}},
};

struct DemoFormulasState {
    float Size = 34.0f; // The em every plate is set at, in pixels: its smallest type is half of it.
    bool Guides = false;
    ImTypesetParagraph* Panels[IM_ARRAYSIZE(PLATES)][PLATE_PANELS] = {};
};

void ShowDemoFormulas(DemoFormulasState* state)
{
    if (ImTypeset::GetFont(ImTypesetFace_Math) == NULL)
        ImGui::TextWrapped("No font has been added for ImTypesetFace_Math, so every plate shows "
                           "its source and the reason it was refused. See Fonts.");
    ImTypeset::TextUnformatted(
        "Each plate is here for one rule of TeX's math mode that it makes visible, and says "
        "which. Nothing in a source is a typed space that counts: every gap is the spacing "
        "between the classes of two neighbouring atoms.",
        NULL, 0, 0.0f, &g_style);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    ImGui::SliderFloat("ImTypesetStyle::EmSize##formulas", &state->Size, 12.0f, 72.0f, "%.0f px");
    ImGui::SameLine();
    ImGui::Checkbox("Baseline and boxes", &state->Guides);

    ImTypesetStyle style = g_style;
    style.EmSize = state->Size;
    style.Align = ImTypesetAlign_Left;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float gap = state->Size; // Between two panels: one em.
    const float label_height = ImGui::GetTextLineHeight();

    for (int n = 0; n < IM_ARRAYSIZE(PLATES); ++n) {
        const DemoPlate& plate = PLATES[n];
        ImGui::PushID(n);
        ImGui::SeparatorText(plate.Title);
        ImTypeset::TextUnformatted(plate.Caption, NULL, 0, 0.0f, &g_style);

        // Lay the panels out, and measure the row they make: each column is
        // as wide as the wider of its formula and its label, and the row is
        // as tall as the tallest rise above the baseline and the deepest
        // drop below it.
        const float avail = ImGui::GetContentRegionAvail().x;
        ImTypesetParagraph* panels[PLATE_PANELS] = {};
        ImVec2 sizes[PLATE_PANELS];
        float baselines[PLATE_PANELS] = {};
        float columns[PLATE_PANELS] = {};
        int count = 0;
        float width = 0.0f;
        float above = 0.0f;
        float below = 0.0f;
        bool labelled = false;
        for (; count < PLATE_PANELS && plate.Panels[count].Source != NULL; ++count) {
            const DemoPanel& panel = plate.Panels[count];
            ImTypesetParagraph*& paragraph = state->Panels[n][count];
            if (paragraph == NULL) {
                paragraph = ImTypeset::CreateParagraph();
                ImTypesetRun run;
                run.Start = 0;
                run.End = (int)strlen(panel.Source);
                run.Flags =
                    panel.Display ? ImTypesetRunFlags_DisplayFormula : ImTypesetRunFlags_Formula;
                ImTypeset::SetParagraphText(paragraph, panel.Source, NULL, &run, 1);
            }
            panels[count] = paragraph;
            // A formula is not broken, so the width it is laid out to only has
            // to be more than it needs: one number, whatever the window's.
            ImTypeset::LayoutParagraph(paragraph, 4096.0f, &style);
            sizes[count] = ImTypeset::GetParagraphSize(paragraph);
            baselines[count] = ImTypeset::GetParagraphLine(paragraph, 0).Baseline;
            const float label_width = ImGui::CalcTextSize(panel.Label).x;
            columns[count] = sizes[count].x > label_width ? sizes[count].x : label_width;
            width += columns[count] + (count > 0 ? gap : 0.0f);
            above = baselines[count] > above ? baselines[count] : above;
            const float drop = sizes[count].y - baselines[count];
            below = drop > below ? drop : below;
            labelled |= panel.Label[0] != 0;
        }

        // The row, centred, and under it the labels on the same columns.
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(avail, above + below + (labelled ? label_height + 4.0f : 0.0f)));
        float x = origin.x + (avail > width ? (float)(int)((avail - width) * 0.5f) : 0.0f);
        const float baseline = origin.y + above;
        if (state->Guides)
            draw_list->AddLine(ImVec2(x, baseline), ImVec2(x + width, baseline),
                               IM_COL32(255, 96, 96, 160));
        // DrawParagraph() draws what it is told to, in view or not; the row's
        // rectangle was reserved above, and a row that is scrolled out of
        // view is skipped here, as a host with a long page of its own skips.
        const bool in_view = ImGui::IsItemVisible();
        for (int i = 0; i < count && in_view; ++i) {
            const ImVec2 pos(x + (float)(int)((columns[i] - sizes[i].x) * 0.5f),
                             baseline - baselines[i]);
            ImTypeset::DrawParagraph(panels[i], draw_list, pos);
            if (state->Guides)
                draw_list->AddRect(pos, ImVec2(pos.x + sizes[i].x, pos.y + sizes[i].y),
                                   IM_COL32(80, 160, 255, 140));
            DimmedLabel(x + columns[i] * 0.5f, baseline + below + 4.0f, plate.Panels[i].Label,
                        true);
            x += columns[i] + gap;
        }

        // A panel that could not be set shows its source, and says why.
        for (int i = 0; i < count; ++i) {
            if (ImTypeset::GetParagraphFormulaCount(panels[i]) < 1)
                continue;
            const ImTypesetFormula formula = ImTypeset::GetParagraphFormula(panels[i], 0);
            if (formula.Refused)
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "refused at byte %d: %s",
                                   formula.BytePos, formula.Reason);
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    DisabledTextWrapped("Not here: whether the math face has a glyph for every character of a "
                        "source. ImTypeset::FaceHasGlyph() answers that for one character.",
                        NULL);
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
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Clipboard: %.80s", clipboard != NULL ? clipboard : "");
    ImGui::PopTextWrapPos();

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
    ImGui::PushTextWrapPos(0.0f);
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
    ImGui::PopTextWrapPos();

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
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%d paragraphs in the document, copies %d to %d submitted",
                        shown_copies * IM_ARRAYSIZE(DOCUMENT), first + 1, last + 1);
    ImGui::PopTextWrapPos();
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
        "pixel and a half of the width it was laid out with, and every glyph within two pixels "
        "of where it was laid out, which is how far a caret can stand from its letter. The "
        "price is paid inside the "
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
        "This window keeps what it shows for the 32 contexts it was last shown in. A program "
        "with more live ImTypeset contexts than that, showing it in each in turn, has it start "
        "over in a context at every turn, and what it had made there before stays in that "
        "context until the context is destroyed.",
        "Math is a subset. Accents, matrices, alignment and macros are refused, and a refused "
        "formula is shown as its source: $\\hat{x}$.",
        "A formula's type is as small as its style makes it: the scripts of scripts in "
        "$\\frac{\\frac{a}{b}}{\\frac{c}{d}}$ are a few pixels tall at text sizes.",
    };
    for (int i = 0; i < IM_ARRAYSIZE(LIMITS); ++i) {
        ImGui::Bullet();
        ImTypeset::TextUnformatted(LIMITS[i], NULL, ImTypesetTextFlags_Math, 0.0f, &style);
    }

    // Two widths for a character (ADVANCES in imtypeset.h). A ruler in the
    // Code face three ways from one x: measured on the device, which is the
    // default for that face; measured by the font; and drawn by ImGui itself
    // in the role's ImFont at the pixel height the typeset rows have.
    ImGui::Bullet();
    ImTypeset::TextUnformatted(
        "A character has two widths. The font's own is a fraction of a pixel; the one ImGui's "
        "atlas holds for the size on this display is, under a hinting rasteriser, a whole "
        "number of pixels. Text in the Code faces is measured by the second unless a style "
        "says otherwise (ImTypesetStyle::Advances), so that rows of code stand on one grid "
        "and under ImGui's own text in the same font; it is then a layout for this display, "
        "and its characters are not the width the designer drew. Everything else is measured "
        "by its font and drawn within two pixels of that. The same ruler three ways:",
        NULL, 0, 0.0f, &style);
    static const char* const RULER = "1234567890123456789012345678901234567890";
    ImTypesetStyle code = style;
    code.Face = ImTypesetFace_Code;
    ImGui::TextDisabled("ImTypesetAdvances_Default: the device's advances");
    ImTypeset::TextUnformatted(RULER, NULL, 0, 0.0f, &code);
    code.Advances = ImTypesetAdvances_Font;
    ImGui::TextDisabled("ImTypesetAdvances_Font: the font's advances, each glyph on a pixel");
    ImTypeset::TextUnformatted(RULER, NULL, 0, 0.0f, &code);
    ImTypesetFontMetrics regular_face, code_face;
    ImFont* code_font = ImTypeset::GetFont(ImTypesetFace_Code);
    if (code_font != NULL &&
        ImTypeset::GetFontMetrics(ImTypesetFace_Regular, 1000.0f, &regular_face) &&
        ImTypeset::GetFontMetrics(ImTypesetFace_Code, 1000.0f, &code_face)) {
        ImGui::TextDisabled("ImGui::TextUnformatted in the same ImFont at the same pixel height");
        // The pixel height of the Code face at the em the rows above are set
        // at: the two faces are one em, not one height.
        ImGui::PushFont(code_font, g_font_size * code_face.FontSize / regular_face.FontSize);
        ImGui::TextUnformatted(RULER);
        ImGui::PopFont();
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
// And at the end one table: a block whose text is its cells, each a stretch
// of it, written as a copy of the table reads (a tab between cells, a newline
// between rows). Its columns are held, so text that arrives in its last cell
// (the Stream checkbox) widens that column and moves no other while the table
// is narrower than the document; once the held widths no longer fit, the table
// is fitted to the width like any other and every column can move.
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
    static const char* const TABLE[4][3] = {
        {"Quantity", "Value", "Remarks"},
        {"The golden ratio", "$\\frac{1 + \\sqrt{5}}{2}$", "A formula is a cell's text like any."},
        {"A dozen", "12", "Numbers read best set right."},
        {"Text that arrives", "0", "This cell grows:"},
    };
    ImVector<ImTypesetCell> cells;
    ImVector<ImTypesetRun> runs;
    ImTypesetTableColumn columns[3];
    columns[1].Align = ImTypesetAlign_Right;
    {
        ImTypesetBlock table;
        table.Kind = ImTypesetBlockKind_Table;
        table.Flags = ImTypesetBlockFlags_TableHeader | ImTypesetBlockFlags_TableHeld |
                      ImTypesetBlockFlags_Math;
        table.Style = 2;
        table.TextStart = text.size();
        table.CellCount = 12;
        table.ColumnCount = 3;
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 3; ++column) {
                if (column != 0)
                    text.append("\t");
                else if (row != 0)
                    text.append("\n");
                // A cell's offsets, and a run's, are bytes of the BLOCK's text.
                ImTypesetCell cell;
                cell.TextStart = text.size() - table.TextStart;
                text.append(TABLE[row][column]);
                cell.TextEnd = text.size() - table.TextStart;
                cell.Flags = column == 0 ? ImTypesetCellFlags_RowStart : 0;
                cells.push_back(cell);
                if (row == 0) {
                    ImTypesetRun bold;
                    bold.Start = cell.TextStart;
                    bold.End = cell.TextEnd;
                    bold.Face = ImTypesetFace_Bold;
                    runs.push_back(bold);
                }
            }
        }
        table.TextEnd = text.size();
        table.RunCount = runs.Size;
        blocks.push_back(table);
    }
    ImTypesetDocumentContent content;
    content.Text = text.begin();
    content.TextSize = text.size();
    content.Blocks = blocks.Data;
    content.BlockCount = blocks.Size;
    content.Runs = runs.Data;
    content.RunCount = runs.Size;
    content.Cells = cells.Data;
    content.CellCount = cells.Size;
    content.Columns = columns;
    content.ColumnCount = IM_ARRAYSIZE(columns);
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
    int Columns = 1; // How many the view shows across; 1 is the document uncut.
    bool Balance = false;
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
               "text arrives. One block is laid out again; no other is visited. The last "
               "block is the table at the document's end, and the text goes into its last "
               "cell: scroll down to watch a held column grow. The other columns keep their "
               "places until the table is as wide as the pane; from there it is fitted to the "
               "width and they are squeezed.");
    ImGui::SameLine();
    ImGui::Checkbox("Marks", &state->MarksOn);
    ImGui::SameLine();
    HelpMarker("Every \"formula\" of the first paragraphs, given to the draw as (block, byte) "
               "ranges: a filled mark behind one, an underline and another ink for the next. "
               "Marks are no part of the layout and cost nothing when they change.");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    ImGui::SliderInt("Columns", &state->Columns, 1, 4);
    ImGui::SameLine();
    ImGui::Checkbox("Balance", &state->Balance);
    ImGui::SameLine();
    HelpMarker("SetDocumentColumns(): the same document, cut where TeX would end a page and "
               "read down one column and then the next. Filled in order, a column is as tall "
               "as the view, the rest are to the right (scroll sideways), and only the columns "
               "in view are laid out. Balanced, the columns are one height, the least that "
               "holds the document, which needs every block laid out: watch the counts below. "
               "Drag across columns to select in reading order.");

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

    // Columns are how a document is shown, said beside the layout and not in
    // the description: one column is the document uncut.
    ImTypesetColumns columns;
    columns.Count = state->Columns;
    columns.Flags = state->Balance ? ImTypesetColumnFlags_Balance : ImTypesetColumnFlags_None;
    ImTypeset::SetDocumentColumns(state->Document, state->Columns > 1 ? &columns : NULL);

    // The whole document in a child window that scrolls it. Drag to select
    // across blocks; Ctrl+C copies.
    ImTypeset::DocumentView("##document", state->Document,
                            ImVec2(0.0f, ImGui::GetFontSize() * 18.0f), NULL, &draw, &g_style);
    ShowLastError();

    const ImTypesetDocumentStats now = ImTypeset::GetDocumentStats(state->Document);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::Text("%d blocks (the last a table), %d of them never laid out at this width.",
                now.Blocks, now.BlocksDeferred);
    ImGui::Text("This frame: %d laid out, %d texts built, %d blocks visited by a pass over all.",
                (int)(now.BlocksLaidOut - state->Last.BlocksLaidOut),
                (int)(now.MaterialBuilds - state->Last.MaterialBuilds),
                (int)(now.StructureVisits - state->Last.StructureVisits));
    ImGui::Text("Of the table: %d cells measured and %d rows set this frame.",
                (int)(now.TableCellsMeasured - state->Last.TableCellsMeasured),
                (int)(now.TableRowsSet - state->Last.TableRowsSet));
    if (state->Columns > 1)
        ImGui::Text("In %d columns: %d cut this frame, %d heights tried to balance them.",
                    ImTypeset::GetDocumentColumnCount(state->Document),
                    (int)(now.ColumnsCut - state->Last.ColumnsCut),
                    (int)(now.BalanceHeightsTried - state->Last.BalanceHeightsTried));
    ImGui::PopTextWrapPos();
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
    ImU64 LastCall = 0; // Which call of the demo last used it.

    float FontSize = 19.0f;                           // Chosen at the top of the window.
    ImTypesetStats LastFrameStats = ImTypesetStats(); // What the library did in its last frame.
    // The same for each section shown alone, by ShowDemoSection().
    ImTypesetStats LastSectionStats[ImTypesetDemoSection_COUNT] = {};
    DemoParagraphState Paragraph;
    DemoFacesState Faces;
    DemoSpecimenState Specimen;
    DemoMathState Math;
    DemoFormulasState Formulas;
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
//
// The limit that follows: a program that keeps MORE than DEMO_STATES_MAX
// contexts alive and shows the demo in each in turn has a context's state
// dropped and made again at every turn, and each time the paragraphs and
// documents the dropped state had made stay in their context, unused, until
// that context is destroyed. The public header has nothing to ask whether a
// context of some serial is still alive, so the demo cannot destroy them for
// it. Thirty-two is far past any number of contexts a program shows a demo
// in, and costs at most that many states of some 4 kB each, in each of the
// two sets below, kept until the program ends. Known limits says so.
const int DEMO_STATES_MAX = 32;
struct DemoStates {
    ImVector<DemoState*> All;
    ImU64 Calls = 0;
    ~DemoStates()
    {
        for (int i = 0; i < All.Size; ++i)
            IM_DELETE(All[i]);
    }
};
// Two sets of them: what the demo window remembers, and what the sections
// shown alone by ShowDemoSection() remember. A host may show both in one
// frame, at two sizes of type and in windows of two widths, and a paragraph
// object or a kept measurement shared by the two would be laid out or taken
// again at each call.
DemoStates g_window_states;
DemoStates g_section_states;

// The state of the current context in one of the sets, made if there is none.
// Null with no current context.
DemoState* DemoStateOfCurrentContext(DemoStates& g_states)
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

// The titles, as the window's headers show them.
const char* const SECTION_NAMES[ImTypesetDemoSection_COUNT] = {
    "Paragraph", "Faces and sizes", "Specimen", "Math",         "Formula gallery", "Selection",
    "Inspector", "Stress",          "Fonts",    "Known limits", "Document",
};

void ShowSection(DemoState* demo, ImTypesetDemoSection section)
{
    switch (section) {
    case ImTypesetDemoSection_Paragraph:
        ShowDemoParagraph(&demo->Paragraph);
        break;
    case ImTypesetDemoSection_Faces:
        ShowDemoFaces(&demo->Faces);
        break;
    case ImTypesetDemoSection_Specimen:
        ShowDemoSpecimen(&demo->Specimen, demo->Fonts.Added);
        break;
    case ImTypesetDemoSection_Math:
        ShowDemoMath(&demo->Math, demo->Fonts.Added);
        break;
    case ImTypesetDemoSection_Formulas:
        ShowDemoFormulas(&demo->Formulas);
        break;
    case ImTypesetDemoSection_Selection:
        ShowDemoSelection(&demo->Selection);
        break;
    case ImTypesetDemoSection_Inspector:
        ShowDemoInspector(&demo->Inspector);
        break;
    case ImTypesetDemoSection_Stress:
        ShowDemoStress(&demo->Stress, demo->Fonts.Added);
        break;
    case ImTypesetDemoSection_Fonts:
        ShowDemoFonts(&demo->Fonts);
        break;
    case ImTypesetDemoSection_KnownLimits:
        ShowDemoKnownLimits();
        break;
    case ImTypesetDemoSection_Document:
        ShowDemoDocument(&demo->Document, demo->Fonts.Added);
        break;
    default:
        break;
    }
}

// What the library did between two readings of its counters: the ones a frame
// in which something happened adds to.
ImTypesetStats StatsBetween(const ImTypesetStats& before, const ImTypesetStats& after)
{
    ImTypesetStats done = ImTypesetStats();
    done.Layouts = after.Layouts - before.Layouts;
    done.TextBuilds = after.TextBuilds - before.TextBuilds;
    done.DrawCalls = after.DrawCalls - before.DrawCalls;
    done.CulledCalls = after.CulledCalls - before.CulledCalls;
    done.VerticesEmitted = after.VerticesEmitted - before.VerticesEmitted;
    done.BytesHashed = after.BytesHashed - before.BytesHashed;
    return done;
}

} // namespace

//-----------------------------------------------------------------------------
// [SECTION] ShowDemoWindow()
//-----------------------------------------------------------------------------

namespace
{

// Whether widgets can be submitted now: an ImGui context, and a frame begun
// and not rendered. NewFrame invalidates the draw data and Render makes it
// valid again, which is the one sign imgui.h gives of being inside a frame.
// Outside one ImGui::Begin() and the calls that read the current window do
// not answer: they assert, or read a window that is not there.
bool InsideFrame()
{
    return ImGui::GetCurrentContext() != NULL && ImGui::GetFrameCount() > 0 &&
           ImGui::GetDrawData() == NULL;
}

} // namespace

void ImTypeset::ShowDemoWindow(bool* p_open)
{
    if (!InsideFrame())
        return;
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
    DemoState* demo = DemoStateOfCurrentContext(g_window_states);
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

    for (int section = 0; section < ImTypesetDemoSection_COUNT; ++section)
        if (ImGui::CollapsingHeader(SECTION_NAMES[section]))
            ShowSection(demo, section);

    // What this frame did, for the next one to show.
    demo->LastFrameStats = StatsBetween(stats, ImTypeset::GetStats());
    ImGui::End();
}

//-----------------------------------------------------------------------------
// [SECTION] ShowDemoSection()
//-----------------------------------------------------------------------------
// The contents of one section, into the window that is current. A host with
// pages of its own (tabs, a sidebar and a child window) shows a section as one
// of them:
//
//     for (int n = 0; n < ImTypesetDemoSection_COUNT; ++n)
//         if (ImGui::BeginTabItem(ImTypeset::GetDemoSectionName(n))) {
//             ImTypeset::ShowDemoSection(n);
//             ImGui::EndTabItem();
//         }
//
// The section sets its text at the size ImGui's own text has there, so the
// host sizes it with ImGui::PushFont(NULL, size) around the call. It is the
// section the demo window shows, with a state of its own: see [SECTION] One
// state per context.

void ImTypeset::ShowDemoSection(ImTypesetDemoSection section)
{
    if (section < 0 || section >= ImTypesetDemoSection_COUNT || !InsideFrame())
        return;
    DemoState* demo = DemoStateOfCurrentContext(g_section_states);
    if (demo == NULL) {
        ImGui::TextWrapped("There is no current ImTypeset context. Call "
                           "ImTypeset::CreateContext() after ImGui::CreateContext().");
        return;
    }
    const ImTypesetStats stats = ImTypeset::GetStats();
    g_frame = demo->LastSectionStats[section];
    g_font_size = ImGui::GetStyle().FontSizeBase;
    g_style = ImTypeset::GetStyle();
    g_style.FontSize = ImGui::GetFontSize();

    // Under an ID of its own, so that a section's "Width" is not the host's.
    ImGui::PushID("ImTypeset Demo");
    ImGui::PushID(section);
    ShowSection(demo, section);
    ImGui::PopID();
    ImGui::PopID();
    demo->LastSectionStats[section] = StatsBetween(stats, ImTypeset::GetStats());
}

const char* ImTypeset::GetDemoSectionName(ImTypesetDemoSection section)
{
    if (section < 0 || section >= ImTypesetDemoSection_COUNT)
        return "";
    return SECTION_NAMES[section];
}

#endif // IMTYPESET_DISABLE_DEMO
