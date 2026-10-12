"""The job matrix of .github/workflows/sidecar-probe.yml, as one line for
$GITHUB_OUTPUT: `matrix={"include": [...]}`.

    python3 tools/ci/sidecar_matrix.py

Runner labels come from the environment, so that a label GitHub has retired
is changed when the workflow is started by hand and not by an edit:

    IMTYPESET_CI_LINUX         default ubuntu-latest
    IMTYPESET_CI_MACOS_ARM64   default macos-14
    IMTYPESET_CI_MACOS_X86_64  default macos-15-intel; "none" leaves it out

The defaults were read from GitHub's documentation of its hosted runners on
2026-10-11 through a summarising fetch, not from a run.

Which wheels exist decides the rest. From PyPI's file lists for imgui-bundle
1.92.900 and 1.92.801 (same day, same kind of read): manylinux x86-64 and
macOS arm64 (macosx_14_0) for CPython 3.11 to 3.14, and NO macOS x86-64 wheel.
So the Intel job expects pip to find nothing, and says so if that is true.
"""

import json
import os

PYTHONS = ("3.11", "3.12", "3.13")
PRIMARY = "1.92.900"   # advtext's pin
SECONDARY = "1.92.801"  # pptmstr's


def matrix(environ=os.environ) -> dict:
    linux = environ.get("IMTYPESET_CI_LINUX") or "ubuntu-latest"
    arm64 = environ.get("IMTYPESET_CI_MACOS_ARM64") or "macos-14"
    intel = environ.get("IMTYPESET_CI_MACOS_X86_64") or "macos-15-intel"
    include = []

    def add(os_label, short, python, bundle, flags):
        include.append({
            "os": os_label,
            "python": python,
            "bundle": bundle,
            "flags": " ".join(flags),
            "id": f"{short}-py{python}-bundle{bundle}",
            "name": f"{short} / CPython {python} / imgui-bundle {bundle}",
        })

    for python in PYTHONS:
        # The offset-mutation run takes minutes; once per platform is enough.
        mutate = ["--mutate"] if python == PYTHONS[0] else []
        add(linux, "linux-x86_64", python, PRIMARY, ["--also-lookup"] + mutate)
        add(arm64, "macos-arm64", python, PRIMARY, mutate)
    add(linux, "linux-x86_64", PYTHONS[0], SECONDARY, ["--also-lookup"])
    add(arm64, "macos-arm64", PYTHONS[0], SECONDARY, [])
    if intel != "none":
        add(intel, "macos-x86_64", PYTHONS[0], PRIMARY, ["--expect-no-wheel"])
    return {"include": include}


if __name__ == "__main__":
    print("matrix=" + json.dumps(matrix(), separators=(",", ":")))
