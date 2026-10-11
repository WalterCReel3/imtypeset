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
  - a file or a symbolic link in it was added, removed or edited (one SHA-256
    over the SHA-256 of every one, compared with the lock; a link is hashed
    as where it points and is never followed);
  - `import advtext` resolves anywhere else. The reference's interpreter has
    an editable install of ../imgui-adv-text, so that is what a run without
    PYTHONPATH=build/advtext-reference imports;
  - any `advtext` module is found anywhere else, at the moment it is
    imported. `require()` puts a finder first on `sys.meta_path` that looks
    at where each `advtext` or `advtext.*` module is about to be loaded from
    and exits before it is. A script therefore never runs a line of a module
    from outside the pinned tree, whichever of its imports would have reached
    one and however late: nothing has been dumped with it, and nothing is
    written after it;
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
import os
import sys
from importlib.machinery import ModuleSpec
from pathlib import Path
from types import ModuleType

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


LINK_PREFIX = b"symlink\0"


def tree_digest(root: Path) -> tuple[int, str]:
    """
    The number of files and symbolic links under `root`, the stamp aside, and
    the SHA-256 of the listing `<sha256>  <path>` of them sorted by path as
    bytes: what pin_reference.sh computes with find, sort and sha256sum.

    A regular file is hashed as its bytes. A symbolic link -- to a file, to a
    directory, or to nothing -- is one entry, hashed as `symlink`, a NUL and
    where it points, and is not followed: what is behind it is not in the
    tree. Both walks do exactly this, so a link added to the pinned tree
    changes the digest in both and a tree with none hashes as its files alone.
    Anything else (a directory, a socket, a FIFO) is no entry in either.
    """
    entries: list[tuple[bytes, Path]] = []
    for here, directories, files in os.walk(root, followlinks=False):
        for name in (*directories, *files):
            path = Path(here) / name
            if path == root / STAMP.name:
                continue
            if path.is_symlink() or path.is_file():
                entries.append((os.fsencode(path.relative_to(root).as_posix()), path))
    listing = hashlib.sha256()
    for name, path in sorted(entries):
        if path.is_symlink():
            digest = hashlib.sha256(LINK_PREFIX + os.fsencode(os.readlink(path))).hexdigest()
        else:
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
        listing.update(digest.encode("ascii") + b"  " + name + b"\n")
    return len(entries), listing.hexdigest()


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
        _install_guard()
        try:
            import advtext as already  # noqa: PLC0415 - the import is the thing being checked
        except ImportError as error:
            _refuse(f"`import advtext` failed ({error}); PYTHONPATH must be {SHOWN}")
    _verified = locked
    _install_guard()
    # What was imported before the guard was there to see it.
    require_all_modules()
    return locked


def _is_ours(name: str) -> bool:
    return name == "advtext" or name.startswith("advtext.")


def _inside(location: str | None) -> bool:
    if not location:
        return False
    package = (ROOT_DIR / "advtext").resolve()
    found = Path(location).resolve()
    return found == package or package in found.parents


def _outside(name: str, origin: str | None, locations: list[str] | None) -> str | None:
    """
    Where module `name` is from if that is not the pinned tree, else None.
    `origin` is its file; `locations` what a package searches for submodules.
    A module with neither cannot be placed, and is refused as such.
    """
    places = [origin] if origin else []
    places += list(locations or ())
    if not places:
        return "nowhere on disk (no file and no search path)"
    for place in places:
        if not _inside(place):
            return place
    return None


def _refuse_module(name: str, where: str) -> None:
    hint = ""
    if name == "advtext":
        hint = (
            f" PYTHONPATH must be {SHOWN} (without it the interpreter's editable install of "
            "../imgui-adv-text is what gets imported)"
        )
    _refuse(f"module `{name}` is from {where}, outside {SHOWN}/advtext.{hint}")


class _Guard:
    """
    First on `sys.meta_path`: asks the finders after it where an `advtext`
    module would come from, and exits 2 if that is not the pinned tree.
    Otherwise it returns what they found, so the import goes on as it would
    have. Every other name is passed over.
    """

    def find_spec(
        self, name: str, path: object = None, target: ModuleType | None = None
    ) -> ModuleSpec | None:
        if not _is_ours(name):
            return None
        for finder in list(sys.meta_path):
            find = getattr(finder, "find_spec", None)
            if finder is self or find is None:
                continue
            spec = find(name, path, target)
            if spec is None:
                continue
            origin = spec.origin if spec.has_location else None
            where = _outside(name, origin, spec.submodule_search_locations)
            if where is not None:
                _refuse_module(name, where)
            return spec
        return None


_guard: _Guard | None = None


def _install_guard() -> None:
    global _guard
    if _guard is None:
        _guard = _Guard()
    if not sys.meta_path or sys.meta_path[0] is not _guard:
        if _guard in sys.meta_path:
            sys.meta_path.remove(_guard)
        sys.meta_path.insert(0, _guard)


def require_all_modules() -> None:
    """
    Every `advtext` module loaded so far comes from the pinned tree, and the
    import guard is still first on `sys.meta_path`; or exit 2.

    The guard refuses a module as it is imported. This is the same question
    asked of `sys.modules`, for what the guard cannot see: a module put there
    by hand, or imported while something had moved the guard aside. A script
    calls it after its last import and BEFORE it writes its first file.
    """
    require()
    _install_guard()
    for name, module in sorted(sys.modules.items()):
        if not _is_ours(name) or module is None:
            continue
        origin = getattr(module, "__file__", None)
        locations = getattr(module, "__path__", None)
        where = _outside(name, origin, list(locations) if locations is not None else None)
        if where is not None:
            _refuse_module(name, where)


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
