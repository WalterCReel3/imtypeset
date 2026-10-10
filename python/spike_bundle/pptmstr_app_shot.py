"""SPIKE: run the REAL pptmstr (`pptmstr.app.main`) for N frames, report what its
rich renderer did, photograph the last frame.

    # as it is on disk
    sh tools/shot.sh --screen 1600x1000x24 -- ../pptmstr/.venv/bin/python -P -B \
        python/spike_bundle/pptmstr_app_shot.py --out today.png -- --fake
    # with the three overlay modules (docs/advtext-rework-plan.md, "Start here")
    PYTHONPATH=$PWD/python/spike_bundle/pptmstr_overlay:$PWD/python \
    IMTYPESET_LIBRARY=$PWD/build/advtext-rework-plan-pptmstr/libimtypeset_bundle.so \
    sh tools/shot.sh --screen 1600x1000x24 -- ../pptmstr/.venv/bin/python -P -B \
        python/spike_bundle/pptmstr_app_shot.py --out imtypeset.png -- --fake

What this adds to the application, and nothing else: a frame count after which
it exits (as pptmstr's own scripts/screenshot.py does), `--rich`, which sets
CONTEXT's mode to RICH on the first frame as a click on its radio button
would, a clock round `rich_pane.draw`, and a count of the calls it makes into
imtypeset. Set XDG_CONFIG_HOME to a scratch directory: the application saves its
settings and window layout there.

The screenshot is hello_imgui's own, which hands over a numpy array; pptmstr's
environment has no numpy, so SPIKE_EXTRA_SITE names a site-packages that has
one (appended LAST to sys.path).
"""

import argparse
import json
import os
import statistics
import struct
import sys
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
PPTMSTR = os.environ.get("PPTMSTR_ROOT", os.path.join(os.path.dirname(REPO), "pptmstr"))
if os.environ.get("SPIKE_EXTRA_SITE"):
    sys.path.append(os.environ["SPIKE_EXTRA_SITE"])


def write_png(path: str, pixels) -> None:
    height, width = pixels.shape[0], pixels.shape[1]
    rgb = pixels[:, :, :3].tobytes()

    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    row = width * 3
    raw = b"".join(b"\0" + rgb[y * row : (y + 1) * row] for y in range(height))
    with open(path, "wb") as out:
        out.write(b"\x89PNG\r\n\x1a\n")
        out.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        out.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        out.write(chunk(b"IEND", b""))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", default="")
    parser.add_argument("--frames", type=int, default=240)
    parser.add_argument("--focus", default="DETAIL", help="dockable window to bring to the front")
    parser.add_argument("--rich", action="store_true", help="CONTEXT in RICH mode")
    parser.add_argument("argv", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    argv = [a for a in args.argv if a != "--"] or ["--fake"]

    from imgui_bundle import hello_imgui, immapp

    from pptmstr import app as pptmstr_app
    from pptmstr.ui import rich_pane, transcript_pane

    typeset = sys.modules.get("pptmstr.ui.typeset")
    times: list = []
    library_calls = [0]
    real_draw = rich_pane.draw

    def timed_draw(*a, **k):
        started = time.perf_counter()
        try:
            return real_draw(*a, **k)
        finally:
            times.append(time.perf_counter() - started)

    rich_pane.draw = timed_draw

    if args.rich:
        real_pane = transcript_pane.draw

        def rich_first(snap, state, selected):
            if not getattr(state, "_spike_rich", False):
                state.mode = transcript_pane.RenderMode.RICH
                state._spike_rich = True
            return real_pane(snap, state, selected)

        transcript_pane.draw = rich_first

    real_run = immapp.run
    frames = [0]

    def patched_run(runner, *rest):
        user_after = runner.callbacks.after_swap

        def after_swap() -> None:
            if user_after:
                user_after()
            frames[0] += 1
            if args.focus and frames[0] == 2:
                hello_imgui.get_runner_params().docking_params.focus_dockable_window(args.focus)
            if frames[0] == 30 and typeset is not None and typeset.module() is not None:
                # From here on, count the calls into the library.
                ts = typeset.module()

                class Counting:
                    def __init__(self, real):
                        self.real = real

                    def __getattr__(self, name):
                        function = getattr(self.real, name)

                        def call(*a):
                            library_calls[0] += 1
                            return function(*a)

                        return call

                ts._lib = Counting(ts._lib)
            if frames[0] >= args.frames:
                hello_imgui.get_runner_params().app_shall_exit = True

        runner.callbacks.after_swap = after_swap
        runner.fps_idling.enable_idling = False
        return real_run(runner, *rest)

    immapp.run = patched_run
    try:
        status = int(pptmstr_app.main(argv))
    finally:
        immapp.run = real_run

    steady = sorted(times[len(times) // 4 :]) or [0.0]
    report = {
        "frames": frames[0],
        "pptmstr.app": pptmstr_app.__file__,
        "pptmstr.ui.rich_pane": rich_pane.__file__,
        "imtypeset": None,
        "rich_pane.draw calls": len(times),
        "rich_pane.draw median_us": round(statistics.median(steady) * 1e6, 1),
        "rich_pane.draw p95_us": round(steady[int(0.95 * (len(steady) - 1))] * 1e6, 1),
        "rich_pane.draw first_us": round(times[0] * 1e6, 1) if times else None,
    }
    if typeset is not None:
        ts = typeset.module()
        report["imtypeset"] = "in use" if ts is not None else f"not in use: {typeset.reason()}"
        if ts is not None:
            report["imtypeset host"] = ts.host_info()["host"]
            report["wheel"] = ts.host_info()["bundle"]["wheel_version"]
            report["library calls after frame 30"] = library_calls[0]
            report["library calls per rich_pane.draw"] = round(
                library_calls[0] / max(1, len(times) * (frames[0] - 30) / max(1, frames[0])), 2
            )
    print(json.dumps(report, indent=1))
    if args.out:
        try:
            write_png(args.out, hello_imgui.final_app_window_screenshot())
            print("wrote", args.out)
        except Exception as exc:  # noqa: BLE001
            print("no screenshot:", type(exc).__name__, exc)
    return status


if __name__ == "__main__":
    raise SystemExit(main())
