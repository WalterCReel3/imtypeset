"""Checks an INSTALLED imtypeset distribution, from the environment it is
installed in, with no IMTYPESET_* or PYTHONPATH variable set:

    <the environment's python> -P -B tools/wheel/check_installed.py
        [--other-python <interpreter with another imgui-bundle>]
        [--old-library <a sidecar library built from an earlier ref, for this imgui-bundle>]
        [--dev-library <a development sidecar for this imgui-bundle, its attestation beside it>]
        [--self-hosted-library <libimtypeset_pyhost.so>]
        [--scratch <directory>]  [--skip-headless]

  1. THE INSTALL: `import imtypeset` with no variable; the library is the one
     inside the package; the gate accepted it on its attestation; the
     distribution's metadata pins the imgui-bundle that is running. And the
     usage example in the metadata's description (the project's page) runs.
  2. THE FROZEN SHAPE: the same import with `sys.frozen` set, `sys.executable`
     naming a file that does not exist, every way of starting a process
     replaced by one that fails the check, and the package's directory and
     files read-only. Nothing may be started and nothing written. And THE
     UNFLAGGED SHAPE: the same without `sys.frozen`, `sys.executable` an
     existing file named for an application, as a packager that sets no flag
     leaves it.
  3. THE HEADLESS CHECKS of tools/bundle_route, through the installed package.
  4. PARTIAL UPDATES, each on a copy of the installed package in a scratch
     directory (the installation itself is never changed): what the import
     gate says when only part of a pair was replaced. Every refusal must be an
     ImportError whose first line is a sentence with no file path in it, with
     the expected `kind`, from a process that exits and is not killed.
  5. THE ENVIRONMENT: an installed distribution reads no variable. With
     IMTYPESET_LIBRARY naming each of: a sidecar with no attestation, a
     truncated one, a development build with its attestation beside it, a
     self-hosted library, a file that does not exist; with IMTYPESET_GATE and
     with IMTYPESET_PRELOAD_BUNDLE; in all three shapes: the import is the
     plain one, the named file is never mapped, nothing is started, and
     host_info() names the variable as ignored.
  6. A SECOND DEAR IMGUI in the global symbol scope (a library made here that
     exports Dear ImGui's names, opened RTLD_GLOBAL first): refused.
  7. THE DEVELOPMENT ROUTE, with --dev-library: this repository's package
     (python/imtypeset, which carries no library) and IMTYPESET_LIBRARY. With
     an attestation beside the library: held to it before the load. Without:
     loaded and probed in a child first, and refused where no child can be
     started. A truncated library ends a child, never the importing process.

Prints one line a check; exit 0 when all pass. This is what CI runs after
`pip install` of each wheel it builds (.github/workflows/wheels.yml).
"""

import argparse
import importlib.metadata
import importlib.util
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# What a child runs: import the package (from `site` first, when one is given)
# and say what happened, as one JSON line.
CHILD = r"""
import json, os, sys
site, shape = sys.argv[1], sys.argv[2]
if site:
    sys.path.insert(0, site)
started = []
if shape.startswith("global:"):
    import ctypes
    ctypes.CDLL(shape[len("global:"):], mode=ctypes.RTLD_GLOBAL)
if shape in ("frozen", "unflagged"):
    import subprocess
    if shape == "frozen":
        sys.frozen = True
        sys.executable = "/nonexistent/Application.app/Contents/MacOS/application"
    else:
        sys.executable = sys.argv[3]
    def refuse(*args, **kwargs):
        started.append(repr(args)[:200])
        raise OSError("a packaged application has no Python interpreter to start")
    subprocess.Popen = refuse
    subprocess.run = refuse
    os.system = refuse
    for name in ("posix_spawn", "posix_spawnp", "execv", "execve", "fork"):
        if hasattr(os, name):
            setattr(os, name, refuse)
answer = {"started": started}
try:
    import imtypeset
except ImportError as refused:
    text = str(refused)
    answer.update(imported=False, kind=getattr(refused, "kind", None), first=text.splitlines()[0],
                  rest=text.splitlines()[1:])
else:
    info = imtypeset.host_info()
    bundle = info["bundle"] or {}
    mapped = ""
    if sys.platform.startswith("linux"):
        with open("/proc/self/maps", encoding="utf-8", errors="replace") as handle:
            mapped = handle.read()
    named = os.environ.get("IMTYPESET_LIBRARY", "")
    answer.update(imported=True, file=imtypeset.__file__, version=imtypeset.version(),
                  route=info["route"], library=info["library"], host=info["host"],
                  ignored=info["ignored_environment"], checked_by=info["checked_by"],
                  named_mapped=bool(named) and os.path.realpath(named) in mapped,
                  distribution_version=info["distribution_version"],
                  checked_in=bundle.get("checked_in"), accepted_by=bundle.get("accepted_by"),
                  stand_in=bundle.get("stand_in"), headers=bundle.get("headers"),
                  wheel_version=bundle.get("wheel_version"), built_for=bundle.get("built_for"),
                  matched_by=(bundle.get("attestation") or {}).get("matched_by"),
                  second_module_check=bundle.get("second_module_check"),
                  preload=bundle.get("preload"), probes=bundle.get("probes_passed"))
print("CHECK-INSTALLED " + json.dumps(answer))
"""

failed = []


def report(name: str, ok: bool, detail: str = "") -> bool:
    print(("PASS " if ok else "FAIL ") + name + (f": {detail}" if detail else ""), flush=True)
    if not ok:
        failed.append(name)
    return ok


def clean_environment(extra=None) -> dict:
    environment = {
        name: value for name, value in os.environ.items()
        if not name.startswith("IMTYPESET_") and not name.startswith("PYTHON")
    }
    environment.update(extra or {})
    return environment


UNFLAGGED = [""]  # an existing file named for an application: main() makes it


def child(site: str = "", frozen: bool = False, python: str = "", environment=None, cwd=None,
          shape: str = "") -> dict:
    """`shape`: "plain", "frozen", "unflagged", or "global:<library>" (that
    library opened RTLD_GLOBAL before the import)."""
    shape = shape or ("frozen" if frozen else "plain")
    done = subprocess.run(
        [python or sys.executable, "-P", "-B", "-c", CHILD, site, shape, UNFLAGGED[0]],
        capture_output=True, text=True, env=clean_environment(environment), cwd=cwd, timeout=300,
    )
    lines = [l for l in done.stdout.splitlines() if l.startswith("CHECK-INSTALLED ")]
    answer = json.loads(lines[-1][len("CHECK-INSTALLED "):]) if lines else {}
    answer["returncode"] = done.returncode
    answer["stderr"] = done.stderr.strip()[-600:]
    return answer


def expect_refusal(name: str, answer: dict, kind: str, *words: str) -> None:
    first = answer.get("first", "")
    problems = []
    if answer.get("returncode") != 0:
        problems.append(f"the process ended with status {answer.get('returncode')}: {answer.get('stderr')}")
    if answer.get("imported") is not False:
        problems.append("the import was not refused")
    if answer.get("kind") != kind:
        problems.append(f"kind {answer.get('kind')!r}, expected {kind!r}")
    if not first.startswith("imtypeset: ") or os.sep in first or "\\" in first:
        problems.append("the first line is not a sentence free of paths")
    for word in words:
        if word not in first:
            problems.append(f"the sentence does not say {word!r}")
    if answer.get("started"):
        problems.append(f"a process was started: {answer['started']}")
    report(name, not problems, "; ".join(problems) if problems else f"[{kind}] {first}")


def tree(path: str) -> list:
    listing = []
    for root, directories, files in os.walk(path):
        for entry in directories + files:
            full = os.path.join(root, entry)
            status = os.lstat(full)
            listing.append((os.path.relpath(full, path), status.st_size, status.st_mtime_ns))
    return sorted(listing)


def read_only(path: str):
    """Takes write permission off a directory and everything in it; returns
    what to call to put it back."""
    saved = []
    for root, directories, files in os.walk(path):
        for entry in [root] + [os.path.join(root, f) for f in files]:
            mode = stat.S_IMODE(os.lstat(entry).st_mode)
            saved.append((entry, mode))
            os.chmod(entry, mode & ~0o222)

    def restore():
        for entry, mode in saved:
            os.chmod(entry, mode)

    return restore


def mapped_end(path: str) -> int:
    """Where what the loader maps of an ELF file ends (the end of its last
    PT_LOAD segment); the file's length for anything else. A development
    library is mostly debugging sections, which come after that: cut to half
    its length, it still loads."""
    import struct

    with open(path, "rb") as handle:
        head = handle.read(64)
        if head[:4] != b"\x7fELF" or len(head) < 64:
            return os.path.getsize(path)
        (phoff,) = struct.unpack_from("<Q", head, 0x20)
        phentsize, phnum = struct.unpack_from("<HH", head, 0x36)
        handle.seek(phoff)
        table = handle.read(phentsize * phnum)
    end = 0
    for index in range(phnum):
        p_type, _flags, p_offset, _vaddr, _paddr, p_filesz = struct.unpack_from("<IIQQQQ", table, index * phentsize)
        if p_type == 1:
            end = max(end, p_offset + p_filesz)
    return end or os.path.getsize(path)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--other-python", default="")
    parser.add_argument("--old-library", default="")
    parser.add_argument("--dev-library", default="")
    parser.add_argument("--self-hosted-library", default="")
    parser.add_argument("--scratch", default="")
    parser.add_argument("--skip-headless", action="store_true")
    args = parser.parse_args(argv)

    spec = importlib.util.find_spec("imtypeset")
    if spec is None or not spec.origin:
        print("check_installed.py: no imtypeset in this environment")
        return 2
    package = os.path.dirname(spec.origin)
    scratch = os.path.abspath(args.scratch) if args.scratch else tempfile.mkdtemp(prefix="imtypeset-check-")
    if args.other_python and os.sep in args.other_python:
        args.other_python = os.path.abspath(args.other_python)
    os.makedirs(scratch, exist_ok=True)
    UNFLAGGED[0] = os.path.join(scratch, "Application")
    with open(UNFLAGGED[0], "wb"):
        pass
    os.chmod(UNFLAGGED[0], 0o755)
    for option in ("dev_library", "self_hosted_library", "old_library"):
        if getattr(args, option):
            setattr(args, option, os.path.abspath(getattr(args, option)))

    # --- 1. the install -------------------------------------------------------------------
    plain = child(cwd=scratch)
    if not report("import imtypeset, no variable set", plain.get("imported") is True,
                  plain.get("first", plain.get("stderr", ""))):
        print(f"{len(failed)} failed")
        return 1
    library = plain["library"]
    print(f"     imtypeset {plain['version']} (distribution {plain['distribution_version']}), "
          f"library {library}, headers {plain['headers']}")
    report("the library is the one inside the package",
           plain["route"] == "installed" and os.path.dirname(library) == os.path.dirname(plain["file"]),
           f"route {plain['route']}")
    report("accepted on the attestation, by content",
           plain["checked_in"] == "attestation" and plain["matched_by"] == {"library": "sha256", "module": "sha256"},
           f"{plain['checked_in']} {plain['matched_by']}, {plain['accepted_by']}, {plain['probes']} probe rows")
    report("the duplicate-module check was made", str(plain["second_module_check"]).startswith("done: "),
           str(plain["second_module_check"]))
    try:
        distribution = importlib.metadata.distribution("imtypeset")
        requires = [r for r in (distribution.requires or []) if r.replace("_", "-").startswith("imgui-bundle")]
        pin = requires[0].split("==")[-1].strip() if len(requires) == 1 and "==" in requires[0] else None
        report("the metadata pins the imgui-bundle that is running, exactly",
               pin == plain["wheel_version"] == plain["built_for"]["wheel_version"],
               f"Requires-Dist {requires}, running {plain['wheel_version']}, built for {plain['built_for']['wheel_version']}")
        report("the metadata's version is the one host_info() gives",
               distribution.version == plain["distribution_version"], distribution.version)
        report("a stand-in build says so in its version, and only a stand-in build does",
               plain["stand_in"] == distribution.version.endswith(".dev0+standin")
               and ("standin" in distribution.version) == plain["stand_in"],
               f"stand_in {plain['stand_in']}, version {distribution.version}")
        # The usage example of the project's page (python/README-pypi.md, which
        # is the metadata's description): the text a reader will paste, run
        # as it is installed here.
        import re

        example = re.search(r"^```python\n(.*?)^```", distribution.read_text("METADATA") or "", re.M | re.S)
        if not report("the metadata's description has a usage example", example is not None):
            pass
        else:
            ran = subprocess.run([sys.executable, "-P", "-B", "-c", example.group(1)], capture_output=True,
                                 text=True, env=clean_environment(), cwd=scratch, timeout=300)
            report("the usage example of the project's page runs as written",
                   ran.returncode == 0 and ran.stdout.startswith(plain["version"] + " "),
                   (ran.stdout.strip() or ran.stderr.strip())[-300:])
    except importlib.metadata.PackageNotFoundError:
        report("the distribution's metadata is installed", False)

    # --- 2. the frozen shape --------------------------------------------------------------
    if os.geteuid() == 0:
        report("the frozen shape (read-only package)", False, "run as root: permissions prove nothing")
    else:
        before = tree(package)
        restore = read_only(package)
        try:
            writable = os.access(package, os.W_OK)
            frozen = child(frozen=True, cwd=scratch, environment={"HOME": "/nonexistent"})
        finally:
            restore()
        report("frozen: imported with sys.executable not an interpreter and the package read-only",
               frozen.get("imported") is True and not writable and frozen.get("checked_in") == "attestation",
               frozen.get("first", "") or f"checked in {frozen.get('checked_in')}")
        report("frozen: no process was started", frozen.get("started") == [], str(frozen.get("started")))
        report("frozen: nothing was written in the package", tree(package) == before)
        restore = read_only(package)
        try:
            unflagged = child(shape="unflagged", cwd=scratch, environment={"HOME": "/nonexistent"})
        finally:
            restore()
        report("unflagged (no sys.frozen, sys.executable an application): imported, nothing started, "
               "nothing written",
               unflagged.get("imported") is True and unflagged.get("checked_in") == "attestation"
               and unflagged.get("started") == [] and tree(package) == before,
               unflagged.get("first", "") or f"checked in {unflagged.get('checked_in')}, started {unflagged.get('started')}")

    # --- 3. the headless checks, through the installed package ----------------------------
    if not args.skip_headless:
        for script in ("check_document_edits.py", "check_swap_answers.py"):
            done = subprocess.run(
                [sys.executable, "-P", "-B", os.path.join(REPO, "tools", "bundle_route", script)],
                capture_output=True, text=True, env=clean_environment(), cwd=scratch, timeout=600,
            )
            lines = done.stdout.splitlines()
            passed = sum(1 for l in lines if l.startswith("PASS"))
            bad = [l for l in lines if l.startswith("FAIL")]
            report(f"tools/bundle_route/{script} through the installed package",
                   done.returncode == 0 and not bad and passed > 0,
                   f"{passed} PASS" + (f", {bad[:3]} {done.stderr.strip()[-300:]}" if done.returncode else ""))

    # --- 4. partial updates ---------------------------------------------------------------
    module_directory = os.path.join(os.path.dirname(package), "imgui_bundle")
    counter = [0]

    def copy(with_bundle=module_directory) -> str:
        """A site directory holding a copy of the installed package, and
        imgui_bundle as a link beside it (or not at all)."""
        counter[0] += 1
        site = os.path.join(scratch, f"site-{counter[0]}")
        shutil.rmtree(site, ignore_errors=True)
        os.makedirs(site)
        shutil.copytree(package, os.path.join(site, "imtypeset"),
                        ignore=shutil.ignore_patterns("__pycache__"))
        if with_bundle:
            os.symlink(with_bundle, os.path.join(site, "imgui_bundle"))
        return site

    def lib_of(site: str) -> str:
        return os.path.join(site, "imtypeset", os.path.basename(library))

    def edit(path: str, old: str, new: str) -> None:
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        assert old in text, f"{path} no longer holds {old!r}"
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text.replace(old, new, 1))

    site = copy()
    baseline = child(site, cwd=scratch)
    report("a copy of the package beside the same imgui_bundle is accepted (the baseline)",
           baseline.get("imported") is True and baseline.get("file", "").startswith(site),
           baseline.get("first", ""))

    site = copy()
    size = os.path.getsize(lib_of(site))
    with open(lib_of(site), "r+b") as handle:
        handle.truncate(size // 2)
    expect_refusal("the library truncated to half (an interrupted update)", child(site, cwd=scratch),
                   "damaged", "Install imtypeset again")

    site = copy()
    with open(lib_of(site), "r+b") as handle:
        handle.seek(size // 2)
        byte = handle.read(1)
        handle.seek(size // 2)
        handle.write(bytes([byte[0] ^ 0x40]))
    expect_refusal("one byte of the library changed", child(site, cwd=scratch), "damaged")

    site = copy()
    with open(lib_of(site), "wb") as handle:
        handle.write(b"")
    expect_refusal("the library empty", child(site, cwd=scratch), "damaged")

    site = copy()
    os.remove(os.path.join(site, "imtypeset", "_attestation.json"))
    expect_refusal("the attestation missing", child(site, cwd=scratch), "unchecked")
    expect_refusal("the attestation missing, frozen: refused and nothing started",
                   child(site, frozen=True, cwd=scratch), "unchecked")
    expect_refusal("the attestation missing, unflagged: refused and nothing started",
                   child(site, shape="unflagged", cwd=scratch), "unchecked")
    if args.dev_library:
        for shape in ("plain", "frozen", "unflagged"):
            expect_refusal(f"the attestation missing and IMTYPESET_LIBRARY naming a good library, {shape}: "
                           "still an installed package, still refused",
                           child(site, shape=shape, cwd=scratch,
                                 environment={"IMTYPESET_LIBRARY": args.dev_library}), "unchecked")

    site = copy()
    os.remove(lib_of(site))
    expect_refusal("the library missing, the attestation there", child(site, cwd=scratch), "damaged",
                   "Install imtypeset again")

    for name, document in (
        ("the attestation's library record not an object", {"library": "libimtypeset_bundle.so"}),
        ("the attestation's module record not an object", {"module": ["sha256"]}),
        ("the attestation's probe rows not triples", {"probes": [["ImGuiIO.Fonts", True]]}),
        ("the attestation's probe rows not a list", {"probes": {"ImGuiIO.Fonts": True}}),
        ("the attestation's platform a string", {"platform": "Linux x86_64"}),
    ):
        site = copy()
        with open(os.path.join(site, "imtypeset", "_attestation.json"), encoding="utf-8") as handle:
            edited = json.load(handle)
        edited.update(document)
        with open(os.path.join(site, "imtypeset", "_attestation.json"), "w", encoding="utf-8") as handle:
            json.dump(edited, handle)
        expect_refusal(name, child(site, cwd=scratch), "damaged")

    site = copy()
    with open(os.path.join(site, "imtypeset", "_attestation.json"), encoding="utf-8") as handle:
        edited = json.load(handle)
    del edited["imgui_check"]
    with open(os.path.join(site, "imtypeset", "_attestation.json"), "w", encoding="utf-8") as handle:
        json.dump(edited, handle)
    expect_refusal("the attestation lacking what the library is compared with after the load",
                   child(site, cwd=scratch), "refused")

    site = copy()
    with open(os.path.join(site, "imtypeset", "_attestation.json"), "w", encoding="utf-8") as handle:
        handle.write('{"schema": 1, "library": ')
    expect_refusal("the attestation cut short", child(site, cwd=scratch), "damaged")

    site = copy()
    attestation_path = os.path.join(site, "imtypeset", "_attestation.json")
    with open(attestation_path, encoding="utf-8") as handle:
        attestation = json.load(handle)
    other = dict(attestation, platform={"system": "Darwin" if attestation["platform"]["system"] != "Darwin" else "Linux",
                                        "machine": "arm64"})
    with open(attestation_path, "w", encoding="utf-8") as handle:
        json.dump(other, handle)
    expect_refusal("a package copied from another platform", child(site, cwd=scratch), "wrong-platform")

    site = copy()
    edit(os.path.join(site, "imtypeset", "_capi.py"), 'C_VERSION = "', 'C_VERSION = "9.')
    expect_refusal("the Python modules of another version over this library (version string)",
                   child(site, cwd=scratch), "package-mismatch", "refuses this package")

    site = copy()
    edit(os.path.join(site, "imtypeset", "_capi.py"), "PROTOTYPES = {",
         'PROTOTYPES = {\n    "ImTypeset_AFunctionOfALaterRelease": (_INT, []),')
    expect_refusal("the Python modules of a later release over this library (a new function)",
                   child(site, cwd=scratch), "package-mismatch", "does not export")

    # The rule is the binding's, and the binding is the platform's
    # (docs/sidecar.md, sections 2 and 9). A library LINKED to the wheel's
    # module by name (Linux) finds it through a run path that leads beside its
    # own package and nowhere else: apart, refused. A library bound by LOOKUP
    # (macOS) names no file and binds to the module the process has loaded,
    # which the gate has already held to the attestation by content: apart,
    # accepted.
    linked = attestation.get("run_path") == "$ORIGIN/../imgui_bundle"
    system = attestation["platform"]["system"]
    report("the binding is the platform's: linked by name on Linux, by lookup on macOS",
           linked == (system == "Linux") and attestation.get("bind") == ("link" if linked else "lookup"),
           f"{system}: bind {attestation.get('bind')!r}, run path {attestation.get('run_path')!r}")
    site = copy(with_bundle="")
    apart = child(site, cwd=scratch)
    if linked:
        expect_refusal("imgui_bundle imported from another directory than the package's neighbour "
                       "(a library linked by name): refused",
                       apart, "layout", "same environment")
    else:
        report("imgui_bundle imported from another directory than the package's neighbour "
               "(a library bound by lookup): accepted, the module held to the attestation by content",
               apart.get("imported") is True and apart.get("file", "").startswith(site)
               and apart.get("checked_in") == "attestation"
               and (apart.get("matched_by") or {}).get("module") in ("sha256", "image")
               and str(apart.get("second_module_check")).startswith("done: "),
               apart.get("first", "") or f"matched by {apart.get('matched_by')}, "
               f"{apart.get('second_module_check')}")

    if attestation["platform"]["system"] == "Linux":
        # The loader maps nothing of an ELF file's tail, so these would load.
        for cut in (1, 64, 4096):
            site = copy()
            with open(lib_of(site), "r+b") as handle:
                handle.truncate(size - cut)
            expect_refusal(f"the library cut {cut} bytes short at its tail", child(site, cwd=scratch),
                           "damaged", "Install imtypeset again")
        site = copy()
        with open(lib_of(site), "ab") as handle:
            handle.write(b"\0" * 4096)
        longer = child(site, cwd=scratch)
        report("the library with bytes added after its end: accepted by its image",
               longer.get("imported") is True and (longer.get("matched_by") or {}).get("library") == "image",
               longer.get("first", "") or str(longer.get("matched_by")))

    if shutil.which("strip") and attestation["platform"]["system"] == "Linux":
        site = copy()
        subprocess.run(["strip", "--strip-all", lib_of(site)], check=True)
        stripped = child(site, cwd=scratch)
        report("the library rewritten by a tool that keeps what is mapped (strip): accepted by its image",
               stripped.get("imported") is True and (stripped.get("matched_by") or {}).get("library") == "image",
               stripped.get("first", "") or str(stripped.get("matched_by")))
        with open(lib_of(site), "r+b") as handle:
            handle.truncate(os.path.getsize(lib_of(site)) - 1)
        expect_refusal("that stripped library cut one byte short", child(site, cwd=scratch), "damaged")

    if args.other_python:
        # The wheel built for this imgui-bundle, beside another one.
        their_module = subprocess.run(
            [args.other_python, "-P", "-B", "-c",
             "import imgui_bundle, os; print(os.path.dirname(imgui_bundle.__file__)); print(imgui_bundle.__version__)"],
            capture_output=True, text=True, env=clean_environment(), check=True,
        ).stdout.split("\n")
        site = copy(with_bundle=their_module[0])
        expect_refusal(f"this wheel beside imgui-bundle {their_module[1]} (only one of the two updated)",
                       child(site, python=args.other_python, cwd=scratch), "bundle-mismatch",
                       plain["wheel_version"], their_module[1], "updated together")
        expect_refusal("the same, frozen", child(site, frozen=True, python=args.other_python, cwd=scratch),
                       "bundle-mismatch", plain["wheel_version"], their_module[1])

    if args.old_library:
        # The development route (a checkout's package), with a library built
        # from an earlier ref: refused by the package when an attestation
        # stands for it, by the child's probe when none does.
        old = child(os.path.join(REPO, "python"), cwd=scratch,
                    environment={"IMTYPESET_LIBRARY": args.old_library})
        expect_refusal("a library from an earlier ref under this checkout's package (IMTYPESET_LIBRARY)", old,
                       "package-mismatch" if os.path.isfile(args.old_library + ".attestation.json") else
                       old.get("kind") if old.get("kind") in ("package-mismatch", "refused") else "package-mismatch")

    # --- 5. the environment: an installed distribution reads no variable ------------------
    unattested = truncated = ""
    if args.dev_library:
        unattested = os.path.join(scratch, "named", "libimtypeset_bundle.so")
        os.makedirs(os.path.dirname(unattested), exist_ok=True)
        shutil.copyfile(args.dev_library, unattested)  # the library, and no attestation beside it
        truncated = os.path.join(scratch, "named", "truncated.so")
        with open(args.dev_library, "rb") as source, open(truncated, "wb") as target:
            target.write(source.read(mapped_end(args.dev_library) // 2))  # cut inside what is mapped
        report("--dev-library has its attestation beside it",
               os.path.isfile(args.dev_library + ".attestation.json"), args.dev_library)
    settings = [
        ("IMTYPESET_LIBRARY naming a sidecar with no attestation (review-8 F1 a)", {"IMTYPESET_LIBRARY": unattested}),
        ("IMTYPESET_LIBRARY naming a truncated sidecar (F1 a)", {"IMTYPESET_LIBRARY": truncated}),
        ("IMTYPESET_LIBRARY naming a self-hosted library (F1 b)", {"IMTYPESET_LIBRARY": args.self_hosted_library}),
        ("IMTYPESET_LIBRARY naming a development build with its attestation (F1 c)",
         {"IMTYPESET_LIBRARY": args.dev_library}),
        ("IMTYPESET_LIBRARY naming no file", {"IMTYPESET_LIBRARY": os.path.join(scratch, "no-such-file.so")}),
        ("IMTYPESET_GATE=probe", {"IMTYPESET_GATE": "probe"}),
        ("IMTYPESET_GATE=nonsense", {"IMTYPESET_GATE": "nonsense"}),
        ("IMTYPESET_PRELOAD_BUNDLE=1", {"IMTYPESET_PRELOAD_BUNDLE": "1"}),
        ("all four at once", {"IMTYPESET_LIBRARY": unattested or os.path.join(scratch, "no-such-file.so"),
                              "IMTYPESET_GATE": "probe", "IMTYPESET_PRELOAD_BUNDLE": "1",
                              "IMTYPESET_PYTHON": os.path.join(REPO, "python")}),
    ]
    for name, environment in settings:
        if "" in environment.values():
            print(f"SKIP {name}: needs --dev-library or --self-hosted-library", flush=True)
            continue
        for shape in ("plain", "frozen", "unflagged"):
            answer = child(shape=shape, cwd=scratch, environment=environment)
            problems = []
            if answer.get("returncode") != 0:
                problems.append(f"the process ended with status {answer.get('returncode')}: {answer.get('stderr')}")
            if answer.get("imported") is not True:
                problems.append(f"not imported: [{answer.get('kind')}] {answer.get('first')}")
            else:
                for key in ("route", "library", "checked_in", "matched_by", "preload", "host"):
                    if answer.get(key) != plain.get(key):
                        problems.append(f"{key} is {answer.get(key)!r}, and {plain.get(key)!r} with no variable")
                if sorted(answer.get("ignored") or []) != sorted(environment):
                    problems.append(f"host_info() says ignored {answer.get('ignored')}, and {sorted(environment)} were set")
                if answer.get("named_mapped"):
                    problems.append("the file IMTYPESET_LIBRARY names is mapped in the process")
            if answer.get("started"):
                problems.append(f"a process was started: {answer['started']}")
            report(f"{name}, {shape}: not read", not problems, "; ".join(problems))
    report("with no variable set, host_info() names none as ignored", plain.get("ignored") == [],
           str(plain.get("ignored")))

    # --- 6. a second Dear ImGui in the global symbol scope --------------------------------
    compiler = shutil.which("c++") or shutil.which("g++")
    if compiler and sys.platform.startswith("linux"):
        source = os.path.join(scratch, "second_imgui.cpp")
        second = os.path.join(scratch, "libsecond_imgui.so")
        with open(source, "w", encoding="utf-8") as handle:
            handle.write("// Exports four of Dear ImGui's names, as an executable or an extension with a\n"
                         "// Dear ImGui of its own linked in would. None of them is ever called.\n"
                         "namespace ImGui { void* GetIO() { return 0; } void* GetStyle() { return 0; }\n"
                         "void* GetWindowDrawList() { return 0; } void* MemAlloc(unsigned long) { return 0; } }\n")
        subprocess.run([compiler, "-shared", "-fPIC", "-o", second, source], check=True)
        expect_refusal("a second Dear ImGui in the global symbol scope (review-8 F10)",
                       child(shape="global:" + second, cwd=scratch), "refused", "second Dear ImGui")
        report("the plain import says the global scope was looked at",
               "global Dear ImGui names: none but the wheel's" in str(plain["second_module_check"]),
               str(plain["second_module_check"]))
    else:
        print("SKIP a second Dear ImGui in the global symbol scope: needs a C++ compiler, on Linux", flush=True)

    # --- 7. the development route ---------------------------------------------------------
    if args.dev_library:
        checkout = os.path.join(REPO, "python")

        def developer(library, shape="plain", **more):
            return child(checkout, shape=shape, cwd=scratch,
                         environment=dict({"IMTYPESET_LIBRARY": library}, **more))

        def expect_import(name, answer, **wanted):
            problems = [f"{key} is {answer.get(key)!r}, expected {value!r}"
                        for key, value in wanted.items() if answer.get(key) != value]
            if answer.get("imported") is not True:
                problems = [f"not imported: [{answer.get('kind')}] {answer.get('first')} {answer.get('stderr')}"]
            report(name, not problems, "; ".join(problems))

        for shape in ("plain", "frozen", "unflagged"):
            answer = developer(args.dev_library, shape)
            expect_import(f"a checkout, a library with its attestation, {shape}: held to it, nothing started",
                          answer, route="IMTYPESET_LIBRARY", checked_in="attestation", started=[], ignored=[],
                          matched_by={"library": "sha256", "module": "sha256"})
        expect_import("a checkout, a library with no attestation: loaded and probed in a child first",
                      developer(unattested), route="IMTYPESET_LIBRARY", checked_in="child", ignored=[])
        for shape in ("frozen", "unflagged"):
            expect_refusal(f"a checkout, a library with no attestation, {shape}: refused, nothing started "
                           "(review-8 F1 a)", developer(unattested, shape), "unchecked")
            expect_refusal(f"a checkout, a truncated library, {shape}: refused, never mapped",
                           developer(truncated, shape), "unchecked")
            expect_refusal(f"a checkout, IMTYPESET_GATE=probe over an attestation, {shape}: refused, "
                           "nothing started", developer(args.dev_library, shape, IMTYPESET_GATE="probe"),
                           "unchecked")
        cut = developer(truncated)
        report("a checkout, a truncated library with no attestation: an ImportError, and the importing "
               "process lives",
               cut.get("returncode") == 0 and cut.get("imported") is False
               and cut.get("kind") in ("not-a-library", "refused"),
               f"status {cut.get('returncode')} [{cut.get('kind')}] {cut.get('first', cut.get('stderr'))}")
        expect_refusal("a checkout, IMTYPESET_GATE=attested and no attestation", 
                       developer(unattested, IMTYPESET_GATE="attested"), "unchecked")
        expect_refusal("a checkout, IMTYPESET_GATE=nonsense", developer(unattested, IMTYPESET_GATE="nonsense"),
                       "configuration")
        expect_import("a checkout, IMTYPESET_GATE=probe over an attestation: the child decides",
                      developer(args.dev_library, IMTYPESET_GATE="probe"), checked_in="child")
        moved = os.path.join(scratch, "named", "moved.so")
        shutil.copyfile(args.dev_library, moved)
        shutil.copyfile(args.dev_library + ".attestation.json", moved + ".attestation.json")
        with open(moved, "r+b") as handle:
            handle.truncate(os.path.getsize(moved) - 1)
        expect_refusal("a checkout, an attested library cut one byte short: refused before the load",
                       developer(moved), "damaged")
        if args.self_hosted_library:
            hosted = developer(args.self_hosted_library)
            expect_import("a checkout, a self-hosted library: loaded in a child first, then accepted by its "
                          "build information, and the route says so (review-8 F1 b)",
                          hosted, route="IMTYPESET_LIBRARY (build-info check only)", host="self-hosted")
            print(f"     checked by: {hosted.get('checked_by')}")
            for shape in ("frozen", "unflagged"):
                expect_refusal(f"a checkout, a self-hosted library, {shape}: refused, nothing started",
                               developer(args.self_hosted_library, shape), "unchecked")

    print(f"{len(failed)} failed" if failed else "all passed")
    if not args.scratch:
        shutil.rmtree(scratch, ignore_errors=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
