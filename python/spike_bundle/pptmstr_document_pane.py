"""SPIKE: a pptmstr transcript through ONE imtypeset.Document, beside pptmstr's
own pane and beside the per-paragraph spike (pptmstr_pane.py).

Run with pptmstr's interpreter and a library built for its wheel:

    IMTYPESET_BUNDLE_OUT=build/api-document-bundle-pptmstr \
        sh python/host/build_bundle_library.sh ../imgui_bundle ../pptmstr/.venv/bin/python
    IMTYPESET_LIBRARY=build/api-document-bundle-pptmstr/libimtypeset_bundle.so \
        ../pptmstr/.venv/bin/python -P -B python/spike_bundle/pptmstr_document_pane.py \
        [--repeat 4] [--stream] [--frames 240 --screenshot out.png]

pptmstr is imported READ-ONLY from ../pptmstr: its blocks (ui/blocks.py), its
inline tokens (ui/inline.py) and its pane (ui/rich_pane.py) are the real ones.

TODAY      pptmstr.ui.rich_pane.draw(): per frame and per block, inline tokens
           wrapped in Python and one ImDrawList.add_text per styled run.
PARAGRAPH  one imtypeset.Paragraph per block, as pptmstr_pane.py: per frame and
           per block a layout, a draw and an invisible button.
DOCUMENT   the blocks described once as imtypeset.Blocks in ONE Document: one
           call when the transcript changes (set_blocks, or append_text for the
           streaming tail), and per frame layout + draw + one hit test,
           whatever the number of blocks.

What stays in Python in the DOCUMENT pane, because a block cannot say it:
  - a TABLE: a Host block reserves its place and pptmstr's own _draw_table
    (ImGui's begin_table) draws into it; its height is reported back with
    set_block_extent when it changes;
  - the per-block context menu ("copy paragraph"): one invisible button over
    the whole document and a hit test when it is right-clicked;
  - "N earlier blocks not shown": not needed, nothing is windowed.
A fence's chrome rows (its language label, "N more lines", "(fence not
closed)") are said as dim one-line paragraphs; unlike pptmstr's they are then
text a selection would copy.

Prints, per pane: the median time of its draw per frame, and the calls into
imtypeset and into the imgui_bundle binding (functions and methods; attribute
reads are not counted) that one steady frame made.
"""

import argparse
import bisect
import json
import os
import statistics
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
PPTMSTR = os.environ.get("PPTMSTR_ROOT", os.path.join(os.path.dirname(REPO), "pptmstr"))
sys.path[:0] = [os.path.join(REPO, "python"), os.path.join(REPO, "examples", "python")]
sys.path.append(PPTMSTR)
# hello_imgui hands a screenshot over as a numpy array, and pptmstr's environment
# has no numpy. SPIKE_EXTRA_SITE names a site-packages that does; it goes LAST.
if os.environ.get("SPIKE_EXTRA_SITE"):
    sys.path.append(os.environ["SPIKE_EXTRA_SITE"])

DOC = (
    "# Reviewing the line breaker\n"
    "\n"
    "The breaker keeps a list of **active nodes** and, for every legal breakpoint, asks each of "
    "them how bad the line from there to here would be. With badness $b$, penalty $p$ and line "
    "penalty $l$ the demerits are $d = (l + b)^2 + p^2$ when $p \\geq 0$, and the *total-fit* "
    "path is the one that minimises $\\sum_i d_i$ over the whole paragraph.\n"
    "\n"
    "- `try_break` runs once per candidate, so it is the hot loop and allocates nothing.\n"
    "- A ratio $r = \\frac{w - W}{Y}$ above the tolerance deactivates the node.\n"
    "- The second pass allows hyphenation; the *emergency* pass adds stretch.\n"
    "\n"
    "1. Read the reference module before porting it.\n"
    "2. Replay the goldens: integers equal, doubles equal to the last bit.\n"
    "\n"
    "> A divergence is a finding to record, never something to tune a test around.\n"
    "\n"
    "```py\n"
    "def demerits(l, b, p):\n"
    "    return (l + b) ** 2 + p * p if p >= 0 else (l + b) ** 2 - p * p\n"
    "```\n"
    "\n"
    "The measured cost is what matters to a pane that redraws every frame: a paragraph that "
    "has not changed should cost one call, not one call per styled run, and a paragraph that "
    "has changed should cost one layout. See [the notes](http://example.com/notes) for the "
    "numbers, and $e^{i\\pi} + 1 = 0$ for no reason at all.\n"
    "\n"
)
TABLE = (
    "| pass | what it allows | cost |\n"
    "|---|---|---|\n"
    "| first | no hyphenation | one sweep |\n"
    "| second | hyphenation | one more |\n"
    "\n"
    "---\n"
    "\n"
)
STREAM = (
    "The live tail arrives a few characters at a time, as a model writes it, and the pane has "
    "to show it on the frame it arrives without setting the whole transcript again. "
)


class Counter:
    def __init__(self):
        self.n = 0


class Proxy:
    """Stands in for the imgui module, a draw list or a font and counts the calls made
    through it."""

    def __init__(self, target, counter):
        object.__setattr__(self, "_target", target)
        object.__setattr__(self, "_counter", counter)

    def __bool__(self):
        return self._target is not None

    def __getattr__(self, name):
        attr = getattr(self._target, name)
        if isinstance(attr, type) and name != "ImVec2":
            return attr  # an enum, whose members are read and not called
        if not callable(attr):
            return attr
        counter = self._counter

        def call(*args, **kwargs):
            counter.n += 1
            result = attr(*[_unwrap(a) for a in args], **{k: _unwrap(v) for k, v in kwargs.items()})
            if type(result).__name__ in ("ImDrawList", "ImFont"):
                return Proxy(result, counter)
            return result

        return call


def _unwrap(value):
    return value._target if isinstance(value, Proxy) else value


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--frames", type=int, default=0)
    parser.add_argument("--screenshot", default="")
    parser.add_argument("--repeat", type=int, default=4, help="copies of the sample text")
    parser.add_argument("--tables", action="store_true", help="a table and a rule in each copy")
    parser.add_argument("--stream", action="store_true", help="a live tail that grows every frame")
    parser.add_argument("--panes", default="today,paragraph,document")
    parser.add_argument("--justify", action="store_true", help="imtypeset panes justified")
    parser.add_argument("--top", action="store_true", help="do not scroll to the tail")
    args = parser.parse_args()
    shown = args.panes.split(",")

    from imgui_bundle import hello_imgui, immapp
    from imgui_bundle import imgui as real_imgui

    import imtypeset as ts
    import bundle_support as support
    from pptmstr import theme
    from pptmstr.transcript import SegmentKind, Transcript
    from pptmstr.ui import rich_pane
    from pptmstr.ui.blocks import Block as PBlock
    from pptmstr.ui.blocks import BlockCursor, BlockKind
    from pptmstr.ui.span_layout import layout_inline
    from pptmstr.ui.transcript_pane import NodeTranscript

    ig = [real_imgui]  # The imgui the panes below call: swapped for a Proxy on a counted frame.

    # The blocks, exactly as pptmstr's transcript pane derives them.
    transcript = Transcript()
    transcript.append(SegmentKind.OUTPUT, (DOC + (TABLE if args.tables else "")) * args.repeat)
    cache = NodeTranscript()
    cache.sync(transcript, transcript.published_length)
    cursor = BlockCursor()
    cursor.feed(cache.lines[: len(cache.lines) - (1 if cache.open_line else 0)])
    blocks = cursor.blocks
    live_text = [""]  # The streaming tail's whole text, and what this frame added to it.
    live_added = [""]

    prose = (BlockKind.PARAGRAPH, BlockKind.HEADING, BlockKind.BULLET_ITEM,
             BlockKind.ORDERED_ITEM, BlockKind.BLOCKQUOTE)
    indent = 18.0
    align = ts.Align.Justify if args.justify else ts.Align.Left

    # --- PARAGRAPH: the per-paragraph spike ------------------------------------------------

    paragraphs: dict = {}
    paragraph_style = ts.Style(Align=align)

    def paragraph_for(index: int, block):
        made = paragraphs.get(index)
        if made is not None:
            return made
        if block.kind is BlockKind.FENCE:
            text = "\n".join(rich_pane.fence_body(block))
            made = (ts.Paragraph(text, [ts.Run(0, len(text), ts.Face.Code)]), 0.0, "")
        else:
            rows = layout_inline(block.inline or (), 1e9, lambda t, w: len(t), lambda t: 0.0)
            text, runs = "", []
            for n, row in enumerate(rows):
                text += "\n" if n else ""
                for run in row.runs:
                    start = len(text)
                    text += run.text
                    face = (ts.Face.Code if run.code else ts.Face.Bold if run.bold
                            else ts.Face.Italic if run.italic else ts.Face.Default)
                    colour = theme.P.accent.u32 if run.link else 0
                    if face != ts.Face.Default or colour:
                        runs.append(ts.Run(start, len(text), face, 0.0, colour))
            if block.kind is BlockKind.HEADING:
                runs = [ts.Run(0, len(text), ts.Face.Bold, 1.4 - 0.1 * block.level)]
            marker = {BlockKind.BULLET_ITEM: "•", BlockKind.ORDERED_ITEM: "1."}.get(block.kind, "")
            left = indent if block.kind in (BlockKind.BULLET_ITEM, BlockKind.ORDERED_ITEM,
                                            BlockKind.BLOCKQUOTE) else 0.0
            made = (ts.Paragraph(text, runs, ts.TextFlags.Math), left, marker)
        paragraphs[index] = made
        return made

    live_paragraph = [None]

    def draw_paragraphs() -> None:
        imgui = ig[0]
        width = imgui.get_content_region_avail().x
        draw_list = support.draw_list_address()
        for index, block in enumerate(blocks):
            if block.kind not in prose and block.kind is not BlockKind.FENCE:
                rich_pane._draw_one(index, rich_state, block, width, imgui.get_font_size())
                continue
            paragraph, left, marker = paragraph_for(index, block)
            paragraph.layout(max(width - left, 1.0), paragraph_style)
            start = imgui.get_cursor_screen_pos()
            if marker:
                imgui.get_window_draw_list().add_text(start, theme.P.text_dim.u32, marker)
            paragraph.draw(draw_list, (start.x + left, start.y))
            w, h = paragraph.size
            imgui.push_id(index)
            imgui.invisible_button("##hit", imgui.ImVec2(width, max(h, 1.0)))
            imgui.pop_id()
        if args.stream:
            if live_paragraph[0] is None:
                live_paragraph[0] = ts.Paragraph()
            paragraph = live_paragraph[0]
            paragraph.set_text(live_text[0])
            paragraph.layout(max(width, 1.0), paragraph_style)
            start = imgui.get_cursor_screen_pos()
            paragraph.draw(draw_list, (start.x, start.y))
            imgui.invisible_button("##live", imgui.ImVec2(width, max(paragraph.size[1], 1.0)))

    # --- DOCUMENT ----------------------------------------------------------------------------

    P = theme.P
    styles = [ts.BlockStyle(Align=align, SpaceBelow=4.0)]
    style_index: dict = {"body": 0}

    def style(name: str, **fields) -> int:
        if name not in style_index:
            style_index[name] = len(styles)
            styles.append(ts.BlockStyle(Align=fields.pop("Align", align), SpaceBelow=4.0, **fields))
        return style_index[name]

    def describe(source):
        """pptmstr's blocks as (imtypeset blocks, marks, the source block of each, tables)."""
        out, marks, origin, tables = [], [], [], []

        def add(block, index):
            out.append(block)
            origin.append(index)

        for index, block in enumerate(source):
            kind = block.kind
            if kind is BlockKind.TABLE:
                tables.append(len(out))
                add(ts.Block(ts.BlockKind.Host, "\n".join(block.lines), extent=60.0), index)
            elif kind is BlockKind.THEMATIC_BREAK:
                add(ts.Block(ts.BlockKind.Rule, extent=1.0, style=style("rule", SpaceAbove=6.0)), index)
            elif kind is BlockKind.FENCE:
                dim = style("chrome", Color=P.text_dim.u32, Align=ts.Align.Left)
                lang = dict(block.meta).get("lang")
                body = rich_pane.fence_body(block)
                if lang:
                    add(ts.Block(text=lang, style=dim), index)
                add(ts.Block(ts.BlockKind.Code, "\n".join(body[:200]),
                             style=style("code", Color=P.diff_context.u32)), index)
                if len(body) > 200:
                    add(ts.Block(text=f"... {len(body) - 200} more line(s), copy to see all", style=dim), index)
                if not block.closed:
                    add(ts.Block(text="(fence not closed)",
                                 style=style("warn", Color=P.warn.u32, Align=ts.Align.Left)), index)
            elif kind is BlockKind.LITERAL:
                role = rich_pane._KIND_COLOUR.get(block.segment_kind, "text")
                add(ts.Block(ts.BlockKind.Code, "\n".join(block.lines),
                             style=style("literal-" + role, Color=getattr(P, role).u32,
                                         Face=ts.Face.Regular)), index)
            else:
                text, runs, hard = [], [], False
                at = 0
                bold = italic = link = False
                for token in block.inline or ():
                    kind_ = token.type
                    if kind_ in ("text", "code_inline", "softbreak", "hardbreak"):
                        piece = (token.content if kind_ in ("text", "code_inline")
                                 else " " if kind_ == "softbreak" else "\n")
                        hard = hard or kind_ == "hardbreak"
                        end = at + len(piece)
                        if kind_ == "code_inline":
                            runs.append(ts.Run(at, end, ts.Face.Code, 0.0, P.diff_context.u32))
                        elif bold or italic or link:
                            # pptmstr's rule: strong is the bold face, emphasis a colour.
                            runs.append(ts.Run(
                                at, end, ts.Face.Bold if bold else ts.Face.Default, 0.0,
                                P.text_strong.u32 if bold else P.accent.u32 if italic else 0,
                                0, index + 1 if link else 0))
                        if link:
                            where = ((len(out), at), (len(out), end))
                            marks.append(ts.Mark(where, P.accent.u32))
                            marks.append(ts.Mark(where, P.accent.u32, ts.MarkPlace.Under))
                        text.append(piece)
                        at = end
                    elif kind_ in ("strong_open", "strong_close"):
                        bold = kind_ == "strong_open"
                    elif kind_ in ("em_open", "em_close"):
                        italic = kind_ == "em_open"
                    elif kind_ in ("link_open", "link_close"):
                        link = kind_ == "link_open"
                flags = ts.BlockFlags.Math | (ts.BlockFlags.HardBreaks if hard else 0)
                joined = "".join(text)
                if kind is BlockKind.HEADING:
                    add(ts.Block(ts.BlockKind.Heading, joined, runs, flags=flags, level=block.level,
                                 style=style(f"h{block.level}", Face=ts.Face.Bold,
                                             Scale=1.4 - 0.1 * block.level, Color=P.accent.u32,
                                             Align=ts.Align.Left)), index)
                elif kind in (BlockKind.BULLET_ITEM, BlockKind.ORDERED_ITEM):
                    marker = "•" if kind is BlockKind.BULLET_ITEM else dict(block.meta).get("start", "1") + "."
                    add(ts.Block(ts.BlockKind.ListItem, joined, runs, marker=marker, flags=flags,
                                 left_indent=indent,
                                 style=style("item", MarkerColor=P.text_dim.u32, MarkerGap=6.0)), index)
                elif kind is BlockKind.BLOCKQUOTE:
                    add(ts.Block(text=joined, runs=runs, flags=flags, left_indent=indent, quote_depth=1,
                                 style=style("quote", Color=P.text_dim.u32)), index)
                else:
                    add(ts.Block(text=joined, runs=runs, flags=flags), index)
        return out, marks, origin, tables

    document = [None]
    described: dict = {}
    hovered = [None]

    def make_document() -> None:
        """What runs when the transcript changes: ONE call into the library."""
        out, marks, origin, tables = describe(blocks)
        if args.stream:
            out.append(ts.Block(text=""))
            origin.append(len(blocks))
        document[0] = ts.Document.from_blocks(out, styles)
        document[0].set_options(ts.DocumentOptions(QuoteIndent=indent - 3.0, QuoteBarThickness=2.0,
                                                   QuoteBarColor=P.border.u32, RuleColor=P.border.u32))
        described.update(marks=document[0].pack_marks(marks), origin=origin, tables=tables,
                         extents={i: 60.0 for i in tables}, count=len(out))

    def draw_document() -> None:
        imgui = ig[0]
        doc = document[0]
        if args.stream:
            if live_added[0] is None:  # the tail finalised: the block starts again, one call
                doc.replace_blocks(described["count"] - 1, 1, [ts.Block(text=live_text[0])])
            elif live_added[0]:
                doc.append_text(live_added[0])
        width = imgui.get_content_region_avail().x
        doc.layout(max(width, 1.0))
        origin = imgui.get_cursor_screen_pos()
        x, y = origin.x, origin.y
        doc.draw(support.draw_list_address(), (x, y), marks=described["marks"])
        mouse = imgui.get_mouse_pos()
        hit = doc.hit_test((mouse.x - x, mouse.y - y))
        hovered[0] = hit
        tables = described["tables"]
        if tables:
            # The tables in view: the blocks under the top and the bottom of what the
            # window shows, and the Host blocks between them.
            top = imgui.get_scroll_y()
            first = doc.hit_test((0.0, top)).position.block
            last = doc.hit_test((0.0, top + imgui.get_window_height())).position.block
            for at in range(bisect.bisect_left(tables, first), bisect.bisect_right(tables, last)):
                index = tables[at]
                geometry = doc.block_geometry(index)
                imgui.set_cursor_screen_pos(imgui.ImVec2(x + geometry.full_min[0], y + geometry.full_min[1]))
                rich_pane._draw_table(index, blocks[described["origin"][index]])
                height = imgui.get_cursor_screen_pos().y - (y + geometry.full_min[1])
                if abs(height - described["extents"][index]) > 0.5:
                    described["extents"][index] = height
                    doc.set_block_extent(index, height)
            imgui.set_cursor_screen_pos(origin)
        size = doc.size()
        imgui.invisible_button("##document", imgui.ImVec2(max(size[0], 1.0), max(size[1], 1.0)))
        if imgui.begin_popup_context_item("##document_ctx"):
            block = hit.position.block
            source = described["origin"][block] if block < len(described["origin"]) else None
            if source is not None and source < len(blocks):
                label = blocks[source].kind.value.replace("_", " ")
                if imgui.menu_item_simple(f"copy {label}"):
                    imgui.set_clipboard_text("\n".join(blocks[source].lines))
            imgui.end_popup()

    # --- TODAY ---------------------------------------------------------------------------------

    rich_state = rich_pane.RichState()

    def draw_today() -> None:
        live = None
        if args.stream and live_text[0]:
            live = PBlock(BlockKind.PARAGRAPH, SegmentKind.OUTPUT, (live_text[0],), False)
        rich_pane.draw(rich_state, blocks, live)

    panes = [(name, draw) for name, draw in (
        ("today", draw_today), ("paragraph", draw_paragraphs), ("document", draw_document),
    ) if name in shown]
    times = {name: [] for name, _ in panes}
    calls = {name: {"imtypeset": 0, "imgui_bundle": 0} for name, _ in panes}
    counted = [0]
    count_frame = max(12, (args.frames or 80) // 2)

    class CountingLibrary:
        def __init__(self, real, counter):
            self.real, self.counter = real, counter

        def __getattr__(self, name):
            function = getattr(self.real, name)
            counter = self.counter

            def call(*a):
                counter.n += 1
                return function(*a)

            return call

    def gui() -> None:
        counted[0] += 1
        if args.stream:
            # Twelve characters a frame; at 1,100 the tail "finalises" and starts again.
            if len(live_text[0]) >= 1100:
                live_text[0], live_added[0] = "", None
            else:
                at = len(live_text[0])
                live_added[0] = (STREAM * 12)[at : at + 12]
                live_text[0] += live_added[0]
        if document[0] is None and "document" in shown:
            make_document()
        counting = counted[0] == count_frame
        size = real_imgui.get_io().display_size
        each = size.x / len(panes)
        for n, (name, draw) in enumerate(panes):
            real_imgui.set_next_window_pos(real_imgui.ImVec2(n * each, 0.0))
            real_imgui.set_next_window_size(real_imgui.ImVec2(each, size.y))
            real_imgui.begin(f"{name}: {len(blocks)} blocks", None, real_imgui.WindowFlags_.no_collapse)
            if counting:
                bundle, library = Counter(), Counter()
                proxy = Proxy(real_imgui, bundle)
                kept = (rich_pane.imgui, rich_pane.face, ts._lib, support.draw_list_address)

                def counted_address():
                    bundle.n += 1
                    return kept[3]()

                ig[0] = rich_pane.imgui = proxy
                rich_pane.face = lambda which: Proxy(kept[1](which), bundle)
                ts._lib = CountingLibrary(kept[2], library)
                support.draw_list_address = counted_address
                try:
                    draw()
                finally:
                    ig[0] = real_imgui
                    rich_pane.imgui, rich_pane.face, ts._lib = kept[0], kept[1], kept[2]
                    support.draw_list_address = kept[3]
                calls[name] = {"imtypeset": library.n, "imgui_bundle": bundle.n}
            else:
                started = time.perf_counter()
                draw()
                times[name].append(time.perf_counter() - started)
            if not args.top:
                real_imgui.set_scroll_here_y(1.0)
            real_imgui.end()
        if args.frames and counted[0] >= args.frames:
            hello_imgui.get_runner_params().app_shall_exit = True

    def load_fonts() -> None:
        theme.load_fonts()  # pptmstr's faces first, as in its app
        if ts.current_context() is None:
            ts.create_context()
        support.add_fonts()

    def before_exit() -> None:
        extra = {"layouts": ts.stats().Layouts, "paragraphs": len(paragraphs)}
        if document[0] is not None:
            extra["document"] = document[0].stats()._asdict()
            extra["document_blocks"] = len(document[0])
            extra["hovered"] = hovered[0] and hovered[0]._asdict()
        print(json.dumps(extra))
        ts.destroy_context()

    runner = hello_imgui.RunnerParams()
    runner.app_window_params.window_title = "SPIKE: pptmstr transcript through one imtypeset.Document"
    runner.app_window_params.window_geometry.size = (1500, 900)
    runner.imgui_window_params.default_imgui_window_type = (
        hello_imgui.DefaultImGuiWindowType.no_default_window
    )
    runner.ini_disable = True
    runner.fps_idling.enable_idling = False
    runner.callbacks.load_additional_fonts = load_fonts
    runner.callbacks.show_gui = gui
    runner.callbacks.before_exit = before_exit
    immapp.run(runner)

    if args.screenshot:
        support.write_png(args.screenshot, hello_imgui.final_app_window_screenshot())
    report = {"frames": counted[0], "blocks": len(blocks), "stream": args.stream,
              "wheel": ts.host_info()["bundle"]["wheel_version"]}
    for name, samples in times.items():
        warm = max(10, len(samples) // 4)
        steady = sorted(samples[warm:]) or [0.0]
        report[name] = {
            "first_frame_us": round(samples[0] * 1e6, 1),
            "median_us": round(statistics.median(steady) * 1e6, 1),
            "p95_us": round(steady[int(0.95 * (len(steady) - 1))] * 1e6, 1),
            "calls_per_frame": calls[name],
        }
    print(json.dumps(report))


if __name__ == "__main__":
    main()
