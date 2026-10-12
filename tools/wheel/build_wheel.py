"""Builds the imtypeset wheel for one interpreter's imgui-bundle: the Python
package with the sidecar library inside it, pinned to that imgui-bundle, and
the attestation `import imtypeset` checks the pair against.

    python3 -P -B tools/wheel/build_wheel.py --python <interpreter> --out <directory>
        [--headers fork|stand-in] [--work <directory>] [--bundle-checkout <clone>]
        [--platform-tag auto|<tag>] [--attested-where <text>] [--release]

  --python           the interpreter whose installed imgui_bundle the wheel is for
                     (default: the one running this script). Nothing is installed
                     into it and nothing of it is written.
  --out              where the wheel is written.
  --headers          fork (default): the wheel's own Dear ImGui headers, fetched
                     (python/host/fetch_headers.py; needs the network once).
                     stand-in: upstream's, by name. A wheel built that way says so
                     in its version (+standin), its metadata, its attestation and
                     host_info(), and --release refuses to build one.
  --work             scratch, default build/py-dist/<python tag>-<bundle version>.
  --platform-tag     auto (default): on Linux, the manylinux tag the library's
                     versioned symbols allow, never below manylinux_2_28 (the
                     imgui-bundle wheel's own); on macOS, macosx_<target>_<arch>.
  --release          refuse stand-in headers and anything Dear ImGui's own layout
                     check did not accept.

It runs under any Python 3.11 or later with nothing installed, and writes the
wheel itself (a zip, by the wheel specification): there is no build backend to
download. pyproject.toml's backend (tools/wheel/imtypeset_backend.py) calls build() here.

WHAT A WHEEL IS, and why (docs/sidecar.md, section 8):

  name       imtypeset
  version    <library version>.<bundle line>[+standin], the line being the
             imgui-bundle version as one number (1.92.900 is 192900). One
             version line per supported imgui-bundle, so that a resolver given
             `imgui-bundle==X` and `imtypeset` settles on the wheel built for X.
  tag        cp<XY>-none-<platform>: the CPython version, because the
             attestation is of that CPython's imgui_bundle module (and, on
             Linux, the library names that module's file); no CPython ABI,
             because the library is loaded with ctypes and uses none.
  requires   imgui-bundle==<exactly the version it was built for>
  holds      imtypeset/*.py, imtypeset/libimtypeset_bundle.{so,dylib},
             imtypeset/_attestation.json, and the notices. Nothing else.

THE macOS BRANCHES HAVE NEVER RUN (the platform tag, the absence of stripping).
"""

import argparse
import base64
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tools"))
import version as imtypeset_version  # noqa: E402  (tools/version.py)

PACKAGE = os.path.join(REPO, "python", "imtypeset")
MODULES = ("__init__.py", "_bundle.py", "_capi.py", "demo.py")
SOURCE_URL = "https://github.com/WalterCReel3/imtypeset"

# What a manylinux_2_28 wheel may ask of the system's C++ runtime, beside
# glibc 2.28. Written from the manylinux policy as remembered (PEP 600 and
# auditwheel's table), not read here.
MANYLINUX_2_28_LIMITS = {"GLIBCXX": (3, 4, 24), "CXXABI": (1, 3, 11), "GCC": (8, 0, 0)}


class Failed(Exception):
    pass


def bundle_line(wheel_version: str) -> str:
    """1.92.900 -> "192900": the imgui-bundle version as one release segment
    that orders as the versions do."""
    found = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", wheel_version)
    if not found:
        raise Failed(f"imgui-bundle version {wheel_version!r} is not N.N.N: no version line for it")
    major, minor, patch = (int(part) for part in found.groups())
    if minor > 99 or patch > 999:
        raise Failed(f"imgui-bundle {wheel_version}: the version line holds two digits of minor "
                     "and three of patch")
    return str(major * 100000 + minor * 1000 + patch)


def distribution_version(library_version: str, wheel_version: str, stand_in: bool) -> str:
    return f"{library_version}.{bundle_line(wheel_version)}" + ("+standin" if stand_in else "")


def clean_environment(extra=None) -> dict:
    """The environment for a child: nothing that points a Python at another
    package, at a library, or at a build front end's overlay."""
    environment = {
        name: value for name, value in os.environ.items()
        if not name.startswith("IMTYPESET_") and name not in ("PYTHONPATH", "PYTHONHOME", "PYTHONSTARTUP")
    }
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    environment.update(extra or {})
    return environment


def run(command, environment=None, what="") -> str:
    done = subprocess.run(command, capture_output=True, text=True, env=environment or clean_environment())
    if done.returncode != 0:
        raise Failed(f"{what or command[0]} ended with status {done.returncode}:\n"
                     + (done.stdout + done.stderr).strip()[-4000:])
    return done.stdout


_FACTS = """
import json, platform, sys, sysconfig
import imgui_bundle
from imgui_bundle import imgui
print(json.dumps({
    "wheel_version": imgui_bundle.__version__,
    "imgui_version": imgui.get_version(),
    "module": imgui_bundle._imgui_bundle.__file__,
    "python_tag": "cp%d%d" % sys.version_info[:2],
    "python": "%d.%d" % sys.version_info[:2],
    "implementation": sys.implementation.name,
    "system": platform.system(),
    "machine": platform.machine(),
    "sysconfig_platform": sysconfig.get_platform(),
}))
"""


def target_facts(python: str) -> dict:
    facts = json.loads(run([python, "-P", "-B", "-c", _FACTS], what=f"{python} (import imgui_bundle)"))
    if facts["implementation"] != "cpython":
        raise Failed(f"{python} is {facts['implementation']}; imgui-bundle's wheels are CPython's")
    return facts


def symbol_versions(library: str) -> dict:
    """{"GLIBC": (2, 14), ...}: the newest version of each versioned symbol set
    the library asks the system for (ELF; from `objdump -T`)."""
    listing = run(["objdump", "-T", library], what="objdump -T")
    newest = {}
    for family, text in re.findall(r"\b(GLIBC|GLIBCXX|CXXABI|GCC)_(\d+(?:\.\d+)*)\b", listing):
        number = tuple(int(part) for part in text.split("."))
        if number > newest.get(family, ()):
            newest[family] = number
    return newest


def platform_tag(asked: str, facts: dict, library: str):
    """(the tag, what it rests on)."""
    if asked != "auto":
        return asked, "given on the command line"
    if facts["system"] == "Darwin":
        target = os.environ.get("MACOSX_DEPLOYMENT_TARGET", "14.0")
        major, _, minor = target.partition(".")
        return (f"macosx_{major}_{minor or '0'}_{facts['machine']}",
                f"MACOSX_DEPLOYMENT_TARGET {target}, the build script's")
    if facts["system"] != "Linux":
        raise Failed(f"no platform tag for {facts['system']}")
    plain = "linux_" + facts["machine"]
    newest = symbol_versions(library)
    said = ", ".join(f"{family}_{'.'.join(map(str, number))}" for family, number in sorted(newest.items()))
    glibc = newest.get("GLIBC", (2, 0))
    over = [family for family, limit in MANYLINUX_2_28_LIMITS.items() if newest.get(family, (0,)) > limit]
    if glibc[0] != 2 or over:
        return plain, f"the library needs {said}, past what a manylinux tag allows ({over})"
    return (f"manylinux_2_{max(28, glibc[1])}_{facts['machine']}",
            f"the newest versioned symbols the library needs are {said}; the floor is 2.28, the "
            "imgui-bundle wheel's own tag")


def record_hash(data: bytes) -> str:
    return "sha256=" + base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b"=").decode("ascii")


def metadata(version: str, facts: dict, attestation: dict) -> str:
    stand_in = attestation["stand_in"]
    lines = [
        "Metadata-Version: 2.1",
        "Name: imtypeset",
        f"Version: {version}",
        f"Summary: Typeset text for Dear ImGui: the sidecar library for imgui-bundle {facts['wheel_version']}"
        + (" (STAND-IN HEADERS: a development build, not a release)" if stand_in else ""),
        f"Project-URL: Source, {SOURCE_URL}",
        f"Requires-Python: =={facts['python']}.*",
        f"Requires-Dist: imgui-bundle=={facts['wheel_version']}",
        "License-File: THIRD-PARTY.md",
        "License-File: NOTICE.txt",
        "Keywords: imgui,typesetting" + (",stand-in-headers" if stand_in else ""),
        "Description-Content-Type: text/plain",
        "",
        f"imtypeset {attestation['imtypeset_version']}, as a shared library loaded with ctypes beside an",
        f"unmodified imgui-bundle {facts['wheel_version']} wheel, and the Python package over its C ABI.",
        "",
        f"Built for: imgui-bundle=={facts['wheel_version']} (Dear ImGui {facts['imgui_version']}), "
        f"CPython {facts['python']}, {facts['system']} {facts['machine']}.",
        f"Headers:   {attestation['headers']}",
        f"Checked:   {attestation['accepted_by']}, {len(attestation['probes'])} probe rows, "
        f"on {attestation['attested_on']} ({attestation['attested_where'] or 'not said where'}).",
        "",
        "`import imtypeset` refuses any other imgui-bundle with a sentence (an ImportError).",
        "No licence has been chosen for imtypeset yet; the notices of what it derives from",
        "are in this distribution's licenses directory.",
    ]
    if stand_in:
        lines += [
            "",
            "THIS WHEEL WAS BUILT AGAINST STAND-IN HEADERS: upstream Dear ImGui's, not the headers",
            "of the fork the imgui-bundle wheel is built from. Dear ImGui's own layout check refuses",
            "it, and it is accepted on imtypeset's field probe alone. It is a development build.",
        ]
    return "\n".join(lines) + "\n"


def notice(version: str, facts: dict, attestation: dict) -> str:
    return f"""imtypeset {version}

What is inside imtypeset/{attestation['library']['file']}, and what is not.

Inside, compiled from this project's sources ({SOURCE_URL}):
  - imtypeset itself. No licence has been chosen for it yet.
  - routines in its OpenType reader that follow fontTools (MIT);
  - range tables generated from the Unicode Character Database (Unicode licence);
  - drawing code that reproduces Dear ImGui's vertex layout and clip tests (MIT);
  - inline functions of the Dear ImGui headers it was compiled against (MIT):
    {attestation['headers']}.
THIRD-PARTY.md, beside this file, has each notice in full.

Not inside: Dear ImGui, FreeType, SDL, or any font. The library's Dear ImGui
symbols are left undefined and are bound, when it is loaded, to the extension
module of the installed imgui-bundle {facts['wheel_version']} wheel, which carries its own
notices. The C++ runtime (libstdc++ or libc++) is the system's, linked dynamically.
"""


def write_wheel(path: str, files: list) -> None:
    """`files`: (name in the archive, bytes, executable). Sorted, with one fixed
    date, so that the same inputs give the same archive."""
    files = sorted(files)
    record_name = next(name for name, _, _ in files if name.endswith(".dist-info/RECORD"))
    rows = [f"{name},{record_hash(data)},{len(data)}" for name, data, _ in files if name != record_name]
    rows.append(f"{record_name},,")
    record = ("\n".join(rows) + "\n").encode("utf-8")
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, data, executable in files:
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (0o100755 if executable else 0o100644) << 16
            archive.writestr(info, record if name == record_name else data)


def build(python: str, out: str, headers: str = "fork", work: str = "", bundle_checkout: str = "",
          platform: str = "auto", attested_where: str = "", release: bool = False,
          log=print) -> str:
    if release and headers != "fork":
        raise Failed("--release builds against the wheel's own headers only; stand-in headers are "
                     "for development and are not published")
    python = os.path.abspath(python) if os.sep in python else (shutil.which(python) or python)
    facts = target_facts(python)
    library_version = imtypeset_version.version()
    copies = imtypeset_version.problems()
    if copies:
        raise Failed("the version is not one number:\n  " + "\n  ".join(copies))
    version = distribution_version(library_version, facts["wheel_version"], headers == "stand-in")
    work = os.path.abspath(work or os.path.join(REPO, "build", "py-dist",
                                               f"{facts['python_tag']}-{facts['wheel_version']}"))
    stage = os.path.join(work, "stage")
    lib_out = os.path.join(work, "library")
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(os.path.join(stage, "imtypeset"))
    os.makedirs(out, exist_ok=True)
    log(f"imtypeset {library_version} for imgui-bundle {facts['wheel_version']} "
        f"({facts['python_tag']}, {facts['system']} {facts['machine']}): distribution {version}")

    # 1. The library, by the same script a development build uses, with a run
    #    path that leads from the installed package to the installed wheel.
    #    Its own gate step is left out: a library with that run path loads
    #    where it is installed, and step 3 runs the probe there.
    environment = clean_environment({
        "IMTYPESET_BUNDLE_OUT": lib_out,
        "IMTYPESET_BUNDLE_HEADERS": headers,
        "IMTYPESET_BUNDLE_STEPS": "fetch compile link audit",
        "IMTYPESET_HEADER_CACHE": os.environ.get("IMTYPESET_HEADER_CACHE", ""),
        "IMTYPESET_IMGUI_FORK_SOURCE": os.environ.get("IMTYPESET_IMGUI_FORK_SOURCE", ""),
        "IMTYPESET_IMGUI_CLONE": os.environ.get("IMTYPESET_IMGUI_CLONE", ""),
        "IMTYPESET_BUNDLE_JOBS": os.environ.get("IMTYPESET_BUNDLE_JOBS", "4"),
    })
    if facts["system"] == "Linux":
        environment["IMTYPESET_BUNDLE_RPATH"] = "$ORIGIN/../imgui_bundle"
    environment = {name: value for name, value in environment.items() if value != ""}
    script = os.path.join(REPO, "python", "host", "build_bundle_library.sh")
    built = subprocess.run(["sh", script, bundle_checkout or os.path.join(REPO, "..", "imgui_bundle"), python],
                           env=environment, capture_output=True, text=True)
    with open(os.path.join(work, "build.log"), "w", encoding="utf-8") as handle:
        handle.write(built.stdout + built.stderr)
    if built.returncode != 0:
        raise Failed(f"build_bundle_library.sh ended with status {built.returncode} "
                     f"(log: {os.path.join(work, 'build.log')}):\n" + (built.stdout + built.stderr).strip()[-3000:])
    with open(os.path.join(lib_out, "LIBRARY"), encoding="utf-8") as handle:
        built_library = handle.read().strip()
    with open(os.path.join(lib_out, "BUILD"), encoding="utf-8") as handle:
        build_facts = dict(line.rstrip("\n").split("=", 1) for line in handle if "=" in line)

    # 2. The package as it will be installed: the modules, the library beside
    #    them. On Linux the debugging sections are taken off (the linker's
    #    build-id, which the attestation records, is not among them). On macOS
    #    the file is left as linked: taking anything off a signed image there
    #    unsigns it.
    for module in MODULES:
        shutil.copyfile(os.path.join(PACKAGE, module), os.path.join(stage, "imtypeset", module))
    library = os.path.join(stage, "imtypeset", os.path.basename(built_library))
    shutil.copyfile(built_library, library)
    os.chmod(library, 0o755)
    if facts["system"] == "Linux":
        run(["strip", "--strip-debug", library], what="strip --strip-debug")
        # The wheel as a neighbour, where the run path looks for it.
        os.symlink(os.path.dirname(facts["module"]), os.path.join(stage, "imgui_bundle"))

    # 3. The full probe, in a child of the target interpreter, on the staged
    #    library; accepted, it is written down as the package's attestation.
    tag, tag_basis = platform_tag(platform, facts, library)
    attestation_path = os.path.join(stage, "imtypeset", "_attestation.json")
    extras = {
        "imtypeset_version": library_version,
        "distribution_version": version,
        "bind": build_facts.get("bind", ""),
        "run_path": environment.get("IMTYPESET_BUNDLE_RPATH", ""),
        "compiler": build_facts.get("compiler", ""),
        "python_tag": facts["python_tag"],
        "platform_tag": tag,
        "platform_tag_basis": tag_basis,
        "attested_where": attested_where or "a local build",
    }
    extras_path = os.path.join(work, "attestation-extras.json")
    with open(extras_path, "w", encoding="utf-8") as handle:
        json.dump(extras, handle)
    log(run([python, "-P", "-B", os.path.join(REPO, "tools", "bundle_route", "attest.py"),
             library, attestation_path, extras_path], what="the full probe (attest.py)").strip())
    with open(attestation_path, encoding="utf-8") as handle:
        attestation = json.load(handle)
    if attestation["imtypeset_version"] != library_version:
        raise Failed(f"the library says it is {attestation['imtypeset_version']!r} and the header "
                     f"says {library_version!r}")
    if release and (attestation["stand_in"] or attestation["accepted_by"] != "imgui-check+probe"):
        raise Failed(f"--release: headers {attestation['headers']}, accepted by "
                     f"{attestation['accepted_by']}; a release is accepted by Dear ImGui's own check")

    # 4. The staged package, imported as an application would: no variable.
    said = run([python, "-P", "-B", "-c",
                "import sys; sys.path.insert(0, sys.argv[1]); import imtypeset, json; "
                "print(json.dumps(imtypeset.host_info()))", stage],
               what="import imtypeset from the staged package")
    info = json.loads(said.strip().splitlines()[-1])
    if info["route"] != "installed" or info["bundle"]["checked_in"] != "attestation":
        raise Failed(f"the staged package was not accepted on its attestation: {info}")

    # 5. The archive.
    dist_info = f"imtypeset-{version}.dist-info"
    files = []
    for name in MODULES + (os.path.basename(library), "_attestation.json"):
        with open(os.path.join(stage, "imtypeset", name), "rb") as handle:
            files.append((f"imtypeset/{name}", handle.read(), name == os.path.basename(library)))
    with open(os.path.join(REPO, "THIRD-PARTY.md"), "rb") as handle:
        files.append((f"{dist_info}/licenses/THIRD-PARTY.md", handle.read(), False))
    files.append((f"{dist_info}/licenses/NOTICE.txt", notice(version, facts, attestation).encode("utf-8"), False))
    files.append((f"{dist_info}/METADATA", metadata(version, facts, attestation).encode("utf-8"), False))
    files.append((f"{dist_info}/WHEEL", (
        "Wheel-Version: 1.0\n"
        "Generator: imtypeset tools/wheel/build_wheel.py\n"
        "Root-Is-Purelib: false\n"
        f"Tag: {facts['python_tag']}-none-{tag}\n").encode("utf-8"), False))
    files.append((f"{dist_info}/RECORD", b"", False))
    wheel = os.path.join(os.path.abspath(out), f"imtypeset-{version}-{facts['python_tag']}-none-{tag}.whl")
    write_wheel(wheel, files)
    log(f"platform tag {tag}: {tag_basis}")
    log(f"wrote {wheel} ({os.path.getsize(wheel)} bytes, {len(files)} files)")
    return wheel


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--python", default=sys.executable)
    parser.add_argument("--out", required=True)
    parser.add_argument("--headers", choices=("fork", "stand-in"), default="fork")
    parser.add_argument("--work", default="")
    parser.add_argument("--bundle-checkout", default="")
    parser.add_argument("--platform-tag", default="auto")
    parser.add_argument("--attested-where", default="")
    parser.add_argument("--release", action="store_true")
    args = parser.parse_args(argv)
    try:
        build(args.python, args.out, args.headers, args.work, args.bundle_checkout,
              args.platform_tag, args.attested_where, args.release)
    except Failed as failure:
        print(f"build_wheel.py: {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
