#!/usr/bin/env python3
"""
Dump table and column goldens from the Python reference (imgui-adv-text).

Writes tests/golden/tables/*.golden: a table's column widths and every row set
to them, at several measures; and one vertical list cut into columns. The
record vocabulary is tools/oracle/FORMAT-tables.md, over FORMAT-blocks.md,
FORMAT-para.md and FORMAT.md, whose records are written by the functions of
the sibling scripts dump_blocks.py, dump_para.py and dump_core.py, imported
here and not edited.

Run from the imtypeset repository root, with the reference's interpreter:

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=build/advtext-reference \\
        ../imgui-adv-text/.venv/bin/python -P -B tools/oracle/dump_tables.py

    ... dump_tables.py --verify   re-reads every golden, rebuilds each case
                                  from its INPUT records alone, rebuilds the
                                  result from the EXPECTED records with a
                                  reader that calls nothing in advtext.ui or
                                  advtext.columns, and replays the case through
                                  the reference and compares every record.

The script only reads the reference. Output is deterministic: a second run is
byte-identical to the first.

Every expectation is an answer of one of the reference's own functions:

    a table            advtext.markdown.build_document (the TableBlock is
                       captured; markdown is not an input), ui.with_columns
    limits             ui.table_column_limits, ui._natural_widths,
                       ui._longest_word, ui.table_natural_widths
    column widths      ui._column_widths and whichever of ui._apportioned,
                       ui._squeezed, ui._shared it called
    rows               ui._table_boxes: ui._table_row, ui._wrapped_row,
                       ui._cell_lines
    the table box      ui._stacked, ui.compose_blocks, shipout.ship
    columns            columns.split_into_columns: prune_top, vsplit,
                       vert_break, _break_badness; compose_into_columns

**Intermediate values are read, not recomputed.** The reference's functions
are run under `sys.settrace` (`Tracer`), which keeps, for each call of a
function named to it, the arguments, the locals as they stood when the call
returned, the value of each local when it was first bound, and the locals at
any source line it was asked to stop at. `slack`, `give`, the widths before the
last column takes the remainder, the running height at each legal breakpoint
of a vertical list: each is a local of the reference, copied out. Nothing is
patched and no logic is restated; where a record needs a number that is not a
local (the sum of a list, an index into the caller's material) the comment at
that line says so.
"""

from __future__ import annotations

import argparse
import contextlib
import inspect
import sys
from collections.abc import Callable, Iterator, Sequence
from dataclasses import dataclass, field, replace
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

import pinned_reference  # noqa: E402

pinned_reference.require()

import dump_blocks as blocks  # noqa: E402
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
    emit_spec,
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
from dump_para import Capture, Consulted  # noqa: E402

from advtext import columns as C  # noqa: E402
from advtext import markdown, ui  # noqa: E402
from advtext.fontmetrics import Face  # noqa: E402
from advtext.mathtext import markdown_math  # noqa: E402
from advtext.nodes import (  # noqa: E402
    EJECT_PENALTY,
    INF_PENALTY,
    Box,
    BoxKind,
    Glue,
    GlueSign,
    GlueSpec,
    Kern,
    KernKind,
    Node,
    Penalty,
    Rule,
)
from advtext.pack import IGNORE_DEPTH, CharDimens, PackFitness, PackMode, PackResult, hpack, vpack  # noqa: E402
from advtext.shipout import Point, is_atom, ship  # noqa: E402
from advtext.style import (  # noqa: E402
    Align,
    BlockKind,
    BlockStyle,
    Emphasis,
    Faces,
    Spacing,
    Strut,
    Theme,
    fontlib_metrics,
    parse_font_key,
)
from advtext.units import MAX_DIMEN, UNITY, GlueOrder, Scaled, sp_from_pt, sp_from_px  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OUT_DIR = ROOT / "tests" / "golden" / "tables"
REFERENCE = pinned_reference.ROOT_DIR
FORMAT_VERSION = 1
MEASURE = para.MEASURE
PARAMS = ui.PROSE_BREAKING
PT = UNITY


# --------------------------------------------------------------------------
# Provenance: which reference these files were dumped from
# --------------------------------------------------------------------------


def provenance() -> list[str]:
    import fontTools
    import imgui_bundle
    import markdown_it

    # The commit is the one tools/oracle/REFERENCE.lock pins; pinned_reference
    # has verified that the tree `advtext` was imported from is that commit's.
    lines = pinned_reference.header_lines()
    lines.append(
        f"Versions: imgui_bundle {imgui_bundle.__version__}, fontTools {fontTools.version}, "
        f"markdown-it-py {markdown_it.__version__}, Python {sys.version.split()[0]}."
    )
    return lines


_PROVENANCE: list[str] | None = None


class Writer(core.Writer):
    """
    dump_core's writer, with this script's header, provenance and file id.
    """

    def __init__(self, name: str, about: str) -> None:
        global _PROVENANCE
        super().__init__(name, about)
        if _PROVENANCE is None:
            _PROVENANCE = provenance()
        self.lines = [
            "# Generated by tools/oracle/dump_tables.py from the Python reference. Do not edit.",
            "# Record vocabulary: tools/oracle/FORMAT-tables.md, over FORMAT-blocks.md, FORMAT-para.md and FORMAT.md.",
            *[f"# {line}" for line in _PROVENANCE],
            *[f"# {line}" for line in about.strip().splitlines()],
            f"golden {FORMAT_VERSION} {q('tables/' + name)}",
        ]


# --------------------------------------------------------------------------
# Reading the reference's intermediate values
# --------------------------------------------------------------------------


def _snap(value: Any) -> Any:
    """
    A local as it is now: a list is copied, since the reference edits its lists
    in place (`widths[-1] += ...`).
    """
    return list(value) if isinstance(value, list) else value


@dataclass(slots=True)
class Call:
    name: str
    args: dict[str, Any]
    firsts: dict[str, Any] = field(default_factory=dict)
    marks: dict[str, list[dict[str, Any]]] = field(default_factory=dict)
    locals: dict[str, Any] = field(default_factory=dict)
    result: Any = None
    children: list[Call] = field(default_factory=list)

    def named(self, name: str) -> list[Call]:
        return [child for child in self.children if child.name == name]


class Tracer:
    """
    Runs a call of the reference and returns the tree of calls it made to the
    functions named here, each with its arguments, locals and result.

    `plain` functions are recorded at call and return. `lined` ones also have
    each local recorded when first bound, and the whole frame at every source
    line whose text (stripped) is one of the marks given for it.
    """

    def __init__(self, plain: Sequence[Any], lined: dict[Any, Sequence[str]]) -> None:
        self._codes: dict[Any, tuple[str, bool, dict[int, str]]] = {}
        for function in plain:
            self._codes[function.__code__] = (function.__name__, False, {})
        for function, texts in lined.items():
            source, start = inspect.getsourcelines(function)
            at: dict[int, str] = {}
            for text in texts:
                found = [start + offset for offset, line in enumerate(source) if line.strip() == text]
                assert len(found) == 1, f"{function.__name__}: {text!r} is on {len(found)} lines"
                at[found[0]] = text
            self._codes[function.__code__] = (function.__name__, True, at)

    def run(self, function: Callable[..., Any], *args: Any, **kwargs: Any) -> tuple[Any, list[Call]]:
        roots: list[Call] = []
        stack: list[Call] = []
        codes = self._codes

        def tracer(frame: Any, event: str, arg: Any) -> Any:
            if event != "call":
                return None
            entry = codes.get(frame.f_code)
            if entry is None:
                return None
            name, lined, marks = entry
            call = Call(name, {key: _snap(value) for key, value in frame.f_locals.items()})
            (stack[-1].children if stack else roots).append(call)
            stack.append(call)

            def local(frame: Any, event: str, arg: Any) -> Any:
                if event == "line" and lined:
                    seen = frame.f_locals
                    for key, value in seen.items():
                        if key not in call.firsts:
                            call.firsts[key] = _snap(value)
                    text = marks.get(frame.f_lineno)
                    if text is not None:
                        call.marks.setdefault(text, []).append({key: _snap(value) for key, value in seen.items()})
                elif event == "return":
                    call.locals = {key: _snap(value) for key, value in frame.f_locals.items()}
                    call.result = arg
                    popped = stack.pop()
                    assert popped is call
                return local

            return local

        previous = sys.gettrace()
        sys.settrace(tracer)
        try:
            result = function(*args, **kwargs)
        finally:
            sys.settrace(previous)
        assert not stack
        return result, roots


SHARES_SETTLED = "if shares[-1] < 1:"
COST_KNOWN = "if cost <= least_cost:"

TABLE_TRACER = Tracer(
    plain=[
        ui._stacked,
        ui._table_boxes,
        ui.table_column_limits,
        ui._natural_widths,
        ui._longest_word,
        ui._table_row,
        ui._wrapped_row,
        ui._cell_lines,
        ui.break_paragraph,
        ui.append_to_vlist,
    ],
    lined={
        ui._column_widths: (),
        ui._apportioned: (),
        ui._squeezed: (),
        ui._shared: (SHARES_SETTLED,),
    },
)

COLUMN_TRACER = Tracer(
    plain=[C.split_into_columns, C.vsplit, C.prune_top, C._break_badness],
    lined={C.vert_break: (COST_KNOWN,)},
)


# --------------------------------------------------------------------------
# The INPUT: a table as data
# --------------------------------------------------------------------------

# ("run", font key, breakable, text) -- dump_para's -- or ("atom", box)
Event = tuple[Any, ...]


@dataclass(frozen=True, slots=True)
class ThemeIn:
    """
    Every Theme value building and setting a table reads.
    """

    px_per_inch: float
    spacing: Spacing
    cell_padding: Scaled
    table_rule_thickness: Scaled
    faces: tuple[str, str, str]

    @classmethod
    def of(cls, theme: Theme) -> ThemeIn:
        return cls(
            px_per_inch=float(theme.px_per_inch),
            spacing=theme.spacing,
            cell_padding=theme.cell_padding,
            table_rule_thickness=theme.table_rule_thickness,
            faces=tuple(face.value for face in para.stand_in_faces(theme)),  # type: ignore[arg-type]
        )

    def cut_down(self, style: BlockStyle | None) -> Theme:
        """
        A Theme holding the recorded values and nothing else: every other
        field is zero, so a path that read one would not reproduce the answers.
        """
        assert self.faces[1] == Face.MATH.value
        return Theme(
            blocks={} if style is None else {BlockKind.TABLE: style},
            px_per_inch=self.px_per_inch,
            marker_gap=0,
            list_indent=0,
            quote_indent=0,
            rule_thickness=0,
            quote_rule_thickness=0,
            cell_padding=self.cell_padding,
            table_rule_thickness=self.table_rule_thickness,
            spacing=self.spacing,
            faces=Faces(regular=Face(self.faces[0]), code=Face(self.faces[2])),
        )


@dataclass(frozen=True, slots=True)
class TableIn:
    style: BlockStyle  # without its own left_indent, as dump_blocks records one
    left_indent: Scaled
    has_header: bool
    rows: tuple[tuple[tuple[Event, ...], ...], ...]  # every row, the header first
    aligns: tuple[markdown.ColumnAlign, ...]
    widths: tuple[Scaled, ...] = ()
    fractions: tuple[float, ...] = ()


def build_cell(theme: Theme, resolve: Callable[[str], Any], events: Sequence[Event]) -> markdown.TableCell:
    """
    The direct driver for one cell: recorded events in, the reference's cell out.

    Exactly the calls `_Converter._inline` makes for a table cell: one
    `_append_text` per run, and `_formula` for an inline formula, handed the
    recorded box in the place of a formula builder.
    """
    converter = markdown._Converter(theme, resolve)
    run = markdown._Run()
    for event in events:
        if event[0] == "run":
            converter._append_text(run, event[3], parse_font_key(event[1]), event[2])
        else:
            assert event[0] == "atom"
            inner = event[1].contents[1]
            converter._math = lambda source, display, size, inner=inner: inner
            converter._formula(run, BlockKind.TABLE, Emphasis(), "", False, False)
    return markdown.TableCell(material=tuple(run.nodes))


def rebuild(theme_in: ThemeIn, table: TableIn, resolve: Callable[[str], Any]) -> markdown.TableBlock:
    """
    A table from its records alone, through the reference's own constructors.
    """
    theme = theme_in.cut_down(table.style)
    rows = [tuple(build_cell(theme, resolve, cell) for cell in row) for row in table.rows]
    header = rows[0] if table.has_header else ()
    body = tuple(rows[1:] if table.has_header else rows)
    plain = markdown.TableBlock(
        style=table.style, header=header, rows=body, aligns=table.aligns, left_indent=table.left_indent
    )
    if table.widths:
        return ui.with_columns(plain, widths=table.widths)
    if table.fractions:
        return ui.with_columns(plain, fractions=table.fractions)
    return plain


def normalised(block: markdown.TableBlock) -> markdown.TableBlock:
    """
    A reference table with what the records leave out removed: link and
    formula spans, the `header` flag of each cell, the style's own indent.
    """

    def bare(row: tuple[markdown.TableCell, ...]) -> tuple[markdown.TableCell, ...]:
        return tuple(markdown.TableCell(material=cell.material) for cell in row)

    return replace(
        block,
        style=blocks.recorded_style(block.style),
        header=bare(block.header),
        rows=tuple(bare(row) for row in block.rows),
    )


@dataclass(slots=True)
class TableCapture:
    """
    dump_para's capture of `_append_text`, plus which run each cell came from
    and the box of each inline formula.
    """

    runs: Capture = field(default_factory=Capture)
    inlines: list[Any] = field(default_factory=list)
    tables: list[tuple[markdown.TableBlock, list[Any]]] = field(default_factory=list)

    @contextlib.contextmanager
    def installed(self) -> Iterator[TableCapture]:
        converter = markdown._Converter
        inline, formula, table = converter._inline, converter._formula, converter._table

        def recording_inline(conv: Any, kind: BlockKind, tokens: Any) -> Any:
            run = inline(conv, kind, tokens)
            self.inlines.append(run)
            return run

        def recording_formula(conv: Any, run: Any, *args: Any) -> None:
            formula(conv, run, *args)
            span = run.formulas[-1]
            if span.refusal or span.unclosed:
                return  # set as source in the code face: `_append_text` saw it
            atom = run.nodes[-1]
            assert isinstance(atom, Box) and is_atom(atom)
            if id(run) not in self.runs._by_run:
                self.runs._by_run[id(run)] = []
                self.runs._pinned.append(run)
            self.runs._last = self.runs._by_run[id(run)]
            self.runs._last.append(("atom", atom))

        def recording_table(conv: Any, tokens: Any, start: int) -> int:
            before = len(self.inlines)
            out = table(conv, tokens, start)
            self.tables.append((conv._blocks[-1], self.inlines[before:]))
            return out

        with self.runs.installed():
            converter._inline = recording_inline  # type: ignore[method-assign]
            converter._formula = recording_formula  # type: ignore[method-assign]
            converter._table = recording_table  # type: ignore[method-assign]
            try:
                yield self
            finally:
                converter._inline = inline  # type: ignore[method-assign]
                converter._formula = formula  # type: ignore[method-assign]
                converter._table = table  # type: ignore[method-assign]

    def input_of(self, block: markdown.TableBlock) -> TableIn:
        (found,) = [runs for made, runs in self.tables if made is block]
        rows = ui._table_rows(block)
        assert sum(len(row) for row in rows) == len(found)
        at = 0
        out: list[tuple[tuple[Event, ...], ...]] = []
        for row in rows:
            cells = []
            for cell in row:
                run = found[at]
                at += 1
                assert tuple(run.nodes) == cell.material
                cells.append(tuple(self.runs._by_run.get(id(run), ())))
            out.append(tuple(cells))
        return TableIn(
            style=blocks.recorded_style(block.style),
            left_indent=block.left_indent,
            has_header=bool(block.header),
            rows=tuple(out),
            aligns=tuple(block.aligns),
        )


# --------------------------------------------------------------------------
# Emitting a table's INPUT
# --------------------------------------------------------------------------


def ints(values: Sequence[int]) -> tuple[object, ...]:
    return (len(values), *values)


def emit_table_inputs(
    w: Writer, theme_in: ThemeIn, table: TableIn, block: markdown.TableBlock, consulted: Consulted
) -> None:
    w.r("theme", theme_in.px_per_inch, theme_in.spacing, theme_in.cell_padding, theme_in.table_rule_thickness)
    w.r("faces", *[q(face) for face in theme_in.faces])
    consulted.emit(w)
    rows = ui._table_rows(block)
    emit_measure(w, MEASURE, *[cell.material for row in rows for cell in row])
    style = table.style
    columns = max((len(row) for row in rows), default=0)
    w.r(
        "table",
        columns,
        len(rows),
        table.has_header,
        table.left_indent,
        style.space_above,
        style.space_below,
        q(style.font.key),
    )
    blocks.emit_style(w, style)
    w.r("aligns", len(table.aligns), *table.aligns)
    if table.widths:
        w.r("policy", "widths", *ints(table.widths))
    elif table.fractions:
        w.r("policy", "fractions", len(table.fractions), *[float(share) for share in table.fractions])
    else:
        w.r("policy", "apportioned")
    for r, (row_in, row) in enumerate(zip(table.rows, rows, strict=True)):
        w.r("row", r, len(row))
        for c, (events, cell) in enumerate(zip(row_in, row, strict=True)):
            w.r("cell", r, c, len(events))
            for event in events:
                if event[0] == "run":
                    w.r("run", q(event[1]), event[2], q(event[3]))
                else:
                    w.r("atom")
                    emit_node(w, event[1])
            emit_nodes(w, "material", cell.material)
            emit_nodes(w, "unshrinkable", ui._unshrinkable(cell.material))
            w.r("endcell")
        w.r("endrow")
    w.r("endtable")


# --------------------------------------------------------------------------
# Emitting a table's EXPECTED records
# --------------------------------------------------------------------------


def emit_limits(w: Writer, name: str, block: markdown.TableBlock, theme: Theme) -> None:
    """
    What a table wants whatever the measure: each cell on one line, and each
    cell's longest unbreakable stretch.
    """
    limits, roots = TABLE_TRACER.run(ui.table_column_limits, block, theme, MEASURE, PARAMS)
    rows = ui._table_rows(block)
    w.r("limits")
    if rows:
        (root,) = roots
        (natural,) = [call.result for call in root.named("_natural_widths")]
        words = root.named("_longest_word")
        for r, sizes in enumerate(natural):
            w.r("natural", r, *ints(sizes))
        assert limits.natural == ui.table_natural_widths(block, theme, MEASURE)
        w.r("widest", *ints(limits.natural))
        at = 0
        for r, row in enumerate(rows):
            for c, cell in enumerate(row):
                call = words[at]
                at += 1
                assert call.args["material"] is cell.material
                broke = call.named("break_paragraph")
                w.r("word", r, c, call.result)
                if broke:
                    (asked,) = broke
                    paragraph = emit_run(
                        w, f"{name}/word/{r}.{c}", "material", asked.args["nodes"], asked.args["shape"], asked.args["params"], MEASURE, None
                    )
                    assert paragraph == asked.result
                    # The reference measures each line in a generator of its
                    # own; the same call, made again, and held to its maximum.
                    sizes = [hpack(line.contents, measure=MEASURE).box.width for line in paragraph.boxes]
                    assert max(sizes, default=0) == call.result
                    w.r("wordlines", *ints(sizes))
        assert at == len(words)
        w.r("floor", *ints(limits.floor))
    else:
        assert not roots[0].children and limits == ui.ColumnLimits((), ())
    w.r("endlimits")


def emit_solve(w: Writer, block: markdown.TableBlock, solved: Call) -> list[Scaled]:
    """
    One `_column_widths` call: which regime, and the numbers it went through.
    """
    widths = solved.result
    here = solved.locals
    kids = solved.children
    if not kids:
        w.r("solve", "given", here["available"], here["columns"])
    elif kids[0].name == "_shared":
        (shared,) = kids
        w.r("solve", "fractions", here["available"], here["columns"])
        w.r("total", float(shared.firsts["total"]))
        w.r("rounded", *ints(shared.firsts["shares"]))
        (settled,) = shared.marks[SHARES_SETTLED]
        w.r("last", settled["shares"][-1])
        w.r("borrowed", shared.locals.get("widest", -1))
    elif kids[-1].name == "_apportioned":
        (scaled,) = kids
        fitted = "scaled" in scaled.locals
        w.r("solve", "apportioned" if fitted else "unscaled", here["available"], here["columns"])
        w.r("widest", *ints(here["widest"]))
        w.r("total", scaled.locals["total"])
        if fitted:
            w.r("floored", *ints(scaled.firsts["scaled"]))
    else:
        squeezed = kids[-1]
        assert squeezed.name == "_squeezed" and all(kid.name == "_longest_word" for kid in kids[:-1])
        there = squeezed.locals
        how = "floors" if "give" not in there else "rigid" if "widths" not in there else "squeezed"
        w.r("solve", how, here["available"], here["columns"])
        w.r("widest", *ints(here["widest"]))
        w.r("narrowest", *ints(here["narrowest"]))
        w.r("slack", there["slack"])
        if "give" in there:
            w.r("give", *ints(there["give"]))
            w.r("total", there["total"])
        if "widths" in there:
            w.r("floored", *ints(squeezed.firsts["widths"]))
    w.r("columns", *ints(widths))
    w.r("endsolve")
    return widths


def emit_layout(w: Writer, name: str, block: markdown.TableBlock, theme: Theme, width: Scaled) -> Box:
    """
    The table at one measure: the widths, every row, the stack, the commands.
    """
    extents: list[tuple[Scaled, Scaled]] = []
    vlist, roots = TABLE_TRACER.run(ui._stacked, (block,), theme, width, MEASURE, PARAMS, 0, None, extents)
    (root,) = roots
    (table,) = root.named("_table_boxes")
    boxes: list[Box] = table.result
    w.r("layout", q(name), width)

    rows = ui._table_rows(block)
    set_rows: list[Box] = []
    rule: Box | None = None
    if rows:
        (solved,) = table.named("_column_widths")
        widths = emit_solve(w, block, solved)
        assert tuple(widths) == ui.table_columns(block, theme, width, MEASURE)
        assert tuple(widths) == ui.TableMeasure().columns(block, theme, width, MEASURE)
        row_calls = [call for call in table.children if call.name in ("_table_row", "_wrapped_row")]
        assert len(row_calls) == len(rows) and len(boxes) in (len(rows), len(rows) + 1)
        at = 0
        for r, call in enumerate(row_calls):
            strutted = boxes[at]
            at += 1
            if call.name == "_table_row":
                w.r("row", r, "fits")
                emit_node(w, call.result)
            else:
                w.r("row", r, "wrapped")
                for c, cell_call in enumerate(call.named("_cell_lines")):
                    (asked,) = cell_call.named("break_paragraph")
                    there = cell_call.locals
                    w.r("cell", c, cell_call.args["width"], there["inner"])
                    paragraph = emit_run(
                        w, f"{name}/{r}.{c}", "unshrinkable", asked.args["nodes"], asked.args["shape"], asked.args["params"], MEASURE, None
                    )
                    assert paragraph == asked.result and list(paragraph.boxes) == list(there["lines"])
                    appends = cell_call.named("append_to_vlist")
                    assert len(appends) == len(there["lines"])
                    w.r("lines", len(appends))
                    walk = iter(there["vlist"])
                    for line, appended in zip(there["lines"], appends, strict=True):
                        node = next(walk)
                        w.r("append", appended.args["prev_depth"], appended.result)
                        if isinstance(node, Glue):
                            emit_node(w, node)
                            node = next(walk)
                        assert isinstance(node, Box)
                        assert node == replace(line, shift_amount=node.shift_amount, height=node.height, depth=node.depth)
                        w.r("dims", node.width, node.height, node.depth, node.shift_amount)
                    assert next(walk, None) is None
                    stacked = there["stacked"]
                    w.r("stacked", stacked.width, stacked.height, stacked.depth)
                    cell, below = cell_call.result
                    assert there["first"] == cell.height and there["below"] == below
                    assert cell.contents == stacked.contents and cell.kind is BoxKind.VLIST
                    w.r("vtop", cell.width, cell.height, cell.depth, below)
                    w.r("endcell")
                made = call.result
                w.r("rowbox", *blocks.box_header(made))
                w.r("lowest", made.last_baseline)
            assert strutted == replace(call.result, height=strutted.height, depth=strutted.depth)
            w.r("strutted", strutted.width, strutted.height, strutted.depth, strutted.shift_amount)
            if call.name == "_wrapped_row":
                w.r("resumed", strutted.resumed_depth)
            w.r("endrow")
            set_rows.append(strutted)
            if r == 0 and len(boxes) == len(rows) + 1:
                rule = boxes[at]
                at += 1
                kern, ruled = rule.contents
                assert isinstance(kern, Kern) and isinstance(ruled, Rule)
                w.r("headrule")
                emit_node(w, rule)
        assert at == len(boxes)
    else:
        assert not boxes and not table.children

    appends = root.named("append_to_vlist")
    w.r("stack", len(appends))
    walk = iter(vlist)
    for appended in appends:
        node = next(walk)
        glue = None
        if isinstance(node, Glue):
            glue, node = node, next(walk)
        assert node is appended.args["item"]
        w.r("append", appended.args["prev_depth"], appended.result, ui.prev_depth_after(node))
        if glue is not None:
            emit_node(w, glue)
        if node is rule:
            w.r("ref", "headrule")
        else:
            (r,) = [index for index, candidate in enumerate(set_rows) if candidate is node]
            w.r("ref", "row", r)
    assert next(walk, None) is None
    w.r("endstack")
    (extent,) = extents
    w.r("extents", 1, *extent)

    vbox = ui.compose_blocks((block,), theme, width, MEASURE)
    assert vbox == vpack(vlist).box
    origin = Point(0, vbox.height)
    w.r("vbox", vbox.width, vbox.height, vbox.depth)
    w.r("origin", origin.x, origin.y)
    emit_cmds(w, ship(vbox, origin, measure=MEASURE))
    w.r("endlayout")
    return vbox


# --------------------------------------------------------------------------
# One table case
# --------------------------------------------------------------------------


def widths_of(theme: Theme, widths_px: Sequence[float]) -> list[tuple[str, Scaled]]:
    return [(f"ui-{px:g}px", sp_from_px(px, theme.px_per_inch)) for px in widths_px]


def emit_table_case(
    w: Writer,
    name: str,
    source_kind: str,
    source: str,
    theme: Theme,
    table: TableIn,
    original: markdown.TableBlock | None,
    widths_px: Sequence[float],
) -> None:
    """
    One table: its cells as data, its limits, then its layout at each width.

    The expectations are computed from the table rebuilt from the records
    under the cut-down Theme, after asserting, per width, that it composes to
    exactly the box the reference's own table does under its own Theme.
    """
    theme_in = ThemeIn.of(theme)
    consulted = Consulted()
    block = rebuild(theme_in, table, consulted.resolve)
    if original is not None:
        assert block == normalised(original), f"{name}: the records do not rebuild the table"
    cut = theme_in.cut_down(None)
    w.case(name, "table")
    w.r("source", source_kind, q(source))
    emit_table_inputs(w, theme_in, table, block, consulted)
    emit_limits(w, name, block, cut)
    for run_name, width in widths_of(theme, widths_px):
        vbox = emit_layout(w, run_name, block, cut, width)
        if original is not None:
            assert vbox == ui.compose_blocks((original,), theme, width, MEASURE), f"{name}: {run_name}"
    w.end()


def tables_of(source: str, theme: Theme) -> list[tuple[TableIn, markdown.TableBlock]]:
    capture = TableCapture()
    with capture.installed():
        made = markdown.build_document(source, theme, fontlib_metrics, math=markdown_math()).blocks
    return [(capture.input_of(block), block) for block in made if isinstance(block, markdown.TableBlock)]


def md_table(
    w: Writer,
    name: str,
    source: str,
    theme: Theme,
    widths_px: Sequence[float],
    *,
    widths: Callable[[Theme], Sequence[Scaled]] | None = None,
    fractions: Sequence[float] | None = None,
    aligns: Sequence[markdown.ColumnAlign] | None = None,
) -> None:
    """
    The one table of a markdown source, optionally sized by `ui.with_columns`.
    """
    ((table, original),) = tables_of(source, theme)
    if aligns is not None:
        table = replace(table, aligns=tuple(aligns))
        original = replace(original, aligns=tuple(aligns))
    if widths is not None:
        original = ui.with_columns(original, widths=widths(theme))
        table = replace(table, widths=original.column_widths)
    if fractions is not None:
        original = ui.with_columns(original, fractions=fractions)
        table = replace(table, fractions=original.column_fractions)
    emit_table_case(w, name, "markdown", source, theme, table, original, widths_px)


def built_table(
    w: Writer,
    name: str,
    theme: Theme,
    rows: Sequence[Sequence[str]],
    widths_px: Sequence[float],
    *,
    header: bool = True,
    aligns: Sequence[markdown.ColumnAlign] = (),
    left_indent: Scaled = 0,
) -> None:
    """
    A table markdown cannot write: ragged rows, no header, no rows, alignments
    that are not one per column. Each cell is one breakable run in the table's
    face.
    """
    style = theme.style(BlockKind.TABLE)
    font = style.font.key
    table = TableIn(
        style=blocks.recorded_style(style),
        left_indent=left_indent,
        has_header=header and bool(rows) and bool(rows[0]),
        rows=tuple(tuple((para.run(font, text),) if text else () for text in row) for row in rows),
        aligns=tuple(aligns),
    )
    emit_table_case(w, name, "built", "", theme, table, None, widths_px)


# --------------------------------------------------------------------------
# The table files
# --------------------------------------------------------------------------

L, CTR, R = markdown.ColumnAlign.LEFT, markdown.ColumnAlign.CENTER, markdown.ColumnAlign.RIGHT

PROSE = (
    "| Name | What it does | Font |\n"
    "|:-----|:------------:|-----:|\n"
    "| alpha | Breaks a paragraph into lines by choosing every break at once | no |\n"
    "| bravo | Short | yes |\n"
    "| charlie | Packs a horizontal list to a width and reports how it went | no |\n"
)

ALIGNED = (
    "| Left | Centre | Right | Default |\n"
    "|:-----|:------:|------:|---------|\n"
    "| a | b | c | d |\n"
    "| a longer cell | a longer cell | a longer cell | a longer cell |\n"
)

MATH = (
    "| Quantity | Formula | Note |\n"
    "| --- | :---: | ---: |\n"
    "| Energy | $E = mc^2$ | rest mass |\n"
    "| Root | $x = \\frac{-b}{2a}$ and the other one | a fraction is taller than its line |\n"
    "| Refused | $\\begin{matrix}a\\end{matrix}$ | set as source |\n"
)


def dump_demo() -> Writer:
    w = Writer(
        "demo",
        """
Every table of the reference's demo/tables.md, and the one of demo/sample.md,
each alone, at widths from very narrow to wider than it needs: all the
regimes of the default apportioning on real cells.
""",
    )
    theme = Theme.build()
    source = (REFERENCE / "demo" / "tables.md").read_text(encoding="utf-8")
    names = ("short-cells", "prose-in-cells", "alignment-by-column", "too-wide-for-the-pane", "links-in-cells")
    plans = ((40.0, 240.0, 640.0), (40.0, 240.0, 400.0, 640.0, 1400.0), (120.0, 240.0, 640.0), (240.0, 640.0, 1000.0), (240.0, 640.0))
    found = tables_of(source, theme)
    assert len(found) == len(names)
    for index, ((table, original), name, plan) in enumerate(zip(found, names, plans, strict=True)):
        emit_table_case(w, name, "demo-file", f"demo/tables.md#{index}", theme, table, original, plan)
    source = (REFERENCE / "demo" / "sample.md").read_text(encoding="utf-8")
    ((table, original),) = tables_of(source, theme)
    emit_table_case(w, "sample-modules", "demo-file", "demo/sample.md#0", theme, table, original, (240.0, 467.0, 1070.0))
    ragged = Theme.build(13.0, align=Align.LEFT)
    source = (REFERENCE / "demo" / "tables.md").read_text(encoding="utf-8")
    table, original = tables_of(source, ragged)[1]
    emit_table_case(w, "prose-in-cells-13px", "demo-file", "demo/tables.md#1", ragged, table, original, (240.0, 400.0))
    return w


def dump_shapes() -> Writer:
    w = Writer(
        "shapes",
        """
The shapes a table can have: one cell; a header alone; no header; no rows;
ragged rows; empty cells and an empty column; one alignment per column, too
few and too many; inline math in a cell; many rows; a table inside a list; a
second theme.
""",
    )
    theme = Theme.build()
    narrow_to_wide = (40.0, 240.0, 640.0)
    md_table(w, "one-by-one", "| only |\n|---|\n", theme, (10.0, 240.0))
    md_table(w, "header-and-one-row", "| a | b |\n|---|---|\n| 1 | 2 |\n", theme, narrow_to_wide)
    built_table(w, "one-cell-no-header", theme, [["alone"]], (240.0,), header=False)
    built_table(w, "no-header-three-rows", theme, [["a", "b"], ["c", "d"], ["e", "f"]], (240.0,), header=False)
    built_table(w, "no-rows-at-all", theme, [], (240.0,))
    built_table(
        w,
        "ragged-rows",
        theme,
        [["h1", "h2"], ["one"], ["one", "two", "three and a little more"], [], ["one", "two", "three", "four"]],
        (60.0, 240.0, 640.0),
        aligns=(L, R),
    )
    md_table(
        w,
        "empty-cells",
        "| a | | c |\n|---|---|---|\n| | | |\n| x | | a cell with enough words in it to wrap when the column is narrow |\n",
        theme,
        (120.0, 240.0, 640.0),
    )
    built_table(w, "every-cell-empty", theme, [["", ""], ["", ""]], (240.0,))
    # With no padding either there is nothing to scale: `_apportioned` hands
    # back the zeros it was given and the table has no width.
    built_table(w, "every-cell-empty-no-padding", replace(theme, cell_padding=0), [["", ""], ["", ""]], (240.0,))
    for px, align in ((16.0, Align.JUSTIFY), (13.0, Align.LEFT)):
        themed = Theme.build(px, align=align)
        md_table(w, f"alignment-per-column-{px:g}px", ALIGNED, themed, (120.0, 300.0, 640.0))
    md_table(w, "wrapped-in-every-alignment", PROSE, theme, (200.0, 300.0, 420.0, 800.0))
    built_table(w, "more-alignments-than-columns", theme, [["a", "b"], ["c", "d"]], (240.0,), aligns=(R, CTR, R, L))
    md_table(w, "inline-math", MATH, theme, (160.0, 320.0, 640.0))
    many = "| n | square | words |\n|--:|--:|:--|\n" + "".join(
        f"| {n} | {n * n} | row {n} of forty |\n" for n in range(1, 41)
    )
    md_table(w, "many-rows", many, theme, (200.0, 640.0))
    listed = "- An item.\n\n  | a | b |\n  |---|---|\n  | one | two words |\n"
    md_table(w, "table-in-a-list", listed, theme, (20.0, 30.0, 240.0))
    wide_ppi = Theme.build(16.0, 144.0)
    md_table(w, "wrapped-at-144ppi", PROSE, wide_ppi, (300.0, 800.0))
    unruled = replace(theme, table_rule_thickness=0, cell_padding=0)
    md_table(w, "no-rule-no-padding", PROSE, unruled, (200.0, 640.0))
    return w


def dump_widths() -> Writer:
    w = Writer(
        "widths",
        """
How column widths are solved: one table swept from very narrow to very wide
through every regime; the edges between regimes to the scaled point; widths
and fractions a caller supplied through ui.with_columns, including widths
below a column's longest word and fractions whose shares round to nothing.
""",
    )
    theme = Theme.build()
    md_table(w, "prose-swept", PROSE, theme, (1.0, 40.0, 100.0, 150.0, 200.0, 250.0, 300.0, 350.0, 420.0, 558.0, 1070.0, 2000.0))

    # The exact edges: the measure at which the table just fits, and at which
    # the floors just fit, and one scaled point either side of each.
    ((_, prose),) = tables_of(PROSE, theme)
    natural = sum(ui.table_natural_widths(prose, theme, MEASURE))
    floor = sum(ui.table_column_limits(prose, theme, MEASURE).floor)
    ((table, original),) = tables_of(PROSE, theme)
    theme_in = ThemeIn.of(theme)
    consulted = Consulted()
    block = rebuild(theme_in, table, consulted.resolve)
    w.case("prose-at-the-edges", "table")
    w.r("source", "markdown", q(PROSE))
    emit_table_inputs(w, theme_in, table, block, consulted)
    emit_limits(w, "prose-at-the-edges", block, theme_in.cut_down(None))
    for label, width in (
        ("natural-minus-1sp", natural - 1),
        ("natural", natural),
        ("natural-plus-1sp", natural + 1),
        ("floor-minus-1sp", floor - 1),
        ("floor", floor),
        ("floor-plus-1sp", floor + 1),
        ("floor-plus-7sp", floor + 7),
    ):
        vbox = emit_layout(w, label, block, theme_in.cut_down(None), width)
        assert vbox == ui.compose_blocks((original,), theme, width, MEASURE)
    w.end()

    one_word = "| k | v |\n|---|---|\n| alpha | beta |\n| gamma | delta |\n"
    md_table(w, "nothing-to-give", one_word, theme, (40.0, 60.0, 80.0, 240.0))

    def points(*pt: float) -> Callable[[Theme], Sequence[Scaled]]:
        return lambda _: [sp_from_pt(value) for value in pt]

    md_table(w, "given-widths", PROSE, theme, (240.0, 640.0), widths=points(60.0, 140.0, 40.0))
    md_table(w, "given-widths-below-the-longest-word", PROSE, theme, (240.0,), widths=points(20.0, 30.0, 10.0))
    md_table(w, "given-widths-of-one-unit", PROSE, theme, (240.0,), widths=lambda _: [1, 1, 1])
    md_table(w, "given-widths-wider-than-the-measure", PROSE, theme, (240.0,), widths=points(200.0, 400.0, 200.0))
    md_table(w, "given-fractions", PROSE, theme, (240.0, 420.0, 800.0), fractions=(1, 3, 1))
    md_table(w, "given-fractions-uneven", PROSE, theme, (333.0, 334.0), fractions=(1.0, 1.0, 1.0))
    md_table(w, "given-fractions-tiny-share", PROSE, theme, (1.0, 240.0), fractions=(1e-9, 1.0, 1e-9))
    md_table(w, "given-fractions-tiny-last", PROSE, theme, (1.0, 240.0), fractions=(1.0, 1.0, 1e-12))
    return w


# --------------------------------------------------------------------------
# Columns: the INPUT is a vertical list
# --------------------------------------------------------------------------


def shallow(node: Node) -> Node:
    """
    A node of a vertical list as the splitter reads it: a box keeps its kind
    and its four dimensions and loses its contents.
    """
    if isinstance(node, Box):
        return Box(kind=node.kind, width=node.width, height=node.height, depth=node.depth, shift_amount=node.shift_amount)
    return node


def emit_vlist(w: Writer, nodes: Sequence[Node]) -> None:
    w.r("vlist", len(nodes))
    for node in nodes:
        if isinstance(node, Box):
            w.r("v", "box", node.kind, node.width, node.height, node.depth, node.shift_amount)
        elif isinstance(node, Rule):
            w.r("v", "rule", node.width, node.height, node.depth)
        elif isinstance(node, Glue):
            w.r("v", "glue", *core.spec_tokens(node.spec))
        elif isinstance(node, Kern):
            w.r("v", "kern", node.width, node.kind)
        elif isinstance(node, Penalty):
            w.r("v", "penalty", node.value)
        else:
            raise TypeError(type(node))
    w.r("endvlist")


def _suffix_start(material: Sequence[Node], rest: Sequence[Node]) -> tuple[bool, int]:
    """
    Where in `material` a pruned remainder begins.

    `prune_top` returns nothing, or one new glue and then a suffix of what it
    was given -- the same objects. Both facts are asserted. The index is this
    script's bookkeeping; the reference keeps none.
    """
    if not rest:
        return False, len(material)
    assert isinstance(rest[0], Glue)
    start = len(material) - (len(rest) - 1)
    assert all(mine is theirs for mine, theirs in zip(rest[1:], material[start:], strict=True))
    return True, start


def emit_split(
    w: Writer,
    name: str,
    material: Sequence[Node],
    height: Scaled,
    count: int,
    *,
    top_skip: GlueSpec = C.SPLIT_TOP_SKIP,
    max_depth: Scaled = MAX_DIMEN,
    measure: Any = MEASURE,
) -> C.ColumnSplit:
    """
    One `split_into_columns` call: per column, the scan `vert_break` made, the
    cut, the packed column and its commands.
    """
    split, roots = COLUMN_TRACER.run(
        C.split_into_columns, material, height=height, count=count, split_top_skip=top_skip, split_max_depth=max_depth
    )
    (root,) = roots
    cuts = root.named("vsplit")
    assert len(cuts) == count == len(split.results)
    w.r("split", q(name), height, count, max_depth)
    emit_spec(w, "split_top_skip", top_skip)
    for k, (cut, result) in enumerate(zip(cuts, split.results, strict=True)):
        given = cut.args["nodes"]
        head, start = _suffix_start(material, given)
        (scan,) = cut.named("vert_break")
        place = scan.result
        w.r("column", k, head, start, place)
        if head:
            emit_node(w, given[0])
        seen = scan.marks.get(COST_KNOWN, [])
        raw = scan.named("_break_badness")
        assert len(seen) == len(raw)
        w.r("scan", len(seen))
        for at, bad in zip(seen, raw, strict=True):
            assert (bad.args["cur_height"], bad.args["shrink"]) == (at["cur_height"], at["shrink"])
            w.r(
                "cand",
                at["index"],
                at["penalty_here"],
                at["cur_height"],
                at["prev_dp"],
                *at["stretch"].amounts,
                at["shrink"],
                bad.result,
                at["cost"],
                at["cost"] <= at["least_cost"],
            )
        last = seen[-1]
        stop = "overfull" if last["cost"] == C.AWFUL_BAD else "end" if last["index"] == len(given) else "eject"
        w.r("place", place, scan.locals["least_cost"], stop)
        assert result.box.contents == tuple(given[:place])
        w.r("packed", result.fitness, result.natural, result.excess, result.badness, result.overfull_by)
        w.r("colbox", *blocks.box_header(result.box))
        origin = Point(0, result.box.height)
        w.r("origin", origin.x, origin.y)
        emit_cmds(w, ship(result.box, origin, measure=measure))
        w.r("endcolumn")
    head, start = _suffix_start(material, split.overflow)
    w.r("overflow", head, start, len(split.overflow))
    if head:
        emit_node(w, split.overflow[0])
    w.r("endsplit")
    return split


def vsplit_case(
    w: Writer,
    name: str,
    source_kind: str,
    source: str,
    material: Sequence[Node],
    splits: Sequence[tuple[str, Scaled, int, dict[str, Any]]],
) -> None:
    """
    A vertical list given by its dimensions alone, and one or more splits.
    """
    listed = [shallow(node) for node in material]
    w.case(name, "vsplit")
    w.r("source", source_kind, q(source))
    emit_vlist(w, listed)
    for label, height, count, options in splits:
        split = emit_split(w, label, listed, height, count, measure=None, **options)
        # The contents of a box are nothing to the splitter: the real list is
        # cut at the same places.
        real = C.split_into_columns(
            material,
            height=height,
            count=count,
            split_top_skip=options.get("top_skip", C.SPLIT_TOP_SKIP),
            split_max_depth=options.get("max_depth", MAX_DIMEN),
        )
        assert [len(box.contents) for box in real.columns] == [len(box.contents) for box in split.columns]
        assert [blocks.box_header(box) for box in real.columns] == [blocks.box_header(box) for box in split.columns]
        assert len(real.overflow) == len(split.overflow)
    w.end()


LINE_HEIGHT, LINE_DEPTH, BASELINE = 8 * PT, 2 * PT, 14 * PT
INTERLINE = Glue(GlueSpec(width=BASELINE - LINE_DEPTH - LINE_HEIGHT))


def line(height: Scaled = LINE_HEIGHT, depth: Scaled = LINE_DEPTH, width: Scaled = 100 * PT) -> Box:
    return Box(BoxKind.HLIST, (), width=width, height=height, depth=depth)


def stack(count: int, glue: Glue = INTERLINE) -> list[Node]:
    """
    `count` uniform lines with 4 pt of glue between them: tests/test_columns.py's
    fixture, whose interline glue is not zero and not the head gap.
    """
    out: list[Node] = []
    for index in range(count):
        if index:
            out.append(glue)
        out.append(line())
    return out


def holding(lines: int) -> Scaled:
    """
    The column height that holds exactly `lines` of `stack`: the 2 pt head
    glue, then lines 14 pt apart, the last one's depth left out.
    """
    return (C.SPLIT_TOP_SKIP.width - LINE_HEIGHT) + LINE_HEIGHT + (lines - 1) * BASELINE


def dump_vertbreak() -> Writer:
    w = Writer(
        "vertbreak",
        """
Vertical lists built by hand and cut into columns: where a cut is legal,
what each legal cut costs, which one is taken, what is discarded at the head
of the next column. Boxes are empty hboxes of the given size.
""",
    )
    none: dict[str, Any] = {}
    twenty = stack(20)
    vsplit_case(
        w,
        "uniform-lines",
        "built",
        "",
        twenty,
        [
            ("five-lines-exactly", holding(5), 3, none),
            ("one-sp-short-of-five", holding(5) - 1, 3, none),
            ("one-sp-over-five", holding(5) + 1, 3, none),
            ("everything-fits", holding(20), 2, none),
            ("far-too-tall", holding(600), 1, none),
            ("one-column-overflowing", holding(3), 1, none),
            ("seven-columns", holding(3), 7, none),
            ("more-columns-than-material", holding(8), 5, none),
            ("shorter-than-one-line", 5 * PT, 3, none),
            ("deep-top-skip", holding(5), 3, {"top_skip": GlueSpec(width=20 * PT)}),
            ("top-skip-shorter-than-a-line", holding(5), 3, {"top_skip": GlueSpec(width=3 * PT)}),
            ("stretchable-top-skip", holding(5), 3, {"top_skip": GlueSpec(width=12 * PT, stretch=3 * PT, shrink=PT)}),
            ("max-depth-1pt", holding(5), 3, {"max_depth": PT}),
            ("max-depth-zero", holding(5), 3, {"max_depth": 0}),
        ],
    )
    vsplit_case(w, "empty-list", "built", "", [], [("two-columns", 50 * PT, 2, none)])
    vsplit_case(w, "one-line", "built", "", [line()], [("fits", 50 * PT, 2, none), ("too-tall-for-it", 4 * PT, 2, none)])
    vsplit_case(
        w,
        "initial-glue-and-only-discardables",
        "built",
        "",
        [Glue(GlueSpec(width=30 * PT)), Kern(5 * PT), Penalty(0), *stack(6)],
        [("three-lines", holding(3), 3, none)],
    )
    vsplit_case(
        w,
        "nothing-but-discardables",
        "built",
        "",
        [Glue(GlueSpec(width=3 * PT)), Kern(5 * PT), Penalty(50), Glue(GlueSpec(width=PT))],
        [("two-columns", 50 * PT, 2, none)],
    )
    # §973: a kern is a cut only when glue follows it, explicit or not; glue is
    # a cut only after a box or rule -- not after a kern.
    kerns: list[Node] = [
        line(),
        Kern(4 * PT),
        line(),
        Kern(4 * PT, KernKind.EXPLICIT),
        Glue(GlueSpec(width=PT)),
        line(),
        Kern(4 * PT),
        Glue(GlueSpec(width=PT)),
        line(),
        Glue(GlueSpec(width=2 * PT)),
        Kern(2 * PT),
        line(),
        Kern(4 * PT),
    ]
    vsplit_case(
        w,
        "kerns-and-glue",
        "built",
        "",
        kerns,
        [("two-lines", 2 * PT + 8 * PT + 14 * PT, 4, none), ("three-lines", 2 * PT + 8 * PT + 29 * PT, 3, none), ("all", 200 * PT, 1, none)],
    )
    ruled: list[Node] = [*stack(3), INTERLINE, Rule(width=100 * PT, height=PT, depth=0), INTERLINE, *stack(4)]
    vsplit_case(w, "a-rule-among-lines", "built", "", ruled, [("cut-at-the-rule", holding(3) + 5 * PT, 3, none), ("rule-heads-a-column", holding(3), 3, none)])
    tall: list[Node] = [*stack(2), INTERLINE, line(height=80 * PT), INTERLINE, *stack(3)]
    vsplit_case(
        w,
        "a-box-taller-than-the-column",
        "built",
        "",
        tall,
        [("it-overflows-its-column", holding(4), 4, none), ("it-is-first", holding(1), 4, none)],
    )
    deep: list[Node] = [*stack(3), INTERLINE, line(depth=9 * PT), INTERLINE, *stack(3)]
    vsplit_case(
        w,
        "a-deep-line",
        "built",
        "",
        deep,
        [
            ("unlimited-depth", holding(4), 2, none),
            ("max-depth-2pt", holding(4), 2, {"max_depth": 2 * PT}),
            ("max-depth-2pt-and-7pt-more", holding(4) + 7 * PT, 2, {"max_depth": 2 * PT}),
            ("negative-max-depth", holding(4), 2, {"max_depth": -PT}),
        ],
    )

    # Penalties (§974). The reference's own composed documents hold none, so
    # these are the only goldens that reach these arms.
    def with_penalty(value: int, after: int, count: int = 12) -> list[Node]:
        nodes = stack(count)
        at = 2 * after - 1
        return [*nodes[:at], Penalty(value), *nodes[at:]]

    vsplit_case(
        w,
        "penalties",
        "built",
        "",
        with_penalty(EJECT_PENALTY, 3),
        [("eject-after-three", holding(100), 3, none), ("eject-later-than-the-column", holding(2), 3, none)],
    )
    vsplit_case(w, "penalty-below-eject", "built", "", with_penalty(EJECT_PENALTY - 5, 3), [("forced", holding(8), 2, none)])
    forbidden: list[Node] = [line(), Penalty(INF_PENALTY), line(), Penalty(INF_PENALTY + 7), line()]
    vsplit_case(w, "penalty-infinite", "built", "", forbidden, [("no-cut-at-all", holding(1), 2, none)])
    vsplit_case(
        w,
        "penalty-positive-and-negative",
        "built",
        "",
        [*with_penalty(150, 4)[:-4], Penalty(-200), *stack(4)],
        [("rigid-material", holding(5), 3, none)],
    )

    # Stretch and shrink: the only material in which two legal cuts differ in
    # badness, so the only place a penalty changes the choice.
    springy = Glue(GlueSpec(width=4 * PT, stretch=2 * PT, shrink=PT))

    def widow(value: int) -> list[Node]:
        nodes = stack(10, springy)
        return [*nodes[:9], Penalty(value), *nodes[9:]]

    stretching = [
        ("six-lines", holding(6), 2, none),
        ("six-lines-and-3pt", holding(6) + 3 * PT, 2, none),
        ("six-lines-less-3pt", holding(6) - 3 * PT, 2, none),
        ("six-lines-less-5pt", holding(6) - 5 * PT, 2, none),
        ("six-lines-less-6pt", holding(6) - 6 * PT, 2, none),
        ("five-and-a-half", holding(5) + 7 * PT, 2, none),
    ]
    vsplit_case(w, "stretch-and-shrink", "built", "", stack(10, springy), stretching)
    vsplit_case(w, "stretch-with-a-penalty-of-150", "built", "", widow(150), stretching)
    vsplit_case(w, "stretch-with-a-penalty-of-5000", "built", "", widow(5000), stretching)
    vsplit_case(w, "stretch-with-a-penalty-of-minus-300", "built", "", widow(-300), stretching)
    fil = Glue(GlueSpec(width=4 * PT, stretch=PT, stretch_order=GlueOrder.FIL))
    mixed: list[Node] = [line(), springy, line(), fil, line(), springy, line(), Glue(GlueSpec(width=4 * PT, stretch=-PT, stretch_order=GlueOrder.FIL)), line(), springy, line()]
    vsplit_case(
        w,
        "infinite-stretch",
        "built",
        "",
        mixed,
        [("four-lines", holding(4), 2, none), ("two-lines", holding(2), 3, none), ("all-of-it-and-more", holding(9), 1, none)],
    )
    shrinking = Glue(GlueSpec(width=4 * PT, shrink=PT, shrink_order=GlueOrder.FIL))
    vsplit_case(
        w,
        "infinite-shrink-is-counted-as-finite",
        "built",
        "",
        stack(8, shrinking),
        [("five-lines-less-2pt", holding(5) - 2 * PT, 2, none)],
    )
    return w


def document_vlist(file: str, theme: Theme, width: Scaled) -> tuple[Node, ...]:
    source = (REFERENCE / file).read_text(encoding="utf-8")
    made = markdown.build_document(source, theme, fontlib_metrics).blocks
    return ui.compose_blocks(made, theme, width, MEASURE).contents


def dump_documents() -> Writer:
    w = Writer(
        "documents",
        """
The reference's demo documents composed at a column's measure and cut into
columns. The vertical list is given by its dimensions alone (FORMAT-tables.md
says why that is enough), so the columns ship as lines and rules.
""",
    )
    none: dict[str, Any] = {}
    theme = Theme.build()

    def px(value: float) -> Scaled:
        return sp_from_px(value, theme.px_per_inch)

    # advtext/columns.py's own docstring figures: sample.md at a 262 pt
    # measure, three columns of 527 pt.
    vsplit_case(
        w,
        "sample-262pt",
        "demo-file",
        "demo/sample.md 16px justify 262pt",
        document_vlist("demo/sample.md", theme, sp_from_pt(262.0)),
        [("three-of-527pt", sp_from_pt(527.0), 3, none), ("six-of-250pt", sp_from_pt(250.0), 6, none), ("four-of-196pt", sp_from_pt(196.0), 4, none)],
    )
    for count, width_px in ((1, 640.0), (2, 420.0), (3, 280.0)):
        material = document_vlist("demo/columns.md", theme, px(width_px))
        vsplit_case(
            w,
            f"columns-md-{count}-up",
            "demo-file",
            f"demo/columns.md 16px justify {width_px:g}px",
            material,
            [
                (f"{count}-of-800px", px(800.0), count, none),
                (f"{count * 3}-of-800px", px(800.0), count * 3, none),
                (f"{count * 4}-of-300px", px(300.0), count * 4, none),
            ],
        )
    # A document of tables: a wrapped row is one box however many lines its
    # cells have, and the rows of a table are joined by glue.
    vsplit_case(
        w,
        "tables-md-400px",
        "demo-file",
        "demo/tables.md 16px justify 400px",
        document_vlist("demo/tables.md", theme, px(400.0)),
        [("four-of-500px", px(500.0), 4, none), ("eight-of-200px", px(200.0), 8, none)],
    )
    return w


# --------------------------------------------------------------------------
# Columns over blocks: the whole path, from block records to commands
# --------------------------------------------------------------------------

SHORT = (
    "A first paragraph, long enough to be set on several lines at the width of one column, "
    "so that a column can end inside it.\n"
    "\n"
    "A second paragraph follows it and is cut the same way, between two of its lines.\n"
    "\n"
    "The third and last.\n"
)

LONG = (
    "# A heading\n"
    "\n"
    "A paragraph under the heading with enough words in it to run to several lines when the "
    "measure is a column's, which is what gives the splitter legal places to cut.\n"
    "\n"
    "- One item.\n"
    "- A second item.\n"
    "- A third, and these three with the gaps between them are one unbreakable run.\n"
    "\n"
    "## A second heading\n"
    "\n"
    "The paragraph after it. A block boundary is a kern with no glue behind it, so no column "
    "ends between a heading and its first line, nor between two list items of one line each.\n"
    "\n"
    "```\n"
    "code line one\n"
    "code line two\n"
    "code line three\n"
    "```\n"
    "\n"
    "A closing paragraph that is also long enough to be divided between two columns if the "
    "cut happens to fall inside it.\n"
)

QUOTED = (
    "Before the quote.\n"
    "\n"
    "> A quotation of several paragraphs is one box however long it is.\n"
    ">\n"
    "> So when it is taller than the column there is nowhere inside it to cut, and the "
    "column that holds it is overfull.\n"
    ">\n"
    "> Its third paragraph.\n"
    "\n"
    "After the quote, a paragraph that runs to more than one line at this measure.\n"
)


def columns_case(
    w: Writer,
    name: str,
    source: str,
    theme: Theme,
    width_px: float,
    splits: Sequence[tuple[str, float, int]],
) -> None:
    """
    A markdown document: dump_blocks' INPUT and `compose` group for it at one
    column width, then the composed list cut into columns, shipped whole.
    """
    capture = blocks.BlockCapture()
    with capture.installed():
        originals = tuple(markdown.build_document(source, theme, fontlib_metrics).blocks)
    theme_in = blocks.ThemeIn.of(theme)
    inputs = [capture.input_of(block) for block in originals]
    consulted = Consulted()
    rebuilt = tuple(blocks.rebuild(theme_in, block_in, consulted.resolve) for block_in in inputs)
    for block, original in zip(rebuilt, originals, strict=True):
        assert block == blocks.normalised(original)
    cut = theme_in.cut_down({})
    width = sp_from_px(width_px, theme.px_per_inch)

    w.case(name, "columns")
    w.r("source", "markdown", q(source))
    blocks.emit_inputs(w, theme_in, inputs, rebuilt, consulted)
    vbox = blocks.emit_compose(w, f"ui-{width_px:g}px", rebuilt, cut, width, MEASURE)
    assert vbox == ui.compose_blocks(originals, theme, width, MEASURE)
    emit_vlist(w, vbox.contents)
    for label, height_px, count in splits:
        height = sp_from_px(height_px, theme.px_per_inch)
        split = emit_split(w, label, vbox.contents, height, count)
        joined = C.compose_into_columns(originals, theme, MEASURE, column_width=width, column_height=height, count=count)
        assert joined == split, f"{name}: compose_into_columns disagrees with composing and splitting"
    w.end()


def dump_columns() -> Writer:
    w = Writer(
        "columns",
        """
Short markdown documents composed at a column's width and cut into two and
three columns, from the blocks (dump_blocks' records) to each column's
commands: short material, long material, material that fits in the first
column, and a quote taller than the column.
""",
    )
    theme = Theme.build()
    columns_case(w, "short-material", SHORT, theme, 240.0, [("two-of-90px", 90.0, 2), ("three-of-60px", 60.0, 3), ("three-of-400px", 400.0, 3)])
    columns_case(w, "long-material", LONG, theme, 220.0, [("two-of-260px", 260.0, 2), ("three-of-170px", 170.0, 3)])
    columns_case(w, "a-quote-taller-than-the-column", QUOTED, theme, 200.0, [("three-of-90px", 90.0, 3), ("two-of-200px", 200.0, 2)])
    ragged = Theme.build(13.0, align=Align.LEFT)
    columns_case(w, "long-material-13px-left", LONG, ragged, 200.0, [("three-of-150px", 150.0, 3)])
    return w


# --------------------------------------------------------------------------
# Verification: tables
# --------------------------------------------------------------------------


def _ints(row: Sequence[str], at: int) -> list[int]:
    count = int(row[at])
    assert len(row) == at + 1 + count, row
    return [int(value) for value in row[at + 1 :]]


def read_table_inputs(records: Records) -> tuple[ThemeIn, TableIn, para._Tables, Any, list[list[dict[str, Any]]]]:
    row = records.take("theme")
    faces = tuple(unq(token) for token in records.take("faces")[1:])
    theme_in = ThemeIn(float.fromhex(row[1]), para._token_enum(Spacing, row[2]), int(row[3]), int(row[4]), faces)  # type: ignore[arg-type]
    tables = para._Tables()
    tables.read(records)
    measure = read_measure(records)
    if measure is None:
        measure = lambda node: CharDimens(0, 0, 0)  # noqa: E731 - a table with no characters
    head = records.take("table")
    columns, nrows, has_header, left_indent = int(head[1]), int(head[2]), head[3] == "1", int(head[4])
    style = blocks._read_style(records, unq(head[7]), Align.LEFT, int(head[5]), int(head[6]), 0)
    row = records.take("aligns")
    aligns = tuple(para._token_enum(markdown.ColumnAlign, token) for token in row[2:])
    assert len(aligns) == int(row[1])
    policy = records.take("policy")
    widths: tuple[int, ...] = ()
    fractions: tuple[float, ...] = ()
    if policy[1] == "widths":
        widths = tuple(_ints(policy, 2))
    elif policy[1] == "fractions":
        fractions = tuple(float.fromhex(token) for token in policy[3:])
        assert len(fractions) == int(policy[2])
    else:
        assert policy[1:] == ["apportioned"]
    rows: list[tuple[tuple[Event, ...], ...]] = []
    derived: list[list[dict[str, Any]]] = []
    for r in range(nrows):
        row = records.take("row")
        assert int(row[1]) == r
        cells: list[tuple[Event, ...]] = []
        lists: list[dict[str, Any]] = []
        for c in range(int(row[2])):
            cell = records.take("cell")
            assert (int(cell[1]), int(cell[2])) == (r, c)
            events: list[Event] = []
            for _ in range(int(cell[3])):
                event = records.take()
                if event[0] == "run":
                    events.append(("run", unq(event[1]), event[2] == "1", unq(event[3])))
                else:
                    assert event == ["atom"]
                    events.append(("atom", read_node(records)))
            found: dict[str, Any] = {}
            for _ in range(2):
                list_name, nodes = read_nodes(records)
                found[list_name] = tuple(nodes)
            records.take("endcell")
            cells.append(tuple(events))
            lists.append(found)
        records.take("endrow")
        rows.append(tuple(cells))
        derived.append(lists)
    records.take("endtable")
    assert columns == max((len(row) for row in rows), default=0)
    table = TableIn(style, left_indent, has_header, tuple(rows), aligns, widths, fractions)
    return theme_in, table, tables, measure, derived


def read_break(records: Records) -> list[Box]:
    """
    One `break ... endbreak` group as FORMAT.md states it: the line boxes.
    """
    records.take("break")
    para._read_shape(records)
    para._read_params(records)
    count = int(records.take("paragraph")[1])
    records.take("breaks")
    boxes = [read_packed(records).box for _ in range(count)]
    records.take("endbreak")
    return boxes


def read_limits(records: Records, table: TableIn, padding: Scaled, measure: Any) -> tuple[list[list[int]], list[int]]:
    """
    The `limits` group, with the statements FORMAT-tables.md makes about it.
    """
    records.take("limits")
    natural: list[list[int]] = []
    floor: list[int] = []
    if table.rows:
        for r, row in enumerate(table.rows):
            found = records.take("natural")
            assert int(found[1]) == r
            natural.append(_ints(found, 2))
            assert len(natural[-1]) == len(row)
        columns = max(len(row) for row in table.rows)
        widest = _ints(records.take("widest"), 1)
        assert widest == [max((sizes[c] for sizes in natural if c < len(sizes)), default=0) for c in range(columns)]
        floor = [0] * columns
        for r, row in enumerate(table.rows):
            for c, events in enumerate(row):
                word = records.take("word")
                assert (int(word[1]), int(word[2])) == (r, c)
                if records.peek()[0] == "break":
                    lines = read_break(records)
                    sizes = _ints(records.take("wordlines"), 1)
                    assert len(sizes) == len(lines) and max(sizes) == int(word[3])
                    assert sizes == [hpack(line.contents, measure=measure).box.width for line in lines]
                else:
                    assert not events and int(word[3]) == 0
                floor[c] = max(floor[c], int(word[3]) + 2 * padding)
        assert floor == _ints(records.take("floor"), 1)
    records.take("endlimits")
    return natural, floor


def read_layout(
    records: Records,
    theme_in: ThemeIn,
    table: TableIn,
    block: markdown.TableBlock,
    natural: list[list[int]],
    floor: list[int],
    measure: Any,
) -> None:
    """
    One `layout` group read with nothing from advtext.ui: the widths re-derived
    by the arithmetic FORMAT-tables.md states, the rows rebuilt from their
    records, the stack packed and shipped and compared with `vbox` and `cmds`.
    """
    row = records.take("layout")
    width = int(row[2])
    padding = theme_in.cell_padding
    strut = table.style.strut
    rows = ui._table_rows(block)
    set_rows: list[Box] = []
    resumed_of: list[int] = []
    rule: Box | None = None
    if rows:
        solve = records.take("solve")
        how, available, columns = solve[1], int(solve[2]), int(solve[3])
        assert available == max(width - table.left_indent, UNITY)
        assert columns == max(len(found) for found in rows)
        said: dict[str, list[str]] = {}
        while records.peek()[0] != "endsolve":
            found = records.take()
            said[found[0]] = found
        records.take("endsolve")
        widths = _ints(said["columns"], 1)
        assert len(widths) == columns
        if how == "given":
            assert widths == list(table.widths)
        elif how == "fractions":
            total = float.fromhex(said["total"][1])
            assert total == sum(table.fractions)
            rounded = _ints(said["rounded"], 1)
            assert rounded == [max(1, round(available * share / total)) for share in table.fractions]
            last = int(said["last"][1])
            assert last == rounded[-1] + available - sum(rounded)
            want = [*rounded[:-1], last]
            borrowed = int(said["borrowed"][1])
            if last < 1:
                assert borrowed == max(range(columns), key=want.__getitem__)
                want[borrowed] -= 1 - last
                want[-1] = 1
            else:
                assert borrowed == -1
            assert widths == want and sum(widths) == available
        else:
            widest = _ints(said["widest"], 1)
            assert widest == [max((sizes[c] for sizes in natural if c < len(sizes)), default=0) for c in range(columns)]
            if how in ("apportioned", "unscaled"):
                assert sum(widest) <= available and int(said["total"][1]) == sum(widest)
                if how == "unscaled":
                    assert sum(widest) <= 0 and widths == widest
                else:
                    floored = _ints(said["floored"], 1)
                    assert floored == [size * available // sum(widest) for size in widest]
                    assert widths == [*floored[:-1], floored[-1] + available - sum(floored)]
            else:
                assert sum(widest) > available
                narrowest = _ints(said["narrowest"], 1)
                assert narrowest == floor
                slack = int(said["slack"][1])
                assert slack == available - sum(narrowest)
                if how == "floors":
                    assert slack <= 0 and widths == narrowest
                else:
                    give = _ints(said["give"], 1)
                    assert give == [wide - narrow for wide, narrow in zip(widest, narrowest)]
                    total_give = int(said["total"][1])
                    assert total_give == sum(give)
                    if how == "rigid":
                        assert slack > 0 and total_give <= 0 and widths == narrowest
                    else:
                        assert how == "squeezed" and slack > 0 and total_give > 0
                        floored = _ints(said["floored"], 1)
                        assert floored == [narrow + slack * room // total_give for narrow, room in zip(narrowest, give)]
                        assert widths == [*floored[:-1], floored[-1] + available - sum(floored)]

        aligns = list(table.aligns) + [L] * (columns - len(table.aligns))
        for r, cells in enumerate(rows):
            found = records.take("row")
            assert int(found[1]) == r
            fits = all(size <= widths[c] for c, size in enumerate(natural[r]))
            assert found[2] == ("fits" if fits else "wrapped")
            if fits:
                made = read_node(records)
                assert isinstance(made, Box) and len(made.contents) == columns + 1
                assert made.contents[0] == Kern(width=table.left_indent)
                for c, cell_box in enumerate(made.contents[1:]):
                    assert isinstance(cell_box, Box) and cell_box.width == widths[c]
                    material = cells[c].material if c < len(cells) else ()
                    inside = cell_box.contents
                    assert inside[0] == Kern(width=padding) == inside[-1]
                    lead = 0 if aligns[c] is L else 1
                    trail = 0 if aligns[c] is R else 1
                    assert len(inside) == 2 + lead + len(material) + trail
                    assert tuple(inside[1 + lead : 1 + lead + len(material)]) == tuple(material)
                    for glue in (*inside[1 : 1 + lead], *inside[len(inside) - 1 - trail : -1]):
                        assert isinstance(glue, Glue) and glue.spec.stretch_order is not GlueOrder.NORMAL
            else:
                cell_boxes: list[Node] = [Kern(width=table.left_indent)]
                lowest = 0
                for c in range(columns):
                    cell = records.take("cell")
                    assert (int(cell[1]), int(cell[2])) == (c, widths[c])
                    assert int(cell[3]) == max(widths[c] - 2 * padding, UNITY)
                    lines = read_break(records)
                    assert int(records.take("lines")[1]) == len(lines) >= 1
                    vlist: list[Node] = []
                    prev_depth = IGNORE_DEPTH
                    first = 0
                    for at, broken in enumerate(lines):
                        append = records.take("append")
                        assert int(append[1]) == prev_depth
                        if records.peek()[0] == "glue":
                            assert at > 0
                            vlist.append(read_node(records))
                        dims = [int(value) for value in records.take("dims")[1:]]
                        assert dims == [broken.width, max(broken.height, strut.height), max(broken.depth, strut.depth), padding]
                        vlist.append(replace(broken, height=dims[1], depth=dims[2], shift_amount=padding))
                        prev_depth = int(append[2])
                        assert prev_depth == dims[2]
                        if at == 0:
                            first = dims[1]
                    stacked = vpack(vlist).box
                    said_stacked = records.take("stacked")
                    assert [stacked.width, stacked.height, stacked.depth] == [int(value) for value in said_stacked[1:]]
                    vtop = [int(value) for value in records.take("vtop")[1:]]
                    below = stacked.height - first
                    assert vtop == [widths[c], first, below + stacked.depth, below]
                    records.take("endcell")
                    cell_boxes.append(replace(stacked, width=widths[c], height=first, depth=below + stacked.depth))
                    lowest = max(lowest, below)
                made = hpack(cell_boxes, measure=measure).box
                header = records.take("rowbox")
                assert [tok(value) for value in blocks.box_header(made)] == header[1:]
                assert int(records.take("lowest")[1]) == lowest
            dims = [int(value) for value in records.take("strutted")[1:]]
            assert dims == [made.width, max(made.height, strut.height), max(made.depth, strut.depth), made.shift_amount]
            made = replace(made, height=dims[1], depth=dims[2])
            resumed = made.depth
            if not fits:
                resumed = int(records.take("resumed")[1])
                assert resumed == made.depth - lowest
            records.take("endrow")
            set_rows.append(made)
            resumed_of.append(resumed)
            if r == 0 and records.peek()[0] == "headrule":
                records.take()
                found_rule = read_node(records)
                assert table.has_header and theme_in.table_rule_thickness
                assert found_rule == hpack(
                    [Kern(width=table.left_indent), Rule(width=sum(widths), height=theme_in.table_rule_thickness, depth=0)]
                ).box
                assert isinstance(found_rule, Box)
                rule = found_rule
            elif r == 0:
                assert not (table.has_header and theme_in.table_rule_thickness)

    count = int(records.take("stack")[1])
    assert count == len(set_rows) + (rule is not None)
    vlist = []
    prev_depth = IGNORE_DEPTH
    for _ in range(count):
        append = records.take("append")
        assert int(append[1]) == prev_depth
        if records.peek()[0] == "glue":
            vlist.append(read_node(records))
        ref = records.take("ref")
        if ref[1] == "headrule":
            assert rule is not None
            box, resumed = rule, rule.depth
        else:
            box, resumed = set_rows[int(ref[2])], resumed_of[int(ref[2])]
        assert int(append[2]) == box.depth and int(append[3]) == resumed
        prev_depth = resumed
        vlist.append(box)
    records.take("endstack")
    extents = [int(value) for value in records.take("extents")[2:]]
    vbox = vpack(vlist).box
    assert extents == [0, vbox.height + vbox.depth]
    assert [vbox.width, vbox.height, vbox.depth] == [int(value) for value in records.take("vbox")[1:]]
    row = records.take("origin")
    origin = Point(int(row[1]), int(row[2]))
    assert origin == Point(0, vbox.height)
    assert same_cmds(ship(vbox, origin, measure=measure), read_cmds(records))
    records.take("endlayout")


def verify_table(records: Records, name: str) -> int:
    row = records.take("source")
    assert row[1] in ("markdown", "demo-file", "built")
    start = records.at
    theme_in, table, tables, measure, derived = read_table_inputs(records)
    block = rebuild(theme_in, table, tables.resolve)
    for cells, lists in zip(ui._table_rows(block), derived, strict=True):
        for cell, found in zip(cells, lists, strict=True):
            assert found["material"] == cell.material
            assert found["unshrinkable"] == ui._unshrinkable(cell.material)
    theme = theme_in.cut_down(None)

    again = Writer("verify", "")
    again.lines = []
    emit_table_inputs(again, theme_in, table, block, blocks._as_consulted(tables))
    blocks._same_rows(again, records, start)

    start = records.at
    natural, floor = read_limits(records, table, theme_in.cell_padding, measure)
    again.lines = []
    emit_limits(again, name, block, theme)
    blocks._same_rows(again, records, start)

    checks = 2
    while records.peek()[0] != "endcase":
        start = records.at
        run_name, width = unq(records.peek()[1]), int(records.peek()[2])
        if run_name.startswith("ui-"):
            assert sp_from_px(float(run_name[3:-2]), theme_in.px_per_inch) == width
        read_layout(records, theme_in, table, block, natural, floor, measure)
        again.lines = []
        emit_layout(again, run_name, block, theme, width)
        blocks._same_rows(again, records, start)
        checks += 1
    return checks


# --------------------------------------------------------------------------
# Verification: columns
# --------------------------------------------------------------------------


def read_vlist(records: Records) -> list[Node]:
    count = int(records.take("vlist")[1])
    out: list[Node] = []
    for _ in range(count):
        row = records.take("v")
        kind = row[1]
        if kind == "box":
            out.append(
                Box(
                    kind=core._enum(BoxKind, row[2]),
                    width=int(row[3]),
                    height=int(row[4]),
                    depth=int(row[5]),
                    shift_amount=int(row[6]),
                )
            )
        elif kind == "rule":
            out.append(Rule(int(row[2]), int(row[3]), int(row[4])))
        elif kind == "glue":
            out.append(Glue(core._spec(row[2:7])))
        elif kind == "kern":
            out.append(Kern(int(row[2]), core._enum(KernKind, row[3])))
        else:
            assert kind == "penalty", row
            out.append(Penalty(int(row[2])))
    records.take("endvlist")
    return out


def read_split(records: Records, material: Sequence[Node], measure: Any) -> tuple[str, Scaled, int, dict[str, Any]]:
    """
    One `split` group read with nothing from advtext.columns: each column
    rebuilt from the indices, packed, shipped, and compared; each scan held to
    what FORMAT-tables.md says of it.
    """
    row = records.take("split")
    name, height, count, max_depth = unq(row[1]), int(row[2]), int(row[3]), int(row[4])
    top_skip = para._read_spec(records, "split_top_skip")
    at = 0
    for k in range(count):
        column = records.take("column")
        assert int(column[1]) == k
        head, start, place = column[2] == "1", int(column[3]), int(column[4])
        nodes: list[Node] = []
        # Everything discarded at the head of this column is discardable, and
        # what the column starts on is a box or a rule.
        assert start >= at and all(isinstance(node, Glue | Kern | Penalty) for node in material[at:start])
        if head:
            glue = read_node(records)
            first = material[start]
            assert isinstance(first, Box | Rule)
            assert glue == Glue(replace(top_skip, width=max(0, top_skip.width - first.height)))
            nodes.append(glue)
        else:
            assert start == len(material)
        nodes += material[start:]
        scans = int(records.take("scan")[1])
        best: tuple[int, int] | None = None
        least = (1 << 30) - 1
        stopped = ""
        for _ in range(scans):
            cand = records.take("cand")
            index, penalty = int(cand[1]), int(cand[2])
            badness, cost, taken = int(cand[10]), int(cand[11]), cand[12] == "1"
            assert 0 <= index <= len(nodes) and penalty < INF_PENALTY and not stopped
            if index == len(nodes):
                assert penalty == EJECT_PENALTY
            else:
                node = nodes[index]
                if isinstance(node, Glue):
                    assert penalty == 0 and index > 0 and isinstance(nodes[index - 1], Box | Rule)
                elif isinstance(node, Kern):
                    assert penalty == 0 and index + 1 < len(nodes) and isinstance(nodes[index + 1], Glue)
                else:
                    assert isinstance(node, Penalty) and node.value == penalty
            want = badness  # awful_bad: past the shrink, nothing is added
            if badness < (1 << 30) - 1:
                want = penalty if penalty <= EJECT_PENALTY else badness + penalty if badness < 10000 else 100000
            assert cost == want and taken == (cost <= least)
            if taken:
                best, least = (index, cost), cost
            if cost == (1 << 30) - 1:
                stopped = "overfull"
            elif penalty <= EJECT_PENALTY:
                stopped = "end" if index == len(nodes) else "eject"
        found = records.take("place")
        assert stopped == found[3] and int(found[2]) == least
        assert place == (best[0] if best is not None else len(nodes)) == int(found[1])
        packed_row = records.take("packed")
        result = vpack(nodes[:place], height, PackMode.EXACTLY, max_depth=max_depth)
        assert [tok(value) for value in (result.fitness, result.natural, result.excess, result.badness, result.overfull_by)] == packed_row[1:]
        assert [tok(value) for value in blocks.box_header(result.box)] == records.take("colbox")[1:]
        assert result.box.height == height
        origin_row = records.take("origin")
        origin = Point(int(origin_row[1]), int(origin_row[2]))
        assert origin == Point(0, height)
        assert same_cmds(ship(result.box, origin, measure=measure), read_cmds(records))
        records.take("endcolumn")
        at = start + place - head
    overflow = records.take("overflow")
    head, start, left = overflow[1] == "1", int(overflow[2]), int(overflow[3])
    assert all(isinstance(node, Glue | Kern | Penalty) for node in material[at:start])
    if head:
        read_node(records)
        assert left == 1 + len(material) - start
    else:
        assert left == 0 and start == len(material)
    records.take("endsplit")
    options: dict[str, Any] = {"top_skip": top_skip, "max_depth": max_depth}
    return name, height, count, options


def verify_splits(records: Records, material: Sequence[Node], measure: Any) -> int:
    checks = 0
    again = Writer("verify", "")
    while records.peek()[0] != "endcase":
        start = records.at
        name, height, count, options = read_split(records, material, measure)
        again.lines = []
        emit_split(again, name, material, height, count, measure=measure, **options)
        blocks._same_rows(again, records, start)
        checks += 1
    return checks


def verify_vsplit(records: Records) -> int:
    row = records.take("source")
    assert row[1] in ("built", "demo-file")
    return verify_splits(records, read_vlist(records), None)


def verify_columns(records: Records) -> int:
    row = records.take("source")
    assert row[1] == "markdown"
    start = records.at
    theme_in, inputs, tables, measure, _ = blocks.read_inputs(records)
    rebuilt = tuple(blocks.rebuild(theme_in, block_in, tables.resolve) for block_in in inputs)
    again = Writer("verify", "")
    again.lines = []
    blocks.emit_inputs(again, theme_in, inputs, rebuilt, blocks._as_consulted(tables))
    blocks._same_rows(again, records, start)

    start = records.at
    compose = records.peek()
    assert compose[0] == "compose"
    again.lines = []
    vbox = blocks.emit_compose(again, unq(compose[1]), rebuilt, theme_in.cut_down({}), int(compose[2]), measure)
    records.at += len(again.lines)
    blocks._same_rows(again, records, start)
    listed = read_vlist(records)
    assert listed == [shallow(node) for node in vbox.contents]
    return 2 + verify_splits(records, vbox.contents, measure)


def verify(path: Path) -> tuple[int, int]:
    text = path.read_bytes().decode("utf-8")
    assert text.endswith("\n") and "\r" not in text and "\t" not in text
    for line in text.split("\n"):
        assert not line.startswith(" ") and not line.endswith(" ") and "  " not in line, line
    assert any(line.startswith("# Reference: imgui-adv-text commit ") for line in text.split("\n")[:8])
    records = Records(text)
    header = records.take("golden")
    assert int(header[1]) == FORMAT_VERSION and unq(header[2]) == "tables/" + path.stem
    cases = checks = 0
    while not records.done():
        row = records.take("case")
        try:
            if row[2] == "table":
                checks += verify_table(records, unq(row[1]))
            elif row[2] == "vsplit":
                checks += verify_vsplit(records)
            else:
                assert row[2] == "columns", row
                checks += verify_columns(records)
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
    dump_demo,
    dump_shapes,
    dump_widths,
    dump_vertbreak,
    dump_documents,
    dump_columns,
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
