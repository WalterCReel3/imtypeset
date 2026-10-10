"""What the bundle-hosted examples share: a hello_imgui runner that can run N
frames unattended and photograph the last one, the fonts, and the two bridges
between imtypeset's handles (plain addresses) and the bundle's objects.

Not part of the imtypeset package: an application on Dear ImGui Bundle already
has its runner and needs only `font_object` and `draw_list_address`.
"""

import ctypes
import os
import struct
import zlib

import imgui_bundle
from imgui_bundle import hello_imgui, imgui

import imtypeset as ts

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FONT_DIR = os.path.join(REPO, "tests", "data", "fonts")
FONTS = {
    ts.Face.Regular: "Roboto-Regular.ttf",
    ts.Face.Bold: "Roboto-Bold.ttf",
    ts.Face.Italic: "Roboto-RegularItalic.ttf",
    ts.Face.Code: "Inconsolata-Medium.ttf",
    ts.Face.Math: "latinmodern-math.otf",
}

# The bundle's own module, for the few Dear ImGui functions called by address.
_wheel = ctypes.CDLL(imgui_bundle._imgui_bundle.__file__)
_wheel._ZN5ImGui17GetWindowDrawListEv.restype = ctypes.c_void_p
_wheel._ZN5ImGui17GetWindowDrawListEv.argtypes = []


def draw_list_address() -> int:
    """The current window's ImDrawList*, as imtypeset.draw_paragraph() takes it."""
    return _wheel._ZN5ImGui17GetWindowDrawListEv()


_font_objects: dict = {}


def add_fonts() -> None:
    """imtypeset's five faces into the bundle's atlas. Call outside a frame, with an
    imtypeset context current; remembers the bundle's ImFont object for each."""
    atlas = imgui.get_io().fonts
    for face, name in FONTS.items():
        ts.add_font(face, os.path.join(FONT_DIR, name))
        _font_objects[face] = atlas.fonts[atlas.fonts.size() - 1]


def font_object(face):
    """The bundle's ImFont for a face added by add_fonts(), for imgui.push_font()."""
    return _font_objects[face]


def write_png(path: str, pixels) -> None:
    """A (height, width, 3 or 4) uint8 array as an RGB PNG, standard library only."""
    height, width = pixels.shape[0], pixels.shape[1]
    rgb = pixels[:, :, :3].tobytes()

    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    row = width * 3
    raw = b"".join(b"\0" + rgb[y * row : (y + 1) * row] for y in range(height))
    with open(path, "wb") as out:
        out.write(b"\x89PNG\r\n\x1a\n")
        out.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        out.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        out.write(chunk(b"IEND", b""))


def run(gui, title: str, size=(960, 560), frames: int = 0, screenshot: str = "", setup=None,
        before_exit=None, script=None) -> int:
    """Runs `gui` in a hello_imgui application shaped like the ones this is for:
    RunnerParams, a load_additional_fonts callback that makes the imtypeset
    context and adds the fonts, immapp.run. With `frames`, exits after that many and writes
    `screenshot`. `script(frame)` may post input events before each frame's GUI.
    Returns the number of frames shown."""
    from imgui_bundle import immapp

    count = [0]
    runner = hello_imgui.RunnerParams()
    runner.app_window_params.window_title = title
    runner.app_window_params.window_geometry.size = size
    runner.ini_disable = True  # an example leaves no settings file behind
    runner.fps_idling.enable_idling = not frames

    def load_fonts() -> None:
        # hello_imgui empties the atlas and calls this after post_init, and
        # again when the DPI changes; a font added anywhere earlier is gone by
        # the first frame. The application's own fonts first, then imtypeset's.
        hello_imgui.imgui_default_settings.load_default_font_with_font_awesome_icons()
        if ts.current_context() is None:
            ts.create_context()
        add_fonts()
        if setup is not None:
            setup()

    def show_gui() -> None:
        count[0] += 1
        gui()
        if frames and count[0] >= frames:
            runner.app_shall_exit = True

    def pre_new_frame() -> None:
        if script is not None:
            script(count[0] + 1)

    def exiting() -> None:
        if before_exit is not None:
            before_exit()
        # While the Dear ImGui context is still there: imtypeset's memory is
        # that context's allocator's.
        ts.destroy_context()

    runner.callbacks.load_additional_fonts = load_fonts
    runner.callbacks.show_gui = show_gui
    runner.callbacks.pre_new_frame = pre_new_frame
    runner.callbacks.before_exit = exiting
    immapp.run(runner)
    if screenshot:
        write_png(screenshot, hello_imgui.final_app_window_screenshot())
    return count[0]
