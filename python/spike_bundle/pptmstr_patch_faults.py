"""What the pptmstr patch's rich pane does when something inside it goes wrong,
and what its find marks and streaming tables do, run in pptmstr's own wheel.

    IMTYPESET_LIBRARY=$PWD/build/pptmstr-bundle/libimtypeset_bundle.so \
    sh tools/shot.sh -- ../pptmstr/.venv/bin/python -P -B \
        python/spike_bundle/pptmstr_patch_faults.py --scenario table-raises [--overlay DIR]

`pptmstr.ui.rich_pane.draw` is the overlay's (python/spike_bundle/pptmstr_overlay,
or --overlay DIR for another copy of the same three files), every other module
is pptmstr's, read-only from ../pptmstr. One scenario a process; one JSON object
on stdout. Exit 0 when every expectation in "verdict" holds, 1 when one does
not, 2 when the application itself died.

A fault is put in at frame 30 by replacing a function; nothing in the overlay
knows about this file.

  none               nothing goes wrong.
  table-raises       `_draw_table` raises from then on.
  table-raises-once  it raises in one frame only.
  table-cell-raises  a cell raises while the table is being drawn: between
                     BeginTable and EndTable, inside the table's child.
  menu-copy-raises   a drag selects, a right click opens the pane's menu, "copy
                     selection" is chosen (the menu item answers True once) and
                     `Document.range_text` raises: inside BeginPopup/EndPopup.
  key-copy-raises    the same failure from Ctrl+C: after the document was drawn.
  describe-raises-once
                     describing the blocks raises in one frame: before anything
                     of the frame was drawn.
  draw-raises-once   `Document.draw` raises in one frame.
  soft-standing      nothing raises: one block is described with its runs out of
                     order, which the library applies and reports as
                     DescriptionCut, so `Document.draw` answers False in every
                     frame the block is in view. That is "done, differently" and
                     must never count as a failing frame.

What is watched, every frame, around the one call `rich_pane.draw(...)`:
  the current ImGui window before and after (another one after means a Begin
  was left open); whether both renderers drew (the frame was drawn twice);
  whether the call raised; whether imtypeset is still in use and, if it is not,
  the reason the patch gives.

  find               the marks a needle makes: each must cover the needle in the
                     block's own text (a block that grows when lower-cased is in
                     the page), and their number is capped.
  stream-find        a long document, a needle, and text arriving a few
                     characters a frame: the time `_marks` takes per frame, and
                     the marks at the end against a search from nothing.
  stream-table       a table whose cells wrap, arriving a few characters a frame:
                     how often its Host block's height falls back to the guess,
                     and in how many frames the block is shorter than the table.
"""

import argparse
import importlib.util
import json
import os
import statistics
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path[:0] = [os.path.join(REPO, "python"), HERE]

import pptmstr_corpus as corpus  # noqa: E402

FAULT_AT = 30

PAGE = (
    "# A page with everything in it\n"
    "\n"
    "A paragraph with **bold**, a [link](http://example.com/x) and `code`, long enough to wrap "
    "at the pane's width so that a drag across it selects more than one line of text.\n"
    "\n"
    "| name | value | note |\n"
    "|---|---|---|\n"
    "| alpha | 1 | the first |\n"
    "| beta | 2 | the second |\n"
    "\n"
    "- an item\n"
    "- another item\n"
    "\n"
    "```py\n"
    "x = 1\n"
    "```\n"
    "\n"
    "The last paragraph.\n"
)

# U+0130 is one character whose lower case is two ("i" and a combining dot), so
# every offset after it in the lower-cased text is one further on per İ.
FIND_PAGE = (
    "İİİ İstanbul has a NEEDLE in it, and a needle after it.\n"
    "\n"
    "A plain block with one Needle.\n"
    "\n" + ("needle " * 3000) + "\n"
    "\n"
    "The block after the flood has a needle too.\n"
)
NEEDLE = "Needle"

TABLE_TAIL = (
    "| step | what | cost |\n"
    "|---|---|---|\n"
    + "".join(
        f"| {n} | row number {n} of the table, with a cell long enough that it has to wrap onto a "
        f"second line and a third at the width the pane gives one column of three, so that the "
        f"table is taller than a line a row | {n * 7} us |\n"
        for n in range(1, 7)
    )
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--scenario", default="none")
    parser.add_argument("--overlay", default=os.path.join(HERE, "pptmstr_overlay"))
    parser.add_argument("--frames", type=int, default=70)
    parser.add_argument("--chunk", type=int, default=6, help="stream: characters a frame")
    args = parser.parse_args()
    scenario = args.scenario

    spec = importlib.util.spec_from_file_location(
        "pptmstr_overlay_site", os.path.join(args.overlay, "sitecustomize.py"))
    overlay = importlib.util.module_from_spec(spec)
    os.environ["PPTMSTR_OVERLAY"] = "0"
    spec.loader.exec_module(overlay)
    overlay.install()

    from imgui_bundle import hello_imgui, imgui, immapp

    from pptmstr import theme
    from pptmstr.transcript import SegmentKind
    from pptmstr.ui import rich_pane
    from pptmstr.ui.blocks import BlockCursor
    from pptmstr.ui.detail import _ProseLine

    typeset = sys.modules["pptmstr.ui.typeset"]

    # --- the content ---------------------------------------------------------------------

    stream_text = ""
    if scenario == "find":
        settled_text = FIND_PAGE
    elif scenario == "stream-find":
        reports = "".join(text for kind, text in corpus.ops_from_reports() if kind is SegmentKind.OUTPUT)
        settled_text = reports
        stream_text = "\nA tail that arrives slowly and holds the layout twice: the layout.\n" * 6
    elif scenario == "stream-table":
        settled_text = PAGE
        stream_text = "\n" + TABLE_TAIL
    else:
        settled_text = PAGE
    needle = {"find": NEEDLE, "stream-find": "the layout"}.get(scenario, "")

    lines = [_ProseLine(SegmentKind.OUTPUT, line) for line in settled_text.split("\n")]
    cursor = BlockCursor()
    arrived = [0]

    def blocks_now():
        """(blocks, live) as transcript_pane derives them: the last line is still open."""
        if arrived[0] < len(stream_text):
            arrived[0] = min(len(stream_text), arrived[0] + args.chunk)
            tail = (settled_text + stream_text[: arrived[0]]).split("\n")
            del lines[len(settled_text.split("\n")) - 1 :]
            lines.extend(_ProseLine(SegmentKind.OUTPUT, line) for line in tail[len(lines) :])
        cursor.feed(lines[: len(lines) - 1])
        return cursor.blocks, cursor.live_block(lines)

    # --- what is watched -------------------------------------------------------------------

    rich = rich_pane.RichState()
    rich.find = needle
    frame = [0]
    this = {"classic": 0, "document": 0}
    events: list = []
    totals = {"frames": 0, "classic frames": 0, "document frames": 0, "drawn twice": 0,
              "left a window open": 0, "draw raised": 0, "blank frames": 0}
    fault = {"armed": False, "fired": 0, "once": scenario.endswith("-once")}
    soft = {"blocks cut": 0, "draw answered False": 0, "codes": set(), "failures seen": 0}
    pane = {"origin": None, "selection": None}
    marks_seen: dict = {}
    marks_time: list = []
    table_log: list = []

    class Injected(RuntimeError):
        pass

    def fire() -> bool:
        # Only on the imtypeset path: the classic renderer shares `_draw_table`.
        if not fault["armed"] or (fault["once"] and fault["fired"]) or typeset.module() is None:
            return False
        fault["fired"] += 1
        return True

    real_classic = rich_pane._draw_classic

    def counted_classic(*a, **k):
        this["classic"] += 1
        return real_classic(*a, **k)

    rich_pane._draw_classic = counted_classic

    def instrument(ts) -> None:
        real_draw = ts.Document.draw
        real_range_text = ts.Document.range_text
        real_pack = ts.Document.pack_marks
        real_table = rich_pane._draw_table
        real_describe = rich_pane._describe
        real_marks = rich_pane._marks
        real_menu_item = imgui.menu_item_simple
        real_plain = rich_pane._plain_text
        inside = [False]

        def draw(self, *a, **k):
            if scenario == "draw-raises-once" and fire():
                raise Injected("Document.draw")
            this["document"] += 1
            answer = real_draw(self, *a, **k)
            if answer is False:
                soft["draw answered False"] += 1
                soft["codes"].add(int(ts.last_error().code))
            return answer

        def range_text(self, *a, **k):
            if scenario in ("menu-copy-raises", "key-copy-raises") and fire():
                raise Injected("Document.range_text")
            return real_range_text(self, *a, **k)

        def pack_marks(self, marks):
            marks_seen["last"] = [m for m in marks if m.place == ts.MarkPlace.Behind]
            return real_pack(self, marks)

        def table(index, block):
            if scenario.startswith("table-raises") and fire():
                raise Injected("_draw_table")
            inside[0] = True
            try:
                return real_table(index, block)
            finally:
                inside[0] = False

        def plain(tokens):
            if scenario == "table-cell-raises" and inside[0] and fire():
                raise Injected("a table cell")
            return real_plain(tokens)

        def describe(*a, **k):
            if scenario == "describe-raises-once" and fire():
                raise Injected("_describe")
            if scenario != "soft-standing" or soft["blocks cut"]:
                return real_describe(*a, **k)
            out = a[4]
            before = len(out)
            real_describe(*a, **k)
            for index in range(before, len(out)):
                made = out[index]
                if made.kind == ts.BlockKind.Paragraph and len(made.text) >= 8:
                    out[index] = made._replace(runs=(ts.Run(3, 5), ts.Run(0, 2)))
                    soft["blocks cut"] += 1
                    break

        def marks(*a, **k):
            started = time.perf_counter()
            try:
                return real_marks(*a, **k)
            finally:
                marks_time.append((frame[0], time.perf_counter() - started))

        def menu_item(label, *a, **k):
            answer = real_menu_item(label, *a, **k)
            if scenario == "menu-copy-raises" and label == "copy selection" and fault["armed"]:
                return True  # the click on it
            return answer

        ts.Document.draw = draw
        ts.Document.range_text = range_text
        ts.Document.pack_marks = pack_marks
        rich_pane._draw_table = table
        rich_pane._plain_text = plain
        rich_pane._describe = describe
        rich_pane._marks = marks
        imgui.menu_item_simple = menu_item

    def window_name() -> str:
        return imgui.internal.get_current_window().name

    def script() -> None:
        """Synthetic mouse and keys, for the scenarios that need a selection or a menu."""
        if pane["origin"] is None or scenario not in ("menu-copy-raises", "key-copy-raises"):
            return
        io = imgui.get_io()
        x, y = pane["origin"]
        f = frame[0] + 1
        if f < 20:
            io.add_mouse_pos_event(*((x + 30.0, y + 48.0) if f < 8 else (x + 330.0, y + 66.0)))
            io.add_mouse_button_event(0, 5 <= f < 12)
        elif scenario == "menu-copy-raises":
            io.add_mouse_pos_event(x + 120.0, y + 36.0)
            io.add_mouse_button_event(1, f in (20, 21))
        elif f in (FAULT_AT + 1, FAULT_AT + 2):
            io.add_key_event(imgui.Key.mod_ctrl, f == FAULT_AT + 1)
            io.add_key_event(imgui.Key.c, f == FAULT_AT + 1)

    def gui() -> None:
        frame[0] += 1
        f = frame[0]
        if f == 1 and typeset.module() is not None:
            instrument(typeset.module())
        if f == FAULT_AT:
            fault["armed"] = True
        this["classic"] = this["document"] = 0
        blocks, live = blocks_now()
        if scenario == "describe-raises-once" and f == FAULT_AT:
            blocks = list(blocks)  # another sequence: everything is described again
        viewport = imgui.get_main_viewport()
        imgui.set_next_window_pos(viewport.pos)
        imgui.set_next_window_size(viewport.size)
        imgui.begin("faults", None, imgui.WindowFlags_.no_decoration | imgui.WindowFlags_.no_move)
        imgui.begin_child("pane", imgui.ImVec2(0.0, 0.0))
        origin = imgui.get_cursor_screen_pos()
        pane["origin"] = (origin.x, origin.y)
        before = window_name()
        raised = None
        try:
            rich_pane.draw(rich, blocks, live)
        except Exception as exc:  # noqa: BLE001 -- what the pane's caller would have met
            raised = f"{type(exc).__name__}: {exc}"
        after = window_name()
        event = {}
        if after != before:
            event["left open"] = after
            totals["left a window open"] += 1
        if raised:
            event["draw raised"] = raised
            totals["draw raised"] += 1
        if this["classic"] and this["document"]:
            event["drawn twice"] = True
            totals["drawn twice"] += 1
        if not this["classic"] and not this["document"]:
            totals["blank frames"] += 1
            event["blank"] = True
        totals["frames"] += 1
        totals["classic frames"] += 1 if this["classic"] else 0
        totals["document frames"] += 1 if this["document"] else 0
        soft["failures seen"] = max(soft["failures seen"], rich.typeset_failures)
        st = rich.typeset
        if f == 19 and st is not None:
            pane["selection"] = repr(st.selection)
        if os.environ.get("FAULTS_DEBUG") and 18 <= f <= 26:
            m = imgui.get_mouse_pos()
            sys.stderr.write(f"DBG f={f} down1={imgui.is_mouse_down(1)} rel1={imgui.is_mouse_released(1)} "
                             f"hov={imgui.is_item_hovered()} mouse=({m.x:.0f},{m.y:.0f}) origin={pane['origin']} "
                             f"rect={imgui.get_item_rect_min().y:.0f}..{imgui.get_item_rect_max().y:.0f} "
                             f"open={imgui.is_popup_open('##rich_block_ctx')}\n")
        if f == FAULT_AT - 1:
            pane["menu open"] = imgui.is_popup_open("##rich_block_ctx")
        if scenario == "stream-table" and st is not None and len(st.tables) > 1:
            last = st.tables[-1]  # the one that is arriving; the page has one of its own
            table_log.append((f, round(st.extents.get(last, -1.0), 1),
                              round(st.measured.get(last, -1.0), 1)))
        if event:
            event["frame"] = f
            event["in use"] = typeset.module() is not None
            events.append(event)
        # The harness's own End calls: with a window left open they are the wrong ones.
        for end in (imgui.end_child, imgui.end):
            try:
                end()
            except Exception as exc:  # noqa: BLE001
                events.append({"frame": f, "harness " + end.__name__: f"{type(exc).__name__}: {exc}"[:200]})
        if f >= args.frames and arrived[0] >= len(stream_text):
            hello_imgui.get_runner_params().app_shall_exit = True

    runner = hello_imgui.RunnerParams()
    runner.app_window_params.window_title = "pptmstr patch faults"
    runner.app_window_params.window_geometry.size = (900, 700)
    runner.ini_disable = True
    runner.fps_idling.enable_idling = False
    runner.callbacks.load_additional_fonts = theme.load_fonts
    runner.callbacks.show_gui = gui
    runner.callbacks.pre_new_frame = script

    died = None
    try:
        immapp.run(runner)
    except BaseException as exc:  # noqa: BLE001 -- reported; this is the outcome being looked for
        died = f"{type(exc).__name__}: {exc}"[:400]

    report = {
        "scenario": scenario,
        "overlay": os.path.relpath(args.overlay, REPO),
        "imtypeset at start": "in use" if marks_time or totals["document frames"] else "not in use",
        "imtypeset at end": "in use" if typeset.module() is not None else "off",
        "reason()": typeset.reason(),
        "trouble()": typeset.trouble() if hasattr(typeset, "trouble") else "(this overlay has no trouble())",
        "fault fired": fault["fired"],
        "application died": died,
        **totals,
        "events": events[:12],
        "events in all": len(events),
    }
    verdict: dict = {}
    st = rich.typeset
    if scenario == "find" and st is not None:
        found = marks_seen.get("last", [])
        wrong = []
        for mark in found:
            (block, start), (_, end) = mark.range
            covered = st.texts[block][start:end]
            if covered.lower() != NEEDLE.lower():
                wrong.append((block, start, end, covered))
        report["find marks"] = len(found)
        report["find marks not on the needle"] = len(wrong)
        report["first wrong"] = wrong[:3]
        report["cap"] = rich_pane._FIND_MARK_CAP
        verdict["every mark covers the needle"] = not wrong and bool(found)
        verdict["the marks are capped"] = len(found) <= rich_pane._FIND_MARK_CAP
    if scenario == "stream-find" and st is not None:
        per_frame: dict = {}
        for f, seconds in marks_time:
            per_frame[f] = per_frame.get(f, 0.0) + seconds
        streaming = sorted(seconds for f, seconds in per_frame.items() if f > 10)
        report["blocks"] = len(st.texts)
        report["characters"] = sum(len(text) for text in st.texts)
        report["_marks median_us while streaming"] = round(statistics.median(streaming) * 1e6, 1)
        report["_marks p95_us while streaming"] = round(streaming[int(0.95 * (len(streaming) - 1))] * 1e6, 1)
        incremental = marks_seen.get("last", [])
        # The same search from nothing: by the overlay's own function on a state that
        # remembers no search, then by this file's own.
        for name in ("found", "found_upto", "found_needle", "folded", "folded_from", "marks_key"):
            if hasattr(st, name):
                setattr(st, name, type(getattr(st, name))() if name != "marks_key" else None)
        rich_pane._marks(typeset.module(), st, rich.find.lower())
        fresh = marks_seen.get("last", [])
        plain = []
        for index, text in enumerate(st.texts):
            at = text.lower().find(needle)
            while at >= 0:
                plain.append(((index, at), (index, at + len(needle))))
                at = text.lower().find(needle, at + len(needle))
        report["find marks"] = len(incremental)
        verdict["the marks kept while streaming are those of a search from nothing"] = (
            [m.range for m in incremental] == [m.range for m in fresh] == plain[: len(fresh)]
            and (len(fresh) == len(plain) or len(fresh) == rich_pane._FIND_MARK_CAP)
        )
    if scenario == "stream-table":
        heights = [extent for _, extent, _ in table_log]
        # The Host block shorter than the table ImGui drew into it: the table is cut off.
        short = sum(1 for _, extent, measured in table_log if extent < measured - 0.5)
        report["table frames"] = len(table_log)
        report["frames the table was cut off by its Host block"] = short
        report["host height changes"] = sum(1 for a, b in zip(heights, heights[1:]) if a != b)
        report["host height fell"] = sum(1 for a, b in zip(heights, heights[1:]) if b < a - 0.5)
        report["heights, each change"] = [heights[0]] + [h for h, p in zip(heights[1:], heights) if h != p][:40]
        report["height at the end, and as ImGui last drew it"] = list(table_log[-1][1:]) if table_log else None
        verdict["a streaming table's height never falls back to the guess"] = (
            bool(table_log) and report["host height fell"] == 0
        )
        verdict["it is cut off in fewer than a quarter of the frames"] = short * 4 < len(table_log)
    if scenario == "soft-standing":
        report["blocks described with runs out of order"] = soft["blocks cut"]
        report["frames Document.draw answered False"] = soft["draw answered False"]
        report["last_error codes after those"] = sorted(soft["codes"])
        report["most failing frames counted in a row"] = soft["failures seen"]
        ts = typeset.module()
        verdict["the application lived"] = died is None
        verdict["the soft outcome was reached in every document frame"] = (
            soft["blocks cut"] == 1
            and soft["draw answered False"] == totals["document frames"] >= args.frames - 1
        )
        verdict["it was DescriptionCut and nothing else"] = (
            ts is not None and sorted(soft["codes"]) == [int(ts.ErrorCode.DescriptionCut)]
        )
        verdict["no frame counted as failing"] = soft["failures seen"] == 0
        verdict["imtypeset is still in use, and no frame went to the classic renderer"] = (
            ts is not None and totals["classic frames"] == 0 and not typeset.reason()
        )
        verdict["no window was left open"] = totals["left a window open"] == 0
        verdict["draw never raised into its caller"] = totals["draw raised"] == 0
    if scenario in ("none", "table-raises", "table-raises-once", "table-cell-raises",
                    "menu-copy-raises", "key-copy-raises", "describe-raises-once",
                    "draw-raises-once"):
        verdict["the application lived"] = died is None
        verdict["no window was left open"] = totals["left a window open"] == 0
        verdict["no frame was drawn twice"] = totals["drawn twice"] == 0
        verdict["draw never raised into its caller"] = totals["draw raised"] == 0
        if scenario != "none":
            verdict["the fault was reached"] = fault["fired"] > 0
        if scenario in ("menu-copy-raises", "key-copy-raises"):
            report["selection at frame 19"] = pane["selection"]
            report["menu open at frame 29"] = pane.get("menu open")
        if scenario == "none" or fault["once"] or scenario.endswith("copy-raises"):
            verdict["imtypeset is still in use (one failure is not a reason to stop)"] = (
                typeset.module() is not None
            )
        else:
            verdict["switched off, and says why"] = typeset.module() is None and "Injected" in typeset.reason()
        if typeset.module() is not None and scenario != "none":
            verdict["the failure is on record"] = "Injected" in str(report["trouble()"])
    report["verdict"] = verdict
    print(json.dumps(report, indent=1, ensure_ascii=False))
    if died is not None:
        return 2
    return 0 if all(verdict.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
