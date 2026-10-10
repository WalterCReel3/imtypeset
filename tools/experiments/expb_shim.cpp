// Experiment B: a C ABI over imtypeset for a Python process that already holds
// Dear ImGui inside imgui_bundle's extension module.
//
// This file is compiled into libimtypeset_expb.so together with a snapshot of
// src/*.cpp. Every ImGui symbol is left undefined in that library and is
// resolved, when it is loaded, against the bundle's _imgui_bundle.*.so. So the
// ImGui that runs is the bundle's build, and the ImGui this file and the
// library were COMPILED against is only a set of headers that has to describe
// the same structs. expb_check_layout() and expb_probe() are how that is
// tested before anything else is called.
//
// The bundle builds ImGui for Python with an IM_ASSERT that throws
// std::runtime_error (imgui_bundle_cmake/imgui_bundle_config.h). An exception
// leaving an extern "C" function called through ctypes ends the process, so
// every entry here catches. This file is therefore compiled with exceptions
// on; the library's own sources keep -fno-exceptions -fno-rtti, as the project
// builds them.
//
// It is an experiment on one machine against one wheel. It is not a way to
// distribute anything.

#include "imtypeset.h"

#include "font_loader.h"
#include "fontlib.h"
#include "support/soft_raster.h"

#include <cfloat>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <exception>
#include <new>
#include <string>
#include <vector>

#define EXPB_API extern "C" __attribute__((visibility("default")))

namespace
{

void put(char* out, int cap, const std::string& text)
{
    if (out == nullptr || cap <= 0)
        return;
    std::snprintf(out, static_cast<size_t>(cap), "%s", text.c_str());
}

std::string fmt(const char* format, ...)
{
    char buffer[2048];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    return buffer;
}

// How many leading bytes of a block a constructor touched: the block starts as
// a repeating pattern, and what is left of the pattern at the end was not
// written. ImGui's constructors for these types memset the whole object first,
// so the answer is the sizeof the code that RAN was compiled with.
template<typename Construct>
int touched_bytes(Construct construct)
{
    enum { CAP = 1 << 16 };
    static unsigned char block[CAP];
    for (int i = 0; i < CAP; ++i)
        block[i] = static_cast<unsigned char>(0xA5 ^ (i * 131));
    construct(static_cast<void*>(block));
    int last = -1;
    for (int i = 0; i < CAP; ++i)
        if (block[i] != static_cast<unsigned char>(0xA5 ^ (i * 131)))
            last = i;
    return last + 1;
}

soft_raster::Renderer* g_renderer = nullptr;

} // namespace

// What this library was compiled against, as JSON.
EXPB_API void expb_abi(char* out, int cap)
{
    std::string s = "{";
    s += fmt("\"IMGUI_VERSION\": \"%s\", \"IMGUI_VERSION_NUM\": %d, ", IMGUI_VERSION,
             IMGUI_VERSION_NUM);
#ifdef IMGUI_HAS_DOCK
    s += "\"IMGUI_HAS_DOCK\": true, ";
#else
    s += "\"IMGUI_HAS_DOCK\": false, ";
#endif
#ifdef IMGUI_USE_WCHAR32
    s += "\"IMGUI_USE_WCHAR32\": true, ";
#else
    s += "\"IMGUI_USE_WCHAR32\": false, ";
#endif
#ifdef IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    s += "\"IMGUI_DISABLE_OBSOLETE_FUNCTIONS\": true, ";
#else
    s += "\"IMGUI_DISABLE_OBSOLETE_FUNCTIONS\": false, ";
#endif
    s +=
        fmt("\"sizeof\": {\"ImGuiIO\": %zu, \"ImGuiStyle\": %zu, \"ImVec2\": %zu, "
            "\"ImVec4\": %zu, \"ImDrawVert\": %zu, \"ImDrawIdx\": %zu, \"ImWchar\": %zu, "
            "\"ImTextureID\": %zu, \"ImTextureRef\": %zu, \"ImDrawCmd\": %zu, "
            "\"ImDrawList\": %zu, \"ImFontConfig\": %zu, \"ImFont\": %zu, "
            "\"ImFontBaked\": %zu, \"ImFontGlyph\": %zu, \"ImFontAtlas\": %zu, "
            "\"ImDrawData\": %zu, \"ImGuiTextBuffer\": %zu, \"ImTextureData\": %zu, "
            "\"ImGuiPlatformIO\": %zu}, ",
            sizeof(ImGuiIO), sizeof(ImGuiStyle), sizeof(ImVec2), sizeof(ImVec4), sizeof(ImDrawVert),
            sizeof(ImDrawIdx), sizeof(ImWchar), sizeof(ImTextureID), sizeof(ImTextureRef),
            sizeof(ImDrawCmd), sizeof(ImDrawList), sizeof(ImFontConfig), sizeof(ImFont),
            sizeof(ImFontBaked), sizeof(ImFontGlyph), sizeof(ImFontAtlas), sizeof(ImDrawData),
            sizeof(ImGuiTextBuffer), sizeof(ImTextureData), sizeof(ImGuiPlatformIO));
    s += fmt("\"IMTYPESET_VERSION\": \"%s\"}", IMTYPESET_VERSION);
    put(out, cap, s);
}

// ImGui's own check, run by the bundle's ImGui on this library's compile-time
// sizes. 1: passed. 0: it returned false. -1: it raised (the bundle's
// IM_ASSERT), and `out` has what it said.
EXPB_API int expb_check_layout(char* out, int cap)
{
    try {
        const bool ok = ImGui::DebugCheckVersionAndDataLayout(
            IMGUI_VERSION, sizeof(ImGuiIO), sizeof(ImGuiStyle), sizeof(ImVec2), sizeof(ImVec4),
            sizeof(ImDrawVert), sizeof(ImDrawIdx));
        put(out, cap, ok ? "ok" : "returned false");
        return ok ? 1 : 0;
    } catch (const std::exception& e) {
        put(out, cap, e.what());
        return -1;
    }
}

// The sizes the bundle's ImGui was compiled with, for the types that check
// does not cover and this library reads or writes, next to the sizes this
// library was compiled with; and the defaults the bundle's ImFontConfig
// constructor wrote, read back through this library's idea of the fields.
// Needs no ImGui context.
EXPB_API int expb_probe_sizes(char* out, int cap)
{
    try {
        std::string s = "{";
        const int cfg = touched_bytes([](void* p) { new (p) ImFontConfig(); });
        s += fmt("\"ImFontConfig\": [%d, %zu], ", cfg, sizeof(ImFontConfig));
        const int list = touched_bytes([](void* p) { new (p) ImDrawList(nullptr); });
        s += fmt("\"ImDrawList\": [%d, %zu], ", list, sizeof(ImDrawList));
        const int font = touched_bytes([](void* p) { new (p) ImFont(); });
        s += fmt("\"ImFont\": [%d, %zu], ", font, sizeof(ImFont));
        const int baked = touched_bytes([](void* p) { new (p) ImFontBaked(); });
        s += fmt("\"ImFontBaked\": [%d, %zu], ", baked, sizeof(ImFontBaked));
        const int io = touched_bytes([](void* p) { new (p) ImGuiIO(); });
        s += fmt("\"ImGuiIO\": [%d, %zu], ", io, sizeof(ImGuiIO));
        const int data = touched_bytes([](void* p) { new (p) ImDrawData(); });
        s += fmt("\"ImDrawData\": [%d, %zu], ", data, sizeof(ImDrawData));

        ImFontConfig c;
        s +=
            fmt("\"ImFontConfig_defaults\": {\"FontDataOwnedByAtlas\": %d, "
                "\"GlyphMaxAdvanceX_is_FLT_MAX\": %d, \"RasterizerMultiply\": %g, "
                "\"RasterizerDensity\": %g, \"OversampleH\": %d, \"MergeMode\": %d, "
                "\"FontData_null\": %d, \"SizePixels\": %g}",
                c.FontDataOwnedByAtlas ? 1 : 0, c.GlyphMaxAdvanceX == FLT_MAX ? 1 : 0,
                static_cast<double>(c.RasterizerMultiply), static_cast<double>(c.RasterizerDensity),
                static_cast<int>(c.OversampleH), c.MergeMode ? 1 : 0, c.FontData == nullptr ? 1 : 0,
                static_cast<double>(c.SizePixels));
        s += "}";
        put(out, cap, s);
        return 1;
    } catch (const std::exception& e) {
        put(out, cap, e.what());
        return -1;
    }
}

// Fields of the live ImGui context read through this library's headers, for
// Python to compare with what the bundle's own binding reads. Call after at
// least one frame has been rendered.
EXPB_API int expb_probe_context(char* out, int cap)
{
    try {
        ImGuiIO& io = ImGui::GetIO();
        ImGuiStyle& style = ImGui::GetStyle();
        const ImDrawData* dd = ImGui::GetDrawData();
        ImFont* font = ImGui::GetFont();
        ImFontBaked* baked = font != nullptr ? font->GetFontBaked(16.0f) : nullptr;
        const ImFontGlyph* glyph =
            baked != nullptr ? baked->FindGlyph(static_cast<ImWchar>('A')) : nullptr;
        std::string s = "{";
        s += fmt("\"io\": {\"DisplaySize\": [%g, %g], \"DeltaTime\": %.9g, \"BackendFlags\": %d, "
                 "\"ConfigFlags\": %d, \"FontGlobalScale_or_end\": %d, \"Framerate\": %.9g, "
                 "\"MetricsRenderVertices\": %d, \"MetricsRenderIndices\": %d, "
                 "\"MousePos\": [%g, %g], \"FontsCount\": %d, \"FontsLocked\": %d, "
                 "\"FontLoaderName\": \"%s\"}, ",
                 static_cast<double>(io.DisplaySize.x), static_cast<double>(io.DisplaySize.y),
                 static_cast<double>(io.DeltaTime), io.BackendFlags, io.ConfigFlags, 0,
                 static_cast<double>(io.Framerate), io.MetricsRenderVertices,
                 io.MetricsRenderIndices, static_cast<double>(io.MousePos.x),
                 static_cast<double>(io.MousePos.y), io.Fonts->Fonts.Size, io.Fonts->Locked ? 1 : 0,
                 io.Fonts->FontLoaderName != nullptr ? io.Fonts->FontLoaderName : "");
        s += fmt("\"style\": {\"Alpha\": %.9g, \"WindowPadding\": [%g, %g], "
                 "\"ItemSpacing\": [%g, %g], \"Text\": [%.9g, %.9g, %.9g, %.9g], "
                 "\"TextSelectedBg\": [%.9g, %.9g, %.9g, %.9g], \"last_colour\": [%.9g, %.9g]}, ",
                 static_cast<double>(style.Alpha), static_cast<double>(style.WindowPadding.x),
                 static_cast<double>(style.WindowPadding.y),
                 static_cast<double>(style.ItemSpacing.x), static_cast<double>(style.ItemSpacing.y),
                 static_cast<double>(style.Colors[ImGuiCol_Text].x),
                 static_cast<double>(style.Colors[ImGuiCol_Text].y),
                 static_cast<double>(style.Colors[ImGuiCol_Text].z),
                 static_cast<double>(style.Colors[ImGuiCol_Text].w),
                 static_cast<double>(style.Colors[ImGuiCol_TextSelectedBg].x),
                 static_cast<double>(style.Colors[ImGuiCol_TextSelectedBg].y),
                 static_cast<double>(style.Colors[ImGuiCol_TextSelectedBg].z),
                 static_cast<double>(style.Colors[ImGuiCol_TextSelectedBg].w),
                 static_cast<double>(style.Colors[ImGuiCol_COUNT - 1].x),
                 static_cast<double>(style.Colors[ImGuiCol_COUNT - 1].w));
        s += fmt("\"ImGuiCol_COUNT\": %d, \"ImGuiCol_Text\": %d, \"ImGuiCol_TextSelectedBg\": %d, ",
                 static_cast<int>(ImGuiCol_COUNT), static_cast<int>(ImGuiCol_Text),
                 static_cast<int>(ImGuiCol_TextSelectedBg));
        if (dd != nullptr) {
            s +=
                fmt("\"draw_data\": {\"Valid\": %d, \"CmdListsCount\": %d, \"TotalVtxCount\": %d, "
                    "\"TotalIdxCount\": %d, \"DisplaySize\": [%g, %g], \"lists\": [",
                    dd->Valid ? 1 : 0, dd->CmdLists.Size, dd->TotalVtxCount, dd->TotalIdxCount,
                    static_cast<double>(dd->DisplaySize.x), static_cast<double>(dd->DisplaySize.y));
            for (int i = 0; i < dd->CmdLists.Size; ++i) {
                const ImDrawList* l = dd->CmdLists[i];
                s += fmt(
                    "%s{\"vtx\": %d, \"idx\": %d, \"cmd\": %d, \"flags\": %d, "
                    "\"_VtxCurrentIdx\": %u, \"first_cmd_elem\": %u, "
                    "\"first_cmd_clip\": [%g, %g, %g, %g]}",
                    i ? ", " : "", l->VtxBuffer.Size, l->IdxBuffer.Size, l->CmdBuffer.Size,
                    static_cast<int>(l->Flags), l->_VtxCurrentIdx,
                    l->CmdBuffer.Size > 0 ? l->CmdBuffer[0].ElemCount : 0u,
                    l->CmdBuffer.Size > 0 ? static_cast<double>(l->CmdBuffer[0].ClipRect.x) : 0.0,
                    l->CmdBuffer.Size > 0 ? static_cast<double>(l->CmdBuffer[0].ClipRect.y) : 0.0,
                    l->CmdBuffer.Size > 0 ? static_cast<double>(l->CmdBuffer[0].ClipRect.z) : 0.0,
                    l->CmdBuffer.Size > 0 ? static_cast<double>(l->CmdBuffer[0].ClipRect.w) : 0.0);
            }
            s += "]}, ";
        }
        if (baked != nullptr && glyph != nullptr) {
            s += fmt("\"glyph_A\": {\"baked_Size\": %.9g, \"Ascent\": %.9g, \"Descent\": %.9g, "
                     "\"Visible\": %d, \"Colored\": %d, \"Codepoint\": %u, \"AdvanceX\": %.9g, "
                     "\"box\": [%.9g, %.9g, %.9g, %.9g], \"uv\": [%.9g, %.9g, %.9g, %.9g]}, ",
                     static_cast<double>(baked->Size), static_cast<double>(baked->Ascent),
                     static_cast<double>(baked->Descent), static_cast<int>(glyph->Visible),
                     static_cast<int>(glyph->Colored), static_cast<unsigned>(glyph->Codepoint),
                     static_cast<double>(glyph->AdvanceX), static_cast<double>(glyph->X0),
                     static_cast<double>(glyph->Y0), static_cast<double>(glyph->X1),
                     static_cast<double>(glyph->Y1), static_cast<double>(glyph->U0),
                     static_cast<double>(glyph->V0), static_cast<double>(glyph->U1),
                     static_cast<double>(glyph->V1));
        }
        const ImVec2 white = ImGui::GetFontTexUvWhitePixel();
        s += fmt("\"white_uv\": [%.9g, %.9g], \"font_size\": %.9g, \"frame_count\": %d}",
                 static_cast<double>(white.x), static_cast<double>(white.y),
                 static_cast<double>(ImGui::GetFontSize()), ImGui::GetFrameCount());
        put(out, cap, s);
        return 1;
    } catch (const std::exception& e) {
        put(out, cap, e.what());
        return -1;
    }
}

// The font src/font_loader hands to the atlas for a face — the file with its
// cmap rewritten in memory — written to `out_path`, and the codepoints minted
// for it listed in `out` as JSON. Uses no ImGui call.
EXPB_API int expb_write_atlas_font(const char* font_path, const char* out_path, int wchar_bits,
                                   char* out, int cap)
{
    using namespace ImTypeset::detail;
    try {
        FontLibrary library;
        std::string why;
        if (!library.register_font(Face::MATH, font_path, &why)) {
            put(out, cap, "register_font: " + why);
            return 0;
        }
        const FaceData* face = library.face_data(Face::MATH, &why);
        if (face == nullptr) {
            put(out, cap, "face_data: " + why);
            return 0;
        }
        std::vector<uint8_t> bytes;
        if (!build_atlas_font(*face, wchar_bits, bytes, &why)) {
            put(out, cap, "build_atlas_font: " + why);
            return 0;
        }
        std::FILE* f = std::fopen(out_path, "wb");
        if (f == nullptr) {
            put(out, cap, "cannot open the output file");
            return 0;
        }
        const bool wrote = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
        std::fclose(f);
        if (!wrote) {
            put(out, cap, "short write");
            return 0;
        }
        const std::vector<SyntheticCodepoint>& minted = face->synthetic_codepoints();
        std::string s = fmt("{\"bytes\": %zu, \"minted_count\": %zu, \"minted\": [", bytes.size(),
                            minted.size());
        // First, last and a few between: enough to look for in the atlas.
        const size_t step = minted.size() > 8 ? minted.size() / 8 : 1;
        bool first = true;
        for (size_t i = 0; i < minted.size(); i += step) {
            s += fmt("%s[%u, %u]", first ? "" : ", ", minted[i].codepoint, minted[i].glyph);
            first = false;
        }
        if (!minted.empty())
            s += fmt(", [%u, %u]", minted.back().codepoint, minted.back().glyph);
        s += "]}";
        put(out, cap, s);
        return 1;
    } catch (const std::exception& e) {
        put(out, cap, e.what());
        return -1;
    }
}

// --- The public API, as a binding would call it -------------------------------------

EXPB_API int expb_create_context(char* out, int cap)
{
    try {
        ImTypesetContext* ctx = ImTypeset::CreateContext();
        if (ctx == nullptr) {
            put(out, cap, ImTypeset::GetLastError()->Message);
            return 0;
        }
        put(out, cap, "ok");
        return 1;
    } catch (const std::exception& e) {
        put(out, cap, e.what());
        return -1;
    }
}

EXPB_API int expb_destroy_context(char* out, int cap)
{
    try {
        ImTypeset::DestroyContext();
        put(out, cap, "ok");
        return 1;
    } catch (const std::exception& e) {
        put(out, cap, e.what());
        return -1;
    }
}

EXPB_API int expb_add_font_file(int face, const char* path, char* out, int cap)
{
    try {
        ImFont* font = ImTypeset::AddFontFromFileTTF(face, path);
        if (font == nullptr) {
            put(out, cap, ImTypeset::GetLastError()->Message);
            return 0;
        }
        put(out, cap, "ok");
        return 1;
    } catch (const std::exception& e) {
        put(out, cap, e.what());
        return -1;
    }
}

EXPB_API int expb_add_font_memory(int face, const void* data, int size, char* out, int cap)
{
    try {
        ImFont* font = ImTypeset::AddFontFromMemoryTTF(face, data, size);
        if (font == nullptr) {
            put(out, cap, ImTypeset::GetLastError()->Message);
            return 0;
        }
        put(out, cap, "ok");
        return 1;
    } catch (const std::exception& e) {
        put(out, cap, e.what());
        return -1;
    }
}

EXPB_API int expb_enable_math(int enable)
{
    try {
        return ImTypeset::EnableMath(enable != 0) ? 1 : 0;
    } catch (const std::exception&) {
        return -1;
    }
}

EXPB_API int expb_set_style(float em_size, int align)
{
    try {
        ImTypesetStyle& style = ImTypeset::GetStyle();
        style.EmSize = em_size;
        style.Align = align;
        return 1;
    } catch (const std::exception&) {
        return -1;
    }
}

// The widget, inside a Begin()/End() pair made by Python.
// kind 0: Text(); 1: TextJustified().
EXPB_API int expb_text(int kind, const char* text, int flags, char* out, int cap)
{
    try {
        if (kind == 1)
            ImTypeset::TextJustified(text, nullptr, flags);
        else
            ImTypeset::Text(text, nullptr, flags);
        const ImTypesetErrorInfo* error = ImTypeset::GetLastError();
        put(out, cap, error->Code == ImTypesetError_None ? "ok" : error->Message);
        return 1;
    } catch (const std::exception& e) {
        put(out, cap, e.what());
        return -1;
    }
}

// Counters: text builds, layouts, draw calls, culled calls, vertices emitted,
// bytes compared, cached paragraphs.
EXPB_API int expb_stats(int* out7)
{
    try {
        const ImTypesetStats* s = ImTypeset::GetStats();
        if (s == nullptr)
            return 0;
        out7[0] = s->TextBuilds;
        out7[1] = s->Layouts;
        out7[2] = s->DrawCalls;
        out7[3] = s->CulledCalls;
        out7[4] = s->VerticesEmitted;
        out7[5] = s->BytesCompared;
        out7[6] = s->CachedParagraphs;
        return 1;
    } catch (const std::exception&) {
        return -1;
    }
}

// The paragraph path: create, set, lay out, draw into the current window's
// draw list at the cursor, copy all of it. Returns the line count, or -1.
EXPB_API int expb_paragraph_roundtrip(const char* text, float width, int flags, float* size_xy,
                                      char* copied, int cap)
{
    try {
        ImTypesetParagraph* p = ImTypeset::CreateParagraph();
        if (p == nullptr) {
            put(copied, cap, ImTypeset::GetLastError()->Message);
            return -1;
        }
        ImTypeset::SetParagraphText(p, text, nullptr, nullptr, 0, flags);
        const ImVec2 size = ImTypeset::LayoutParagraph(p, width);
        size_xy[0] = size.x;
        size_xy[1] = size.y;
        const int lines = ImTypeset::GetParagraphLineCount(p);
        ImTypesetSelection all;
        all.Start = 0;
        all.End = ImTypeset::GetParagraphLength(p);
        ImTypeset::DrawParagraph(p, ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), &all);
        ImGui::Dummy(size);
        ImGuiTextBuffer buffer;
        ImTypeset::AppendParagraphSelectionText(p, all, &buffer);
        put(copied, cap, buffer.c_str());
        ImTypeset::DestroyParagraph(p);
        return lines;
    } catch (const std::exception& e) {
        put(copied, cap, e.what());
        return -1;
    }
}

// --- A renderer, so there is something to look at ------------------------------------

// After the ImGui context exists and before the first frame. Stands in for a
// renderer backend: it announces RendererHasTextures and uploads the atlas.
EXPB_API int expb_raster_init(char* out, int cap)
{
    try {
        if (g_renderer == nullptr)
            g_renderer = new soft_raster::Renderer();
        g_renderer->init();
        put(out, cap, "ok");
        return 1;
    } catch (const std::exception& e) {
        put(out, cap, e.what());
        return -1;
    }
}

// After ImGui::Render(). `path` null renders and writes nothing, which is what
// keeps the atlas textures serviced on frames nobody looks at.
EXPB_API int expb_raster_frame(const char* path, int width, int height, char* out, int cap)
{
    try {
        if (g_renderer == nullptr) {
            put(out, cap, "expb_raster_init was not called");
            return 0;
        }
        // A colour no ImGui style uses, so that what is left of it is what
        // nothing drew on.
        soft_raster::Image image(width, height, IM_COL32(255, 0, 255, 255));
        g_renderer->render(ImGui::GetDrawData(), image);
        if (path != nullptr && !soft_raster::write_png(path, image)) {
            put(out, cap, "cannot write the image");
            return 0;
        }
        // How much of the frame something drew on, and how much of that is
        // light (text on ImGui's dark window): a blank image is a failure that
        // a file on disk would not show.
        long drawn = 0;
        long light = 0;
        for (size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
            if (image.rgba[i] != 255 || image.rgba[i + 1] != 0 || image.rgba[i + 2] != 255)
                ++drawn;
            if (image.rgba[i] > 160 && image.rgba[i + 1] > 160 && image.rgba[i + 2] > 160)
                ++light;
        }
        put(out, cap, fmt("{\"drawn_pixels\": %ld, \"light_pixels\": %ld}", drawn, light));
        return 1;
    } catch (const std::exception& e) {
        put(out, cap, e.what());
        return -1;
    }
}

EXPB_API int expb_raster_shutdown()
{
    try {
        if (g_renderer != nullptr) {
            g_renderer->shutdown();
            delete g_renderer;
            g_renderer = nullptr;
        }
        return 1;
    } catch (const std::exception&) {
        return -1;
    }
}
