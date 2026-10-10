"""Medians of what measure_pptmstr.sh and ../spike_advtext/measure.sh wrote.

    python3 python/spike_bundle/summarise.py build/advtext-rework-plan-run/results-pptmstr.jsonl [more.jsonl]

Runs with the same arguments are one group; a time is the median over the
group's runs, followed by each run's value. The load average each run started
under is printed with it: these were not taken on a quiet machine.
"""

import collections
import json
import statistics
import sys

TIMES = (
    "parse_us", "parse_again_us", "first_frame_us", "first_frame_convert_us", "steady_median_us",
    "steady_p95_us", "stream_median_us", "stream_p95_us", "stream_max_us", "stream_draw_median_us",
    "stream_draw_p95_us", "stream_parse_median_us", "stream_convert_median_us", "scroll_median_us",
    "scroll_p95_us", "scroll_max_us", "resize_median_us", "resize_p95_us", "with_needle_median_us",
    "drag_median_us", "copy_frame_us", "settle_all_us", "copy_everything_us", "copy_visible_us",
    "select_all_and_copy_us", "find_index_build_us",
)
FACTS = (
    "lines", "blocks", "document_blocks", "tables", "first_frame_phases_us", "steady_phases_median_us",
    "stream_phases_median_us", "profile_median", "keystroke_frame_us",
    "content_height_changes_while_scrolling", "largest_content_height_change_px",
    "sum_of_content_height_changes_px", "scroll_range_px", "height_before_settling_px",
    "height_settled_px", "estimate_error_by_kind", "estimate_net_error_px", "document_advance_px",
    "imgui_advance_px", "empty_library_call_ns", "empty_binding_call_ns", "selection", "clipboard_chars",
    "copy_everything_chars", "copied_chars", "blocks_with_an_error", "edits", "marks", "document_stats",
)


def main() -> None:
    groups = collections.defaultdict(list)
    for path in sys.argv[1:]:
        command = ""
        for line in open(path, encoding="utf-8"):
            if line.startswith("#"):
                command = line.strip()
                continue
            try:
                row = json.loads(line)
            except ValueError:
                print("NOT JSON after", command, "::", line[:160].strip())
                continue
            row["_load"] = command.split()[3] if len(command.split()) > 3 else "?"
            groups[command.split("::", 1)[-1].strip()].append(row)
    for arguments in sorted(groups):
        rows = groups[arguments]
        print(f"{arguments}   (n={len(rows)}, load {', '.join(r['_load'] for r in rows)})")
        for name in TIMES:
            values = [r[name] for r in rows if r.get(name) is not None]
            if values:
                print(f"    {name:28s} {statistics.median(values):>12.1f}   {[round(v) for v in values]}")
        for name in FACTS:
            if rows[0].get(name) is not None:
                print(f"    {name:28s} {rows[0][name]}")


if __name__ == "__main__":
    main()
