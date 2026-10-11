"""What happens to imtypeset's fonts when a hello_imgui application loads its
fonts a second time, run inside the installed wheel.

    IMTYPESET_LIBRARY=<out>/libimtypeset_bundle.so PYTHONPATH=python \
    sh tools/shot.sh -- <python> -P -B tools/bundle_route/font_lifetime.py SCENARIO

One scenario a process (some of them are expected to be able to crash), one
JSON line per event on stdout, flushed, so that a crash shows how far it got.
The last line is {"event": "done", ...} when the process survived. Exit 0, or 1
when a font of the application's left the atlas (same-count-add and
same-count-cycle look).

    once        nothing is provoked: counts how often hello_imgui calls
                load_additional_fonts by itself, and what is in the atlas
                when it does.
    reassign    at frame 20 the application assigns the callback again, which
                is the only way hello_imgui calls it twice. The atlas is NOT
                emptied first (hello_imgui does not), so imtypeset's
                add_font replaces each role's font.
    clear       the same, and the callback empties the atlas itself first:
                every ImFont the context holds has been freed when
                imtypeset.add_font runs.
    clear-lost  the atlas is emptied and only the application's font comes
                back: imtypeset is left with no font and must say so.
    same-count  the atlas is emptied and the application adds as many fonts
                as the atlas held, in the same frame, and imtypeset's are not
                added again: the case validate_fonts' comment names as the
                one it does not see.
    same-count-add
                the same, then imtypeset.add_font for one role. Reports
                whether a font of the APPLICATION's left the atlas.
    same-count-cycle
                that, twelve times over inside the one callback, each time for
                another role first: every time, the atlas must end with the
                application's fonts and the one just added.

Every scenario draws a Document and a widget each frame, before and after.
"""

import ctypes
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FONT_DIR = os.path.join(REPO, "tests", "data", "fonts")
FRAMES = 60
AT = 20


def say(**event) -> None:
    sys.stdout.write(json.dumps(event) + "\n")
    sys.stdout.flush()


def main() -> int:
    scenario = sys.argv[1] if len(sys.argv) > 1 else "once"
    import imgui_bundle
    from imgui_bundle import hello_imgui, imgui, immapp

    import imtypeset as ts

    wheel = ctypes.CDLL(imgui_bundle._imgui_bundle.__file__)
    wheel._ZN5ImGui17GetWindowDrawListEv.restype = ctypes.c_void_p
    wheel._ZN5ImGui17GetWindowDrawListEv.argtypes = []

    faces = {
        ts.Face.Regular: "Roboto-Regular.ttf",
        ts.Face.Bold: "Roboto-Bold.ttf",
        ts.Face.Code: "Inconsolata-Medium.ttf",
        ts.Face.Math: "latinmodern-math.otf",
    }
    state = {"frame": 0, "loads": 0, "doc": None, "draws": [0, 0], "errors": {}, "removed": 0}
    runner = hello_imgui.RunnerParams()
    runner.app_window_params.window_title = "font lifetime"
    runner.app_window_params.window_geometry.size = (640, 400)
    runner.ini_disable = True
    runner.fps_idling.enable_idling = False

    def atlas_size() -> int:
        return imgui.get_io().fonts.fonts.size()

    def held() -> dict:
        return {face.name: ts.get_font(face) for face in faces}

    def load_fonts() -> None:
        state["loads"] += 1
        second = state["loads"] > 1
        before = atlas_size()
        say(event="load_additional_fonts", call=state["loads"], frame=state["frame"],
            atlas_fonts_on_entry=before)
        atlas = imgui.get_io().fonts
        if second and scenario != "reassign":
            atlas.clear()
            say(event="atlas cleared", atlas_fonts=atlas_size())
        if not second or scenario != "reassign":
            atlas.add_font_default()
        if second and scenario == "same-count-cycle":
            order = list(faces)
            for cycle in range(12):
                for face, name in faces.items():  # all four roles hold a font again
                    ts.add_font(face, os.path.join(FONT_DIR, name))
                count = atlas_size()
                stale = held()
                atlas.clear()
                for _ in range(count):
                    atlas.add_font_default()
                first = order[cycle % len(order)]
                ts.add_font(first, os.path.join(FONT_DIR, faces[first]))
                state["removed"] += atlas_size() != count + 1
                say(event="cycle", cycle=cycle, first_role=first.name, stale_pointer=stale[first.name],
                    atlas_fonts=atlas_size(), expected_atlas_fonts=count + 1,
                    application_font_removed=atlas_size() != count + 1)
            for face, name in faces.items():
                ts.add_font(face, os.path.join(FONT_DIR, name))
            return
        if second and scenario.startswith("same-count"):
            while atlas_size() < before:
                atlas.add_font_from_file_ttf(os.path.join(FONT_DIR, "Roboto-Regular.ttf"), 15.0)
            say(event="application fonts back", atlas_fonts=atlas_size(), imtypeset_holds=held())
            if scenario == "same-count-add":
                ts.add_font(ts.Face.Regular, os.path.join(FONT_DIR, faces[ts.Face.Regular]))
                state["removed"] += atlas_size() != before + 1
                say(event="one imtypeset font added", atlas_fonts=atlas_size(),
                    expected_atlas_fonts=before + 1, imtypeset_holds=held())
            return
        if second and scenario == "clear-lost":
            say(event="imtypeset fonts not added again", atlas_fonts=atlas_size())
            return
        if ts.current_context() is None:
            ts.create_context()
        for face, name in faces.items():
            ts.add_font(face, os.path.join(FONT_DIR, name))
        say(event="imtypeset fonts added", atlas_fonts=atlas_size(), imtypeset_holds=held())

    def gui() -> None:
        state["frame"] += 1
        frame = state["frame"]
        if state["doc"] is None:
            state["doc"] = ts.Document.from_blocks(
                [ts.Block(ts.BlockKind.Heading, "Fonts, twice"),
                 ts.Block(text="A paragraph with bold in it and $x^2$.",
                          runs=[ts.Run(17, 21, ts.Face.Bold)], flags=ts.BlockFlags.Math),
                 ts.Block(ts.BlockKind.Code, "code = 1")])
        doc = state["doc"]
        doc.layout(400.0)
        origin = imgui.get_cursor_screen_pos()
        try:
            drawn = doc.draw(wheel._ZN5ImGui17GetWindowDrawListEv(), (origin.x, origin.y))
            shown = ts.text("a widget, $a+b$", flags=ts.TextFlags.Math)
        except Exception as exc:  # noqa: BLE001 -- reported, and the run goes on
            drawn = shown = False
            state["errors"][f"{type(exc).__name__}: {exc}"] = frame
        half = 0 if frame <= AT else 1
        state["draws"][half] += 1
        if frame in (AT, AT + 1, AT + 2, FRAMES):
            say(event="frame", frame=frame, document_drawn=drawn, widget_drawn=shown,
                last_error=ts.last_error().message, atlas_fonts=atlas_size(),
                imtypeset_holds=held())
        if frame == AT and scenario != "once":
            hello_imgui.get_runner_params().callbacks.load_additional_fonts = load_fonts
        if frame >= FRAMES:
            hello_imgui.get_runner_params().app_shall_exit = True

    def exiting() -> None:
        ts.destroy_context()

    runner.callbacks.load_additional_fonts = load_fonts
    runner.callbacks.show_gui = gui
    runner.callbacks.before_exit = exiting
    say(event="start", scenario=scenario, wheel=imgui_bundle.__version__,
        imgui=imgui.get_version(), headers=ts.host_info()["bundle"]["headers"])
    immapp.run(runner)
    say(event="done", scenario=scenario, load_additional_fonts_calls=state["loads"],
        frames=state["frame"], errors=state["errors"],
        application_fonts_removed=state["removed"])
    return 1 if state["removed"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
