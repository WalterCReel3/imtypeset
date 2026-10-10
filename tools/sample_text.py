#!/usr/bin/env python3
"""
Reduce a markdown file to plain paragraphs, for the benchmarks.

The port has no markdown, so the benchmarks cannot set the reference's
`demo/sample.md` as the reference does. This writes the document's prose as one
paragraph per line with a blank line between paragraphs, which is at once what
`bench/bench_*.cpp` read and a markdown source the reference parses into
paragraph blocks and nothing else -- so both sides can be handed the same file.

    python3 tools/sample_text.py ../imgui-adv-text/demo/sample.md bench/data/sample.txt

What becomes a paragraph: a paragraph, a heading (its text), a block quote, each
list item (its marker dropped). What is dropped: fenced code (its line ends are
content and it is not broken), tables, rules. Inline syntax is removed and its
text kept: emphasis markers, backquotes, and a link becomes its label.

Uses the standard library only and reads nothing but its first argument.
"""

from __future__ import annotations

import re
import sys

_ITEM = re.compile(r"^\s*(?:[-*+]|\d+[.)])\s+")
_RULE = re.compile(r"^\s*(?:-{3,}|\*{3,}|_{3,})\s*$")
_LINK = re.compile(r"!?\[([^\]]*)\]\([^)]*\)")
_LEFT = re.compile(r"[*_`\[\]<>#|\\$~]")


def _inline(text: str) -> str:
    text = _LINK.sub(r"\1", text)
    text = text.replace("`", "")
    text = re.sub(r"\*+", "", text)
    text = re.sub(r"(?<!\w)_+|_+(?!\w)", "", text)
    return " ".join(text.split())


def paragraphs(source: str) -> tuple[list[str], dict[str, int]]:
    out: list[str] = []
    dropped = {"code lines": 0, "table rows": 0, "rules": 0}
    current: list[str] = []
    fenced = False

    def flush() -> None:
        if current:
            text = _inline(" ".join(current))
            if text:
                out.append(text)
            current.clear()

    for line in source.splitlines():
        if line.lstrip().startswith("```"):
            flush()
            fenced = not fenced
            continue
        if fenced:
            dropped["code lines"] += 1
            continue
        if not line.strip():
            flush()
        elif _RULE.match(line):
            flush()
            dropped["rules"] += 1
        elif line.lstrip().startswith("|"):
            flush()
            dropped["table rows"] += 1
        elif line.startswith("#"):
            flush()
            current.append(line.lstrip("#").strip())
            flush()
        elif _ITEM.match(line):
            flush()
            current.append(_ITEM.sub("", line))
        elif line.lstrip().startswith(">"):
            current.append(line.lstrip()[1:].strip())
        else:
            current.append(line.strip())
    flush()
    return out, dropped


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        sys.stderr.write("usage: sample_text.py <source.md> <out.txt>\n")
        return 2
    with open(argv[1], encoding="utf-8") as handle:
        paras, dropped = paragraphs(handle.read())
    for index, text in enumerate(paras):
        # Anything left that markdown would read as syntax would make the two
        # sides set different text; a paragraph opening like a list item would
        # make the reference set a list.
        if _LEFT.search(text) or _ITEM.match(text):
            sys.stderr.write(f"paragraph {index + 1} still holds markdown syntax: {text!r}\n")
            return 1
    with open(argv[2], "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n\n".join(paras) + "\n")
    count = sum(len(text) for text in paras)
    words = sum(len(text.split()) for text in paras)
    sys.stderr.write(
        f"{len(paras)} paragraphs, {words} words, {count} characters; dropped "
        + ", ".join(f"{n} {what}" for what, n in dropped.items())
        + "\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
