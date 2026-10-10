#!/usr/bin/env python3
"""
The provenance of tests/golden: a hash of every file, and what it was dumped from.

Writes two files:

    tests/golden/MANIFEST.sha256   `<sha256>  <path under tests/golden>` for
                                   every golden file, sorted by path
    tests/golden/PROVENANCE.md     the reference's commit and uncommitted files,
                                   the interpreter and library versions, the
                                   font files' hashes, the date, the command
                                   that regenerates each directory, and the
                                   in-memory patches each oracle script applied
                                   to the reference (its REFERENCE_PATCHES)

Run from the repository root. To WRITE, use the reference's interpreter, since
the versions recorded are the ones the goldens were dumped under:

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=../imgui-adv-text \\
        ../imgui-adv-text/.venv/bin/python -B tools/oracle/manifest.py

To CHECK, any Python 3 will do, and nothing outside this repository is read:

    python3 -B tools/oracle/manifest.py --check

`--check` hashes every file under tests/golden again and compares with
MANIFEST.sha256. It names each file that differs, each that the manifest lists
and is gone, and each that is there and not listed, and exits 1 if there is
any. It does not look at PROVENANCE.md: that file holds a date.

Re-runnable: a run that finds nothing changed leaves both files byte-identical
(the date in PROVENANCE.md is kept unless something else in it changed). Other
goldens are regenerated late in a session, so the LAST gate of a session runs
this once more; a manifest written before that is expected to be stale.

Only read-only git commands are run against the reference, and nothing is
written outside tests/golden.
"""

from __future__ import annotations

import argparse
import ast
import datetime
import hashlib
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GOLDEN = ROOT / "tests" / "golden"
ORACLE = ROOT / "tools" / "oracle"
REFERENCE = ROOT.parent / "imgui-adv-text"
MANIFEST = GOLDEN / "MANIFEST.sha256"
PROVENANCE = GOLDEN / "PROVENANCE.md"
OURS = {MANIFEST.name, PROVENANCE.name}

INVOCATION = "PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=../imgui-adv-text ../imgui-adv-text/.venv/bin/python -B"

# Which script writes which directory, and with what arguments. A directory
# under tests/golden that is not named here is reported, not guessed at.
WRITERS: dict[str, tuple[str, ...]] = {
    "blocks": ("dump_blocks.py",),
    "core": ("dump_core.py",),
    "doc": ("dump_doc_extras.py",),
    "draw": ("dump_draw.py",),
    "font": ("dump_font.py",),
    "font-synth": ("dump_font_synth.py",),
    "font-wild": ("dump_font_wild.py",),
    "font-wild-kern": ("dump_kern_legacy.py",),
    "math": ("dump_math.py",),
    "para": ("dump_para.py",),
    "para-map": ("dump_para_map.py",),
    "selection": ("dump_selection.py",),
    "tables": ("dump_tables.py",),
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for piece in iter(lambda: handle.read(1 << 20), b""):
            digest.update(piece)
    return digest.hexdigest()


def golden_files() -> list[Path]:
    return sorted(
        (path for path in GOLDEN.rglob("*") if path.is_file() and not (path.parent == GOLDEN and path.name in OURS)),
        key=lambda path: path.relative_to(GOLDEN).as_posix(),
    )


def manifest_text() -> str:
    return "".join(f"{sha256(path)}  {path.relative_to(GOLDEN).as_posix()}\n" for path in golden_files())


def read_manifest() -> dict[str, str]:
    listed: dict[str, str] = {}
    for line in MANIFEST.read_text(encoding="utf-8").splitlines():
        digest, _, name = line.partition("  ")
        listed[name] = digest
    return listed


def check() -> int:
    if not MANIFEST.exists():
        print(f"manifest: {MANIFEST.relative_to(ROOT)} does not exist; run tools/oracle/manifest.py")
        return 1
    listed = read_manifest()
    present = {path.relative_to(GOLDEN).as_posix(): path for path in golden_files()}
    problems = 0
    for name in sorted(set(listed) | set(present)):
        if name not in present:
            print(f"manifest: MISSING   tests/golden/{name} (listed, not on disk)")
        elif name not in listed:
            print(f"manifest: UNLISTED  tests/golden/{name} (on disk, not listed)")
        elif sha256(present[name]) != listed[name]:
            print(f"manifest: DIFFERS   tests/golden/{name}")
        else:
            continue
        problems += 1
    if problems:
        print(f"manifest: {problems} of {len(set(listed) | set(present))} files do not match MANIFEST.sha256")
        print("manifest: if the goldens were regenerated on purpose, run tools/oracle/manifest.py and review the diff")
        return 1
    print(f"manifest: {len(present)} files match MANIFEST.sha256")
    return 0


def git(*args: str) -> str:
    done = subprocess.run(
        ["git", "--no-optional-locks", "-C", str(REFERENCE), *args], capture_output=True, text=True, check=False, cwd=ROOT
    )
    return done.stdout if done.returncode == 0 else f"(git {' '.join(args)} failed: {done.stderr.strip()})\n"


def patches_of(script: str) -> str:
    """
    A script's module-level `REFERENCE_PATCHES`, read from its source and not by importing it.
    """
    tree = ast.parse((ORACLE / script).read_text(encoding="utf-8"))
    for node in tree.body:
        targets = (
            node.targets if isinstance(node, ast.Assign) else [node.target] if isinstance(node, ast.AnnAssign) else []
        )
        if any(isinstance(target, ast.Name) and target.id == "REFERENCE_PATCHES" for target in targets):
            try:
                value = ast.literal_eval(node.value)  # type: ignore[arg-type]
            except ValueError:
                return "declared, not a literal: read the script"
            return "; ".join(str(item) for item in value) if value else "none"
    return "none (the script declares no REFERENCE_PATCHES)"


def version_of(distribution: str) -> str:
    from importlib import metadata

    try:
        return metadata.version(distribution)
    except metadata.PackageNotFoundError:
        return "not installed in this interpreter"


def font_rows() -> list[str]:
    rows: list[str] = []
    for path in sorted((ROOT / "tests" / "data" / "fonts").iterdir()):
        if path.suffix.lower() in (".ttf", ".otf", ".ttc"):
            rows.append(f"| `tests/data/fonts/{path.name}` | `{sha256(path)}` |")
    try:
        from advtext import fontlib
        from advtext.fontmetrics import Face
    except ImportError:
        rows.append("| the reference's faces | not importable in this interpreter |")
        return rows
    for face in Face:
        try:
            path = Path(fontlib.font_file(face))
        except Exception as error:  # noqa: BLE001 - a face with no file is a fact to record
            rows.append(f"| reference `{face.value}` | no file: {type(error).__name__} |")
            continue
        try:
            shown = "../imgui-adv-text/" + path.resolve().relative_to(REFERENCE.resolve()).as_posix()
        except ValueError:
            shown = str(path)
        rows.append(f"| reference `{face.value}`: `{shown}` | `{sha256(path)}` |")
    return rows


def provenance_text(manifest: str, date: str) -> str:
    commit = git("rev-parse", "HEAD").strip()
    dirty = [line for line in git("status", "--porcelain").splitlines() if line.strip()]
    directories = sorted(path.name for path in GOLDEN.iterdir() if path.is_dir())
    counts: dict[str, int] = {}
    for line in manifest.splitlines():
        name = line.partition("  ")[2]
        counts[name.split("/")[0]] = counts.get(name.split("/")[0], 0) + 1
    out = [
        "# Where the goldens came from",
        "",
        "Written by `tools/oracle/manifest.py`. Do not edit: run it again.",
        "",
        "`MANIFEST.sha256`, beside this file, holds the SHA-256 of every file under",
        "`tests/golden`. `python3 -B tools/oracle/manifest.py --check` compares the",
        "tree with it and names every file that differs, is missing or is not listed.",
        "",
        "**Goldens are regenerated late in a session, by more than one task. The last",
        "gate of a session regenerates this file and the manifest**; a copy written",
        "before that describes the tree as it was then. A regenerated golden that",
        "differs is looked at before the manifest is rewritten (CLAUDE.md, \"The golden",
        "format\").",
        "",
        f"- Date written: {date}",
        f"- SHA-256 of `MANIFEST.sha256`: `{hashlib.sha256(manifest.encode('utf-8')).hexdigest()}`",
        f"- Files listed: {len(manifest.splitlines())}",
        "",
        "## The reference",
        "",
        "`../imgui-adv-text`, the Python prototype (D32), read and never written.",
        "",
        f"- Commit: `{commit}`",
        f"- Uncommitted files at the time ({len(dirty)}), as `git status --porcelain` prints them:",
        *([f"  - `{line}`" for line in dirty] or ["  - none"]),
        "",
        "The commit and the list are those of the moment this file was written, not",
        "of the moment each directory was dumped: nothing records that per directory.",
        "A directory dumped earlier in the session was dumped from the same commit",
        "only if the reference did not move in between; `--verify` of its script is",
        "what shows it still agrees.",
        "",
        "## The interpreter and libraries",
        "",
        f"- Python: {sys.version.split()[0]} (`{sys.executable}`)",
        f"- imgui_bundle: {version_of('imgui-bundle')}",
        f"- fontTools: {version_of('fonttools')}",
        f"- markdown-it-py: {version_of('markdown-it-py')}",
        "",
        "## Font files",
        "",
        "| file | SHA-256 |",
        "|---|---|",
        *font_rows(),
        "",
        "## Directories",
        "",
        "Each command is run from the repository root; the prefix is",
        "",
        f"    {INVOCATION}",
        "",
        "`REFERENCE_PATCHES` is what a script changed in the reference **in memory**",
        "before dumping (D32.3), as the script itself declares it.",
        "",
        "| directory | files | regenerate with | REFERENCE_PATCHES |",
        "|---|---|---|---|",
    ]
    for name in directories:
        writer = WRITERS.get(name)
        if writer is None:
            out.append(f"| `{name}` | {counts.get(name, 0)} | **unknown: not in manifest.py's WRITERS** | unknown |")
            continue
        command = "tools/oracle/" + " ".join(writer)
        out.append(f"| `{name}` | {counts.get(name, 0)} | `... {command}` | {patches_of(writer[0])} |")
    loose = sorted(name for name in counts if "/" not in name and name not in directories)
    if loose:
        out += ["", "Files directly under `tests/golden`: " + ", ".join(f"`{name}`" for name in loose) + "."]
    out += [
        "",
        "Scripts that take arguments, read files outside the repository (the fonts",
        "installed on the machine, for `font-wild`) or need more than one run say so",
        "in their own docstrings and in `tools/oracle/FORMAT*.md`; the command above",
        "is the plain invocation.",
        "",
    ]
    return "\n".join(out)


def write(date: str | None) -> int:
    manifest = manifest_text()
    today = date or datetime.date.today().isoformat()
    text = provenance_text(manifest, today)
    if date is None and PROVENANCE.exists():
        # Keep the date already written if nothing else would change.
        before = PROVENANCE.read_text(encoding="utf-8")
        for line in before.splitlines():
            if line.startswith("- Date written: "):
                kept = provenance_text(manifest, line.removeprefix("- Date written: "))
                if kept == before:
                    text = kept
                break
    changed = []
    for path, content in ((MANIFEST, manifest), (PROVENANCE, text)):
        if not path.exists() or path.read_text(encoding="utf-8") != content:
            path.write_text(content, encoding="utf-8", newline="\n")
            changed.append(path.name)
    print(f"manifest: {len(manifest.splitlines())} files; wrote " + (", ".join(changed) if changed else "nothing (unchanged)"))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--check", action="store_true", help="compare tests/golden with MANIFEST.sha256; exit 1 if they differ")
    parser.add_argument("--date", default=None, help="the date to record (YYYY-MM-DD); today by default")
    args = parser.parse_args()
    return check() if args.check else write(args.date)


if __name__ == "__main__":
    sys.exit(main())
