"""What a frame costs from Python: the minimal host's page, N steady frames.

    IMTYPESET_LIBRARY=build/py-host/libimtypeset_pyhost.so SDL_VIDEODRIVER=dummy \
        python3 -P -B examples/python/bench_host.py [--frames 1000]

Times, per frame, with time.perf_counter_ns around each part: the two imtypeset
widget calls (a justified paragraph with three formulas, and a display formula),
ImGui::TextWrapped of the same source through the same host, the whole frame as
Python sees it up to ImGui::Render, and the render and present of the backend.
Also the cost of a binding call that does nothing. Take numbers from a Release
build of the library only; the script prints what it was given.
"""

import argparse
import os
import statistics
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path[:0] = [
    os.path.join(REPO, "python"),
    os.path.join(REPO, "python", "host"),
    os.path.dirname(os.path.abspath(__file__)),  # minimal_host, under -P too
]

import imtypeset as ts  # noqa: E402
import imtypeset_host as host  # noqa: E402
from minimal_host import DISPLAY, FONTS, PROSE  # noqa: E402


def summary(name, samples_ns, per=1):
    us = sorted(s / 1000.0 / per for s in samples_ns)
    print(
        f"{name:<44} median {statistics.median(us):8.2f} us   mean {statistics.fmean(us):8.2f} us"
        f"   p95 {us[int(len(us) * 0.95)]:8.2f} us   n={len(us)}"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--frames", type=int, default=1000)
    args = parser.parse_args()
    clock = time.perf_counter_ns

    with host.Window("bench", 960, 560, dpi_scale=1.0) as window:
        ts.create_context()
        for face, name in FONTS.items():
            ts.add_font(face, os.path.join(REPO, "tests", "data", "fonts", name))
        print("library:", ts.host_info()["library"])
        print("build:", ts.host_info()["rasteriser"], f"ImWchar {ts.host_info()['imwchar_bits']} bits,",
              "imtypeset", ts.version(), "Dear ImGui", ts.host_info()["imgui_version"])
        print("host:", host.info()["video_driver"], host.info()["renderer_backend"], host.info()["renderer"])
        print("python:", sys.version.split()[0], "text:", len(PROSE), "characters +", len(DISPLAY))

        selection = (0, 0)
        typeset, imgui, python_frame, present = [], [], [], []
        before = None
        for index in range(args.frames + 10):
            if index == 10:  # Ten frames to settle: layout, the atlas, the window's size.
                before = ts.stats()
            t0 = clock()
            assert window.begin_frame()
            host.set_next_window_pos(0.0, 0.0)
            host.set_next_window_size(960.0, 560.0)
            host.begin("demo", host.WINDOW_NO_TITLE_BAR | host.WINDOW_NO_RESIZE | host.WINDOW_NO_MOVE)
            host.push_font(ts.get_font(ts.Face.Regular), 17.0)
            host.begin_child("imgui", 440.0)
            t1 = clock()
            host.text_wrapped(PROSE)
            t2 = clock()
            host.end_child()
            host.same_line(24.0)
            host.begin_child("imtypeset", 440.0)
            t3 = clock()
            _, selection = ts.selectable_text("prose", PROSE, flags=ts.TextFlags.Math, selection=selection)
            ts.text(DISPLAY, flags=ts.TextFlags.Math)
            t4 = clock()
            host.end_child()
            host.pop_font()
            host.end()
            t5 = clock()
            window.end_frame()
            t6 = clock()
            if index >= 10:
                imgui.append(t2 - t1)
                typeset.append(t4 - t3)
                python_frame.append(t5 - t0)
                present.append(t6 - t5)
        after = ts.stats()

        calls = 20000
        empty = []
        for _ in range(20):
            t0 = clock()
            for _ in range(calls):
                ts.current_context()
            empty.append(clock() - t0)

        frames = args.frames
        print(f"steady frames: {frames}; layouts in them: {after.Layouts - before.Layouts}; "
              f"text builds: {after.TextBuilds - before.TextBuilds}; "
              f"paragraphs drawn per frame: {(after.DrawCalls - before.DrawCalls) / frames:g}; "
              f"vertices per frame: {(after.VerticesEmitted - before.VerticesEmitted) / frames:g}; "
              f"bytes hashed per frame: {(after.BytesHashed - before.BytesHashed) / frames:g}")
        summary("imtypeset: selectable_text + text (2 calls)", typeset)
        summary("ImGui::TextWrapped, same source (1 call)", imgui)
        summary("frame, NewFrame to End, in Python", python_frame)
        summary("Render + backend + present", present)
        summary("a binding call that does nothing", empty, per=calls)
        ts.destroy_context()


if __name__ == "__main__":
    main()
