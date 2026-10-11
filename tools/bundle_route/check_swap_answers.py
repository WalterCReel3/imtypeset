"""What the advtext swap asked of the library, inside an installed imgui_bundle
wheel: a role's font found as the WHEEL's ImFont object and drawn with by the
host, a draw with no draw list, what a copy holds around a rule, and a context
whose Dear ImGui context was destroyed under it.

    IMTYPESET_LIBRARY=<out>/libimtypeset_bundle.so PYTHONPATH=python \
        <python> -P -B tools/bundle_route/check_swap_answers.py

Headless: Dear ImGui contexts of the wheel's and no window. Prints one line a
check; exit 0 when all pass, 1 otherwise. The same behaviour is tested against
the self-hosted library in tests/python/test_binding.py and in C++ in
tests/test_api_answers.cpp.
"""

import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FONT_DIR = os.path.join(REPO, "tests", "data", "fonts")
FONTS = {"Regular": "Roboto-Regular.ttf", "Bold": "Roboto-Bold.ttf", "Code": "Inconsolata-Medium.ttf"}

from imgui_bundle import imgui  # noqa: E402

import imtypeset as ts  # noqa: E402

failed = []


def check(name: str, got, want=True) -> None:
    ok = got == want
    print("PASS" if ok else "FAIL", name, "" if ok else f": got {got!r}, want {want!r}")
    if not ok:
        failed.append(name)


def code_of(call):
    try:
        call()
    except ts.Error as exc:
        return int(exc.code)
    return None


def new_imgui_context():
    context = imgui.create_context()
    io = imgui.get_io()
    io.backend_flags |= imgui.BackendFlags_.renderer_has_textures
    io.display_size = (800.0, 600.0)
    io.delta_time = 1.0 / 60.0
    io.set_ini_filename("")
    return context


def main() -> int:
    context = new_imgui_context()
    hosts = imgui.get_io().fonts.add_font_default()
    ts.create_context()
    for role, name in FONTS.items():
        ts.add_font(ts.Face[role], os.path.join(FONT_DIR, name))

    # A role's font, as the wheel's own ImFont object.
    fonts = imgui.get_io().fonts.fonts
    places = {role: ts.font_atlas_index(ts.Face[role]) for role in FONTS}
    check("every role's font has a place in the atlas", all(p is not None for p in places.values()))
    check("  and they are different places, none the host's font",
          sorted(places.values()), [1, 2, 3])
    for role, name in FONTS.items():
        check(f"  the font at {role}'s place is named for its file",
              name[:20] in fonts[places[role]].get_debug_name())
    check("a role without a font has no place", ts.font_atlas_index(ts.Face.Italic), None)
    check("the host's own font is still the first", fonts[0].font_id, hosts.font_id)

    document = ts.Document.from_blocks(
        [ts.Block(text="une"), ts.Block(ts.BlockKind.Rule), ts.Block(text="deux")]
    )
    document.layout(400.0)
    check("a rule leaves no trace in a copy",
          document.range_text(((0, 0), (2, 4)), separator=" | "), "une | deux")

    check("no draw list outside a frame is refused",
          code_of(lambda: document.draw(None, (0.0, 0.0))), int(ts.ErrorCode.InvalidArgument))
    imgui.new_frame()
    imgui.set_next_window_size((500.0, 400.0))
    imgui.begin("w")
    draw_list = imgui.get_window_draw_list()
    before = draw_list.vtx_buffer.size()
    at = imgui.get_cursor_screen_pos()
    check("no draw list inside a window draws", document.draw(None, (at.x, at.y)), True)
    check("  into that window's draw list", draw_list.vtx_buffer.size() > before, True)
    # The host's own text in the Code role's font: a fixed pitch.
    imgui.push_font(fonts[places["Code"]], 16.0)
    narrow = imgui.calc_text_size("iiiiiiii")
    wide = imgui.calc_text_size("MMMMMMMM")
    imgui.text("drawn by the host in the role's font")
    imgui.pop_font()
    check("the host measures its text in the role's font", narrow.x > 20.0 and narrow.x == wide.x)
    imgui.end()
    imgui.render()

    # The ImGui context goes and the imtypeset context is left behind.
    imgui.destroy_context(context)
    no_context = int(ts.ErrorCode.NoContext)
    check("with its ImGui context destroyed, a draw is refused",
          code_of(lambda: document.draw_to_buffers((0.0, 0.0), (0.0, 0.0, 900.0, 900.0))), no_context)
    check("  and add_font is", code_of(lambda: ts.add_font(ts.Face.Regular, os.path.join(FONT_DIR, FONTS["Regular"]))), no_context)
    check("  and a role has no font to give", ts.get_font(ts.Face.Regular), None)
    check("  and a copy still answers", document.range_text(((0, 0), (0, 3))), "une")
    ts.destroy_context()
    check("  and the context is destroyed, its documents closed", (ts.current_context(), document.closed), (None, True))

    # The next ImGui context, with a context of its own.
    context = new_imgui_context()
    ts.create_context()
    ts.add_font(ts.Face.Regular, os.path.join(FONT_DIR, FONTS["Regular"]))
    again = ts.Document.from_blocks([ts.Block(text="encore")])
    again.layout(400.0)
    imgui.new_frame()
    vertices, _ = again.draw_to_buffers((0.0, 0.0), (0.0, 0.0, 900.0, 900.0))
    imgui.render()
    check("a new pair of contexts draws", len(vertices) > 0, True)
    ts.destroy_context()
    imgui.destroy_context(context)

    print("FAILED: " + ", ".join(failed) if failed else "all passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
