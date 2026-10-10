"""The window around the self-hosted Python demo: python/host/imtypeset_host.h
from Python.

THIS IS NOT A DEAR IMGUI BINDING. It is an SDL2 window, Dear ImGui's stock
backends and the two dozen ImGui calls examples/python/minimal_host.py makes,
out of the same shared library `imtypeset` loaded, so that the two share one
Dear ImGui. A program that needs more of ImGui wants a real binding.

    import imtypeset, imtypeset_host as host
    with host.Window("title", 900, 600) as window:
        imtypeset.create_context()
        ...
        while window.begin_frame():
            host.begin("demo")
            imtypeset.text("...")
            host.end()
            window.end_frame()
        imtypeset.destroy_context()
"""

from __future__ import annotations

import ctypes as C
import struct
import zlib
from typing import Optional, Tuple

import imtypeset

SDL_RENDERER = 0  # ImGui's SDL_Renderer backend: no OpenGL, runs under SDL's dummy driver.
OPENGL3 = 1

# ImGuiWindowFlags the demo uses (imgui.h, v1.92.9).
WINDOW_NO_TITLE_BAR = 1 << 0
WINDOW_NO_RESIZE = 1 << 1
WINDOW_NO_MOVE = 1 << 2
WINDOW_NO_COLLAPSE = 1 << 5
WINDOW_NO_SAVED_SETTINGS = 1 << 8


class _Info(C.Structure):
    _fields_ = [
        ("StructSize", C.c_int),
        ("ImGuiVersionNum", C.c_int),
        ("SizeofImWchar", C.c_int),
        ("SizeofImDrawVert", C.c_int),
        ("SizeofImDrawIdx", C.c_int),
        ("SizeofImGuiIO", C.c_int),
        ("SizeofImGuiStyle", C.c_int),
        ("HasWindow", C.c_int),
        ("FramebufferWidth", C.c_int),
        ("FramebufferHeight", C.c_int),
        ("ImGuiVersion", C.c_char * 32),
        ("VideoDriver", C.c_char * 32),
        ("RendererBackend", C.c_char * 64),
        ("Renderer", C.c_char * 128),
        ("FontLoader", C.c_char * 32),
    ]


_lib = imtypeset.library()
if not hasattr(_lib, "ImTypesetHost_Create"):
    raise ImportError(
        f"imtypeset_host: {imtypeset.host_info()['library']!r} is an imtypeset library without "
        "the host surface (python/host); build the imtypeset_pyhost target of python/CMakeLists.txt"
    )

_FLOATS = C.POINTER(C.c_float)
_PROTOTYPES = {
    "GetInfo": (C.c_int, [C.POINTER(_Info), C.c_int]),
    "GetError": (C.c_char_p, []),
    "Create": (C.c_int, [C.c_char_p, C.c_int, C.c_int, C.c_int, C.c_float]),
    "Destroy": (None, []),
    "BeginFrame": (C.c_int, []),
    "EndFrame": (C.c_int, [C.c_void_p, C.c_int, C.POINTER(C.c_int), C.POINTER(C.c_int)]),
    "ScriptMouse": (None, [C.c_int, C.c_float, C.c_float, C.c_int]),
    "ScriptCopyShortcut": (None, [C.c_int]),
    "SetNextWindowPos": (None, [C.c_float, C.c_float]),
    "SetNextWindowSize": (None, [C.c_float, C.c_float]),
    "Begin": (C.c_int, [C.c_char_p, C.c_int]),
    "End": (None, []),
    "BeginChild": (C.c_int, [C.c_char_p, C.c_float]),
    "EndChild": (None, []),
    "SameLine": (None, [C.c_float]),
    "Separator": (None, []),
    "Dummy": (None, [C.c_float, C.c_float]),
    "TextWrapped": (None, [C.c_char_p, C.c_void_p]),
    "SliderFloat": (C.c_int, [C.c_char_p, _FLOATS, C.c_float, C.c_float]),
    "PushFont": (None, [C.c_void_p, C.c_float]),
    "PopFont": (None, []),
    "GetContentRegionAvail": (None, [_FLOATS]),
    "GetCursorScreenPos": (None, [_FLOATS]),
    "GetMousePos": (None, [_FLOATS]),
    "GetItemRect": (None, [_FLOATS]),
    "IsMouseDown": (C.c_int, [C.c_int]),
    "GetWindowDrawList": (C.c_void_p, []),
    "GetClipboardText": (C.c_char_p, []),
    "SetClipboardText": (None, [C.c_char_p]),
}
for _name, (_result, _arguments) in _PROTOTYPES.items():
    _function = getattr(_lib, "ImTypesetHost_" + _name)
    _function.restype = _result
    _function.argtypes = _arguments


class HostError(RuntimeError):
    pass


def info() -> dict:
    """What the library's Dear ImGui is and, with a window open, what it draws through."""
    raw = _Info()
    _lib.ImTypesetHost_GetInfo(C.byref(raw), C.sizeof(raw))
    return {
        "imgui_version": raw.ImGuiVersion.decode(),
        "imgui_version_num": raw.ImGuiVersionNum,
        "imwchar_bits": raw.SizeofImWchar * 8,
        "sizeof_imguiio": raw.SizeofImGuiIO,
        "sizeof_imguistyle": raw.SizeofImGuiStyle,
        "font_loader": raw.FontLoader.decode(),
        "has_window": bool(raw.HasWindow),
        "framebuffer": (raw.FramebufferWidth, raw.FramebufferHeight),
        "video_driver": raw.VideoDriver.decode(),
        "renderer_backend": raw.RendererBackend.decode(),
        "renderer": raw.Renderer.decode(),
    }


def write_png(path: str, width: int, height: int, rgb: bytes) -> None:
    """Top-down RGB24 pixels as a PNG, with the standard library only."""

    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    row = width * 3
    raw = b"".join(b"\0" + rgb[y * row : (y + 1) * row] for y in range(height))
    with open(path, "wb") as out:
        out.write(b"\x89PNG\r\n\x1a\n")
        out.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        out.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        out.write(chunk(b"IEND", b""))


class Window:
    """The one window, with its Dear ImGui context. `dpi_scale` 0 asks SDL."""

    def __init__(
        self,
        title: str = "imtypeset",
        width: int = 900,
        height: int = 600,
        backend: int = SDL_RENDERER,
        dpi_scale: float = 0.0,
    ):
        if not _lib.ImTypesetHost_Create(title.encode("utf-8"), width, height, backend, dpi_scale):
            raise HostError(_lib.ImTypesetHost_GetError().decode("utf-8", "replace"))
        self._open = True
        self.last_frame: Optional[Tuple[int, int, bytes]] = None

    def close(self) -> None:
        """Destroys imtypeset's context too, first: its memory is ImGui's."""
        if self._open:
            imtypeset.destroy_context()
            _lib.ImTypesetHost_Destroy()
            self._open = False

    def __enter__(self) -> "Window":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def begin_frame(self) -> bool:
        """Events, then a new ImGui frame. False once the window was closed."""
        return bool(_lib.ImTypesetHost_BeginFrame())

    def end_frame(self, screenshot: Optional[str] = None, capture: bool = False) -> None:
        """Renders and presents. `screenshot` names a PNG to write of this frame;
        `capture` keeps its pixels in `last_frame` as (width, height, rgb)."""
        if screenshot is None and not capture:
            if not _lib.ImTypesetHost_EndFrame(None, 0, None, None):
                raise HostError(_lib.ImTypesetHost_GetError().decode("utf-8", "replace"))
            return
        width, height = info()["framebuffer"]
        pixels = C.create_string_buffer(max(width * height * 3, 1))
        if not _lib.ImTypesetHost_EndFrame(pixels, len(pixels), None, None):
            raise HostError(_lib.ImTypesetHost_GetError().decode("utf-8", "replace"))
        self.last_frame = (width, height, pixels.raw[: width * height * 3])
        if screenshot is not None:
            write_png(screenshot, *self.last_frame)


def script_mouse(x: float, y: float, left_down: bool = False) -> None:
    """From now on every frame starts with the pointer here, whatever the real one does."""
    _lib.ImTypesetHost_ScriptMouse(1, x, y, 1 if left_down else 0)


def unscript_mouse() -> None:
    _lib.ImTypesetHost_ScriptMouse(0, 0.0, 0.0, 0)


def script_copy_shortcut(down: bool) -> None:
    """Ctrl+C, pressed or released."""
    _lib.ImTypesetHost_ScriptCopyShortcut(1 if down else 0)


def set_next_window_pos(x: float, y: float) -> None:
    _lib.ImTypesetHost_SetNextWindowPos(x, y)


def set_next_window_size(width: float, height: float) -> None:
    _lib.ImTypesetHost_SetNextWindowSize(width, height)


def begin(name: str, flags: int = 0) -> bool:
    return bool(_lib.ImTypesetHost_Begin(name.encode("utf-8"), flags))


def end() -> None:
    _lib.ImTypesetHost_End()


def begin_child(str_id: str, width: float) -> bool:
    """A child `width` wide, as tall as what is put in it."""
    return bool(_lib.ImTypesetHost_BeginChild(str_id.encode("utf-8"), width))


def end_child() -> None:
    _lib.ImTypesetHost_EndChild()


def same_line(spacing: float = -1.0) -> None:
    _lib.ImTypesetHost_SameLine(spacing)


def separator() -> None:
    _lib.ImTypesetHost_Separator()


def dummy(width: float, height: float) -> None:
    _lib.ImTypesetHost_Dummy(width, height)


def text_wrapped(text: str) -> None:
    """ImGui::TextWrapped, unformatted."""
    data = text.encode("utf-8")
    begin_ = C.cast(C.c_char_p(data), C.c_void_p).value or 0
    _lib.ImTypesetHost_TextWrapped(data, begin_ + len(data))


def slider_float(label: str, value: float, value_min: float, value_max: float) -> float:
    held = C.c_float(value)
    _lib.ImTypesetHost_SliderFloat(label.encode("utf-8"), C.byref(held), value_min, value_max)
    return held.value


def push_font(font: Optional[int], size: float = 0.0) -> None:
    """`font` is an ImFont's address (imtypeset.get_font) or None for the current one."""
    _lib.ImTypesetHost_PushFont(font, size)


def pop_font() -> None:
    _lib.ImTypesetHost_PopFont()


def _floats(function, count: int) -> Tuple[float, ...]:
    out = (C.c_float * count)()
    function(out)
    return tuple(out)


def get_content_region_avail() -> Tuple[float, ...]:
    return _floats(_lib.ImTypesetHost_GetContentRegionAvail, 2)


def get_cursor_screen_pos() -> Tuple[float, ...]:
    return _floats(_lib.ImTypesetHost_GetCursorScreenPos, 2)


def get_mouse_pos() -> Tuple[float, ...]:
    return _floats(_lib.ImTypesetHost_GetMousePos, 2)


def get_item_rect() -> Tuple[float, ...]:
    """The last item's (min x, min y, max x, max y)."""
    return _floats(_lib.ImTypesetHost_GetItemRect, 4)


def is_mouse_down(button: int = 0) -> bool:
    return bool(_lib.ImTypesetHost_IsMouseDown(button))


def get_window_draw_list() -> int:
    """The current window's ImDrawList, as the address imtypeset.Paragraph.draw takes."""
    return _lib.ImTypesetHost_GetWindowDrawList()


def get_clipboard_text() -> str:
    return _lib.ImTypesetHost_GetClipboardText().decode("utf-8", "replace")


def set_clipboard_text(text: str) -> None:
    _lib.ImTypesetHost_SetClipboardText(text.encode("utf-8"))
