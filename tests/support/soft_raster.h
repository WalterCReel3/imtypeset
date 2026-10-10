// A software renderer backend for Dear ImGui: ImDrawData in, an RGBA8 image out.
// For tests and examples in an environment with no display and no GPU, so that
// "it renders" can mean pixels someone looked at.
//
//     soft_raster::Renderer renderer;
//     renderer.init();                           // after ImGui::CreateContext()
//     ...
//     ImGui::Render();
//     soft_raster::Image image(800, 600, IM_COL32(255, 255, 255, 255));
//     renderer.render(ImGui::GetDrawData(), image);
//     soft_raster::write_png("frame.png", image);
//     ...
//     renderer.shutdown();                       // before ImGui::DestroyContext()
//
// It is written to behave as the stock GPU backends do, read from
// backends/imgui_impl_opengl3.cpp and docs/BACKENDS.md at the pinned tag, so
// that what it shows is what a host would see:
//
//   - Textures follow the 1.92 protocol (ImGuiBackendFlags_RendererHasTextures):
//     every ImTextureData in ImDrawData::Textures whose status is not OK is
//     created, updated or destroyed, and acknowledged with SetTexID/SetStatus.
//     The renderer keeps its own copy of the pixels, as a GPU does, so a request
//     that was not honoured shows as wrong pixels and not as a lucky read of
//     ImGui's buffer.
//   - Triangles are filled by the top-left rule with pixel centres at
//     (x + 0.5, y + 0.5): a pixel is covered if its centre is inside, or on a
//     top or a left edge. Vertex positions are snapped to 1/256 of a pixel and
//     coverage is decided in integers, so two triangles sharing an edge cover
//     every pixel along it exactly once.
//   - The scissor rectangle of each ImDrawCmd is projected and truncated to
//     integers exactly as the OpenGL3 backend does.
//   - Sampling is bilinear with clamp-to-edge, texel centres at
//     (i + 0.5) / size, which the font atlas expects. A quad whose corners are on
//     pixel corners and whose UVs are on texel corners therefore reproduces the
//     texels one for one. Nearest sampling is available through
//     ImGuiPlatformIO::DrawCallback_SetSamplerNearest.
//   - Colour is vertex colour times texel, blended as the backends blend:
//     rgb = src.rgb * src.a + dst.rgb * (1 - src.a),
//     a   = src.a + dst.a * (1 - src.a), in 8 bits per channel, no gamma.
//     Clear the image to an opaque colour for the result to be a picture.
//
// The texture state machine in update_texture() and the scissor projection in
// render() follow Dear ImGui's docs/BACKENDS.md and
// backends/imgui_impl_opengl3.cpp, which are Copyright (c) 2014-2026 Omar
// Cornut and under the MIT License (LICENSE.txt in the ImGui distribution).
// The rasteriser, the sampler and the image writers are not derived from them.
//
// Not done: anything a backend with shaders could be asked for beyond the
// above. A draw command naming a texture this renderer was never given is
// skipped and counted in Stats::commands_without_texture.

#pragma once

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace soft_raster
{

// Straight RGBA, 8 bits per channel, rows top to bottom.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;

    Image() = default;
    Image(int w, int h, ImU32 colour = IM_COL32(0, 0, 0, 255))
        : width(w > 0 ? w : 0)
        , height(h > 0 ? h : 0)
        , rgba(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u)
    {
        clear(colour);
    }

    void clear(ImU32 colour)
    {
        const uint8_t r = static_cast<uint8_t>((colour >> IM_COL32_R_SHIFT) & 0xFF);
        const uint8_t g = static_cast<uint8_t>((colour >> IM_COL32_G_SHIFT) & 0xFF);
        const uint8_t b = static_cast<uint8_t>((colour >> IM_COL32_B_SHIFT) & 0xFF);
        const uint8_t a = static_cast<uint8_t>((colour >> IM_COL32_A_SHIFT) & 0xFF);
        for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
            rgba[i] = r;
            rgba[i + 1] = g;
            rgba[i + 2] = b;
            rgba[i + 3] = a;
        }
    }

    bool contains(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }

    uint8_t* pixel(int x, int y)
    {
        return rgba.data() +
               (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
    }
    const uint8_t* pixel(int x, int y) const
    {
        return rgba.data() +
               (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
    }

    // The pixel as an IM_COL32, or 0 outside the image.
    ImU32 get(int x, int y) const
    {
        if (!contains(x, y))
            return 0;
        const uint8_t* p = pixel(x, y);
        return IM_COL32(p[0], p[1], p[2], p[3]);
    }
};

// A texture as the renderer holds it: its own RGBA8 copy.
struct Texture {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

// Counts since the Renderer was made, for tests to assert on.
struct Stats {
    long textures_created = 0;
    long texture_updates = 0; // WantUpdates requests honoured, not rectangles
    long textures_destroyed = 0;
    long draw_commands = 0;
    long triangles = 0;
    long pixels_shaded = 0; // fragments that passed coverage and scissor
    long commands_without_texture = 0;
};

class Renderer
{
public:
    Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Announces to the current ImGui context what this renderer honours. Call
    // once, after ImGui::CreateContext() and before the first NewFrame().
    void init()
    {
        ImGuiIO& io = ImGui::GetIO();
        IM_ASSERT(io.BackendRendererUserData == nullptr &&
                  "a renderer backend is already attached");
        io.BackendRendererUserData = this;
        io.BackendRendererName = "soft_raster";
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
#if IMGUI_VERSION_NUM >= 19274 // where ImGuiPlatformIO got the three callbacks
        ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
        platform_io.DrawCallback_ResetRenderState = &reset_render_state;
        platform_io.DrawCallback_SetSamplerLinear = &set_sampler_linear;
        platform_io.DrawCallback_SetSamplerNearest = &set_sampler_nearest;
#endif
    }

    // Destroys every texture ImGui still holds and detaches. Call before
    // ImGui::DestroyContext().
    void shutdown()
    {
        for (ImTextureData* tex : ImGui::GetPlatformIO().Textures)
            if (tex->RefCount == 1 && tex->TexID != ImTextureID_Invalid)
                destroy_texture(tex);
        ImGuiIO& io = ImGui::GetIO();
        io.BackendRendererUserData = nullptr;
        io.BackendRendererName = nullptr;
        io.BackendFlags &=
            ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
#if IMGUI_VERSION_NUM >= 19274
        ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
        platform_io.DrawCallback_ResetRenderState = nullptr;
        platform_io.DrawCallback_SetSamplerLinear = nullptr;
        platform_io.DrawCallback_SetSamplerNearest = nullptr;
#endif
    }

    // Honours the texture requests in draw_data->Textures, then draws every
    // command list into `target`. The target is not cleared.
    void render(ImDrawData* draw_data, Image& target)
    {
        if (draw_data == nullptr)
            return;
        if (draw_data->Textures != nullptr)
            for (ImTextureData* tex : *draw_data->Textures)
                if (tex->Status != ImTextureStatus_OK)
                    update_texture(tex);

        const ImVec2 clip_off = draw_data->DisplayPos;
        const ImVec2 clip_scale = draw_data->FramebufferScale;
        _nearest = false;
        for (const ImDrawList* draw_list : draw_data->CmdLists) {
            for (const ImDrawCmd& cmd : draw_list->CmdBuffer) {
                if (cmd.UserCallback != nullptr) {
                    if (cmd.UserCallback == &reset_render_state ||
                        cmd.UserCallback == &set_sampler_linear)
                        _nearest = false;
                    else if (cmd.UserCallback == &set_sampler_nearest)
                        _nearest = true;
                    else
                        cmd.UserCallback(draw_list, &cmd);
                    continue;
                }

                // As the OpenGL3 backend projects and truncates it.
                const ImVec2 clip_min((cmd.ClipRect.x - clip_off.x) * clip_scale.x,
                                      (cmd.ClipRect.y - clip_off.y) * clip_scale.y);
                const ImVec2 clip_max((cmd.ClipRect.z - clip_off.x) * clip_scale.x,
                                      (cmd.ClipRect.w - clip_off.y) * clip_scale.y);
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                    continue;
                Scissor scissor;
                scissor.x0 = static_cast<int>(clip_min.x);
                scissor.y0 = static_cast<int>(clip_min.y);
                scissor.x1 = scissor.x0 + static_cast<int>(clip_max.x - clip_min.x);
                scissor.y1 = scissor.y0 + static_cast<int>(clip_max.y - clip_min.y);
                scissor.x0 = std::max(scissor.x0, 0);
                scissor.y0 = std::max(scissor.y0, 0);
                scissor.x1 = std::min(scissor.x1, target.width);
                scissor.y1 = std::min(scissor.y1, target.height);
                if (scissor.x1 <= scissor.x0 || scissor.y1 <= scissor.y0)
                    continue;

                ++_stats.draw_commands;
                const Texture* texture = find(cmd.GetTexID());
                if (texture == nullptr) {
                    ++_stats.commands_without_texture;
                    continue;
                }

                const ImDrawVert* vertices = draw_list->VtxBuffer.Data + cmd.VtxOffset;
                const ImDrawIdx* indices = draw_list->IdxBuffer.Data + cmd.IdxOffset;
                for (unsigned int i = 0; i + 2 < cmd.ElemCount; i += 3) {
                    const ImDrawVert* tri[3] = {&vertices[indices[i]], &vertices[indices[i + 1]],
                                                &vertices[indices[i + 2]]};
                    triangle(tri, clip_off, clip_scale, scissor, *texture, target);
                }
            }
        }
    }

    // A texture of the caller's own, for ImGui::Image and ImDrawList::AddImage.
    // `rgba` is width * height * 4 bytes and is copied.
    ImTextureID add_texture(int width, int height, const uint8_t* rgba)
    {
        auto texture = std::make_unique<Texture>();
        texture->width = width;
        texture->height = height;
        texture->rgba.assign(rgba,
                             rgba + static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
        _textures.push_back(std::move(texture));
        return static_cast<ImTextureID>(_textures.size());
    }

    // The renderer's copy of a texture, or null if the identifier is not one of
    // its own or the texture has been destroyed.
    const Texture* find(ImTextureID id) const
    {
        if (id == ImTextureID_Invalid || id > static_cast<ImTextureID>(_textures.size()))
            return nullptr;
        return _textures[static_cast<size_t>(id) - 1].get();
    }

    const Stats& stats() const
    {
        return _stats;
    }

private:
    struct Scissor {
        int x0, y0, x1, y1; // half-open, already inside the target
    };

    // Identifiers only: the render loop recognises them by address.
    static void reset_render_state(const ImDrawList*, const ImDrawCmd*) {}
    static void set_sampler_linear(const ImDrawList*, const ImDrawCmd*) {}
    static void set_sampler_nearest(const ImDrawList*, const ImDrawCmd*) {}

    // Copies a rectangle of ImGui's buffer into ours, converting Alpha8 to
    // white with that alpha, which is what such an atlas means.
    static void upload(const ImTextureData* tex, Texture& texture, int x, int y, int w, int h)
    {
        x = std::max(x, 0);
        y = std::max(y, 0);
        w = std::min(w, std::min(tex->Width, texture.width) - x);
        h = std::min(h, std::min(tex->Height, texture.height) - y);
        for (int row = 0; row < h; ++row) {
            const unsigned char* src =
                tex->Pixels + (static_cast<size_t>(y + row) * static_cast<size_t>(tex->Width) +
                               static_cast<size_t>(x)) *
                                  static_cast<size_t>(tex->BytesPerPixel);
            uint8_t* dst = texture.rgba.data() +
                           (static_cast<size_t>(y + row) * static_cast<size_t>(texture.width) +
                            static_cast<size_t>(x)) *
                               4u;
            if (tex->Format == ImTextureFormat_RGBA32) {
                std::memcpy(dst, src, static_cast<size_t>(w) * 4u);
            } else {
                for (int col = 0; col < w; ++col) {
                    dst[col * 4 + 0] = 255;
                    dst[col * 4 + 1] = 255;
                    dst[col * 4 + 2] = 255;
                    dst[col * 4 + 3] = src[col];
                }
            }
        }
    }

    void update_texture(ImTextureData* tex)
    {
        if (tex->Status == ImTextureStatus_WantCreate) {
            IM_ASSERT(tex->TexID == ImTextureID_Invalid && tex->BackendUserData == nullptr);
            auto texture = std::make_unique<Texture>();
            texture->width = tex->Width;
            texture->height = tex->Height;
            texture->rgba.assign(
                static_cast<size_t>(tex->Width) * static_cast<size_t>(tex->Height) * 4u, 0);
            upload(tex, *texture, 0, 0, tex->Width, tex->Height);
            _textures.push_back(std::move(texture));
            tex->SetTexID(static_cast<ImTextureID>(_textures.size()));
            tex->SetStatus(ImTextureStatus_OK);
            ++_stats.textures_created;
        } else if (tex->Status == ImTextureStatus_WantUpdates) {
            Texture* texture = mutable_find(tex->TexID);
            IM_ASSERT(texture != nullptr && "an update for a texture this renderer does not hold");
            if (texture != nullptr)
                for (const ImTextureRect& r : tex->Updates)
                    upload(tex, *texture, r.x, r.y, r.w, r.h);
            tex->SetStatus(ImTextureStatus_OK);
            ++_stats.texture_updates;
        } else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
            destroy_texture(tex);
        }
    }

    void destroy_texture(ImTextureData* tex)
    {
        const ImTextureID id = tex->TexID;
        if (id != ImTextureID_Invalid && id <= static_cast<ImTextureID>(_textures.size())) {
            // The slot stays, empty, so identifiers handed out earlier never
            // come to name a different texture.
            _textures[static_cast<size_t>(id) - 1].reset();
            ++_stats.textures_destroyed;
        }
        tex->SetTexID(ImTextureID_Invalid);
        tex->SetStatus(ImTextureStatus_Destroyed);
    }

    Texture* mutable_find(ImTextureID id)
    {
        if (id == ImTextureID_Invalid || id > static_cast<ImTextureID>(_textures.size()))
            return nullptr;
        return _textures[static_cast<size_t>(id) - 1].get();
    }

    static double texel(const Texture& texture, int x, int y, int channel)
    {
        x = std::clamp(x, 0, texture.width - 1);
        y = std::clamp(y, 0, texture.height - 1);
        return texture.rgba[(static_cast<size_t>(y) * static_cast<size_t>(texture.width) +
                             static_cast<size_t>(x)) *
                                4u +
                            static_cast<size_t>(channel)];
    }

    // The texture at (u, v) as four channels in 0..255.
    void sample(const Texture& texture, double u, double v, double out[4]) const
    {
        if (texture.width <= 0 || texture.height <= 0) {
            out[0] = out[1] = out[2] = out[3] = 0.0;
            return;
        }
        if (_nearest) {
            const int x = static_cast<int>(std::floor(u * texture.width));
            const int y = static_cast<int>(std::floor(v * texture.height));
            for (int c = 0; c < 4; ++c)
                out[c] = texel(texture, x, y, c);
            return;
        }
        const double fx = u * texture.width - 0.5;
        const double fy = v * texture.height - 0.5;
        const double x0 = std::floor(fx);
        const double y0 = std::floor(fy);
        const double tx = fx - x0;
        const double ty = fy - y0;
        const int ix = static_cast<int>(x0);
        const int iy = static_cast<int>(y0);
        for (int c = 0; c < 4; ++c) {
            const double top =
                texel(texture, ix, iy, c) * (1.0 - tx) + texel(texture, ix + 1, iy, c) * tx;
            const double bottom =
                texel(texture, ix, iy + 1, c) * (1.0 - tx) + texel(texture, ix + 1, iy + 1, c) * tx;
            out[c] = top * (1.0 - ty) + bottom * ty;
        }
    }

    static uint8_t to_byte(double value)
    {
        const double rounded = std::floor(value + 0.5);
        return static_cast<uint8_t>(std::clamp(rounded, 0.0, 255.0));
    }

    // Twice the signed area of (a, b, p), in 1/256-pixel units squared.
    static int64_t orient(int64_t ax, int64_t ay, int64_t bx, int64_t by, int64_t px, int64_t py)
    {
        return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
    }

    // Whether a pixel centre lying exactly on the edge a->b of a triangle whose
    // third vertex is c belongs to the triangle. With y down: a left edge has
    // the interior to its right; a top edge is horizontal with the interior
    // below. Stated from the geometry, so it does not depend on winding.
    static bool owns_edge(int64_t ax, int64_t ay, int64_t bx, int64_t by, int64_t cx, int64_t cy)
    {
        if (ay == by)
            return cy > ay;
        // Where the edge's line is at c's height, compared with c, without
        // dividing: interior to the right means c.x is greater.
        const int64_t lhs = (cx - ax) * (by - ay) - (bx - ax) * (cy - ay);
        return by > ay ? lhs > 0 : lhs < 0;
    }

    void triangle(const ImDrawVert* const tri[3], const ImVec2& offset, const ImVec2& scale,
                  const Scissor& scissor, const Texture& texture, Image& target)
    {
        constexpr double subpixel = 256.0;
        // Beyond this a coordinate is not a position on any target this is for,
        // and keeping it out keeps the products below inside 64 bits.
        constexpr double limit = 1.0e6;
        int64_t x[3];
        int64_t y[3];
        for (int i = 0; i < 3; ++i) {
            const double fx = (static_cast<double>(tri[i]->pos.x) - static_cast<double>(offset.x)) *
                              static_cast<double>(scale.x);
            const double fy = (static_cast<double>(tri[i]->pos.y) - static_cast<double>(offset.y)) *
                              static_cast<double>(scale.y);
            if (!(std::fabs(fx) < limit) || !(std::fabs(fy) < limit))
                return; // also rejects NaN
            x[i] = static_cast<int64_t>(std::floor(fx * subpixel + 0.5));
            y[i] = static_cast<int64_t>(std::floor(fy * subpixel + 0.5));
        }
        const int64_t area = orient(x[0], y[0], x[1], y[1], x[2], y[2]);
        if (area == 0)
            return;
        ++_stats.triangles;
        // Both windings are drawn, as with culling off. `sign` makes the three
        // edge functions positive inside whichever it is.
        const int64_t sign = area > 0 ? 1 : -1;
        const bool owns[3] = {
            owns_edge(x[1], y[1], x[2], y[2], x[0], y[0]),
            owns_edge(x[2], y[2], x[0], y[0], x[1], y[1]),
            owns_edge(x[0], y[0], x[1], y[1], x[2], y[2]),
        };

        const int64_t min_x = std::min({x[0], x[1], x[2]});
        const int64_t max_x = std::max({x[0], x[1], x[2]});
        const int64_t min_y = std::min({y[0], y[1], y[2]});
        const int64_t max_y = std::max({y[0], y[1], y[2]});
        // Pixels whose centre (px * 256 + 128) can lie within the bounds.
        const int px0 = std::max(scissor.x0, static_cast<int>(floor_div(min_x - 128 + 255, 256)));
        const int px1 = std::min(scissor.x1, static_cast<int>(floor_div(max_x - 128, 256)) + 1);
        const int py0 = std::max(scissor.y0, static_cast<int>(floor_div(min_y - 128 + 255, 256)));
        const int py1 = std::min(scissor.y1, static_cast<int>(floor_div(max_y - 128, 256)) + 1);

        double colour[3][4];
        for (int i = 0; i < 3; ++i) {
            const ImU32 c = tri[i]->col;
            colour[i][0] = static_cast<double>((c >> IM_COL32_R_SHIFT) & 0xFF);
            colour[i][1] = static_cast<double>((c >> IM_COL32_G_SHIFT) & 0xFF);
            colour[i][2] = static_cast<double>((c >> IM_COL32_B_SHIFT) & 0xFF);
            colour[i][3] = static_cast<double>((c >> IM_COL32_A_SHIFT) & 0xFF);
        }
        const bool flat = tri[0]->col == tri[1]->col && tri[1]->col == tri[2]->col;
        const double inverse_area = 1.0 / static_cast<double>(area * sign);

        for (int py = py0; py < py1; ++py) {
            const int64_t cy = static_cast<int64_t>(py) * 256 + 128;
            for (int px = px0; px < px1; ++px) {
                const int64_t cx = static_cast<int64_t>(px) * 256 + 128;
                const int64_t w[3] = {
                    sign * orient(x[1], y[1], x[2], y[2], cx, cy),
                    sign * orient(x[2], y[2], x[0], y[0], cx, cy),
                    sign * orient(x[0], y[0], x[1], y[1], cx, cy),
                };
                if (w[0] < 0 || w[1] < 0 || w[2] < 0)
                    continue;
                if ((w[0] == 0 && !owns[0]) || (w[1] == 0 && !owns[1]) || (w[2] == 0 && !owns[2]))
                    continue;
                ++_stats.pixels_shaded;

                const double b0 = static_cast<double>(w[0]) * inverse_area;
                const double b1 = static_cast<double>(w[1]) * inverse_area;
                const double b2 = static_cast<double>(w[2]) * inverse_area;
                const double u = b0 * static_cast<double>(tri[0]->uv.x) +
                                 b1 * static_cast<double>(tri[1]->uv.x) +
                                 b2 * static_cast<double>(tri[2]->uv.x);
                const double v = b0 * static_cast<double>(tri[0]->uv.y) +
                                 b1 * static_cast<double>(tri[1]->uv.y) +
                                 b2 * static_cast<double>(tri[2]->uv.y);
                double texel_colour[4];
                sample(texture, u, v, texel_colour);

                double src[4];
                for (int c = 0; c < 4; ++c) {
                    const double vertex =
                        flat ? colour[0][c]
                             : b0 * colour[0][c] + b1 * colour[1][c] + b2 * colour[2][c];
                    src[c] = vertex * texel_colour[c] / 255.0; // 0..255
                }
                const double alpha = src[3] / 255.0;
                uint8_t* dst = target.pixel(px, py);
                dst[0] = to_byte(src[0] * alpha + dst[0] * (1.0 - alpha));
                dst[1] = to_byte(src[1] * alpha + dst[1] * (1.0 - alpha));
                dst[2] = to_byte(src[2] * alpha + dst[2] * (1.0 - alpha));
                dst[3] = to_byte(src[3] + dst[3] * (1.0 - alpha));
            }
        }
    }

    static int64_t floor_div(int64_t a, int64_t b)
    {
        const int64_t quotient = a / b;
        return (a % b != 0 && ((a < 0) != (b < 0))) ? quotient - 1 : quotient;
    }

    std::vector<std::unique_ptr<Texture>> _textures; // identifier is index + 1
    Stats _stats;
    bool _nearest = false;
};

// --- Writing an image ---------------------------------------------------------

// Binary PPM (P6): red, green, blue; alpha is dropped. False if the file cannot
// be written.
inline bool write_ppm(const char* path, const Image& image)
{
    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr)
        return false;
    bool ok = std::fprintf(f, "P6\n%d %d\n255\n", image.width, image.height) > 0;
    std::vector<uint8_t> row(static_cast<size_t>(image.width) * 3u);
    for (int y = 0; ok && y < image.height; ++y) {
        const uint8_t* src = image.pixel(0, y);
        for (int x = 0; x < image.width; ++x) {
            row[static_cast<size_t>(x) * 3u + 0] = src[x * 4 + 0];
            row[static_cast<size_t>(x) * 3u + 1] = src[x * 4 + 1];
            row[static_cast<size_t>(x) * 3u + 2] = src[x * 4 + 2];
        }
        ok = row.empty() || std::fwrite(row.data(), 1, row.size(), f) == row.size();
    }
    return std::fclose(f) == 0 && ok;
}

namespace png_detail
{

inline uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc = 0)
{
    crc = ~crc;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

inline void put32(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value >> 24));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

inline void chunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& body)
{
    put32(out, static_cast<uint32_t>(body.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), body.begin(), body.end());
    put32(out, crc32(out.data() + start, out.size() - start));
}

} // namespace png_detail

// The image as the bytes of a PNG: 8-bit RGBA, no filtering, and the pixel data
// in stored (uncompressed) deflate blocks, so there is no compressor to carry.
// About four bytes a pixel.
inline std::vector<uint8_t> encode_png(const Image& image)
{
    using namespace png_detail;
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

    std::vector<uint8_t> header;
    put32(header, static_cast<uint32_t>(image.width));
    put32(header, static_cast<uint32_t>(image.height));
    header.insert(header.end(), {8, 6, 0, 0, 0}); // depth, RGBA, deflate, adaptive, no interlace
    chunk(out, "IHDR", header);

    // Scanlines, each led by its filter type, 0.
    std::vector<uint8_t> raw;
    const size_t stride = static_cast<size_t>(image.width) * 4u;
    raw.reserve((stride + 1) * static_cast<size_t>(image.height));
    for (int y = 0; y < image.height; ++y) {
        raw.push_back(0);
        if (stride != 0)
            raw.insert(raw.end(), image.pixel(0, y), image.pixel(0, y) + stride);
    }

    // zlib: header, stored blocks of at most 65535 bytes, Adler-32 of the data.
    std::vector<uint8_t> data = {0x78, 0x01};
    size_t position = 0;
    do {
        const size_t length = std::min<size_t>(raw.size() - position, 65535);
        const bool last = position + length == raw.size();
        data.push_back(last ? 1 : 0);
        data.push_back(static_cast<uint8_t>(length & 0xFF));
        data.push_back(static_cast<uint8_t>(length >> 8));
        data.push_back(static_cast<uint8_t>(~length & 0xFF));
        data.push_back(static_cast<uint8_t>((~length >> 8) & 0xFF));
        data.insert(data.end(), raw.begin() + static_cast<std::ptrdiff_t>(position),
                    raw.begin() + static_cast<std::ptrdiff_t>(position + length));
        position += length;
    } while (position < raw.size());
    uint32_t a = 1;
    uint32_t b = 0;
    for (const uint8_t byte : raw) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    put32(data, (b << 16) | a);
    chunk(out, "IDAT", data);
    chunk(out, "IEND", {});
    return out;
}

// False if the file cannot be written.
inline bool write_png(const char* path, const Image& image)
{
    const std::vector<uint8_t> bytes = encode_png(image);
    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr)
        return false;
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    return std::fclose(f) == 0 && ok;
}

} // namespace soft_raster
