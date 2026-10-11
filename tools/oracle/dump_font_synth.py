"""
Dump fontTools' reading of hand-made `cmap` and `post` tables, for the parts of
the C++ OpenType reader that no installed font reaches.

Run from the repository root, with the reference's interpreter:

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=build/advtext-reference \
        ../imgui-adv-text/.venv/bin/python -P -B tools/oracle/dump_font_synth.py

Writes tests/golden/font-synth/synth.golden.

WHY. In the real-font corpus (tests/golden/font-wild) the picked cmap subtable
is always format 4 or 12 and always well formed, and glyph names decide nothing
unless a font has a MATH table. So the corpus cannot say whether the reader
takes a format 0, 6 or 13 subtable, an out-of-order format 12 group, or the
names of a format 2 post table with unnamed and repeated glyphs, as fontTools
(and so the reference) does. Each case here is one table, written below byte by
byte, put in place of that table in a bundled font
(tests/data/fonts/Roboto-Regular.ttf), and read back with fontTools. The golden
holds the table's bytes and fontTools' answer; tests/test_opentype_wild.cpp
puts the same bytes into the same font and compares. No font file is written.

Records (shared golden format; strings percent-encoded; `h64` is the word-wise
FNV-1a of tools/oracle/FORMAT-font-wild.md):

  CASE    name, the tag of the table replaced
  TABLE   the replacement table, as hex
  PICK    platform, encoding, format of the subtable `getBestCmap()` reads
  CMAP    n, then n of (codepoint, glyph id), ascending: `getBestCmap()`
  NAMES   glyphs, h64 over every name in glyph order (each byte a word, then
          the word 0x10000 after each name)
  NAME    glyph id, name: every glyph a case is about, and a spread of others
  ORDER   n, then n glyph ids: those glyphs sorted by name, as Python sorts
          strings (what the reference's minting order is made from)
  RAISES  exception type: fontTools refused the table
  END     name
"""

from __future__ import annotations

import io
import logging
import struct
import sys
from pathlib import Path
from typing import Any

import fontTools
import fontTools.ttLib

ROOT = Path(__file__).resolve().parents[2]
FONT = ROOT / "tests" / "data" / "fonts" / "Roboto-Regular.ttf"
OUT = ROOT / "tests" / "golden" / "font-synth"

_MASK = (1 << 64) - 1


def h64(words: Any) -> str:
    h = 0xCBF29CE484222325
    for w in words:
        h = ((h ^ (w & _MASK)) * 0x100000001B3) & _MASK
    return f"{h:016x}"


def s(text: str) -> str:
    data = text.encode("utf-8")
    if not data:
        return "%"
    safe = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-"
    return "".join(chr(b) if b in safe else f"%{b:02X}" for b in data)


def replace_table(font: bytes, tag: bytes, table: bytes) -> bytes:
    """The sfnt with one table's bytes replaced; checksums are left as they were."""
    count = struct.unpack_from(">H", font, 4)[0]
    records = []
    for i in range(count):
        t, _, offset, length = struct.unpack_from(">4sIII", font, 12 + 16 * i)
        records.append((t, table if t == tag else font[offset : offset + length]))
    assert any(t == tag for t, _ in records)
    out = bytearray(font[:12])
    at = 12 + 16 * count
    body = bytearray()
    for t, data in records:
        out += struct.pack(">4sIII", t, 0, at + len(body), len(data))
        body += data + b"\0" * (-len(data) % 4)
    return bytes(out + body)


def cmap_table(subtables: list[tuple[int, int, bytes]]) -> bytes:
    header = struct.pack(">HH", 0, len(subtables))
    at = 4 + 8 * len(subtables)
    body = b""
    for platform, encoding, data in subtables:
        header += struct.pack(">HHI", platform, encoding, at + len(body))
        body += data
    return header + body


def format0(glyphs: list[int]) -> bytes:
    assert len(glyphs) == 256
    return struct.pack(">HHH", 0, 262, 0) + bytes(glyphs)


def format4(segments: list[tuple[int, int, int]]) -> bytes:
    """(first, last, first glyph) segments, by idDelta; the terminator is added."""
    segments = segments + [(0xFFFF, 0xFFFF, 1)]
    n = len(segments)
    ends = b"".join(struct.pack(">H", last) for _, last, _ in segments)
    starts = b"".join(struct.pack(">H", first) for first, _, _ in segments)
    deltas = b"".join(struct.pack(">H", (glyph - first) & 0xFFFF) for first, _, glyph in segments)
    body = ends + b"\0\0" + starts + deltas + b"\0\0" * n
    return struct.pack(">HHHHHHH", 4, 14 + len(body), 0, n * 2, 0, 0, 0) + body


def format6(first: int, glyphs: list[int]) -> bytes:
    return struct.pack(">HHHHH", 6, 10 + 2 * len(glyphs), 0, first, len(glyphs)) + b"".join(
        struct.pack(">H", g) for g in glyphs
    )


def format12or13(fmt: int, groups: list[tuple[int, int, int]]) -> bytes:
    body = b"".join(struct.pack(">III", *g) for g in groups)
    return struct.pack(">HHIII", fmt, 0, 16 + len(body), 0, len(groups)) + body


def post2(indices: list[int], strings: list[bytes], stated: int | None = None) -> bytes:
    header = struct.pack(">IIhhIIIII", 0x00020000, 0, -150, 100, 0, 0, 0, 0, 0)
    count = len(indices) if stated is None else stated
    return (
        header
        + struct.pack(">H", count)
        + b"".join(struct.pack(">H", i) for i in indices)
        + b"".join(bytes([len(x)]) + x for x in strings)
    )


def cases(glyphs: int) -> list[tuple[str, bytes, bytes, list[int]]]:
    """(name, tag, table, glyph ids the case is about)."""
    out: list[tuple[str, bytes, bytes, list[int]]] = []

    def cmap(name: str, subtables: list[tuple[int, int, bytes]]) -> None:
        out.append((name, b"cmap", cmap_table(subtables), []))

    # Format 0: a glyph per byte code; glyph 0 is "not mapped".
    table = [0] * 256
    for code in range(0x20, 0x7F):
        table[code] = code - 0x1C
    table[0x41] = 0
    table[0xE9] = 200
    cmap("cmap-format-0", [(0, 3, format0(table))])
    # Format 6: a trimmed array, with holes that are glyph 0.
    cmap("cmap-format-6", [(3, 1, format6(0x40, [5, 6, 0, 8, 9, 0, 0, 12, 300, 301]))])
    # Format 12: groups in and out of order, touching, inverted, to glyph 0,
    # and past the end of Unicode.
    cmap(
        "cmap-format-12-groups",
        [
            (
                3,
                10,
                format12or13(
                    12,
                    [
                        (0x41, 0x50, 10),  # taken
                        (0x50, 0x55, 100),  # starts on the last code taken: replaces it
                        (0x30, 0x35, 5),  # starts before the previous end: left out
                        (0x60, 0x5F, 7),  # inverted: left out
                        (0x70, 0x75, 0),  # glyph 0: the first code maps nothing
                        (0x80, 0x80, 0),  # glyph 0, one code: nothing
                        (0x90, 0x95, 40),
                        (0x95, 0x97, 0),  # touches the previous end with glyph 0
                        (0x1F600, 0x1F60F, 500),
                        (0x10FFF0, 0x110005, 600),  # cut off at U+10FFFF
                    ],
                ),
            )
        ],
    )
    # Format 13: one glyph for every code of a group.
    cmap(
        "cmap-format-13-groups",
        [
            (
                0,
                6,
                format12or13(
                    13,
                    [
                        (0x41, 0x45, 7),
                        (0x45, 0x48, 9),  # replaces U+0045
                        (0x50, 0x60, 0),  # glyph 0: none of it
                        (0x2000, 0x2003, 11),
                        (0x10FFFE, 0x110000, 12),
                    ],
                ),
            )
        ],
    )
    # Which subtable is read when several are Unicode.
    low = format4([(0x41, 0x5A, 30)])
    mid = format4([(0x41, 0x5A, 60)])
    full = format12or13(12, [(0x41, 0x5A, 90), (0x1F600, 0x1F601, 400)])
    cmap("cmap-prefers-3-10", [(0, 3, low), (3, 1, mid), (3, 10, full)])
    cmap("cmap-prefers-0-4-over-3-1", [(0, 3, low), (0, 4, full), (3, 1, mid)])
    cmap("cmap-prefers-3-1-over-0-3", [(0, 3, low), (1, 0, format0(table)), (3, 1, mid)])
    cmap("cmap-first-record-of-a-pair-wins", [(3, 1, low), (3, 1, mid)])

    # post format 2. Indices below 258 are the standard Macintosh names; from
    # 258 they count the strings that follow.
    strings = [b"alpha", b"beta", b"alpha", b"alpha.1", b"", b"zeta", b"beta", b"alpha"]
    about = list(range(0, 24))
    indices = [0] * glyphs
    for g in range(glyphs):
        indices[g] = 3 + g % 200  # standard names, repeating: every repeat is a duplicate
    for g, index in enumerate([0, 258, 259, 260, 261, 262, 263, 264, 265, 258, 258, 259, 36, 36, 36]):
        indices[g] = index
    out.append(("post-2-repeated-and-unnamed", b"post", post2(indices, strings), about))
    # Fewer names than glyphs, and an index whose string is not there.
    short = [0, 258, 259, 300, 258, 3]
    out.append(
        ("post-2-fewer-names-than-glyphs", b"post", post2(short, [b"one", b"two"]), list(range(0, 12)))
    )
    # A string cut off by the end of the table.
    out.append(
        (
            "post-2-string-runs-past-the-table",
            b"post",
            post2([0, 258, 259, 260] + [3] * (glyphs - 4), [b"whole", b"cut"])[:-2],
            list(range(0, 8)),
        )
    )
    return out


def main() -> int:
    logging.disable(logging.CRITICAL)
    base = FONT.read_bytes()
    probe = fontTools.ttLib.TTFont(io.BytesIO(base), lazy=True)
    glyphs = int(probe["maxp"].numGlyphs)
    probe.close()
    lines = [
        "# fontTools " + fontTools.version + "'s reading of hand-made cmap and post tables put into",
        "# tests/data/fonts/Roboto-Regular.ttf. Written by tools/oracle/dump_font_synth.py, which",
        "# documents every record. Do not edit.",
    ]
    for name, tag, table, about in cases(glyphs):
        lines.append(f"CASE {s(name)} {tag.decode('ascii')}")
        lines.append(f"TABLE {table.hex()}")
        try:
            font = fontTools.ttLib.TTFont(io.BytesIO(replace_table(base, tag, table)), lazy=True)
            if tag == b"cmap":
                cmap = font["cmap"]
                for platform, encoding in ((3, 10), (0, 6), (0, 4), (3, 1), (0, 3), (0, 2), (0, 1), (0, 0)):
                    sub = cmap.getcmap(platform, encoding)
                    if sub is not None:
                        lines.append(f"PICK {platform} {encoding} {sub.format}")
                        break
                best = font.getBestCmap()
                pairs = sorted((int(cp), font.getGlyphID(n)) for cp, n in best.items())
                lines.append("CMAP " + " ".join([str(len(pairs))] + [f"{cp} {g}" for cp, g in pairs]))
            else:
                order = font.getGlyphOrder()
                words: list[int] = []
                for n in order:
                    words += list(n.encode("latin-1")) + [0x10000]
                lines.append(f"NAMES {len(order)} {h64(words)}")
                shown = sorted(set(about) | set(range(0, len(order), max(1, len(order) // 16))))
                for g in shown:
                    lines.append(f"NAME {g} {s(order[g])}")
                by_name = sorted(shown, key=lambda g: order[g])
                lines.append("ORDER " + " ".join([str(len(by_name))] + [str(g) for g in by_name]))
            font.close()
        except Exception as error:
            lines.append(f"RAISES {s(type(error).__name__)}")
        lines.append(f"END {s(name)}")
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "synth.golden").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{sum(1 for x in lines if x.startswith('CASE'))} cases")
    return 0


if __name__ == "__main__":
    sys.exit(main())
