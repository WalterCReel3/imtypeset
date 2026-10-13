"""The requirement a package that DEPENDS on imtypeset writes, checked: the
environment marker that keeps `pip install <that package>` working where
there is no imtypeset wheel.

    <python with `packaging`> -I -B tools/wheel/dependent_check.py [--out <directory>]
        [--imtypeset 0.4.1.192900] [--bundle 1.92.900]

Prints the two requirement lines, then the marker evaluated (with `packaging`,
which is what pip evaluates it with) for a table of environments: where a
wheel exists it must be true, and false everywhere else the marker can tell
apart. With --out, also writes a dependent to give pip's resolver:
`dependent_stand_in-0.0.0-py3-none-any.whl`, a wheel holding nothing but
metadata with those two lines as its Requires-Dist, and
`dependent_only_imtypeset-0.0.0-py3-none-any.whl` with the second line alone.
docs/releasing.md says how they were used.

WHAT NO MARKER CAN SAY: which C library a Linux has. On musl (Alpine) the
marker is true, there is no musllinux wheel of imtypeset, and pip fails on
the dependency.
"""

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build_wheel  # noqa: E402

# Where imtypeset has wheels: Linux x86-64 and macOS arm64, CPython 3.11 to
# 3.14 (tools/ci/wheel_matrix.py). The upper bound is a wheel that does not
# exist yet, not a judgement of that CPython.
MARKER = ("python_version >= '3.11' and python_version < '3.15' and "
          "((sys_platform == 'linux' and platform_machine == 'x86_64') or "
          "(sys_platform == 'darwin' and platform_machine == 'arm64'))")

# (what it is, sys_platform, platform_machine, python_version, is there a wheel)
ENVIRONMENTS = (
    ("Linux x86-64, CPython 3.11", "linux", "x86_64", "3.11", True),
    ("Linux x86-64, CPython 3.14", "linux", "x86_64", "3.14", True),
    ("macOS arm64, CPython 3.12", "darwin", "arm64", "3.12", True),
    ("macOS arm64, CPython 3.14", "darwin", "arm64", "3.14", True),
    ("Linux x86-64, CPython 3.15", "linux", "x86_64", "3.15", False),
    ("Linux x86-64, CPython 3.10", "linux", "x86_64", "3.10", False),
    ("Linux aarch64", "linux", "aarch64", "3.12", False),
    ("macOS x86-64 (Intel)", "darwin", "x86_64", "3.12", False),
    ("Windows x86-64", "win32", "AMD64", "3.12", False),
    ("Windows arm64", "win32", "ARM64", "3.12", False),
    ("Emscripten (Pyodide)", "emscripten", "wasm32", "3.14", False),
)


def lines(imtypeset: str, bundle: str) -> list:
    return [f"imgui-bundle=={bundle}", f"imtypeset=={imtypeset}; {MARKER}"]


def write_dependent(directory: str, name: str, requires: list) -> str:
    dist_info = f"{name}-0.0.0.dist-info"
    metadata = "\n".join(["Metadata-Version: 2.1", f"Name: {name}", "Version: 0.0.0",
                          "Summary: a stand-in for a package that depends on imtypeset"]
                         + [f"Requires-Dist: {line}" for line in requires]) + "\n"
    files = [
        (f"{dist_info}/METADATA", metadata.encode("utf-8"), False),
        (f"{dist_info}/WHEEL", b"Wheel-Version: 1.0\nGenerator: imtypeset tools/wheel/dependent_check.py\n"
                               b"Root-Is-Purelib: true\nTag: py3-none-any\n", False),
        (f"{dist_info}/RECORD", b"", False),
    ]
    path = os.path.join(directory, f"{name}-0.0.0-py3-none-any.whl")
    build_wheel.write_wheel(path, files)
    return path


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--out", default="")
    parser.add_argument("--imtypeset", default="")
    parser.add_argument("--bundle", default="1.92.900")
    args = parser.parse_args(argv)
    from packaging.markers import Marker
    from packaging.requirements import Requirement

    version = args.imtypeset or build_wheel.distribution_version(
        build_wheel.imtypeset_version.version(), args.bundle, False)
    requires = lines(version, args.bundle)
    print("\n".join(requires))
    marker = Requirement(requires[1]).marker
    assert str(marker) == str(Marker(MARKER))
    failed = 0
    for what, platform, machine, python, wheel in ENVIRONMENTS:
        got = marker.evaluate({"sys_platform": platform, "platform_machine": machine,
                               "python_version": python, "python_full_version": python + ".0"})
        failed += got != wheel
        print(f"{'ok  ' if got == wheel else 'FAIL'} {what}: the marker is {got}, a wheel {'exists' if wheel else 'does not'}")
    if args.out:
        os.makedirs(args.out, exist_ok=True)
        print(write_dependent(args.out, "dependent_stand_in", requires))
        print(write_dependent(args.out, "dependent_only_imtypeset", requires[1:]))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
