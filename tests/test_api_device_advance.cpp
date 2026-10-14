// GetDeviceGlyphAdvance answers whole framebuffer pixels (imscribe's H49).
// Layout holds a device advance as the whole number of scaled points at or
// above that many framebuffer pixels (src/device.cpp, DeviceMetrics::scaled),
// so the scaled points converted back are a whole pixel and a residue under
// one scaled point. The query answers the pixels without the residue: the
// advance of Dear ImGui's own entry for the cell, divided by the density.
// Worked by hand: the oracle is the atlas, asked here directly.

#include "golden.h"

#include "context.h"
#include "draw.h"
#include "imtypeset.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <cmath>
#include <string>

using namespace ImTypeset::detail;

namespace
{

const struct {
    ImTypesetFace face;
    const char* file;
} FONTS[] = {
    {ImTypesetFace_Regular, "Roboto-Regular.ttf"},
    {ImTypesetFace_Code, "Inconsolata-Medium.ttf"},
};

struct Stage {
    explicit Stage(float density)
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(800.0f, 600.0f);
        io.DisplayFramebufferScale = ImVec2(density, density);
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        ctx = ImTypeset::CreateContext();
        if (ctx == nullptr) {
            FAIL("CreateContext: %s", ImTypeset::GetLastError()->Message);
            return;
        }
        for (const auto& entry : FONTS) {
            const std::string path = golden::data_path(std::string("fonts/") + entry.file);
            if (ImTypeset::AddFontFromFileTTF(entry.face, path.c_str()) == nullptr) {
                FAIL("%s: %s", entry.file, ImTypeset::GetLastError()->Message);
                ctx = nullptr;
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

    ImTypesetContext* ctx = nullptr;
};

// Dear ImGui's entry for the code face as a style sets it on this display:
// the one the library measures with (tests/test_device_grid.cpp asks it the
// same way).
ImFontBaked* entry_for(ImTypesetContext& ctx, const ImTypesetStyle& style, double density)
{
    ImFont* font = ImTypeset::GetFont(ImTypesetFace_Code);
    if (font == nullptr)
        return nullptr;
    const ResolvedStyle resolved = resolve_style(ctx, style);
    const FontMetrics* ours = ctx.library.metrics(Face::CODE, static_cast<double>(resolved.em));
    return face_bake(font, ours, resolved.em, resolved.px_per_inch, density);
}

// Whether an answer is a whole number of framebuffer pixels divided by the
// density, as a float: at a density of 1.5 that float times 1.5 need not be
// whole, so the pixels are rounded and divided again.
void check_whole(float answer, float density)
{
    const double framebuffer = py_round(static_cast<double>(answer) * static_cast<double>(density));
    CHECK_EQ(answer, static_cast<float>(framebuffer / static_cast<double>(density)));
}

} // namespace

TEST(the_device_advance_is_the_atlas_advance_in_whole_framebuffer_pixels)
{
    for (const float density : {1.0f, 1.5f, 2.0f}) {
        Stage stage(density);
        REQUIRE(stage.ctx != nullptr);
        for (int height = 12; height <= 24; ++height) {
            CHECK_CONTEXT("density %g, pixel height %d", static_cast<double>(density), height);
            ImTypesetStyle style;
            style.FontSize = static_cast<float>(height);
            const float answer = ImTypeset::GetDeviceGlyphAdvance(ImTypesetFace_Code, '0', &style);
            REQUIRE(answer > 0.0f);
            check_whole(answer, density);
            // And the atlas's own number for it.
            ImFontBaked* baked = entry_for(*stage.ctx, style, static_cast<double>(density));
            REQUIRE(baked != nullptr);
            const double atlas =
                static_cast<double>(baked->GetCharAdvance(static_cast<ImWchar>('0')));
#ifdef IMGUI_ENABLE_FREETYPE
            // FreeType's entries are whole already. stb_truetype's are whole
            // only with PixelSnapH at density 1, and layout rounds them as
            // PixelSnapH would (src/device.cpp).
            CHECK_EQ(atlas, std::floor(atlas));
#endif
            CHECK_EQ(answer, static_cast<float>(snap_px(atlas) / static_cast<double>(density)));
            // Every character of the face's grid answers the same cell.
            CHECK_EQ(ImTypeset::GetDeviceGlyphAdvance(ImTypesetFace_Code, 'W', &style), answer);
        }
    }
}

TEST(a_scaled_run_and_a_prose_face_answer_whole_framebuffer_pixels_too)
{
    for (const float density : {1.0f, 1.5f, 2.0f}) {
        Stage stage(density);
        REQUIRE(stage.ctx != nullptr);
        ImTypesetStyle style;
        style.FontSize = 16.0f;
        for (const float scale : {0.75f, 1.25f, 2.0f}) {
            CHECK_CONTEXT("density %g, scale %g", static_cast<double>(density),
                          static_cast<double>(scale));
            const float answer =
                ImTypeset::GetDeviceGlyphAdvance(ImTypesetFace_Code, '0', &style, scale);
            REQUIRE(answer > 0.0f);
            check_whole(answer, density);
        }
        for (const unsigned int c : {static_cast<unsigned int>('i'), 0x57u, 0x6Du}) {
            CHECK_CONTEXT("density %g, U+%04X", static_cast<double>(density), c);
            const float answer = ImTypeset::GetDeviceGlyphAdvance(ImTypesetFace_Regular, c, &style);
            REQUIRE(answer > 0.0f);
            check_whole(answer, density);
        }
    }
}
