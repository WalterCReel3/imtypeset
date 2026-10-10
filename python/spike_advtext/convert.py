"""SPIKE: an advtext `Document` (what `advtext.markdown.build_document` and
`advtext.stream.StreamDocument` produce) as imtypeset block descriptions.

advtext is imported READ-ONLY from ../imgui-adv-text. Nothing here imports
imtypeset: the descriptions are plain tuples in the field order of
`imtypeset.Block`, `imtypeset.Run` and `imtypeset.Mark`, with the header's
enumerators as integers, so the census can run without the library.

advtext's blocks hold NODES (`TextBlock.material` is a tuple of CharNode, Glue,
Kern, Penalty and boxes), and imtypeset takes TEXT with runs. So this walks
each block's material back into a string:

    CharNode            its character (the one WRITTEN, for a ReplacedChar), and
                        a run when its face or size is not the block's
    Glue                a space; glue of no natural width (the fill before a
                        hard break) a newline, as advtext.spans.material_text reads it
    Penalty <= EJECT    a newline, if the fill did not already give one
    Kern                nothing: imtypeset kerns for itself
    Box at a FormulaSpan  the formula's SOURCE as a run flagged Formula or
                        DisplayFormula: an explicit range, no dollars rescanned
    Box at index 0      the first-line indent, which goes to the style instead

and counts, in `Described.misfits`, everything it met that a block description
does not say. `docs/advtext-rework-plan.md` reads the counts.
"""

import collections
from typing import Any, NamedTuple

# include/imtypeset.h, as integers.
KIND_PARAGRAPH, KIND_HEADING, KIND_LIST_ITEM, KIND_CODE, KIND_RULE, KIND_HOST = 0, 1, 2, 3, 4, 5
FACE_DEFAULT, FACE_REGULAR, FACE_BOLD, FACE_ITALIC, FACE_BOLD_ITALIC = 0, 1, 2, 3, 4
FACE_CODE, FACE_CODE_BOLD, FACE_MATH = 5, 6, 7
ALIGN = {"JUSTIFY": 0, "LEFT": 1, "CENTER": 2, "RIGHT": 3}
FACES = {"prose": FACE_REGULAR, "prose-bold": FACE_BOLD, "prose-italic": FACE_ITALIC,
         "prose-bold-italic": FACE_BOLD_ITALIC, "code": FACE_CODE, "code-bold": FACE_CODE_BOLD,
         "math": FACE_MATH}
RUN_NO_BREAK, RUN_FORMULA, RUN_DISPLAY_FORMULA = 1, 2, 4
BLOCK_HARD_BREAKS = 1
MARK_INK, MARK_BEHIND, MARK_UNDER = 0, 1, 2


class Described(NamedTuple):
    blocks: list  # imtypeset.Block field order
    styles: list  # dicts of imtypeset.BlockStyle fields
    links: list  # (block, start, end, target): the host's table; Run.tag is 1 + its index
    tables: list  # indices of the Host blocks that stand for tables
    texts: list  # each block's text, for the host's own find
    misfits: Any  # collections.Counter
    kinds: Any  # collections.Counter of what was walked


class Converter:
    """Holds the style table, so that a stream's later blocks reuse its indices."""

    def __init__(self, theme, px_from_sp, body_size_sp: int):
        self.theme = theme
        self.px = lambda sp: px_from_sp(sp, theme.px_per_inch)
        self.body = body_size_sp
        self.style_index: dict = {}
        self.styles: list = []
        self.font_cache: dict = {}

    def style(self, style, colour: int = 0) -> int:
        key = (style, colour)
        found = self.style_index.get(key)
        if found is None:
            found = self.style_index[key] = len(self.styles)
            size = style.font.size
            self.styles.append({
                "Face": FACES[style.font.face.value],
                "Scale": size / self.body,
                "LineHeight": style.interline.baselineskip.width / size,
                "Align": ALIGN[style.align.name],
                "FirstLineIndent": self.px(style.first_line_indent),
                "SpaceAbove": self.px(style.space_above),
                "SpaceBelow": self.px(style.space_below),
                "Color": colour,
            })
        return found

    def font(self, key: str):
        found = self.font_cache.get(key)
        if found is None:
            name, _, size = key.rpartition("@")
            found = self.font_cache[key] = (FACES[name], int(size))
        return found

    def text_of(self, material, style, formulas, links, misfits, kinds, at: int):
        """(text, runs, hard, link rows) for one run of material."""
        base = (FACES[style.font.face.value], style.font.size)
        formula_at = {span.start: span for span in formulas if not span.refusal and not span.unclosed}
        link_at = sorted(links, key=lambda span: span.start)
        parts: list = []
        runs: list = []
        offsets = [0] * (len(material) + 1)
        pos = 0
        run_start, run_font = 0, base
        hard = False

        def close(end: int) -> None:
            if run_font != base and end > run_start:
                face, size = run_font
                runs.append((run_start, end, face if face != base[0] else FACE_DEFAULT,
                             0.0 if size == base[1] else size / self.body, 0, 0, 0))

        for index, node in enumerate(material):
            offsets[index] = pos
            name = type(node).__name__
            kinds[f"node {name}"] += 1
            if name in ("CharNode", "ReplacedChar"):
                font = self.font(node.font)
                if font != run_font:
                    close(pos)
                    run_start, run_font = pos, font
                if name == "ReplacedChar":
                    misfits["characters no face has (advtext sets U+FFFD on the node; imtypeset stands in itself)"] += 1
                    parts.append(chr(node.original))
                else:
                    parts.append(chr(node.codepoint))
                pos += 1
            elif name == "Glue":
                if node.spec.width == 0:
                    if not parts or parts[-1] != "\n":
                        parts.append("\n")
                        pos += 1
                        hard = True
                else:
                    parts.append(" ")
                    pos += 1
            elif name == "Penalty":
                if node.value <= -10000 and index < len(material) - 1 and (not parts or parts[-1] != "\n"):
                    parts.append("\n")
                    pos += 1
                    hard = True
            elif name == "Kern":
                kinds["kerns dropped (imtypeset kerns for itself)"] += 1
            elif name == "Box":
                span = formula_at.get(index)
                if span is not None:
                    close(pos)
                    source = span.source
                    runs.append((pos, pos + len(source), FACE_DEFAULT, 0.0, 0,
                                 RUN_DISPLAY_FORMULA if span.display else RUN_FORMULA, 0))
                    parts.append(source)
                    pos += len(source)
                    run_start, run_font = pos, base
                    kinds["formulas set (an explicit Formula run)"] += 1
                elif index == 0 and style.first_line_indent:
                    kinds["first-line indent boxes (to BlockStyle.FirstLineIndent)"] += 1
                else:
                    misfits["a box in the material that is neither a formula nor the indent"] += 1
            else:
                misfits[f"material node {name} (no text equivalent)"] += 1
        offsets[len(material)] = pos
        close(pos)
        for span in formulas:
            if span.refusal and not span.unclosed:
                kinds["formulas refused by advtext's builder (their source, in the code face)"] += 1
            if span.unclosed:
                kinds["display formulas not closed yet (source so far)"] += 1
        rows = []
        for span in link_at:
            start, end = offsets[min(span.start, len(material))], offsets[min(span.end, len(material))]
            if end > start:
                rows.append((at, start, end, span.target))
        runs.sort(key=lambda run: run[0])
        return "".join(parts), runs, hard, rows

    def describe(self, blocks, base: int = 0, palette_ink=None) -> Described:
        """`blocks` as descriptions; `base` is the index the first will have in the document."""
        out: list = []
        links: list = []
        tables: list = []
        texts: list = []
        misfits: Any = collections.Counter()
        kinds: Any = collections.Counter()
        for block in blocks:
            at = base + len(out)
            name = type(block).__name__
            if name == "TextBlock":
                kinds[f"TextBlock {block.kind.name}"] += 1
                text, runs, hard, rows = self.text_of(block.material, block.style, block.formulas,
                                                      block.links, misfits, kinds, at)
                # A link: the run's tag is 1 + its row in the host's table.
                tagged = []
                for row in rows:
                    links.append(row)
                    tagged.append((row[1], row[2], FACE_DEFAULT, 0.0, 0, 0, len(links)))
                    kinds["links (Run.tag into a host table)"] += 1
                if tagged:
                    misfits["links whose range also carries a styled run (two runs over one range)"] += sum(
                        1 for t in tagged for r in runs if r[0] < t[1] and t[0] < r[1])
                marker = ""
                if block.marker:
                    marker = "".join(chr(n.codepoint) for n in block.marker if hasattr(n, "codepoint"))
                    kinds["list markers"] += 1
                kind = (KIND_HEADING if block.kind.name.startswith("HEADING") else
                        KIND_LIST_ITEM if block.kind.name.endswith("_ITEM") else KIND_PARAGRAPH)
                level = int(block.kind.name[-1]) if kind == KIND_HEADING else 0
                if block.quote_depth > 1:
                    kinds["blocks quoted more than one deep"] += 1
                if block.style.left_indent:
                    misfits["a style with its own left indent"] += 1
                out.append((kind, text, runs + tagged if not _overlap(runs, tagged) else runs, marker,
                            self.style(block.style), self.px(block.left_indent), block.quote_depth, 0, 0,
                            0.0, BLOCK_HARD_BREAKS if hard else 0, level))
                texts.append(text)
            elif name == "CodeBlock":
                kinds["CodeBlock"] += 1
                kinds["code lines"] += len(block.lines)
                if block.language:
                    kinds["code blocks naming a language (kept by the host; advtext does not draw it)"] += 1
                if len(block.lines) > 16:
                    kinds["code blocks over 16 lines (advtext lays them out in 16-line pieces)"] += 1
                text = "\n".join("".join(chr(n.codepoint) for n in line) for line in block.lines)
                out.append((KIND_CODE, text, (), "", self.style(block.style), self.px(block.left_indent),
                            0, 0, 0, 0.0, 0, 0))
                texts.append(text)
            elif name == "RuleBlock":
                kinds["RuleBlock"] += 1
                out.append((KIND_RULE, "", (), "", self.style(block.style), self.px(block.left_indent),
                            0, 0, 0, max(1.0, self.px(block.thickness)), 0, 0))
                texts.append("")
            elif name == "TableBlock":
                cells = len(block.header) + sum(len(row) for row in block.rows)
                misfits["TableBlock (no table in the core: a Host block the view must draw)"] += 1
                misfits["table cells (each its own run list, links and formulas)"] += cells
                tables.append(at)
                out.append((KIND_HOST, "", (), "", self.style(block.style), self.px(block.left_indent),
                            0, 0, 0, 24.0 * (1 + len(block.rows)), 0, 0))
                texts.append("")
            else:
                misfits[f"block type {name}"] += 1
                out.append((KIND_HOST, "", (), "", 0, 0.0, 0, 0, 0, 16.0, 0, 0))
                texts.append("")
        return Described(out, self.styles, links, tables, texts, misfits, kinds)


def _overlap(runs, tagged) -> bool:
    return any(r[0] < t[1] and t[0] < r[1] for t in tagged for r in runs)
