#!/usr/bin/env python3
"""
Dump block/document-layer goldens from the Python reference (imgui-adv-text).

Writes tests/golden/blocks/*.golden: several blocks -- paragraphs, headings,
list items with their markers, code blocks, quotes with their bar, thematic
rules -- stacked into one document box, at several widths. The record
vocabulary is tools/oracle/FORMAT-blocks.md, over FORMAT-para.md and
FORMAT.md, whose records are written by the functions of the sibling scripts
dump_para.py and dump_core.py, imported here and not edited.

Run from the imtypeset repository root, with the reference's interpreter:

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=build/advtext-reference \\
        ../imgui-adv-text/.venv/bin/python -P -B tools/oracle/dump_blocks.py

    ... dump_blocks.py --verify   re-reads every golden, rebuilds each case
                                  from its INPUT records alone -- blocks built
                                  from the recorded runs, a Theme holding only
                                  the recorded values and no block styles, a
                                  metrics resolver answering only from the
                                  recorded tables -- replays it through the
                                  reference and compares every EXPECTED
                                  record; then rebuilds the document's
                                  vertical list from the EXPECTED records
                                  alone and packs and ships that.

The script only reads the reference. Output is deterministic: a second run is
byte-identical to the first.

Every expectation is an answer of one of the reference's own functions, called
directly and not re-implemented:

    blocks             advtext.markdown.build_document, text_block, code_block
                       (the Block list is captured; markdown is not an input)
    a block's lines    advtext.ui._text_boxes / _block_boxes / _rule_box,
                       advtext.linebreak.break_paragraph as _text_boxes calls it
    stacking           advtext.ui._stacked, _runs, _barred, with
                       advtext.pack.append_to_vlist observed (wrapped in
                       memory) for the prev_depth it was handed and returned
    the document box   advtext.ui.compose_blocks
    commands           advtext.shipout.ship
    units and roles    advtext.ui.RunComposer, advtext.ui.units.ship_unit and
                       moved, advtext.ui.bare_view.block_kind

A case's INPUT is the block list as data. A text block's runs are the calls
the reference itself made to `_append_text` while building it, a code block's
source is what `_Converter._code_block` was handed, and a marker's text is what
`_marker_material` was handed -- each captured by wrapping the method in
memory. The script then rebuilds every block from that data with a cut-down
Theme and asserts that the rebuilt list composes to the same box as the
reference's own list under the reference's own Theme, at every width dumped.
"""

from __future__ import annotations

import argparse
import contextlib
import sys
from collections.abc import Callable, Iterator, Sequence
from dataclasses import dataclass, field, replace
from itertools import chain
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

import pinned_reference  # noqa: E402

pinned_reference.require()

import dump_core as core  # noqa: E402
import dump_para as para  # noqa: E402
from dump_core import (  # noqa: E402
    Records,
    emit_cmds,
    emit_measure,
    emit_node,
    emit_nodes,
    emit_run,
    emit_skip,
    q,
    read_cmds,
    read_measure,
    read_node,
    read_nodes,
    read_packed,
    read_skip,
    same_cmds,
    tok,
    unq,
)
from dump_para import HARDBREAK, Capture, Consulted, Event, build_block  # noqa: E402

from advtext import markdown, ui  # noqa: E402
from advtext.fontmetrics import Face  # noqa: E402
from advtext.nodes import Box, BoxKind, Glue, GlueSign, Kern, Node, Rule  # noqa: E402
from advtext.pack import IGNORE_DEPTH, CharDimens, vpack  # noqa: E402
from advtext.shipout import Point, ship  # noqa: E402
from advtext.style import (  # noqa: E402
    Align,
    BlockKind,
    BlockStyle,
    Faces,
    Spacing,
    Strut,
    Theme,
    fontlib_metrics,
    parse_font_key,
)
from advtext.ui import units  # noqa: E402
from advtext.ui.bare_view import block_kind  # noqa: E402
from advtext.units import GlueOrder, Scaled, sp_from_px  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OUT_DIR = ROOT / "tests" / "golden" / "blocks"
REFERENCE = pinned_reference.ROOT_DIR
FORMAT_VERSION = 1

# Very narrow, three ordinary columns, very wide: dump_para's.
WIDTHS_PX = para.WIDTHS_PX
MEASURE = para.MEASURE

Block = Any  # markdown.TextBlock | markdown.CodeBlock | markdown.RuleBlock


class Writer(core.Writer):
    """
    dump_core's writer, with this script's header and file id.
    """

    def __init__(self, name: str, about: str) -> None:
        super().__init__(name, about)
        self.lines = [
            "# Generated by tools/oracle/dump_blocks.py from the Python reference. Do not edit.",
            "# Record vocabulary: tools/oracle/FORMAT-blocks.md, over FORMAT-para.md and FORMAT.md.",
            *[f"# {line}" for line in about.strip().splitlines()],
            f"golden {FORMAT_VERSION} {q('blocks/' + name)}",
        ]


# --------------------------------------------------------------------------
# The INPUT: a document as data
# --------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class ThemeIn:
    """
    Every Theme value building and composing a block list reads.

    `list_indent` and `rule_thickness` are informational: by the time a block
    exists they are already in its `left_indent` and its `thickness`.
    """

    px_per_inch: float
    spacing: Spacing
    marker_gap: Scaled
    quote_indent: Scaled
    quote_rule_thickness: Scaled
    list_indent: Scaled
    rule_thickness: Scaled
    faces: tuple[str, str, str]

    @classmethod
    def of(cls, theme: Theme) -> ThemeIn:
        return cls(
            px_per_inch=float(theme.px_per_inch),
            spacing=theme.spacing,
            marker_gap=theme.marker_gap,
            quote_indent=theme.quote_indent,
            quote_rule_thickness=theme.quote_rule_thickness,
            list_indent=theme.list_indent,
            rule_thickness=theme.rule_thickness,
            faces=tuple(face.value for face in para.stand_in_faces(theme)),  # type: ignore[arg-type]
        )

    def cut_down(self, blocks: dict[BlockKind, BlockStyle]) -> Theme:
        """
        A Theme holding the recorded values and nothing else.

        Every field the records do not carry -- or carry for information only
        -- is zero, so a path that read one would not reproduce the answers.
        """
        assert self.faces[1] == Face.MATH.value
        return Theme(
            blocks=blocks,
            px_per_inch=self.px_per_inch,
            marker_gap=self.marker_gap,
            list_indent=0,
            quote_indent=self.quote_indent,
            rule_thickness=0,
            quote_rule_thickness=self.quote_rule_thickness,
            cell_padding=0,
            table_rule_thickness=0,
            spacing=self.spacing,
            faces=Faces(regular=Face(self.faces[0]), code=Face(self.faces[2])),
        )


@dataclass(frozen=True, slots=True)
class TextIn:
    kind: BlockKind
    style: BlockStyle
    left_indent: Scaled
    quote_depth: int
    events: tuple[Event, ...]
    marker: tuple[str, str] | None


@dataclass(frozen=True, slots=True)
class CodeIn:
    style: BlockStyle
    left_indent: Scaled
    source: str


@dataclass(frozen=True, slots=True)
class RuleIn:
    style: BlockStyle
    left_indent: Scaled
    thickness: Scaled


BlockIn = TextIn | CodeIn | RuleIn


def recorded_style(style: BlockStyle) -> BlockStyle:
    """
    A block's style as the records carry it: without its own `left_indent`.

    A block's `left_indent` field is absolute and already holds the style's;
    composition reads the field and never the style's.
    """
    return replace(style, left_indent=0)


def rebuild(theme_in: ThemeIn, block_in: BlockIn, resolve: Callable[[str], Any]) -> Block:
    """
    One block from its records alone, through the reference's own constructors.
    """
    if isinstance(block_in, TextIn):
        theme = theme_in.cut_down({block_in.kind: block_in.style})
        built = build_block(
            theme, resolve, block_in.kind, block_in.events, indent=block_in.left_indent, marker=block_in.marker
        )
        return replace(built, quote_depth=block_in.quote_depth)
    if isinstance(block_in, CodeIn):
        theme = theme_in.cut_down({BlockKind.CODE: block_in.style})
        converter = markdown._Converter(theme, resolve)
        converter._indent = block_in.left_indent
        return converter._code_block(block_in.source, "")
    return markdown.RuleBlock(style=block_in.style, thickness=block_in.thickness, left_indent=block_in.left_indent)


@dataclass(slots=True)
class BlockCapture:
    """
    dump_para's capture of `_append_text`, plus markers and code sources.
    """

    runs: Capture = field(default_factory=Capture)
    markers: list[tuple[tuple[Node, ...], tuple[str, str]]] = field(default_factory=list)
    codes: list[tuple[Any, str]] = field(default_factory=list)

    @contextlib.contextmanager
    def installed(self) -> Iterator[BlockCapture]:
        converter = markdown._Converter
        marker_material = converter._marker_material
        code_block = converter._code_block

        def recording_marker(conv: Any, text: str, kind: BlockKind | None = None) -> tuple[Node, ...]:
            nodes = marker_material(conv, text, kind)
            # The one `_append_text` call `_marker_material` makes is the
            # last one the run capture saw.
            last = self.runs._last
            assert last is not None and last[-1][0] == "run" and last[-1][3] == text and last[-1][2] is False
            self.markers.append((nodes, (last[-1][1], text)))
            return nodes

        def recording_code(conv: Any, source: str, language: str) -> Any:
            block = code_block(conv, source, language)
            self.codes.append((block, source))
            return block

        with self.runs.installed():
            converter._marker_material = recording_marker  # type: ignore[method-assign]
            converter._code_block = recording_code  # type: ignore[method-assign]
            try:
                yield self
            finally:
                converter._marker_material = marker_material  # type: ignore[method-assign]
                converter._code_block = code_block  # type: ignore[method-assign]

    def input_of(self, block: Block) -> BlockIn:
        style = recorded_style(block.style)
        if isinstance(block, markdown.TextBlock):
            (found,) = [entry for entry in self.runs.blocks if entry[0] is block]
            marker = None
            if block.marker:
                (marker,) = [text for nodes, text in self.markers if nodes is block.marker]
            assert not block.formulas, "formulas are not block-layer input"
            return TextIn(block.kind, style, block.left_indent, block.quote_depth, tuple(found[1]), marker)
        if isinstance(block, markdown.CodeBlock):
            assert type(block) is markdown.CodeBlock
            (source,) = [text for made, text in self.codes if made is block]
            return CodeIn(style, block.left_indent, source)
        assert isinstance(block, markdown.RuleBlock)
        return RuleIn(style, block.left_indent, block.thickness)


def normalised(block: Block) -> Block:
    """
    A reference block with what the records leave out removed.
    """
    style = recorded_style(block.style)
    if isinstance(block, markdown.TextBlock):
        return replace(block, style=style, links=())
    if isinstance(block, markdown.CodeBlock):
        return replace(block, style=style, language="")
    return replace(block, style=style)


# --------------------------------------------------------------------------
# Emitting the INPUT
# --------------------------------------------------------------------------


def emit_style(w: Writer, style: BlockStyle) -> None:
    emit_skip(w, style.interline)
    w.r("strut", style.strut.height, style.strut.depth)


def emit_inputs(w: Writer, theme_in: ThemeIn, inputs: Sequence[BlockIn], blocks: Sequence[Block], consulted: Consulted) -> None:
    w.r(
        "theme",
        theme_in.px_per_inch,
        theme_in.spacing,
        theme_in.marker_gap,
        theme_in.quote_indent,
        theme_in.quote_rule_thickness,
        theme_in.list_indent,
        theme_in.rule_thickness,
    )
    w.r("faces", *[q(face) for face in theme_in.faces])
    consulted.emit(w)
    lists: list[Sequence[Node]] = []
    for block in blocks:
        if isinstance(block, markdown.TextBlock):
            lists += [block.material, block.marker]
        elif isinstance(block, markdown.CodeBlock):
            lists += list(block.lines)
    emit_measure(w, MEASURE, *lists)
    w.r("blocks", len(blocks))
    for index, (block_in, block) in enumerate(zip(inputs, blocks, strict=True)):
        style = block_in.style
        if isinstance(block_in, TextIn):
            w.r(
                "text",
                index,
                block_in.kind,
                style.align,
                q(style.font.key),
                block_in.left_indent,
                style.first_line_indent,
                block_in.quote_depth,
                style.space_above,
                style.space_below,
            )
            emit_style(w, style)
            w.r("runs", len(block_in.events))
            for event in block_in.events:
                if event[0] == "run":
                    w.r("run", q(event[1]), event[2], q(event[3]))
                else:
                    w.r("hardbreak")
            w.r("endruns")
            if block_in.marker is not None:
                w.r("marker", q(block_in.marker[0]), q(block_in.marker[1]))
            emit_nodes(w, "material", block.material)
            if block_in.marker is not None:
                emit_nodes(w, "marker", block.marker)
            if style.align is not Align.JUSTIFY:
                emit_nodes(w, "unshrinkable", ui._unshrinkable(block.material))
            w.r("endtext")
        elif isinstance(block_in, CodeIn):
            w.r(
                "code",
                index,
                q(style.font.key),
                block_in.left_indent,
                style.space_above,
                style.space_below,
                len(block.lines),
                q(block_in.source),
            )
            emit_style(w, style)
            for line in block.lines:
                emit_nodes(w, "line", line)
            w.r("endcode")
        else:
            w.r("rule", index, block_in.left_indent, block_in.thickness, style.space_above, style.space_below)
            emit_style(w, style)
            w.r("endrule")
    w.r("endblocks")


# --------------------------------------------------------------------------
# Emitting the EXPECTED: one `compose` group per width
# --------------------------------------------------------------------------


def box_header(box: Box) -> tuple[object, ...]:
    return (box.width, box.height, box.depth, box.shift_amount, float(box.glue_set), box.glue_sign, box.glue_order)


@contextlib.contextmanager
def appends_observed() -> Iterator[list[tuple[Any, Scaled, Scaled]]]:
    """
    Every `append_to_vlist` call `advtext.ui` makes: the item, the
    `prev_depth` it was handed, the `prev_depth` it returned.
    """
    log: list[tuple[Any, Scaled, Scaled]] = []
    original = ui.append_to_vlist

    def recording(vlist: list[Node], item: Any, prev_depth: Scaled, skip: Any) -> Scaled:
        out = original(vlist, item, prev_depth, skip)
        log.append((item, prev_depth, out))
        return out

    ui.append_to_vlist = recording  # type: ignore[assignment]
    try:
        yield log
    finally:
        ui.append_to_vlist = original  # type: ignore[assignment]


def emit_lines(
    w: Writer, name: str, index: int, block: Block, theme: Theme, width: Scaled, measure: Any
) -> tuple[Box, ...]:
    """
    One block alone: the boxes `_block_boxes` gives `_stacked` for it.
    """
    params = ui.PROSE_BREAKING
    strutted = ui._block_boxes(block, theme, width, measure, params)
    strut = block.style.strut
    if isinstance(block, markdown.TextBlock):
        w.r("lines", index, "text", len(strutted))
        list_name, material, shape, line_params = core._ui_run(block, width, params)
        paragraph = emit_run(w, f"{name}/{index}", list_name, material, shape, line_params, measure, None)
        lines = ui._text_boxes(block, theme, width, measure, params)
        broken = list(paragraph.boxes)
        assert len(lines) == len(broken) == len(strutted)
        if block.marker:
            assert lines[1:] == broken[1:] and lines[0] != broken[0]
            w.r("hung")
            emit_node(w, lines[0])
        else:
            assert lines == broken, "advtext.ui._text_boxes disagrees with the recorded run"
        w.r("strutted", len(strutted))
        for line, set_line in zip(lines, strutted, strict=True):
            # The strut changes two numbers of the box and nothing else.
            assert set_line == replace(line, height=set_line.height, depth=set_line.depth)
            assert set_line.height == max(line.height, strut.height)
            assert set_line.depth == max(line.depth, strut.depth)
            w.r("dims", set_line.width, set_line.height, set_line.depth, set_line.shift_amount)
    elif isinstance(block, markdown.CodeBlock):
        w.r("lines", index, "code", len(strutted))
        assert len(strutted) == len(block.lines)
        for box in strutted:
            assert box.shift_amount == block.left_indent
            emit_node(w, box)
    else:
        w.r("lines", index, "rule", len(strutted))
        (box,) = strutted
        assert box == ui._rule_box(block, width, measure)
        emit_node(w, box)
    w.r("endlines")
    return strutted


def emit_stack(
    w: Writer,
    blocks: tuple[Block, ...],
    depth: int,
    vlist: Sequence[Node],
    number: Callable[[Block], int],
    lines: Sequence[tuple[Box, ...]],
    log: list[tuple[Any, Scaled, Scaled]],
) -> None:
    """
    One `_stacked` result, item by item, with why each item is there.

    The items are read off the list the reference built; nothing is computed
    here but the sum the gap kern is asserted against.
    """
    runs = ui._runs(blocks, depth)
    w.r("stack", depth, len(runs))
    at = 0
    above_run: tuple[Block, ...] | None = None
    for run in runs:
        barred = ui._quote_depth(run[0]) > depth
        w.r("srun", number(run[0]), len(run), barred)
        if above_run is not None:
            below, above = above_run[-1].style.space_below, run[0].style.space_above
            if at == 0:
                # Nothing is in the list yet: every run so far set no box.
                w.r("gap", below, above, 0, "list-empty")
            else:
                assert below + above == ui.run_gap(above_run, run)
                w.r("gap", below, above, below + above, "sum")
                if below + above:
                    assert vlist[at] == Kern(width=below + above)
                    emit_node(w, vlist[at])
                    at += 1
        expected: Sequence[Box | None] = (None,) if barred else lines[number(run[0])]
        for line, want in enumerate(expected):
            glue = None
            if isinstance(vlist[at], Glue):
                glue = vlist[at]
                at += 1
            box = vlist[at]
            at += 1
            assert isinstance(box, Box)
            (entry,) = [found for found in log if found[0] is box]
            w.r("append", entry[1], entry[2])
            if glue is not None:
                emit_node(w, glue)
            if want is not None:
                assert box == want
                w.r("ref", number(run[0]), line)
                continue
            kern_in, rule, kern_back, inner = box.contents
            assert isinstance(kern_in, Kern) and isinstance(rule, Rule) and isinstance(kern_back, Kern)
            assert isinstance(inner, Box) and inner.kind is BoxKind.VLIST and box.kind is BoxKind.HLIST
            w.r("barred", *box_header(box))
            emit_node(w, kern_in)
            emit_node(w, rule)
            emit_node(w, kern_back)
            w.r("inner", *box_header(inner))
            emit_stack(w, run, depth + 1, inner.contents, number, lines, log)
            w.r("endbarred")
        w.r("endsrun")
        above_run = run
    assert at == len(vlist)
    w.r("endstack")


def emit_compose(w: Writer, name: str, blocks: tuple[Block, ...], theme: Theme, width: Scaled, measure: Any) -> Box:
    """
    The whole document at one width: each block alone, then the stacking.
    """
    params = ui.PROSE_BREAKING
    w.r("compose", q(name), width)
    lines = [emit_lines(w, name, index, block, theme, width, measure) for index, block in enumerate(blocks)]

    def number(block: Block) -> int:
        (found,) = [index for index, candidate in enumerate(blocks) if candidate is block]
        return found

    extents: list[tuple[Scaled, Scaled]] = []
    with appends_observed() as log:
        vlist = ui._stacked(blocks, theme, width, measure, params, depth=0, memo=None, extents=extents)
    emit_stack(w, blocks, 0, vlist, number, lines, log)
    assert len(extents) == len(blocks)
    w.r("extents", len(extents), *[value for extent in extents for value in extent])

    vbox = ui.compose_blocks(blocks, theme, width, measure)
    assert vbox == vpack(vlist).box
    origin = Point(0, vbox.height)
    w.r("vbox", vbox.width, vbox.height, vbox.depth)
    w.r("origin", origin.x, origin.y)
    cmds = ship(vbox, origin, measure=measure)
    emit_cmds(w, cmds)

    # The units the reference draws by, each with one ink; a quote's later
    # blocks take theirs by index range.
    composer = ui.RunComposer(theme, width, measure)
    runs = ui.block_runs(blocks)
    w.r("units", len(runs))
    placed: list[Any] = []
    cmd_at = index_at = 0
    for run in runs:
        unit = units.ship_unit(composer.compose(run), measure)
        first = number(run[0])
        top = extents[first][0]
        placed.append(units.moved(unit.cmds, top, index_at))
        w.r("unit", first, len(run), top, cmd_at, cmd_at + len(unit.cmds), index_at, index_at + unit.count)
        for offset, (block, span) in enumerate(zip(run, unit.spans, strict=True)):
            w.r("span", first + offset, index_at + span[0], index_at + span[1], block_kind(block))
        cmd_at += len(unit.cmds)
        index_at += unit.count
    assert same_cmds(list(chain.from_iterable(placed)), cmds), "the units do not tile the document's commands"
    w.r("endcompose")
    return vbox


# --------------------------------------------------------------------------
# One case
# --------------------------------------------------------------------------


def widths_of(theme: Theme, widths_px: Sequence[float]) -> list[tuple[str, Scaled]]:
    return [(f"ui-{px:g}px", sp_from_px(px, theme.px_per_inch)) for px in widths_px]


def emit_case(
    w: Writer,
    name: str,
    source_kind: str,
    source: str,
    theme: Theme,
    make: Callable[[], Sequence[Block]],
    *,
    widths_px: Sequence[float] = WIDTHS_PX,
) -> None:
    """
    One document: its blocks as data, then its composition at each width.

    `make` builds the reference's blocks through its public entries. Tables
    are dropped from what it returns (D32.4 leaves them for later); every
    other block is recorded, rebuilt from the record, and the rebuilt list is
    what the expectations are computed from -- after asserting, per width,
    that it composes to exactly what the reference's own list does.
    """
    capture = BlockCapture()
    with capture.installed():
        originals = tuple(block for block in make() if not isinstance(block, markdown.TableBlock))
    theme_in = ThemeIn.of(theme)
    inputs = [capture.input_of(block) for block in originals]
    consulted = Consulted()
    blocks = tuple(rebuild(theme_in, block_in, consulted.resolve) for block_in in inputs)
    for block, original in zip(blocks, originals, strict=True):
        assert block == normalised(original), f"{name}: the records do not rebuild the block"
    cut = theme_in.cut_down({})

    w.case(name, "blocks")
    w.r("source", source_kind, q(source))
    emit_inputs(w, theme_in, inputs, blocks, consulted)
    for run_name, width in widths_of(theme, widths_px):
        vbox = emit_compose(w, run_name, blocks, cut, width, MEASURE)
        assert vbox == ui.compose_blocks(originals, theme, width, MEASURE), f"{name}: {run_name}"
    w.end()


def md_case(w: Writer, name: str, source: str, theme: Theme, *, widths_px: Sequence[float] = WIDTHS_PX) -> None:
    emit_case(
        w,
        name,
        "markdown",
        source,
        theme,
        lambda: markdown.build_document(source, theme, fontlib_metrics).blocks,
        widths_px=widths_px,
    )


def demo_case(w: Writer, name: str, file: str, theme: Theme, widths_px: Sequence[float]) -> None:
    source = (REFERENCE / file).read_text(encoding="utf-8")
    emit_case(
        w,
        name,
        "demo-file",
        file,
        theme,
        lambda: markdown.build_document(source, theme, fontlib_metrics).blocks,
        widths_px=widths_px,
    )


def built_case(
    w: Writer, name: str, theme: Theme, make: Callable[[], Sequence[Block]], *, widths_px: Sequence[float] = WIDTHS_PX
) -> None:
    emit_case(w, name, "built", "", theme, make, widths_px=widths_px)


def tag(theme: Theme, px: float) -> str:
    return f"{px:g}px-{theme.style(BlockKind.PARAGRAPH).align.name.lower()}"


def restyled(theme: Theme, **by_kind: dict[str, Any]) -> Theme:
    """
    `theme` with fields of some block styles replaced, keyed by BlockKind name.
    """
    blocks = dict(theme.blocks)
    for kind_name, changes in by_kind.items():
        kind = BlockKind[kind_name]
        blocks[kind] = replace(blocks[kind], **changes)
    return replace(theme, blocks=blocks)


THEMES: tuple[tuple[float, Align], ...] = ((16.0, Align.JUSTIFY), (13.0, Align.LEFT))
NARROW_TO_WIDE = (40.0, 240.0, 640.0)


def widths_at(px: float) -> Sequence[float]:
    """
    All five columns at the default body size, three at the other: the second
    size is there for its rounding, not for more line breaks.
    """
    return WIDTHS_PX if px == 16.0 else NARROW_TO_WIDE


# --------------------------------------------------------------------------
# The files
# --------------------------------------------------------------------------

TWO_PARAGRAPHS = (
    "The first paragraph is long enough to be set on more than one line at an ordinary width, "
    "so the space below it is measured from a last line.\n"
    "\n"
    "The second follows it. Nothing but a kern separates them."
)


def dump_paragraphs() -> Writer:
    w = Writer(
        "paragraphs",
        """
Two paragraphs and three, in every alignment, at two body sizes and at 144
pixels per inch: the gap between two blocks of the same kind, and the
prev_depth reset that keeps interline glue out of it.
""",
    )
    for px in (16.0, 13.0):
        for align in para.ALIGNS:
            theme = Theme.build(px, align=align)
            wide = align in (Align.JUSTIFY, Align.LEFT)
            md_case(w, f"two-paragraphs-{tag(theme, px)}", TWO_PARAGRAPHS, theme, widths_px=widths_at(px if wide else 13.0))
    theme = Theme.build(16.0, 144.0)
    md_case(w, "two-paragraphs-144ppi", TWO_PARAGRAPHS, theme, widths_px=NARROW_TO_WIDE)
    md_case(w, "three-one-line-paragraphs", "One.\n\nTwo.\n\nThree.", Theme.build(), widths_px=NARROW_TO_WIDE)
    md_case(w, "one-paragraph-alone", "Alone.", Theme.build(), widths_px=NARROW_TO_WIDE)
    md_case(
        w,
        "hard-break-then-paragraph",
        "A line ended by hand\\\nand its second line.\n\nThen another block.",
        Theme.build(),
        widths_px=NARROW_TO_WIDE,
    )
    return w


HEADINGS = "".join(
    f"{'#' * level} Heading level {level}\n\nText under the level {level} heading, set as a paragraph.\n\n"
    for level in range(1, 7)
)


def dump_headings() -> Writer:
    w = Writer(
        "headings",
        """
Heading levels 1 to 6, each followed by text; headings one after another; a
heading last; a heading long enough to wrap; a setext heading.
""",
    )
    for px, align in THEMES:
        theme = Theme.build(px, align=align)
        md_case(w, f"levels-1-to-6-with-text-{tag(theme, px)}", HEADINGS, theme, widths_px=widths_at(px))
    theme = Theme.build()
    md_case(w, "headings-in-a-row", "# One\n\n## Two\n\n### Three\n\n#### Four\n\n##### Five\n\n###### Six\n", theme, widths_px=NARROW_TO_WIDE)
    md_case(w, "text-then-heading-last", "Some text first.\n\n## A heading that ends the document\n", theme, widths_px=NARROW_TO_WIDE)
    md_case(
        w,
        "long-heading-wraps",
        "# A first-level heading with enough words in it to need several lines\n\nBody.\n",
        theme,
        widths_px=NARROW_TO_WIDE,
    )
    md_case(w, "setext-and-emphasis", "Title *in italics*\n=====\n\nUnder `it`.\n", theme, widths_px=NARROW_TO_WIDE)
    return w


NESTED = (
    "1. First ordered item, with enough words to wrap at the ordinary widths.\n"
    "   - A bullet inside it.\n"
    "     1. An ordered item three deep.\n"
    "     2. And its sibling.\n"
    "   - A second bullet.\n"
    "2. Second ordered item.\n"
    "\n"
    "After the list.\n"
)


def dump_lists() -> Writer:
    w = Writer(
        "lists",
        """
List items: a paragraph then three items, lists nested three deep with
ordered and unordered markers, an item of two paragraphs, markers wider than
the gutter, an item that sets nothing.
""",
    )
    for px, align in THEMES:
        theme = Theme.build(px, align=align)
        md_case(
            w,
            f"paragraph-then-three-items-{tag(theme, px)}",
            "A paragraph before the list.\n\n- First item.\n- Second item, which is the long one of the three and wraps.\n- Third item.\n",
            theme,
            widths_px=widths_at(px),
        )
        md_case(w, f"nested-three-deep-{tag(theme, px)}", NESTED, theme, widths_px=widths_at(px))
    theme = Theme.build()
    md_case(
        w,
        "item-with-two-paragraphs",
        "- The item's first paragraph.\n\n  Its second paragraph, which has no marker.\n- The next item.\n\nAfter.\n",
        theme,
    )
    md_case(
        w,
        "ordered-from-9-wide-markers",
        "9. Nine.\n10. Ten, whose marker is wider.\n11. Eleven.\n\n98765432. A marker wider than the gutter.\n",
        theme,
        widths_px=NARROW_TO_WIDE,
    )
    md_case(
        w,
        "empty-item-and-item-holding-only-code",
        "- a\n-\n- c\n\n1. ```\n   only code\n   ```\n2. text\n",
        theme,
        widths_px=NARROW_TO_WIDE,
    )
    md_case(w, "tight-then-loose", "- a\n- b\n\n- c\n\n- d\n\npara\n", theme, widths_px=NARROW_TO_WIDE)
    md_case(w, "list-first-and-last", "* only\n* items\n", theme, widths_px=NARROW_TO_WIDE)
    return w


def dump_quotes() -> Writer:
    w = Writer(
        "quotes",
        """
Block quotes and their bar: a quote between paragraphs, a quote of two
paragraphs, a quote holding a list, a quote inside a quote, a quote inside a
list, and the blocks that interrupt a bar (code, a rule) because only a text
block records its quote depth.
""",
    )
    for px, align in THEMES:
        theme = Theme.build(px, align=align)
        md_case(
            w,
            f"quote-between-paragraphs-{tag(theme, px)}",
            "Before the quote.\n\n> A quotation long enough to wrap at the ordinary widths, with a bar beside it.\n\nAfter the quote.\n",
            theme,
            widths_px=widths_at(px),
        )
        md_case(
            w,
            f"quote-containing-a-list-{tag(theme, px)}",
            "> Quoted text introducing a list:\n>\n> - first quoted item\n> - second quoted item\n>   1. nested and numbered\n>\n> Quoted text after it.\n\nUnquoted.\n",
            theme,
            widths_px=widths_at(px),
        )
    theme = Theme.build()
    md_case(w, "quote-of-two-paragraphs", "> First quoted paragraph.\n>\n> Second quoted paragraph.\n", theme, widths_px=NARROW_TO_WIDE)
    md_case(
        w,
        "quote-in-a-quote",
        "> Outer.\n>\n> > Inner, quoted twice.\n> >\n> > Inner again.\n>\n> Outer again.\n\n> > Twice from the start.\n",
        theme,
        widths_px=NARROW_TO_WIDE,
    )
    md_case(w, "quote-in-a-list", "- An item.\n\n  > A quote inside the item.\n\n- Next item.\n", theme, widths_px=NARROW_TO_WIDE)
    md_case(
        w,
        "quote-with-heading",
        "> ## A heading in a quote\n>\n> And its text.\n",
        theme,
        widths_px=NARROW_TO_WIDE,
    )
    md_case(
        w,
        "bar-interrupted-by-code-and-rule",
        "> Above the code.\n>\n> ```\n> quoted code\n> ```\n>\n> Between.\n>\n> ---\n>\n> Below the rule.\n",
        theme,
        widths_px=NARROW_TO_WIDE,
    )
    md_case(w, "two-quotes-in-a-row", "> One.\n\n<!-- -->\n\n> Two.\n", theme, widths_px=NARROW_TO_WIDE)
    md_case(w, "twice-quoted-alone", "> > Quoted twice and nothing quoted once.\n", theme, widths_px=NARROW_TO_WIDE)
    return w


CODE = (
    "Before the code.\n"
    "\n"
    "```python\n"
    "def f(x):\n"
    "\treturn x  # a tab, expanded to the next multiple of four columns\n"
    "\n"
    "a_long_line = 'this line is longer than a narrow column and is never broken, so it overflows the measure'\n"
    "```\n"
    "\n"
    "After the code.\n"
)


def dump_code() -> Writer:
    w = Writer(
        "code",
        """
Code blocks: between paragraphs, with a tab, an interior blank line and a line
longer than the measure; characters the code face lacks; an indented block; a
block inside a list; consecutive blocks.
""",
    )
    for px, align in THEMES:
        theme = Theme.build(px, align=align)
        md_case(w, f"code-between-paragraphs-{tag(theme, px)}", CODE, theme, widths_px=widths_at(px))
    theme = Theme.build()
    md_case(
        w,
        "code-with-characters-the-face-lacks",
        "```\nplain\n☃ snowman é 中\nplain again\n```\n\nAfter.\n",
        theme,
        widths_px=NARROW_TO_WIDE,
    )
    md_case(w, "indented-code", "Text.\n\n    indented code\n      and more\n\nText.\n", theme, widths_px=NARROW_TO_WIDE)
    md_case(w, "code-in-a-list", "- An item.\n\n  ```\n  code in the item\n  ```\n\n- Next.\n", theme, widths_px=NARROW_TO_WIDE)
    md_case(w, "two-code-blocks", "```\none\n```\n\n```\ntwo\nlines\n```\n", theme, widths_px=NARROW_TO_WIDE)
    md_case(w, "code-alone", "```\nalone\n```\n", theme, widths_px=NARROW_TO_WIDE)
    built_case(
        w,
        "code-block-entry-with-crlf",
        theme,
        lambda: (markdown.code_block("a\r\nb\rc\n\n\n", theme, fontlib_metrics, indent=theme.list_indent),),
        widths_px=NARROW_TO_WIDE,
    )
    return w


def dump_rules() -> Writer:
    w = Writer(
        "rules",
        """
Thematic rules: between paragraphs, first, last, two in a row, alone, and
inside lists, where the rule starts at the enclosing indent and is narrower
than the measure -- or, in a column narrower than the indent, of negative
width.
""",
    )
    for px, align in THEMES:
        theme = Theme.build(px, align=align)
        md_case(w, f"rule-between-paragraphs-{tag(theme, px)}", "Above the rule.\n\n---\n\nBelow the rule.\n", theme, widths_px=widths_at(px))
    theme = Theme.build()
    md_case(w, "rule-first-and-last", "---\n\nBetween two rules.\n\n---\n", theme, widths_px=NARROW_TO_WIDE)
    md_case(w, "two-rules-in-a-row", "Above.\n\n---\n\n---\n\nBelow.\n", theme, widths_px=NARROW_TO_WIDE)
    md_case(w, "rule-alone", "---\n", theme, widths_px=NARROW_TO_WIDE)
    md_case(
        w,
        "rule-in-nested-lists",
        "- An item.\n\n  ---\n\n  - Nested.\n\n    ---\n\n- Next.\n",
        theme,
        widths_px=(40.0, 60.0, 240.0, 640.0),
    )
    md_case(w, "rule-after-heading-before-code", "## Heading\n\n---\n\n```\ncode\n```\n", theme, widths_px=NARROW_TO_WIDE)
    return w


def dump_edges() -> Writer:
    w = Writer(
        "edges",
        """
The edges of the stacking rule: no blocks at all; blocks that set no box (an
empty code block) first, in the middle, last and alone; an empty text block;
styles with no space above or below, so no kern is written; a negative gap;
space above the first block and below the last, which is not applied; a first
line indent.
""",
    )
    theme = Theme.build()
    md_case(w, "no-blocks", "", theme, widths_px=(240.0,))
    md_case(w, "empty-code-first", "```\n```\n\nAfter the empty block.\n", theme, widths_px=NARROW_TO_WIDE)
    md_case(w, "empty-code-in-the-middle", "Before.\n\n```\n```\n\nAfter.\n", theme, widths_px=NARROW_TO_WIDE)
    md_case(w, "empty-code-last", "Before.\n\n```\n```\n", theme, widths_px=NARROW_TO_WIDE)
    md_case(w, "empty-code-alone", "```\n```\n", theme, widths_px=(240.0,))
    md_case(w, "two-empty-code-blocks-then-text", "```\n```\n\n```\n```\n\nText.\n", theme, widths_px=(240.0,))
    built_case(
        w,
        "empty-text-block-between-paragraphs",
        theme,
        lambda: (
            markdown.text_block("Before.", theme, fontlib_metrics),
            markdown.text_block("", theme, fontlib_metrics),
            markdown.text_block("After.", theme, fontlib_metrics),
        ),
        widths_px=NARROW_TO_WIDE,
    )
    built_case(w, "empty-text-block-alone", theme, lambda: (markdown.text_block("", theme, fontlib_metrics),), widths_px=(240.0,))
    built_case(
        w,
        "heading-first-and-quote-last-edge-space",
        theme,
        lambda: (
            markdown.text_block("Space above this is not applied", theme, fontlib_metrics, kind=BlockKind.HEADING_1),
            markdown.text_block("Body.", theme, fontlib_metrics),
            markdown.text_block(
                "Space below this is not applied.",
                theme,
                fontlib_metrics,
                kind=BlockKind.QUOTE,
                indent=theme.quote_indent,
                quote_depth=1,
            ),
        ),
        widths_px=NARROW_TO_WIDE,
    )
    flush = restyled(theme, PARAGRAPH={"space_below": 0})
    built_case(
        w,
        "no-space-between-paragraphs",
        flush,
        lambda: tuple(markdown.text_block(text, flush, fontlib_metrics) for text in ("No kern below.", "None above.", "Last.")),
        widths_px=NARROW_TO_WIDE,
    )
    negative = restyled(theme, PARAGRAPH={"space_below": 100000}, HEADING_2={"space_above": -300000})
    built_case(
        w,
        "negative-gap",
        negative,
        lambda: (
            markdown.text_block("A paragraph.", negative, fontlib_metrics),
            markdown.text_block("A heading pulled up", negative, fontlib_metrics, kind=BlockKind.HEADING_2),
            markdown.text_block("A paragraph.", negative, fontlib_metrics),
        ),
        widths_px=NARROW_TO_WIDE,
    )
    cancelling = restyled(theme, PARAGRAPH={"space_below": 200000}, QUOTE={"space_above": -200000})
    built_case(
        w,
        "gap-that-sums-to-zero",
        cancelling,
        lambda: (
            markdown.text_block("A paragraph.", cancelling, fontlib_metrics),
            markdown.text_block(
                "A quote.", cancelling, fontlib_metrics, kind=BlockKind.QUOTE, indent=cancelling.quote_indent, quote_depth=1
            ),
        ),
        widths_px=(240.0,),
    )
    indented = restyled(theme, PARAGRAPH={"first_line_indent": sp_from_px(20.0)})
    built_case(
        w,
        "first-line-indent",
        indented,
        lambda: tuple(
            markdown.text_block(text, indented, fontlib_metrics)
            for text in ("An indented first line, on a paragraph long enough to have a second.", "And another.")
        ),
        widths_px=NARROW_TO_WIDE,
    )
    built_case(
        w,
        "marker-on-a-paragraph-and-unquoted-quote-kind",
        theme,
        lambda: (
            markdown.text_block("An item built by hand.", theme, fontlib_metrics, kind=BlockKind.ORDERED_ITEM, marker="iv.", indent=theme.list_indent),
            markdown.text_block("A quote-kind block with no depth has no bar.", theme, fontlib_metrics, kind=BlockKind.QUOTE),
            markdown.text_block("Depth two with no enclosing indent.", theme, fontlib_metrics, kind=BlockKind.QUOTE, quote_depth=2),
        ),
        widths_px=NARROW_TO_WIDE,
    )
    return w


def dump_demo() -> Writer:
    w = Writer(
        "demo",
        """
The reference's demo documents as markdown.build_document built them, their
tables dropped: demo/sample.md justified at the default size and two widths,
demo/themes.md ragged right at 13 px and one.
""",
    )
    theme = Theme.build()
    demo_case(w, "sample-16px-justify", "demo/sample.md", theme, (240.0, 640.0))
    theme = Theme.build(13.0, align=Align.LEFT)
    demo_case(w, "themes-13px-left", "demo/themes.md", theme, (400.0,))
    return w


# --------------------------------------------------------------------------
# Verification
# --------------------------------------------------------------------------


def _read_style(records: Records, font: str, align: Align, above: Scaled, below: Scaled, first: Scaled) -> BlockStyle:
    interline = read_skip(records)
    row = records.take("strut")
    style = BlockStyle(
        font=parse_font_key(font),
        interline=interline,
        align=align,
        space_above=above,
        space_below=below,
        first_line_indent=first,
    )
    assert style.strut == Strut(int(row[1]), int(row[2]))
    return style


def read_inputs(records: Records) -> tuple[ThemeIn, list[BlockIn], para._Tables, Any, list[dict[str, Any]]]:
    """
    The INPUT records of one case, and the derived node lists written beside them.
    """
    row = records.take("theme")
    faces = tuple(unq(token) for token in records.take("faces")[1:])
    theme_in = ThemeIn(
        px_per_inch=float.fromhex(row[1]),
        spacing=para._token_enum(Spacing, row[2]),
        marker_gap=int(row[3]),
        quote_indent=int(row[4]),
        quote_rule_thickness=int(row[5]),
        list_indent=int(row[6]),
        rule_thickness=int(row[7]),
        faces=faces,  # type: ignore[arg-type]
    )
    tables = para._Tables()
    tables.read(records)
    measure = read_measure(records)
    if measure is None:
        measure = lambda node: CharDimens(0, 0, 0)  # noqa: E731 - a case with no characters
    inputs: list[BlockIn] = []
    derived: list[dict[str, Any]] = []
    count = int(records.take("blocks")[1])
    for index in range(count):
        row = records.take()
        assert int(row[1]) == index
        lists: dict[str, Any] = {}
        if row[0] == "text":
            kind, align = para._token_enum(BlockKind, row[2]), para._token_enum(Align, row[3])
            style = _read_style(records, unq(row[4]), align, int(row[8]), int(row[9]), int(row[6]))
            events: list[Event] = []
            for _ in range(int(records.take("runs")[1])):
                event = records.take()
                assert event[0] in ("run", "hardbreak")
                events.append(("run", unq(event[1]), event[2] == "1", unq(event[3])) if event[0] == "run" else HARDBREAK)
            records.take("endruns")
            marker = None
            if records.peek()[0] == "marker":
                found = records.take()
                marker = (unq(found[1]), unq(found[2]))
            while records.peek()[0] == "nodes":
                name, nodes = read_nodes(records)
                lists[name] = tuple(nodes)
            records.take("endtext")
            inputs.append(TextIn(kind, style, int(row[5]), int(row[7]), tuple(events), marker))
        elif row[0] == "code":
            style = _read_style(records, unq(row[2]), Align.LEFT, int(row[4]), int(row[5]), 0)
            code_lines = []
            for _ in range(int(row[6])):
                name, nodes = read_nodes(records)
                assert name == "line"
                code_lines.append(tuple(nodes))
            lists["lines"] = tuple(code_lines)
            records.take("endcode")
            inputs.append(CodeIn(style, int(row[3]), unq(row[7])))
        else:
            assert row[0] == "rule", row
            # A rule's style carries no font the path reads; any key will do.
            style = _read_style(records, "prose@0", Align.LEFT, int(row[4]), int(row[5]), 0)
            records.take("endrule")
            inputs.append(RuleIn(style, int(row[2]), int(row[3])))
        derived.append(lists)
    records.take("endblocks")
    return theme_in, inputs, tables, measure, derived


def read_lines(records: Records, index: int) -> tuple[Box, ...]:
    """
    One `lines` group, read as FORMAT-blocks.md states it: the boxes a block
    gives the stack, rebuilt from the records alone.
    """
    row = records.take("lines")
    assert int(row[1]) == index
    count = int(row[3])
    boxes: list[Box] = []
    if row[2] == "text":
        records.take("break")
        para._read_shape(records)
        para._read_params(records)
        paragraph = records.take("paragraph")
        assert int(paragraph[1]) == count
        records.take("breaks")
        boxes = [read_packed(records).box for _ in range(count)]
        records.take("endbreak")
        if records.peek()[0] == "hung":
            records.take()
            hung = read_node(records)
            assert isinstance(hung, Box)
            boxes[0] = hung
        assert int(records.take("strutted")[1]) == count
        for at in range(count):
            dims = records.take("dims")
            assert (boxes[at].width, boxes[at].shift_amount) == (int(dims[1]), int(dims[4]))
            boxes[at] = replace(boxes[at], height=int(dims[2]), depth=int(dims[3]))
    else:
        assert row[2] in ("code", "rule")
        for _ in range(count):
            box = read_node(records)
            assert isinstance(box, Box)
            boxes.append(box)
    records.take("endlines")
    return tuple(boxes)


def _read_box(row: Sequence[str], kind: BoxKind, contents: Sequence[Node]) -> Box:
    return Box(
        kind=kind,
        contents=tuple(contents),
        width=int(row[1]),
        height=int(row[2]),
        depth=int(row[3]),
        shift_amount=int(row[4]),
        glue_set=float.fromhex(row[5]),
        glue_sign=core._enum(GlueSign, row[6]),
        glue_order=core._enum(GlueOrder, row[7]),
    )


def read_stack(records: Records, depth: int, inputs: Sequence[BlockIn], lines: Sequence[tuple[Box, ...]]) -> list[Node]:
    """
    One `stack` group as the vertical list it describes, checking on the way
    every statement the records make about gaps and `prev_depth`.
    """
    row = records.take("stack")
    assert int(row[1]) == depth
    nodes: list[Node] = []
    last: int | None = None
    for _ in range(int(row[2])):
        run = records.take("srun")
        first, count, barred = int(run[1]), int(run[2]), run[3] == "1"
        if last is None:
            assert records.peek()[0] != "gap"
        else:
            gap = records.take("gap")
            below, above, amount = int(gap[1]), int(gap[2]), int(gap[3])
            assert (below, above) == (inputs[last].style.space_below, inputs[first].style.space_above)
            if gap[4] == "sum":
                assert nodes and amount == below + above
                if amount:
                    kern = read_node(records)
                    assert kern == Kern(width=amount)
                    nodes.append(kern)
            else:
                assert gap[4] == "list-empty" and not nodes and amount == 0
        appended = 0
        prev_depth = IGNORE_DEPTH  # every run starts a fresh rhythm
        while records.peek()[0] != "endsrun":
            append = records.take("append")
            assert int(append[1]) == prev_depth
            if records.peek()[0] == "glue":
                assert prev_depth != IGNORE_DEPTH
                nodes.append(read_node(records))
            if records.peek()[0] == "ref":
                ref = records.take()
                assert not barred and int(ref[1]) == first and int(ref[2]) == appended
                box = lines[first][appended]
            else:
                assert barred
                header = records.take("barred")
                kern_in, rule, kern_back = read_node(records), read_node(records), read_node(records)
                inner_header = records.take("inner")
                inner = _read_box(inner_header, BoxKind.VLIST, read_stack(records, depth + 1, inputs, lines))
                records.take("endbarred")
                box = _read_box(header, BoxKind.HLIST, (kern_in, rule, kern_back, inner))
            nodes.append(box)
            prev_depth = int(append[2])
            assert prev_depth == box.depth
            appended += 1
        records.take("endsrun")
        assert appended == (1 if barred else len(lines[first]))
        last = first + count - 1
    records.take("endstack")
    return nodes


def verify_blocks(records: Records) -> int:
    row = records.take("source")
    assert row[1] in ("markdown", "demo-file", "built")
    start = records.at
    theme_in, inputs, tables, measure, derived = read_inputs(records)
    blocks = tuple(rebuild(theme_in, block_in, tables.resolve) for block_in in inputs)
    for block, lists in zip(blocks, derived, strict=True):
        if isinstance(block, markdown.TextBlock):
            assert lists["material"] == block.material
            assert lists.get("marker", ()) == block.marker
            if block.style.align is not Align.JUSTIFY:
                assert lists["unshrinkable"] == ui._unshrinkable(block.material)
            else:
                assert "unshrinkable" not in lists
        elif isinstance(block, markdown.CodeBlock):
            assert lists["lines"] == block.lines
    theme = theme_in.cut_down({})

    # (1) The INPUT records, re-emitted from what was read, are the file's.
    again = Writer("verify", "")
    again.lines = []
    emit_inputs(again, theme_in, inputs, blocks, _as_consulted(tables))
    _same_rows(again, records, start)

    checks = 1
    while records.peek()[0] != "endcase":
        start = records.at
        row = records.take("compose")
        run_name, width = unq(row[1]), int(row[2])
        if run_name.startswith("ui-"):
            assert sp_from_px(float(run_name[3:-2]), theme_in.px_per_inch) == width

        # (2) The EXPECTED records alone give the document: read them with a
        # reader that calls nothing in advtext.ui.
        lines = [read_lines(records, index) for index in range(len(inputs))]
        vlist = read_stack(records, 0, inputs, lines)
        extents = [int(value) for value in records.take("extents")[2:]]
        vbox = vpack(vlist).box
        row = records.take("vbox")
        assert (vbox.width, vbox.height, vbox.depth) == tuple(int(value) for value in row[1:4])
        row = records.take("origin")
        origin = Point(int(row[1]), int(row[2]))
        assert origin == Point(0, vbox.height)
        cmds = read_cmds(records)
        assert same_cmds(ship(vbox, origin, measure=measure), cmds)
        cmd_at = index_at = 0
        for _ in range(int(records.take("units")[1])):
            unit = records.take("unit")
            first, count = int(unit[1]), int(unit[2])
            assert int(unit[3]) == extents[2 * first]
            assert (int(unit[4]), int(unit[6])) == (cmd_at, index_at)
            cmd_at, index_end = int(unit[5]), int(unit[7])
            for offset in range(count):
                span = records.take("span")
                assert int(span[1]) == first + offset and int(span[2]) == index_at
                assert span[4] == tok(block_kind(blocks[first + offset]))
                index_at = int(span[3])
            assert index_at == index_end
        assert cmd_at == len(cmds)
        records.take("endcompose")

        # (3) The reference, on the rebuilt blocks and the cut-down Theme,
        # writes the same records.
        assert vbox == ui.compose_blocks(blocks, theme, width, measure)
        again.lines = []
        emit_compose(again, run_name, blocks, theme, width, measure)
        _same_rows(again, records, start)
        checks += 1
    return checks


def _as_consulted(tables: para._Tables) -> Consulted:
    return Consulted(space=dict(tables.space), has=dict(tables.has), kern=dict(tables.kern))


def _same_rows(again: Writer, records: Records, start: int) -> None:
    rows = [line.split(" ") for line in again.lines]
    found = records.rows[start : records.at]
    assert len(rows) == len(found), (len(rows), len(found))
    for mine, theirs in zip(rows, found):
        assert mine == theirs, (mine, theirs)


def verify(path: Path) -> tuple[int, int]:
    text = path.read_bytes().decode("utf-8")
    assert text.endswith("\n") and "\r" not in text and "\t" not in text
    for line in text.split("\n"):
        assert not line.startswith(" ") and not line.endswith(" ") and "  " not in line, line
    records = Records(text)
    header = records.take("golden")
    assert int(header[1]) == FORMAT_VERSION and unq(header[2]) == "blocks/" + path.stem
    cases = checks = 0
    while not records.done():
        row = records.take("case")
        assert row[2] == "blocks"
        try:
            checks += verify_blocks(records)
        except Exception:
            print(f"{path.name}: case {unq(row[1])} failed near record {records.at}", file=sys.stderr)
            raise
        records.take("endcase")
        cases += 1
    return cases, checks


# --------------------------------------------------------------------------
# Entry point
# --------------------------------------------------------------------------

DUMPS: tuple[Callable[[], Writer], ...] = (
    dump_paragraphs,
    dump_headings,
    dump_lists,
    dump_quotes,
    dump_code,
    dump_rules,
    dump_edges,
    dump_demo,
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
