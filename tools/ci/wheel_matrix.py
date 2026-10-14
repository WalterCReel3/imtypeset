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
    # Not ubuntu-latest: an older C library to compile against, so that the
    # measured platform tag stays low (.github/workflows/wheels.yml).
    linux = environ.get("IMTYPESET_CI_LINUX") or "ubuntu-22.04"
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


def requires_python(pythons=None) -> str:
    """The Requires-Python every wheel of a release declares: the CPythons the
    matrix builds, as one range. One value for the whole release, because an
    index keeps one per release and an installer filters every file of the
    release by it (docs/releasing.md, section 6); which CPython a file is for
    is its tag, `cp3XY-none-<platform>`. A matrix with a hole in it cannot be
    one range, and is refused rather than written as one."""
    pythons = sidecar_matrix.PYTHONS if pythons is None else pythons
    versions = sorted(tuple(int(part) for part in python.split(".")) for python in pythons)
    if not versions or any(len(v) != 2 or v[0] != 3 for v in versions) or \
            [v[1] for v in versions] != list(range(versions[0][1], versions[-1][1] + 1)):
        raise ValueError(f"the matrix's CPythons {list(pythons)} are not one run of 3.X minors")
    return f">=3.{versions[0][1]},<3.{versions[-1][1] + 1}"


def _release(text: str):
    parts = text.split(".")
    if not parts or not all(part.isdigit() for part in parts):
        return None
    return tuple(int(part) for part in parts)


def admits(specifier: str, python: str):
    """Whether every CPython `python` ("3.12": 3.12.0 and each patch after it)
    satisfies the Requires-Python `specifier`. Reads the clauses a range of
    final releases is written with (>=, >, <=, <, ==, !=, the last two with or
    without .*); anything else answers None, which a caller takes as "cannot
    say", never as yes."""
    low = _release(python)
    if low is None:
        return None
    candidates = (low + (0,), low + (10**6,))
    for clause in (part.strip() for part in specifier.split(",")):
        for op in (">=", "<=", "==", "!=", ">", "<"):
            if clause.startswith(op):
                value = clause[len(op):].strip()
                break
        else:
            return None
        prefix = value.endswith(".*")
        bound = _release(value[:-2] if prefix else value)
        if bound is None or (prefix and op not in ("==", "!=")):
            return None
        for candidate in candidates:
            if prefix:
                hit = candidate[:len(bound)] == bound
                ok = hit if op == "==" else not hit
            else:
                width = max(len(candidate), len(bound))
                c = candidate + (0,) * (width - len(candidate))
                b = bound + (0,) * (width - len(bound))
                ok = {">=": c >= b, ">": c > b, "<=": c <= b, "<": c < b,
                      "==": c == b, "!=": c != b}[op]
            if not ok:
                return False
    return True


if __name__ == "__main__":
    print("matrix=" + json.dumps(matrix(), separators=(",", ":")))
