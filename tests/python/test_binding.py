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

    def test_a_refused_reload_leaves_the_package_as_it_was_loaded(self):
        # importlib.reload() with the library gone: Python keeps the package,
        # so its submodules must stay the ones it was loaded with. A second
        # imtypeset._capi would hand the package's functions struct classes
        # they were not declared with.
        code = (
            "import importlib, os, sys\n"
            "import imtypeset\n"
            "capi = sys.modules['imtypeset._capi']\n"
            "version = imtypeset.version()\n"
            "good = os.environ['IMTYPESET_LIBRARY']\n"
            "os.environ['IMTYPESET_LIBRARY'] = '/nonexistent/libimtypeset.so'\n"
            "try:\n"
            "    importlib.reload(imtypeset)\n"
            "except ImportError:\n"
            "    pass\n"
            "else:\n"
            "    raise SystemExit('reloaded with a library that is not there')\n"
            "assert sys.modules.get('imtypeset') is imtypeset\n"
            "assert sys.modules.get('imtypeset._capi') is capi, 'the submodule was dropped'\n"
            "import imtypeset._capi as again\n"
            "assert again is capi and imtypeset._capi is capi, 'a second _capi'\n"
            "assert imtypeset.version() == version\n"
            "assert imtypeset.host_info()['library'] == os.path.abspath(good)\n"
            "os.environ['IMTYPESET_LIBRARY'] = good\n"
            "importlib.reload(imtypeset)\n"
            "assert sys.modules['imtypeset._capi'] is capi\n"
            "assert imtypeset.version() == version\n"
            "print('reload ok')\n"
        )
        result = child(code)()
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertIn("reload ok", result.stdout)

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

    def test_a_table_struct_that_moved_is_refused_at_import(self):
        patch = (
            "class Cell(ctypes.Structure):\n"
            "    _fields_ = capi.Cell._fields_ + [('Extra', ctypes.c_int)]\n"
            "capi.Cell = Cell\n"
            "capi.TABLE_STRUCTS = tuple((n, Cell if n == 'Cell' else t) for n, t in capi.TABLE_STRUCTS)"
        )
        result = child(self.PATCHED_IMPORT.format(patch=patch))()
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("ImportError", result.stderr)
        self.assertIn("refuses this package's table structs", result.stderr)

    def test_a_column_struct_that_moved_is_refused_at_import(self):
        patch = (
            "class ColumnGeometry(ctypes.Structure):\n"
            "    _fields_ = capi.ColumnGeometry._fields_ + [('Extra', ctypes.c_int)]\n"
            "capi.ColumnGeometry = ColumnGeometry\n"
            "capi.COLUMN_STRUCTS = tuple((n, ColumnGeometry if n == 'ColumnGeometry' else t) for n, t in capi.COLUMN_STRUCTS)"
        )
        result = child(self.PATCHED_IMPORT.format(patch=patch))()
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("ImportError", result.stderr)
        self.assertIn("refuses this package's column structs", result.stderr)

    def test_a_package_from_before_columns_is_refused_at_import(self):
        # The document's stats as they were before columns added two counts.
        patch = (
            "class DocumentStats(ctypes.Structure):\n"
            "    _fields_ = capi.DocumentStats._fields_[:-2]\n"
            "capi.DocumentStats = DocumentStats\n"
            "capi.DOCUMENT_STRUCTS = tuple((n, DocumentStats if n == 'DocumentStats' else t) for n, t in capi.DOCUMENT_STRUCTS)"
        )
        result = child(self.PATCHED_IMPORT.format(patch=patch))()
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("ImportError", result.stderr)
        self.assertIn("refuses this package's document structs", result.stderr)

    def test_a_library_that_describes_itself_in_fewer_bytes_is_refused_at_import(self):
        # A package that reads more of ImTypeset_BuildInfo than the library has.
        patch = (
            "class BuildInfo(ctypes.Structure):\n"
            "    _fields_ = capi.BuildInfo._fields_ + [('SizeofLater', ctypes.c_int)]\n"
            "capi.BuildInfo = BuildInfo\n"
            "capi.PROTOTYPES['ImTypeset_GetBuildInfo'] = (ctypes.c_int, [ctypes.POINTER(BuildInfo), ctypes.c_int])"
        )
        result = child(self.PATCHED_IMPORT.format(patch=patch))()
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("ImportError", result.stderr)
        self.assertIn("the library is older than the package", result.stderr)

    def test_the_mirrored_document_structs_have_the_sizes_the_library_reports(self):
        raw = ts._capi.BuildInfo()
        whole = ts.library().ImTypeset_GetBuildInfo(raw, ts._capi.C.sizeof(raw))
        self.assertEqual(whole, ts._capi.C.sizeof(raw))
        structs = ts._capi.DOCUMENT_STRUCTS + ts._capi.TABLE_STRUCTS + ts._capi.COLUMN_STRUCTS
        self.assertEqual(len(structs), 20)
        for name, struct_type in structs:
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

    def test_a_container_that_is_not_there_is_cut_and_said_without_raising(self):
        document = ts.Document()
        self.addCleanup(document.close)
        # Done, differently: the blocks are set, so nothing is raised.
        self.assertFalse(
            document.set_blocks([ts.Block(text="before"), ts.Block(text="between", container=3), ts.Block(text="après")])
        )
        self.assertEqual(ts.last_error().code, ts.ErrorCode.DescriptionCut)
        self.assertEqual(document.block_count, 3)
        document.layout(300.0)
        self.assertEqual(document.range_text(((0, 0), (2, 5)), separator="|"), "before|between|après")

    def test_a_table_with_no_cells_is_set_whole_and_gives_no_text(self):
        document = ts.Document()
        self.addCleanup(document.close)
        self.assertTrue(
            document.set_blocks([ts.Block(text="before"), ts.Block(ts.BlockKind.Table), ts.Block(text="après")])
        )
        self.assertEqual(document.edit_error.code, ts.ErrorCode.None_)
        self.assertEqual(document.block_count, 3)
        document.layout(300.0)
        self.assertTrue(document.layout_window(0.0, 1e6))
        self.assertEqual(document.table_geometry(1).row_count, 0)
        self.assertEqual(document.range_text(((0, 0), (2, 5)), separator="|"), "before|après")

    def test_from_blocks_keeps_what_the_library_cut(self):
        document = ts.Document.from_blocks([ts.Block(text="before"), ts.Block(text="cut", container=3)])
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


class Requests(unittest.TestCase):
    """What a host building a grid of code asked for (tests/test_api_requests.cpp)."""

    def test_font_metrics_are_the_font_files_at_a_size(self):
        # Roboto: 2048 units to the em, an hhea ascender of 1900 and a
        # descender of -500. Inconsolata: 1000, 859 and -190.
        prose = ts.font_metrics(ts.Face.Regular, 16.0)
        self.assertEqual(prose.em_size, 16.0)
        self.assertEqual(prose.ascent, 16.0 * 1900 / 2048)
        self.assertEqual(prose.descent, 16.0 * 500 / 2048)
        self.assertEqual(prose.font_size, 18.75)
        self.assertEqual(prose.line_gap, 0.0)
        self.assertTrue(0.0 < prose.x_height < prose.ascent)
        self.assertEqual(prose.space_advance, ts.glyph_advance(ts.Face.Regular, 16.0, " "))
        code = ts.font_metrics(ts.Face.Code, 1000.0)
        self.assertEqual((code.ascent, code.descent, code.font_size), (859.0, 190.0, 1049.0))
        # The em at which a face is 16 px tall as ImGui means it, from the
        # metrics at any size: what advtext's em_size_for_pixel_height answers.
        probe = ts.font_metrics(ts.Face.Regular, 1024.0)
        em = 16.0 * probe.em_size / probe.font_size
        self.assertAlmostEqual(em, 16.0 * 2048 / 2400, places=5)
        self.assertAlmostEqual(ts.font_metrics(ts.Face.Regular, em).font_size, 16.0, places=5)
        # A monospace face: every character one advance; a kerned face not.
        cell = ts.glyph_advance(ts.Face.Code, 16.0, "M")
        self.assertEqual(cell, 8.0)
        self.assertEqual(ts.glyph_advance(ts.Face.Code, 16.0, "i"), cell)
        self.assertEqual(ts.glyph_advance(ts.Face.Code, 16.0, ord("i")), cell)
        self.assertGreater(
            ts.glyph_advance(ts.Face.Regular, 16.0, "M"), ts.glyph_advance(ts.Face.Regular, 16.0, "i")
        )
        # CodeBold answers for Bold's font until it has its own.
        self.assertEqual(ts.font_metrics(ts.Face.CodeBold, 16.0), ts.font_metrics(ts.Face.Bold, 16.0))
        # No ImGui context is asked for: the answer is the same between frames.
        self.assertEqual(ts.font_metrics(ts.Face.Regular, 16.0), prose)
        for bad in (0.0, -1.0, math.nan, math.inf, 2.0e6):
            with self.assertRaises(ts.Error) as raised:
                ts.font_metrics(ts.Face.Regular, bad)
            self.assertEqual(raised.exception.code, ts.ErrorCode.InvalidArgument)
            with self.assertRaises(ts.Error):
                ts.glyph_advance(ts.Face.Regular, bad, "a")
        with self.assertRaises(ts.Error) as raised:
            ts.font_metrics(99, 16.0)
        self.assertEqual(raised.exception.code, ts.ErrorCode.InvalidArgument)

    def test_face_has_glyph_answers_for_the_role_alone(self):
        self.assertTrue(ts.face_has_glyph(ts.Face.Code, "A"))
        self.assertTrue(ts.face_has_glyph(ts.Face.Code, ord("A")))
        # Set, in code, from the regular and the math face: the code face lacks them.
        self.assertFalse(ts.face_has_glyph(ts.Face.Code, "\u0416"))
        self.assertTrue(ts.face_has_glyph(ts.Face.Regular, "\u0416"))
        self.assertFalse(ts.face_has_glyph(ts.Face.Code, "\u2320"))
        self.assertTrue(ts.face_has_glyph(ts.Face.Math, "\u2320"))
        self.assertFalse(ts.face_has_glyph(ts.Face.Code, 0x110000))
        self.assertFalse(ts.face_has_glyph(ts.Face.Code, -1))
        self.assertFalse(ts.face_has_glyph(99, "A"))

    def test_a_paragraph_hit_answers_the_tag_under_the_point(self):
        text = "plain linked words and more"
        with ts.Paragraph(text, [ts.Run(6, 18, tag=41)]) as paragraph:
            paragraph.layout(1000.0, ts.Style(Align=ts.Align.Left))
            line = paragraph.lines()[0]
            y = (line.min[1] + line.max[1]) / 2.0
            for index in range(len(text)):
                left = paragraph.caret_rect(index)[0]
                right = paragraph.caret_rect(index + 1)[0]
                expected = 41 if 6 <= index < 18 else 0
                for part in (0.25, 0.75):
                    hit = paragraph.hit_test((left + (right - left) * part, y))
                    self.assertEqual(hit.tag, expected, (index, part))
            beside = paragraph.hit_test((line.max[0] + 40.0, y))
            self.assertEqual((beside.inside, beside.tag), (False, 0))

    def test_fixed_line_height_keeps_a_code_block_on_its_pitch(self):
        source = "plain\na \u2320 b \u27e8 c \u0416\nbelow\nlast"
        heights = {}
        for flags in (0, ts.BlockFlags.FixedLineHeight):
            document = ts.Document.from_blocks(
                [ts.Block(ts.BlockKind.Code, source, flags=flags)],
                [ts.BlockStyle(LineHeight=20.0 / 16.0)],
            )
            self.addCleanup(document.close)
            document.layout(600.0, ts.Style(EmSize=16.0))
            geometry = document.block_geometry(0)
            self.assertEqual(geometry.line_count, 4)
            heights[flags] = geometry.full_max[1] - geometry.full_min[1]
            if flags:
                for line in range(4):
                    box = document.block_line(0, line)
                    self.assertAlmostEqual(box.min[1], 20.0 * line, places=3)
                    self.assertAlmostEqual(box.max[1], 20.0 * (line + 1), places=3)
        self.assertAlmostEqual(heights[ts.BlockFlags.FixedLineHeight], 80.0, places=3)
        self.assertGreater(heights[0], 80.5)


class Tables(unittest.TestCase):
    """A table through the package: one block given by rows of cells."""

    ROWS = [["Name", "Unit", "Note"], ["mass", "kg", "the SI unit"]]
    TEXT = "Name\tUnit\tNote\nmass\tkg\tthe SI unit"
    # A two-byte character before, inside and after cells.
    ACCENTED_ROWS = [["né", "Größe", "x"], ["été", "naïve çà", "plain"]]

    def table(self, rows, width=400.0, **fields):
        fields.setdefault("flags", ts.BlockFlags.TableHeader)
        document = ts.Document.from_blocks([ts.Block(ts.BlockKind.Table, rows=rows, **fields)])
        self.addCleanup(document.close)
        self.assertEqual(document.edit_error.code, ts.ErrorCode.None_)
        document.layout(width)
        return document

    def test_a_table_from_rows_lays_out_and_copies_as_its_joined_text(self):
        document = ts.Document()
        self.addCleanup(document.close)
        with Calls() as calls:
            whole = document.set_blocks(
                [ts.Block(ts.BlockKind.Table, rows=self.ROWS, flags=ts.BlockFlags.TableHeader)]
            )
        self.assertTrue(whole)
        self.assertEqual(calls.names, ["ImTypeset_SetDocumentBlocks"])
        document.layout(400.0)
        geometry = document.table_geometry(0)
        self.assertEqual(
            (geometry.row_count, geometry.column_count, geometry.cell_count), (2, 3, 6)
        )
        self.assertTrue(geometry.laid_out)
        self.assertFalse(geometry.stated)
        self.assertGreater(geometry.max[0], geometry.min[0])
        self.assertGreater(geometry.max[1], geometry.min[1])
        self.assertEqual(document.range_text(((0, 0), (0, len(self.TEXT)))), self.TEXT)
        row = document.table_row(0, 1)
        self.assertGreater(row.max, row.min)
        self.assertEqual((row.cell_start, row.cell_count, row.wrapped), (3, 3, False))
        stats = document.stats()
        self.assertGreaterEqual(stats.TableCellsMeasured, 6)
        self.assertGreaterEqual(stats.TableRowsSet, 2)
        # What is not a table, or not there, is all zero.
        self.assertEqual(document.table_geometry(3).row_count, 0)
        self.assertEqual(document.table_column(0, 3), ts.TableColumnGeometry(0.0, 0.0, 0.0, 0.0))
        self.assertEqual(document.table_row(0, 2), ts.TableRowGeometry(0.0, 0.0, 0, 0, False))

    def test_the_text_of_a_table_may_be_given_too_if_it_is_the_joined_text(self):
        document = self.table(self.ROWS, text=self.TEXT)
        self.assertEqual(document.table_geometry(0).cell_count, 6)
        self.assertEqual(document.range_text(((0, 0), (0, len(self.TEXT)))), self.TEXT)

    def test_text_that_is_not_the_joined_text_raises(self):
        with self.assertRaises(ValueError):
            ts.Document.from_blocks([ts.Block(kind=ts.BlockKind.Table, text="x", rows=[["a"]])])
        with self.assertRaises(ValueError):
            ts.Document.from_blocks([ts.Block(text="a", rows=[["a"]])])
        with self.assertRaises(ValueError):
            ts.Document.from_blocks([ts.Block(text="a", columns=[ts.TableColumn()])])
        with self.assertRaises(TypeError):
            ts.Document.from_blocks([ts.Block(ts.BlockKind.Table, rows=["abc"])])

    def test_cells_and_positions_round_trip_in_characters(self):
        rows = self.ACCENTED_ROWS
        text = "\n".join("\t".join(row) for row in rows)
        document = self.table(rows)
        self.assertEqual(document.range_text(((0, 0), (0, len(text)))), text)
        start = 0
        for r, row in enumerate(rows):
            for c, cell in enumerate(row):
                for offset in range(len(cell) + 1):
                    with self.subTest(row=r, column=c, offset=offset):
                        with Calls() as calls:
                            position = document.table_position(0, r, c, offset)
                        self.assertEqual(calls.names, ["ImTypeset_GetDocumentTablePosition"])
                        self.assertEqual(position, ts.Position(0, start + offset))
                        with Calls() as calls:
                            where = document.table_cell(position)
                        self.assertEqual(calls.names, ["ImTypeset_GetDocumentTableCell"])
                        self.assertEqual(where, ts.CellPosition(r, c, r * len(row) + c, offset))
                # Past the cell: its end.
                self.assertEqual(
                    document.table_position(0, r, c, 999), ts.Position(0, start + len(cell))
                )
                start += len(cell) + 1
        self.assertEqual(document.table_cell((4, 0)), ts.CellPosition(-1, 0, 0, 0))

    def test_a_run_of_a_cell_indexes_the_cell(self):
        rows = [["né", ts.Cell("çà link ici", runs=[ts.Run(3, 7, tag=9)])], ["x", "y"]]
        document = self.table(rows)
        for offset, tag in ((1, 0), (4, 9), (6, 9), (9, 0)):
            caret = document.caret_rect(document.table_position(0, 0, 1, offset))
            hit = document.hit_test((caret[0] + 1.0, (caret[1] + caret[3]) / 2.0))
            self.assertEqual((hit.tag, hit.row, hit.column), (tag, 0, 1), offset)

    def test_a_hit_in_a_cell_answers_its_row_and_column(self):
        document = self.table(self.ACCENTED_ROWS)
        for r in range(2):
            for c in range(3):
                column = document.table_column(0, c)
                row = document.table_row(0, r)
                self.assertGreater(column.max, column.min)
                self.assertGreater(row.max, row.min)
                hit = document.hit_test(((column.min + column.max) / 2.0, (row.min + row.max) / 2.0))
                self.assertEqual((hit.row, hit.column), (r, c))
                self.assertEqual(hit.position.block, 0)
                self.assertTrue(hit.in_block)
                where = document.table_cell(hit.position)
                self.assertEqual((where.row, where.column), (r, c))
        prose = ts.Document.from_blocks([ts.Block(text="no table here")])
        self.addCleanup(prose.close)
        prose.layout(400.0)
        hit = prose.hit_test((5.0, 5.0))
        self.assertEqual((hit.row, hit.column), (-1, -1))

    def test_append_table_rows_adds_rows_in_one_call(self):
        document = self.table(self.ROWS)
        self.assertEqual(document.table_geometry(0).row_count, 2)
        with Calls() as calls:
            whole = document.append_table_rows(
                [["length", "m", ts.Cell("the mètre", runs=[ts.Run(4, 9, tag=3)])]]
            )
        self.assertTrue(whole)
        self.assertEqual(calls.names, ["ImTypeset_AppendDocumentTableCells"])
        geometry = document.table_geometry(0)
        self.assertEqual((geometry.row_count, geometry.cell_count), (3, 9))
        text = self.TEXT + "\nlength\tm\tthe mètre"
        self.assertEqual(document.range_text(((0, 0), (0, len(text)))), text)
        position = document.table_position(0, 2, 2, 6)
        self.assertEqual(position, ts.Position(0, text.index("mètre") + 2))
        self.assertEqual(document.table_cell(position), ts.CellPosition(2, 2, 8, 6))
        caret = document.caret_rect(position)
        self.assertEqual(document.hit_test((caret[0] + 1.0, (caret[1] + caret[3]) / 2.0)).tag, 3)
        # Two rows at once, and then a table that had none.
        document.append_table_rows([["a", "b", "c"], ["d", "e", "f"]])
        self.assertEqual(document.table_geometry(0).row_count, 5)
        text += "\na\tb\tc\nd\te\tf"
        self.assertEqual(document.range_text(((0, 0), (0, len(text)))), text)
        empty = ts.Document.from_blocks([ts.Block(ts.BlockKind.Table)])
        self.addCleanup(empty.close)
        empty.layout(400.0)
        self.assertEqual(empty.table_geometry(0).row_count, 0)
        empty.append_table_rows([["é", "b"]])
        self.assertEqual(empty.table_geometry(0).row_count, 1)
        self.assertEqual(empty.range_text(((0, 0), (0, 3))), "é\tb")
        self.assertEqual(empty.table_position(0, 0, 1, 1), ts.Position(0, 3))
        prose = ts.Document.from_blocks([ts.Block(text="no table here")])
        self.addCleanup(prose.close)
        with self.assertRaises(ValueError):
            prose.append_table_rows([["a"]])

    def test_a_row_that_arrives_in_pieces_is_appended_to_and_never_replaced(self):
        # The row being written is described with the cells it has: the cell
        # being written is the table's last, append_text grows it and
        # append_table_cells adds the next one. One library call a piece.
        document = self.table([["Name", "Unit", "Note"]], flags=ts.BlockFlags.TableHeld)
        steps = [
            ("rows", [["ma"]]),
            ("text", "ss"),
            ("cells", ["k"]),
            ("text", "g"),
            ("cells", [ts.Cell("the SI", runs=[ts.Run(4, 6, tag=5)])]),
            ("text", " unit"),
            ("cells", []),
            ("rows", [["len"]]),
            ("text", "gth"),
            ("cells", ["m", "the mètre"]),
        ]
        before = document.stats()
        for kind, given in steps:
            with Calls() as calls:
                if kind == "rows":
                    self.assertTrue(document.append_table_rows(given))
                elif kind == "text":
                    self.assertTrue(document.append_text(given))
                else:
                    self.assertTrue(document.append_table_cells(given))
            edits = [name for name in calls.names if name.startswith("ImTypeset_Append")]
            self.assertEqual(len(edits), 0 if given == [] else 1)
            document.layout(600.0)
            document.layout_window(0.0, 1.0e6)
        text = self.TEXT + "\nlength\tm\tthe mètre"
        self.assertEqual(document.range_text(((0, 0), (0, len(text)))), text)
        geometry = document.table_geometry(0)
        self.assertEqual((geometry.row_count, geometry.column_count, geometry.cell_count), (3, 3, 9))
        # Positions in the appended cells are the block's, accents and all.
        position = document.table_position(0, 2, 2, 6)
        self.assertEqual(position, ts.Position(0, text.index("mètre") + 2))
        self.assertEqual(document.table_cell(position), ts.CellPosition(2, 2, 8, 6))
        linked = document.caret_rect(document.table_position(0, 1, 2, 5))
        self.assertEqual(document.hit_test((linked[0] + 1.0, (linked[1] + linked[3]) / 2.0)).tag, 5)
        # It is the table described whole.
        whole = self.table(self.ROWS + [["length", "m", "the mètre"]], flags=ts.BlockFlags.TableHeld)
        self.assertEqual(whole.table_geometry(0), geometry)
        # Each piece measured the cell it touched and nothing else: ten, after
        # the three of the header at the first layout.
        after = document.stats()
        self.assertEqual(after.TableCellsMeasured - before.TableCellsMeasured, 3 + 10)
        # A table with no row yet gets its first; a document whose last block
        # is no table refuses.
        empty = ts.Document.from_blocks([ts.Block(ts.BlockKind.Table)])
        self.addCleanup(empty.close)
        empty.layout(400.0)
        self.assertTrue(empty.append_table_cells(["é", "b"]))
        self.assertTrue(empty.append_table_cells(["c"]))
        self.assertEqual(empty.table_geometry(0).row_count, 1)
        self.assertEqual(empty.range_text(((0, 0), (0, 5))), "é\tb\tc")
        self.assertEqual(empty.table_position(0, 0, 2, 1), ts.Position(0, 5))
        prose = ts.Document.from_blocks([ts.Block(text="no table here")])
        self.addCleanup(prose.close)
        with self.assertRaises(ValueError):
            prose.append_table_cells(["a"])
        with self.assertRaises(TypeError):
            document.append_table_cells("ab")

    def test_set_table_columns_states_the_widths(self):
        document = self.table(self.ROWS)
        widths = (90.0, 60.0, 150.0)
        columns = [
            ts.TableColumn(width=widths[0]),
            ts.TableColumn(ts.Align.Center, widths[1]),
            ts.TableColumn(ts.Align.Right, widths[2]),
        ]
        with Calls() as calls:
            self.assertTrue(document.set_table_columns(0, columns))
        self.assertEqual(calls.names, ["ImTypeset_SetDocumentTableColumns"])
        self.assertTrue(document.table_geometry(0).stated)
        for index, width in enumerate(widths):
            column = document.table_column(0, index)
            self.assertAlmostEqual(column.max - column.min, width, delta=0.01)
        with self.assertRaises(ts.Error):
            document.set_table_columns(1, columns)
        # The same, described with the block.
        described = self.table(self.ROWS, columns=columns)
        self.assertTrue(described.table_geometry(0).stated)
        for index, width in enumerate(widths):
            column = described.table_column(0, index)
            self.assertAlmostEqual(column.max - column.min, width, delta=0.01)

    RAGGED = [["a", "b", "c"], ["d"], ["", "e", ""], []]

    def test_a_ragged_row_and_an_empty_cell_do_not_raise(self):
        document = self.table(self.RAGGED)
        geometry = document.table_geometry(0)
        self.assertEqual((geometry.row_count, geometry.column_count), (4, 3))
        # A row with no cell at all is a row of one empty cell.
        self.assertEqual(geometry.cell_count, 8)
        self.assertTrue(document.layout_window(0.0, 1e6))
        # The text is "a\tb\tc\nd\n\te\t\n": "e" is its character 9.
        self.assertEqual(document.table_position(0, 2, 1, 1), ts.Position(0, 10))
        self.assertEqual(document.table_cell((0, 10)), ts.CellPosition(2, 1, 5, 1))
        # An empty cell is a place: the one at the end of row 2, and row 3's.
        self.assertEqual(document.table_cell((0, 11)), ts.CellPosition(2, 2, 6, 0))
        self.assertEqual(document.table_cell((0, 12)), ts.CellPosition(3, 0, 7, 0))
        self.assertEqual(document.table_row(0, 1).cell_count, 1)
        for row in range(4):
            box = document.table_row(0, row)
            self.assertGreater(box.max, box.min)
        frame(lambda: document.draw(None, (10.0, 10.0)))
        document.append_table_rows([[], ["", "f"]])
        self.assertEqual(document.table_geometry(0).row_count, 6)

    def test_a_copy_keeps_the_tab_of_an_empty_cell_and_adds_none_to_a_short_row(self):
        # imtypeset.h, "WHAT A COPY GIVES": a tab between the cells of a row,
        # a newline between rows, and an empty cell keeps its tab.
        for rows in (
            [["a", "b"], ["d"], ["e", "f"]],
            [["", "e", "x"]],
            [["x", "e", ""]],
            [["a"], [""]],
            self.RAGGED,
        ):
            with self.subTest(rows=rows):
                text = "\n".join("\t".join(row or [""]) for row in rows)
                document = self.table(rows)
                self.assertEqual(document.range_text(((0, 0), (0, len(text)))), text)


class Columns(unittest.TestCase):
    """A document shown in columns. Every block's text is not ASCII, so every
    position below that is past a block's start is one a byte offset would get wrong."""

    WIDTH = 240.0
    GUTTER = 16.0
    COUNT = 35

    def setUp(self):
        self.texts = [
            f"Paragraphe numéro {i:02d}. {ACCENTED} {ACCENTED}" for i in range(self.COUNT)
        ]
        self.document = ts.Document.from_blocks([ts.Block(text=text) for text in self.texts])
        self.addCleanup(self.document.close)
        # The size is stated: the default is ImGui's current font's, which is
        # another before the first frame than after it.
        self.assertTrue(self.document.layout(self.WIDTH, ts.Style(EmSize=16.0)))
        self.end = ts.Position(self.COUNT - 1, len(self.texts[-1]))
        first = self.document.block_line(0, 0)
        self.pitch = first.max[1] - first.min[1]
        self.assertGreater(self.document.block_geometry(0).line_count, 5)
        # A column in order: a whole number of lines and half of one, so that
        # a column's foot is not always a paragraph's.
        self.height = 13.5 * self.pitch

    def uncut_height(self):
        """The uncut column's height, everything laid out to say it."""
        self.document.set_columns(None)
        self.assertTrue(self.document.layout_window(0.0, 1e9))
        return self.document.block_geometry(self.COUNT - 1).full_max[1]

    def in_order(self):
        self.document.set_columns(ts.Columns(height=self.height, gutter=self.GUTTER))

    def balanced(self, count=3):
        self.document.set_columns(
            ts.Columns(count=count, gutter=self.GUTTER, flags=ts.ColumnFlags.Balance)
        )

    def columns(self):
        """Every column there is, found by asking for one after another."""
        found = []
        while True:
            column = self.document.column(len(found))
            if not column.exists:
                return found
            found.append(column)
            self.assertLess(len(found), 1000)

    def lines(self):
        """(block, its geometry, the line) for every line, of the UNCUT column."""
        for block in range(self.COUNT):
            geometry = self.document.block_geometry(block)
            for index in range(geometry.line_count):
                yield block, geometry, self.document.block_line(block, index)

    def slice(self, start, end):
        """What a copy of [start, end) gives, from the strs."""
        pieces = []
        for block in range(start[0], end[0] + 1):
            first = start[1] if block == start[0] else 0
            last = end[1] if block == end[0] else len(self.texts[block])
            if last > first:
                pieces.append(self.texts[block][first:last])
        return "\n\n".join(pieces)

    def check_tiling(self, columns):
        self.assertEqual(columns[0].start, ts.Position(0, 0))
        for left, right in zip(columns, columns[1:]):
            self.assertEqual(left.end, right.start)
            self.assertLess(left.start, left.end)
        self.assertEqual(columns[-1].end, self.end)
        for k, column in enumerate(columns):
            self.assertIsInstance(column.start, ts.Position)
            self.assertAlmostEqual(column.min[0], k * (self.WIDTH + self.GUTTER), places=3)
            self.assertAlmostEqual(column.max[0] - column.min[0], self.WIDTH, places=3)
            self.assertEqual(column.min[1], 0.0)

    def test_uncut_is_one_column_that_holds_everything(self):
        document = self.document
        self.assertEqual(document.column_count(), 1)
        only = document.column(0)
        self.assertTrue(only.exists)
        self.assertEqual((only.start, only.end), (ts.Position(0, 0), self.end))
        self.assertEqual(document.column_pos((7.0, 1234.5)), (7.0, 1234.5))
        self.assertEqual(
            document.column(1),
            ts.ColumnGeometry(
                (0.0, 0.0), (0.0, 0.0), (0.0, 0.0), (0.0, 0.0), (0, 0), (0, 0), 0.0, False, False
            ),
        )
        self.assertFalse(document.column(-1).exists)
        stats = document.stats()
        self.assertEqual((stats.ColumnsCut, stats.BalanceHeightsTried), (0, 0))

    def test_columns_filled_in_order_tile_the_document(self):
        document = self.document
        self.in_order()
        self.assertGreater(document.column_count(), 1)
        columns = self.columns()
        self.assertGreater(len(columns), 1)
        self.assertEqual(document.column_count(), len(columns))
        self.check_tiling(columns)
        for column in columns:
            self.assertFalse(column.overfull)
            self.assertAlmostEqual(column.max[1], self.height, places=3)
            self.assertLessEqual(column.content_max[1], self.height + self.pitch)
            self.assertGreater(column.content_max[1], column.content_min[1])
            self.assertGreaterEqual(column.content_min[1], 0.0)
        tops = [column.uncut_top for column in columns]
        self.assertEqual(tops[0], 0.0)
        self.assertEqual(tops, sorted(set(tops)))
        width, height = document.size()
        self.assertAlmostEqual(
            width, len(columns) * self.WIDTH + (len(columns) - 1) * self.GUTTER, delta=1.0
        )
        self.assertAlmostEqual(height, self.height, delta=1.0)
        stats = document.stats()
        self.assertGreaterEqual(stats.ColumnsCut, len(columns))
        self.assertEqual(stats.BalanceHeightsTried, 0)
        self.assertEqual(stats.BlocksLaidOut, self.COUNT)
        # No column holds more than its height, so there are at least as many
        # as the uncut column's height asks for; and none is nearly empty.
        uncut_height = self.uncut_height()
        self.assertGreaterEqual(len(columns) * self.height, uncut_height - 0.5)
        self.assertLess(len(columns) * self.height, 2.0 * uncut_height)
        # The same cuts, found again from the front.
        self.in_order()
        self.assertEqual(self.columns(), columns)
        self.assertEqual(document.stats().BlocksLaidOut, self.COUNT)

    def test_balanced_columns_are_the_count_asked_for_and_one_height(self):
        document = self.document
        uncut_height = self.uncut_height()
        self.balanced(3)
        self.assertEqual(document.column_count(), 3)
        columns = self.columns()
        self.assertEqual(len(columns), 3)
        self.check_tiling(columns)
        heights = {column.max[1] for column in columns}
        self.assertEqual(len(heights), 1)
        height = heights.pop()
        # Three columns hold what one did, so they are a third of it or
        # more; and the least height that does is not far over that.
        self.assertGreaterEqual(height * 3, uncut_height - 0.5)
        self.assertLess(height, uncut_height / 3.0 + 4.0 * self.pitch)
        for column in columns:
            self.assertFalse(column.overfull)
            self.assertLessEqual(column.content_max[1], height + self.pitch)
        self.assertEqual(document.size()[0], 3 * self.WIDTH + 2 * self.GUTTER)
        stats = document.stats()
        self.assertGreater(stats.BalanceHeightsTried, 0)
        self.assertGreaterEqual(stats.ColumnsCut, 3)
        self.assertEqual(stats.BlocksLaidOut, self.COUNT)
        # Asking again searches for nothing.
        self.columns()
        self.assertEqual(document.stats().BalanceHeightsTried, stats.BalanceHeightsTried)

    def test_a_column_starts_at_a_line_start_counted_in_characters(self):
        starts = {
            (block, line.text_start): geometry.full_min[1] + line.min[1]
            for block, geometry, line in self.lines()
        }
        for fill in (self.in_order, self.balanced):
            with self.subTest(fill=fill.__name__):
                fill()
                columns = self.columns()
                inside = [c for c in columns if c.start.offset > 0]
                # Some cut falls between two lines of a paragraph, where an
                # offset in bytes is past the one in characters.
                self.assertTrue(inside)
                for column in columns:
                    self.assertIn(tuple(column.start), starts)
                    self.assertAlmostEqual(column.uncut_top, starts[tuple(column.start)], delta=0.01)
                for column in inside:
                    block, offset = column.start
                    text = self.texts[block]
                    self.assertEqual(text[offset - 1], " ")
                    self.assertGreater(len(text[:offset].encode("utf-8")), offset)
                    self.assertEqual(
                        self.document.range_text(((block, offset), (block, len(text)))),
                        text[offset:],
                    )

    def test_a_copy_across_a_cut_is_the_copy_of_the_uncut_document(self):
        document = self.document
        self.in_order()
        columns = self.columns()
        self.assertGreaterEqual(len(columns), 3)
        ranges = [
            (columns[0].start, columns[2].start),
            ((columns[0].end.block, 0), columns[1].end),
            (
                (columns[1].start.block, max(columns[1].start.offset - 9, 0)),
                (columns[1].start.block, columns[1].start.offset + 9),
            ),
            ((0, 5), self.end),
        ]
        in_order = [document.range_text(given) for given in ranges]
        self.balanced()
        balanced = [document.range_text(given) for given in ranges]
        document.set_columns(None)
        uncut = [document.range_text(given) for given in ranges]
        for given, a, b, c in zip(ranges, in_order, balanced, uncut):
            wanted = self.slice(*given)
            self.assertTrue(wanted)
            # The uncut document's copy is the slice of the strs, less the
            # space a line was broken at when the range ends at that break.
            self.assertIn(c, (wanted, wanted.rstrip(" ")))
            self.assertEqual(a, c)
            self.assertEqual(b, c)
        self.assertEqual(document.range_text(((0, 0), self.end), "|"), "|".join(self.texts))

    def test_a_hit_in_a_column_answers_a_position_the_column_holds(self):
        document = self.document
        for fill in (self.in_order, self.balanced):
            with self.subTest(fill=fill.__name__):
                fill()
                columns = self.columns()
                for k, column in enumerate(columns):
                    x = column.min[0] + 30.0
                    for part in (0.1, 0.5, 0.9):
                        y = column.content_min[1] + part * (
                            column.content_max[1] - column.content_min[1]
                        )
                        hit = document.hit_test((x, y))
                        self.assertLessEqual(column.start, hit.position, (k, part))
                        self.assertLess(hit.position, column.end, (k, part))
                        caret = document.caret_rect(hit.position)
                        self.assertGreaterEqual(caret[0], column.min[0] - 0.5)
                        self.assertLessEqual(caret[2], column.max[0] + 0.5)
                # A point in a gutter belongs to the column on its left.
                second = columns[1]
                y = (second.content_min[1] + second.content_max[1]) / 2.0
                hit = document.hit_test((second.max[0] + self.GUTTER / 2.0, y))
                self.assertLessEqual(second.start, hit.position)
                self.assertLessEqual(hit.position, second.end)

    def test_a_drag_from_one_column_to_the_next_selects_in_reading_order(self):
        document = self.document
        self.in_order()
        first, second = self.columns()[:2]
        anchor = (first.min[0] + 30.0, (first.content_min[1] + first.content_max[1]) / 2.0)
        active = (second.min[0] + 30.0, second.content_min[1] + 5.0)
        selected = document.select_range(anchor, active)
        self.assertEqual(selected.start, document.hit_test(anchor).position)
        self.assertEqual(selected.end, document.hit_test(active).position)
        self.assertLess(selected.start, first.end)
        self.assertGreaterEqual(selected.end, second.start)
        self.assertEqual(document.range_text(selected), self.slice(*selected))
        # One rectangle a line, each inside the column that holds the line.
        rects = document.range_rects(selected)
        self.assertGreater(len(rects), 1)
        for rect in rects:
            column = first if rect[0] < first.max[0] else second
            self.assertGreaterEqual(rect[0], column.min[0] - 0.5)
            self.assertLessEqual(rect[2], column.max[0] + 0.5)
        self.assertTrue(any(rect[0] >= second.min[0] for rect in rects))
        self.assertTrue(any(rect[2] <= first.max[0] + 0.5 for rect in rects))

    def test_column_pos_takes_a_point_of_the_uncut_column_into_its_column(self):
        document = self.document
        lines = list(self.lines())
        for fill in (self.in_order, self.balanced):
            with self.subTest(fill=fill.__name__):
                fill()
                columns = self.columns()
                seen = set()
                for block, geometry, line in lines:
                    at = ts.Position(block, line.text_start)
                    k = max(i for i, column in enumerate(columns) if column.start <= at)
                    seen.add(k)
                    column = columns[k]
                    uncut = (
                        geometry.full_min[0] + line.min[0] + 1.0,
                        geometry.full_min[1] + (line.min[1] + line.max[1]) / 2.0,
                    )
                    x, y = document.column_pos(uncut)
                    self.assertAlmostEqual(x, column.min[0] + uncut[0], delta=0.01)
                    self.assertAlmostEqual(
                        y, uncut[1] - column.uncut_top + column.content_min[1], delta=0.01
                    )
                    self.assertTrue(column.min[0] <= x < column.max[0], (block, k))
                    self.assertTrue(column.content_min[1] <= y <= column.content_max[1], (block, k))
                    # And the point on the page is the line it was in the uncut column.
                    self.assertEqual(document.hit_test((x, y)).position, at)
                self.assertEqual(seen, set(range(len(columns))))

    def test_no_columns_shows_it_uncut_again(self):
        document = self.document
        point = (30.0, self.uncut_height() / 2.0)
        uncut = document.hit_test(point)
        size = document.size()
        for none in (None, ts.Columns(), (0, 0.0, 0.0, 0), ts.Columns(count=3)):
            self.balanced()
            self.assertEqual(document.column_count(), 3)
            self.assertNotEqual(document.size(), size)
            document.set_columns(none)
            self.assertEqual(document.column_count(), 1)
            self.assertEqual(document.column(0).end, self.end)
            self.assertFalse(document.column(1).exists)
            self.assertEqual(document.hit_test(point), uncut)
            self.assertEqual(document.size(), size)
            self.assertEqual(document.column_pos(point), point)
        # Nothing was laid out again for any of it.
        self.assertEqual(document.stats().BlocksLaidOut, self.COUNT)

    def test_columns_are_drawn_side_by_side(self):
        document = self.document
        build = ts.build_info()

        def reach():
            vertices, _ = document.draw_to_buffers((0.0, 0.0), (0.0, 0.0, 1e5, 1e5))
            size, at = build.sizeof_imdrawvert, build.offsetof_imdrawvert_pos
            points = [
                struct.unpack_from("<ff", vertices, i * size + at)
                for i in range(len(vertices) // size)
            ]
            return len(points), max(p[0] for p in points), max(p[1] for p in points)

        count, right, foot = frame(reach)
        self.assertLessEqual(right, self.WIDTH + 1.0)
        self.assertGreater(foot, self.uncut_height() - 2.0 * self.pitch)
        self.balanced()
        last = self.columns()[-1]
        count_in_columns, right, foot = frame(reach)
        self.assertEqual(count_in_columns, count)
        self.assertGreater(right, last.min[0])
        self.assertLessEqual(right, last.max[0] + 1.0)
        self.assertLessEqual(foot, last.max[1] + 8.0)

    def test_a_question_about_a_column_is_one_call_into_the_library(self):
        document = self.document
        with Calls() as calls:
            document.set_columns(ts.Columns(2, flags=ts.ColumnFlags.Balance))
            document.column_count()
            document.column(1)
            document.column_pos((1.0, 2.0))
            document.set_columns(None)
        self.assertEqual(
            calls.names,
            [
                "ImTypeset_SetDocumentColumns",
                "ImTypeset_GetDocumentColumnCount",
                "ImTypeset_GetDocumentColumn",
                "ImTypeset_GetDocumentColumnPos",
                "ImTypeset_SetDocumentColumns",
            ],
        )

    def test_a_closed_document_answers_no_question_about_columns(self):
        document = ts.Document.from_blocks([ts.Block(text="x")])
        document.close()
        for call in (
            lambda: document.set_columns(None),
            lambda: document.column_count(),
            lambda: document.column(0),
            lambda: document.column_pos((0.0, 0.0)),
        ):
            with self.assertRaises(ValueError):
                call()


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


# --- What leaves a call on input a host controls (docs/python-errors.md) ------------------

import ctypes  # noqa: E402
import enum  # noqa: E402
import math  # noqa: E402

from imtypeset import _capi, demo  # noqa: E402

T, V, O, U, E, OK = TypeError, ValueError, OverflowError, UnicodeEncodeError, ts.Error, None
NAN, INF, HUGE, LONE = math.nan, math.inf, 10**400, "\ud800"
PLAIN = "Plain words, enough of them to wrap onto several lines at this width."
ROBOTO = os.path.join(FONT_DIR, FONTS[ts.Face.Regular])
F = frame  # A row that draws does it inside a frame.


class Hostile:
    """What a row calls into, made new for the row and closed after it."""

    def __init__(self):
        self.made = []

    def close(self):
        for thing in self.made:
            thing.close()

    def paragraph(self, text):
        paragraph = ts.Paragraph(text, [ts.Run(0, 2, ts.Face.Bold, tag=3)])
        self.made.append(paragraph)
        paragraph.layout(260.0)
        return paragraph

    @property
    def par(self):  # Text that is not ASCII: positions go through the library.
        return self.paragraph(ACCENTED)

    @property
    def plain(self):  # ASCII: positions are converted in the package.
        return self.paragraph(PLAIN)

    @property
    def doc(self):  # Blocks 0 to 5 of every kind, and block 6 a table of two rows.
        table = ts.Block(
            ts.BlockKind.Table,
            rows=[["né", ts.Cell("Größe", [ts.Run(0, 2, ts.Face.Bold)])], ["c", "d"]],
            columns=[ts.TableColumn(ts.Align.Left, 60.0), ts.TableColumn(ts.Align.Right, 80.0)],
        )
        document = ts.Document.from_blocks(
            every_kind() + [table], STYLES(), [ts.Container(Kind=ts.ContainerKind.List, Indent=24.0)]
        )
        self.made.append(document)
        document.layout(400.0)
        return document

    @property
    def prose(self):  # The last block is a paragraph: what append_text appends to.
        document = ts.Document.from_blocks([ts.Block(text=PROSE)])
        self.made.append(document)
        document.layout(400.0)
        return document

    def a_context_and_back(self):
        held = ts.current_context()
        other = ts.create_context()
        try:
            ts.set_current_context(other)
            return ts.current_context() == other
        finally:
            ts.destroy_context(other)
            ts.set_current_context(held)


def blocks_with(**fields):
    return [ts.Block(text="né " + PLAIN)._replace(**fields)]


def run_with(**fields):
    return [ts.Run(0, 2)._replace(**fields)]


def mark_with(**fields):
    return [ts.Mark(((1, 2), (1, 9)), RED)._replace(**fields)]


SEL = ((1, 2), (2, 3))
BOX = (0.0, 0.0, 600.0, 400.0)

# (call, the input, what leaves the call, the call). `OK` is nothing at all:
# the call returns. An exception class is the class exactly, not one of its
# subclasses. ONE LINE HERE FOR A NEW PUBLIC NAME, and its name in
# docs/python-errors.md: test_every_public_name_is_in_the_table and
# test_the_document_names_every_call_of_the_table fail until both are there.
TABLE = [
    # --- the module's functions
    ("col32", "r=None", T, lambda x: ts.col32(None, 0, 0)),
    ("col32", "r=1.5", T, lambda x: ts.col32(1.5, 0, 0)),
    ("col32", "r=2**40: the low byte", OK, lambda x: ts.col32(2**40, 0, 0)),
    ("library", "no argument", OK, lambda x: ts.library()),
    ("build_info", "no argument", OK, lambda x: ts.build_info()),
    ("version", "no argument", OK, lambda x: ts.version()),
    ("host_info", "no argument", OK, lambda x: ts.host_info()),
    ("last_error", "no argument", OK, lambda x: ts.last_error()),
    ("current_context", "no argument", OK, lambda x: ts.current_context()),
    ("get_context_serial", "no argument", OK, lambda x: ts.get_context_serial()),
    ("stats", "no argument", OK, lambda x: ts.stats()),
    ("get_style", "no argument", OK, lambda x: ts.get_style()),
    ("create_context", "a second context, then destroyed", OK, lambda x: x.a_context_and_back()),
    ("destroy_context", "ctx='x'", T, lambda x: ts.destroy_context("x")),
    ("destroy_context", "ctx=1.5", T, lambda x: ts.destroy_context(1.5)),
    ("set_current_context", "ctx=b'x'", T, lambda x: ts.set_current_context(b"x")),
    ("set_current_context", "ctx=[]", T, lambda x: ts.set_current_context([])),
    ("set_style", "style=5", T, lambda x: ts.set_style(5)),
    ("set_style", "style=a BlockStyle", T, lambda x: ts.set_style(ts.BlockStyle())),
    ("set_text_cache_limits", "max_paragraphs=None", T, lambda x: ts.set_text_cache_limits(None)),
    ("set_text_cache_limits", "max_bytes='x'", V, lambda x: ts.set_text_cache_limits(0, "x")),
    ("set_text_cache_limits", "max_bytes=nan", V, lambda x: ts.set_text_cache_limits(0, NAN)),
    ("set_text_cache_limits", "max_bytes=inf", O, lambda x: ts.set_text_cache_limits(0, INF)),
    ("set_text_cache_limits", "2**40: clamped", OK, lambda x: ts.set_text_cache_limits(2**40, 2**40)),
    ("codepoint_to_byte_offset", "text=None", T, lambda x: ts.codepoint_to_byte_offset(None, 0)),
    ("codepoint_to_byte_offset", "text=bytes", T, lambda x: ts.codepoint_to_byte_offset(b"a", 0)),
    ("codepoint_to_byte_offset", "a lone surrogate", U, lambda x: ts.codepoint_to_byte_offset(LONE, 0)),
    ("codepoint_to_byte_offset", "offset=None", T, lambda x: ts.codepoint_to_byte_offset("aé", None)),
    ("codepoint_to_byte_offset", "offset=nan", V, lambda x: ts.codepoint_to_byte_offset("aé", NAN)),
    ("codepoint_to_byte_offset", "offset=inf", O, lambda x: ts.codepoint_to_byte_offset("aé", INF)),
    ("codepoint_to_byte_offset", "offset=2**40: clamped", OK, lambda x: ts.codepoint_to_byte_offset("aé", 2**40)),
    ("byte_to_codepoint_offset", "text=None", T, lambda x: ts.byte_to_codepoint_offset(None, 0)),
    ("byte_to_codepoint_offset", "a lone surrogate", U, lambda x: ts.byte_to_codepoint_offset(LONE, 0)),
    ("byte_to_codepoint_offset", "offset='x'", V, lambda x: ts.byte_to_codepoint_offset("aé", "x")),
    ("byte_to_codepoint_offset", "offset=inf", O, lambda x: ts.byte_to_codepoint_offset("aé", INF)),
    ("byte_to_codepoint_offset", "offset=-2**40: clamped", OK, lambda x: ts.byte_to_codepoint_offset("aé", -(2**40))),
    ("add_font", "face=None", T, lambda x: ts.add_font(None, ROBOTO)),
    ("add_font", "face='x'", V, lambda x: ts.add_font("x", ROBOTO)),
    ("add_font", "face=inf", O, lambda x: ts.add_font(INF, ROBOTO)),
    ("add_font", "face=2**32+1: no such role", E, lambda x: ts.add_font(2**32 + 1, ROBOTO)),
    ("add_font", "source=None", T, lambda x: ts.add_font(ts.Face.Italic, None)),
    ("add_font", "source=2**31 (an int)", T, lambda x: ts.add_font(ts.Face.Italic, 2**31)),
    ("add_font", "source=1.5", T, lambda x: ts.add_font(ts.Face.Italic, 1.5)),
    ("add_font", "source=a lone surrogate", U, lambda x: ts.add_font(ts.Face.Italic, LONE)),
    ("add_font", "source=''", E, lambda x: ts.add_font(ts.Face.Italic, "")),
    ("add_font", "face_index=None", T, lambda x: ts.add_font(ts.Face.Italic, ROBOTO, None)),
    ("add_font", "face_index=2**40: no such face in the file", E, lambda x: ts.add_font(ts.Face.Italic, ROBOTO, 2**40)),
    ("remove_font", "face=None", T, lambda x: ts.remove_font(None)),
    ("remove_font", "face='x'", V, lambda x: ts.remove_font("x")),
    ("remove_font", "face=inf", O, lambda x: ts.remove_font(INF)),
    ("remove_font", "face=2**32+1: no such role", OK, lambda x: ts.remove_font(2**32 + 1)),
    ("get_font", "face=None", T, lambda x: ts.get_font(None)),
    ("get_font", "face='x'", V, lambda x: ts.get_font("x")),
    ("get_font", "face=inf", O, lambda x: ts.get_font(INF)),
    ("get_font", "face=2**32+1: no such role", OK, lambda x: ts.get_font(2**32 + 1)),
    ("font_atlas_index", "face=None", T, lambda x: ts.font_atlas_index(None)),
    ("font_atlas_index", "face=nan", V, lambda x: ts.font_atlas_index(NAN)),
    ("font_atlas_index", "face=inf", O, lambda x: ts.font_atlas_index(INF)),
    ("font_atlas_index", "face=-2**40: no such role", OK, lambda x: ts.font_atlas_index(-(2**40))),
    ("face_has_glyph", "face=None", T, lambda x: ts.face_has_glyph(None, "a")),
    ("face_has_glyph", "face='x'", V, lambda x: ts.face_has_glyph("x", "a")),
    ("face_has_glyph", "char=''", T, lambda x: ts.face_has_glyph(ts.Face.Regular, "")),
    ("face_has_glyph", "char='ab'", T, lambda x: ts.face_has_glyph(ts.Face.Regular, "ab")),
    ("face_has_glyph", "char=None", T, lambda x: ts.face_has_glyph(ts.Face.Regular, None)),
    ("face_has_glyph", "char=nan", V, lambda x: ts.face_has_glyph(ts.Face.Regular, NAN)),
    ("face_has_glyph", "char=inf", O, lambda x: ts.face_has_glyph(ts.Face.Regular, INF)),
    ("face_has_glyph", "char=-1, 2**40, a lone surrogate: False", OK,
     lambda x: [ts.face_has_glyph(ts.Face.Regular, c) for c in (-1, 2**40, LONE)]),
    ("font_metrics", "face=None", T, lambda x: ts.font_metrics(None, 16.0)),
    ("font_metrics", "face='x'", V, lambda x: ts.font_metrics("x", 16.0)),
    ("font_metrics", "face=99", E, lambda x: ts.font_metrics(99, 16.0)),
    ("font_metrics", "face=2**40", E, lambda x: ts.font_metrics(2**40, 16.0)),
    ("font_metrics", "em_size=None", T, lambda x: ts.font_metrics(ts.Face.Regular, None)),
    ("font_metrics", "em_size='16'", T, lambda x: ts.font_metrics(ts.Face.Regular, "16")),
    ("font_metrics", "em_size=10**400", O, lambda x: ts.font_metrics(ts.Face.Regular, HUGE)),
    ("font_metrics", "em_size=nan", E, lambda x: ts.font_metrics(ts.Face.Regular, NAN)),
    ("font_metrics", "em_size=inf", E, lambda x: ts.font_metrics(ts.Face.Regular, INF)),
    ("font_metrics", "em_size=0", E, lambda x: ts.font_metrics(ts.Face.Regular, 0)),
    ("font_metrics", "em_size=-16", E, lambda x: ts.font_metrics(ts.Face.Regular, -16.0)),
    ("font_metrics", "em_size=1e300", E, lambda x: ts.font_metrics(ts.Face.Regular, 1e300)),
    ("glyph_advance", "face=None", T, lambda x: ts.glyph_advance(None, 16.0, "a")),
    ("glyph_advance", "face=99", E, lambda x: ts.glyph_advance(99, 16.0, "a")),
    ("glyph_advance", "em_size=None", T, lambda x: ts.glyph_advance(ts.Face.Regular, None, "a")),
    ("glyph_advance", "em_size=nan", E, lambda x: ts.glyph_advance(ts.Face.Regular, NAN, "a")),
    ("glyph_advance", "em_size=10**400", O, lambda x: ts.glyph_advance(ts.Face.Regular, HUGE, "a")),
    ("glyph_advance", "char=''", T, lambda x: ts.glyph_advance(ts.Face.Regular, 16.0, "")),
    ("glyph_advance", "char='ab'", T, lambda x: ts.glyph_advance(ts.Face.Regular, 16.0, "ab")),
    ("glyph_advance", "char=None", T, lambda x: ts.glyph_advance(ts.Face.Regular, 16.0, None)),
    ("glyph_advance", "char=nan", V, lambda x: ts.glyph_advance(ts.Face.Regular, 16.0, NAN)),
    ("glyph_advance", "char=inf", O, lambda x: ts.glyph_advance(ts.Face.Regular, 16.0, INF)),
    ("glyph_advance", "char=-1, 2**40, a lone surrogate: the missing glyph's", OK,
     lambda x: [ts.glyph_advance(ts.Face.Regular, 16.0, c) for c in (-1, 2**40, LONE)]),
    ("text", "text=None", T, lambda x: F(lambda: ts.text(None))),
    ("text", "text=bytes", T, lambda x: F(lambda: ts.text(b"abc"))),
    ("text", "a lone surrogate", U, lambda x: F(lambda: ts.text("a" + LONE))),
    ("text", "flags=None", T, lambda x: F(lambda: ts.text("a", flags=None))),
    ("text", "flags='x'", V, lambda x: F(lambda: ts.text("a", flags="x"))),
    ("text", "flags=inf", O, lambda x: F(lambda: ts.text("a", flags=INF))),
    ("text", "wrap_width=None", T, lambda x: F(lambda: ts.text("a", wrap_width=None))),
    ("text", "wrap_width='200'", T, lambda x: F(lambda: ts.text("a", wrap_width="200"))),
    ("text", "wrap_width=10**400", O, lambda x: F(lambda: ts.text("a", wrap_width=HUGE))),
    ("text", "wrap_width=nan, inf, -1, 1e300", OK,
     lambda x: F(lambda: [ts.text(PLAIN, wrap_width=w) for w in (NAN, INF, -1, 1e300)])),
    ("text", "style=5", T, lambda x: F(lambda: ts.text("a", style=5))),
    ("text", "'', NUL inside, 2 MB", OK, lambda x: F(lambda: [ts.text(t) for t in ("", "a\0b", "a" * (1 << 21))])),
    ("text_with_id", "str_id=None", T, lambda x: F(lambda: ts.text_with_id(None, "a"))),
    ("text_with_id", "str_id=a lone surrogate", U, lambda x: F(lambda: ts.text_with_id(LONE, "a"))),
    ("text_with_id", "text=5", T, lambda x: F(lambda: ts.text_with_id("id", 5))),
    ("text_with_id", "text=a lone surrogate", U, lambda x: F(lambda: ts.text_with_id("id", LONE))),
    ("text_with_id", "flags=None", T, lambda x: F(lambda: ts.text_with_id("id", "a", flags=None))),
    ("text_with_id", "wrap_width=None", T, lambda x: F(lambda: ts.text_with_id("id", "a", wrap_width=None))),
    ("text_with_id", "wrap_width=10**400", O, lambda x: F(lambda: ts.text_with_id("id", "a", wrap_width=HUGE))),
    ("text_with_id", "style='x'", T, lambda x: F(lambda: ts.text_with_id("id", "a", style="x"))),
    ("text_with_id", "str_id=''", OK, lambda x: F(lambda: ts.text_with_id("", "a"))),
    ("selectable_text", "str_id=None", T, lambda x: F(lambda: ts.selectable_text(None, "a"))),
    ("selectable_text", "str_id='': the library refuses it", E, lambda x: F(lambda: ts.selectable_text("", "a"))),
    ("selectable_text", "text=None", T, lambda x: F(lambda: ts.selectable_text("id", None))),
    ("selectable_text", "text=a lone surrogate", U, lambda x: F(lambda: ts.selectable_text("id", LONE))),
    ("selectable_text", "flags='x'", V, lambda x: F(lambda: ts.selectable_text("id", "a", flags="x"))),
    ("selectable_text", "wrap_width=None", T, lambda x: F(lambda: ts.selectable_text("id", "a", wrap_width=None))),
    ("selectable_text", "style=5", T, lambda x: F(lambda: ts.selectable_text("id", "a", style=5))),
    ("selectable_text", "selection=5", T, lambda x: F(lambda: ts.selectable_text("id", "abc", selection=5))),
    ("selectable_text", "selection=()", V, lambda x: F(lambda: ts.selectable_text("id", "abc", selection=()))),
    ("selectable_text", "selection={}", V, lambda x: F(lambda: ts.selectable_text("id", "abc", selection={}))),
    ("selectable_text", "selection=(None, 1)", T, lambda x: F(lambda: ts.selectable_text("id", "abc", selection=(None, 1)))),
    ("selectable_text", "selection=('x', 1)", V, lambda x: F(lambda: ts.selectable_text("id", "abc", selection=("x", 1)))),
    ("selectable_text", "selection=(inf, 1)", O, lambda x: F(lambda: ts.selectable_text("id", "abc", selection=(INF, 1)))),
    ("selectable_text", "selection=(-5, 2**40): clamped", OK,
     lambda x: F(lambda: [ts.selectable_text("id", t, selection=(-5, 2**40)) for t in ("abc", "aéc")])),
    ("calc_text_size", "text=None", T, lambda x: ts.calc_text_size(None)),
    ("calc_text_size", "a lone surrogate", U, lambda x: ts.calc_text_size(LONE)),
    ("calc_text_size", "flags=None", T, lambda x: ts.calc_text_size("a", flags=None)),
    ("calc_text_size", "flags=nan", V, lambda x: ts.calc_text_size("a", flags=NAN)),
    ("calc_text_size", "wrap_width='x'", T, lambda x: ts.calc_text_size("a", wrap_width="x")),
    ("calc_text_size", "wrap_width=10**400", O, lambda x: ts.calc_text_size("a", wrap_width=HUGE)),
    ("calc_text_size", "style=5", T, lambda x: ts.calc_text_size("a", style=5)),
    ("show_demo_window", "open=None, 'x', an object", OK,
     lambda x: F(lambda: [ts.show_demo_window(o) for o in (None, "x", object())])),
    # --- imtypeset.demo
    ("demo.show_demo_window", "open=5", OK, lambda x: F(lambda: demo.show_demo_window(5))),
    ("show_demo_window", "outside a frame: nothing drawn", OK, lambda x: ts.show_demo_window()),
    ("demo.show_demo_section", "outside a frame: nothing drawn", OK,
     lambda x: demo.show_demo_section(demo.DemoSection.Math)),
    ("demo.sections", "no argument", OK, lambda x: demo.sections()),
    ("demo.section_name", "section=None", T, lambda x: demo.section_name(None)),
    ("demo.section_name", "section='x'", V, lambda x: demo.section_name("x")),
    ("demo.section_name", "section=inf", O, lambda x: demo.section_name(INF)),
    ("demo.section_name", "section=2**32: no such section", OK, lambda x: demo.section_name(2**32)),
    ("demo.show_demo_section", "section=None", T, lambda x: F(lambda: demo.show_demo_section(None))),
    ("demo.show_demo_section", "section='x'", V, lambda x: F(lambda: demo.show_demo_section("x"))),
    ("demo.show_demo_section", "section=inf", O, lambda x: F(lambda: demo.show_demo_section(INF))),
    ("demo.show_demo_section", "section=2**32: no such section", OK, lambda x: F(lambda: demo.show_demo_section(2**32))),
    ("demo.DemoSection", "99", V, lambda x: demo.DemoSection(99)),
    # --- Paragraph
    ("Paragraph", "text=5", T, lambda x: ts.Paragraph(5)),
    ("Paragraph", "text=bytes", T, lambda x: ts.Paragraph(b"abc")),
    ("Paragraph", "a lone surrogate", U, lambda x: ts.Paragraph(LONE)),
    ("Paragraph", "runs=5", T, lambda x: ts.Paragraph("abc", 5)),
    ("Paragraph", "flags=None", T, lambda x: ts.Paragraph("abc", (), None)),
    ("Paragraph.set_text", "text=None", T, lambda x: x.par.set_text(None)),
    ("Paragraph.set_text", "a lone surrogate", U, lambda x: x.par.set_text("a" + LONE)),
    ("Paragraph.set_text", "runs=a generator", T, lambda x: x.par.set_text("abc", (r for r in [ts.Run(0, 1)]))),
    ("Paragraph.set_text", "runs=[None]", T, lambda x: x.par.set_text("abc", [None])),
    ("Paragraph.set_text", "runs=[(0,)]", T, lambda x: x.par.set_text("abc", [(0,)])),
    ("Paragraph.set_text", "runs=['ab']", V, lambda x: x.par.set_text("abc", ["ab"])),
    ("Paragraph.set_text", "Run.start=None", T, lambda x: x.par.set_text("abc", run_with(start=None))),
    ("Paragraph.set_text", "Run.end=nan", V, lambda x: x.par.set_text("abc", run_with(end=NAN))),
    ("Paragraph.set_text", "Run.end=inf", O, lambda x: x.par.set_text("abc", run_with(end=INF))),
    ("Paragraph.set_text", "Run.face=None", T, lambda x: x.par.set_text("abc", run_with(face=None))),
    ("Paragraph.set_text", "Run.face='x'", V, lambda x: x.par.set_text("abc", run_with(face="x"))),
    ("Paragraph.set_text", "Run.scale=None", T, lambda x: x.par.set_text("abc", run_with(scale=None))),
    ("Paragraph.set_text", "Run.scale=10**400", O, lambda x: x.par.set_text("abc", run_with(scale=HUGE))),
    ("Paragraph.set_text", "Run.color=None", T, lambda x: x.par.set_text("abc", run_with(color=None))),
    ("Paragraph.set_text", "Run.color=1.5", T, lambda x: x.par.set_text("abc", run_with(color=1.5))),
    ("Paragraph.set_text", "Run.flags=None", T, lambda x: x.par.set_text("abc", run_with(flags=None))),
    ("Paragraph.set_text", "Run.tag='x'", V, lambda x: x.par.set_text("abc", run_with(tag="x"))),
    ("Paragraph.set_text", "flags=inf", O, lambda x: x.par.set_text("abc", (), INF)),
    ("Paragraph.set_text", "every number of a run 2**40", OK,
     lambda x: [x.par.set_text(t, [ts.Run(-(2**40), 2**40, 2**40, 1e300, 2**40, 2**40, 2**40)]) for t in ("abc", "aéc")]),
    ("Paragraph.layout", "width=None", T, lambda x: x.par.layout(None)),
    ("Paragraph.layout", "width='x'", T, lambda x: x.par.layout("x")),
    ("Paragraph.layout", "width=10**400", O, lambda x: x.par.layout(HUGE)),
    ("Paragraph.layout", "width=nan, inf, -1, 0", OK, lambda x: [x.par.layout(w) for w in (NAN, INF, -1, 0)]),
    ("Paragraph.layout", "style=5", T, lambda x: x.par.layout(100.0, 5)),
    ("Paragraph.close", "twice", OK, lambda x: [x.par.close() for _ in range(2)]),
    ("Paragraph.closed", "no argument", OK, lambda x: x.par.closed),
    ("Paragraph.error", "no argument", OK, lambda x: x.par.error),
    ("Paragraph.status", "no argument", OK, lambda x: x.par.status),
    ("Paragraph.size", "no argument", OK, lambda x: x.par.size),
    ("Paragraph.line_count", "no argument", OK, lambda x: x.par.line_count),
    ("Paragraph.lines", "no argument", OK, lambda x: x.par.lines()),
    ("Paragraph.formulas", "no argument", OK, lambda x: x.par.formulas()),
    ("Paragraph.line", "index=None", T, lambda x: x.par.line(None)),
    ("Paragraph.line", "index='x'", V, lambda x: x.par.line("x")),
    ("Paragraph.line", "index=inf", O, lambda x: x.par.line(INF)),
    ("Paragraph.line", "index=-1, 2**32: no such line", OK, lambda x: [x.par.line(i) for i in (-1, 2**32)]),
    ("Paragraph.draw", "draw_list='x'", T, lambda x: F(lambda: x.par.draw("x", (0, 0)))),
    ("Paragraph.draw", "draw_list=b'x'", T, lambda x: F(lambda: x.par.draw(b"x", (0, 0)))),
    ("Paragraph.draw", "draw_list=1.5", T, lambda x: F(lambda: x.par.draw(1.5, (0, 0)))),
    ("Paragraph.draw", "pos=None", T, lambda x: F(lambda: x.par.draw(None, None))),
    ("Paragraph.draw", "pos=(1, 2, 3)", T, lambda x: F(lambda: x.par.draw(None, (1, 2, 3)))),
    ("Paragraph.draw", "pos=('a', 'b')", T, lambda x: F(lambda: x.par.draw(None, ("a", "b")))),
    ("Paragraph.draw", "pos=(10**400, 0)", O, lambda x: F(lambda: x.par.draw(None, (HUGE, 0)))),
    ("Paragraph.draw", "pos=(nan, inf), (), (1,)", OK,
     lambda x: F(lambda: [x.par.draw(None, pos) for pos in ((NAN, INF), (), (1,))])),
    ("Paragraph.draw", "selection=5", T, lambda x: F(lambda: x.par.draw(None, (0, 0), 5))),
    ("Paragraph.draw", "selection=(1,)", V, lambda x: F(lambda: x.par.draw(None, (0, 0), (1,)))),
    ("Paragraph.draw", "selection=(None, 1)", T, lambda x: F(lambda: x.par.draw(None, (0, 0), (None, 1)))),
    ("Paragraph.draw", "selection=(nan, 1)", V, lambda x: F(lambda: x.par.draw(None, (0, 0), (NAN, 1)))),
    ("Paragraph.draw", "selection=(inf, 1)", O, lambda x: F(lambda: x.par.draw(None, (0, 0), (INF, 1)))),
    ("Paragraph.draw_to_buffers", "pos=None", T, lambda x: x.par.draw_to_buffers(None, BOX)),
    ("Paragraph.draw_to_buffers", "pos=(10**400, 0)", O, lambda x: x.par.draw_to_buffers((HUGE, 0), BOX)),
    ("Paragraph.draw_to_buffers", "clip_rect=None", T, lambda x: x.par.draw_to_buffers((0, 0), None)),
    ("Paragraph.draw_to_buffers", "clip_rect of five", T, lambda x: x.par.draw_to_buffers((0, 0), (0, 0, 1, 1, 1))),
    ("Paragraph.draw_to_buffers", "clip_rect=(0, 0, None, 1)", T, lambda x: x.par.draw_to_buffers((0, 0), (0, 0, None, 1))),
    ("Paragraph.draw_to_buffers", "selection={}", V, lambda x: x.par.draw_to_buffers((0, 0), BOX, {})),
    ("Paragraph.draw_to_buffers", "selection=('x', 1)", V, lambda x: x.par.draw_to_buffers((0, 0), BOX, ("x", 1))),
    ("Paragraph.hit_test", "local_pos=None", T, lambda x: x.par.hit_test(None)),
    ("Paragraph.hit_test", "local_pos=(None, 0)", T, lambda x: x.par.hit_test((None, 0))),
    ("Paragraph.hit_test", "local_pos=(10**400, 0)", O, lambda x: x.par.hit_test((HUGE, 0))),
    ("Paragraph.hit_test", "local_pos=(nan, -inf)", OK, lambda x: x.par.hit_test((NAN, -INF))),
    ("Paragraph.select_range", "anchor=None", T, lambda x: x.par.select_range(None, (1, 1))),
    ("Paragraph.select_range", "active=(0, 10**400)", O, lambda x: x.par.select_range((0, 0), (0, HUGE))),
    ("Paragraph.select_range", "granularity=None", T, lambda x: x.par.select_range((0, 0), (9, 9), None)),
    ("Paragraph.select_range", "granularity='x'", V, lambda x: x.par.select_range((0, 0), (9, 9), "x")),
    ("Paragraph.select_range", "granularity=inf", O, lambda x: x.par.select_range((0, 0), (9, 9), INF)),
    ("Paragraph.select_range", "granularity=2**40", OK, lambda x: x.par.select_range((0, 0), (9, 9), 2**40)),
    ("Paragraph.range_rects", "selection=None", T, lambda x: x.par.range_rects(None)),
    ("Paragraph.range_rects", "selection=[]", V, lambda x: x.par.range_rects([])),
    ("Paragraph.range_rects", "selection=(0, None)", T, lambda x: x.par.range_rects((0, None))),
    ("Paragraph.range_rects", "selection=(0, 'x')", V, lambda x: x.par.range_rects((0, "x"))),
    ("Paragraph.range_rects", "selection=(0, inf)", O, lambda x: x.par.range_rects((0, INF))),
    ("Paragraph.range_rects", "selection=(-2**40, 2**40)", OK, lambda x: [p.range_rects((-(2**40), 2**40)) for p in (x.par, x.plain)]),
    ("Paragraph.caret_rect", "offset=None", T, lambda x: x.par.caret_rect(None)),
    ("Paragraph.caret_rect", "offset=nan", V, lambda x: x.par.caret_rect(NAN)),
    ("Paragraph.caret_rect", "offset=inf", O, lambda x: x.par.caret_rect(INF)),
    ("Paragraph.caret_rect", "offset=-1, 2**40", OK, lambda x: [p.caret_rect(o) for p in (x.par, x.plain) for o in (-1, 2**40)]),
    ("Paragraph.range_text", "selection=5", T, lambda x: x.par.range_text(5)),
    ("Paragraph.range_text", "selection=''", V, lambda x: x.par.range_text("")),
    ("Paragraph.range_text", "selection=(None, None)", T, lambda x: x.par.range_text((None, None))),
    ("Paragraph.range_text", "selection=(0, inf)", O, lambda x: x.par.range_text((0, INF))),
    # --- Document: the edits
    ("Document", "no argument", OK, lambda x: ts.Document().close()),
    ("Document.from_blocks", "blocks=None", T, lambda x: ts.Document.from_blocks(None)),
    ("Document.from_blocks", "blocks=a generator", T, lambda x: ts.Document.from_blocks(b for b in every_kind())),
    ("Document.from_blocks", "blocks=[None]", T, lambda x: ts.Document.from_blocks([None])),
    ("Document.from_blocks", "blocks='x'", V, lambda x: ts.Document.from_blocks("x")),
    ("Document.set_blocks", "blocks=5", T, lambda x: x.doc.set_blocks(5)),
    ("Document.set_blocks", "blocks=[5]", T, lambda x: x.doc.set_blocks([5])),
    ("Document.set_blocks", "blocks=[(1, 2, 3)]", T, lambda x: x.doc.set_blocks([(1, 2, 3)])),
    ("Document.set_blocks", "Block.text=None", T, lambda x: x.doc.set_blocks(blocks_with(text=None))),
    ("Document.set_blocks", "Block.text=bytes", T, lambda x: x.doc.set_blocks(blocks_with(text=b"abc"))),
    ("Document.set_blocks", "Block.text=a lone surrogate", U, lambda x: x.doc.set_blocks(blocks_with(text=LONE))),
    ("Document.set_blocks", "Block.marker=5", T, lambda x: x.doc.set_blocks(blocks_with(marker=5))),
    ("Document.set_blocks", "Block.marker=a lone surrogate", U, lambda x: x.doc.set_blocks(blocks_with(marker=LONE))),
    ("Document.set_blocks", "Block.kind=None", T, lambda x: x.doc.set_blocks(blocks_with(kind=None))),
    ("Document.set_blocks", "Block.kind='x'", V, lambda x: x.doc.set_blocks(blocks_with(kind="x"))),
    ("Document.set_blocks", "Block.kind=inf", O, lambda x: x.doc.set_blocks(blocks_with(kind=INF))),
    ("Document.set_blocks", "Block.style=None", T, lambda x: x.doc.set_blocks(blocks_with(style=None))),
    ("Document.set_blocks", "Block.level=nan", V, lambda x: x.doc.set_blocks(blocks_with(level=NAN))),
    ("Document.set_blocks", "Block.flags=None", T, lambda x: x.doc.set_blocks(blocks_with(flags=None))),
    ("Document.set_blocks", "Block.left_indent=None", T, lambda x: x.doc.set_blocks(blocks_with(left_indent=None))),
    ("Document.set_blocks", "Block.left_indent='x'", T, lambda x: x.doc.set_blocks(blocks_with(left_indent="x"))),
    ("Document.set_blocks", "Block.extent=10**400", O, lambda x: x.doc.set_blocks(blocks_with(extent=HUGE))),
    ("Document.set_blocks", "Block.runs=5", T, lambda x: x.doc.set_blocks(blocks_with(runs=5))),
    ("Document.set_blocks", "Block.runs=[None]", T, lambda x: x.doc.set_blocks(blocks_with(runs=[None]))),
    ("Document.set_blocks", "Run.start=None", T, lambda x: x.doc.set_blocks(blocks_with(runs=run_with(start=None)))),
    ("Document.set_blocks", "Run.start=1.5", T, lambda x: x.doc.set_blocks(blocks_with(runs=run_with(start=1.5)))),
    ("Document.set_blocks", "Run.end=nan", T, lambda x: x.doc.set_blocks(blocks_with(runs=run_with(end=NAN)))),
    ("Document.set_blocks", "Run.face=None", T, lambda x: x.doc.set_blocks(blocks_with(runs=run_with(face=None)))),
    ("Document.set_blocks", "Run.face='x'", V, lambda x: x.doc.set_blocks(blocks_with(runs=run_with(face="x")))),
    ("Document.set_blocks", "Run.face=inf", O, lambda x: x.doc.set_blocks(blocks_with(runs=run_with(face=INF)))),
    ("Document.set_blocks", "Run.scale=None", T, lambda x: x.doc.set_blocks(blocks_with(runs=run_with(scale=None)))),
    ("Document.set_blocks", "Run.scale=10**400", O, lambda x: x.doc.set_blocks(blocks_with(runs=run_with(scale=HUGE)))),
    ("Document.set_blocks", "Run.color=None", T, lambda x: x.doc.set_blocks(blocks_with(runs=run_with(color=None)))),
    ("Document.set_blocks", "Run.color=1.5", T, lambda x: x.doc.set_blocks(blocks_with(runs=run_with(color=1.5)))),
    ("Document.set_blocks", "Run.flags=None", T, lambda x: x.doc.set_blocks(blocks_with(runs=run_with(flags=None)))),
    ("Document.set_blocks", "Run.tag='x'", V, lambda x: x.doc.set_blocks(blocks_with(runs=run_with(tag="x")))),
    ("Document.set_blocks", "rows on a block that is no table", V, lambda x: x.doc.set_blocks(blocks_with(rows=[["a"]]))),
    ("Document.set_blocks", "a table's row is a str", T,
     lambda x: x.doc.set_blocks([ts.Block(ts.BlockKind.Table, rows=["ab"])])),
    ("Document.set_blocks", "a table's cell is None", T,
     lambda x: x.doc.set_blocks([ts.Block(ts.BlockKind.Table, rows=[[None]])])),
    ("Document.set_blocks", "a table's cell holds a lone surrogate", U,
     lambda x: x.doc.set_blocks([ts.Block(ts.BlockKind.Table, rows=[[LONE]])])),
    ("Document.set_blocks", "a table's text is not its rows'", V,
     lambda x: x.doc.set_blocks([ts.Block(ts.BlockKind.Table, text="x", rows=[["a"]])])),
    ("Document.set_blocks", "TableColumn.align=None", T,
     lambda x: x.doc.set_blocks([ts.Block(ts.BlockKind.Table, rows=[["a"]], columns=[(None,)])])),
    ("Document.set_blocks", "TableColumn.width='x'", T,
     lambda x: x.doc.set_blocks([ts.Block(ts.BlockKind.Table, rows=[["a"]], columns=[(0, "x")])])),
    ("Document.set_blocks", "styles=[5]", T, lambda x: x.doc.set_blocks(blocks_with(), [5])),
    ("Document.set_blocks", "styles=a Style", T, lambda x: x.doc.set_blocks(blocks_with(), [ts.Style()])),
    ("Document.set_blocks", "containers=None", T, lambda x: x.doc.set_blocks(blocks_with(), (), None)),
    ("Document.set_blocks", "every number of a block and a run 2**40", OK,
     lambda x: x.doc.set_blocks([
         ts.Block(2**40, text, [ts.Run(-(2**40), 2**40, 2**40, 1e300, 2**40, 2**40, 2**40)], "", 2**40,
                  1e300, 2**40, 2**40, 2**40, 1e300, 2**40, 2**40)
         for text in ("abc", "aéc")])),
    ("Document.set_blocks", "every number of a table's column and cell run 2**40", OK,
     lambda x: x.doc.set_blocks([ts.Block(
         ts.BlockKind.Table, rows=[[ts.Cell("né", [ts.Run(-(2**40), 2**40, 2**40, 1e300, -1, 2**40, 2**40)])]],
         columns=[(2**40, 1e300, -1e300)])])),
    ("Document.replace_blocks", "first=None", T, lambda x: x.doc.replace_blocks(None, 0)),
    ("Document.replace_blocks", "first='x'", V, lambda x: x.doc.replace_blocks("x", 0)),
    ("Document.replace_blocks", "count=inf", O, lambda x: x.doc.replace_blocks(0, INF)),
    ("Document.replace_blocks", "first=-1", V, lambda x: x.doc.replace_blocks(-1, 0)),
    ("Document.replace_blocks", "count=2**40: blocks that are not there", V, lambda x: x.doc.replace_blocks(0, 2**40)),
    ("Document.replace_blocks", "blocks=[None]", T, lambda x: x.doc.replace_blocks(0, 1, [None])),
    ("Document.replace_blocks", "Block.text=a lone surrogate", U, lambda x: x.doc.replace_blocks(0, 1, blocks_with(text=LONE))),
    ("Document.replace_blocks", "Block.style=2**40: clamped", OK, lambda x: x.doc.replace_blocks(0, 1, blocks_with(style=2**40))),
    ("Document.append_text", "text=None", T, lambda x: x.prose.append_text(None)),
    ("Document.append_text", "text=bytes", T, lambda x: x.prose.append_text(b"abc")),
    ("Document.append_text", "a lone surrogate", U, lambda x: x.prose.append_text(LONE)),
    ("Document.append_text", "a document with no block", V, lambda x: ts.Document().append_text("a")),
    ("Document.append_text", "runs=5", T, lambda x: x.prose.append_text("a", 5)),
    ("Document.append_text", "runs=[None]", T, lambda x: x.prose.append_text("a", [None])),
    ("Document.append_text", "Run.start=None", T, lambda x: x.prose.append_text("a", run_with(start=None))),
    ("Document.append_text", "Run.face='x'", V, lambda x: x.prose.append_text("a", run_with(face="x"))),
    ("Document.append_text", "Run.scale=10**400", O, lambda x: x.prose.append_text("a", run_with(scale=HUGE))),
    ("Document.append_text", "Run.color=None", T, lambda x: x.prose.append_text("a", run_with(color=None))),
    ("Document.append_text", "every number of a run 2**40", OK,
     lambda x: x.prose.append_text(" é", [ts.Run(-(2**40), 2**40, 2**40, 1e300, 2**40, 2**40, 2**40)])),
    ("Document.append_table_cells", "cells=None", T, lambda x: x.doc.append_table_cells(None)),
    ("Document.append_table_cells", "cells='ab'", T, lambda x: x.doc.append_table_cells("ab")),
    ("Document.append_table_cells", "cells=5", T, lambda x: x.doc.append_table_cells(5)),
    ("Document.append_table_cells", "a cell is 5", T, lambda x: x.doc.append_table_cells([5])),
    ("Document.append_table_cells", "a cell holds a lone surrogate", U,
     lambda x: x.doc.append_table_cells([LONE])),
    ("Document.append_table_cells", "the last block is not a table", V,
     lambda x: x.prose.append_table_cells(["a"])),
    ("Document.append_table_cells", "a cell's Run.face=None", T,
     lambda x: x.doc.append_table_cells([ts.Cell("ab", [ts.Run(0, 1, None)])])),
    ("Document.append_table_cells", "a cell's Run.tag=2**40: its low 32 bits", OK,
     lambda x: x.doc.append_table_cells([ts.Cell("ab", [ts.Run(0, 1, tag=2**40)])])),
    ("Document.append_table_cells", "no cells: nothing done", OK,
     lambda x: x.doc.append_table_cells([])),
    ("Document.append_table_rows", "rows=None", T, lambda x: x.doc.append_table_rows(None)),
    ("Document.append_table_rows", "rows=['ab']", T, lambda x: x.doc.append_table_rows(["ab"])),
    ("Document.append_table_rows", "a cell is 5", T, lambda x: x.doc.append_table_rows([[5]])),
    ("Document.append_table_rows", "a cell holds a lone surrogate", U, lambda x: x.doc.append_table_rows([[LONE]])),
    ("Document.append_table_rows", "the last block is not a table", V, lambda x: x.prose.append_table_rows([["a"]])),
    ("Document.append_table_rows", "a cell's Run.face=None", T,
     lambda x: x.doc.append_table_rows([[ts.Cell("ab", run_with(face=None))]])),
    ("Document.append_table_rows", "a cell's Run.tag='x'", V,
     lambda x: x.doc.append_table_rows([[ts.Cell("ab", run_with(tag="x"))]])),
    ("Document.append_table_rows", "every number of a cell's run 2**40", OK,
     lambda x: x.doc.append_table_rows([[ts.Cell("né", [ts.Run(-(2**40), 2**40, 2**40, 1e300, 2**40, 2**40, 2**40)])]])),
    ("Document.set_table_columns", "block=None", T, lambda x: x.doc.set_table_columns(None, [])),
    ("Document.set_table_columns", "block='x'", V, lambda x: x.doc.set_table_columns("x", [])),
    ("Document.set_table_columns", "block=inf", O, lambda x: x.doc.set_table_columns(INF, [])),
    ("Document.set_table_columns", "block=2**32+6: no such block", E, lambda x: x.doc.set_table_columns(2**32 + 6, [])),
    ("Document.set_table_columns", "columns=None", T, lambda x: x.doc.set_table_columns(6, None)),
    ("Document.set_table_columns", "columns=[None]", T, lambda x: x.doc.set_table_columns(6, [None])),
    ("Document.set_table_columns", "TableColumn.align='x'", V, lambda x: x.doc.set_table_columns(6, [("x",)])),
    ("Document.set_table_columns", "TableColumn.width=None", T, lambda x: x.doc.set_table_columns(6, [(0, None)])),
    ("Document.set_table_columns", "TableColumn.share=10**400", O, lambda x: x.doc.set_table_columns(6, [(0, 0.0, HUGE)])),
    ("Document.set_table_columns", "align=2**40, width=1e300, share=nan", OK,
     lambda x: x.doc.set_table_columns(6, [(2**40, 1e300, NAN)])),
    ("Document.set_block_extent", "block=None", T, lambda x: x.doc.set_block_extent(None, 1.0)),
    ("Document.set_block_extent", "block=nan", V, lambda x: x.doc.set_block_extent(NAN, 1.0)),
    ("Document.set_block_extent", "block=2**32+3: no such block", E, lambda x: x.doc.set_block_extent(2**32 + 3, 1.0)),
    ("Document.set_block_extent", "extent=None", T, lambda x: x.doc.set_block_extent(3, None)),
    ("Document.set_block_extent", "extent=10**400", O, lambda x: x.doc.set_block_extent(3, HUGE)),
    ("Document.set_block_extent", "extent=nan, inf, -1", OK, lambda x: [x.doc.set_block_extent(3, e) for e in (NAN, INF, -1)]),
    ("Document.set_options", "options=5", T, lambda x: x.doc.set_options(5)),
    ("Document.set_options", "options=a Style", T, lambda x: x.doc.set_options(ts.Style())),
    ("Document.set_options", "options=None", OK, lambda x: x.doc.set_options(None)),
    ("Document.block_count", "no argument", OK, lambda x: x.doc.block_count),
    ("Document.style_count", "no argument", OK, lambda x: x.doc.style_count),
    ("Document.container_count", "no argument", OK, lambda x: x.doc.container_count),
    ("Document.closed", "no argument", OK, lambda x: x.doc.closed),
    ("Document.close", "twice", OK, lambda x: [x.doc.close() for _ in range(2)]),
    # --- Document: layout and drawing
    ("Document.layout", "width=None", T, lambda x: x.doc.layout(None)),
    ("Document.layout", "width='x'", T, lambda x: x.doc.layout("x")),
    ("Document.layout", "width=10**400", O, lambda x: x.doc.layout(HUGE)),
    ("Document.layout", "width=nan, inf, -1, 0", OK, lambda x: [x.doc.layout(w) for w in (NAN, INF, -1, 0)]),
    ("Document.layout", "style=5", T, lambda x: x.doc.layout(100.0, 5)),
    ("Document.layout_window", "top=None", T, lambda x: x.doc.layout_window(None, 1.0)),
    ("Document.layout_window", "bottom='x'", T, lambda x: x.doc.layout_window(0.0, "x")),
    ("Document.layout_window", "bottom=10**400", O, lambda x: x.doc.layout_window(0.0, HUGE)),
    ("Document.layout_window", "nan, inf", OK, lambda x: [x.doc.layout_window(a, b) for a, b in ((NAN, NAN), (-INF, INF), (9.0, 0.0))]),
    ("Document.size", "no argument", OK, lambda x: x.doc.size()),
    ("Document.stats", "no argument", OK, lambda x: x.doc.stats()),
    ("Document.block_error", "block=None", T, lambda x: x.doc.block_error(None)),
    ("Document.block_error", "block='x'", V, lambda x: x.doc.block_error("x")),
    ("Document.block_error", "block=inf", O, lambda x: x.doc.block_error(INF)),
    ("Document.block_error", "block=-1, 2**32+1: no such block", OK, lambda x: [x.doc.block_error(b) for b in (-1, 2**32 + 1)]),
    ("Document.pack_marks", "marks=None", T, lambda x: x.doc.pack_marks(None)),
    ("Document.pack_marks", "marks=a generator", T, lambda x: x.doc.pack_marks(m for m in mark_with())),
    ("Document.pack_marks", "marks=[None]", T, lambda x: x.doc.pack_marks([None])),
    ("Document.pack_marks", "marks=[()]", T, lambda x: x.doc.pack_marks([()])),
    ("Document.pack_marks", "marks=[{}]", T, lambda x: x.doc.pack_marks([{}])),
    ("Document.pack_marks", "Mark.range=5", T, lambda x: x.doc.pack_marks(mark_with(range=5))),
    ("Document.pack_marks", "Mark.range of one position", V, lambda x: x.doc.pack_marks(mark_with(range=((1, 2),)))),
    ("Document.pack_marks", "a position's block is None", T, lambda x: x.doc.pack_marks(mark_with(range=((None, 2), (1, 9))))),
    ("Document.pack_marks", "a position's offset is 1.5", T, lambda x: x.doc.pack_marks(mark_with(range=((1, 1.5), (1, 9))))),
    ("Document.pack_marks", "Mark.color=None", T, lambda x: x.doc.pack_marks(mark_with(color=None))),
    ("Document.pack_marks", "Mark.place=None", T, lambda x: x.doc.pack_marks(mark_with(place=None))),
    ("Document.pack_marks", "Mark.place='x'", V, lambda x: x.doc.pack_marks(mark_with(place="x"))),
    ("Document.pack_marks", "Mark.place=inf", O, lambda x: x.doc.pack_marks(mark_with(place=INF))),
    ("Document.pack_marks", "Mark.thickness=None", T, lambda x: x.doc.pack_marks(mark_with(thickness=None))),
    ("Document.pack_marks", "Mark.offset=10**400", O, lambda x: x.doc.pack_marks(mark_with(offset=HUGE))),
    ("Document.pack_marks", "every number of a mark 2**40", OK,
     lambda x: x.doc.pack_marks([(((2**40, -(2**40)), (-(2**40), 2**40)), 2**40, 2**40, 1e300, -1e300), (((0, 0), (0, 1)), -1)])),
    ("Document.draw", "draw_list='x'", T, lambda x: F(lambda: x.doc.draw("x", (0, 0)))),
    ("Document.draw", "draw_list=1.5", T, lambda x: F(lambda: x.doc.draw(1.5, (0, 0)))),
    ("Document.draw", "pos=None", T, lambda x: F(lambda: x.doc.draw(None, None))),
    ("Document.draw", "pos=(1, 2, 3)", T, lambda x: F(lambda: x.doc.draw(None, (1, 2, 3)))),
    ("Document.draw", "pos=(10**400, 0)", O, lambda x: F(lambda: x.doc.draw(None, (HUGE, 0)))),
    ("Document.draw", "pos=(nan, inf)", OK, lambda x: F(lambda: x.doc.draw(None, (NAN, INF)))),
    ("Document.draw", "selection=5", T, lambda x: F(lambda: x.doc.draw(None, (0, 0), 5))),
    ("Document.draw", "selection=(0, 1)", T, lambda x: F(lambda: x.doc.draw(None, (0, 0), (0, 1)))),
    ("Document.draw", "selection of one position", V, lambda x: F(lambda: x.doc.draw(None, (0, 0), ((0, 1),)))),
    ("Document.draw", "selection=((None, 0), (1, 1))", T, lambda x: F(lambda: x.doc.draw(None, (0, 0), ((None, 0), (1, 1))))),
    ("Document.draw", "selection=((0, 1.5), (1, 1))", T, lambda x: F(lambda: x.doc.draw(None, (0, 0), ((0, 1.5), (1, 1))))),
    ("Document.draw", "selection=((-2**40, -2**40), (2**40, 2**40))", OK,
     lambda x: F(lambda: x.doc.draw(None, (0, 0), ((-(2**40), -(2**40)), (2**40, 2**40))))),
    ("Document.draw", "marks=5", T, lambda x: F(lambda: x.doc.draw(None, (0, 0), None, 5))),
    ("Document.draw", "marks=[None]", T, lambda x: F(lambda: x.doc.draw(None, (0, 0), None, [None]))),
    ("Document.draw", "Mark.color=None", T, lambda x: F(lambda: x.doc.draw(None, (0, 0), None, mark_with(color=None)))),
    ("Document.draw_to_buffers", "pos=None", T, lambda x: x.doc.draw_to_buffers(None, BOX)),
    ("Document.draw_to_buffers", "clip_rect=None", T, lambda x: x.doc.draw_to_buffers((0, 0), None)),
    ("Document.draw_to_buffers", "clip_rect=(0, 0, 10**400, 1)", O, lambda x: x.doc.draw_to_buffers((0, 0), (0, 0, HUGE, 1))),
    ("Document.draw_to_buffers", "selection='x'", V, lambda x: x.doc.draw_to_buffers((0, 0), BOX, "x")),
    ("Document.draw_to_buffers", "marks=[()]", T, lambda x: x.doc.draw_to_buffers((0, 0), BOX, None, [()])),
    ("Document.draw_to_buffers", "Mark.place='x'", V, lambda x: x.doc.draw_to_buffers((0, 0), BOX, None, mark_with(place="x"))),
    ("Document.view", "str_id=None", T, lambda x: F(lambda: x.doc.view(None))),
    ("Document.view", "str_id=bytes", T, lambda x: F(lambda: x.doc.view(b"view"))),
    ("Document.view", "str_id=a lone surrogate", U, lambda x: F(lambda: x.doc.view(LONE))),
    ("Document.view", "str_id='': the view's own ID", OK, lambda x: F(lambda: x.doc.view(""))),
    ("Document.view", "str_id='\\0x': the same", OK, lambda x: F(lambda: x.doc.view("\0x"))),
    ("Document.view", "size=None", T, lambda x: F(lambda: x.doc.view("view", None))),
    ("Document.view", "size=(None, 0)", T, lambda x: F(lambda: x.doc.view("view", (None, 0)))),
    ("Document.view", "size=(10**400, 0)", O, lambda x: F(lambda: x.doc.view("view", (HUGE, 0)))),
    ("Document.view", "size=(nan, -inf)", OK, lambda x: F(lambda: x.doc.view("view", (NAN, -INF)))),
    ("Document.view", "selection=5", T, lambda x: F(lambda: x.doc.view("view", (0, 0), 5))),
    ("Document.view", "selection=()", V, lambda x: F(lambda: x.doc.view("view", (0, 0), ()))),
    ("Document.view", "marks=[None]", T, lambda x: F(lambda: x.doc.view("view", (0, 0), None, [None]))),
    ("Document.view", "style=5", T, lambda x: F(lambda: x.doc.view("view", (0, 0), None, (), 5))),
    # --- Document: questions
    ("Document.hit_test", "local_pos=None", T, lambda x: x.doc.hit_test(None)),
    ("Document.hit_test", "local_pos=('a', 'b')", T, lambda x: x.doc.hit_test(("a", "b"))),
    ("Document.hit_test", "local_pos=(10**400, 0)", O, lambda x: x.doc.hit_test((HUGE, 0))),
    ("Document.hit_test", "local_pos=(nan, inf)", OK, lambda x: x.doc.hit_test((NAN, INF))),
    ("Document.select_range", "anchor=None", T, lambda x: x.doc.select_range(None, (1, 1))),
    ("Document.select_range", "active=(10**400, 0)", O, lambda x: x.doc.select_range((0, 0), (HUGE, 0))),
    ("Document.select_range", "granularity=None", T, lambda x: x.doc.select_range((0, 0), (9, 9), None)),
    ("Document.select_range", "granularity='x'", V, lambda x: x.doc.select_range((0, 0), (9, 9), "x")),
    ("Document.select_range", "granularity=inf", O, lambda x: x.doc.select_range((0, 0), (9, 9), INF)),
    ("Document.select_range", "granularity=2**40", OK, lambda x: x.doc.select_range((0, 0), (9, 9), 2**40)),
    ("Document.range_rects", "range=None", T, lambda x: x.doc.range_rects(None)),
    ("Document.range_rects", "range=(0, 1)", T, lambda x: x.doc.range_rects((0, 1))),
    ("Document.range_rects", "range of three positions", V, lambda x: x.doc.range_rects(((0, 0), (0, 1), (0, 2)))),
    ("Document.range_rects", "a position of three", V, lambda x: x.doc.range_rects(((0, 0, 0), (0, 1)))),
    ("Document.range_rects", "a position's block is 'x'", T, lambda x: x.doc.range_rects((("x", 0), (0, 1)))),
    ("Document.range_rects", "a position's offset is None", T, lambda x: x.doc.range_rects(((0, None), (0, 1)))),
    ("Document.range_rects", "positions 2**40", OK, lambda x: x.doc.range_rects(((-(2**40), 2**40), (2**40, -(2**40))))),
    ("Document.caret_rect", "position=None", T, lambda x: x.doc.caret_rect(None)),
    ("Document.caret_rect", "position=(1,)", V, lambda x: x.doc.caret_rect((1,))),
    ("Document.caret_rect", "position=(1, 1.5)", T, lambda x: x.doc.caret_rect((1, 1.5))),
    ("Document.caret_rect", "position=(2**32+1, 2**40)", OK, lambda x: x.doc.caret_rect((2**32 + 1, 2**40))),
    ("Document.range_text", "range=5", T, lambda x: x.doc.range_text(5)),
    ("Document.range_text", "range='x'", V, lambda x: x.doc.range_text("x")),
    ("Document.range_text", "a position's block is None", T, lambda x: x.doc.range_text(((None, 0), (1, 1)))),
    ("Document.range_text", "separator=5", T, lambda x: x.doc.range_text(SEL, 5)),
    ("Document.range_text", "separator=bytes", T, lambda x: x.doc.range_text(SEL, b"\n")),
    ("Document.range_text", "separator=a lone surrogate", U, lambda x: x.doc.range_text(SEL, LONE)),
    ("Document.range_text", "separator='', NUL inside", OK, lambda x: [x.doc.range_text(SEL, s) for s in ("", "a\0b")]),
    ("Document.block_geometry", "block=None", T, lambda x: x.doc.block_geometry(None)),
    ("Document.block_geometry", "block='x'", V, lambda x: x.doc.block_geometry("x")),
    ("Document.block_geometry", "block=inf", O, lambda x: x.doc.block_geometry(INF)),
    ("Document.block_geometry", "block=-1, 2**32+1: no such block", OK, lambda x: [x.doc.block_geometry(b) for b in (-1, 2**32 + 1)]),
    ("Document.block_line", "block=None", T, lambda x: x.doc.block_line(None, 0)),
    ("Document.block_line", "line='x'", V, lambda x: x.doc.block_line(1, "x")),
    ("Document.block_line", "line=inf", O, lambda x: x.doc.block_line(1, INF)),
    ("Document.block_line", "line=2**32: no such line", OK, lambda x: x.doc.block_line(1, 2**32)),
    ("Document.table_geometry", "block=None", T, lambda x: x.doc.table_geometry(None)),
    ("Document.table_geometry", "block=nan", V, lambda x: x.doc.table_geometry(NAN)),
    ("Document.table_geometry", "block=inf", O, lambda x: x.doc.table_geometry(INF)),
    ("Document.table_geometry", "block=2**32+6: no such block", OK, lambda x: x.doc.table_geometry(2**32 + 6)),
    ("Document.table_column", "block=None", T, lambda x: x.doc.table_column(None, 0)),
    ("Document.table_column", "column='x'", V, lambda x: x.doc.table_column(6, "x")),
    ("Document.table_column", "column=inf", O, lambda x: x.doc.table_column(6, INF)),
    ("Document.table_column", "column=2**32: no such column", OK, lambda x: x.doc.table_column(6, 2**32)),
    ("Document.table_row", "row=None", T, lambda x: x.doc.table_row(6, None)),
    ("Document.table_row", "block='x'", V, lambda x: x.doc.table_row("x", 0)),
    ("Document.table_row", "row=inf", O, lambda x: x.doc.table_row(6, INF)),
    ("Document.table_row", "row=2**32: no such row", OK, lambda x: x.doc.table_row(6, 2**32)),
    ("Document.set_columns", "columns=5", T, lambda x: x.doc.set_columns(5)),
    ("Document.set_columns", "columns of five", T, lambda x: x.doc.set_columns((1, 2, 3, 4, 5))),
    ("Document.set_columns", "columns='x'", V, lambda x: x.doc.set_columns("x")),
    ("Document.set_columns", "Columns.count=None", T, lambda x: x.doc.set_columns(ts.Columns(None))),
    ("Document.set_columns", "Columns.count=inf", O, lambda x: x.doc.set_columns(ts.Columns(INF))),
    ("Document.set_columns", "Columns.height=None", T, lambda x: x.doc.set_columns(ts.Columns(2, None))),
    ("Document.set_columns", "Columns.gutter=10**400", O, lambda x: x.doc.set_columns(ts.Columns(2, 100.0, HUGE))),
    ("Document.set_columns", "Columns.flags='x'", V, lambda x: x.doc.set_columns(ts.Columns(2, 100.0, 0.0, "x"))),
    ("Document.set_columns", "count=2**40, height=nan, gutter=-inf", OK, lambda x: x.doc.set_columns((2**40, NAN, -INF, 2**40))),
    ("Document.column_count", "no argument", OK, lambda x: x.doc.column_count()),
    ("Document.column", "index=None", T, lambda x: x.doc.column(None)),
    ("Document.column", "index='x'", V, lambda x: x.doc.column("x")),
    ("Document.column", "index=inf", O, lambda x: x.doc.column(INF)),
    ("Document.column", "index=-1, 2**32: no such column", OK, lambda x: [x.doc.column(i) for i in (-1, 2**32)]),
    ("Document.column_pos", "uncut_pos=None", T, lambda x: x.doc.column_pos(None)),
    ("Document.column_pos", "uncut_pos=(0, 10**400)", O, lambda x: x.doc.column_pos((0, HUGE))),
    ("Document.column_pos", "uncut_pos=(nan, inf)", OK, lambda x: x.doc.column_pos((NAN, INF))),
    ("Document.table_cell", "position=None", T, lambda x: x.doc.table_cell(None)),
    ("Document.table_cell", "position='x'", V, lambda x: x.doc.table_cell("x")),
    ("Document.table_cell", "position=(6, None)", T, lambda x: x.doc.table_cell((6, None))),
    ("Document.table_cell", "position=(2**32+6, 2**40)", OK, lambda x: x.doc.table_cell((2**32 + 6, 2**40))),
    ("Document.table_position", "block=None", T, lambda x: x.doc.table_position(None, 0, 0)),
    ("Document.table_position", "row='x'", V, lambda x: x.doc.table_position(6, "x", 0)),
    ("Document.table_position", "column=inf", O, lambda x: x.doc.table_position(6, 0, INF)),
    ("Document.table_position", "offset=None", T, lambda x: x.doc.table_position(6, 0, 0, None)),
    ("Document.table_position", "every index 2**40", OK,
     lambda x: [x.doc.table_position(*where) for where in ((2**40, 0, 0), (6, 2**40, 0), (6, 0, 2**40), (6, 0, 1, 2**40))]),
    # --- the classes a host constructs
    ("Error", "constructed with a str where the library's ErrorInfo goes", AttributeError, lambda x: ts.Error("no", "where")),
    ("Style", "a field of the wrong kind", T, lambda x: ts.Style(FontSize="x")),
    ("Style", "a field set to None", T, lambda x: setattr(ts.Style(), "Face", None)),
    ("Style", "FontSize=10**400", O, lambda x: ts.Style(FontSize=HUGE)),
    ("Style", "a name that is no field, an int past a C int", OK, lambda x: ts.Style(Fontsize=3.0, Face=2**40)),
    ("BlockStyle", "Scale=None", T, lambda x: ts.BlockStyle(Scale=None)),
    ("BlockStyle", "one value too many", T, lambda x: ts.BlockStyle(*range(12))),
    ("Container", "Kind='x'", T, lambda x: ts.Container(Kind="x")),
    ("DocumentOptions", "CodeOverflow=1.5", T, lambda x: ts.DocumentOptions(CodeOverflow=1.5)),
]

# A valid call of every method and property of the two handles: what
# test_a_closed_handle_raises_value_error makes of each once its handle is
# closed. ONE LINE HERE TOO FOR A NEW METHOD of Paragraph or Document.
PARAGRAPH_CALLS = {
    "set_text": lambda p: p.set_text("abc"),
    "layout": lambda p: p.layout(100.0),
    "error": lambda p: p.error,
    "status": lambda p: p.status,
    "size": lambda p: p.size,
    "line_count": lambda p: p.line_count,
    "line": lambda p: p.line(0),
    "lines": lambda p: p.lines(),
    "formulas": lambda p: p.formulas(),
    "draw": lambda p: frame(lambda: p.draw(None, (0.0, 0.0))),
    "draw_to_buffers": lambda p: p.draw_to_buffers((0.0, 0.0), BOX),
    "hit_test": lambda p: p.hit_test((1.0, 1.0)),
    "select_range": lambda p: p.select_range((0.0, 0.0), (9.0, 9.0)),
    "range_rects": lambda p: p.range_rects((0, 2)),
    "caret_rect": lambda p: p.caret_rect(1),
    "range_text": lambda p: p.range_text((0, 2)),
}
DOCUMENT_CALLS = {
    "set_blocks": lambda d: d.set_blocks(blocks_with()),
    "replace_blocks": lambda d: d.replace_blocks(0, 0, blocks_with()),
    "append_text": lambda d: d.append_text("a"),
    "append_table_rows": lambda d: d.append_table_rows([["a"]]),
    "append_table_cells": lambda d: d.append_table_cells(["a"]),
    "set_table_columns": lambda d: d.set_table_columns(6, [(0, 50.0)]),
    "set_block_extent": lambda d: d.set_block_extent(3, 4.0),
    "set_options": lambda d: d.set_options(None),
    "block_count": lambda d: d.block_count,
    "style_count": lambda d: d.style_count,
    "container_count": lambda d: d.container_count,
    "layout": lambda d: d.layout(300.0),
    "layout_window": lambda d: d.layout_window(0.0, 100.0),
    "size": lambda d: d.size(),
    "stats": lambda d: d.stats(),
    "block_error": lambda d: d.block_error(0),
    "draw": lambda d: frame(lambda: d.draw(None, (0.0, 0.0))),
    "draw_to_buffers": lambda d: d.draw_to_buffers((0.0, 0.0), BOX),
    "view": lambda d: frame(lambda: d.view("view")),
    "hit_test": lambda d: d.hit_test((1.0, 1.0)),
    "select_range": lambda d: d.select_range((0.0, 0.0), (9.0, 9.0)),
    "range_rects": lambda d: d.range_rects(SEL),
    "caret_rect": lambda d: d.caret_rect((1, 1)),
    "range_text": lambda d: d.range_text(SEL),
    "block_geometry": lambda d: d.block_geometry(0),
    "block_line": lambda d: d.block_line(1, 0),
    "table_geometry": lambda d: d.table_geometry(6),
    "table_column": lambda d: d.table_column(6, 0),
    "table_row": lambda d: d.table_row(6, 0),
    "set_columns": lambda d: d.set_columns(None),
    "column_count": lambda d: d.column_count(),
    "column": lambda d: d.column(0),
    "column_pos": lambda d: d.column_pos((0.0, 0.0)),
    "table_cell": lambda d: d.table_cell((6, 1)),
    "table_position": lambda d: d.table_position(6, 0, 0),
}
# What a closed handle still answers, and never reaches the library for.
CLOSED_ANSWERS = {"close", "closed", "from_blocks", "pack_marks"}


class HostileInput(unittest.TestCase):
    """docs/python-errors.md, row by row: what leaves each public call of the
    package on input a host controls, and that nothing but `imtypeset.Error`,
    TypeError, ValueError (UnicodeEncodeError is one) and OverflowError does."""

    DOCUMENT = os.path.join(REPO, "docs", "python-errors.md")

    @staticmethod
    def generated():
        """Rows for the types: a NamedTuple refuses one value too many, an enum
        a value it has no member for, whatever else a table above says of it."""
        rows = []
        for name in ts.__all__:
            thing = getattr(ts, name)
            if not isinstance(thing, type):
                continue
            if issubclass(thing, tuple) and hasattr(thing, "_fields"):
                rows.append((name, "one value too many", T,
                             lambda x, thing=thing: thing(*range(len(thing._fields) + 1))))
            elif issubclass(thing, enum.Enum):
                rows.append((name, "'x'", V, lambda x, thing=thing: thing("x")))
        return rows

    def rows(self):
        return TABLE + self.generated()

    def public_names(self):
        names = set(ts.__all__) | {f"demo.{name}" for name in demo.__all__}
        for handle in (ts.Paragraph, ts.Document):
            names |= {
                f"{handle.__name__}.{name}" for name in vars(handle) if not name.startswith("_")
            }
        return names

    def tearDown(self):
        ts.set_text_cache_limits()

    def test_each_row_raises_what_the_table_says_and_nothing_else(self):
        for call, given, expected, body in self.rows():
            with self.subTest(call=call, given=given):
                made = Hostile()
                try:
                    body(made)
                    left = None
                except Exception as exc:  # noqa: BLE001 - the class is what is compared
                    left = type(exc)
                finally:
                    made.close()
                self.assertIs(left, expected)

    def test_no_row_expects_anything_but_the_four(self):
        allowed = {OK, T, V, O, U, E}
        for call, given, expected, _ in self.rows():
            if call == "Error":  # Constructed by a host, which no host does.
                continue
            self.assertIn(expected, allowed, f"{call}: {given}")

    def test_every_public_name_is_in_the_table(self):
        missing = self.public_names() - {call for call, _, _, _ in self.rows()}
        self.assertEqual(sorted(missing), [], "add a row to TABLE for each (and to docs/python-errors.md)")

    def test_the_table_names_nothing_that_is_not_public(self):
        self.assertEqual(sorted({call for call, _, _, _ in self.rows()} - self.public_names()), [])

    def test_the_document_names_every_call_of_the_table(self):
        with open(self.DOCUMENT, encoding="utf-8") as stream:
            text = stream.read()
        missing = sorted({call for call, _, _, _ in self.rows() if f"`{call}`" not in text})
        self.assertEqual(missing, [], "docs/python-errors.md names each call as `name`")

    def test_a_closed_handle_raises_value_error(self):
        for handle, calls in ((ts.Paragraph, PARAGRAPH_CALLS), (ts.Document, DOCUMENT_CALLS)):
            names = {name for name in vars(handle) if not name.startswith("_")} - CLOSED_ANSWERS
            self.assertEqual(sorted(names - set(calls)), [], f"{handle.__name__}: add a valid call")
            self.assertEqual(sorted(set(calls) - names), [])
            for how in ("open", "closed", "context destroyed"):
                for name, call in calls.items():
                    with self.subTest(handle=handle.__name__, call=name, how=how):
                        made = Hostile()
                        self.addCleanup(made.close)
                        if handle is ts.Paragraph:
                            thing = made.par
                        elif name == "append_text":
                            thing = made.prose
                        else:
                            thing = made.doc
                        if how == "open":
                            call(thing)  # The call is a valid one: it is the handle that is refused below.
                            continue
                        if how == "closed":
                            thing.close()
                        else:
                            # A context of its own, destroyed under the handle.
                            held = ts.current_context()
                            other = ts.create_context()
                            ts.set_current_context(other)
                            try:
                                thing = ts.Paragraph("abc") if handle is ts.Paragraph else ts.Document()
                            finally:
                                ts.destroy_context(other)
                                ts.set_current_context(held)
                        self.assertTrue(thing.closed)
                        with self.assertRaises(ValueError) as caught:
                            call(thing)
                        self.assertIs(type(caught.exception), ValueError)

    def test_a_number_past_a_c_int_names_nothing_and_no_other_block(self):
        made = Hostile()
        self.addCleanup(made.close)
        document = made.doc
        nothing = document.block_geometry(10**6)
        self.assertFalse(nothing.laid_out)
        self.assertNotEqual(document.block_geometry(1), nothing)
        # 2**32 + 1 is block 1 in its low 32 bits.
        for block in (2**32 + 1, 2**40, 2**64 + 1, -(2**32) + 1):
            self.assertEqual(document.block_geometry(block), nothing, block)
            self.assertEqual(document.block_error(block).code, ts.ErrorCode.None_)
            self.assertEqual(document.caret_rect((block, 0)), (0.0, 0.0, 0.0, 0.0))
        self.assertEqual(document.table_geometry(2**32 + 6).row_count, 0)
        self.assertEqual(document.table_geometry(6).row_count, 2)
        self.assertFalse(document.column(2**32).exists)
        self.assertTrue(document.column(0).exists)
        # A range to a block past the last is a range to the end.
        whole = document.range_text(((0, 0), (10**6, 0)))
        self.assertEqual(document.range_text(((0, 0), (2**32 + 1, 2))), whole)
        self.assertEqual(document.range_text(((0, 0), (len(document) - 1, 2**32 + 1))), whole)
        # A role.
        self.assertIsNotNone(ts.get_font(ts.Face.Regular))
        self.assertIsNone(ts.get_font(2**32 + ts.Face.Regular))
        self.assertIsNone(ts.font_atlas_index(2**32 + ts.Face.Regular))
        self.assertFalse(ts.face_has_glyph(2**32 + ts.Face.Regular, "a"))
        self.assertFalse(ts.remove_font(2**32 + ts.Face.Regular))
        self.assertIsNotNone(ts.get_font(ts.Face.Regular))
        with self.assertRaises(ts.Error) as caught:
            ts.add_font(2**32 + ts.Face.Regular, ROBOTO)
        self.assertEqual(caught.exception.code, ts.ErrorCode.InvalidArgument)
        self.assertEqual(demo.section_name(2**32), "")
        self.assertEqual(demo.section_name(0), "Paragraph")
        # A position in a paragraph: past the end is the end.
        for text in (PLAIN, ACCENTED):
            paragraph = made.paragraph(text)
            self.assertEqual(paragraph.range_text((0, 2**32 + 2)), text)
            self.assertEqual(paragraph.range_text((-(2**32) + 2, 2**40)), text)
            self.assertEqual(paragraph.caret_rect(2**32 + 1), paragraph.caret_rect(len(text)))
            self.assertNotEqual(paragraph.caret_rect(1), paragraph.caret_rect(len(text)))
        self.assertEqual(ts.codepoint_to_byte_offset("aéb", 2**32 + 1), 4)
        self.assertEqual(ts.byte_to_codepoint_offset("aéb", 2**32 + 1), 3)

    def test_what_a_description_cannot_hold_is_brought_to_what_it_can(self):
        made = Hostile()
        self.addCleanup(made.close)
        document = made.doc
        # Flags and a tag keep their low 32 bits; a colour its 32.
        run = ts.Run(0, 5, ts.Face.Bold, 1.0, 2**40 + RED, 2**40, 2**32 + 7)
        self.assertIsInstance(document.set_blocks([ts.Block(text="né " + PLAIN, runs=[run])]), bool)
        document.layout(400.0)
        self.assertEqual(document.block_count, 1)
        self.assertEqual(document.hit_test((3.0, 3.0)).tag, 7)
        self.assertEqual(document.range_text(((0, 0), (0, 5))), "né Pl")
        # A level, a style, an indent that no block has: the library's answer, not an exception.
        self.assertIsInstance(
            document.set_blocks([ts.Block(text="abc", style=2**40, level=2**40, left_indent=1e300)]), bool
        )
        self.assertIn(document.edit_error.code, (ts.ErrorCode.None_, ts.ErrorCode.DescriptionCut))
        self.assertEqual(document.range_text(((0, 0), (0, 3))), "abc")
        # And what was clamped is laid out and drawn as anything else is.
        extreme = ts.Run(-(2**40), 2**40, 2**40, 1e300, 2**40, 2**40, 2**40)
        blocks = [
            ts.Block(2**40, "né abc", [extreme], "", 2**40, 1e300, 2**40, 2**40, 2**40, 1e300, 2**40, 2**40),
            ts.Block(text="plain", quote_depth=2**40, level=-(2**40), item=2**40, left_indent=-1e300),
            ts.Block(ts.BlockKind.Table, rows=[[ts.Cell("né", [extreme]), "b"]],
                     columns=[(2**40, 1e300, -1e300), (-(2**40), NAN, INF)]),
        ]
        self.assertFalse(document.set_blocks(blocks))
        self.assertEqual(document.edit_error.code, ts.ErrorCode.DescriptionCut)
        self.assertIn("Kind", document.edit_error.message)
        document.layout(300.0)
        self.assertIsInstance(document.layout_window(0.0, 1000.0), bool)
        vertices, _ = document.draw_to_buffers((0.0, 0.0), BOX)
        self.assertGreater(len(vertices), 0)
        self.assertEqual(document.range_text(((0, 0), (2, 2**40)), "|"), "né abc|plain|né\tb")
        marks = document.pack_marks([(((2**40, 0), (0, 2**40)), -1)])
        self.assertEqual(marks.count, 1)
        self.assertEqual(marks.array[0].Range.Start.Block, 2**31 - 1)
        self.assertEqual(marks.array[0].Color, 0xFFFFFFFF)
        self.assertIsInstance(document.draw_to_buffers((0.0, 0.0), BOX, None, marks), tuple)

    def test_what_would_end_the_process_is_refused_first(self):
        made = Hostile()
        self.addCleanup(made.close)
        # An empty ID is an assertion of Dear ImGui's in BeginChild: the
        # library gives a view without one its own.
        for str_id in ("", "\0", "\0view", "##"):
            self.assertTrue(frame(lambda: made.doc.view(str_id))[0])
        # The demo outside a frame is an assertion in Begin, or a window that
        # is not there: the library draws nothing.
        self.assertIsNone(ts.show_demo_window())
        self.assertIsNone(demo.show_demo_window())
        for section in demo.sections():
            demo.show_demo_section(section)
        # ctypes hands over the address of a str's characters as an address.
        for address in ("x", b"x", bytearray(b"x"), 1.5, [], object()):
            with self.assertRaises(TypeError):
                frame(lambda: made.par.draw(address, (0.0, 0.0)))
            with self.assertRaises(TypeError):
                frame(lambda: made.doc.draw(address, (0.0, 0.0)))
            with self.assertRaises(TypeError):
                ts.set_current_context(address)
            with self.assertRaises(TypeError):
                ts.destroy_context(address)
        self.assertIsNotNone(ts.current_context())
        frame(lambda: made.par.draw(ctypes.c_void_p(host.get_window_draw_list()), (0.0, 0.0)))
        # bytes(n) is n zero bytes.
        for source in (5, 2**31, 2**40, True):
            with self.assertRaises(TypeError):
                ts.add_font(ts.Face.Italic, source)
        self.assertIsNone(ts.get_font(ts.Face.Italic))

    def test_the_modules_under_the_package_raise_import_error_and_these(self):
        # Not for a host to call; reached by `import imtypeset`.
        self.assertTrue(os.path.isfile(_capi.library_path()))
        for path, expected in ((None, ImportError), ("no-such-library.so", ImportError), (b"x", ImportError),
                               (5, TypeError), ("a\0b", ValueError), (LONE, UnicodeEncodeError)):
            with self.subTest(path=path):
                with self.assertRaises(expected) as caught:
                    _capi.load(path)
                self.assertIs(type(caught.exception), expected)


if __name__ == "__main__":
    unittest.main(verbosity=2 if "-v" in sys.argv else 1)
