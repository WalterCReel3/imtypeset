#!/usr/bin/env python3
"""
Dump layout-core goldens from the Python reference (imgui-adv-text).

Writes tests/golden/core/*.golden. Each file carries INPUT and EXPECTED, so a
C++ test replays the input through the port and compares; the C++ side needs no
Python and, because every case carries the CharMeasure answers it used, no font
reader either. The record vocabulary is specified in tools/oracle/FORMAT.md.

Run from the imtypeset repository root, with the reference's interpreter:

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=../imgui-adv-text \\
        ../imgui-adv-text/.venv/bin/python -B tools/oracle/dump_core.py

    ... dump_core.py --verify   re-reads every golden with a reader written
                                from FORMAT.md alone, rebuilds the inputs from
                                the records, replays them through the reference
                                and compares with the recorded expectation.

The script only reads the reference. Output is deterministic: no clock, no
`random`, no hash-ordered iteration reaches a file, so a second run is
byte-identical to the first.

Where the paragraph material comes from. Font-built paragraphs are produced by
the reference's own composition path and not by a parallel one:
`advtext.markdown.text_block` / `build_document` turn text into material
(`_Converter._append_text`: CharNodes, GPOS kerns as Kern(NORMAL), interword
glue from `advtext.style.interword_glue`), measured through
`advtext.style.char_measure(fontlib_metrics)`; the breaking parameters and
per-line shape are `advtext.ui._skips`, `_shape` and `_unshrinkable` applied to
`PROSE_BREAKING`, exactly as `advtext.ui._text_boxes` applies them. What this
script adds on top is only the vertical assembly used for the ship-out
expectation (see `_ship_lines`), which uses pack.append_to_vlist directly and
no Strut, because Strut belongs to the style layer.

Break positions are not part of the reference's public result. They are read
from its private pieces (`_prepare`, `_Breaker`, `_first_fit`, `_chain`), and
every run asserts that assembling those breaks reproduces the public
`break_paragraph` answer, so the positions cannot drift from it.
"""

from __future__ import annotations

import argparse
import enum
import sys
from collections.abc import Callable, Iterable, Iterator, Sequence
from dataclasses import dataclass, field, replace
from pathlib import Path

from advtext import linebreak as L
from advtext import markdown, ui
from advtext.badness import INF_BAD, badness
from advtext.linebreak import (
    AWFUL_BAD,
    BreakPass,
    LineBreakParams,
    LineSpec,
    Paragraph,
    ParShape,
    break_paragraph,
)
from advtext.mathtext import markdown_math
from advtext.nodes import (
    EJECT_PENALTY,
    INF_PENALTY,
    Box,
    BoxKind,
    CharNode,
    Disc,
    Glue,
    GlueSign,
    GlueSpec,
    GlueTotal,
    Kern,
    KernKind,
    MathOff,
    MathOn,
    Node,
    Penalty,
    Rule,
    is_discardable,
    permits_break_at_following_glue,
)
from advtext.pack import (
    IGNORE_DEPTH,
    OVERFULL_BADNESS,
    CharDimens,
    CharMeasure,
    InterlineSkip,
    PackFitness,
    PackMode,
    PackResult,
    append_to_vlist,
    hpack,
    interline_glue,
    vpack,
)
from advtext.shipout import (
    BILLION,
    Atom,
    DrawCmd,
    Glyph,
    Line,
    Point,
    RuleRect,
    SetGlue,
    atom_box,
    is_atom,
    pascal_round,
    ship,
)
from advtext.style import (
    Align,
    BlockKind,
    Faces,
    Spacing,
    Theme,
    char_measure,
    fontlib_metrics,
)
from advtext.units import (
    BP_PER_INCH,
    DEFAULT_PX_PER_INCH,
    MAX_DIMEN,
    PT_PER_INCH,
    RUNNING,
    UNITY,
    GlueOrder,
    Scaled,
    pt_from_sp,
    px_from_sp,
    round_to_px,
    sp_from_bp,
    sp_from_pt,
    sp_from_px,
)

ROOT = Path(__file__).resolve().parents[2]
OUT_DIR = ROOT / "tests" / "golden" / "core"
REFERENCE = ROOT.parent / "imgui-adv-text"
FORMAT_VERSION = 1
PT = UNITY

# --------------------------------------------------------------------------
# Lexical layer
# --------------------------------------------------------------------------


def q(text: str) -> str:
    """
    A string as one token: UTF-8, with every byte that is not printable ASCII,
    and '%', '#' and space, written %XX. The empty string is a lone '%'.
    """
    if text == "":
        return "%"
    out: list[str] = []
    for byte in text.encode("utf-8"):
        if byte <= 0x20 or byte >= 0x7F or byte in (0x25, 0x23):
            out.append(f"%{byte:02X}")
        else:
            out.append(chr(byte))
    return "".join(out)


def unq(token: str) -> str:
    """
    The inverse of `q`, as FORMAT.md states it.
    """
    if token == "%":
        return ""
    raw = bytearray()
    index = 0
    while index < len(token):
        if token[index] == "%":
            raw.append(int(token[index + 1 : index + 3], 16))
            index += 3
        else:
            raw.append(ord(token[index]))
            index += 1
    return raw.decode("utf-8")


def tok(value: object) -> str:
    """
    One value as one token. Enums are their lower-case member name, booleans
    0/1, floats `float.hex()`, integers decimal; a `str` is taken to be a token
    already (free text goes through `q` at the call site).
    """
    if isinstance(value, enum.Enum):
        return value.name.lower()
    if isinstance(value, bool):
        return "1" if value else "0"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return value.hex()
    if isinstance(value, str):
        assert value and " " not in value and "\n" not in value, repr(value)
        return value
    raise TypeError(type(value))


class Writer:
    """
    One golden file being built in memory.
    """

    def __init__(self, name: str, about: str) -> None:
        self.name = name
        self.lines: list[str] = [
            "# Generated by tools/oracle/dump_core.py from the Python reference. Do not edit.",
            "# Record vocabulary: tools/oracle/FORMAT.md.",
            *[f"# {line}" for line in about.strip().splitlines()],
            f"golden {FORMAT_VERSION} {q('core/' + name)}",
        ]
        self.cases = 0
        self.names: set[str] = set()

    def r(self, *tokens: object) -> None:
        self.lines.append(" ".join(tok(token) for token in tokens))

    def case(self, name: str, kind: str) -> None:
        assert name not in self.names, f"duplicate case {name} in {self.name}"
        self.names.add(name)
        self.cases += 1
        self.r("case", q(name), kind)

    def end(self) -> None:
        self.r("endcase")

    def text(self) -> str:
        return "\n".join(self.lines) + "\n"


# --------------------------------------------------------------------------
# Emitters for the shared records: nodes, specs, pack results, draw commands
# --------------------------------------------------------------------------


def spec_tokens(spec: GlueSpec) -> tuple[object, ...]:
    return (spec.width, spec.stretch, spec.stretch_order, spec.shrink, spec.shrink_order)


def emit_node(w: Writer, node: Node) -> None:
    if isinstance(node, markdown.ReplacedChar):
        # Before CharNode, which it subclasses. It measures, packs, breaks and
        # ships as the CharNode it is; `original` is what a copy returns.
        w.r("rchar", q(node.font), node.codepoint, node.original)
    elif isinstance(node, CharNode):
        w.r("char", q(node.font), node.codepoint)
    elif isinstance(node, Box):
        w.r(
            "box",
            node.kind,
            node.width,
            node.height,
            node.depth,
            node.shift_amount,
            float(node.glue_set),
            node.glue_sign,
            node.glue_order,
            len(node.contents),
        )
        for child in node.contents:
            emit_node(w, child)
        w.r("endbox")
    elif isinstance(node, Rule):
        w.r("rule", node.width, node.height, node.depth)
    elif isinstance(node, Glue):
        w.r("glue", *spec_tokens(node.spec))
    elif isinstance(node, Kern):
        w.r("kern", node.width, node.kind)
    elif isinstance(node, Penalty):
        w.r("penalty", node.value)
    elif isinstance(node, Disc):
        w.r("disc", node.replace_count, len(node.pre_break), len(node.post_break))
        for child in node.pre_break:
            emit_node(w, child)
        for child in node.post_break:
            emit_node(w, child)
        w.r("enddisc")
    elif isinstance(node, MathOn):
        w.r("mathon", node.width)
    elif isinstance(node, MathOff):
        w.r("mathoff", node.width)
    else:
        raise TypeError(type(node))


def emit_nodes(w: Writer, name: str, nodes: Sequence[Node]) -> None:
    w.r("nodes", name, len(nodes))
    for node in nodes:
        emit_node(w, node)
    w.r("endnodes")


def emit_spec(w: Writer, role: str, spec: GlueSpec) -> None:
    w.r("spec", role, *spec_tokens(spec))


def emit_skip(w: Writer, skip: InterlineSkip) -> None:
    emit_spec(w, "baselineskip", skip.baselineskip)
    emit_spec(w, "lineskip", skip.lineskip)
    w.r("lineskiplimit", skip.lineskiplimit)


def emit_packed(w: Writer, result: PackResult) -> None:
    w.r(
        "packed",
        result.fitness,
        result.natural,
        result.excess,
        result.badness,
        result.overfull_by,
    )
    emit_node(w, result.box)


def emit_cmds(w: Writer, cmds: Sequence[DrawCmd]) -> None:
    w.r("cmds", len(cmds))
    for cmd in cmds:
        if isinstance(cmd, Glyph):
            w.r("glyph", q(cmd.font), cmd.codepoint, cmd.x, cmd.y, cmd.index)
        elif isinstance(cmd, RuleRect):
            w.r("rulerect", cmd.x, cmd.y, cmd.width, cmd.height)
        elif isinstance(cmd, SetGlue):
            # Atom first: it is a SetGlue subclass with the same seven fields.
            tag = "atom" if isinstance(cmd, Atom) else "setglue"
            w.r(tag, cmd.x, cmd.y, cmd.width, cmd.natural, cmd.height, cmd.depth, cmd.index)
        elif isinstance(cmd, Line):
            w.r("line", cmd.x, cmd.y, cmd.width, cmd.height, cmd.depth, cmd.start, cmd.end)
        else:
            raise TypeError(type(cmd))
    w.r("endcmds")


def chars_in(nodes: Iterable[Node]) -> Iterator[CharNode]:
    for node in nodes:
        if isinstance(node, CharNode):
            yield node
        elif isinstance(node, Box):
            yield from chars_in(node.contents)
        elif isinstance(node, Disc):
            yield from chars_in(node.pre_break)
            yield from chars_in(node.post_break)


def emit_measure(w: Writer, measure: CharMeasure | None, *lists: Iterable[Node]) -> None:
    """
    The CharMeasure answer for every (font, codepoint) in the given lists,
    sorted, so a C++ test can answer from a table without a font reader.
    """
    if measure is None:
        return
    pairs = sorted({(c.font, c.codepoint) for nodes in lists for c in chars_in(nodes)})
    for font, codepoint in pairs:
        dimens = measure(CharNode(font, codepoint))
        w.r("measure", q(font), codepoint, dimens.width, dimens.height, dimens.depth)


# --------------------------------------------------------------------------
# Deterministic pseudo-random numbers (no `random`: its stream is not a contract)
# --------------------------------------------------------------------------


class Rng:
    """
    A 64-bit LCG (Knuth's MMIX constants). Only the high bits are used.
    """

    def __init__(self, seed: int) -> None:
        self.state = seed & 0xFFFFFFFFFFFFFFFF

    def bits(self) -> int:
        self.state = (self.state * 6364136223846793005 + 1442695040888963407) & 0xFFFFFFFFFFFFFFFF
        return self.state >> 33

    def below(self, n: int) -> int:
        return self.bits() % n

    def between(self, low: int, high: int) -> int:
        return low + self.below(high - low + 1)

    def pick(self, items: Sequence):  # type: ignore[type-arg]
        return items[self.below(len(items))]

    def chance(self, numerator: int, denominator: int) -> bool:
        return self.below(denominator) < numerator


# --------------------------------------------------------------------------
# Hand-built material
# --------------------------------------------------------------------------

_DESCENDS = frozenset("gjpqy")
_ASCENDS = frozenset("bdfhijklt")


def fake_measure(node: CharNode) -> CharDimens:
    """
    Round-ish but not round metrics for hand-built lists: no font is opened.

    The odd scaled-point remainder (`+ codepoint`) is deliberate: widths that
    are whole points hide rounding errors in the glue arithmetic.
    """
    scale = 2 if node.font.startswith("big") else 1
    character = chr(node.codepoint)
    width = ((4 + node.codepoint % 5) * PT + node.codepoint) * scale
    if character.isupper() or character in _ASCENDS or character.isdigit():
        height = 7 * PT * scale
    else:
        height = (9 * PT // 2 + 3) * scale
    depth = 2 * PT * scale if character in _DESCENDS else 0
    return CharDimens(width=width, height=height, depth=depth)


def chars(text: str, font: str = "fake@10") -> list[Node]:
    return [CharNode(font, ord(character)) for character in text]


def word(points: int, height: int = 7, depth: int = 2) -> Box:
    return Box(kind=BoxKind.HLIST, width=points * PT, height=height * PT, depth=depth * PT)


SPACE = GlueSpec(width=6 * PT, stretch=4 * PT, shrink=2 * PT)
STIFF = GlueSpec(width=6 * PT)
FIL = GlueSpec(stretch=PT, stretch_order=GlueOrder.FIL)
LINE = 100 * PT
SHOWCASE = (12, 19, 27, 14, 12, 13, 19, 15, 18)
PERMISSIVE = LineBreakParams(pretolerance=10000, tolerance=10000)
RAGGED = LineBreakParams(
    right_skip=FIL, hyphen_penalty=0, double_hyphen_demerits=0, final_hyphen_demerits=0
)
HAND_SKIP = InterlineSkip(
    baselineskip=GlueSpec(width=14 * PT, stretch=PT // 2, shrink=PT // 4),
    lineskip=GlueSpec(width=PT, stretch=PT // 8),
    lineskiplimit=0,
)


def para(widths: Sequence[int], space: GlueSpec = SPACE) -> list[Node]:
    out: list[Node] = []
    for index, width in enumerate(widths):
        if index:
            out.append(Glue(space))
        out.append(word(width))
    return out


# --------------------------------------------------------------------------
# units.golden
# --------------------------------------------------------------------------


def dump_units() -> Writer:
    w = Writer(
        "units",
        """
Conversions of advtext/units.py. Python round() is half-to-even; the tie cases
are in here on purpose. Every integer result fits in a signed 64-bit integer.
""",
    )
    rng = Rng(0x756E697473)

    w.case("constants", "table")
    for name, value in (
        ("UNITY", UNITY),
        ("MAX_DIMEN", MAX_DIMEN),
        ("RUNNING", RUNNING),
        ("PT_PER_INCH", PT_PER_INCH),
        ("BP_PER_INCH", BP_PER_INCH),
        ("DEFAULT_PX_PER_INCH", DEFAULT_PX_PER_INCH),
    ):
        w.r("const", name, "float" if isinstance(value, float) else "int", value)
    for order in GlueOrder:
        w.r("const", "GlueOrder." + order.name, "int", int(order))
    w.end()

    ties = [(k + 0.5) / UNITY for k in range(-6, 7)]
    points: list[float] = [
        0.0,
        -0.0,
        1.0,
        -1.0,
        10.0,
        12.0,
        -5.0,
        16383.0,
        0.5,
        0.25,
        12.75,
        -3.125,
        1.0 / 65536.0,
        0.1,
        1.0 / 3.0,
        9.9999,
        12.045,
        72.27,
        72.0,
        -0.7,
        2.5,
        3.5,
        -2.5,
        MAX_DIMEN / UNITY,
        -MAX_DIMEN / UNITY,
        16383.99999,
        16384.0,
        1e-9,
        -1e-9,
        1e6,
        -1e6,
        123456789.125,
        1e12,
        *ties,
        *[(k + 0.5) / UNITY + 1000.0 for k in range(-3, 4)],
    ]
    for _ in range(120):
        kind = rng.below(4)
        if kind == 0:
            points.append((rng.below(1 << 30) - (1 << 29)) / 1000.0)
        elif kind == 1:
            points.append((rng.below(1 << 24) - (1 << 23)) / 4096.0)
        elif kind == 2:
            points.append((2 * rng.below(1 << 20) + 1) / (2.0 * UNITY) * rng.pick((1, -1)))
        else:
            points.append((rng.below(1 << 30) - (1 << 29)) / 7.0)

    w.case("sp_from_pt", "table")
    for value in points:
        w.r("sp_from_pt", value, sp_from_pt(value))
    w.end()

    w.case("sp_from_bp", "table")
    for value in points:
        w.r("sp_from_bp", value, sp_from_bp(value))
    w.end()

    resolutions = (DEFAULT_PX_PER_INCH, 72.0, PT_PER_INCH, 144.0, 120.5, 192.0)
    pixels: list[float] = [
        0.0,
        -0.0,
        0.5,
        1.0,
        3.4,
        13.0,
        16.0,
        24.0,
        800.0,
        -12.5,
        420.0,
        0.1,
        1e-7,
        65536.0,
        1e9,
        *ties,
    ]
    for _ in range(60):
        pixels.append((rng.below(1 << 26) - (1 << 25)) / rng.pick((8.0, 10.0, 3.0, 64.0)))
    w.case("sp_from_px", "table")
    for resolution in resolutions:
        for value in pixels:
            w.r("sp_from_px", value, resolution, sp_from_px(value, resolution))
    w.end()

    lengths: list[Scaled] = [
        0,
        1,
        -1,
        2,
        3,
        UNITY,
        -UNITY,
        UNITY // 2,
        UNITY + 1,
        789381,
        MAX_DIMEN,
        -MAX_DIMEN,
        MAX_DIMEN + 1,
        RUNNING,
        IGNORE_DEPTH,
        1 << 31,
        (1 << 31) - 1,
        -(1 << 31),
        (1 << 40) + 1,
        (1 << 53) + 1,
        -((1 << 53) + 1),
        (1 << 62) - 1,
        sp_from_pt(2.5),
        sp_from_pt(3.5),
        sp_from_pt(-2.5),
        sp_from_pt(0.5),
        sp_from_pt(1.5),
        sp_from_pt(-0.5),
        sp_from_pt(-1.5),
        sp_from_px(0.5),
        sp_from_px(3.4),
        3 * sp_from_px(3.4),
        sp_from_px(16.0),
    ]
    for _ in range(120):
        lengths.append(rng.below(1 << rng.pick((12, 20, 26, 31, 34))) * rng.pick((1, -1)))

    w.case("pt_from_sp", "table")
    for length in lengths:
        w.r("pt_from_sp", length, pt_from_sp(length))
    w.end()

    w.case("px_from_sp", "table")
    for resolution in resolutions:
        for length in lengths:
            w.r("px_from_sp", length, resolution, px_from_sp(length, resolution))
    w.end()

    # What Python does with inputs C++ has undefined or different behaviour for.
    # The port cannot raise; these say which inputs it has to have an answer for.
    w.case("non-finite-and-huge", "table")
    nan, inf = float("nan"), float("inf")
    calls: list[tuple[str, Callable[..., object], tuple[object, ...]]] = []
    for name, function in (("sp_from_pt", sp_from_pt), ("sp_from_bp", sp_from_bp)):
        for value in (nan, inf, -inf, 1e300, -1e300, 2.0**47, 2.0**48, -(2.0**48), 1.5e14):
            calls.append((name, function, (value,)))
    for args in (
        (nan, 96.0),
        (inf, 96.0),
        (-inf, 96.0),
        (1e300, 96.0),
        (1.0, 0.0),
        (1.0, -0.0),
        (1.0, nan),
        (1.0, inf),
        (1.0, -96.0),
        (0.0, inf),
        (1.0, 1e-300),
    ):
        calls.append(("sp_from_px", sp_from_px, args))
    for args in ((UNITY, 0.0), (UNITY, nan), (UNITY, inf), (UNITY, -inf), (0, inf), (UNITY, -96.0), (UNITY, 1e300), (MAX_DIMEN, 1e308)):
        calls.append(("px_from_sp", px_from_sp, args))
        calls.append(("round_to_px", round_to_px, args))
    for name, function, args in calls:
        try:
            result = function(*args)
        except (ValueError, OverflowError, ZeroDivisionError) as error:
            w.r("raises", name, type(error).__name__.lower(), *args)
        else:
            if isinstance(result, int) and not -(1 << 63) <= result < (1 << 63):
                w.r("beyond_int64", name, str(result), *args)
            else:
                w.r(name, *args, result)
    w.end()

    # round_to_px lands on exact halves at 72.27 px/inch, where a pixel is a
    # point; the half-point lengths above are the half-to-even witnesses.
    w.case("round_to_px", "table")
    for resolution in resolutions:
        for length in lengths:
            w.r("round_to_px", length, resolution, round_to_px(length, resolution))
    w.end()
    return w


# --------------------------------------------------------------------------
# badness.golden
# --------------------------------------------------------------------------


def dump_badness() -> Writer:
    w = Writer(
        "badness",
        """
badness(t, s) of advtext/badness.py (tex.web section 108).
""",
    )
    w.case("constants", "table")
    w.r("const", "INF_BAD", "int", INF_BAD)
    w.end()

    # s = 297 makes r = t, so every value the function can produce is addressed.
    w.case("every-r-at-s-297", "table")
    for t in range(0, 1293):
        w.r("badness", t, 297, badness(t, 297))
    w.end()

    ts = [
        0,
        1,
        2,
        50,
        148,
        149,
        296,
        297,
        1290,
        1291,
        32768,
        65535,
        65536,
        65537,
        131072,
        1_000_000,
        7_000_000,
        7_230_583,
        7_230_584,
        7_230_585,
        7_230_891,
        8_000_000,
        20_000_000,
        MAX_DIMEN - 1,
        MAX_DIMEN,
        MAX_DIMEN + 1,
        1 << 31,
        (1 << 40) + 12345,
    ]
    ss = [
        -(1 << 31),
        -50,
        -1,
        0,
        1,
        2,
        10,
        100,
        296,
        297,
        298,
        65535,
        65536,
        65537,
        1_000_000,
        1_663_496,
        1_663_497,
        1_663_498,
        1_663_793,
        2_000_000,
        7_230_584,
        9_000_000,
        MAX_DIMEN,
        1 << 31,
        (1 << 40) + 54321,
    ]
    w.case("grid", "table")
    for t in ts:
        for s in ss:
            w.r("badness", t, s, badness(t, s))
    w.end()

    w.case("calibration", "table")
    for s in (1, 10, 297, 65536, 1_000_000, 7_230_584):
        w.r("badness", s, s, badness(s, s))
    for s in (100, 65536, 1_000_000, 7_000_000):
        w.r("badness", s // 2, s, badness(s // 2, s))
    w.r("badness", 8_000_000, 2_000_000, badness(8_000_000, 2_000_000))
    w.r("badness", 4, 1, badness(4, 1))
    w.r("badness", 7_230_891, 1_663_793, badness(7_230_891, 1_663_793))
    w.r("badness", 8_000_000, 1_000_000, badness(8_000_000, 1_000_000))
    w.r("badness", 7_230_585, 1_663_496, badness(7_230_585, 1_663_496))
    w.end()

    rng = Rng(0x626164)
    w.case("random", "table")
    for _ in range(600):
        t = rng.below(1 << rng.pick((8, 16, 20, 23, 24, 30)))
        s = rng.below(1 << rng.pick((8, 16, 20, 21, 22, 24, 30)))
        w.r("badness", t, s, badness(t, s))
    w.end()

    # The reference raises ValueError for t < 0. The port has no exceptions;
    # these records name the inputs it must treat as a contract violation.
    w.case("negative-t-is-rejected", "table")
    for t, s in ((-1, 65536), (-1, 0), (-65536, -1), (-MAX_DIMEN, MAX_DIMEN)):
        try:
            badness(t, s)
        except ValueError:
            w.r("badness_raises", t, s)
        else:
            raise AssertionError("the reference accepted a negative t")
    w.end()
    return w


# --------------------------------------------------------------------------
# nodes.golden
# --------------------------------------------------------------------------


def dump_nodes() -> Writer:
    w = Writer(
        "nodes",
        """
advtext/nodes.py: constants, GlueTotal accumulation, and the two break predicates.
""",
    )
    w.case("constants", "table")
    for name, value in (
        ("INF_PENALTY", INF_PENALTY),
        ("EJECT_PENALTY", EJECT_PENALTY),
        ("INF_BAD", INF_BAD),
        ("AWFUL_BAD", AWFUL_BAD),
        ("IGNORE_DEPTH", IGNORE_DEPTH),
        ("OVERFULL_BADNESS", OVERFULL_BADNESS),
        ("BILLION", BILLION),
    ):
        w.r("const", name, "float" if isinstance(value, float) else "int", value)
    w.end()

    n, fil, fill, filll = GlueOrder.NORMAL, GlueOrder.FIL, GlueOrder.FILL, GlueOrder.FILLL
    totals: list[tuple[str, list[tuple[int, GlueOrder]]]] = [
        ("empty", []),
        ("finite-only", [(3 * PT, n), (6 * PT, n)]),
        ("finite-and-fil", [(10 * PT, n), (PT, fil)]),
        # Section 646's own example: 3pt, 8fill, 5fil, 6pt, -3fil, -8fill = 2fil.
        (
            "section-646",
            [(3 * PT, n), (8 * PT, fill), (5 * PT, fil), (6 * PT, n), (-3 * PT, fil), (-8 * PT, fill)],
        ),
        ("tiny-filll-wins", [(1000 * PT, n), (1000 * PT, fil), (1000 * PT, fill), (1, filll)]),
        ("cancels-everywhere", [(5, n), (-5, n), (7, fil), (-7, fil), (9, filll), (-9, filll)]),
        ("negative-dominant", [(4 * PT, n), (-2 * PT, fill)]),
        ("fil-cancels-to-finite", [(4 * PT, n), (PT, fil), (-PT, fil)]),
        ("finite-cancels-fil-zero", [(4 * PT, n), (-4 * PT, n)]),
    ]
    rng = Rng(0x676C7565)
    for index in range(24):
        contributions = [
            (
                rng.between(-5, 9) * rng.pick((1, PT, PT // 3)),
                GlueOrder(rng.pick((0, 0, 0, 1, 1, 2, 3))),
            )
            for _ in range(rng.between(1, 9))
        ]
        totals.append((f"random-{index:02d}", contributions))
    for name, contributions in totals:
        w.case("gluetotal-" + name, "gluetotal")
        total = GlueTotal()
        for amount, order in contributions:
            w.r("contrib", amount, order)
            total = total + GlueTotal.of(amount, order)
        w.r("total", *total.amounts, total.dominant_order, total.dominant)
        w.end()

    box = Box(BoxKind.HLIST, width=PT)
    vbox = Box(BoxKind.VLIST, width=PT)
    subjects: list[Node] = [
        CharNode("fake@10", ord("a")),
        box,
        vbox,
        Rule(),
        Rule(width=PT, height=PT, depth=0),
        Glue(GlueSpec()),
        Glue(SPACE),
        Kern(PT, KernKind.NORMAL),
        Kern(PT, KernKind.EXPLICIT),
        Kern(PT, KernKind.ACCENT),
        Kern(0, KernKind.EXPLICIT),
        Penalty(0),
        Penalty(INF_PENALTY),
        Penalty(EJECT_PENALTY),
        Disc(),
        Disc(pre_break=(box,), post_break=(box,), replace_count=1),
        MathOn(),
        MathOff(),
        MathOn(PT),
        MathOff(PT),
    ]
    w.case("break-predicates", "predicates")
    for node in subjects:
        emit_node(w, node)
        w.r("pred", is_discardable(node), permits_break_at_following_glue(node))
    w.end()

    w.case("penalty-predicates", "predicates")
    for value in (
        0,
        1,
        -1,
        50,
        -50,
        9999,
        10000,
        10001,
        -9999,
        -10000,
        -10001,
        1 << 30,
        -(1 << 30),
    ):
        penalty = Penalty(value)
        emit_node(w, penalty)
        w.r("penalty_pred", penalty.forbids_break, penalty.forces_break)
    w.end()

    w.case("rule-predicates", "predicates")
    for rule in (
        Rule(),
        Rule(PT, PT, PT),
        Rule(RUNNING, PT, PT),
        Rule(PT, RUNNING, PT),
        Rule(PT, PT, RUNNING),
        Rule(0, 0, 0),
        Rule(RUNNING + 1, PT, PT),
    ):
        emit_node(w, rule)
        w.r("rule_pred", rule.is_running)
    w.end()
    return w


# --------------------------------------------------------------------------
# Random material, shared by pack and shipout
# --------------------------------------------------------------------------

_ODD = (1, 3, PT // 3, PT // 7, PT, PT + 1, 2 * PT - 1)


def random_spec(rng: Rng, *, infinite: bool = True) -> GlueSpec:
    orders = (0, 0, 0, 0, 1, 1, 2, 3) if infinite else (0,)
    stretch = rng.between(0, 6) * rng.pick(_ODD)
    shrink = rng.between(0, 4) * rng.pick(_ODD)
    if rng.chance(1, 14):
        stretch = -stretch
    if rng.chance(1, 20):
        shrink = -shrink
    return GlueSpec(
        width=rng.between(-2, 8) * rng.pick(_ODD),
        stretch=stretch,
        stretch_order=GlueOrder(rng.pick(orders)),
        shrink=shrink,
        shrink_order=GlueOrder(rng.pick(orders)),
    )


def random_hlist(rng: Rng, depth: int = 0, *, with_chars: bool = True) -> list[Node]:
    out: list[Node] = []
    for _ in range(rng.between(1, 9)):
        roll = rng.below(20)
        if roll < 5:
            box = Box(
                BoxKind.HLIST,
                width=rng.between(0, 30) * rng.pick(_ODD),
                height=rng.between(0, 9) * rng.pick(_ODD),
                depth=rng.between(0, 4) * rng.pick(_ODD),
                shift_amount=rng.between(-3, 3) * rng.pick(_ODD) if rng.chance(1, 3) else 0,
            )
            out.append(box)
        elif roll < 8 and with_chars:
            out.extend(chars(rng.pick(("a", "Tg", "xyz", "fi", "Qp", "j")), rng.pick(("fake@10", "big@20"))))
        elif roll < 13:
            out.append(Glue(random_spec(rng)))
        elif roll < 15:
            out.append(Kern(rng.between(-3, 5) * rng.pick(_ODD), KernKind(rng.pick((1, 2, 3)))))
        elif roll < 16:
            out.append(Penalty(rng.pick((0, 50, -50, INF_PENALTY, EJECT_PENALTY))))
        elif roll < 17:
            out.append(
                Rule(
                    width=rng.between(0, 5) * rng.pick(_ODD),
                    height=RUNNING if rng.chance(1, 2) else rng.between(0, 6) * PT,
                    depth=RUNNING if rng.chance(1, 3) else rng.between(0, 3) * PT,
                )
            )
        elif roll < 18:
            out.append(MathOn(rng.between(0, 2) * PT) if rng.chance(1, 2) else MathOff(rng.between(0, 2) * PT))
        elif roll < 19:
            out.append(Disc(pre_break=(word(2),), post_break=(), replace_count=0))
        elif depth < 2:
            inner = random_hlist(rng, depth + 1, with_chars=with_chars)
            packed = hpack(inner, rng.between(-2, 6) * PT, PackMode.ADDITIONAL, measure=fake_measure).box
            if rng.chance(1, 3):
                packed = replace(packed, shift_amount=rng.between(-3, 3) * rng.pick(_ODD))
            out.append(packed)
    return out


def random_vlist(rng: Rng, depth: int = 0) -> list[Node]:
    out: list[Node] = []
    for _ in range(rng.between(1, 7)):
        roll = rng.below(16)
        if roll < 7:
            inner = random_hlist(rng, depth + 1)
            packed = hpack(inner, rng.between(-1, 5) * PT, PackMode.ADDITIONAL, measure=fake_measure).box
            if rng.chance(1, 3):
                packed = replace(packed, shift_amount=rng.between(-4, 6) * rng.pick(_ODD))
            out.append(packed)
        elif roll < 11:
            out.append(Glue(random_spec(rng)))
        elif roll < 12:
            out.append(Kern(rng.between(-2, 5) * rng.pick(_ODD)))
        elif roll < 13:
            out.append(
                Rule(
                    width=RUNNING if rng.chance(1, 2) else rng.between(0, 40) * PT,
                    height=rng.between(0, 2) * rng.pick(_ODD),
                    depth=rng.between(0, 2) * rng.pick(_ODD),
                )
            )
        elif roll < 14:
            out.append(Penalty(rng.pick((0, -100, INF_PENALTY))))
        elif roll < 15:
            out.append(rng.pick((MathOn(PT), MathOff(PT), Disc())))
        elif depth < 1:
            inner_v = random_vlist(rng, depth + 1)
            packed_v = vpack(inner_v, rng.between(-2, 4) * PT, PackMode.ADDITIONAL).box
            if rng.chance(1, 3):
                packed_v = replace(packed_v, shift_amount=rng.between(-2, 4) * PT)
            out.append(packed_v)
    return out


# --------------------------------------------------------------------------
# pack.golden
# --------------------------------------------------------------------------

ERROR_SLUGS = {
    "hpack was given a CharNode": "char-without-measure",
    "ship was given a CharNode": "char-without-measure",
    "a rule's width is never running inside an hlist": "running-width-in-hlist",
    "a character cannot appear in a vertical list": "char-in-vlist",
    "a rule's height and depth are never running inside a vlist": "running-height-or-depth-in-vlist",
    "a ParShape needs at least one LineSpec": "empty-parshape",
}


def slug_of(error: ValueError) -> str:
    message = str(error)
    for prefix, slug in ERROR_SLUGS.items():
        if message.startswith(prefix):
            return slug
    raise AssertionError(f"an error this dump has no name for: {message}")


def hpack_case(
    w: Writer,
    name: str,
    nodes: Sequence[Node],
    target: Scaled = 0,
    mode: PackMode = PackMode.ADDITIONAL,
    measure: CharMeasure | None = fake_measure,
) -> PackResult | None:
    w.case(name, "hpack")
    emit_measure(w, measure, nodes)
    emit_nodes(w, "in", nodes)
    w.r("hpack", target, mode)
    result: PackResult | None = None
    try:
        result = hpack(nodes, target, mode, measure=measure)
    except ValueError as error:
        w.r("error", slug_of(error))
    else:
        emit_packed(w, result)
    w.end()
    return result


def vpack_case(
    w: Writer,
    name: str,
    nodes: Sequence[Node],
    target: Scaled = 0,
    mode: PackMode = PackMode.ADDITIONAL,
    max_depth: Scaled = MAX_DIMEN,
) -> PackResult | None:
    w.case(name, "vpack")
    emit_nodes(w, "in", nodes)
    w.r("vpack", target, mode, max_depth)
    result: PackResult | None = None
    try:
        result = vpack(nodes, target, mode, max_depth=max_depth)
    except ValueError as error:
        w.r("error", slug_of(error))
    else:
        emit_packed(w, result)
    w.end()
    return result


def dump_pack() -> Writer:
    w = Writer(
        "pack",
        """
advtext/pack.py: hpack, vpack, interline_glue, append_to_vlist.
A case with no `measure` record is packed with no CharMeasure at all.
""",
    )
    ex, add = PackMode.EXACTLY, PackMode.ADDITIONAL
    n, fil, fill, filll = GlueOrder.NORMAL, GlueOrder.FIL, GlueOrder.FILL, GlueOrder.FILLL
    none = None

    def g(width: int = 0, stretch: int = 0, so: GlueOrder = n, shrink: int = 0, ho: GlueOrder = n) -> Glue:
        return Glue(GlueSpec(width, stretch, so, shrink, ho))

    # --- hpack: natural size and what each node contributes
    hpack_case(w, "h-empty-natural", [], measure=none)
    hpack_case(w, "h-empty-to-target", [], 72 * PT, ex, measure=none)
    hpack_case(w, "h-empty-negative-target", [], -3 * PT, ex, measure=none)
    hpack_case(w, "h-empty-additional", [], 5 * PT, add, measure=none)
    hpack_case(
        w,
        "h-natural-sums-widths-max-height-depth",
        [word(10, 7, 2), g(6 * PT, 4 * PT, n, 2 * PT), word(20, 9, 1), word(5, 3, 4)],
        measure=none,
    )
    hpack_case(
        w,
        "h-kerns-and-math-add-width-only",
        [word(10), Kern(3 * PT), MathOn(2 * PT), word(4), MathOff(PT), Kern(-PT, KernKind.EXPLICIT)],
        measure=none,
    )
    hpack_case(
        w,
        "h-penalty-and-disc-occupy-nothing",
        [word(10), Penalty(50), Disc(pre_break=(word(2),), post_break=(word(3),), replace_count=1), word(9)],
        measure=none,
    )
    hpack_case(
        w,
        "h-shift-down-gives-up-height-gains-depth",
        [word(10, 7, 2), replace(word(10, 7, 2), shift_amount=3 * PT), replace(word(10, 7, 2), shift_amount=-4 * PT)],
        measure=none,
    )
    hpack_case(
        w,
        "h-shift-past-height-keeps-zero-floor",
        [replace(word(10, 2, 1), shift_amount=5 * PT)],
        measure=none,
    )
    hpack_case(
        w,
        "h-running-rule-loses-the-maximum",
        [word(10, 7, 2), Rule(width=PT), Rule(width=2 * PT, height=9 * PT, depth=RUNNING)],
        measure=none,
    )
    hpack_case(w, "h-only-running-rule", [Rule(width=PT)], measure=none)
    hpack_case(w, "h-rule-negative-width", [Rule(width=-PT, height=PT, depth=PT), word(5)], measure=none)
    hpack_case(w, "h-running-width-rule-is-an-error", [word(5), Rule(height=PT, depth=0)], measure=none)
    hpack_case(w, "h-chars-measured", chars("Typography jq"))
    hpack_case(w, "h-chars-two-fonts", [*chars("ab"), *chars("AB", "big@20"), g(3 * PT), *chars("gy")])
    hpack_case(w, "h-char-without-measure-is-an-error", chars("a"), measure=none)
    hpack_case(w, "h-nested-vbox", [vpack([word(10), g(3 * PT), word(12)]).box, word(4)], measure=none)

    # --- hpack: stretching
    three = [word(10), g(6 * PT, 1 * PT), word(10), g(6 * PT, 2 * PT), word(10), g(6 * PT, 3 * PT), word(10)]
    natural = 58 * PT
    for label, excess in (
        ("one-sp", 1),
        ("third", 2 * PT),
        ("half", 3 * PT),
        ("just-under-all", 6 * PT - 1),
        ("all", 6 * PT),
        ("just-over-all", 6 * PT + 1),
        ("double", 12 * PT),
        ("huge", 5000 * PT),
    ):
        hpack_case(w, f"h-stretch-finite-{label}", three, natural + excess, ex, measure=none)
        hpack_case(w, f"h-stretch-finite-{label}-additional", three, excess, add, measure=none)
    hpack_case(w, "h-stretch-nothing-to-stretch", [word(10), g(6 * PT), word(10)], 40 * PT, ex, measure=none)
    hpack_case(w, "h-stretch-no-glue-at-all", [word(10), word(10)], 40 * PT, ex, measure=none)
    for order in (fil, fill, filll):
        hpack_case(
            w,
            f"h-stretch-one-{order.name.lower()}",
            [word(10), g(6 * PT, 4 * PT), word(10), g(0, PT, order)],
            70 * PT,
            ex,
            measure=none,
        )
    hpack_case(
        w,
        "h-stretch-two-fil-centre",
        [g(0, PT, fil), word(10), g(6 * PT, 4 * PT), word(10), g(0, PT, fil)],
        71 * PT + 1,
        ex,
        measure=none,
    )
    hpack_case(
        w,
        "h-stretch-higher-order-takes-all",
        [g(0, 100 * PT, fil), word(10), g(0, 1, fill), word(10), g(0, 5 * PT)],
        70 * PT,
        ex,
        measure=none,
    )
    hpack_case(
        w,
        "h-stretch-section-646-mix",
        [g(0, 3 * PT), g(0, 8 * PT, fill), g(0, 5 * PT, fil), g(0, 6 * PT), g(0, -3 * PT, fil), g(0, -8 * PT, fill), word(10)],
        30 * PT,
        ex,
        measure=none,
    )
    hpack_case(
        w,
        "h-stretch-infinite-cancels-falls-to-finite",
        [word(10), g(0, PT, fil), g(6 * PT, 4 * PT), g(0, -PT, fil), word(10)],
        29 * PT,
        ex,
        measure=none,
    )
    hpack_case(
        w,
        "h-stretch-negative-finite-total",
        [word(10), g(6 * PT, -4 * PT), word(10)],
        30 * PT,
        ex,
        measure=none,
    )
    hpack_case(
        w,
        "h-stretch-negative-fil-total",
        [word(10), g(6 * PT, -PT, fil), word(10)],
        30 * PT,
        ex,
        measure=none,
    )
    hpack_case(
        w,
        "h-stretch-ratio-not-representable",
        [word(10), g(6 * PT, 3), word(10), g(6 * PT, 3), word(10), g(6 * PT, 1)],
        48 * PT + 1,
        ex,
        measure=none,
    )
    hpack_case(
        w,
        "h-stretch-near-max-dimen",
        [g(MAX_DIMEN // 2, MAX_DIMEN // 3), g(1, 1)],
        MAX_DIMEN,
        ex,
        measure=none,
    )

    # --- hpack: shrinking
    squeezable = [word(40), g(6 * PT, 4 * PT, n, 2 * PT), word(40), g(6 * PT, 4 * PT, n, 3 * PT), word(40)]
    natural = 132 * PT
    for label, wanted in (
        ("one-sp", 1),
        ("half", 5 * PT // 2),
        ("half-plus-one", 5 * PT // 2 + 1),
        ("just-under-all", 5 * PT - 1),
        ("exactly-all", 5 * PT),
        ("one-sp-over", 5 * PT + 1),
        ("far-over", 40 * PT),
    ):
        hpack_case(w, f"h-shrink-finite-{label}", squeezable, natural - wanted, ex, measure=none)
        hpack_case(w, f"h-shrink-finite-{label}-additional", squeezable, -wanted, add, measure=none)
    hpack_case(w, "h-shrink-nothing-to-shrink", [word(40), g(6 * PT, 4 * PT), word(40)], 80 * PT, ex, measure=none)
    hpack_case(w, "h-shrink-no-glue-at-all", [word(40), word(40)], 70 * PT, ex, measure=none)
    for order in (fil, fill, filll):
        hpack_case(
            w,
            f"h-shrink-infinite-{order.name.lower()}-never-overfull",
            [word(40), g(6 * PT, 0, n, 2 * PT), word(40), g(0, 0, n, 1, order)],
            20 * PT,
            ex,
            measure=none,
        )
    hpack_case(
        w,
        "h-shrink-negative-finite-total",
        [word(40), g(6 * PT, 0, n, -2 * PT), word(40)],
        80 * PT,
        ex,
        measure=none,
    )
    hpack_case(
        w,
        "h-shrink-negative-fil-total",
        [word(40), g(6 * PT, 0, n, -2 * PT, fil), word(40)],
        80 * PT,
        ex,
        measure=none,
    )
    hpack_case(
        w,
        "h-shrink-to-negative-width",
        [word(4), g(6 * PT, 0, n, 2 * PT), word(4)],
        -5 * PT,
        ex,
        measure=none,
    )
    hpack_case(
        w,
        "h-shrink-with-stretch-of-other-order-present",
        [word(40), g(6 * PT, PT, fil, 2 * PT), word(40)],
        85 * PT,
        ex,
        measure=none,
    )

    # --- vpack
    lines = [word(50, 7, 2), word(40, 8, 3), word(60, 6, 1)]
    vpack_case(w, "v-empty-natural", [])
    vpack_case(w, "v-empty-to-target", [], 30 * PT, ex)
    vpack_case(w, "v-heights-and-depths-chain", lines)
    vpack_case(
        w,
        "v-width-is-widest-shifted-right",
        [word(50), replace(word(48), shift_amount=5 * PT), replace(word(60), shift_amount=-20 * PT)],
    )
    vpack_case(
        w,
        "v-glue-closes-the-running-depth",
        [word(50, 7, 2), Glue(GlueSpec(3 * PT, PT, n, PT)), word(50, 7, 4)],
    )
    vpack_case(w, "v-kern-closes-the-running-depth", [word(50, 7, 2), Kern(3 * PT), word(50, 7, 4)])
    vpack_case(w, "v-ends-in-glue-has-zero-depth", [word(50, 7, 2), g(3 * PT)])
    vpack_case(w, "v-depth-past-max-depth-becomes-height", lines[:1] + [word(40, 8, 9)], max_depth=4 * PT)
    vpack_case(w, "v-depth-exactly-max-depth", lines[:1] + [word(40, 8, 4)], max_depth=4 * PT)
    vpack_case(w, "v-max-depth-zero", lines, max_depth=0)
    vpack_case(w, "v-max-depth-with-exact-target", lines, 60 * PT, ex, max_depth=0)
    vpack_case(w, "v-plain-does-not-clamp", lines[:1] + [word(40, 8, 900)])
    vpack_case(w, "v-penalty-disc-math-occupy-nothing", [word(50, 7, 2), Penalty(5), Disc(), MathOn(PT), MathOff(PT), word(50, 7, 3)])
    vpack_case(w, "v-running-width-rule", [word(50), Rule(height=PT, depth=PT // 2), word(60)])
    vpack_case(w, "v-rule-wider-than-boxes", [word(50), Rule(width=90 * PT, height=PT, depth=0)])
    vpack_case(w, "v-only-running-width-rule", [Rule(height=PT, depth=0)])
    vpack_case(w, "v-running-height-rule-is-an-error", [word(50), Rule(width=PT, depth=0)])
    vpack_case(w, "v-running-depth-rule-is-an-error", [word(50), Rule(width=PT, height=PT)])
    vpack_case(w, "v-char-is-an-error", [word(50), CharNode("fake@10", ord("a"))])
    stack = [word(50, 7, 2), Glue(GlueSpec(5 * PT, 2 * PT, n, PT)), word(50, 7, 2), Glue(GlueSpec(5 * PT, 4 * PT, n, 2 * PT)), word(50, 7, 2)]
    natural = 36 * PT
    for label, delta in (
        ("stretch-half", 3 * PT),
        ("stretch-all", 6 * PT),
        ("stretch-over", 6 * PT + 1),
        ("shrink-half", -3 * PT // 2),
        ("shrink-all", -3 * PT),
        ("shrink-over", -3 * PT - 1),
        ("exact", 0),
    ):
        vpack_case(w, f"v-set-{label}", stack, natural + delta, ex)
        vpack_case(w, f"v-set-{label}-additional", stack, delta, add)
    vpack_case(w, "v-fil-at-the-bottom", [*stack, g(0, PT, fil)], 200 * PT, ex)
    vpack_case(w, "v-fill-beats-fil", [g(0, PT, fil), *stack, g(0, 1, fill)], 200 * PT, ex)
    vpack_case(w, "v-infinite-shrink", [*stack, g(0, 0, n, PT, fil)], 10 * PT, ex)
    vpack_case(w, "v-nested", [vpack(stack).box, g(2 * PT), hpack(para((10, 20))).box])

    # --- random lists, both directions
    rng = Rng(0x7061636B)
    for index in range(70):
        nodes = random_hlist(rng)
        base = hpack(nodes, measure=fake_measure).natural
        mode = rng.pick((ex, ex, add))
        delta = rng.between(-12, 12) * rng.pick(_ODD)
        hpack_case(w, f"h-random-{index:02d}", nodes, base + delta if mode is ex else delta, mode)
    for index in range(50):
        nodes = random_vlist(rng)
        base = vpack(nodes).natural
        mode = rng.pick((ex, ex, add))
        delta = rng.between(-12, 12) * rng.pick(_ODD)
        max_depth = rng.pick((MAX_DIMEN, MAX_DIMEN, 0, PT, 3 * PT))
        vpack_case(w, f"v-random-{index:02d}", nodes, base + delta if mode is ex else delta, mode, max_depth)

    # --- interline_glue
    skip = InterlineSkip(
        baselineskip=GlueSpec(12 * PT, PT, n, PT // 2),
        lineskip=GlueSpec(PT, PT // 4, fil, 3),
        lineskiplimit=0,
    )

    def interline_case(name: str, prev_depth: Scaled, box: Box, with_skip: InterlineSkip = skip) -> None:
        w.case(name, "interline")
        emit_skip(w, with_skip)
        w.r("prev_depth", prev_depth)
        emit_node(w, box)
        glue = interline_glue(prev_depth, box, with_skip)
        if glue is None:
            w.r("none")
        else:
            emit_node(w, glue)
        w.end()

    interline_case("i-first-box-gets-none", IGNORE_DEPTH, word(50, 7, 2))
    interline_case("i-below-the-sentinel-gets-none", IGNORE_DEPTH - 1, word(50, 7, 2))
    interline_case("i-just-above-the-sentinel", IGNORE_DEPTH + 1, word(50, 7, 2))
    interline_case("i-baselineskip-ordinary", 2 * PT, word(50, 7, 2))
    interline_case("i-baselineskip-zero-depth", 0, word(50, 7, 2))
    interline_case("i-gap-exactly-at-limit-uses-baselineskip", 5 * PT, word(50, 7, 2))
    interline_case("i-gap-one-sp-below-limit-uses-lineskip", 5 * PT + 1, word(50, 7, 2))
    interline_case("i-tall-line-uses-lineskip", 2 * PT, word(50, 30, 2))
    interline_case("i-negative-prev-depth", -3 * PT, word(50, 7, 2))
    interline_case("i-vbox-item", 2 * PT, vpack([word(50), word(50)]).box)
    positive = replace(skip, lineskiplimit=2 * PT)
    interline_case("i-positive-limit-ordinary", 2 * PT, word(50, 7, 2), positive)
    interline_case("i-positive-limit-exactly", 3 * PT, word(50, 7, 2), positive)
    interline_case("i-positive-limit-below", 3 * PT + 1, word(50, 7, 2), positive)
    negative = replace(skip, lineskiplimit=-4 * PT)
    interline_case("i-negative-limit-allows-overlap", 8 * PT, word(50, 7, 2), negative)
    interline_case("i-negative-limit-below", 10 * PT, word(50, 7, 2), negative)

    # --- append_to_vlist
    def append_case(name: str, items: Sequence[Box | Rule], start: Scaled = IGNORE_DEPTH, with_skip: InterlineSkip = skip) -> None:
        w.case(name, "append")
        emit_skip(w, with_skip)
        w.r("prev_depth", start)
        emit_nodes(w, "items", items)
        vlist: list[Node] = []
        prev_depth = start
        depths: list[Scaled] = []
        for item in items:
            prev_depth = append_to_vlist(vlist, item, prev_depth, with_skip)
            depths.append(prev_depth)
        w.r("prev_depths", len(depths), *depths)
        emit_nodes(w, "vlist", vlist)
        w.end()

    append_case("a-nothing", [])
    append_case("a-three-lines", lines)
    append_case("a-tall-middle-line", [word(50, 7, 2), word(50, 30, 9), word(50, 7, 2)])
    append_case("a-rule-suppresses-glue-both-sides", [word(50, 7, 2), Rule(height=PT, depth=0), word(50, 7, 2), word(50, 7, 2)])
    append_case("a-rule-first", [Rule(height=PT, depth=PT), word(50, 7, 2)])
    append_case("a-two-rules", [word(50, 7, 2), Rule(height=PT, depth=0), Rule(height=PT, depth=0), word(50, 7, 2)])
    append_case("a-continuing-a-list", lines, start=4 * PT)
    append_case("a-vboxes", [vpack(lines).box, vpack(lines[:2]).box])
    for index in range(12):
        items: list[Box | Rule] = []
        for _ in range(rng.between(1, 7)):
            if rng.chance(1, 6):
                items.append(Rule(height=rng.between(0, 2) * PT, depth=rng.between(0, 1) * PT))
            else:
                items.append(word(rng.between(10, 60), rng.between(0, 16), rng.between(0, 8)))
        random_skip = InterlineSkip(
            baselineskip=GlueSpec(rng.between(8, 16) * PT, rng.between(0, 2) * PT, n, rng.between(0, 1) * PT),
            lineskip=GlueSpec(rng.between(0, 2) * PT),
            lineskiplimit=rng.between(-2, 3) * PT,
        )
        append_case(f"a-random-{index:02d}", items, rng.pick((IGNORE_DEPTH, IGNORE_DEPTH, 0, 3 * PT)), random_skip)
    return w


# --------------------------------------------------------------------------
# shipout.golden
# --------------------------------------------------------------------------


def ship_case(
    w: Writer,
    name: str,
    box: Box,
    origin: Point = Point(),
    measure: CharMeasure | None = fake_measure,
) -> None:
    w.case(name, "ship")
    emit_measure(w, measure, [box])
    emit_node(w, box)
    w.r("origin", origin.x, origin.y)
    try:
        cmds = ship(box, origin, measure=measure)
    except ValueError as error:
        w.r("error", slug_of(error))
    else:
        emit_cmds(w, cmds)
    w.end()


def dump_shipout() -> Writer:
    w = Writer(
        "shipout",
        """
advtext/shipout.py: pascal_round, is_atom/atom_box, and ship() of hand-built
and random box trees. A case with no `measure` record ships with no CharMeasure.
""",
    )
    ex = PackMode.EXACTLY
    n, fil, fill = GlueOrder.NORMAL, GlueOrder.FIL, GlueOrder.FILL
    none = None

    def g(width: int = 0, stretch: int = 0, so: GlueOrder = n, shrink: int = 0, ho: GlueOrder = n) -> Glue:
        return Glue(GlueSpec(width, stretch, so, shrink, ho))

    def hp(nodes: Sequence[Node], target: Scaled = 0, mode: PackMode = PackMode.ADDITIONAL) -> Box:
        return hpack(nodes, target, mode, measure=fake_measure).box

    w.case("pascal-round", "table")
    values = [
        0.0,
        -0.0,
        0.5,
        -0.5,
        1.5,
        -1.5,
        2.5,
        -2.5,
        3.5,
        0.4,
        0.6,
        -0.4,
        -0.6,
        12.3,
        -12.3,
        1000.49,
        0.49999999999999994,
        -0.49999999999999994,
        0.5000000000000001,
        4503599627370496.0,
        4503599627370497.0,
        4503599627370495.5,
        -4503599627370495.5,
        9007199254740993.0,
        BILLION,
        -BILLION,
        BILLION - 0.5,
        2.0**52 + 0.5,
    ]
    rng = Rng(0x726F756E64)
    for _ in range(80):
        values.append((rng.below(1 << 24) - (1 << 23)) / rng.pick((2.0, 4.0, 3.0, 10.0)))
    for value in values:
        w.r("pascal_round", value, pascal_round(value))
    w.end()

    # --- is_atom / atom_box
    inner = hp(chars("x"))
    candidates: list[tuple[str, Box]] = [
        ("plain-hbox", hp(chars("ab"))),
        ("empty-hbox", Box(BoxKind.HLIST)),
        ("only-mathon", Box(BoxKind.HLIST, contents=(MathOn(),))),
        ("mathon-mathoff", Box(BoxKind.HLIST, contents=(MathOn(), MathOff()))),
        ("mathoff-mathon", Box(BoxKind.HLIST, contents=(MathOff(), MathOn()))),
        ("mathon-box-no-mathoff", Box(BoxKind.HLIST, contents=(MathOn(), inner))),
        ("vlist-bracketed", Box(BoxKind.VLIST, contents=(MathOn(), inner, MathOff()))),
        ("shifted-down", replace(inner, shift_amount=3 * PT)),
        ("shifted-up", replace(inner, shift_amount=-3 * PT)),
        ("vbox", vpack([inner, g(2 * PT), inner]).box),
        ("already-atom", atom_box(inner)),
    ]
    for name, box in candidates:
        w.case("atom-" + name, "atom")
        emit_node(w, box)
        w.r("is_atom", is_atom(box))
        wrapped = atom_box(box)
        emit_node(w, wrapped)
        w.r("is_atom", is_atom(wrapped))
        w.end()

    # --- horizontal walk
    ship_case(w, "s-natural-hbox-running-sum", hp(chars("Typo")))
    ship_case(w, "s-origin-is-the-reference-point", hp(chars("Typo")), Point(100 * PT + 7, 200 * PT - 3))
    ship_case(w, "s-negative-origin", hp(chars("ab")), Point(-5 * PT, -9 * PT))
    ship_case(
        w,
        "s-kerns-and-math-move-the-pen",
        hp([*chars("a"), Kern(3 * PT), *chars("b"), MathOn(2 * PT), *chars("c"), MathOff(PT), Kern(-PT, KernKind.EXPLICIT), *chars("d")]),
    )
    ship_case(w, "s-penalty-and-disc-emit-nothing", hp([*chars("a"), Penalty(0), Disc(pre_break=tuple(chars("-"))), *chars("b")]))
    ship_case(w, "s-char-without-measure-is-an-error", hp(chars("a")), measure=none)
    ship_case(w, "s-empty-hbox", Box(BoxKind.HLIST, width=10 * PT, height=7 * PT, depth=2 * PT), measure=none)
    ship_case(w, "s-empty-vbox", Box(BoxKind.VLIST, width=10 * PT, height=7 * PT, depth=2 * PT), measure=none)

    def spaced(text: str, glue: Glue) -> list[Node]:
        out: list[Node] = []
        for index, character in enumerate(text):
            if index:
                out.append(glue)
            out.extend(chars(character))
        return out

    accum = spaced("abcd", g(4 * PT, PT))
    natural = hpack(accum, measure=fake_measure).natural
    ship_case(w, "s-glue-accumulates-one-sp-over-three", hp(accum, natural + 1, ex))
    ship_case(w, "s-glue-accumulates-two-sp-over-three", hp(accum, natural + 2, ex))
    ship_case(w, "s-glue-accumulates-seven-pt", hp(accum, natural + 7 * PT + 1, ex))
    half = spaced("abc", g(4 * PT, 5))
    natural = hpack(half, measure=fake_measure).natural
    ship_case(w, "s-glue-pascal-tie-stretching", hp(half, natural + 5, ex))
    half_shrink = spaced("abc", g(4 * PT, 0, n, 5))
    ship_case(w, "s-glue-pascal-tie-shrinking", hp(half_shrink, natural - 5, ex))
    squeeze = spaced("ab", g(4 * PT, 0, n, 2 * PT))
    natural = hpack(squeeze, measure=fake_measure).natural
    ship_case(w, "s-shrinking-takes-the-ratio-negatively", hp(squeeze, natural - PT, ex))
    ship_case(w, "s-overfull-shrinks-fully", hp(squeeze, natural - 5 * PT, ex))
    mixed = [*chars("a"), g(4 * PT, 2 * PT), *chars("b"), g(0, PT, fil), *chars("c"), g(PT, 3 * PT, fill, PT)]
    natural = hpack(mixed, measure=fake_measure).natural
    ship_case(w, "s-glue-of-another-order-stays-natural", hp(mixed, natural + 30 * PT, ex))
    ship_case(w, "s-glue-in-an-unset-box-stays-natural", hp(mixed))
    ship_case(w, "s-underfull-finite-stretch-past-all", hp(accum, hpack(accum, measure=fake_measure).natural + 40 * PT, ex))
    # Hand-set boxes: a glue_set no packing produced, including the vet_glue clamp.
    ship_case(
        w,
        "s-hand-set-ratio-half-ties-every-step",
        Box(BoxKind.HLIST, contents=tuple(spaced("abcde", g(4 * PT, 1))), width=60 * PT, height=7 * PT, glue_set=0.5, glue_sign=GlueSign.STRETCHING),
    )
    ship_case(
        w,
        "s-hand-set-ratio-half-ties-shrinking",
        Box(BoxKind.HLIST, contents=tuple(spaced("abcde", g(4 * PT, 0, n, 1))), width=60 * PT, height=7 * PT, glue_set=0.5, glue_sign=GlueSign.SHRINKING),
    )
    ship_case(
        w,
        "s-vet-glue-clamps-at-a-billion",
        Box(BoxKind.HLIST, contents=tuple(spaced("abc", g(4 * PT, PT))), width=60 * PT, height=7 * PT, glue_set=1.0e9, glue_sign=GlueSign.STRETCHING),
    )
    ship_case(
        w,
        "s-vet-glue-clamps-at-minus-a-billion",
        Box(BoxKind.HLIST, contents=tuple(spaced("abc", g(4 * PT, 0, n, PT))), width=60 * PT, height=7 * PT, glue_set=1.0e9, glue_sign=GlueSign.SHRINKING),
    )
    ship_case(
        w,
        "s-negative-ratio",
        Box(BoxKind.HLIST, contents=tuple(spaced("abc", g(4 * PT, 3))), width=60 * PT, height=7 * PT, glue_set=-0.5, glue_sign=GlueSign.STRETCHING),
    )
    ship_case(
        w,
        "s-sign-set-but-order-fil-and-glue-finite",
        Box(BoxKind.HLIST, contents=tuple(spaced("abc", g(4 * PT, PT))), width=60 * PT, height=7 * PT, glue_set=2.0, glue_sign=GlueSign.STRETCHING, glue_order=fil),
    )

    # --- nesting and shifts
    sub = hp(chars("xy"))
    ship_case(w, "s-nested-boxes-compose-origins", hp([*chars("a"), hp([*chars("b"), hp(chars("c"))]), *chars("d")]), Point(10 * PT, 20 * PT))
    ship_case(w, "s-pen-resumes-at-declared-width", hp([replace(sub, width=sub.width + 9 * PT), *chars("z")]))
    ship_case(w, "s-pen-resumes-at-narrower-declared-width", hp([replace(sub, width=PT), *chars("z")]))
    ship_case(w, "s-shift-moves-down-in-an-hlist", hp([*chars("a"), replace(sub, shift_amount=3 * PT), replace(sub, shift_amount=-2 * PT)]))
    two = [hp(chars("Typo")), replace(hp(chars("gy")), shift_amount=12 * PT), replace(hp(chars("ab")), shift_amount=-3 * PT)]
    ship_case(w, "s-shift-moves-right-in-a-vlist", vpack(two).box, Point(0, 50 * PT))
    ship_case(w, "s-vlist-steps-height-then-depth", vpack([hp(chars("Tg")), hp(chars("x")), hp(chars("jQ"))]).box)
    ship_case(w, "s-vertical-kern-and-glue-move-down", vpack([hp(chars("a")), Kern(3 * PT), hp(chars("b")), g(5 * PT), hp(chars("c"))]).box)
    stretchy = [hp(chars("a")), g(5 * PT, 2 * PT, n, PT), hp(chars("b")), g(5 * PT, 3, n, PT), hp(chars("c")), g(0, 1), hp(chars("d"))]
    natural = vpack(stretchy).natural
    ship_case(w, "s-vertical-glue-stretches-with-accumulation", vpack(stretchy, natural + 2 * PT + 1, ex).box)
    ship_case(w, "s-vertical-glue-shrinks", vpack(stretchy, natural - PT - 1, ex).box)
    ship_case(w, "s-vertical-glue-takes-no-index", vpack([hp(spaced("ab", g(4 * PT))), g(5 * PT), hp(spaced("cd", g(4 * PT)))]).box)
    ship_case(w, "s-vlist-ignores-penalty-disc-math", vpack([hp(chars("a")), Penalty(0), Disc(), MathOn(PT), MathOff(PT), hp(chars("b"))]).box)
    ship_case(w, "s-char-in-a-vlist-is-an-error", Box(BoxKind.VLIST, contents=(CharNode("fake@10", 97),), width=PT, height=PT))

    # --- rules
    ship_case(w, "s-rule-in-hlist", hp([*chars("a"), Rule(3 * PT, 5 * PT, 2 * PT), *chars("b")]))
    ship_case(w, "s-running-rule-in-hlist-takes-box-height-depth", hp([*chars("Tg"), Rule(width=PT), *chars("b")]))
    ship_case(w, "s-running-height-only", hp([*chars("Tg"), Rule(width=PT, depth=PT)]))
    ship_case(w, "s-rule-with-no-thickness-draws-nothing", hp([*chars("a"), Rule(3 * PT, 0, 0), *chars("b")]))
    ship_case(w, "s-rule-negative-thickness-draws-nothing", hp([*chars("a"), Rule(3 * PT, PT, -2 * PT), *chars("b")]))
    ship_case(w, "s-rule-zero-width-draws-nothing", hp([*chars("a"), Rule(0, 5 * PT, 2 * PT), *chars("b")]))
    ship_case(w, "s-rule-negative-width-moves-pen-back", hp([*chars("a"), Rule(-2 * PT, 5 * PT, 2 * PT), *chars("b")]))
    ship_case(
        w,
        "s-running-width-rule-in-hlist-is-an-error",
        Box(BoxKind.HLIST, contents=(Rule(height=PT, depth=0),), width=10 * PT, height=PT),
        measure=none,
    )
    ship_case(w, "s-running-rule-in-vlist-takes-box-width", vpack([hp(chars("Typography")), Rule(height=PT, depth=PT // 2), hp(chars("ab"))]).box, Point(4 * PT, 0))
    ship_case(w, "s-fixed-rule-in-vlist", vpack([hp(chars("ab")), Rule(30 * PT, PT, 0), hp(chars("cd"))]).box)
    ship_case(w, "s-rule-no-thickness-in-vlist", vpack([hp(chars("ab")), Rule(30 * PT, 0, 0), hp(chars("cd"))]).box)
    ship_case(
        w,
        "s-running-height-rule-in-vlist-is-an-error",
        Box(BoxKind.VLIST, contents=(Rule(width=PT, depth=0),), width=10 * PT, height=PT),
        measure=none,
    )

    # --- reading order and lines
    line_a = hp(spaced("abc", g(4 * PT, PT)))
    line_b = hp([*chars("d"), hp(spaced("ef", g(2 * PT))), g(4 * PT), *chars("g")])
    page = vpack([line_a, g(5 * PT), line_b, g(5 * PT), Box(BoxKind.HLIST, width=20 * PT, height=7 * PT), g(5 * PT), hp(chars("h"))]).box
    ship_case(w, "s-indices-dense-across-a-nested-tree", page, Point(0, page.height))
    ship_case(w, "s-bare-hbox-is-one-line", line_a)
    ship_case(w, "s-nested-hbox-is-not-a-line", line_b)
    ship_case(w, "s-vbox-in-hbox-in-vbox-lines", vpack([hp([*chars("a"), vpack([hp(chars("b")), hp(chars("c"))]).box, *chars("d")])]).box)
    ship_case(w, "s-vbox-root-inner-vbox-lines", vpack([vpack([hp(chars("a")), hp(chars("b"))]).box, hp(chars("c"))]).box)
    ship_case(w, "s-line-reports-box-dimensions-not-ink", vpack([replace(hp(chars("xx")), height=10 * PT, depth=4 * PT), hp(chars("xx"))]).box)
    skip = HAND_SKIP
    vlist: list[Node] = []
    prev = IGNORE_DEPTH
    for text in ("Typo", "gyp", "xx"):
        prev = append_to_vlist(vlist, hp(spaced(text, g(3 * PT))), prev, skip)
    ship_case(w, "s-lines-one-baselineskip-apart", vpack(vlist).box)

    # --- atoms
    frac = replace(vpack([hp(chars("a")), Rule(height=PT // 2, depth=0), hp(chars("b"))]).box, shift_amount=2 * PT)
    formula = atom_box(hp([*chars("x"), g(2 * PT, PT), *chars("y"), frac, Rule(PT, 3 * PT, PT)]))
    with_atom = [*chars("A"), g(4 * PT, 2 * PT), formula, g(4 * PT, 2 * PT), *chars("B")]
    natural = hpack(with_atom, measure=fake_measure).natural
    ship_case(w, "s-atom-takes-one-index", hp(with_atom))
    ship_case(w, "s-atom-in-a-stretched-line", hp(with_atom, natural + 3 * PT + 1, ex), Point(7, 11))
    ship_case(w, "s-atom-in-a-paragraph-line", vpack([hp(with_atom), g(4 * PT), hp([formula, formula])]).box)
    ship_case(w, "s-atom-shifted", hp([*chars("A"), replace(formula, shift_amount=3 * PT), *chars("B")]))
    ship_case(w, "s-atom-inside-an-atom-is-ordinary", hp([*chars("A"), atom_box(hp([*chars("u"), formula, *chars("v")])), *chars("B")]))
    ship_case(w, "s-atom-as-root-is-a-line-not-an-atom", formula)
    ship_case(w, "s-atom-directly-in-a-vlist-is-a-line", vpack([formula, hp(chars("z"))]).box)
    ship_case(w, "s-atom-in-nested-hbox", hp([*chars("A"), hp([formula, *chars("m")]), *chars("B")]))
    ship_case(w, "s-atom-of-shifted-box", hp([*chars("A"), atom_box(replace(hp(chars("q")), shift_amount=-4 * PT)), *chars("B")]))
    ship_case(w, "s-empty-atom", hp([*chars("A"), Box(BoxKind.HLIST, contents=(MathOn(), MathOff())), *chars("B")]))
    ship_case(w, "s-atom-with-mathsurround", hp([*chars("A"), Box(BoxKind.HLIST, contents=(MathOn(2 * PT), *chars("x"), MathOff(2 * PT)), width=20 * PT), *chars("B")]))

    # --- random trees
    rng = Rng(0x73686970)
    for index in range(60):
        if rng.chance(1, 2):
            nodes = random_vlist(rng)
            base = vpack(nodes).natural
            box = vpack(nodes, base + rng.between(-6, 10) * rng.pick(_ODD), ex, max_depth=rng.pick((MAX_DIMEN, PT))).box
        else:
            hnodes = random_hlist(rng)
            if rng.chance(1, 3):
                position = rng.below(len(hnodes) + 1)
                hnodes.insert(position, atom_box(hp(random_hlist(rng, 1))))
            base = hpack(hnodes, measure=fake_measure).natural
            box = hp(hnodes, base + rng.between(-6, 10) * rng.pick(_ODD), ex)
        origin = Point(rng.between(-20, 300) * rng.pick(_ODD), rng.between(-20, 300) * rng.pick(_ODD))
        ship_case(w, f"s-random-{index:02d}", box, origin)
    return w


# --------------------------------------------------------------------------
# Line breaking: shared run emitter
# --------------------------------------------------------------------------


def break_with_positions(
    nodes: Sequence[Node],
    shape: ParShape,
    params: LineBreakParams,
    measure: CharMeasure | None,
) -> tuple[Paragraph, list[tuple[int, str]], int]:
    """
    The public result, the breaks that produced it, and the prepared length.

    The breaks come from the reference's private functions, driven exactly as
    `break_paragraph` drives them; the assertion is what makes that safe.
    """
    public = break_paragraph(nodes, shape, params, measure=measure)
    body = L._prepare(nodes, params)
    if params.first_fit:
        chosen = L._first_fit(body, shape, params, measure)
        demerits, which = 0, BreakPass.FIRST_FIT
    else:
        attempts: list[tuple[int, Scaled, bool, BreakPass]] = []
        if params.pretolerance >= 0:
            attempts.append((min(params.pretolerance, INF_BAD), 0, False, BreakPass.FIRST))
        attempts.append((min(params.tolerance, INF_BAD), 0, params.emergency_stretch <= 0, BreakPass.SECOND))
        if params.emergency_stretch > 0:
            attempts.append((min(params.tolerance, INF_BAD), params.emergency_stretch, True, BreakPass.EMERGENCY))
        for threshold, extra, final, which in attempts:
            winner = L._Breaker(
                body=body,
                shape=shape,
                params=params,
                threshold=threshold,
                extra_stretch=extra,
                final_pass=final,
                measure=measure,
            ).run()
            if winner is not None:
                chosen = L._chain(winner)
                demerits = winner.total_demerits
                break
        else:
            raise AssertionError("no pass produced a breaking")
    rebuilt = L._assemble(body, chosen, shape, params, measure, demerits, which)
    assert rebuilt == public, "the private replay disagrees with break_paragraph"
    return public, [(p.position, p.kind.name.lower()) for p in chosen], len(body)


def _ship_lines(
    lines: Sequence[Box], skip: InterlineSkip, measure: CharMeasure | None
) -> tuple[Box, Point, list[DrawCmd]]:
    """
    The vertical assembly used for a run's ship-out expectation.

    Each line box is appended with `append_to_vlist` starting from
    `IGNORE_DEPTH`, the list is `vpack`ed at its natural height, and the result
    is shipped with its top-left corner at the origin: `Point(0, vbox.height)`.
    """
    vlist: list[Node] = []
    prev_depth = IGNORE_DEPTH
    for line in lines:
        prev_depth = append_to_vlist(vlist, line, prev_depth, skip)
    vbox = vpack(vlist).box
    origin = Point(0, vbox.height)
    return vbox, origin, ship(vbox, origin, measure=measure)


def emit_run(
    w: Writer,
    run_name: str,
    list_name: str,
    nodes: Sequence[Node],
    shape: ParShape,
    params: LineBreakParams,
    measure: CharMeasure | None,
    skip: InterlineSkip | None,
) -> Paragraph:
    w.r("break", q(run_name), list_name)
    w.r("shape", len(shape.lines), *[v for line in shape.lines for v in (line.width, line.indent)])
    w.r(
        "params",
        params.pretolerance,
        params.tolerance,
        params.line_penalty,
        params.adj_demerits,
        params.double_hyphen_demerits,
        params.final_hyphen_demerits,
        params.hyphen_penalty,
        params.ex_hyphen_penalty,
        params.emergency_stretch,
        params.first_fit,
    )
    emit_spec(w, "left_skip", params.left_skip)
    emit_spec(w, "right_skip", params.right_skip)
    emit_spec(w, "par_fill_skip", params.par_fill_skip)
    paragraph, breaks, prepared = break_with_positions(nodes, shape, params, measure)
    w.r("paragraph", len(paragraph.lines), paragraph.total_demerits, paragraph.pass_used, prepared)
    w.r("breaks", len(breaks), *[v for position, kind in breaks for v in (position, kind)])
    for line in paragraph.lines:
        emit_packed(w, line)
    if skip is not None:
        vbox, origin, cmds = _ship_lines(paragraph.boxes, skip, measure)
        emit_skip(w, skip)
        w.r("vbox", vbox.width, vbox.height, vbox.depth)
        w.r("origin", origin.x, origin.y)
        emit_cmds(w, cmds)
    w.r("endbreak")
    return paragraph


@dataclass(slots=True)
class _TieProbe(L._Breaker):
    """
    The reference's breaker, watched for one situation and otherwise untouched.

    Before each breakpoint is scored it looks for two active nodes with the
    same line number and the same total demerits, one that was already active
    before the previous breakpoint (a survivor) and one the previous breakpoint
    created, both of which make a feasible line of the same fitness class to
    here. Section 855's `<=` then chooses between them.
    """

    earlier: list[object] = field(default_factory=list)
    survivor_created_tie: bool = False

    def _try_break(self, penalty: int, hyphenated: bool, position: int, kind: object) -> None:
        entering = list(self.active)
        old = [a for a in entering if any(a is b for b in self.earlier)]
        new = [a for a in entering if not any(a is b for b in self.earlier)]
        for survivor in old:
            for created in new:
                if (survivor.line_number, survivor.total_demerits) != (created.line_number, created.total_demerits):
                    continue
                verdicts = []
                for r in (survivor, created):
                    candidate = L._Passive(position, kind, r.passive)  # type: ignore[arg-type]
                    verdicts.append(L._classify(self._evaluate(r.passive, candidate, r.line_number)))
                if verdicts[0] == verdicts[1] and verdicts[0][0] <= self.threshold:
                    self.survivor_created_tie = True
        self.earlier = entering
        L._Breaker._try_break(self, penalty, hyphenated, position, kind)  # type: ignore[arg-type]


def tie_properties(nodes: Sequence[Node], shape: ParShape, params: LineBreakParams) -> tuple[bool, bool]:
    """
    Whether the first pass over this paragraph (a) ends with the minimum total
    demerits held by chains of different line counts and (b) met a
    survivor-against-created tie on the way. Box-only material; no measure.
    """
    assert params.pretolerance >= 0 and not params.first_fit
    probe = _TieProbe(
        body=L._prepare(nodes, params),
        shape=shape,
        params=params,
        threshold=min(params.pretolerance, INF_BAD),
        extra_stretch=0,
        final_pass=False,
        measure=None,
    )
    winner = probe.run()
    assert winner is not None, "the probed pass failed, so a later pass chose the breaks"
    best = min(a.total_demerits for a in probe.active)
    counts = {a.line_number for a in probe.active if a.total_demerits == best}
    return len(counts) > 1, probe.survivor_created_tie


Run = tuple[str, ParShape, LineBreakParams]


def linebreak_case(
    w: Writer,
    name: str,
    nodes: Sequence[Node],
    runs: Sequence[Run],
    measure: CharMeasure | None = fake_measure,
    skip: InterlineSkip | None = HAND_SKIP,
) -> list[Paragraph]:
    w.case(name, "linebreak")
    emit_measure(w, measure, nodes)
    emit_nodes(w, "material", nodes)
    results = [
        emit_run(w, run_name, "material", nodes, shape, params, measure, skip)
        for run_name, shape, params in runs
    ]
    w.end()
    return results


# --------------------------------------------------------------------------
# linebreak.golden: hand-built paragraphs
# --------------------------------------------------------------------------


def random_paragraph(rng: Rng, words: int) -> list[Node]:
    """
    A paragraph of box-words and fake-font words with every breakable construct.
    """
    space = GlueSpec(
        width=rng.between(3, 7) * PT + rng.below(PT),
        stretch=rng.between(1, 4) * PT + rng.below(PT),
        shrink=rng.between(0, 2) * PT + rng.below(PT),
    )
    out: list[Node] = []
    for index in range(words):
        if index:
            roll = rng.below(40)
            if roll == 0:
                out.append(Penalty(rng.pick((EJECT_PENALTY, -20000))))
            elif roll == 1:
                out.append(Penalty(rng.pick((INF_PENALTY, 20000))))
            elif roll < 5:
                out.append(Penalty(rng.pick((-150, -50, 0, 50, 150, 900))))
            elif roll < 7:
                out.append(Kern(rng.between(1, 3) * PT, KernKind.EXPLICIT))
            elif roll < 9:
                out.append(Kern(rng.between(-1, 2) * PT + 77, KernKind.NORMAL))
            if roll == 9:
                # A sentence space: wider and stretchier.
                out.append(Glue(replace(space, width=space.width * 4 // 3, stretch=space.stretch * 3)))
            else:
                out.append(Glue(space))
            if roll == 10:
                out.append(Glue(space))
        roll = rng.below(30)
        if roll < 14:
            out.append(Box(BoxKind.HLIST, width=rng.between(4, 34) * PT + rng.below(PT), height=7 * PT, depth=2 * PT))
        elif roll < 24:
            text = "".join(rng.pick("abcdefghijklmnopqrstuvwxyzTQW") for _ in range(rng.between(1, 9)))
            out.extend(chars(text))
        elif roll < 27:
            # A compound word with a discretionary in it.
            head = "".join(rng.pick("abcdefgh") for _ in range(rng.between(2, 5)))
            tail = "".join(rng.pick("mnopqrst") for _ in range(rng.between(2, 5)))
            style = rng.below(3)
            out.extend(chars(head))
            if style == 0:
                out.append(Disc(pre_break=tuple(chars("-"))))
            elif style == 1:
                out.append(Disc())
            else:
                out.append(Disc(pre_break=tuple(chars("-")), post_break=tuple(chars("k")), replace_count=1))
                out.extend(chars("c"))
            out.extend(chars(tail))
        else:
            # An inline formula written into the list, with glue inside it.
            out.append(MathOn(rng.between(0, 1) * PT))
            out.extend(chars("x"))
            out.append(Glue(GlueSpec(2 * PT, PT, GlueOrder.NORMAL, PT // 2)))
            out.extend(chars("y"))
            out.append(MathOff(rng.between(0, 1) * PT))
    if rng.chance(1, 8):
        out.append(Glue(space))
    return out


THRESHOLD_GLUE = GlueSpec(width=6 * PT, stretch=297, shrink=297)


def fitness_threshold_cases(w: Writer) -> None:
    """
    Lines whose badness is exactly 12, 13, 99 or 100, placed so that the
    fitness class each one gets (§817's thresholds) changes the total.

    Every line under test is two words and one glue of 297 sp of stretch and
    shrink, in a 100 pt measure. With 297 sp available the badness formula's
    ratio is the excess itself, so an excess of 148 sp is badness 12, 149 is
    13, 296 is 99 and 297 is 100. Forced breaks pin the lines, so the breaks
    are the same on both sides of a threshold and only the class differs; a
    neighbouring line of a known class and `adj_demerits` of 7777 make the
    class show in `total_demerits`.

    The assertions are the construction's own claims. They fail the dump, and
    not a test, if the reference or these lists ever stop landing on the
    thresholds.
    """
    uniform = ParShape.uniform

    def line(delta: Scaled) -> list[Node]:
        # Natural width is 100 pt + delta: negative stretches, positive shrinks.
        return [word(40), Glue(THRESHOLD_GLUE), Box(BoxKind.HLIST, width=54 * PT + delta, height=7 * PT, depth=2 * PT)]

    def after_forced_break() -> list[Node]:
        return [Penalty(EJECT_PENALTY), Glue(THRESHOLD_GLUE)]

    def params(pretolerance: int = 10000, tolerance: int = 10000) -> LineBreakParams:
        return LineBreakParams(pretolerance=pretolerance, tolerance=tolerance, adj_demerits=7777)

    def case(
        name: str,
        nodes: list[Node],
        runs: Sequence[Run],
        under_test: int,
        badness_value: int,
        fitness: PackFitness,
    ) -> list[Paragraph]:
        results = linebreak_case(w, "fitness-threshold-" + name, nodes, runs)
        for result in results:
            assert len(result.lines) == len(results[0].lines)
            packed = result.lines[under_test]
            assert (packed.badness, packed.fitness) == (badness_value, fitness), (name, packed.badness, packed.fitness)
        return results

    def stretched_then_last(excess: int) -> list[Node]:
        return [*line(-excess), *after_forced_break(), word(30)]

    def stretched_then_shrunk(excess: int) -> list[Node]:
        return [*line(-excess), *after_forced_break(), *line(200), *after_forced_break(), word(30)]

    def stretched_then_shrinking(excess: int) -> list[Node]:
        return [*line(-200), *after_forced_break(), *line(excess), *after_forced_break(), word(30)]

    # 99 | 100: loose against very loose. The line after it is the decent last
    # line, two classes from very loose and one from loose. The further runs
    # are the feasibility test at the same values: a line of badness b passes a
    # threshold of b and fails one of b - 1, and on the final pass a forced
    # break from the only active node is kept whatever its badness.
    loose = case(
        "stretch-99",
        stretched_then_last(296),
        [
            ("permissive", uniform(LINE), params()),
            ("pretolerance-99", uniform(LINE), params(pretolerance=99)),
            ("pretolerance-98", uniform(LINE), params(pretolerance=98)),
        ],
        0,
        99,
        PackFitness.LOOSE,
    )
    very_loose = case(
        "stretch-100",
        stretched_then_last(297),
        [
            ("permissive", uniform(LINE), params()),
            ("pretolerance-100", uniform(LINE), params(pretolerance=100)),
            ("pretolerance-99", uniform(LINE), params(pretolerance=99)),
            ("final-pass-tolerance-99", uniform(LINE), params(pretolerance=-1, tolerance=99)),
            ("final-pass-tolerance-100", uniform(LINE), params(pretolerance=-1, tolerance=100)),
        ],
        0,
        100,
        PackFitness.LOOSE,
    )
    assert [p.pass_used for p in loose] == [BreakPass.FIRST, BreakPass.FIRST, BreakPass.SECOND]
    assert [p.pass_used for p in very_loose] == [BreakPass.FIRST, BreakPass.FIRST, BreakPass.SECOND, BreakPass.SECOND, BreakPass.SECOND]
    # Badness alone moves the total by (10 + 100)^2 - (10 + 99)^2 = 219. The
    # rest is adj_demerits, which a very loose first line is charged twice:
    # against the paragraph's start, which counts as decent, and against the
    # decent line after it. A loose one is charged neither.
    assert very_loose[0].total_demerits - loose[0].total_demerits == 219 + 2 * 7777

    # 12 | 13 stretching: decent against loose, next to a tight line.
    decent = case("stretch-12", stretched_then_shrunk(148), [("permissive", uniform(LINE), params())], 0, 12, PackFitness.LOOSE)
    looser = case("stretch-13", stretched_then_shrunk(149), [("permissive", uniform(LINE), params())], 0, 13, PackFitness.LOOSE)
    assert looser[0].total_demerits - decent[0].total_demerits == (10 + 13) ** 2 - (10 + 12) ** 2 + 7777

    # 12 | 13 shrinking: decent against tight, next to a loose line.
    decent = case("shrink-12", stretched_then_shrinking(148), [("permissive", uniform(LINE), params())], 1, 12, PackFitness.TIGHT)
    tighter = case("shrink-13", stretched_then_shrinking(149), [("permissive", uniform(LINE), params())], 1, 13, PackFitness.TIGHT)
    assert tighter[0].total_demerits - decent[0].total_demerits == (10 + 13) ** 2 - (10 + 12) ** 2 + 7777


def dump_linebreak() -> Writer:
    w = Writer(
        "linebreak",
        """
advtext/linebreak.py on hand-built paragraphs: total-fit and first-fit.
Each case has one node list and several `break` runs over it.
""",
    )
    uniform = ParShape.uniform
    default = LineBreakParams()
    first_fit = LineBreakParams(first_fit=True)
    ragged_ff = replace(RAGGED, first_fit=True)
    elastic = GlueSpec(width=6 * PT, shrink=1, shrink_order=GlueOrder.FIL)
    fil_shrink = GlueSpec(shrink=PT, shrink_order=GlueOrder.FIL)

    showcase = para(SHOWCASE)
    linebreak_case(
        w,
        "showcase",
        showcase,
        [
            ("default", uniform(LINE), default),
            ("permissive", uniform(LINE), PERMISSIVE),
            ("first-fit", uniform(LINE), first_fit),
            ("one-line", uniform(200 * PT), default),
            ("pretolerance-negative", uniform(LINE), LineBreakParams(pretolerance=-1)),
            ("narrower-first-line", ParShape((LineSpec(60 * PT), LineSpec(LINE))), default),
            ("hanging", ParShape.hanging(LINE, 12 * PT), default),
            ("hanging-after-2", ParShape.hanging(LINE, 12 * PT, after=2), default),
            ("hanging-negative-indent", ParShape.hanging(LINE, -12 * PT), default),
            ("last-spec-repeats", ParShape((LineSpec(40 * PT), LineSpec(70 * PT))), default),
            ("indented", uniform(80 * PT, 20 * PT), default),
            ("narrow-45", uniform(45 * PT), default),
            ("narrow-45-first-fit", uniform(45 * PT), first_fit),
            ("narrow-20-every-word-overfull", uniform(20 * PT), default),
            ("narrow-20-first-fit", uniform(20 * PT), first_fit),
            ("zero-width", uniform(0), default),
            ("zero-width-first-fit", uniform(0), first_fit),
            ("emergency", uniform(45 * PT), LineBreakParams(emergency_stretch=20 * PT)),
            ("line-penalty-0", uniform(LINE), LineBreakParams(line_penalty=0, pretolerance=10000)),
            ("line-penalty-200", uniform(LINE), LineBreakParams(line_penalty=200, pretolerance=10000)),
            ("adj-demerits-0", uniform(70 * PT), LineBreakParams(adj_demerits=0, pretolerance=10000, tolerance=10000)),
            ("adj-demerits-default", uniform(70 * PT), PERMISSIVE),
            ("tolerance-above-inf-bad", uniform(LINE), LineBreakParams(pretolerance=20000, tolerance=20000)),
            ("left-skip", uniform(LINE), LineBreakParams(left_skip=GlueSpec(5 * PT))),
            ("left-skip-first-fit", uniform(LINE), LineBreakParams(left_skip=GlueSpec(5 * PT), first_fit=True)),
            ("right-skip-finite-stretch", uniform(LINE), LineBreakParams(right_skip=GlueSpec(0, 20 * PT))),
            ("both-skips-with-width", uniform(LINE), LineBreakParams(left_skip=GlueSpec(3 * PT, PT), right_skip=GlueSpec(2 * PT, 0, GlueOrder.NORMAL, PT))),
            ("no-parfillskip", uniform(LINE), LineBreakParams(par_fill_skip=GlueSpec(), pretolerance=10000, tolerance=10000)),
            ("parfillskip-finite", uniform(LINE), LineBreakParams(par_fill_skip=GlueSpec(0, 30 * PT))),
            # The three skips are NOT made shrink-finite (only body glue is), so
            # a skip with infinite shrink means no line is ever overfull.
            ("right-skip-fil-shrink", uniform(LINE), LineBreakParams(right_skip=fil_shrink)),
            ("right-skip-fil-shrink-narrow", uniform(20 * PT), LineBreakParams(right_skip=fil_shrink)),
            ("right-skip-fil-shrink-narrow-first-fit", uniform(20 * PT), LineBreakParams(right_skip=fil_shrink, first_fit=True)),
            ("left-skip-fil-shrink-narrow", uniform(20 * PT), LineBreakParams(left_skip=fil_shrink)),
            ("par-fill-skip-fil-shrink-narrow", uniform(20 * PT), LineBreakParams(par_fill_skip=fil_shrink)),
            # left_skip is inserted only when it differs from GlueSpec() in any
            # field; one scaled point of stretch is enough to put a glue node,
            # and so a reading-order index, at the head of every line.
            ("left-skip-stretch-1sp", uniform(LINE), LineBreakParams(left_skip=GlueSpec(stretch=1))),
            ("left-skip-stretch-1sp-first-fit", uniform(LINE), LineBreakParams(left_skip=GlueSpec(stretch=1), first_fit=True)),
            ("left-skip-fil-order-zero-amount", uniform(LINE), LineBreakParams(left_skip=GlueSpec(stretch_order=GlueOrder.FIL))),
        ],
    )
    stiff = para(SHOWCASE, STIFF)
    linebreak_case(
        w,
        "showcase-stiff-ragged",
        stiff,
        [
            ("uniform", uniform(LINE), RAGGED),
            ("uniform-first-fit", uniform(LINE), ragged_ff),
            ("narrow", uniform(45 * PT), RAGGED),
            ("narrow-first-fit", uniform(45 * PT), ragged_ff),
            ("hanging", ParShape.hanging(LINE, 12 * PT), RAGGED),
            ("hanging-first-fit", ParShape.hanging(LINE, 12 * PT), ragged_ff),
            ("indented", uniform(80 * PT, 20 * PT), RAGGED),
            ("indented-first-fit", uniform(80 * PT, 20 * PT), ragged_ff),
            ("centred", uniform(LINE), replace(RAGGED, left_skip=FIL)),
            ("centred-first-fit", uniform(LINE), replace(ragged_ff, left_skip=FIL)),
            ("ragged-left-no-parfill", uniform(LINE), replace(RAGGED, left_skip=FIL, right_skip=GlueSpec(), par_fill_skip=GlueSpec())),
            ("ragged-left-no-parfill-first-fit", uniform(LINE), replace(ragged_ff, left_skip=FIL, right_skip=GlueSpec(), par_fill_skip=GlueSpec())),
        ],
    )
    shape3 = ParShape((LineSpec(40 * PT, 5 * PT), LineSpec(70 * PT), LineSpec(55 * PT, 9 * PT)))
    linebreak_case(
        w,
        "showcase-twice-parshape",
        para(SHOWCASE * 2, STIFF),
        [("total-fit", shape3, RAGGED), ("first-fit", shape3, ragged_ff), ("justified", shape3, PERMISSIVE)],
    )
    linebreak_case(
        w,
        "trailing-space-is-replaced",
        [*para(SHOWCASE), Glue(SPACE)],
        [("default", uniform(LINE), default), ("first-fit", uniform(LINE), first_fit)],
    )
    linebreak_case(
        w,
        "two-trailing-spaces",
        [*para((10, 10)), Glue(SPACE), Glue(SPACE)],
        [("default", uniform(LINE), default), ("narrow", uniform(12 * PT), default), ("first-fit", uniform(12 * PT), first_fit)],
    )
    linebreak_case(
        w,
        "leading-glue-is-not-a-breakpoint",
        [Glue(SPACE), *para((40, 40, 40))],
        [("default", uniform(LINE), default), ("first-fit", uniform(50 * PT), first_fit)],
    )
    linebreak_case(w, "empty", [], [("default", uniform(LINE), default), ("first-fit", uniform(LINE), first_fit), ("centred", uniform(LINE), replace(RAGGED, left_skip=FIL))])
    linebreak_case(w, "only-glue", [Glue(SPACE)], [("default", uniform(LINE), default), ("first-fit", uniform(LINE), first_fit)])
    linebreak_case(w, "only-penalty", [Penalty(EJECT_PENALTY)], [("default", uniform(LINE), default), ("first-fit", uniform(LINE), first_fit)])
    linebreak_case(
        w,
        "one-wide-word",
        [word(300)],
        [
            ("default", uniform(LINE), default),
            ("emergency", uniform(LINE), LineBreakParams(emergency_stretch=50 * PT)),
            ("first-fit", uniform(LINE), first_fit),
        ],
    )
    wide_runs = [
        ("default", uniform(LINE), default),
        ("emergency", uniform(LINE), LineBreakParams(emergency_stretch=50 * PT)),
        ("prose", uniform(LINE), ui.PROSE_BREAKING),
        ("first-fit", uniform(LINE), first_fit),
        ("ragged-first-fit", uniform(LINE), ragged_ff),
    ]
    linebreak_case(w, "wide-word-first", para((300, 20, 30, 40, 20, 30)), wide_runs)
    linebreak_case(w, "wide-word-last", para((20, 30, 40, 20, 30, 300)), wide_runs)
    linebreak_case(w, "wide-word-in-the-middle", para((20, 30, 40, 20, 30, 300, 20, 30, 40, 20, 30, 25, 35)), wide_runs)

    adjacency: list[Node] = [
        word(40),
        Glue(GlueSpec(6 * PT, 5 * PT, GlueOrder.NORMAL, 2 * PT)),
        word(39),
        Penalty(EJECT_PENALTY),
        Glue(GlueSpec(6 * PT, 5 * PT, GlueOrder.NORMAL, 2 * PT)),
        word(48),
        Glue(GlueSpec(7 * PT, 5 * PT, GlueOrder.NORMAL, 5 * PT)),
        word(48),
    ]
    linebreak_case(
        w,
        "adjacent-fitness-classes",
        adjacency,
        [
            ("adj-0", uniform(LINE), LineBreakParams(pretolerance=10000, tolerance=10000, adj_demerits=0)),
            ("adj-7777", uniform(LINE), LineBreakParams(pretolerance=10000, tolerance=10000, adj_demerits=7777)),
            ("adj-negative", uniform(LINE), LineBreakParams(pretolerance=10000, tolerance=10000, adj_demerits=-7777)),
            ("adj-past-awful-bad", uniform(LINE), LineBreakParams(pretolerance=10000, tolerance=10000, adj_demerits=1 << 30)),
        ],
    )

    def with_penalty(value: int, at_word: int, space: GlueSpec = SPACE) -> list[Node]:
        nodes = para(SHOWCASE, space)
        nodes.insert(2 * at_word - 1, Penalty(value))
        return nodes

    for value in (-99, -50, -1, 0, 1, 50, 5000, 9999, INF_PENALTY, 15000, EJECT_PENALTY, -9999, -15000):
        linebreak_case(
            w,
            f"penalty-{value}-before-word-5",
            with_penalty(value, 5),
            [("permissive", uniform(LINE), PERMISSIVE), ("first-fit", uniform(LINE), first_fit)],
        )
    linebreak_case(
        w,
        "forced-break-after-two-words",
        [*para(SHOWCASE)[:3], Penalty(EJECT_PENALTY), *para(SHOWCASE)[3:]],
        [("default", uniform(LINE), default), ("first-fit", uniform(LINE), first_fit), ("narrow-first-fit", uniform(25 * PT), first_fit)],
    )
    linebreak_case(
        w,
        "inf-penalty-blocks-word-4",
        with_penalty(INF_PENALTY, 4),
        [("permissive", uniform(LINE), PERMISSIVE), ("ragged", uniform(LINE), RAGGED), ("ragged-first-fit", uniform(LINE), ragged_ff), ("narrow-first-fit", uniform(40 * PT), first_fit)],
    )
    linebreak_case(
        w,
        "hard-break-terminator-mid-paragraph",
        [*para((20, 30)), Penalty(INF_PENALTY), Glue(markdown.PARFILLSKIP), Penalty(EJECT_PENALTY), *para((25, 35, 40, 30))],
        [("default", uniform(LINE), default), ("first-fit", uniform(LINE), first_fit), ("centred", uniform(LINE), replace(RAGGED, left_skip=FIL, par_fill_skip=GlueSpec()))],
    )
    linebreak_case(
        w,
        "consecutive-forced-breaks-make-an-empty-line",
        [*para((20, 30)), Penalty(EJECT_PENALTY), Penalty(EJECT_PENALTY), *para((25, 35))],
        [("default", uniform(LINE), default), ("first-fit", uniform(LINE), first_fit)],
    )

    formula: list[Node] = [word(50), MathOn(), word(10), Glue(SPACE), word(10), MathOff(), Glue(SPACE), word(50)]
    linebreak_case(w, "formula-glue-is-not-a-breakpoint", formula, [("narrow", uniform(60 * PT), default), ("first-fit", uniform(60 * PT), first_fit)])
    formula_ff: list[Node] = [
        word(30), Glue(STIFF), word(30), Glue(STIFF), MathOn(PT), word(20), Glue(STIFF), word(20), MathOff(PT), Glue(STIFF), word(30), Glue(STIFF), word(30),
    ]
    linebreak_case(
        w,
        "formula-with-mathsurround",
        formula_ff,
        [
            ("ragged", uniform(LINE), RAGGED),
            ("ragged-first-fit", uniform(LINE), ragged_ff),
            ("break-at-mathoff", uniform(114 * PT), default),
            ("break-at-mathoff-first-fit", uniform(114 * PT), ragged_ff),
            ("mathon-starts-a-line", uniform(66 * PT), ragged_ff),
        ],
    )
    for kind in (KernKind.EXPLICIT, KernKind.NORMAL, KernKind.ACCENT):
        kerned: list[Node] = [word(30), Kern(2 * PT, kind), Glue(SPACE), word(30), Glue(SPACE), word(30)]
        linebreak_case(
            w,
            f"kern-{kind.name.lower()}-before-glue",
            kerned,
            [("narrow", uniform(30 * PT), default), ("first-fit", uniform(30 * PT), first_fit), ("ragged-90", uniform(62 * PT), RAGGED)],
        )
        leading: list[Node] = [word(30), Glue(SPACE), Kern(2 * PT, kind), word(30), Glue(SPACE), Kern(2 * PT, kind), Glue(SPACE), word(30)]
        linebreak_case(
            w,
            f"kern-{kind.name.lower()}-after-glue",
            leading,
            [("narrow", uniform(32 * PT), default), ("first-fit", uniform(32 * PT), first_fit)],
        )
    linebreak_case(
        w,
        "adjacent-breakpoints",
        [word(60), Glue(STIFF), Penalty(0), Kern(3 * PT, KernKind.EXPLICIT), Glue(STIFF), word(60)],
        [("ragged", uniform(LINE), RAGGED), ("ragged-first-fit", uniform(LINE), ragged_ff), ("default", uniform(LINE), default)],
    )

    hyphen = Disc(pre_break=(word(2),))
    linebreak_case(
        w,
        "disc-pre-break-ends-the-line",
        [word(40), Glue(SPACE), word(30), hyphen, word(30), Glue(SPACE), word(40)],
        [
            ("hyphen-penalty-0", uniform(80 * PT), LineBreakParams(hyphen_penalty=0)),
            ("default", uniform(80 * PT), default),
            ("first-fit", uniform(80 * PT), first_fit),
            ("hyphen-penalty-inf", uniform(80 * PT), LineBreakParams(hyphen_penalty=INF_PENALTY)),
            ("hyphen-penalty-inf-first-fit", uniform(80 * PT), LineBreakParams(hyphen_penalty=INF_PENALTY, first_fit=True)),
        ],
    )
    for count in (0, 1, 2):
        disc = Disc(pre_break=(word(2),), post_break=(word(3),), replace_count=count)
        linebreak_case(
            w,
            f"disc-replacing-{count}",
            [word(40), Glue(SPACE), word(30), disc, word(9), word(21), Glue(SPACE), word(40)],
            [
                ("taken", uniform(80 * PT), LineBreakParams(hyphen_penalty=0)),
                ("not-taken", uniform(200 * PT), default),
                ("ragged", uniform(80 * PT), RAGGED),
                ("ragged-first-fit", uniform(80 * PT), ragged_ff),
            ],
        )
    explicit = Disc()
    linebreak_case(
        w,
        "disc-empty-pre-break-uses-ex-hyphen-penalty",
        [word(40), Glue(SPACE), word(30), explicit, word(30), Glue(SPACE), word(40)],
        [
            ("default", uniform(80 * PT), default),
            ("ex-0-hyphen-inf", uniform(80 * PT), LineBreakParams(ex_hyphen_penalty=0, hyphen_penalty=INF_PENALTY)),
            ("ex-inf", uniform(80 * PT), LineBreakParams(ex_hyphen_penalty=INF_PENALTY)),
            ("ex-inf-first-fit", uniform(80 * PT), LineBreakParams(ex_hyphen_penalty=INF_PENALTY, first_fit=True)),
        ],
    )
    hyphens: list[Node] = []
    for index in range(8):
        if index:
            hyphens.append(Glue(SPACE))
        hyphens.extend([word(22), Disc(pre_break=(word(2),)), word(22)])
    linebreak_case(
        w,
        "disc-double-and-final-hyphen-demerits",
        hyphens,
        [
            ("default", uniform(70 * PT), PERMISSIVE),
            ("no-hyphen-demerits", uniform(70 * PT), replace(PERMISSIVE, double_hyphen_demerits=0, final_hyphen_demerits=0)),
            ("heavy-double", uniform(70 * PT), replace(PERMISSIVE, double_hyphen_demerits=1_000_000)),
            ("heavy-final", uniform(70 * PT), replace(PERMISSIVE, final_hyphen_demerits=1_000_000)),
            ("first-fit", uniform(70 * PT), first_fit),
        ],
    )
    linebreak_case(
        w,
        "disc-at-the-very-end",
        [word(40), Glue(SPACE), word(30), Disc(pre_break=(word(2),))],
        [("default", uniform(60 * PT), default), ("first-fit", uniform(60 * PT), first_fit)],
    )
    linebreak_case(
        w,
        "disc-followed-by-glue-then-pruned",
        [word(40), Disc(pre_break=(word(2),), post_break=(word(3),)), Glue(SPACE), Penalty(0), Kern(PT, KernKind.EXPLICIT), Kern(PT), word(40)],
        [("default", uniform(45 * PT), LineBreakParams(hyphen_penalty=0)), ("first-fit", uniform(45 * PT), first_fit)],
    )

    linebreak_case(
        w,
        "infinite-shrink-is-made-finite",
        [word(90), Glue(elastic), word(90)],
        [("narrow", uniform(50 * PT), default), ("fits-after-shrink", uniform(186 * PT - 1), default), ("first-fit", uniform(186 * PT - 1), first_fit)],
    )
    linebreak_case(
        w,
        "characters-through-the-measure",
        [*chars("ab"), Glue(SPACE), *chars("cd"), Glue(SPACE), *chars("Typography"), Glue(SPACE), *chars("jq")],
        [("default", uniform(40 * PT), default), ("first-fit", uniform(40 * PT), first_fit), ("wide", uniform(400 * PT), default)],
    )
    linebreak_case(
        w,
        "pruning-after-a-break",
        [word(60), Glue(STIFF), Penalty(0), Glue(STIFF), MathOn(PT), MathOff(PT), Kern(2 * PT, KernKind.EXPLICIT), Kern(2 * PT, KernKind.NORMAL), Glue(STIFF), word(60)],
        [("default", uniform(70 * PT), default), ("first-fit", uniform(70 * PT), first_fit)],
    )
    linebreak_case(
        w,
        "rule-and-box-precede-a-break",
        [word(30), Rule(PT, 5 * PT, 0), Glue(SPACE), CharNode("fake@10", ord("a")), Glue(SPACE), Rule(40 * PT, PT, 0), Glue(SPACE), word(30)],
        [("default", uniform(45 * PT), default), ("first-fit", uniform(45 * PT), first_fit)],
    )

    # Equal words on an exact measure: many breakings tie, and section 855's
    # `<=` (the later active node wins) is the only thing choosing.
    for count, measure_pt in ((12, 58), (15, 90), (18, 42), (9, 26)):
        equal = para((10,) * count)
        linebreak_case(
            w,
            f"ties-{count}-equal-words-at-{measure_pt}pt",
            equal,
            [
                ("default", uniform(measure_pt * PT), default),
                ("permissive", uniform(measure_pt * PT), PERMISSIVE),
                ("ragged-total-fit", uniform(measure_pt * PT), RAGGED),
                ("ragged-first-fit", uniform(measure_pt * PT), ragged_ff),
                ("first-fit", uniform(measure_pt * PT), first_fit),
                ("emergency", uniform(measure_pt * PT), LineBreakParams(emergency_stretch=30 * PT)),
            ],
        )
    symmetric = para((20, 10, 20, 10, 20, 10, 20, 10, 20, 10, 20), GlueSpec(5 * PT, 5 * PT, GlueOrder.NORMAL, 5 * PT))
    linebreak_case(
        w,
        "ties-symmetric-words",
        symmetric,
        [("w60", uniform(60 * PT), PERMISSIVE), ("w65", uniform(65 * PT), PERMISSIVE), ("w45", uniform(45 * PT), PERMISSIVE), ("w45-line-penalty-0", uniform(45 * PT), replace(PERMISSIVE, line_penalty=0))],
    )

    # Ties the breaker's ordering decides. With a `fil` right skip every line
    # that fits has badness 0 and is DECENT, so its demerits are exactly
    # line_penalty squared: every chain with the same number of lines ties.
    # With line_penalty 0 as well, chains with DIFFERENT numbers of lines tie
    # at zero, and `min(active)` returning the first minimum is what chooses.
    # `tie_properties` proves both situations really occur in these runs.
    zero_cost = replace(RAGGED, line_penalty=0)
    tie_words = para((10,) * 12, STIFF)
    for label, params, need_counts in (("ragged", RAGGED, False), ("ragged-line-penalty-0", zero_cost, True)):
        for measure_pt in (58, 42, 26):
            counts_tie, survivor_tie = tie_properties(tie_words, uniform(measure_pt * PT), params)
            assert survivor_tie, (label, measure_pt)
            assert counts_tie == need_counts, (label, measure_pt, counts_tie)
    linebreak_case(
        w,
        "ties-survivor-against-created-and-across-line-counts",
        tie_words,
        [
            (f"{label}-{measure_pt}pt", uniform(measure_pt * PT), params)
            for label, params in (("ragged", RAGGED), ("ragged-line-penalty-0", zero_cost), ("ragged-line-penalty-0-first-fit", replace(zero_cost, first_fit=True)))
            for measure_pt in (58, 42, 26)
        ],
    )
    assert tie_properties(showcase, uniform(LINE), zero_cost) == (True, True)
    linebreak_case(
        w,
        "ties-showcase-every-line-free",
        showcase,
        [
            ("ragged-line-penalty-0", uniform(LINE), zero_cost),
            ("ragged-line-penalty-0-narrow", uniform(45 * PT), zero_cost),
            ("ragged-line-penalty-0-hanging", ParShape.hanging(LINE, 30 * PT), zero_cost),
            ("centred-line-penalty-0", uniform(LINE), replace(zero_cost, left_skip=FIL)),
        ],
    )
    # A penalty between two glues: the first glue and the penalty are
    # breakpoints, the second glue is not (a penalty does not permit a break at
    # the glue after it), and a break at either prunes what follows it.
    sandwich: list[Node] = [word(40)]
    for value in (0, 50, -50, 0):
        sandwich.extend([Glue(SPACE), Penalty(value), Glue(SPACE), word(40)])
    linebreak_case(
        w,
        "glue-penalty-glue",
        sandwich,
        [
            ("default", uniform(LINE), default),
            ("permissive", uniform(LINE), PERMISSIVE),
            ("permissive-92", uniform(92 * PT), PERMISSIVE),
            ("permissive-52", uniform(52 * PT), PERMISSIVE),
            ("ragged", uniform(LINE), RAGGED),
            ("ragged-first-fit", uniform(LINE), ragged_ff),
            ("first-fit", uniform(LINE), first_fit),
            ("first-fit-52", uniform(52 * PT), first_fit),
        ],
    )

    # Demerits past AWFUL_BAD: every forced line saturates at 10^8, so a chain's
    # running total crosses 2^30 - 1 after eleven of them. A candidate whose
    # total is past AWFUL_BAD is never recorded (section 855 compares against
    # an accumulator that starts there), so the non-final pass runs out of
    # active nodes and fails. The run `adj-past-awful-bad` of
    # `adjacent-fitness-classes` reaches the same refusal through one line
    # whose adjacency charge alone is 2^30; summing two such charges needs
    # more than 31 bits, which Python's integers have and an `int` does not.
    saturating: list[Node] = []
    for index in range(16):
        if index:
            saturating.extend([Penalty(EJECT_PENALTY), Glue(SPACE)])
        saturating.extend([word(5), Glue(GlueSpec(6 * PT, 1)), word(5)])
    linebreak_case(
        w,
        "demerits-past-awful-bad",
        saturating,
        [
            ("permissive", uniform(LINE), PERMISSIVE),
            ("permissive-emergency", uniform(LINE), replace(PERMISSIVE, emergency_stretch=PT)),
            ("default", uniform(LINE), default),
            ("first-fit", uniform(LINE), first_fit),
        ],
        skip=None,
    )
    unbreakable: list[Node] = []
    for index in range(14):
        if index:
            unbreakable.append(Glue(GlueSpec(6 * PT, 1)))
        unbreakable.append(word(40))
    linebreak_case(
        w,
        "every-line-underfull",
        unbreakable,
        [
            ("default", uniform(LINE), default),
            ("permissive", uniform(LINE), PERMISSIVE),
            ("emergency-small", uniform(LINE), LineBreakParams(emergency_stretch=PT)),
            ("emergency-large", uniform(LINE), LineBreakParams(emergency_stretch=200 * PT)),
            ("first-fit", uniform(LINE), first_fit),
        ],
    )

    rng = Rng(0x62726B)
    presets: list[tuple[str, LineBreakParams]] = [
        ("default", default),
        ("permissive", PERMISSIVE),
        ("prose", ui.PROSE_BREAKING),
        ("prose-bounded", ui.PROSE_BREAKING_BOUNDED),
        ("first-fit", first_fit),
        ("ragged-first-fit", replace(ui.PROSE_BREAKING, right_skip=ui.RAGGED_RIGHT, first_fit=True)),
        ("centred-first-fit", replace(ui.PROSE_BREAKING, left_skip=ui.RAGGED_RIGHT, right_skip=ui.RAGGED_RIGHT, par_fill_skip=GlueSpec(), first_fit=True)),
        ("no-first-pass", LineBreakParams(pretolerance=-1, tolerance=400)),
        ("left-skip", LineBreakParams(left_skip=GlueSpec(4 * PT + 5, PT // 2), tolerance=2000)),
    ]
    for index in range(48):
        words = rng.between(3, 44)
        nodes = random_paragraph(rng, words)
        runs: list[Run] = []
        for _ in range(3):
            width = rng.between(30, 190) * PT + rng.below(PT)
            roll = rng.below(6)
            if roll == 0:
                shape = ParShape.hanging(width, rng.between(5, 20) * PT, rng.between(0, 2))
            elif roll == 1:
                shape = ParShape(tuple(LineSpec(width - rng.between(0, 25) * PT, rng.between(0, 12) * PT) for _ in range(rng.between(2, 4))))
            elif roll == 2:
                shape = ParShape.uniform(width, rng.between(1, 15) * PT)
            else:
                shape = ParShape.uniform(width)
            label, params = rng.pick(presets)
            runs.append((f"{len(runs)}-{label}", shape, params))
        if words <= 12:
            runs.append(("ragged-total-fit", ParShape.uniform(rng.between(40, 120) * PT), RAGGED))
        linebreak_case(w, f"random-{index:02d}", nodes, runs)

    fitness_threshold_cases(w)

    # ParShape itself: the constructor's refusals and `for_line`.
    w.case("parshape-empty-is-an-error", "parshape")
    try:
        ParShape(())
    except ValueError as error:
        w.r("shape", 0)
        w.r("error", slug_of(error))
    w.end()
    for name, shape in (
        ("uniform", ParShape.uniform(LINE, 3 * PT)),
        ("hanging-after-0", ParShape.hanging(LINE, 12 * PT, 0)),
        ("hanging-after-1", ParShape.hanging(LINE, 12 * PT)),
        ("hanging-after-3", ParShape.hanging(LINE, 12 * PT, 3)),
        ("hanging-negative", ParShape.hanging(LINE, -12 * PT, 2)),
        ("three-specs", shape3),
    ):
        w.case("parshape-" + name, "parshape")
        w.r("shape", len(shape.lines), *[v for line in shape.lines for v in (line.width, line.indent)])
        for line in (1, 2, 3, 4, 5, 100):
            spec = shape.for_line(line)
            w.r("for_line", line, spec.width, spec.indent)
        w.end()
    return w


# --------------------------------------------------------------------------
# paragraphs.golden: real text through the reference's own composition path
# --------------------------------------------------------------------------

PROSE_A = (
    "Typography is the craft of endowing human language with a durable visual form, "
    "and thus with an independent existence. Its heartwood is calligraphy; the dance, "
    "on a tiny stage, of the living, speaking hand. And its roots reach into living "
    "soil, though its branches may be hung each year with new machines."
)
PROSE_SENTENCES = (
    "Mr. Smith went to NASA. Then he left! Really? Yes. \"Quite so.\" (He said.) "
    "The AVATAR of WAVE Theory, To Yo, P. A. T. V. Wa. We fly; fjord gypsy 1,234.5 "
    "and so on... at the end."
)
PROSE_HARD_BREAKS = "Roses are red,\n  violets are blue;  \n\nthe measure is narrow\nand so are you."
PROSE_UNBROKEN = "Antidisestablishmentarianism pneumonoultramicroscopicsilicovolcanoconiosis a b c"
MIXED_MARKDOWN = (
    "Plain words, then **bold words** and *italic words* and ***both at once***, "
    "with `inline code --flag=value` that must not be cut, and a [link](https://example.org) "
    "to finish the paragraph off properly across a few lines.\n"
)
MATH_MARKDOWN = (
    "The roots are $x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}$ and the energy is $E = mc^2$, "
    "while $\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2}$ sits in the line with the words around it "
    "and $a_i^2 + b_i^2$ follows.\n"
)

Measured = tuple[str, markdown.TextBlock, Theme]


def _ui_run(block: markdown.TextBlock, width: Scaled, params: LineBreakParams) -> tuple[str, Sequence[Node], ParShape, LineBreakParams]:
    """
    What `advtext.ui._text_boxes` hands `break_paragraph` for this block.

    The same three private helpers, in the same order: `_skips` for the
    alignment, `_unshrinkable` plus `first_fit` for anything not justified,
    `_shape` for the per-line widths.
    """
    align = block.style.align
    material: Sequence[Node] = block.material
    line_params = ui._skips(align, params)
    list_name = "material"
    if align is not Align.JUSTIFY:
        material = ui._unshrinkable(block.material)
        line_params = replace(line_params, first_fit=True)
        list_name = "unshrinkable"
    return list_name, material, ui._shape(block, width), line_params


def paragraph_case(
    w: Writer,
    name: str,
    source_kind: str,
    source: str,
    block: markdown.TextBlock,
    theme: Theme,
    widths_px: Sequence[float],
    measure: CharMeasure,
    extra: Sequence[tuple[str, float, LineBreakParams]] = (),
) -> None:
    """
    One text block at several widths, as the UI would break it, plus variants.

    `extra` runs bypass `_skips`: they hand the block's own material to the
    breaker with the given parameters, to reach paths the UI never takes
    (plain-TeX tolerance on real text, first-fit on justified material).
    """
    w.case(name, "linebreak")
    w.r("source", source_kind, q(source))
    w.r(
        "theme",
        float(theme.px_per_inch),
        theme.spacing,
        block.kind,
        block.style.align,
        q(block.style.font.key),
        block.left_indent,
        block.style.first_line_indent,
    )
    unshrinkable = ui._unshrinkable(block.material)
    emit_measure(w, measure, block.material, block.marker)
    emit_nodes(w, "material", block.material)
    emitted_unshrinkable = False
    skip = block.style.interline
    for width_px in widths_px:
        width = sp_from_px(width_px, theme.px_per_inch)
        list_name, material, shape, params = _ui_run(block, width, ui.PROSE_BREAKING)
        if list_name == "unshrinkable" and not emitted_unshrinkable:
            emit_nodes(w, "unshrinkable", unshrinkable)
            emitted_unshrinkable = True
        emit_run(w, f"ui-{width_px:g}px", list_name, material, shape, params, measure, skip)
    for label, width_px, params in extra:
        width = sp_from_px(width_px, theme.px_per_inch)
        emit_run(w, f"{label}-{width_px:g}px", "material", block.material, ui._shape(block, width), params, measure, skip)
    w.end()


def dump_paragraphs() -> Writer:
    w = Writer(
        "paragraphs",
        """
Real text, built into material by the reference's own composition path
(advtext.markdown.text_block / build_document with advtext.style.fontlib_metrics)
and broken as advtext.ui._text_boxes breaks it. The `measure` records are
advtext.style.char_measure(fontlib_metrics) answers; the kerns are the Kern
nodes in the material. Each run also ships its lines (FORMAT.md, "ship section").
""",
    )
    measure = char_measure(fontlib_metrics)
    default = LineBreakParams()
    first_fit = replace(ui.PROSE_BREAKING, first_fit=True)
    widths = (120.0, 200.0, 300.0, 420.0, 800.0)

    def plain(text: str, theme: Theme, **kwargs: object) -> markdown.TextBlock:
        return markdown.text_block(text, theme, fontlib_metrics, **kwargs)  # type: ignore[arg-type]

    justified = Theme.build()
    paragraph_case(
        w,
        "prose-16px-justified",
        "text",
        PROSE_A,
        plain(PROSE_A, justified),
        justified,
        (40.0, *widths),
        measure,
        extra=[
            ("plain-tex", 300.0, default),
            ("plain-tex", 420.0, default),
            ("plain-tex", 120.0, default),
            ("first-fit", 300.0, first_fit),
            ("first-fit", 120.0, first_fit),
            ("bounded", 200.0, ui.PROSE_BREAKING_BOUNDED),
        ],
    )
    for align in (Align.LEFT, Align.RIGHT, Align.CENTER):
        theme = Theme.build(align=align)
        paragraph_case(w, f"prose-16px-{align.name.lower()}", "text", PROSE_A, plain(PROSE_A, theme), theme, (40.0, 200.0, 300.0, 420.0), measure)
    for size in (13.0, 24.0):
        theme = Theme.build(size)
        paragraph_case(w, f"prose-{size:g}px-justified", "text", PROSE_A, plain(PROSE_A, theme), theme, (200.0, 420.0), measure)
    hidpi = Theme.build(16.0, 144.0)
    paragraph_case(w, "prose-16px-at-144-px-per-inch", "text", PROSE_A, plain(PROSE_A, hidpi), hidpi, (300.0, 630.0), measure)
    for spacing in Spacing:
        theme = Theme.build(spacing=spacing)
        paragraph_case(
            w,
            f"sentences-{spacing.name.lower().replace('_', '-')}",
            "text",
            PROSE_SENTENCES,
            plain(PROSE_SENTENCES, theme),
            theme,
            (200.0, 420.0),
            measure,
        )
    for align in (Align.JUSTIFY, Align.LEFT, Align.CENTER):
        theme = Theme.build(align=align)
        paragraph_case(
            w,
            f"hard-breaks-{align.name.lower()}",
            "text-hard-breaks",
            PROSE_HARD_BREAKS,
            plain(PROSE_HARD_BREAKS, theme, hard_breaks=True),
            theme,
            (90.0, 300.0),
            measure,
        )
    paragraph_case(w, "unbroken-words", "text", PROSE_UNBROKEN, plain(PROSE_UNBROKEN, justified), justified, (120.0, 300.0), measure, extra=[("plain-tex", 120.0, default), ("first-fit", 120.0, first_fit)])
    paragraph_case(
        w,
        "heading-2",
        "text",
        "A heading long enough to wrap onto a second line",
        plain("A heading long enough to wrap onto a second line", justified, kind=BlockKind.HEADING_2),
        justified,
        (120.0, 300.0, 800.0),
        measure,
        extra=[("ragged-total-fit", 200.0, replace(ui.PROSE_BREAKING, right_skip=ui.RAGGED_RIGHT))],
    )
    item_text = "A list item whose text runs on for long enough to need a continuation line or two."
    paragraph_case(
        w,
        "bullet-item-indented",
        "text",
        item_text,
        plain(item_text, justified, kind=BlockKind.BULLET_ITEM, marker="•", indent=justified.list_indent),
        justified,
        (200.0, 420.0),
        measure,
    )
    paragraph_case(
        w,
        "quote-indented",
        "text",
        item_text,
        plain(item_text, justified, kind=BlockKind.QUOTE, indent=justified.quote_indent, quote_depth=1),
        justified,
        (200.0, 420.0),
        measure,
    )
    code_text = "for (int i = 0; i < n; ++i) { total += values[i] * weights[i]; }"
    code_theme = Theme.build(faces=Faces.mono())
    paragraph_case(w, "monospace-body", "text", code_text, plain(code_text, code_theme), code_theme, (200.0, 420.0), measure)

    def markdown_blocks(source: str, theme: Theme, math: bool = False) -> list[markdown.TextBlock]:
        document = markdown.build_document(source, theme, fontlib_metrics, math=markdown_math() if math else None)
        return [block for block in document.blocks if isinstance(block, markdown.TextBlock)]

    (mixed,) = markdown_blocks(MIXED_MARKDOWN, justified)
    paragraph_case(w, "mixed-faces", "markdown", MIXED_MARKDOWN, mixed, justified, (200.0, 300.0, 420.0), measure, extra=[("first-fit", 300.0, first_fit)])
    (formulas,) = markdown_blocks(MATH_MARKDOWN, justified, math=True)
    assert any(isinstance(node, Box) and is_atom(node) for node in formulas.material), "no inline formula was set"
    paragraph_case(w, "inline-math", "markdown-math", MATH_MARKDOWN, formulas, justified, (200.0, 300.0, 420.0), measure, extra=[("first-fit", 300.0, first_fit)])

    # Characters the face lacks: borrowed from another face of the theme where
    # one has it (a plain `char` in that face's key), else U+FFFD in the prose
    # face as a markdown.ReplacedChar, which is the `rchar` record.
    borrowed_text = "Arrows \u2192 and \u2200x \u2208 S, then \u6f22\u5b57 and \U0001f600 in prose."
    borrowed = plain(borrowed_text, justified)
    assert any(isinstance(node, markdown.ReplacedChar) for node in borrowed.material), "nothing was replaced"
    paragraph_case(w, "stand-in-characters", "text", borrowed_text, borrowed, justified, (120.0, 420.0), measure)
    (code_borrowed,) = markdown_blocks("Inline `\u03c0 r\u00b2 \u2192 \u6f22` in the code face.\n", justified)
    paragraph_case(w, "stand-in-characters-in-code", "markdown", "Inline `\u03c0 r\u00b2 \u2192 \u6f22` in the code face.\n", code_borrowed, justified, (120.0, 420.0), measure)

    sample = (REFERENCE / "demo" / "sample.md").read_text(encoding="utf-8")
    blocks = markdown_blocks(sample, justified)
    largest = max(blocks, key=lambda block: len(block.material))
    assert len(largest.material) == 635, len(largest.material)
    paragraph_case(
        w,
        "sample-md-largest-paragraph",
        "markdown-block-of-demo/sample.md",
        "",
        largest,
        justified,
        (200.0, 300.0, 420.0, 800.0),
        measure,
        extra=[("plain-tex", 420.0, default), ("first-fit", 420.0, first_fit)],
    )
    ordered = next(block for block in blocks if block.kind is BlockKind.ORDERED_ITEM)
    paragraph_case(w, "sample-md-first-ordered-item", "markdown-block-of-demo/sample.md", "", ordered, justified, (200.0, 420.0), measure)
    quote = next(block for block in blocks if block.kind is BlockKind.QUOTE)
    paragraph_case(w, "sample-md-quote", "markdown-block-of-demo/sample.md", "", quote, justified, (200.0, 420.0), measure)
    ragged_blocks = markdown_blocks(sample, Theme.build(align=Align.LEFT))
    ragged_largest = max(ragged_blocks, key=lambda block: len(block.material))
    paragraph_case(w, "sample-md-largest-paragraph-ragged", "markdown-block-of-demo/sample.md", "", ragged_largest, Theme.build(align=Align.LEFT), (200.0, 420.0), measure)
    return w


# --------------------------------------------------------------------------
# Verification: a reader written from FORMAT.md, and a replay through the reference
# --------------------------------------------------------------------------


class Records:
    """
    A golden file as FORMAT.md describes it: records, each a list of tokens.
    """

    def __init__(self, text: str) -> None:
        self.rows = [line.split(" ") for line in text.split("\n") if line and not line.startswith("#")]
        self.at = 0

    def peek(self) -> list[str]:
        return self.rows[self.at]

    def take(self, tag: str | None = None) -> list[str]:
        row = self.rows[self.at]
        if tag is not None and row[0] != tag:
            raise AssertionError(f"expected {tag}, found {row}")
        self.at += 1
        return row

    def done(self) -> bool:
        return self.at >= len(self.rows)


def _enum(cls: type[enum.Enum], token: str):  # type: ignore[no-untyped-def]
    return cls[token.upper()]


def _spec(tokens: Sequence[str]) -> GlueSpec:
    return GlueSpec(int(tokens[0]), int(tokens[1]), _enum(GlueOrder, tokens[2]), int(tokens[3]), _enum(GlueOrder, tokens[4]))


def read_node(records: Records) -> Node:
    row = records.take()
    tag = row[0]
    if tag == "char":
        return CharNode(unq(row[1]), int(row[2]))
    if tag == "rchar":
        return markdown.ReplacedChar(unq(row[1]), int(row[2]), int(row[3]))
    if tag == "box":
        children = tuple(read_node(records) for _ in range(int(row[9])))
        records.take("endbox")
        return Box(
            kind=_enum(BoxKind, row[1]),
            contents=children,
            width=int(row[2]),
            height=int(row[3]),
            depth=int(row[4]),
            shift_amount=int(row[5]),
            glue_set=float.fromhex(row[6]),
            glue_sign=_enum(GlueSign, row[7]),
            glue_order=_enum(GlueOrder, row[8]),
        )
    if tag == "rule":
        return Rule(int(row[1]), int(row[2]), int(row[3]))
    if tag == "glue":
        return Glue(_spec(row[1:6]))
    if tag == "kern":
        return Kern(int(row[1]), _enum(KernKind, row[2]))
    if tag == "penalty":
        return Penalty(int(row[1]))
    if tag == "disc":
        pre = tuple(read_node(records) for _ in range(int(row[2])))
        post = tuple(read_node(records) for _ in range(int(row[3])))
        records.take("enddisc")
        return Disc(pre_break=pre, post_break=post, replace_count=int(row[1]))
    if tag == "mathon":
        return MathOn(int(row[1]))
    if tag == "mathoff":
        return MathOff(int(row[1]))
    raise AssertionError(f"not a node record: {row}")


def read_nodes(records: Records) -> tuple[str, list[Node]]:
    row = records.take("nodes")
    nodes = [read_node(records) for _ in range(int(row[2]))]
    records.take("endnodes")
    return row[1], nodes


def read_measure(records: Records) -> CharMeasure | None:
    table: dict[tuple[str, int], CharDimens] = {}
    while records.peek()[0] == "measure":
        row = records.take()
        table[(unq(row[1]), int(row[2]))] = CharDimens(int(row[3]), int(row[4]), int(row[5]))
    if not table:
        return None
    return lambda node: table[(node.font, node.codepoint)]


def read_packed(records: Records) -> PackResult:
    row = records.take("packed")
    box = read_node(records)
    assert isinstance(box, Box)
    return PackResult(box, _enum(PackFitness, row[1]), int(row[2]), int(row[3]), int(row[4]), int(row[5]))


def read_cmds(records: Records) -> list[DrawCmd]:
    count = int(records.take("cmds")[1])
    out: list[DrawCmd] = []
    for _ in range(count):
        row = records.take()
        tag = row[0]
        if tag == "glyph":
            out.append(Glyph(unq(row[1]), int(row[2]), int(row[3]), int(row[4]), int(row[5])))
        elif tag == "rulerect":
            out.append(RuleRect(*[int(v) for v in row[1:5]]))
        elif tag == "setglue":
            out.append(SetGlue(*[int(v) for v in row[1:8]]))
        elif tag == "atom":
            out.append(Atom(*[int(v) for v in row[1:8]]))
        elif tag == "line":
            out.append(Line(*[int(v) for v in row[1:8]]))
        else:
            raise AssertionError(row)
    records.take("endcmds")
    return out


def same_cmds(a: Sequence[DrawCmd], b: Sequence[DrawCmd]) -> bool:
    return len(a) == len(b) and all(type(x) is type(y) and x == y for x, y in zip(a, b))


def read_skip(records: Records) -> InterlineSkip:
    baselineskip = _spec(records.take("spec")[2:7])
    lineskip = _spec(records.take("spec")[2:7])
    return InterlineSkip(baselineskip, lineskip, int(records.take("lineskiplimit")[1]))


def _expect_error(records: Records, call: Callable[[], object]) -> None:
    slug = records.take("error")[1]
    try:
        call()
    except ValueError as error:
        assert slug_of(error) == slug
    else:
        raise AssertionError(f"the reference did not raise for {slug}")


def verify_table(records: Records) -> int:
    checked = 0
    while records.peek()[0] != "endcase":
        row = records.take()
        tag = row[0]
        hexf = float.fromhex
        if tag == "const":
            pass
        elif tag == "sp_from_pt":
            assert sp_from_pt(hexf(row[1])) == int(row[2])
        elif tag == "sp_from_bp":
            assert sp_from_bp(hexf(row[1])) == int(row[2])
        elif tag == "sp_from_px":
            assert sp_from_px(hexf(row[1]), hexf(row[2])) == int(row[3])
        elif tag == "pt_from_sp":
            assert pt_from_sp(int(row[1])).hex() == row[2]
        elif tag == "px_from_sp":
            assert px_from_sp(int(row[1]), hexf(row[2])).hex() == row[3]
        elif tag == "round_to_px":
            assert round_to_px(int(row[1]), hexf(row[2])) == int(row[3])
        elif tag == "badness":
            assert badness(int(row[1]), int(row[2])) == int(row[3])
        elif tag == "badness_raises":
            try:
                badness(int(row[1]), int(row[2]))
            except ValueError:
                pass
            else:
                raise AssertionError(row)
        elif tag == "pascal_round":
            assert pascal_round(hexf(row[1])) == int(row[2])
        elif tag in ("raises", "beyond_int64"):
            function = {"sp_from_pt": sp_from_pt, "sp_from_bp": sp_from_bp, "sp_from_px": sp_from_px, "px_from_sp": px_from_sp, "round_to_px": round_to_px}[row[1]]
            first = int if row[1] in ("px_from_sp", "round_to_px") else hexf
            args = [first(row[3]), *[hexf(v) for v in row[4:]]]
            try:
                result = function(*args)
            except (ValueError, OverflowError, ZeroDivisionError) as error:
                assert tag == "raises" and type(error).__name__.lower() == row[2], row
            else:
                assert tag == "beyond_int64" and str(result) == row[2], row
        else:
            raise AssertionError(row)
        checked += 1
    return checked


def verify_case(records: Records, kind: str) -> int:
    if kind == "table":
        return verify_table(records)
    if kind == "gluetotal":
        total = GlueTotal()
        while records.peek()[0] == "contrib":
            row = records.take()
            total = total + GlueTotal.of(int(row[1]), _enum(GlueOrder, row[2]))
        row = records.take("total")
        assert total.amounts == tuple(int(v) for v in row[1:5])
        assert total.dominant_order is _enum(GlueOrder, row[5]) and total.dominant == int(row[6])
        return 1
    if kind == "predicates":
        checked = 0
        while records.peek()[0] != "endcase":
            node = read_node(records)
            row = records.take()
            if row[0] == "pred":
                assert [is_discardable(node), permits_break_at_following_glue(node)] == [v == "1" for v in row[1:3]]
            elif row[0] == "penalty_pred":
                assert isinstance(node, Penalty)
                assert [node.forbids_break, node.forces_break] == [v == "1" for v in row[1:3]]
            elif row[0] == "rule_pred":
                assert isinstance(node, Rule)
                assert node.is_running == (row[1] == "1")
            else:
                raise AssertionError(row)
            checked += 1
        return checked
    if kind in ("hpack", "vpack"):
        measure = read_measure(records)
        _, nodes = read_nodes(records)
        row = records.take(kind)
        target, mode = int(row[1]), _enum(PackMode, row[2])
        if kind == "hpack":
            call = lambda: hpack(nodes, target, mode, measure=measure)  # noqa: E731
        else:
            max_depth = int(row[3])
            call = lambda: vpack(nodes, target, mode, max_depth=max_depth)  # noqa: E731
        if records.peek()[0] == "error":
            _expect_error(records, call)
        else:
            assert call() == read_packed(records)
        return 1
    if kind == "interline":
        skip = read_skip(records)
        prev_depth = int(records.take("prev_depth")[1])
        box = read_node(records)
        assert isinstance(box, Box)
        got = interline_glue(prev_depth, box, skip)
        if records.peek()[0] == "none":
            records.take()
            assert got is None
        else:
            assert got == read_node(records)
        return 1
    if kind == "append":
        skip = read_skip(records)
        prev_depth = int(records.take("prev_depth")[1])
        _, items = read_nodes(records)
        row = records.take("prev_depths")
        vlist: list[Node] = []
        depths: list[int] = []
        for item in items:
            assert isinstance(item, Box | Rule)
            prev_depth = append_to_vlist(vlist, item, prev_depth, skip)
            depths.append(prev_depth)
        assert depths == [int(v) for v in row[2:]] and len(depths) == int(row[1])
        assert read_nodes(records)[1] == vlist
        return 1
    if kind == "atom":
        box = read_node(records)
        assert isinstance(box, Box)
        assert is_atom(box) == (records.take("is_atom")[1] == "1")
        wrapped = read_node(records)
        assert atom_box(box) == wrapped and isinstance(wrapped, Box)
        assert is_atom(wrapped) == (records.take("is_atom")[1] == "1")
        return 1
    if kind == "ship":
        measure = read_measure(records)
        box = read_node(records)
        assert isinstance(box, Box)
        row = records.take("origin")
        origin = Point(int(row[1]), int(row[2]))
        if records.peek()[0] == "error":
            _expect_error(records, lambda: ship(box, origin, measure=measure))
        else:
            assert same_cmds(ship(box, origin, measure=measure), read_cmds(records))
        return 1
    if kind == "parshape":
        row = records.take("shape")
        specs = tuple(LineSpec(int(row[2 + 2 * i]), int(row[3 + 2 * i])) for i in range(int(row[1])))
        if records.peek()[0] == "error":
            _expect_error(records, lambda: ParShape(specs))
            return 1
        shape = ParShape(specs)
        while records.peek()[0] == "for_line":
            row = records.take()
            assert shape.for_line(int(row[1])) == LineSpec(int(row[2]), int(row[3]))
        return 1
    if kind == "linebreak":
        while records.peek()[0] in ("source", "theme"):
            records.take()
        measure = read_measure(records)
        lists: dict[str, list[Node]] = {}
        runs = 0
        while records.peek()[0] != "endcase":
            if records.peek()[0] == "nodes":
                name, nodes = read_nodes(records)
                lists[name] = nodes
                continue
            row = records.take("break")
            nodes = lists[row[2]]
            row = records.take("shape")
            shape = ParShape(tuple(LineSpec(int(row[2 + 2 * i]), int(row[3 + 2 * i])) for i in range(int(row[1]))))
            row = records.take("params")
            numbers = [int(v) for v in row[1:10]]
            params = LineBreakParams(
                *numbers,
                left_skip=_spec(records.take("spec")[2:7]),
                right_skip=_spec(records.take("spec")[2:7]),
                par_fill_skip=_spec(records.take("spec")[2:7]),
                first_fit=row[10] == "1",
            )
            got, breaks, prepared = break_with_positions(nodes, shape, params, measure)
            row = records.take("paragraph")
            assert (len(got.lines), got.total_demerits, got.pass_used.name.lower(), prepared) == (int(row[1]), int(row[2]), row[3], int(row[4]))
            row = records.take("breaks")
            assert [(int(row[2 + 2 * i]), row[3 + 2 * i]) for i in range(int(row[1]))] == breaks
            for line in got.lines:
                assert line == read_packed(records)
            if records.peek()[0] == "spec":
                skip = read_skip(records)
                vbox, origin, cmds = _ship_lines(got.boxes, skip, measure)
                row = records.take("vbox")
                assert (vbox.width, vbox.height, vbox.depth) == tuple(int(v) for v in row[1:4])
                row = records.take("origin")
                assert (origin.x, origin.y) == (int(row[1]), int(row[2]))
                assert same_cmds(cmds, read_cmds(records))
            records.take("endbreak")
            runs += 1
        return runs
    raise AssertionError(f"unknown case kind {kind}")


def verify(path: Path) -> tuple[int, int]:
    raw = path.read_bytes()
    text = raw.decode("utf-8")
    assert text.endswith("\n") and "\r" not in text and "\t" not in text
    for line in text.split("\n"):
        assert not line.startswith(" ") and not line.endswith(" ") and "  " not in line, line
    records = Records(text)
    header = records.take("golden")
    assert int(header[1]) == FORMAT_VERSION and unq(header[2]) == "core/" + path.stem
    cases = checks = 0
    while not records.done():
        row = records.take("case")
        checks += verify_case(records, row[2])
        records.take("endcase")
        cases += 1
    return cases, checks


# --------------------------------------------------------------------------
# Entry point
# --------------------------------------------------------------------------

DUMPS: tuple[Callable[[], Writer], ...] = (
    dump_units,
    dump_badness,
    dump_nodes,
    dump_pack,
    dump_shipout,
    dump_linebreak,
    dump_paragraphs,
)


def main() -> int:
    parser = argparse.ArgumentParser(description=(__doc__ or "").strip().splitlines()[0])
    parser.add_argument("--verify", action="store_true", help="re-read the goldens and replay them")
    args = parser.parse_args()

    if args.verify:
        total_cases = total_checks = 0
        for path in sorted(OUT_DIR.glob("*.golden")):
            cases, checks = verify(path)
            print(f"{path.relative_to(ROOT)}: {cases} cases, {checks} replayed checks ok")
            total_cases += cases
            total_checks += checks
        print(f"total: {total_cases} cases, {total_checks} replayed checks ok")
        return 0

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    total_cases = total_bytes = 0
    written: set[str] = set()
    for dump in DUMPS:
        writer = dump()
        data = writer.text().encode("utf-8")
        path = OUT_DIR / f"{writer.name}.golden"
        path.write_bytes(data)
        written.add(path.name)
        print(f"{path.relative_to(ROOT)}: {writer.cases} cases, {len(writer.lines)} lines, {len(data)} bytes")
        total_cases += writer.cases
        total_bytes += len(data)
    stale = sorted(p.name for p in OUT_DIR.glob("*.golden") if p.name not in written)
    if stale:
        print(f"warning: goldens this script does not write: {stale}", file=sys.stderr)
    print(f"total: {total_cases} cases, {total_bytes} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
