"""Checks an INSTALLED imtypeset distribution, from the environment it is
installed in, with no IMTYPESET_* or PYTHONPATH variable set:

    <the environment's python> -P -B tools/wheel/check_installed.py
        [--other-python <interpreter with another imgui-bundle>]
        [--old-library <a sidecar library built from an earlier ref, for this imgui-bundle>]
        [--scratch <directory>]  [--skip-headless]

  1. THE INSTALL: `import imtypeset` with no variable; the library is the one
     inside the package; the gate accepted it on its attestation; the
     distribution's metadata pins the imgui-bundle that is running.
  2. THE FROZEN SHAPE: the same import with `sys.frozen` set, `sys.executable`
     naming a file that does not exist, every way of starting a process
     replaced by one that fails the check, and the package's directory and
     files read-only. Nothing may be started and nothing written.
  3. THE HEADLESS CHECKS of tools/bundle_route, through the installed package.
  4. PARTIAL UPDATES, each on a copy of the installed package in a scratch
     directory (the installation itself is never changed): what the import
     gate says when only part of a pair was replaced. Every refusal must be an
     ImportError whose first line is a sentence with no file path in it, with
     the expected `kind`, from a process that exits and is not killed.

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
site, frozen = sys.argv[1], sys.argv[2] == "frozen"
if site:
    sys.path.insert(0, site)
started = []
if frozen:
    import subprocess
    sys.frozen = True
    sys.executable = "/nonexistent/Application.app/Contents/MacOS/application"
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
    answer.update(imported=True, file=imtypeset.__file__, version=imtypeset.version(),
                  route=info["route"], library=info["library"],
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


def child(site: str = "", frozen: bool = False, python: str = "", environment=None, cwd=None) -> dict:
    done = subprocess.run(
        [python or sys.executable, "-P", "-B", "-c", CHILD, site, "frozen" if frozen else "plain"],
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


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--other-python", default="")
    parser.add_argument("--old-library", default="")
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
               plain["stand_in"] == ("+standin" in distribution.version),
               f"stand_in {plain['stand_in']}, version {distribution.version}")
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

    site = copy(with_bundle="")
    expect_refusal("imgui_bundle imported from another directory than the package's neighbour",
                   child(site, cwd=scratch), "layout", "same environment")

    site = copy()
    expect_refusal("frozen with IMTYPESET_GATE=probe", child(site, frozen=True, cwd=scratch,
                                                             environment={"IMTYPESET_GATE": "probe"}),
                   "configuration")

    if shutil.which("strip") and attestation["platform"]["system"] == "Linux":
        site = copy()
        subprocess.run(["strip", "--strip-all", lib_of(site)], check=True)
        stripped = child(site, cwd=scratch)
        report("the library rewritten by a tool that keeps what is mapped (strip): accepted by its image",
               stripped.get("imported") is True and (stripped.get("matched_by") or {}).get("library") == "image",
               stripped.get("first", "") or str(stripped.get("matched_by")))

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
        # The development override, with a library built from an earlier ref.
        old = child(cwd=scratch, environment={"IMTYPESET_LIBRARY": os.path.abspath(args.old_library)})
        expect_refusal("a library from an earlier ref under this package (IMTYPESET_LIBRARY)", old,
                       "package-mismatch")

    print(f"{len(failed)} failed" if failed else "all passed")
    if not args.scratch:
        shutil.rmtree(scratch, ignore_errors=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
