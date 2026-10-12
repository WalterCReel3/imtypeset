"""Turns the probe's verdict into the job's colour, after the report has been
published: exit 0 when verdict.json says ok, 1 otherwise or when there is none.

    python3 tools/ci/sidecar_verdict.py <report-directory>

"ok" is: a library built against the wheel's own headers was accepted by the
import gate, Dear ImGui's own layout check included, and the headless checks
passed; or, for a job started with --expect-no-wheel, that there is no wheel.
A red job still has its whole report in the job summary and the artifact.
"""

import json
import os
import sys


def main(argv: list) -> int:
    path = os.path.join(argv[0], "verdict.json")
    try:
        with open(path, encoding="utf-8") as handle:
            verdict = json.load(handle)
    except (OSError, ValueError) as exc:
        print(f"sidecar_verdict.py: no verdict ({exc}): the probe did not finish")
        return 1
    print(f"sidecar_verdict.py: {verdict.get('platform')}: H1 {verdict.get('h1')}, "
          f"H2 {verdict.get('h2')}, ok={verdict.get('ok')}")
    return 0 if verdict.get("ok") is True else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
