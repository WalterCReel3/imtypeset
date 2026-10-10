"""SPIKE: an advtext view's text drawn through imtypeset in place of
advtext.backends.imgui_draw, on the same wheel, side by side.

Run with the reference project's interpreter and a library built for its wheel:

    python/host/build_bundle_library.sh
    IMTYPESET_LIBRARY=build/py-host-bundle/libimtypeset_bundle.so \
        ../imgui-adv-text/.venv/bin/python -P -B python/spike_bundle/advtext_view.py \
        [--frames 240 --screenshot out.png]

LEFT   advtext's own smallest view, `examples/minimal.py`'s MarkdownPane and
       show(), imported READ-ONLY from ../imgui-adv-text: parse once, compose
       and ship when the width changes, and every frame
       advtext.backends.imgui_draw.draw() walks the draw commands in Python.
RIGHT  the same paragraphs as imtypeset.Paragraph objects at the same em size
       and width: laid out when the width changes, one draw call each per frame.

The text is prose with bold and italic only, so that both sides set the same
thing (advtext's markdown layer does lists, tables and fences; imtypeset has no
markdown and this spike does not pretend otherwise).

Prints the median time each side's draw took per frame.
"""

import argparse
import importlib.util
import json
import os
import re
import statistics
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
ADVTEXT = os.environ.get("ADVTEXT_ROOT", os.path.join(os.path.dirname(REPO), "imgui-adv-text"))
sys.path[:0] = [os.path.join(REPO, "python"), os.path.join(REPO, "examples", "python")]
sys.path.append(ADVTEXT)

PARAGRAPHS = [
    "This paragraph was broken into lines by a **total-fit** line breaker and *justified* by "
    "stretching the glue between its words, the way TeX does it. Every glyph was then placed "
    "one at a time on a draw list that belongs to the window around it.",
    "The measured cost is what matters to a pane that redraws every frame: a paragraph that has "
    "not changed should cost one call, not one call per styled run, and a paragraph that has "
    "changed should cost **one layout** and nothing else.",
    "Nothing here is a widget. The text is geometry the host owns, which is why it can be "
    "*hit-tested*, selected and copied by arithmetic, and why the same lines come out of the "
    "reference and of its port: integers equal, doubles equal to the last bit.",
]


def styled(markdown: str):
    """(text, [(start, end, bold)]) for a paragraph using only ** and *."""
    text, runs = "", []
    for piece in re.split(r"(\*\*[^*]+\*\*|\*[^*]+\*)", markdown):
        bold = piece.startswith("**")
        inner = piece.strip("*") if piece.startswith("*") else piece
        if piece.startswith("*"):
            runs.append((len(text), len(text) + len(inner), bold))
        text += inner
    return text, runs


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--frames", type=int, default=0)
    parser.add_argument("--screenshot", default="")
    parser.add_argument("--repeat", type=int, default=4, help="copies of the three paragraphs")
    args = parser.parse_args()

    from imgui_bundle import hello_imgui, imgui, immapp

    import imtypeset as ts
    import bundle_support as support
    from advtext import load_faces
    from advtext.style import BODY_SIZE_PX

    spec = importlib.util.spec_from_file_location(
        "advtext_minimal", os.path.join(ADVTEXT, "examples", "minimal.py")
    )
    minimal = importlib.util.module_from_spec(spec)
    sys.modules["advtext_minimal"] = minimal
    spec.loader.exec_module(minimal)

    source = PARAGRAPHS * args.repeat
    pane = minimal.MarkdownPane("\n\n".join(source) + "\n")
    em_px = BODY_SIZE_PX  # Theme.build()'s default body em, in device pixels
    loaded, paragraphs = [], []

    def load_fonts() -> None:
        hello_imgui.imgui_default_settings.load_default_font_with_font_awesome_icons()
        loaded.append(load_faces())  # advtext's faces, as its example loads them
        if ts.current_context() is None:
            ts.create_context()
        support.add_fonts()

    style = ts.Style(EmSize=em_px)

    def draw_through_imtypeset() -> None:
        if not paragraphs:
            for markdown in source:
                text, runs = styled(markdown)
                paragraphs.append(ts.Paragraph(text, [
                    ts.Run(a, b, ts.Face.Bold if bold else ts.Face.Italic) for a, b, bold in runs
                ]))
        width = float(max(1, int(imgui.get_content_region_avail().x)))
        draw_list = support.draw_list_address()
        for paragraph in paragraphs:
            paragraph.layout(width, style)
            start = imgui.get_cursor_screen_pos()
            paragraph.draw(draw_list, (start.x, start.y))
            imgui.dummy(imgui.ImVec2(*paragraph.size))

    times = {"advtext": [], "imtypeset": []}
    counted = [0]
    glyphs = [0]

    def advtext_side() -> None:
        glyphs[0] = minimal.show(pane, loaded[-1], imgui).glyphs

    def gui() -> None:
        counted[0] += 1
        size = imgui.get_io().display_size
        half = size.x / 2.0
        for n, (name, draw) in enumerate((("advtext", advtext_side), ("imtypeset", draw_through_imtypeset))):
            imgui.set_next_window_pos(imgui.ImVec2(n * half, 0.0))
            imgui.set_next_window_size(imgui.ImVec2(half, size.y))
            imgui.begin(name, None, imgui.WindowFlags_.no_collapse)
            started = time.perf_counter()
            draw()
            times[name].append(time.perf_counter() - started)
            imgui.end()
        if args.frames and counted[0] >= args.frames:
            hello_imgui.get_runner_params().app_shall_exit = True

    def before_exit() -> None:
        for paragraph in paragraphs:
            paragraph.close()
        print(json.dumps({"imtypeset_layouts": ts.stats().Layouts, "advtext_layouts": pane.layouts,
                          "advtext_glyphs": glyphs[0], "em_px": em_px}))
        ts.destroy_context()

    runner = hello_imgui.RunnerParams()
    runner.app_window_params.window_title = "SPIKE: advtext view through imtypeset"
    runner.app_window_params.window_geometry.size = (1100, 700)
    runner.imgui_window_params.default_imgui_window_type = (
        hello_imgui.DefaultImGuiWindowType.no_default_window
    )
    runner.ini_disable = True
    runner.fps_idling.enable_idling = False
    runner.callbacks.load_additional_fonts = load_fonts
    runner.callbacks.show_gui = gui
    runner.callbacks.before_exit = before_exit
    immapp.run(runner)

    if args.screenshot:
        support.write_png(args.screenshot, hello_imgui.final_app_window_screenshot())
    warm = max(10, counted[0] // 4)
    report = {"frames": counted[0], "paragraphs": len(source),
              "wheel": ts.host_info()["bundle"]["wheel_version"]}
    for name, samples in times.items():
        steady = sorted(samples[warm:]) or [0.0]
        report[name] = {
            "first_frame_us": round(samples[0] * 1e6, 1),
            "median_us": round(statistics.median(steady) * 1e6, 1),
            "p95_us": round(steady[int(0.95 * (len(steady) - 1))] * 1e6, 1),
        }
    print(json.dumps(report))


if __name__ == "__main__":
    main()
