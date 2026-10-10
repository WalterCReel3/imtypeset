"""Experiment A: can font BYTES reach the installed imgui_bundle's atlas from
Python through ImFontConfig.font_data, with no file on disk?

Run with the reference's interpreter, from the imtypeset root:

    PYTHONDONTWRITEBYTECODE=1 ../imgui-adv-text/.venv/bin/python -B \
        tools/experiments/exp_a_font_bytes.py [FONT [CODEPOINT ...]]

With no arguments it runs every way of handing the bytes over, each in its own
subprocess (a crash is then a result), for tests/data/fonts/Roboto-Regular.ttf
and U+0041. Prints one JSON object per variant. Writes nothing.

`--variant NAME FONT CP...` is what the subprocesses run.
"""

from __future__ import annotations

import ctypes
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
DEFAULT_FONT = os.path.join(ROOT, "tests", "data", "fonts", "Roboto-Regular.ttf")

VARIANTS = [
    "bytes",
    "bytearray",
    "memoryview",
    "numpy",
    "ctypes_array",
    "ctypes_void_p",
    "int_address",
    "capsule_unnamed",
    "capsule",
    "capsule_owned_by_atlas",
]

# Kept at module level so the buffer outlives the atlas: since 1.92 the data
# has to persist for as long as the atlas does.
_KEEP: list = []


# nanobind's caster for `void*` takes a PyCapsule whose name is exactly this
# and nothing else, None included (found by trying: the setter's TypeError
# names typing_extensions.CapsuleType, and an unnamed capsule is refused). A
# capsule keeps the POINTER to its name, so the bytes object must stay alive.
NB_CAPSULE_NAME = b"nb_handle"


def capsule_of(address: int, name: bytes | None = NB_CAPSULE_NAME):
    """A PyCapsule holding `address`, with no destructor."""
    new = ctypes.pythonapi.PyCapsule_New
    new.restype = ctypes.py_object
    new.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p]
    return new(address, name, None)


def make_value(variant: str, data: bytes, imgui):
    """The object assigned to font_data, and whether the atlas is to own it."""
    if variant == "bytes":
        _KEEP.append(data)
        return data, False
    if variant == "bytearray":
        value = bytearray(data)
        _KEEP.append(value)
        return value, False
    if variant == "memoryview":
        _KEEP.append(data)
        return memoryview(data), False
    if variant == "numpy":
        import numpy as np

        value = np.frombuffer(data, dtype=np.uint8).copy()
        _KEEP.append(value)
        return value, False
    buffer = (ctypes.c_ubyte * len(data)).from_buffer_copy(data)
    _KEEP.append(buffer)
    address = ctypes.addressof(buffer)
    if variant == "ctypes_array":
        return buffer, False
    if variant == "ctypes_void_p":
        return ctypes.c_void_p(address), False
    if variant == "int_address":
        return address, False
    if variant == "capsule_unnamed":
        return capsule_of(address, None), False
    if variant == "capsule":
        return capsule_of(address), False
    if variant == "capsule_owned_by_atlas":
        # The atlas frees with ImGui::MemFree, so the block has to come from
        # ImGui::MemAlloc. The binding does not publish MemAlloc; take it from
        # the extension module itself.
        import imgui_bundle._imgui_bundle as native

        lib = ctypes.CDLL(native.__file__)
        mem_alloc = lib["_ZN5ImGui8MemAllocEm"]
        mem_alloc.restype = ctypes.c_void_p
        mem_alloc.argtypes = [ctypes.c_size_t]
        block = mem_alloc(len(data))
        ctypes.memmove(block, data, len(data))
        return capsule_of(block), True
    raise SystemExit("unknown variant " + variant)


def run_variant(variant: str, font_path: str, codepoints: list[int]) -> dict:
    out: dict = {"variant": variant, "font": os.path.basename(font_path)}
    with open(font_path, "rb") as handle:
        data = handle.read()
    out["bytes"] = len(data)

    from imgui_bundle import imgui

    out["imgui_version"] = imgui.get_version()
    imgui.create_context()
    io = imgui.get_io()
    io.set_ini_filename(None)
    io.display_size = imgui.ImVec2(800, 600)
    io.delta_time = 1.0 / 60.0
    io.backend_flags |= imgui.BackendFlags_.renderer_has_textures.value

    cfg = imgui.ImFontConfig()
    value, owned = make_value(variant, data, imgui)
    try:
        cfg.font_data = value
    except Exception as exc:  # the binding refused the object
        lines = str(exc).splitlines()
        out["assign"] = "%s: %s" % (type(exc).__name__, " | ".join(l.strip() for l in lines if l.strip()))
        return out
    out["assign"] = "ok"
    out["readback_type"] = type(cfg.font_data).__name__
    cfg.font_data_size = len(data)
    cfg.font_data_owned_by_atlas = owned
    cfg.size_pixels = 16.0

    try:
        font = io.fonts.add_font(cfg)
    except Exception as exc:
        out["add_font"] = "%s: %s" % (type(exc).__name__, str(exc).splitlines()[0])
        return out
    out["add_font"] = "ok" if font is not None else "None"
    if font is None:
        return out

    for _ in range(3):
        imgui.new_frame()
        imgui.begin("a")
        imgui.push_font(font, 16.0)
        imgui.text_unformatted("".join(chr(c) for c in codepoints))
        imgui.pop_font()
        imgui.end()
        imgui.render()
    out["frame_vertices"] = imgui.get_draw_data().total_vtx_count
    out["font_loader"] = io.fonts.font_loader_name

    baked = font.get_font_baked(16.0)
    # The atlas texture itself, so that "visible" is checked on pixels and not
    # only on the flag the loader set.
    tex = io.fonts.tex_data
    pixels = tex.get_pixels_array().reshape(tex.height, tex.width, tex.bytes_per_pixel)
    out["atlas"] = [tex.width, tex.height, tex.bytes_per_pixel]
    glyphs = []
    for cp in codepoints:
        glyph = baked.find_glyph_no_fallback(cp)
        if glyph is None:
            glyphs.append({"cp": "U+%04X" % cp, "found": False})
            continue
        x0, x1 = round(glyph.u0 * tex.width), round(glyph.u1 * tex.width)
        y0, y1 = round(glyph.v0 * tex.height), round(glyph.v1 * tex.height)
        glyphs.append(
            {
                "cp": "U+%04X" % cp,
                "found": True,
                "inked_texels": int((pixels[y0:y1, x0:x1, -1] > 0).sum()),
                "visible": bool(glyph.is_visible()),
                "box": [glyph.x0, glyph.y0, glyph.x1, glyph.y1],
                "advance_x": glyph.advance_x,
                "uv": [glyph.u0, glyph.v0, glyph.u1, glyph.v1],
            }
        )
    out["glyphs"] = glyphs
    imgui.destroy_context()
    out["destroy_context"] = "ok"
    return out


def main(argv: list[str]) -> int:
    if len(argv) >= 2 and argv[0] == "--variant":
        variant, font = argv[1], argv[2]
        cps = [int(a, 0) for a in argv[3:]] or [0x41]
        print(json.dumps(run_variant(variant, font, cps)))
        return 0

    font = argv[0] if argv else DEFAULT_FONT
    cps = argv[1:] or ["0x41"]
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
    for variant in VARIANTS:
        proc = subprocess.run(
            [sys.executable, "-B", os.path.abspath(__file__), "--variant", variant, font, *cps],
            capture_output=True,
            text=True,
            env=env,
            cwd=os.environ.get("TMPDIR", "/tmp"),
            timeout=120,
        )
        line = proc.stdout.strip().splitlines()[-1] if proc.stdout.strip() else ""
        try:
            result = json.loads(line)
        except ValueError:
            result = {"variant": variant, "stdout": proc.stdout[-400:]}
        result["exit"] = proc.returncode
        if proc.returncode != 0:
            result["stderr_tail"] = proc.stderr.strip().splitlines()[-3:]
        print(json.dumps(result))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
