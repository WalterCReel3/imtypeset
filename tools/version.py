"""The version of imtypeset, from the one place that states it, and a check
that every copy agrees.

    python3 -I -B tools/version.py            prints it, e.g. 0.3.0
    python3 -I -B tools/version.py --check    exit 1, naming the file, if a copy differs
    python3 -I -B tools/version.py --check --tag v0.3.0
                                              also: the tag is "v" + the version, and
                                              docs/CHANGELOG.md has a section for it
    python3 -I -B tools/version.py --notes    that section of docs/CHANGELOG.md

THE ONE PLACE is `IMTYPESET_VERSION` in include/imtypeset.h. CMake reads it
there (CMakeLists.txt), tools/amalgamate.py reads it there, and
tools/wheel/build_wheel.py reads it through this file for the wheel's version.
The copies, each of which something refuses to build or to load when it
differs, and all of which this check reads:

  include/imtypeset.h       IMTYPESET_VERSION_NUM (XYYZZ)
  include/imtypeset_c.h     IMTYPESET_C_VERSION, _NUM   (static_assert in src/imtypeset_c.cpp)
  python/imtypeset/_capi.py C_VERSION   (the library refuses the package at import)
  vcpkg.json, ports/imtypeset/vcpkg.json
  python/README-pypi.md     the versions in its examples (the page on PyPI)
"""

import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _read(relative: str) -> str:
    with open(os.path.join(REPO, relative), encoding="utf-8") as handle:
        return handle.read()


def _find(relative: str, pattern: str) -> str:
    found = re.search(pattern, _read(relative), re.M)
    return found.group(1) if found else ""


def version() -> str:
    found = _find("include/imtypeset.h", r'^#define[ \t]+IMTYPESET_VERSION[ \t]+"(\d+\.\d+\.\d+)"')
    if not found:
        raise SystemExit("tools/version.py: include/imtypeset.h does not define IMTYPESET_VERSION")
    return found


def number(text: str) -> int:
    major, minor, patch = (int(part) for part in text.split("."))
    return major * 10000 + minor * 100 + patch


def problems(tag: str = "") -> list:
    want = version()
    found = []

    def copy(where, got, expected=want):
        if str(got) != str(expected):
            found.append(f"{where} is {got!r}; include/imtypeset.h says {expected!r}")

    copy("include/imtypeset.h IMTYPESET_VERSION_NUM",
         _find("include/imtypeset.h", r"^#define[ \t]+IMTYPESET_VERSION_NUM[ \t]+(\d+)"), number(want))
    copy("include/imtypeset_c.h IMTYPESET_C_VERSION",
         _find("include/imtypeset_c.h", r'^#define[ \t]+IMTYPESET_C_VERSION[ \t]+"([^"]*)"'))
    copy("include/imtypeset_c.h IMTYPESET_C_VERSION_NUM",
         _find("include/imtypeset_c.h", r"^#define[ \t]+IMTYPESET_C_VERSION_NUM[ \t]+(\d+)"), number(want))
    copy("python/imtypeset/_capi.py C_VERSION",
         _find("python/imtypeset/_capi.py", r'^C_VERSION = "([^"]*)"'))
    for manifest in ("vcpkg.json", "ports/imtypeset/vcpkg.json"):
        copy(manifest, json.loads(_read(manifest)).get("version"))
    # The project's page on PyPI shows versions as examples of what to pin.
    readme = _read("python/README-pypi.md")
    for got in sorted(set(re.findall(r"\b(\d+\.\d+\.\d+)\.19\d{4}\b", readme)
                          + re.findall(r"prints `(\d+\.\d+\.\d+) ", readme))):
        copy("python/README-pypi.md, a version in an example", got)
    if not re.search(r"imtypeset==" + re.escape(want) + r"\.19\d{4}\b", readme):
        found.append(f"python/README-pypi.md has no example pin `imtypeset=={want}.<line>`")
    if tag:
        if tag != "v" + want:
            found.append(f"the tag is {tag!r} and the version is {want!r}: the tag must be 'v{want}'")
        if not re.search(r"^## " + re.escape(want) + r"\b", _read("docs/CHANGELOG.md"), re.M):
            found.append(f"docs/CHANGELOG.md has no section '## {want}'")
    return found


def notes() -> str:
    """The section of docs/CHANGELOG.md for this version, without its heading."""
    found = re.search(r"^## " + re.escape(version()) + r"\b[^\n]*\n(.*?)(?=^## |\Z)",
                      _read("docs/CHANGELOG.md"), re.M | re.S)
    if not found:
        raise SystemExit(f"tools/version.py: docs/CHANGELOG.md has no section '## {version()}'")
    return found.group(1).strip() + "\n"


def main(argv) -> int:
    if "--notes" in argv:
        sys.stdout.write(notes())
        return 0
    if "--check" not in argv:
        print(version())
        return 0
    tag = argv[argv.index("--tag") + 1] if "--tag" in argv else ""
    found = problems(tag)
    for problem in found:
        print("tools/version.py: " + problem, file=sys.stderr)
    if not found:
        print(f"version {version()}: every copy agrees" + (f", and the tag {tag}" if tag else ""))
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
