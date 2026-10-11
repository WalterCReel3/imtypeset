"""
Holds the oracle scripts to the pinned reference.

The reference is one commit of ../imgui-adv-text, extracted read-only into
build/advtext-reference by tools/oracle/pin_reference.sh and described by
tools/oracle/REFERENCE.lock. Every script that dumps a golden calls
`require()` before it imports `advtext`, and takes the reference's files
(`demo/sample.md`, ...) from `ROOT_DIR` here. Nothing in tools/oracle reads
../imgui-adv-text's working tree: it is on another branch, and its Python
layout core is not in it.

`require()` exits with status 2 and a message, before anything is dumped, when

  - build/advtext-reference is missing, or its stamp names another commit
    than REFERENCE.lock;
  - a file in it was added, removed or edited (one SHA-256 over the SHA-256 of
    every file, compared with the lock);
  - `import advtext` resolves anywhere else. The reference's interpreter has
    an editable install of ../imgui-adv-text, so that is what a run without
    PYTHONPATH=build/advtext-reference imports;
  - the interpreter was started without -B / PYTHONDONTWRITEBYTECODE, which
    would have it write __pycache__ into the pinned tree.

The interpreter and its libraries are NOT pinned by this: they are those of
../imgui-adv-text/.venv, which no commit holds. PROVENANCE.md records their
versions, and the fonts the reference loads come from that environment's
imgui_bundle.

The invocation, from the repository root:

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=build/advtext-reference \\
        ../imgui-adv-text/.venv/bin/python -P -B tools/oracle/<script>
"""

from __future__ import annotations

import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ROOT_DIR = ROOT / "build" / "advtext-reference"
LOCK = Path(__file__).resolve().parent / "REFERENCE.lock"
STAMP = ROOT_DIR / ".pinned-commit"
SHOWN = "build/advtext-reference"

INVOCATION = (
    "PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=build/advtext-reference ../imgui-adv-text/.venv/bin/python -P -B"
)

_verified: dict[str, str] | None = None


def _refuse(why: str) -> None:
    sys.stderr.write(
        "pinned_reference: REFUSING TO RUN: " + why + "\n"
        "pinned_reference: the oracle is the pinned tree only. From the repository root:\n"
        "pinned_reference:     tools/oracle/pin_reference.sh\n"
        f"pinned_reference:     {INVOCATION} tools/oracle/<script>\n"
    )
    sys.stderr.flush()
    raise SystemExit(2)


def _fields(path: Path) -> dict[str, str]:
    out: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#") and "=" in line:
            key, _, value = line.partition("=")
            out[key] = value
    return out


def tree_digest(root: Path) -> tuple[int, str]:
    """
    The number of files under `root`, the stamp aside, and the SHA-256 of the
    listing `<sha256>  <path>` of them sorted by path as bytes: what
    pin_reference.sh computes with find, sort and sha256sum.
    """
    paths = sorted(
        (path.relative_to(root).as_posix().encode("utf-8"), path)
        for path in root.rglob("*")
        if path.is_file() and path != root / STAMP.name
    )
    listing = hashlib.sha256()
    for name, path in paths:
        listing.update(hashlib.sha256(path.read_bytes()).hexdigest().encode("ascii") + b"  " + name + b"\n")
    return len(paths), listing.hexdigest()


def lock() -> dict[str, str]:
    if not LOCK.is_file():
        _refuse(f"{LOCK} is missing")
    fields = _fields(LOCK)
    for key in ("tag", "commit", "tree", "files", "sha256"):
        if key not in fields:
            _refuse(f"{LOCK} has no `{key}=` line")
    return fields


def require() -> dict[str, str]:
    """
    Verify the pinned tree and that `advtext` is imported from it, or exit 2.
    Returns the lock's fields. The tree is hashed once per process.
    """
    global _verified
    if _verified is not None:
        return _verified
    locked = lock()
    if not sys.dont_write_bytecode:
        _refuse("the interpreter would write __pycache__ into the pinned tree; run it with -B")
    if not ROOT_DIR.is_dir():
        _refuse(f"{SHOWN} does not exist; run tools/oracle/pin_reference.sh")
    if not STAMP.is_file():
        _refuse(f"{SHOWN} has no .pinned-commit stamp: it was not written by pin_reference.sh")
    stamped = _fields(STAMP).get("commit", "")
    if stamped != locked["commit"]:
        _refuse(f"{SHOWN} holds commit {stamped or '(none)'}, REFERENCE.lock pins {locked['commit']}")
    count, digest = tree_digest(ROOT_DIR)
    if digest != locked["sha256"]:
        _refuse(
            f"{SHOWN} does not match REFERENCE.lock ({count} files, digest {digest}; locked {locked['files']} "
            f"files, {locked['sha256']}): a file was added, removed or edited. Remove the directory and pin again"
        )

    already = sys.modules.get("advtext")
    if already is None:
        try:
            import advtext as already  # noqa: PLC0415 - the import is the thing being checked
        except ImportError as error:
            _refuse(f"`import advtext` failed ({error}); PYTHONPATH must be {SHOWN}")
    origin = Path(getattr(already, "__file__", None) or "/nonexistent").resolve()
    package = (ROOT_DIR / "advtext").resolve()
    if package not in origin.parents:
        _refuse(
            f"`advtext` was imported from {origin.parent}, not from {SHOWN}/advtext. "
            f"PYTHONPATH must be {SHOWN} (without it the interpreter's editable install of "
            "../imgui-adv-text is what gets imported)"
        )
    _verified = locked
    return locked


def require_all_modules() -> None:
    """
    Every `advtext` module imported so far comes from the pinned tree, or exit
    2. Called at the end of a dump: `require()` checks the package before the
    imports, this checks what the imports actually loaded.
    """
    locked = require()
    package = (ROOT_DIR / "advtext").resolve()
    for name, module in sorted(sys.modules.items()):
        if name != "advtext" and not name.startswith("advtext."):
            continue
        origin = getattr(module, "__file__", None)
        if origin is None:
            continue
        if package not in Path(origin).resolve().parents:
            _refuse(f"module {name} was loaded from {origin}, outside {SHOWN} (commit {locked['commit']})")


def header_lines() -> list[str]:
    """
    What a golden's header says about the reference it was dumped from.
    """
    locked = require()
    return [
        f"Reference: imgui-adv-text commit {locked['commit']}.",
        f"Reference tree: tag {locked['tag']}, extracted into {SHOWN} and verified against "
        f"tools/oracle/REFERENCE.lock ({locked['files']} files).",
    ]
