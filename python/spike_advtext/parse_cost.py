"""SPIKE: what of advtext's parse is markdown-it and what is building node material.

    PYTHONDONTWRITEBYTECODE=1 ../imgui-adv-text/.venv/bin/python -P -B \
        python/spike_advtext/parse_cost.py [--file sample.md]

No window and no library. For --repeat copies of a demo file, warm (the faces'
metrics already read), the median of seven runs each of:

  markdown_it   advtext's own parser object, tokens only (`advtext.markdown._parser`)
  build         `build_document`: tokens, then CharNode/Glue/Kern material for every
                character, measured against the fonts
  describe      convert.py over the result: the material walked back into text and runs

The difference between the first two is the most a converter that went from
tokens straight to text and runs could save; the third is what the detour
through material costs on top.
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
sys.path.insert(0, HERE)
sys.path.append(ADVTEXT)

import convert  # noqa: E402


def timed(function, runs: int = 7):
    samples = []
    for _ in range(runs):
        started = time.perf_counter()
        function()
        samples.append(time.perf_counter() - started)
    return round(statistics.median(samples) * 1e3, 3), round(min(samples) * 1e3, 3)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--file", default="sample.md")
    args = parser.parse_args()

    from advtext import Theme, build_document, fontlib_metrics, px_from_sp
    from advtext import markdown as advtext_markdown
    from advtext.mathtext import markdown_math
    from advtext.style import BlockKind

    with open(os.path.join(ADVTEXT, "demo", args.file), encoding="utf-8") as handle:
        one = handle.read()
    theme = Theme.build()
    body = theme.style(BlockKind.PARAGRAPH).font.size
    math = markdown_math()
    build_document(one, theme, fontlib_metrics, math=math)  # reads the faces
    report = {"file": args.file, "unit": "ms, (median, min) of 7", "rows": []}
    for repeat in (1, 5, 10, 20):
        source = "\n\n".join([one] * repeat)
        tokens = advtext_markdown._parser(True, True)
        document = build_document(source, theme, fontlib_metrics, math=math)
        report["rows"].append({
            "repeat": repeat,
            "bytes": len(source.encode("utf-8")),
            "blocks": len(document.blocks),
            "markdown_it": timed(lambda: tokens.parse(source)),
            "build": timed(lambda: build_document(source, theme, fontlib_metrics, math=math)),
            "describe": timed(lambda: convert.Converter(theme, px_from_sp, body).describe(document.blocks)),
        })
    print(json.dumps(report, indent=1))


if __name__ == "__main__":
    main()
