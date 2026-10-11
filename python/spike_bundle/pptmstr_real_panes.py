"""SPIKE: pptmstr's REAL transcript pane, today's renderer and the imtypeset one,
on real transcripts, measured.

    IMTYPESET_LIBRARY=$PWD/build/advtext-rework-plan-pptmstr/libimtypeset_bundle.so \
    sh tools/shot.sh --screen 1600x1000x24 -- ../pptmstr/.venv/bin/python -P -B \
        python/spike_bundle/pptmstr_real_panes.py --pane document --scenario steady

What runs is `pptmstr.ui.transcript_pane.draw(snap, state, node)`, pptmstr's own
function (read-only from ../pptmstr), in a hello_imgui application with
pptmstr's fonts and style, on a transcript from pptmstr_corpus.py. ONE pane per
process, so that the synthetic mouse is over it:

--pane today      pptmstr as it is on disk: rich_pane.draw windows the history
                  to 400 lines and draws every run with ImDrawList.add_text.
--pane document   the same call with python/spike_bundle/pptmstr_overlay's
                  rich_pane.py (and typeset.py, theme.py): ONE imtypeset.Document.
--mode wrap       the pane's DEFAULT mode instead of RICH. `today` is
                  text_wrapped per line over the last 400 lines; `document` is a
                  PROTOTYPE that exists only in this file (lines_document below):
                  one Code block per run of same-kind lines, wrapped.

--scenario steady   the whole transcript from the first frame, following the tail.
           scroll   the same, then the mouse wheel: up to the top and down again.
           stream   the last --tail appends arrive --chunk characters a frame.
           resize   the window's width changes every frame.
           find     a needle typed one letter every 20 frames (today: the search
                    box, which demotes RICH to filtered lines; document:
                    RichState.find, which marks it in place).
           select   document only: a mouse drag down the pane, then Ctrl+C.
--profile           every frame is instrumented: time inside the imgui_bundle
                    binding, time inside the imtypeset library, calls of each;
                    what is left of the pane's time is Python.

Prints one JSON object. Times are microseconds of the pane's own draw call
(transcript_pane.draw), medians over the frames after warm-up; Xvfb + llvmpipe,
so nothing here says what the GPU or the swap costs.
"""

import argparse
import importlib.util
import json
import os
import statistics
import sys
import time
import types

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path[:0] = [os.path.join(REPO, "python"), HERE]
if os.environ.get("SPIKE_EXTRA_SITE"):
    sys.path.append(os.environ["SPIKE_EXTRA_SITE"])

import pptmstr_corpus as corpus  # noqa: E402

NEEDLE = "the layout"


class Clock:
    """Time and calls spent inside something, for one frame at a time."""

    def __init__(self):
        self.seconds = 0.0
        self.calls = 0


class Timed:
    """Stands in for the imgui module, a draw list, a font or a ctypes library and
    books every call made through it."""

    def __init__(self, target, clock, wrap=("ImDrawList", "ImFont", "ListClipper")):
        object.__setattr__(self, "_target", target)
        object.__setattr__(self, "_clock", clock)
        object.__setattr__(self, "_wrap", wrap)

    def __bool__(self):
        return self._target is not None

    def __getattr__(self, name):
        attr = getattr(self._target, name)
        if isinstance(attr, type) and name not in ("ImVec2", "ListClipper"):
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
    parser.add_argument("--mode", default="rich", choices=("rich", "wrap"))
    parser.add_argument("--scenario", default="steady",
                        choices=("steady", "scroll", "stream", "resize", "find", "select"))
    parser.add_argument("--source", default="", choices=("", "sessions", "repo"),
                        help="repo (default): the reports in this repository. sessions needs --sessions")
    parser.add_argument("--sessions", default="", metavar="DIR",
                        help="opt in: read the Claude Code session logs in DIR (nothing is read by default)")
    parser.add_argument("--pick", default="median", choices=("median", "p90", "max"))
    parser.add_argument("--repeat", type=int, default=1, help="the transcript this many times over")
    parser.add_argument("--reports", type=int, default=0, help="--source repo: only the first N reports")
    parser.add_argument("--tail", type=int, default=12, help="stream: appends held back")
    parser.add_argument("--chunk", type=int, default=24, help="stream: characters a frame")
    parser.add_argument("--frames", type=int, default=300)
    parser.add_argument("--profile", action="store_true")
    parser.add_argument("--settle", action="store_true",
                        help="document: lay the whole document out once it is set")
    parser.add_argument("--lazy-copy", action="store_true",
                        help="the 'copy' button's text is not built every frame (a proposed step)")
    parser.add_argument("--screenshot", default="")
    args = parser.parse_args()
    args.source = args.source or ("sessions" if args.sessions else "repo")

    # The overlay's sitecustomize.py by path: Debian has a module of that name already.
    spec = importlib.util.spec_from_file_location(
        "pptmstr_overlay_site", os.path.join(HERE, "pptmstr_overlay", "sitecustomize.py"))
    overlay = importlib.util.module_from_spec(spec)
    os.environ["PPTMSTR_OVERLAY"] = "0"  # loaded for install(), which is called below or not
    spec.loader.exec_module(overlay)

    document = args.pane == "document"
    if document:
        overlay.install()  # pptmstr.theme, pptmstr.ui.rich_pane, pptmstr.ui.typeset from the overlay
    else:
        os.environ.pop("IMTYPESET_LIBRARY", None)  # today's pptmstr loads nothing

    from imgui_bundle import hello_imgui, imgui, immapp

    from pptmstr import theme
    from pptmstr.transcript import SegmentKind, Transcript
    from pptmstr.ui import blocks as blocks_module
    from pptmstr.ui import rich_pane, transcript_pane, widgets

    typeset = sys.modules.get("pptmstr.ui.typeset")

    if args.source == "sessions":
        path, ops = corpus.pick_session(args.pick, args.sessions)
        label = f"session {args.pick}"
    else:
        ops = corpus.ops_from_reports(limit_files=args.reports)
        label = "the .concerns reports" + (f" (first {args.reports})" if args.reports else "")
    ops = ops * args.repeat
    held = []
    if args.scenario == "stream":
        ops, held = ops[: -args.tail], ops[-args.tail :]
    transcript = Transcript()
    for kind, text in ops:
        transcript.append(kind, text)
    pending = [[kind, text] for kind, text in held]

    node = ("spike", None)
    record = types.SimpleNamespace(transcript=transcript, node_id=node)
    snap = types.SimpleNamespace(get=lambda selected: record)
    state = transcript_pane.TranscriptState()
    state.mode = transcript_pane.RenderMode.RICH if args.mode == "rich" else transcript_pane.RenderMode.WRAP

    # --- the WRAP-mode prototype: a Document of one Code block per run ---------------------

    lines_doc: dict = {}

    def lines_document(state, cache, lines) -> None:
        """What transcript_pane._draw_lines would be on a Document: every run of
        same-kind lines is one verbatim block that wraps; nothing is windowed."""
        ts = typeset.module()
        if not imgui.begin_child("##transcript"):
            imgui.end_child()
            return
        if "doc" not in lines_doc:
            P = theme.P
            kinds = list(SegmentKind)
            lines_doc.update(
                doc=ts.Document(), runs=0, last=None, kinds=kinds,
                styles=[ts.BlockStyle(Align=ts.Align.Left, Color=getattr(P, transcript_pane._KIND_COLOUR[k]).u32)
                        for k in kinds],
                style=ts.Style(Align=ts.Align.Left, LineHeight=1.05, Color=P.text.u32),
            )
        d = lines_doc
        doc = d["doc"]
        starts = cache.run_starts
        n = len(starts)
        last_key = (n, len(cache.lines), len(cache.lines[-1].text)) if cache.lines else None
        if last_key != d["last"]:
            def block(run):
                a, b = cache.run_bounds(run)
                return ts.Block(ts.BlockKind.Code, "\n".join(line.text for line in cache.lines[a:b]),
                                style=d["kinds"].index(cache.lines[a].kind))
            if d["runs"] == 0:
                doc.set_blocks([block(run) for run in range(n)], d["styles"])
                doc.set_options(ts.DocumentOptions(CodeOverflow=ts.CodeOverflow.Wrap))
            else:
                first = d["runs"] - 1
                made = [block(run) for run in range(first, n)]
                old = d["tail_text"]
                if len(made) == 1 and made[0].text.startswith(old) and len(made[0].text) > len(old):
                    doc.append_text(made[0].text[len(old):])
                else:
                    doc.replace_blocks(first, 1, made)
            a, b = cache.run_bounds(n - 1)
            d["tail_text"] = "\n".join(line.text for line in cache.lines[a:b])
            d["runs"], d["last"] = n, last_key
        width = max(1.0, imgui.get_content_region_avail().x)
        doc.layout(width, d["style"])
        origin = imgui.get_cursor_screen_pos()
        doc.draw(typeset.window_draw_list(), (origin.x, origin.y))
        imgui.invisible_button("##lines", imgui.ImVec2(width, max(doc.size()[1], 1.0)))
        transcript_pane._handle_scroll(state)
        imgui.end_child()

    if document and args.mode == "wrap":
        transcript_pane._draw_lines = lines_document

    if args.lazy_copy:
        # What "build the copy text when the button is clicked" saves: the three
        # functions transcript_pane.draw calls for it every frame answer at once.
        # Today's line path still needs _visible's list to draw from; the document
        # paths do not, with no filter set.
        transcript_pane.copy_text = lambda lines: ""
        rich_pane.block_copy_text = lambda blocks: ""
        if document:
            rich_pane.windowed = lambda blocks: []
            real_visible = transcript_pane._visible
            transcript_pane._visible = (
                lambda state, cache: cache.lines if state.show_reasoning and not state.search
                else real_visible(state, cache))

    # --- instrumentation --------------------------------------------------------------------

    phases = {name: [] for name in ("sync_lines", "parse_blocks", "copy_value", "describe_and_set")}
    frame_phase: dict = {}

    def phase(owner, name, key):
        real = getattr(owner, name)

        def timed(*a, **k):
            started = time.perf_counter()
            try:
                return real(*a, **k)
            finally:
                frame_phase[key] = frame_phase.get(key, 0.0) + time.perf_counter() - started

        setattr(owner, name, timed)

    phase(transcript_pane.NodeTranscript, "sync", "sync_lines")
    phase(blocks_module.BlockCursor, "feed", "parse_blocks")
    phase(blocks_module.BlockCursor, "live_block", "parse_blocks")
    # transcript_pane.draw builds the "copy" button's text every frame, clicked or not.
    phase(rich_pane, "windowed", "copy_value")
    phase(rich_pane, "block_copy_text", "copy_value")
    phase(transcript_pane, "copy_text", "copy_value")
    phase(transcript_pane, "_visible", "copy_value")
    if document and args.mode == "rich":
        phase(rich_pane, "_sync", "describe_and_set")
    scroll_log: list = []
    real_follow = widgets.follow_tail

    def logging_follow(following):
        scroll_log.append((imgui.get_scroll_y(), imgui.get_scroll_max_y()))
        if os.environ.get("SPIKE_SCROLL_LOG") and len(scroll_log) > 1:
            before, now = scroll_log[-2][1], scroll_log[-1][1]
            if abs(now - before) > 300.0:
                st = state.rich.typeset
                print("frame", frame[0], "scroll_max", before, "->", now, "y", scroll_log[-1][0],
                      "doc height", st and st.doc.size()[1], "laid out", st and st.doc.stats().BlocksLaidOut)
        return real_follow(following)

    widgets.follow_tail = logging_follow

    binding, library = Clock(), Clock()
    profile_rows: list = []
    if args.profile:
        proxy = Timed(imgui, binding)
        transcript_pane.imgui = rich_pane.imgui = widgets.imgui = proxy
        real_face = rich_pane.face
        rich_pane.face = lambda which: Timed(real_face(which), binding)

    times: list = []
    rebuilt: dict = {}
    marks: dict = {}
    frame = [0]
    width = [760.0]
    extra: dict = {}

    def gui() -> None:
        frame[0] += 1
        n = frame[0]
        ts = typeset.module() if typeset is not None else None
        if n == 1 and args.profile and ts is not None:
            ts._lib = Timed(ts._lib, library, wrap=())
            real_address = typeset._window_draw_list

            def counted_address():
                started = time.perf_counter()
                value = real_address()
                binding.seconds += time.perf_counter() - started
                binding.calls += 1
                return value

            typeset._window_draw_list = counted_address

        # -- what the scenario does to the application before this frame's draw --
        if args.scenario == "stream" and pending and n > 20:
            kind, text = pending[0]
            whole = kind not in (SegmentKind.OUTPUT, SegmentKind.REASONING)
            piece = text if whole else text[: args.chunk]
            transcript.append(kind, piece)
            pending[0][1] = text[len(piece):]
            if not pending[0][1]:
                pending.pop(0)
                marks.setdefault("stream_ops_done", []).append(n)
            if not pending:
                marks["stream_end"] = n
        if args.scenario == "resize" and n > 30:
            width[0] = 760.0 - 4.0 * ((n - 30) % 60 if (n - 30) % 120 < 60 else 60 - (n - 30) % 60)
        if args.scenario == "find" and n >= 60 and (n - 60) % 20 == 0:
            typed = NEEDLE[: 1 + (n - 60) // 20]
            if 1 + (n - 60) // 20 <= len(NEEDLE):
                marks.setdefault("keystrokes", []).append(n)
                if document:
                    state.rich.find = typed
                else:
                    state.search = typed
        if args.scenario == "scroll" and n == 40:
            state.follow_tail = False

        imgui.set_next_window_pos(imgui.ImVec2(20.0, 20.0))
        imgui.set_next_window_size(imgui.ImVec2(width[0], 860.0))
        imgui.begin("CONTEXT", None, imgui.WindowFlags_.no_collapse)
        frame_phase.clear()
        binding.seconds = library.seconds = 0.0
        binding.calls = library.calls = 0
        started = time.perf_counter()
        transcript_pane.draw(snap, state, node)
        spent = time.perf_counter() - started
        times.append(spent)
        for name in phases:
            phases[name].append(frame_phase.get(name, 0.0))
        if args.profile:
            profile_rows.append((spent, binding.seconds, binding.calls, library.seconds, library.calls))
        imgui.end()

        if n == 5:
            imgui.push_font(theme.face(theme.Face.BODY), imgui.get_font_size())
            extra["imgui_advance_px"] = round(imgui.calc_text_size("M" * 100).x / 100.0, 4)
            extra["imgui_font_size"] = imgui.get_font_size()
            imgui.pop_font()
        if document and args.mode == "rich" and state.rich.typeset is not None:
            st = state.rich.typeset
            if n == 5:
                # The description as it was handed over, for the estimate check at exit.
                keep: list = []
                probe = rich_pane._Typeset(ts, st.size)
                for index, block in enumerate(state.caches[node].block_cursor.blocks):
                    rich_pane._describe(ts, probe, block, index, keep)
                rebuilt["blocks"] = [tuple(b) for b in keep]
                rebuilt["width"] = max(1.0, width[0] - 2 * imgui.get_style().window_padding.x - 14.0)
            if args.settle and n == 2:
                started = time.perf_counter()
                st.doc.layout_window(0.0, 1e9)
                extra["settle_all_us"] = round((time.perf_counter() - started) * 1e6, 1)
            if n == 30:
                extra["height_before_settling_px"] = round(st.doc.size()[1], 1)
        if args.frames and n >= args.frames:
            hello_imgui.get_runner_params().app_shall_exit = True

    def pre_new_frame() -> None:
        n = frame[0] + 1
        io = imgui.get_io()
        if args.scenario in ("scroll", "select", "steady", "find", "stream", "resize"):
            io.add_mouse_pos_event(400.0, 480.0)
        if args.scenario == "scroll" and n > 40:
            up = (n - 40) < (args.frames - 40) * 0.6
            io.add_mouse_wheel_event(0.0, 3.0 if up else -5.0)
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
                io.add_key_event(imgui.Key.mod_ctrl, True)
                io.add_key_event(imgui.Key.c, True)
            elif n == 171:
                io.add_key_event(imgui.Key.c, False)
                io.add_key_event(imgui.Key.mod_ctrl, False)
            elif n > 60:
                io.add_mouse_pos_event(560.0, 750.0)

    def before_exit() -> None:
        ts = typeset.module() if typeset is not None else None
        extra["imtypeset"] = ("in use" if ts is not None else
                              ("not loaded" if typeset is None else f"not in use: {typeset.reason()}"))
        cache = state.caches.get(node)
        if cache is not None:
            extra["lines"] = len(cache.lines)
            extra["blocks"] = len(cache.block_cursor.blocks)
        st = state.rich.typeset if document else None
        if st is not None and ts is not None:
            doc = st.doc
            extra["document_blocks"] = len(doc)
            extra["tables"] = len(st.tables)
            extra["document_stats"] = doc.stats()._asdict()
            extra["height_at_exit_px"] = round(doc.size()[1], 1)
            if args.scenario == "select":
                extra["selection"] = st.selection and [tuple(st.selection.start), tuple(st.selection.end)]
                extra["clipboard_chars"] = len(imgui.get_clipboard_text() or "")
            # Copy of everything, and the true height, measured once at the end.
            started = time.perf_counter()
            whole = doc.range_text(((0, 0), (len(doc) - 1, 1 << 30)))
            extra["copy_everything_us"] = round((time.perf_counter() - started) * 1e6, 1)
            extra["copy_everything_chars"] = len(whole)
            started = time.perf_counter()
            doc.layout_window(0.0, 1e9)
            extra["settle_rest_us"] = round((time.perf_counter() - started) * 1e6, 1)
            extra["height_settled_px"] = round(doc.size()[1], 1)
            # How wrong the height of a block is before it is laid out: a second
            # document of the same blocks, each asked for in turn.
            fresh = ts.Document.from_blocks(
                [ts.Block(*b) for b in rebuilt["blocks"]], st.styles) if rebuilt.get("blocks") else None
            if fresh is not None:
                fresh.set_options(st.options)
                fresh.layout(rebuilt["width"], st.style)
                before = fresh.size()[1]
                errors = []
                for index in range(len(fresh)):
                    fresh.block_geometry(index)
                    after = fresh.size()[1]
                    errors.append((after - before, index))
                    before = after
                by_kind: dict = {}
                for error, index in errors:
                    name = ts.BlockKind(rebuilt["blocks"][index][0]).name
                    row = by_kind.setdefault(name, [0, 0, 0.0, 0.0])
                    row[0] += 1
                    row[1] += 1 if abs(error) > 0.5 else 0
                    row[2] += abs(error)
                    row[3] = max(row[3], abs(error))
                extra["estimate_error_by_kind"] = {
                    name: {"blocks": row[0], "wrong_by_over_half_a_px": row[1],
                           "sum_abs_px": round(row[2], 1), "worst_px": round(row[3], 1)}
                    for name, row in by_kind.items()}
                extra["estimate_net_error_px"] = round(sum(e for e, _ in errors), 1)
                line = ts.Document.from_blocks([ts.Block(ts.BlockKind.Code, "M" * 100)], st.styles)
                line.layout(4000.0, st.style)
                box = line.block_line(0, 0)
                extra["document_advance_px"] = round((box.max[0] - box.min[0]) / 100.0, 4)
            extra["document_stats_settled"] = doc.stats()._asdict()
            wrong = [i for i in range(len(doc)) if doc.block_error(i).code]
            extra["blocks_with_an_error"] = len(wrong)
            # An empty call into the library, for the cost of the boundary alone.
            handle = doc._h()
            raw = getattr(ts._lib, "_target", ts._lib)
            started = time.perf_counter()
            for _ in range(20000):
                raw.ImTypeset_GetDocumentBlockCount(handle)
            extra["empty_library_call_ns"] = round((time.perf_counter() - started) / 20000 * 1e9)
        started = time.perf_counter()
        for _ in range(20000):
            imgui.get_font_size()
        extra["empty_binding_call_ns"] = round((time.perf_counter() - started) / 20000 * 1e9)
        if args.profile:
            clock = Clock()
            proxy = Timed(imgui, clock)
            started = time.perf_counter()
            for _ in range(20000):
                proxy.get_font_size()
            extra["proxied_binding_call_ns"] = round((time.perf_counter() - started) / 20000 * 1e9)
        if cache is not None and args.pane == "today":
            started = time.perf_counter()
            text = rich_pane.block_copy_text(rich_pane.windowed(cache.block_cursor.blocks))
            extra["copy_visible_us"] = round((time.perf_counter() - started) * 1e6, 1)
            extra["copy_visible_chars"] = len(text)
        if ts is not None:
            ts.destroy_context()

    runner = hello_imgui.RunnerParams()
    runner.app_window_params.window_title = "SPIKE: pptmstr's transcript pane"
    runner.app_window_params.window_geometry.size = (800, 900)
    runner.imgui_window_params.default_imgui_window_type = (
        hello_imgui.DefaultImGuiWindowType.no_default_window
    )
    runner.ini_disable = True
    runner.fps_idling.enable_idling = False
    runner.callbacks.load_additional_fonts = theme.load_fonts
    runner.callbacks.setup_imgui_style = theme.apply_style
    runner.callbacks.show_gui = gui
    runner.callbacks.pre_new_frame = pre_new_frame
    runner.callbacks.before_exit = before_exit
    immapp.run(runner)

    if args.screenshot:
        from pptmstr_app_shot import write_png

        write_png(args.screenshot, hello_imgui.final_app_window_screenshot())

    warm = 30
    report = {
        "pane": args.pane, "mode": args.mode, "scenario": args.scenario, "transcript": label,
        "repeat": args.repeat, "frames": frame[0], **extra,
        "first_frame_us": round(times[0] * 1e6, 1),
        "first_frame_phases_us": {name: round(values[0] * 1e6, 1) for name, values in phases.items()
                                  if values and values[0]},
        "second_frame_us": round(times[1] * 1e6, 1),
        "steady_median_us": med(times[warm:]),
        "steady_p95_us": p95(times[warm:]),
        "steady_max_us": round(max(times[warm:]) * 1e6, 1),
        "steady_phases_median_us": {name: med(values[warm:]) for name, values in phases.items()
                                    if any(values[warm:])},
    }
    if args.scenario == "stream" and "stream_end" in marks:
        during = times[21 : marks["stream_end"]]
        report["stream_frames"] = len(during)
        report["stream_median_us"] = med(during)
        report["stream_p95_us"] = p95(during)
        report["stream_max_us"] = round(max(during) * 1e6, 1) if during else None
        report["stream_phases_median_us"] = {name: med(values[21 : marks["stream_end"]])
                                             for name, values in phases.items()}
    if args.scenario == "find":
        strokes = marks.get("keystrokes", [])
        report["keystroke_frame_us"] = [round(times[n - 1] * 1e6, 1) for n in strokes]
        report["with_needle_median_us"] = med(times[strokes[-1] :]) if strokes else None
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
        report["sum_of_content_height_changes_px"] = round(sum(jumps), 1)
    if args.profile:
        rows = profile_rows[warm:]
        overhead = extra.get("proxied_binding_call_ns", 0) - extra.get("empty_binding_call_ns", 0)
        per = lambda i: statistics.median(r[i] for r in rows)  # noqa: E731
        total, in_binding, binding_calls, in_library, library_calls = (per(i) for i in range(5))
        proxy_cost = (binding_calls + library_calls) * max(overhead, 0) * 1e-9
        report["profile_median"] = {
            "pane_us_instrumented": round(total * 1e6, 1),
            "binding_calls": binding_calls,
            "binding_us": round(in_binding * 1e6, 1),
            "library_calls": library_calls,
            "library_us": round(in_library * 1e6, 1),
            "instrument_overhead_us_estimate": round(proxy_cost * 1e6, 1),
            "python_us": round((total - in_binding - in_library - proxy_cost) * 1e6, 1),
        }
    print(json.dumps(report))


if __name__ == "__main__":
    main()
