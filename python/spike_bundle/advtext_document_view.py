"""SPIKE: an advtext view's text through ONE imtypeset.Document, beside
advtext's own views and beside the per-paragraph spike (advtext_view.py).

Run with the reference project's interpreter and a library built for its wheel:

    IMTYPESET_BUNDLE_OUT=build/api-document-bundle-advtext sh python/host/build_bundle_library.sh
    IMTYPESET_LIBRARY=build/api-document-bundle-advtext/libimtypeset_bundle.so \
        ../imgui-adv-text/.venv/bin/python -P -B python/spike_bundle/advtext_document_view.py \
        [--repeat 4] [--stream] [--today stream|minimal] [--frames 240 --screenshot out.png]

advtext is imported READ-ONLY from ../imgui-adv-text.

TODAY      --today stream (the default): advtext.StreamDocument and
           advtext.StreamView, the view advtext has for text that arrives a
           piece at a time and for long documents; `view.draw()` once a frame.
           --today minimal: examples/minimal.py's MarkdownPane and show(), the
           whole document composed at once and every draw command walked in
           Python every frame (what advtext_view.py measured).
PARAGRAPH  one imtypeset.Paragraph per paragraph: a layout, a draw and a dummy
           each, every frame.
DOCUMENT   the paragraphs as imtypeset.Blocks in ONE Document: one call when
           the text changes (append_text for the tail), and per frame
           layout + draw + size.

The text is prose with bold and italic only, so that every side sets the same
thing. Prints, per pane, the median time of its draw per frame and the calls
into imtypeset one steady frame made (advtext's own calls into the
imgui_bundle binding are not counted: its view imports imgui itself).
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
STREAM = (
    "The live tail arrives a few characters at a time, as a model writes it, and the pane has "
    "to show it on the frame it arrives without setting the whole transcript again. "
)


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
    parser.add_argument("--stream", action="store_true", help="a live tail that grows every frame")
    parser.add_argument("--today", default="stream", choices=("stream", "minimal"))
    parser.add_argument("--panes", default="today,paragraph,document")
    args = parser.parse_args()
    shown = args.panes.split(",")

    from imgui_bundle import hello_imgui, imgui, immapp

    import imtypeset as ts
    import bundle_support as support
    from advtext import (
        DARK_PALETTE, StreamDocument, StreamView, Theme, char_measure, fontlib_metrics, load_faces,
    )
    from advtext.style import BODY_SIZE_PX

    spec = importlib.util.spec_from_file_location(
        "advtext_minimal", os.path.join(ADVTEXT, "examples", "minimal.py")
    )
    minimal = importlib.util.module_from_spec(spec)
    sys.modules["advtext_minimal"] = minimal
    spec.loader.exec_module(minimal)

    source = PARAGRAPHS * args.repeat
    markdown = "\n\n".join(source) + "\n\n"
    em_px = BODY_SIZE_PX
    loaded = []
    live_added = [""]  # What this frame adds to the tail; None when a new paragraph starts.
    live_length = [0]

    def load_fonts() -> None:
        hello_imgui.imgui_default_settings.load_default_font_with_font_awesome_icons()
        loaded.append(load_faces())
        if ts.current_context() is None:
            ts.create_context()
        support.add_fonts()

    style = ts.Style(EmSize=em_px)

    # --- TODAY -------------------------------------------------------------------------------

    today: dict = {}

    def draw_today() -> None:
        if args.today == "minimal":
            if "pane" not in today:
                today["pane"] = minimal.MarkdownPane(markdown)
            minimal.show(today["pane"], loaded[-1], imgui)
            return
        if "view" not in today:
            theme = Theme.build()
            today["stream"] = StreamDocument(theme, fontlib_metrics)
            today["view"] = StreamView(today["stream"], char_measure(fontlib_metrics))
            today["stream"].append(markdown)
        if args.stream:
            today["stream"].append("\n\n" if live_added[0] is None else live_added[0])
        imgui.begin_child("##advtext", imgui.ImVec2(0.0, 0.0), 0,
                          imgui.WindowFlags_.always_vertical_scrollbar.value)
        today["view"].draw(fonts=loaded[-1], palette=DARK_PALETTE)
        imgui.end_child()

    # --- PARAGRAPH -----------------------------------------------------------------------------

    paragraphs: list = []
    tail: dict = {"text": ""}

    def draw_paragraphs() -> None:
        if not paragraphs:
            for one in source:
                text, runs = styled(one)
                paragraphs.append(ts.Paragraph(text, [
                    ts.Run(a, b, ts.Face.Bold if bold else ts.Face.Italic) for a, b, bold in runs
                ]))
            if args.stream:
                paragraphs.append(ts.Paragraph())
        if args.stream:
            if live_added[0] is None:
                paragraphs.append(ts.Paragraph())
                tail["text"] = ""
            else:
                tail["text"] += live_added[0]
                paragraphs[-1].set_text(tail["text"])
        width = float(max(1, int(imgui.get_content_region_avail().x)))
        draw_list = support.draw_list_address()
        for paragraph in paragraphs:
            paragraph.layout(width, style)
            start = imgui.get_cursor_screen_pos()
            paragraph.draw(draw_list, (start.x, start.y))
            imgui.dummy(imgui.ImVec2(*paragraph.size))

    # --- DOCUMENT ------------------------------------------------------------------------------

    document = [None]

    def draw_document() -> None:
        doc = document[0]
        if doc is None:
            blocks = []
            for one in source:
                text, runs = styled(one)
                blocks.append(ts.Block(text=text, runs=[
                    ts.Run(a, b, ts.Face.Bold if bold else ts.Face.Italic) for a, b, bold in runs
                ]))
            if args.stream:
                blocks.append(ts.Block(text=""))
            doc = document[0] = ts.Document.from_blocks(blocks, [ts.BlockStyle(SpaceBelow=em_px * 0.5)])
        if args.stream:
            if live_added[0] is None:
                doc.replace_blocks(len(doc), 0, [ts.Block(text="")])
            elif live_added[0]:
                doc.append_text(live_added[0])
        width = float(max(1, int(imgui.get_content_region_avail().x)))
        doc.layout(width, style)
        start = imgui.get_cursor_screen_pos()
        doc.draw(support.draw_list_address(), (start.x, start.y))
        imgui.dummy(imgui.ImVec2(*doc.size()))

    panes = [(name, draw) for name, draw in (
        ("today", draw_today), ("paragraph", draw_paragraphs), ("document", draw_document),
    ) if name in shown]
    times = {name: [] for name, _ in panes}
    calls = {name: 0 for name, _ in panes}
    counted = [0]
    count_frame = max(6, (args.frames or 80) // 2)

    class CountingLibrary:
        def __init__(self, real):
            self.real, self.n = real, 0

        def __getattr__(self, name):
            function = getattr(self.real, name)

            def call(*a):
                self.n += 1
                return function(*a)

            return call

    def gui() -> None:
        counted[0] += 1
        if args.stream:
            if live_length[0] >= 1100:
                live_added[0], live_length[0] = None, 0
            else:
                live_added[0] = (STREAM * 12)[live_length[0] : live_length[0] + 12]
                live_length[0] += 12
        size = imgui.get_io().display_size
        each = size.x / len(panes)
        for n, (name, draw) in enumerate(panes):
            imgui.set_next_window_pos(imgui.ImVec2(n * each, 0.0))
            imgui.set_next_window_size(imgui.ImVec2(each, size.y))
            imgui.begin(f"{name}: {len(source)} paragraphs", None, imgui.WindowFlags_.no_collapse)
            if counted[0] == count_frame:
                real = ts._lib
                ts._lib = CountingLibrary(real)
                try:
                    draw()
                finally:
                    calls[name], ts._lib = ts._lib.n, real
            else:
                started = time.perf_counter()
                draw()
                times[name].append(time.perf_counter() - started)
            if name != "today" or args.today == "minimal":
                imgui.set_scroll_here_y(1.0)
            imgui.end()
        if args.frames and counted[0] >= args.frames:
            hello_imgui.get_runner_params().app_shall_exit = True

    def before_exit() -> None:
        extra = {"imtypeset_layouts": ts.stats().Layouts, "em_px": em_px}
        if document[0] is not None:
            extra["document"] = document[0].stats()._asdict()
        print(json.dumps(extra))
        ts.destroy_context()

    runner = hello_imgui.RunnerParams()
    runner.app_window_params.window_title = "SPIKE: advtext text through one imtypeset.Document"
    runner.app_window_params.window_geometry.size = (1500, 800)
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
    report = {"frames": counted[0], "paragraphs": len(source), "stream": args.stream,
              "today_is": args.today, "wheel": ts.host_info()["bundle"]["wheel_version"]}
    for name, samples in times.items():
        warm = max(4, len(samples) // 4)
        steady = sorted(samples[warm:]) or [0.0]
        report[name] = {
            "first_frame_us": round(samples[0] * 1e6, 1),
            "median_us": round(statistics.median(steady) * 1e6, 1),
            "p95_us": round(steady[int(0.95 * (len(steady) - 1))] * 1e6, 1),
            "imtypeset_calls_per_frame": calls[name],
        }
    print(json.dumps(report))


if __name__ == "__main__":
    main()
