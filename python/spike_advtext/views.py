"""SPIKE: advtext's own document in advtext's own views, and the same document
through ONE imtypeset.Document, in advtext's wheel, measured.

    IMTYPESET_LIBRARY=$PWD/build/advtext-rework-plan-advtext/libimtypeset_bundle.so \
    sh tools/shot.sh --screen 1600x1000x24 -- ../imgui-adv-text/.venv/bin/python -P -B \
        python/spike_advtext/views.py --pane document --repeat 1 --scenario steady

The document is `advtext.markdown.build_document` on ../imgui-adv-text/demo/sample.md
(`--file`), `--repeat` times over, with the reference's own formula builder.
advtext is imported READ-ONLY. One pane per process:

--pane today      advtext.ui.bare_view.BareView, as the demo's Find page uses it
                  (advtext.ui.pages.find.Finder for the find scenario), and for
                  `stream` advtext.StreamDocument + advtext.StreamView.
--pane document   convert.py turns the SAME parsed blocks into descriptions, one
                  imtypeset.Document holds them, a frame is layout + draw. For
                  `stream` the parsing is still advtext's StreamDocument; what
                  changed between two revisions is replaced, or appended to.
                  Tables are Host blocks nothing draws here.

Scenarios as in python/spike_bundle/pptmstr_real_panes.py: steady, scroll,
stream, resize, find, select; --profile books the time inside the imgui_bundle
binding and inside the imtypeset library, and counts the calls.

Prints one JSON object; times are microseconds of the pane's own draw.
"""

import argparse
import json
import os
import statistics
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
ADVTEXT = os.environ.get("ADVTEXT_ROOT", os.path.join(os.path.dirname(REPO), "imgui-adv-text"))
sys.path[:0] = [os.path.join(REPO, "python"), HERE]
sys.path.append(ADVTEXT)

import convert  # noqa: E402

NEEDLE = "the line"


class Clock:
    def __init__(self):
        self.seconds = 0.0
        self.calls = 0


class Timed:
    """Stands in for the imgui module, a draw list or a ctypes library and books
    every call made through it."""

    def __init__(self, target, clock, wrap=("ImDrawList",)):
        object.__setattr__(self, "_target", target)
        object.__setattr__(self, "_clock", clock)
        object.__setattr__(self, "_wrap", wrap)

    def __bool__(self):
        return self._target is not None

    def __getattr__(self, name):
        attr = getattr(self._target, name)
        if isinstance(attr, type) and name != "ImVec2":
            return attr
        if not callable(attr):
            return attr
        clock, wrap = self._clock, self._wrap

        def call(*args, **kwargs):
            args = [a._target if isinstance(a, Timed) else a for a in args]
            started = time.perf_counter()
            result = attr(*args, **kwargs)
            clock.seconds += time.perf_counter() - started
            clock.calls += 1
            if type(result).__name__ in wrap:
                return Timed(result, clock, wrap)
            return result

        return call


def med(values):
    return round(statistics.median(values) * 1e6, 1) if values else None


def p95(values):
    values = sorted(values)
    return round(values[int(0.95 * (len(values) - 1))] * 1e6, 1) if values else None


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--pane", default="document", choices=("today", "document"))
    parser.add_argument("--scenario", default="steady",
                        choices=("steady", "scroll", "stream", "resize", "find", "select"))
    parser.add_argument("--file", default="sample.md")
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--chunk", type=int, default=24, help="stream: characters a frame")
    parser.add_argument("--frames", type=int, default=300)
    parser.add_argument("--profile", action="store_true")
    parser.add_argument("--screenshot", default="")
    args = parser.parse_args()
    document_pane = args.pane == "document"

    import imgui_bundle
    from imgui_bundle import hello_imgui, imgui, immapp

    from advtext import (
        DARK_PALETTE, StreamDocument, StreamView, Theme, build_document, char_measure,
        fontlib_metrics, load_faces, px_from_sp,
    )
    from advtext.fontlib import font_file
    from advtext.fontmetrics import Face
    from advtext.mathtext import markdown_math
    from advtext.style import BlockKind
    from advtext.ui.bare_view import BareView

    ts = None
    if document_pane:
        import imtypeset as ts

    with open(os.path.join(ADVTEXT, "demo", args.file), encoding="utf-8") as handle:
        one = handle.read()
    source = "\n\n".join([one] * args.repeat)
    theme = Theme.build()
    body_sp = theme.style(BlockKind.PARAGRAPH).font.size
    body_px = px_from_sp(body_sp, theme.px_per_inch)
    measure = char_measure(fontlib_metrics)
    math = markdown_math()
    extra: dict = {"bytes": len(source.encode("utf-8"))}
    loaded: list = []
    binding, library = Clock(), Clock()
    real_imgui = imgui

    streaming = args.scenario == "stream"
    held = ""
    if streaming:
        # The last copy of the file arrives a few characters a frame.
        cut = len(source) - len(one)
        source, held = source[:cut], one
    pending = [held]

    started = time.perf_counter()
    parsed = None if streaming else build_document(source, theme, fontlib_metrics, math=math)
    extra["parse_us"] = round((time.perf_counter() - started) * 1e6, 1)
    if not streaming:
        # Again, with the faces' metrics now read and the formula builder's memory warm:
        # what a second document, or an edit of this one, costs.
        started = time.perf_counter()
        parsed = build_document(source, theme, fontlib_metrics, math=math)
        extra["parse_again_us"] = round((time.perf_counter() - started) * 1e6, 1)
    stream = None
    if streaming:
        stream = StreamDocument(theme, fontlib_metrics, math=math)
        started = time.perf_counter()
        stream.append(source)
        _ = stream.blocks
        extra["parse_us"] = round((time.perf_counter() - started) * 1e6, 1)
    if parsed is not None:
        extra["blocks"] = len(parsed.blocks)

    # --- TODAY ---------------------------------------------------------------------------

    today: dict = {}
    phase: dict = {}

    def draw_today(width_px) -> None:
        if "view" not in today:
            if streaming:
                today["view"] = StreamView(stream, measure)
            else:
                today["view"] = BareView(document=parsed, theme=theme, measure=measure,
                                         px_per_inch=theme.px_per_inch)
        view = today["view"]
        view.draw(fonts=loaded[-1], palette=DARK_PALETTE, width_px=width_px)

    # --- DOCUMENT --------------------------------------------------------------------------

    state: dict = {"doc": None, "seen": (), "selection": None, "anchor": None, "marks": None,
                   "find": "", "revision": 0, "marks_key": None, "texts": [], "links": [], "tail": None}
    converter = convert.Converter(theme, px_from_sp, body_sp)

    def to_blocks(described):
        return [ts.Block(b[0], b[1], sorted(b[2]), *b[3:]) for b in described.blocks]

    def sync_document() -> None:
        blocks = stream.blocks if streaming else parsed.blocks
        seen = state["seen"]
        if state["doc"] is not None and blocks is seen:
            return
        began = time.perf_counter()
        if state["doc"] is None:
            described = converter.describe(blocks)
            made = to_blocks(described)
            styles = [ts.BlockStyle(**fields) for fields in described.styles]
            state["doc"] = ts.Document.from_blocks(made, styles)
            state["styles"] = len(styles)
            state["texts"] = list(described.texts)
            state["links"] = list(described.links)
            extra["tables_not_drawn"] = len(described.tables)
        else:
            doc = state["doc"]
            first = 0
            limit = min(len(seen), len(blocks))
            while first < limit and seen[first] is blocks[first]:
                first += 1
            described = converter.describe(blocks[first:], base=first)
            made = to_blocks(described)
            fresh = [ts.BlockStyle(**fields) for fields in described.styles[state["styles"]:]]
            old_tail = state["tail"]
            if (first == len(seen) - 1 == len(blocks) - 1 and old_tail is not None and not fresh
                    and made[0]._replace(text="") == old_tail._replace(text="")
                    and made[0].text.startswith(old_tail.text) and len(made[0].text) > len(old_tail.text)):
                doc.append_text(made[0].text[len(old_tail.text):])
                phase["appends"] = phase.get("appends", 0) + 1
            else:
                doc.replace_blocks(first, len(seen) - first, made, fresh)
                phase["replaces"] = phase.get("replaces", 0) + 1
            state["styles"] = len(described.styles)
            state["texts"][first:] = described.texts
            state["links"] = [row for row in state["links"] if row[0] < first] + list(described.links)
        state["tail"] = made[-1] if made else None
        state["seen"] = blocks
        state["revision"] += 1
        phase["convert"] = phase.get("convert", 0.0) + time.perf_counter() - began

    def marks_for(needle: str):
        key = (state["revision"], needle)
        if state["marks_key"] != key:
            marks = []
            for block, start, end, _target in state["links"]:
                marks.append(ts.Mark(((block, start), (block, end)), DARK_PALETTE.link))
                marks.append(ts.Mark(((block, start), (block, end)), DARK_PALETTE.link, ts.MarkPlace.Under))
            if needle:
                low, size = needle.lower(), len(needle)
                for index, text in enumerate(state["texts"]):
                    folded = text.lower()
                    at = folded.find(low)
                    while at >= 0 and len(marks) < 4000:
                        marks.append(ts.Mark(((index, at), (index, at + size)), 0x5939B3E8, ts.MarkPlace.Behind))
                        at = folded.find(low, at + size)
            state["marks"] = state["doc"].pack_marks(marks) if marks else None
            state["marks_key"] = key
            extra["marks"] = len(marks)
        return state["marks"]

    draw_list_address = [None]

    def draw_document(width_px) -> None:
        im = imgui_bundle.imgui  # the proxy on a profiled run
        sync_document()
        doc = state["doc"]
        doc.layout(width_px, state["style"])
        origin = im.get_cursor_screen_pos()
        x, y = origin.x, origin.y
        doc.draw(draw_list_address[0](), (x, y), selection=state["selection"], marks=marks_for(state["find"]))
        im.invisible_button("##document", im.ImVec2(width_px, max(doc.size()[1], 1.0)))
        if im.is_item_hovered() or im.is_item_active():
            mouse = im.get_mouse_pos()
            local = (mouse.x - x, mouse.y - y)
            if im.is_item_activated():
                state["anchor"], state["selection"] = local, None
            elif im.is_item_active() and state["anchor"] is not None:
                picked = doc.select_range(state["anchor"], local)
                state["selection"] = None if picked.start == picked.end else picked
            else:
                state["hit"] = doc.hit_test(local)  # the link under the mouse is hit.tag
        if (state["selection"] is not None and im.get_io().key_ctrl
                and im.is_key_pressed(im.Key.c, False)):
            im.set_clipboard_text(doc.range_text(state["selection"]))

    # --- the application -------------------------------------------------------------------

    times: list = []
    convert_times: list = []
    profile_rows: list = []
    scroll_log: list = []
    marks_at: dict = {}
    frame = [0]
    width = [760.0]
    finder: dict = {}

    def gui() -> None:
        frame[0] += 1
        n = frame[0]
        if n == 1 and args.profile:
            imgui_bundle.imgui = Timed(real_imgui, binding)
            if ts is not None:
                ts._lib = Timed(ts._lib, library, wrap=())
        if streaming and pending[0] and n > 20:
            piece, pending[0] = pending[0][: args.chunk], pending[0][args.chunk:]
            began = time.perf_counter()
            stream.append(piece)
            _ = stream.blocks  # the parse, which both panes pay
            phase["stream_parse"] = time.perf_counter() - began
            if not pending[0]:
                marks_at["stream_end"] = n
        if args.scenario == "resize" and n > 30:
            width[0] = 760.0 - 4.0 * ((n - 30) % 60 if (n - 30) % 120 < 60 else 60 - (n - 30) % 60)
        if args.scenario == "find" and n >= 60 and (n - 60) % 20 == 0 and 1 + (n - 60) // 20 <= len(NEEDLE):
            typed = NEEDLE[: 1 + (n - 60) // 20]
            marks_at.setdefault("keystrokes", []).append(n)
            if document_pane:
                state["find"] = typed
            else:
                from advtext.ui.pages.find import Finder

                began = time.perf_counter()
                if "finder" not in finder:
                    finder["finder"] = Finder(today["view"])
                    extra["find_index_build_us"] = round((time.perf_counter() - began) * 1e6, 1)
                    began = time.perf_counter()
                finder["finder"].search(typed)
                phase["find"] = time.perf_counter() - began
        if args.scenario == "select" and not document_pane and n == 170:
            view = today["view"]
            began = time.perf_counter()
            view.select_all()
            text = view.selected_text()
            extra["select_all_and_copy_us"] = round((time.perf_counter() - began) * 1e6, 1)
            extra["copied_chars"] = len(text)

        real_imgui.set_next_window_pos(real_imgui.ImVec2(20.0, 20.0))
        real_imgui.set_next_window_size(real_imgui.ImVec2(width[0], 860.0))
        real_imgui.begin("advtext", None, real_imgui.WindowFlags_.no_collapse)
        real_imgui.begin_child("##text", real_imgui.ImVec2(0.0, 0.0),
                               real_imgui.ChildFlags_.borders.value
                               | real_imgui.ChildFlags_.always_use_window_padding.value,
                               real_imgui.WindowFlags_.always_vertical_scrollbar.value)
        width_px = float(max(1, int(real_imgui.get_content_region_avail().x)))
        binding.seconds = library.seconds = 0.0
        binding.calls = library.calls = 0
        find_cost = phase.pop("find", 0.0)
        phase.pop("convert", None)
        began = time.perf_counter()
        (draw_document if document_pane else draw_today)(width_px)
        spent = time.perf_counter() - began + find_cost
        times.append(spent)
        convert_times.append((phase.get("convert", 0.0), phase.pop("stream_parse", 0.0)))
        if args.profile:
            profile_rows.append((spent, binding.seconds, binding.calls, library.seconds, library.calls))
        scroll_log.append((real_imgui.get_scroll_y(), real_imgui.get_scroll_max_y()))
        if streaming and document_pane:
            real_imgui.set_scroll_y(1e9)  # follow the tail, as StreamView does for itself
        real_imgui.end_child()
        real_imgui.end()
        if args.frames and n >= args.frames:
            hello_imgui.get_runner_params().app_shall_exit = True

    def pre_new_frame() -> None:
        n = frame[0] + 1
        io = real_imgui.get_io()
        io.add_mouse_pos_event(400.0, 480.0)
        if args.scenario == "scroll" and n > 40:
            down = (n - 40) < (args.frames - 40) * 0.6
            io.add_mouse_wheel_event(0.0, -3.0 if down else 5.0)
        if args.scenario == "select":
            if n == 60:
                io.add_mouse_pos_event(60.0, 150.0)
                io.add_mouse_button_event(0, True)
            elif 60 < n < 160:
                io.add_mouse_pos_event(60.0 + 5.0 * (n - 60), 150.0 + 6.0 * (n - 60))
            elif n == 160:
                io.add_mouse_pos_event(560.0, 750.0)
                io.add_mouse_button_event(0, False)
            elif n == 170:
                io.add_key_event(real_imgui.Key.mod_ctrl, True)
                io.add_key_event(real_imgui.Key.c, True)
            elif n == 171:
                io.add_key_event(real_imgui.Key.c, False)
                io.add_key_event(real_imgui.Key.mod_ctrl, False)
            elif n > 60:
                io.add_mouse_pos_event(560.0, 750.0)

    def load_fonts() -> None:
        hello_imgui.imgui_default_settings.load_default_font_with_font_awesome_icons()
        loaded.append(load_faces(tuple(Face)))
        if ts is not None:
            import ctypes

            if ts.current_context() is None:
                ts.create_context()
            for face, ours in ((Face.PROSE, ts.Face.Regular), (Face.PROSE_BOLD, ts.Face.Bold),
                               (Face.PROSE_ITALIC, ts.Face.Italic),
                               (Face.PROSE_BOLD_ITALIC, ts.Face.BoldItalic), (Face.CODE, ts.Face.Code),
                               (Face.MATH, ts.Face.Math)):
                ts.add_font(ours, str(font_file(face)))
            state["style"] = ts.Style(EmSize=body_px, PixelsPerInch=theme.px_per_inch,
                                      Color=DARK_PALETTE.ink)
            wheel = ctypes.CDLL(imgui_bundle._imgui_bundle.__file__)
            address = wheel._ZN5ImGui17GetWindowDrawListEv
            address.restype = ctypes.c_void_p
            address.argtypes = []

            def counted():
                began = time.perf_counter()
                value = address()
                binding.seconds += time.perf_counter() - began
                binding.calls += 1
                return value

            draw_list_address[0] = counted if args.profile else address

    def before_exit() -> None:
        if streaming:
            extra["blocks"] = len(stream.blocks)
        doc = state["doc"]
        if doc is not None:
            extra["document_blocks"] = len(doc)
            extra["document_stats"] = doc.stats()._asdict()
            extra["edits"] = {k: v for k, v in phase.items() if k in ("appends", "replaces")}
            if args.scenario == "select":
                extra["selection"] = state["selection"] and [tuple(state["selection"].start),
                                                             tuple(state["selection"].end)]
                extra["clipboard_chars"] = len(real_imgui.get_clipboard_text() or "")
            extra["height_before_settling_px"] = round(doc.size()[1], 1)
            began = time.perf_counter()
            doc.layout_window(0.0, 1e9)
            extra["settle_rest_us"] = round((time.perf_counter() - began) * 1e6, 1)
            extra["height_settled_px"] = round(doc.size()[1], 1)
            began = time.perf_counter()
            whole = doc.range_text(((0, 0), (len(doc) - 1, 1 << 30)))
            extra["copy_everything_us"] = round((time.perf_counter() - began) * 1e6, 1)
            extra["copy_everything_chars"] = len(whole)
            extra["blocks_with_an_error"] = sum(1 for i in range(len(doc)) if doc.block_error(i).code)
            handle = doc._h()
            raw = getattr(ts._lib, "_target", ts._lib)
            began = time.perf_counter()
            for _ in range(20000):
                raw.ImTypeset_GetDocumentBlockCount(handle)
            extra["empty_library_call_ns"] = round((time.perf_counter() - began) / 20000 * 1e9)
        elif "view" in today:
            layout = today["view"].layout
            stats = today["view"].layout_stats
            extra["today_layout_stats"] = str(stats)[:300] if stats is not None else None
        began = time.perf_counter()
        for _ in range(20000):
            real_imgui.get_font_size()
        extra["empty_binding_call_ns"] = round((time.perf_counter() - began) / 20000 * 1e9)
        if args.profile:
            proxy = Timed(real_imgui, Clock())
            began = time.perf_counter()
            for _ in range(20000):
                proxy.get_font_size()
            extra["proxied_binding_call_ns"] = round((time.perf_counter() - began) / 20000 * 1e9)
        imgui_bundle.imgui = real_imgui
        if ts is not None:
            ts.destroy_context()

    runner = hello_imgui.RunnerParams()
    runner.app_window_params.window_title = "SPIKE: advtext's document"
    runner.app_window_params.window_geometry.size = (800, 900)
    runner.imgui_window_params.default_imgui_window_type = (
        hello_imgui.DefaultImGuiWindowType.no_default_window
    )
    runner.ini_disable = True
    runner.fps_idling.enable_idling = False
    runner.callbacks.load_additional_fonts = load_fonts
    runner.callbacks.show_gui = gui
    runner.callbacks.pre_new_frame = pre_new_frame
    runner.callbacks.before_exit = before_exit
    immapp.run(runner)

    if args.screenshot:
        sys.path.insert(0, os.path.join(REPO, "examples", "python"))
        import bundle_support

        bundle_support.write_png(args.screenshot, hello_imgui.final_app_window_screenshot())

    warm = 30
    report = {
        "pane": args.pane, "scenario": args.scenario, "file": args.file, "repeat": args.repeat,
        "frames": frame[0], **extra,
        "first_frame_us": round(times[0] * 1e6, 1),
        "first_frame_convert_us": round(convert_times[0][0] * 1e6, 1),
        "second_frame_us": round(times[1] * 1e6, 1),
        "steady_median_us": med(times[warm:]),
        "steady_p95_us": p95(times[warm:]),
        "steady_max_us": round(max(times[warm:]) * 1e6, 1),
    }
    if streaming and "stream_end" in marks_at:
        end = marks_at["stream_end"]
        during = times[21:end]
        report["stream_frames"] = len(during)
        report["stream_draw_median_us"] = med(during)
        report["stream_draw_p95_us"] = p95(during)
        report["stream_draw_max_us"] = round(max(during) * 1e6, 1)
        report["stream_parse_median_us"] = med([c[1] for c in convert_times[21:end]])
        report["stream_parse_p95_us"] = p95([c[1] for c in convert_times[21:end]])
        report["stream_convert_median_us"] = med([c[0] for c in convert_times[21:end]])
    if args.scenario == "find":
        strokes = marks_at.get("keystrokes", [])
        report["keystroke_frame_us"] = [round(times[n - 1] * 1e6, 1) for n in strokes]
        report["with_needle_median_us"] = med(times[strokes[-1]:]) if strokes else None
    if args.scenario == "select":
        report["drag_median_us"] = med(times[60:160])
        report["drag_p95_us"] = p95(times[60:160])
        report["copy_frame_us"] = round(times[169] * 1e6, 1)
    if args.scenario == "scroll":
        report["scroll_median_us"] = med(times[40:])
        report["scroll_p95_us"] = p95(times[40:])
        report["scroll_max_us"] = round(max(times[40:]) * 1e6, 1)
        maxes = [m for _, m in scroll_log[40:]]
        ys = [y for y, _ in scroll_log[40:]]
        jumps = [abs(b - a) for a, b in zip(maxes, maxes[1:]) if b != a]
        report["scroll_range_px"] = [round(min(ys), 1), round(max(ys), 1)]
        report["content_height_changes_while_scrolling"] = len(jumps)
        report["largest_content_height_change_px"] = round(max(jumps), 1) if jumps else 0.0
    if args.scenario == "resize":
        report["resize_median_us"] = med(times[31:])
        report["resize_p95_us"] = p95(times[31:])
    if args.profile:
        rows = profile_rows[warm:]
        per = lambda i: statistics.median(r[i] for r in rows)  # noqa: E731
        total, in_binding, binding_calls, in_library, library_calls = (per(i) for i in range(5))
        report["profile_median"] = {
            "pane_us_instrumented": round(total * 1e6, 1),
            "binding_calls": binding_calls, "binding_us": round(in_binding * 1e6, 1),
            "library_calls": library_calls, "library_us": round(in_library * 1e6, 1),
        }
    print(json.dumps(report))


if __name__ == "__main__":
    main()
