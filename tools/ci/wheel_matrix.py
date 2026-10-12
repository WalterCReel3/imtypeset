"""The job matrix of .github/workflows/wheels.yml, as one line for
$GITHUB_OUTPUT: `matrix={"include": [...]}`. One job builds, checks, installs
and tests one wheel: one platform, one CPython, one imgui-bundle.

    python3 tools/ci/wheel_matrix.py

Runner labels come from the environment, as for tools/ci/sidecar_matrix.py
(IMTYPESET_CI_LINUX, IMTYPESET_CI_MACOS_ARM64), and so do the pythons and the
imgui-bundle versions, which are that file's: a wheel is built for exactly the
pairs the probe workflow probes. There is no macOS x86-64 job: PyPI has no
imgui-bundle wheel for it (docs/sidecar.md, section 1).
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sidecar_matrix  # noqa: E402


def matrix(environ=os.environ) -> dict:
    linux = environ.get("IMTYPESET_CI_LINUX") or "ubuntu-latest"
    arm64 = environ.get("IMTYPESET_CI_MACOS_ARM64") or "macos-14"
    include = []
    for os_label, short in ((linux, "linux-x86_64"), (arm64, "macos-arm64")):
        for bundle in (sidecar_matrix.PRIMARY, sidecar_matrix.SECONDARY):
            for python in sidecar_matrix.PYTHONS:
                include.append({
                    "os": os_label,
                    "python": python,
                    "bundle": bundle,
                    "id": f"{short}-py{python}-bundle{bundle}",
                    "name": f"{short} / CPython {python} / imgui-bundle {bundle}",
                })
    return {"include": include}


if __name__ == "__main__":
    print("matrix=" + json.dumps(matrix(), separators=(",", ":")))
