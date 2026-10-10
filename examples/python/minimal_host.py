"""A minimal Python host for imtypeset: Dear ImGui's wrapped text beside a
justified paragraph with formulas, a width slider, selection with copy, and a
small document (styled blocks handed over once, drawn by one call a frame).

    cmake -S python -B build/py-host -G Ninja -DIMTYPESET_SDL2_DIR=<an SDL2> && cmake --build build/py-host
    IMTYPESET_LIBRARY=build/py-host/libimtypeset_pyhost.so python3 -P -B examples/python/minimal_host.py
    ... minimal_host.py --frames 30 --screenshot out.png     # unattended: scripted drag and Ctrl+C

Python owns the loop. `imtypeset` is the ctypes binding (python/imtypeset);
`imtypeset_host` is the window and the few ImGui calls used here (python/host),
which is NOT a general Dear ImGui binding.
"""

import argparse
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path[:0] = [os.path.join(REPO, "python"), os.path.join(REPO, "python", "host")]

import imtypeset as ts  # noqa: E402
import imtypeset_host as host  # noqa: E402

FONTS = {
    ts.Face.Regular: "Roboto-Regular.ttf",
    ts.Face.Bold: "Roboto-Bold.ttf",
    ts.Face.Italic: "Roboto-RegularItalic.ttf",
    ts.Face.Code: "Inconsolata-Medium.ttf",
    ts.Face.Math: "latinmodern-math.otf",
}
PROSE = (
    "A formula written between dollar signs is set by the math engine and sits in its line as "
    "one box: the roots of $ax^2 + bx + c = 0$ are $x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}$, "
    "and $e^{i\\pi} + 1 = 0$ needs no introduction. Drag over this paragraph to select, and "
    "press Ctrl+C: a formula is copied as its source, dollars included."
)
DISPLAY = "$$ \\sum_{k=0}^{n} k = \\frac{n(n+1)}{2} $$"
NOTE = "Blocks are described once; a frame is one call. See the header for the rest."


def document():
    """Styled blocks in: a heading, a paragraph with a tagged run, a list item, code."""
    left = ts.BlockStyle(Align=ts.Align.Left, SpaceBelow=6.0)
    title = ts.BlockStyle(Face=ts.Face.Bold, Scale=1.2, Align=ts.Align.Left, SpaceBelow=4.0)
    tagged = ts.Run(NOTE.index("the header"), NOTE.index("the header") + 10, color=ts.col32(90, 170, 255), tag=1)
    return ts.Document.from_blocks(
        [
            ts.Block(ts.BlockKind.Heading, "imtypeset.Document", style=1, level=2),
            ts.Block(text=NOTE, runs=[tagged]),
            ts.Block(ts.BlockKind.ListItem, "A list item, with its marker.", marker="1.", left_indent=22.0),
            ts.Block(ts.BlockKind.Code, "doc.layout(width)\ndoc.draw(draw_list, pos)"),
        ],
        styles=[left, title],
    )


def script(frame, rect):
    """A drag across the paragraph and a Ctrl+C, for a run nobody is at."""
    x0, y0, x1, _ = rect
    start, stop = (x0 + 60.0, y0 + 34.0), (x0 + 0.72 * (x1 - x0), y0 + 80.0)
    host.script_mouse(*(start if frame < 8 else stop), left_down=5 <= frame < 12)
    if frame in (14, 15):
        host.script_copy_shortcut(frame == 14)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--frames", type=int, default=0, help="run N scripted frames and exit")
    parser.add_argument("--screenshot", help="write frame N as a PNG")
    parser.add_argument("--opengl", action="store_true", help="ImGui's OpenGL3 backend")
    args = parser.parse_args()
    if args.screenshot and args.frames < 20:
        parser.error("--screenshot needs --frames N, N >= 20: the script takes 16 frames")

    backend = host.OPENGL3 if args.opengl else host.SDL_RENDERER
    with host.Window("imtypeset: minimal Python host", 960, 560, backend, dpi_scale=1.0) as window:
        ts.create_context()
        for face, name in FONTS.items():
            ts.add_font(face, os.path.join(REPO, "tests", "data", "fonts", name))
        print("host:", ts.host_info()["host"], host.info()["video_driver"], host.info()["renderer"])
        width, selection, rect, frame = 440.0, (0, 0), None, 0
        doc = document()
        while window.begin_frame():
            frame += 1
            if args.frames and rect:
                script(frame, rect)
            host.set_next_window_pos(0.0, 0.0)
            host.set_next_window_size(960.0, 560.0)
            host.begin("demo", host.WINDOW_NO_TITLE_BAR | host.WINDOW_NO_RESIZE | host.WINDOW_NO_MOVE)
            host.push_font(ts.get_font(ts.Face.Regular), 17.0)
            width = host.slider_float("column width", width, 220.0, 450.0)
            host.begin_child("imgui", width)
            host.text_wrapped("ImGui::TextWrapped")
            host.separator()
            host.text_wrapped(PROSE)
            host.end_child()
            host.same_line(24.0)
            host.begin_child("imtypeset", width)
            host.text_wrapped("imtypeset.selectable_text, justified")
            host.separator()
            _, selection = ts.selectable_text("prose", PROSE, flags=ts.TextFlags.Math, selection=selection)
            rect = host.get_item_rect()
            ts.text(DISPLAY, flags=ts.TextFlags.Math)
            host.separator()
            doc.view("document", (0.0, 150.0))  # a child of its own: scrolls, selects, copies
            host.end_child()
            host.separator()
            host.text_wrapped(f"selection: characters {selection[0]}..{selection[1]} of the str")
            host.text_wrapped("clipboard: " + host.get_clipboard_text())
            host.pop_font()
            host.end()
            last = bool(args.frames) and frame >= args.frames
            window.end_frame(screenshot=args.screenshot if last else None)
            if last:
                break
        print("frames:", frame, "selection:", selection, "layouts:", ts.stats().Layouts)
        print("document:", len(doc), "blocks,", doc.stats().BlocksLaidOut, "laid out,", doc.size())


if __name__ == "__main__":
    main()
