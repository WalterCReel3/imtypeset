// Where a glyph is drawn against where it was laid out, and against where
// Dear ImGui's own text puts the same character of the same string in the
// same ImFont at the same size: requests H34 and H35 of the first two hosts.
//
// The inputs are theirs: a fenced block of eight rows in the code face, among
// them a ruler of forty digits and a row of seventy-four characters, and the
// ruler again as a paragraph in the body face, where it is one unbroken word.
// Sizes are stated both ways a host states one, as a pixel height and as an
// em, 14 to 20; under two face tables, the usual one and one whose every role
// is the monospace file; on displays of density 1 and 2.
//
// Everything is read from vertices, and no pixel: a glyph's pen is the left
// edge of its quad less the offset its bitmap has in the atlas entry the quad
// is textured from, which is checked by the quad's texture coordinates.
//
// WHAT IS ASSERTED, per glyph, on every page:
//   - text measured on the device (the code block by default; everything
//     under ImTypesetAdvances_Device): drawn where it was laid out; one size
//     of step in a monospace face, on every row; rows agreeing at every
//     column; the pens Dear ImGui's own for the same string wherever ImGui's
//     advances for the face are whole framebuffer pixels; the selection's
//     fill, the carets and the hit tests on the glyphs;
//   - text measured by its font (everything under ImTypesetAdvances_Font):
//     every glyph less than two pixels from where it was laid out, however
//     long the word.
//
// EXACT means to the rounding of an advance to a scaled point, a 65,536th of
// a point: a pen is a whole framebuffer pixel, and where layout has the
// character is a sum of advances each rounded to a scaled point, so the two
// differ by up to half a scaled point a character before it on the row. At
// 96 units to the inch that is 0.000005 px a character; `EXACT_PX` is 1/256.
//
// IMTYPESET_DEVICE_ADVANCES_TABLE=1 prints the measurements, one row a page.
// IMTYPESET_DEVICE_ADVANCES_SHOTS=<directory> writes pictures there: the ruler
// and the long row drawn by Dear ImGui, on the device's advances and on the
// font's, one under another, at densities 1 and 2.

#include "golden.h"

#include "context.h"
#include "draw.h"
#include "imtypeset.h"
#include "support/soft_raster.h"
#include "tex_rules.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{

using namespace ImTypeset::detail;

constexpr int WIDTH = 1500;
constexpr int HEIGHT = 700;
constexpr ImU32 BLACK = IM_COL32(0, 0, 0, 255);
// See EXACT at the top.
constexpr double EXACT_PX = 1.0 / 256.0;

const char* const RULER = "1234567890123456789012345678901234567890";
const char* const LONG =
    "total = float(baked.get_char_advance(code)) + kern_px  # 40th is in 'kern'";
const char* const CODE_ROWS[] = {
    "name       size  kind",
    "alpha         1  file",
    "beta_long    42  dir",
    "iiii|llll|mmmm|WWWW|",
    "0000|1111|8888|....|",
    RULER,
    LONG,
    "end. Next: a; b? C! D, e. f",
};
constexpr int ROW_COUNT = static_cast<int>(sizeof CODE_ROWS / sizeof CODE_ROWS[0]);
constexpr int RULER_ROW = 5;
constexpr int LONG_ROW = 6;

enum class Faces { USUAL, MONO };
enum class SizeIs { PIXEL_HEIGHT, EM };

struct Page {
    Faces faces;
    SizeIs size_is;
    float size;
    float density;
    // ImTypesetAdvances_: how the page's text is measured.
    int advances;
};

const char* file_of(Faces faces, ImTypesetFace face)
{
    if (faces == Faces::MONO && face != ImTypesetFace_Math)
        return "Inconsolata-Medium.ttf";
    switch (face) {
    case ImTypesetFace_Regular:
        return "Roboto-Regular.ttf";
    case ImTypesetFace_Bold:
        return "Roboto-Bold.ttf";
    case ImTypesetFace_Italic:
        return "Roboto-RegularItalic.ttf";
    case ImTypesetFace_BoldItalic:
        return "Roboto-BoldItalic.ttf";
    case ImTypesetFace_Code:
        return "Inconsolata-Medium.ttf";
    default:
        return "latinmodern-math.otf";
    }
}

// An ImGui context on a display of one density with the library's faces in
// its atlas, and a window over the whole display.
struct Stage {
    Stage(Faces faces, float scale, const ImFontConfig* code_config = nullptr)
        : density(scale)
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.DisplaySize = ImVec2(static_cast<float>(WIDTH), static_cast<float>(HEIGHT));
        io.DisplayFramebufferScale = ImVec2(scale, scale);
        io.DeltaTime = 1.0f / 60.0f;
        io.Fonts->AddFontDefault();
        if (!IMTYPESET_CHECKVERSION())
            FAIL("IMTYPESET_CHECKVERSION: %s", ImTypeset::GetLastError()->Message);
        ctx = ImTypeset::CreateContext();
        if (ctx == nullptr) {
            FAIL("CreateContext: %s", ImTypeset::GetLastError()->Message);
            return;
        }
        for (const ImTypesetFace face :
             {ImTypesetFace_Regular, ImTypesetFace_Bold, ImTypesetFace_Italic,
              ImTypesetFace_BoldItalic, ImTypesetFace_Code, ImTypesetFace_Math}) {
            const std::string path =
                golden::data_path(std::string("fonts/") + file_of(faces, face));
            const ImFontConfig* config = face == ImTypesetFace_Code ? code_config : nullptr;
            if (ImTypeset::AddFontFromFileTTF(face, path.c_str(), 0, config) == nullptr)
                FAIL("%s: %s", path.c_str(), ImTypeset::GetLastError()->Message);
        }
    }
    ~Stage()
    {
        ImTypeset::DestroyContext();
        ImGui::DestroyContext();
    }
    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;

    void begin()
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, BLACK);
        ImGui::Begin("device advances", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    }

    void end()
    {
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        ImGui::Render();
    }

    float density;
    ImTypesetContext* ctx = nullptr;
};

ImTypesetStyle style_of(const Page& page)
{
    ImTypesetStyle style;
    if (page.size_is == SizeIs::EM)
        style.EmSize = page.size;
    else
        style.FontSize = page.size;
    style.Align = ImTypesetAlign_Left;
    style.Spacing = ImTypesetSpacing_French;
#ifndef REPRODUCTION_AT_HEAD
    style.Advances = page.advances;
#endif
    return style;
}

// The atlas entry the library draws a face from on a page, asked of the one
// function the draw loop asks (src/draw.h, `face_bake`): at the page's em, in
// its units to the inch, on a grid of its density.
ImFontBaked* library_bake(Stage& stage, const Page& page, ImTypesetFace face)
{
    ImFont* font = ImTypeset::GetFont(face);
    if (font == nullptr || stage.ctx == nullptr)
        return nullptr;
    const ResolvedStyle resolved = resolve_style(*stage.ctx, style_of(page));
    const Face role = static_cast<Face>(face - 1);
    const FontMetrics* ours = stage.ctx->library.metrics(role, static_cast<double>(resolved.em));
    return face_bake(font, ours, resolved.em, resolved.px_per_inch,
                     static_cast<double>(page.density));
}

struct Pen {
    float x = 0.0f; // the pen, in ImGui's units
    float y = 0.0f; // the top of the quad
    bool found = false;
};

// The pens of the characters of `text` that have ink, from the textured quads
// [first, end) of a draw list in order: each quad's left edge less the left
// offset of its character's bitmap in `baked`, which is `scale` units to the
// texel. A quad whose texture coordinates are not that character's in that
// entry is a failure: the entry is not the one drawn from.
std::vector<Pen> pens_of(const ImDrawList* list, int first, int end, const std::string& text,
                         ImFontBaked* baked, float scale)
{
    std::vector<Pen> pens(text.size());
    int v = first;
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == ' ' || c == '\n')
            continue;
        const ImFontGlyph* glyph = baked->FindGlyph(static_cast<ImWchar>(c));
        if (glyph == nullptr || !glyph->Visible)
            continue;
        // Rectangles (a selection's fill) carry the white texel on every corner.
        while (v + 3 < end && list->VtxBuffer[v].uv.x == list->VtxBuffer[v + 2].uv.x)
            v += 4;
        if (!(v + 3 < end)) {
            FAIL("no quad for character %d ('%c') of \"%s\"", static_cast<int>(i), c, text.c_str());
            return pens;
        }
        const ImDrawVert& a = list->VtxBuffer[v];
        const ImDrawVert& b = list->VtxBuffer[v + 2];
        if (a.uv.x != glyph->U0 || a.uv.y != glyph->V0 || b.uv.x != glyph->U1 ||
            b.uv.y != glyph->V1) {
            FAIL("the quad for character %d ('%c') is not textured from the %g px entry",
                 static_cast<int>(i), c, static_cast<double>(baked->Size));
            return pens;
        }
        pens[i].x = a.pos.x - glyph->X0 * scale;
        pens[i].y = a.pos.y;
        pens[i].found = true;
        v += 4;
    }
    return pens;
}

// The steps between the pens of one row, as "7 x15, 8 x24": each run of equal
// steps in order. Steps across a space are divided among the characters.
std::string steps_of(const std::vector<Pen>& pens, size_t begin, size_t end, double density,
                     int* distinct)
{
    std::string out;
    std::vector<double> seen;
    double last = -1.0;
    int run = 0;
    const auto flush = [&]() {
        if (run == 0)
            return;
        char text[48];
        std::snprintf(text, sizeof text, "%s%g x%d", out.empty() ? "" : ", ", last, run);
        out += text;
    };
    size_t prev = begin;
    while (prev < end && !pens[prev].found)
        ++prev;
    for (size_t i = prev + 1; i < end; ++i) {
        if (!pens[i].found)
            continue;
        const double step =
            static_cast<double>(pens[i].x - pens[prev].x) * density / static_cast<double>(i - prev);
        if (std::find(seen.begin(), seen.end(), step) == seen.end())
            seen.push_back(step);
        if (step != last) {
            flush();
            last = step;
            run = 0;
        }
        ++run;
        prev = i;
    }
    flush();
    *distinct = static_cast<int>(seen.size());
    return out;
}

struct Measured {
    bool ok = false;
    // The code block. Lengths are in FRAMEBUFFER pixels unless they say units.
    double laid_advance = 0.0;  // the ruler's second rectangle less its first
    double imgui_advance = 0.0; // ImGui's own advance for '1' in the entry drawn from
    std::string ruler_steps;
    int distinct_steps = 0;      // over every row of the block
    double fortieth_drawn = 0.0; // the ruler's 40th character from its first's place
    double fortieth_laid = 0.0;
    double fortieth_imgui = 0.0;
    double rows_apart = 0.0; // the most two rows differ by at one column, drawn
    double glyph_off = 0.0;  // the most a pen is from its rectangle's left edge
    double glyph_off_signed = 0.0;
    double from_imgui = 0.0; // the most a pen is from ImGui's for the same character
    bool imgui_comparable = false;
    double caret_off = 0.0;  // the most a caret is from the pen of its character
    double fill_left = 0.0;  // the selection of characters 21 to 40 of the long row:
    double fill_right = 0.0; // its fill's edges less the pens of the 21st and the 41st
    int hits_wrong = 0;      // of two points in every drawn cell
    int hits = 0;
    int fractional_pens = 0; // pens not on a framebuffer pixel
    // The most a pen is from its row's first pen plus as many of the block's
    // first step as it has characters before it: zero when N characters take
    // N times one length on every row.
    double grid_off = 0.0;
    double step = 0.0; // the first step drawn in the block
    // The ruler as a paragraph in the body face.
    double prose_off = 0.0;
    double prose_off_signed = 0.0;
    std::string prose_steps;
    int prose_hits_wrong = 0;
    int prose_hits = 0;
    double prose_from_imgui = 0.0;
    bool prose_comparable = false;
    int glyphs = 0;
};

std::string code_text()
{
    std::string text;
    for (int r = 0; r < ROW_COUNT; ++r) {
        if (r != 0)
            text += '\n';
        text += CODE_ROWS[r];
    }
    return text;
}

ImTypesetDocument* document_of(const std::string& text, ImTypesetBlockKind kind, float width,
                               const ImTypesetStyle& style)
{
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    if (document == nullptr)
        return nullptr;
    ImTypesetBlock block;
    block.Kind = kind;
    block.TextStart = 0;
    block.TextEnd = static_cast<int>(text.size());
    ImTypesetDocumentContent content;
    content.Text = text.data();
    content.TextSize = static_cast<int>(text.size());
    content.Blocks = &block;
    content.BlockCount = 1;
    if (!ImTypeset::SetDocumentBlocks(document, &content))
        FAIL("SetDocumentBlocks: %s", ImTypeset::GetLastError()->Message);
    if (!ImTypeset::LayoutDocument(document, width, &style))
        FAIL("LayoutDocument: %s", ImTypeset::GetLastError()->Message);
    return document;
}

// The left edge of the rectangle the library answers for one character.
bool rect_of(ImTypesetDocument* document, int offset, ImVec4* out)
{
    ImTypesetDocumentRange range;
    range.Start.Block = 0;
    range.Start.Offset = offset;
    range.End.Block = 0;
    range.End.Offset = offset + 1;
    ImVector<ImVec4> rects;
    if (ImTypeset::GetDocumentRangeRects(document, range, &rects) < 1)
        return false;
    *out = rects[0];
    return true;
}

// ImGui's own pens for `text` in `font` at `size`, by drawing it: the text is
// added to the window's draw list at `pos` with the font pushed, as a host
// draws a line of its own beside typeset text, and the pens read back.
std::vector<Pen> imgui_pens(ImFont* font, float size, const ImVec2& pos, const std::string& text,
                            double* advance)
{
    ImGui::PushFont(font, size);
    ImDrawList* list = ImGui::GetWindowDrawList();
    const int first = list->VtxBuffer.Size;
    list->AddText(pos, BLACK, text.data(), text.data() + text.size());
    ImFontBaked* baked = ImGui::GetFontBaked();
    const float scale = ImGui::GetFontSize() / baked->Size;
    std::vector<Pen> pens = pens_of(list, first, list->VtxBuffer.Size, text, baked, scale);
    if (advance != nullptr)
        *advance = static_cast<double>(baked->GetCharAdvance(static_cast<ImWchar>('1'))) *
                   static_cast<double>(scale) *
                   static_cast<double>(ImGui::GetIO().DisplayFramebufferScale.x);
    ImGui::PopFont();
    return pens;
}

// Two points in the drawn cell of each character with ink, a quarter and
// three quarters of the way across it, asked of the hit test: the caret
// position nearest the first is the character's own and nearest the second
// the next one's.
void hit_cells(ImTypesetDocument* document, const ImVec2& pos, const std::vector<Pen>& pens,
               size_t begin, size_t end, int* wrong, int* count)
{
    for (size_t i = begin; i + 1 < end; ++i) {
        if (!pens[i].found || !pens[i + 1].found)
            continue;
        ImVec4 rect;
        if (!rect_of(document, static_cast<int>(i), &rect))
            continue;
        const float y = (rect.y + rect.w) * 0.5f;
        const float cell = pens[i + 1].x - pens[i].x;
        for (int quarter = 1; quarter <= 3; quarter += 2) {
            const float x = pens[i].x + cell * 0.25f * static_cast<float>(quarter) - pos.x;
            const ImTypesetDocumentHit hit = ImTypeset::HitTestDocument(document, ImVec2(x, y));
            const int expected = static_cast<int>(i) + (quarter == 3 ? 1 : 0);
            ++*count;
            if (hit.Position.Offset != expected)
                ++*wrong;
        }
    }
}

Measured measure(Stage& stage, const Page& page, const ImVec2& pos = ImVec2(20.0f, 20.0f))
{
    Measured m;
    const double density = static_cast<double>(page.density);
    const ImTypesetStyle style = style_of(page);
    const std::string code = code_text();
    const std::string ruler = RULER;
    ImTypesetDocument* block = document_of(code, ImTypesetBlockKind_Code, 1400.0f, style);
    ImTypesetDocument* prose = document_of(ruler, ImTypesetBlockKind_Paragraph, 1400.0f, style);
    if (block == nullptr || prose == nullptr)
        return m;
    const ImVec2 prose_pos(pos.x, pos.y + 360.0f);
    const ImVec2 imgui_pos(pos.x, pos.y + 440.0f);

    // The selection the library draws: characters 21 to 40 of the long row.
    size_t long_start = 0;
    for (int r = 0; r < LONG_ROW; ++r)
        long_start += std::strlen(CODE_ROWS[r]) + 1;
    size_t ruler_start = 0;
    for (int r = 0; r < RULER_ROW; ++r)
        ruler_start += std::strlen(CODE_ROWS[r]) + 1;
    ImTypesetDocumentDraw selected;
    selected.Selection.Start.Offset = static_cast<int>(long_start) + 20;
    selected.Selection.End.Offset = static_cast<int>(long_start) + 40;

    std::vector<Pen> pens, prose_pens, own_ruler, own_long, own_prose;
    double fill_x0 = 0.0, fill_x1 = 0.0;
    bool own_code_same = false, own_prose_same = false;
    // Twice, so that the frame read adds nothing to the atlas and every
    // texture coordinate in it is one the entries still hold.
    for (int frame = 0; frame < 2; ++frame) {
        stage.begin();
        ImDrawList* list = ImGui::GetWindowDrawList();
        const int v0 = list->VtxBuffer.Size;
        if (!ImTypeset::DrawDocument(block, list, pos, &selected))
            FAIL("DrawDocument: %s", ImTypeset::GetLastError()->Message);
        const int v1 = list->VtxBuffer.Size;
        if (!ImTypeset::DrawDocument(prose, list, prose_pos))
            FAIL("DrawDocument: %s", ImTypeset::GetLastError()->Message);
        const int v2 = list->VtxBuffer.Size;

        ImFontBaked* code_bake = library_bake(stage, page, ImTypesetFace_Code);
        ImFontBaked* prose_bake = library_bake(stage, page, ImTypesetFace_Regular);
        if (code_bake == nullptr || prose_bake == nullptr) {
            FAIL("a face has no atlas entry");
        } else if (frame == 1) {
            const float scale = 1.0f / page.density;
            pens = pens_of(list, v0, v1, code, code_bake, scale);
            prose_pens = pens_of(list, v1, v2, ruler, prose_bake, scale);
            // The fill: the one untextured quad among the block's vertices.
            for (int v = v0; v + 3 < v1; v += 4) {
                if (list->VtxBuffer[v].uv.x == list->VtxBuffer[v + 2].uv.x) {
                    fill_x0 = static_cast<double>(list->VtxBuffer[v].pos.x);
                    fill_x1 = static_cast<double>(list->VtxBuffer[v + 2].pos.x);
                    break;
                }
            }
            // ImGui's own text in the same fonts at the sizes the library drew
            // at, where ImGui has such a size: it draws whole sizes only.
            const float code_size = code_bake->Size / page.density;
            const float prose_size = prose_bake->Size / page.density;
            own_code_same = code_size == std::floor(code_size);
            own_prose_same = prose_size == std::floor(prose_size);
            ImFont* code_font = ImTypeset::GetFont(ImTypesetFace_Code);
            ImFont* prose_font = ImTypeset::GetFont(ImTypesetFace_Regular);
            if (own_code_same) {
                own_ruler = imgui_pens(code_font, code_size, imgui_pos, ruler, &m.imgui_advance);
                own_long = imgui_pens(code_font, code_size,
                                      ImVec2(imgui_pos.x, imgui_pos.y + 40.0f), LONG, nullptr);
            }
            if (own_prose_same)
                own_prose = imgui_pens(prose_font, prose_size,
                                       ImVec2(imgui_pos.x, imgui_pos.y + 80.0f), ruler, nullptr);
        }
        stage.end();
    }
    if (pens.empty() || prose_pens.empty())
        return m;

    // The code block, row by row.
    size_t row_start = 0;
    std::vector<double> column_min, column_max;
    for (int r = 0; r < ROW_COUNT; ++r) {
        const size_t length = std::strlen(CODE_ROWS[r]);
        ImVec4 first_rect;
        if (!rect_of(block, static_cast<int>(row_start), &first_rect)) {
            FAIL("no rectangle for the first character of row %d", r);
            return m;
        }
        for (size_t c = 0; c < length; ++c) {
            const size_t i = row_start + c;
            if (!pens[i].found)
                continue;
            ++m.glyphs;
            const double pen = static_cast<double>(pens[i].x);
            const double device = pen * density;
            if (std::fabs(device - std::floor(device + 0.5)) > 1.0 / 64.0)
                ++m.fractional_pens;
            ImVec4 rect;
            if (!rect_of(block, static_cast<int>(i), &rect)) {
                FAIL("no rectangle for character %d of row %d", static_cast<int>(c), r);
                return m;
            }
            const double off = (pen - static_cast<double>(pos.x + rect.x)) * density;
            if (std::fabs(off) > std::fabs(m.glyph_off_signed))
                m.glyph_off_signed = off;
            const ImTypesetPosition at = {0, static_cast<int>(i)};
            const ImVec4 caret = ImTypeset::GetDocumentCaretRect(block, at);
            m.caret_off = std::max(m.caret_off,
                                   std::fabs(pen - static_cast<double>(pos.x + caret.x)) * density);
            if (column_min.size() <= c) {
                column_min.resize(c + 1, 1.0e30);
                column_max.resize(c + 1, -1.0e30);
            }
            column_min[c] = std::min(column_min[c], pen);
            column_max[c] = std::max(column_max[c], pen);
        }
        hit_cells(block, pos, pens, row_start, row_start + length, &m.hits_wrong, &m.hits);
        if (r == RULER_ROW) {
            ImVec4 second;
            if (rect_of(block, static_cast<int>(row_start) + 1, &second))
                m.laid_advance = static_cast<double>(second.x - first_rect.x) * density;
            ImVec4 fortieth;
            if (rect_of(block, static_cast<int>(row_start) + 39, &fortieth))
                m.fortieth_laid = static_cast<double>(fortieth.x - first_rect.x) * density;
            m.fortieth_drawn = (static_cast<double>(pens[row_start + 39].x) -
                                static_cast<double>(pos.x + first_rect.x)) *
                               density;
            int distinct = 0;
            m.ruler_steps = steps_of(pens, row_start, row_start + length, density, &distinct);
            if (own_code_same && own_ruler.size() == length) {
                m.fortieth_imgui = static_cast<double>(own_ruler[39].x - own_ruler[0].x) * density;
                for (size_t c = 0; c < length; ++c)
                    m.from_imgui = std::max(
                        m.from_imgui,
                        std::fabs(static_cast<double>(pens[row_start + c].x - pens[row_start].x) -
                                  static_cast<double>(own_ruler[c].x - own_ruler[0].x)) *
                            density);
            }
        }
        if (r == LONG_ROW) {
            m.fill_left = (fill_x0 - static_cast<double>(pens[row_start + 20].x)) * density;
            m.fill_right = (fill_x1 - static_cast<double>(pens[row_start + 40].x)) * density;
            if (own_code_same && own_long.size() == length) {
                // From the row's first character with ink, which is its first.
                for (size_t c = 0; c < length; ++c) {
                    if (!pens[row_start + c].found || !own_long[c].found)
                        continue;
                    m.from_imgui = std::max(
                        m.from_imgui,
                        std::fabs(static_cast<double>(pens[row_start + c].x - pens[row_start].x) -
                                  static_cast<double>(own_long[c].x - own_long[0].x)) *
                            density);
                }
            }
        }
        row_start += length + 1;
    }
    m.imgui_comparable = own_code_same;
    m.prose_comparable = own_prose_same;
    m.glyph_off = std::fabs(m.glyph_off_signed);
    for (size_t c = 0; c < column_min.size(); ++c)
        if (column_max[c] >= column_min[c])
            m.rows_apart = std::max(m.rows_apart, (column_max[c] - column_min[c]) * density);
    // Every row's steps together: how many sizes of step the block has.
    {
        std::vector<double> seen;
        size_t start = 0;
        for (int r = 0; r < ROW_COUNT; ++r) {
            const size_t length = std::strlen(CODE_ROWS[r]);
            size_t prev = start;
            for (size_t i = start + 1; i < start + length; ++i) {
                if (!pens[i].found)
                    continue;
                if (pens[prev].found) {
                    const double step = static_cast<double>(pens[i].x - pens[prev].x) * density /
                                        static_cast<double>(i - prev);
                    if (std::find(seen.begin(), seen.end(), step) == seen.end())
                        seen.push_back(step);
                }
                prev = i;
            }
            start += length + 1;
        }
        m.distinct_steps = static_cast<int>(seen.size());
        const double cell = seen.empty() ? 0.0 : seen.front();
        m.step = cell;
        start = 0;
        for (int r = 0; r < ROW_COUNT; ++r) {
            const size_t length = std::strlen(CODE_ROWS[r]);
            for (size_t i = start; i < start + length; ++i) {
                if (!pens[i].found || !pens[start].found)
                    continue;
                const double along = static_cast<double>(pens[i].x - pens[start].x) * density;
                m.grid_off =
                    std::max(m.grid_off, std::fabs(along - cell * static_cast<double>(i - start)));
            }
            start += length + 1;
        }
    }

    // The ruler as a paragraph.
    for (size_t i = 0; i < ruler.size(); ++i) {
        ImVec4 rect;
        if (!prose_pens[i].found || !rect_of(prose, static_cast<int>(i), &rect))
            continue;
        const double off =
            (static_cast<double>(prose_pens[i].x) - static_cast<double>(prose_pos.x + rect.x)) *
            density;
        if (std::fabs(off) > std::fabs(m.prose_off_signed))
            m.prose_off_signed = off;
        if (own_prose_same && own_prose.size() == ruler.size())
            m.prose_from_imgui =
                std::max(m.prose_from_imgui,
                         std::fabs(static_cast<double>(prose_pens[i].x - prose_pens[0].x) -
                                   static_cast<double>(own_prose[i].x - own_prose[0].x)) *
                             density);
    }
    m.prose_off = std::fabs(m.prose_off_signed);
    int distinct = 0;
    m.prose_steps = steps_of(prose_pens, 0, ruler.size(), density, &distinct);
    hit_cells(prose, prose_pos, prose_pens, 0, ruler.size(), &m.prose_hits_wrong, &m.prose_hits);

    ImTypeset::DestroyDocument(block);
    ImTypeset::DestroyDocument(prose);
    m.ok = true;
    return m;
}

const char* loader_name()
{
#ifdef IMGUI_ENABLE_FREETYPE
    return "FreeType";
#else
    return "stb_truetype";
#endif
}

// Whether the loader this build rasterises with bakes whole-pixel advances of
// itself: FreeType with its default hinting does, stb_truetype scales the
// font's own.
constexpr bool LOADER_HINTS =
#ifdef IMGUI_ENABLE_FREETYPE
    true;
#else
    false;
#endif

const float SIZES[] = {14.0f, 15.0f, 16.0f, 17.0f, 18.0f, 19.0f, 20.0f};
const float DENSITIES[] = {1.0f, 2.0f};

void print_row(const Page& page, const Measured& m)
{
    std::printf("%-5s %-2s %2g d%g  adv %-8.4f imgui %-7.4f steps %d: %-18.18s 40th %g / %.2f / "
                "%-6s rows %-4.3g glyph %+-6.2f imgui %-5s caret %-5.2f fill %+.0f/%+.0f hits "
                "%d/%d frac %d | prose %+-5.2f hits %d/%d %.30s\n",
                page.faces == Faces::MONO ? "mono" : "usual",
                page.size_is == SizeIs::EM ? "em" : "px", static_cast<double>(page.size),
                static_cast<double>(page.density), m.laid_advance, m.imgui_advance,
                m.distinct_steps, m.ruler_steps.c_str(), m.fortieth_drawn, m.fortieth_laid,
                m.imgui_comparable ? std::to_string(m.fortieth_imgui).substr(0, 6).c_str() : "n/a",
                m.rows_apart, m.glyph_off_signed,
                m.imgui_comparable ? std::to_string(m.from_imgui).substr(0, 5).c_str() : "n/a",
                m.caret_off, m.fill_left, m.fill_right, m.hits_wrong, m.hits, m.fractional_pens,
                m.prose_off_signed, m.prose_hits_wrong, m.prose_hits, m.prose_steps.c_str());
}

bool table_asked()
{
    const char* asked = std::getenv("IMTYPESET_DEVICE_ADVANCES_TABLE");
    return asked != nullptr && asked[0] != '\0' && asked[0] != '0';
}

// Every page of one way of measuring: both densities, both face tables, an
// em and a pixel height of 14 to 20. `check` is handed each page measured.
template<class Check>
void every_page(int advances, const ImFontConfig* code_config, Check check)
{
    for (const float density : DENSITIES) {
        for (const Faces faces : {Faces::MONO, Faces::USUAL}) {
            Stage stage(faces, density, code_config);
            REQUIRE(stage.ctx != nullptr);
            for (const SizeIs size_is : {SizeIs::EM, SizeIs::PIXEL_HEIGHT}) {
                for (const float size : SIZES) {
                    const Page page = {faces, size_is, size, density, advances};
                    CHECK_CONTEXT(
                        "%s faces, %s %g, density %g, %s", faces == Faces::MONO ? "mono" : "usual",
                        size_is == SizeIs::EM ? "em" : "pixel height", static_cast<double>(size),
                        static_cast<double>(density), loader_name());
                    const Measured m = measure(stage, page);
                    CHECK(m.ok);
                    if (m.ok)
                        check(page, m);
                }
            }
        }
    }
}

// What holds of a block of code measured on the device, on any page.
void check_code_on_the_grid(const Measured& m)
{
    CHECK_EQ(m.glyphs, 200);
    // Drawn where it was laid out.
    CHECK(m.glyph_off < EXACT_PX);
    // One size of step, a whole number of framebuffer pixels, on every row:
    // N characters take N times it.
    CHECK_EQ(m.distinct_steps, 1);
    CHECK_EQ(m.grid_off, 0.0);
    CHECK_EQ(m.fractional_pens, 0);
    CHECK_EQ(m.step, std::floor(m.step));
    CHECK(std::fabs(m.laid_advance - m.step) < EXACT_PX);
    // Rows agree at every column.
    CHECK_EQ(m.rows_apart, 0.0);
    // A caret is on the pen of its character, a selection's fill starts on
    // the pen of its first character and ends on the pen of the one after
    // its last, and a point in a drawn cell is that character's.
    CHECK(m.caret_off < EXACT_PX);
    CHECK_EQ(m.fill_left, 0.0);
    CHECK_EQ(m.fill_right, 0.0);
    CHECK_EQ(m.hits, 340);
    CHECK_EQ(m.hits_wrong, 0);
}

} // namespace

// The measurements, printed: what the requests' own tables hold, from this
// tree, under this build's loader. Asserts nothing but that every page could
// be measured.
TEST(the_table_of_measurements)
{
    if (!table_asked())
        return;
    for (const int advances :
         {ImTypesetAdvances_Font, ImTypesetAdvances_Default, ImTypesetAdvances_Device}) {
        std::printf("--- %s, ImTypesetAdvances_%s; lengths in framebuffer pixels ---\n",
                    loader_name(),
                    advances == ImTypesetAdvances_Font
                        ? "Font"
                        : (advances == ImTypesetAdvances_Device ? "Device" : "Default"));
        every_page(advances, nullptr,
                   [](const Page& page, const Measured& m) { print_row(page, m); });
    }
}

TEST(code_is_drawn_where_it_was_laid_out_on_one_grid_and_on_dear_imguis_own_pens)
{
    int compared = 0;
    int incomparable = 0;
    every_page(ImTypesetAdvances_Default, nullptr, [&](const Page& page, const Measured& m) {
        check_code_on_the_grid(m);
        // The 40th character of the ruler, drawn and laid out.
        CHECK_EQ(m.fortieth_drawn, 39.0 * m.step);
        CHECK(std::fabs(m.fortieth_laid - m.fortieth_drawn) < EXACT_PX);
        // Dear ImGui draws whole sizes only: at density 2 a face whose size
        // in framebuffer pixels is odd has no line of ImGui's to stand beside.
        if (!m.imgui_comparable) {
            ++incomparable;
            CHECK(page.density != 1.0f);
            return;
        }
        ++compared;
        if (LOADER_HINTS) {
            // The same string in the same ImFont at the same size: the same
            // pens, for the ruler and for the row of seventy-four characters.
            CHECK_EQ(m.imgui_advance, m.step);
            CHECK_EQ(m.from_imgui, 0.0);
            CHECK_EQ(m.fortieth_imgui, m.fortieth_drawn);
        } else {
            // Under stb_truetype ImGui's advance is the font's own, scaled,
            // and its pens are at fractions of a pixel: the device's advance
            // is that one rounded to the pixel, half up. It is a function of
            // the font and the size ImGui has, which at a density above 1 is
            // the size in units where the library's is the size in
            // framebuffer pixels: at density 1 the two are one entry.
            if (page.density == 1.0f)
                CHECK_EQ(m.step, std::floor(m.imgui_advance + 0.5));
        }
    });
    // Not vacuous: every page of density 1 has a line of ImGui's beside it.
    CHECK(compared >= 28);
    std::printf("    %s: %d pages compared with Dear ImGui's own text, %d at density 2 whose size "
                "ImGui cannot draw\n",
                loader_name(), compared, incomparable);
}

TEST(with_pixel_snap_dear_imguis_pens_are_the_grid_under_any_loader_at_density_1)
{
    // ImFontConfig::PixelSnapH rounds the advance ImGui bakes as the library
    // rounds it, in ImGui's units: the two are one number at density 1,
    // whichever loader made the entry.
    ImFontConfig config;
    config.PixelSnapH = true;
    int compared = 0;
    for (const Faces faces : {Faces::MONO, Faces::USUAL}) {
        Stage stage(faces, 1.0f, &config);
        REQUIRE(stage.ctx != nullptr);
        for (const SizeIs size_is : {SizeIs::EM, SizeIs::PIXEL_HEIGHT}) {
            for (const float size : SIZES) {
                const Page page = {faces, size_is, size, 1.0f, ImTypesetAdvances_Default};
                CHECK_CONTEXT("%s %g", size_is == SizeIs::EM ? "em" : "pixel height",
                              static_cast<double>(size));
                const Measured m = measure(stage, page);
                REQUIRE(m.ok);
                check_code_on_the_grid(m);
                REQUIRE(m.imgui_comparable);
                CHECK_EQ(m.imgui_advance, m.step);
                CHECK_EQ(m.from_imgui, 0.0);
                ++compared;
            }
        }
    }
    CHECK_EQ(compared, 28);

    // At a density above 1 the flag rounds ImGui's advance to a whole UNIT,
    // which is not the framebuffer's grid: where the entry's advance is an
    // odd number of framebuffer pixels ImGui's own text leaves the library's
    // pens, under a loader that had it right without the flag. It is why the
    // library does not set the flag on the fonts it adds.
    if (!LOADER_HINTS)
        return;
    int odd = 0, even = 0;
    Stage dense(Faces::MONO, 2.0f, &config);
    REQUIRE(dense.ctx != nullptr);
    for (const float size : SIZES) {
        const Page page = {Faces::MONO, SizeIs::PIXEL_HEIGHT, size, 2.0f,
                           ImTypesetAdvances_Default};
        CHECK_CONTEXT("density 2, pixel height %g", static_cast<double>(size));
        const Measured m = measure(dense, page);
        REQUIRE(m.ok);
        check_code_on_the_grid(m);
        REQUIRE(m.imgui_comparable);
        if (std::fmod(m.step, 2.0) != 0.0) {
            ++odd;
            CHECK(m.from_imgui > 1.0);
        } else {
            ++even;
            CHECK_EQ(m.from_imgui, 0.0);
        }
    }
    CHECK(odd > 0);
    CHECK(even > 0);
}

TEST(prose_measured_on_the_device_is_drawn_where_it_was_laid_out_too)
{
    every_page(ImTypesetAdvances_Device, nullptr, [&](const Page& page, const Measured& m) {
        check_code_on_the_grid(m);
        // The number of forty digits in the body face: one word.
        CHECK(m.prose_off < EXACT_PX);
        CHECK_EQ(m.prose_hits, 78);
        CHECK_EQ(m.prose_hits_wrong, 0);
        if (LOADER_HINTS && m.prose_comparable)
            CHECK_EQ(m.prose_from_imgui, 0.0);
        (void)page;
    });
}

TEST(text_measured_by_its_font_is_drawn_less_than_two_pixels_from_where_it_was_laid_out)
{
    // The bound DrawRules::word_glyphs_held gives with word_spaces_led: a
    // word's first glyph less than a pixel from its place and every other
    // within a pixel of its laid-out distance from the first. A row of a code
    // block is one word, and so is a number of forty digits.
    double worst_code = 0.0, worst_prose = 0.0, worst_rows = 0.0, worst_fill = 0.0;
    int wrong = 0, asked = 0;
    every_page(ImTypesetAdvances_Font, nullptr, [&](const Page&, const Measured& m) {
        CHECK(m.glyph_off < 2.0);
        CHECK(m.prose_off < 2.0);
        CHECK(m.caret_off < 2.0);
        CHECK_EQ(m.fractional_pens, 0);
        // Two rows that start at one x: each glyph within a pixel of its
        // distance from the first, so two of them within two of each other.
        CHECK(m.rows_apart <= 2.0);
        CHECK(std::fabs(m.fill_left) < 2.5);
        CHECK(std::fabs(m.fill_right) < 2.5);
        worst_code = std::max(worst_code, m.glyph_off);
        worst_prose = std::max(worst_prose, m.prose_off);
        worst_rows = std::max(worst_rows, m.rows_apart);
        worst_fill =
            std::max(worst_fill, std::max(std::fabs(m.fill_left), std::fabs(m.fill_right)));
        wrong += m.hits_wrong + m.prose_hits_wrong;
        asked += m.hits + m.prose_hits;
    });
    // What was measured, which is inside the bound: a row starts on a pixel
    // here, so only the hold within the word shows.
    CHECK(worst_code <= WORD_GLYPH_BOUND_PX + EXACT_PX);
    CHECK(worst_prose <= WORD_GLYPH_BOUND_PX + EXACT_PX);
    std::printf("    %s, font advances: a glyph of a code row at most %.2f px from its rectangle, "
                "of the 40-digit number %.2f; two rows at most %.0f px apart at a column; a "
                "selection's edge at most %.0f px from the glyph's pen; %d of %d points in drawn "
                "cells hit another character\n",
                loader_name(), worst_code, worst_prose, worst_rows, worst_fill, wrong, asked);
}

// --- A layout measured on a device is for that device -----------------------------------

namespace
{

// A document of `pairs` paragraphs each followed by a block of three rows of
// code, laid out `width` wide under `style`.
ImTypesetDocument* long_document(int pairs, float width, const ImTypesetStyle& style,
                                 const ImTypesetBlockStyle* block_style = nullptr)
{
    static const char* const PROSE =
        "Typography of ordinary running text depends on the rhythm of its word spaces. ";
    static const char* const CODE =
        "name       size  kind\nalpha         1  file\nbeta_long    42  dir";
    std::string text;
    std::vector<ImTypesetBlock> blocks;
    for (int i = 0; i < pairs; ++i) {
        ImTypesetBlock block;
        block.Kind = ImTypesetBlockKind_Paragraph;
        block.TextStart = static_cast<int>(text.size());
        text += PROSE;
        block.TextEnd = static_cast<int>(text.size());
        blocks.push_back(block);
        block.Kind = ImTypesetBlockKind_Code;
        block.TextStart = static_cast<int>(text.size());
        text += CODE;
        block.TextEnd = static_cast<int>(text.size());
        blocks.push_back(block);
    }
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    if (document == nullptr)
        return nullptr;
    ImTypesetDocumentContent content;
    content.Text = text.data();
    content.TextSize = static_cast<int>(text.size());
    content.Blocks = blocks.data();
    content.BlockCount = static_cast<int>(blocks.size());
    if (block_style != nullptr) {
        content.Styles = block_style;
        content.StyleCount = 1;
    }
    if (!ImTypeset::SetDocumentBlocks(document, &content))
        FAIL("SetDocumentBlocks: %s", ImTypeset::GetLastError()->Message);
    if (!ImTypeset::LayoutDocument(document, width, &style))
        FAIL("LayoutDocument: %s", ImTypeset::GetLastError()->Message);
    return document;
}

// One frame that draws the document at the window's top-left corner.
void draw_frame(Stage& stage, ImTypesetDocument* document)
{
    stage.begin();
    if (!ImTypeset::DrawDocument(document, ImGui::GetWindowDrawList(), ImVec2(20.0f, 20.0f)))
        FAIL("DrawDocument: %s", ImTypeset::GetLastError()->Message);
    stage.end();
}

// How far apart layout has the first two characters of block 1, the first
// block of code, in pixels.
double code_step(ImTypesetDocument* document)
{
    ImVector<ImVec4> a, b;
    const ImTypesetDocumentRange first = {{1, 0}, {1, 1}};
    const ImTypesetDocumentRange second = {{1, 1}, {1, 2}};
    if (ImTypeset::GetDocumentRangeRects(document, first, &a) < 1 ||
        ImTypeset::GetDocumentRangeRects(document, second, &b) < 1)
        return 0.0;
    return static_cast<double>(b[0].x - a[0].x);
}

void set_density(float density)
{
    ImGui::GetIO().DisplayFramebufferScale = ImVec2(density, density);
}

} // namespace

TEST(a_document_is_laid_out_again_for_another_display_and_only_where_it_is_shown)
{
    Stage stage(Faces::USUAL, 1.0f);
    REQUIRE(stage.ctx != nullptr);
    ImTypesetStyle style;
    style.FontSize = 16.0f;
    style.Align = ImTypesetAlign_Left;
    const int pairs = 1000;
    const ImU64 blocks = 2 * static_cast<ImU64>(pairs);
    ImTypesetDocument* document = long_document(pairs, 600.0f, style);
    REQUIRE(document != nullptr);

    draw_frame(stage, document);
    const ImTypesetDocumentStats first = ImTypeset::GetDocumentStats(document);
    // Lazy: what the window shows, of two thousand blocks.
    CHECK(first.BlocksLaidOut > 4 && first.BlocksLaidOut < 60);
    // What describing every block once costs: two steps a block, with no
    // line broken.
    CHECK_EQ(first.StructureVisits, 2 * blocks);
    const float unit_step = ImTypeset::GetDeviceGlyphAdvance(ImTypesetFace_Code, 'n', &style);
    CHECK(unit_step > 0.0f);
    CHECK(std::fabs(code_step(document) - static_cast<double>(unit_step)) < EXACT_PX);
    CHECK(std::fabs(static_cast<double>(unit_step) -
                    std::floor(static_cast<double>(unit_step) + 0.5)) < EXACT_PX);

    // A frame in which nothing changed lays nothing out.
    draw_frame(stage, document);
    ImTypesetDocumentStats now = ImTypeset::GetDocumentStats(document);
    CHECK_EQ(now.BlocksLaidOut, first.BlocksLaidOut);
    CHECK_EQ(now.StructureVisits, first.StructureVisits);
    CHECK_EQ(now.MaterialBuilds, first.MaterialBuilds);

    // The window moves to a display of density 2, and the host calls nothing
    // but what it calls every frame: the draw sees it. Every block is gone
    // over once, without a line broken, and the blocks shown are laid out
    // for the new display; the rest wait until they are looked at.
    set_density(2.0f);
    draw_frame(stage, document);
    const ImTypesetDocumentStats dense = ImTypeset::GetDocumentStats(document);
    CHECK_EQ(dense.StructureVisits - first.StructureVisits, 2 * blocks);
    CHECK_EQ(dense.BlocksLaidOut - first.BlocksLaidOut, first.BlocksLaidOut);
    CHECK_EQ(dense.BlocksDeferred, static_cast<int>(blocks - first.BlocksLaidOut));
    // On the new display's grid: a whole number of framebuffer pixels, which
    // is what the atlas has there and not twice what it had.
    const float dense_step = ImTypeset::GetDeviceGlyphAdvance(ImTypesetFace_Code, 'n', &style);
    CHECK(std::fabs(code_step(document) - static_cast<double>(dense_step)) < EXACT_PX);
    CHECK(std::fabs(static_cast<double>(dense_step) * 2.0 -
                    std::floor(static_cast<double>(dense_step) * 2.0 + 0.5)) < EXACT_PX);
    draw_frame(stage, document);
    now = ImTypeset::GetDocumentStats(document);
    CHECK_EQ(now.BlocksLaidOut, dense.BlocksLaidOut);
    CHECK_EQ(now.StructureVisits, dense.StructureVisits);

    // An edit still costs what it changes: text appended to the last block
    // lays that block out when it is next looked at, and no other.
    CHECK(ImTypeset::AppendDocumentText(document, "\nappended     7  row"));
    draw_frame(stage, document);
    now = ImTypeset::GetDocumentStats(document);
    CHECK_EQ(now.BlocksLaidOut, dense.BlocksLaidOut);
    CHECK_EQ(now.StructureVisits, dense.StructureVisits);
    const ImTypesetBlockGeometry last =
        ImTypeset::GetDocumentBlockGeometry(document, static_cast<int>(blocks) - 1);
    CHECK(last.LaidOut);
    CHECK_EQ(last.LineCount, 4);
    now = ImTypeset::GetDocumentStats(document);
    CHECK_EQ(now.BlocksLaidOut, dense.BlocksLaidOut + 1);
    const ImU64 after_edit = now.BlocksLaidOut;

    // Another size, said through the style as a slider says it: the same.
    style.FontSize = 19.0f;
    CHECK(ImTypeset::LayoutDocument(document, 600.0f, &style));
    draw_frame(stage, document);
    now = ImTypeset::GetDocumentStats(document);
    CHECK_EQ(now.StructureVisits - dense.StructureVisits, 2 * blocks);
    CHECK(now.BlocksLaidOut - after_edit > 2 && now.BlocksLaidOut - after_edit < 60);
    const float larger = ImTypeset::GetDeviceGlyphAdvance(ImTypesetFace_Code, 'n', &style);
    CHECK(larger > dense_step);
    CHECK(std::fabs(code_step(document) - static_cast<double>(larger)) < EXACT_PX);

    // Back on the first display at the first size: the first advance again.
    set_density(1.0f);
    style.FontSize = 16.0f;
    CHECK(ImTypeset::LayoutDocument(document, 600.0f, &style));
    draw_frame(stage, document);
    CHECK(std::fabs(code_step(document) - static_cast<double>(unit_step)) < EXACT_PX);
    // Two devices were met, and each has kept its number.
    CHECK_EQ(stage.ctx->devices.table.size(), size_t(2));
    CHECK_EQ(stage.ctx->devices.epoch, 0u);
    ImTypeset::DestroyDocument(document);
}

TEST(a_document_measured_by_its_fonts_is_the_same_on_every_display)
{
    Stage stage(Faces::USUAL, 1.0f);
    REQUIRE(stage.ctx != nullptr);
    ImTypesetStyle style;
    style.FontSize = 16.0f;
    style.Advances = ImTypesetAdvances_Font;
    ImTypesetDocument* document = long_document(200, 600.0f, style);
    REQUIRE(document != nullptr);
    draw_frame(stage, document);
    const ImTypesetDocumentStats first = ImTypeset::GetDocumentStats(document);
    const double step = code_step(document);
    CHECK(std::fabs(step - static_cast<double>(ImTypeset::GetGlyphAdvance(
                               ImTypesetFace_Code, 16.0f * 2048.0f / 2400.0f, 'n'))) < 1.0e-3);
    set_density(2.0f);
    draw_frame(stage, document);
    CHECK(ImTypeset::LayoutDocument(document, 600.0f, &style));
    draw_frame(stage, document);
    const ImTypesetDocumentStats dense = ImTypeset::GetDocumentStats(document);
    CHECK_EQ(dense.BlocksLaidOut, first.BlocksLaidOut);
    CHECK_EQ(dense.StructureVisits, first.StructureVisits);
    CHECK_EQ(dense.MaterialBuilds, first.MaterialBuilds);
    CHECK_EQ(code_step(document), step);
    ImTypeset::DestroyDocument(document);
}

TEST(a_block_can_say_what_measures_it_and_is_laid_out_again_when_its_display_changes)
{
    // The document is measured by its fonts and one style of block says the
    // device: the document's style does not show that the display matters.
    Stage stage(Faces::USUAL, 1.0f);
    REQUIRE(stage.ctx != nullptr);
    ImTypesetStyle style;
    style.FontSize = 15.0f;
    style.Advances = ImTypesetAdvances_Font;
    ImTypesetBlockStyle on_device;
    on_device.Advances = ImTypesetAdvances_Device;
    ImTypesetDocument* document = long_document(50, 600.0f, style, &on_device);
    REQUIRE(document != nullptr);
    draw_frame(stage, document);
    ImTypesetStyle asked = style;
    const float unit_step = ImTypeset::GetDeviceGlyphAdvance(ImTypesetFace_Code, 'n', &asked);
    CHECK(std::fabs(code_step(document) - static_cast<double>(unit_step)) < EXACT_PX);
    const ImTypesetDocumentStats first = ImTypeset::GetDocumentStats(document);
    set_density(2.0f);
    draw_frame(stage, document);
    const float dense_step = ImTypeset::GetDeviceGlyphAdvance(ImTypesetFace_Code, 'n', &asked);
    CHECK(std::fabs(code_step(document) - static_cast<double>(dense_step)) < EXACT_PX);
    const ImTypesetDocumentStats dense = ImTypeset::GetDocumentStats(document);
    CHECK_EQ(dense.StructureVisits - first.StructureVisits, first.StructureVisits);
    CHECK_EQ(dense.BlocksLaidOut - first.BlocksLaidOut, first.BlocksLaidOut);
    ImTypeset::DestroyDocument(document);

    // And the other way about: a document on the device, a block by its font.
    set_density(1.0f);
    style.Advances = ImTypesetAdvances_Default;
    ImTypesetBlockStyle by_font;
    by_font.Advances = ImTypesetAdvances_Font;
    document = long_document(5, 600.0f, style, &by_font);
    REQUIRE(document != nullptr);
    const float font_step =
        ImTypeset::GetGlyphAdvance(ImTypesetFace_Code, 15.0f * 2048.0f / 2400.0f, 'n');
    CHECK(std::fabs(code_step(document) - static_cast<double>(font_step)) < 1.0e-3);
    CHECK(font_step != std::floor(font_step));
    ImTypeset::DestroyDocument(document);
}

TEST(a_paragraph_and_a_widget_are_set_again_for_another_display_once)
{
    Stage stage(Faces::USUAL, 1.0f);
    REQUIRE(stage.ctx != nullptr);
    ImTypesetStyle code;
    code.Face = ImTypesetFace_Code;
    code.FontSize = 16.0f;
    code.Align = ImTypesetAlign_Left;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    REQUIRE(p != nullptr);
    CHECK(ImTypeset::SetParagraphText(p, "if (a == b) return 0;"));
    CHECK(ImTypeset::LayoutParagraph(p, 600.0f, &code));
    const ImTypesetStats first = ImTypeset::GetStats();
    const float width = ImTypeset::GetParagraphLine(p, 0).Max.x;
    CHECK(ImTypeset::LayoutParagraph(p, 600.0f, &code));
    CHECK_EQ(ImTypeset::GetStats().TextBuilds, first.TextBuilds);
    CHECK_EQ(ImTypeset::GetStats().Layouts, first.Layouts);

    set_density(2.0f);
    CHECK(ImTypeset::LayoutParagraph(p, 600.0f, &code));
    const ImTypesetStats dense = ImTypeset::GetStats();
    CHECK_EQ(dense.TextBuilds, first.TextBuilds + 1);
    CHECK_EQ(dense.Layouts, first.Layouts + 1);
    CHECK(ImTypeset::LayoutParagraph(p, 600.0f, &code));
    CHECK_EQ(ImTypeset::GetStats().TextBuilds, dense.TextBuilds);
    // Twenty-one characters, each a whole number of framebuffer pixels.
    const float dense_width = ImTypeset::GetParagraphLine(p, 0).Max.x;
    const float step = ImTypeset::GetDeviceGlyphAdvance(ImTypesetFace_Code, 'a', &code);
    CHECK(std::fabs(static_cast<double>(dense_width) - 21.0 * static_cast<double>(step)) <
          EXACT_PX);
    (void)width;

    // By its font: nothing to set again.
    ImTypesetStyle by_font = code;
    by_font.Advances = ImTypesetAdvances_Font;
    CHECK(ImTypeset::LayoutParagraph(p, 600.0f, &by_font));
    const ImTypesetStats font = ImTypeset::GetStats();
    set_density(1.0f);
    CHECK(ImTypeset::LayoutParagraph(p, 600.0f, &by_font));
    CHECK_EQ(ImTypeset::GetStats().TextBuilds, font.TextBuilds);
    CHECK_EQ(ImTypeset::GetStats().Layouts, font.Layouts);
    ImTypeset::DestroyParagraph(p);

    // A widget: once for each display, and never again while it stays.
    ImU64 builds[6] = {};
    for (int frame = 0; frame < 6; ++frame) {
        set_density(frame < 3 ? 1.0f : 2.0f);
        stage.begin();
        ImTypeset::TextUnformatted("int main(void) { return 0; }", nullptr, 0, 0.0f, &code);
        stage.end();
        builds[frame] = ImTypeset::GetStats().TextBuilds;
    }
    CHECK_EQ(builds[1], builds[0]);
    CHECK_EQ(builds[2], builds[0]);
    CHECK_EQ(builds[3], builds[0] + 1);
    CHECK_EQ(builds[4], builds[3]);
    CHECK_EQ(builds[5], builds[3]);
}

TEST(text_on_the_device_has_no_kerns_and_a_formula_keeps_its_fonts_advances)
{
    Stage stage(Faces::USUAL, 1.0f);
    REQUIRE(stage.ctx != nullptr);
    ImTypesetStyle style;
    style.FontSize = 17.0f;
    style.Align = ImTypesetAlign_Left;
    const char* const text = "AVATAR Wavy $\\frac{a+b}{c}$ To";
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    REQUIRE(p != nullptr);
    struct Counted {
        int kerned_pairs = 0; // letters of a word not one advance apart
        int device_keys = 0;
        int font_keys = 0;
        int math_on_device = 0;
        std::vector<Scaled> math_x; // each math glyph from the formula's first
    };
    const auto count = [&](ImTypesetAdvances advances) {
        Counted c;
        style.Advances = advances;
        CHECK(ImTypeset::SetParagraphText(p, text, nullptr, nullptr, 0, ImTypesetTextFlags_Math));
        CHECK(ImTypeset::LayoutParagraph(p, 800.0f, &style));
        const std::vector<DrawCmd>& cmds = shipped_commands(*p);
        const DrawCmd* before = nullptr;
        Scaled math_first = 0;
        bool have_math = false;
        for (const DrawCmd& cmd : cmds) {
            if (cmd.type != CmdType::GLYPH) {
                before = nullptr;
                continue;
            }
            FontSpec spec;
            if (!parse_font_key(cmd.font(), &spec)) {
                FAIL("a glyph's key is not one of the library's");
                continue;
            }
            if (spec.face == Face::MATH) {
                c.math_on_device += spec.device != 0;
                if (!have_math)
                    math_first = cmd.x;
                have_math = true;
                c.math_x.push_back(cmd.x - math_first);
                before = nullptr;
                continue;
            }
            (spec.device != 0 ? c.device_keys : c.font_keys) += 1;
            if (before != nullptr) {
                const CharDimens dimens = stage.ctx->cache(before->font(), before->codepoint);
                c.kerned_pairs += cmd.x - before->x != dimens.width;
            }
            before = &cmd;
        }
        return c;
    };
    const Counted by_font = count(ImTypesetAdvances_Font);
    const Counted on_device = count(ImTypesetAdvances_Device);
    // The font kerns AV, VA, TA, Wa, To; the device has none.
    CHECK(by_font.kerned_pairs >= 4);
    CHECK_EQ(on_device.kerned_pairs, 0);
    CHECK_EQ(by_font.device_keys, 0);
    CHECK_EQ(on_device.font_keys, 0);
    CHECK(on_device.device_keys > 10);
    // The formula is the same formula: no glyph of it is measured on the
    // device, and its glyphs stand where they stood against its first.
    CHECK_EQ(on_device.math_on_device, 0);
    CHECK(by_font.math_x.size() >= 4);
    CHECK(on_device.math_x == by_font.math_x);
    ImTypeset::DestroyParagraph(p);
}

TEST(under_the_references_rules_the_default_measures_every_face_by_its_font)
{
    Stage stage(Faces::USUAL, 1.0f);
    REQUIRE(stage.ctx != nullptr);
    ImTypesetStyle style;
    style.Face = ImTypesetFace_Code;
    style.FontSize = 15.0f;
    const auto commands = [&](ImTypesetAdvances advances) {
        style.Advances = advances;
        ImTypesetParagraph* p = ImTypeset::CreateParagraph();
        std::vector<DrawCmd> out;
        if (p == nullptr)
            return out;
        CHECK(ImTypeset::SetParagraphText(p, "total = float(baked) + kern_px"));
        CHECK(ImTypeset::LayoutParagraph(p, 800.0f, &style));
        out = shipped_commands(*p);
        ImTypeset::DestroyParagraph(p);
        return out;
    };
    const auto same = [](const std::vector<DrawCmd>& a, const std::vector<DrawCmd>& b) {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i].type != b[i].type || a[i].x != b[i].x || a[i].y != b[i].y ||
                a[i].a != b[i].a || a[i].codepoint != b[i].codepoint)
                return false;
        return true;
    };
    stage.ctx->tex_rules = TexRules::reference();
    const std::vector<DrawCmd> reference_default = commands(ImTypesetAdvances_Default);
    const std::vector<DrawCmd> reference_font = commands(ImTypesetAdvances_Font);
    const std::vector<DrawCmd> reference_device = commands(ImTypesetAdvances_Device);
    CHECK(!reference_default.empty());
    CHECK(same(reference_default, reference_font));
    CHECK_EQ(resolve_style(*stage.ctx, ImTypesetStyle()).device, 0);
    // Asked for outright it is still the device.
    CHECK(!same(reference_device, reference_font));
    stage.ctx->tex_rules = TexRules::product();
    const std::vector<DrawCmd> product_default = commands(ImTypesetAdvances_Default);
    CHECK(same(product_default, reference_device));
    CHECK(same(commands(ImTypesetAdvances_Font), reference_font));
}

TEST(without_its_imgui_context_text_is_measured_by_its_font_and_set_again_when_it_is_back)
{
    Stage stage(Faces::USUAL, 1.0f);
    REQUIRE(stage.ctx != nullptr);
    ImTypesetStyle code;
    code.Face = ImTypesetFace_Code;
    code.FontSize = 15.0f;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    REQUIRE(p != nullptr);
    CHECK(ImTypeset::SetParagraphText(p, "0123456789"));
    CHECK(ImTypeset::LayoutParagraph(p, 800.0f, &code));
    const float on_device = ImTypeset::GetParagraphLine(p, 0).Max.x;
    const float step = ImTypeset::GetDeviceGlyphAdvance(ImTypesetFace_Code, '0', &code);
    CHECK(std::fabs(static_cast<double>(on_device) - 10.0 * static_cast<double>(step)) < EXACT_PX);

    ImGuiContext* imgui = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(nullptr);
    // Nothing of the atlas is asked: the font file answers.
    CHECK_EQ(ImTypeset::GetDeviceGlyphAdvance(ImTypesetFace_Code, '0', &code), 0.0f);
    CHECK_EQ(ImTypeset::GetLastError()->Code, ImTypesetError_NoContext);
    CHECK(ImTypeset::LayoutParagraph(p, 800.0f, &code));
    const float by_font = ImTypeset::GetParagraphLine(p, 0).Max.x;
    const float font_step =
        ImTypeset::GetGlyphAdvance(ImTypesetFace_Code, 15.0f * 2048.0f / 2400.0f, '0');
    CHECK(std::fabs(static_cast<double>(by_font) - 10.0 * static_cast<double>(font_step)) < 1.0e-3);
    CHECK(by_font != on_device);
    ImGui::SetCurrentContext(imgui);
    CHECK(ImTypeset::LayoutParagraph(p, 800.0f, &code));
    CHECK_EQ(ImTypeset::GetParagraphLine(p, 0).Max.x, on_device);
    ImTypeset::DestroyParagraph(p);
}

TEST(the_device_advance_a_host_can_ask_for_is_the_atlas_entrys_rounded_to_the_pixel)
{
    for (const float density : DENSITIES) {
        Stage stage(Faces::USUAL, density);
        REQUIRE(stage.ctx != nullptr);
        CHECK_CONTEXT("density %g", static_cast<double>(density));
        ImTypesetStyle style;
        for (const float size : SIZES) {
            CHECK_CONTEXT("pixel height %g", static_cast<double>(size));
            style.FontSize = size;
            const Page page = {Faces::USUAL, SizeIs::PIXEL_HEIGHT, size, density,
                               ImTypesetAdvances_Default};
            for (const ImTypesetFace face : {ImTypesetFace_Code, ImTypesetFace_Regular}) {
                ImFontBaked* baked = library_bake(stage, page, face);
                REQUIRE(baked != nullptr);
                for (const unsigned int c : {'M', 'i', ' ', '0'}) {
                    const ImFontGlyph* glyph = baked->FindGlyph(static_cast<ImWchar>(c));
                    REQUIRE(glyph != nullptr);
                    const double expected = std::floor(static_cast<double>(glyph->AdvanceX) + 0.5) /
                                            static_cast<double>(density);
                    const float got = ImTypeset::GetDeviceGlyphAdvance(face, c, &style);
                    CHECK_EQ(ImTypeset::GetLastError()->Code, ImTypesetError_None);
                    CHECK(std::fabs(static_cast<double>(got) - expected) < 1.0e-4);
                    if (LOADER_HINTS)
                        CHECK_EQ(glyph->AdvanceX, std::floor(glyph->AdvanceX));
                }
            }
        }
        // A scale is a block style's or a run's: the size it comes to.
        style.FontSize = 16.0f;
        ImTypesetStyle doubled = style;
        doubled.FontSize = 32.0f;
        CHECK_EQ(ImTypeset::GetDeviceGlyphAdvance(ImTypesetFace_Code, 'M', &style, 2.0f),
                 ImTypeset::GetDeviceGlyphAdvance(ImTypesetFace_Code, 'M', &doubled));
        // The style's own Advances is not read.
        ImTypesetStyle by_font = style;
        by_font.Advances = ImTypesetAdvances_Font;
        CHECK_EQ(ImTypeset::GetDeviceGlyphAdvance(ImTypesetFace_Code, 'M', &by_font),
                 ImTypeset::GetDeviceGlyphAdvance(ImTypesetFace_Code, 'M', &style));
        // What it refuses.
        CHECK_EQ(ImTypeset::GetDeviceGlyphAdvance(ImTypesetFace_Code, 0x4E2D, &style), 0.0f);
        CHECK_EQ(ImTypeset::GetLastError()->Code, ImTypesetError_InvalidArgument);
        CHECK_EQ(ImTypeset::GetDeviceGlyphAdvance(ImTypesetFace_COUNT, 'M', &style), 0.0f);
        CHECK_EQ(ImTypeset::GetLastError()->Code, ImTypesetError_InvalidArgument);
        CHECK(ImTypeset::RemoveFont(ImTypesetFace_Code));
        CHECK_EQ(ImTypeset::GetDeviceGlyphAdvance(ImTypesetFace_Code, 'M', &style), 0.0f);
        CHECK_EQ(ImTypeset::GetLastError()->Code, ImTypesetError_NoFont);
    }
}

TEST(when_the_devices_numbers_run_out_they_start_again_and_what_was_laid_out_is_set_again)
{
    Stage stage(Faces::USUAL, 1.0f);
    REQUIRE(stage.ctx != nullptr);
    ImTypesetStyle code;
    code.Face = ImTypesetFace_Code;
    code.FontSize = 16.0f;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    REQUIRE(p != nullptr);
    CHECK(ImTypeset::SetParagraphText(p, "0123456789"));
    CHECK(ImTypeset::LayoutParagraph(p, 800.0f, &code));
    const float width = ImTypeset::GetParagraphLine(p, 0).Max.x;
    const ImU64 builds = ImTypeset::GetStats().TextBuilds;
    // A host that sets text at one number of units to the inch after another
    // (a zoom that is animated): each is a device.
    ImTypesetStyle zoomed = code;
    for (uint32_t i = 0; i < MAX_DEVICES + 10; ++i) {
        zoomed.PixelsPerInch = 100.0f + 0.25f * static_cast<float>(i);
        CHECK(resolve_style(*stage.ctx, zoomed).device != 0);
    }
    CHECK_EQ(stage.ctx->devices.epoch, 1u);
    CHECK(stage.ctx->devices.table.size() < 20);
    // The paragraph's device has another number now, or none: it is not
    // taken for current, and is set again to the same lines.
    CHECK(ImTypeset::LayoutParagraph(p, 800.0f, &code));
    CHECK_EQ(ImTypeset::GetStats().TextBuilds, builds + 1);
    CHECK_EQ(ImTypeset::GetParagraphLine(p, 0).Max.x, width);
    ImTypeset::DestroyParagraph(p);
}

// Pictures for a person: the ruler and the row of seventy-four characters,
// every role the monospace file at a pixel height of 16 (an advance of 7.63
// px in the font, 8 in the atlas under a hinting loader), three times one
// under another from one x: by Dear ImGui in the role's ImFont, by the
// library on the device's advances, and by the library on the font's. Red
// hairlines stand every ten characters of the device's grid; characters 21 to
// 40 of the long row are selected in both of the library's. Asserts nothing.
TEST(pictures_for_whoever_sets_IMTYPESET_DEVICE_ADVANCES_SHOTS)
{
    const char* const directory = std::getenv("IMTYPESET_DEVICE_ADVANCES_SHOTS");
    if (directory == nullptr || directory[0] == '\0') {
        std::printf("    SKIPPED: IMTYPESET_DEVICE_ADVANCES_SHOTS is not set\n");
        return;
    }
    const std::string rows = std::string(RULER) + "\n" + LONG;
    constexpr int SHOT_WIDTH = 640;
    constexpr int SHOT_HEIGHT = 250;
    for (const float density : DENSITIES) {
        Stage stage(Faces::MONO, density);
        REQUIRE(stage.ctx != nullptr);
        ImGui::GetIO().DisplaySize =
            ImVec2(static_cast<float>(SHOT_WIDTH), static_cast<float>(SHOT_HEIGHT));
        soft_raster::Renderer renderer;
        renderer.init();
        ImTypesetStyle style;
        style.FontSize = 16.0f;
        style.Align = ImTypesetAlign_Left;
        style.Color = BLACK;
        style.SelectionColor = IM_COL32(120, 170, 255, 255);
        ImTypesetStyle by_font = style;
        by_font.Advances = ImTypesetAdvances_Font;
        ImTypesetDocument* on_device = document_of(rows, ImTypesetBlockKind_Code, 620.0f, style);
        ImTypesetDocument* on_font = document_of(rows, ImTypesetBlockKind_Code, 620.0f, by_font);
        REQUIRE(on_device != nullptr && on_font != nullptr);
        ImTypesetDocumentDraw selected;
        selected.Selection.Start.Offset = 41 + 20;
        selected.Selection.End.Offset = 41 + 40;
        const float cell = ImTypeset::GetDeviceGlyphAdvance(ImTypesetFace_Code, '0', &style);
        const float left = 10.0f;
        soft_raster::Image image;
        for (int frame = 0; frame < 2; ++frame) {
            stage.begin();
            ImDrawList* list = ImGui::GetWindowDrawList();
            for (int column = 0; column <= 70; column += 10) {
                const float x = left + cell * static_cast<float>(column);
                list->AddRectFilled(ImVec2(x, 0.0f),
                                    ImVec2(x + 1.0f / density, static_cast<float>(SHOT_HEIGHT)),
                                    IM_COL32(255, 150, 150, 255));
            }
            list->AddText(ImVec2(left, 4.0f), IM_COL32(120, 120, 120, 255),
                          "Dear ImGui, the role's ImFont at 16 px");
            ImGui::PushFont(ImTypeset::GetFont(ImTypesetFace_Code), 16.0f);
            list->AddText(ImVec2(left, 20.0f), BLACK, RULER);
            list->AddText(ImVec2(left, 40.0f), BLACK, LONG);
            ImGui::PopFont();
            list->AddText(ImVec2(left, 74.0f), IM_COL32(120, 120, 120, 255),
                          "imtypeset, ImTypesetAdvances_Default: the device's advances");
            CHECK(ImTypeset::DrawDocument(on_device, list, ImVec2(left, 90.0f), &selected));
            list->AddText(ImVec2(left, 154.0f), IM_COL32(120, 120, 120, 255),
                          "imtypeset, ImTypesetAdvances_Font: the font's advances, 7.63 px");
            CHECK(ImTypeset::DrawDocument(on_font, list, ImVec2(left, 170.0f), &selected));
            stage.end();
            image = soft_raster::Image(static_cast<int>(static_cast<float>(SHOT_WIDTH) * density),
                                       static_cast<int>(static_cast<float>(SHOT_HEIGHT) * density),
                                       IM_COL32(255, 255, 255, 255));
            renderer.render(ImGui::GetDrawData(), image);
        }
        char path[1024];
        std::snprintf(path, sizeof path, "%s/device-advances-%s-%gx.png", directory,
                      LOADER_HINTS ? "freetype" : "stb", static_cast<double>(density));
        if (!soft_raster::write_png(path, image))
            FAIL("could not write %s", path);
        // Two enlargements by repeating pixels, for a person to see the steps
        // in: the first forty characters, and the selection's two ends in
        // both of the library's blocks.
        const auto enlarged = [&](const char* suffix, int x0, int y0, int x1, int y1, int zoom) {
            x1 = std::min(x1, image.width);
            y1 = std::min(y1, image.height);
            soft_raster::Image large((x1 - x0) * zoom, (y1 - y0) * zoom,
                                     IM_COL32(255, 255, 255, 255));
            for (int y = 0; y < large.height; ++y)
                for (int x = 0; x < large.width; ++x)
                    std::memcpy(
                        &large.rgba[(static_cast<size_t>(y) * static_cast<size_t>(large.width) +
                                     static_cast<size_t>(x)) *
                                    4u],
                        &image.rgba[(static_cast<size_t>(y0 + y / zoom) *
                                         static_cast<size_t>(image.width) +
                                     static_cast<size_t>(x0 + x / zoom)) *
                                    4u],
                        4);
            std::snprintf(path, sizeof path, "%s/device-advances-%s-%gx-%s.png", directory,
                          LOADER_HINTS ? "freetype" : "stb", static_cast<double>(density), suffix);
            if (!soft_raster::write_png(path, large))
                FAIL("could not write %s", path);
        };
        const int scale = static_cast<int>(density);
        enlarged("x3", 0, 0, 340 * scale, image.height, 3);
        enlarged("selection", 140 * scale, 84 * scale, 350 * scale, 212 * scale, 6 / scale);
        std::printf("    wrote %s and two more beside it, cell %g units\n", path,
                    static_cast<double>(cell));
        ImTypeset::DestroyDocument(on_device);
        ImTypeset::DestroyDocument(on_font);
        renderer.shutdown();
    }
}
