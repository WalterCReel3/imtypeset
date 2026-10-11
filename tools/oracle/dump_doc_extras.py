#!/usr/bin/env python3
"""
Dump document-level goldens from the Python reference (imgui-adv-text): the
behaviours above one paragraph and above the block stack, which have no golden
anywhere else and stop being recoverable once advtext runs on imtypeset.

Writes tests/golden/doc/*.golden, four groups:

    selection.golden    selection and copy ACROSS blocks (kind `doc`)
    decorations.golden  links, colour spans, syntax ink, find hits (kind `deco`)
    hosts.golden        a block reserving a rectangle (kind `host`)
    stream.golden       a document fed in chunks (kind `stream`), and the
                        settled-line property (kind `flips`)

The record vocabulary is tools/oracle/FORMAT-doc.md, over FORMAT.md,
FORMAT-para.md and FORMAT-blocks.md. dump_core.py, dump_blocks.py and
dump_draw.py are imported and not edited.

Run from the imtypeset repository root, with the reference's interpreter:

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=build/advtext-reference \\
        ../imgui-adv-text/.venv/bin/python -P -B tools/oracle/dump_doc_extras.py

    ... dump_doc_extras.py --verify   re-reads every golden; rebuilds each case
                                      from its INPUT records alone (the rows
                                      before `expect`), re-asks every recorded
                                      question (each `range`, `drag`, `probe`
                                      row's own fields, read back from the
                                      file) of the reference and compares
                                      every EXPECTED record.
    ... --only selection,hosts        write (or verify) some groups only.

The script only reads the reference. Output is deterministic: a second run is
byte-identical to the first.

Every expectation is an answer of one of the reference's own functions:

    blocks              advtext.markdown.build_document (math=markdown_math())
    layout              advtext.ui.bare_view.BareView.layout_for, which is
                        advtext.ui.units.layout_units; StreamView for streams
    selection           UnitModel.select_at / hit / word_at / line_at / rects,
                        UnitLayout.block_at, BareView.selected_blocks
    copy                UnitModel.copy_text, separators_within, atoms_within,
                        originals_within, _leads, edge_hosts; BareView.block_text
    index -> material   advtext.spans.map_text_block, map_code_block
    decorations         advtext.spans.find_spans, FindIndex, colour_text,
                        code_position; BareView.set_colours, _decorate,
                        span_rects; bare_view._inked, _role_inks, _overlaid;
                        advtext.backends.imgui_draw.draw_runs through
                        dump_draw's recording draw list
    host blocks         BareView.host_for / _hosted, advtext.ui._host_box,
                        run_gap, compose_blocks
    streaming           advtext.stream.StreamDocument, StreamView.sync,
                        LayoutStats, BareView.set_lines
    settled lines       tests/test_stream_stability.py's own _flips

Two things here are the script's and not the reference's, and both are
checked against it where they touch it:

  - the block's TEXT and the byte offset of every material node in it. The
    reference keeps no source offsets (advtext/spans.py says so); the text is
    the concatenation of what its converter was handed (`_append_text`,
    `_formula`, a hard break as a newline), captured by wrapping those in
    memory, and every character node is asserted to be the character at the
    offset recorded for it.
  - `digest`, an FNV-1a hash of a unit's commands, so that a stream's steps
    can say "these commands" without repeating them.
"""

from __future__ import annotations

import argparse
import contextlib
import importlib.util
import sys
from collections.abc import Callable, Iterator, Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

import pinned_reference  # noqa: E402

pinned_reference.require()

import dump_blocks as blk  # noqa: E402
import dump_core as core  # noqa: E402
from dump_core import Rng, emit_cmds, q, unq  # noqa: E402

from advtext import markdown, spans, ui  # noqa: E402
from advtext.backends.imgui_draw import Palette, rgba  # noqa: E402
from advtext.markdown import CodeBlock, Document, RuleBlock, TableBlock, TextBlock  # noqa: E402
from advtext.mathtext import markdown_math  # noqa: E402
from advtext.nodes import Box, CharNode, Glue  # noqa: E402
from advtext.selection import Granularity, Selection  # noqa: E402
from advtext.shipout import Atom, Glyph, Line, Point, RuleRect, SetGlue, ship  # noqa: E402
from advtext.stream import CommitPolicy, StreamDocument  # noqa: E402
from advtext.style import Align, BlockKind, Theme, char_measure, fontlib_metrics  # noqa: E402
from advtext.ui import HostBlock, Viewport  # noqa: E402
from advtext.ui import bare_view as bv  # noqa: E402
from advtext.ui import units as U  # noqa: E402
from advtext.ui.stream_view import StreamView  # noqa: E402
from advtext.units import px_from_sp, sp_from_px  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OUT_DIR = ROOT / "tests" / "golden" / "doc"
REFERENCE = pinned_reference.ROOT_DIR
FORMAT_VERSION = 1

# Patches applied to the reference in memory before dumping (D32.3). None.
REFERENCE_PATCHES: tuple[str, ...] = ()

MEASURE = char_measure(fontlib_metrics)
ALIGNS = {align.name.lower(): align for align in Align}
GRANULARITIES = (Granularity.CHARACTER, Granularity.WORD, Granularity.LINE)
EVERYWHERE = Viewport(top=-(1 << 40), bottom=1 << 40)

Row = list[str]


class Writer(core.Writer):
    def __init__(self, name: str, about: str) -> None:
        super().__init__(name, about)
        self.lines = [
            "# Generated by tools/oracle/dump_doc_extras.py from the Python reference. Do not edit.",
            "# Record vocabulary: tools/oracle/FORMAT-doc.md.",
            *[f"# {line}" for line in about.strip().splitlines()],
            f"golden {FORMAT_VERSION} {q('doc/' + name)}",
        ]


def row(*tokens: object) -> Row:
    return [core.tok(token) for token in tokens]


def rows_of(inputs: Sequence[Row], tag: str) -> list[Row]:
    return [r for r in inputs if r[0] == tag]


def one(inputs: Sequence[Row], tag: str) -> Row:
    (found,) = rows_of(inputs, tag)
    return found


def fl(token: str) -> float:
    return float.fromhex(token)


# --------------------------------------------------------------------------
# What the converter was handed: a block's text, and where each node is in it
# --------------------------------------------------------------------------


@dataclass(slots=True)
class Capture:
    """
    The converter's calls behind each block, by wrapping its methods in memory.
    """

    by_run: dict[int, list[tuple[Any, ...]]] = field(default_factory=dict)
    pinned: list[object] = field(default_factory=list)
    blocks: list[tuple[Any, list[tuple[Any, ...]]]] = field(default_factory=list)
    markers: list[tuple[Any, tuple[str, str]]] = field(default_factory=list)
    codes: list[tuple[Any, str]] = field(default_factory=list)
    last_text: tuple[str, str] = ("", "")
    pending: Any = None

    def add(self, run: Any, piece: tuple[Any, ...]) -> None:
        if id(run) not in self.by_run:
            self.by_run[id(run)] = []
            self.pinned.append(run)
        self.by_run[id(run)].append(piece)

    @contextlib.contextmanager
    def installed(self) -> Iterator[Capture]:
        conv = markdown._Converter
        saved = (conv._append_text, conv._formula, conv._text_block, conv._marker_material,
                 conv._code_block, markdown._unskip, markdown._fill_and_break)  # fmt: skip
        append_text, formula, text_block, marker_material, code_block, unskip, fill_and_break = saved

        def r_append(self_: Any, run: Any, text: str, font: Any, breakable: bool) -> None:
            start = len(run.nodes)
            append_text(self_, run, text, font, breakable)
            self.last_text = (font.key, text)
            self.add(run, ("run", text, start, len(run.nodes), font.key, breakable))

        def r_formula(self_: Any, run: Any, kind: Any, emphasis: Any, source: str, display: bool, breakable: bool) -> None:
            formula(self_, run, kind, emphasis, source, display, breakable)
            span = run.formulas[-1]
            if not span.refusal and span.end == span.start + 1 and isinstance(run.nodes[span.start], Box):
                # A refused formula went through `_append_text` as code and
                # is recorded there, as the run of text it was set as.
                self.add(run, ("formula", span.text, span.start, span.end, source, display))

        def r_unskip(run: Any, link_starts: Any) -> None:
            unskip(run, link_starts)
            self.pending = run

        def r_fill(align: Align) -> Any:
            nodes = fill_and_break(align)
            if self.pending is not None:
                run, self.pending = self.pending, None
                start = len(run.nodes)
                self.add(run, ("hardbreak", "\n", start, start + len(nodes), "", False))
            return nodes

        def r_text_block(self_: Any, kind: Any, run: Any) -> Any:
            block = text_block(self_, kind, run)
            self.blocks.append((block, list(self.by_run.get(id(run), []))))
            return block

        def r_marker(self_: Any, text: str, kind: Any = None) -> Any:
            nodes = marker_material(self_, text, kind)
            assert self.last_text[1] == text
            self.markers.append((nodes, self.last_text))
            return nodes

        def r_code(self_: Any, source: str, language: str) -> Any:
            block = code_block(self_, source, language)
            self.codes.append((block, source))
            return block

        conv._append_text, conv._formula, conv._text_block = r_append, r_formula, r_text_block  # type: ignore[method-assign]
        conv._marker_material, conv._code_block = r_marker, r_code  # type: ignore[method-assign]
        markdown._unskip, markdown._fill_and_break = r_unskip, r_fill
        try:
            yield self
        finally:
            (conv._append_text, conv._formula, conv._text_block, conv._marker_material,  # type: ignore[method-assign]
             conv._code_block, markdown._unskip, markdown._fill_and_break) = saved  # fmt: skip


@dataclass(slots=True)
class Content:
    """
    One block's text as a host would hand it over, and each material node's bytes in it.
    """

    text: str
    byte_of: dict[int, tuple[int, int]]  # material index -> (byte offset, byte length)
    pieces: list[tuple[Any, ...]]

    def span(self, start: int, end: int) -> tuple[int, int]:
        """The bytes of material `[start, end)`: first sourced node to last, or (-1, -1)."""
        found = [self.byte_of[i] for i in range(start, end) if i in self.byte_of]
        if not found:
            return (-1, -1)
        return (found[0][0], found[-1][0] + found[-1][1])


def _offsets(text: str) -> list[int]:
    out = [0]
    for character in text:
        out.append(out[-1] + len(character.encode("utf-8")))
    return out


def text_content(block: TextBlock, pieces: Sequence[tuple[Any, ...]]) -> Content:
    """
    The text of a text block and the bytes of each of its nodes.

    A piece is one call the converter made. Its nodes are read off against its
    text and every reading is asserted: a character node is the next character
    that is not white space, a glue is the white space at the cursor.
    """
    material = block.material
    offset = 1 if block.style.first_line_indent else 0
    assert not offset or isinstance(material[0], Box)
    text = "".join(piece[1] for piece in pieces)
    enc = _offsets(text)
    byte_of: dict[int, tuple[int, int]] = {}
    out: list[tuple[Any, ...]] = []
    at = 0
    for number, (kind, ptext, start, end, extra, flag) in enumerate(pieces):
        following = pieces[number + 1][2] if number + 1 < len(pieces) else len(material) - offset
        low, high = start + offset, min(end, following) + offset
        if kind == "run":
            cursor = 0
            for index in range(low, high):
                node = material[index]
                if isinstance(node, CharNode):
                    while ptext[cursor].isspace():
                        cursor += 1
                    assert spans._written(node) == ptext[cursor], (ptext, cursor, node)
                    byte_of[index] = (enc[at + cursor], enc[at + cursor + 1] - enc[at + cursor])
                    cursor += 1
                elif isinstance(node, Glue):
                    assert ptext[cursor].isspace(), (ptext, cursor)
                    opened = cursor
                    while cursor < len(ptext) and ptext[cursor].isspace():
                        cursor += 1
                    byte_of[index] = (enc[at + opened], enc[at + cursor] - enc[at + opened])
            assert not ptext[cursor:].strip(), (ptext, cursor)
        elif kind == "hardbreak":
            for index in range(low, high):
                if isinstance(material[index], Glue):
                    byte_of[index] = (enc[at], 1)
        else:
            assert high == low + 1 and isinstance(material[low], Box)
            byte_of[low] = (enc[at], enc[at + len(ptext)] - enc[at])
        out.append((kind, enc[at], enc[at + len(ptext)], low, high, extra, flag))
        at += len(ptext)
    return Content(text, byte_of, out)


def code_content(block: CodeBlock) -> Content:
    """A code block's text is `material_text`'s: its lines after tab expansion, newline-joined."""
    found = spans.material_text(block)
    enc = _offsets(found.text)
    byte_of = {
        flat: (enc[k], enc[k + 1] - enc[k]) for k, flat in enumerate(found.indices) if flat is not None
    }
    return Content(found.text, byte_of, [])


# --------------------------------------------------------------------------
# A document built from INPUT rows
# --------------------------------------------------------------------------


@dataclass(slots=True)
class Doc:
    theme: Theme
    document: Document
    capture: Capture
    contents: dict[int, Content]


def theme_of(inputs: Sequence[Row]) -> Theme:
    found = one(inputs, "themein")
    return Theme.build(fl(found[1]), align=ALIGNS[found[2]])


def build_doc(inputs: Sequence[Row]) -> Doc:
    theme = theme_of(inputs)
    md = one(inputs, "markdown")
    capture = Capture()
    with capture.installed():
        built = markdown.build_document(
            unq(md[3]), theme, fontlib_metrics, math=markdown_math() if md[1] == "1" else None
        )
    keep = [n for n, block in enumerate(built.blocks) if not (md[2] == "1" and isinstance(block, TableBlock))]
    document = Document(
        blocks=tuple(built.blocks[n] for n in keep), sources=tuple(built.sources[n] for n in keep)
    )
    contents: dict[int, Content] = {}
    for number, block in enumerate(document.blocks):
        if isinstance(block, TextBlock):
            found = [p for made, p in capture.blocks if made is block]
            if not found:
                # A display formula's block is constructed where it is parsed
                # and not by `_text_block`: its run is the one holding exactly
                # its nodes.
                found = [
                    capture.by_run[id(run)]
                    for run in capture.pinned
                    if len(run.nodes) == len(block.material)
                    and all(a is b for a, b in zip(run.nodes, block.material, strict=True))
                ]
            (pieces,) = found
            contents[number] = text_content(block, pieces)
        elif isinstance(block, CodeBlock):
            contents[number] = code_content(block)
    return Doc(theme, document, capture, contents)


def emit_theme(w: Writer, doc: Doc) -> None:
    theme = doc.theme
    theme_in = blk.ThemeIn.of(theme)
    w.r("theme", theme_in.px_per_inch, theme_in.spacing, theme_in.marker_gap, theme_in.quote_indent,
        theme_in.quote_rule_thickness, theme_in.list_indent, theme_in.rule_thickness)  # fmt: skip
    w.r("faces", *[q(face) for face in theme_in.faces])
    w.r("decor", theme.table_rule_thickness)


def emit_pieces(w: Writer, content: Content) -> None:
    for kind, b0, b1, m0, m1, extra, flag in content.pieces:
        if kind == "run":
            w.r("piece", "run", b0, b1, m0, m1, q(extra), bool(flag))
        elif kind == "hardbreak":
            w.r("piece", "hardbreak", b0, b1, m0, m1)
        else:
            w.r("piece", "formula", b0, b1, m0, m1, bool(flag), q(extra))


def emit_spans(w: Writer, content: Content, links: Sequence[Any], formulas: Sequence[Any]) -> None:
    for index, link in enumerate(links):
        b0, b1 = content.span(link.start, link.end)
        w.r("link", index, b0, b1, link.start, link.end, q(link.target))
    for index, formula in enumerate(formulas):
        b0, b1 = content.span(formula.start, formula.end)
        w.r("formula", index, b0, b1, formula.start, formula.end, formula.display,
            bool(formula.refusal), q(formula.text))  # fmt: skip


def emit_block(w: Writer, doc: Doc, number: int, block: Any) -> None:
    """
    One text, code or rule block in host-neutral terms: FORMAT-blocks.md's headers, plus text and pieces.
    """
    theme = doc.theme
    style = block.style
    quoted = block.quote_depth if isinstance(block, TextBlock) else 0
    depth = U._list_depth(block, quoted, theme.list_indent, theme.quote_indent)
    if isinstance(block, TextBlock):
        content = doc.contents[number]
        w.r("text", number, block.kind, style.align, q(style.font.key), block.left_indent,
            style.first_line_indent, block.quote_depth, style.space_above, style.space_below)  # fmt: skip
        blk.emit_style(w, style)
        if block.marker:
            (marker,) = [text for nodes, text in doc.capture.markers if nodes is block.marker]
            w.r("marker", q(marker[0]), q(marker[1]), U._numbered(block))
        w.r("nest", -1 if depth is None else depth)
        w.r("content", len(content.text.encode("utf-8")), len(block.material), q(content.text))
        emit_pieces(w, content)
        emit_spans(w, content, block.links, block.formulas)
        w.r("endtext")
    elif isinstance(block, CodeBlock):
        content = doc.contents[number]
        (source,) = [text for made, text in doc.capture.codes if made is block]
        w.r("code", number, q(style.font.key), block.left_indent, style.space_above,
            style.space_below, len(block.lines), q(source))  # fmt: skip
        blk.emit_style(w, style)
        w.r("nest", -1 if depth is None else depth)
        w.r("language", q(block.language))
        w.r("content", len(content.text.encode("utf-8")), sum(map(len, block.lines)), q(content.text))
        w.r("endcode")
    else:
        assert isinstance(block, RuleBlock)
        w.r("rule", number, block.left_indent, block.thickness, style.space_above, style.space_below)
        blk.emit_style(w, style)
        w.r("nest", -1 if depth is None else depth)
        w.r("endrule")


def emit_blocks(w: Writer, doc: Doc) -> None:
    emit_theme(w, doc)
    blocks = doc.document.blocks
    w.r("blocks", len(blocks))
    for number, block in enumerate(blocks):
        emit_block(w, doc, number, block)
    w.r("endblocks")


# --------------------------------------------------------------------------
# A layout, and reading-order indices as (block, byte offset)
# --------------------------------------------------------------------------


@dataclass(slots=True)
class IndexMap:
    owner: list[int]  # reading-order index -> block number
    where: dict[int, tuple[int, int]]  # index -> (byte offset, byte length) in its block's text
    unmapped: list[int]

    def pos(self, index: int) -> tuple[int, int]:
        """(block, byte) of the item at `index`; byte -1 for an item the text does not hold."""
        if not 0 <= index < len(self.owner):
            return (-1, -1)
        found = self.where.get(index)
        return (self.owner[index], -1 if found is None else found[0])

    def span(self, start: int, end: int) -> tuple[int, int, int, int]:
        """
        `[start, end)` as (block, byte) of its first sourced item and (block,
        byte end) of its last; four -1 when it holds none.
        """
        low, high = max(start, 0), min(end, len(self.owner))
        first = next((i for i in range(low, high) if i in self.where), None)
        if first is None:
            return (-1, -1, -1, -1)
        last = next(i for i in range(high - 1, low - 1, -1) if i in self.where)
        return (self.owner[first], self.where[first][0], self.owner[last], sum(self.where[last]))


def index_map(layout: U.UnitLayout, contents: dict[int, Content]) -> IndexMap:
    owner = [-1] * layout.length
    where: dict[int, tuple[int, int]] = {}
    unmapped: list[int] = []
    for unit in layout.placed:
        for number, block in zip(unit.block_range, unit.blocks, strict=True):
            own = unit.indices_of(number)
            for index in own:
                owner[index] = number
            content = contents.get(number)
            if content is None or isinstance(block, HostBlock):
                continue
            try:
                if isinstance(block, TextBlock):
                    if len(unit.blocks) == 1:
                        mapped = spans.map_text_block(block, unit.unit.cmds)
                    else:
                        start, stop = unit.unit.spans[number - unit.block_start]
                        mapped = spans.map_text_block(block, unit.unit.cmds, start=start, stop=stop)
                    base, skipped = unit.base + mapped.base, 0
                elif isinstance(block, CodeBlock):
                    mapped = spans.map_code_block(block, base=own.start)
                    base, skipped = mapped.base, unit.skipped if unit.part is not None else 0
                else:
                    continue
            except spans.SpanMapError:
                unmapped.append(number)
                continue
            for material_index, local in enumerate(mapped.shipped):
                found = content.byte_of.get(material_index + skipped)
                if local is None or found is None:
                    continue
                for k in range(mapped.extent[material_index]):
                    assert owner[base + local + k] == number
                    where[base + local + k] = found
    return IndexMap(owner, where, unmapped)


def whole_layout(view: bv.BareView, width: int) -> U.UnitLayout:
    layout = view.layout_for(width)
    return layout


def emit_units(w: Writer, view: bv.BareView, layout: U.UnitLayout) -> None:
    w.r("size", layout.height, layout.length, len(layout.placed))
    for unit in layout.placed:
        w.r("unit", unit.block_start, len(unit.blocks), -1 if unit.part is None else unit.part,
            unit.y, unit.height, unit.base, unit.unit.count, unit.unit.lines)  # fmt: skip
    for number in range(len(view.document.blocks)):
        own = layout.indices_of_block(number)
        rect = view.block_rect(number)
        assert rect is not None
        w.r("bspan", number, own.start, own.stop, rect.x, rect.y, rect.width, rect.height)


def emit_imap(w: Writer, view: bv.BareView, layout: U.UnitLayout, im: IndexMap) -> None:
    for number in range(len(view.document.blocks)):
        own = layout.indices_of_block(number)
        tokens: list[object] = []
        for index in own:
            found = im.where.get(index)
            tokens += [-1, 0] if found is None else [found[0], found[1]]
        w.r("imap", number, own.start, len(own), *tokens)
    w.r("unmapped", len(im.unmapped), *im.unmapped)


def emit_leads(w: Writer, layout: U.UnitLayout) -> None:
    for unit, leads in zip(layout.placed, layout.model._leads, strict=True):
        for offset, (lead, indent) in enumerate(leads):
            w.r("lead", unit.block_start + offset, lead is not None, q(lead or ""), q(indent))


def emit_copy_maps(w: Writer, layout: U.UnitLayout, im: IndexMap) -> None:
    model = layout.model
    for tag, found in (
        ("sep", model.separators_within(0, layout.length)),
        ("atomtext", model.atoms_within(0, layout.length)),
        ("original", model.originals_within(0, layout.length)),
    ):
        w.r(tag + "s", len(found))
        for index in sorted(found):
            w.r(tag, index, *im.pos(index), q(found[index]))


# --------------------------------------------------------------------------
# Group 1: selection and copy across blocks
# --------------------------------------------------------------------------


def thinned(items: list[Any], limit: int) -> list[Any]:
    seen: set[Any] = set()
    unique = [item for item in items if not (item in seen or seen.add(item))]
    if len(unique) <= limit:
        return unique
    return [unique[(k * len(unique)) // limit] for k in range(limit)]


def fragments_by_block(layout: U.UnitLayout) -> dict[int, list[Any]]:
    out: dict[int, list[Any]] = {}
    for unit in layout.placed:
        if not unit.unit.lines:
            continue
        starts = [unit.base + first for first, _ in unit.unit.spans]
        for fragment in unit.model.fragments:
            position = max(i for i, start in enumerate(starts) if start <= fragment.start or i == 0)
            out.setdefault(unit.block_start + position, []).append(fragment)
    return out


def choose_queries(layout: U.UnitLayout, nblocks: int, seed: int) -> dict[str, list[tuple[Any, ...]]]:
    rng = Rng(seed)
    length = layout.length
    owned = [(n, layout.indices_of_block(n)) for n in range(nblocks)]
    full = [(n, own) for n, own in owned if len(own)]
    ranges: list[tuple[int, int]] = [(0, length)]
    for n, own in full:
        ranges.append((own.start, own.stop))
    for (_, a), (_, b) in zip(full, full[1:], strict=False):
        mid_a, mid_b = (a.start + a.stop) // 2, (b.start + b.stop) // 2
        ranges += [(mid_a, mid_b), (a.stop - 1, b.start + 1), (a.start, b.start), (a.stop, b.stop), (a.stop, b.start)]
    for (_, a), (_, c) in zip(full, full[2:], strict=False):
        ranges.append(((a.start + a.stop) // 2, (c.start + c.stop) // 2))
    for _ in range(12):
        low = rng.below(length + 1)
        ranges.append((low, rng.between(low, length)))

    by_block = fragments_by_block(layout)
    blocks = sorted(by_block)
    drags: list[tuple[Any, ...]] = []
    probes: list[tuple[int, int]] = []
    for upper, lower in zip(blocks, blocks[1:], strict=False):
        a, b = by_block[upper][-1], by_block[lower][0]
        pa = (a.line.x + a.line.width // 3, (a.top + a.bottom) // 2)
        pb = (b.line.x + 2 * b.line.width // 3, (b.top + b.bottom) // 2)
        gap = (pb[0], (a.bottom + b.top) // 2)
        for granularity in GRANULARITIES:
            for p1, p2 in ((pa, pb), (pb, pa), (pa, gap), (gap, pb), (gap, gap)):
                drags.append((*p1, *p2, granularity))
        probes += [pa, pb, gap, (a.line.x + a.line.width + 1000, a.bottom - 1), (b.line.x - 1000, b.top)]
    if blocks:
        first, last = by_block[blocks[0]][0], by_block[blocks[-1]][-1]
        for granularity in GRANULARITIES:
            drags.append((first.line.x, first.top - 5, last.line.x + last.line.width, last.bottom + 5, granularity))
        probes += [(0, -100000), (layout.width, layout.height + 100000)]
    return {"range": thinned(ranges, 140), "drag": thinned(drags, 240), "probe": thinned(probes, 160)}


def read_queries(rows: Sequence[Row]) -> dict[str, list[tuple[Any, ...]]]:
    """The questions a layout section asked, read back from its rows."""
    out: dict[str, list[tuple[Any, ...]]] = {"range": [], "drag": [], "probe": []}
    for r in rows:
        if r[0] == "range":
            out["range"].append((int(r[1]), int(r[2])))
        elif r[0] == "drag":
            out["drag"].append((int(r[1]), int(r[2]), int(r[3]), int(r[4]), Granularity[r[5].upper()]))
        elif r[0] == "probe":
            out["probe"].append((int(r[1]), int(r[2])))
    return out


def emit_range(w: Writer, view: bv.BareView, layout: U.UnitLayout, im: IndexMap, start: int, end: int,
               *, rects: bool = True) -> None:  # fmt: skip
    selection = Selection(start, end)
    view.select(selection)
    touched = view.selected_blocks()
    w.r("range", start, end, *im.span(start, end), len(touched), *touched)
    w.r("copy", q(layout.model.copy_text(selection)))
    if rects:
        found = layout.model.rects(selection)
        w.r("rects", len(found))
        for rect in found:
            w.r("rect", rect.x, rect.y, rect.width, rect.height)
    view.clear_selection()


def emit_selection_layout(w: Writer, doc: Doc, view: bv.BareView, name: str, width: int,
                          asked: dict[str, list[tuple[Any, ...]]] | None, seed: int) -> None:  # fmt: skip
    layout = whole_layout(view, width)
    model = layout.model
    im = index_map(layout, doc.contents)
    nblocks = len(doc.document.blocks)
    w.r("layout", q(name), width)
    emit_units(w, view, layout)
    emit_leads(w, layout)
    emit_cmds(w, layout.cmds)
    emit_imap(w, view, layout, im)
    emit_copy_maps(w, layout, im)
    queries = asked if asked is not None else choose_queries(layout, nblocks, seed)
    w.r("drags", len(queries["drag"]))
    for ax, ay, bx, by, granularity in queries["drag"]:
        found = model.select_at(Point(ax, ay), Point(bx, by), granularity)
        w.r("drag", ax, ay, bx, by, granularity, found.start, found.end, *im.span(found.start, found.end))
    w.r("probes", len(queries["probe"]))
    for x, y in queries["probe"]:
        point = Point(x, y)
        hit = model.hit(point)
        under = layout.block_at(point)
        word, line = model.word_at(point), model.line_at(point)
        w.r("probe", x, y, hit, -1 if under is None else under, *im.pos(hit),
            word.start, word.end, line.start, line.end)  # fmt: skip
    w.r("ranges", len(queries["range"]))
    for start, end in queries["range"]:
        emit_range(w, view, layout, im, start, end)
    w.r("blocktexts", nblocks)
    for number in range(nblocks):
        w.r("blocktext", number, q(view.block_text(number)))
    w.r("endlayout")


def layout_sections(expected: Sequence[Row]) -> dict[str, list[Row]]:
    out: dict[str, list[Row]] = {}
    current: list[Row] | None = None
    for r in expected:
        if r[0] == "layout":
            current = out.setdefault(unq(r[1]), [])
        elif r[0] == "endlayout":
            current = None
        elif current is not None:
            current.append(r)
    return out


def run_doc(w: Writer, inputs: Sequence[Row], expected: Sequence[Row] | None) -> None:
    doc = build_doc(inputs)
    emit_blocks(w, doc)
    sections = layout_sections(expected) if expected is not None else {}
    seed = int(one(inputs, "seed")[1])
    for number, found in enumerate(rows_of(inputs, "width")):
        name = unq(found[1])
        width = sp_from_px(fl(found[2]), doc.theme.px_per_inch)
        view = bv.BareView(document=doc.document, theme=doc.theme, measure=MEASURE)
        asked = read_queries(sections[name]) if expected is not None else None
        emit_selection_layout(w, doc, view, name, width, asked, seed + number)


# --------------------------------------------------------------------------
# Group 2: decorations
# --------------------------------------------------------------------------

_SETUP: Any = None


def draw_setup() -> Any:
    """dump_draw's ImGui context and fonts, made once and only when a draw is recorded."""
    global _SETUP
    if _SETUP is None:
        import dump_draw

        _SETUP = (dump_draw, dump_draw.make_setup())
    return _SETUP


def emit_ink_ranges(w: Writer, tag: str, inks: Sequence[tuple[int, int, int]], im: IndexMap) -> None:
    w.r(tag, len(inks))
    for start, end, colour in inks:
        w.r("ink", start, end, colour, *im.span(start, end))


def run_deco(w: Writer, inputs: Sequence[Row], expected: Sequence[Row] | None) -> None:
    doc = build_doc(inputs)
    emit_blocks(w, doc)
    blocks = doc.document.blocks
    palette = Palette()
    roles = {BlockKind[r[1].upper()]: int(r[2]) for r in rows_of(inputs, "role")}
    if roles:
        palette = palette.with_roles(roles)
    styled = one(inputs, "links")[1] == "1"

    # The host's colour spans, in the order given: later ones win.
    colours: dict[int, list[spans.ColourSpan]] = {}
    index = spans.FindIndex.of(blocks)
    finds: list[tuple[str, bool, int, tuple[spans.TextMatch, ...]]] = []
    for r in inputs:
        if r[0] == "find":
            needle, fold, mark = unq(r[1]), r[2] == "1", int(r[3])
            matches = index.find(needle, ignore_case=fold)
            finds.append((needle, fold, mark, matches))
            for number, block in enumerate(blocks):
                if isinstance(block, TextBlock | CodeBlock):
                    made = spans.find_spans(block, needle, mark=mark, ignore_case=fold)
                    # FindIndex and find_spans are two routes to one answer.
                    assert [(s.start, s.end) for s in made] == [
                        (m.start, m.end) for m in matches if m.block == number
                    ]
                    colours.setdefault(number, []).extend(made)
        elif r[0] == "codeink":
            number, line, column, length, ink = (int(t) for t in r[1:6])
            block = blocks[number]
            assert isinstance(block, CodeBlock)
            colours.setdefault(number, []).append(
                spans.ColourSpan(
                    spans.code_position(block, line, column),
                    spans.code_position(block, line, column + length),
                    ink=ink,
                )
            )
        elif r[0] == "textink":
            number, start, end, ink, mark = (int(t) for t in r[1:6])
            made_one = spans.colour_text(
                blocks[number], start, end, ink=None if ink < 0 else ink, mark=None if mark < 0 else mark
            )
            assert made_one is not None
            colours.setdefault(number, []).append(made_one)
    select = one(inputs, "select")

    for found in rows_of(inputs, "width"):
        name = unq(found[1])
        width = sp_from_px(fl(found[2]), doc.theme.px_per_inch)
        view = bv.BareView(document=doc.document, theme=doc.theme, measure=MEASURE, style_links=styled)
        layout = whole_layout(view, width)
        im = index_map(layout, doc.contents)
        w.r("layout", q(name), width)
        emit_units(w, view, layout)
        emit_cmds(w, layout.cmds)
        emit_imap(w, view, layout, im)

        for needle, fold, mark, matches in finds:
            w.r("found", q(needle), fold, mark, len(matches))
            for match in matches:
                assert match.cell is None
                rects = view.span_rects(match.block, match.start, match.end)
                w.r("match", match.block, match.start, match.end,
                    *doc.contents[match.block].span(match.start, match.end), len(rects))  # fmt: skip
                for rect in rects:
                    w.r("rect", rect.x, rect.y, rect.width, rect.height)

        for number in sorted(colours):
            view.set_colours(number, colours[number])
            w.r("colours", number, len(colours[number]))
            for span in colours[number]:
                w.r("colour", span.start, span.end, -1 if span.ink is None else span.ink,
                    -1 if span.mark is None else span.mark,
                    *doc.contents[number].span(span.start, span.end))  # fmt: skip

        visible = list(layout.visible_units(EVERYWHERE))
        inks, fills = view._decorate(visible, palette)
        role_inks = bv._role_inks(visible, palette)
        final = bv._overlaid(role_inks, inks)
        runs = bv._inked(visible, palette)
        w.r("unmapped_blocks", view._unmapped)
        w.r("runs", len(runs))
        for cmds, ink in runs:
            w.r("inkrun", len(cmds), ink)
        emit_ink_ranges(w, "decoinks", inks, im)
        emit_ink_ranges(w, "roleinks", role_inks, im)
        emit_ink_ranges(w, "inks", final, im)
        w.r("fills", len(fills))
        for rect, colour in fills:
            w.r("fill", rect.x, rect.y, rect.width, rect.height, colour)
        w.r("linkareas", len(view._links))
        for area in view._links:
            w.r("linkarea", area.block, area.number, q(area.target), len(area.runs), len(area.rects))
            for run in area.runs:
                w.r("lrun", run.line, run.start, run.end, *im.span(run.start, run.end))
            for rect in area.rects:
                w.r("rect", rect.x, rect.y, rect.width, rect.height)

        # What reaches the draw list: draw_runs with exactly what `_paint` hands it.
        dump_draw, setup = draw_setup()
        chosen = Selection(int(select[1]) * layout.length // 100, int(select[2]) * layout.length // 100)
        highlights = tuple(layout.visible_rects(chosen, EVERYWHERE))
        case = dump_draw.Case(
            name=name, note="", runs=runs, origin=(fl(select[3]), fl(select[4])), palette=palette,
            highlights=highlights, fills=tuple(fills), inks=tuple(final), px_per_inch=doc.theme.px_per_inch,
        )  # fmt: skip
        ran = dump_draw.run_case(setup, case)
        w.r("drawn", chosen.start, chosen.end, ran.origin[0], ran.origin[1], len(highlights))
        texts = 0
        glyph_inks: list[list[int]] = []
        filled: list[tuple[Any, ...]] = []
        for op in ran.ops:
            if op[0] == "text":
                texts += 1
                if glyph_inks and glyph_inks[-1][1] == op[4]:
                    glyph_inks[-1][0] += 1
                else:
                    glyph_inks.append([1, op[4]])
            elif op[0] == "rectfilled":
                filled.append((texts, *op[1:6]))
        w.r("rectsfilled", len(filled))
        for before, x0, y0, x1, y1, colour in filled:
            w.r("rectfilled", before, float(x0), float(y0), float(x1), float(y1), colour)
        w.r("glyphinks", texts, len(glyph_inks), *[token for pair in glyph_inks for token in pair])
        w.r("endlayout")


# --------------------------------------------------------------------------
# Group 3: host blocks
# --------------------------------------------------------------------------


class Widget:
    """A `Hosted`: a height it is asked for, and a text it copies as."""

    def __init__(self, height_px: float, text: str) -> None:
        self.height_px = height_px
        self._text = text

    def height_for(self, width_px: float) -> float:
        return self.height_px

    def draw(self, **_: Any) -> object:
        return None

    @property
    def text(self) -> str:
        return self._text


def emit_host_layout(w: Writer, view: bv.BareView, layout: U.UnitLayout, width: int) -> None:
    theme = view.theme
    laid = view._layout_blocks(width)
    w.r("hosts", len(view._hosts))
    for number in sorted(view._hosts):
        host = view._hosts[number]
        stand_in = host.stand_in
        assert stand_in is not None and laid[number] is stand_in
        box = ui._host_box(stand_in)
        w.r("hosted", number, host.indent, float(host.width_px), float(host.height_px), stand_in.height,
            stand_in.left_indent, box.width, box.height, box.depth, q(str(host.hosted.text)))  # fmt: skip
    emit_units(w, view, layout)
    runs = ui.block_runs(laid)
    assert len(runs) == len(layout.placed)
    for position in range(1, len(runs)):
        w.r("gap", position, ui.run_gap(runs[position - 1], runs[position]))
    whole = ui.compose_blocks(laid, theme, width, MEASURE)
    shipped = tuple(ship(whole, Point(0, whole.height), measure=MEASURE))
    w.r("composed", whole.width, whole.height + whole.depth,
        whole.height + whole.depth == layout.height, shipped == layout.cmds)  # fmt: skip
    w.r("built", len(layout.stats.built), *layout.stats.built)


def run_host(w: Writer, inputs: Sequence[Row], expected: Sequence[Row] | None) -> None:
    doc = build_doc(inputs)
    emit_blocks(w, doc)
    fences = {unq(r[1]): (fl(r[2]), unq(r[3])) for r in rows_of(inputs, "hostfence")}
    resizes = {unq(r[1]): fl(r[2]) for r in rows_of(inputs, "resize")}
    sections = layout_sections(expected) if expected is not None else {}
    seed = int(one(inputs, "seed")[1])
    for found in rows_of(inputs, "width"):
        name = unq(found[1])
        width = sp_from_px(fl(found[2]), doc.theme.px_per_inch)
        widgets: dict[int, Widget] = {}

        def host_for(_view: Any, number: int, block: Any, widgets: dict[int, Widget] = widgets) -> Widget | None:
            if not isinstance(block, CodeBlock) or block.language not in fences:
                return None
            if number not in widgets:
                widgets[number] = Widget(*fences[block.language])
            return widgets[number]

        view = bv.BareView(document=doc.document, theme=doc.theme, measure=MEASURE, host_for=host_for)
        layout = whole_layout(view, width)
        model = layout.model
        im = index_map(layout, doc.contents)
        nblocks = len(doc.document.blocks)
        w.r("layout", q(name), width)
        emit_host_layout(w, view, layout, width)
        emit_leads(w, layout)
        emit_cmds(w, layout.cmds)
        emit_imap(w, view, layout, im)
        emit_copy_maps(w, layout, im)
        leading, trailing = model.edge_hosts()
        w.r("edge_hosts", len(leading), *[layout.placed[p].block_start for p in leading],
            len(trailing), *[layout.placed[p].block_start for p in trailing])  # fmt: skip
        everything = Selection(0, layout.length)
        for flag in (False, True):
            w.r("copyall", flag, q(model.copy_text(everything, everything=flag)))
        view.select_all()
        chosen = view.selected_blocks()
        w.r("selectall", len(chosen), *chosen, q(view.selected_text()))
        view.clear_selection()
        if expected is not None:
            ranges = read_queries(sections[name])["range"]
        else:
            ranges = thinned(choose_queries(layout, nblocks, seed)["range"], 40)
        w.r("ranges", len(ranges))
        for start, end in ranges:
            emit_range(w, view, layout, im, start, end, rects=False)
        w.r("probes", 2 * len(view._hosts))
        for number in sorted(view._hosts):
            rect = view.block_rect(number)
            assert rect is not None
            for x, y in ((rect.x + rect.width // 2, rect.y + rect.height // 2), (rect.x, rect.y + rect.height)):
                under = layout.block_at(Point(x, y))
                hit = model.hit(Point(x, y))
                w.r("probe", x, y, hit, -1 if under is None else under, *im.pos(hit))
        w.r("blocktexts", nblocks)
        for number in range(nblocks):
            w.r("blocktext", number, q(view.block_text(number)))
        if resizes:
            for number, widget in widgets.items():
                language = doc.document.blocks[number].language
                if language in resizes:
                    widget.height_px = resizes[language]
            w.r("resized", view._hosts_resized())
            view.invalidate()
            again = whole_layout(view, width)
            emit_host_layout(w, view, again, width)
            w.r("endresized")
        w.r("endlayout")


# --------------------------------------------------------------------------
# Group 4: streaming
# --------------------------------------------------------------------------

_MASK = 0xFFFFFFFFFFFFFFFF


def digest(cmds: Sequence[Any]) -> str:
    """
    FNV-1a, 64 bits, over a command list: FORMAT-doc.md has the byte order.
    """
    state = 0xCBF29CE484222325

    def eat(value: int) -> None:
        nonlocal state
        for byte in (value & _MASK).to_bytes(8, "little"):
            state = ((state ^ byte) * 0x100000001B3) & _MASK

    for cmd in cmds:
        if isinstance(cmd, Glyph):
            eat(1)
            for byte in cmd.font.encode("utf-8"):
                eat(byte)
            for value in (cmd.codepoint, cmd.x, cmd.y, cmd.index):
                eat(value)
        elif isinstance(cmd, RuleRect):
            for value in (2, cmd.x, cmd.y, cmd.width, cmd.height):
                eat(value)
        elif isinstance(cmd, SetGlue):
            for value in (4 if isinstance(cmd, Atom) else 3, cmd.x, cmd.y, cmd.width, cmd.natural,
                          cmd.height, cmd.depth, cmd.index):  # fmt: skip
                eat(value)
        else:
            assert isinstance(cmd, Line)
            for value in (5, cmd.x, cmd.y, cmd.width, cmd.height, cmd.depth, cmd.start, cmd.end):
                eat(value)
    return f"{state:016x}"


def block_row(number: int, state: str, block: Any) -> Row:
    if isinstance(block, TextBlock):
        return row("sblock", number, state, "text", block.kind, block.style.align,
                   len(block.material), q(spans.material_text(block).text))  # fmt: skip
    if isinstance(block, CodeBlock):
        return row("sblock", number, state, "code", BlockKind.CODE, block.style.align,
                   sum(map(len, block.lines)), q(spans.material_text(block).text))  # fmt: skip
    if isinstance(block, TableBlock):
        return row("sblock", number, state, "table", BlockKind.TABLE, Align.LEFT, 0, q(""))
    return row("sblock", number, state, "rule", BlockKind.RULE, Align.LEFT, 0, q(""))


def run_stream(w: Writer, inputs: Sequence[Row], expected: Sequence[Row] | None) -> None:
    theme = theme_of(inputs)
    policy = CommitPolicy[one(inputs, "policy")[1].upper()]
    math = one(inputs, "math")[1] == "1"
    full = one(inputs, "full")[1] == "1"
    found = one(inputs, "width")
    width = sp_from_px(fl(found[2]), theme.px_per_inch)
    chunks = [unq(r[1]) for r in rows_of(inputs, "chunk")]

    stream = StreamDocument(theme, fontlib_metrics, policy=policy, math=markdown_math() if math else None)
    view = StreamView(stream, MEASURE)
    was_blocks: tuple[Any, ...] = ()
    was_committed = 0
    was_units: dict[int, tuple[Any, ...]] = {}
    was_lines: tuple[Any, ...] = ()
    was_live: dict[int, tuple[Any, ...]] = {}
    totals = {"steps": 0, "moved": 0, "built": 0, "units": 0, "unsettled": 0}

    def step(number: int, label: str) -> None:
        nonlocal was_blocks, was_committed, was_units, was_lines, was_live
        view.sync()
        layout = view.layout_for(width)
        blocks = stream.blocks
        committed = len(stream.committed)
        w.r("step", number, label, stream.revision, committed, len(stream.live), len(layout.placed),
            layout.height, layout.length)  # fmt: skip
        # A block is written when it is not the object it was a step ago.
        for index, block in enumerate(blocks):
            if index < len(was_blocks) and was_blocks[index] is block:
                continue
            w.lines.append(" ".join(block_row(index, "committed" if index < committed else "live", block)))
        # The stability property: what had committed a step ago is where it was.
        lines = view.set_lines(0, was_committed)
        moved = sum(1 for index in range(was_committed) if was_blocks[index] is not blocks[index])
        moved += 0 if lines == was_lines else 1
        w.r("stable", was_committed, len(lines), moved)
        # The settled lines of each block still being written.
        live: dict[int, tuple[Any, ...]] = {}
        for index in range(committed, len(blocks)):
            if not isinstance(blocks[index], TextBlock):
                continue
            now = view.set_lines(index, index + 1)
            live[index] = now
            before = was_live.get(index)
            if before is not None and len(before) > 1:
                kept = before[:-1] == now[: len(before) - 1]
                totals["unsettled"] += 0 if kept else 1
                w.r("settled", index, len(before), len(now), kept)
        stats = layout.stats
        w.r("built", len(stats.built), *stats.built)
        w.r("broken", stats.blocks_broken, stats.units)
        units: dict[int, tuple[Any, ...]] = {}
        for position, unit in enumerate(layout.placed):
            units[position] = (unit.block_start, len(unit.blocks), -1 if unit.part is None else unit.part,
                               unit.y, unit.height, unit.base, unit.unit.count, digest(unit.unit.cmds))  # fmt: skip
            if was_units.get(position) != units[position]:
                w.r("unit", position, *units[position])
                if full and position in stats.built:
                    emit_cmds(w, unit.unit.cmds)
        w.r("endstep")
        totals["steps"] += 1
        totals["moved"] += moved
        totals["built"] += len(stats.built)
        totals["units"] += stats.units
        was_blocks, was_committed, was_units, was_live = blocks, committed, units, live
        was_lines = view.set_lines(0, committed)

    for number, chunk in enumerate(chunks):
        stream.append(chunk)
        step(number, "append")
    stream.close()
    step(len(chunks), "close")
    source = "".join(chunks)
    whole = markdown.build_document(
        source, stream.theme if policy is CommitPolicy.RESET else stream.live_theme, fontlib_metrics,
        references=False, math=markdown_math() if math else None,
    )  # fmt: skip
    layout = view.layout_for(width)
    w.r("final", len(stream.blocks), stream.blocks == whole.blocks, layout.height, layout.length, digest(layout.cmds))
    w.r("totals", totals["steps"], totals["moved"], totals["unsettled"], totals["built"], totals["units"])
    if full:
        emit_cmds(w, layout.cmds)


def stability_module() -> Any:
    path = REFERENCE / "tests" / "test_stream_stability.py"
    spec = importlib.util.spec_from_file_location("reference_test_stream_stability", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def run_flips(w: Writer, inputs: Sequence[Row], expected: Sequence[Row] | None) -> None:
    """
    The reference's own property test, driven as it drives itself: `_flips`
    over the word prefixes of the sample paragraph, ragged, shrink zeroed.
    """
    test = stability_module()
    from advtext.linebreak import LineBreakParams
    from advtext.units import sp_from_pt

    found = one(inputs, "property")
    width_pt, which, words = fl(found[1]), found[2], int(found[3])
    params = ui.PROSE_BREAKING if which == "prose" else LineBreakParams()
    paragraph = test._without_shrink(test._sample_paragraph())
    width = sp_from_pt(width_pt)
    ragged = test._ragged(params)
    prefixes = test._word_prefixes(paragraph)[:words]
    core.emit_nodes(w, "material", prefixes[-1])
    w.r("measure_width", width, test._widest_word(paragraph) < width)
    flips, steps, most = test._flips(paragraph, width, ragged, words=words)
    for count, prefix in enumerate(prefixes, start=1):
        lines = test._lines(prefix, width, ragged)
        w.r("prefix", count, len(prefix), len(lines), *[len(test._text(line).split()) for line in lines])
    w.r("first_fit", len(test._first_fit(prefixes[-1], width)), *test._first_fit(prefixes[-1], width))
    w.r("flips", steps, most, len(flips), *[flip.words for flip in flips])


# --------------------------------------------------------------------------
# The cases
# --------------------------------------------------------------------------

MIXED = """# A heading with a [link](https://example.org/a) in it

A first paragraph with *emphasis*, `inline code`, an inline formula $a^2+b^2=c^2$ and a [link that is long enough to wrap across a line break at a narrow width](https://example.org/long). It ends with a hard break\\
and goes on after it.

$$\\sum_{k=0}^{n} k = \\frac{n(n+1)}{2}$$

- A bullet item with $x_i$ in it.
- A second bullet
  with two paragraphs.

  The second paragraph of the item.
  1. A numbered item inside.
  2. Another.

> A quoted paragraph, first of two.
>
> The second quoted paragraph, with a [link](https://example.org/q).

```python
def f(x):
    return x + 1

print(f(41))
```

---

The last paragraph. Find me: needle, Needle and needle again; a needle
split across two source lines, and a character no face has: 字.
"""

HOSTED = """Before the widget.

```diff
-old
+new
```

Between two widgets.

```diff
-a
```

- An item, then a widget inside it:

  ```diff
  +in a list
  ```
- The next item.

```python
print("an ordinary code block")
```

After everything.
"""

HOST_EDGES = """```diff
-first thing in the document
```

The only text.

```diff
+last thing in the document
```
"""

HOST_ONLY = """```diff
-a document that is one widget
```
"""

STREAM_TAIL = """
A closing paragraph with an inline formula $e^{i\\pi}+1=0$ in the middle of it and words after.

$$
\\sum_{k=0}^{n} k = \\frac{n(n+1)}{2}
$$

Done.
"""


def sample_source(*, tables: bool) -> str:
    text = (REFERENCE / "demo" / "sample.md").read_text(encoding="utf-8")
    if tables:
        return text
    return "".join(line for line in text.splitlines(keepends=True) if not line.startswith("|"))


def doc_inputs(source: str, px: float, align: Align, widths: Sequence[float], *, math: bool = True,
               seed: int = 1) -> list[Row]:  # fmt: skip
    return [
        row("markdown", math, True, q(source)),
        row("themein", px, align),
        row("seed", seed),
        *[row("width", q(f"ui-{width:g}px"), float(width)) for width in widths],
    ]


def selection_cases() -> list[tuple[str, str, list[Row]]]:
    return [
        ("mixed/16px-justify", "doc", doc_inputs(MIXED, 16.0, Align.JUSTIFY, (240.0, 400.0), seed=11)),
        ("mixed/13px-left", "doc", doc_inputs(MIXED, 13.0, Align.LEFT, (240.0,), seed=12)),
        ("two-paragraphs", "doc", doc_inputs(blk.TWO_PARAGRAPHS, 16.0, Align.JUSTIFY, (240.0, 640.0), seed=13)),
        ("nested-lists", "doc", doc_inputs(blk.NESTED, 16.0, Align.JUSTIFY, (240.0, 640.0), seed=14)),
        ("code", "doc", doc_inputs(blk.CODE, 16.0, Align.JUSTIFY, (240.0,), seed=15)),
        ("headings", "doc", doc_inputs(blk.HEADINGS, 13.0, Align.LEFT, (240.0,), seed=16)),
        ("demo-sample", "doc", doc_inputs(sample_source(tables=True), 16.0, Align.JUSTIFY, (400.0,), seed=17)),
    ]


MARK_A, MARK_B, MARK_C = rgba(0xFF, 0xE0, 0x66, 0x99), rgba(0x66, 0xE0, 0xFF, 0x99), rgba(0xFF, 0x66, 0xCC, 0x80)
INK_KEYWORD, INK_NUMBER, INK_TEXT = rgba(0xAA, 0x22, 0x88), rgba(0x11, 0x88, 0x33), rgba(0xCC, 0x44, 0x00)


def decoration_cases() -> list[tuple[str, str, list[Row]]]:
    def with_spans(base: list[Row], *, links: bool, roles: Sequence[tuple[BlockKind, int]] = ()) -> list[Row]:
        code = MIXED_CODE_BLOCK
        return [
            *base,
            row("links", links),
            *[row("role", kind, ink) for kind, ink in roles],
            # A search for a word in three cases, a phrase that crosses a
            # source line (and, set narrow, a set line), one that crosses a
            # run boundary (roman into italic), and one inside a link.
            row("find", q("needle"), False, MARK_A),
            row("find", q("Needle"), True, MARK_B),
            row("find", q("needle split across"), False, MARK_C),
            row("find", q("with emphasis, inline"), False, MARK_C),
            row("find", q("enough to wrap"), False, MARK_A),
            row("find", q("return x"), False, MARK_B),
            row("find", q("no such text"), False, MARK_A),
            # A tokeniser's (line, column, length): `def`, `return`, `1`, `41`.
            row("codeink", code, 0, 0, 3, INK_KEYWORD),
            row("codeink", code, 1, 4, 6, INK_KEYWORD),
            row("codeink", code, 1, 15, 1, INK_NUMBER),
            row("codeink", code, 3, 8, 2, INK_NUMBER),
            # Offsets into `material_text(block).text`: an ink across the
            # hard break of block 1, and an ink with a mark in the heading.
            row("textink", 0, 2, 9, INK_TEXT, MARK_B),
            row("textink", MIXED_LAST_BLOCK, 4, 20, INK_TEXT, -1),
            row("select", 20, 70, 12.25, 30.5),
        ]

    base = doc_inputs(MIXED, 16.0, Align.JUSTIFY, (240.0, 400.0))
    quote_roles = ((BlockKind.QUOTE, rgba(0x55, 0x55, 0x99)), (BlockKind.HEADING_1, rgba(0x99, 0x22, 0x22)),
                   (BlockKind.CODE, rgba(0x22, 0x55, 0x22)))  # fmt: skip
    quoted = "> # A heading in a quote\n>\n> A paragraph under it with a [link](https://example.org/z) and a needle.\n\nAfter.\n"
    return [
        ("mixed/links-styled", "deco", with_spans(base, links=True)),
        ("mixed/links-unstyled-roles", "deco", with_spans(base[:3] + base[3:4], links=False, roles=quote_roles)),
        (
            "quote/roles",
            "deco",
            [
                *doc_inputs(quoted, 16.0, Align.LEFT, (240.0,)),
                row("links", True),
                *[row("role", kind, ink) for kind, ink in quote_roles],
                row("find", q("needle"), False, MARK_A),
                row("select", 0, 100, 0.0, 0.0),
            ],
        ),
    ]


MIXED_CODE_BLOCK = 0  # set by `locate_mixed`
MIXED_LAST_BLOCK = 0


def locate_mixed() -> None:
    """The block numbers the decoration cases name, read from the document and not counted by hand."""
    global MIXED_CODE_BLOCK, MIXED_LAST_BLOCK
    built = markdown.build_document(MIXED, Theme.build(), fontlib_metrics, math=markdown_math())
    (MIXED_CODE_BLOCK,) = [n for n, block in enumerate(built.blocks) if isinstance(block, CodeBlock)]
    MIXED_LAST_BLOCK = len(built.blocks) - 1
    assert all(not f.refusal for b in built.blocks if isinstance(b, TextBlock) for f in b.formulas)


def host_cases() -> list[tuple[str, str, list[Row]]]:
    def hosted(source: str, widths: Sequence[float], height: float, *, resize: float | None = None,
               px: float = 16.0) -> list[Row]:  # fmt: skip
        return [
            *doc_inputs(source, px, Align.JUSTIFY, widths, seed=31),
            row("hostfence", q("diff"), height, q("-old\n+new\n")),
            *([row("resize", q("diff"), resize)] if resize is not None else []),
        ]

    return [
        ("between-text", "host", hosted(HOSTED, (240.0, 400.0), 75.0, resize=120.5)),
        ("between-text/13px", "host", hosted(HOSTED, (240.0,), 33.3, px=13.0)),
        ("at-the-edges", "host", hosted(HOST_EDGES, (240.0,), 50.0, resize=10.0)),
        ("only-a-host", "host", hosted(HOST_ONLY, (240.0,), 40.0)),
        ("zero-height", "host", hosted(HOSTED, (240.0,), 0.0, resize=18.0)),
        ("zero-height-first", "host", hosted(HOST_EDGES, (240.0,), 0.0)),
        ("not-hosted", "host", doc_inputs(HOSTED, 16.0, Align.JUSTIFY, (240.0,), seed=31)),
    ]


def chunked(source: str, cuts: Sequence[int]) -> list[str]:
    edges = [0, *sorted({cut for cut in cuts if 0 < cut < len(source)}), len(source)]
    return [source[a:b] for a, b in zip(edges, edges[1:], strict=False)]


def stream_cases() -> list[tuple[str, str, list[Row]]]:
    source = sample_source(tables=False) + STREAM_TAIL
    whole = sample_source(tables=True)

    def case(chunks: Sequence[str], *, align: Align = Align.JUSTIFY, policy: CommitPolicy = CommitPolicy.KEEP_STREAMED,
             full: bool = False, width: float = 400.0) -> list[Row]:  # fmt: skip
        return [
            row("themein", 16.0, align),
            row("policy", policy),
            row("math", True),
            row("full", full),
            row("width", q(f"ui-{width:g}px"), width),
            *[row("chunk", q(chunk)) for chunk in chunks],
        ]

    lines = source.splitlines(keepends=True)
    # Cuts that land inside a word, inside `$...$`, inside `$$` ... `$$`,
    # inside the opening fence, inside the code and inside the closing fence.
    inline = source.index("$e^{i")
    display = source.index("$$\n\\sum")
    fence = source.index("```python")
    close = source.index("```", fence + 3)
    cuts = [5, 23, inline + 1, inline + 6, inline + 14, display + 1, display + 2, display + 9, display + 30,
            source.index("$$\n\nDone") + 1, fence + 2, fence + 6, fence + 14, close - 9, close + 1, close + 2,
            len(source) - 3]  # fmt: skip
    return [
        ("sample/by-line", "stream", case(lines)),
        ("sample/by-line-reset", "stream", case(lines, policy=CommitPolicy.RESET)),
        ("sample/by-line-ragged", "stream", case(lines, align=Align.LEFT)),
        ("sample/7-chars", "stream", case(chunked(source, range(7, len(source), 7)))),
        ("sample/64-chars", "stream", case(chunked(source, range(64, len(source), 64)))),
        ("sample/cuts-mid-word-formula-fence", "stream", case(chunked(source, cuts), full=True)),
        ("sample/one-chunk", "stream", case([source])),
        ("sample-with-table/by-line", "stream", case(whole.splitlines(keepends=True))),
        ("mixed/3-chars", "stream", case(chunked(MIXED, range(3, len(MIXED), 3)), width=240.0)),
        ("flips/270pt-prose", "flips", [row("property", 270.0, "prose", 45)]),
        ("flips/420pt-prose", "flips", [row("property", 420.0, "prose", 45)]),
        ("flips/270pt-plain", "flips", [row("property", 270.0, "plain", 45)]),
    ]


RUNNERS: dict[str, Callable[[Writer, Sequence[Row], Sequence[Row] | None], None]] = {
    "doc": run_doc,
    "deco": run_deco,
    "host": run_host,
    "stream": run_stream,
    "flips": run_flips,
}

GROUPS: dict[str, tuple[str, Callable[[], list[tuple[str, str, list[Row]]]]]] = {
    "selection": (
        """
Selection and copy ACROSS blocks: for a document at a width, the placed units,
each reading-order index as (block, byte offset into the block's text), the
separators a copy writes, and drags, probes and ranges with their rectangles
and the exact copied string.
""",
        selection_cases,
    ),
    "decorations": (
        """
Links, colour spans, syntax ink and find hits: the ink ranges and fills the
reference hands to draw_runs, the link areas, and the filled rectangles and
per-glyph colours that reach the draw list.
""",
        decoration_cases,
    ),
    "hosts": (
        """
Host blocks: a block that reserves a rectangle between text blocks -- its
stacking, extents, what a copy across it writes, and a resize.
""",
        host_cases,
    ),
    "stream": (
        """
Streaming: a document fed in chunks -- after each chunk which blocks are
committed and which live, which units the reference built, the units that
changed, and the stability property as checked records; then the reference's
own settled-line property test (kind `flips`).
""",
        stream_cases,
    ),
}


def emit_case(w: Writer, name: str, kind: str, inputs: Sequence[Row], expected: Sequence[Row] | None) -> None:
    w.case(name, kind)
    for found in inputs:
        w.lines.append(" ".join(found))
    w.r("expect")
    RUNNERS[kind](w, inputs, expected)
    w.end()


def dump(group: str) -> Writer:
    about, cases = GROUPS[group]
    w = Writer(group, about)
    for name, kind, inputs in cases():
        emit_case(w, name, kind, inputs, None)
    return w


def verify(path: Path) -> tuple[int, int]:
    """
    Every case of one file again, from its own INPUT rows and its own questions.
    """
    text = path.read_text(encoding="utf-8")
    body = [line for line in text.split("\n") if line and not line.startswith("#")]
    assert body[0] == f"golden {FORMAT_VERSION} {q('doc/' + path.stem)}", body[0]
    cases = 0
    compared = 0
    at = 1
    while at < len(body):
        head = body[at].split(" ")
        assert head[0] == "case", head
        end = body.index("endcase", at)
        rows = [line.split(" ") for line in body[at + 1 : end]]
        cut = rows.index(["expect"])
        again = Writer(path.stem, "")
        again.lines = []
        emit_case(again, unq(head[1]), head[2], rows[:cut], rows[cut + 1 :])
        want = body[at : end + 1]
        if again.lines != want:
            for number, (ours, theirs) in enumerate(zip(again.lines, want, strict=False)):
                if ours != theirs:
                    raise AssertionError(
                        f"{path.name}: case {unq(head[1])}, record {number}:\n  file: {theirs[:300]}\n  now:  {ours[:300]}"
                    )
            raise AssertionError(f"{path.name}: case {unq(head[1])}: {len(again.lines)} records, file has {len(want)}")
        cases += 1
        compared += len(want)
        at = end + 1
    return cases, compared


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--only", default="", help="comma-separated groups: " + ", ".join(GROUPS))
    args = parser.parse_args()
    chosen = [name for name in GROUPS if not args.only or name in args.only.split(",")]
    assert chosen, "no such group"
    locate_mixed()
    if args.verify:
        for name in chosen:
            cases, compared = verify(OUT_DIR / f"{name}.golden")
            print(f"{name}.golden: {cases} cases, {compared} records re-derived and equal")
        return 0
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for name in chosen:
        w = dump(name)
        text = w.text()
        (OUT_DIR / f"{name}.golden").write_text(text, encoding="utf-8", newline="\n")
        print(f"{name}.golden: {w.cases} cases, {len(w.lines)} lines, {len(text.encode('utf-8'))} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
