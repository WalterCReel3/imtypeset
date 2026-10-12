"""Is typeset text as sharp as the application's own on this display?

    python/host/build_bundle_library.sh
    IMTYPESET_LIBRARY=build/py-host-bundle/libimtypeset_bundle.so \
        python3 -P -B examples/python/hidpi_check.py [--frames N --screenshot FILE]

For a display whose framebuffer is denser than its window coordinates, which
is every Mac the wheel supports (Retina: two framebuffer pixels to the unit).
It answers twice.

BY NUMBERS, printed once and shown in the window. The first paragraph's
vertices are taken from the library (Paragraph.draw_to_buffers) and compared
with the font atlas:

    texels per framebuffer pixel   1.000 is a glyph bitmap made for this
                                   display; 0.500 at density 2 is one made for
                                   a display half as dense and stretched,
                                   which looks soft.
    off the grid                   glyph corners and rule edges that are not on
                                   a framebuffer pixel; 0 is right.

and the line ends `PASS` or `FAIL`.

BY EYE, which is the one that counts. The window shows, top to bottom:

    1  the display's density as ImGui has it (io.DisplayFramebufferScale) and
       the DPI font scale (style.FontScaleDpi). On a Retina Mac: 2.0 and 1.0.
       If it says 1.0 on a Retina display the runner is not reporting the
       density, and nothing below means anything.
    2  one sentence twice, the same face at the same size: first by
       imgui.text, then by imtypeset. They must be EQUALLY SHARP and equally
       heavy. Typeset text that is softer, or bolder and blurred, is a FAIL
       whatever the numbers say.
    3  a formula. The fraction's bar and the radical's bar are hairlines, one
       framebuffer pixel thick, thinner than the separator under them (which
       is one unit, two framebuffer pixels at density 2); the radical's bar
       starts at the top of its sign with no gap and no step.
    4  a justified paragraph. Drag across it: the highlight's edges are sharp,
       not a grey fringe half a pixel wide, and the words do not shift.

If there is a second, ordinary display, drag the window onto it: line 1
changes to 1.0, the numbers still say PASS, and the text is as sharp as the
application's there too.

--screenshot FILE writes the last frame at the framebuffer's resolution, which
is the thing to send back with the printed lines.

--assume-density D takes the numbers as if the display's density were D,
whatever it is: for trying this script where there is no dense display. The
picture on the screen is then NOT what the numbers are about.

Exits 77 when IMTYPESET_LIBRARY is not a bundle-hosted library, 1 on FAIL.
"""

import argparse
import os
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path[:0] = [os.path.join(REPO, "python"), os.path.dirname(os.path.abspath(__file__))]

SENTENCE = "Sphinx of black quartz, judge my vow: the five boxing wizards jump."
FORMULA = "The roots are $x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}$, as ever."
PROSE = (
    "It is a truth universally acknowledged, that a single man in possession of a good "
    "fortune, must be in want of a wife. However little known the feelings or views of such "
    "a man may be on his first entering a neighbourhood, this truth is so well fixed in the "
    "minds of the surrounding families, that he is considered as the rightful property of "
    "some one or other of their daughters."
)


def measure(vertices: bytes, build, density: float, texture) -> dict:
    """What the quads of one draw say about how they meet the framebuffer."""
    size = build.sizeof_imdrawvert
    pos, uv = build.offsetof_imdrawvert_pos, build.offsetof_imdrawvert_uv
    glyphs = rects = off = 0
    low, high = None, None

    def on_grid(value: float) -> bool:
        device = value * density
        return abs(device - round(device)) <= 1.0 / 64.0

    for at in range(0, len(vertices) - 4 * size + 1, 4 * size):
        x0, y0 = struct.unpack_from("<ff", vertices, at + pos)
        u0, v0 = struct.unpack_from("<ff", vertices, at + uv)
        x1, y1 = struct.unpack_from("<ff", vertices, at + 2 * size + pos)
        u1, v1 = struct.unpack_from("<ff", vertices, at + 2 * size + uv)
        if not all(on_grid(v) for v in (x0, y0, x1, y1)):
            off += 1
        if u1 == u0 and v1 == v0:
            rects += 1
            continue
        glyphs += 1
        for texels, pixels in (
            ((u1 - u0) * texture.width, (x1 - x0) * density),
            ((v1 - v0) * texture.height, (y1 - y0) * density),
        ):
            if pixels > 0.0:
                ratio = texels / pixels
                low = ratio if low is None else min(low, ratio)
                high = ratio if high is None else max(high, ratio)
    passed = glyphs > 0 and off == 0 and abs(low - 1.0) < 0.01 and abs(high - 1.0) < 0.01
    return {"glyphs": glyphs, "rects": rects, "off": off, "low": low, "high": high, "passed": passed}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--font-size", type=float, default=17.0, help="pixel height of the type")
    parser.add_argument("--frames", type=int, default=0, help="run N frames and exit")
    parser.add_argument("--screenshot", default="", help="write the last frame as a PNG")
    parser.add_argument("--assume-density", type=float, default=0.0)
    args = parser.parse_args()
    if not os.path.basename(os.environ.get("IMTYPESET_LIBRARY", "")).startswith("libimtypeset_bundle"):
        sys.stderr.write(
            "hidpi_check.py: SKIPPED: IMTYPESET_LIBRARY does not name a bundle-hosted library; "
            "python/host/build_bundle_library.sh builds one and prints the line to use.\n"
        )
        sys.exit(77)

    from imgui_bundle import imgui

    import imtypeset as ts
    import bundle_support as support

    state = {"frame": 0, "selection": (0, 0), "verdict": None, "line": ""}
    paragraphs = {}

    def numbers(width: float) -> None:
        """Once the atlas has what the paragraphs need: the frame after they
        were first drawn, so that the texture measured is the one drawn from."""
        io = imgui.get_io()
        real = io.display_framebuffer_scale.x
        density = args.assume_density if args.assume_density > 0.0 else real
        if args.assume_density > 0.0:
            io.display_framebuffer_scale = imgui.ImVec2(density, density)
        try:
            results = []
            for _ in range(2):  # the first may add glyphs to the atlas and move the rest
                results = []
                for name in ("sentence", "formula"):
                    vertices, _ = paragraphs[name].draw_to_buffers(
                        (12.0, 12.0), (0.0, 0.0, width + 24.0, 4000.0)
                    )
                    results.append(measure(vertices, ts.build_info(), density, io.fonts.tex_data))
        finally:
            if args.assume_density > 0.0:
                io.display_framebuffer_scale = imgui.ImVec2(real, real)
        glyphs = sum(r["glyphs"] for r in results)
        off = sum(r["off"] for r in results)
        low = min(r["low"] for r in results)
        high = max(r["high"] for r in results)
        state["verdict"] = all(r["passed"] for r in results)
        state["line"] = (
            f"{glyphs} glyphs and {sum(r['rects'] for r in results)} rules at density {density:g}"
            f"{' (ASSUMED, not this display)' if args.assume_density > 0.0 else ''}: "
            f"{low:.3f} to {high:.3f} texels per framebuffer pixel, {off} off the grid: "
            f"{'PASS' if state['verdict'] else 'FAIL'}"
        )
        print("hidpi_check:", state["line"])

    def gui() -> None:
        state["frame"] += 1
        io = imgui.get_io()
        style = ts.get_style()
        style.FontSize = args.font_size
        style.Align = ts.Align.Left
        width = max(imgui.get_content_region_avail().x - 8.0, 200.0)

        imgui.text(
            f"1  io.DisplayFramebufferScale {io.display_framebuffer_scale.x:g} x "
            f"{io.display_framebuffer_scale.y:g}    style.FontScaleDpi "
            f"{imgui.get_style().font_scale_dpi:g}"
        )
        if state["line"]:
            imgui.text_wrapped("   " + state["line"])
        imgui.separator()

        # The library's own Regular, as the application's font: one face, one
        # size, set by each.
        imgui.push_font(support.font_object(ts.Face.Regular), args.font_size)
        imgui.text_disabled("2  imgui.text, then imtypeset")
        imgui.text(SENTENCE)
        imgui.pop_font()
        if "sentence" not in paragraphs:
            paragraphs["sentence"] = ts.Paragraph(SENTENCE)
            paragraphs["formula"] = ts.Paragraph(FORMULA, flags=ts.TextFlags.Math)
        for name in ("sentence", "formula"):
            if name == "formula":
                imgui.separator()
                imgui.text_disabled("3  hairlines: the fraction's bar and the radical's")
            p = paragraphs[name]
            p.layout(width, style)
            x, y = imgui.get_cursor_screen_pos()
            p.draw(None, (x, y))
            imgui.dummy(p.size)
        imgui.separator()

        imgui.text_disabled("4  justified; drag across it")
        style.Align = ts.Align.Justify
        _, state["selection"] = ts.selectable_text(
            "prose", PROSE, wrap_width=min(width, 520.0), style=style, selection=state["selection"]
        )
        if state["frame"] == 3:
            numbers(width)

    def report() -> None:
        io = imgui.get_io()
        print("hidpi_check: io.DisplayFramebufferScale",
              (io.display_framebuffer_scale.x, io.display_framebuffer_scale.y),
              "| style.FontScaleDpi", imgui.get_style().font_scale_dpi,
              "| display size", (io.display_size.x, io.display_size.y))

    support.run(gui, "imtypeset: display density", size=(760, 520), frames=args.frames,
                screenshot=args.screenshot, before_exit=report)
    if args.screenshot:
        print("hidpi_check: wrote", args.screenshot)
    if state["verdict"] is None:
        sys.exit("hidpi_check.py: fewer than three frames ran; nothing was measured")
    sys.exit(0 if state["verdict"] else 1)


if __name__ == "__main__":
    main()
