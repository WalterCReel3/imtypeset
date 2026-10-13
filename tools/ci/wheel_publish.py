"""Gathers the wheels of one release for an upload to a package index, after
refusing anything that is not that release, whole.

    python3 -I -B tools/ci/wheel_publish.py <tag> <directory holding the wheels, at any depth> <out>

Refuses, and writes nothing, unless:
  - the tag is "v" + the version every copy in the tree agrees on, and
    docs/CHANGELOG.md has a section for it (tools/version.py);
  - every wheel passes tools/wheel/check_wheel.py --release for that tag (no
    stand-in headers, accepted by Dear ImGui's own layout check, no
    development or local version segment, a manylinux or macosx tag);
  - the wheels are EXACTLY the matrix of tools/ci/wheel_matrix.py: one for
    every (platform, CPython, imgui-bundle) of it, none twice, none besides.
    An index takes files one at a time and never lets a file name be used
    again, so a release with a hole in it cannot be repaired by replacing
    anything: it is refused here, before the first file leaves.

Then copies the wheels, flat, into <out> (which must not hold anything), prints
each with its sha256, and writes the list to $GITHUB_STEP_SUMMARY when that is
set. It uploads nothing: in .github/workflows/wheels.yml the next step is the
index's own action, which reads <out>.
"""

import hashlib
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import wheel_matrix  # noqa: E402

# What the matrix calls a platform, and how a wheel's platform tag begins.
PLATFORMS = {"linux-x86_64": ("manylinux_", "_x86_64"), "macos-arm64": ("macosx_", "_arm64")}


def line(bundle: str) -> str:
    major, minor, patch = (int(part) for part in bundle.split("."))
    return str(major * 100000 + minor * 1000 + patch)


def expected(version: str) -> dict:
    """{(python tag, distribution version, platform of the matrix): the job's id}."""
    cells = {}
    for entry in wheel_matrix.matrix({})["include"]:
        short = entry["id"].split("-py")[0]
        cells[("cp" + entry["python"].replace(".", ""), f"{version}.{line(entry['bundle'])}", short)] = entry["id"]
    return cells


def cell_of(name: str):
    parts = name[:-len(".whl")].split("-")
    if len(parts) != 5:
        return None
    _, version, python_tag, _abi, platform_tag = parts
    for short, (prefix, suffix) in PLATFORMS.items():
        if platform_tag.startswith(prefix) and platform_tag.endswith(suffix):
            return (python_tag, version, short)
    return (python_tag, version, platform_tag)


def main(argv) -> int:
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    tag, directory, out = argv
    python = [sys.executable, "-I", "-B"]
    if subprocess.run(python + [os.path.join(REPO, "tools", "version.py"), "--check", "--tag", tag]).returncode:
        return 1
    version = tag[1:]
    wheels = sorted(os.path.join(root, name) for root, _, names in os.walk(directory)
                    for name in names if name.startswith("imtypeset-") and name.endswith(".whl"))
    if not wheels:
        print(f"wheel_publish.py: no wheel under {directory}", file=sys.stderr)
        return 1
    if subprocess.run(python + [os.path.join(REPO, "tools", "wheel", "check_wheel.py"),
                                "--release", "--tag", tag] + wheels).returncode:
        return 1
    want = expected(version)
    have = {}
    problems = []
    for path in wheels:
        cell = cell_of(os.path.basename(path))
        if cell in have:
            problems.append(f"two wheels for {cell}: {os.path.basename(have[cell])}, {os.path.basename(path)}")
        elif cell not in want:
            problems.append(f"{os.path.basename(path)} is no cell of the matrix")
        else:
            have[cell] = path
    for cell in sorted(set(want) - set(have)):
        problems.append(f"no wheel for {want[cell]} ({cell[1]}, {cell[0]})")
    if problems:
        print(f"wheel_publish.py: the wheels are not the release's matrix ({len(want)} cells):", file=sys.stderr)
        for problem in problems:
            print("  " + problem, file=sys.stderr)
        return 1
    if os.path.isdir(out) and os.listdir(out):
        print(f"wheel_publish.py: {out} is not empty", file=sys.stderr)
        return 1
    os.makedirs(out, exist_ok=True)
    rows = []
    for cell in sorted(have):
        name = os.path.basename(have[cell])
        shutil.copyfile(have[cell], os.path.join(out, name))
        with open(have[cell], "rb") as handle:
            rows.append(f"{hashlib.sha256(handle.read()).hexdigest()}  {name}")
    text = [f"{len(rows)} wheels of {tag}, every cell of the matrix, each a release:"] + rows
    print("\n".join(text))
    summary = os.environ.get("GITHUB_STEP_SUMMARY", "")
    if summary:
        with open(summary, "a", encoding="utf-8") as handle:
            handle.write("\n### Wheels gathered for the index\n\n```\n" + "\n".join(text) + "\n```\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
