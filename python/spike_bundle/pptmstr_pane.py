"""SPIKE: a pptmstr transcript pane drawn through imtypeset, beside pptmstr's own.

Run with pptmstr's interpreter (its wheel is not the reference project's) and a
library built for it:

    python/host/build_bundle_library.sh ../imgui_bundle ../pptmstr/.venv/bin/python
    IMTYPESET_LIBRARY=build/py-host-bundle/libimtypeset_bundle.so \
        ../pptmstr/.venv/bin/python -P -B python/spike_bundle/pptmstr_pane.py \
        [--frames 240 --screenshot out.png]

The application is shaped as pptmstr's is (pptmstr/app.py): hello_imgui
RunnerParams, `theme.load_fonts` as the load_additional_fonts callback, a
show_gui that draws panes, immapp.run. pptmstr is imported READ-ONLY from
../pptmstr (nothing is written there: -B, no settings file).

LEFT   pptmstr.ui.rich_pane.draw(): the pane as it is today. Per frame, per
       block: inline tokens wrapped in Python (span_layout.layout_inline with
       ImFont.calc_word_wrap_position_python and imgui.calc_text_size), then
       one ImDrawList.add_text per styled run.
RIGHT  the same blocks through imtypeset: one imtypeset.Paragraph per prose
       block, made once from the same inline tokens (bold, italic and code as
       runs), laid out when the width changes, and drawn by one call per frame.
       Fences are a Code-face paragraph. Tables and rules are left to pptmstr's
       own drawing; they are not what this replaces.

Prints the median time each pane's draw took per frame.
"""

import argparse
import json
import os
import statistics
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
PPTMSTR = os.environ.get("PPTMSTR_ROOT", os.path.join(os.path.dirname(REPO), "pptmstr"))
sys.path[:0] = [os.path.join(REPO, "python"), os.path.join(REPO, "examples", "python")]
sys.path.append(PPTMSTR)
# hello_imgui hands a screenshot over as a numpy array, and pptmstr's environment
# has no numpy. SPIKE_EXTRA_SITE names a site-packages that does; it goes LAST,
# so nothing of pptmstr's own environment is shadowed.
if os.environ.get("SPIKE_EXTRA_SITE"):
    sys.path.append(os.environ["SPIKE_EXTRA_SITE"])

DOC = (
    "# Reviewing the line breaker\n"
    "\n"
    "The breaker keeps a list of **active nodes** and, for every legal breakpoint, asks each of "
    "them how bad the line from there to here would be. With badness $b$, penalty $p$ and line "
    "penalty $l$ the demerits are $d = (l + b)^2 + p^2$ when $p \\geq 0$, and the *total-fit* "
    "path is the one that minimises $\\sum_i d_i$ over the whole paragraph.\n"
    "\n"
    "- `try_break` runs once per candidate, so it is the hot loop and allocates nothing.\n"
    "- A ratio $r = \\frac{w - W}{Y}$ above the tolerance deactivates the node.\n"
    "- The second pass allows hyphenation; the *emergency* pass adds stretch.\n"
    "\n"
    "1. Read the reference module before porting it.\n"
    "2. Replay the goldens: integers equal, doubles equal to the last bit.\n"
    "\n"
    "> A divergence is a finding to record, never something to tune a test around.\n"
    "\n"
    "```py\n"
    "def demerits(l, b, p):\n"
    "    return (l + b) ** 2 + p * p if p >= 0 else (l + b) ** 2 - p * p\n"
    "```\n"
    "\n"
    "The measured cost is what matters to a pane that redraws every frame: a paragraph that "
    "has not changed should cost one call, not one call per styled run, and a paragraph that "
    "has changed should cost one layout. See [the notes](http://example.com/notes) for the "
    "numbers, and $e^{i\\pi} + 1 = 0$ for no reason at all.\n"
    "\n"
)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--frames", type=int, default=0)
    parser.add_argument("--screenshot", default="")
    parser.add_argument("--repeat", type=int, default=4, help="copies of the document shown")
    parser.add_argument("--size", type=float, default=0.0, help="font size of the right pane")
    parser.add_argument("--ragged", action="store_true", help="right pane flush left, not justified")
    args = parser.parse_args()

    from imgui_bundle import hello_imgui, imgui, immapp

    import imtypeset as ts
    import bundle_support as support
    from pptmstr import theme
    from pptmstr.transcript import SegmentKind, Transcript
    from pptmstr.ui import rich_pane
    from pptmstr.ui.blocks import BlockCursor, BlockKind
    from pptmstr.ui.span_layout import layout_inline
    from pptmstr.ui.transcript_pane import NodeTranscript

    # The blocks, exactly as pptmstr's transcript pane derives them.
    transcript = Transcript()
    transcript.append(SegmentKind.OUTPUT, DOC * args.repeat)
    cache = NodeTranscript()
    cache.sync(transcript, transcript.published_length)
    cursor = BlockCursor()
    cursor.feed(cache.lines[: len(cache.lines) - (1 if cache.open_line else 0)])
    blocks = cursor.blocks

    prose = (BlockKind.PARAGRAPH, BlockKind.HEADING, BlockKind.BULLET_ITEM,
             BlockKind.ORDERED_ITEM, BlockKind.BLOCKQUOTE)
    paragraphs: dict = {}
    indent = 18.0

    def paragraph_for(index: int, block):
        """(Paragraph, left indent, marker) for a block, made once."""
        made = paragraphs.get(index)
        if made is not None:
            return made
        if block.kind is BlockKind.FENCE:
            text = "\n".join(rich_pane.fence_body(block))
            made = (ts.Paragraph(text, [ts.Run(0, len(text), ts.Face.Code)]), 0.0, "")
        else:
            # pptmstr's own flattening of the inline tokens into styled runs,
            # asked for one row of unlimited width.
            rows = layout_inline(block.inline or (), 1e9, lambda t, w: len(t), lambda t: 0.0)
            text, runs = "", []
            for n, row in enumerate(rows):
                text += "\n" if n else ""
                for run in row.runs:
                    start = len(text)
                    text += run.text
                    face = (ts.Face.Code if run.code else ts.Face.Bold if run.bold
                            else ts.Face.Italic if run.italic else ts.Face.Default)
                    colour = theme.P.accent.u32 if run.link else 0
                    if face != ts.Face.Default or colour:
                        runs.append(ts.Run(start, len(text), face, 0.0, colour))
            if block.kind is BlockKind.HEADING:
                runs = [ts.Run(0, len(text), ts.Face.Bold, 1.4 - 0.1 * block.level)]
            marker = {BlockKind.BULLET_ITEM: "•", BlockKind.ORDERED_ITEM: "1."}.get(block.kind, "")
            left = indent if block.kind in (BlockKind.BULLET_ITEM, BlockKind.ORDERED_ITEM,
                                            BlockKind.BLOCKQUOTE) else 0.0
            made = (ts.Paragraph(text, runs, ts.TextFlags.Math), left, marker)
        paragraphs[index] = made
        return made

    def draw_through_imtypeset() -> None:
        if args.size:
            imgui.push_font(None, args.size)
        try:
            draw_blocks()
        finally:
            if args.size:
                imgui.pop_font()

    style = ts.Style(Align=ts.Align.Left) if args.ragged else None

    def draw_blocks() -> None:
        width = imgui.get_content_region_avail().x
        draw_list = support.draw_list_address()
        for index, block in enumerate(blocks):
            if block.kind not in prose and block.kind is not BlockKind.FENCE:
                rich_pane._draw_one(index, rich_state, block, width, imgui.get_font_size())
                continue
            paragraph, left, marker = paragraph_for(index, block)
            paragraph.layout(max(width - left, 1.0), style)  # free when nothing changed
            start = imgui.get_cursor_screen_pos()
            if marker:
                imgui.get_window_draw_list().add_text(start, theme.P.text_dim.u32, marker)
            paragraph.draw(draw_list, (start.x + left, start.y))
            w, h = paragraph.size
            imgui.push_id(index)
            imgui.invisible_button("##hit", imgui.ImVec2(width, max(h, 1.0)))
            if imgui.is_item_hovered() and imgui.is_mouse_clicked(1):
                imgui.set_clipboard_text("\n".join(block.lines))  # pptmstr's copy-block
            imgui.pop_id()

    rich_state = rich_pane.RichState()
    times = {"pptmstr": [], "imtypeset": []}
    counted = [0]

    def gui() -> None:
        counted[0] += 1
        size = imgui.get_io().display_size
        half = size.x / 2.0
        for n, (name, draw) in enumerate((
            ("pptmstr", lambda: rich_pane.draw(rich_state, blocks, None)),
            ("imtypeset", draw_through_imtypeset),
        )):
            imgui.set_next_window_pos(imgui.ImVec2(n * half, 0.0))
            imgui.set_next_window_size(imgui.ImVec2(half, size.y))
            imgui.begin(f"{name}: {len(blocks)} blocks", None, imgui.WindowFlags_.no_collapse)
            started = time.perf_counter()
            draw()
            times[name].append(time.perf_counter() - started)
            imgui.end()
        if args.frames and counted[0] >= args.frames:
            hello_imgui.get_runner_params().app_shall_exit = True

    def load_fonts() -> None:
        theme.load_fonts()  # pptmstr's faces first, as in its app
        if ts.current_context() is None:
            ts.create_context()
        support.add_fonts()

    def before_exit() -> None:
        for made in paragraphs.values():
            made[0].close()
        print(json.dumps({"layouts": ts.stats().Layouts, "paragraphs": len(paragraphs)}))
        ts.destroy_context()

    runner = hello_imgui.RunnerParams()
    runner.app_window_params.window_title = "SPIKE: pptmstr pane through imtypeset"
    runner.app_window_params.window_geometry.size = (1280, 900)
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
    warm = max(10, len(times["pptmstr"]) // 4)
    report = {"frames": counted[0], "blocks": len(blocks), "wheel": ts.host_info()["bundle"]["wheel_version"]}
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
