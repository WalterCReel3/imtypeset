#!/usr/bin/env python3
"""
Time the reference's font setup, for the comparison in docs/performance.md.

Run with the reference's interpreter and nothing written into its tree:

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=build/advtext-reference \
        ../imgui-adv-text/.venv/bin/python -P -B bench/reference_font_setup.py

One process is one sample: everything here is paid once per process, and the
reference caches all of it. Run it several times for a spread. What is timed:

  import        importing advtext.fontlib (fontTools comes with it)
  metrics       advtext.fontlib.metrics(face, 16.0) for each face: open the
                file with fontTools and read what layout needs
  imgui file    advtext.fontlib.imgui_font_file(face): for the math face, write
                the minted codepoints into the cmap and save a copy under the
                temporary directory; for the others, nothing

The patched copy is written under $TMPDIR, which is where the reference puts
it; this script writes nothing else and prints to stdout.
"""

from __future__ import annotations

import time


def main() -> int:
    start = time.perf_counter()
    from advtext import fontlib
    from advtext.fontmetrics import Face

    imported = time.perf_counter() - start
    print(f"| import advtext.fontlib | {imported * 1000.0:.1f} ms |")
    total_metrics = 0.0
    total_file = 0.0
    for face in Face:
        start = time.perf_counter()
        fontlib.metrics(face, 16.0)
        took_metrics = time.perf_counter() - start
        start = time.perf_counter()
        path = fontlib.imgui_font_file(face)
        took_file = time.perf_counter() - start
        total_metrics += took_metrics
        total_file += took_file
        print(
            f"| {face.value}: metrics | {took_metrics * 1000.0:.1f} ms | "
            f"imgui_font_file | {took_file * 1000.0:.1f} ms | {path.name} |"
        )
    print(
        f"| all faces | metrics {total_metrics * 1000.0:.1f} ms | "
        f"imgui_font_file {total_file * 1000.0:.1f} ms | "
        f"with the import {(imported + total_metrics + total_file) * 1000.0:.1f} ms |"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
