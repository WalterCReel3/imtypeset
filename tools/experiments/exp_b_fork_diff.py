"""Experiment B, diagnosis: where the ImGui inside the installed imgui_bundle
differs from a stock ImGui header, found without calling across any boundary.

    PYTHONDONTWRITEBYTECODE=1 ../imgui-adv-text/.venv/bin/python -B \
        tools/experiments/exp_b_fork_diff.py [IMGUI_H]

IMGUI_H defaults to build/bundle-research/imgui-1.92.9-docking/imgui.h (the
upstream tag v1.92.9-docking). Three things are asked, all of the bundle's own
binding or of its stub file:
  - sizeof(ImGuiIO) and sizeof(ImGuiStyle) as the bundle's ImGui was compiled:
    its own debug_check_version_and_data_layout() is called with candidate
    sizes until it stops raising;
  - the members of ImGuiStyle, in order, as the stub lists them (it carries the
    original C++ declaration of each) against the header's;
  - the values of three enums against the header's.
Prints JSON. Writes nothing.
"""

from __future__ import annotations

import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))


def bundle_sizes(imgui) -> dict:
    def passes(io: int, style: int) -> str:
        try:
            imgui.debug_check_version_and_data_layout(imgui.get_version(), io, style, 8, 16, 20, 4)
            return ""
        except RuntimeError as exc:
            return str(exc)

    out: dict = {}
    for io in range(2048, 4096, 8):
        if "sz_io" not in passes(io, 0):
            out["ImGuiIO"] = io
            break
    for style in range(1024, 2048, 4):
        if not passes(out.get("ImGuiIO", 0), style):
            out["ImGuiStyle"] = style
            break
    return out


def header_enum(source: str, name: str) -> dict:
    body = re.search(r"enum %s\s*(?::\s*\w+)?\s*\{(.*?)\n\};" % name, source, re.S).group(1)
    body = re.sub(r"#ifndef IMGUI_DISABLE_OBSOLETE_FUNCTIONS.*?#endif", "", body, flags=re.S)
    values: dict = {}
    current = -1
    for line in body.splitlines():
        match = re.match(r"(\w+)\s*(?:=\s*([^,]+))?,?$", line.split("//")[0].strip())
        if not match:
            continue
        key, value = match.groups()
        if value is None:
            current += 1
        else:
            try:
                current = eval(value.strip(), {}, values)  # the header's own constant expressions
            except Exception:
                continue
        values[key] = current
    return values


def squash(name: str) -> str:
    return name.replace("_", "").lower()


def main(argv: list[str]) -> int:
    header = argv[0] if argv else os.path.join(ROOT, "build", "bundle-research", "imgui-1.92.9-docking", "imgui.h")
    source = open(header, encoding="utf-8").read()
    from imgui_bundle import imgui
    import imgui_bundle

    out: dict = {"header": os.path.relpath(header, ROOT), "bundle_imgui": imgui.get_version()}
    out["bundle_sizeof"] = bundle_sizes(imgui)

    stub = os.path.join(os.path.dirname(imgui_bundle.__file__), "imgui", "__init__.pyi")
    stub_text = open(stub, encoding="utf-8").read()
    style_stub = stub_text[stub_text.index("\nclass Style:") :]
    style_stub = style_stub[: style_stub.index("\nclass ", 10)]
    member = re.compile(r"^\s+#\s+(\w[\w ]*?)\s+\**([A-Z]\w*)(\[[^\]]*\])?;", re.M)
    in_stub = [m.group(2) for m in member.finditer(style_stub)]
    style_header = source[source.index("\nstruct ImGuiStyle\n") :]
    style_header = style_header[: style_header.index("\n};")]
    in_header = [m.group(2) for m in re.finditer(r"^\s+(\w[\w ]*?)\s+\**([A-Z]\w*)(\[[^\]]*\])?;", style_header, re.M)]
    out["ImGuiStyle_members"] = {
        "header_count": len(in_header),
        "stub_count": len(in_stub),
        "only_in_bundle": [m for m in in_stub if m not in in_header],
        "only_in_header": [m for m in in_header if m not in in_stub],
    }

    enums = {}
    for cpp, py in (("ImGuiStyleVar_", imgui.StyleVar_), ("ImGuiCol_", imgui.Col_), ("ImGuiBackendFlags_", imgui.BackendFlags_)):
        stock = header_enum(source, cpp)
        bound = {squash(e.name): int(e.value) for e in py}
        differ = {}
        for key, value in stock.items():
            short = squash(key[len(cpp) :])
            if short in bound and bound[short] != value:
                differ[key] = {"header": value, "bundle": bound[short]}
        known = {squash(k[len(cpp) :]) for k in stock}
        enums[cpp] = {
            "header_count": len(stock),
            "bundle_count": len(bound),
            "values_that_differ": differ,
            "only_in_bundle": [e.name for e in py if squash(e.name) not in known],
        }
    out["enums"] = enums
    print(json.dumps(out, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
