"""imtypeset's demo inside a Dear ImGui Bundle application, on the installed wheel.

    python/host/build_bundle_library.sh
    IMTYPESET_LIBRARY=build/py-host-bundle/libimtypeset_bundle.so \
        python3 -P -B examples/python/bundle_demo.py [--show window|pages] [--page NAME]

Two ways an application shows the library's demo, and this is both:

  --show window   `imtypeset.demo.show_demo_window()`: the whole demo window,
                  floating over the application, as imgui.show_demo_window() is.
  --show pages    (the default) an application with pages of its own: a
                  sidebar that lists `demo.sections()` and a child window in
                  which the chosen one is drawn by `demo.show_demo_section()`.
                  This is how an application that already has a demo of its
                  own gives the specimen, the formula gallery and the rest a
                  page each without owning any of their code.

Unattended: --frames N runs N frames and exits; --grab FILE photographs the X
display with ImageMagick's `import` two frames before that (it needs neither
numpy nor a read-back from the renderer, so it works in an environment that has
only the wheel); --open "A,B" opens those sections of the demo window; --scroll Y
scrolls the window or the page; --type TEXT clicks at --click X,Y, selects
what the field there holds and types TEXT over it (the Math page's source is at
the default place when the page is shown with --show pages).

Exits 77 when IMTYPESET_LIBRARY is not a bundle-hosted library.
"""

import argparse
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path[:0] = [os.path.join(REPO, "python"), os.path.dirname(os.path.abspath(__file__))]

DEMO_WINDOW = "ImTypeset Demo"  # The title ImTypeset::ShowDemoWindow() gives its window.


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--show", choices=("window", "pages"), default="pages")
    parser.add_argument("--page", default="Specimen", help="the section the sidebar opens on")
    parser.add_argument("--open", default="", help="sections of the demo window to open")
    parser.add_argument("--scroll", type=float, default=0.0)
    parser.add_argument("--type", default="", help="text to type into the field at --click")
    parser.add_argument("--click", default="320,40", help="where --type clicks, X,Y")
    parser.add_argument("--font-size", type=float, default=19.0, help="type size of a page")
    parser.add_argument("--size", default="1000x760", help="the application window, WxH")
    parser.add_argument("--frames", type=int, default=0, help="run N frames and exit")
    parser.add_argument("--grab", default="", help="photograph the X display into this PNG")
    args = parser.parse_args()
    if not os.path.basename(os.environ.get("IMTYPESET_LIBRARY", "")).startswith("libimtypeset_bundle"):
        sys.stderr.write(
            "bundle_demo.py: SKIPPED: IMTYPESET_LIBRARY does not name a bundle-hosted library; "
            "python/host/build_bundle_library.sh builds one and prints the line to use.\n"
        )
        sys.exit(77)
    width, height = (int(n) for n in args.size.split("x"))

    import imgui_bundle
    from imgui_bundle import imgui

    import imtypeset as ts
    from imtypeset import demo
    import bundle_support as support

    info = ts.host_info()
    print("wheel:", imgui_bundle.__version__, "| Dear ImGui", imgui.get_version(),
          "| headers:", info["bundle"]["headers"], "| accepted by:", info["bundle"]["accepted_by"])
    sections = demo.sections()
    names = [demo.section_name(section) for section in sections]
    print("sections:", ", ".join(names))
    if args.page not in names:
        sys.exit(f"bundle_demo.py: no section is called {args.page!r}")
    state = {"page": sections[names.index(args.page)], "open": True, "frame": 0, "grabbed": False}
    wanted = [name.strip() for name in args.open.split(",") if name.strip()]

    def bold_italic() -> None:
        # bundle_support adds five faces; the specimen has a row for each of six.
        ts.add_font(ts.Face.BoldItalic, os.path.join(support.FONT_DIR, "Roboto-BoldItalic.ttf"))

    def arrange_demo_window() -> None:
        """Opens the sections --open names, from outside: a collapsing header
        keeps whether it is open in its window's storage under its label's ID."""
        imgui.begin(DEMO_WINDOW)
        if state["frame"] == 1:
            for name in names:
                imgui.get_state_storage().set_int(imgui.get_id(name), 1 if name in wanted else 0)
            imgui.set_window_pos((10.0, 40.0))
            imgui.set_window_size((width - 20.0, height - 50.0))
        elif args.scroll > 0.0:
            imgui.set_scroll_y(args.scroll)
        imgui.end()

    def gui_window() -> None:
        imgui.text_wrapped(
            "An application's own window. The demo window floats over it: "
            "imtypeset.demo.show_demo_window(open)."
        )
        if not state["open"] and imgui.button("Show it again"):
            state["open"] = True
        if state["open"]:
            state["open"] = demo.show_demo_window(True)
            if state["open"] and args.frames:
                arrange_demo_window()

    def gui_pages() -> None:
        imgui.begin_child("pages", (170.0, 0.0), imgui.ChildFlags_.borders)
        imgui.text_disabled("imtypeset.demo")
        for section, name in zip(sections, names):
            if imgui.selectable(name, section == state["page"])[0]:
                state["page"] = section
        imgui.end_child()
        imgui.same_line()
        imgui.begin_child("page", (0.0, 0.0), imgui.ChildFlags_.borders)
        # The section sets its text at the size ImGui's own text has here.
        imgui.push_font(None, args.font_size)
        demo.show_demo_section(state["page"])
        imgui.pop_font()
        if args.scroll > 0.0 and state["frame"] > 3:
            imgui.set_scroll_y(args.scroll)
        imgui.end_child()

    def gui() -> None:
        state["frame"] += 1
        if args.show == "window":
            gui_window()
        else:
            gui_pages()

    click = tuple(float(n) for n in args.click.split(","))

    def type_text(frame: int) -> None:
        io = imgui.get_io()
        if frame == 6:
            io.add_mouse_pos_event(*click)
            io.add_mouse_button_event(0, True)
        elif frame == 7:
            io.add_mouse_button_event(0, False)
        elif frame == 9:
            state["wanted_text"] = io.want_text_input
            io.add_key_event(imgui.Key.mod_ctrl, True)
            io.add_key_event(imgui.Key.a, True)
        elif frame == 10:
            io.add_key_event(imgui.Key.a, False)
            io.add_key_event(imgui.Key.mod_ctrl, False)
        elif frame == 11:
            io.add_input_characters_utf8(args.type)
        elif frame == 13:
            io.add_mouse_pos_event(-1.0e9, -1.0e9)

    def before_frame(frame: int) -> None:
        if args.type and args.frames:
            type_text(frame)
        # What is on the display now is the frame before this one.
        if args.grab and args.frames and frame == args.frames - 1 and not state["grabbed"]:
            state["grabbed"] = True
            subprocess.run(["import", "-window", "root", args.grab], check=True)

    def report() -> None:
        stats = ts.stats()
        print("frames:", state["frame"], "| layouts:", stats.Layouts, "| paragraphs drawn:",
              stats.DrawCalls, "| vertices:", stats.VerticesEmitted,
              "| demo window open:", state["open"])
        if args.type:
            print("the click reached a text field:", state.get("wanted_text"))
        error = ts.last_error()
        print("last error:", error.code.name if hasattr(error.code, "name") else error.code)

    support.run(gui, "imtypeset demo in Dear ImGui Bundle", size=(width, height),
                frames=args.frames, setup=bold_italic, script=before_frame, before_exit=report)
    if args.grab:
        print("grabbed:", args.grab, os.path.getsize(args.grab) if os.path.exists(args.grab) else "MISSING")


if __name__ == "__main__":
    main()
