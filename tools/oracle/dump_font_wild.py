"""
Dump a compact digest of every font installed on this machine, as fontTools and
the Python reference's `advtext.fontlib` answer it, for the C++ OpenType reader's
corpus test.

Run from the repository root, with the reference's interpreter:

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=../imgui-adv-text \
        ../imgui-adv-text/.venv/bin/python -B tools/oracle/dump_font_wild.py

Writes tests/golden/font-wild/*.golden. tools/oracle/FORMAT-font-wild.md
documents every record. Reads the reference and the system fonts and writes to
neither. Options: `--jobs N` (default: all cores), `--limit SECONDS` per face
(default 900), `--only SUBSTRING` to dump the faces whose path contains it into
`$TMPDIR` instead of the golden directory, for looking at one font.

The corpus is what `fc-list : file` reports. Fonts are not copied: each face is
identified by absolute path, face index, byte length, SHA-256 and CRC-32, so a
test on another machine, or after a package upgrade, can tell that it is not
reading the bytes the golden was made from and skip.

Per face there are three sources, kept apart by record name:

- BYTES: read here with `struct` from the file. What the file holds.
- fontTools: whether `TTFont` opens it, and the cmap subtables as it lists them.
- fontlib: the reference, with the file registered for a role through
  `fontlib.register_font`, read back as `fontlib._face_data(role)` -- the one
  record every public answer of the reference is computed from. Values are in
  font units and by glyph id; the scaling to a size is `value * (size /
  unitsPerEm)` and is covered by tests/golden/font.

The output is a function of the font files, fontTools and the reference alone
and is byte-identical on a re-run, unless a face hits the time limit.
"""

from __future__ import annotations

import collections
import hashlib
import logging
import multiprocessing
import os
import re
import signal
import struct
import subprocess
import sys
import zlib
from pathlib import Path
from typing import Any

import fontTools
import fontTools.ttLib

from advtext import fontlib
from advtext.fontmetrics import Face

# The conventions of the bundled-font oracle: `s` percent-encodes a string
# token, `f` writes a float as `float.hex()`, `Lcg` is the generator behind
# every "deterministic sample".
from dump_font import Lcg, f, s

OUT = Path(__file__).resolve().parents[2] / "tests" / "golden" / "font-wild"

SFNT_SUFFIXES = (".ttf", ".otf", ".ttc")
TYPE1_SUFFIXES = (".pfa", ".pfb", ".t1")

# Files per golden. A directory's files are sorted and cut into runs of this
# many, so no golden is unwieldy and a directory with one font is one file.
SHARD_FILES = 96

CMAP_SAMPLES = 32
HMTX_SAMPLES = 32
BOX_SAMPLES = 64
BOX_CHUNKS = 16
KERN_SAMPLES = 256
KERN_SAMPLES_UNKERNED = 8
MATH_VARIANT_SAMPLES = 24
MATH_ITALIC_SAMPLES = 16
SYN_SAMPLES = 16

# The size MathParams is dumped at. Not a power of two times unitsPerEm for
# any real font, so `size / unitsPerEm` is inexact and the order of operations
# shows.
MATH_SIZE = 10.0

CMAP_PREFERENCE = ((3, 10), (0, 6), (0, 4), (3, 1), (0, 3), (0, 2), (0, 1), (0, 0))

_MASK = (1 << 64) - 1
_FNV_OFFSET = 0xCBF29CE484222325
_FNV_PRIME = 0x100000001B3


def h64(words: Any) -> str:
    """
    FNV-1a with a 64-bit word, not a byte, folded per step.

    `h = offset; for w: h = ((h ^ (w mod 2^64)) * prime) mod 2^64`, printed as 16
    lower-case hex digits. A negative integer enters as its two's complement.
    """
    h = _FNV_OFFSET
    for w in words:
        h = ((h ^ (w & _MASK)) * _FNV_PRIME) & _MASK
    return f"{h:016x}"


def double_bits(value: float) -> int:
    """The IEEE-754 bits of a double, with -0.0 folded onto +0.0."""
    return struct.unpack("<Q", struct.pack("<d", value + 0.0))[0]


class FaceTimeout(BaseException):
    """Raised by SIGALRM. Not an `Exception`, so nothing in fontTools swallows it."""


def _on_alarm(_signum: int, _frame: Any) -> None:
    raise FaceTimeout()


_ADDRESS = re.compile(r" at 0x[0-9a-fA-F]+")


def exc_tokens(exc: BaseException) -> tuple[str, str]:
    """(type name, message) as two string tokens. The message is cut at 160 characters."""
    text = _ADDRESS.sub(" at 0x?", " ".join(str(exc).split()))[:160]
    return type(exc).__name__, s(text)


# --------------------------------------------------------------------------------
# BYTES: the sfnt wrapper, read without fontTools
# --------------------------------------------------------------------------------


class Sfnt:
    """One face's table directory inside a file's bytes."""

    def __init__(self, blob: bytes, offset: int) -> None:
        self.blob = blob
        self.version = blob[offset : offset + 4]
        (count,) = struct.unpack_from(">H", blob, offset + 4)
        self.tables: dict[str, tuple[int, int]] = {}
        self.order: list[str] = []
        for i in range(count):
            tag, _sum, at, length = struct.unpack_from(">4sIII", blob, offset + 12 + 16 * i)
            name = tag.decode("latin-1")
            self.order.append(name)
            self.tables.setdefault(name, (at, length))

    def table(self, tag: str) -> bytes | None:
        entry = self.tables.get(tag)
        if entry is None:
            return None
        return self.blob[entry[0] : entry[0] + entry[1]]


def face_offsets(blob: bytes) -> list[int] | None:
    """Offsets of each face's table directory, or None if this is not an sfnt."""
    magic = blob[:4]
    if magic == b"ttcf":
        (count,) = struct.unpack_from(">I", blob, 8)
        return list(struct.unpack_from(f">{count}I", blob, 12))
    if magic in (b"\x00\x01\x00\x00", b"OTTO", b"true", b"typ1"):
        return [0]
    return None


def sfnt_version_token(version: bytes) -> str:
    return {
        b"\x00\x01\x00\x00": "0x00010000",
        b"OTTO": "OTTO",
        b"true": "true",
        b"typ1": "typ1",
    }.get(version, s(version.decode("latin-1")))


def u16(data: bytes, at: int) -> int:
    return struct.unpack_from(">H", data, at)[0]


_POST_FORMATS = {
    0x00010000: "post1",
    0x00020000: "post2",
    0x00025000: "post2.5",
    0x00030000: "post3",
    0x00040000: "post4",
}

_COMPONENT_BITS = (
    (0x0001, "ARG_1_AND_2_ARE_WORDS"),
    (0x0002, "ARGS_ARE_XY_VALUES"),
    (0x0004, "ROUND_XY_TO_GRID"),
    (0x0008, "WE_HAVE_A_SCALE"),
    (0x0040, "WE_HAVE_AN_X_AND_Y_SCALE"),
    (0x0080, "WE_HAVE_A_TWO_BY_TWO"),
    (0x0100, "WE_HAVE_INSTRUCTIONS"),
    (0x0200, "USE_MY_METRICS"),
    (0x0400, "OVERLAP_COMPOUND"),
    (0x0800, "SCALED_COMPONENT_OFFSET"),
    (0x1000, "UNSCALED_COMPONENT_OFFSET"),
)


def glyf_scan(
    sfnt: Sfnt, num_glyphs: int, loca_format: int, n_hmetrics: int
) -> tuple[list[int | None], dict[str, int]]:
    """
    numberOfContours per glyph id (None for a glyph with no data), and a census
    of the composite features a reader has to handle, both from the bytes.
    """
    glyf = sfnt.table("glyf")
    loca = sfnt.table("loca")
    assert glyf is not None and loca is not None
    if loca_format == 0:
        offsets = [2 * v for v in struct.unpack_from(f">{num_glyphs + 1}H", loca, 0)]
    else:
        offsets = list(struct.unpack_from(f">{num_glyphs + 1}I", loca, 0))
    census: collections.Counter[str] = collections.Counter()
    contours: list[int | None] = []
    children: dict[int, list[int]] = {}
    hmtx = sfnt.table("hmtx") or b""

    def lsb(gid: int) -> int | None:
        at = 4 * gid + 2 if gid < n_hmetrics else 4 * n_hmetrics + 2 * (gid - n_hmetrics)
        return struct.unpack_from(">h", hmtx, at)[0] if at + 2 <= len(hmtx) else None

    for gid in range(num_glyphs):
        start, end = offsets[gid], offsets[gid + 1]
        if end - start < 10:
            contours.append(None)
            census["no_data"] += 1
            if end != start:
                census["data_shorter_than_header"] += 1
            continue
        n, x_min = struct.unpack_from(">hh", glyf, start)
        contours.append(n)
        if n > 0:
            census["simple"] += 1
            # fontTools draws a simple glyph shifted in x by lsb - xMin.
            if lsb(gid) not in (None, x_min):
                census["simple_with_lsb_not_xmin"] += 1
            continue
        if n == 0:
            census["simple_zero_contours"] += 1
            continue
        census["composite"] += 1
        if n != -1:
            census["composite_contours_below_minus_one"] += 1
        at = start + 10
        seen = 0
        children[gid] = []
        while True:
            flag, child = struct.unpack_from(">HH", glyf, at)
            at += 4
            children[gid].append(child)
            census["components"] += 1
            seen |= flag
            if not flag & 0x0002:
                census["components_placed_by_point_matching"] += 1
            if child >= num_glyphs:
                census["components_naming_a_glyph_out_of_range"] += 1
            at += 4 if flag & 0x0001 else 2
            if flag & 0x0008:
                at += 2
            elif flag & 0x0040:
                at += 4
            elif flag & 0x0080:
                at += 8
            if not flag & 0x0020:
                break
        for bit, name in _COMPONENT_BITS:
            if seen & bit:
                census[f"glyphs_with_{name}"] += 1

    depths: dict[int, int] = {}

    def depth(gid: int, guard: int) -> int:
        if gid in depths:
            return depths[gid]
        if guard > 64:
            census["component_cycle_or_depth_over_64"] = 1
            return 0
        d = 1 + max((depth(c, guard + 1) for c in children[gid] if c in children), default=0)
        depths[gid] = d
        return d

    census["max_component_depth"] = max((depth(g, 0) for g in children), default=0)
    return contours, dict(census)


def legacy_kern(data: bytes) -> tuple[int, int, list[int], int]:
    """(version, subtable count, subtable formats, pairs in format 0 subtables)."""
    version = u16(data, 0)
    formats: list[int] = []
    pairs = 0
    if version == 0:
        count = u16(data, 2)
        at = 4
        for _ in range(count):
            if at + 6 > len(data):
                break
            _ver, length, coverage = struct.unpack_from(">HHH", data, at)
            fmt = coverage >> 8
            formats.append(fmt)
            if fmt == 0 and at + 8 <= len(data):
                pairs += u16(data, at + 6)
            if length == 0:
                break
            at += length
        return 0, count, formats, pairs
    # Apple's: a 16.16 version and 32-bit counts.
    (count,) = struct.unpack_from(">I", data, 4)
    at = 8
    for _ in range(min(count, 4096)):
        if at + 8 > len(data):
            break
        length, coverage, _tuple = struct.unpack_from(">IHH", data, at)
        fmt = coverage & 0xFF
        formats.append(fmt)
        if fmt == 0 and at + 10 <= len(data):
            pairs += u16(data, at + 8)
        if length == 0:
            break
        at += length
    return 1, count, formats, pairs


def cmap_picked_offset(cmap: bytes) -> tuple[int, int, int] | None:
    """(platform, encoding, offset) of the subtable `getBestCmap()` picks: the first
    record, in file order, of the first preferred (platform, encoding) present."""
    count = u16(cmap, 2)
    records = [struct.unpack_from(">HHI", cmap, 4 + 8 * i) for i in range(count)]
    for wanted in CMAP_PREFERENCE:
        for platform, encoding, offset in records:
            if (platform, encoding) == wanted:
                return platform, encoding, offset
    return None


def cmap_by_the_specification(cmap: bytes, offset: int) -> tuple[int, dict[int, int]] | None:
    """
    The subtable at `offset` read as the OpenType specification describes a
    lookup, with nothing fontTools adds: (format, codepoint -> glyph id),
    glyph 0 included. None for a format other than 4 and 12.

    Format 4: a codepoint belongs to the first segment whose endCode is not
    below it, if that segment's startCode is not above it; the final 0xFFFF
    segment is a segment like any other; an idRangeOffset index past the end
    of the table is glyph 0. Format 12: the first group containing the
    codepoint; a group is not clamped to U+10FFFF but this function stops
    there so a hostile range cannot run away.
    """
    fmt = u16(cmap, offset)
    out: dict[int, int] = {}
    if fmt == 4:
        length = u16(cmap, offset + 2)
        seg = u16(cmap, offset + 6) // 2
        ends = struct.unpack_from(f">{seg}H", cmap, offset + 14)
        starts = struct.unpack_from(f">{seg}H", cmap, offset + 16 + 2 * seg)
        deltas = struct.unpack_from(f">{seg}H", cmap, offset + 16 + 4 * seg)
        ranges_at = offset + 16 + 6 * seg
        ranges = struct.unpack_from(f">{seg}H", cmap, ranges_at)
        limit = min(len(cmap), offset + length) if length else len(cmap)
        claimed = -1  # highest codepoint an earlier segment's endCode covers
        for i in range(seg):
            for cp in range(max(starts[i], claimed + 1), ends[i] + 1):
                if ranges[i] == 0:
                    gid = (cp + deltas[i]) & 0xFFFF
                else:
                    at = ranges_at + 2 * i + ranges[i] + 2 * (cp - starts[i])
                    raw = u16(cmap, at) if at + 2 <= limit else 0
                    gid = (raw + deltas[i]) & 0xFFFF if raw else 0
                out[cp] = gid
            claimed = max(claimed, ends[i])
        return 4, out
    if fmt == 12:
        (groups,) = struct.unpack_from(">I", cmap, offset + 12)
        for i in range(groups):
            start, end, gid = struct.unpack_from(">III", cmap, offset + 16 + 12 * i)
            for cp in range(start, min(end, 0x10FFFF) + 1):
                out.setdefault(cp, gid + (cp - start))
        return 12, out
    return None


def gpos_lookup_shapes(gpos: bytes, lookups: list[int]) -> list[tuple[int, ...]]:
    """
    The on-disk shape of each subtable of the named lookups, from the bytes:

        (lookup, subtable, lookup_type, lookup_flag, is_extension, effective_type,
         pos_format, value_format1, value_format2, coverage_format,
         classdef1_format, classdef2_format, count1, count2)

    -1 where a field does not exist. Fields past pos_format are read for
    effective type 2 (PairPos) only. count1/count2 are PairSetCount/-1 for
    format 1 and Class1Count/Class2Count for format 2.
    """
    lookup_list = u16(gpos, 8)
    total = u16(gpos, lookup_list)
    out = []
    for li in lookups:
        if li >= total:
            continue
        lookup = lookup_list + u16(gpos, lookup_list + 2 + 2 * li)
        lookup_type, lookup_flag, sub_count = struct.unpack_from(">HHH", gpos, lookup)
        for si in range(sub_count):
            sub = lookup + u16(gpos, lookup + 6 + 2 * si)
            is_ext = lookup_type == 9
            effective = lookup_type
            if is_ext:
                _fmt, effective, ext_offset = struct.unpack_from(">HHI", gpos, sub)
                sub += ext_offset
            shape = [li, si, lookup_type, lookup_flag, int(is_ext), effective]
            if effective == 2:
                pos_format, cov, vf1, vf2 = struct.unpack_from(">HHHH", gpos, sub)
                cov_format = u16(gpos, sub + cov)
                if pos_format == 1:
                    shape += [pos_format, vf1, vf2, cov_format, -1, -1, u16(gpos, sub + 8), -1]
                else:
                    cd1, cd2, c1, c2 = struct.unpack_from(">HHHH", gpos, sub + 8)
                    shape += [
                        pos_format, vf1, vf2, cov_format,
                        u16(gpos, sub + cd1), u16(gpos, sub + cd2), c1, c2,
                    ]  # fmt: skip
            else:
                shape += [u16(gpos, sub), -1, -1, -1, -1, -1, -1, -1]
            out.append(tuple(shape))
    return out


# --------------------------------------------------------------------------------
# One face
# --------------------------------------------------------------------------------


class Digest:
    def __init__(self) -> None:
        self.lines: list[str] = []
        self.census: list[tuple[str, ...]] = []
        self.stage = "start"

    def rec(self, tag: str, *tokens: object) -> None:
        out = [tag]
        for token in tokens:
            if isinstance(token, bool):
                out.append("1" if token else "0")
            elif isinstance(token, int):
                out.append(str(token))
            elif isinstance(token, float):
                out.append(f(token))
            elif isinstance(token, str):
                out.append(token)  # already encoded by s(), or a bare keyword
            else:
                raise TypeError(f"{tag}: cannot write {token!r}")
        line = " ".join(out)
        assert "\n" not in line and "  " not in line, line
        self.lines.append(line)

    def count(self, *key: object) -> None:
        """One face counted under this census key."""
        self.census.append(tuple(str(k) for k in key))


def spaced(n: int, k: int) -> list[int]:
    """Up to k indices into range(n), first and last included, evenly spaced."""
    if n <= k:
        return list(range(n))
    return sorted({i * (n - 1) // (k - 1) for i in range(k)})


def dump_bytes(d: Digest, sfnt: Sfnt) -> dict[str, Any]:
    """The BYTES records. Returns what later stages need."""
    d.stage = "bytes"
    tags = set(sfnt.tables)
    if "glyf" in tags:
        flavour = "TrueType"
    elif "CFF " in tags:
        flavour = "CFF"
    elif "CFF2" in tags:
        flavour = "CFF2"
    else:
        flavour = "none"
    variable = "fvar" in tags
    d.rec("SFNT", sfnt_version_token(sfnt.version), flavour, variable)
    d.rec("TABLES", len(sfnt.order), *(s(t) for t in sfnt.order))
    d.count("flavour", flavour)
    d.count("sfnt_version", sfnt_version_token(sfnt.version))
    if variable:
        d.count("variable", flavour)
    for tag in ("CBDT", "EBDT", "sbix", "SVG ", "COLR", "bdat", "MATH", "GPOS", "GSUB", "kern",
                "morx", "kerx", "OS/2", "post", "VORG", "vhea", "gvar", "HVAR", "avar"):  # fmt: skip
        if tag in tags:
            d.count("has_table", tag)
    if "OS/2" not in tags:
        d.count("missing_table", "OS/2")

    info: dict[str, Any] = {"flavour": flavour, "num_glyphs": 0, "contours": None}

    head = sfnt.table("head")
    upm = loca_format = -1
    if head is not None and len(head) >= 54:
        upm = u16(head, 18)
        loca_format = struct.unpack_from(">h", head, 50)[0]
    maxp = sfnt.table("maxp")
    num_glyphs = u16(maxp, 4) if maxp is not None and len(maxp) >= 6 else -1
    hhea = sfnt.table("hhea")
    ascender = descender = line_gap = n_hmetrics = 0
    has_hhea = hhea is not None and len(hhea) >= 36
    if has_hhea:
        assert hhea is not None
        ascender, descender, line_gap = struct.unpack_from(">hhh", hhea, 4)
        n_hmetrics = u16(hhea, 34)
    os2 = sfnt.table("OS/2")
    os2_version = -1
    sx_height = 0
    has_sx = False
    if os2 is not None and len(os2) >= 2:
        os2_version = u16(os2, 0)
        if os2_version >= 2 and len(os2) >= 88:
            has_sx = True
            sx_height = struct.unpack_from(">h", os2, 86)[0]
    d.rec("HEAD", upm, loca_format)
    d.rec("MAXP", num_glyphs)
    d.rec("HHEA", has_hhea, ascender, descender, line_gap, n_hmetrics)
    d.rec("OS2", os2 is not None, os2_version, has_sx, sx_height)
    d.count("units_per_em", upm)
    d.count("os2_version", os2_version)
    if num_glyphs >= 0 and n_hmetrics < num_glyphs:
        d.count("hmtx_fewer_metrics_than_glyphs")
    info["num_glyphs"] = max(num_glyphs, 0)
    info["n_hmetrics"] = n_hmetrics

    post = sfnt.table("post")
    post_format = "none"
    if post is not None and len(post) >= 4:
        (v,) = struct.unpack_from(">I", post, 0)
        post_format = _POST_FORMATS.get(v, f"0x{v:08x}")
    d.rec("POST", post_format)
    info["post"] = post_format

    kern = sfnt.table("kern")
    if kern is None or len(kern) < 4:
        d.rec("LEGACYKERN", 0, -1, 0, 0, 0)
        info["legacy_kern"] = False
    else:
        try:
            version, count, formats, pairs = legacy_kern(kern)
            d.rec("LEGACYKERN", 1, version, count, pairs, len(formats), *formats)
            for fmt in sorted(set(formats)):
                d.count("legacy_kern_subtable_format", f"version{version}", f"format{fmt}")
        except struct.error as exc:
            d.rec("LEGACYKERN", 1, -1, 0, 0, 0)
            d.count("legacy_kern_unreadable", type(exc).__name__)
        info["legacy_kern"] = True

    if flavour == "TrueType" and num_glyphs > 0 and loca_format in (0, 1):
        try:
            contours, census = glyf_scan(sfnt, num_glyphs, loca_format, n_hmetrics)
            info["contours"] = contours
            d.rec("GLYF", len(census), *(x for k in sorted(census) for x in (k, census[k])))
            d.count("loca_format", "long" if loca_format else "short")
            for key, value in census.items():
                if value and key not in ("simple", "components", "max_component_depth", "no_data"):
                    d.count("glyf_faces_with", key)
            d.count("glyf_max_component_depth", census.get("max_component_depth", 0))
        except (struct.error, IndexError, AssertionError) as exc:
            d.rec("GLYFERR", *exc_tokens(exc))
            d.count("glyf_scan_failed", type(exc).__name__)
    return info


def dump_fonttools(d: Digest, path: str, index: int) -> Any:
    """Open with fontTools and list the cmap subtables. Returns the TTFont or None."""
    d.stage = "fontTools"
    try:
        font = fontTools.ttLib.TTFont(path, fontNumber=index, lazy=True)
        order = font.getGlyphOrder()
    except Exception as exc:
        d.rec("FONTTOOLS", *exc_tokens(exc))
        d.count("fonttools_open", type(exc).__name__)
        return None
    d.rec("FONTTOOLS", "ok", s("%"))
    d.count("fonttools_open", "ok")
    d.rec("GLYPHORDER", len(order))

    try:
        tables = font["cmap"].tables
        picked = None
        for platform, encoding in CMAP_PREFERENCE:
            picked = font["cmap"].getcmap(platform, encoding)
            if picked is not None:
                break
        for table in tables:
            size = len(table.cmap) if table.format != 14 else len(table.uvsDict)
            d.rec(
                "CMAPSUB", table.platformID, table.platEncID, table.format,
                int(getattr(table, "language", 0) or 0), size, table is picked,
            )  # fmt: skip
            d.count("cmap_subtable", f"format{table.format}")
        if picked is None:
            d.count("cmap_picked", "none")
        else:
            d.count(
                "cmap_picked", f"({picked.platformID},{picked.platEncID})", f"format{picked.format}"
            )
    except Exception as exc:
        d.rec("CMAPERR", *exc_tokens(exc))
        d.count("cmap_unreadable_by_fonttools", type(exc).__name__)
    return font


class _Indexed:
    """`fontTools.ttLib.TTFont` with a face index supplied; see `open_fontlib`."""

    def __init__(self, index: int) -> None:
        self.index = index
        self.real = fontTools.ttLib.TTFont

    def __call__(self, *args: Any, **kwargs: Any) -> Any:
        kwargs.setdefault("fontNumber", self.index)
        return self.real(*args, **kwargs)


def open_fontlib(d: Digest, path: str, index: int, collection: bool, role: Face) -> Any:
    """
    Register the file for `role` and have the reference read it.

    The reference opens `TTFont(path, lazy=True)` with no face index, which
    fontTools refuses for a collection. That refusal is recorded (`FONTLIB
    stock`). So that the faces of a collection have answers at all, the same
    reference code is then run with the face index supplied -- by pointing
    `fontTools.ttLib.TTFont`, which `_face_data` imports at call time, at a
    wrapper that adds `fontNumber` -- and recorded as `FONTLIB indexed`. For a
    file that is not a collection nothing is patched: stock is the answer.
    """
    d.stage = "fontlib"
    fontlib.register_font(role, path)
    data = None
    try:
        data = fontlib._face_data(role)
        d.rec("FONTLIB", "stock", role.value, "ok", s("%"))
        d.count("fontlib_stock", "ok")
    except Exception as exc:
        kind, message = exc_tokens(exc)
        d.rec("FONTLIB", "stock", role.value, kind, message)
        d.count("fontlib_stock", kind, generalise(str(exc)))
        if not collection:
            d.count("fontlib", kind, generalise(str(exc)))
    if collection:
        wrapper = _Indexed(index)
        fontTools.ttLib.TTFont = wrapper  # type: ignore[misc,assignment]
        try:
            fontlib._face_data.cache_clear()
            data = fontlib._face_data(role)
            d.rec("FONTLIB", "indexed", role.value, "ok", s("%"))
            d.count("fontlib", "ok")
        except Exception as exc:
            kind, message = exc_tokens(exc)
            d.rec("FONTLIB", "indexed", role.value, kind, message)
            d.count("fontlib", kind, generalise(str(exc)))
        finally:
            fontTools.ttLib.TTFont = wrapper.real  # type: ignore[misc]
    elif data is not None:
        d.count("fontlib", "ok")
    return data


_DIGITS = re.compile(r"\d+")
_QUOTED = re.compile(r"'[^']*'")


def generalise(message: str) -> str:
    """An exception message with its particulars removed, as a census key."""
    text = " ".join(message.split())
    text = re.sub(r"/\S+", "<path>", text)
    text = _QUOTED.sub("<q>", text)
    text = _DIGITS.sub("N", text)
    return text[:100]


def dump_face_data(d: Digest, data: Any, font: Any, info: dict[str, Any]) -> None:
    """Everything read back from the reference's `_FaceData`."""
    order = font.getGlyphOrder()
    n = len(order)
    gid_of: dict[str, int] = {}
    for gid, name in enumerate(order):
        gid_of.setdefault(name, gid)

    def gid(name: str) -> int:
        found = gid_of.get(name)
        if found is not None:
            return found
        # fontTools names a glyph id the font does not have `glyphNNNNN`.
        try:
            return int(font.getGlyphID(name))
        except Exception:
            return -1

    # ---- vertical metrics
    d.stage = "pick"
    os2_source = "os2"
    try:
        raw = int(getattr(font["OS/2"], "sxHeight", 0) or 0) if "OS/2" in font else 0
        if not raw:
            os2_source = "fallback"
    except Exception:
        os2_source = "fallback"
    d.rec("PICK", data.units_per_em, data.ascent, data.descent, data.x_height, os2_source)
    d.count("x_height_source", os2_source)

    # ---- cmap
    d.stage = "cmap"
    own = font.getBestCmap() or {}
    minted = sorted(cp for cp in data.cmap if cp not in own)
    pairs = sorted((cp, gid(name)) for cp, name in data.cmap.items() if cp in own)
    assert len(pairs) == len(own), "the reference dropped a cmap entry"
    beyond = sum(1 for _cp, g in pairs if g >= n or g < 0)
    d.rec("CMAP", len(pairs), h64(x for p in pairs for x in p), beyond, len(minted))
    picks = spaced(len(pairs), CMAP_SAMPLES)
    d.rec("CMAPS", len(picks), *(x for i in picks for x in pairs[i]))
    dump_cmap_bytes(d, info["sfnt"], dict(pairs))
    if beyond:
        d.count("cmap_names_a_glyph_out_of_range")
    if pairs and pairs[-1][0] > 0xFFFF:
        d.count("cmap_reaches_beyond_bmp")

    # ---- hmtx
    d.stage = "hmtx"
    advances = [data.advances.get(name, 0) for name in order]
    d.rec("HMTX", n, h64(advances))
    wanted = set(spaced(n, HMTX_SAMPLES - 3))
    wanted.update(g for g in (info["n_hmetrics"] - 1, info["n_hmetrics"]) if 0 <= g < n)
    picks = sorted(wanted)
    d.rec("HMTXS", len(picks), *(x for g in picks for x in (g, advances[g])))

    # ---- ink boxes: all of them hashed, a sample written
    d.stage = "boxes"
    boxes: list[tuple[float, float, float, float] | None] = []
    errors: list[tuple[int, BaseException]] = []
    for g, name in enumerate(order):
        try:
            boxes.append(data.glyph_bounds(name))
        except Exception as exc:
            boxes.append(None)
            errors.append((g, exc))

    def exact_words(lo: int, hi: int) -> Any:
        for g in range(lo, hi):
            box = boxes[g]
            if box is None:
                yield from (1, 1, 1, 1, 1)
            else:
                yield 0
                for v in box:
                    yield double_bits(v)

    def rounded_words() -> Any:
        for box in boxes:
            if box is None:
                yield from (1, 1, 1, 1, 1)
            else:
                yield 0
                for v in box:
                    yield round(v)

    fractional = ties = zero = 0
    for box in boxes:
        if box is None:
            continue
        if box == (0.0, 0.0, 0.0, 0.0):
            zero += 1
        if any(v != int(v) for v in box):
            fractional += 1
        if any(abs(v - int(v)) == 0.5 for v in box):
            ties += 1
    try:
        rounded = h64(rounded_words())
    except (OverflowError, ValueError) as exc:  # a NaN or infinite coordinate
        rounded = "unroundable-" + type(exc).__name__
        d.count("box_not_finite")
    d.rec("BOXES", n, h64(exact_words(0, n)), rounded, zero, fractional, ties, len(errors))
    chunks = [(k * n // BOX_CHUNKS, (k + 1) * n // BOX_CHUNKS) for k in range(BOX_CHUNKS)]
    d.rec("BOXCHUNKS", BOX_CHUNKS, *(h64(exact_words(lo, hi)) for lo, hi in chunks))
    for g, exc in errors[:8]:
        d.rec("BOXERR", g, *exc_tokens(exc))
    for kind in sorted({type(exc).__name__ for _g, exc in errors}):
        d.count("glyph_draw_raises", kind)
    if fractional:
        d.count("has_non_integral_ink_box", info["flavour"])

    contours = info["contours"]

    def kind_of(g: int) -> str:
        if contours is None or g >= len(contours):
            return "cff" if info["flavour"] in ("CFF", "CFF2") else "other"
        c = contours[g]
        if c is None:
            return "empty"
        return "composite" if c < 0 else "simple"

    wanted = {0, n - 1} if n else set()
    if contours is not None:
        composites = [g for g, c in enumerate(contours) if c is not None and c < 0]
        empties = [g for g, c in enumerate(contours) if c is None or c == 0]
        wanted.update(composites[:6])
        wanted.update(composites[-2:])
        wanted.update(composites[i] for i in spaced(len(composites), 8))
        wanted.update(empties[:4])
    else:
        blank = [g for g, box in enumerate(boxes) if box == (0.0, 0.0, 0.0, 0.0)]
        wanted.update(blank[:4])
    wanted.update(g for g, _exc in errors[:4])
    wanted.update(g for g, box in enumerate(boxes) if box and any(v != int(v) for v in box)
                  and g % 7 == 0 and g < 7 * 8)  # fmt: skip
    fill = spaced(n, max(2, BOX_SAMPLES - len(wanted)))
    wanted.update(fill)
    rng = Lcg(0x1B0C5 ^ n)
    guard = 0
    while len(wanted) < min(BOX_SAMPLES, n) and guard < 4096:
        wanted.add(rng.below(n))
        guard += 1
    for g in sorted(wanted)[: BOX_SAMPLES + 16]:
        box = boxes[g]
        if box is None:
            d.rec("BOXX", g, kind_of(g))
        elif all(v == int(v) and abs(v) < 2**31 for v in box):
            # Integral coordinates are written as integers to keep the file
            # small: the double is exactly that integer.
            d.rec("BOXI", g, kind_of(g), *(int(v) for v in box))
        else:
            d.rec("BOXF", g, kind_of(g), *box)

    dump_cff(d, data, font, info)
    dump_kerning(d, data, font, info, order, gid)
    if data.math is not None:
        dump_math(d, data, order, gid, minted)


def dump_cmap_bytes(d: Digest, sfnt: Sfnt, reference: dict[int, int]) -> None:
    """
    How the picked subtable read by the specification alone compares with what
    the reference holds (which is fontTools' reading of it).
    """
    cmap = sfnt.table("cmap")
    try:
        assert cmap is not None
        picked = cmap_picked_offset(cmap)
        parsed = cmap_by_the_specification(cmap, picked[2]) if picked is not None else None
    except (struct.error, AssertionError, IndexError) as exc:
        d.rec("CMAPBYTESERR", *exc_tokens(exc))
        d.count("cmap_scan_failed", type(exc).__name__)
        return
    if parsed is None:
        d.rec("CMAPBYTES", -1, 0, 0, 0, 0, 0, 0)
        d.count("cmap_picked_format_not_scanned")
        return
    fmt, naive = parsed
    to_zero = sum(1 for g in naive.values() if g == 0)
    # Format 4's closing segment conventionally maps U+FFFF to glyph 0.
    to_zero_real = sum(1 for cp, g in naive.items() if g == 0 and cp != 0xFFFF)
    kept = {cp: g for cp, g in naive.items() if g != 0}
    only_spec = sorted(cp for cp in kept if cp not in reference)
    only_ref = sorted(cp for cp in reference if cp not in kept)
    differ = sorted(cp for cp, g in kept.items() if cp in reference and reference[cp] != g)
    d.rec("CMAPBYTES", fmt, len(naive), to_zero, to_zero_real, len(only_spec), len(only_ref),
          len(differ))  # fmt: skip
    for label, cps in (("only_spec", only_spec), ("only_reference", only_ref), ("differ", differ)):
        for cp in cps[:4]:
            d.rec("CMAPDIFF", label, cp, naive.get(cp, -1), reference.get(cp, -1))
    if to_zero_real:
        d.count("cmap_picked_subtable_maps_a_codepoint_other_than_FFFF_to_glyph_0", f"format{fmt}")
    if only_spec or only_ref or differ:
        d.count("cmap_read_by_the_specification_differs_from_the_reference", f"format{fmt}")
    else:
        d.count("cmap_read_by_the_specification_minus_glyph_0_equals_the_reference", f"format{fmt}")


def dump_cff(d: Digest, data: Any, font: Any, info: dict[str, Any]) -> None:
    """What a Type 2 interpreter must be sized for, after every glyph was drawn."""
    if info["flavour"] != "CFF":
        return
    d.stage = "cff"
    try:
        cff = font["CFF "].cff
        top = cff.topDictIndex[0]
        is_cid = hasattr(top, "ROS")
        fd_count = len(top.FDArray) if hasattr(top, "FDArray") else 0
        matrix_default = list(top.FontMatrix) == [0.001, 0, 0, 0.001, 0, 0]
        ops: set[str] = set()
        fractional = 0
        # The charstrings the reference's glyph set drew from: already
        # decompiled by the pass over every glyph, subroutines included when
        # they were called.
        drawn = getattr(data.glyph_set, "charStrings", None)
        programs = list(drawn.values()) if drawn is not None else []
        subrs = list(getattr(drawn, "globalSubrs", []) or []) if drawn is not None else []
        if drawn is not None and hasattr(drawn, "fdArray") and drawn.fdArray is not None:
            for fd in drawn.fdArray:
                subrs.extend(getattr(getattr(fd, "Private", None), "Subrs", []) or [])
        elif drawn is not None:
            subrs.extend(getattr(getattr(drawn, "private", None), "Subrs", []) or [])
        used_subrs = 0
        for charstring in programs + subrs:
            program = getattr(charstring, "program", None)
            if program is None:
                continue  # a subroutine no glyph called
            for token in program:
                if isinstance(token, str):
                    ops.add(token)
                elif isinstance(token, float):
                    fractional += 1
        for sub in subrs:
            if getattr(sub, "program", None) is not None:
                used_subrs += 1
        d.rec(
            "CFF", int(cff.major), is_cid, fd_count, int(top.CharstringType), matrix_default,
            len(cff.GlobalSubrs), len(subrs), used_subrs, fractional,
        )  # fmt: skip
        d.rec("CFFOPS", len(ops), *sorted(ops))
        d.count("cff_keying", "cid" if is_cid else "name")
        d.count("cff_charstring_type", int(top.CharstringType))
        if not matrix_default:
            d.count("cff_font_matrix_not_default")
        if fractional:
            d.count("cff_has_non_integer_operands")
        if len(cff.GlobalSubrs):
            d.count("cff_has_global_subrs")
        for op in ops:
            d.count("cff_operator", op)
    except Exception as exc:
        d.rec("CFFERR", *exc_tokens(exc))
        d.count("cff_census_failed", type(exc).__name__)


def kern_lookup_indices(font: Any) -> tuple[list[int], int, int]:
    """
    The lookups the reference reads, as it selects them: every FeatureRecord
    tagged `kern`, de-duplicated, ascending. Also the number of such records
    and the number of distinct lookup lists among them.
    """
    if "GPOS" not in font:
        return [], 0, 0
    gpos = font["GPOS"].table
    if gpos.FeatureList is None or gpos.LookupList is None:
        return [], 0, 0
    indices: list[int] = []
    lists = set()
    records = 0
    for record in gpos.FeatureList.FeatureRecord:
        if record.FeatureTag == "kern":
            records += 1
            lists.add(tuple(int(i) for i in record.Feature.LookupListIndex))
            indices.extend(int(i) for i in record.Feature.LookupListIndex)
    return sorted(set(indices)), records, len(lists)


def dump_kerning(
    d: Digest, data: Any, font: Any, info: dict[str, Any], order: list[str], gid: Any
) -> None:
    d.stage = "kern"
    n = len(order)
    subs = data.kern_subtables
    lookups, records, distinct = kern_lookup_indices(font)
    d.rec("KERNLOOKUPS", records, distinct, len(lookups), *lookups)

    # ---- the inventory, from the bytes
    gpos_bytes = info["sfnt"].table("GPOS")
    shapes: list[tuple[int, ...]] = []
    if gpos_bytes is not None and lookups:
        try:
            shapes = gpos_lookup_shapes(gpos_bytes, lookups)
            for shape in shapes:
                d.rec("KSHAPE", *shape)
        except (struct.error, IndexError) as exc:
            d.rec("KSHAPEERR", *exc_tokens(exc))
            d.count("kern_shape_scan_failed", type(exc).__name__)
    read_shapes = [x for x in shapes if x[5] == 2]
    for shape in shapes:
        ext = "extension" if shape[4] else "direct"
        if shape[5] == 2:
            d.count("kern_subtable", f"PairPos{shape[6]}", ext)
            d.count("kern_value_formats", f"PairPos{shape[6]}",
                    f"vf1=0x{shape[7]:04x}", f"vf2=0x{shape[8]:04x}")  # fmt: skip
            d.count("kern_coverage_format", f"PairPos{shape[6]}", f"coverage{shape[9]}")
            if shape[6] == 2:
                d.count("kern_classdef_formats", f"classdef1={shape[10]}", f"classdef2={shape[11]}")
        else:
            d.count("kern_feature_subtable_not_read", f"type{shape[5]}", f"format{shape[6]}", ext)
    for combo in sorted({(f"PairPos{x[6]}", "extension" if x[4] else "direct") for x in read_shapes}):
        d.count("faces_with_kern_subtable", *combo)
    for flag in sorted({x[3] for x in shapes if x[5] == 2}):
        d.count("faces_with_kern_lookup_flag", f"0x{flag:04x}")
    if len(lookups) > 1:
        d.count("faces_with_more_than_one_kern_lookup")
    if distinct > 1:
        d.count("faces_whose_kern_lookups_differ_by_script_or_language")

    # ---- the subtables the reference holds, each hashed whole
    d.rec("KERNSUBTABLES", len(subs))
    listed: set[tuple[int, int]] = set()
    class_axes: list[tuple[list[int], list[int]]] = []
    for index, sub in enumerate(subs):
        if isinstance(sub, fontlib._PairKerns):
            triples = sorted((gid(a), gid(b), v) for (a, b), v in sub.pairs.items())
            listed.update((a, b) for a, b, _v in triples)
            d.rec("KSUB", index, 1, len(triples), 0, h64(x for t in triples for x in t))
        else:
            coverage = sorted(gid(name) for name in sub.coverage)
            first = sorted((gid(k), v) for k, v in sub.first_classes.items())
            second = sorted((gid(k), v) for k, v in sub.second_classes.items())
            words = [len(coverage), *coverage, len(first), *(x for p in first for x in p),
                     len(second), *(x for p in second for x in p)]  # fmt: skip
            for row in sub.values:
                words.append(len(row))
                words.extend(row)
            width = max((len(row) for row in sub.values), default=0)
            d.rec("KSUB", index, 2, len(sub.values), width, h64(words))
            class_axes.append(([g for g in coverage if 0 <= g < n],
                               [g for g, c in second if c and 0 <= g < n]))  # fmt: skip

    has_kern = bool(subs)
    legacy = info["legacy_kern"]
    d.count("kerning", ("gpos" if has_kern else "none") + ("+legacy_kern_table" if legacy else ""))
    if n == 0:
        d.rec("KERNS", 0, 0)
        return

    def answer(left: int, right: int) -> tuple[int | None, int, str]:
        """(the reference's kern, the subtable that answered or -1, error type)."""
        try:
            value = data.pair_kern(order[left], order[right])
        except Exception as exc:
            return None, -1, type(exc).__name__
        which = -1
        for index, sub in enumerate(subs):
            try:
                if fontlib._subtable_kern(sub, order[left], order[right]) is not None:
                    which = index
                    break
            except Exception:
                break
        return value, which, ""

    rng = Lcg(0x4B45524E ^ n)
    chosen: dict[tuple[int, int], tuple[int | None, int, str]] = {}

    def take(pairs: Any, quota: int, want_nonzero: bool | None = None) -> None:
        added = 0
        for pair in pairs:
            if added >= quota:
                break
            if pair in chosen or not (0 <= pair[0] < n and 0 <= pair[1] < n):
                continue
            result = answer(*pair)
            if want_nonzero is not None and bool(result[0]) != want_nonzero:
                continue
            chosen[pair] = result
            added += 1

    take([(0, 0), (n - 1, n - 1), (0, n - 1)], 3)
    if has_kern:
        in_range = sorted(p for p in listed if 0 <= p[0] < n and 0 <= p[1] < n)
        take([in_range[i] for i in spaced(len(in_range), 80)], 80)
        # Class pairs: candidates drawn from each format 2 subtable's own
        # axes, most of the quota spent on pairs it kerns.
        candidates: list[tuple[int, int]] = []
        for lefts, rights in class_axes:
            if not lefts:
                continue
            for _ in range(1200 // len(class_axes)):
                left = lefts[rng.below(len(lefts))]
                right = rights[rng.below(len(rights))] if rights and rng.below(8) else rng.below(n)
                candidates.append((left, right))
        take(candidates, 96, want_nonzero=True)
        take(candidates, 16, want_nonzero=False)
        # A first glyph that heads a format 1 PairSet with a second it does
        # not list: the pair the reference lets fall through to later subtables.
        firsts = sorted({p[0] for p in in_range})
        pool = sorted({g for _lefts, rights in class_axes for g in rights}) or list(range(n))
        fall = [(firsts[i], pool[rng.below(len(pool))]) for i in spaced(len(firsts), 32)]
        take(fall, 32)
        take(((rng.below(n), rng.below(n)) for _ in range(4096)), KERN_SAMPLES - len(chosen))
    else:
        take(((rng.below(n), rng.below(n)) for _ in range(256)),
             KERN_SAMPLES_UNKERNED - len(chosen))  # fmt: skip

    good = sorted((pair, r) for pair, r in chosen.items() if r[0] is not None)
    bad = sorted((pair, r) for pair, r in chosen.items() if r[0] is None)
    d.rec("KERNS", len(good), sum(1 for _p, r in good if r[0]))
    for start in range(0, len(good), 16):
        row = good[start : start + 16]
        d.rec("K", len(row), *(x for (a, b), r in row for x in (a, b, r[0], r[1])))
    for (a, b), r in bad:
        d.rec("KERR", a, b, r[2])
    if bad:
        d.count("pair_kern_raises", bad[0][1][2])
    if has_kern and not any(r[0] for _p, r in good):
        d.count("kern_subtables_present_but_no_sampled_pair_is_non_zero")


def dump_math(d: Digest, data: Any, order: list[str], gid: Any, minted: list[int]) -> None:
    d.stage = "math"
    math = data.math
    names = sorted(math.constants)
    d.rec("MATHCONSTS", len(names), *(x for name in names for x in (s(name), math.constants[name])))
    italics = sorted((gid(name), v) for name, v in math.italic_corrections.items())
    d.rec("MATHITALIC", len(italics), h64(x for p in italics for x in p))
    picks = spaced(len(italics), MATH_ITALIC_SAMPLES)
    d.rec("MATHITALICS", len(picks), *(x for i in picks for x in italics[i]))

    chains = sorted(
        (gid(base), [(gid(name), extent, data.codepoints[name]) for name, extent in chain])
        for base, chain in math.vertical_variants.items()
    )
    words: list[int] = []
    for base, chain in chains:
        words += [base, len(chain)]
        for entry in chain:
            words += entry
    d.rec("MATHVARIANTS", len(chains), h64(words))
    for i in spaced(len(chains), MATH_VARIANT_SAMPLES):
        base, chain = chains[i]
        d.rec("VV", base, len(chain), *(x for entry in chain for x in entry))

    # Minted codepoints: descending codepoint is ascending glyph NAME.
    synthetic = [(cp, gid(data.cmap[cp]), data.cmap[cp]) for cp in sorted(minted, reverse=True)]
    by_gid = sorted(g for _cp, g, _name in synthetic)
    d.rec("SYN", len(synthetic), h64(x for cp, g, _n in synthetic for x in (cp, g)),
          [g for _cp, g, _n in synthetic] == by_gid)  # fmt: skip
    for i in spaced(len(synthetic), SYN_SAMPLES):
        cp, g, name = synthetic[i]
        d.rec("SYNS", cp, g, s(name))
    d.count("math_minted_codepoints", "some" if synthetic else "none")
    if synthetic and [g for _cp, g, _n in synthetic] != by_gid:
        d.count("math_minting_order_is_not_glyph_id_order")

    try:
        params = fontlib.math_metrics(MATH_SIZE).math
        d.rec("MATHPARAMS", "ok", s("%"), f(MATH_SIZE))
        for name in sorted(params.__dataclass_fields__):
            d.rec("MP", name, float(getattr(params, name)))
        d.count("math_metrics", "ok")
    except Exception as exc:
        d.rec("MATHPARAMS", *exc_tokens(exc), f(MATH_SIZE))
        d.count("math_metrics", type(exc).__name__, generalise(str(exc)))


def dump_face(task: tuple[str, int, int, float]) -> tuple[str, int, list[str], list[tuple[str, ...]]]:
    path, index, face_count, limit = task
    logging.disable(logging.CRITICAL)
    d = Digest()
    blob = Path(path).read_bytes()
    d.rec("FACE", s(path), index, face_count, len(blob), hashlib.sha256(blob).hexdigest(),
          zlib.crc32(blob) & 0xFFFFFFFF)  # fmt: skip
    d.count("faces")
    collection = blob[:4] == b"ttcf"
    d.count("container", "collection" if collection else "single")
    role = Face.CODE_BOLD
    signal.signal(signal.SIGALRM, _on_alarm)
    signal.setitimer(signal.ITIMER_REAL, limit)
    try:
        offsets = face_offsets(blob)
        assert offsets is not None
        sfnt = Sfnt(blob, offsets[index])
        info = dump_bytes(d, sfnt)
        info["sfnt"] = sfnt
        font = dump_fonttools(d, path, index)
        if "MATH" in sfnt.tables:
            # The math role, so that `math_metrics` answers for this file. The
            # reference reads the MATH table and mints codepoints whatever the
            # role, so the rest of the digest does not depend on this choice.
            role = Face.MATH
        data = open_fontlib(d, path, index, collection, role)
        if data is not None and font is not None:
            dump_face_data(d, data, font, info)
        d.stage = "done"
    except FaceTimeout:
        d.rec("TIMEOUT", d.stage)
        d.count("timeout", d.stage)
    except Exception as exc:
        # Nothing here is expected: a failure of this script, not of the font.
        d.rec("DUMPERR", d.stage, *exc_tokens(exc))
        d.count("dump_error", d.stage, type(exc).__name__, generalise(str(exc)))
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
        fontlib.unregister_font(role)
    d.rec("END", index)
    return path, index, d.lines, d.census


# --------------------------------------------------------------------------------
# The corpus
# --------------------------------------------------------------------------------


def corpus() -> list[str]:
    listing = subprocess.run(
        ["fc-list", ":", "file"], check=True, capture_output=True, text=True
    ).stdout
    paths = set()
    for line in listing.splitlines():
        line = line.strip()
        if line.endswith(":"):
            line = line[:-1]
        if line:
            paths.add(line)
    return sorted(paths)


def shard_names(files: list[str]) -> dict[str, str]:
    """path -> golden file name: the directory, then a run number within it."""
    by_dir: dict[str, list[str]] = collections.defaultdict(list)
    for path in files:
        by_dir[os.path.dirname(path)].append(path)
    out: dict[str, str] = {}
    for directory, members in by_dir.items():
        stem = directory.removeprefix("/usr/share/fonts").strip("/").replace("/", ".") or "root"
        stem = re.sub(r"[^A-Za-z0-9._-]", "_", stem)
        for i, path in enumerate(sorted(members)):
            out[path] = f"{stem}.{i // SHARD_FILES:02d}.golden"
    return out


def header(title: str) -> list[str]:
    return [
        f"# {title}",
        "# Written by tools/oracle/dump_font_wild.py; do not edit. "
        "Records: tools/oracle/FORMAT-font-wild.md",
        f"# fontTools {fontTools.version}",
    ]


def main(argv: list[str]) -> int:
    jobs = os.cpu_count() or 1
    limit = 900.0
    only = None
    args = list(argv)
    while args:
        arg = args.pop(0)
        if arg == "--jobs":
            jobs = int(args.pop(0))
        elif arg == "--limit":
            limit = float(args.pop(0))
        elif arg == "--only":
            only = args.pop(0)
        else:
            print(f"unknown argument {arg!r}", file=sys.stderr)
            return 2

    everything = corpus()
    out = OUT
    if only is not None:
        everything = [p for p in everything if only in p]
        out = Path(os.environ.get("TMPDIR", "/tmp")) / "font-wild-only"
    out.mkdir(parents=True, exist_ok=True)

    files: list[str] = []
    refused: list[tuple[str, str]] = []
    tasks: list[tuple[str, int, int, float]] = []
    sizes: dict[str, int] = {}
    for path in everything:
        suffix = os.path.splitext(path)[1].lower()
        if suffix in TYPE1_SUFFIXES:
            refused.append((path, "Type1"))
            continue
        if suffix not in SFNT_SUFFIXES:
            refused.append((path, "not-ttf-otf-ttc"))
            continue
        try:
            with open(path, "rb") as handle:
                lead = handle.read(12)
                count = 1
                if lead[:4] == b"ttcf":
                    (count,) = struct.unpack_from(">I", lead, 8)
                elif lead[:4] not in (b"\x00\x01\x00\x00", b"OTTO", b"true", b"typ1"):
                    refused.append((path, "not-sfnt-magic-" + lead[:4].hex()))
                    continue
            sizes[path] = os.path.getsize(path)
        except OSError as exc:
            refused.append((path, "unreadable-" + type(exc).__name__))
            continue
        files.append(path)
        tasks.extend((path, i, count, limit) for i in range(count))

    # Largest first, so the long faces are not all left to the end.
    tasks.sort(key=lambda t: (-sizes[t[0]], t[0], t[1]))
    results: dict[tuple[str, int], tuple[list[str], list[tuple[str, ...]]]] = {}
    with multiprocessing.Pool(jobs, maxtasksperchild=64) as pool:
        for done, (path, index, lines, census) in enumerate(
            pool.imap_unordered(dump_face, tasks, chunksize=1), 1
        ):
            results[(path, index)] = (lines, census)
            if done % 250 == 0 or done == len(tasks):
                print(f"  {done}/{len(tasks)} faces", file=sys.stderr, flush=True)

    names = shard_names(files)
    shards: dict[str, list[str]] = collections.defaultdict(list)
    shard_faces: collections.Counter[str] = collections.Counter()
    census: collections.Counter[tuple[str, ...]] = collections.Counter()
    examples: dict[tuple[str, ...], str] = {}
    for path, index in sorted(results):
        lines, counted = results[(path, index)]
        shards[names[path]].extend(lines)
        shard_faces[names[path]] += 1
        for key in counted:
            census[key] += 1
            examples.setdefault(key, f"{path}#{index}")

    written: list[tuple[str, int]] = []
    for name in sorted(shards):
        text = "\n".join(header(f"font-wild digests, {shard_faces[name]} faces") + shards[name])
        data = (text + "\n").encode("utf-8")
        (out / name).write_bytes(data)
        written.append((name, len(data)))

    index_lines = header("font-wild index: the corpus, what was refused unread, and the census")
    index_lines.append(f"CORPUS {len(everything)} {len(files)} {len(tasks)} {len(refused)}")
    for name in sorted(shards):
        index_lines.append(f"SHARD {s(name)} {shard_faces[name]}")
    for path, why in refused:
        index_lines.append(f"REFUSED {s(path)} {s(why)}")
    for key in sorted(census):
        tokens = " ".join(s(k) for k in key)
        index_lines.append(f"CENSUS {census[key]} {s(examples[key])} {len(key)} {tokens}")
    data = ("\n".join(index_lines) + "\n").encode("utf-8")
    (out / "index.golden").write_bytes(data)
    written.append(("index.golden", len(data)))

    total = sum(size for _n, size in written)
    print(f"{total} bytes in {len(written)} files under {out}")
    print(f"{len(everything)} fc-list files: {len(files)} sfnt files, {len(tasks)} faces, "
          f"{len(refused)} refused unread")  # fmt: skip
    for key in sorted(census):
        print(f"{census[key]:6d}  {' | '.join(key)}    e.g. {examples[key]}")
    stale = sorted(p.name for p in out.glob("*.golden") if p.name not in {n for n, _ in written})
    if stale:
        print("stale goldens not written by this run:", ", ".join(stale), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
