"""
Rich rendering: blocks in, draw-list output.

Step 5 of planning/archive/2026-08-10-transcript-markdown.md (the drawing half; wrapping
lives in span_layout.py). This is the only module in the markdown stack that
touches ImGui -- blocks.py, inline.py and span_layout.py are all plain data and
unit-tested; this one draws, and is exercised against a live frame by
scripts/verify_rich_render.py instead.

Deliberately out of scope, both flagged in the planning doc as unresolved and
sidestepped here rather than guessed at:

- **Clickable links.** The doc calls block hit-testing vs. link clicks
  "unresolved and must be prototyped first in step 5" -- an invisible_button
  covering a block eats the clicks a real link widget underneath it would need.
  Rather than build the rest of the renderer on top of an unvalidated
  interaction model, links render as underlined text and nothing else.
  Copy-block (``Block.lines`` is the verbatim source) is how the operator gets
  the actual URL out today.
- **Syntax highlighting.** Explicitly deferred in the doc to arrive together
  with code selection, on a widget (``TextEditor``) this renderer does not use.
- **Per-cell inline styling in a table.** Step 4 left ``TABLE.inline`` as
  ``None`` for exactly this reason (cell splitting is this step's job, not
  step 4's) -- but a cell draws as plain wrapped text here, no nested emphasis.
- **Alignment from a table's delimiter row** (``:---``, `---:`, `:---:`).
  Parsed by nothing here; every column is left-aligned.

Advance-width equality is what makes single-font-width layout correct:
Inconsolata Medium and Bold measure identically (verified in
scripts/verify_fonts.py -- 8.00px advance either way at 16px, only the ink
differs), so layout only ever measures with one face even though bold runs draw
in a different one.
"""

from __future__ import annotations

import bisect
import os
from collections.abc import Iterator, Sequence
from contextlib import contextmanager
from dataclasses import dataclass
from typing import Any

from imgui_bundle import imgui

from ..theme import Face, P, current, face, faded
from ..transcript import SegmentKind
from . import typeset
from .blocks import Block, BlockKind, with_live_inline
from .inline import InlineToken, parse_inline
from .span_layout import Row, Run, layout_inline

# How many of a single fence's own lines get drawn. Independent of the outer
# block window -- one 3000-line fence inside an otherwise-small window is still
# a per-frame stall without its own cap (planning doc: "a code block also needs
# an inner per-block clamp, or the outer bound is unenforceable").
_FENCE_LINE_CAP = 200

# The outer window is bounded by cumulative *source lines*, not block count, for
# the same reason -- a handful of huge blocks can blow the budget even with few
# blocks in the window (planning doc, same sentence).
_LINE_WINDOW = 400

# A live (still-streaming) block is inline-parsed only up to this many
# characters; longer renders as plain literal text until it finalises.
# Planning doc's Consequences section: ~1.7 ms extrapolated at 4 KB is too much
# at 60 fps for something re-parsed every single frame.
_LIVE_INLINE_LIMIT = 1200

_INDENT = 18.0

# Duplicated from transcript_pane.py's private _KIND_COLOUR rather than
# imported: transcript_pane.py calls into this module (draw()), so importing
# the other way would cycle. Eight entries, SegmentKind grows rarely -- cheaper
# to duplicate than to invent a shared module for one dict.
_KIND_COLOUR = {
    SegmentKind.REASONING: "text_dim",
    SegmentKind.OUTPUT: "text",
    SegmentKind.TOOL_CALL: "accent",
    SegmentKind.TOOL_RESULT: "diff_context",
    SegmentKind.ERROR: "danger",
    SegmentKind.SYSTEM: "text_dim",
    SegmentKind.COMPACTION: "warn",
}

_PROSE_KINDS = (
    BlockKind.PARAGRAPH,
    BlockKind.HEADING,
    BlockKind.BULLET_ITEM,
    BlockKind.ORDERED_ITEM,
    BlockKind.BLOCKQUOTE,
)


@dataclass
class RichState:
    """
    Interaction latch for the rich view, the ``TranscriptState.context_run``
    counterpart for blocks. One field because the pane only ever shows one
    node's transcript at a time -- see that field's own docstring.
    """

    context_block: int | None = None
    # Text to mark wherever the document shows it. Set by the pane that owns a
    # search box; empty marks nothing. Read by the imtypeset path only.
    find: str = ""
    # The imtypeset document these blocks were described into, and the record of
    # what was described (``_Typeset``). ``None`` until the first frame that draws
    # through imtypeset, and for ever when ``typeset.module()`` is ``None``.
    typeset: Any = None
    # Frames in a row in which the imtypeset path failed for this pane.
    typeset_failures: int = 0


def windowed(blocks: Sequence[Block]) -> list[Block]:
    """
    A tail-anchored suffix of ``blocks`` whose *rendered* line count -- a fence's
    own contribution capped at ``_FENCE_LINE_CAP``, matching what actually gets
    drawn -- stays under ``_LINE_WINDOW``. Never a block count: a single huge
    fence must not be let through just because it is "one block".

    Always includes at least the newest block, even if it alone is over budget --
    an empty render is a worse failure than a slightly-over-budget frame.
    """
    out: list[Block] = []
    total = 0
    for block in reversed(blocks):
        if block.kind is BlockKind.FENCE:
            # Counted over the body rather than over lines: the delimiters are not
            # drawn, and a language label takes one row when there is one.
            cost = min(len(fence_body(block)), _FENCE_LINE_CAP)
            cost += 1 if dict(block.meta).get("lang") else 0
        else:
            cost = len(block.lines)
        if out and total + cost > _LINE_WINDOW:
            break
        out.append(block)
        total += cost
    out.reverse()
    return out


def block_copy_text(blocks: Sequence[Block]) -> str:
    """Verbatim source of every given block, blank-line separated -- the
    "copy visible" affordance's rich-mode counterpart."""
    return "\n\n".join("\n".join(b.lines) for b in blocks)


def _plain_text(tokens: Sequence[InlineToken]) -> str:
    """Inline tokens flattened to plain text -- table cells and the
    over-the-limit live-block fallback use this instead of full span layout."""
    parts = []
    for tok in tokens:
        if tok.type in ("text", "code_inline"):
            parts.append(tok.content)
        elif tok.type in ("softbreak", "hardbreak"):
            parts.append(" ")
    return "".join(parts)


@contextmanager
def _body_font(size: float) -> Iterator[imgui.ImFont]:
    """
    Push BODY for the scope of a measurement pass, popped unconditionally.

    Only measurement needs this: ``ImDrawList.add_text`` takes a font directly,
    so drawing never depends on what's ambient. Measurement -- both
    ``calc_word_wrap_position_python`` (a method on a specific font) and
    ``calc_text_size`` (the *ambient* font) -- needs the two to agree, which is
    what pushing BODY here guarantees regardless of what the caller left active.
    """
    body = face(Face.BODY)
    imgui.push_font(body, size)
    try:
        yield body or imgui.get_font()
    finally:
        imgui.pop_font()


def _layout(tokens: Sequence[InlineToken], width: float, size: float) -> tuple[Row, ...]:
    with _body_font(size) as font:
        avail = max(1.0, width)

        def wrap_at(text: str, w: float) -> int:
            return font.calc_word_wrap_position_python(size, text, max(w, 0.0))

        def measure_width(text: str) -> float:
            return imgui.calc_text_size(text).x

        return layout_inline(tokens, avail, wrap_at, measure_width)


def _run_color(run: Run, base: int) -> int:
    if run.code:
        return P.diff_context.u32
    if run.bold:
        return P.text_strong.u32
    if run.italic:
        return P.accent.u32
    return base


def _run_font(run: Run) -> imgui.ImFont | None:
    return face(Face.BOLD) if run.bold else face(Face.BODY)


def _line_advance() -> float:
    """
    Row-to-row advance *inside* a block.

    Not ``get_text_line_height_with_spacing``: that adds ``style.ItemSpacing.y``,
    which is the gap ImGui puts between *widgets*. Between one block and the next
    that is exactly right and already supplied -- ``_draw_one`` ends each block with
    an ``invisible_button``, and ImGui spaces after it. Between two wrapped rows of
    the same paragraph it is a widget gap inserted into a paragraph, which leaded
    every line of rendered markdown and is what made a block read looser than the
    same words drawn by ``imgui.text_wrapped`` beside it.
    """
    return imgui.get_text_line_height()


def _draw_rows(
    dl: imgui.ImDrawList, start: imgui.ImVec2, rows: Sequence[Row], size: float, base: int
) -> float:
    line_h = _line_advance()
    y = start.y
    for row in rows:
        x = start.x
        for run in row.runs:
            font = _run_font(run)
            col = _run_color(run, base)
            dl.add_text(font or imgui.get_font(), size, imgui.ImVec2(x, y), col, run.text)
            width = imgui.calc_text_size(run.text).x
            if run.link:
                # Just inside the bottom of the line box. Previously written as
                # ``line_h - 4.0`` against a height that included ItemSpacing.y,
                # which arrived at the same place only because that spacing happened
                # to be 4px; measured from the tight height it needs no such luck.
                underline_y = y + line_h - 1.0
                dl.add_line(
                    imgui.ImVec2(x, underline_y),
                    imgui.ImVec2(x + width, underline_y),
                    col,
                    1.0,
                )
            x += width
        y += line_h
    return y - start.y


def _draw_prose(
    dl: imgui.ImDrawList, start: imgui.ImVec2, width: float, block: Block, size: float
) -> float:
    indented = block.kind in (BlockKind.BULLET_ITEM, BlockKind.ORDERED_ITEM, BlockKind.BLOCKQUOTE)
    indent = _INDENT if indented else 0.0
    text_x = start.x + indent
    tokens = block.inline or ()
    rows = _layout(tokens, width - indent, size)

    base = P.text.u32
    if block.kind is BlockKind.HEADING:
        base = P.accent.u32
    elif block.kind is BlockKind.BLOCKQUOTE:
        base = P.text_dim.u32

    height = _draw_rows(dl, imgui.ImVec2(text_x, start.y), rows, size, base)

    if block.kind is BlockKind.BULLET_ITEM:
        dl.add_text(imgui.ImVec2(start.x, start.y), P.text_dim.u32, "•")
    elif block.kind is BlockKind.ORDERED_ITEM:
        n = dict(block.meta).get("start", "1")
        dl.add_text(imgui.ImVec2(start.x, start.y), P.text_dim.u32, f"{n}.")
    elif block.kind is BlockKind.BLOCKQUOTE:
        dl.add_line(
            imgui.ImVec2(start.x + 3.0, start.y),
            imgui.ImVec2(start.x + 3.0, start.y + height),
            P.border.u32,
            2.0,
        )
    return height


def fence_body(block: Block) -> tuple[str, ...]:
    """
    The code inside a fence, without its delimiters.

    ``Block.lines`` deliberately stays verbatim -- copy-as-markdown depends on
    getting back exactly what the model emitted, fences included -- so the ``` rows
    are dropped here, where the drawing happens, rather than at parse time. An
    unclosed fence has an opening delimiter and no closing one, which is why the
    tail is only trimmed when the block actually closed.
    """
    body = block.lines[1:]  # the opening delimiter is always the first line
    if block.closed and body:
        body = body[:-1]
    return body


def _draw_fence(dl: imgui.ImDrawList, start: imgui.ImVec2, block: Block) -> float:
    line_h = _line_advance()
    lines = fence_body(block)
    shown = lines[:_FENCE_LINE_CAP]
    y = start.y
    lang = dict(block.meta).get("lang")
    if lang:
        # The info string as a label, not as the ```lang row it came from. Costs the
        # same single line the delimiter used to, and says something.
        dl.add_text(imgui.ImVec2(start.x, y), P.text_dim.u32, lang)
        y += line_h
    for line in shown:
        dl.add_text(imgui.ImVec2(start.x, y), P.diff_context.u32, line or " ")
        y += line_h
    if len(lines) > len(shown):
        dl.add_text(
            imgui.ImVec2(start.x, y),
            P.text_dim.u32,
            f"... {len(lines) - len(shown)} more line(s), copy to see all",
        )
        y += line_h
    if not block.closed:
        dl.add_text(imgui.ImVec2(start.x, y), P.warn.u32, "(fence not closed)")
        y += line_h
    return y - start.y


def _draw_literal(dl: imgui.ImDrawList, start: imgui.ImVec2, block: Block) -> float:
    line_h = _line_advance()
    color = getattr(P, _KIND_COLOUR.get(block.segment_kind, "text")).u32
    y = start.y
    for line in block.lines:
        dl.add_text(imgui.ImVec2(start.x, y), color, line or " ")
        y += line_h
    return y - start.y


def _draw_thematic_break(dl: imgui.ImDrawList, start: imgui.ImVec2, width: float) -> float:
    line_h = _line_advance()
    y = start.y + line_h * 0.5
    dl.add_line(imgui.ImVec2(start.x, y), imgui.ImVec2(start.x + width, y), P.border.u32, 1.0)
    return line_h


def _split_table_row(line: str) -> list[str]:
    """
    GFM cell split on unescaped ``|``. A leading/trailing pipe (the common GFM
    style) is trimmed; ``\\|`` is an escaped literal pipe, not a separator.
    """
    trimmed = line.strip()
    if trimmed.startswith("|"):
        trimmed = trimmed[1:]
    if trimmed.endswith("|") and not trimmed.endswith("\\|"):
        trimmed = trimmed[:-1]
    cells: list[str] = []
    current: list[str] = []
    i = 0
    while i < len(trimmed):
        ch = trimmed[i]
        if ch == "\\" and i + 1 < len(trimmed) and trimmed[i + 1] == "|":
            current.append("|")
            i += 2
            continue
        if ch == "|":
            cells.append("".join(current).strip())
            current = []
            i += 1
            continue
        current.append(ch)
        i += 1
    cells.append("".join(current).strip())
    return cells


def _draw_table(index: int, block: Block) -> None:
    if len(block.lines) < 2:
        return
    header = _split_table_row(block.lines[0])
    body = [_split_table_row(line) for line in block.lines[2:]]
    columns = max(1, len(header))
    flags = (
        imgui.TableFlags_.resizable
        | imgui.TableFlags_.borders
        | imgui.TableFlags_.sizing_stretch_prop
    )
    if not imgui.begin_table(f"##rich_table_{index}", columns, flags):
        return
    try:
        for cell in header:
            imgui.table_setup_column(cell or " ")
        imgui.table_headers_row()
        for row in body:
            imgui.table_next_row()
            for col in range(columns):
                imgui.table_next_column()
                raw = row[col] if col < len(row) else ""
                plain = _plain_text(parse_inline(raw)) if raw else ""
                imgui.text_wrapped(plain or " ")
    finally:
        imgui.end_table()

    if imgui.begin_popup_context_item(f"##rich_table_ctx_{index}"):
        try:
            if imgui.menu_item_simple(f"copy table ({len(body)} row(s))"):
                imgui.set_clipboard_text("\n".join(block.lines))
        finally:
            imgui.end_popup()


def _draw_one(index: int, rich: RichState, block: Block, width: float, size: float) -> None:
    imgui.push_id(index)

    if block.kind is BlockKind.TABLE:
        _draw_table(index, block)
        imgui.pop_id()
        return

    start = imgui.get_cursor_screen_pos()
    dl = imgui.get_window_draw_list()

    if block.kind is BlockKind.THEMATIC_BREAK:
        height = _draw_thematic_break(dl, start, width)
    elif block.kind is BlockKind.FENCE:
        height = _draw_fence(dl, start, block)
    elif block.kind is BlockKind.LITERAL:
        height = _draw_literal(dl, start, block)
    elif block.kind in _PROSE_KINDS:
        height = _draw_prose(dl, start, width, block, size)
    else:  # pragma: no cover -- exhaustive over BlockKind; a future kind lands here
        height = imgui.get_text_line_height_with_spacing()

    imgui.invisible_button("##hit", imgui.ImVec2(width, max(height, 1.0)))
    if imgui.is_item_hovered():
        rich.context_block = index

    if imgui.begin_popup_context_item("##rich_block_ctx"):
        label = block.kind.value.replace("_", " ")
        if imgui.menu_item_simple(f"copy {label}"):
            imgui.set_clipboard_text("\n".join(block.lines))
        imgui.end_popup()

    imgui.pop_id()


def draw(rich: RichState, blocks: Sequence[Block], live: Block | None) -> None:
    """
    Draw a node's blocks in rich mode.

    ``blocks`` is the node's full committed history. ``live`` is whatever
    ``BlockCursor.live_block()`` returned this frame, or None.

    Through imtypeset when ``typeset.module()`` has it: the blocks are described
    into one retained document when they change and the frame is a constant number
    of calls, whatever the history's length, so nothing is windowed. Otherwise the
    draw-list renderer below, which windows.

    Exactly one of the two draws a frame. ``_draw_typeset`` says whether the frame
    is its own; it is not only when it failed before putting anything on screen,
    and then the renderer below draws this frame in ImGui state nothing has
    touched.
    """
    ts = typeset.module()
    if ts is None or not _draw_typeset(ts, rich, blocks, live):
        _draw_classic(rich, blocks, live)


def _draw_classic(rich: RichState, blocks: Sequence[Block], live: Block | None) -> None:
    """The renderer this module has always had: windowed, one draw-list call a run."""
    width = imgui.get_content_region_avail().x
    size = imgui.get_font_size()

    window = windowed(blocks)
    dropped = len(blocks) - len(window)
    if dropped > 0:
        imgui.text_disabled(f"... {dropped} earlier block(s) not shown (history window is bounded)")

    if not imgui.is_popup_open("##rich_block_ctx"):
        rich.context_block = None

    for i, block in enumerate(window):
        _draw_one(i, rich, block, width, size)

    if live is not None:
        live = with_live_inline(live, _LIVE_INLINE_LIMIT)
        if live.inline is None and live.kind in _PROSE_KINDS:
            # Over the limit (or the parse was never attempted for this kind) --
            # render the raw source plainly rather than block on inline parsing
            # every frame. Styling appears once the block finalises.
            start = imgui.get_cursor_screen_pos()
            dl = imgui.get_window_draw_list()
            height = _draw_literal(dl, start, live)
            imgui.invisible_button("##live", imgui.ImVec2(width, max(height, 1.0)))
        else:
            _draw_one(len(window), rich, live, width, size)


# -- the imtypeset path ----------------------------------------------------------
#
# One retained ``imtypeset.Document`` per ``RichState``. The blocks are described
# into it when they change -- one call into the library per change -- and a frame
# is layout + draw (+ one hit test while the mouse is over it), whatever the
# number of blocks. What a block description cannot say stays here: a table (a
# Host block reserves its place and ``_draw_table`` draws into it), the context
# menu, and the selection's mouse handling.

# Baseline to baseline in ems. Inconsolata's ascent plus descent is 1.049 em, which
# is the row advance ``_line_advance`` uses, so rows keep the pitch they have today.
_LINE_HEIGHT_EM = 1.05

# Opt-in, because a transcript is full of dollars that are not mathematics.
_MATH = os.environ.get("PPTMSTR_TYPESET_MATH", "") == "1"
_JUSTIFY = os.environ.get("PPTMSTR_TYPESET_JUSTIFY", "") == "1"

_FIND_MARK_CAP = 2000

# Frames in a row the imtypeset path may fail in one pane before it is switched
# off for the run. One failure is survived: the document is described again from
# the blocks on the next frame.
_FAILURES_BEFORE_OFF = 3

_STYLE_NAMES = (
    "body",
    "heading",
    "item",
    "quote",
    "chrome",
    "warn",
    "code",
    "rule",
    *(f"literal-{kind.value}" for kind in SegmentKind),
)


class _Typeset:
    """One document, and the record of what was described into it."""

    def __init__(self, ts: Any, size: float) -> None:
        self.palette = current()
        self.size = size
        self.doc = ts.Document()
        self.source: Sequence[Block] | None = None  # identity: the sequence last described
        self.count = 0  # how many of its blocks are in the document
        self.settled = 0  # how many document blocks stand for them; the rest is the live tail
        self.tail: list[Any] = []  # the live block as last described
        self.live_key: Any = None
        self.live: Block | None = None  # the live block itself: a table can be one
        self.origin: list[int] = []  # document block -> index into source (the live one: count)
        self.texts: list[str] = []  # document block -> its text, as described
        # The same folded for a search (``_fold``), as far as find has needed.
        self.folded: list[tuple[str, list[int] | None]] = []
        self.folded_from = 0  # entries from here on were the live tail's and are stale
        self.found: list[Any] = []  # a mark for each place ``found_needle`` is, by block
        self.found_needle = ""
        self.found_upto = 0  # blocks before this one have been searched and have not changed
        self.tables: list[int] = []  # document blocks that are tables, ascending
        self.extents: dict[int, float] = {}  # table -> the height its Host block has
        self.measured: dict[int, float] = {}  # table -> the height ImGui last drew it at
        # (document block, source) of a table being described again -> the two above
        self.carried: dict[tuple[int, int], tuple[float | None, float | None]] = {}
        self.links: list[Any] = []  # an underline for each link
        self.revision = 0
        self.marks: Any = None
        self.marks_key: Any = None
        self.selection: Any = None
        self.anchor: tuple[float, float] | None = None
        self.granularity = 0

        gap = imgui.get_style().item_spacing.y
        align = ts.Align.Justify if _JUSTIFY else ts.Align.Left
        look = {
            "body": {},
            "heading": {"Color": P.accent.u32, "Align": ts.Align.Left},
            "item": {"MarkerColor": P.text_dim.u32},
            "quote": {"Color": P.text_dim.u32},
            "chrome": {"Color": P.text_dim.u32, "Align": ts.Align.Left},
            "warn": {"Color": P.warn.u32, "Align": ts.Align.Left},
            "code": {"Color": P.diff_context.u32},
            "rule": {"SpaceAbove": size * 0.5, "SpaceBelow": size * 0.5 + gap},
        }
        for kind in SegmentKind:
            look[f"literal-{kind.value}"] = {
                "Color": getattr(P, _KIND_COLOUR.get(kind, "text")).u32
            }
        self.styles = [
            ts.BlockStyle(**{"Align": align, "SpaceBelow": gap, **look[name]})
            for name in _STYLE_NAMES
        ]
        self.style = ts.Style(Align=align, LineHeight=_LINE_HEIGHT_EM, Color=P.text.u32)
        self.options = ts.DocumentOptions(
            QuoteIndent=_INDENT - 3.0,
            QuoteBarThickness=2.0,
            QuoteBarColor=P.border.u32,
            RuleColor=P.border.u32,
            CodeOverflow=ts.CodeOverflow.Clip,  # as add_text leaves a long line today
        )


_STYLE = {name: index for index, name in enumerate(_STYLE_NAMES)}


def _prose(
    ts: Any, st: _Typeset, tokens: Sequence[InlineToken], base: int
) -> tuple[str, list[Any], bool]:
    """Inline tokens as (text, runs, has a hard break), and an underline per link."""
    at = len(st.origin)
    parts: list[str] = []
    runs: list[Any] = []
    pos = 0
    bold = italic = link = hard = False
    for tok in tokens:
        kind = tok.type
        if kind == "strong_open" or kind == "strong_close":
            bold = kind == "strong_open"
            continue
        if kind == "em_open" or kind == "em_close":
            italic = kind == "em_open"
            continue
        if kind == "link_open" or kind == "link_close":
            link = kind == "link_open"
            continue
        if kind == "hardbreak":
            piece, hard = "\n", True
        elif kind == "softbreak":
            piece = " "
        else:
            piece = tok.content  # text, code_inline, and anything else as its literal content
        if not piece:
            continue
        end = pos + len(piece)
        # _run_color and _run_font: strong is the bold face, emphasis a colour.
        colour = (
            P.diff_context.u32
            if kind == "code_inline"
            else P.text_strong.u32 if bold else P.accent.u32 if italic else 0
        )
        if bold or colour:
            runs.append(ts.Run(pos, end, ts.Face.Bold if bold else ts.Face.Default, 0.0, colour))
        if link:
            st.links.append(ts.Mark(((at, pos), (at, end)), colour or base, ts.MarkPlace.Under))
        parts.append(piece)
        pos = end
    return "".join(parts), runs, hard


def _describe(ts: Any, st: _Typeset, block: Block, source: int, out: list[Any]) -> None:
    """Append the document blocks that stand for one of ours, and their bookkeeping."""

    def add(made: Any) -> None:
        out.append(made)
        st.origin.append(source)
        st.texts.append(made.text)

    kind = block.kind
    if kind in _PROSE_KINDS and block.inline is not None:
        base = P.text.u32
        if kind is BlockKind.HEADING:
            base = P.accent.u32
        elif kind is BlockKind.BLOCKQUOTE:
            base = P.text_dim.u32
        text, runs, hard = _prose(ts, st, block.inline, base)
        flags = (ts.BlockFlags.HardBreaks if hard else 0) | (ts.BlockFlags.Math if _MATH else 0)
        if kind is BlockKind.HEADING:
            add(
                ts.Block(
                    ts.BlockKind.Heading,
                    text,
                    runs,
                    style=_STYLE["heading"],
                    flags=flags,
                    level=block.level,
                )
            )
        elif kind is BlockKind.BULLET_ITEM or kind is BlockKind.ORDERED_ITEM:
            marker = (
                "•" if kind is BlockKind.BULLET_ITEM else dict(block.meta).get("start", "1") + "."
            )
            # A marker's own width plus the gap, so "12." cannot run into its text.
            indent = max(_INDENT, (len(marker) + 1) * st.size * 0.5)
            add(
                ts.Block(
                    ts.BlockKind.ListItem,
                    text,
                    runs,
                    marker=marker,
                    style=_STYLE["item"],
                    left_indent=indent,
                    flags=flags,
                )
            )
        elif kind is BlockKind.BLOCKQUOTE:
            add(
                ts.Block(
                    text=text,
                    runs=runs,
                    style=_STYLE["quote"],
                    left_indent=_INDENT,
                    quote_depth=1,
                    flags=flags,
                )
            )
        else:
            add(ts.Block(text=text, runs=runs, style=_STYLE["body"], flags=flags))
    elif kind is BlockKind.TABLE:
        at = len(st.origin)
        st.tables.append(at)
        # A guess at its height until it has been drawn once: a row is a line and
        # its cell padding. A table that is described again because text arrived
        # in it keeps the height it had, which was measured, and what ImGui last
        # drew it at; the guess is a floor, since no row is shorter than a line.
        rows = max(1, len(block.lines) - 1)
        extent = rows * (st.size + 2.0 * imgui.get_style().cell_padding.y)
        held, measured = st.carried.get((at, source), (None, None))
        if held is not None:
            extent = max(extent, held)
        if measured is not None:
            st.measured[at] = measured
        st.extents[at] = extent
        add(
            ts.Block(ts.BlockKind.Host, "\n".join(block.lines), extent=extent, style=_STYLE["body"])
        )
    elif kind is BlockKind.THEMATIC_BREAK:
        add(ts.Block(ts.BlockKind.Rule, extent=1.0, style=_STYLE["rule"]))
    elif kind is BlockKind.FENCE:
        lang = dict(block.meta).get("lang")
        if lang:
            add(ts.Block(text=lang, style=_STYLE["chrome"]))
        add(ts.Block(ts.BlockKind.Code, "\n".join(fence_body(block)), style=_STYLE["code"]))
        if not block.closed:
            add(ts.Block(text="(fence not closed)", style=_STYLE["warn"]))
    else:
        # LITERAL, and live prose too long to parse every frame: verbatim lines.
        add(
            ts.Block(
                ts.BlockKind.Code,
                "\n".join(block.lines),
                style=_STYLE[f"literal-{block.segment_kind.value}"],
            )
        )


def _appendable(ts: Any, old: Any, new: Any) -> bool:
    """Whether ``new`` is ``old`` with more text on the end and nothing else changed.
    Never for a table: its Host block's text is not text the library sets."""
    return (
        new.kind != ts.BlockKind.Host
        and len(new.text) > len(old.text)
        and new.text.startswith(old.text)
        and old._replace(text="", runs=()) == new._replace(text="", runs=())
        and list(old.runs) == list(new.runs)
    )


def _sync(ts: Any, st: _Typeset, blocks: Sequence[Block], live: Block | None) -> None:
    """Bring the document up to date: at most one call into the library."""
    count = len(blocks)
    live_key = None if live is None else (live.kind, live.lines, live.closed)
    whole = st.source is not blocks or count < st.count
    if not whole and count == st.count and live_key == st.live_key:
        return

    first = 0 if whole else st.settled
    cut = bisect.bisect_left(st.tables, first)
    st.carried = {}
    for index in st.tables[cut:]:
        height = (st.extents.pop(index, None), st.measured.pop(index, None))
        if not whole:
            st.carried[(index, st.origin[index])] = height
    del st.tables[cut:]
    del st.origin[first:], st.texts[first:]
    st.folded_from = min(st.folded_from, first)
    st.found_upto = min(st.found_upto, first)
    st.links = [mark for mark in st.links if mark.range[0][0] < first]

    out: list[Any] = []
    for index in range(0 if whole else st.count, count):
        _describe(ts, st, blocks[index], index, out)
    settled = first + len(out)
    if live is not None:
        _describe(ts, st, with_live_inline(live, _LIVE_INLINE_LIMIT), count, out)
    tail = out[settled - first :]

    if whole:
        st.doc.set_blocks(out, st.styles)
        st.doc.set_options(st.options)
        st.selection = None
    elif (
        count == st.count
        and len(tail) == 1 == len(st.tail)
        and _appendable(ts, st.tail[0], tail[0])
    ):
        st.doc.append_text(tail[0].text[len(st.tail[0].text) :])
    else:
        st.doc.replace_blocks(first, len(st.tail), out)
        if st.selection is not None and max(st.selection[0][0], st.selection[1][0]) >= first:
            st.selection = None
    if st.doc.edit_error.code:
        # Applied, with something in the description cut to what the library takes.
        typeset.note(f"description: {st.doc.edit_error.message}")
    st.source, st.count, st.settled, st.tail, st.live_key = blocks, count, settled, tail, live_key
    st.live = live
    st.revision += 1


def _fold(text: str) -> tuple[str, list[int] | None]:
    """
    ``text`` in lower case for a search, and, when lowering changed its length,
    the index in ``text`` each character of the result came from. It does for a
    few characters (a capital I with a dot becomes two), and a match found in the
    lowered text is then somewhere else in the text the document holds.
    """
    folded = text.lower()
    if len(folded) == len(text):
        return folded, None
    parts: list[str] = []
    where: list[int] = []
    for index, char in enumerate(text):
        low = char.lower()
        parts.append(low)
        where.extend([index] * len(low))
    return "".join(parts), where


def _find(ts: Any, st: _Typeset, needle: str) -> list[Any]:
    """
    A mark for each place the document shows ``needle`` (already in lower case),
    ``_FIND_MARK_CAP`` at most. A block is searched when it is new or has changed
    and not again: text that arrives a piece a frame costs a search of the live
    tail, not of the history.
    """
    found = st.found
    if needle != st.found_needle:
        found.clear()
        st.found_needle = needle
        st.found_upto = 0
    while found and found[-1].range[0][0] >= st.found_upto:
        found.pop()
    # Case is folded once a block, not once a keystroke.
    folded = st.folded
    del folded[st.folded_from :]
    folded.extend(_fold(text) for text in st.texts[len(folded) :])
    st.folded_from = st.settled
    if not needle:
        st.found_upto = st.settled
        return found
    colour = faded(P.warn, 0.35)
    size = len(needle)
    behind = ts.MarkPlace.Behind
    index = st.found_upto
    while index < len(folded) and len(found) < _FIND_MARK_CAP:
        text, where = folded[index]
        at = text.find(needle)
        while at >= 0 and len(found) < _FIND_MARK_CAP:
            if where is None:
                start, end = at, at + size
            else:
                start, end = where[at], where[at + size - 1] + 1
            found.append(ts.Mark(((index, start), (index, end)), colour, behind))
            at = text.find(needle, at + size)
        index += 1
    # At the cap the block it was reached in may hold more: it is searched again
    # if the marks before it ever become fewer.
    capped = len(found) >= _FIND_MARK_CAP
    st.found_upto = min(st.settled, found[-1].range[0][0] if capped else index)
    return found


def _marks(ts: Any, st: _Typeset, needle: str) -> Any:
    """Link underlines and find marks, packed once per change of either."""
    key = (st.revision, needle)
    if st.marks_key != key:
        marks = st.links + _find(ts, st, needle)
        st.marks = st.doc.pack_marks(marks) if marks else None
        st.marks_key = key
    return st.marks


def _draw_tables(st: _Typeset, x: float, y: float, width: float) -> None:
    """
    The tables the window shows, each drawn by ``_draw_table`` into its Host block.

    A table's height is known only once ImGui has drawn it, and not on the first
    frame it is drawn: its columns take a frame or three to find their widths, and
    until they have, cells wrap in slivers and the table measures many times its
    height. So each table is drawn in a child the size of its Host block, which
    keeps a table that is momentarily enormous out of the pane's own extent; a
    measurement is believed when it repeats, and then the Host block is told. If
    that table began above the pane's top, the scroll moves by the same amount, so
    that what the operator is reading stays where it is.
    """
    doc = st.doc
    window_top = imgui.get_window_pos().y
    top = max(0.0, window_top - y)
    first = doc.hit_test((0.0, top)).position.block
    last = doc.hit_test((0.0, top + imgui.get_window_height())).position.block
    assert st.source is not None
    flags = imgui.WindowFlags_.no_scrollbar | imgui.WindowFlags_.no_scroll_with_mouse
    for at in range(bisect.bisect_left(st.tables, first), bisect.bisect_right(st.tables, last)):
        index = st.tables[at]
        geometry = doc.block_geometry(index)
        left, upper = x + geometry.full_min[0], y + geometry.full_min[1]
        held = st.extents.get(index, 0.0)
        imgui.set_cursor_screen_pos(imgui.ImVec2(left, upper))
        imgui.push_style_var(imgui.StyleVar_.window_padding, imgui.ImVec2(0.0, 0.0))
        try:
            shown = imgui.begin_child(
                f"##table_{index}", imgui.ImVec2(width, max(held, 1.0)), 0, flags
            )
        finally:
            imgui.pop_style_var()
        # EndChild whatever BeginChild answered and whatever drawing the table does.
        try:
            height = 0.0
            source = st.origin[index]
            table = st.source[source] if source < len(st.source) else st.live
            if shown and table is not None:
                _draw_table(index, table)
                height = imgui.get_cursor_pos().y - imgui.get_style().item_spacing.y
        finally:
            imgui.end_child()
        if height <= 0.0:
            continue  # clipped: nothing was drawn and nothing was learnt
        repeated = abs(height - st.measured.get(index, -1.0)) <= 0.5
        st.measured[index] = height
        if repeated and abs(height - held) > 0.5:
            st.extents[index] = height
            doc.set_block_extent(index, height)
            if upper < window_top:
                imgui.set_scroll_y(imgui.get_scroll_y() + height - held)


def _draw_typeset(ts: Any, rich: RichState, blocks: Sequence[Block], live: Block | None) -> bool:
    """
    The frame through imtypeset. True when the frame is this function's: it drew
    it, or it failed once drawing had begun, and then the frame stays as far as it
    got. False when it failed before anything was put on screen, so that the
    caller's own renderer can draw the frame instead.

    It never raises and never leaves ImGui's state other than it found it: what
    is begun below is ended in a ``finally``. A failure is recorded
    (``typeset.trouble()``) and the document is dropped, to be described again
    from the blocks on the next frame; ``_FAILURES_BEFORE_OFF`` failing frames in
    a row switch the library off for the run, and ``typeset.reason()`` says why.
    """
    begun = False
    try:
        st, width = _prepare(ts, rich, blocks, live)
        begun = True
        _emit(rich, st, width, blocks, live)
    except Exception as exc:  # noqa: BLE001 -- a pane that draws beats one that raises
        why = f"{type(exc).__name__}: {exc}"
        rich.typeset = None
        rich.typeset_failures += 1
        if rich.typeset_failures >= _FAILURES_BEFORE_OFF:
            typeset.disable(f"{why} ({rich.typeset_failures} frames in a row)")
        else:
            typeset.note(why)
        return begun
    rich.typeset_failures = 0
    return True


def _prepare(
    ts: Any, rich: RichState, blocks: Sequence[Block], live: Block | None
) -> tuple[_Typeset, float]:
    """Everything a frame needs that draws nothing and begins nothing in ImGui."""
    size = imgui.get_font_size()
    st = rich.typeset
    if st is None or st.palette is not current() or st.size != size:
        st = rich.typeset = _Typeset(ts, size)
    _sync(ts, st, blocks, live)
    width = max(1.0, imgui.get_content_region_avail().x)
    st.doc.layout(width, st.style)
    _marks(ts, st, rich.find.lower())
    return st, width


def _copy(doc: Any, selection: Any) -> None:
    """The selection to the clipboard. One that cannot be read is recorded and
    nothing is copied: it is no reason to stop drawing through the library."""
    try:
        text = doc.range_text(selection)
    except Exception as exc:  # noqa: BLE001
        typeset.note(f"copy: {type(exc).__name__}: {exc}")
        return
    imgui.set_clipboard_text(text)


def _emit(
    rich: RichState, st: _Typeset, width: float, blocks: Sequence[Block], live: Block | None
) -> None:
    """Draws the frame: the document, its tables, the pane's item, mouse and menu."""
    doc = st.doc
    origin = imgui.get_cursor_screen_pos()
    x, y = origin.x, origin.y
    doc.draw(
        typeset.window_draw_list(),
        (x, y),
        selection=st.selection,
        marks=st.marks,  # packed by _prepare
    )
    # A table that cannot be drawn does not take the pane's item with it: the
    # text is on screen, so the frame is finished and the failure raised after.
    failed: Exception | None = None
    if st.tables:
        try:
            _draw_tables(st, x, y, width)
        except Exception as exc:  # noqa: BLE001
            failed = exc
        imgui.set_cursor_screen_pos(imgui.ImVec2(x, y))

    imgui.invisible_button("##rich", imgui.ImVec2(width, max(doc.size()[1], 1.0)))
    hovered = imgui.is_item_hovered()
    active = imgui.is_item_active()
    menu_open = imgui.is_popup_open("##rich_block_ctx")
    if hovered or active:
        mouse = imgui.get_mouse_pos()
        local = (mouse.x - x, mouse.y - y)
        if hovered and not menu_open:
            hit = doc.hit_test(local)
            rich.context_block = st.origin[hit.position.block] if hit.in_block else None
        # A drag selects: by character, by word after a double click, by line
        # after a triple one. Ctrl+C copies what is selected.
        if imgui.is_item_activated():
            st.anchor = local
            st.granularity = min(max(imgui.get_mouse_clicked_count(0), 1), 3) - 1
            st.selection = None
        elif active and st.anchor is not None:
            picked = doc.select_range(st.anchor, local, st.granularity)
            st.selection = None if picked.start == picked.end else picked
    elif not menu_open:
        rich.context_block = None
    if (
        st.selection is not None
        and imgui.is_window_focused()
        and imgui.get_io().key_ctrl
        and imgui.is_key_pressed(imgui.Key.c, False)
    ):
        _copy(doc, st.selection)

    if imgui.begin_popup_context_item("##rich_block_ctx"):
        try:
            if st.selection is not None and imgui.menu_item_simple("copy selection"):
                _copy(doc, st.selection)
            target = rich.context_block
            if target is not None:
                block = blocks[target] if target < len(blocks) else live
                if block is not None:
                    label = block.kind.value.replace("_", " ")
                    if imgui.menu_item_simple(f"copy {label}"):
                        imgui.set_clipboard_text("\n".join(block.lines))
        finally:
            imgui.end_popup()
    if failed is not None:
        raise failed
