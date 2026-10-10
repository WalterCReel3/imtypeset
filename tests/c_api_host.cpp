// The C++ half of tests/test_c_api.c, which a C compiler builds and which
// therefore cannot hold an ImGui context. This file owns the headless ImGui
// frame, hands the C test the one ImGui object the C ABI takes (a draw list,
// as the pointer it is), and answers what the C++ API produces for the same
// input, so that the C test can hold the C ABI to it byte for byte.
//
// Everything here is declared again, by hand, at the top of test_c_api.c.

#include "imtypeset.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(__cpp_exceptions)
#include <stdexcept>
#endif

namespace
{

std::string g_path;

// ImGui's allocator, as this host installs it: malloc and free, and on request
// an exception out of one allocation. That is how the C test gets a C++
// exception raised inside a call of the C ABI, whatever the library and ImGui
// themselves assert on.
int g_throws = 0;
#if defined(__cpp_exceptions)
int g_throw_countdown = 0;
int g_throw_kind = 0;
#endif

void* host_alloc(size_t size, void*)
{
#if defined(__cpp_exceptions)
    if (g_throw_countdown > 0 && --g_throw_countdown == 0) {
        ++g_throws;
        if (g_throw_kind == 0)
            throw std::runtime_error("c_api_host: the allocator refused");
        throw 42;
    }
#endif
    return std::malloc(size);
}

void host_free(void* pointer, void*)
{
    std::free(pointer);
}

int copy_out(void* out, int capacity, const void* data, int count, size_t element_size)
{
    const int n = count < capacity ? count : capacity;
    if (out != nullptr && data != nullptr && n > 0)
        std::memcpy(out, data, static_cast<size_t>(n) * element_size);
    return count;
}

} // namespace

extern "C" {

// What this translation unit's ImGui says, for ImTypeset_GetBuildInfo() to be
// compared with: 0 ImWchar, 1 ImDrawVert, 2 ImDrawIdx, 3 FreeType, 4 the
// offset of ImDrawVert::col, 5 IMGUI_VERSION_NUM, 6 whether exceptions are on,
// 7 whether a failed IM_ASSERT throws (tests/imconfig_throwing.h), 8 and 9 the
// offsets of ImDrawVert::pos and ImDrawVert::uv.
int c_api_host_fact(int which)
{
    switch (which) {
    case 0:
        return static_cast<int>(sizeof(ImWchar));
    case 1:
        return static_cast<int>(sizeof(ImDrawVert));
    case 2:
        return static_cast<int>(sizeof(ImDrawIdx));
    case 3:
#ifdef IMGUI_ENABLE_FREETYPE
        return 1;
#else
        return 0;
#endif
    case 4:
        return static_cast<int>(offsetof(ImDrawVert, col));
    case 5:
        return IMGUI_VERSION_NUM;
    case 6:
#if defined(__cpp_exceptions)
        return 1;
#else
        return 0;
#endif
    case 7:
#if defined(IMTYPESET_TEST_IM_ASSERT_THROWS)
        return 1;
#else
        return 0;
#endif
    case 8:
        return static_cast<int>(offsetof(ImDrawVert, pos));
    case 9:
        return static_cast<int>(offsetof(ImDrawVert, uv));
    default:
        return -1;
    }
}

// Arms the allocator: the next allocation ImGui is asked for throws, a
// std::runtime_error for kind 0 and something that is not a std::exception
// otherwise. Returns 0, having done nothing, in a build without exceptions.
int c_api_host_throw_from_next_allocation(int kind)
{
#if defined(__cpp_exceptions)
    g_throw_kind = kind;
    g_throw_countdown = 1;
    return 1;
#else
    static_cast<void>(kind);
    return 0;
#endif
}

// How many allocations have thrown.
int c_api_host_throws(void)
{
    return g_throws;
}

// tests/data/<relative>, zero-terminated, valid until the next call.
const char* c_api_host_data_path(const char* relative)
{
    g_path = std::string(IMTYPESET_TEST_DATA_DIR) + "/" + relative;
    return g_path.c_str();
}

int c_api_host_create(void)
{
    IMGUI_CHECKVERSION();
    ImGui::SetAllocatorFunctions(host_alloc, host_free, nullptr);
    if (ImGui::CreateContext() == nullptr)
        return 0;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1024.0f, 768.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    return 1;
}

void c_api_host_destroy(void)
{
    ImGui::DestroyContext();
}

// NewFrame and one window, 700 pixels wide, that the widgets go into. Returns
// the window's draw list.
struct ImDrawList* c_api_host_begin_frame(void)
{
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f));
    ImGui::SetNextWindowSize(ImVec2(700.0f, 700.0f));
    ImGui::Begin("c-api", nullptr, ImGuiWindowFlags_NoSavedSettings);
    return ImGui::GetWindowDrawList();
}

void c_api_host_end_frame(void)
{
    ImGui::End();
    ImGui::Render();
}

// The size of the last item, as ImGui has it.
void c_api_host_item_size(float* out_width, float* out_height)
{
    const ImVec2 size = ImGui::GetItemRectSize();
    *out_width = size.x;
    *out_height = size.y;
}

int c_api_host_draw_list_vertex_count(const struct ImDrawList* draw_list)
{
    return draw_list->VtxBuffer.Size;
}

// Vertices [from, from + capacity) of a draw list, as bytes. Returns how many
// there are from `from` on.
int c_api_host_draw_list_vertices(const struct ImDrawList* draw_list, int from, void* out,
                                  int capacity)
{
    const int count = draw_list->VtxBuffer.Size - from;
    if (from < 0 || count <= 0)
        return 0;
    return copy_out(out, capacity, draw_list->VtxBuffer.Data + from, count, sizeof(ImDrawVert));
}

// --- The C++ API's answers ---------------------------------------------------------
//
// A paragraph made, set and laid out through ImTypeset:: with the context's
// style, for the C test to compare its own with. `runs` is `runs_count` times
// six numbers, an ImTypesetRun's fields in order as doubles, so that nothing
// here depends on the C header's struct.

void* c_api_host_reference_paragraph(const char* text, const char* text_end, const double* runs,
                                     int runs_count, int flags, float width)
{
    ImTypesetParagraph* paragraph = ImTypeset::CreateParagraph();
    if (paragraph == nullptr)
        return nullptr;
    ImVector<ImTypesetRun> converted;
    for (int i = 0; i < runs_count; ++i) {
        const double* f = runs + 6 * i;
        ImTypesetRun run;
        run.Start = static_cast<int>(f[0]);
        run.End = static_cast<int>(f[1]);
        run.Face = static_cast<ImTypesetFace>(f[2]);
        run.Scale = static_cast<float>(f[3]);
        run.Color = static_cast<ImU32>(f[4]);
        run.Flags = static_cast<ImTypesetRunFlags>(f[5]);
        converted.push_back(run);
    }
    ImTypeset::SetParagraphText(paragraph, text, text_end, converted.Data, converted.Size, flags);
    ImTypeset::LayoutParagraph(paragraph, width);
    return paragraph;
}

void c_api_host_reference_destroy(void* paragraph)
{
    ImTypeset::DestroyParagraph(static_cast<ImTypesetParagraph*>(paragraph));
}

// ImTypeset::GetParagraphSize and GetParagraphLineCount.
int c_api_host_reference_size(void* paragraph, float* out_width, float* out_height)
{
    const ImTypesetParagraph* p = static_cast<ImTypesetParagraph*>(paragraph);
    const ImVec2 size = ImTypeset::GetParagraphSize(p);
    *out_width = size.x;
    *out_height = size.y;
    return ImTypeset::GetParagraphLineCount(p);
}

// ImTypeset::CalcTextSize with the context's style.
void c_api_host_reference_calc(const char* text, const char* text_end, float wrap_width, int flags,
                               float* out_width, float* out_height)
{
    const ImVec2 size = ImTypeset::CalcTextSize(text, text_end, wrap_width, flags);
    *out_width = size.x;
    *out_height = size.y;
}

int c_api_host_reference_draw(void* paragraph, struct ImDrawList* draw_list, float x, float y,
                              int has_selection, int selection_start, int selection_end)
{
    const ImTypesetTextRange selection = {selection_start, selection_end};
    return ImTypeset::DrawParagraph(static_cast<ImTypesetParagraph*>(paragraph), draw_list,
                                    ImVec2(x, y), has_selection != 0 ? &selection : nullptr)
               ? 1
               : 0;
}

// ImTypeset::DrawParagraphToBuffers into two fresh ImVectors, copied out as
// bytes. Returns the C++ function's bool.
int c_api_host_reference_buffers(void* paragraph, float x, float y, const float* clip_rect,
                                 void* vertices, int vertices_capacity, void* indices,
                                 int indices_capacity, int* out_vertices_count,
                                 int* out_indices_count)
{
    ImVector<ImDrawVert> v;
    ImVector<ImDrawIdx> i;
    const bool ok = ImTypeset::DrawParagraphToBuffers(
        static_cast<ImTypesetParagraph*>(paragraph), ImVec2(x, y),
        ImVec4(clip_rect[0], clip_rect[1], clip_rect[2], clip_rect[3]), &v, &i);
    *out_vertices_count = copy_out(vertices, vertices_capacity, v.Data, v.Size, sizeof(ImDrawVert));
    *out_indices_count = copy_out(indices, indices_capacity, i.Data, i.Size, sizeof(ImDrawIdx));
    return ok ? 1 : 0;
}

// ImTypeset::AppendParagraphRangeText onto a buffer that holds `so_far`.
// Writes the whole buffer, zero-terminated, and returns what the C++ function
// returned: the bytes it appended.
int c_api_host_reference_copy(void* paragraph, int start, int end, const char* so_far, char* out,
                              int out_size)
{
    ImGuiTextBuffer buffer;
    buffer.append(so_far);
    const ImTypesetTextRange range = {start, end};
    const int appended = ImTypeset::AppendParagraphRangeText(
        static_cast<ImTypesetParagraph*>(paragraph), range, &buffer);
    std::snprintf(out, static_cast<size_t>(out_size), "%s", buffer.c_str());
    return appended;
}

} // extern "C"
