// A display whose framebuffer is denser than its window coordinates (macOS
// Retina: io.DisplayFramebufferScale 2): what the library draws there, measured
// through the software renderer, which projects by ImDrawData::FramebufferScale
// as the GPU backends do.
//
// The host's own text is the yardstick: one face added to the atlas the way a
// runner adds it (at its nominal size, with a default ImFontConfig) and drawn
// with ImGui::Text in the same window.
//
// IMTYPESET_HIDPI_OUT=<directory> writes each scale's frame there as a PNG and
// prints the table of measurements; IMTYPESET_HIDPI_TAG names the files.

#include "golden.h"

#include "context.h"
#include "draw.h"
#include "imtypeset.h"
#include "support/soft_raster.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
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
    {ImTypesetFace_BoldItalic, "Roboto-BoldItalic.ttf"},
    {ImTypesetFace_Code, "Inconsolata-Medium.ttf"},
    {ImTypesetFace_Math, "latinmodern-math.otf"},
};

constexpr float SCALES[] = {1.0f, 1.5f, 2.0f, 3.0f};
constexpr int WIDTH = 560;
constexpr int HEIGHT = 150;
constexpr float FONT_PX = 16.0f;
constexpr ImU32 WHITE = IM_COL32(255, 255, 255, 255);
constexpr ImU32 BLACK = IM_COL32(0, 0, 0, 255);

const char* const PROSE = "Sphinx of black quartz, judge my vow: the five boxing wizards jump.";
const char* const FORMULA = "The roots are $x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}$, as ever.";

// An ImGui context on a display of the given density, the software renderer,
// the host's face as ImGui's default font, and the library's six.
struct Stage {
    explicit Stage(float scale)
        : density(scale)
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(static_cast<float>(WIDTH), static_cast<float>(HEIGHT));
        io.DisplayFramebufferScale = ImVec2(scale, scale);
        io.DeltaTime = 1.0f / 60.0f;
        renderer.init();
        const std::string host = golden::data_path("fonts/Roboto-Regular.ttf");
        host_font = io.Fonts->AddFontFromFileTTF(host.c_str(), FONT_PX);
        if (host_font == nullptr)
            FAIL("the host's font was not added");
        ImGui::GetStyle().FontSizeBase = FONT_PX;
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
        // The pixel height ImGui's own text has, so the two are one size.
        ImTypeset::GetStyle().FontSize = FONT_PX;
        ImTypeset::GetStyle().Align = ImTypesetAlign_Left;
    }
    ~Stage()
    {
        ImTypeset::DestroyContext();
        renderer.shutdown();
        ImGui::DestroyContext();
    }
    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;

    // A window over the whole display with nothing of its own drawn in it.
    void begin()
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, BLACK);
        ImGui::Begin("hidpi", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    }

    soft_raster::Image end()
    {
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        ImGui::Render();
        soft_raster::Image image(static_cast<int>(static_cast<float>(WIDTH) * density),
                                 static_cast<int>(static_cast<float>(HEIGHT) * density), WHITE);
        renderer.render(ImGui::GetDrawData(), image);
        return image;
    }

    float density;
    soft_raster::Renderer renderer;
    ImFont* host_font = nullptr;
    ImTypesetContext* ctx = nullptr;
};

ImTypesetParagraph* paragraph_of(const char* text, float width, ImTypesetTextFlags flags = 0)
{
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    if (p == nullptr)
        return nullptr;
    ImTypeset::SetParagraphText(p, text, nullptr, nullptr, 0, flags);
    if (!ImTypeset::LayoutParagraph(p, width))
        FAIL("LayoutParagraph: %s", ImTypeset::GetParagraphError(p)->Message);
    return p;
}

// What the textured quads among the vertices [first, end) of a draw list say
// about how they will meet the framebuffer.
struct Quads {
    int count = 0;
    // Texels of the atlas across a quad, over the device pixels it covers:
    // 1 is a bitmap drawn texel for pixel, 1 / density one made at density 1
    // and stretched.
    double texels_per_pixel_min = 0.0;
    double texels_per_pixel_max = 0.0;
    // Quads with a corner that is not on a device pixel, to 1/64 of one (a
    // third of a pixel is not a float; 1/64 is far inside what float32 keeps
    // of a coordinate on any display).
    int off_grid = 0;
    // Rectangles (untextured: the white pixel) with an edge off the grid.
    int rects = 0;
    int rects_off_grid = 0;
};

bool on_grid(float logical, double density)
{
    const double device = static_cast<double>(logical) * density;
    return std::fabs(device - std::floor(device + 0.5)) <= 1.0 / 64.0;
}

Quads quads_of(const ImDrawList* list, int first, int end, double density)
{
    Quads q;
    const ImTextureData* tex = ImGui::GetIO().Fonts->TexData;
    for (int v = first; v + 3 < end; v += 4) {
        const ImDrawVert& a = list->VtxBuffer[v];
        const ImDrawVert& c = list->VtxBuffer[v + 2];
        const bool aligned = on_grid(a.pos.x, density) && on_grid(a.pos.y, density) &&
                             on_grid(c.pos.x, density) && on_grid(c.pos.y, density);
        const double texels_x = static_cast<double>(c.uv.x - a.uv.x) * tex->Width;
        const double texels_y = static_cast<double>(c.uv.y - a.uv.y) * tex->Height;
        if (texels_x == 0.0 && texels_y == 0.0) {
            ++q.rects;
            q.rects_off_grid += aligned ? 0 : 1;
            continue;
        }
        const double pixels_x = static_cast<double>(c.pos.x - a.pos.x) * density;
        const double pixels_y = static_cast<double>(c.pos.y - a.pos.y) * density;
        for (const double ratio : {texels_x / pixels_x, texels_y / pixels_y}) {
            if (q.count == 0 || ratio < q.texels_per_pixel_min)
                q.texels_per_pixel_min = ratio;
            if (q.count == 0 || ratio > q.texels_per_pixel_max)
                q.texels_per_pixel_max = ratio;
            ++q.count;
        }
        q.off_grid += aligned ? 0 : 1;
    }
    q.count /= 2;
    return q;
}

// Black ink on white in the rows [top, bottom) of the display, in its own
// coordinates.
struct Ink {
    // The ink in logical square pixels: the same text at any density covers
    // the same area.
    double area = 0.0;
    // The share of that ink that lies in pixels at least 98 % black. A stroke
    // rasterised at the framebuffer's density has a solid core as soon as it
    // is two device pixels wide; one stretched from a coarser bitmap keeps
    // the soft edges it was made with.
    double solid = 0.0;
};

Ink ink_of(const soft_raster::Image& image, float top, float bottom, float density)
{
    Ink ink;
    double sum = 0.0;
    double solid = 0.0;
    const int y0 = static_cast<int>(top * density);
    const int y1 = static_cast<int>(bottom * density);
    for (int y = y0; y < y1 && y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const double dark = 255.0 - static_cast<double>(image.pixel(x, y)[0]);
            sum += dark;
            if (dark >= 250.0)
                solid += dark;
        }
    }
    ink.area = sum / 255.0 / static_cast<double>(density * density);
    ink.solid = sum > 0.0 ? solid / sum : 0.0;
    return ink;
}

struct Measured {
    Quads host, prose, formula;
    Ink host_ink, prose_ink, formula_ink;
    soft_raster::Image image;
};

// One display: the host's line at y = 12, the library's prose at y = 44 and a
// line with a formula at y = 84. Drawn twice, so that the frame measured adds
// nothing to the atlas and the texture it names is the one its UVs were
// written for.
Measured measure(Stage& stage)
{
    Measured m;
    ImTypesetParagraph* prose = paragraph_of(PROSE, static_cast<float>(WIDTH) - 24.0f);
    ImTypesetParagraph* formula =
        paragraph_of(FORMULA, static_cast<float>(WIDTH) - 24.0f, ImTypesetTextFlags_Math);
    if (prose == nullptr || formula == nullptr)
        return m;
    for (int frame = 0; frame < 2; ++frame) {
        stage.begin();
        ImDrawList* list = ImGui::GetWindowDrawList();
        const int v0 = list->VtxBuffer.Size;
        ImGui::SetCursorPos(ImVec2(12.0f, 12.0f));
        ImGui::TextUnformatted(PROSE);
        const int v1 = list->VtxBuffer.Size;
        if (!ImTypeset::DrawParagraph(prose, list, ImVec2(12.0f, 44.0f)))
            FAIL("DrawParagraph: %s", ImTypeset::GetLastError()->Message);
        const int v2 = list->VtxBuffer.Size;
        if (!ImTypeset::DrawParagraph(formula, list, ImVec2(12.0f, 84.0f)))
            FAIL("DrawParagraph: %s", ImTypeset::GetLastError()->Message);
        const int v3 = list->VtxBuffer.Size;
        const double density = static_cast<double>(stage.density);
        m.host = quads_of(list, v0, v1, density);
        m.prose = quads_of(list, v1, v2, density);
        m.formula = quads_of(list, v2, v3, density);
        m.image = stage.end();
    }
    m.host_ink = ink_of(m.image, 8.0f, 36.0f, stage.density);
    m.prose_ink = ink_of(m.image, 40.0f, 70.0f, stage.density);
    m.formula_ink = ink_of(m.image, 72.0f, 130.0f, stage.density);
    ImTypeset::DestroyParagraph(prose);
    ImTypeset::DestroyParagraph(formula);
    return m;
}

void print_row(float scale, const char* what, const Quads& q, const Ink& ink)
{
    std::printf("  %.1f  %-8s quads %3d  texels/device px %.3f..%.3f  off the device grid %3d  "
                "rects %d (off %d)  ink %.1f px^2  solid %.1f %%\n",
                static_cast<double>(scale), what, q.count, q.texels_per_pixel_min,
                q.texels_per_pixel_max, q.off_grid, q.rects, q.rects_off_grid, ink.area,
                ink.solid * 100.0);
}

// A binder that asks ImGui nothing: every key is bound, every glyph is there,
// and every key is baked at the size and ascent it was made with, which are in
// the pixels placement is done in.
class FixedBinder : public FontBinder
{
public:
    FixedBinder(float size, float ascent)
        : _size(size)
        , _ascent(ascent)
    {
    }
    void begin() override {}
    Binding bind(uint32_t, FontKey) override { return Binding::BOUND; }
    bool has_glyph(uint32_t, uint32_t, uint32_t) override { return true; }
    bool bake(uint32_t, FontKey, float* size, float* ascent) override
    {
        *size = _size;
        *ascent = _ascent;
        return true;
    }

private:
    float _size;
    float _ascent;
};

const char* const LONG_PROSE =
    "It is a truth universally acknowledged, that a single man in possession of a good fortune, "
    "must be in want of a wife. However little known the feelings or views of such a man may be "
    "on his first entering a neighbourhood, this truth is so well fixed in the minds of the "
    "surrounding families, that he is considered as the rightful property of some one or other "
    "of their daughters. My dear Mr. Bennet, said his lady to him one day, have you heard that "
    "Netherfield Park is let at last? Mr. Bennet replied that he had not. But it is, returned "
    "she; for Mrs. Long has just been here, and she told me all about it.";

const char* const LONG_MATH =
    "With $a \\ne 0$ the roots of $ax^2 + bx + c = 0$ are "
    "$x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}$, and their sum is $-\\frac{b}{a}$ while "
    "their product is $\\frac{c}{a}$; so $\\sqrt{\\frac{c}{a}}$ is their geometric mean and "
    "$\\frac{1}{2}\\left(-\\frac{b}{a}\\right)$ the arithmetic one.";

bool whole(float value)
{
    return static_cast<double>(value) == std::floor(static_cast<double>(value));
}

// The commands of `p` placed twice under the product's rules: at `density`
// with the layout's pixels to the inch, and at density 1 with the origin and
// the pixels to the inch both multiplied by the density, which is the same
// layout drawn on a display whose units ARE its framebuffer pixels. `device`
// receives the second, whose numbers are framebuffer pixels outright.
void place_both(const ImTypesetParagraph* p, double density, bool debug, std::vector<DrawOp>& units,
                std::vector<DrawOp>& device)
{
    DrawRun run;
    run.cmds = p->cmds.data();
    run.count = p->cmds.size();
    run.ink = BLACK;
    FixedBinder binder(static_cast<float>(std::floor(18.0 * density + 0.5)),
                       static_cast<float>(std::floor(14.0 * density + 0.5)));
    Drawer drawer;

    DrawArgs args;
    args.rules = DrawRules::product();
    args.debug = debug;
    args.origin = ImVec2(12.0f, 44.0f);
    args.px_per_inch = p->layout_style.px_per_inch;
    args.density = density;
    const DrawStats first = drawer.place(&run, 1, args, binder);
    CHECK_EQ(first.missing, 0);
    units = drawer.ops();

    args.origin = ImVec2(static_cast<float>(12.0 * density), static_cast<float>(44.0 * density));
    args.px_per_inch = p->layout_style.px_per_inch * density;
    args.density = 1.0;
    drawer.place(&run, 1, args, binder);
    device = drawer.ops();
}

} // namespace

TEST(the_host_s_own_text_is_baked_at_the_framebuffer_s_density)
{
    // The yardstick: what ImGui does for a font it is given at its nominal
    // size, by itself, on a backend with ImGuiBackendFlags_RendererHasTextures.
    for (const float scale : SCALES) {
        CHECK_CONTEXT("density %.1f", static_cast<double>(scale));
        Stage stage(scale);
        REQUIRE(stage.ctx != nullptr);
        const Measured m = measure(stage);
        REQUIRE(m.host.count > 40);
        // Never fewer texels than framebuffer pixels: not a bitmap of density
        // 1 stretched. Under stb_truetype ImGui oversamples a font with a
        // default configuration two to one across, so there it is more.
        CHECK_NEAR(m.host.texels_per_pixel_min, 1.0, 1e-3);
#ifdef IMGUI_ENABLE_FREETYPE
        CHECK_NEAR(m.host.texels_per_pixel_max, 1.0, 1e-3);
#else
        CHECK(m.host.texels_per_pixel_max <= 2.0 + 1e-3);
#endif
    }
}

TEST(glyphs_and_rules_meet_the_framebuffer_pixel_for_pixel_at_every_density)
{
    for (const float scale : SCALES) {
        CHECK_CONTEXT("density %.1f", static_cast<double>(scale));
        Stage stage(scale);
        REQUIRE(stage.ctx != nullptr);
        const Measured m = measure(stage);
        for (const Quads* q : {&m.prose, &m.formula}) {
            REQUIRE(q->count > 20);
            // A texel of the atlas to a pixel of the framebuffer, both ways:
            // nothing is resampled.
            CHECK_NEAR(q->texels_per_pixel_min, 1.0, 1e-3);
            CHECK_NEAR(q->texels_per_pixel_max, 1.0, 1e-3);
            CHECK_EQ(q->off_grid, 0);
            CHECK_EQ(q->rects_off_grid, 0);
        }
        // The fraction's bar and the radical's.
        CHECK_EQ(m.formula.rects, 2);
#ifdef IMGUI_ENABLE_FREETYPE
        // The same face at the same size as the host's line beside it: the
        // same bitmaps, so the same ink, and as much of it solid.
        CHECK_NEAR(m.prose_ink.area, m.host_ink.area, m.host_ink.area * 0.01);
        CHECK_NEAR(m.prose_ink.solid, m.host_ink.solid, 0.02);
#else
        // Not compared under stb_truetype: the host's line is oversampled
        // and set at fractions of a pixel there, and this library's is not,
        // so the two are different bitmaps of one outline.
        if (scale == SCALES[0])
            std::printf("  skipped under stb_truetype: the ink of the host's line against ours\n");
        // What does hold under either rasteriser: denser, the strokes gain
        // a solid core.
        if (scale >= 2.0f)
            CHECK(m.prose_ink.solid > 0.5);
#endif
    }
}

TEST(a_selection_and_the_debug_overlay_are_on_the_framebuffer_s_grid)
{
    for (const float scale : SCALES) {
        CHECK_CONTEXT("density %.1f", static_cast<double>(scale));
        Stage stage(scale);
        REQUIRE(stage.ctx != nullptr);
        ImTypesetParagraph* p = paragraph_of(LONG_PROSE, 300.0f);
        REQUIRE(p != nullptr);
        stage.begin();
        ImDrawList* list = ImGui::GetWindowDrawList();
        const int v0 = list->VtxBuffer.Size;
        const ImTypesetTextRange selection = {10, 120};
        CHECK(ImTypeset::DrawParagraph(p, list, ImVec2(12.3f, 4.7f), &selection));
        const Quads q = quads_of(list, v0, list->VtxBuffer.Size, static_cast<double>(scale));
        CHECK(q.rects >= 2); // the selection runs over more than one line
        CHECK_EQ(q.rects_off_grid, 0);
        CHECK_EQ(q.off_grid, 0);
        stage.end();

        std::vector<DrawOp> units, device;
        place_both(p, static_cast<double>(scale), true, units, device);
        int outlines = 0;
        for (const DrawOp& op : device) {
            if (op.type == OpType::TEXT)
                continue;
            ++outlines;
            CHECK(whole(op.x0) && whole(op.y0) && whole(op.x1) && whole(op.y1));
        }
        CHECK(outlines > 4);
        ImTypeset::DestroyParagraph(p);
    }
}

TEST(placing_at_a_density_is_placing_in_framebuffer_pixels)
{
    // Everything tests/test_word_gaps.cpp and tests/test_harden.cpp establish
    // about the product's rules, they establish in the pixels placement is
    // done in. This is what makes those pixels the framebuffer's: a list
    // placed at a density is, number for number, the list placed where a unit
    // is a framebuffer pixel, divided by the density.
    Stage stage(1.0f);
    REQUIRE(stage.ctx != nullptr);
    for (const float size : {13.0f, 16.0f, 19.0f}) {
        ImTypeset::GetStyle().FontSize = size;
        ImTypeset::GetStyle().Align = ImTypesetAlign_Justify;
        ImTypesetParagraph* paragraphs[] = {
            paragraph_of(LONG_PROSE, 310.0f),
            paragraph_of(LONG_MATH, 310.0f, ImTypesetTextFlags_Math),
        };
        for (const ImTypesetParagraph* p : paragraphs) {
            REQUIRE(p != nullptr);
            for (const float scale : SCALES) {
                CHECK_CONTEXT("size %.0f, density %.1f", static_cast<double>(size),
                              static_cast<double>(scale));
                const double density = static_cast<double>(scale);
                std::vector<DrawOp> units, device;
                place_both(p, density, false, units, device);
                REQUIRE_EQ(units.size(), device.size());
                REQUIRE(units.size() > 100);
                long differ = 0;
                long off_grid = 0;
                for (size_t i = 0; i < units.size(); ++i) {
                    const DrawOp& u = units[i];
                    const DrawOp& d = device[i];
                    const auto same = [density](float unit, float pixel) {
                        return unit == static_cast<float>(static_cast<double>(pixel) / density);
                    };
                    bool equal = u.type == d.type && u.glyph == d.glyph && u.colour == d.colour &&
                                 same(u.x0, d.x0) && same(u.y0, d.y0);
                    bool on = whole(d.x0) && whole(d.y0);
                    if (u.type != OpType::TEXT) {
                        equal = equal && same(u.x1, d.x1) && same(u.y1, d.y1);
                        on = on && whole(d.x1) && whole(d.y1);
                    }
                    differ += equal ? 0 : 1;
                    off_grid += on ? 0 : 1;
                }
                CHECK_EQ(differ, 0);
                CHECK_EQ(off_grid, 0);
            }
        }
        for (ImTypesetParagraph* p : paragraphs)
            ImTypeset::DestroyParagraph(p);
    }
}

TEST(the_bounds_of_the_product_s_rules_are_in_framebuffer_pixels)
{
    // The bounds DrawRules states, taken off the ops as a host receives them
    // (ImGui's coordinates) and multiplied by the density. SLACK is for the
    // float32 a coordinate is: at density 1.5 or 3 a framebuffer pixel is not
    // a float in units.
    constexpr double SLACK = 1.0 / 64.0;
    Stage stage(1.0f);
    REQUIRE(stage.ctx != nullptr);
    long words = 0, spaces = 0, parts = 0, rules = 0;
    double worst_word = 0.0, worst_space = 0.0, worst_part = 0.0;
    for (const float size : {13.0f, 14.0f, 16.0f, 17.0f, 19.0f}) {
        ImTypeset::GetStyle().FontSize = size;
        ImTypeset::GetStyle().Align = ImTypesetAlign_Justify;
        ImTypesetParagraph* paragraphs[] = {
            paragraph_of(LONG_PROSE, 283.0f),
            paragraph_of(LONG_MATH, 283.0f, ImTypesetTextFlags_Math),
        };
        for (const ImTypesetParagraph* p : paragraphs) {
            REQUIRE(p != nullptr);
            for (const float scale : SCALES) {
                CHECK_CONTEXT("size %.0f, density %.1f", static_cast<double>(size),
                              static_cast<double>(scale));
                const double density = static_cast<double>(scale);
                std::vector<DrawOp> units, device;
                place_both(p, density, false, units, device);
                // A length of the layout in framebuffer pixels.
                const double ratio = p->layout_style.px_per_inch * density / PT_PER_INCH;
                const auto px = [ratio](Scaled length) {
                    return static_cast<double>(length) * ratio / static_cast<double>(UNITY);
                };
                // A glyph or a rule is one op, in order; nothing else is,
                // with no overlay and no selection.
                size_t op = 0;
                // The word being walked: its first glyph and its last so far.
                bool in_word = false;
                Scaled first_x = 0, last_x = 0;
                double first_pen = 0.0, last_pen = 0.0;
                // The last glyph of the word before it across a space.
                bool has_before = false;
                Scaled before_x = 0;
                double before_pen = 0.0;
                // The formula being walked: its first glyph.
                bool in_formula = false;
                uint32_t formula_index = 0;
                bool formula_has_glyph = false;
                Scaled formula_x = 0;
                double formula_pen = 0.0;
                const auto end_word = [&] {
                    if (!in_word)
                        return;
                    in_word = false;
                    const double off = std::fabs((last_pen - first_pen) - px(last_x - first_x));
                    worst_word = std::max(worst_word, off);
                    ++words;
                    CHECK(off <= WORD_WIDTH_BOUND_PX + SLACK);
                    has_before = true;
                    before_x = last_x;
                    before_pen = last_pen;
                };
                for (const DrawCmd& cmd : p->cmds) {
                    if (cmd.type == CmdType::GLYPH) {
                        REQUIRE(op < units.size());
                        REQUIRE(units[op].type == OpType::TEXT);
                        const double pen = static_cast<double>(units[op].x0) * density;
                        ++op;
                        if (in_formula && cmd.index == formula_index) {
                            // A part of a formula against another part of it.
                            if (!formula_has_glyph) {
                                formula_has_glyph = true;
                                formula_x = cmd.x;
                                formula_pen = pen;
                            }
                            const double off =
                                std::fabs((pen - formula_pen) - px(cmd.x - formula_x));
                            worst_part = std::max(worst_part, off);
                            ++parts;
                            CHECK(off <= 1.0 + SLACK);
                            continue;
                        }
                        if (in_formula) {
                            // Text set straight after a formula: not a space.
                            in_formula = false;
                            has_before = false;
                        }
                        if (!in_word) {
                            in_word = true;
                            first_x = cmd.x;
                            first_pen = pen;
                            if (has_before) {
                                const double off =
                                    std::fabs((pen - before_pen) - px(cmd.x - before_x));
                                worst_space = std::max(worst_space, off);
                                ++spaces;
                                CHECK(off < 1.5 + SLACK);
                            }
                        }
                        last_x = cmd.x;
                        last_pen = pen;
                    } else if (cmd.type == CmdType::RULE_RECT) {
                        REQUIRE(op < units.size());
                        REQUIRE(units[op].type == OpType::RECT_FILLED);
                        // As thick as it was laid out to within a framebuffer
                        // pixel, and never thinner than one.
                        const double thick =
                            static_cast<double>(units[op].y1 - units[op].y0) * density;
                        ++op;
                        ++rules;
                        CHECK(thick >= 1.0 - SLACK);
                        CHECK(std::fabs(thick - px(cmd.height)) < 1.0 + SLACK);
                        if (!in_formula) {
                            end_word();
                            has_before = false;
                        }
                    } else if (cmd.type == CmdType::SET_GLUE) {
                        end_word();
                        in_formula = false;
                    } else if (cmd.type == CmdType::ATOM) {
                        end_word();
                        // What follows a formula is measured from its parts,
                        // which is another bound than a space between words.
                        has_before = false;
                        in_formula = true;
                        formula_index = cmd.index;
                        formula_has_glyph = false;
                    } else {
                        end_word();
                        has_before = false;
                        in_formula = false;
                    }
                }
                end_word();
                CHECK_EQ(op, units.size());
            }
        }
        for (ImTypesetParagraph* p : paragraphs)
            ImTypeset::DestroyParagraph(p);
    }
    // Enough of each for the bounds to have been tried, and each of them
    // reached to within a tenth of a pixel of what it allows or beyond half.
    CHECK(words > 2000);
    CHECK(spaces > 1500);
    CHECK(parts > 500);
    CHECK(rules > 50);
    CHECK(worst_word > 0.4);
    CHECK(worst_space > 0.5);
    CHECK(worst_part > 0.4);
}

TEST(without_the_rule_a_bound_is_that_many_units_and_so_twice_as_many_framebuffer_pixels)
{
    // What the rule is for, by the bound that is easiest to take off a list:
    // a word's drawn width against its laid-out width. Placed in units, as
    // the product's other rules place it without this one, a word is within
    // half a UNIT, and at density 2 some word is further than half a
    // framebuffer pixel out; with the rule none is.
    Stage stage(1.0f);
    REQUIRE(stage.ctx != nullptr);
    ImTypeset::GetStyle().Align = ImTypesetAlign_Justify;
    const double density = 2.0;
    double worst[2] = {0.0, 0.0};
    for (const float size : {13.0f, 14.0f, 16.0f, 17.0f, 19.0f}) {
        ImTypeset::GetStyle().FontSize = size;
        ImTypesetParagraph* p = paragraph_of(LONG_PROSE, 283.0f);
        REQUIRE(p != nullptr);
        const double ratio = p->layout_style.px_per_inch * density / PT_PER_INCH;
        for (int with = 0; with < 2; ++with) {
            DrawRun run;
            run.cmds = p->cmds.data();
            run.count = p->cmds.size();
            DrawArgs args;
            args.rules = DrawRules::product();
            args.rules.device_pixels = with == 1;
            args.px_per_inch = p->layout_style.px_per_inch;
            args.density = density;
            // Baked for the grid each is placed on.
            FixedBinder binder(with == 1 ? 36.0f : 18.0f, with == 1 ? 28.0f : 14.0f);
            Drawer drawer;
            drawer.place(&run, 1, args, binder);
            size_t op = 0;
            bool in_word = false;
            Scaled first_x = 0, last_x = 0;
            double first_pen = 0.0, last_pen = 0.0;
            const auto end_word = [&] {
                if (in_word)
                    worst[with] =
                        std::max(worst[with], std::fabs((last_pen - first_pen) -
                                                        static_cast<double>(last_x - first_x) *
                                                            ratio / static_cast<double>(UNITY)));
                in_word = false;
            };
            for (const DrawCmd& cmd : p->cmds) {
                if (cmd.type != CmdType::GLYPH) {
                    end_word();
                    continue;
                }
                REQUIRE(op < drawer.ops().size());
                const double pen = static_cast<double>(drawer.ops()[op++].x0) * density;
                if (!in_word) {
                    in_word = true;
                    first_x = cmd.x;
                    first_pen = pen;
                }
                last_x = cmd.x;
                last_pen = pen;
            }
            end_word();
        }
        ImTypeset::DestroyParagraph(p);
    }
    CHECK(worst[0] > 0.75);
    CHECK(worst[0] <= 2.0 * WORD_WIDTH_BOUND_PX);
    CHECK(worst[1] <= WORD_WIDTH_BOUND_PX);
}

TEST(at_density_one_the_rule_changes_nothing_and_the_reference_knows_no_density)
{
    struct Shot {
        ImVector<ImDrawVert> vertices;
        Quads quads;
    };
    const auto draw = [](float scale, const DrawRules& rules, Shot& shot) {
        Stage stage(scale);
        REQUIRE(stage.ctx != nullptr);
        stage.ctx->draw_rules = rules;
        ImTypesetParagraph* prose = paragraph_of(LONG_PROSE, 300.0f);
        ImTypesetParagraph* math = paragraph_of(LONG_MATH, 300.0f, ImTypesetTextFlags_Math);
        REQUIRE(prose != nullptr && math != nullptr);
        for (int frame = 0; frame < 2; ++frame) {
            stage.begin();
            ImDrawList* list = ImGui::GetWindowDrawList();
            const int first = list->VtxBuffer.Size;
            CHECK(ImTypeset::DrawParagraph(prose, list, ImVec2(12.3f, 4.6f)));
            CHECK(ImTypeset::DrawParagraph(math, list, ImVec2(322.5f, 4.2f)));
            shot.vertices.resize(0);
            for (int v = first; v < list->VtxBuffer.Size; ++v)
                shot.vertices.push_back(list->VtxBuffer[v]);
            shot.quads = quads_of(list, first, list->VtxBuffer.Size, static_cast<double>(scale));
            stage.end();
        }
        ImTypeset::DestroyParagraph(prose);
        ImTypeset::DestroyParagraph(math);
    };
    const auto same = [](const Shot& a, const Shot& b, bool with_uv) {
        if (a.vertices.Size != b.vertices.Size)
            return false;
        for (int i = 0; i < a.vertices.Size; ++i) {
            const ImDrawVert& x = a.vertices[i];
            const ImDrawVert& y = b.vertices[i];
            if (x.pos.x != y.pos.x || x.pos.y != y.pos.y || x.col != y.col)
                return false;
            if (with_uv && (x.uv.x != y.uv.x || x.uv.y != y.uv.y))
                return false;
        }
        return true;
    };

    DrawRules without = DrawRules::product();
    without.device_pixels = false;

    // Density 1: the product's vertices with the rule and without it are the
    // same floats, texture coordinates included.
    Shot with_rule, without_rule;
    draw(1.0f, DrawRules::product(), with_rule);
    draw(1.0f, without, without_rule);
    REQUIRE(with_rule.vertices.Size > 1000);
    CHECK(same(with_rule, without_rule, true));

    // Without the rule a density changes no position, under the product's
    // other rules and under the reference's; and the glyphs are then the
    // bitmaps of density 1, a texel to two framebuffer pixels.
    Shot dense;
    draw(2.0f, without, dense);
    CHECK(same(dense, without_rule, false));
    CHECK_NEAR(dense.quads.texels_per_pixel_min, 0.5, 1e-3);
    CHECK_NEAR(dense.quads.texels_per_pixel_max, 0.5, 1e-3);

    Shot reference_one, reference_two;
    draw(1.0f, DrawRules::reference(), reference_one);
    draw(2.0f, DrawRules::reference(), reference_two);
    REQUIRE(reference_one.vertices.Size > 1000);
    CHECK(same(reference_one, reference_two, false));

    // With it, a density does: the picture is not the one of density 1.
    Shot product_two;
    draw(2.0f, DrawRules::product(), product_two);
    CHECK(!same(product_two, with_rule, false));
}

TEST(layout_sizes_and_hit_tests_do_not_change_with_the_density)
{
    struct Answers {
        ImVec2 size;
        int lines = 0;
        ImTypesetLine third;
        ImTypesetHit hit;
        ImVec4 caret;
    };
    const auto ask = [](float scale, Answers& a) {
        Stage stage(scale);
        REQUIRE(stage.ctx != nullptr);
        ImTypesetParagraph* p = paragraph_of(LONG_PROSE, 300.0f);
        REQUIRE(p != nullptr);
        a.size = ImTypeset::GetParagraphSize(p);
        a.lines = ImTypeset::GetParagraphLineCount(p);
        a.third = ImTypeset::GetParagraphLine(p, 2);
        a.hit = ImTypeset::HitTestParagraph(p, ImVec2(101.25f, 47.5f));
        a.caret = ImTypeset::GetParagraphCaretRect(p, 57);
        ImTypeset::DestroyParagraph(p);
    };
    Answers one, two;
    ask(1.0f, one);
    ask(2.0f, two);
    REQUIRE(one.lines > 4);
    CHECK_EQ(one.lines, two.lines);
    CHECK_EQ(one.size.x, two.size.x);
    CHECK_EQ(one.size.y, two.size.y);
    CHECK_EQ(one.third.Min.x, two.third.Min.x);
    CHECK_EQ(one.third.Max.x, two.third.Max.x);
    CHECK_EQ(one.third.Baseline, two.third.Baseline);
    CHECK_EQ(one.hit.TextOffset, two.hit.TextOffset);
    CHECK_EQ(one.hit.Line, two.hit.Line);
    CHECK_EQ(one.caret.x, two.caret.x);
    CHECK_EQ(one.caret.y, two.caret.y);
    CHECK_EQ(one.caret.w, two.caret.w);
}

TEST(which_densities_the_rule_follows)
{
    DrawArgs args;
    args.rules = DrawRules::product();
    // With no ImGui context there is no display to ask.
    CHECK_EQ(Drawer::density_of(args), 1.0);
    args.density = 2.0;
    CHECK_EQ(Drawer::density_of(args), 2.0);
    args.density = 1.5;
    CHECK_EQ(Drawer::density_of(args), 1.5);
    args.density = MAX_DENSITY;
    CHECK_EQ(Drawer::density_of(args), MAX_DENSITY);
    for (const double refused : {0.5, 1.0, MAX_DENSITY * 2.0, static_cast<double>(INFINITY),
                                 static_cast<double>(NAN), -2.0}) {
        args.density = refused;
        CHECK_EQ(Drawer::density_of(args), 1.0);
    }
    // Under the reference's rules there is one kind of pixel.
    args.rules = DrawRules::reference();
    args.density = 2.0;
    CHECK_EQ(Drawer::density_of(args), 1.0);

    // Unnamed, it is the display's as ImGui has it.
    Stage stage(2.0f);
    REQUIRE(stage.ctx != nullptr);
    args.rules = DrawRules::product();
    args.density = 0.0;
    CHECK_EQ(Drawer::density_of(args), 2.0);
    ImGui::GetIO().DisplayFramebufferScale = ImVec2(0.0f, 0.0f);
    CHECK_EQ(Drawer::density_of(args), 1.0);
    ImGui::GetIO().DisplayFramebufferScale = ImVec2(3.0f, 3.0f);
    CHECK_EQ(Drawer::density_of(args), 3.0);
}

// The table and the pictures the decision was taken from; asserts nothing.
TEST(hidpi_report)
{
    const char* out = std::getenv("IMTYPESET_HIDPI_OUT");
    if (out == nullptr || out[0] == '\0')
        return;
    const char* tag = std::getenv("IMTYPESET_HIDPI_TAG");
    std::printf("hidpi report (%s rasteriser)\n",
#ifdef IMGUI_ENABLE_FREETYPE
                "FreeType"
#else
                "stb_truetype"
#endif
    );
    for (const float scale : SCALES) {
        Stage stage(scale);
        REQUIRE(stage.ctx != nullptr);
        const Measured m = measure(stage);
        print_row(scale, "host", m.host, m.host_ink);
        print_row(scale, "prose", m.prose, m.prose_ink);
        print_row(scale, "formula", m.formula, m.formula_ink);
        const ImTextureData* tex = ImGui::GetIO().Fonts->TexData;
        std::printf("       the atlas after it: %d x %d texels\n", tex->Width, tex->Height);
        char path[1024];
        std::snprintf(path, sizeof path, "%s/hidpi-%s-%.1fx.png", out,
                      tag != nullptr ? tag : "shot", static_cast<double>(scale));
        if (!soft_raster::write_png(path, m.image))
            FAIL("could not write %s", path);
    }
}
