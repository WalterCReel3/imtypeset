"""SPIKE: what pptmstr's panes are really asked to show, and what of it an
imtypeset block description cannot say.

    PYTHONDONTWRITEBYTECODE=1 ../pptmstr/.venv/bin/python -P -B \
        python/spike_bundle/pptmstr_census.py [--source sessions|repo] [--sessions DIR]

Loads no library and opens no window: pptmstr's own Transcript, NodeTranscript,
BlockCursor and inline parser (read-only from ../pptmstr) over the corpora of
pptmstr_corpus.py. Prints one JSON object:

sizes      per transcript: bytes, lines, blocks, and what rich_pane.windowed()
           keeps of them (the 400-line window is what RICH mode draws today).
blocks     every BlockKind, counted, with the lines it holds.
inline     every inline token type markdown-it produced.
misfits    each thing counted that ONE imtypeset.Block cannot say as pptmstr
           draws it, by walking pptmstr's block and inline kinds. The reading
           of each is in docs/advtext-rework-plan.md.
"""

import argparse
import collections
import json
import os
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import pptmstr_corpus as corpus  # noqa: E402


def spread(values) -> dict:
    values = sorted(values)
    if not values:
        return {"n": 0}
    return {
        "n": len(values),
        "median": statistics.median(values),
        "p90": values[int(0.9 * (len(values) - 1))],
        "max": values[-1],
        "total": sum(values),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--source", default="sessions", choices=("sessions", "repo"))
    parser.add_argument("--sessions", default=corpus.DEFAULT_SESSIONS)
    args = parser.parse_args()

    from pptmstr.transcript import SegmentKind
    from pptmstr.ui import rich_pane
    from pptmstr.ui.blocks import BlockKind

    if args.source == "sessions":
        files = corpus.session_files(args.sessions)
        transcripts = [corpus.transcript_from_session(path) for path in files]
    else:
        files = corpus.report_files()
        transcripts = [corpus.transcript_from_reports()]

    cmap = None
    try:  # pptmstr's dev extra has fontTools; without it the glyph row is left out.
        from fontTools.ttLib import TTFont

        import imgui_bundle

        font = os.path.join(os.path.dirname(imgui_bundle.__file__), "assets", "fonts",
                            "Inconsolata-Medium.ttf")
        cmap = set(TTFont(font, lazy=True).getBestCmap())
    except Exception:  # noqa: BLE001
        pass

    sizes = collections.defaultdict(list)
    blocks_by_kind = collections.Counter()
    lines_by_kind = collections.Counter()
    segment_lines = collections.Counter()
    inline = collections.Counter()
    misfits = collections.Counter()
    uncovered = collections.Counter()
    longest_word = 0

    for transcript in transcripts:
        cache, cursor = corpus.lines_and_blocks(transcript)
        blocks = cursor.blocks
        if not cache.lines:
            continue
        window = rich_pane.windowed(blocks)
        sizes["bytes"].append(transcript.published_length)
        sizes["lines"].append(len(cache.lines))
        sizes["blocks"].append(len(blocks))
        sizes["blocks_in_the_400_line_window"].append(len(window))
        sizes["bytes_in_the_400_line_window"].append(sum(len(line) + 1 for b in window for line in b.lines))
        sizes["turn_prose_bytes"].append(len(transcript.turn_prose()))
        for line in cache.lines:
            segment_lines[line.kind.value] += 1
        for block in blocks:
            kind = block.kind
            blocks_by_kind[kind.value] += 1
            lines_by_kind[kind.value] += len(block.lines)
            text = "\n".join(block.lines)
            if any(ord(ch) > 127 for ch in text):
                misfits["blocks with non-ASCII text (a per-block offset conversion)"] += 1
            if cmap is not None:
                for ch in set(text):
                    if ord(ch) > 32 and ord(ch) not in cmap:
                        uncovered[ch] += 1
            for word in text.split():
                longest_word = max(longest_word, len(word))
            if kind is BlockKind.TABLE:
                misfits["TABLE blocks (no table in the core: a Host block, drawn by pptmstr)"] += 1
                misfits["TABLE rows"] += max(0, len(block.lines) - 2)
            elif kind is BlockKind.FENCE:
                body = rich_pane.fence_body(block)
                if dict(block.meta).get("lang"):
                    misfits["FENCE language labels (chrome: an extra dim block)"] += 1
                if len(body) > rich_pane._FENCE_LINE_CAP:
                    misfits["FENCE over the 200-line cap (chrome: 'N more lines')"] += 1
                if not block.closed:
                    misfits["FENCE not closed (chrome: '(fence not closed)')"] += 1
                if any("\t" in line for line in body):
                    misfits["FENCE with a tab in it (the tab-range defect applies)"] += 1
            elif kind is BlockKind.LITERAL:
                if any("\t" in line for line in block.lines):
                    misfits["LITERAL with a tab in it (the tab-range defect applies)"] += 1
                if block.segment_kind is SegmentKind.REASONING:
                    misfits["LITERAL reasoning blocks (drawn unwrapped today; fit as Code)"] += 1
            else:
                tokens = block.inline or ()
                depth = {"strong": 0, "em": 0, "link": 0}
                dollars = 0
                for token in tokens:
                    inline[token.type] += 1
                    name = token.type.rsplit("_", 1)[0]
                    if token.type.endswith("_open") and name in depth:
                        depth[name] += 1
                    elif token.type.endswith("_close") and name in depth:
                        depth[name] -= 1
                    elif token.type in ("text", "code_inline"):
                        if token.type == "text":
                            dollars += token.content.count("$")
                        if depth["strong"] and depth["em"]:
                            misfits["text both strong and emphasised (pptmstr: strong wins)"] += 1
                        if token.type == "code_inline" and (depth["strong"] or depth["link"]):
                            misfits["code span inside strong or a link"] += 1
                    elif token.type == "link_open":
                        misfits["links (the URL stays in a host table keyed by Run.tag)"] += 1
                    elif token.type not in ("softbreak", "hardbreak", "strong_open", "strong_close",
                                            "em_open", "em_close", "link_close"):
                        misfits[f"inline token '{token.type}' (drawn as its literal content)"] += 1
                if dollars >= 2:
                    misfits["prose blocks with two or more '$' (formulas IF BlockFlags.Math is set)"] += 1
                elif dollars == 1:
                    misfits["prose blocks with exactly one '$'"] += 1

    report = {
        "source": args.source,
        "files": len(files),
        "transcripts_with_text": len(sizes["bytes"]),
        "sizes": {name: spread(values) for name, values in sizes.items()},
        "lines_by_segment_kind": dict(segment_lines.most_common()),
        "blocks": {kind: {"blocks": n, "lines": lines_by_kind[kind]}
                   for kind, n in blocks_by_kind.most_common()},
        "inline_tokens": dict(inline.most_common()),
        "misfits": dict(sorted(misfits.items())),
        "longest_unbroken_word_chars": longest_word,
    }
    if cmap is not None:
        report["characters_not_in_Inconsolata_Medium"] = {
            "distinct": len(uncovered),
            "blocks_touched_by_the_commonest": [
                [f"U+{ord(ch):04X}", n] for ch, n in uncovered.most_common(12)
            ],
        }
    print(json.dumps(report, indent=1))


if __name__ == "__main__":
    main()
