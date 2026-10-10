"""imtypeset inside a Dear ImGui Bundle application, on the installed wheel.

    python/host/build_bundle_library.sh
    IMTYPESET_LIBRARY=build/py-host-bundle/libimtypeset_bundle.so \
        python3 -P -B examples/python/bundle_host.py [--frames 30 --screenshot out.png]

The page of examples/python/minimal_host.py, with the bundle's own `imgui`
binding for everything that is not imtypeset: Dear ImGui's wrapped text beside
a justified paragraph with formulas, a width slider, selection and copy. The
library IMTYPESET_LIBRARY names holds no Dear ImGui; it draws through the
wheel's, into the application's own windows. `imtypeset.host_info()` says how
the pairing was checked.

Exits 77 when IMTYPESET_LIBRARY is not a bundle-hosted library.
"""

import argparse
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path[:0] = [os.path.join(REPO, "python"), os.path.dirname(os.path.abspath(__file__))]

PROSE = (
    "A formula written between dollar signs is set by the math engine and sits in its line as "
    "one box: the roots of $ax^2 + bx + c = 0$ are $x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}$, "
    "and $e^{i\\pi} + 1 = 0$ needs no introduction. Drag over this paragraph to select, and "
    "press Ctrl+C: a formula is copied as its source, dollars included."
)
DISPLAY = "$$ \\sum_{k=0}^{n} k = \\frac{n(n+1)}{2} $$"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--frames", type=int, default=0, help="run N scripted frames and exit")
    parser.add_argument("--screenshot", default="", help="write the last frame as a PNG")
    args = parser.parse_args()
    if not os.path.basename(os.environ.get("IMTYPESET_LIBRARY", "")).startswith("libimtypeset_bundle"):
        sys.stderr.write(
            "bundle_host.py: SKIPPED: IMTYPESET_LIBRARY does not name a bundle-hosted library; "
            "python/host/build_bundle_library.sh builds one and prints the line to use.\n"
        )
        sys.exit(77)

    from imgui_bundle import imgui

    import imtypeset as ts
    import bundle_support as support

    info = ts.host_info()
    if info["host"] != "imgui_bundle":
        sys.exit(f"bundle_host.py: {info['library']} is not a bundle-hosted library")
    print("host:", info["host"], "| headers:", info["bundle"]["headers"],
          "| accepted by:", info["bundle"]["accepted_by"],
          "| probes:", info["bundle"]["probes_passed"])
    state = {"selection": (0, 0), "width": 440.0, "rect": None}

    def script(frame: int) -> None:
        """A drag across the paragraph and a Ctrl+C, for a run nobody is at."""
        if not args.frames or state["rect"] is None:
            return
        io = imgui.get_io()
        (x0, y0), (x1, _) = state["rect"]
        start, stop = (x0 + 60.0, y0 + 34.0), (x0 + 0.72 * (x1 - x0), y0 + 80.0)
        io.add_mouse_pos_event(*(start if frame < 8 else stop))
        io.add_mouse_button_event(0, 5 <= frame < 12)
        if frame in (14, 15):
            io.add_key_event(imgui.Key.mod_ctrl, frame == 14)
            io.add_key_event(imgui.Key.c, frame == 14)

    def gui() -> None:
        imgui.push_font(support.font_object(ts.Face.Regular), 17.0)
        _, state["width"] = imgui.slider_float("column width", state["width"], 220.0, 450.0)
        imgui.begin_child("imgui", (state["width"], 0.0), imgui.ChildFlags_.auto_resize_y)
        imgui.text_wrapped("imgui.text_wrapped")
        imgui.separator()
        imgui.text_wrapped(PROSE)
        imgui.end_child()
        imgui.same_line(0.0, 24.0)
        imgui.begin_child("imtypeset", (state["width"], 0.0), imgui.ChildFlags_.auto_resize_y)
        imgui.text_wrapped("imtypeset.selectable_text, justified")
        imgui.separator()
        _, state["selection"] = ts.selectable_text(
            "prose", PROSE, flags=ts.TextFlags.Math, selection=state["selection"]
        )
        state["rect"] = (imgui.get_item_rect_min(), imgui.get_item_rect_max())
        ts.text(DISPLAY, flags=ts.TextFlags.Math)
        imgui.end_child()
        imgui.separator()
        imgui.text_wrapped(
            f"selection: characters {state['selection'][0]}..{state['selection'][1]} of the str"
        )
        imgui.text_wrapped("clipboard: " + imgui.get_clipboard_text())
        imgui.pop_font()

    shown = support.run(
        gui, "imtypeset in Dear ImGui Bundle", frames=args.frames, screenshot=args.screenshot,
        script=script,
        before_exit=lambda: print("selection:", state["selection"], "layouts:", ts.stats().Layouts),
    )
    print("frames:", shown)


if __name__ == "__main__":
    main()
