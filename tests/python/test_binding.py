"""The ctypes binding (python/imtypeset) against the library, headless.

    IMTYPESET_LIBRARY=build/py-host/libimtypeset_pyhost.so SDL_VIDEODRIVER=dummy \
        python3 -P -B tests/python/test_binding.py [-v]

Standard library only. Exits 77 (ctest: skipped) when there is no library to
test. Every position the tests compare is an index into a Python `str`, most of
them into text that is not ASCII, so a byte offset that leaked through the
binding fails here.
"""

import os
import resource
import struct
import subprocess
import sys
import unittest
import zlib

# No swap on the machines this runs on: a runaway allocation must end this
# process and not the session (CLAUDE.md, "Run every binary under a memory guard").
# A sanitised library cannot run under an address-space limit; its guard is
# ASAN_OPTIONS=hard_rss_limit_mb, which whoever preloads the runtime sets.
_LIMIT = 6_000_000 * 1024
_soft, _hard = resource.getrlimit(resource.RLIMIT_AS)
if "ASAN_OPTIONS" in os.environ:
    pass
elif _soft == resource.RLIM_INFINITY or _soft > _LIMIT:
    resource.setrlimit(resource.RLIMIT_AS, (_LIMIT, _hard))

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path[:0] = [os.path.join(REPO, "python"), os.path.join(REPO, "python", "host")]
FONT_DIR = os.path.join(REPO, "tests", "data", "fonts")

if not os.environ.get("IMTYPESET_LIBRARY"):
    print("SKIPPED: IMTYPESET_LIBRARY is not set; build python/CMakeLists.txt and point it at "
          "libimtypeset_pyhost.so")
    sys.exit(77)
os.environ.setdefault("SDL_VIDEODRIVER", "dummy")

import imtypeset as ts  # noqa: E402
import imtypeset_host as host  # noqa: E402

FONTS = {
    ts.Face.Regular: "Roboto-Regular.ttf",
    ts.Face.Bold: "Roboto-Bold.ttf",
    ts.Face.Code: "Inconsolata-Medium.ttf",
    ts.Face.Math: "latinmodern-math.otf",
}
# Two bytes or more for one character in every word, single spaces throughout,
# so that a slice of the str is what a copy of the same range gives.
ACCENTED = (
    "Ça coûte très cher, naïve façade: älter Übermut größer früher über allem, "
    "où l'été déjà fini pèse à côté d'une âme sœur éblouie, ça và très bien même"
)
RED = ts.col32(255, 0, 0)
window = None


def setUpModule():
    global window
    window = host.Window("test_binding", 640, 480, dpi_scale=1.0)
    ts.create_context()
    for face, name in FONTS.items():
        ts.add_font(face, os.path.join(FONT_DIR, name))


def tearDownModule():
    window.close()


def frame(body, capture=False):
    """One ImGui frame with one window in it, `body` called inside."""
    assert window.begin_frame()
    host.set_next_window_pos(0.0, 0.0)
    host.set_next_window_size(640.0, 480.0)
    host.begin("test", host.WINDOW_NO_TITLE_BAR | host.WINDOW_NO_RESIZE | host.WINDOW_NO_MOVE)
    try:
        return body()
    finally:
        host.end()
        window.end_frame(capture=capture)


def child(code):
    """Runs `code` in a fresh interpreter with this one's environment plus `env`."""

    def run(**env):
        full = dict(os.environ, PYTHONDONTWRITEBYTECODE="1", **env)
        prelude = f"import sys; sys.path[:0] = {sys.path[:2]!r}\n"
        return subprocess.run(
            [sys.executable, "-P", "-B", "-c", prelude + code],
            capture_output=True, text=True, env=full, timeout=120,
        )

    return run


class Loading(unittest.TestCase):
    def test_the_package_says_which_imgui_it_resolved(self):
        info = ts.host_info()
        self.assertEqual(info["host"], "self-hosted")
        self.assertTrue(info["imgui_layout_checked"])
        self.assertEqual(info["library"], os.path.abspath(os.environ["IMTYPESET_LIBRARY"]))
        self.assertEqual(info["imgui_version_num"], host.info()["imgui_version_num"])
        self.assertEqual(info["imwchar_bits"], host.info()["imwchar_bits"])
        self.assertEqual(info["rasteriser"], host.info()["font_loader"])

    def test_build_facts(self):
        build = ts.build_info()
        self.assertEqual(build.version, ts.version())
        self.assertEqual(build.abi_version, 1)
        self.assertGreaterEqual(build.imgui_version_num, 19200)
        self.assertIn(build.sizeof_imwchar, (2, 4))
        self.assertIn(build.sizeof_imdrawidx, (2, 4))
        self.assertEqual(build.face_count, len(ts.Face))

    def test_no_library_is_an_import_error_that_says_what_to_do(self):
        result = child("import imtypeset")(IMTYPESET_LIBRARY="")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ImportError", result.stderr)
        self.assertIn("IMTYPESET_LIBRARY is not set", result.stderr)

    def test_a_library_that_is_not_imtypeset_is_an_import_error(self):
        import _ctypes  # A shared library that is certainly here and certainly not ours.

        result = child("import imtypeset")(IMTYPESET_LIBRARY=_ctypes.__file__)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ImportError", result.stderr)
        self.assertIn("does not export", result.stderr)

    def test_a_refused_import_leaves_nothing_a_second_attempt_meets(self):
        import _ctypes

        # Three refusals, each leaving no module of the package in sys.modules;
        # then, in the same process, the import with the library set right.
        code = (
            "import os, sys\n"
            "good = os.environ['GOOD_LIBRARY']\n"
            "for bad in ('', '/nonexistent/libimtypeset.so', os.environ['NOT_OURS']):\n"
            "    os.environ['IMTYPESET_LIBRARY'] = bad\n"
            "    try:\n"
            "        import imtypeset\n"
            "    except ImportError:\n"
            "        pass\n"
            "    else:\n"
            "        raise SystemExit('imported with ' + repr(bad))\n"
            "    left = sorted(n for n in sys.modules if n == 'imtypeset' or n.startswith('imtypeset.'))\n"
            "    if left:\n"
            "        raise SystemExit('left behind after ' + repr(bad) + ': ' + repr(left))\n"
            # Another copy of the package, in front on the path: it must get
            # its own submodule, not the one the refused import had loaded.
            "import tempfile\n"
            "with tempfile.TemporaryDirectory() as other:\n"
            "    os.mkdir(os.path.join(other, 'imtypeset'))\n"
            "    open(os.path.join(other, 'imtypeset', '__init__.py'), 'w').write('from . import _capi\\nWHOSE = _capi.WHOSE\\n')\n"
            "    open(os.path.join(other, 'imtypeset', '_capi.py'), 'w').write('WHOSE = \"the other copy\"\\n')\n"
            "    sys.path.insert(0, other)\n"
            "    import imtypeset as stub\n"
            "    assert stub.WHOSE == 'the other copy', stub.WHOSE\n"
            "    sys.path.remove(other)\n"
            "    for name in [n for n in sys.modules if n.split('.')[0] == 'imtypeset']:\n"
            "        del sys.modules[name]\n"
            "os.environ['IMTYPESET_LIBRARY'] = good\n"
            "import imtypeset\n"
            "assert imtypeset.host_info()['library'] == os.path.abspath(good)\n"
            "assert imtypeset.version() == imtypeset.build_info().version\n"
            "print('second attempt ok')\n"
        )
        result = child(code)(
            GOOD_LIBRARY=os.environ["IMTYPESET_LIBRARY"], NOT_OURS=_ctypes.__file__
        )
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertIn("second attempt ok", result.stdout)

    # `import imtypeset` in a child whose copy of imtypeset._capi was changed
    # by `patch` first: what the import does with a package and a library that
    # do not belong together.
    PATCHED_IMPORT = (
        "import ctypes, sys, types, importlib.util as u, os\n"
        "root = [p for p in sys.path if os.path.isdir(os.path.join(p, 'imtypeset'))][0]\n"
        "package = types.ModuleType('imtypeset'); package.__path__ = [os.path.join(root, 'imtypeset')]\n"
        "sys.modules['imtypeset'] = package\n"
        "spec = u.spec_from_file_location('imtypeset._capi', os.path.join(root, 'imtypeset', '_capi.py'))\n"
        "capi = u.module_from_spec(spec); sys.modules['imtypeset._capi'] = capi; spec.loader.exec_module(capi)\n"
        "{patch}\n"
        "del sys.modules['imtypeset']\n"
        "import imtypeset\n"
    )

    def test_a_struct_that_moved_is_refused_at_import(self):
        # One mirrored struct a field longer: the library's own layout check
        # has to be what refuses it.
        patch = (
            "class Line(ctypes.Structure):\n"
            "    _fields_ = capi.Line._fields_ + [('Extra', ctypes.c_int)]\n"
            "capi.Line = Line"
        )
        result = child(self.PATCHED_IMPORT.format(patch=patch))()
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("ImportError", result.stderr)
        self.assertIn("refuses this package", result.stderr)

    def test_another_abi_revision_is_refused_at_import(self):
        result = child(self.PATCHED_IMPORT.format(patch="capi.C_ABI_VERSION = 2"))()
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("ImportError", result.stderr)
        self.assertIn("C ABI revision 1", result.stderr)

    def test_a_document_struct_that_moved_is_refused_at_import(self):
        patch = (
            "class Mark(ctypes.Structure):\n"
            "    _fields_ = capi.Mark._fields_ + [('Extra', ctypes.c_int)]\n"
            "capi.Mark = Mark\n"
            "capi.DOCUMENT_STRUCTS = tuple((n, Mark if n == 'Mark' else t) for n, t in capi.DOCUMENT_STRUCTS)"
        )
        result = child(self.PATCHED_IMPORT.format(patch=patch))()
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("ImportError", result.stderr)
        self.assertIn("refuses this package's document structs", result.stderr)

    def test_the_mirrored_document_structs_have_the_sizes_the_library_reports(self):
        raw = ts._capi.BuildInfo()
        ts.library().ImTypeset_GetBuildInfo(raw, ts._capi.C.sizeof(raw))
        for name, struct_type in ts._capi.DOCUMENT_STRUCTS:
            self.assertEqual(getattr(raw, "Sizeof" + name), ts._capi.C.sizeof(struct_type), name)
        self.assertEqual(raw.SizeofRun, ts._capi.C.sizeof(ts._capi.Run))

    def test_the_patched_import_itself_works_when_nothing_is_patched(self):
        result = child(self.PATCHED_IMPORT.format(patch="pass"))()
        self.assertEqual(result.returncode, 0, result.stderr)


class Offsets(unittest.TestCase):
    def test_the_library_counts_characters_as_python_does(self):
        for text in ("", "plain", ACCENTED, "a\U0001d465b€", "é\0é"):
            data = text.encode("utf-8")
            for index in range(len(text) + 1):
                at = len(text[:index].encode("utf-8"))
                self.assertEqual(ts.codepoint_to_byte_offset(text, index), at, (text, index))
                self.assertEqual(ts.byte_to_codepoint_offset(text, at), index, (text, index))
            self.assertEqual(ts.codepoint_to_byte_offset(text, len(text) + 5), len(data))

    def test_a_str_that_is_not_unicode_text_never_reaches_the_library(self):
        with self.assertRaises(UnicodeEncodeError):
            ts.calc_text_size("lone \ud800 surrogate")


class Errors(unittest.TestCase):
    def test_a_missing_font_file_raises_with_the_reason(self):
        with self.assertRaises(ts.Error) as caught:
            ts.add_font(ts.Face.Italic, os.path.join(FONT_DIR, "no-such-font.ttf"))
        self.assertEqual(caught.exception.code, ts.ErrorCode.FileNotFound)
        self.assertIn("no-such-font.ttf", caught.exception.message)
        self.assertIsNone(ts.get_font(ts.Face.Italic))

    def test_bytes_that_are_not_a_font_raise_font_refused(self):
        with self.assertRaises(ts.Error) as caught:
            ts.add_font(ts.Face.Italic, b"this is not a font" * 64)
        self.assertEqual(caught.exception.code, ts.ErrorCode.FontRefused)
        self.assertTrue(caught.exception.message)
        self.assertIn(caught.exception.message, str(caught.exception))

    def test_a_roles_font_is_found_in_the_atlas_by_its_place(self):
        places = {face: ts.font_atlas_index(face) for face in FONTS}
        self.assertTrue(all(place is not None and place >= 0 for place in places.values()))
        self.assertEqual(len(set(places.values())), len(FONTS))
        self.assertEqual(ts.font_atlas_index(ts.Face.Default), places[ts.Face.Regular])
        self.assertEqual(ts.font_atlas_index(ts.Face.CodeBold), places[ts.Face.Bold])
        self.assertIsNone(ts.font_atlas_index(ts.Face.BoldItalic))
        self.assertIsNone(ts.font_atlas_index(99))
        # The font at that place is the role's: the host draws its own text
        # in it, and the text is as wide as the same words typeset in that role.
        def body():
            widths = []
            host.push_font(ts.get_font(ts.Face.Code), 16.0)
            try:
                for words in ("iiiiiiii", "MMMMMMMM"):
                    host.text_wrapped(words)
                    left, _, right, _ = host.get_item_rect()
                    widths.append(right - left)
            finally:
                host.pop_font()
            return widths

        narrow, wide = frame(body)
        self.assertGreater(narrow, 20.0)
        self.assertEqual(narrow, wide)  # The Code role is a fixed-pitch face.

    def test_a_font_from_bytes_and_its_removal(self):
        with open(os.path.join(FONT_DIR, "Roboto-BoldItalic.ttf"), "rb") as source:
            font = ts.add_font(ts.Face.BoldItalic, source.read())
        self.assertEqual(ts.get_font(ts.Face.BoldItalic), font)
        self.assertTrue(ts.remove_font(ts.Face.BoldItalic))
        self.assertFalse(ts.remove_font(ts.Face.BoldItalic))
        self.assertIsNone(ts.get_font(ts.Face.BoldItalic))

    def test_with_no_context_a_call_raises_no_context(self):
        held = ts.current_context()
        ts.set_current_context(None)
        try:
            with self.assertRaises(ts.Error) as caught:
                ts.Paragraph("text")
            self.assertEqual(caught.exception.code, ts.ErrorCode.NoContext)
            with self.assertRaises(ts.Error) as caught:
                ts.calc_text_size("text")
            self.assertEqual(caught.exception.code, ts.ErrorCode.NoContext)
        finally:
            ts.set_current_context(held)
        self.assertEqual(ts.current_context(), held)

    def test_a_context_left_behind_by_its_imgui_context_refuses_and_is_destroyed(self):
        # In a child: this process's window must stay as it is.
        code = (
            "import os\n"
            "import imtypeset as ts, imtypeset_host as host\n"
            "font = os.environ['A_FONT']\n"
            "window = host.Window('first', 320, 240, dpi_scale=1.0)\n"
            "ts.create_context()\n"
            "ts.add_font(ts.Face.Regular, font)\n"
            "document = ts.Document.from_blocks([ts.Block(text='Des mots gardés.')])\n"
            "document.layout(300.0)\n"
            "clip = (0.0, 0.0, 1000.0, 1000.0)\n"
            "assert window.begin_frame()\n"
            "assert len(document.draw_to_buffers((0.0, 0.0), clip)[0]) > 0\n"
            "window.end_frame()\n"
            # The ImGui context goes and the imtypeset context is NOT destroyed
            # first, which Window.close() would do.
            "ts.library().ImTypesetHost_Destroy(); window._open = False\n"
            "assert ts.current_context() is not None\n"
            "assert ts.get_font(ts.Face.Regular) is None\n"
            "assert ts.font_atlas_index(ts.Face.Regular) is None\n"
            "for call in (lambda: document.draw_to_buffers((0.0, 0.0), clip),\n"
            "             lambda: document.draw(None, (0.0, 0.0)),\n"
            "             lambda: ts.add_font(ts.Face.Regular, font)):\n"
            "    try:\n"
            "        call()\n"
            "    except ts.Error as error:\n"
            "        assert error.code == ts.ErrorCode.NoContext, error\n"
            "    else:\n"
            "        raise SystemExit('not refused')\n"
            # What needs no ImGui still answers.
            "assert document.range_text(((0, 0), (0, 99))) == 'Des mots gardés.'\n"
            # A second ImGui context, wherever it was allocated: the rule is to
            # destroy the old imtypeset context and make one in the new.
            "window = host.Window('second', 320, 240, dpi_scale=1.0)\n"
            "ts.destroy_context()\n"
            "assert document.closed and ts.current_context() is None\n"
            "ts.create_context()\n"
            "ts.add_font(ts.Face.Regular, font)\n"
            "again = ts.Document.from_blocks([ts.Block(text='Des mots gardés.')])\n"
            "again.layout(300.0)\n"
            "assert window.begin_frame()\n"
            "assert len(again.draw_to_buffers((0.0, 0.0), clip)[0]) > 0\n"
            "window.end_frame()\n"
            "ts.destroy_context()\n"
            "window.close()\n"
            "ts.destroy_context()\n"  # With nothing to destroy: nothing.
            "print('lifetime ok')\n"
        )
        result = child(code)(A_FONT=os.path.join(FONT_DIR, "Roboto-Regular.ttf"))
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertIn("lifetime ok", result.stdout)

    def test_a_contexts_serial_is_its_own_and_is_never_used_again(self):
        held = ts.current_context()
        serial = ts.get_context_serial()
        self.assertGreater(serial, 0)
        seen = {serial}
        try:
            ts.set_current_context(None)
            self.assertEqual(ts.get_context_serial(), 0)
            # A second context is not the current one until it is made so.
            ts.set_current_context(held)
            other = ts.create_context()
            self.assertEqual(ts.get_context_serial(), serial)
            ts.set_current_context(other)
            seen.add(ts.get_context_serial())
            self.assertEqual(len(seen), 2)
            self.assertNotIn(0, seen)
            ts.destroy_context(other)
            self.assertEqual(ts.get_context_serial(), 0)
            # Made and destroyed in turn, whatever address each comes back at.
            for _ in range(16):
                again = ts.create_context()
                ts.set_current_context(again)
                number = ts.get_context_serial()
                self.assertNotIn(number, seen)
                seen.add(number)
                ts.destroy_context(again)
        finally:
            ts.set_current_context(held)
        self.assertEqual(ts.get_context_serial(), serial)

    def test_a_refused_formula_is_false_and_not_an_exception(self):
        text = "Ünïcode before $\\begin{matrix} a \\end{matrix}$ after"
        outcome = []
        frame(lambda: outcome.append((ts.text(text, flags=ts.TextFlags.Math), ts.last_error())))
        ok, error = outcome[0]
        self.assertFalse(ok)
        self.assertEqual(error.code, ts.ErrorCode.MathRefused)
        self.assertTrue(error.message)
        # The place is a character of the str, inside the formula; the byte
        # offset is further on by the two accented letters before it.
        start, stop = text.index("$"), text.rindex("$") + 1
        self.assertTrue(start <= error.codepoint_pos < stop, error)
        self.assertEqual(error.byte_pos, len(text[: error.codepoint_pos].encode("utf-8")))


class Widgets(unittest.TestCase):
    def test_steady_frames_lay_nothing_out(self):
        style = ts.Style(Align=ts.Align.Left, EmSize=15.0)

        def body():
            self.assertTrue(ts.text(ACCENTED))
            self.assertTrue(ts.text("Left, with $x^2 + y^2$.", flags=ts.TextFlags.Math, style=style))
            self.assertTrue(ts.text_with_id("counter", "a text found by its id"))

        frame(body)
        frame(body)
        before = ts.stats()
        for _ in range(50):
            frame(body)
        after = ts.stats()
        self.assertEqual(after.Layouts, before.Layouts)
        self.assertEqual(after.TextBuilds, before.TextBuilds)
        self.assertEqual(after.DrawCalls - before.DrawCalls, 150)
        self.assertGreater(after.VerticesEmitted, before.VerticesEmitted)

    def test_a_style_goes_in_and_comes_back(self):
        held = ts.get_style()
        try:
            changed = ts.get_style()
            changed.Align = ts.Align.Center
            changed.EmSize = 21.0
            ts.set_style(changed)
            self.assertEqual(ts.get_style().Align, ts.Align.Center)
            self.assertEqual(ts.get_style().EmSize, 21.0)
        finally:
            ts.set_style(held)
        with self.assertRaises(TypeError):
            ts.text("x", style={"Align": 1})

    def test_calc_text_size_wraps(self):
        wide = ts.calc_text_size(ACCENTED)
        narrow = ts.calc_text_size(ACCENTED, wrap_width=200.0)
        self.assertGreater(wide[0], 400.0)
        self.assertLessEqual(narrow[0], 200.0)
        self.assertGreater(narrow[1], 2.5 * wide[1])

    def test_a_selection_given_in_characters_comes_back_in_characters(self):
        results = []
        frame(lambda: results.append(ts.selectable_text("given", ACCENTED, selection=(3, 17))))
        self.assertEqual(results[0], (True, (3, 17)))
        frame(lambda: results.append(ts.selectable_text("kept", ACCENTED)))
        self.assertEqual(results[1], (True, None))

    def test_a_drag_and_ctrl_c_copy_the_slice_of_the_str(self):
        state = {"selection": (0, 0), "rect": None}

        def body():
            _, state["selection"] = ts.selectable_text(
                "drag", ACCENTED, wrap_width=300.0, selection=state["selection"]
            )
            state["rect"] = host.get_item_rect()

        host.set_clipboard_text("")
        frame(body)
        x0, y0, x1, y1 = state["rect"]
        self.assertGreater(y1 - y0, 60.0)  # Several lines: the drag crosses a line break.
        start, stop = (x0 + 50.0, y0 + 8.0), (x0 + 210.0, y0 + 54.0)
        try:
            for position, down in [(start, False)] * 2 + [(start, True)] * 2 + [(stop, True)] * 3 + [(stop, False)] * 2:
                host.script_mouse(*position, left_down=down)
                frame(body)
            host.script_copy_shortcut(True)
            frame(body)
            host.script_copy_shortcut(False)
            frame(body)
        finally:
            host.unscript_mouse()
        first, last = state["selection"]
        self.assertGreater(first, 0)
        self.assertGreater(last, first + 20)
        self.assertLess(last, len(ACCENTED))
        self.assertEqual(host.get_clipboard_text(), ACCENTED[first:last])

    def test_a_frame_can_be_read_back_and_written_as_a_png(self):
        frame(lambda: ts.text(ACCENTED), capture=True)
        width, height, pixels = window.last_frame
        self.assertEqual((width, height), (640, 480))
        self.assertEqual(len(pixels), width * height * 3)
        self.assertGreater(len(set(pixels[i : i + 3] for i in range(0, 640 * 120 * 3, 3))), 8)
        path = os.path.join(os.environ.get("TMPDIR", "/tmp"), f"test_binding_{os.getpid()}.png")
        try:
            host.write_png(path, width, height, pixels)
            with open(path, "rb") as source:
                data = source.read()
        finally:
            if os.path.exists(path):
                os.remove(path)
        self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(struct.unpack(">II", data[16:24]), (width, height))
        size = struct.unpack(">I", data[33:37])[0]
        self.assertEqual(data[37:41], b"IDAT")
        rows = zlib.decompress(data[41 : 41 + size])
        self.assertEqual(rows[1 : 1 + width * 3], pixels[: width * 3])


class Paragraphs(unittest.TestCase):
    def test_lines_tile_the_str(self):
        with ts.Paragraph(ACCENTED) as paragraph:
            self.assertTrue(paragraph.layout(260.0))
            self.assertTrue(paragraph.status & ts.StatusFlags.LaidOut)
            lines = paragraph.lines()
            self.assertEqual(len(lines), paragraph.line_count)
            self.assertGreater(len(lines), 3)
            self.assertEqual(lines[0].text_start, 0)
            self.assertEqual(lines[-1].text_end, len(ACCENTED))
            for above, below in zip(lines, lines[1:]):
                self.assertEqual(above.text_end, below.text_start)
                self.assertLess(above.baseline, below.baseline)
                # A line starts at a word: the break took the space before it.
                self.assertEqual(ACCENTED[below.text_start - 1], " ")
            self.assertEqual(paragraph.size[0], 260.0)
            self.assertEqual(paragraph.error.code, 0)

    def test_copy_is_the_slice(self):
        with ts.Paragraph(ACCENTED) as paragraph:
            paragraph.layout(260.0)
            self.assertEqual(paragraph.range_text((0, len(ACCENTED))), ACCENTED)
            for first, last in ((0, 1), (1, 9), (17, 61), (len(ACCENTED) - 4, len(ACCENTED))):
                self.assertEqual(paragraph.range_text((first, last)), ACCENTED[first:last])
            self.assertEqual(paragraph.range_text((5, 5)), "")
            self.assertEqual(paragraph.range_text((0, 2), after_text=True), "\n\n" + ACCENTED[:2])

    def test_hit_test_selection_rects_and_caret_agree(self):
        with ts.Paragraph(ACCENTED) as paragraph:
            paragraph.layout(260.0)
            lines = paragraph.lines()
            second = lines[1]
            middle = ((second.min[1] + second.max[1]) / 2.0)
            hit = paragraph.hit_test((second.min[0] + 1.0, middle))
            self.assertEqual((hit.text_offset, hit.line, hit.inside), (second.text_start, 1, True))
            end = paragraph.hit_test((259.0, middle))
            self.assertEqual(end.line, 1)
            self.assertTrue(second.text_start < end.text_offset <= second.text_end)

            word = paragraph.select_range((second.min[0] + 1.0, middle), (second.min[0] + 1.0, middle), ts.Granularity.Word)
            picked = ACCENTED[word[0] : word[1]]
            self.assertEqual(word[0], second.text_start)
            self.assertTrue(picked and " " not in picked.strip(), picked)
            line = paragraph.select_range((5.0, middle), (5.0, middle), ts.Granularity.Line)
            self.assertEqual(line, (second.text_start, second.text_end))

            rects = paragraph.range_rects((lines[0].text_start + 2, lines[2].text_start + 2))
            self.assertEqual(len(rects), 3)
            self.assertEqual(len(paragraph.range_rects((0, len(ACCENTED)))), len(lines))
            self.assertEqual(paragraph.range_rects((4, 4)), [])

            caret = paragraph.caret_rect(second.text_start)
            self.assertEqual(caret[0], caret[2])
            self.assertEqual((caret[1], caret[3]), (second.min[1], second.max[1]))

    def test_formulas_are_placed_in_the_str(self):
        text = "Çà: $a+b$ puis $\\begin{matrix} a \\end{matrix}$ et $$c$$ là"
        with ts.Paragraph() as paragraph:
            self.assertTrue(paragraph.set_text(text, flags=ts.TextFlags.Math))
            self.assertFalse(paragraph.layout(400.0))
            self.assertEqual(paragraph.error.code, ts.ErrorCode.MathRefused)
            formulas = paragraph.formulas()
            self.assertEqual(
                [text[f.text_start : f.text_end] for f in formulas],
                ["$a+b$", "$\\begin{matrix} a \\end{matrix}$", "$$c$$"],
            )
            self.assertEqual([f.refused for f in formulas], [False, True, False])
            self.assertEqual([f.display for f in formulas], [False, False, True])
            refused = formulas[1]
            self.assertTrue(refused.reason)
            self.assertTrue(refused.text_start <= refused.pos < refused.text_end)
            self.assertEqual(paragraph.error.codepoint_pos, refused.pos)
            self.assertEqual(formulas[0].pos, -1)
            # A formula copies as its source.
            self.assertEqual(paragraph.range_text((0, len(text))), text)

    def test_runs_are_given_in_characters(self):
        text = "début naïf, cœur à côté"
        first, last = text.index("cœur"), text.index("cœur") + len("cœur")
        with ts.Paragraph(text, runs=[ts.Run(first, last, color=RED)]) as paragraph:
            self.assertTrue(paragraph.layout(600.0))
            vertices, indices = paragraph.draw_to_buffers((0.0, 0.0), (0.0, 0.0, 1000.0, 1000.0))
            build = ts.build_info()
            size = build.sizeof_imdrawvert
            self.assertEqual(len(vertices) % size, 0)
            count = len(vertices) // size
            colours = [
                struct.unpack_from("<I", vertices, i * size + build.offsetof_imdrawvert_col)[0]
                for i in range(count)
            ]
            # One quad for each character that is not a space; four red ones.
            self.assertEqual(count, 4 * len(text.replace(" ", "")))
            self.assertEqual(sum(1 for c in colours if c == RED), 4 * len("cœur"))
            index_format = "<H" if build.sizeof_imdrawidx == 2 else "<I"
            values = [v[0] for v in struct.iter_unpack(index_format, indices)]
            self.assertEqual(len(values), count // 4 * 6)
            self.assertEqual((min(values), max(values)), (0, count - 1))
            # A second call reuses the buffers and answers the same.
            self.assertEqual(paragraph.draw_to_buffers((0.0, 0.0), (0.0, 0.0, 1000.0, 1000.0)), (vertices, indices))
            # With a selection there is one rectangle more.
            selected, _ = paragraph.draw_to_buffers((0.0, 0.0), (0.0, 0.0, 1000.0, 1000.0), selection=(0, 5))
            self.assertEqual(len(selected) // size, count + 4)

    def test_draw_into_a_window_emits_what_the_buffers_hold(self):
        with ts.Paragraph(ACCENTED) as paragraph:
            paragraph.layout(300.0)
            vertices, _ = paragraph.draw_to_buffers((8.0, 8.0), (0.0, 0.0, 640.0, 480.0))
            before = ts.stats()
            frame(lambda: paragraph.draw(host.get_window_draw_list(), (8.0, 8.0)))
            after = ts.stats()
            self.assertEqual(after.DrawCalls - before.DrawCalls, 1)
            self.assertEqual(
                after.VerticesEmitted - before.VerticesEmitted,
                len(vertices) // ts.build_info().sizeof_imdrawvert,
            )

    def test_drawing_before_layout_raises(self):
        with ts.Paragraph("not laid out") as paragraph:
            with self.assertRaises(ts.Error):
                frame(lambda: paragraph.draw(host.get_window_draw_list(), (0.0, 0.0)))

    def test_a_paragraph_does_not_outlive_its_context(self):
        held = ts.current_context()
        ts.set_current_context(None)
        other = ts.create_context()
        try:
            self.assertEqual(ts.current_context(), other)
            paragraph = ts.Paragraph("belongs to the other context")
            survivor = None
            ts.set_current_context(held)
            survivor = ts.Paragraph("belongs to the first")
            ts.destroy_context(other)
            self.assertTrue(paragraph.closed)
            with self.assertRaises(ValueError):
                paragraph.size
            with self.assertRaises(ValueError):
                paragraph.layout(100.0)
            paragraph.close()
            del paragraph
            self.assertFalse(survivor.closed)
            self.assertEqual(survivor.line_count, 0)
            survivor.close()
        finally:
            ts.set_current_context(held)
        self.assertEqual(ts.current_context(), held)


class Calls:
    """Counts the calls the package makes into the library while it is in force."""

    def __init__(self):
        self.names = []

    def __enter__(self):
        self.real = ts._lib
        outer = self

        class Counting:
            def __getattr__(self, name):
                function = getattr(outer.real, name)

                def call(*args):
                    outer.names.append(name)
                    return function(*args)

                return call

        ts._lib = Counting()
        return self

    def __exit__(self, *exc):
        ts._lib = self.real


STYLES = lambda: [  # noqa: E731
    ts.BlockStyle(Align=ts.Align.Left, SpaceBelow=8.0),
    ts.BlockStyle(Face=ts.Face.Bold, Scale=1.5, Align=ts.Align.Left, SpaceBelow=8.0),
]
PROSE = "Démonstration: words, then \\frac{1}{2} as a formula, then a link où ça coûte."
CODE = "x = 1  # été\n\ty = 2\n"


def every_kind():
    formula = PROSE.index("\\frac"), PROSE.index("{2}") + 3
    link = PROSE.index("link"), PROSE.index("link") + 4
    return [
        ts.Block(ts.BlockKind.Heading, "Un titre accentué", style=1, level=2),
        ts.Block(
            text=PROSE,
            runs=[ts.Run(*formula, flags=ts.RunFlags.Formula), ts.Run(*link, color=RED, tag=42)],
        ),
        ts.Block(ts.BlockKind.Code, CODE),
        ts.Block(ts.BlockKind.Rule, extent=2.0),
        ts.Block(ts.BlockKind.Host, "widget", extent=30.0),
        ts.Block(ts.BlockKind.ListItem, "Un élément.", marker="1.", left_indent=24.0, container=1, item=1),
    ]


def long_blocks(count):
    """About 120 bytes a block, every block with text that is not ASCII."""
    return [
        ts.Block(text=f"Bloc numéro {i:05d}: " + "où l'été déjà fini pèse à côté d'une âme sœur éblouie, ça và très bien même ici")
        for i in range(count)
    ]


class Documents(unittest.TestCase):
    def document(self):
        document = ts.Document.from_blocks(
            every_kind(), STYLES(), [ts.Container(Kind=ts.ContainerKind.List, Indent=24.0)]
        )
        self.addCleanup(document.close)
        return document

    def test_every_kind_of_block_is_set_and_stacks(self):
        document = self.document()
        self.assertEqual((len(document), document.block_count), (6, 6))
        self.assertEqual((document.style_count, document.container_count), (2, 1))
        self.assertTrue(document.layout(400.0))
        self.assertTrue(document.layout_window(0.0, 1e6))
        self.assertEqual(document.size()[0], 400.0)
        above = document.block_geometry(0)
        self.assertEqual(above.full_min[1], 0.0)
        for index in range(1, 6):
            geometry = document.block_geometry(index)
            self.assertTrue(geometry.laid_out)
            self.assertGreaterEqual(geometry.full_min[1], above.full_max[1] - 0.01)
            above = geometry
        host_block = document.block_geometry(4)
        self.assertAlmostEqual(host_block.full_max[1] - host_block.full_min[1], 30.0, places=2)
        self.assertAlmostEqual(document.block_geometry(5).full_min[0], 24.0, places=2)
        self.assertGreaterEqual(document.size()[1], above.full_max[1] - 1.0)
        self.assertEqual(document.block_error(1).code, 0)
        # A code block has a line per source line, and they tile its str.
        self.assertEqual(document.block_geometry(2).line_count, 2)
        first, second = document.block_line(2, 0), document.block_line(2, 1)
        self.assertEqual((first.text_start, second.text_start), (0, CODE.index("\ty")))
        self.assertEqual(CODE[second.text_start : second.text_end], "\ty = 2")

    def test_a_table_block_is_set_as_an_empty_block_and_said_without_raising(self):
        document = ts.Document()
        self.addCleanup(document.close)
        # Done, differently: the blocks are set, so nothing is raised.
        self.assertFalse(
            document.set_blocks([ts.Block(text="before"), ts.Block(ts.BlockKind.Table), ts.Block(text="après")])
        )
        self.assertEqual(ts.last_error().code, ts.ErrorCode.DescriptionCut)
        self.assertEqual(document.block_count, 3)
        document.layout(300.0)
        self.assertEqual(document.range_text(((0, 0), (2, 5)), separator="|"), "before|après")

    def test_from_blocks_keeps_what_the_library_cut(self):
        document = ts.Document.from_blocks([ts.Block(text="before"), ts.Block(ts.BlockKind.Table)])
        self.addCleanup(document.close)
        self.assertEqual(document.edit_error.code, ts.ErrorCode.DescriptionCut)
        self.assertEqual((len(document), document.block_count), (2, 2))
        self.assertTrue(document.set_blocks([ts.Block(text="x")]))
        self.assertEqual(document.edit_error.code, ts.ErrorCode.None_)

    def test_a_refused_edit_leaves_what_python_keeps_of_the_document(self):
        document = ts.Document.from_blocks([ts.Block(text="héllo"), ts.Block(text="wörld")])
        self.addCleanup(document.close)
        document.layout(400.0)
        # One more block than a document holds: refused whole.
        too_many = [ts.Block()] * 4194305
        with self.assertRaises(ts.Error) as raised:
            document.replace_blocks(1, 1, too_many)
        self.assertEqual(raised.exception.code, ts.ErrorCode.InvalidArgument)
        self.assertEqual((len(document), document.block_count), (2, 2))
        self.assertEqual(document.range_text(((0, 1), (1, 3)), separator="|"), "éllo|wör")
        with self.assertRaises(ts.Error):
            document.set_blocks(too_many)
        # Whatever the library holds after refusing, Python counts the same.
        self.assertEqual(len(document), document.block_count)
        self.assertTrue(document.set_blocks([ts.Block(text="naïve"), ts.Block(text="façade")]))
        document.layout(400.0)
        self.assertEqual(document.range_text(((0, 2), (1, 3)), separator="|"), "ïve|faç")

    def test_layout_is_lazy_and_a_draw_lays_out_what_is_in_view(self):
        document = ts.Document.from_blocks(long_blocks(2000))
        self.addCleanup(document.close)
        self.assertTrue(document.layout(600.0))
        stats = document.stats()
        self.assertEqual((stats.Blocks, stats.BlocksDeferred, stats.BlocksLaidOut), (2000, 2000, 0))
        frame(lambda: self.assertTrue(document.draw(host.get_window_draw_list(), (8.0, 8.0))))
        stats = document.stats()
        self.assertGreater(stats.BlocksLaidOut, 5)
        self.assertLess(stats.BlocksLaidOut, 60)  # A 480-pixel window, not 2,000 blocks.
        self.assertEqual(stats.BlocksDeferred, 2000 - stats.BlocksLaidOut)
        laid_out = stats.BlocksLaidOut
        for _ in range(20):
            frame(lambda: document.draw(host.get_window_draw_list(), (8.0, 8.0)))
        self.assertEqual(document.stats().BlocksLaidOut, laid_out)

    def test_append_text_costs_one_block(self):
        document = ts.Document.from_blocks(long_blocks(500) + [ts.Block(text="Tail:")])
        self.addCleanup(document.close)
        document.layout(600.0)
        document.layout_window(0.0, 1e7)
        before = document.stats()
        self.assertEqual(before.BlocksDeferred, 0)
        added = " é" * 3
        document.append_text(added, runs=[ts.Run(6, 8, face=ts.Face.Bold, tag=9)])
        document.layout_window(0.0, 1e7)
        after = document.stats()
        self.assertEqual(after.BlocksLaidOut, before.BlocksLaidOut + 1)
        self.assertEqual(after.StructureVisits, before.StructureVisits)
        self.assertEqual(document.range_text(((500, 0), (500, 99))), "Tail:" + added)
        # Positions in the appended, accented part are still characters.
        self.assertEqual(document.range_text(((500, 6), (500, 9))), "é é")
        with self.assertRaises(ValueError):
            ts.Document().append_text("nothing to append to")
        ruled = ts.Document.from_blocks([ts.Block(ts.BlockKind.Rule)])
        self.addCleanup(ruled.close)
        with self.assertRaises(ts.Error):
            ruled.append_text("a rule holds no text, né")
        self.assertEqual(ruled.range_text(((0, 0), (0, 9))), "")
        # Refused, so nothing of the text is kept on this side either.
        self.assertEqual((ruled._texts, ruled._last_size), ([None], 0))

    def test_a_copy_across_blocks_is_the_source(self):
        document = self.document()
        document.layout(400.0)
        # The rule gives nothing, not even a separator; the code block gives its
        # source without the line end that closes it.
        code = CODE[:-1]
        whole = document.range_text(((0, 3), (5, 99)))
        self.assertEqual(
            whole, "\n\n".join(["titre accentué", PROSE, code, "widget", "Un élément."])
        )
        self.assertEqual(
            document.range_text(((0, 3), (5, 2)), separator=" | "),
            " | ".join(["titre accentué", PROSE, code, "widget", "Un"]),
        )
        self.assertEqual(document.range_text(((1, 0), (2, 5)), separator=""), PROSE + CODE[:5])
        # Inside one block, slices of the str; the formula comes whole or not at all.
        start, end = PROSE.index("then a link"), PROSE.index("ça")
        self.assertEqual(document.range_text(((1, start), (1, end))), PROSE[start:end])
        self.assertEqual(document.range_text(((2, 9), (2, 12))), CODE[9:12])
        self.assertEqual(document.range_text(((1, 5), (1, 5))), "")
        long = ts.Document.from_blocks(long_blocks(40))
        self.addCleanup(long.close)
        long.layout(500.0)
        self.assertEqual(
            long.range_text(((0, 0), (39, 999))), "\n\n".join(b.text for b in long_blocks(40))
        )

    def test_a_hit_on_a_tagged_run_answers_its_tag_and_a_character(self):
        document = self.document()
        document.layout(400.0)
        at = PROSE.index("link") + 1
        caret = document.caret_rect((1, at))
        hit = document.hit_test((caret[0] + 1.0, (caret[1] + caret[3]) / 2.0))
        self.assertEqual(hit.position.block, 1)
        self.assertIn(hit.position.offset, (at, at + 1))
        self.assertEqual((hit.tag, hit.inside, hit.in_block), (42, True, True))
        plain = document.caret_rect((1, 2))
        self.assertEqual(document.hit_test((plain[0] + 1.0, (plain[1] + plain[3]) / 2.0)).tag, 0)
        # A caret after the accented words is where the same character is hit.
        tail = PROSE.index("coûte")
        caret = document.caret_rect((1, tail))
        hit = document.hit_test((caret[0] + 0.5, (caret[1] + caret[3]) / 2.0))
        self.assertEqual(hit.position, ts.Position(1, tail))
        drag = document.select_range((0.0, 2.0), (caret[0] + 0.5, (caret[1] + caret[3]) / 2.0))
        self.assertEqual(drag, ts.DocumentRange(ts.Position(0, 0), ts.Position(1, tail)))
        self.assertGreaterEqual(len(document.range_rects(drag)), 2)
        self.assertEqual(document.range_rects(((1, 4), (1, 4))), [])

    def test_a_block_that_gives_no_text_leaves_no_trace_in_a_copy(self):
        blocks = [
            ts.Block(text="une"),
            ts.Block(ts.BlockKind.Rule),
            ts.Block(ts.BlockKind.Host, extent=20.0),
            ts.Block(text=""),
            ts.Block(ts.BlockKind.Code, "\n\n"),
            ts.Block(text="deux"),
            ts.Block(ts.BlockKind.Host, "[w]", extent=20.0),
            ts.Block(ts.BlockKind.Rule),
        ]
        document = ts.Document.from_blocks(blocks)
        self.addCleanup(document.close)
        document.layout(400.0)
        self.assertEqual(document.range_text(((0, 0), (7, 0)), separator=" | "), "une | deux | [w]")
        self.assertEqual(document.range_text(((0, 0), (7, 0))), "une\n\ndeux\n\n[w]")
        self.assertEqual(document.range_text(((0, 0), (4, 0)), separator=" | "), "une")
        self.assertEqual(document.range_text(((1, 0), (5, 4)), separator=" | "), "deux")
        self.assertEqual(document.range_text(((1, 0), (4, 0)), separator=" | "), "")
        self.assertEqual(document.range_text(((0, 3), (5, 0)), separator=" | "), "")

    def test_a_code_block_ends_at_its_last_character(self):
        for given in ("é = 1\nb", "é = 1\nb\n", "é = 1\nb\n\n\n", "é = 1\r\nb\r\n\r\n", "é\n  \n"):
            with self.subTest(given=given):
                held = given.rstrip("\r\n")
                document = ts.Document.from_blocks(
                    [ts.Block(text="avant"), ts.Block(ts.BlockKind.Code, given), ts.Block(text="après")]
                )
                self.addCleanup(document.close)
                document.layout(400.0)
                self.assertEqual(document.range_text(((1, 0), (1, len(given)))), held)
                self.assertEqual(document.range_text(((1, 0), (1, 9999))), held)
                self.assertEqual(
                    document.range_text(((0, 0), (2, 5)), separator="|"), f"avant|{held}|après"
                )
                where = document.block_geometry(1)
                below = document.hit_test((2000.0, where.full_max[1] - 1.0))
                self.assertEqual(below.position, ts.Position(1, len(held)))
        # Text that arrives after blank lines makes them part of the block.
        document = ts.Document.from_blocks([ts.Block(ts.BlockKind.Code, "é\n\n")])
        self.addCleanup(document.close)
        document.layout(400.0)
        self.assertEqual(document.range_text(((0, 0), (0, 99))), "é")
        self.assertTrue(document.append_text("ç\n"))
        self.assertEqual(document.range_text(((0, 0), (0, 99))), "é\n\nç")
        self.assertEqual(document.range_text(((0, 3), (0, 4))), "ç")

    def test_a_mark_takes_a_formula_whole_or_not_at_all(self):
        text = "ça x^2+1 là"
        first, end = text.index("x"), text.index(" là")
        document = ts.Document.from_blocks(
            [ts.Block(text=text, runs=[ts.Run(first, end, flags=ts.RunFlags.Formula)])],
            styles=[ts.BlockStyle(Align=ts.Align.Left)],
        )
        self.addCleanup(document.close)
        document.layout(400.0)
        clip = (0.0, 0.0, 2000.0, 2000.0)

        def drawn(start, stop, place):
            mark = ts.Mark(((0, start), (0, stop)), RED, place)
            return document.draw_to_buffers((0.0, 0.0), clip, marks=[mark])

        plain = document.draw_to_buffers((0.0, 0.0), clip)
        for place in (ts.MarkPlace.Ink, ts.MarkPlace.Behind, ts.MarkPlace.Under):
            with self.subTest(place=place):
                whole = drawn(first, end, place)
                self.assertNotEqual(whole, plain)
                self.assertEqual(drawn(first + 1, end - 1, place), plain)  # Inside it: nothing.
                self.assertEqual(drawn(first, first + 1, place), whole)  # Its first character: all.
                self.assertEqual(drawn(1, first + 2, place), drawn(1, end, place))
                self.assertEqual(drawn(first + 2, len(text), place), drawn(end, len(text), place))
        self.assertEqual(document.range_text(((0, first + 1), (0, end - 1))), "")
        self.assertEqual(document.range_text(((0, first), (0, first + 1))), text[first:end])

    def test_no_draw_list_is_the_current_windows(self):
        document = self.document()
        document.layout(400.0)
        size = ts.build_info().sizeof_imdrawvert
        expected, _ = document.draw_to_buffers((8.0, 8.0), (0.0, 0.0, 640.0, 480.0))
        before = ts.stats().VerticesEmitted
        frame(lambda: self.assertTrue(document.draw(None, (8.0, 8.0))))
        self.assertEqual(ts.stats().VerticesEmitted - before, len(expected) // size)
        paragraph = ts.Paragraph("Un paragraphe.")
        self.addCleanup(paragraph.close)
        paragraph.layout(300.0)
        before = ts.stats().VerticesEmitted
        frame(lambda: paragraph.draw(None, (8.0, 8.0)))
        self.assertGreater(ts.stats().VerticesEmitted, before)
        # Outside a frame there is no current window: refused, not followed.
        with self.assertRaises(ts.Error) as caught:
            document.draw(None, (8.0, 8.0))
        self.assertEqual(caught.exception.code, ts.ErrorCode.InvalidArgument)
        with self.assertRaises(ts.Error):
            paragraph.draw(None, (8.0, 8.0))

    def test_marks_packed_from_tuples_are_the_marks(self):
        document = self.document()
        document.layout(400.0)
        clip = (0.0, 0.0, 2000.0, 2000.0)
        marks = [
            ts.Mark(((0, 3), (0, 99)), RED),
            ts.Mark(((1, 2), (5, 4)), ts.col32(0, 0, 255, 128), ts.MarkPlace.Behind),
            ts.Mark(((0, 0), (0, 5)), ts.col32(0, 255, 0), ts.MarkPlace.Under, 2.5, 3.5),
            ts.Mark(((-1, -5), (99, 7)), RED, ts.MarkPlace.Under),
        ]
        packed = document.pack_marks(marks)
        self.assertEqual(packed.count, len(marks))
        for raw, mark in zip(packed.array, marks):
            (start_block, start), (end_block, end) = mark.range
            self.assertEqual(
                (raw.Range.Start.Block, raw.Range.End.Block, raw.Color, raw.Place),
                (start_block, end_block, mark.color, int(mark.place)),
            )
            self.assertEqual((raw.Thickness, raw.Offset), (mark.thickness, mark.offset))
            self.assertEqual(raw.Range.Start.Offset, document._byte(start_block, start))
            self.assertEqual(raw.Range.End.Offset, document._byte(end_block, end))
        # A character past ASCII before the mark: the offsets are bytes.
        title = document.range_text(((0, 0), (0, 99)))
        self.assertGreater(len(title.encode()), len(title))
        self.assertEqual(packed.array[0].Range.End.Offset, len(title.encode()))
        as_tuples = [tuple(mark)[:2] for mark in marks[:1]] + [tuple(mark) for mark in marks[1:]]
        self.assertEqual(
            document.draw_to_buffers((0.0, 0.0), clip, marks=document.pack_marks(as_tuples)),
            document.draw_to_buffers((0.0, 0.0), clip, marks=marks),
        )
        self.assertEqual(document.pack_marks([]).count, 0)

    def test_marks_and_a_selection_are_drawn(self):
        document = self.document()
        document.layout(400.0)
        build = ts.build_info()
        size = build.sizeof_imdrawvert
        clip = (0.0, 0.0, 2000.0, 2000.0)

        def reds(vertices):
            return sum(
                1
                for i in range(len(vertices) // size)
                if struct.unpack_from("<I", vertices, i * size + build.offsetof_imdrawvert_col)[0] == RED
            )

        plain, indices = document.draw_to_buffers((0.0, 0.0), clip)
        count = len(plain) // size
        self.assertGreater(count, 200)
        self.assertEqual(len(indices) // build.sizeof_imdrawidx, count // 4 * 6)
        self.assertEqual(reds(plain), 4 * len("link"))  # The run's own colour.
        word = PROSE.index("words"), PROSE.index("words") + 5
        under = ts.Mark(((1, word[0]), (1, word[1])), ts.col32(0, 0, 255), ts.MarkPlace.Under)
        ink = ts.Mark(((0, 3), (0, 8)), RED)
        marked, _ = document.draw_to_buffers((0.0, 0.0), clip, marks=[under])
        self.assertEqual(len(marked) // size, count + 4)
        inked, _ = document.draw_to_buffers((0.0, 0.0), clip, marks=[ink])
        self.assertEqual(len(inked) // size, count)
        self.assertEqual(reds(inked), 4 * len("link") + 4 * len("titre"))
        selected, _ = document.draw_to_buffers((0.0, 0.0), clip, selection=((1, 0), (1, 5)))
        self.assertEqual(len(selected) // size, count + 4)
        # Into a window: packed once, the same vertices, one call a frame.
        packed = document.pack_marks([under, ink])
        both, _ = document.draw_to_buffers((8.0, 8.0), (0.0, 0.0, 640.0, 480.0), marks=packed)
        before = ts.stats().VerticesEmitted
        with Calls() as calls:
            frame(lambda: self.assertTrue(document.draw(host.get_window_draw_list(), (8.0, 8.0), marks=packed)))
        self.assertEqual(ts.stats().VerticesEmitted - before, len(both) // size)
        self.assertEqual([n for n in calls.names if "Document" in n], ["ImTypeset_DrawDocument"])
        debug, _ = document.draw_to_buffers((0.0, 0.0), clip, debug=True)
        self.assertGreater(len(debug), len(plain))

    def test_an_edit_is_one_call_into_the_library_and_a_frame_is_two(self):
        blocks = long_blocks(300)
        document = ts.Document()
        self.addCleanup(document.close)
        with Calls() as calls:
            document.set_blocks(blocks, STYLES())
        self.assertEqual(calls.names, ["ImTypeset_SetDocumentBlocks"])
        with Calls() as calls:
            document.replace_blocks(10, 5, long_blocks(2), [ts.BlockStyle(Scale=2.0)])
        self.assertEqual(calls.names, ["ImTypeset_ReplaceDocumentBlocks"])
        self.assertEqual((len(document), document.block_count, document.style_count), (297, 297, 3))
        with Calls() as calls:
            document.append_text(" suite")
        self.assertEqual(calls.names, ["ImTypeset_AppendDocumentText"])
        with Calls() as calls:
            document.replace_blocks(len(document), 0, [ts.Block(text="one more, appended as a block")])
        self.assertEqual(calls.names, ["ImTypeset_ReplaceDocumentBlocks"])

        def body():
            document.layout(600.0)
            document.draw(host.get_window_draw_list(), (8.0, 8.0), selection=((0, 2), (3, 7)))
            document.hit_test((30.0, 40.0))

        frame(body)
        with Calls() as calls:
            frame(body)
        self.assertEqual(
            calls.names,
            ["ImTypeset_LayoutDocument", "ImTypeset_DrawDocument", "ImTypeset_HitTestDocument"],
        )
        with self.assertRaises(ValueError):
            document.replace_blocks(290, 20)
        with self.assertRaises(ValueError):
            document.replace_blocks(-1, 0)

    def test_replace_blocks_keeps_positions_in_characters(self):
        blocks = long_blocks(6)
        document = ts.Document.from_blocks(blocks)
        self.addCleanup(document.close)
        document.layout(500.0)
        document.replace_blocks(1, 2, [ts.Block(text="plain ASCII"), ts.Block(text="çà et là"), ts.Block(text="x")])
        texts = [blocks[0].text, "plain ASCII", "çà et là", "x"] + [b.text for b in blocks[3:]]
        self.assertEqual(len(document), 7)
        for index, text in enumerate(texts):
            self.assertEqual(document.range_text(((index, 1), (index, 7))), text[1:7], index)
        document.replace_blocks(5, 2)
        self.assertEqual(document.block_count, 5)
        document.append_text(" — fin")
        self.assertEqual(document.range_text(((4, 3), (4, 999))), (texts[4] + " — fin")[3:])
        document.replace_blocks(0, 5)
        self.assertEqual((len(document), document.block_count), (0, 0))

    def test_offsets_are_characters_in_blocks_that_are_not_ascii(self):
        document = ts.Document.from_blocks(
            [ts.Block(text=ACCENTED), ts.Block(text="plain"), ts.Block(text=ACCENTED, runs=[ts.Run(3, 8, tag=5)])]
        )
        self.addCleanup(document.close)
        document.layout(260.0)
        geometry = document.block_geometry(2)
        self.assertGreater(geometry.line_count, 3)
        lines = [document.block_line(2, i) for i in range(geometry.line_count)]
        self.assertEqual((lines[0].text_start, lines[-1].text_end), (0, len(ACCENTED)))
        for above, below in zip(lines, lines[1:]):
            self.assertEqual(above.text_end, below.text_start)
            self.assertEqual(ACCENTED[below.text_start - 1], " ")
        second = lines[1]
        y = geometry.full_min[1] + (second.min[1] + second.max[1]) / 2.0
        hit = document.hit_test((geometry.full_min[0] + second.min[0] + 1.0, y))
        self.assertEqual((hit.position, hit.line), (ts.Position(2, second.text_start), 1))
        word = document.select_range((5.0, y), (5.0, y), ts.Granularity.Line)
        self.assertEqual(word, ((2, second.text_start), (2, second.text_end)))
        for first, last in ((0, 1), (1, 9), (17, 61), (len(ACCENTED) - 4, len(ACCENTED))):
            self.assertEqual(document.range_text(((2, first), (2, last))), ACCENTED[first:last])
        # The run was given in characters: the tag is under character 5 and not under 9.
        for at, tag in ((5, 5), (9, 0)):
            caret = document.caret_rect((2, at))
            self.assertEqual(document.hit_test((caret[0] + 1.0, (caret[1] + caret[3]) / 2.0)).tag, tag)

    def test_a_refused_formula_is_a_false_and_a_block_error(self):
        text = "Çà: $\\begin{matrix} a \\end{matrix}$ là"
        document = ts.Document.from_blocks([ts.Block(text="fine"), ts.Block(text=text, flags=ts.BlockFlags.Math)])
        self.addCleanup(document.close)
        self.assertTrue(document.layout(400.0))
        self.assertFalse(document.layout_window(0.0, 1e6))
        self.assertEqual(ts.last_error().code, ts.ErrorCode.MathRefused)
        error = document.block_error(1)
        self.assertEqual(error.code, ts.ErrorCode.MathRefused)
        self.assertTrue(text.index("$") <= error.codepoint_pos <= text.rindex("$"), error)
        self.assertEqual(document.block_error(0).code, 0)
        self.assertEqual(document.range_text(((1, 0), (1, 99))), text)

    def test_the_cost_of_a_position_does_not_grow_with_the_document(self):
        import time

        def per_call(count):
            document = ts.Document.from_blocks(long_blocks(count))
            try:
                document.layout(600.0)
                last = count - 1
                # The far end of the document: wherever a scan from the top would be slowest.
                geometry = document.block_geometry(last)
                point = (40.0, geometry.full_min[1] + 3.0)
                self.assertEqual(document.hit_test(point).position.block, last)
                text = long_blocks(count)[-1].text
                self.assertEqual(document.range_text(((last, 20), (last, 60))), text[20:60])
                best_hit = best_copy = 1e9
                for _ in range(5):
                    start = time.perf_counter()
                    for _ in range(200):
                        document.hit_test(point)
                    best_hit = min(best_hit, (time.perf_counter() - start) / 200)
                    start = time.perf_counter()
                    for _ in range(200):
                        document.range_text(((last, 20), (last, 60)))
                    best_copy = min(best_copy, (time.perf_counter() - start) / 200)
                self.assertLess(document.stats().BlocksLaidOut, 8)
                return best_hit, best_copy
            finally:
                document.close()

        small = per_call(1000)
        large = per_call(10000)
        if "-v" in sys.argv:
            print(
                f"\n    hit_test {small[0] * 1e6:.1f} us at 1,000 blocks, {large[0] * 1e6:.1f} us at 10,000; "
                f"range_text {small[1] * 1e6:.1f} us and {large[1] * 1e6:.1f} us",
                end=" ",
            )
        # Ten times the blocks; a conversion linear in the document would be ten times slower.
        self.assertLess(large[0], 3.0 * small[0] + 20e-6)
        self.assertLess(large[1], 3.0 * small[1] + 20e-6)

    def test_the_view_selects_across_blocks_and_copies(self):
        document = ts.Document.from_blocks(long_blocks(30))
        self.addCleanup(document.close)
        state = {"selection": ((0, 0), (0, 0)), "rect": None, "ok": None}

        def body():
            state["ok"], state["selection"] = document.view("view", (0.0, 0.0), selection=state["selection"])
            state["rect"] = host.get_item_rect()

        host.set_clipboard_text("")
        frame(body)
        frame(body)
        self.assertTrue(state["ok"])
        x0, y0, _, _ = state["rect"]
        start, stop = (x0 + 60.0, y0 + 8.0), (x0 + 200.0, y0 + 70.0)
        try:
            for position, down in [(start, False)] * 2 + [(start, True)] * 2 + [(stop, True)] * 3 + [(stop, False)] * 2:
                host.script_mouse(*position, left_down=down)
                frame(body)
            host.script_copy_shortcut(True)
            frame(body)
            host.script_copy_shortcut(False)
            frame(body)
        finally:
            host.unscript_mouse()
        selection = state["selection"]
        self.assertIsInstance(selection, ts.DocumentRange)
        self.assertGreater(selection.start.offset, 0)
        self.assertGreater(selection.end.block, selection.start.block)
        blocks = long_blocks(30)
        expected = "\n\n".join(
            blocks[i].text[
                (selection.start.offset if i == selection.start.block else 0) : (
                    selection.end.offset if i == selection.end.block else None
                )
            ]
            for i in range(selection.start.block, selection.end.block + 1)
        )
        self.assertEqual(host.get_clipboard_text(), expected)
        self.assertEqual(document.range_text(selection), expected)
        # Kept inside the library when none is given.
        kept = []
        frame(lambda: kept.append(document.view("view-kept")))
        self.assertEqual(kept[0], (True, None))

    def test_options_and_extent(self):
        document = self.document()
        document.layout(400.0)
        document.set_options(ts.DocumentOptions(CodeOverflow=ts.CodeOverflow.Grow, RuleColor=RED))
        document.set_options(None)
        with self.assertRaises(TypeError):
            document.set_options({"CodeOverflow": 1})
        document.set_block_extent(4, 60.0)
        geometry = document.block_geometry(4)
        self.assertAlmostEqual(geometry.full_max[1] - geometry.full_min[1], 60.0, places=2)
        with self.assertRaises(ts.Error):
            document.set_block_extent(1, 60.0)  # A paragraph has no extent.
        with self.assertRaises(TypeError):
            ts.Document.from_blocks([ts.Block(text="x")], styles=[{"Scale": 2.0}])
        with self.assertRaises(UnicodeEncodeError):
            ts.Document.from_blocks([ts.Block(text="lone \ud800 surrogate")])

    def test_a_document_does_not_outlive_its_context(self):
        held = ts.current_context()
        ts.set_current_context(None)
        other = ts.create_context()
        try:
            document = ts.Document.from_blocks([ts.Block(text="belongs to the other context")])
            ts.set_current_context(held)
            ts.destroy_context(other)
            self.assertTrue(document.closed)
            with self.assertRaises(ValueError):
                document.layout(100.0)
            with self.assertRaises(ValueError):
                document.set_blocks([])
            document.close()
        finally:
            ts.set_current_context(held)
        with ts.Document() as scoped:
            self.assertFalse(scoped.closed)
        self.assertTrue(scoped.closed)

    def test_the_demo_window_runs_for_a_frame(self):
        results = []

        def body():
            results.append(ts.show_demo_window(True))
            results.append(ts.show_demo_window())

        assert window.begin_frame()
        try:
            body()
        finally:
            window.end_frame()
        self.assertEqual(results, [True, None])


class ParagraphFailures(unittest.TestCase):
    def test_a_count_of_minus_one_raises_and_is_not_an_empty_answer(self):
        # What the C layer answers when it caught a C++ exception: the library
        # built here cannot throw, so the answer is put in its place.
        with ts.Paragraph(ACCENTED) as paragraph:
            paragraph.layout(260.0)
            real = ts._lib

            class Failing:
                def __getattr__(self, name):
                    if name in ("ImTypeset_GetParagraphRangeRects", "ImTypeset_AppendParagraphRangeText",
                                "ImTypeset_GetParagraphLineCount"):
                        return lambda *args: -1
                    return getattr(real, name)

            ts._lib = Failing()
            try:
                for call in (
                    lambda: paragraph.range_rects((0, 5)),
                    lambda: paragraph.range_text((0, 5)),
                    lambda: paragraph.line_count,
                ):
                    with self.assertRaises(ts.Error):
                        call()
            finally:
                ts._lib = real
            self.assertEqual(paragraph.range_text((0, 2)), ACCENTED[:2])


if __name__ == "__main__":
    unittest.main(verbosity=2 if "-v" in sys.argv else 1)
