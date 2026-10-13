"""Reads imtypeset wheels as files, without installing them, and says whether
each is what tools/wheel/build_wheel.py promises; with --release, whether it
may be published.

    python3 -I -B tools/wheel/check_wheel.py [--release] [--tag v0.3.0] <wheel> [<wheel> ...]

For every wheel:
  - the archive holds exactly the package's modules, one library, the
    attestation, the notices and the three metadata files, and every file has
    the sha256 RECORD gives it;
  - the file name, METADATA, WHEEL and the attestation agree on the version,
    the CPython tag and the platform tag;
  - the version is <library version>.<bundle line>, the line being that of the
    one `Requires-Dist: imgui-bundle==X`, which is the wheel the attestation is of;
  - the library in the archive is the file the attestation is about (sha256,
    and the sha256 of what the loader maps of it);
  - a build against stand-in headers says so in its version (.dev0+standin,
    a development release, which orders below the release of its line), and
    nothing else does;
  - the metadata is version 2.4, says License-Expression: MIT, names the
    licence and the two notices as License-File (the licence being this
    repository's LICENSE), has the project's URLs and a markdown description;
  - on Linux, the platform tag claims no more than the library in the archive
    supports, measured here again with objdump (build_wheel.measured_linux_tag).

--release adds: no stand-in headers, accepted by Dear ImGui's own layout check
and not by the field probe alone, no development or local version segment, the
platform tag measured (so objdump must be there) and not plain linux_<arch>;
and --tag: the library version is the tag's. This is the step that keeps a development build
out of a release (.github/workflows/wheels.yml, tools/release.sh).

Exit 0 when every wheel passes.
"""

import base64
import hashlib
import importlib.util
import json
import os
import re
import shutil
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import build_wheel  # noqa: E402

MODULES = {"__init__.py", "_bundle.py", "_capi.py", "demo.py"}
LIBRARIES = {"libimtypeset_bundle.so", "libimtypeset_bundle.dylib"}


def _gate():
    spec = importlib.util.spec_from_file_location(
        "_bundle", os.path.join(REPO, "python", "imtypeset", "_bundle.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def bundle_line(wheel_version: str) -> str:
    major, minor, patch = (int(part) for part in wheel_version.split("."))
    return str(major * 100000 + minor * 1000 + patch)


def check(path: str, release: bool, tag: str, gate) -> list:
    problems = []
    name = os.path.basename(path)
    parts = name[:-len(".whl")].split("-") if name.endswith(".whl") else []
    if len(parts) != 5 or parts[0] != "imtypeset":
        return [f"the file name is not imtypeset-<version>-<python>-<abi>-<platform>.whl"]
    _, version, python_tag, abi_tag, platform_tag = parts
    try:
        archive = zipfile.ZipFile(path)
    except (OSError, zipfile.BadZipFile) as exc:
        return [f"not a zip archive: {exc}"]
    with archive:
        names = set(archive.namelist())
        dist_info = f"imtypeset-{version}.dist-info"
        libraries = sorted(LIBRARIES & {n.split("/", 1)[1] for n in names if n.startswith("imtypeset/")})
        if len(libraries) != 1:
            return [f"holds {len(libraries)} libraries, not one: {libraries}"]
        expected = {f"imtypeset/{m}" for m in MODULES} | {
            f"imtypeset/{libraries[0]}", "imtypeset/_attestation.json",
            f"{dist_info}/METADATA", f"{dist_info}/WHEEL", f"{dist_info}/RECORD",
            f"{dist_info}/licenses/LICENSE", f"{dist_info}/licenses/THIRD-PARTY.md",
            f"{dist_info}/licenses/NOTICE.txt",
        }
        if names != expected:
            problems.append(f"files: missing {sorted(expected - names)}, unexpected {sorted(names - expected)}")
            if expected - names:
                return problems
        recorded = {}
        for row in archive.read(f"{dist_info}/RECORD").decode("utf-8").splitlines():
            file_name, digest, _size = row.rsplit(",", 2)
            recorded[file_name] = digest
        for file_name in sorted(names - {f"{dist_info}/RECORD"}):
            digest = "sha256=" + base64.urlsafe_b64encode(
                hashlib.sha256(archive.read(file_name)).digest()).rstrip(b"=").decode("ascii")
            if recorded.get(file_name) != digest:
                problems.append(f"RECORD does not give {file_name} its sha256")
        metadata = archive.read(f"{dist_info}/METADATA").decode("utf-8")
        head = metadata.split("\n\n", 1)[0]
        fields = {}
        for line in head.splitlines():
            key, _, value = line.partition(": ")
            fields.setdefault(key, []).append(value)
        wheel_file = archive.read(f"{dist_info}/WHEEL").decode("utf-8")
        attestation = json.loads(archive.read("imtypeset/_attestation.json"))
        library_bytes = archive.read(f"imtypeset/{libraries[0]}")
        licence_bytes = archive.read(f"{dist_info}/licenses/LICENSE") if f"{dist_info}/licenses/LICENSE" in names else b""

    if fields.get("Name") != ["imtypeset"] or fields.get("Version") != [version]:
        problems.append(f"METADATA says {fields.get('Name')} {fields.get('Version')}; the file name says {version}")
    requires = fields.get("Requires-Dist", [])
    pin = re.fullmatch(r"imgui-bundle==(\d+\.\d+\.\d+)", requires[0]) if len(requires) == 1 else None
    if not pin:
        return problems + [f"Requires-Dist is not exactly one `imgui-bundle==X`: {requires}"]
    wheel_version = pin.group(1)
    if f"Tag: {python_tag}-{abi_tag}-{platform_tag}" not in wheel_file.splitlines():
        problems.append("WHEEL's Tag is not the file name's")
    if abi_tag != "none" or not re.fullmatch(r"cp3\d+", python_tag):
        problems.append(f"the tag is {python_tag}-{abi_tag}; expected cp3XY-none")
    if fields.get("Requires-Python") != [f"=={python_tag[2]}.{python_tag[3:]}.*"]:
        problems.append(f"Requires-Python {fields.get('Requires-Python')} is not the tag's CPython")

    stand_in = bool(attestation.get("stand_in"))
    library_version = str(attestation.get("imtypeset_version"))
    release_version = f"{library_version}.{bundle_line(wheel_version)}"
    suffix = version[len(release_version):] if version.startswith(release_version) else None
    if suffix is None:
        problems.append(f"version {version} does not begin {release_version} "
                        f"(library version, line of imgui-bundle {wheel_version})")
    elif suffix not in ("", build_wheel.STAND_IN_SUFFIX) or (suffix != "") != stand_in:
        problems.append(f"version {version} with stand_in {stand_in}: a stand-in build is "
                        f"{release_version}{build_wheel.STAND_IN_SUFFIX} and nothing else is")
    if fields.get("Metadata-Version") != ["2.4"]:
        problems.append(f"Metadata-Version {fields.get('Metadata-Version')}: License-Expression and "
                        "License-File need 2.4")
    if sorted(fields.get("License-File", [])) != ["LICENSE", "NOTICE.txt", "THIRD-PARTY.md"]:
        problems.append(f"License-File {fields.get('License-File')} is not the licence and the two notices")
    if fields.get("License-Expression") != ["MIT"]:
        problems.append(f"License-Expression is {fields.get('License-Expression')}, not MIT")
    # Metadata 2.4: `License` may not stand beside `License-Expression`, and
    # licence classifiers are deprecated in its favour.
    if "License" in fields or any(c.startswith("License ::") for c in fields.get("Classifier", [])):
        problems.append("the metadata has a `License` field or a licence classifier beside License-Expression")
    with open(os.path.join(REPO, "LICENSE"), "rb") as handle:
        if licence_bytes != handle.read():
            problems.append("licenses/LICENSE is not this repository's LICENSE")
    for label in ("Repository", "Changelog", "Documentation"):
        if not any(u.startswith(label + ", https://") for u in fields.get("Project-URL", [])):
            problems.append(f"no Project-URL labelled {label}")
    content_type = (fields.get("Description-Content-Type") or [""])[0]
    if not content_type.startswith("text/markdown") or "\n\n" not in metadata or not metadata.split("\n\n", 1)[1].strip():
        problems.append(f"no markdown description for the project's page ({content_type!r})")
    if stand_in != str(attestation.get("headers", "")).startswith("stand-in:"):
        problems.append("the attestation's stand_in and headers disagree")
    for key, want in (("distribution_version", version), ("wheel_version", wheel_version),
                      ("python_tag", python_tag), ("platform_tag", platform_tag)):
        if attestation.get(key) != want:
            problems.append(f"the attestation's {key} is {attestation.get(key)!r}, not {want!r}")
    if attestation.get("module", {}).get("file", "").find(python_tag.replace("cp", "cpython-")) < 0 \
            and attestation.get("platform", {}).get("system") == "Linux":
        problems.append(f"the attested module {attestation.get('module', {}).get('file')!r} is not {python_tag}'s")
    if not attestation.get("probes") or not all(row[1] for row in attestation["probes"]):
        problems.append("the attestation's probe rows are missing or not all passed")

    with tempfile.TemporaryDirectory() as scratch:
        copy = os.path.join(scratch, libraries[0])
        with open(copy, "wb") as handle:
            handle.write(library_bytes)
        now = gate.identity(copy)
        measured = None
        if platform_tag.startswith(("manylinux", "linux_")) and shutil.which("objdump"):
            machine = str(attestation.get("platform", {}).get("machine"))
            try:
                measured = build_wheel.measured_linux_tag(machine, copy, str(attestation.get("module", {}).get("file")))
            except build_wheel.Failed as failure:
                problems.append(f"the library's needs could not be measured: {failure}")
    if platform_tag.startswith(("manylinux", "linux_")):
        if measured is None:
            if release:
                problems.append("NOT A RELEASE: the platform tag was not measured (no objdump here)")
            else:
                print(f"note {name}: the platform tag was not measured (no objdump here)")
        else:
            claimed, supported = build_wheel.manylinux_level(platform_tag), build_wheel.manylinux_level(measured[0])
            if platform_tag != measured[0] and not platform_tag.startswith("linux_") and not (
                    claimed and supported and claimed >= supported):
                problems.append(f"the platform tag {platform_tag} claims more than the library supports: "
                                f"{measured[0]} ({measured[1]})")
    for key in ("sha256", "image_sha256", "size"):
        if now[key] != attestation.get("library", {}).get(key):
            problems.append(f"the library's {key} is not the attestation's")
    if now["image_sha256"] is None:
        problems.append("the library does not read as an ELF or Mach-O image")

    if release:
        if stand_in or suffix != "":
            problems.append("NOT A RELEASE: built against stand-in headers")
        if attestation.get("accepted_by") != "imgui-check+probe" or not attestation.get("imgui_check"):
            problems.append(f"NOT A RELEASE: accepted by {attestation.get('accepted_by')!r}, and a "
                            "release is accepted by Dear ImGui's own layout check as well")
        if not re.fullmatch(r"\d+(\.\d+)*", version):
            problems.append(f"NOT A RELEASE: {version} has a development, pre-release or local segment, "
                            "and an index takes none of a local one")
        if platform_tag.startswith("linux_"):
            problems.append(f"NOT A RELEASE: the platform tag {platform_tag} is a build for one machine; "
                            "an index takes manylinux tags")
        if tag and tag != "v" + library_version:
            problems.append(f"NOT THIS RELEASE: the library is {library_version} and the tag is {tag}")
    if not problems:
        print(f"ok   {name}: imtypeset {library_version} for imgui-bundle=={wheel_version}, "
              f"{python_tag}, {platform_tag}, headers {attestation.get('headers')}, "
              f"accepted by {attestation.get('accepted_by')}")
    return problems


def main(argv) -> int:
    release = "--release" in argv
    tag = argv[argv.index("--tag") + 1] if "--tag" in argv else ""
    wheels = [a for i, a in enumerate(argv) if a.endswith(".whl") and (i == 0 or argv[i - 1] != "--tag")]
    if not wheels:
        print(__doc__, file=sys.stderr)
        return 2
    gate = _gate()
    failed = False
    for path in wheels:
        problems = check(path, release, tag, gate)
        if problems:
            failed = True
            print(f"FAIL {os.path.basename(path)}")
            for problem in problems:
                print("     " + problem)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
