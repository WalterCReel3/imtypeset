// tests/support/soft_raster.h: that it honours ImGui's texture protocol, fills
// by the top-left rule with every shared edge covered once, scissors as the
// stock backends do, and samples so that a pixel-aligned quad reproduces its
// texels exactly -- which is what makes a picture from it evidence about text.

#include "support/soft_raster.h"

#include "check.h"
#include "golden.h"

#include "imgui.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace
{

constexpr ImU32 white = IM_COL32(255, 255, 255, 255);
constexpr ImU32 black = IM_COL32(0, 0, 0, 255);
constexpr ImU32 red = IM_COL32(255, 0, 0, 255);

// An ImGui context with the software renderer attached, for one test.
struct Session {
    soft_raster::Renderer renderer;
    int width;
    int height;

    explicit Session(int w = 200, int h = 120, float framebuffer_scale = 1.0f)
        : width(w)
        , height(h)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
        io.DisplayFramebufferScale = ImVec2(framebuffer_scale, framebuffer_scale);
        io.DeltaTime = 1.0f / 60.0f;
        renderer.init();
    }
    ~Session()
    {
        renderer.shutdown();
        ImGui::DestroyContext();
    }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Ends the frame and draws it over `background`.
    soft_raster::Image finish(ImU32 background = black)
    {
        ImGui::Render();
        const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
        soft_raster::Image image(static_cast<int>(static_cast<float>(width) * scale.x),
                                 static_cast<int>(static_cast<float>(height) * scale.y),
                                 background);
        renderer.render(ImGui::GetDrawData(), image);
        return image;
    }
};

// How many pixels of the image are not `background`, and their bounds.
struct Coverage {
    long count = 0;
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1; // inclusive
};

Coverage coverage(const soft_raster::Image& image, ImU32 background)
{
    Coverage c;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            if (image.get(x, y) == background)
                continue;
            ++c.count;
            c.x0 = x < c.x0 ? x : c.x0;
            c.y0 = y < c.y0 ? y : c.y0;
            c.x1 = x > c.x1 ? x : c.x1;
            c.y1 = y > c.y1 ? y : c.y1;
        }
    }
    return c;
}

// Checks that exactly the pixels of [x0, x1) x [y0, y1) are `inside` and every
// other pixel is `outside`.
void check_exact_rect(const soft_raster::Image& image, int x0, int y0, int x1, int y1, ImU32 inside,
                      ImU32 outside)
{
    long wrong = 0;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const bool in = x >= x0 && x < x1 && y >= y0 && y < y1;
            if (image.get(x, y) != (in ? inside : outside)) {
                if (wrong < 5)
                    FAIL("pixel (%d, %d) is %08x, expected %08x (%s the rectangle)", x, y,
                         static_cast<unsigned>(image.get(x, y)),
                         static_cast<unsigned>(in ? inside : outside), in ? "inside" : "outside");
                ++wrong;
            }
        }
    }
    CHECK_EQ(wrong, 0);
}

// A triangle straight into the draw list: no anti-aliasing fringe, and the
// vertex order is the caller's.
void raw_triangle(ImDrawList* list, ImVec2 a, ImVec2 b, ImVec2 c, ImU32 colour)
{
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    list->PrimReserve(3, 3);
    list->PrimVtx(a, uv, colour);
    list->PrimVtx(b, uv, colour);
    list->PrimVtx(c, uv, colour);
}

} // namespace

TEST(a_window_with_text_renders_something)
{
    Session session;
    soft_raster::Image image;
    // A window is hidden on the frame it first appears, while it is auto-sized.
    for (int frame = 0; frame < 3; ++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::Begin("soft raster");
        ImGui::TextUnformatted("typeset");
        ImGui::End();
        image = session.finish();
    }
    const Coverage c = coverage(image, black);
    CHECK(c.count > 500);
    CHECK(c.x0 >= 10);
    CHECK(c.y0 >= 10);
    CHECK(c.x1 < image.width && c.y1 < image.height);

    const soft_raster::Stats& stats = session.renderer.stats();
    CHECK(stats.textures_created >= 1);
    CHECK(stats.triangles > 0);
    CHECK(stats.pixels_shaded > 0);
    CHECK_EQ(stats.commands_without_texture, 0);

    // Text is drawn in a colour of its own: some pixel is nearer white than
    // any part of the window's frame is.
    long bright = 0;
    for (int y = 0; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x)
            if (image.pixel(x, y)[0] > 200 && image.pixel(x, y)[1] > 200 &&
                image.pixel(x, y)[2] > 200)
                ++bright;
    CHECK(bright > 20);
}

// docs/BACKENDS.md: a request is honoured, then acknowledged with SetTexID and
// SetStatus. The renderer keeps its own copy, so the copy is what is compared.
TEST(textures_are_created_updated_and_acknowledged)
{
    Session session;
    ImGuiIO& io = ImGui::GetIO();
    const std::string path = golden::data_path("fonts/Roboto-Regular.ttf");
    ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), 16.0f);
    REQUIRE(font != nullptr);

    const auto check_copies = [&]() {
        for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
            if (tex->Status == ImTextureStatus_Destroyed)
                continue;
            CHECK_EQ(static_cast<int>(tex->Status), static_cast<int>(ImTextureStatus_OK));
            CHECK(tex->TexID != ImTextureID_Invalid);
            const soft_raster::Texture* copy = session.renderer.find(tex->TexID);
            if (!CHECK(copy != nullptr))
                continue;
            REQUIRE_EQ(copy->width, tex->Width);
            REQUIRE_EQ(copy->height, tex->Height);
            REQUIRE_EQ(static_cast<int>(tex->Format), static_cast<int>(ImTextureFormat_RGBA32));
            CHECK(std::memcmp(copy->rgba.data(), tex->Pixels, copy->rgba.size()) == 0);
        }
    };

    ImGui::NewFrame();
    ImGui::GetBackgroundDrawList()->AddText(font, 16.0f, ImVec2(4, 4), white, "abc");
    session.finish();
    CHECK_EQ(session.renderer.stats().textures_created, 1);
    check_copies();

    // Glyphs nobody has asked for yet, at a size nobody has asked for: ImGui
    // rasterises them into unused parts of the texture and asks for an update
    // (or, if it had to grow the atlas, for a new texture and the old one's
    // destruction). Either way the copies must match afterwards.
    const long requests_before =
        session.renderer.stats().texture_updates + session.renderer.stats().textures_created;
    for (int frame = 0; frame < 3; ++frame) {
        ImGui::NewFrame();
        ImGui::GetBackgroundDrawList()->AddText(font, 29.0f, ImVec2(4, 30), white,
                                                "QWERTY xyz 0123");
        session.finish();
    }
    const long requests_after =
        session.renderer.stats().texture_updates + session.renderer.stats().textures_created;
    CHECK(requests_after > requests_before);
    check_copies();
    CHECK_EQ(session.renderer.stats().commands_without_texture, 0);
}

TEST(shutdown_destroys_what_it_created)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(64, 64);
    soft_raster::Renderer renderer;
    renderer.init();
    CHECK((io.BackendFlags & ImGuiBackendFlags_RendererHasTextures) != 0);
    ImGui::NewFrame();
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(1, 1), ImVec2(5, 5), red);
    ImGui::Render();
    soft_raster::Image image(64, 64);
    renderer.render(ImGui::GetDrawData(), image);
    REQUIRE(ImGui::GetPlatformIO().Textures.Size >= 1);
    ImTextureData* tex = ImGui::GetPlatformIO().Textures[0];
    const ImTextureID id = tex->TexID;
    CHECK(renderer.find(id) != nullptr);

    renderer.shutdown();
    CHECK(tex->TexID == ImTextureID_Invalid);
    CHECK(renderer.find(id) == nullptr);
    CHECK_EQ(renderer.stats().textures_destroyed, 1);
    CHECK((io.BackendFlags & ImGuiBackendFlags_RendererHasTextures) == 0);
    CHECK(io.BackendRendererUserData == nullptr);
    ImGui::DestroyContext();
}

TEST(a_pixel_aligned_rectangle_covers_exactly_its_pixels)
{
    Session session;
    ImGui::NewFrame();
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(10, 20), ImVec2(30, 25), red);
    const soft_raster::Image image = session.finish();
    check_exact_rect(image, 10, 20, 30, 25, red, black);
    CHECK_EQ(session.renderer.stats().pixels_shaded, 20 * 5);
}

// A pixel centre exactly on an edge belongs to the triangle on whose top or
// left edge it lies. With corners on half pixels every edge passes through
// pixel centres: the left and top ones are drawn, the right and bottom are not.
TEST(a_rectangle_on_half_pixels_follows_the_top_left_rule)
{
    Session session;
    ImGui::NewFrame();
    // The quad itself, through PrimRect: what is under test is how the
    // rasteriser treats an edge through pixel centres, and AddRectFilled is
    // free to give a rectangle off the pixel grid an anti-aliased fringe (it
    // does from Dear ImGui 1.93 on).
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    list->PrimReserve(6, 4);
    list->PrimRect(ImVec2(10.5f, 20.5f), ImVec2(30.5f, 25.5f), red);
    const soft_raster::Image image = session.finish();
    check_exact_rect(image, 10, 20, 30, 25, red, black);
}

// A half-transparent pixel drawn twice is not the colour of one drawn once, so
// blending shows whether the two triangles of a quad both took the diagonal.
TEST(the_shared_edge_of_two_triangles_is_covered_once)
{
    const ImU32 half = IM_COL32(255, 255, 255, 128);
    const ImVec2 corners[4] = {ImVec2(12.3f, 9.7f), ImVec2(71.9f, 14.2f), ImVec2(64.4f, 58.6f),
                               ImVec2(8.1f, 49.5f)};
    long counts[2] = {0, 0};
    for (int split = 0; split < 2; ++split) {
        CHECK_CONTEXT("split along diagonal %d", split);
        Session session(96, 72);
        ImGui::NewFrame();
        ImDrawList* list = ImGui::GetBackgroundDrawList();
        if (split == 0) {
            raw_triangle(list, corners[0], corners[1], corners[2], half);
            raw_triangle(list, corners[0], corners[2], corners[3], half);
        } else {
            // The other diagonal, and both triangles wound the other way.
            raw_triangle(list, corners[3], corners[1], corners[0], half);
            raw_triangle(list, corners[3], corners[2], corners[1], half);
        }
        const soft_raster::Image image = session.finish();
        long once = 0;
        long other = 0;
        for (int y = 0; y < image.height; ++y) {
            for (int x = 0; x < image.width; ++x) {
                const uint8_t value = image.pixel(x, y)[0];
                if (value == 0)
                    continue;
                // 255 * (128 / 255) over black.
                if (value == 128)
                    ++once;
                else
                    ++other;
            }
        }
        CHECK_EQ(other, 0);
        CHECK(once > 2000);
        counts[split] = once;
        CHECK_EQ(session.renderer.stats().triangles, 2);
        CHECK_EQ(session.renderer.stats().pixels_shaded, once);
    }
    // The same quadrilateral, so the same pixels, whichever way it is cut.
    CHECK_EQ(counts[0], counts[1]);
}

// A fan of thin triangles round a centre that is itself a pixel centre: every
// spoke is a shared edge, some of them through pixel centres, and the centre
// pixel is a vertex of all of them. Nothing is drawn twice and nothing missed.
TEST(a_fan_of_triangles_leaves_no_gap_and_no_overlap)
{
    const ImU32 half = IM_COL32(255, 255, 255, 128);
    Session session(96, 96);
    ImGui::NewFrame();
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    const ImVec2 centre(48.5f, 48.5f);
    const ImVec2 rim[8] = {ImVec2(88.5f, 48.5f), ImVec2(76.5f, 76.5f), ImVec2(48.5f, 88.5f),
                           ImVec2(20.5f, 76.5f), ImVec2(8.5f, 48.5f),  ImVec2(20.5f, 20.5f),
                           ImVec2(48.5f, 8.5f),  ImVec2(76.5f, 20.5f)};
    for (int i = 0; i < 8; ++i)
        raw_triangle(list, centre, rim[i], rim[(i + 1) % 8], half);
    const soft_raster::Image image = session.finish();
    long once = 0;
    long other = 0;
    for (int y = 0; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x)
            if (image.pixel(x, y)[0] == 128)
                ++once;
            else if (image.pixel(x, y)[0] != 0)
                ++other;
    CHECK_EQ(other, 0);
    CHECK_EQ(image.pixel(48, 48)[0], 128);
    // Interior pixels well inside each sector and on each spoke.
    CHECK_EQ(image.pixel(60, 48)[0], 128);
    CHECK_EQ(image.pixel(48, 60)[0], 128);
    CHECK_EQ(image.pixel(36, 48)[0], 128);
    CHECK_EQ(image.pixel(48, 36)[0], 128);
    CHECK_EQ(image.pixel(58, 58)[0], 128);
    CHECK_EQ(image.pixel(38, 38)[0], 128);
    CHECK_EQ(session.renderer.stats().pixels_shaded, once);
}

// The scissor rectangle is ClipRect projected and truncated as
// imgui_impl_opengl3.cpp does it: x from (int)min, width from (int)(max - min).
TEST(the_scissor_rectangle_clips_as_the_opengl_backend_does)
{
    Session session;
    ImGui::NewFrame();
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    list->PushClipRect(ImVec2(20, 30), ImVec2(50, 40), false);
    list->AddRectFilled(ImVec2(0, 0), ImVec2(200, 120), red);
    list->PopClipRect();
    soft_raster::Image image = session.finish();
    check_exact_rect(image, 20, 30, 50, 40, red, black);

    ImGui::NewFrame();
    list = ImGui::GetBackgroundDrawList();
    list->PushClipRect(ImVec2(20.7f, 30.2f), ImVec2(50.9f, 40.9f), false);
    list->AddRectFilled(ImVec2(0, 0), ImVec2(200, 120), red);
    list->PopClipRect();
    image = session.finish();
    // (int)20.7 = 20 and (int)(50.9 - 20.7) = 30; (int)30.2 = 30 and (int)(40.9 - 30.2) = 10.
    check_exact_rect(image, 20, 30, 50, 40, red, black);

    // A clip rectangle partly and wholly off the target.
    ImGui::NewFrame();
    list = ImGui::GetBackgroundDrawList();
    list->PushClipRect(ImVec2(-15, -15), ImVec2(6, 4), false);
    list->AddRectFilled(ImVec2(-50, -50), ImVec2(200, 120), red);
    list->PopClipRect();
    list->PushClipRect(ImVec2(500, 500), ImVec2(600, 600), false);
    list->AddRectFilled(ImVec2(-50, -50), ImVec2(900, 900), red);
    list->PopClipRect();
    image = session.finish();
    check_exact_rect(image, 0, 0, 6, 4, red, black);
}

TEST(framebuffer_scale_and_display_position_are_applied)
{
    Session session(100, 60, 2.0f);
    ImGui::NewFrame();
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(10, 20), ImVec2(30, 25), red);
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    CHECK_EQ(draw_data->FramebufferScale.x, 2.0f);
    soft_raster::Image image(200, 120);
    session.renderer.render(draw_data, image);
    check_exact_rect(image, 20, 40, 60, 50, red, black);

    // A viewport whose top-left is not the origin: positions and clip
    // rectangles are both relative to DisplayPos.
    draw_data->DisplayPos = ImVec2(5, 10);
    image.clear(black);
    session.renderer.render(draw_data, image);
    check_exact_rect(image, 10, 20, 50, 30, red, black);
    draw_data->DisplayPos = ImVec2(0, 0);
}

// The reason the sampler is bilinear with texel centres at half-integers: a
// quad with corners on pixel corners and UVs on texel corners puts each pixel
// centre on a texel centre, and the image must then be the texels themselves.
// Drawn white over black, the red channel is the atlas's coverage.
TEST(a_pixel_aligned_glyph_quad_reproduces_the_atlas_exactly)
{
    Session session(256, 160);
    ImGuiIO& io = ImGui::GetIO();
    const std::string path = golden::data_path("fonts/Roboto-Regular.ttf");
    ImFont* roboto = io.Fonts->AddFontFromFileTTF(path.c_str(), 16.0f);
    REQUIRE(roboto != nullptr);
    ImFont* builtin = io.Fonts->AddFontDefault();

    struct Placed {
        int x, y, w, h;   // where on the image, in pixels
        int tex_x, tex_y; // which texels
        ImWchar codepoint;
        float size;
    };
    std::vector<Placed> placed;

    ImGui::NewFrame();
    // Bake everything first: a glyph's UVs are only good while the atlas
    // texture it was packed into is the current one.
    struct Wanted {
        ImFont* font;
        float size;
        const char* text;
    };
    const Wanted wanted[] = {
        {roboto, 16.0f, "AgjQ@8"}, {roboto, 31.0f, "Wf%"}, {builtin, 13.0f, "Mg#"}};
    for (const Wanted& w : wanted)
        for (const char* c = w.text; *c != '\0'; ++c)
            w.font->GetFontBaked(w.size)->FindGlyph(static_cast<ImWchar>(*c));

    ImTextureData* tex = io.Fonts->TexData;
    REQUIRE(tex != nullptr);
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    int pen_x = 3;
    int pen_y = 5;
    for (const Wanted& w : wanted) {
        for (const char* c = w.text; *c != '\0'; ++c) {
            const ImFontGlyph* glyph =
                w.font->GetFontBaked(w.size)->FindGlyph(static_cast<ImWchar>(*c));
            REQUIRE(glyph != nullptr);
            // Texel rectangle from the UVs, which the atlas wrote as n / size.
            const int tx0 = static_cast<int>(glyph->U0 * static_cast<float>(tex->Width) + 0.5f);
            const int ty0 = static_cast<int>(glyph->V0 * static_cast<float>(tex->Height) + 0.5f);
            const int tx1 = static_cast<int>(glyph->U1 * static_cast<float>(tex->Width) + 0.5f);
            const int ty1 = static_cast<int>(glyph->V1 * static_cast<float>(tex->Height) + 0.5f);
            CHECK_NEAR(glyph->U0 * static_cast<float>(tex->Width), tx0, 1e-3);
            CHECK_NEAR(glyph->V1 * static_cast<float>(tex->Height), ty1, 1e-3);
            Placed p{pen_x, pen_y, tx1 - tx0, ty1 - ty0, tx0, ty0, static_cast<ImWchar>(*c),
                     w.size};
            REQUIRE(p.w > 0 && p.h > 0);
            list->AddImage(io.Fonts->TexRef,
                           ImVec2(static_cast<float>(p.x), static_cast<float>(p.y)),
                           ImVec2(static_cast<float>(p.x + p.w), static_cast<float>(p.y + p.h)),
                           ImVec2(glyph->U0, glyph->V0), ImVec2(glyph->U1, glyph->V1), white);
            placed.push_back(p);
            pen_x += p.w + 3;
        }
        pen_x = 3;
        pen_y += 44;
    }
    const soft_raster::Image image = session.finish();
    REQUIRE(io.Fonts->TexData == tex); // the atlas was not replaced under the UVs
    REQUIRE_EQ(static_cast<int>(tex->Format), static_cast<int>(ImTextureFormat_RGBA32));

    long compared = 0;
    long inked = 0;
    for (const Placed& p : placed) {
        CHECK_CONTEXT("U+%04X at %g px", static_cast<unsigned>(p.codepoint),
                      static_cast<double>(p.size));
        long wrong = 0;
        for (int y = 0; y < p.h; ++y) {
            for (int x = 0; x < p.w; ++x) {
                const unsigned char* texel =
                    static_cast<const unsigned char*>(tex->GetPixelsAt(p.tex_x + x, p.tex_y + y));
                // White times the texel, over black: each channel is
                // texel.rgb * texel.a / 255. The atlas stores white, so that
                // is the coverage.
                const int expected = (texel[0] * texel[3] * 2 + 255) / (2 * 255);
                const int got = image.pixel(p.x + x, p.y + y)[0];
                if (got != expected)
                    ++wrong;
                ++compared;
                inked += texel[3] != 0;
            }
        }
        CHECK_EQ(wrong, 0);
    }
    CHECK(compared > 1500);
    CHECK(inked > 300); // the glyphs were really there to be compared
}

// What ImGui itself emits for text at an integer origin, compared texel for
// texel. Whether its quads are pixel-aligned depends on the rasteriser: the
// FreeType loader places glyphs on whole pixels, stb_truetype's oversampling
// does not, and then there is nothing exact to compare.
TEST(text_at_an_integer_origin_is_the_atlas_texel_for_texel)
{
    Session session(200, 80);
    ImGuiIO& io = ImGui::GetIO();
    const std::string path = golden::data_path("fonts/Roboto-Regular.ttf");
    ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), 16.0f);
    REQUIRE(font != nullptr);
    const float size = 20.0f;
    const char* text = "Hg";

    ImGui::NewFrame();
    ImFontBaked* baked = font->GetFontBaked(size);
    const ImFontGlyph* first = baked->FindGlyph(static_cast<ImWchar>(text[0]));
    const ImFontGlyph* second = baked->FindGlyph(static_cast<ImWchar>(text[1]));
    REQUIRE(first != nullptr && second != nullptr);
    const auto whole = [](float v) { return v == static_cast<float>(static_cast<int>(v)); };
    const bool aligned = whole(first->X0) && whole(first->Y0) && whole(first->X1) &&
                         whole(first->Y1) && whole(first->AdvanceX) && whole(second->X0) &&
                         whole(second->Y0);
    if (!aligned) {
        std::printf("skipped: with %s the glyph quads are not on whole pixels (X0 %g, advance %g), "
                    "so text cannot be compared with the atlas texel for texel\n",
                    io.Fonts->FontLoaderName, static_cast<double>(first->X0),
                    static_cast<double>(first->AdvanceX));
        session.finish();
        return;
    }
    const ImVec2 origin(30.0f, 20.0f);
    ImGui::GetBackgroundDrawList()->AddText(font, size, origin, white, text);
    ImTextureData* tex = io.Fonts->TexData;
    const soft_raster::Image image = session.finish();
    REQUIRE(io.Fonts->TexData == tex);

    float pen = origin.x;
    long compared = 0;
    for (const ImFontGlyph* glyph : {first, second}) {
        const int x0 = static_cast<int>(pen + glyph->X0);
        const int y0 = static_cast<int>(origin.y + glyph->Y0);
        const int w = static_cast<int>(glyph->X1 - glyph->X0);
        const int h = static_cast<int>(glyph->Y1 - glyph->Y0);
        const int tx0 = static_cast<int>(glyph->U0 * static_cast<float>(tex->Width) + 0.5f);
        const int ty0 = static_cast<int>(glyph->V0 * static_cast<float>(tex->Height) + 0.5f);
        long wrong = 0;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const unsigned char* texel =
                    static_cast<const unsigned char*>(tex->GetPixelsAt(tx0 + x, ty0 + y));
                const int expected = (texel[0] * texel[3] * 2 + 255) / (2 * 255);
                wrong += image.pixel(x0 + x, y0 + y)[0] != expected;
                ++compared;
            }
        }
        CHECK_EQ(wrong, 0);
        pen += glyph->AdvanceX;
    }
    CHECK(compared > 200);
}

// A 2x2 texture magnified eight times: bilinear gives each texel's value at its
// centre and a ramp between; nearest gives four flat blocks.
TEST(bilinear_and_nearest_sampling_of_a_user_texture)
{
#if IMGUI_VERSION_NUM < 19274
    // The sampler is switched through ImGuiPlatformIO's draw callbacks, which
    // this Dear ImGui does not have yet.
    std::printf("soft_raster: sampling not compared: Dear ImGui %d has no "
                "ImGuiPlatformIO::DrawCallback_SetSamplerNearest (19274)\n",
                IMGUI_VERSION_NUM);
#else
    Session session(64, 32);
    const uint8_t texels[16] = {255, 255, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255, 255};
    const ImTextureID id = session.renderer.add_texture(2, 2, texels);
    REQUIRE(id != ImTextureID_Invalid);

    ImGui::NewFrame();
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    list->AddImage(ImTextureRef(id), ImVec2(0, 0), ImVec2(16, 16));
    list->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest, nullptr);
    list->AddImage(ImTextureRef(id), ImVec2(32, 0), ImVec2(48, 16));
    list->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr);
    list->AddImage(ImTextureRef(id), ImVec2(0, 16), ImVec2(16, 32));
    const soft_raster::Image image = session.finish(IM_COL32(0, 0, 255, 255));
    CHECK_EQ(session.renderer.stats().commands_without_texture, 0);

    // Bilinear. A pixel centre at x + 0.5 of 16 is (x + 0.5) / 8 texels along,
    // and texel centres are at 0.5 and 1.5. Pixel 3 is at 0.4375, left of the
    // first centre, so it is clamped to that texel. Pixels 7 and 8 are at
    // 0.9375 and 1.0625: 7/16 and 9/16 of the way from the first to the second.
    CHECK_EQ(image.pixel(3, 3)[0], 255);
    CHECK_EQ(image.pixel(12, 3)[0], 0);
    CHECK_EQ(image.pixel(3, 12)[0], 0);
    CHECK_EQ(image.pixel(12, 12)[0], 255);
    CHECK_EQ(image.pixel(7, 0)[0], 143); // 255 * (1 - 7/16) = 143.44
    CHECK_EQ(image.pixel(8, 0)[0], 112); // 255 * (1 - 9/16) = 111.56
    for (int x = 1; x < 16; ++x)
        CHECK(image.pixel(x, 0)[0] <= image.pixel(x - 1, 0)[0]);

    // Nearest: four blocks, each one colour to its edge.
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const bool light = (x < 8) == (y < 8);
            if (image.pixel(32 + x, y)[0] != (light ? 255 : 0)) {
                FAIL("nearest: pixel (%d, %d) is %d", x, y, image.pixel(32 + x, y)[0]);
                return;
            }
        }
    }

    // After the reset, bilinear again.
    CHECK_EQ(image.pixel(7, 16)[0], 143);
#endif
}

TEST(a_command_with_an_unknown_texture_is_skipped_and_counted)
{
    Session session(32, 32);
    ImGui::NewFrame();
    ImGui::GetBackgroundDrawList()->AddImage(ImTextureRef(static_cast<ImTextureID>(12345)),
                                             ImVec2(0, 0), ImVec2(16, 16));
    const soft_raster::Image image = session.finish();
    CHECK_EQ(coverage(image, black).count, 0);
    CHECK_EQ(session.renderer.stats().commands_without_texture, 1);
}

// Vertices that are not positions on any target must be ignored, not turned
// into an out-of-range conversion.
TEST(degenerate_and_absurd_triangles_are_ignored)
{
    Session session(32, 32);
    ImGui::NewFrame();
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    const float nan = std::nanf("");
    const float inf = std::numeric_limits<float>::infinity();
    raw_triangle(list, ImVec2(1, 1), ImVec2(1, 1), ImVec2(1, 1), red);
    raw_triangle(list, ImVec2(1, 1), ImVec2(5, 5), ImVec2(9, 9), red);
    raw_triangle(list, ImVec2(nan, 1), ImVec2(5, 5), ImVec2(9, 1), red);
    raw_triangle(list, ImVec2(1, 1), ImVec2(inf, 5), ImVec2(9, 1), red);
    raw_triangle(list, ImVec2(1, 1), ImVec2(1e30f, 5), ImVec2(9, 1), red);
    raw_triangle(list, ImVec2(-1e30f, -1e30f), ImVec2(1e30f, -1e30f), ImVec2(0, 1e30f), red);
    const soft_raster::Image image = session.finish();
    CHECK_EQ(coverage(image, black).count, 0);
    CHECK_EQ(session.renderer.stats().triangles, 0);

    // A triangle far larger than the target, but a real one, is clipped to it.
    ImGui::NewFrame();
    list = ImGui::GetBackgroundDrawList();
    list->PushClipRectFullScreen();
    raw_triangle(list, ImVec2(-90000, -90000), ImVec2(90000, -90000), ImVec2(0, 90000), red);
    list->PopClipRect();
    const soft_raster::Image filled = session.finish();
    check_exact_rect(filled, 0, 0, 32, 32, red, black);
}

// --- Writing ---------------------------------------------------------------------

namespace
{

uint32_t be32(const uint8_t* p)
{
    return (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | uint32_t{p[3]};
}

// Undoes encode_png by the PNG and zlib specifications, for what it writes:
// stored blocks only. Checks every length, every CRC and the Adler-32.
bool decode_stored_png(const std::vector<uint8_t>& bytes, soft_raster::Image& out)
{
    const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (bytes.size() < 8 || std::memcmp(bytes.data(), signature, 8) != 0)
        return false;
    std::vector<uint8_t> zlib;
    size_t at = 8;
    bool ended = false;
    while (at + 12 <= bytes.size()) {
        const uint32_t length = be32(&bytes[at]);
        if (at + 12 + length > bytes.size())
            return false;
        const uint8_t* type = &bytes[at + 4];
        const uint8_t* body = &bytes[at + 8];
        if (soft_raster::png_detail::crc32(type, 4 + length) != be32(body + length))
            return false;
        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (length != 13 || body[8] != 8 || body[9] != 6 || body[10] != 0 || body[11] != 0 ||
                body[12] != 0)
                return false;
            out =
                soft_raster::Image(static_cast<int>(be32(body)), static_cast<int>(be32(body + 4)));
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            zlib.insert(zlib.end(), body, body + length);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            ended = true;
        }
        at += 12 + length;
    }
    if (!ended || at != bytes.size() || zlib.size() < 6)
        return false;
    if (zlib[0] != 0x78 || ((zlib[0] << 8) | zlib[1]) % 31 != 0)
        return false;
    std::vector<uint8_t> raw;
    size_t z = 2;
    for (;;) {
        if (z + 5 > zlib.size())
            return false;
        const uint8_t header = zlib[z];
        if ((header & 0x06) != 0)
            return false; // not a stored block
        const unsigned length = zlib[z + 1] | (unsigned{zlib[z + 2]} << 8);
        const unsigned check = zlib[z + 3] | (unsigned{zlib[z + 4]} << 8);
        if ((length ^ check) != 0xFFFF || z + 5 + length > zlib.size())
            return false;
        raw.insert(raw.end(), zlib.begin() + static_cast<std::ptrdiff_t>(z + 5),
                   zlib.begin() + static_cast<std::ptrdiff_t>(z + 5 + length));
        z += 5 + length;
        if ((header & 1) != 0)
            break;
    }
    if (z + 4 != zlib.size())
        return false;
    uint32_t a = 1;
    uint32_t b = 0;
    for (const uint8_t byte : raw) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    if (((b << 16) | a) != be32(&zlib[z]))
        return false;
    const size_t stride = static_cast<size_t>(out.width) * 4u;
    if (raw.size() != (stride + 1) * static_cast<size_t>(out.height))
        return false;
    for (int y = 0; y < out.height; ++y) {
        const uint8_t* row = raw.data() + static_cast<size_t>(y) * (stride + 1);
        if (row[0] != 0)
            return false;
        if (stride != 0)
            std::memcpy(out.pixel(0, y), row + 1, stride);
    }
    return true;
}

soft_raster::Image patterned(int width, int height)
{
    soft_raster::Image image(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint8_t* p = image.pixel(x, y);
            p[0] = static_cast<uint8_t>(x * 7 + y);
            p[1] = static_cast<uint8_t>(y * 13);
            p[2] = static_cast<uint8_t>(x ^ y);
            p[3] = static_cast<uint8_t>(255 - x);
        }
    }
    return image;
}

} // namespace

TEST(png_crc_is_the_specifications)
{
    // The check value every CRC-32 implementation is tested with.
    const uint8_t digits[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK_EQ(soft_raster::png_detail::crc32(digits, sizeof digits), 0xCBF43926u);
    // The IEND chunk of every PNG: type and no data.
    const uint8_t iend[] = {'I', 'E', 'N', 'D'};
    CHECK_EQ(soft_raster::png_detail::crc32(iend, sizeof iend), 0xAE426082u);
}

TEST(a_png_decodes_to_the_image_it_was_made_from)
{
    // 200 x 100 is 80,100 bytes of scanlines: more than one stored block.
    for (const auto& size : {std::pair<int, int>{1, 1}, {3, 2}, {200, 100}, {0, 0}, {5, 0}}) {
        CHECK_CONTEXT("%d x %d", size.first, size.second);
        const soft_raster::Image image = patterned(size.first, size.second);
        const std::vector<uint8_t> bytes = soft_raster::encode_png(image);
        soft_raster::Image back;
        if (!CHECK(decode_stored_png(bytes, back)))
            continue;
        CHECK_EQ(back.width, image.width);
        CHECK_EQ(back.height, image.height);
        CHECK(back.rgba == image.rgba);
    }
}

TEST(images_are_written_to_files)
{
    const soft_raster::Image image = patterned(17, 9);
    const char* png_path = "soft_raster_test_output.png";
    const char* ppm_path = "soft_raster_test_output.ppm";
    REQUIRE(soft_raster::write_png(png_path, image));
    REQUIRE(soft_raster::write_ppm(ppm_path, image));

    std::vector<uint8_t> png;
    REQUIRE(golden::read_file(png_path, png));
    CHECK(png == soft_raster::encode_png(image));

    std::vector<uint8_t> ppm;
    REQUIRE(golden::read_file(ppm_path, ppm));
    const char header[] = "P6\n17 9\n255\n";
    const size_t header_size = sizeof header - 1;
    REQUIRE_EQ(ppm.size(), header_size + 17u * 9u * 3u);
    CHECK(std::memcmp(ppm.data(), header, header_size) == 0);
    long wrong = 0;
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 17; ++x)
            for (int c = 0; c < 3; ++c)
                wrong += ppm[header_size + static_cast<size_t>((y * 17 + x) * 3 + c)] !=
                         image.pixel(x, y)[c];
    CHECK_EQ(wrong, 0);

    std::remove(png_path);
    std::remove(ppm_path);
    CHECK(!soft_raster::write_png("no-such-directory/x.png", image));
    CHECK(!soft_raster::write_ppm("no-such-directory/x.ppm", image));
}
