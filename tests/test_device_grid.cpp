// The grid of a fixed-pitch face measured on the device, on the text a diff
// really holds: characters the code face lacks, characters that draw nothing,
// tabs among wide characters; and what keeps the grid true while an
// application runs: the atlas rasterised another way, a display of another
// density, a document asked a question while its ImGui context is away.
//
// Everything is read from vertices, as in tests/test_device_advances.cpp: a
// glyph's pen is the left edge of its quad less the offset of its bitmap in
// the atlas entry the quad is textured from, and which entry that is, is
// found by the quad's texture coordinates among the entries of every face.
//
// WHAT IS ASSERTED of a row of a Code block in a fixed-pitch face on the
// device, per character:
//   - it takes the number of cells the row's table says, a whole number;
//   - where it draws a glyph, the glyph's pen is where layout has the
//     character (EXACT_PX: see tests/test_device_advances.cpp), which is a
//     whole number of cells from the row's start;
//   - the rectangle answered for it is its cells, a point a quarter and three
//     quarters across them hits it and the next character, and a copy of the
//     row is the row as it was written.
//
// IMTYPESET_DEVICE_GRID_TABLE=1 prints what was measured, a line a row.
// IMTYPESET_DEVICE_GRID_SHOTS=<directory> writes the pictures there.

#include "golden.h"

#include "blocks.h"
#include "context.h"
#include "draw.h"
#include "font_loader.h"
#include "imtypeset.h"
#include "support/soft_raster.h"
#include "tex_rules.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{

using namespace ImTypeset::detail;

constexpr int WIDTH = 1500;
constexpr int HEIGHT = 900;
constexpr ImU32 BLACK = IM_COL32(0, 0, 0, 255);
constexpr double EXACT_PX = 1.0 / 256.0;

constexpr bool LOADER_HINTS =
#ifdef IMGUI_ENABLE_FREETYPE
    true;
#else
    false;
#endif

const ImTypesetFace ALL_FACES[] = {ImTypesetFace_Regular, ImTypesetFace_Bold,
                                   ImTypesetFace_Italic,  ImTypesetFace_BoldItalic,
                                   ImTypesetFace_Code,    ImTypesetFace_Math};

const char* file_of(ImTypesetFace face)
{
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

bool env_set(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}

// An ImGui context on a display of one density with the usual faces in its
// atlas, and a window over the whole display.
struct Stage {
    explicit Stage(float scale, const char* regular_path = nullptr)
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
        for (const ImTypesetFace face : ALL_FACES) {
            std::string path = golden::data_path(std::string("fonts/") + file_of(face));
            if (face == ImTypesetFace_Regular && regular_path != nullptr)
                path = regular_path;
            if (ImTypeset::AddFontFromFileTTF(face, path.c_str()) == nullptr) {
                FAIL("%s: %s", path.c_str(), ImTypeset::GetLastError()->Message);
                ok = false;
            }
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
        ImGui::Begin("device grid", nullptr,
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

    ImTypesetContext* ctx = nullptr;
    bool ok = true;
};

void set_density(float density)
{
    ImGui::GetIO().DisplayFramebufferScale = ImVec2(density, density);
}

double density_now()
{
    return static_cast<double>(ImGui::GetIO().DisplayFramebufferScale.x);
}

void utf8(std::string& out, uint32_t c)
{
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

// How wide a character of a row is expected to be.
enum : int {
    ZERO = 0,   // no cell
    ONE = 1,    // one cell
    TWO = 2,    // two
    GUEST = -1, // borrowed from another face: as many cells as its own baked advance needs
    TAB = -2    // to the next tab stop: a multiple of TAB_WIDTH cells
};

struct Char {
    uint32_t codepoint;
    int cells;
};

struct RowSpec {
    const char* name;
    std::vector<Char> chars;
};

// The rows a diff really has. The code face is Inconsolata: it holds the
// box-drawing block, U+FFFD, U+0301 (no advance, with ink) and U+200B (an
// advance of half an em and no ink), and lacks U+FEFF, U+200D, the controls,
// Cyrillic, Greek, CJK and everything past the BMP. Roboto, the regular
// face, holds Cyrillic and Greek; Latin Modern Math holds U+1D400.
std::vector<RowSpec> diff_rows()
{
    return {
        {"ascii", {{'a', ONE}, {'b', ONE}, {'c', ONE}, {' ', ONE}, {'d', ONE}, {'|', ONE}}},
        {"zero width space",
         {{'a', ONE}, {0x200B, ZERO}, {'b', ONE}, {'c', ONE}, {'d', ONE}, {'|', ONE}}},
        {"byte order mark", {{0xFEFF, ZERO}, {'a', ONE}, {'b', ONE}, {'c', ONE}, {'|', ONE}}},
        {"combining acute", {{'e', ONE}, {0x0301, ZERO}, {'x', ONE}, {'|', ONE}}},
        {"zero width joiner", {{'a', ONE}, {0x200D, ZERO}, {'b', ONE}, {'|', ONE}}},
        {"cjk no face has", {{0x4E2D, ONE}, {0x6587, ONE}, {'x', ONE}, {'|', ONE}}},
        {"box drawing",
         {{0x250C, ONE}, {0x2500, ONE}, {0x2502, ONE}, {0x2588, ONE}, {'x', ONE}, {'|', ONE}}},
        {"emoji", {{0x1F600, ONE}, {'x', ONE}, {'|', ONE}}},
        {"escape",
         {{'a', ONE},
          {0x1B, ZERO},
          {'[', ONE},
          {'3', ONE},
          {'1', ONE},
          {'m', ONE},
          {'b', ONE},
          {'|', ONE}}},
        {"cyrillic from the regular face",
         {{0x416, GUEST}, {'x', ONE}, {0x416, GUEST}, {0x416, GUEST}, {'y', ONE}, {'|', ONE}}},
        {"greek from the regular face", {{0x3A9, GUEST}, {'x', ONE}, {'|', ONE}}},
        {"math alphabet from the math face", {{0x1D400, GUEST}, {'x', ONE}, {'|', ONE}}},
        {"em space", {{'a', ONE}, {0x2003, ONE}, {'b', ONE}, {'|', ONE}}},
        {"tab after ascii", {{'a', ONE}, {'b', ONE}, {'\t', TAB}, {'x', ONE}, {'|', ONE}}},
        {"tab after a wide character", {{0x416, GUEST}, {'\t', TAB}, {'x', ONE}, {'|', ONE}}},
        {"tab after a character of no width",
         {{'a', ONE}, {0x200B, ZERO}, {0x1B, ZERO}, {'\t', TAB}, {'x', ONE}, {'|', ONE}}},
        {"tab after wide and narrow",
         {{'a', ONE},
          {0x416, GUEST},
          {0x416, GUEST},
          {0x416, GUEST},
          {'b', ONE},
          {'\t', TAB},
          {'x', ONE},
          {'|', ONE}}},
        {"replacement character written", {{0xFFFD, ONE}, {'x', ONE}, {'|', ONE}}},
    };
}

struct Text {
    std::string bytes;
    // For row r: the byte offset of each of its characters, and one more for
    // the row's end.
    std::vector<std::vector<int>> offsets;
};

Text text_of(const std::vector<RowSpec>& rows)
{
    Text text;
    for (size_t r = 0; r < rows.size(); ++r) {
        if (r != 0)
            text.bytes += '\n';
        std::vector<int> offsets;
        for (const Char& c : rows[r].chars) {
            offsets.push_back(static_cast<int>(text.bytes.size()));
            utf8(text.bytes, c.codepoint);
        }
        offsets.push_back(static_cast<int>(text.bytes.size()));
        text.offsets.push_back(offsets);
    }
    return text;
}

ImTypesetDocument* document_of(const std::string& text, ImTypesetBlockKind kind, float width,
                               const ImTypesetStyle& style, ImTypesetBlockFlags flags = 0,
                               const ImTypesetBlockStyle* block_style = nullptr,
                               const ImTypesetDocumentOptions* options = nullptr)
{
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    if (document == nullptr)
        return nullptr;
    ImTypesetBlock block;
    block.Kind = kind;
    block.Flags = flags;
    block.TextStart = 0;
    block.TextEnd = static_cast<int>(text.size());
    ImTypesetDocumentContent content;
    content.Text = text.data();
    content.TextSize = static_cast<int>(text.size());
    content.Blocks = &block;
    content.BlockCount = 1;
    if (block_style != nullptr) {
        content.Styles = block_style;
        content.StyleCount = 1;
    }
    if (options != nullptr && !ImTypeset::SetDocumentOptions(document, options))
        FAIL("SetDocumentOptions: %s", ImTypeset::GetLastError()->Message);
    if (!ImTypeset::SetDocumentBlocks(document, &content))
        FAIL("SetDocumentBlocks: %s", ImTypeset::GetLastError()->Message);
    if (!ImTypeset::LayoutDocument(document, width, &style))
        FAIL("LayoutDocument: %s", ImTypeset::GetLastError()->Message);
    return document;
}

// The atlas entry the library draws a face from for a style, on the display
// as it is now: asked of the one function the draw loop asks.
ImFontBaked* library_bake(Stage& stage, const ImTypesetStyle& style, ImTypesetFace face,
                          float scale = 1.0f)
{
    ImFont* font = ImTypeset::GetFont(face);
    if (font == nullptr || stage.ctx == nullptr)
        return nullptr;
    const ResolvedStyle resolved = resolve_style(*stage.ctx, style);
    const Scaled em =
        scale == 1.0f
            ? resolved.em
            : round_to_scaled(static_cast<double>(resolved.em) * static_cast<double>(scale));
    const Face role = static_cast<Face>(face - 1);
    const FontMetrics* ours = stage.ctx->library.metrics(role, static_cast<double>(em));
    return face_bake(font, ours, em, resolved.px_per_inch,
                     density_now() > 1.0 ? density_now() : 1.0);
}

// A glyph the draw wrote: its pen in FRAMEBUFFER pixels, and what it is.
struct Drawn {
    bool found = false;
    double pen = 0.0;
    double top = 0.0;
    ImTypesetFace face = 0;
    uint32_t shown = 0;   // the codepoint of the glyph drawn
    double advance = 0.0; // its advance in its entry, in framebuffer pixels
    ImU32 colour = 0;
};

// The textured quads among the vertices [first, end) of a list, in order.
std::vector<int> glyph_quads(const ImDrawList* list, int first, int end)
{
    std::vector<int> quads;
    for (int v = first; v + 3 < end; v += 4) {
        if (list->VtxBuffer[v].uv.x != list->VtxBuffer[v + 2].uv.x)
            quads.push_back(v);
    }
    return quads;
}

// Which glyph a quad is, looked for among the entries of every face: the
// character itself, and the two that stand in for one no face has.
bool identify(Stage& stage, const ImTypesetStyle& style, const ImDrawList* list, int v,
              uint32_t codepoint, Drawn* out, float scale = 1.0f)
{
    const double density = density_now() > 1.0 ? density_now() : 1.0;
    const ImDrawVert& a = list->VtxBuffer[v];
    const ImDrawVert& b = list->VtxBuffer[v + 2];
    for (const uint32_t shown : {codepoint, uint32_t{0xFFFD}, uint32_t{'?'}}) {
        for (const ImTypesetFace face : ALL_FACES) {
            // What the atlas is asked for: the codepoint, or under a 16-bit
            // ImWchar the stand-in the face gives one past the BMP.
            uint32_t asked = shown;
            const FaceData* const data = stage.ctx->fonts.faces[face - 1].data;
            if (data != nullptr && shown != 0)
                asked = atlas_codepoint(*data, shown);
            if (asked == 0 || asked > IM_UNICODE_CODEPOINT_MAX)
                continue;
            ImFontBaked* baked = library_bake(stage, style, face, scale);
            if (baked == nullptr)
                continue;
            const ImFontGlyph* glyph = baked->FindGlyphNoFallback(static_cast<ImWchar>(asked));
            if (glyph == nullptr || !glyph->Visible)
                continue;
            if (a.uv.x != glyph->U0 || a.uv.y != glyph->V0 || b.uv.x != glyph->U1 ||
                b.uv.y != glyph->V1)
                continue;
            out->found = true;
            // An entry above density 1 is in framebuffer pixels; at density 1
            // a unit is one.
            out->pen = static_cast<double>(a.pos.x) * density - static_cast<double>(glyph->X0);
            out->top = static_cast<double>(a.pos.y) * density;
            out->face = face;
            out->shown = shown;
            out->advance = static_cast<double>(glyph->AdvanceX);
            out->colour = a.col;
            return true;
        }
    }
    return false;
}

// One block of rows drawn: for each row, each character's glyph if it drew
// one. Quads are taken in order; a quad that is no candidate of the next
// character belongs to a later one, and one left over is a failure.
std::vector<std::vector<Drawn>> drawn_rows(Stage& stage, const ImTypesetStyle& style,
                                           ImTypesetDocument* document, const ImVec2& pos,
                                           const std::vector<RowSpec>& rows,
                                           const ImTypesetDocumentDraw* draw = nullptr,
                                           float scale = 1.0f)
{
    std::vector<std::vector<Drawn>> out(rows.size());
    // Twice: the first frame loads every glyph the second will be read by, so
    // that no texture coordinate moves under the reading.
    for (int frame = 0; frame < 2; ++frame) {
        stage.begin();
        ImDrawList* list = ImGui::GetWindowDrawList();
        const int v0 = list->VtxBuffer.Size;
        if (!ImTypeset::DrawDocument(document, list, pos, draw))
            FAIL("DrawDocument: %s", ImTypeset::GetLastError()->Message);
        const std::vector<int> quads = glyph_quads(list, v0, list->VtxBuffer.Size);
        size_t q = 0;
        for (size_t r = 0; r < rows.size(); ++r) {
            out[r].assign(rows[r].chars.size(), Drawn());
            for (size_t i = 0; i < rows[r].chars.size(); ++i) {
                const uint32_t codepoint = rows[r].chars[i].codepoint;
                if (codepoint == ' ' || codepoint == '\t' || q >= quads.size())
                    continue;
                if (identify(stage, style, list, quads[q], codepoint, &out[r][i], scale))
                    ++q;
            }
        }
        if (frame == 1 && q != quads.size())
            FAIL("%d of %d glyph quads are no character of the rows",
                 static_cast<int>(quads.size() - q), static_cast<int>(quads.size()));
        stage.end();
    }
    return out;
}

// Where layout has the caret before a byte of block 0, in framebuffer pixels
// from the document's left edge.
double caret_px(ImTypesetDocument* document, int offset)
{
    const ImTypesetPosition at = {0, offset};
    return static_cast<double>(ImTypeset::GetDocumentCaretRect(document, at).x) *
           (density_now() > 1.0 ? density_now() : 1.0);
}

double round_half_up(double value)
{
    return std::floor(value + 0.5);
}

struct RowMeasured {
    std::vector<double> cells; // each character's laid-out width in cells
    double worst_off = 0.0;    // a drawn pen from where layout has its character
    double worst_grid = 0.0;   // a laid-out position from a whole number of cells
    int drawn = 0;
};

// The rows of `rows` as a Code block of `style`, measured. `cell` is the code
// face's cell in framebuffer pixels.
std::vector<RowMeasured> measure_rows(Stage& stage, const ImTypesetStyle& style,
                                      const std::vector<RowSpec>& rows, const Text& text,
                                      ImTypesetDocument* document, const ImVec2& pos, double cell,
                                      std::vector<std::vector<Drawn>>* drawn_out = nullptr)
{
    const double density = density_now() > 1.0 ? density_now() : 1.0;
    const std::vector<std::vector<Drawn>> drawn = drawn_rows(stage, style, document, pos, rows);
    std::vector<RowMeasured> out(rows.size());
    for (size_t r = 0; r < rows.size(); ++r) {
        const double start = caret_px(document, text.offsets[r][0]);
        for (size_t i = 0; i < rows[r].chars.size(); ++i) {
            const double at = caret_px(document, text.offsets[r][i]);
            // The character's rectangle: none for one of no width. Not the
            // next caret, which at a row's end is the next row's start.
            ImTypesetDocumentRange range;
            range.Start.Block = range.End.Block = 0;
            range.Start.Offset = text.offsets[r][i];
            range.End.Offset = text.offsets[r][i + 1];
            ImVector<ImVec4> rects;
            double wide = 0.0;
            if (ImTypeset::GetDocumentRangeRects(document, range, &rects) >= 1) {
                wide = static_cast<double>(rects[0].z - rects[0].x) * density;
                // It starts where the caret before the character stands.
                out[r].worst_grid = std::max(
                    out[r].worst_grid, std::fabs(static_cast<double>(rects[0].x) * density - at));
            }
            out[r].cells.push_back(wide / cell);
            const double column = (at - start) / cell;
            out[r].worst_grid =
                std::max(out[r].worst_grid, std::fabs(column - round_half_up(column)) * cell);
            if (drawn[r][i].found) {
                ++out[r].drawn;
                const double laid = static_cast<double>(pos.x) * density + at;
                out[r].worst_off = std::max(out[r].worst_off, std::fabs(drawn[r][i].pen - laid));
            }
        }
    }
    if (drawn_out != nullptr)
        *drawn_out = drawn;
    return out;
}

ImTypesetStyle code_style(float pixel_height)
{
    ImTypesetStyle style;
    style.FontSize = pixel_height;
    style.Align = ImTypesetAlign_Left;
    style.Color = BLACK;
    return style;
}

// The code face's cell for a style on the display as it is, in framebuffer
// pixels: the library's own answer, which the tests of
// tests/test_device_advances.cpp hold to Dear ImGui's entry.
double cell_of(const ImTypesetStyle& style, float scale = 1.0f)
{
    const float units = ImTypeset::GetDeviceGlyphAdvance(ImTypesetFace_Code, '0', &style, scale);
    const double px = static_cast<double>(units) * (density_now() > 1.0 ? density_now() : 1.0);
    // A whole number of framebuffer pixels, answered as a float of units
    // from a whole number of scaled points.
    if (std::fabs(px - round_half_up(px)) > EXACT_PX)
        FAIL("the code face's cell is %.6f framebuffer pixels, which is not a whole number", px);
    return round_half_up(px);
}

} // namespace

// --- Finding 1: the grid, whatever the row holds ---------------------------------------

TEST(every_character_of_a_code_row_takes_a_whole_number_of_cells_and_is_drawn_in_them)
{
    const std::vector<RowSpec> rows = diff_rows();
    const Text text = text_of(rows);
    const bool table = env_set("IMTYPESET_DEVICE_GRID_TABLE");
    for (const float density : {1.0f, 2.0f}) {
        for (const float size : {13.0f, 16.0f, 17.0f, 22.0f}) {
            CHECK_CONTEXT("density %g, pixel height %g", static_cast<double>(density),
                          static_cast<double>(size));
            Stage stage(density);
            REQUIRE(stage.ctx != nullptr && stage.ok);
            const ImTypesetStyle style = code_style(size);
            const double cell = cell_of(style);
            REQUIRE(cell >= 1.0);
            CHECK_EQ(cell, round_half_up(cell));
            ImTypesetDocumentOptions options;
            options.CodeOverflow = ImTypesetCodeOverflow_Grow;
            ImTypesetDocument* document = document_of(text.bytes, ImTypesetBlockKind_Code, 1400.0f,
                                                      style, 0, nullptr, &options);
            REQUIRE(document != nullptr);
            const ImVec2 pos(20.0f, 20.0f);
            std::vector<std::vector<Drawn>> drawn;
            const std::vector<RowMeasured> measured =
                measure_rows(stage, style, rows, text, document, pos, cell, &drawn);
            for (size_t r = 0; r < rows.size(); ++r) {
                CHECK_CONTEXT("row \"%s\"", rows[r].name);
                if (table) {
                    std::string cells;
                    for (const double c : measured[r].cells) {
                        char one[24];
                        std::snprintf(one, sizeof one, " %.4g", c);
                        cells += one;
                    }
                    std::printf(
                        "    d%g px%-2g cell %-2g %-34s cells%s | glyph from layout %.4f px, "
                        "layout from the grid %.4f px, %d drawn\n",
                        static_cast<double>(density), static_cast<double>(size), cell, rows[r].name,
                        cells.c_str(), measured[r].worst_off, measured[r].worst_grid,
                        measured[r].drawn);
                }
                double column = 0.0;
                for (size_t i = 0; i < rows[r].chars.size(); ++i) {
                    CHECK_CONTEXT("character %d, U+%04X", static_cast<int>(i),
                                  rows[r].chars[i].codepoint);
                    const double cells = measured[r].cells[i];
                    double expected = static_cast<double>(rows[r].chars[i].cells);
                    if (rows[r].chars[i].cells == TAB) {
                        const double stop = static_cast<double>(TAB_WIDTH);
                        expected = stop - std::fmod(column, stop);
                    } else if (rows[r].chars[i].cells == GUEST) {
                        // As many cells as hold the advance of the glyph drawn
                        // for it, in the entry it is drawn from; were nothing
                        // drawn for a character past the BMP the font's own
                        // advance would decide, which the test does not work
                        // out again.
                        if (drawn[r][i].found) {
                            CHECK(drawn[r][i].face != ImTypesetFace_Code);
                            CHECK_EQ(drawn[r][i].shown, rows[r].chars[i].codepoint);
                            expected =
                                std::max(1.0, std::ceil(round_half_up(drawn[r][i].advance) / cell));
                        } else {
                            CHECK(rows[r].chars[i].codepoint > IM_UNICODE_CODEPOINT_MAX);
                            expected = round_half_up(cells);
                            CHECK(expected >= 1.0);
                        }
                    }
                    CHECK(std::fabs(cells - expected) * cell < EXACT_PX);
                    column += expected;
                }
                CHECK(measured[r].worst_grid < EXACT_PX);
                CHECK(measured[r].worst_off < EXACT_PX);
            }
            // The bar that ends every row is at the column its row's table
            // adds up to: columns agree between rows that agree in cells.
            ImTypeset::DestroyDocument(document);
        }
    }
}

// --- Finding 2: the atlas rasterised another way ----------------------------------------

namespace
{

// The width layout has for the first four characters of block 0, in pixels.
double four_wide(ImTypesetDocument* document)
{
    return caret_px(document, 4) - caret_px(document, 0);
}

// What ImGui's Fonts debug window does when a loader flag is ticked: every
// font's output destroyed, the flag changed in place, the output made again.
// Same ImFont pointers, same FontIds, same number of fonts.
void rebuild_fonts_with(void (*change)(ImFontAtlas*))
{
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    for (ImFont* font : atlas->Fonts)
        ImFontAtlasFontDestroyOutput(atlas, font);
    change(atlas);
    for (ImFont* font : atlas->Fonts)
        ImFontAtlasFontInitOutput(atlas, font);
}

} // namespace

TEST(advances_remembered_do_not_outlive_the_rasterisation_they_were_read_from)
{
    // The reviewer's sequence: a block laid out, the atlas's rasterisation
    // changed in place, another block laid out. Three changes: FreeType's
    // hinting turned off and its emboldening turned on, in the atlas's
    // shared loader flags, and a pixel of extra advance in every source,
    // which works under either loader. Whether a change moves the cell
    // depends on the size (unhinted, an advance is rounded by the library and
    // often to the pixel hinting chose); what is asserted at every size is
    // that afterwards there is one cell, the atlas's.
    struct Change {
        const char* name;
        void (*apply)(ImFontAtlas*);
        bool freetype_only;
        bool always_moves;
    };
    const Change changes[] = {
        {"NoHinting", [](ImFontAtlas* atlas) { atlas->FontLoaderFlags |= 1u << 0; }, true, false},
        {"Bold", [](ImFontAtlas* atlas) { atlas->FontLoaderFlags |= 1u << 5; }, true, false},
        {"a pixel of extra advance",
         [](ImFontAtlas* atlas) {
             for (ImFont* font : atlas->Fonts)
                 for (ImFontConfig* source : font->Sources)
                     source->GlyphExtraAdvanceX += 1.0f;
         },
         false, true},
    };
    for (const Change& change : changes) {
        if (change.freetype_only && !LOADER_HINTS)
            continue;
        std::string moved_at;
        for (const bool frame_between : {true, false}) {
            for (float size = 13.0f; size <= 24.0f; size += 1.0f) {
                CHECK_CONTEXT("%s, pixel height %g, %s", change.name, static_cast<double>(size),
                              frame_between ? "a frame between" : "within one frame");
                Stage stage(1.0f);
                REQUIRE(stage.ctx != nullptr && stage.ok);
                const ImTypesetStyle style = code_style(size);
                ImTypesetDocument* first =
                    document_of("aaaa", ImTypesetBlockKind_Code, 800.0f, style);
                REQUIRE(first != nullptr);
                stage.begin();
                CHECK(ImTypeset::DrawDocument(first, ImGui::GetWindowDrawList(),
                                              ImVec2(10.0f, 10.0f)));
                stage.end();
                const double before = four_wide(first);

                rebuild_fonts_with(change.apply);
                if (frame_between) {
                    stage.begin();
                    stage.end();
                }
                ImTypesetDocument* second =
                    document_of("ZZZZ", ImTypesetBlockKind_Code, 800.0f, style);
                REQUIRE(second != nullptr);
                const double second_at_once = four_wide(second);
                // What is on screen from the next frame on.
                stage.begin();
                CHECK(ImTypeset::DrawDocument(first, ImGui::GetWindowDrawList(),
                                              ImVec2(10.0f, 10.0f)));
                CHECK(ImTypeset::DrawDocument(second, ImGui::GetWindowDrawList(),
                                              ImVec2(10.0f, 60.0f)));
                stage.end();
                const double first_after = four_wide(first);
                const double second_after = four_wide(second);
                // The atlas's own answer now, asked of Dear ImGui and not of
                // the library.
                ImFontBaked* baked = library_bake(stage, style, ImTypesetFace_Code);
                REQUIRE(baked != nullptr);
                const double atlas_cell = round_half_up(
                    static_cast<double>(baked->GetCharAdvance(static_cast<ImWchar>('a'))));
                if (env_set("IMTYPESET_DEVICE_GRID_TABLE"))
                    std::printf("    %-24s px%-2g %-16s 'aaaa' %g before; then 'ZZZZ' %g; a frame "
                                "later 'aaaa' %g, 'ZZZZ' %g; the atlas's cell %g\n",
                                change.name, static_cast<double>(size),
                                frame_between ? "a frame between" : "within one frame", before,
                                second_at_once, first_after, second_after, atlas_cell);
                if (std::fabs(before - 4.0 * atlas_cell) > EXACT_PX && frame_between)
                    moved_at += " " + std::to_string(static_cast<int>(size));
                if (change.always_moves)
                    CHECK(std::fabs(before - first_after) > 1.0);
                CHECK(std::fabs(first_after - second_after) < EXACT_PX);
                CHECK(std::fabs(first_after - 4.0 * atlas_cell) < EXACT_PX);
                // And drawn there.
                const std::vector<RowSpec> rows = {
                    {"ZZZZ", {{'Z', ONE}, {'Z', ONE}, {'Z', ONE}, {'Z', ONE}}}};
                const Text text = text_of(rows);
                const std::vector<RowMeasured> measured = measure_rows(
                    stage, style, rows, text, second, ImVec2(20.0f, 20.0f), atlas_cell);
                CHECK_EQ(measured[0].drawn, 4);
                CHECK(measured[0].worst_off < EXACT_PX);
                ImTypeset::DestroyDocument(first);
                ImTypeset::DestroyDocument(second);
            }
        }
        std::printf("    %s: the cell moved at pixel heights%s\n", change.name,
                    moved_at.empty() ? " (none of 13 to 24)" : moved_at.c_str());
    }
}

TEST(the_entry_asked_for_does_not_follow_the_density_a_font_was_last_pushed_at)
{
    // At a pixel height of 15 the code face's entry at density 1 has an
    // advance of 7; an entry of the same size rasterised at density 2 has 7.5.
    // A font's CurrentRasterizerDensity is whatever the display's was when it
    // was last the current font, which is no business of a layout's.
    Stage stage(1.0f);
    REQUIRE(stage.ctx != nullptr && stage.ok);
    const ImTypesetStyle style = code_style(15.0f);
    ImFont* code = ImTypeset::GetFont(ImTypesetFace_Code);
    REQUIRE(code != nullptr);
    const float plain = ImTypeset::GetDeviceGlyphAdvance(ImTypesetFace_Code, 'a', &style);
    code->CurrentRasterizerDensity = 2.0f;
    const float pushed = ImTypeset::GetDeviceGlyphAdvance(ImTypesetFace_Code, 'b', &style);
    ImTypesetDocument* document = document_of("cdef", ImTypesetBlockKind_Code, 800.0f, style);
    REQUIRE(document != nullptr);
    if (env_set("IMTYPESET_DEVICE_GRID_TABLE"))
        std::printf("    15 px: 'a' %g; with the font's density left at 2, 'b' %g and 'cdef' %g\n",
                    static_cast<double>(plain), static_cast<double>(pushed), four_wide(document));
    CHECK_EQ(pushed, plain);
    CHECK(std::fabs(four_wide(document) - 4.0 * static_cast<double>(plain)) < EXACT_PX);
    // And it is drawn from the entry it was measured in.
    const std::vector<RowSpec> rows = {{"cdef", {{'c', ONE}, {'d', ONE}, {'e', ONE}, {'f', ONE}}}};
    const Text text = text_of(rows);
    const std::vector<RowMeasured> measured = measure_rows(
        stage, style, rows, text, document, ImVec2(20.0f, 20.0f), static_cast<double>(plain));
    CHECK_EQ(measured[0].drawn, 4);
    CHECK(measured[0].worst_off < EXACT_PX);
    ImTypeset::DestroyDocument(document);
}

TEST(an_advance_is_never_taken_from_an_entry_of_another_size)
{
    // With its sizes locked a font answers the nearest entry it has: the
    // 16 px one for a line set at 22.
    Stage stage(1.0f);
    REQUIRE(stage.ctx != nullptr && stage.ok);
    const ImTypesetStyle small = code_style(16.0f);
    const ImTypesetStyle large = code_style(22.0f);
    const float small_cell = ImTypeset::GetDeviceGlyphAdvance(ImTypesetFace_Code, '0', &small);
    ImFont* code = ImTypeset::GetFont(ImTypesetFace_Code);
    REQUIRE(code != nullptr);
    ImTypesetDocument* document = nullptr;
    code->Flags |= ImFontFlags_LockBakedSizes;
    {
        document = document_of("0000", ImTypesetBlockKind_Code, 800.0f, large);
        REQUIRE(document != nullptr);
        if (env_set("IMTYPESET_DEVICE_GRID_TABLE"))
            std::printf(
                "    the font's sizes locked at 16 px (cell %g): '0000' at 22 px is %g wide\n",
                static_cast<double>(small_cell), four_wide(document));
        CHECK(std::fabs(four_wide(document) - 4.0 * static_cast<double>(small_cell)) > 1.0);
    }
    code->Flags &= ~ImFontFlags_LockBakedSizes;
    // Once the atlas can be asked, what was laid out without it is set again.
    stage.begin();
    CHECK(ImTypeset::DrawDocument(document, ImGui::GetWindowDrawList(), ImVec2(10.0f, 10.0f)));
    stage.end();
    stage.begin();
    stage.end();
    const double cell = cell_of(large);
    CHECK(cell > static_cast<double>(small_cell));
    CHECK(std::fabs(four_wide(document) - 4.0 * cell) < EXACT_PX);
    ImTypeset::DestroyDocument(document);
}

// --- Finding 3: a paragraph kept across a change of display ----------------------------

TEST(a_paragraph_laid_out_for_another_display_is_not_drawn_as_it_stands)
{
    Stage stage(1.0f);
    REQUIRE(stage.ctx != nullptr && stage.ok);
    ImTypesetStyle style = code_style(15.0f);
    style.Face = ImTypesetFace_Code;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    REQUIRE(p != nullptr);
    const char* const ruler = "1234567890123456789012345678901234567890";
    CHECK(ImTypeset::SetParagraphText(p, ruler));
    CHECK(ImTypeset::LayoutParagraph(p, 1000.0f, &style));
    stage.begin();
    CHECK(ImTypeset::DrawParagraph(p, ImGui::GetWindowDrawList(), ImVec2(10.0f, 10.0f)));
    stage.end();
    const float fortieth = ImTypeset::GetParagraphCaretRect(p, 39).x;

    set_density(2.0f);
    stage.begin();
    ImDrawList* list = ImGui::GetWindowDrawList();
    const int had = list->VtxBuffer.Size;
    const bool drew = ImTypeset::DrawParagraph(p, list, ImVec2(10.0f, 10.0f));
    const int code = ImTypeset::GetLastError()->Code;
    const std::string said = ImTypeset::GetLastError()->Message;
    const int wrote = list->VtxBuffer.Size - had;
    // A paragraph whose text is measured by its font is the same on every
    // display and is drawn.
    ImTypesetStyle by_font = style;
    by_font.Advances = ImTypesetAdvances_Font;
    ImTypesetParagraph* q = ImTypeset::CreateParagraph();
    REQUIRE(q != nullptr);
    CHECK(ImTypeset::SetParagraphText(q, ruler));
    set_density(1.0f);
    CHECK(ImTypeset::LayoutParagraph(q, 1000.0f, &by_font));
    set_density(2.0f);
    CHECK(ImTypeset::DrawParagraph(q, list, ImVec2(10.0f, 80.0f)));
    stage.end();
    if (env_set("IMTYPESET_DEVICE_GRID_TABLE"))
        std::printf("    laid out at density 1 (40th character at %g units), drawn at density 2: "
                    "DrawParagraph answered %s, wrote %d vertices, said \"%s\"\n",
                    static_cast<double>(fortieth), drew ? "true" : "false", wrote, said.c_str());
    CHECK(!drew);
    CHECK_EQ(wrote, 0);
    CHECK_EQ(code, static_cast<int>(ImTypesetError_InvalidArgument));
    CHECK(said.find("LayoutParagraph") != std::string::npos);

    // Laid out again, it is the new display's and is drawn.
    CHECK(ImTypeset::LayoutParagraph(p, 1000.0f, &style));
    const double cell = cell_of(style);
    CHECK(std::fabs(static_cast<double>(ImTypeset::GetParagraphCaretRect(p, 39).x) * 2.0 -
                    39.0 * cell) < EXACT_PX);
    stage.begin();
    CHECK(ImTypeset::DrawParagraph(p, ImGui::GetWindowDrawList(), ImVec2(10.0f, 10.0f)));
    stage.end();
    ImTypeset::DestroyParagraph(p);
    ImTypeset::DestroyParagraph(q);
}

// --- Finding 4: asked while its ImGui context is away ----------------------------------

TEST(a_document_asked_without_its_imgui_context_answers_from_the_layout_it_has)
{
    Stage stage(1.0f);
    REQUIRE(stage.ctx != nullptr && stage.ok);
    const ImTypesetStyle style = code_style(16.0f);
    const std::string ruler = "1234567890123456789012345678901234567890";
    ImTypesetDocument* document = document_of(ruler, ImTypesetBlockKind_Code, 800.0f, style);
    REQUIRE(document != nullptr);
    const double cell = cell_of(style);
    const double on_device = caret_px(document, 39);
    CHECK(std::fabs(on_device - 39.0 * cell) < EXACT_PX);
    const ImTypesetDocumentStats laid = ImTypeset::GetDocumentStats(document);

    ImGuiContext* imgui = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(nullptr);
    const ImTypesetPosition at = {0, 39};
    const double away = static_cast<double>(ImTypeset::GetDocumentCaretRect(document, at).x);
    const ImTypesetDocumentStats asked = ImTypeset::GetDocumentStats(document);
    // One never laid out has nothing to answer from and is set by its fonts.
    ImTypesetDocument* fresh = ImTypeset::CreateDocument();
    REQUIRE(fresh != nullptr);
    ImTypesetBlock block;
    block.Kind = ImTypesetBlockKind_Code;
    block.TextEnd = static_cast<int>(ruler.size());
    ImTypesetDocumentContent content;
    content.Text = ruler.data();
    content.TextSize = static_cast<int>(ruler.size());
    content.Blocks = &block;
    content.BlockCount = 1;
    CHECK(ImTypeset::SetDocumentBlocks(fresh, &content));
    CHECK(ImTypeset::LayoutDocument(fresh, 800.0f, &style));
    const double fresh_away = static_cast<double>(ImTypeset::GetDocumentCaretRect(fresh, at).x);
#ifndef REPRODUCTION_AT_HEAD
    const bool fresh_said = ImTypeset::GetDocumentStats(fresh).MeasuredWithoutDevice;
#else
    const bool fresh_said = false;
#endif
    ImGui::SetCurrentContext(imgui);

    if (env_set("IMTYPESET_DEVICE_GRID_TABLE"))
        std::printf("    40th character: %g on the device; asked with no ImGui context %g "
                    "(%d blocks laid out for the question); a document first laid out there %g\n",
                    on_device, away, static_cast<int>(asked.BlocksLaidOut - laid.BlocksLaidOut),
                    fresh_away);
    CHECK_EQ(away, on_device);
    CHECK_EQ(asked.BlocksLaidOut, laid.BlocksLaidOut);
#ifndef REPRODUCTION_AT_HEAD
    CHECK(!asked.MeasuredWithoutDevice);
#endif
#ifndef REPRODUCTION_AT_HEAD
    CHECK(fresh_said);
#else
    (void)fresh_said;
#endif
    CHECK(std::fabs(fresh_away - on_device) > 1.0);
    // Back in its context each is on the device, and says so.
    CHECK_EQ(caret_px(document, 39), on_device);
    CHECK_EQ(ImTypeset::GetDocumentStats(document).BlocksLaidOut, laid.BlocksLaidOut);
    CHECK_EQ(caret_px(fresh, 39), on_device);
#ifndef REPRODUCTION_AT_HEAD
    CHECK(!ImTypeset::GetDocumentStats(fresh).MeasuredWithoutDevice);
#endif
    ImTypeset::DestroyDocument(document);
    ImTypeset::DestroyDocument(fresh);
}

// --- Finding 5: a wrapped line's continuation -------------------------------------------

TEST(a_wrapped_code_line_continues_a_whole_number_of_cells_in)
{
    for (const float density : {1.0f, 2.0f}) {
        for (const float size : {13.0f, 16.0f, 19.0f, 22.0f}) {
            CHECK_CONTEXT("density %g, pixel height %g", static_cast<double>(density),
                          static_cast<double>(size));
            Stage stage(density);
            REQUIRE(stage.ctx != nullptr && stage.ok);
            const ImTypesetStyle style = code_style(size);
            const double cell = cell_of(style);
            std::string line;
            for (int i = 0; i < 12; ++i)
                line += "word" + std::to_string(i) + " ";
            ImTypesetDocumentOptions options;
            options.CodeOverflow = ImTypesetCodeOverflow_Wrap;
            ImTypesetDocument* document =
                document_of(line, ImTypesetBlockKind_Code, 300.0f, style, 0, nullptr, &options);
            REQUIRE(document != nullptr);
            const int lines = ImTypeset::GetDocumentBlockGeometry(document, 0).LineCount;
            CHECK(lines >= 2);
            double worst = 0.0;
            double hang = 0.0;
            for (int l = 1; l < lines; ++l) {
                const ImTypesetLine got = ImTypeset::GetDocumentBlockLine(document, 0, l);
                const double start = caret_px(document, got.TextStart);
                hang = start / cell;
                worst = std::max(worst, std::fabs(hang - round_half_up(hang)) * cell);
            }
            if (env_set("IMTYPESET_DEVICE_GRID_TABLE"))
                std::printf("    d%g px%-2g cell %-2g: %d lines, a continuation starts %.4f cells "
                            "in (%.4f px off the grid)\n",
                            static_cast<double>(density), static_cast<double>(size), cell, lines,
                            hang, worst);
            CHECK(worst < EXACT_PX);
            CHECK(hang >= 1.0);
            ImTypeset::DestroyDocument(document);
        }
    }
}

// --- Ruling 6: the advance without the bitmap -------------------------------------------

TEST(dear_imguis_advance_only_route_answers_the_advance_of_the_glyph_it_would_bake)
{
    // ImFontBaked::GetCharAdvance against FindGlyph()->AdvanceX: in an entry
    // that rasterises on a first ask (the default below 128 px) and in one
    // told to load advances alone (LoadNoRenderOnLayout), at the densities
    // and in the two forms an entry is asked for: a size at a rasteriser
    // density, and, as the library asks above density 1, the size in
    // framebuffer pixels at a rasteriser density of 1.
    const uint32_t sample[] = {' ', 'a', 'i', 'M', 'W', '0', '|', 0xE9, 0x2014, 0x416, 0x2502};
    int compared = 0;
    int packed_by_find = 0;
    int packed_by_advance_only = 0;
    for (const float density : {1.0f, 1.5f, 2.0f, 3.0f}) {
        for (const float size : {13.0f, 16.0f, 22.0f, 24.0f}) {
            for (const bool in_framebuffer_pixels : {false, true}) {
                CHECK_CONTEXT("density %g, size %g, %s", static_cast<double>(density),
                              static_cast<double>(size),
                              in_framebuffer_pixels ? "size in framebuffer pixels"
                                                    : "size at a density");
                std::vector<float> by_glyph;
                for (int route = 0; route < 3; ++route) {
                    Stage stage(1.0f);
                    REQUIRE(stage.ctx != nullptr && stage.ok);
                    int index = 0;
                    for (const ImTypesetFace face : {ImTypesetFace_Code, ImTypesetFace_Regular}) {
                        ImFont* font = ImTypeset::GetFont(face);
                        REQUIRE(font != nullptr);
                        ImFontBaked* baked = in_framebuffer_pixels
                                                 ? font->GetFontBaked(size * density, 1.0f)
                                                 : font->GetFontBaked(size, density);
                        REQUIRE(baked != nullptr);
                        if (route == 2)
                            baked->LoadNoRenderOnLayout = 1;
                        for (const uint32_t c : sample) {
                            if (!font->IsGlyphInFont(static_cast<ImWchar>(c)))
                                continue;
                            float advance = 0.0f;
                            if (route == 0) {
                                advance = baked->FindGlyph(static_cast<ImWchar>(c))->AdvanceX;
                                by_glyph.push_back(advance);
                            } else {
                                advance = baked->GetCharAdvance(static_cast<ImWchar>(c));
                                REQUIRE(static_cast<size_t>(index) < by_glyph.size());
                                CHECK_EQ(advance, by_glyph[static_cast<size_t>(index)]);
                                // ImGui gives every entry its space when it
                                // makes the entry.
                                if (c != ' ')
                                    CHECK_EQ(baked->IsGlyphLoaded(static_cast<ImWchar>(c)),
                                             route == 1);
                                ++compared;
                            }
                            ++index;
                        }
                        if (route == 0)
                            packed_by_find += baked->Glyphs.Size;
                        if (route == 2)
                            packed_by_advance_only += baked->Glyphs.Size;
                    }
                }
            }
        }
    }
    CHECK(compared > 500);
    std::printf("    %d advances the same by both routes; glyphs packed: %d asked by FindGlyph, %d "
                "asked for the advance alone\n",
                compared, packed_by_find, packed_by_advance_only);
}

// --- Ruling 7: spaces that are cells ---------------------------------------------------

TEST(a_justified_line_stretches_its_prose_spaces_and_never_a_code_runs)
{
    Stage stage(1.0f);
    REQUIRE(stage.ctx != nullptr && stage.ok);
    ImTypesetStyle style;
    style.FontSize = 16.0f;
    style.Color = BLACK;
    const std::string before = "Stretch these words apart so the line is full and call ";
    const std::string code = "git diff --stat. HEAD~3 -- src";
    const std::string after = " before the next words of the sentence run on to a second line.";
    const std::string text = before + code + after;
    ImTypesetRun run;
    run.Start = static_cast<int>(before.size());
    run.End = static_cast<int>(before.size() + code.size());
    run.Face = ImTypesetFace_Code;
    const double cell = cell_of(style);
    int checked = 0;
    double widest_prose_space = 0.0;
    double narrowest_prose_space = 1.0e9;
    for (const float width : {420.0f, 455.0f, 500.0f, 530.0f, 610.0f}) {
        CHECK_CONTEXT("width %g", static_cast<double>(width));
        ImTypesetParagraph* p = ImTypeset::CreateParagraph();
        REQUIRE(p != nullptr);
        CHECK(ImTypeset::SetParagraphText(p, text.data(), text.data() + text.size(), &run, 1));
        CHECK(ImTypeset::LayoutParagraph(p, width, &style));
        CHECK(ImTypeset::GetParagraphLineCount(p) >= 2);
        const auto caret = [&](size_t byte) {
            return ImTypeset::GetParagraphCaretRect(p, static_cast<int>(byte));
        };
        int broken_at = 0;
        // Inside the code run, on one line: every step from a character to
        // the next is a cell, the spaces among them.
        for (size_t i = before.size(); i + 1 < before.size() + code.size(); ++i) {
            const ImVec4 a = caret(i);
            const ImVec4 b = caret(i + 1);
            if (a.y != b.y)
                continue;
            // A space a line was broken at is no width: the caret before it
            // is the start of the later line.
            if (text[i] == ' ' && a.x == b.x) {
                ++broken_at;
                continue;
            }
            CHECK_CONTEXT("code character %d ('%c')", static_cast<int>(i - before.size()), text[i]);
            CHECK(std::fabs(static_cast<double>(b.x - a.x) - cell) < EXACT_PX);
            ++checked;
        }
        // The prose spaces of the lines that are justified take the slack.
        for (size_t i = 0; i + 1 < before.size(); ++i) {
            if (text[i] != ' ')
                continue;
            const ImVec4 a = caret(i);
            const ImVec4 b = caret(i + 1);
            if (a.y != b.y)
                continue;
            widest_prose_space = std::max(widest_prose_space, static_cast<double>(b.x - a.x));
            narrowest_prose_space = std::min(narrowest_prose_space, static_cast<double>(b.x - a.x));
        }
        // Both margins flush on every line but the last.
        const int lines = ImTypeset::GetParagraphLineCount(p);
        CHECK(broken_at < lines);
        for (int l = 0; l + 1 < lines; ++l)
            CHECK(std::fabs(static_cast<double>(ImTypeset::GetParagraphLine(p, l).Max.x) -
                            static_cast<double>(width)) < 0.01);
        ImTypeset::DestroyParagraph(p);
    }
    CHECK(checked > 100);
    CHECK(widest_prose_space > narrowest_prose_space + 0.5);
    std::printf(
        "    %d steps inside the code run each one cell of %g px; prose spaces from %.2f to "
        "%.2f px\n",
        checked, cell, narrowest_prose_space, widest_prose_space);
}

TEST(a_paragraph_in_the_code_face_is_ragged_and_its_columns_agree_line_to_line)
{
    for (const float density : {1.0f, 2.0f}) {
        CHECK_CONTEXT("density %g", static_cast<double>(density));
        Stage stage(density);
        REQUIRE(stage.ctx != nullptr && stage.ok);
        ImTypesetStyle style;
        style.FontSize = 15.0f;
        style.Face = ImTypesetFace_Code;
        style.Color = BLACK;
        // Justify, the default: a face of one advance is not stretched.
        const std::string text =
            "A paragraph set in the code face. Its words wrap as prose wraps, and every "
            "character of every line stands a whole number of cells from the left margin, "
            "so that a column of one line is a column of the next. End.";
        const double cell = cell_of(style);
        ImTypesetParagraph* p = ImTypeset::CreateParagraph();
        REQUIRE(p != nullptr);
        CHECK(ImTypeset::SetParagraphText(p, text.data(), text.data() + text.size()));
        CHECK(ImTypeset::LayoutParagraph(p, 333.0f, &style));
        const int lines = ImTypeset::GetParagraphLineCount(p);
        CHECK(lines >= 4);
        double worst = 0.0;
        bool ragged = false;
        for (int l = 0; l < lines; ++l) {
            const ImTypesetLine line = ImTypeset::GetParagraphLine(p, l);
            for (int byte = line.TextStart; byte < line.TextEnd; ++byte) {
                const double column =
                    static_cast<double>(ImTypeset::GetParagraphCaretRect(p, byte).x) *
                    static_cast<double>(density) / cell;
                worst = std::max(worst, std::fabs(column - round_half_up(column)) * cell);
            }
            if (l + 1 < lines && static_cast<double>(line.Max.x) < 333.0 - 0.01)
                ragged = true;
        }
        if (env_set("IMTYPESET_DEVICE_GRID_TABLE"))
            std::printf("    d%g: %d lines, a character at most %.4f px from a whole cell, %s\n",
                        static_cast<double>(density), lines, worst,
                        ragged ? "ragged" : "both margins flush");
        CHECK(worst < EXACT_PX);
        CHECK(ragged);
        // Drawn there: the pens of two lines at one column are one x.
        stage.begin();
        ImDrawList* list = ImGui::GetWindowDrawList();
        const int v0 = list->VtxBuffer.Size;
        CHECK(ImTypeset::DrawParagraph(p, list, ImVec2(20.0f, 20.0f)));
        // The first frame loads every glyph the second is read by, in every
        // face the reading looks in, so that no texture coordinate moves
        // under the reading.
        for (const int v : glyph_quads(list, v0, list->VtxBuffer.Size)) {
            Drawn loaded;
            for (size_t i = 0; i < text.size(); ++i)
                if (text[i] != ' ' &&
                    identify(stage, style, list, v, static_cast<unsigned char>(text[i]), &loaded))
                    break;
        }
        stage.end();
        stage.begin();
        list = ImGui::GetWindowDrawList();
        const int v1 = list->VtxBuffer.Size;
        CHECK(ImTypeset::DrawParagraph(p, list, ImVec2(20.0f, 20.0f)));
        const std::vector<int> again = glyph_quads(list, v1, list->VtxBuffer.Size);
        size_t q = 0;
        double worst_pen = 0.0;
        for (size_t i = 0; i < text.size() && q < again.size(); ++i) {
            if (text[i] == ' ')
                continue;
            Drawn drawn;
            if (!identify(stage, style, list, again[q], static_cast<unsigned char>(text[i]),
                          &drawn)) {
                FAIL("the quad for character %d is not its glyph", static_cast<int>(i));
                break;
            }
            ++q;
            const double laid =
                (20.0 +
                 static_cast<double>(ImTypeset::GetParagraphCaretRect(p, static_cast<int>(i)).x)) *
                static_cast<double>(density);
            worst_pen = std::max(worst_pen, std::fabs(drawn.pen - laid));
        }
        stage.end();
        CHECK_EQ(q, again.size());
        CHECK(worst_pen < EXACT_PX);
        ImTypeset::DestroyParagraph(p);
    }
}

// --- The combinations a diff view is made of -------------------------------------------

namespace
{

// One character of a document to find among what a draw wrote.
struct Wanted {
    ImTypesetPosition at;
    uint32_t codepoint;
};

// The glyphs a draw wrote for `wanted`, in order: quads are taken in order,
// a quad that is not the next character's glyph belongs to a later one.
// Characters that draw nothing are left not found. Drawn twice, as in
// `drawn_rows`. `unmatched` is how many quads were no wanted character.
std::vector<Drawn> drawn_chars(Stage& stage, const ImTypesetStyle& style,
                               ImTypesetDocument* document, const ImVec2& pos,
                               const std::vector<Wanted>& wanted,
                               const ImTypesetDocumentDraw* draw = nullptr, float scale = 1.0f,
                               int* unmatched = nullptr, std::vector<ImVec4>* fills = nullptr,
                               std::vector<ImU32>* fill_colours = nullptr)
{
    std::vector<Drawn> out(wanted.size());
    for (int frame = 0; frame < 2; ++frame) {
        stage.begin();
        ImDrawList* list = ImGui::GetWindowDrawList();
        const int v0 = list->VtxBuffer.Size;
        if (!ImTypeset::DrawDocument(document, list, pos, draw))
            FAIL("DrawDocument: %s", ImTypeset::GetLastError()->Message);
        const std::vector<int> quads = glyph_quads(list, v0, list->VtxBuffer.Size);
        out.assign(wanted.size(), Drawn());
        size_t q = 0;
        for (size_t i = 0; i < wanted.size() && q < quads.size(); ++i) {
            if (wanted[i].codepoint == ' ' || wanted[i].codepoint == '\t')
                continue;
            if (identify(stage, style, list, quads[q], wanted[i].codepoint, &out[i], scale))
                ++q;
        }
        if (unmatched != nullptr)
            *unmatched = static_cast<int>(quads.size() - q);
        if (fills != nullptr) {
            fills->clear();
            if (fill_colours != nullptr)
                fill_colours->clear();
            const double density = density_now() > 1.0 ? density_now() : 1.0;
            for (int v = v0; v + 3 < list->VtxBuffer.Size; v += 4) {
                if (list->VtxBuffer[v].uv.x != list->VtxBuffer[v + 2].uv.x)
                    continue;
                fills->push_back(ImVec4(
                    static_cast<float>(static_cast<double>(list->VtxBuffer[v].pos.x) * density),
                    static_cast<float>(static_cast<double>(list->VtxBuffer[v].pos.y) * density),
                    static_cast<float>(static_cast<double>(list->VtxBuffer[v + 2].pos.x) * density),
                    static_cast<float>(static_cast<double>(list->VtxBuffer[v + 2].pos.y) *
                                       density)));
                if (fill_colours != nullptr)
                    fill_colours->push_back(list->VtxBuffer[v].col);
            }
        }
        stage.end();
    }
    return out;
}

// Where layout has a position, in framebuffer pixels on the page.
double laid_px(ImTypesetDocument* document, const ImTypesetPosition& at, const ImVec2& pos)
{
    const double density = density_now() > 1.0 ? density_now() : 1.0;
    return (static_cast<double>(pos.x) +
            static_cast<double>(ImTypeset::GetDocumentCaretRect(document, at).x)) *
           density;
}

std::vector<Wanted> wanted_of(const std::vector<RowSpec>& rows, const Text& text, int block = 0)
{
    std::vector<Wanted> wanted;
    for (size_t r = 0; r < rows.size(); ++r)
        for (size_t i = 0; i < rows[r].chars.size(); ++i)
            wanted.push_back(Wanted{{block, text.offsets[r][i]}, rows[r].chars[i].codepoint});
    return wanted;
}

std::vector<Wanted> wanted_ascii(const std::string& text, int block = 0, int from = 0)
{
    std::vector<Wanted> wanted;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\n')
            wanted.push_back(
                Wanted{{block, from + static_cast<int>(i)}, static_cast<unsigned char>(text[i])});
    }
    return wanted;
}

} // namespace

TEST(the_grid_holds_at_every_density_size_and_origin_and_rows_agree_at_every_column)
{
    // Densities 1, 1.5, 2 and 3; pixel heights 13 to 24; the block's origin
    // on a framebuffer pixel and a quarter, a half and three quarters of one
    // off it. Layout's answers are a whole number of cells from the row's
    // start at every origin, since they do not know the origin. A glyph is
    // drawn on a whole framebuffer pixel, at most half a pixel from where
    // layout has it, and EVERY glyph of the block by the same amount: the
    // drawn grid is the laid-out one moved as a whole, so rows agree at every
    // column and a step is the cell.
    const std::vector<RowSpec> rows = diff_rows();
    const Text text = text_of(rows);
    const std::vector<Wanted> wanted = wanted_of(rows, text);
    double worst_layout = 0.0;
    double worst_spread = 0.0;
    double worst_shift = 0.0;
    int pages = 0;
    for (const float density : {1.0f, 1.5f, 2.0f, 3.0f}) {
        Stage stage(density);
        REQUIRE(stage.ctx != nullptr && stage.ok);
        for (float size = 13.0f; size <= 24.0f; size += 1.0f) {
            const ImTypesetStyle style = code_style(size);
            const double cell = cell_of(style);
            ImTypesetDocumentOptions options;
            options.CodeOverflow = ImTypesetCodeOverflow_Grow;
            ImTypesetDocument* document = document_of(text.bytes, ImTypesetBlockKind_Code, 1400.0f,
                                                      style, 0, nullptr, &options);
            REQUIRE(document != nullptr);
            for (size_t r = 0; r < rows.size(); ++r) {
                const double start = caret_px(document, text.offsets[r][0]);
                for (size_t i = 0; i < rows[r].chars.size(); ++i) {
                    const double column = (caret_px(document, text.offsets[r][i]) - start) / cell;
                    worst_layout =
                        std::max(worst_layout, std::fabs(column - round_half_up(column)) * cell);
                }
            }
            for (const float fraction : {0.0f, 0.25f, 0.5f, 0.75f}) {
                CHECK_CONTEXT("density %g, pixel height %g, origin %g of a framebuffer pixel off",
                              static_cast<double>(density), static_cast<double>(size),
                              static_cast<double>(fraction));
                // In units: a fraction of a framebuffer pixel is that over
                // the density.
                const ImVec2 pos(20.0f + fraction / density, 20.0f);
                int unmatched = 0;
                const std::vector<Drawn> drawn =
                    drawn_chars(stage, style, document, pos, wanted, nullptr, 1.0f, &unmatched);
                CHECK_EQ(unmatched, 0);
                double low = 1.0e9;
                double high = -1.0e9;
                int found = 0;
                for (size_t i = 0; i < wanted.size(); ++i) {
                    if (!drawn[i].found)
                        continue;
                    ++found;
                    CHECK(std::fabs(drawn[i].pen - round_half_up(drawn[i].pen)) < 1.0 / 64.0);
                    const double off = drawn[i].pen - laid_px(document, wanted[i].at, pos);
                    low = std::min(low, off);
                    high = std::max(high, off);
                }
                CHECK(found > 40);
                worst_spread = std::max(worst_spread, high - low);
                worst_shift = std::max(worst_shift, std::max(std::fabs(low), std::fabs(high)));
                CHECK(high - low < 2.0 * EXACT_PX);
                CHECK(std::max(std::fabs(low), std::fabs(high)) <= 0.5 + EXACT_PX);
                if (fraction == 0.0f)
                    CHECK(std::max(std::fabs(low), std::fabs(high)) < EXACT_PX);
                ++pages;
            }
            ImTypeset::DestroyDocument(document);
        }
    }
    CHECK(worst_layout < EXACT_PX);
    std::printf("    %d pages (4 densities, 12 sizes, 4 origins), %s: a laid-out character at most "
                "%.5f px from a whole cell; the glyphs of a block at most %.5f px apart in how far "
                "each is from its place; a block at most %.3f px from its place\n",
                pages, LOADER_HINTS ? "FreeType" : "stb_truetype", worst_layout, worst_spread,
                worst_shift);
}

TEST(a_rows_rectangles_carets_hit_tests_and_copy_agree_with_its_cells)
{
    const std::vector<RowSpec> rows = diff_rows();
    const Text text = text_of(rows);
    for (const float density : {1.0f, 2.0f}) {
        CHECK_CONTEXT("density %g", static_cast<double>(density));
        Stage stage(density);
        REQUIRE(stage.ctx != nullptr && stage.ok);
        const ImTypesetStyle style = code_style(16.0f);
        const double cell = cell_of(style);
        ImTypesetDocumentOptions options;
        options.CodeOverflow = ImTypesetCodeOverflow_Grow;
        ImTypesetDocument* document =
            document_of(text.bytes, ImTypesetBlockKind_Code, 1400.0f, style, 0, nullptr, &options);
        REQUIRE(document != nullptr);
        int hits = 0;
        for (size_t r = 0; r < rows.size(); ++r) {
            CHECK_CONTEXT("row \"%s\"", rows[r].name);
            // The whole row's rectangle is its cells added up.
            ImTypesetDocumentRange whole;
            whole.Start.Block = whole.End.Block = 0;
            whole.Start.Offset = text.offsets[r].front();
            whole.End.Offset = text.offsets[r].back();
            ImVector<ImVec4> rects;
            REQUIRE(ImTypeset::GetDocumentRangeRects(document, whole, &rects) >= 1);
            const double wide = static_cast<double>(rects[0].z - rects[0].x) * density / cell;
            CHECK(std::fabs(wide - round_half_up(wide)) * cell < EXACT_PX);
            const float y = (rects[0].y + rects[0].w) * 0.5f;
            // A point a quarter across a character's cells is the
            // character's, and three quarters across is the caret after it:
            // the next position that has a place of its own.
            for (size_t i = 0; i < rows[r].chars.size(); ++i) {
                ImTypesetDocumentRange one;
                one.Start.Block = one.End.Block = 0;
                one.Start.Offset = text.offsets[r][i];
                one.End.Offset = text.offsets[r][i + 1];
                ImVector<ImVec4> own;
                if (ImTypeset::GetDocumentRangeRects(document, one, &own) < 1)
                    continue;
                const float x0 = own[0].x;
                const float x1 = own[0].z;
                if (!(x1 > x0))
                    continue;
                CHECK_CONTEXT("character %d, U+%04X", static_cast<int>(i),
                              rows[r].chars[i].codepoint);
                const ImTypesetDocumentHit before =
                    ImTypeset::HitTestDocument(document, ImVec2(x0 + (x1 - x0) * 0.25f, y));
                const ImTypesetDocumentHit after =
                    ImTypeset::HitTestDocument(document, ImVec2(x0 + (x1 - x0) * 0.75f, y));
                // The caret answered stands at the cell's edge, whichever of
                // the positions that share the edge it names (a character of
                // no width has the place of the one after it).
                const ImVec4 at_before = ImTypeset::GetDocumentCaretRect(document, before.Position);
                const ImVec4 at_after = ImTypeset::GetDocumentCaretRect(document, after.Position);
                CHECK(std::fabs(static_cast<double>(at_before.x - x0)) * density < EXACT_PX);
                CHECK(before.Position.Offset <= text.offsets[r][i]);
                ++hits;
                // Not asked of a row's last character, where the position
                // after it is the row's end and its caret the next row's
                // start; nor of a tab, whose several spaces share its byte,
                // so that only the right half of its last space is nearer
                // the position after it (the document layer's rule for a
                // tab, older than the grid and not changed by it).
                if (i + 1 == rows[r].chars.size() || rows[r].chars[i].codepoint == '\t')
                    continue;
                CHECK(std::fabs(static_cast<double>(at_after.x - x1)) * density < EXACT_PX);
                CHECK(after.Position.Offset >= text.offsets[r][i + 1]);
                ++hits;
            }
            // A copy of the row is the row as written: the tab a tab, the
            // character no face has itself and not what stands in for it.
            ImGuiTextBuffer copied;
            ImTypeset::AppendDocumentRangeText(document, whole, &copied);
            const std::string written =
                text.bytes.substr(static_cast<size_t>(whole.Start.Offset),
                                  static_cast<size_t>(whole.End.Offset - whole.Start.Offset));
            CHECK_EQ(std::string(copied.c_str()), written);
        }
        CHECK(hits > 120);
        ImTypeset::DestroyDocument(document);
    }
}

namespace
{

// A file of `lines` lines as a diff shows one: code, and every so often a
// line holding what the code face lacks. Line `target` is `special`.
std::string diff_file(int lines, int target, const std::string& special,
                      std::vector<int>* line_starts)
{
    std::string text;
    std::string guest;
    utf8(guest, 0x416);
    for (int i = 0; i < lines; ++i) {
        line_starts->push_back(static_cast<int>(text.size()));
        if (i == target) {
            text += special;
        } else if (i % 97 == 13) {
            text += "\t// " + guest + guest + " \xE2\x94\x82 note " + std::to_string(i);
        } else {
            char line[96];
            std::snprintf(line, sizeof line, "    value[%d] = compute(%d, state) + offset_%d;", i,
                          i * 7, i % 13);
            text += line;
        }
        if (i + 1 < lines)
            text += '\n';
    }
    return text;
}

} // namespace

TEST(a_diff_views_block_of_five_thousand_lines_with_marks_and_a_selection_is_on_the_grid)
{
    // The shape advtext's diff view has: ONE Code block, every line exactly
    // its line height, Ink, Behind and Under marks and a selection over
    // character ranges, scrolled to the middle.
    const int LINES = 5000;
    const int TARGET = 2503;
    std::string special = "ab ";
    utf8(special, 0x416); // two cells, from the regular face
    special += "\tcd ";
    utf8(special, 0x2502);
    special += " x";
    utf8(special, 0x200B);
    special += "y = KLMNOPQRSTUVWXYZ;";
    for (const float density : {1.0f, 1.5f, 2.0f}) {
        CHECK_CONTEXT("density %g", static_cast<double>(density));
        Stage stage(density);
        REQUIRE(stage.ctx != nullptr && stage.ok);
        ImTypesetStyle style = code_style(15.0f);
        style.SelectionColor = IM_COL32(1, 2, 250, 255);
        const double cell = cell_of(style);
        std::vector<int> starts;
        const std::string text = diff_file(LINES, TARGET, special, &starts);
        ImTypesetDocumentOptions options;
        options.CodeOverflow = ImTypesetCodeOverflow_Grow;
        ImTypesetDocument* document =
            document_of(text, ImTypesetBlockKind_Code, 1200.0f, style,
                        ImTypesetBlockFlags_FixedLineHeight, nullptr, &options);
        REQUIRE(document != nullptr);
        const ImTypesetBlockGeometry geometry = ImTypeset::GetDocumentBlockGeometry(document, 0);
        CHECK_EQ(geometry.LineCount, LINES);
        // Rows found by arithmetic: line n's top is n line heights down.
        const ImTypesetLine first = ImTypeset::GetDocumentBlockLine(document, 0, 0);
        const ImTypesetLine second = ImTypeset::GetDocumentBlockLine(document, 0, 1);
        const double line_height = static_cast<double>(second.Min.y - first.Min.y);
        CHECK(line_height > 10.0);
        for (const int n : {2, 97, 1000, TARGET, LINES - 1}) {
            const ImTypesetLine line = ImTypeset::GetDocumentBlockLine(document, 0, n);
            CHECK(std::fabs(static_cast<double>(line.Min.y - first.Min.y) -
                            line_height * static_cast<double>(n)) < 0.01);
            CHECK_EQ(line.TextStart, starts[static_cast<size_t>(n)]);
        }
        const int base = starts[TARGET];
        const auto at = [&](const char* piece) {
            const size_t found = special.find(piece);
            if (found == std::string::npos)
                FAIL("the target line has no \"%s\"", piece);
            return base + static_cast<int>(found);
        };
        // The marks: a Behind over "cd │ x" (which holds the tab's end, a
        // box-drawing character and the zero-width space), an Under over
        // "KLMNO", Ink over "PQR", and the selection over "TUVW". Single-
        // character Ink marks in colours of their own find the pens at the
        // ranges' ends.
        const ImU32 BEHIND = IM_COL32(250, 220, 10, 255);
        const ImU32 UNDER = IM_COL32(10, 200, 30, 255);
        const ImU32 INK = IM_COL32(200, 20, 20, 255);
        std::vector<ImTypesetMark> marks;
        const auto mark = [&](int start, int end, ImU32 colour, ImTypesetMarkPlace place) {
            ImTypesetMark m;
            m.Range.Start.Offset = start;
            m.Range.End.Offset = end;
            m.Color = colour;
            m.Place = place;
            marks.push_back(m);
        };
        mark(at("cd"), at("y ="), BEHIND, ImTypesetMarkPlace_Behind);
        mark(at("KLMNO"), at("PQR"), UNDER, ImTypesetMarkPlace_Under);
        mark(at("PQR"), at("STUV"), INK, ImTypesetMarkPlace_Ink);
        ImTypesetDocumentDraw draw;
        draw.Marks = marks.data();
        draw.MarkCount = static_cast<int>(marks.size());
        draw.Selection.Start.Offset = at("TUVW");
        draw.Selection.End.Offset = at("XYZ");

        // Scrolled so that the target line is in the middle of the window,
        // on a whole framebuffer pixel.
        const double target_top =
            static_cast<double>(ImTypeset::GetDocumentBlockLine(document, 0, TARGET).Min.y);
        const ImVec2 pos(30.0f, static_cast<float>(std::floor((300.0 - target_top) *
                                                              static_cast<double>(density)) /
                                                   static_cast<double>(density)));
        // The lines the draw wrote glyphs for, found from where their quads
        // are: a draw passes over the lines outside the clip rectangle, and
        // in a block of exact line heights a glyph's bitmap starts inside
        // its own line's box.
        int first_shown = LINES;
        int last_shown = 0;
        {
            stage.begin();
            ImDrawList* list = ImGui::GetWindowDrawList();
            const int v0 = list->VtxBuffer.Size;
            CHECK(ImTypeset::DrawDocument(document, list, pos, &draw));
            for (const int v : glyph_quads(list, v0, list->VtxBuffer.Size)) {
                const double y = static_cast<double>(list->VtxBuffer[v].pos.y) -
                                 static_cast<double>(pos.y) - static_cast<double>(first.Min.y);
                const int line = static_cast<int>(std::floor(y / line_height));
                first_shown = std::min(first_shown, line);
                last_shown = std::max(last_shown, line + 1);
            }
            stage.end();
        }
        REQUIRE(first_shown < TARGET && last_shown > TARGET + 1 && last_shown <= LINES);
        // A window 900 units tall shows some forty-five lines of the five
        // thousand, and no more than that were given glyphs.
        CHECK(last_shown - first_shown < 60);
        std::vector<Wanted> wanted;
        std::vector<uint32_t> codepoints;
        std::vector<uint32_t> offsets;
        decode_utf8(text, codepoints, &offsets);
        for (size_t i = 0; i < codepoints.size(); ++i) {
            const int byte = static_cast<int>(offsets[i]);
            if (byte < starts[static_cast<size_t>(first_shown)] ||
                byte >= starts[static_cast<size_t>(last_shown)] || codepoints[i] == '\n')
                continue;
            wanted.push_back(Wanted{{0, byte}, codepoints[i]});
        }
        int unmatched = 0;
        std::vector<ImVec4> fills;
        std::vector<ImU32> colours;
        const std::vector<Drawn> drawn = drawn_chars(stage, style, document, pos, wanted, &draw,
                                                     1.0f, &unmatched, &fills, &colours);
        const ImTypesetDocumentStats stats = ImTypeset::GetDocumentStats(document);
        CHECK_EQ(stats.Blocks, 1);

        // Every glyph drawn is one of the wanted lines' and stands where
        // layout has it; the target line's all drew.
        int found = 0;
        int inked = 0;
        double worst = 0.0;
        double worst_column = 0.0;
        const auto pen_of = [&](int byte) -> double {
            for (size_t i = 0; i < wanted.size(); ++i)
                if (wanted[i].at.Offset == byte && drawn[i].found)
                    return drawn[i].pen;
            FAIL("no glyph was drawn for byte %d of the target line", byte - base);
            return 0.0;
        };
        for (size_t i = 0; i < wanted.size(); ++i) {
            if (!drawn[i].found)
                continue;
            ++found;
            const double laid = laid_px(document, wanted[i].at, pos);
            worst = std::max(worst, std::fabs(drawn[i].pen - laid));
            const double column =
                (drawn[i].pen - static_cast<double>(pos.x) * static_cast<double>(density)) / cell;
            worst_column = std::max(worst_column, std::fabs(column - round_half_up(column)) * cell);
            const int byte = wanted[i].at.Offset;
            const bool in_ink = byte >= at("PQR") && byte < at("STUV");
            if (in_ink)
                ++inked;
            CHECK_EQ(drawn[i].colour == INK, in_ink);
        }
        CHECK_EQ(inked, 3);
        CHECK(found > 500);
        CHECK(worst < EXACT_PX);
        // The block starts on a framebuffer pixel that is a whole number of
        // them from the window's edge, so its columns are whole cells from
        // `pos` on the screen too, for every line and every face drawn.
        CHECK(worst_column < EXACT_PX);

        // The fills: each a rectangle whose edges are the pens of the first
        // character marked and of the first after the range.
        const auto fill_of = [&](ImU32 colour, ImVec4* out) {
            int count = 0;
            for (size_t i = 0; i < fills.size(); ++i) {
                if (colours[i] == colour) {
                    *out = fills[i];
                    ++count;
                }
            }
            return count;
        };
        ImVec4 behind, under, selection;
        CHECK_EQ(fill_of(BEHIND, &behind), 1);
        CHECK_EQ(fill_of(UNDER, &under), 1);
        CHECK_EQ(fill_of(style.SelectionColor, &selection), 1);
        CHECK(std::fabs(static_cast<double>(behind.x) - pen_of(at("cd"))) < EXACT_PX);
        CHECK(std::fabs(static_cast<double>(behind.z) - pen_of(at("y ="))) < EXACT_PX);
        CHECK(std::fabs(static_cast<double>(under.x) - pen_of(at("KLMNO"))) < EXACT_PX);
        CHECK(std::fabs(static_cast<double>(under.z) - pen_of(at("PQR"))) < EXACT_PX);
        CHECK(std::fabs(static_cast<double>(selection.x) - pen_of(at("TUVW"))) < EXACT_PX);
        CHECK(std::fabs(static_cast<double>(selection.z) - pen_of(at("XYZ"))) < EXACT_PX);
        // The Behind mark and the selection are a line tall, on the line.
        const double line_px = line_height * static_cast<double>(density);
        CHECK(std::fabs(static_cast<double>(behind.w - behind.y) - line_px) <= 1.0);
        CHECK(std::fabs(static_cast<double>(selection.w - selection.y) - line_px) <= 1.0);
        // A point in a marked cell is that character's.
        const float y = (static_cast<float>(target_top) + static_cast<float>(line_height) * 0.5f);
        for (const char* piece : {"cd", "KLMNO", "PQR", "TUVW"}) {
            const int byte = at(piece);
            const float x =
                static_cast<float>((pen_of(byte) + cell * 0.25) / static_cast<double>(density) -
                                   static_cast<double>(pos.x));
            const ImTypesetDocumentHit hit = ImTypeset::HitTestDocument(document, ImVec2(x, y));
            CHECK_EQ(hit.Position.Offset, byte);
            CHECK_EQ(hit.Line, TARGET);
        }
        std::printf("    density %g: %d lines, %d glyphs of the %d shown lines read, each %.5f px "
                    "at most from layout and %.5f from a whole cell of %g px on the screen; %d "
                    "quads that were no shown character\n",
                    static_cast<double>(density), LINES, found, last_shown - first_shown, worst,
                    worst_column, cell, unmatched);
        ImTypeset::DestroyDocument(document);
    }
}

namespace
{

// The pens of the glyphs a draw wrote in one colour, in order, each found as
// the glyph of the next of `codepoints` that has ink. For text among which
// other things are drawn (a formula's glyphs): an Ink mark in a colour of its
// own picks its characters out.
std::vector<Drawn> drawn_in_colour(Stage& stage, const ImTypesetStyle& style,
                                   ImTypesetDocument* document, const ImVec2& pos,
                                   const ImTypesetDocumentDraw* draw, ImU32 colour,
                                   const std::string& characters)
{
    std::vector<Drawn> out(characters.size());
    for (int frame = 0; frame < 2; ++frame) {
        stage.begin();
        ImDrawList* list = ImGui::GetWindowDrawList();
        const int v0 = list->VtxBuffer.Size;
        if (!ImTypeset::DrawDocument(document, list, pos, draw))
            FAIL("DrawDocument: %s", ImTypeset::GetLastError()->Message);
        std::vector<int> quads;
        for (const int v : glyph_quads(list, v0, list->VtxBuffer.Size))
            if (list->VtxBuffer[v].col == colour)
                quads.push_back(v);
        out.assign(characters.size(), Drawn());
        size_t q = 0;
        for (size_t i = 0; i < characters.size() && q < quads.size(); ++i) {
            if (characters[i] == ' ')
                continue;
            if (identify(stage, style, list, quads[q], static_cast<unsigned char>(characters[i]),
                         &out[i]))
                ++q;
        }
        if (frame == 1 && q != quads.size())
            FAIL("%d quads in the mark's colour are not its characters",
                 static_cast<int>(quads.size() - q));
        stage.end();
    }
    return out;
}

} // namespace

TEST(code_in_a_tables_cells_keeps_its_cells_beside_cells_measured_by_their_font)
{
    for (const float density : {1.0f, 2.0f}) {
        CHECK_CONTEXT("density %g", static_cast<double>(density));
        Stage stage(density);
        REQUIRE(stage.ctx != nullptr && stage.ok);
        ImTypesetStyle style;
        style.FontSize = 16.0f;
        style.Color = BLACK;
        const double cell = cell_of(style);
        // A prose column, by its font, and a code column, on the device.
        const char* const cells[3][2] = {
            {"name", "what it is"},
            {"alpha", "x = f(a, b) # ok"},
            {"beta gamma", "yy = g(c)"},
        };
        std::string text;
        std::vector<ImTypesetCell> table_cells;
        std::vector<ImTypesetRun> runs;
        int code_start[3] = {};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 2; ++c) {
                if (c != 0)
                    text += '\t';
                ImTypesetCell one;
                one.TextStart = static_cast<int>(text.size());
                text += cells[r][c];
                one.TextEnd = static_cast<int>(text.size());
                one.Flags = c == 0 ? ImTypesetCellFlags_RowStart : 0;
                table_cells.push_back(one);
                if (c == 1 && r > 0) {
                    ImTypesetRun run;
                    run.Start = one.TextStart;
                    run.End = one.TextEnd;
                    run.Face = ImTypesetFace_Code;
                    run.Flags = ImTypesetRunFlags_NoBreak;
                    runs.push_back(run);
                    code_start[r] = one.TextStart;
                }
            }
            if (r != 2)
                text += '\n';
        }
        for (const ImTypesetBlockFlags flags :
             {ImTypesetBlockFlags(ImTypesetBlockFlags_TableHeader),
              ImTypesetBlockFlags(ImTypesetBlockFlags_TableHeader | ImTypesetBlockFlags_TableHeld |
                                  ImTypesetBlockFlags_FixedLineHeight)}) {
            CHECK_CONTEXT("block flags %d", flags);
            ImTypesetDocument* document = ImTypeset::CreateDocument();
            REQUIRE(document != nullptr);
            ImTypesetBlock block;
            block.Kind = ImTypesetBlockKind_Table;
            block.Flags = flags;
            block.TextEnd = static_cast<int>(text.size());
            block.RunCount = static_cast<int>(runs.size());
            block.CellCount = static_cast<int>(table_cells.size());
            ImTypesetDocumentContent content;
            content.Text = text.data();
            content.TextSize = static_cast<int>(text.size());
            content.Blocks = &block;
            content.BlockCount = 1;
            content.Runs = runs.data();
            content.RunCount = static_cast<int>(runs.size());
            content.Cells = table_cells.data();
            content.CellCount = static_cast<int>(table_cells.size());
            REQUIRE(ImTypeset::SetDocumentBlocks(document, &content));
            REQUIRE(ImTypeset::LayoutDocument(document, 700.0f, &style));
            const ImVec2 pos(20.0f, 20.0f);
            double starts[3] = {};
            for (int r = 1; r < 3; ++r) {
                CHECK_CONTEXT("row %d", r);
                const std::string code = cells[r][1];
                // Layout: every step of the code cell is one cell, its
                // spaces among them.
                for (size_t i = 0; i + 1 < code.size(); ++i) {
                    const ImTypesetPosition a = {0, code_start[r] + static_cast<int>(i)};
                    const ImTypesetPosition b = {0, code_start[r] + static_cast<int>(i) + 1};
                    const double step =
                        static_cast<double>(ImTypeset::GetDocumentCaretRect(document, b).x -
                                            ImTypeset::GetDocumentCaretRect(document, a).x) *
                        static_cast<double>(density);
                    CHECK(std::fabs(step - cell) < EXACT_PX);
                }
                // Drawn: on whole pixels, the cell's glyphs all the same
                // fraction of a pixel from their places, at most half.
                ImTypesetMark mark;
                mark.Range.Start.Offset = code_start[r];
                mark.Range.End.Offset = code_start[r] + static_cast<int>(code.size());
                mark.Color = IM_COL32(200, 30, 30, 255);
                mark.Place = ImTypesetMarkPlace_Ink;
                ImTypesetDocumentDraw draw;
                draw.Marks = &mark;
                draw.MarkCount = 1;
                const std::vector<Drawn> drawn =
                    drawn_in_colour(stage, style, document, pos, &draw, mark.Color, code);
                double low = 1.0e9, high = -1.0e9;
                for (size_t i = 0; i < code.size(); ++i) {
                    if (!drawn[i].found)
                        continue;
                    CHECK_EQ(drawn[i].face, static_cast<int>(ImTypesetFace_Code));
                    const ImTypesetPosition at = {0, code_start[r] + static_cast<int>(i)};
                    const double off = drawn[i].pen - laid_px(document, at, pos);
                    low = std::min(low, off);
                    high = std::max(high, off);
                }
                CHECK(high - low < 2.0 * EXACT_PX);
                CHECK(std::max(std::fabs(low), std::fabs(high)) <= 0.5 + EXACT_PX);
                const ImTypesetPosition first = {0, code_start[r]};
                starts[r] = laid_px(document, first, pos) + low;
                if (env_set("IMTYPESET_DEVICE_GRID_TABLE"))
                    std::printf("    d%g flags %d row %d: the code cell is laid out at %.4f px and "
                                "its glyphs are %.4f to %.4f px from their places\n",
                                static_cast<double>(density), flags, r,
                                laid_px(document, first, pos), low, high);
            }
            // The two code cells of the column start at one x on the screen:
            // their columns agree down the table.
            CHECK(std::fabs(starts[1] - starts[2]) < EXACT_PX);
            // The code column wants its widest cell: sixteen cells and the
            // padding either side, half an em each.
            const ImTypesetTableColumnGeometry column =
                ImTypeset::GetDocumentTableColumn(document, 0, 1);
            ImTypesetFontMetrics regular;
            REQUIRE(ImTypeset::GetFontMetrics(ImTypesetFace_Regular, 1000.0f, &regular));
            const double em =
                16.0 * static_cast<double>(regular.EmSize) / static_cast<double>(regular.FontSize);
            const double text_wide =
                (static_cast<double>(column.Natural) - em) * static_cast<double>(density);
            // "what it is", the header, is prose and narrower than the code.
            CHECK(std::fabs(text_wide - 16.0 * cell) < 0.01);
            ImTypeset::DestroyDocument(document);
        }
    }
}

TEST(code_in_columns_is_on_whole_pixels_and_steps_by_the_cell_in_every_column)
{
    for (const float density : {1.0f, 2.0f}) {
        CHECK_CONTEXT("density %g", static_cast<double>(density));
        Stage stage(density);
        REQUIRE(stage.ctx != nullptr && stage.ok);
        const ImTypesetStyle style = code_style(15.0f);
        const double cell = cell_of(style);
        std::string text;
        for (int i = 0; i < 60; ++i)
            text += "line " + std::to_string(i) + " = f(" + std::to_string(i * 3) + ");" +
                    (i == 59 ? "" : "\n");
        ImTypesetDocument* document = document_of(text, ImTypesetBlockKind_Code, 331.0f, style,
                                                  ImTypesetBlockFlags_FixedLineHeight);
        REQUIRE(document != nullptr);
        ImTypesetColumns columns;
        columns.Count = 2;
        columns.Flags = ImTypesetColumnFlags_Balance;
        columns.Gutter = 17.5f;
        REQUIRE(ImTypeset::SetDocumentColumns(document, &columns));
        CHECK_EQ(ImTypeset::GetDocumentColumnCount(document), 2);
        const ImVec2 pos(20.0f, 20.0f);
        const std::vector<Wanted> wanted = wanted_ascii(text);
        int unmatched = 0;
        const std::vector<Drawn> drawn =
            drawn_chars(stage, style, document, pos, wanted, nullptr, 1.0f, &unmatched);
        CHECK_EQ(unmatched, 0);
        // Per column: one fraction of a pixel for all of its glyphs.
        double low[2] = {1.0e9, 1.0e9};
        double high[2] = {-1.0e9, -1.0e9};
        int in_column[2] = {0, 0};
        const ImTypesetColumnGeometry second = ImTypeset::GetDocumentColumn(document, 1);
        REQUIRE(second.Exists);
        for (size_t i = 0; i < wanted.size(); ++i) {
            if (!drawn[i].found)
                continue;
            CHECK(std::fabs(drawn[i].pen - round_half_up(drawn[i].pen)) < 1.0 / 64.0);
            const double laid = laid_px(document, wanted[i].at, pos);
            const int c =
                ImTypeset::GetDocumentCaretRect(document, wanted[i].at).x >= second.Min.x ? 1 : 0;
            ++in_column[c];
            low[c] = std::min(low[c], drawn[i].pen - laid);
            high[c] = std::max(high[c], drawn[i].pen - laid);
        }
        for (int c = 0; c < 2; ++c) {
            CHECK_CONTEXT("column %d", c);
            CHECK(in_column[c] > 100);
            CHECK(high[c] - low[c] < 2.0 * EXACT_PX);
            CHECK(std::max(std::fabs(low[c]), std::fabs(high[c])) <= 0.5 + EXACT_PX);
        }
        (void)cell;
        ImTypeset::DestroyDocument(document);
    }
}

TEST(prose_on_the_device_is_on_whole_pixels_whatever_it_holds_and_a_formula_keeps_its_own_width)
{
    for (const float density : {1.0f, 2.0f}) {
        CHECK_CONTEXT("density %g", static_cast<double>(density));
        Stage stage(density);
        REQUIRE(stage.ctx != nullptr && stage.ok);
        ImTypesetStyle style;
        style.FontSize = 17.0f;
        style.Align = ImTypesetAlign_Left;
        style.Color = BLACK;
        style.Advances = ImTypesetAdvances_Device;
        // A face that is not fixed-pitch promises no grid; it promises whole
        // framebuffer pixels, for a character its face lacks and nothing
        // draws (U+200D, U+001B), for one borrowed from the math face
        // (U+1D400) and for one no face has (U+4E2D).
        std::string text = "plain na";
        utf8(text, 0xEF);
        text += "ve a";
        utf8(text, 0x200D);
        text += "b c";
        utf8(text, 0x1B);
        text += "d ";
        utf8(text, 0x1D400);
        utf8(text, 0x4E2D);
        text += " end";
        ImTypesetDocument* document =
            document_of(text, ImTypesetBlockKind_Paragraph, 900.0f, style);
        REQUIRE(document != nullptr);
        double worst = 0.0;
        for (int byte = 0; byte <= static_cast<int>(text.size()); ++byte) {
            const double x = caret_px(document, byte);
            worst = std::max(worst, std::fabs(x - round_half_up(x)));
        }
        // To the rounding of a pixel to scaled points: under 1/256 px a line.
        CHECK(worst < EXACT_PX);
        ImTypeset::DestroyDocument(document);

        // An inline formula in text on the device: the formula is as wide as
        // its font makes it, so what follows it on the line is not a whole
        // number of pixels from the line's start; it is on whole pixels on
        // the screen, each word less than a pixel from its place and every
        // step inside a word the laid-out one.
        const std::string before = "Area ";
        const std::string formula = "$a^2 + b^2$";
        const std::string after = " then more words follow it";
        const std::string with_math = before + formula + after;
        ImTypesetDocument* math = document_of(with_math, ImTypesetBlockKind_Paragraph, 900.0f,
                                              style, ImTypesetBlockFlags_Math);
        REQUIRE(math != nullptr);
        CHECK(ImTypeset::GetDocumentBlockError(math, 0) == nullptr ||
              ImTypeset::GetDocumentBlockError(math, 0)->Code == ImTypesetError_None);
        ImTypesetMark mark;
        mark.Range.Start.Offset = static_cast<int>(before.size() + formula.size());
        mark.Range.End.Offset = static_cast<int>(with_math.size());
        mark.Color = IM_COL32(200, 30, 30, 255);
        mark.Place = ImTypesetMarkPlace_Ink;
        ImTypesetDocumentDraw draw;
        draw.Marks = &mark;
        draw.MarkCount = 1;
        const ImVec2 pos(20.0f, 20.0f);
        const std::vector<Drawn> drawn =
            drawn_in_colour(stage, style, math, pos, &draw, mark.Color, after);
        int found = 0;
        double word_off = 0.0;
        for (size_t i = 0; i < after.size(); ++i) {
            if (!drawn[i].found)
                continue;
            ++found;
            CHECK(std::fabs(drawn[i].pen - round_half_up(drawn[i].pen)) < 1.0 / 64.0);
            const ImTypesetPosition at = {0, mark.Range.Start.Offset + static_cast<int>(i)};
            const double off = drawn[i].pen - laid_px(math, at, pos);
            // A word of a face that is not on a grid is placed from the word
            // before it, less than a pixel from its own place (D48, D50).
            CHECK(std::fabs(off) < 1.0);
            // The first glyph of a word sets the word's fraction.
            if (i == 0 || after[i - 1] == ' ')
                word_off = off;
            CHECK(std::fabs(off - word_off) < EXACT_PX);
        }
        CHECK_EQ(found, 21);
        ImTypeset::DestroyDocument(math);
    }
}

TEST(an_ideograph_borrowed_from_a_face_that_has_it_takes_two_cells)
{
    // No font in tests/data holds a CJK ideograph, and none is copied into
    // the tree: a font installed on this machine is read in place, as the
    // regular face, and the test says so when there is none.
    const char* const candidates[] = {
        "/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf",
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
    };
    const char* path = nullptr;
    for (const char* candidate : candidates) {
        std::vector<uint8_t> bytes;
        if (golden::read_file(candidate, bytes) && !bytes.empty()) {
            path = candidate;
            break;
        }
    }
    if (path == nullptr) {
        std::printf("    SKIPPED: no font with CJK ideographs is installed where this test looks "
                    "(IPA Gothic, Droid Sans Fallback, WenQuanYi Zen Hei)\n");
        return;
    }
    for (const float density : {1.0f, 2.0f}) {
        for (const float size : {14.0f, 16.0f, 19.0f}) {
            CHECK_CONTEXT("%s, density %g, pixel height %g", path, static_cast<double>(density),
                          static_cast<double>(size));
            Stage stage(density, path);
            if (!stage.ok) {
                std::printf("    SKIPPED: %s was refused as the regular face\n", path);
                return;
            }
            const ImTypesetStyle style = code_style(size);
            const double cell = cell_of(style);
            const std::vector<RowSpec> rows = {
                {"ideographs",
                 {{'a', ONE}, {0x4E2D, GUEST}, {0x6587, GUEST}, {'b', ONE}, {'|', ONE}}},
                {"the same columns in ascii",
                 {{'a', ONE},
                  {'1', ONE},
                  {'2', ONE},
                  {'3', ONE},
                  {'4', ONE},
                  {'b', ONE},
                  {'|', ONE}}},
                {"a tab after one", {{0x4E2D, GUEST}, {'\t', TAB}, {'x', ONE}, {'|', ONE}}},
            };
            const Text text = text_of(rows);
            ImTypesetDocument* document =
                document_of(text.bytes, ImTypesetBlockKind_Code, 1400.0f, style);
            REQUIRE(document != nullptr);
            std::vector<std::vector<Drawn>> drawn;
            const std::vector<RowMeasured> measured = measure_rows(
                stage, style, rows, text, document, ImVec2(20.0f, 20.0f), cell, &drawn);
            CHECK(drawn[0][1].found && drawn[0][2].found);
            CHECK(drawn[0][1].face == ImTypesetFace_Regular);
            CHECK(std::fabs(measured[0].cells[1] - 2.0) * cell < EXACT_PX);
            CHECK(std::fabs(measured[0].cells[2] - 2.0) * cell < EXACT_PX);
            CHECK(std::fabs(measured[2].cells[1] - 2.0) * cell < EXACT_PX);
            for (size_t r = 0; r < rows.size(); ++r) {
                CHECK(measured[r].worst_grid < EXACT_PX);
                CHECK(measured[r].worst_off < EXACT_PX);
            }
            // 'b' stands at one column in the row of ideographs and in the
            // row of digits.
            CHECK(drawn[0][3].found && drawn[1][5].found);
            CHECK_EQ(drawn[0][3].pen, drawn[1][5].pen);
            if (density == 1.0f && size == 16.0f)
                std::printf("    %s: U+4E2D has an advance of %g px in its entry and takes %g "
                            "cells of %g px\n",
                            path, drawn[0][1].advance, measured[0].cells[1], cell);
            ImTypeset::DestroyDocument(document);
        }
    }
}

TEST(pictures_for_whoever_sets_IMTYPESET_DEVICE_GRID_SHOTS)
{
    const char* const directory = std::getenv("IMTYPESET_DEVICE_GRID_SHOTS");
    if (directory == nullptr || directory[0] == '\0') {
        std::printf("    SKIPPED: IMTYPESET_DEVICE_GRID_SHOTS is not set\n");
        return;
    }
    constexpr int SHOT_WIDTH = 760;
    constexpr int SHOT_HEIGHT = 640;
    const ImU32 GREY = IM_COL32(110, 110, 110, 255);
    const std::vector<RowSpec> rows = diff_rows();
    const Text text = text_of(rows);
    for (const float density : {1.0f, 1.5f, 2.0f, 3.0f}) {
        Stage stage(density);
        REQUIRE(stage.ctx != nullptr && stage.ok);
        ImGui::GetIO().DisplaySize =
            ImVec2(static_cast<float>(SHOT_WIDTH), static_cast<float>(SHOT_HEIGHT));
        soft_raster::Renderer renderer;
        renderer.init();
        ImTypesetStyle style = code_style(15.0f);
        style.SelectionColor = IM_COL32(150, 190, 255, 255);
        const float cell = static_cast<float>(cell_of(style)) / density;
        ImTypesetDocumentOptions options;
        options.CodeOverflow = ImTypesetCodeOverflow_Grow;
        ImTypesetDocument* block =
            document_of(text.bytes, ImTypesetBlockKind_Code, 330.0f, style,
                        ImTypesetBlockFlags_FixedLineHeight, nullptr, &options);
        REQUIRE(block != nullptr);
        // Marks over the rows that hold what the code face lacks.
        std::vector<ImTypesetMark> marks;
        const auto mark = [&](size_t row, size_t from, size_t to, ImU32 colour,
                              ImTypesetMarkPlace place) {
            ImTypesetMark m;
            m.Range.Start.Offset = text.offsets[row][from];
            m.Range.End.Offset = text.offsets[row][to];
            m.Color = colour;
            m.Place = place;
            marks.push_back(m);
        };
        mark(1, 1, 4, IM_COL32(255, 235, 120, 255), ImTypesetMarkPlace_Behind);
        mark(5, 0, 2, IM_COL32(255, 200, 200, 255), ImTypesetMarkPlace_Behind);
        mark(9, 0, 4, IM_COL32(190, 240, 190, 255), ImTypesetMarkPlace_Behind);
        mark(8, 1, 6, IM_COL32(200, 30, 30, 255), ImTypesetMarkPlace_Ink);
        mark(13, 0, 4, IM_COL32(20, 140, 40, 255), ImTypesetMarkPlace_Under);
        mark(16, 1, 6, IM_COL32(20, 140, 40, 255), ImTypesetMarkPlace_Under);
        ImTypesetDocumentDraw draw;
        draw.Marks = marks.data();
        draw.MarkCount = static_cast<int>(marks.size());
        draw.Selection.Start.Offset = text.offsets[6][1];
        draw.Selection.End.Offset = text.offsets[7][2];

        // A paragraph in the code face, justified by its style and so ragged.
        ImTypesetStyle code_para = style;
        code_para.Face = ImTypesetFace_Code;
        code_para.Align = ImTypesetAlign_Justify;
        const std::string para =
            "A paragraph in the code face wraps as prose does and every character of every "
            "line stands on a cell, so columns agree down the lines. End.";
        ImTypesetDocument* paragraph =
            document_of(para, ImTypesetBlockKind_Paragraph, 330.0f, code_para);
        // Prose with a code run and a formula, justified.
        ImTypesetStyle prose = style;
        prose.Align = ImTypesetAlign_Justify;
        const std::string lead = "Justified prose keeps its code ";
        const std::string code = "a = b + c;  d";
        const std::string rest = " on cells and the formula $x^2 + y^2$ its own width, while "
                                 "the other spaces take up the slack of each line.";
        const std::string mixed = lead + code + rest;
        ImTypesetDocument* inline_code = ImTypeset::CreateDocument();
        {
            ImTypesetBlock b;
            b.Kind = ImTypesetBlockKind_Paragraph;
            b.Flags = ImTypesetBlockFlags_Math;
            b.TextEnd = static_cast<int>(mixed.size());
            b.RunCount = 1;
            ImTypesetRun run;
            run.Start = static_cast<int>(lead.size());
            run.End = static_cast<int>(lead.size() + code.size());
            run.Face = ImTypesetFace_Code;
            run.Color = IM_COL32(150, 30, 120, 255);
            ImTypesetDocumentContent content;
            content.Text = mixed.data();
            content.TextSize = static_cast<int>(mixed.size());
            content.Blocks = &b;
            content.BlockCount = 1;
            content.Runs = &run;
            content.RunCount = 1;
            CHECK(ImTypeset::SetDocumentBlocks(inline_code, &content));
            CHECK(ImTypeset::LayoutDocument(inline_code, 330.0f, &prose));
        }
        // A table: a prose column and a code column.
        const char* const cells[4][2] = {{"name", "what it sets"},
                                         {"alpha", "x = f(a, b)"},
                                         {"beta gamma", "yy = g(c)"},
                                         {"delta", "zzz = h()"}};
        std::string table_text;
        std::vector<ImTypesetCell> table_cells;
        std::vector<ImTypesetRun> table_runs;
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 2; ++c) {
                if (c != 0)
                    table_text += '\t';
                ImTypesetCell one;
                one.TextStart = static_cast<int>(table_text.size());
                table_text += cells[r][c];
                one.TextEnd = static_cast<int>(table_text.size());
                one.Flags = c == 0 ? ImTypesetCellFlags_RowStart : 0;
                table_cells.push_back(one);
                if (c == 1 && r > 0) {
                    ImTypesetRun run;
                    run.Start = one.TextStart;
                    run.End = one.TextEnd;
                    run.Face = ImTypesetFace_Code;
                    table_runs.push_back(run);
                }
            }
            if (r != 3)
                table_text += '\n';
        }
        ImTypesetDocument* table = ImTypeset::CreateDocument();
        {
            ImTypesetBlock b;
            b.Kind = ImTypesetBlockKind_Table;
            b.Flags = ImTypesetBlockFlags_TableHeader | ImTypesetBlockFlags_TableHeld;
            b.TextEnd = static_cast<int>(table_text.size());
            b.RunCount = static_cast<int>(table_runs.size());
            b.CellCount = static_cast<int>(table_cells.size());
            ImTypesetDocumentContent content;
            content.Text = table_text.data();
            content.TextSize = static_cast<int>(table_text.size());
            content.Blocks = &b;
            content.BlockCount = 1;
            content.Runs = table_runs.data();
            content.RunCount = static_cast<int>(table_runs.size());
            content.Cells = table_cells.data();
            content.CellCount = static_cast<int>(table_cells.size());
            CHECK(ImTypeset::SetDocumentBlocks(table, &content));
            CHECK(ImTypeset::LayoutDocument(table, 330.0f, &prose));
        }
        // Code in two columns.
        std::string listing;
        for (int i = 0; i < 16; ++i)
            listing += "row " + std::to_string(i) + ": f(" + std::to_string(i * 3) + ")" +
                       (i == 15 ? "" : "\n");
        ImTypesetDocument* columns = document_of(listing, ImTypesetBlockKind_Code, 330.0f, style,
                                                 ImTypesetBlockFlags_FixedLineHeight);
        ImTypesetColumns two;
        two.Count = 2;
        two.Flags = ImTypesetColumnFlags_Balance;
        CHECK(ImTypeset::SetDocumentColumns(columns, &two));
        REQUIRE(paragraph != nullptr && inline_code != nullptr && table != nullptr &&
                columns != nullptr);

        const float left = 12.0f;
        const float right = 400.0f;
        soft_raster::Image image;
        for (int frame = 0; frame < 2; ++frame) {
            stage.begin();
            ImDrawList* list = ImGui::GetWindowDrawList();
            const auto hairlines = [&](float x0, float y0, float y1, int count) {
                for (int column = 0; column <= count; ++column) {
                    const float x = x0 + cell * static_cast<float>(column);
                    list->AddRectFilled(ImVec2(x, y0), ImVec2(x + 1.0f / density, y1),
                                        column % 4 == 0 ? IM_COL32(255, 140, 140, 255)
                                                        : IM_COL32(255, 225, 225, 255));
                }
            };
            list->AddText(ImVec2(left, 4.0f), GREY,
                          "one Code block, exact line heights, marks, a selection");
            hairlines(left, 20.0f, 400.0f, 28);
            CHECK(ImTypeset::DrawDocument(block, list, ImVec2(left, 22.0f), &draw));
            list->AddText(ImVec2(left, 408.0f), GREY, "a paragraph in the code face");
            hairlines(left, 424.0f, 520.0f, 44);
            CHECK(ImTypeset::DrawDocument(paragraph, list, ImVec2(left, 426.0f)));
            list->AddText(ImVec2(left, 528.0f), GREY, "code in two columns");
            CHECK(ImTypeset::DrawDocument(columns, list, ImVec2(left, 544.0f)));
            list->AddText(ImVec2(right, 4.0f), GREY, "a table: prose by its font, code on cells");
            CHECK(ImTypeset::DrawDocument(table, list, ImVec2(right, 22.0f)));
            list->AddText(ImVec2(right, 130.0f), GREY, "justified prose, a code run, a formula");
            CHECK(ImTypeset::DrawDocument(inline_code, list, ImVec2(right, 148.0f)));
            stage.end();
            image = soft_raster::Image(static_cast<int>(static_cast<float>(SHOT_WIDTH) * density),
                                       static_cast<int>(static_cast<float>(SHOT_HEIGHT) * density),
                                       IM_COL32(255, 255, 255, 255));
            renderer.render(ImGui::GetDrawData(), image);
        }
        char path[1024];
        const char* const loader = LOADER_HINTS ? "freetype" : "stb";
        std::snprintf(path, sizeof path, "%s/device-grid-%s-%gx.png", directory, loader,
                      static_cast<double>(density));
        if (!soft_raster::write_png(path, image))
            FAIL("could not write %s", path);
        // The block of rows enlarged by repeating pixels, to the same size
        // on the page at every density.
        const int zoom = density == 1.0f ? 6 : (density == 1.5f ? 4 : (density == 2.0f ? 3 : 2));
        const int x1 = std::min(image.width, static_cast<int>(210.0f * density));
        const int y0 = static_cast<int>(18.0f * density);
        const int y1 = std::min(image.height, static_cast<int>(232.0f * density));
        soft_raster::Image large(x1 * zoom, (y1 - y0) * zoom, IM_COL32(255, 255, 255, 255));
        for (int y = 0; y < large.height; ++y)
            for (int x = 0; x < large.width; ++x)
                std::memcpy(&large.rgba[(static_cast<size_t>(y) * static_cast<size_t>(large.width) +
                                         static_cast<size_t>(x)) *
                                        4u],
                            &image.rgba[(static_cast<size_t>(y0 + y / zoom) *
                                             static_cast<size_t>(image.width) +
                                         static_cast<size_t>(x / zoom)) *
                                        4u],
                            4);
        std::snprintf(path, sizeof path, "%s/device-grid-%s-%gx-rows.png", directory, loader,
                      static_cast<double>(density));
        if (!soft_raster::write_png(path, large))
            FAIL("could not write %s", path);
        std::printf("    wrote %s, cell %g units\n", path, static_cast<double>(cell));
        ImTypeset::DestroyDocument(block);
        ImTypeset::DestroyDocument(paragraph);
        ImTypeset::DestroyDocument(inline_code);
        ImTypeset::DestroyDocument(table);
        ImTypeset::DestroyDocument(columns);
        renderer.shutdown();
    }
}

// --- Ruling 8: a host from before the structs grew --------------------------------------

TEST(a_host_compiled_against_0_3_0_is_refused_by_its_version_and_by_its_struct_sizes)
{
    // 0.4.0 appended ImTypesetAdvances Advances to ImTypesetStyle and to
    // ImTypesetBlockStyle: a 0.3.0 host's are four bytes shorter.
    const auto first_part = [](const char* version, size_t style) {
        return ImTypeset::DebugCheckVersionAndDataLayout(
            version, sizeof(ImWchar), sizeof(ImDrawVert), sizeof(ImDrawIdx), style,
            sizeof(ImTypesetRun), sizeof(ImTypesetTextRange), sizeof(ImTypesetLine),
            sizeof(ImTypesetHit), sizeof(ImTypesetFormula), sizeof(ImTypesetStats),
            sizeof(ImTypesetErrorInfo));
    };
    CHECK_EQ(std::string(IMTYPESET_VERSION), std::string("0.4.0"));
    CHECK(!first_part("0.3.0", sizeof(ImTypesetStyle) - 4));
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_VersionMismatch));
    CHECK(std::strstr(ImTypeset::GetLastError()->Message, "0.3.0") != nullptr);
    // One that names this version and has the old struct.
    CHECK(!first_part(IMTYPESET_VERSION, sizeof(ImTypesetStyle) - 4));
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_VersionMismatch));
    CHECK(std::strstr(ImTypeset::GetLastError()->Message, "ImTypesetStyle") != nullptr);
    CHECK(!ImTypeset::DebugCheckDocumentDataLayout(
        sizeof(ImTypesetBlock), sizeof(ImTypesetBlockStyle) - 4, sizeof(ImTypesetContainer),
        sizeof(ImTypesetDocumentContent), sizeof(ImTypesetDocumentOptions),
        sizeof(ImTypesetPosition), sizeof(ImTypesetDocumentRange), sizeof(ImTypesetMark),
        sizeof(ImTypesetDocumentDraw), sizeof(ImTypesetDocumentHit), sizeof(ImTypesetBlockGeometry),
        sizeof(ImTypesetDocumentStats)));
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_VersionMismatch));
    CHECK(std::strstr(ImTypeset::GetLastError()->Message, "ImTypesetBlockStyle") != nullptr);
    // And a host that did not look gets no context.
    ImGui::CreateContext();
    CHECK(ImTypeset::CreateContext() == nullptr);
    CHECK(IMTYPESET_CHECKVERSION());
    ImGui::DestroyContext();
}
