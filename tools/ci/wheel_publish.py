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
    every (platform, CPython, imgui-bundle) of it, none twice, none besides;
  - every wheel declares the SAME Requires-Python, and it admits the CPython
    of every wheel's tag. An index keeps one Requires-Python per release, from
    the first file it is given, and an installer filters every file of the
    release by it: 0.4.1 went out with `==3.11.*` on all its files and only
    CPython 3.11 could install it (docs/releasing.md, section 6).
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
import re
import shutil
import subprocess
import sys
import zipfile

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


def requires_python_of(path: str) -> list:
    """The Requires-Python fields of a wheel's METADATA, as written."""
    name = os.path.basename(path)
    version = name[:-len(".whl")].split("-")[1]
    with zipfile.ZipFile(path) as archive:
        text = archive.read(f"imtypeset-{version}.dist-info/METADATA").decode("utf-8")
    return [line.partition(": ")[2] for line in text.split("\n\n", 1)[0].splitlines()
            if line.startswith("Requires-Python: ")]


def requires_python_problems(rows) -> list:
    """rows: (file name, python tag, [Requires-Python as written]) per wheel.
    One value across the release, admitting every file's CPython; read from
    the files, not from this tree, so it holds of what is about to be sent."""
    problems = []
    values = {}
    for name, _python_tag, written in rows:
        if len(written) != 1:
            problems.append(f"{name} has {len(written)} Requires-Python fields, not one: {written}")
        else:
            values.setdefault(written[0], []).append(name)
    if len(values) > 1:
        problems.append("the wheels disagree on Requires-Python, and an index keeps one per release: "
                        + "; ".join(f"{value!r} in {len(names)} ({names[0]}, ...)"
                                    for value, names in sorted(values.items())))
        return problems
    for value in values:
        for name, python_tag, _written in rows:
            python = f"3.{python_tag[3:]}" if re.fullmatch(r"cp3\d+", python_tag) else python_tag
            if wheel_matrix.admits(value, python) is not True:
                problems.append(f"Requires-Python {value!r}, the release's, does not admit {python_tag}: "
                                f"no installer would take {name}")
    return problems


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
    problems += requires_python_problems(
        [(os.path.basename(path), os.path.basename(path).split("-")[2], requires_python_of(path))
         for path in wheels])
    if problems:
        print(f"wheel_publish.py: the wheels are not one release of the matrix ({len(want)} cells):",
              file=sys.stderr)
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
