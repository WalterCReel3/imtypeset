"""
Dump font goldens from the Python reference for the C++ OpenType subset reader.

Run from the repository root, with the reference's interpreter:

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=build/advtext-reference \
        ../imgui-adv-text/.venv/bin/python -P -B tools/oracle/dump_font.py

Writes tests/golden/font/*.golden. Reads the reference and never writes to it.
tools/oracle/FORMAT-font.md documents every record this writes and, more to the
point, what in the reference's answers is fontTools behaviour that a reader
written from the OpenType specification would not arrive at by itself.

Two kinds of data are kept apart in the output and in this file:

- RAW records come from fontTools (or from the file's bytes, where fontTools
  discards what is wanted) and are in font units. They say what the file holds.
- ANSWER records come from `advtext.fontlib`'s public surface. They say what the
  reference makes of it, and are what the port must reproduce.

Where both exist for the same fact the script asserts that they agree, so a
golden is never a transcription of one side that the other contradicts.

The output is a function of the font files and the fontTools version alone: no
paths, no times, no dict order that is not sorted first. Running it twice writes
the same bytes.
"""

from __future__ import annotations

import collections
import dataclasses
import hashlib
import struct
import sys
import tempfile
from pathlib import Path
from typing import Any

import fontTools
from fontTools.pens.basePen import NullPen
from fontTools.ttLib import TTFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pinned_reference  # noqa: E402

pinned_reference.require()

from advtext import fontlib
from advtext.fontmetrics import Face, MathParams

OUT = Path(__file__).resolve().parents[2] / "tests" / "golden" / "font"

# The sizes every per-size ANSWER block is dumped at. The first four are the
# task's; 14.4 has no finite binary expansion, so `size / units_per_em` is
# inexact and a port that multiplies in another order shows it; the last two
# are sizes in scaled points, which is what the reference really passes
# (`advtext.style.fontlib_metrics` calls `metrics(face, float(spec.size))` with
# a `Scaled`): 10 pt exactly, and an odd integer near 13.65 pt.
SIZES = (10.0, 12.0, 13.0, 16.0, 14.4, 655360.0, 894785.0)

PIXEL_HEIGHTS = (8.0, 10.0, 12.0, 13.0, 14.0, 16.0, 18.0, 20.0, 24.0, 32.0, 17.25, 0.0)

# Codepoints no bundled face maps, plus the edges of the ranges that matter.
# U+110000 and 0x7FFFFFFF are not Unicode at all; the reference answers them
# like any other unmapped integer, and a port taking a 32-bit codepoint must too.
AWKWARD = (
    0x7F, 0x80, 0xAD, 0xD800, 0xDFFF, 0xE000, 0xFFFE, 0xFFFF, 0x1F600,
    0xEFFFF, 0xF0000, 0xFFF00, 0xFFFFD, 0xFFFFE, 0xFFFFF, 0x10FFFF, 0x110000, 0x7FFFFFFF,
)  # fmt: skip

# Named in the reference's own comments and tests as the pairs kerning shows on.
CLASSIC_PAIRS = (
    "To", "AV", "Wa", "Ty", "LT", "P.", "ff", "r,", "VA", "Yo", "T.", "F,", "ry", "we",
    "  ", "A ", " A", "''", "\"A", "1.", "7,", "fi", "Te", "Av", "LV", "ky", "xo",
)  # fmt: skip

# The dense kern matrix's axes: printable ASCII and the Latin-1 upper half.
KERN_AXIS = tuple(range(0x20, 0x7F)) + tuple(range(0xA0, 0x100))

# Parameters of the generator behind every "deterministic sample". A plain LCG
# (Numerical Recipes' constants) so the stream is the same on any Python.
_LCG_A, _LCG_C, _LCG_M = 1664525, 1013904223, 2**32


class Lcg:
    def __init__(self, seed: int) -> None:
        self.state = seed % _LCG_M

    def below(self, n: int) -> int:
        self.state = (_LCG_A * self.state + _LCG_C) % _LCG_M
        # The high bits: an LCG's low bits have short periods.
        return (self.state >> 8) % n


# --------------------------------------------------------------------------------
# The shared golden format
# --------------------------------------------------------------------------------

_SAFE = frozenset(b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-")


def s(text: str) -> str:
    """A string token: percent-encoded UTF-8, no raw space, `%` when empty."""
    if text == "":
        return "%"
    return "".join(chr(b) if b in _SAFE else f"%{b:02X}" for b in text.encode("utf-8"))


def f(value: float) -> str:
    """A float token: `float.hex()`, which round-trips exactly through strtod."""
    return float(value).hex()


class Golden:
    def __init__(self, name: str, title: str) -> None:
        self.name = name
        self.lines: list[str] = [
            f"# {title}",
            "# Written by tools/oracle/dump_font.py; do not edit. Records: tools/oracle/FORMAT-font.md",
            f"# fontTools {fontTools.version}",
        ]

    def comment(self, text: str) -> None:
        self.lines.append(f"# {text}")

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

    def write(self) -> int:
        data = ("\n".join(self.lines) + "\n").encode("utf-8")
        (OUT / self.name).write_bytes(data)
        return len(data)


# --------------------------------------------------------------------------------
# Bytes fontTools does not keep
# --------------------------------------------------------------------------------


def table_directory(blob: bytes) -> list[tuple[str, int, int, int]]:
    """(tag, checksum, offset, length) for each table, in file order of the directory."""
    num_tables = struct.unpack_from(">H", blob, 4)[0]
    out = []
    for i in range(num_tables):
        tag, checksum, offset, length = struct.unpack_from(">4sIII", blob, 12 + 16 * i)
        out.append((tag.decode("latin-1"), checksum, offset, length))
    return out


def table_bytes(blob: bytes, tag: str) -> bytes | None:
    for name, _checksum, offset, length in table_directory(blob):
        if name == tag:
            return blob[offset : offset + length]
    return None


def u16(data: bytes, at: int) -> int:
    return struct.unpack_from(">H", data, at)[0]


def gpos_subtable_shapes(blob: bytes) -> list[tuple[int, ...]]:
    """
    The on-disk shape of every GPOS lookup subtable, read from the bytes.

    fontTools drops the Coverage and ClassDef format numbers once it has
    decompiled them, and those are exactly what a from-scratch reader has to
    branch on. One tuple per subtable:

        (lookup, subtable, lookup_type, lookup_flag, is_extension, effective_type,
         pos_format, value_format1, value_format2, coverage_format,
         classdef1_format, classdef2_format, count1, count2)

    with -1 wherever a field does not exist for that subtable. For PairPos
    format 1, count1 is PairSetCount and count2 is -1; for format 2 they are
    Class1Count and Class2Count. Fields past effective_type are only read for
    effective type 2 (PairPos), the one type the reference uses.
    """
    gpos = table_bytes(blob, "GPOS")
    if gpos is None:
        return []
    lookup_list = u16(gpos, 8)
    out = []
    for li in range(u16(gpos, lookup_list)):
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


_COMPONENT_BITS = (
    (0x0001, "ARG_1_AND_2_ARE_WORDS"),
    (0x0002, "ARGS_ARE_XY_VALUES"),
    (0x0004, "ROUND_XY_TO_GRID"),
    (0x0008, "WE_HAVE_A_SCALE"),
    (0x0020, "MORE_COMPONENTS"),
    (0x0040, "WE_HAVE_AN_X_AND_Y_SCALE"),
    (0x0080, "WE_HAVE_A_TWO_BY_TWO"),
    (0x0100, "WE_HAVE_INSTRUCTIONS"),
    (0x0200, "USE_MY_METRICS"),
    (0x0400, "OVERLAP_COMPOUND"),
    (0x0800, "SCALED_COMPONENT_OFFSET"),
    (0x1000, "UNSCALED_COMPONENT_OFFSET"),
)


def glyf_census(blob: bytes, num_glyphs: int, loca_format: int) -> dict[str, int]:
    """
    How often each glyf feature a reader must handle occurs, counted from the bytes.

    fontTools strips the composite flags that only describe the encoding
    (words-or-bytes, which scale form), so the census cannot come from it.
    """
    glyf = table_bytes(blob, "glyf")
    loca = table_bytes(blob, "loca")
    assert glyf is not None and loca is not None
    if loca_format == 0:
        offsets = [2 * v for v in struct.unpack_from(f">{num_glyphs + 1}H", loca, 0)]
    else:
        offsets = list(struct.unpack_from(f">{num_glyphs + 1}I", loca, 0))
    census: collections.Counter[str] = collections.Counter()
    children: dict[int, list[int]] = {}
    for gid in range(num_glyphs):
        start, end = offsets[gid], offsets[gid + 1]
        if start == end:
            census["glyphs_without_data"] += 1
            continue
        contours = struct.unpack_from(">h", glyf, start)[0]
        if contours >= 0:
            census["simple_glyphs"] += 1
            if contours == 0:
                census["simple_glyphs_with_zero_contours"] += 1
            end_points = struct.unpack_from(f">{contours}H", glyf, start + 10)
            n_points = end_points[-1] + 1 if contours else 0
            at = start + 10 + 2 * contours
            at += 2 + u16(glyf, at)  # instructions
            flags: list[int] = []
            while len(flags) < n_points:
                flag = glyf[at]
                at += 1
                flags.append(flag)
                if flag & 0x08:
                    flags.extend([flag] * glyf[at])
                    census["simple_flag_repeats"] += 1
                    at += 1
            first = 0
            for last in end_points:
                contour = flags[first : last + 1]
                if not any(flag & 0x01 for flag in contour):
                    census["contours_with_no_on_curve_point"] += 1
                if len(contour) == 1:
                    census["contours_of_one_point"] += 1
                if contour and not contour[0] & 0x01:
                    census["contours_starting_off_curve"] += 1
                first = last + 1
            if any(flag & 0x40 for flag in flags):
                census["simple_glyphs_with_overlap_flag"] += 1
            continue
        census["composite_glyphs"] += 1
        at = start + 10
        children[gid] = []
        while True:
            flag, child = struct.unpack_from(">HH", glyf, at)
            at += 4
            children[gid].append(child)
            census["components"] += 1
            for bit, name in _COMPONENT_BITS:
                if flag & bit:
                    census[f"component_flag_{name}"] += 1
            at += 4 if flag & 0x0001 else 2
            if flag & 0x0008:
                at += 2
            elif flag & 0x0040:
                at += 4
            elif flag & 0x0080:
                at += 8
            if not flag & 0x0020:
                break

    def depth(gid: int) -> int:
        return 1 + max((depth(c) for c in children[gid] if c in children), default=0)

    census["max_component_depth"] = max((depth(g) for g in children), default=0)
    return dict(census)


def cff_census(font: TTFont) -> tuple[dict[str, int], dict[str, int]]:
    """Facts about the CFF table a charstring interpreter must be sized for."""
    cff = font["CFF "].cff
    top = cff.topDictIndex[0]
    private = top.Private
    local = getattr(private, "Subrs", [])
    facts = {
        "major": int(cff.major),
        "fonts": len(cff.fontNames),
        "is_cid": int(hasattr(top, "ROS")),
        "has_fdarray": int(hasattr(top, "FDArray")),
        "charstring_type": int(top.CharstringType),
        "global_subrs": len(cff.GlobalSubrs),
        "local_subrs": len(local),
        "nominal_width_x": int(private.nominalWidthX),
        "default_width_x": int(private.defaultWidthX),
        "font_matrix_is_default": int(list(top.FontMatrix) == [0.001, 0, 0, 0.001, 0, 0]),
        "non_integer_operands": 0,
    }
    ops: collections.Counter[str] = collections.Counter()

    def scan(charstring: Any) -> None:
        charstring.decompile()
        for token in charstring.program:
            if isinstance(token, str):
                ops[token] += 1
            elif isinstance(token, float):
                facts["non_integer_operands"] += 1

    strings = top.CharStrings
    for name in font.getGlyphOrder():
        scan(strings[name])
    for sub in local:
        scan(sub)
    for sub in cff.GlobalSubrs:
        scan(sub)
    return facts, dict(ops)


# --------------------------------------------------------------------------------
# RAW: one file per font file
# --------------------------------------------------------------------------------


def dump_raw(face: Face, title: str) -> tuple[str, int]:
    """
    Everything font-unit about the file behind `face`, as `<basename>.raw.golden`.

    `face` is only the handle the reference is asked through; the records are
    about the file.
    """
    path = fontlib.font_file(face)
    blob = path.read_bytes()
    data = fontlib._face_data(face)
    font = TTFont(path, lazy=False)
    order = font.getGlyphOrder()
    gid_of = {name: gid for gid, name in enumerate(order)}
    g = Golden(f"{path.name}.raw.golden", f"RAW font-unit data of {path.name} ({title})")

    g.rec("FONT", s(path.name), len(blob), hashlib.sha256(blob).hexdigest(),
          s("OTTO" if font.sfntVersion == "OTTO" else "TrueType"))  # fmt: skip
    for tag, checksum, offset, length in table_directory(blob):
        g.rec("TABLE", s(tag), offset, length, checksum)

    head, hhea, maxp = font["head"], font["hhea"], font["maxp"]
    num_glyphs = int(maxp.numGlyphs)
    assert num_glyphs == len(order)
    g.rec("HEAD", int(head.unitsPerEm), int(head.indexToLocFormat),
          int(head.xMin), int(head.yMin), int(head.xMax), int(head.yMax))  # fmt: skip
    g.rec("HHEA", int(hhea.ascender), int(hhea.descender), int(hhea.lineGap),
          int(hhea.numberOfHMetrics))  # fmt: skip
    g.rec("MAXP", num_glyphs)
    if "OS/2" in font:
        os2 = font["OS/2"]
        has_x = hasattr(os2, "sxHeight")
        g.rec("OS2", int(os2.version), int(os2.sTypoAscender), int(os2.sTypoDescender),
              int(os2.sTypoLineGap), int(os2.usWinAscent), int(os2.usWinDescent),
              int(os2.fsSelection), has_x, int(os2.sxHeight) if has_x else 0,
              int(os2.sCapHeight) if has_x else 0)  # fmt: skip
        raw_x = int(getattr(os2, "sxHeight", 0) or 0)
    else:
        raw_x = 0
    # What the reference settles on, and from where. `hhea` for the vertical
    # extent (never OS/2 sTypo or usWin); OS/2.sxHeight for the x-height unless
    # it is absent or zero, when it is round(0.45 * unitsPerEm).
    assert data.ascent == int(hhea.ascender) and data.descent == -int(hhea.descender)
    assert data.units_per_em == int(head.unitsPerEm)
    g.rec("PICK", data.units_per_em, data.ascent, data.descent, data.x_height,
          "os2" if raw_x else "fallback")  # fmt: skip

    # cmap. `getBestCmap` takes the first subtable present in the order
    # (3,10) (0,6) (0,4) (3,1) (0,3) (0,2) (0,1) (0,0).
    best = {int(k): str(v) for k, v in font.getBestCmap().items()}
    picked = None
    for want in ((3, 10), (0, 6), (0, 4), (3, 1), (0, 3), (0, 2), (0, 1), (0, 0)):
        if font["cmap"].getcmap(*want) is not None:
            picked = want
            break
    for sub in font["cmap"].tables:
        g.rec("CMAPSUB", int(sub.platformID), int(sub.platEncID), int(sub.format),
              len(sub.cmap), (sub.platformID, sub.platEncID) == picked)  # fmt: skip
    minted = fontlib.synthetic_codepoints(face)
    assert {cp: n for cp, n in data.cmap.items() if cp not in minted} == best
    g.rec("CMAPCOUNT", len(best))
    for cp in sorted(best):
        g.rec("CMAP", cp, gid_of[best[cp]])

    # Glyphs: hmtx, the glyf header where there is one, and the ink box the
    # reference computes. FORMAT-font.md says how it computes it; in short it is
    # the exact extent of the curves, which is neither the glyf header box nor
    # the control-point box.
    is_glyf = "glyf" in font
    hmtx = font["hmtx"].metrics
    assert data.advances == {str(n): int(m[0]) for n, m in hmtx.items()}
    counts: collections.Counter[str] = collections.Counter()
    for gid, name in enumerate(order):
        advance, lsb = (int(v) for v in hmtx[name])
        box = data.glyph_bounds(name)
        header = (0, 0, 0, 0)
        contours = 0
        if is_glyf:
            glyph = font["glyf"][name]
            contours = int(glyph.numberOfContours)
            if contours == 0:
                kind = "empty"
            else:
                kind = "composite" if glyph.isComposite() else "simple"
                header = (int(glyph.xMin), int(glyph.yMin), int(glyph.xMax), int(glyph.yMax))
                if kind == "simple" and lsb != header[0]:
                    counts["simple_glyphs_shifted_by_lsb_minus_xmin"] += 1
                if kind == "composite" and lsb != header[0]:
                    counts["composite_glyphs_with_lsb_not_xmin_unshifted"] += 1
                if tuple(float(v) for v in header) != box:
                    counts["ink_box_differs_from_glyf_header"] += 1
        else:
            kind = "cff" if box != (0.0, 0.0, 0.0, 0.0) else "empty"
        if kind == "empty":
            assert box == (0.0, 0.0, 0.0, 0.0), (name, box)
        if any(v != int(v) for v in box):
            counts["ink_box_not_integral"] += 1
        counts[f"kind_{kind}"] += 1
        g.rec("GLYPH", gid, s(name), advance, lsb, kind, contours, *header, *box)
    for key in sorted(counts):
        g.rec("GLYPHSTAT", key, counts[key])

    if is_glyf:
        for key, value in sorted(glyf_census(blob, num_glyphs, int(head.indexToLocFormat)).items()):
            g.rec("GLYFSTAT", key, value)
    else:
        facts, ops = cff_census(font)
        for key in sorted(facts):
            g.rec("CFFSTAT", key, facts[key])
        for key in sorted(ops):
            g.rec("CFFOP", s(key), ops[key])
        strings = font["CFF "].cff[0].CharStrings
        mismatched = 0
        for name in order:
            strings[name].draw(NullPen())
            mismatched += int(strings[name].width != hmtx[name][0])
        g.rec("CFFSTAT", "charstring_width_differs_from_hmtx", mismatched)

    g.rec("LEGACYKERN", "kern" in font)
    dump_gpos(g, font, blob, data, gid_of, order)
    dump_kern_samples(g, data, best, order, seed=num_glyphs)
    font.close()
    return g.name, g.write()


def dump_gpos(
    g: Golden, font: TTFont, blob: bytes, data: Any, gid_of: dict[str, int], order: list[str]
) -> None:
    """GPOS structure, and every pair kern the reference holds, by glyph id."""
    if "GPOS" not in font:
        g.rec("GPOS", 0)
        g.rec("KERNLOOKUPS", 0)
        g.rec("KERNSUBTABLES", 0)
        return
    gpos = font["GPOS"].table
    g.rec("GPOS", 1)
    for record in gpos.ScriptList.ScriptRecord:
        default = record.Script.DefaultLangSys
        g.rec("GPOSSCRIPT", s(record.ScriptTag), len(record.Script.LangSysRecord),
              *(int(i) for i in (default.FeatureIndex if default else ())))  # fmt: skip
    features = gpos.FeatureList.FeatureRecord if gpos.FeatureList else []
    kern_lookups: set[int] = set()
    for index, record in enumerate(features):
        lookups = [int(i) for i in record.Feature.LookupListIndex]
        g.rec("GPOSFEATURE", index, s(record.FeatureTag), *lookups)
        if record.FeatureTag == "kern":
            kern_lookups.update(lookups)
    for shape in gpos_subtable_shapes(blob):
        g.rec("GPOSSUB", *shape)
    # The reference takes the lookups of every feature record tagged `kern`,
    # whatever script or language reaches it, de-duplicated and in index order.
    g.rec("KERNLOOKUPS", len(kern_lookups), *sorted(kern_lookups))

    # Whether any PairSet names the same second glyph twice. The reference keeps
    # the LAST such record (a dict built in file order); a binary search keeps
    # whichever it lands on. Zero here means the bundled files cannot tell.
    duplicates = 0
    with_value2 = 0
    for index in sorted(kern_lookups):
        lookup = gpos.LookupList.Lookup[index]
        for raw in lookup.SubTable:
            inner = raw.ExtSubTable if lookup.LookupType == 9 else raw
            kind = inner.LookupType if lookup.LookupType == 9 else lookup.LookupType
            if kind != 2:
                continue
            with_value2 += int(bool(inner.ValueFormat2))
            if inner.Format == 1:
                for pair_set in inner.PairSet:
                    seconds = [r.SecondGlyph for r in pair_set.PairValueRecord]
                    duplicates += len(seconds) - len(set(seconds))
    g.rec("KERNSTAT", "duplicate_second_glyphs_in_a_pair_set", duplicates)
    g.rec("KERNSTAT", "pair_subtables_with_a_value_format2", with_value2)

    # The kern subtables exactly as the reference holds them, in the order it
    # searches them. This is the whole of the kern data, lossless.
    g.rec("KERNSUBTABLES", len(data.kern_subtables))
    sources = [
        (index, position, int(gpos.LookupList.Lookup[index].LookupType == 9))
        for index in sorted(kern_lookups)
        for position, raw in enumerate(gpos.LookupList.Lookup[index].SubTable)
        if (raw.ExtSubTable.LookupType if gpos.LookupList.Lookup[index].LookupType == 9
            else gpos.LookupList.Lookup[index].LookupType) == 2
    ]  # fmt: skip
    assert len(sources) == len(data.kern_subtables)
    for index, source in enumerate(sources):
        g.rec("KSUBSRC", index, *source)
    for index, sub in enumerate(data.kern_subtables):
        if isinstance(sub, fontlib._PairKerns):
            g.rec("KSUB", index, 1, len(sub.pairs), 0)
            for (left, right), value in sorted(
                sub.pairs.items(), key=lambda kv: (gid_of[kv[0][0]], gid_of[kv[0][1]])
            ):
                g.rec("KPAIR", index, gid_of[left], gid_of[right], value)
        else:
            rows = sub.values
            width = max((len(row) for row in rows), default=0)
            assert all(len(row) == width for row in rows)
            g.rec("KSUB", index, 2, len(rows), width)
            g.rec("KCOV", index, len(sub.coverage), *sorted(gid_of[n] for n in sub.coverage))
            # Class 0 is every glyph not listed; only the non-zero classes are written.
            for tag, classes in (("KCLASS1", sub.first_classes), ("KCLASS2", sub.second_classes)):
                by_class: dict[int, list[int]] = collections.defaultdict(list)
                for name, cls in classes.items():
                    by_class[cls].append(gid_of[name])
                for cls in sorted(by_class):
                    g.rec(tag, index, cls, len(by_class[cls]), *sorted(by_class[cls]))
            for cls, row in enumerate(rows):
                if any(row):
                    g.rec("KROW", index, cls, *row)


def dump_kern_samples(
    g: Golden, data: Any, best: dict[int, str], order: list[str], seed: int
) -> None:
    """The reference's kern answers: a dense codepoint matrix and a glyph-id sample."""
    gid_of = {name: gid for gid, name in enumerate(order)}
    axis = KERN_AXIS
    g.rec("KERNAXIS", len(axis), *axis)
    g.rec("KERNAXISGID", len(axis), *(gid_of[data.glyph(cp)] for cp in axis))
    nonzero = 0
    for left in axis:
        row = [data.pair_kern(data.glyph(left), data.glyph(right)) for right in axis]
        nonzero += sum(1 for v in row if v)
        g.rec("KERNROW", left, *row)
    g.rec("KERNDENSESTAT", len(axis) * len(axis), nonzero)

    # Glyph-id pairs. Uniform pairs are almost all zero, so half the sample has
    # its left glyph drawn from the glyphs some subtable covers.
    covered: list[int] = sorted(
        {
            gid_of[name]
            for sub in data.kern_subtables
            for name in (
                {left for left, _ in sub.pairs} if isinstance(sub, fontlib._PairKerns) else sub.coverage
            )
        }
    )
    rng = Lcg(seed)
    n = len(order)
    pairs: set[tuple[int, int]] = {(0, 0), (0, n - 1), (n - 1, 0), (n - 1, n - 1)}
    target = 4000 if covered else 200
    while len(pairs) < target:
        left = covered[rng.below(len(covered))] if covered and rng.below(2) else rng.below(n)
        pairs.add((left, rng.below(n)))
    hits = 0
    for left, right in sorted(pairs):
        value = data.pair_kern(order[left], order[right])
        hits += int(value != 0)
        g.rec("KERNGID", left, right, value)
    g.rec("KERNGIDSTAT", len(pairs), hits)

    # How much rests on the search falling through a format 1 subtable. For
    # every pair whose left glyph heads a PairSet in some format 1 subtable that
    # does not list the pair: how many there are, and how many of them a later
    # subtable then kerns. A reader that stopped at "first glyph covered" would
    # answer zero for the second number's pairs.
    fell_through = 0
    then_kerned = 0
    for index, sub in enumerate(data.kern_subtables):
        if not isinstance(sub, fontlib._PairKerns) or index + 1 == len(data.kern_subtables):
            continue
        for left in sorted({left for left, _ in sub.pairs}, key=gid_of.__getitem__):
            for right in order:
                if (left, right) in sub.pairs:
                    continue
                if any(
                    fontlib._subtable_kern(earlier, left, right) is not None
                    for earlier in data.kern_subtables[:index]
                ):
                    continue
                fell_through += 1
                for later in data.kern_subtables[index + 1 :]:
                    found = fontlib._subtable_kern(later, left, right)
                    if found is not None:
                        then_kerned += int(found != 0)
                        break
    g.rec("KERNSTAT", "pairs_falling_through_a_format1_subtable", fell_through)
    # Named examples from the dense axis: the left glyph heads a PairSet of
    # subtable 0, that PairSet does not list the right glyph, and the answer is
    # a later subtable's and is not zero.
    examples = 0
    first = data.kern_subtables[0] if data.kern_subtables else None
    if isinstance(first, fontlib._PairKerns) and len(data.kern_subtables) > 1:
        heads = {left for left, _ in first.pairs}
        for left in KERN_AXIS:
            for right in KERN_AXIS:
                names = (data.glyph(left), data.glyph(right))
                if names[0] in heads and names not in first.pairs and data.pair_kern(*names):
                    if examples < 40 and (left * 31 + right) % 97 == 0:
                        g.rec("KERNFALL", left, right, gid_of[names[0]], gid_of[names[1]],
                              data.pair_kern(*names))  # fmt: skip
                        examples += 1
    g.rec("KERNSTAT", "fall_through_examples_written", examples)
    g.rec("KERNSTAT", "of_those_kerned_by_a_later_subtable", then_kerned)


# --------------------------------------------------------------------------------
# RAW: the MATH table
# --------------------------------------------------------------------------------

# The MathConstants the reference's MathParams is built from. Kept here as a
# list so the golden can say which of the font's 56 are read; `dump_math` checks
# it against the values MathParams really carries.
MATH_CONSTANTS_READ = (
    "FractionNumeratorDisplayStyleShiftUp", "FractionNumeratorShiftUp", "StackTopShiftUp",
    "FractionDenominatorDisplayStyleShiftDown", "FractionDenominatorShiftDown",
    "SuperscriptShiftUp", "SuperscriptShiftUpCramped", "SubscriptShiftDown",
    "SuperscriptBaselineDropMax", "SubscriptBaselineDropMin", "AxisHeight",
    "FractionRuleThickness", "UpperLimitGapMin", "LowerLimitGapMin",
    "UpperLimitBaselineRiseMin", "LowerLimitBaselineDropMin", "RadicalRuleThickness",
    "RadicalVerticalGap", "RadicalDisplayStyleVerticalGap", "ScriptPercentScaleDown",
    "ScriptScriptPercentScaleDown", "DisplayOperatorMinHeight",
)  # fmt: skip


def dump_math() -> tuple[str, int]:
    face = Face.MATH
    path = fontlib.font_file(face)
    data = fontlib._face_data(face)
    font = TTFont(path, lazy=False)
    order = font.getGlyphOrder()
    gid_of = {name: gid for gid, name in enumerate(order)}
    table = font["MATH"].table
    g = Golden(f"{path.name}.math.golden", f"RAW MATH table of {path.name}, and the minted codepoints")
    g.rec("FONT", s(path.name), path.stat().st_size, hashlib.sha256(path.read_bytes()).hexdigest(), s("OTTO"))
    g.rec("MATHVERSION", int(table.Version))

    constants = table.MathConstants
    constants.ensureDecompiled()
    device_tables = 0
    names = list(vars(constants))
    assert set(MATH_CONSTANTS_READ) <= set(names)
    for index, name in enumerate(names):
        raw = getattr(constants, name)
        value = int(getattr(raw, "Value", raw))
        is_record = hasattr(raw, "Value")
        device_tables += int(bool(getattr(raw, "DeviceTable", None)))
        assert data.math.constants[name] == value
        # index is the constant's position in the table, which is the order the
        # OpenType specification lists them in and so the order a reader meets them.
        g.rec("MATHCONST", index, s(name), value, is_record, name in MATH_CONSTANTS_READ)

    info = table.MathGlyphInfo
    italics = info.MathItalicsCorrectionInfo
    g.rec("ITALICCOUNT", len(italics.Coverage.glyphs))
    for name, value in zip(italics.Coverage.glyphs, italics.ItalicsCorrection, strict=True):
        device_tables += int(bool(value.DeviceTable))
        assert data.math.italic_corrections[name] == int(value.Value)
        g.rec("ITALIC", gid_of[name], int(value.Value))

    # Not read by the reference. Dumped because the task asks what the font
    # holds; a port that reads only what the reference reads ignores these.
    accents = info.MathTopAccentAttachment
    g.rec("TOPACCENTCOUNT", len(accents.TopAccentCoverage.glyphs))
    for name, value in zip(accents.TopAccentCoverage.glyphs, accents.TopAccentAttachment, strict=True):
        device_tables += int(bool(value.DeviceTable))
        g.rec("TOPACCENT", gid_of[name], int(value.Value))
    extended = info.ExtendedShapeCoverage
    extended_glyphs = sorted(gid_of[n] for n in extended.glyphs) if extended else []
    g.rec("EXTENDEDSHAPE", len(extended_glyphs), *extended_glyphs)
    g.rec("MATHKERNINFO", int(info.MathKernInfo is not None))

    variants = table.MathVariants
    g.rec("MINCONNECTOROVERLAP", int(variants.MinConnectorOverlap))

    def constructions(tag: str, coverage: Any, items: Any, read: bool) -> None:
        g.rec(f"{tag}COUNT", len(coverage.glyphs), read)
        for name, construction in zip(coverage.glyphs, items, strict=True):
            records = construction.MathGlyphVariantRecord
            flat: list[int] = []
            for record in records:
                flat += [gid_of[record.VariantGlyph], int(record.AdvanceMeasurement)]
            g.rec(f"{tag}VARIANTS", gid_of[name], len(records), *flat)
            if read:
                assert data.math.vertical_variants[name] == tuple(
                    (r.VariantGlyph, int(r.AdvanceMeasurement)) for r in records
                )
            assembly = construction.GlyphAssembly
            if assembly is not None:
                parts: list[int] = []
                for part in assembly.PartRecords:
                    parts += [
                        gid_of[part.glyph], int(part.StartConnectorLength),
                        int(part.EndConnectorLength), int(part.FullAdvance), int(part.PartFlags),
                    ]  # fmt: skip
                g.rec(f"{tag}ASSEMBLY", gid_of[name], int(assembly.ItalicsCorrection.Value),
                      len(assembly.PartRecords), *parts)  # fmt: skip

    # Vertical variant lists are read; glyph assemblies (the reference's
    # "ext_tag extensible assembly", ruled out of scope) and everything
    # horizontal are not.
    constructions("V", variants.VertGlyphCoverage, variants.VertGlyphConstruction, True)
    constructions("H", variants.HorizGlyphCoverage, variants.HorizGlyphConstruction, False)
    g.rec("DEVICETABLES", device_tables)

    # Minting. The reference sorts the unencoded glyphs a vertical chain can
    # reach BY GLYPH NAME and assigns downward from SYNTHETIC_FIRST. The names
    # are the CFF charset's, so reproducing the assignment needs them: every
    # glyph name is in the font's .raw.golden, and the sort key is spelled out
    # here so a test can check the order without re-deriving it.
    minted = fontlib.synthetic_codepoints(face)
    g.rec("SYNRANGE", fontlib.SYNTHETIC_FIRST, fontlib.SYNTHETIC_LAST)
    reachable = {n for chain in data.math.vertical_variants.values() for n, _ in chain}
    g.rec("SYNCOUNT", len(minted), len(reachable))
    assert sorted(minted.values()) == [minted[cp] for cp in sorted(minted, reverse=True)]
    by_name_gids = [gid_of[minted[cp]] for cp in sorted(minted, reverse=True)]
    # 0: the minted glyphs in name order are NOT in glyph-id order, so a reader
    # that sorts by glyph id assigns different codepoints.
    g.rec("SYNSTAT", "name_order_is_glyph_id_order", by_name_gids == sorted(by_name_gids))
    g.rec("SYNSTAT", "minted_whose_codepoint_differs_under_glyph_id_order",
          sum(1 for a, b in zip(by_name_gids, sorted(by_name_gids)) if a != b))  # fmt: skip
    for cp in sorted(minted, reverse=True):
        g.rec("SYN", cp, gid_of[minted[cp]], s(minted[cp]))
    # Every reachable glyph and the codepoint a GlyphVariant carries for it:
    # the LOWEST codepoint the font maps to the glyph, else the minted one.
    for name in sorted(reachable, key=lambda n: gid_of[n]):
        g.rec("VARIANTCP", gid_of[name], data.codepoints[name], data.codepoints[name] in minted)
    font.close()
    return g.name, g.write()


# --------------------------------------------------------------------------------
# ANSWERS: one file per role
# --------------------------------------------------------------------------------


def codepoint_sample(face: Face) -> list[int]:
    data = fontlib._face_data(face)
    mapped = sorted(data.cmap)
    chosen: set[int] = set(range(0x20, 0x7F)) | set(range(0xA0, 0x100)) | set(AWKWARD)
    chosen |= {0, 9, 0xA, 0xD, 0x394, 0x3A9, 0x3C7, 0x2013, 0x2014, 0x2015, 0x2126, 0x2206, 0xFFFD}
    chosen |= set(mapped[::5])
    if data.math is not None:
        by_name = data.codepoints
        chosen |= set(fontlib.synthetic_codepoints(face))
        chosen |= {by_name[n] for n in data.math.vertical_variants if n in by_name}
        italic = sorted(cp for cp in mapped if data.cmap[cp] in data.math.italic_corrections)
        chosen |= set(italic[::3])
        chosen |= set(range(0x391, 0x3CA)) | set(range(0x2200, 0x2300)) | {0x1D44E, 0x1D453, 0x1D6FC}
    return sorted(chosen)


def kern_pair_sample(face: Face, sample: list[int]) -> list[tuple[int, int]]:
    data = fontlib._face_data(face)
    pairs: set[tuple[int, int]] = {(ord(p[0]), ord(p[1])) for p in CLASSIC_PAIRS}
    # Unmapped on either side, on both, and a codepoint against itself.
    pairs |= {(0xE000, 0xE000), (0xE000, 0x41), (0x41, 0xE000), (0x110000, 0x54), (0x54, 0x110000)}
    pairs |= {(0x54, 0x6F), (0x3A4, 0x3BF), (0x422, 0x43E), (0x2014, 0x54), (0xC5, 0x56)}
    rng = Lcg(len(sample) * 7919 + len(face.value))
    ascii_axis = list(range(0x20, 0x7F))
    nonzero = [
        (a, b)
        for a in ascii_axis
        for b in ascii_axis
        if data.pair_kern(data.glyph(a), data.glyph(b))
    ]
    for _ in range(120):
        if nonzero:
            pairs.add(nonzero[rng.below(len(nonzero))])
        pairs.add((sample[rng.below(len(sample))], sample[rng.below(len(sample))]))
    return sorted(pairs)


def dump_answers(face: Face, name: str, title: str) -> tuple[str, int]:
    path = fontlib.font_file(face)
    data = fontlib._face_data(face)
    font = TTFont(path, lazy=True)
    gid_of = {n: i for i, n in enumerate(font.getGlyphOrder())}
    font.close()
    is_math = data.math is not None
    g = Golden(name, title)
    g.rec("FACE", s(face.value), s(path.name), s(fontlib.backing_face(face).value), is_math)
    sample = codepoint_sample(face)
    pairs = kern_pair_sample(face, sample)

    # Size-independent: which glyph a codepoint resolves to. An unmapped
    # codepoint resolves to `.notdef`, glyph 0, and is then measured as that
    # glyph: its advance and ink box are .notdef's, not zero.
    g.rec("SAMPLE", len(sample), len(pairs))
    for cp in sample:
        glyph = fontlib.glyph_name(face, cp)
        g.rec("GN", cp, cp in data.cmap, gid_of[glyph], s(glyph))

    for size in SIZES:
        m = fontlib.metrics(face, size)
        assert m is fontlib.metrics(face, size)  # identical arguments, identical object
        g.rec("SIZE", size)
        g.rec("FM", m.ascent, m.descent, m.x_height, m.em, m.space_width, m.size)
        for cp in sample:
            box = m.bbox(cp)
            # GlyphBBox's derived properties are not written: they are exactly
            # these, and `width` is one IEEE subtraction of two written values.
            assert box.height == box.y_max and box.depth == -box.y_min
            assert box.width == box.x_max - box.x_min
            g.rec("CH", cp, m.has(cp), m.advance(cp), box.x_min, box.y_min, box.x_max, box.y_max)
        for left, right in pairs:
            g.rec("K", left, right, m.kern(left, right))
        if is_math:
            params = m.math
            for field in dataclasses.fields(MathParams):
                g.rec("MP", field.name, float(getattr(params, field.name)))
            for cp in sample:
                value = m.italic_correction(cp)
                chain = m.vertical_variants(cp)
                if value != 0.0:
                    g.rec("IC", cp, value)
                if chain:
                    flat: list[object] = []
                    for variant in chain:
                        flat += [variant.codepoint, gid_of[variant.name], variant.extent]
                    g.rec("VV", cp, len(chain), *flat)
            # Absent answers, stated: 0.0 and the empty tuple, for a codepoint
            # with no entry and for one the font does not map at all.
            for cp in (0x41, 0xE000, 0x110000):
                assert m.vertical_variants(cp) == ()
                g.rec("NOVV", cp)
            for cp in (0x20, 0xE000, 0x110000):
                g.rec("IC0", cp, m.italic_correction(cp))
    return g.name, g.write()


# --------------------------------------------------------------------------------
# ANSWERS: the registry, and what goes wrong
# --------------------------------------------------------------------------------


# Scaled-point em sizes for the rounded triples: 10, 12, 13 and 16 pt, 14.4 pt
# rounded to an integer, and the odd one from SIZES.
MEASURE_SIZES = (655360, 786432, 851968, 1048576, 943718, 894785)


def dump_char_measure(face: Face) -> tuple[str, int]:
    """
    `advtext.style.char_measure` for every codepoint the face maps, minted included.

    These integers are what layout consumes: `round()` of the float advance,
    height and depth at an em size in scaled points. Python's `round` is
    half-to-even; CMSTAT says how many values sat exactly on a half.
    """
    from advtext.nodes import CharNode
    from advtext.style import char_measure, font_key, fontlib_metrics

    data = fontlib._face_data(face)
    path = fontlib.font_file(face)
    g = Golden(f"{face.value}.charmeasure.golden",
               f"ANSWERS of advtext.style.char_measure for role {face.value}")  # fmt: skip
    g.rec("FACE", s(face.value), s(path.name), s(fontlib.backing_face(face).value),
          data.math is not None)  # fmt: skip
    codepoints = sorted(data.cmap) + [0xE000, 0x110000]
    g.rec("CMCOUNT", len(codepoints), len(MEASURE_SIZES))
    measure = char_measure(fontlib_metrics)
    ties = 0
    for size in MEASURE_SIZES:
        key = font_key(face, size)
        g.rec("CMSIZE", size, s(key))
        m = fontlib.metrics(face, float(size))
        for cp in codepoints:
            dimens = measure(CharNode(font=key, codepoint=cp))
            box = m.bbox(cp)
            ties += sum(1 for v in (m.advance(cp), box.height, box.depth) if v % 1.0 == 0.5)
            g.rec("CM", cp, dimens.width, dimens.height, dimens.depth)
    g.rec("CMSTAT", "values_exactly_on_a_half", ties)
    return g.name, g.write()


def x_height_fallback(g: Golden, face: Face, scratch: Path) -> None:
    """
    The x-height fallback, which no bundled file reaches, made observable.

    A copy of the role's file with the two bytes of OS/2.sxHeight zeroed is
    registered and asked. The record gives the byte offset to zero, so a test
    can make the same file in memory from tests/data/fonts.
    """
    source = fontlib.font_file(face)
    blob = bytearray(source.read_bytes())
    offset = next(off for tag, _c, off, _l in table_directory(bytes(blob)) if tag == "OS/2") + 86
    before = struct.unpack_from(">h", blob, offset)[0]
    blob[offset : offset + 2] = b"\x00\x00"
    patched = scratch / f"sxheight0-{source.name}"
    patched.write_bytes(blob)
    upm = fontlib._face_data(face).units_per_em
    fontlib.register_font(face, patched)
    after = fontlib._face_data(face)
    m = fontlib.metrics(face, 16.0)
    g.rec("XHEIGHTFALLBACK", s(source.name), offset, before, upm, after.x_height, m.x_height)
    assert after.x_height == round(0.45 * upm)
    fontlib.unregister_font(face)
    patched.unlink()


def outcome(call: Any) -> tuple[str, str]:
    """('ok', repr-ish) or (exception type name, message) for one call."""
    try:
        value = call()
    except Exception as exc:  # the type is the datum
        return type(exc).__name__, str(exc)
    return "ok", "" if value is None else str(value)


def registry_state(g: Golden, label: str) -> None:
    g.rec("STATE", s(label))
    for face in Face:
        path = fontlib.font_file(face)
        blob = path.read_bytes()
        g.rec("ROLE", s(face.value), s(path.name), s(fontlib.backing_face(face).value),
              len(blob), hashlib.sha256(blob).hexdigest(),
              len(fontlib.synthetic_codepoints(face)))  # fmt: skip
    for face in Face:
        for height in PIXEL_HEIGHTS:
            g.rec("EMSIZE", s(face.value), height, fontlib.em_size_for_pixel_height(face, height))


def dump_registry(extra: Path) -> tuple[str, int]:
    g = Golden("registry.golden", "ANSWERS: roles, files, em_size_for_pixel_height, and the failures")
    g.rec("SYNRANGE", fontlib.SYNTHETIC_FIRST, fontlib.SYNTHETIC_LAST)
    registry_state(g, "default")

    # Failures. Messages that would carry an absolute path are not recorded;
    # the ones that name only a role and a file's basename are.
    prose = fontlib._face_data(Face.PROSE)
    kind, message = outcome(lambda: fontlib.MathFaceMetrics(prose, 10.0))
    g.rec("ERR", "math_metrics_over_a_face_without_MATH", kind, s(message))
    kind, _ = outcome(lambda: fontlib.register_font(Face.CODE_BOLD, extra.parent / "no-such-font.ttf"))
    g.rec("ERR", "register_a_file_that_does_not_exist", kind, "%")
    assert not fontlib.registered_fonts()
    kind, _ = outcome(lambda: fontlib.unregister_font(Face.CODE_BOLD))
    g.rec("ERR", "unregister_a_role_with_nothing_registered", kind, "%")

    # Registration: a role the bundle has no file for gets one, and a role that
    # has one is replaced. Both are undone, and the state after is the default.
    fontlib.register_font(Face.CODE_BOLD, extra)
    registry_state(g, f"registered code-bold {extra.name}")
    fontlib.unregister_font(Face.CODE_BOLD)

    fontlib.register_font(Face.MATH, fontlib.font_file(Face.PROSE))
    kind, message = outcome(lambda: fontlib.math_metrics(10.0))
    g.rec("ERR", "math_role_registered_to_a_file_without_MATH", kind, s(message))
    kind, message = outcome(lambda: len(fontlib.synthetic_codepoints(Face.MATH)))
    g.rec("ERR", "synthetic_codepoints_of_that_role", kind, s(message))
    fontlib.unregister_font(Face.MATH)
    assert not fontlib.registered_fonts()
    registry_state(g, "default again")

    with tempfile.TemporaryDirectory(prefix="imtypeset-oracle-font-") as scratch:
        for face in (Face.PROSE, Face.CODE, Face.MATH):
            x_height_fallback(g, face, Path(scratch))
    assert not fontlib.registered_fonts()
    return g.name, g.write()


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    extra = fontlib.font_directory() / "DroidSans.ttf"
    written: list[tuple[str, int]] = []

    # Raw data once per file. CODE_BOLD is skipped: with nothing registered it
    # is PROSE_BOLD's file.
    seen: set[str] = set()
    for face in Face:
        path = fontlib.font_file(face)
        if path.name in seen:
            continue
        seen.add(path.name)
        written.append(dump_raw(face, f"role {face.value}"))
    written.append(dump_math())
    for face in Face:
        written.append(
            dump_answers(
                face,
                f"{face.value}.answers.golden",
                f"ANSWERS of advtext.fontlib for role {face.value}",
            )
        )
    # The rounded triples layout consumes. CODE_BOLD is skipped: with nothing
    # registered every answer is PROSE_BOLD's, which code-bold.answers.golden shows.
    for face in Face:
        if fontlib.backing_face(face) is face:
            written.append(dump_char_measure(face))
    written.append(dump_registry(extra))

    # A file that is no bundled role, read through a registered role. It is here
    # for what the bundled faces do not exercise: numberOfHMetrics < numGlyphs
    # and a kern feature made of PairPos format 1 alone.
    assert extra.name not in seen
    fontlib.register_font(Face.CODE_BOLD, extra)
    written.append(dump_raw(Face.CODE_BOLD, "no bundled role; registered as code-bold"))
    written.append(
        dump_answers(
            Face.CODE_BOLD,
            f"registered.{extra.name}.answers.golden",
            f"ANSWERS of advtext.fontlib for {extra.name} registered as code-bold",
        )
    )
    fontlib.unregister_font(Face.CODE_BOLD)

    stale = sorted(p.name for p in OUT.glob("*.golden") if p.name not in {n for n, _ in written})
    for name, size in written:
        print(f"{size:9d}  tests/golden/font/{name}")
    print(f"{sum(size for _, size in written):9d}  total in {len(written)} files")
    if stale:
        print("stale goldens not written by this run:", ", ".join(stale), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
