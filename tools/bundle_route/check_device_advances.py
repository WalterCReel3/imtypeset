"""Code measured on the device, inside an installed imgui_bundle wheel: the
advance the library lays a row of code out with is the one the WHEEL's own
atlas entry holds, read through the wheel's own accessors, and a row of N
characters is N of them wide. This is the one place the library reads
ImFontGlyph::AdvanceX at layout time, in a process whose Dear ImGui it was not
compiled with.

    IMTYPESET_LIBRARY=<out>/libimtypeset_bundle.so PYTHONPATH=python \
        <python> -P -B tools/bundle_route/check_device_advances.py

Headless: a Dear ImGui context of the wheel's and no window, at display
densities 1 and 2. Prints one line a check; exit 0 when all pass, 1 otherwise.
The same behaviour is tested against the self-hosted library in
tests/python/test_binding.py and in C++ in tests/test_device_advances.cpp.
"""

import math
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FONT_DIR = os.path.join(REPO, "tests", "data", "fonts")
FONTS = {"Regular": "Roboto-Regular.ttf", "Bold": "Roboto-Bold.ttf", "Code": "Inconsolata-Medium.ttf"}
RULER = "1234567890" * 4

from imgui_bundle import imgui  # noqa: E402

import imtypeset as ts  # noqa: E402

failed = []


def check(name: str, got, want=True) -> None:
    ok = got == want
    print("PASS" if ok else "FAIL", name, "" if ok else f": got {got!r}, want {want!r}")
    if not ok:
        failed.append(name)


def near(a: float, b: float) -> bool:
    return abs(a - b) < 1.0 / 256.0


def main() -> int:
    imgui.create_context()
    io = imgui.get_io()
    io.backend_flags |= imgui.BackendFlags_.renderer_has_textures
    io.display_size = (1600.0, 600.0)
    io.delta_time = 1.0 / 60.0
    io.set_ini_filename("")
    io.fonts.add_font_default()
    ts.create_context()
    for role, name in FONTS.items():
        ts.add_font(ts.Face[role], os.path.join(FONT_DIR, name))
    code_font = io.fonts.fonts[ts.font_atlas_index(ts.Face.Code)]
    regular = ts.font_metrics(ts.Face.Regular, 1000.0)
    code = ts.font_metrics(ts.Face.Code, 1000.0)

    document = ts.Document.from_blocks([ts.Block(ts.BlockKind.Code, RULER + "\n" + RULER[:10])])
    for density in (1.0, 2.0):
        io.display_framebuffer_scale = (density, density)
        for height in (14.0, 15.0, 16.0, 17.0, 18.0, 19.0, 20.0):
            where = f"density {density:g}, pixel height {height:g}"
            style = ts.Style(FontSize=height, Align=ts.Align.Left)
            cell = ts.device_glyph_advance(ts.Face.Code, "1", style)
            # The entry the library draws the Code face from: the face's pixel
            # height at the style's em, in framebuffer pixels, rounded by ImGui,
            # at the rasteriser's density 1 above a display density of 1.
            em = height * regular.em_size / regular.font_size
            size = em * code.font_size / code.em_size * density
            baked = (
                code_font.get_font_baked(size)
                if density == 1.0
                else code_font.get_font_baked(size, 1.0)
            )
            own = baked.find_glyph(ord("1")).advance_x
            check(f"{where}: the device's advance is the wheel's entry's, to the pixel",
                  near(cell * density, math.floor(own + 0.5)))
            check(f"{where}:   and that entry's is a whole pixel (the wheel hints)", own, round(own))
            document.layout(1500.0, style)
            for line, count in ((0, 40), (1, 10)):
                box = document.block_line(0, line)
                check(f"{where}:   a row of {count} characters is {count} of them wide",
                      near(box.max[0] - box.min[0], count * cell))
            by_font = ts.Style(FontSize=height, Align=ts.Align.Left, Advances=ts.Advances.Font)
            document.layout(1500.0, by_font)
            box = document.block_line(0, 0)
            check(f"{where}:   and by the font's advances {40 * em / 2:.2f} px wide",
                  abs((box.max[0] - box.min[0]) - 40 * ts.glyph_advance(ts.Face.Code, em, "1")) < 0.01)
    document.close()
    ts.destroy_context()
    imgui.destroy_context()
    print("all passed" if not failed else f"{len(failed)} FAILED: {failed}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
