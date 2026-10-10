/* imtypeset_host.h: the window a Python program needs around imtypeset.
 *
 * THIS IS NOT A DEAR IMGUI BINDING. It is the host side of one small demo:
 * an SDL2 window, Dear ImGui's stock SDL2 backends, and the handful of ImGui
 * calls examples/python/minimal_host.py makes, each flattened to something
 * ctypes can call. It is built into the same shared library as imtypeset and
 * the ImGui they both use, so there is one ImGui in the process and the
 * `struct ImDrawList*` and `struct ImFont*` this header hands out are the ones
 * include/imtypeset_c.h takes. A program that needs more of ImGui than this
 * wants a real binding (Dear ImGui Bundle, cimgui) and imtypeset compiled
 * against that binding's ImGui; see docs/bindings-notes.md.
 *
 * One window, one ImGui context, one thread. Text is UTF-8: zero-terminated
 * where there is one pointer, (begin, end) where there are two.
 */
#ifndef IMTYPESET_HOST_H
#define IMTYPESET_HOST_H

#ifndef IMTYPESET_HOST_API
#if defined(_WIN32)
#define IMTYPESET_HOST_API __declspec(dllexport)
#elif defined(__GNUC__)
#define IMTYPESET_HOST_API __attribute__((visibility("default")))
#else
#define IMTYPESET_HOST_API
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct ImDrawList;
struct ImFont;

/* What this library's ImGui is, for the binding to hold against
 * ImTypeset_BuildInfo before it calls anything else. Only `int` and `char`. */
typedef struct ImTypesetHost_Info {
    int StructSize; /* sizeof(ImTypesetHost_Info) in the library. */
    int ImGuiVersionNum;
    int SizeofImWchar;
    int SizeofImDrawVert;
    int SizeofImDrawIdx;
    int SizeofImGuiIO;
    int SizeofImGuiStyle;
    int HasWindow;        /* Non-zero between Create and Destroy. */
    int FramebufferWidth; /* Pixels; what EndFrame reads back. Zero with no window. */
    int FramebufferHeight;
    char ImGuiVersion[32];
    char VideoDriver[32];     /* SDL's: x11, wayland, offscreen, dummy. "" with no window. */
    char RendererBackend[64]; /* ImGui's io.BackendRendererName. */
    char Renderer[128];       /* SDL_Renderer's name, or GL_RENDERER. */
    char FontLoader[32];      /* FreeType or stb_truetype. */
} ImTypesetHost_Info;

enum ImTypesetHost_Backend_ {
    ImTypesetHost_Backend_SDLRenderer = 0, /* Needs no OpenGL; runs under SDL's dummy driver. */
    ImTypesetHost_Backend_OpenGL3 = 1
};

/* Copies the first `out_size` bytes and returns the size of the whole. Needs
 * no window. */
IMTYPESET_HOST_API int ImTypesetHost_GetInfo(ImTypesetHost_Info* out, int out_size);
/* Why the last call that answered 0 did. Never null. */
IMTYPESET_HOST_API const char* ImTypesetHost_GetError(void);

/* SDL, the window, the ImGui context and its backends. `dpi_scale` zero asks
 * SDL. Non-zero for success. ImGui writes no .ini file. */
IMTYPESET_HOST_API int ImTypesetHost_Create(const char* title, int width, int height, int backend,
                                            float dpi_scale);
/* Destroy the ImTypeset context first: its memory is ImGui's allocator's. */
IMTYPESET_HOST_API void ImTypesetHost_Destroy(void);

/* Reads the window's events and starts an ImGui frame. Zero, with no frame
 * started, once the window was asked to close. */
IMTYPESET_HOST_API int ImTypesetHost_BeginFrame(void);
/* Renders and presents. If `rgb` is given and holds FramebufferWidth x
 * FramebufferHeight x 3 bytes, the frame is read back into it before the
 * present: rows top to bottom, red first. `out_width` and `out_height`, if
 * given, receive the framebuffer's size either way. Zero if the read-back
 * failed or `rgb_capacity` was too small. */
IMTYPESET_HOST_API int ImTypesetHost_EndFrame(unsigned char* rgb, int rgb_capacity, int* out_width,
                                              int* out_height);

/* A mouse for a run nobody is at: from this call on, every frame starts with
 * the pointer at (x, y) and the left button as given, whatever the real one
 * does. `enabled` zero gives the real mouse back. */
IMTYPESET_HOST_API void ImTypesetHost_ScriptMouse(int enabled, float x, float y, int left_down);
/* Ctrl+C (Cmd+C on macOS), pressed or released. */
IMTYPESET_HOST_API void ImTypesetHost_ScriptCopyShortcut(int down);

/* --- The ImGui calls the demo makes. Each is ImGui's of the same name. -------- */

IMTYPESET_HOST_API void ImTypesetHost_SetNextWindowPos(float x, float y);
IMTYPESET_HOST_API void ImTypesetHost_SetNextWindowSize(float width, float height);
/* `flags` is ImGuiWindowFlags; no close button. */
IMTYPESET_HOST_API int ImTypesetHost_Begin(const char* name, int flags);
IMTYPESET_HOST_API void ImTypesetHost_End(void);
/* A child `width` wide that grows to the height of what is in it. */
IMTYPESET_HOST_API int ImTypesetHost_BeginChild(const char* str_id, float width);
IMTYPESET_HOST_API void ImTypesetHost_EndChild(void);
IMTYPESET_HOST_API void ImTypesetHost_SameLine(float spacing);
IMTYPESET_HOST_API void ImTypesetHost_Separator(void);
IMTYPESET_HOST_API void ImTypesetHost_Dummy(float width, float height);
/* ImGui::TextWrapped("%s", text) without the copy: unformatted, wrapped at the
 * window's edge. */
IMTYPESET_HOST_API void ImTypesetHost_TextWrapped(const char* text, const char* text_end);
/* Non-zero when the value changed. */
IMTYPESET_HOST_API int ImTypesetHost_SliderFloat(const char* label, float* value, float value_min,
                                                 float value_max);
/* `font` null keeps the current one; `size` zero keeps the current size. */
IMTYPESET_HOST_API void ImTypesetHost_PushFont(struct ImFont* font, float size);
IMTYPESET_HOST_API void ImTypesetHost_PopFont(void);
IMTYPESET_HOST_API void ImTypesetHost_GetContentRegionAvail(float* out_xy);
IMTYPESET_HOST_API void ImTypesetHost_GetCursorScreenPos(float* out_xy);
IMTYPESET_HOST_API void ImTypesetHost_GetMousePos(float* out_xy);
/* The last item's rectangle: (min x, min y, max x, max y). */
IMTYPESET_HOST_API void ImTypesetHost_GetItemRect(float* out_rect);
IMTYPESET_HOST_API int ImTypesetHost_IsMouseDown(int button);
IMTYPESET_HOST_API struct ImDrawList* ImTypesetHost_GetWindowDrawList(void);
/* ImGui's storage: valid until the next clipboard call. Never null. */
IMTYPESET_HOST_API const char* ImTypesetHost_GetClipboardText(void);
IMTYPESET_HOST_API void ImTypesetHost_SetClipboardText(const char* text);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* IMTYPESET_HOST_H */
