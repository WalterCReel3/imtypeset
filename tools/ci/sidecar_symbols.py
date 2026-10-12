"""Which Dear ImGui symbols the sidecar library needs, and which of them an
installed imgui-bundle wheel's extension module exports.

    python3 -I -B tools/ci/sidecar_symbols.py exports <wheel-module>
    python3 -I -B tools/ci/sidecar_symbols.py needs   <library-or-directory-of-objects>
    python3 -I -B tools/ci/sidecar_symbols.py compare <wheel-module> <library-or-objects|--expected>
    python3 -I -B tools/ci/sidecar_symbols.py audit   <library> <wheel-module>
    python3 -I -B tools/ci/sidecar_symbols.py expected --write <library>

`audit` is what the build script runs after linking; it is the whole of the
check on a platform whose linker is told to leave symbols undefined:

  - every Dear ImGui symbol the library leaves undefined is exported by the
    wheel's module;
  - (Mach-O) every symbol left for lookup at load is one of Dear ImGui's:
    nothing else slipped through the linker's permission;
  - the library exports the C ABI (ImTypeset_*, ImTypesetBundle_*) and nothing
    else.

Exit 0 when all hold, 1 with the offending names otherwise. `compare` prints
the same comparison as a report and exits 0 whatever it finds, unless --strict.

Names are compared as the Itanium C++ ABI mangles them, without the one
leading underscore Mach-O adds and without an ELF symbol version. `nm` is the
only tool used: binutils' on Linux, Xcode's on macOS. THE macOS BRANCHES OF
THIS FILE HAVE NEVER RUN: the options (`nm -gU`, `nm -u`, `nm -m`) and the
shape of `nm -m` output are written from memory of Apple's manual page, and
every command's raw output is kept beside the parsed result so that a wrong
guess shows.

expected_imgui_symbols.txt, beside this file, is the list a Linux x86-64 build
against STAND-IN headers leaves undefined (written by `expected --write`). It
lets a report say what a wheel on another platform lacks before anything has
compiled there. It is not the truth for that platform: two of its names carry
a std::function, which libc++ mangles differently from libstdc++, and a build
against the fork's own headers may call other functions.
"""

from __future__ import annotations

import glob
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXPECTED = os.path.join(HERE, "expected_imgui_symbols.txt")
MACHO = sys.platform == "darwin"

# Dear ImGui's own: a function or member in namespace ImGui or of a class whose
# name begins with Im, a global function so named, or the context pointer.
# imtypeset's own namespace begins with Im as well and is not ImGui's.
_IMGUI = re.compile(r"^(_ZNK?\d+Im|_Z\d+Im|GIm)")
_OURS = re.compile(r"^_ZNK?9ImTypeset")
_C_ABI = re.compile(r"^ImTypeset(Bundle)?_\w+$")


def is_imgui(name: str) -> bool:
    return bool(_IMGUI.match(name)) and not _OURS.match(name)


def _nm(arguments: list, path: str) -> str:
    done = subprocess.run(["nm", *arguments, path], capture_output=True, text=True)
    if done.returncode != 0:
        raise RuntimeError(f"nm {' '.join(arguments)} {path}: {done.stderr.strip()}")
    return done.stdout


def _plain(name: str) -> str:
    if MACHO and name.startswith("_"):
        name = name[1:]
    return name.split("@", 1)[0]


def _names(text: str) -> set:
    """The last field of every line of `nm` output that has one."""
    found = set()
    for line in text.splitlines():
        fields = line.split()
        if fields and not line.rstrip().endswith(":"):
            found.add(_plain(fields[-1]))
    return found


def exports(module: str) -> tuple:
    """(names the shared object exports, nm's raw output)."""
    raw = _nm(["-gU"] if MACHO else ["-D", "--defined-only"], module)
    return _names(raw), raw


def _objects(path: str) -> list:
    return sorted(glob.glob(os.path.join(path, "*.o"))) if os.path.isdir(path) else []


def needs(path: str) -> tuple:
    """(names `path` leaves undefined, raw output). `path` is a linked library,
    or a directory of object files: then a name one object defines is not
    undefined for the set."""
    objects = _objects(path)
    if not objects:
        raw = _nm(["-u"] if MACHO else ["-D", "--undefined-only"], path)
        return _names(raw), raw
    undefined, defined, raws = set(), set(), []
    for one in objects:
        raw = _nm(["-u"] if MACHO else ["--undefined-only"], one)
        raws.append(f"--- {os.path.basename(one)}\n{raw}")
        undefined |= _names(raw)
        defined |= _names(_nm(["-U"] if MACHO else ["--defined-only"], one))
    return undefined - defined, "".join(raws)


def looked_up(library: str) -> tuple:
    """Mach-O only: (names bound by lookup at load and not to a named library,
    or None when `nm -m` did not read as expected; raw output)."""
    raw = _nm(["-m", "-u"], library)
    pattern = re.compile(r"\(undefined\).*?\s(\S+) \((.*)\)\s*$")
    parsed, dynamic = 0, set()
    for line in raw.splitlines():
        match = pattern.search(line)
        if match:
            parsed += 1
            if "dynamically looked up" in match.group(2):
                dynamic.add(_plain(match.group(1)))
    return (dynamic if parsed else None), raw


def read_expected() -> list:
    with open(EXPECTED, encoding="ascii") as handle:
        return [line.strip() for line in handle if line.strip() and not line.startswith("#")]


def comparison(module: str, needed_from: str | None) -> dict:
    """What `compare` prints, as data. `needed_from` None: the tracked list."""
    exported, _ = exports(module)
    if needed_from is None:
        needed, source = set(read_expected()), "expected_imgui_symbols.txt (Linux, stand-in headers)"
    else:
        needed, source = {n for n in needs(needed_from)[0] if is_imgui(n)}, needed_from
    return {
        "module": module,
        "needed_from": source,
        "exported": len(exported),
        "exported_imgui": sum(1 for n in exported if is_imgui(n)),
        "needed": sorted(needed),
        "missing": sorted(needed - exported),
    }


def audit(library: str, module: str) -> list:
    """The violations, each a sentence; empty when the library is sound."""
    problems = []
    exported, _ = exports(module)
    undefined, _ = needs(library)
    imgui = {n for n in undefined if is_imgui(n)}
    if not imgui:
        problems.append("the library leaves no Dear ImGui symbol undefined: it is not a sidecar")
    for name in sorted(imgui - exported):
        problems.append(f"needs {name}, which {os.path.basename(module)} does not export")
    if MACHO:
        dynamic, _ = looked_up(library)
        if dynamic is None:
            problems.append("`nm -m -u` did not read as expected: what is left for lookup at load "
                            "could not be listed")
        else:
            for name in sorted(n for n in dynamic if not is_imgui(n)):
                problems.append(f"leaves {name} for lookup at load, and it is not Dear ImGui's")
    ours, _ = exports(library)
    for name in sorted(n for n in ours if not _C_ABI.match(n)):
        problems.append(f"exports {name}, which is not the C ABI")
    if not any(n.startswith("ImTypesetBundle_") for n in ours):
        problems.append("exports no ImTypesetBundle_* function")
    return problems


def main(argv: list) -> int:
    if len(argv) >= 2 and argv[0] == "exports":
        print("\n".join(sorted(exports(argv[1])[0])))
        return 0
    if len(argv) >= 2 and argv[0] == "needs":
        print("\n".join(sorted(n for n in needs(argv[1])[0] if is_imgui(n))))
        return 0
    if len(argv) >= 3 and argv[0] == "compare":
        result = comparison(argv[1], None if argv[2] == "--expected" else argv[2])
        print(f"module:   {result['module']}")
        print(f"exports:  {result['exported']} symbols, {result['exported_imgui']} of them Dear ImGui's")
        print(f"needed:   {len(result['needed'])} Dear ImGui symbols, from {result['needed_from']}")
        print(f"missing:  {len(result['missing'])}")
        for name in result["missing"]:
            print("  " + name)
        return 1 if result["missing"] and "--strict" in argv else 0
    if len(argv) == 3 and argv[0] == "audit":
        problems = audit(argv[1], argv[2])
        for problem in problems:
            print("sidecar_symbols.py: " + problem, file=sys.stderr)
        if not problems:
            imgui = sum(1 for n in needs(argv[1])[0] if is_imgui(n))
            print(f"sidecar_symbols.py: {os.path.basename(argv[1])} needs {imgui} Dear ImGui "
                  f"symbols, all exported by {os.path.basename(argv[2])}; it exports "
                  f"{len(exports(argv[1])[0])} symbols, all the C ABI")
        return 1 if problems else 0
    if len(argv) == 3 and argv[:2] == ["expected", "--write"]:
        names = sorted(n for n in needs(argv[2])[0] if is_imgui(n))
        with open(EXPECTED, "w", encoding="ascii") as handle:
            handle.write(
                "# The Dear ImGui symbols a Linux x86-64 sidecar built against STAND-IN headers\n"
                "# leaves undefined, as the Itanium ABI mangles them with libstdc++. Written by\n"
                "#   python3 -I -B tools/ci/sidecar_symbols.py expected --write <library>\n"
                "# and read by tools/ci/sidecar_probe.py, which says what it is each time it\n"
                "# uses it. Not edited by hand.\n")
            handle.write("\n".join(names) + "\n")
        print(f"wrote {len(names)} names to {EXPECTED}")
        return 0
    print(__doc__, file=sys.stderr)
    return 64


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
