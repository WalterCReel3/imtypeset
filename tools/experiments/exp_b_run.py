"""Experiment B, second half: typeset text from Python.

    PYTHONDONTWRITEBYTECODE=1 ../imgui-adv-text/.venv/bin/python -B \
        tools/experiments/exp_b_run.py selfhost|fork|docking|deps

`selfhost` is the control: the experiment library carries this repository's
own ImGui and no imgui_bundle is imported; every ImGui call goes through
ctypes. It shows what calling imtypeset from Python costs and that the driver
below works.

For the other variants ImGui is the one inside the installed imgui_bundle: the
frame is driven by `imgui_bundle.imgui` and only the typesetting crosses into
the experiment library. THEY RUN NOTHING unless ImGui's own
DebugCheckVersionAndDataLayout and the size probes in exp_b_gate.py pass: a
library compiled against structs the running ImGui does not have must not be
called. On this machine `deps` and `docking` stop there (see
docs/bindings-notes.md); `fork` needs ../imgui_bundle's ImGui submodule.

The work is done in a subprocess, so a crash is a result. Prints JSON. The one
file written is build/bundle-research/<variant>/frame.png.
"""

from __future__ import annotations

import ctypes
import json
import os
import statistics
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import exp_b_gate  # noqa: E402

FONTS = os.path.join(ROOT, "tests", "data", "fonts")
# ImTypesetFace_ values, from include/imtypeset.h.
FACES = [
    (0, "Roboto-Regular.ttf"),
    (1, "Roboto-Bold.ttf"),
    (2, "Roboto-RegularItalic.ttf"),
    (3, "Roboto-BoldItalic.ttf"),
    (4, "Inconsolata-Medium.ttf"),
    (6, "latinmodern-math.otf"),
]
TEXT_FLAGS_MATH = 1 << 2
WIDTH, HEIGHT = 640, 360
PARAGRAPH = (
    "A paragraph set from Python, justified to the window by total fit. The roots of a "
    r"quadratic are $x = \frac{-b \pm \sqrt{b^2 - 4ac}}{2a}$, and the formula is a box on the "
    "baseline among the words: the line breaker moves it whole, and it is drawn from the same "
    r"font atlas in the same draw call. A sum, $\sum_{k=0}^{n} k = \frac{n(n+1)}{2}$, behaves "
    "the same way."
).encode("utf-8")
FRAMES = 300


class BundleHost:
    """The ImGui frame, driven through imgui_bundle's own binding."""

    def __init__(self) -> None:
        from imgui_bundle import imgui

        self.imgui = imgui
        imgui.create_context()
        io = imgui.get_io()
        io.set_ini_filename(None)
        io.display_size = imgui.ImVec2(WIDTH, HEIGHT)
        io.delta_time = 1.0 / 60.0

    def version(self) -> str:
        return self.imgui.get_version()

    def new_frame(self) -> None:
        self.imgui.new_frame()

    def begin(self) -> None:
        imgui = self.imgui
        imgui.set_next_window_pos(imgui.ImVec2(0, 0))
        imgui.set_next_window_size(imgui.ImVec2(WIDTH, HEIGHT))
        imgui.begin("imtypeset from Python", None, imgui.WindowFlags_.no_saved_settings.value)

    def end(self) -> None:
        self.imgui.end()

    def render(self) -> int:
        self.imgui.render()
        return self.imgui.get_draw_data().total_vtx_count

    def destroy(self) -> None:
        self.imgui.destroy_context()

    def read_back(self) -> dict:
        """What the bundle's binding reads, for comparison with expb_probe_context."""
        imgui = self.imgui
        io, style, dd = imgui.get_io(), imgui.get_style(), imgui.get_draw_data()
        text = style.color_(imgui.Col_.text.value)
        selected = style.color_(imgui.Col_.text_selected_bg.value)
        return {
            "io": {
                "DisplaySize": [io.display_size.x, io.display_size.y],
                "BackendFlags": int(io.backend_flags),
                "ConfigFlags": int(io.config_flags),
                "MetricsRenderVertices": io.metrics_render_vertices,
                "MetricsRenderIndices": io.metrics_render_indices,
                "FontsCount": len(io.fonts.fonts),
                "FontLoaderName": io.fonts.font_loader_name,
            },
            "style": {
                "Alpha": style.alpha,
                "WindowPadding": [style.window_padding.x, style.window_padding.y],
                "ItemSpacing": [style.item_spacing.x, style.item_spacing.y],
                "Text": [text.x, text.y, text.z, text.w],
                "TextSelectedBg": [selected.x, selected.y, selected.z, selected.w],
            },
            "draw_data": {
                "TotalVtxCount": dd.total_vtx_count,
                "TotalIdxCount": dd.total_idx_count,
                "CmdListsCount": len(dd.cmd_lists),
                "lists": [
                    {"vtx": len(l.vtx_buffer), "idx": len(l.idx_buffer), "cmd": len(l.cmd_buffer)}
                    for l in dd.cmd_lists
                ],
            },
        }


class SelfHost:
    """The ImGui frame, driven through the experiment library's own ImGui."""

    def __init__(self, lib) -> None:
        self.lib = lib
        lib.expb_host_version.restype = ctypes.c_char_p
        lib.expb_host_create.argtypes = [ctypes.c_float, ctypes.c_float]
        lib.expb_host_begin.argtypes = [ctypes.c_char_p] + [ctypes.c_float] * 4
        lib.expb_host_render.restype = ctypes.c_int
        for name in ("expb_host_create", "expb_host_new_frame", "expb_host_begin", "expb_host_end", "expb_host_destroy"):
            getattr(lib, name).restype = None
        lib.expb_host_create(WIDTH, HEIGHT)

    def version(self) -> str:
        return self.lib.expb_host_version().decode()

    def new_frame(self) -> None:
        self.lib.expb_host_new_frame()

    def begin(self) -> None:
        self.lib.expb_host_begin(b"imtypeset from Python", 0.0, 0.0, float(WIDTH), float(HEIGHT))

    def end(self) -> None:
        self.lib.expb_host_end()

    def render(self) -> int:
        return self.lib.expb_host_render()

    def destroy(self) -> None:
        self.lib.expb_host_destroy()

    def read_back(self) -> dict:
        return {}


def compare(ours: dict, theirs: dict, path: str = "") -> list[str]:
    """Every value in `theirs` that `ours` has differently, as text."""
    diffs = []
    for key, expected in theirs.items():
        got = ours.get(key)
        here = path + "." + key if path else key
        if isinstance(expected, dict) and isinstance(got, dict):
            diffs += compare(got, expected, here)
        elif isinstance(expected, list) and isinstance(got, list) and len(expected) == len(got):
            for i, (a, b) in enumerate(zip(got, expected)):
                if isinstance(b, dict):
                    diffs += compare(a, b, "%s[%d]" % (here, i))
                elif isinstance(b, float):
                    if abs(a - b) > 1e-6 * max(1.0, abs(b)):
                        diffs.append("%s[%d]: library %r, binding %r" % (here, i, a, b))
                elif a != b:
                    diffs.append("%s[%d]: library %r, binding %r" % (here, i, a, b))
        elif isinstance(expected, float) and isinstance(got, (int, float)):
            if abs(got - expected) > 1e-6 * max(1.0, abs(expected)):
                diffs.append("%s: library %r, binding %r" % (here, got, expected))
        elif got != expected:
            diffs.append("%s: library %r, binding %r" % (here, got, expected))
    return diffs


def median_us(samples: list[int]) -> float:
    return round(statistics.median(samples) / 1000.0, 2)


def child(variant: str) -> dict:
    out: dict = {"variant": variant}
    selfhost = variant == "selfhost"
    lib = exp_b_gate.load(variant, "plain_before_import" if selfhost else "global_after_import")
    out["gate"] = exp_b_gate.gate(lib)
    if not out["gate"]["layout_ok"]:
        out["stopped"] = "the layout check did not pass; nothing else was called"
        return out

    call = exp_b_gate.text_call
    lib.expb_add_font_file.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
    lib.expb_text.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    lib.expb_text.restype = ctypes.c_int
    lib.expb_set_style.argtypes = [ctypes.c_float, ctypes.c_int]
    lib.expb_raster_frame.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]

    host = SelfHost(lib) if selfhost else BundleHost()
    out["imgui_running"] = host.version()
    out["raster_init"] = call(lib.expb_raster_init)
    out["create_context"] = call(lib.expb_create_context)
    if out["create_context"][0] != 1:
        return out
    fonts = {}
    for face, name in FACES:
        fonts[name] = call(lib.expb_add_font_file, face, os.path.join(FONTS, name).encode())
    out["add_fonts"] = fonts
    out["enable_math"] = lib.expb_enable_math(1)
    lib.expb_set_style(16.0, 0)

    stats = (ctypes.c_int * 7)()
    message = ctypes.create_string_buffer(4096)

    def frame(with_text: bool, text: bytes = PARAGRAPH) -> tuple[int, int, int]:
        """(whole frame ns, the text call ns, vertices)"""
        t0 = time.perf_counter_ns()
        host.new_frame()
        host.begin()
        spent = 0
        if with_text:
            t1 = time.perf_counter_ns()
            code = lib.expb_text(1, text, TEXT_FLAGS_MATH, message, len(message))
            spent = time.perf_counter_ns() - t1
            if code != 1:
                raise RuntimeError("expb_text: " + message.value.decode("utf-8", "replace"))
        host.end()
        vertices = host.render()
        return time.perf_counter_ns() - t0, spent, vertices

    # The first frames: the window appears, glyphs are baked, textures are created.
    first = []
    for _ in range(4):
        whole, spent, vertices = frame(True)
        first.append({"frame_us": round(whole / 1000.0, 1), "text_call_us": round(spent / 1000.0, 1), "vertices": vertices})
        call(lib.expb_raster_frame, None, WIDTH, HEIGHT)
    out["first_frames"] = first
    out["first_text_error"] = message.value.decode("utf-8", "replace")

    lib.expb_stats(stats)
    before = list(stats)
    with_text = [frame(True) for _ in range(FRAMES)]
    lib.expb_stats(stats)
    after = list(stats)
    without = [frame(False) for _ in range(FRAMES)]
    whole, spent, vertices = frame(True)
    names = ["TextBuilds", "Layouts", "DrawCalls", "CulledCalls", "VerticesEmitted", "BytesCompared", "CachedParagraphs"]
    out["steady_state"] = {
        "frames": FRAMES,
        "frame_with_text_us_median": median_us([f[0] for f in with_text]),
        "frame_without_text_us_median": median_us([f[0] for f in without]),
        "text_call_us_median": median_us([f[1] for f in with_text]),
        "text_call_us_min": round(min(f[1] for f in with_text) / 1000.0, 2),
        "frame_vertices_with_text": with_text[-1][2],
        "frame_vertices_without_text": without[-1][2],
        "stats_delta_over_frames": {n: a - b for n, a, b in zip(names, after, before)},
    }

    # The same CONTENT in a different object each frame, which is what a
    # caller that formats its text per frame hands over (and, in a generated
    # binding, what any `str` that is not the same object does). The header as
    # snapshotted finds a widget's layout by the text pointer.
    def layouts_over(frames: int, keep_alive: bool) -> dict:
        kept = []
        lib.expb_stats(stats)
        start = list(stats)
        spent = []
        for _ in range(frames):
            fresh = bytes(bytearray(PARAGRAPH))
            if keep_alive:
                kept.append(fresh)
            spent.append(frame(True, fresh)[1])
        lib.expb_stats(stats)
        return {
            "frames": frames,
            "Layouts": stats[1] - start[1],
            "TextBuilds": stats[0] - start[0],
            "CachedParagraphs_after": stats[6],
            "text_call_us_median": median_us(spent),
        }

    out["equal_text_new_object_each_frame"] = {
        "objects_kept_alive": layouts_over(100, True),
        "objects_freed": layouts_over(100, False),
    }

    code, text = call(lib.expb_probe_context)
    try:
        ours = json.loads(text)
    except ValueError:
        ours = {"unparsed": text}
    out["probe_context"] = ours
    theirs = host.read_back()
    if theirs:
        out["probe_vs_binding_differences"] = compare(ours, theirs)

    png = os.path.join(ROOT, "build", "bundle-research", variant, "frame.png")
    out["screenshot"] = call(lib.expb_raster_frame, png.encode(), WIDTH, HEIGHT)
    out["screenshot_path"] = os.path.relpath(png, ROOT)

    # The paragraph path, inside one more frame.
    lib.expb_paragraph_roundtrip.argtypes = [
        ctypes.c_char_p, ctypes.c_float, ctypes.c_int, ctypes.POINTER(ctypes.c_float), ctypes.c_char_p, ctypes.c_int,
    ]
    lib.expb_paragraph_roundtrip.restype = ctypes.c_int
    size = (ctypes.c_float * 2)()
    copied = ctypes.create_string_buffer(8192)
    host.new_frame()
    host.begin()
    lines = lib.expb_paragraph_roundtrip(PARAGRAPH, 400.0, TEXT_FLAGS_MATH, size, copied, len(copied))
    host.end()
    host.render()
    out["paragraph"] = {
        "lines": lines,
        "size": [size[0], size[1]],
        "copy_equals_source": copied.value == PARAGRAPH,
        "copied_head": copied.value[:60].decode("utf-8", "replace"),
    }

    lib.expb_raster_shutdown()
    out["destroy_context"] = call(lib.expb_destroy_context)
    host.destroy()
    out["finished"] = True
    return out


def main(argv: list[str]) -> int:
    if len(argv) == 2 and argv[0] == "--child":
        print(json.dumps(child(argv[1])))
        return 0
    variant = argv[0] if argv else "selfhost"
    proc = subprocess.run(
        [sys.executable, "-B", os.path.abspath(__file__), "--child", variant],
        capture_output=True,
        text=True,
        env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1"),
        cwd=os.environ.get("TMPDIR", "/tmp"),
        timeout=600,
    )
    lines = proc.stdout.strip().splitlines()
    try:
        result = json.loads(lines[-1]) if lines else {"variant": variant}
    except ValueError:
        result = {"variant": variant, "stdout": proc.stdout[-500:]}
    result["exit"] = proc.returncode
    if proc.returncode != 0:
        result["stderr_tail"] = proc.stderr.strip().splitlines()[-6:]
    print(json.dumps(result, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
