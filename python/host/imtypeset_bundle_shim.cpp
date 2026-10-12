// What the bundle-hosted library adds to the C ABI: the means for
// python/imtypeset to decide, at import, whether the Dear ImGui this library
// was COMPILED against is laid out like the Dear ImGui that is in the process
// (Dear ImGui Bundle's module, whose symbols the library resolves at load).
//
// Two checks, because an installed wheel ships no headers:
//
//   ImTypesetBundle_CheckImGuiLayout   Dear ImGui's own IMGUI_CHECKVERSION():
//       this library's version string and six sizes, answered by the ImGui in
//       the process. Passes only when the headers are the wheel's.
//   ImTypesetBundle_LayoutFacts and the probes below
//       the offset and size, as compiled here, of every Dear ImGui field the
//       library reads or writes directly, for python/imtypeset/_bundle.py to
//       compare with what the wheel's ImGui does to live objects.
//
// Dear ImGui Bundle builds ImGui for Python with an IM_ASSERT that throws, and
// the first check reports a mismatch by asserting; so this file is compiled
// with exceptions enabled and catches. Nothing may unwind into ctypes.

#include "imgui.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <new>

#ifndef IMTYPESET_BUNDLE_HEADERS
#define IMTYPESET_BUNDLE_HEADERS "unknown"
#endif

// What the build script knows of the wheel this library is built for; empty
// where a build does not say.
#ifndef IMTYPESET_BUNDLE_WHEEL_VERSION
#define IMTYPESET_BUNDLE_WHEEL_VERSION ""
#endif
#ifndef IMTYPESET_BUNDLE_MODULE_NAME
#define IMTYPESET_BUNDLE_MODULE_NAME ""
#endif
#ifndef IMTYPESET_BUNDLE_MODULE_SHA256
#define IMTYPESET_BUNDLE_MODULE_SHA256 ""
#endif

#define IMTYPESET_BUNDLE_EXPORT extern "C" __attribute__((visibility("default")))

// --- Text input, under stand-in headers ---------------------------------------------
//
// The wheel's Dear ImGui is a fork whose text-input functions take their
// callback as a std::function, so the names upstream's imgui.h declares
// (callback as a function pointer) are not among the wheel's symbols. A library
// compiled against the fork's own headers calls the fork's functions and needs
// nothing here. One compiled against upstream's headers as a stand-in
// (IMTYPESET_BUNDLE_STAND_IN, from the build script) calls upstream's names:
// the demo window does, for its two text fields. They are defined here, in the
// library, and hand over to the wheel's. The wheel's two are declared by hand,
// from its exported symbols (nm -DC on its module, wheels 1.92.801 and
// 1.92.900); a wheel that changes them is a link error when the library is
// built, not a wrong call.
#ifdef IMTYPESET_BUNDLE_STAND_IN
#include <functional>
#include <type_traits>

// The forwarders below overload the wheel's two functions on the callback's
// type and tell "no callback" by comparing with null. Both need the headers
// compiled against to declare the callback as a plain function pointer: were
// it the fork's std::function already, each forwarder would redefine the
// function it means to call.
static_assert(std::is_pointer<ImGuiInputTextCallback>::value,
              "IMTYPESET_BUNDLE_STAND_IN is for headers whose ImGuiInputTextCallback is a "
              "function pointer; these are the fork's, which need no forwarders");

namespace ImGui
{
IMGUI_API bool InputTextMultiline(const char* label, char* buf, size_t buf_size, const ImVec2& size,
                                  ImGuiInputTextFlags flags,
                                  std::function<int(ImGuiInputTextCallbackData*)> callback,
                                  void* user_data);
IMGUI_API bool InputTextWithHint(const char* label, const char* hint, char* buf, size_t buf_size,
                                 ImGuiInputTextFlags flags,
                                 std::function<int(ImGuiInputTextCallbackData*)> callback,
                                 void* user_data);
} // namespace ImGui

bool ImGui::InputTextMultiline(const char* label, char* buf, size_t buf_size, const ImVec2& size,
                               ImGuiInputTextFlags flags, ImGuiInputTextCallback callback,
                               void* user_data)
{
    std::function<int(ImGuiInputTextCallbackData*)> wheels;
    if (callback != nullptr)
        wheels = callback;
    return ImGui::InputTextMultiline(label, buf, buf_size, size, flags, wheels, user_data);
}

bool ImGui::InputTextWithHint(const char* label, const char* hint, char* buf, size_t buf_size,
                              ImGuiInputTextFlags flags, ImGuiInputTextCallback callback,
                              void* user_data)
{
    std::function<int(ImGuiInputTextCallbackData*)> wheels;
    if (callback != nullptr)
        wheels = callback;
    return ImGui::InputTextWithHint(label, hint, buf, buf_size, flags, wheels, user_data);
}
#endif // IMTYPESET_BUNDLE_STAND_IN

// Which headers this library was compiled against, as the build script names
// them: "fork:<commit>" or "stand-in:<what>".
IMTYPESET_BUNDLE_EXPORT const char* ImTypesetBundle_Headers(void)
{
    return IMTYPESET_BUNDLE_HEADERS;
}

// The wheel this library was built for, one "key=value" line each: the
// imgui-bundle version, the file name of its extension module, and the sha256
// of that file. python/imtypeset shows it as host_info()["bundle"]["built_for"]
// and names it when it refuses another wheel.
IMTYPESET_BUNDLE_EXPORT const char* ImTypesetBundle_BuiltFor(void)
{
    return "wheel_version=" IMTYPESET_BUNDLE_WHEEL_VERSION "\n"
           "module=" IMTYPESET_BUNDLE_MODULE_NAME "\n"
           "module_sha256=" IMTYPESET_BUNDLE_MODULE_SHA256 "\n";
}

// ImGui::DebugCheckVersionAndDataLayout with the sizes given; a negative size
// means this library's own. 1 when ImGui accepts, 0 with ImGui's message.
IMTYPESET_BUNDLE_EXPORT int ImTypesetBundle_CheckSizes(long sz_io, long sz_style, char* out,
                                                       int out_size)
{
    const char* message = "ok";
    int result = 1;
    const size_t io = sz_io < 0 ? sizeof(ImGuiIO) : static_cast<size_t>(sz_io);
    const size_t style = sz_style < 0 ? sizeof(ImGuiStyle) : static_cast<size_t>(sz_style);
    try {
        if (!ImGui::DebugCheckVersionAndDataLayout(IMGUI_VERSION, io, style, sizeof(ImVec2),
                                                   sizeof(ImVec4), sizeof(ImDrawVert),
                                                   sizeof(ImDrawIdx))) {
            message = "ImGui::DebugCheckVersionAndDataLayout answered false";
            result = 0;
        }
    } catch (const std::exception& e) {
        if (out != nullptr && out_size > 0)
            std::snprintf(out, static_cast<size_t>(out_size), "%s", e.what());
        return 0;
    } catch (...) {
        message = "ImGui::DebugCheckVersionAndDataLayout threw";
        result = 0;
    }
    if (out != nullptr && out_size > 0)
        std::snprintf(out, static_cast<size_t>(out_size), "%s", message);
    return result;
}

IMTYPESET_BUNDLE_EXPORT int ImTypesetBundle_CheckImGuiLayout(char* out, int out_size)
{
    return ImTypesetBundle_CheckSizes(-1, -1, out, out_size);
}

// One line per fact, "Name offset size\n": every Dear ImGui field src/ reads or
// writes directly (the census is in docs/bindings-notes.md), as these headers
// lay it out. Returns the length needed, as snprintf does.
IMTYPESET_BUNDLE_EXPORT int ImTypesetBundle_LayoutFacts(char* out, int out_size)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#define FACT(Struct, Field)                                                                        \
    {                                                                                              \
#Struct "." #Field, offsetof(Struct, Field), sizeof(static_cast <Struct*>(nullptr)->Field) \
    }
#define SIZE(Struct)               \
    {                              \
#Struct, 0, sizeof(Struct) \
    }
    static const struct {
        const char* name;
        size_t offset;
        size_t size;
    } facts[] = {
        SIZE(ImGuiIO),
        SIZE(ImGuiStyle),
        SIZE(ImDrawVert),
        SIZE(ImDrawIdx),
        SIZE(ImWchar),
        SIZE(ImFontConfig),
        SIZE(ImFontGlyph),
        SIZE(ImGuiTextBuffer),
        SIZE(ImTextureID),
        FACT(ImGuiIO, BackendFlags),
        FACT(ImGuiIO, BackendRendererName),
        FACT(ImGuiIO, Fonts),
        FACT(ImGuiIO, FontDefault),
        FACT(ImGuiStyle, Alpha),
        FACT(ImFontAtlas, Locked),
        FACT(ImFontAtlas, Fonts),
        FACT(ImFont, FontId),
        FACT(ImFont, Sources),
        FACT(ImFontBaked, Size),
        FACT(ImFontBaked, Ascent),
        FACT(ImDrawList, VtxBuffer),
        FACT(ImDrawList, IdxBuffer),
        FACT(ImDrawList, _VtxCurrentIdx),
        FACT(ImDrawList, _VtxWritePtr),
        FACT(ImDrawList, _IdxWritePtr),
        FACT(ImDrawList, _CmdHeader),
        FACT(ImDrawCmdHeader, ClipRect),
        FACT(ImDrawVert, pos),
        FACT(ImDrawVert, uv),
        FACT(ImDrawVert, col),
        FACT(ImFontConfig, Name),
        FACT(ImFontConfig, FontData),
        FACT(ImFontConfig, FontDataSize),
        FACT(ImFontConfig, FontDataOwnedByAtlas),
        FACT(ImFontConfig, MergeMode),
        FACT(ImFontConfig, OversampleH),
        FACT(ImFontConfig, FontNo),
        FACT(ImFontConfig, SizePixels),
        FACT(ImFontConfig, RasterizerDensity),
    };
#undef FACT
#undef SIZE
#pragma GCC diagnostic pop
    int length = 0;
    for (const auto& fact : facts) {
        const size_t room =
            out != nullptr && length < out_size ? static_cast<size_t>(out_size - length) : 0;
        const int n = std::snprintf(room > 0 ? out + length : nullptr, room, "%s %zu %zu\n",
                                    fact.name, fact.offset, fact.size);
        if (n < 0)
            return -1;
        length += n;
    }
    return length;
}

// One line per constant, "Name value\n": every Dear ImGui enumerator src/ names
// outside the demo window, with the value these headers give it. Returns the
// length needed, as snprintf does.
IMTYPESET_BUNDLE_EXPORT int ImTypesetBundle_Constants(char* out, int out_size)
{
#define CONSTANT(Name) \
    {                  \
#Name, Name    \
    }
    static const struct {
        const char* name;
        int value;
    } constants[] = {
        CONSTANT(ImGuiCol_Text),
        CONSTANT(ImGuiCol_TextSelectedBg),
        CONSTANT(ImGuiCol_Separator),
        CONSTANT(ImGuiBackendFlags_RendererHasTextures),
        CONSTANT(ImGuiKey_A),
        CONSTANT(ImGuiKey_C),
        CONSTANT(ImGuiMod_Ctrl),
        CONSTANT(ImGuiMouseCursor_TextInput),
        CONSTANT(ImGuiMouseButton_Left),
    };
#undef CONSTANT
    int length = 0;
    for (const auto& constant : constants) {
        const size_t room =
            out != nullptr && length < out_size ? static_cast<size_t>(out_size - length) : 0;
        const int n = std::snprintf(room > 0 ? out + length : nullptr, room, "%s %d\n",
                                    constant.name, constant.value);
        if (n < 0)
            return -1;
        length += n;
    }
    return length;
}

// A glyph as THIS library's headers read it: X0 Y0 X1 Y1 U0 V0 U1 V1 into
// `box`, and the three bit-fields, which have no offset to report.
IMTYPESET_BUNDLE_EXPORT void ImTypesetBundle_ReadGlyph(const void* glyph, float* box,
                                                       unsigned* visible, unsigned* colored,
                                                       unsigned* codepoint)
{
    const ImFontGlyph* g = static_cast<const ImFontGlyph*>(glyph);
    const float values[8] = {g->X0, g->Y0, g->X1, g->Y1, g->U0, g->V0, g->U1, g->V1};
    std::memcpy(box, values, sizeof values);
    *visible = g->Visible;
    *colored = g->Colored;
    *codepoint = g->Codepoint;
}

// How many bytes the PROCESS's ImFontConfig constructor writes: its sizeof,
// since the constructor begins by zeroing the whole object. The library copies
// ImFontConfig by value, so its own sizeof must be the same number.
IMTYPESET_BUNDLE_EXPORT int ImTypesetBundle_ProbeFontConfigSize(void)
{
    enum { ROOM = 4096 };
    alignas(16) static unsigned char room[ROOM];
    int written = 0;
    for (int pass = 0; pass < 2; ++pass) {
        const unsigned char fill = pass == 0 ? 0xA5 : 0x5A;
        std::memset(room, fill, ROOM);
        new (room) ImFontConfig();
        for (int i = ROOM - 1; i >= 0; --i) {
            if (room[i] != fill) {
                written = written > i + 1 ? written : i + 1;
                break;
            }
        }
    }
    return written;
}

// An ImGuiTextBuffer made by this library's inline constructor, appended to by
// the process's ImGui, read back through this library's inline accessors. The
// copy path (ImTypeset_AppendParagraphSelectionText) works this way. 1 if the
// text survives the round trip.
IMTYPESET_BUNDLE_EXPORT int ImTypesetBundle_ProbeTextBuffer(void)
{
    static const char text[] = "imtypeset bundle probe";
    int same = 0;
    try {
        ImGuiTextBuffer buffer;
        buffer.append(text, text + 9);
        buffer.append(text + 9, nullptr);
        same = buffer.size() == static_cast<int>(sizeof text - 1) &&
               std::memcmp(buffer.begin(), text, sizeof text) == 0;
    } catch (...) {
        same = 0;
    }
    return same;
}
