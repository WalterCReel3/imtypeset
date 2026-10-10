"""
Dump the legacy `kern` table of every corpus face that has one, as fontTools
reads it, for the C++ reader's `kern` extension.

Run from the repository root, with the reference's interpreter:

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=../imgui-adv-text \
        ../imgui-adv-text/.venv/bin/python -B tools/oracle/dump_kern_legacy.py

Writes tests/golden/font-wild-kern/kern.golden. Reads the system fonts and the
corpus digests in tests/golden/font-wild (for the list of faces: path, face
index, byte length, SHA-256) and writes to neither. A face whose file is absent
or no longer the file the digest names is left out and counted on stdout.

WHY THERE IS NO REFERENCE HERE. The Python reference (`advtext/fontlib.py`)
never reads the `kern` table: a font with only that table has no kerning there.
The C++ reader reads it as a deliberate extension (docs/divergences.md), so
what it is checked against is fontTools' own reading of the table
(`ttLib/tables/_k_e_r_n.py`), combined by the rule below, which is this
script's and is the rule the C++ states in src/opentype.h:

  - only a version 0 table;
  - only format 0 subtables whose coverage byte has bit 0 set (horizontal) and
    bits 1 (minimum values) and 2 (cross-stream) clear;
  - a pair's value is the sum over those subtables in order, except that a
    subtable with bit 3 (override) replaces the sum so far;
  - within one subtable a pair listed twice keeps its last value (fontTools
    builds a dict in file order).

The C++ uses these pairs only for a face with no GPOS pair kerning, which is
what `GPOSPAIR 0` says: the count is of the PairPos subtables the reference
holds for the face (`fontlib._read_kern_feature`).

Records, in the shared golden format (tools/oracle/FORMAT-font-wild.md: tokens
separated by spaces, strings percent-encoded, `h64` the word-wise FNV-1a):

  FACE       path, face index, faces in the file, byte length, SHA-256
  KERNTABLE  `ok` version subtables | exception type, message: fontTools could
             not read the table (the face then has no other record but END)
  KSUBTABLE  index, format, coverage byte, pairs fontTools holds, 1 if the rule
             above reads it
  GPOSPAIR   PairPos subtables the reference holds for the face
  KPAIRS     pairs after combining, h64 over (left glyph id, right glyph id,
             value) ascending by (left, right)
  KP         n, then n of (left, right, value): evenly spaced pairs of the
             combined table (at most 64), then pairs it does not list, value 0
  END        face index

The output is a function of the font files and fontTools and is byte-identical
on a re-run.
"""

from __future__ import annotations

import hashlib
import logging
import sys
from pathlib import Path
from typing import Any
from urllib.parse import unquote_to_bytes

import fontTools
import fontTools.ttLib

from advtext import fontlib

ROOT = Path(__file__).resolve().parents[2]
DIGESTS = ROOT / "tests" / "golden" / "font-wild"
OUT = ROOT / "tests" / "golden" / "font-wild-kern"

_FNV_OFFSET = 0xCBF29CE484222325
_FNV_PRIME = 0x100000001B3
_MASK = (1 << 64) - 1


def h64(words: Any) -> str:
    h = _FNV_OFFSET
    for w in words:
        h = ((h ^ (w & _MASK)) * _FNV_PRIME) & _MASK
    return f"{h:016x}"


def s(text: str) -> str:
    data = text.encode("utf-8")
    if not data:
        return "%"
    safe = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-"
    return "".join(chr(b) if b in safe else f"%{b:02X}" for b in data)


def corpus_faces() -> list[tuple[str, int, int, int, str, bool]]:
    """(path, index, faces, bytes, sha256, has a kern table) of every digest face."""
    faces = []
    for path in sorted(DIGESTS.glob("*.golden")):
        if path.name == "index.golden":
            continue
        current = None
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.startswith("FACE "):
                t = line.split(" ")
                current = [unquote_to_bytes(t[1]).decode("utf-8"), int(t[2]), int(t[3]), int(t[4]), t[5], False]  # fmt: skip
            elif line.startswith("LEGACYKERN ") and current is not None:
                current[5] = line.split(" ")[1] == "1"
            elif line.startswith("END") and current is not None:
                faces.append(tuple(current))
                current = None
    return faces


def dump_face(path: str, index: int, count: int) -> list[str]:
    lines: list[str] = []
    font = fontTools.ttLib.TTFont(path, fontNumber=index if count > 1 else -1, lazy=True)
    try:
        try:
            kern = font["kern"]
            tables = list(kern.kernTables)
            version = kern.version
        except Exception as error:  # fontTools could not read it
            lines.append(f"KERNTABLE {s(type(error).__name__)} {s(str(error))}")
            return lines
        lines.append(f"KERNTABLE ok {int(version)} {len(tables)}")
        combined: dict[tuple[int, int], int] = {}
        for i, sub in enumerate(tables):
            fmt = getattr(sub, "format", -1)
            coverage = getattr(sub, "coverage", 0)
            pairs = getattr(sub, "kernTable", None) if fmt == 0 else None
            read = int(version == 0 and fmt == 0 and pairs is not None and (coverage & 0x07) == 0x01)
            lines.append(f"KSUBTABLE {i} {fmt} {coverage} {len(pairs) if pairs is not None else 0} {read}")  # fmt: skip
            if not read:
                continue
            assert pairs is not None
            for (left, right), value in pairs.items():
                key = (font.getGlyphID(left), font.getGlyphID(right))
                if coverage & 0x08:
                    combined[key] = int(value)
                else:
                    combined[key] = combined.get(key, 0) + int(value)
        lines.append(f"GPOSPAIR {len(fontlib._read_kern_feature(font))}")
        ordered = sorted(combined.items())
        words: list[int] = []
        for (left, right), value in ordered:
            words += [left, right, value]
        lines.append(f"KPAIRS {len(ordered)} {h64(words)}")
        sample: list[tuple[int, int, int]] = []
        if ordered:
            step = max(1, len(ordered) // 64)
            for (left, right), value in ordered[::step][:64]:
                sample.append((left, right, value))
        glyphs = len(font.getGlyphOrder())
        absent = [(0, 0), (glyphs - 1, glyphs - 1), (0, glyphs - 1)]
        for left, right, _ in sample[:16]:
            absent.append((right, left))
            absent.append((left, (right + 1) % glyphs))
        for key in absent:
            if key not in combined and (key[0], key[1], 0) not in sample:
                sample.append((key[0], key[1], 0))
        lines.append("KP " + " ".join([str(len(sample))] + [f"{a} {b} {v}" for a, b, v in sample]))
    finally:
        font.close()
    return lines


def main() -> int:
    logging.disable(logging.CRITICAL)
    faces = corpus_faces()
    out = [
        "# Legacy `kern` tables of the real-font corpus, as fontTools " + fontTools.version + " reads them.",
        "# Written by tools/oracle/dump_kern_legacy.py, which documents every record. Do not edit.",
    ]  # fmt: skip
    dumped = skipped = 0
    for path, index, count, size, sha, has_kern in faces:
        if not has_kern:
            continue
        try:
            data = Path(path).read_bytes()
        except OSError:
            skipped += 1
            continue
        if len(data) != size or hashlib.sha256(data).hexdigest() != sha:
            skipped += 1
            continue
        out.append(f"FACE {s(path)} {index} {count} {size} {sha}")
        out += dump_face(path, index, count)
        out.append(f"END {index}")
        dumped += 1
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "kern.golden").write_text("\n".join(out) + "\n", encoding="utf-8")
    print(f"{len(faces)} corpus faces; {dumped} with a kern table dumped; {skipped} absent or changed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
