#!/usr/bin/env python3
"""
Line the rows of several runs of one benchmark up, to see the run-to-run spread.

    for i in 1 2 3 4 5; do build/release/bench/bench_draw > /tmp/draw-$i.txt; done
    python3 bench/spread.py /tmp/draw-*.txt

Reads the `| what | n | min | median | p95 | unit | note |` rows the benchmarks
print and writes one row per `what`: the least minimum, the medians' own least,
median and greatest, and the spread of the medians, (greatest - least) / least.
A second group of files after `--against` is a second set of runs of the same
benchmark (before a change, say); the table then ends with the ratio of the two
medians-of-medians.

Standard library only; reads the files named and writes to stdout.
"""

from __future__ import annotations

import statistics
import sys

_SCALE = {"ns": 1e-3, "us": 1.0, "ms": 1e3}


def rows(path: str) -> dict[str, tuple[float, float, float]]:
    """`what` -> (min, median, p95) in microseconds, in file order."""
    found: dict[str, tuple[float, float, float]] = {}
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
            if len(cells) < 6 or cells[5] not in _SCALE:
                continue
            try:
                low, mid, high = (float(cells[i]) * _SCALE[cells[5]] for i in (2, 3, 4))
            except ValueError:
                continue
            found[cells[0]] = (low, mid, high)
    return found


def fmt(us: float) -> str:
    if us >= 10000.0:
        return f"{us / 1000.0:.2f} ms"
    if us < 1.0:
        return f"{us * 1000.0:.1f} ns"
    return f"{us:.2f} us"


def gather(paths: list[str]) -> dict[str, list[tuple[float, float, float]]]:
    table: dict[str, list[tuple[float, float, float]]] = {}
    for path in paths:
        for what, values in rows(path).items():
            table.setdefault(what, []).append(values)
    return table


def main(argv: list[str]) -> int:
    args = argv[1:]
    if not args:
        sys.stderr.write(__doc__)
        return 2
    before: list[str] = []
    if "--against" in args:
        split = args.index("--against")
        args, before = args[:split], args[split + 1 :]
    table = gather(args)
    old = gather(before)
    head = "| what | runs | least min | median: least | of medians | greatest | spread |"
    rule = "|---|---|---|---|---|---|---|"
    if old:
        head += " before | ratio |"
        rule += "---|---|"
    print(head)
    print(rule)
    for what, runs in table.items():
        medians = sorted(run[1] for run in runs)
        middle = statistics.median(medians)
        spread = (medians[-1] - medians[0]) / medians[0] * 100.0 if medians[0] > 0 else 0.0
        line = (
            f"| {what} | {len(runs)} | {fmt(min(run[0] for run in runs))} | {fmt(medians[0])} "
            f"| {fmt(middle)} | {fmt(medians[-1])} | {spread:.1f}% |"
        )
        if old:
            if what in old:
                was = statistics.median(run[1] for run in old[what])
                line += f" {fmt(was)} | {was / middle:.2f}x |"
            else:
                line += " | |"
        print(line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
