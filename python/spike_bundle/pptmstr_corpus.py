"""SPIKE support: transcripts shaped as pptmstr's driver writes them, for the
measurements in pptmstr_real_panes.py and the census in pptmstr_census.py.

pptmstr is imported READ-ONLY from ../pptmstr; nothing here loads imtypeset.

Two sources, both real text and neither invented for the purpose:

sessions   Claude Code session logs (`*.jsonl`, sub-agents included) replayed
           through the rules of pptmstr/driver.py: a thinking block is
           REASONING, a text block is OUTPUT, a tool use is
           "\\nName(compact args)\\n" as TOOL_CALL, a tool result is
           "name -> first 300 characters\\n" as TOOL_RESULT (ERROR when it
           failed), a user turn is "\\n> text\\n" as SYSTEM. `_compact_args`
           and `_compact_result` are pptmstr's own functions. This is what the
           CONTEXT pane showed for those sessions, to the extent the log holds
           what the SDK streamed. Read in place; nothing of them is copied.
repo       the agent-written reports under .concerns/ in this repository
           (each one is a sub-agent's deliverable, which is what DETAIL's
           "what it delivered" renders), strung into one transcript with a
           tool call and a tool result between them.
"""

import glob
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
PPTMSTR = os.environ.get("PPTMSTR_ROOT", os.path.join(os.path.dirname(REPO), "pptmstr"))
if PPTMSTR not in sys.path:
    sys.path.append(PPTMSTR)

DEFAULT_SESSIONS = os.path.expanduser("~/.claude/projects/-home-wreel-Source-imtypeset")


# Logs written after this moment are left out: the session doing the measuring is
# appending to its own, and a corpus that grows between two runs compares nothing.
SESSIONS_BEFORE = os.environ.get("SPIKE_SESSIONS_BEFORE", "2026-10-10T06:15:00")


def session_files(directory: str = DEFAULT_SESSIONS) -> list:
    import datetime

    cutoff = datetime.datetime.fromisoformat(SESSIONS_BEFORE).timestamp()
    found = glob.glob(os.path.join(directory, "*.jsonl"))
    found += glob.glob(os.path.join(directory, "*", "subagents", "*.jsonl"))
    return sorted(path for path in found if os.path.getmtime(path) < cutoff)


def report_files() -> list:
    return sorted(glob.glob(os.path.join(REPO, ".concerns", "*--result.md")))


def ops_from_session(path: str) -> list:
    """One session log as the (SegmentKind, text) appends pptmstr's driver would make."""
    from pptmstr.driver import _compact_args, _compact_result
    from pptmstr.transcript import SegmentKind

    ops: list = []
    names: dict = {}
    with open(path, encoding="utf-8", errors="replace") as lines:
        for line in lines:
            try:
                entry = json.loads(line)
            except ValueError:
                continue
            message = entry.get("message") if isinstance(entry, dict) else None
            if not isinstance(message, dict):
                continue
            content = message.get("content")
            if entry.get("type") == "user" and isinstance(content, str):
                ops.append((SegmentKind.SYSTEM, f"\n> {content}\n"))
                continue
            if not isinstance(content, list):
                continue
            for block in content:
                if not isinstance(block, dict):
                    continue
                kind = block.get("type")
                if kind == "thinking" and block.get("thinking"):
                    ops.append((SegmentKind.REASONING, block["thinking"]))
                elif kind == "text" and block.get("text"):
                    if entry.get("type") == "user":
                        ops.append((SegmentKind.SYSTEM, f"\n> {block['text']}\n"))
                    else:
                        ops.append((SegmentKind.OUTPUT, block["text"]))
                elif kind == "tool_use":
                    names[block.get("id")] = block.get("name", "tool")
                    arguments = block.get("input") if isinstance(block.get("input"), dict) else {}
                    ops.append(
                        (SegmentKind.TOOL_CALL, f"\n{block.get('name', 'tool')}({_compact_args(arguments)})\n")
                    )
                elif kind == "tool_result":
                    name = names.get(block.get("tool_use_id"), "tool")
                    segment = SegmentKind.ERROR if block.get("is_error") else SegmentKind.TOOL_RESULT
                    ops.append((segment, f"{name} -> {_compact_result(block.get('content'))}\n"))
    return ops


def ops_from_reports(repeat: int = 1, limit_files: int = 0) -> list:
    """The .concerns reports as appends: a turn each."""
    from pptmstr.driver import _compact_result
    from pptmstr.transcript import SegmentKind

    ops: list = []
    files = report_files()
    if limit_files:
        files = files[:limit_files]
    for _ in range(repeat):
        for path in files:
            with open(path, encoding="utf-8") as handle:
                text = handle.read()
            name = os.path.basename(path)
            ops.append((SegmentKind.SYSTEM, f"\n> report on {name}\n"))
            ops.append((SegmentKind.REASONING, "Reading the result file before summarising it.\n"))
            ops.append((SegmentKind.TOOL_CALL, f"\nRead(file_path=.concerns/{name})\n"))
            ops.append((SegmentKind.TOOL_RESULT, f"Read -> {_compact_result(text)}\n"))
            ops.append((SegmentKind.OUTPUT, text if text.endswith("\n") else text + "\n"))
    return ops


def transcript_from_ops(ops):
    from pptmstr.transcript import Transcript

    transcript = Transcript()
    for kind, text in ops:
        transcript.append(kind, text)
    return transcript


def transcript_from_session(path: str):
    return transcript_from_ops(ops_from_session(path))


def transcript_from_reports(repeat: int = 1, limit_files: int = 0):
    return transcript_from_ops(ops_from_reports(repeat, limit_files))


def pick_session(which: str, directory: str = DEFAULT_SESSIONS):
    """(path, ops) of the session whose transcript is the median, p90 or longest by lines."""
    sized = []
    for path in session_files(directory):
        ops = ops_from_session(path)
        lines = sum(text.count("\n") for _, text in ops)
        if lines:
            sized.append((lines, path, ops))
    sized.sort(key=lambda item: item[0])
    if not sized:
        raise SystemExit(f"no session logs with text under {directory}")
    at = {"median": (len(sized) - 1) // 2, "p90": int(0.9 * (len(sized) - 1)), "max": len(sized) - 1}[which]
    return sized[at][1], sized[at][2]


def lines_and_blocks(transcript, finish: bool = True):
    """(NodeTranscript, BlockCursor) exactly as transcript_pane.draw derives them in RICH mode."""
    from pptmstr.ui.blocks import BlockCursor
    from pptmstr.ui.transcript_pane import NodeTranscript

    cache = NodeTranscript()
    cache.sync(transcript, transcript.published_length)
    cursor = BlockCursor()
    stable = len(cache.lines) - (1 if cache.open_line else 0)
    cursor.feed(cache.lines[:stable])
    if finish:
        cursor.finish()
    return cache, cursor


def turn_blocks(text: str):
    """DETAIL's parse of a settled answer: detail.DetailState.deliverable_blocks."""
    from pptmstr.transcript import SegmentKind
    from pptmstr.ui.blocks import BlockCursor
    from pptmstr.ui.detail import _ProseLine

    cursor = BlockCursor()
    cursor.feed([_ProseLine(SegmentKind.OUTPUT, line) for line in text.split("\n")])
    cursor.finish()
    return tuple(cursor.blocks)
