"""SPIKE: what advtext's own documents hold, and what of it an imtypeset block
description cannot say.

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=../imgui-adv-text \
        ../imgui-adv-text/.venv/bin/python -P -B python/spike_advtext/census.py

Loads no library and opens no window. `advtext.markdown.build_document` (with
the reference's own formula builder) over every file under
../imgui-adv-text/demo/*.md, each walked by convert.py. Prints one JSON object:
per file and in total, the block and node kinds met (`walked`) and everything
that did not fit (`misfits`).
"""

import collections
import glob
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
ADVTEXT = os.environ.get("ADVTEXT_ROOT", os.path.join(os.path.dirname(REPO), "imgui-adv-text"))
sys.path.insert(0, HERE)
sys.path.append(ADVTEXT)

import convert  # noqa: E402


def main() -> None:
    from advtext import Theme, build_document, fontlib_metrics, px_from_sp
    from advtext.mathtext import markdown_math
    from advtext.style import BlockKind

    theme = Theme.build()
    body = theme.style(BlockKind.PARAGRAPH).font.size
    report = {"files": {}, "total": {}}
    total_walked, total_misfits = collections.Counter(), collections.Counter()
    for path in sorted(glob.glob(os.path.join(ADVTEXT, "demo", "*.md"))):
        with open(path, encoding="utf-8") as handle:
            source = handle.read()
        document = build_document(source, theme, fontlib_metrics, math=markdown_math())
        converter = convert.Converter(theme, px_from_sp, body)
        described = converter.describe(document.blocks)
        report["files"][os.path.basename(path)] = {
            "bytes": len(source.encode("utf-8")),
            "blocks": len(document.blocks),
            "imtypeset_blocks": len(described.blocks),
            "styles": len(described.styles),
            "walked": dict(sorted(described.kinds.items())),
            "misfits": dict(sorted(described.misfits.items())),
        }
        total_walked.update(described.kinds)
        total_misfits.update(described.misfits)
    report["total"] = {"walked": dict(sorted(total_walked.items())),
                       "misfits": dict(sorted(total_misfits.items()))}
    print(json.dumps(report, indent=1))


if __name__ == "__main__":
    main()
