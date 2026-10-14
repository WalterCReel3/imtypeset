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
                     in its version (.dev0+standin), its metadata, its
                     attestation and host_info(), and --release refuses to
                     build one.
  --work             scratch, default build/py-dist/<python tag>-<bundle version>.
  --platform-tag     auto (default): on Linux, the tag MEASURED from the
                     library that was just built (`platform_tag`): the glibc
                     its versioned symbols need, never below 2.28 (the
                     imgui-bundle wheel's own floor), and plain linux_<arch>
                     when it needs anything a manylinux tag does not promise;
                     on macOS, macosx_<target>_<arch>. A tag given by hand is
                     held to the same measurement and refused if it claims
                     more.
  --release          refuse stand-in headers, headers whose checksums
                     python/host/bundle_pins.toml does not record, and
                     anything Dear ImGui's own layout check did not accept.

It runs under any Python 3.11 or later with nothing installed, and writes the
wheel itself (a zip, by the wheel specification): there is no build backend to
download. pyproject.toml's backend (tools/wheel/imtypeset_backend.py) calls build() here.

WHAT A WHEEL IS, and why (docs/sidecar.md, section 8):

  name       imtypeset
  version    <library version>.<bundle line>, the line being the imgui-bundle
             version as one number (1.92.900 is 192900). One version line per
             supported imgui-bundle, so that a resolver given `imgui-bundle==X`
             and `imtypeset==0.3.0.*` settles on the wheel built for X. A
             build against stand-in headers is <that>.dev0+standin: a
             development release, which every installer orders BELOW the
             release of the same line and does not pick while a release that
             fits exists.
  tag        cp<XY>-none-<platform>: the CPython version, because the
             attestation is of that CPython's imgui_bundle module (and, on
             Linux, the library names that module's file); no CPython ABI,
             because the library is loaded with ctypes and uses none.
  requires   imgui-bundle==<exactly the version it was built for>, and Python
             in the release's one range (tools/ci/wheel_matrix.requires_python:
             the same in every file, because an index keeps one per release;
             the tag, not this, is what limits a file to its CPython)
  holds      imtypeset/*.py, imtypeset/libimtypeset_bundle.{so,dylib},
             imtypeset/_attestation.json, and in <dist-info>/licenses the
             licence (LICENSE, MIT) and the notices. Nothing else.
  describes  itself with python/README-pypi.md: the project's page on PyPI.

The macOS branches (the platform tag, the absence of stripping) ran on GitHub's
macos-14 runners on 2026-10-12 and nowhere else.
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
sys.path.insert(0, os.path.join(REPO, "tools", "ci"))
import wheel_matrix  # noqa: E402  (tools/ci/wheel_matrix.py: the release's CPythons)

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


STAND_IN_SUFFIX = ".dev0+standin"


def distribution_version(library_version: str, wheel_version: str, stand_in: bool) -> str:
    """A stand-in build is a DEVELOPMENT release of its line (PEP 440's
    `.devN`), with the local label saying which kind. `.dev0` orders it below
    the release, where a local label alone would order it above."""
    return f"{library_version}.{bundle_line(wheel_version)}" + (STAND_IN_SUFFIX if stand_in else "")


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


# The libraries a manylinux wheel may name as needed and expect the system to
# have, as far as this library can need them. Written from the manylinux
# policy as remembered (PEP 600 and auditwheel's table), not read here.
MANYLINUX_SYSTEM_LIBRARIES = {
    "libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0", "librt.so.1", "libgcc_s.so.1",
    "libstdc++.so.6", "ld-linux-x86-64.so.2", "ld-linux-aarch64.so.1",
}


def needed_libraries(library: str) -> list:
    """The DT_NEEDED entries of an ELF file (from `objdump -p`)."""
    return re.findall(r"^\s*NEEDED\s+(\S+)", run(["objdump", "-p", library], what="objdump -p"), re.M)


def measured_linux_tag(machine: str, library: str, module: str = ""):
    """(the platform tag THIS FILE supports, what that rests on): measured
    from the library, never asserted. `manylinux_2_<n>_<machine>` with n the
    glibc its versioned symbols need and at least 28, when its libstdc++ and
    libgcc symbols fit what manylinux_2_28 allows and it names no needed
    library but the system's and `module` (the wheel's extension module,
    which the distribution's pin supplies); plain `linux_<machine>`
    otherwise. auditwheel is not run: it would refuse `module` as a library
    from outside."""
    plain = "linux_" + machine
    newest = symbol_versions(library)
    said = ", ".join(f"{family}_{'.'.join(map(str, number))}" for family, number in sorted(newest.items()))
    glibc = newest.get("GLIBC", (2, 0))
    over = [family for family, limit in MANYLINUX_2_28_LIMITS.items() if newest.get(family, (0,)) > limit]
    foreign = [name for name in needed_libraries(library)
               if name not in MANYLINUX_SYSTEM_LIBRARIES and name != module]
    if glibc[0] != 2 or over or foreign:
        why = (f"needs {said}, past what manylinux_2_28 allows of {over}" if over or glibc[0] != 2
               else f"names needed libraries a manylinux tag does not promise: {foreign}")
        return plain, f"measured with objdump: the library {why}"
    return (f"manylinux_2_{max(28, glibc[1])}_{machine}",
            f"measured with objdump: the newest versioned symbols the library needs are {said}, and it "
            f"needs no library but the system's and {module or 'none'}; the floor is 2.28, the "
            "imgui-bundle wheel's own tag")


def manylinux_level(tag: str):
    found = re.fullmatch(r"manylinux_2_(\d+)_\w+", tag)
    return int(found.group(1)) if found else None


def platform_tag(asked: str, facts: dict, library: str):
    """(the tag, what it rests on)."""
    if facts["system"] == "Darwin":
        if asked != "auto":
            return asked, "given on the command line"
        target = os.environ.get("MACOSX_DEPLOYMENT_TARGET", "14.0")
        major, _, minor = target.partition(".")
        return (f"macosx_{major}_{minor or '0'}_{facts['machine']}",
                f"MACOSX_DEPLOYMENT_TARGET {target}, the build script's")
    if facts["system"] != "Linux":
        raise Failed(f"no platform tag for {facts['system']}")
    measured, basis = measured_linux_tag(facts["machine"], library, os.path.basename(facts["module"]))
    if asked == "auto" or asked == measured:
        return measured, basis
    # A tag given by hand may claim less than was measured, never more.
    claimed, supported = manylinux_level(asked), manylinux_level(measured)
    if asked == "linux_" + facts["machine"] or (claimed and supported and claimed >= supported):
        return asked, f"given on the command line; {basis}"
    raise Failed(f"--platform-tag {asked} claims more than the library supports ({measured}: {basis})")


def record_hash(data: bytes) -> str:
    return "sha256=" + base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b"=").decode("ascii")


README = os.path.join(REPO, "python", "README-pypi.md")

# What PyPI's page says of the systems, beside the wheel's own tag.
SYSTEM_CLASSIFIER = {
    "Linux": "Operating System :: POSIX :: Linux",
    "Darwin": "Operating System :: MacOS",
}


def metadata(version: str, facts: dict, attestation: dict) -> str:
    """METADATA, version 2.4: the version that has License-Expression and
    License-File (PEP 639), the files being in <dist-info>/licenses/. 2.4
    forbids `License` beside `License-Expression` and deprecates licence
    classifiers, so there is neither. The description is python/README-pypi.md
    (what the project's page on PyPI shows), then what THIS file was built
    for."""
    stand_in = attestation["stand_in"]
    with open(README, encoding="utf-8") as handle:
        readme = handle.read().rstrip("\n")
    lines = [
        "Metadata-Version: 2.4",
        "Name: imtypeset",
        f"Version: {version}",
        f"Summary: Typeset text for Dear ImGui, on TeX's box-and-glue model: the build for imgui-bundle {facts['wheel_version']}"
        + (" (STAND-IN HEADERS: a development build, not a release)" if stand_in else ""),
        "Author: Walter Reel",
        "License-Expression: MIT",
        "License-File: LICENSE",
        "License-File: THIRD-PARTY.md",
        "License-File: NOTICE.txt",
        "Keywords: imgui,dear-imgui,imgui-bundle,typesetting,text-layout,tex,math"
        + (",stand-in-headers" if stand_in else ""),
        "Classifier: Development Status :: 3 - Alpha",
        "Classifier: Intended Audience :: Developers",
        # The systems and CPythons of the whole release, the same in every file:
        # an index keeps one set of classifiers per release, as it keeps one
        # Requires-Python (below). Which system and CPython a file is for is its tag.
        *(f"Classifier: {classifier}" for classifier in sorted(SYSTEM_CLASSIFIER.values())),
        "Classifier: Programming Language :: C++",
        "Classifier: Programming Language :: Python :: 3",
        "Classifier: Programming Language :: Python :: 3 :: Only",
        *(f"Classifier: Programming Language :: Python :: {python}"
          for python in wheel_matrix.sidecar_matrix.PYTHONS),
        "Classifier: Programming Language :: Python :: Implementation :: CPython",
        "Classifier: Topic :: Software Development :: User Interfaces",
        "Classifier: Topic :: Text Processing :: Markup :: LaTeX",
        "Classifier: Topic :: Text Processing :: Fonts",
        f"Project-URL: Repository, {SOURCE_URL}",
        f"Project-URL: Changelog, {SOURCE_URL}/blob/main/docs/CHANGELOG.md",
        f"Project-URL: Documentation, {SOURCE_URL}/blob/main/docs/sidecar.md",
        f"Project-URL: Issues, {SOURCE_URL}/issues",
        # One value for every file of the release: an index keeps it per release and an
        # installer filters every file by it (docs/releasing.md, section 6).
        f"Requires-Python: {wheel_matrix.requires_python()}",
        f"Requires-Dist: imgui-bundle=={facts['wheel_version']}",
        "Description-Content-Type: text/markdown; charset=UTF-8; variant=GFM",
        "",
    ]
    if stand_in:
        lines += [
            "> **THIS FILE WAS BUILT AGAINST STAND-IN HEADERS** (its version ends `.dev0+standin`):",
            "> upstream Dear ImGui's, not the headers of the fork the imgui-bundle wheel is built",
            "> from. Dear ImGui's own layout check refuses it, and it is accepted on imtypeset's",
            "> field probe alone. It is a development build, and no index holds one.",
            "",
        ]
    lines += [
        readme,
        "",
        "## This file",
        "",
        "A release has one file per platform and CPython, and a page shows this section from one",
        "of them.",
        "",
        f"- imtypeset {attestation['imtypeset_version']}, built for `imgui-bundle=={facts['wheel_version']}` "
        f"(Dear ImGui {facts['imgui_version']}), CPython {facts['python']}, {facts['system']} {facts['machine']}.",
        f"- Dear ImGui headers: `{attestation['headers']}`.",
        f"- Checked: `{attestation['accepted_by']}`, {len(attestation['probes'])} probe rows, "
        f"on {attestation['attested_on']} ({attestation['attested_where'] or 'not said where'}).",
    ]
    return "\n".join(lines) + "\n"


def notice(version: str, facts: dict, attestation: dict) -> str:
    return f"""imtypeset {version}

What is inside imtypeset/{attestation['library']['file']}, and what is not.

Inside, compiled from this project's sources ({SOURCE_URL}):
  - imtypeset itself (MIT: LICENSE, beside this file);
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
    if facts["python"] not in wheel_matrix.sidecar_matrix.PYTHONS:
        raise Failed(f"CPython {facts['python']} is not in the release's matrix "
                     f"{list(wheel_matrix.sidecar_matrix.PYTHONS)} (tools/ci/sidecar_matrix.py): every "
                     f"wheel declares Requires-Python {wheel_matrix.requires_python()}, which would "
                     "exclude this one; add the CPython there first")
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
        # A release is built only from headers whose checksums the pins record.
        "IMTYPESET_HEADERS_REQUIRE_RECORDED": "1" if release else "",
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
    for name in ("LICENSE", "THIRD-PARTY.md"):
        with open(os.path.join(REPO, name), "rb") as handle:
            files.append((f"{dist_info}/licenses/{name}", handle.read(), False))
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
