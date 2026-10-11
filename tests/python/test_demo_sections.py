"""python/imtypeset/demo.py against the library, headless.

    IMTYPESET_LIBRARY=build/py-host/libimtypeset_pyhost.so SDL_VIDEODRIVER=dummy \
        python3 -P -B tests/python/test_demo_sections.py [-v]

Standard library only. Exits 77 (ctest: skipped) when there is no library to
test.
"""

import os
import resource
import sys
import unittest

# A runaway allocation must end this process and not the session. A sanitised
# library cannot run under an address-space limit; its guard is
# ASAN_OPTIONS=hard_rss_limit_mb, which whoever preloads the runtime sets.
_LIMIT = 6_000_000 * 1024
_soft, _hard = resource.getrlimit(resource.RLIMIT_AS)
if "ASAN_OPTIONS" not in os.environ and (_soft == resource.RLIM_INFINITY or _soft > _LIMIT):
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
from imtypeset import demo  # noqa: E402

FONTS = {
    ts.Face.Regular: "Roboto-Regular.ttf",
    ts.Face.Bold: "Roboto-Bold.ttf",
    ts.Face.Italic: "Roboto-RegularItalic.ttf",
    ts.Face.BoldItalic: "Roboto-BoldItalic.ttf",
    ts.Face.Code: "Inconsolata-Medium.ttf",
    ts.Face.Math: "latinmodern-math.otf",
}
# The titles the demo window's headers show, by the value this module gives each.
TITLES = {
    demo.DemoSection.Paragraph: "Paragraph",
    demo.DemoSection.Faces: "Faces and sizes",
    demo.DemoSection.Specimen: "Specimen",
    demo.DemoSection.Math: "Math",
    demo.DemoSection.Formulas: "Formula gallery",
    demo.DemoSection.Selection: "Selection",
    demo.DemoSection.Inspector: "Inspector",
    demo.DemoSection.Stress: "Stress",
    demo.DemoSection.Fonts: "Fonts",
    demo.DemoSection.KnownLimits: "Known limits",
    demo.DemoSection.Document: "Document",
}
window = None


def setUpModule():
    global window
    window = host.Window("test_demo_sections", 900, 700, dpi_scale=1.0)
    ts.create_context()
    for face, name in FONTS.items():
        ts.add_font(face, os.path.join(FONT_DIR, name))


def tearDownModule():
    window.close()


def frames(body, count):
    for _ in range(count):
        assert window.begin_frame()
        try:
            body()
        finally:
            window.end_frame()


class DemoSections(unittest.TestCase):
    def test_the_enum_is_the_library_s_sections_with_their_titles(self):
        self.assertEqual(demo.sections(), tuple(demo.DemoSection))
        self.assertEqual({s: demo.section_name(s) for s in demo.sections()}, TITLES)
        self.assertEqual(demo.section_name(-1), "")
        self.assertEqual(demo.section_name(len(demo.DemoSection)), "")

    def test_each_section_shown_alone_draws_through_the_library_and_settles(self):
        for section in demo.sections():
            with self.subTest(section=section.name):
                frames(lambda: demo.show_demo_section(section), 4)
                before = ts.stats()
                frames(lambda: demo.show_demo_section(section), 3)
                after = ts.stats()
                self.assertGreater(after.DrawCalls, before.DrawCalls)
                self.assertEqual(after.Layouts, before.Layouts)

    def test_a_value_that_is_not_a_section_draws_nothing(self):
        before = ts.stats()
        frames(lambda: demo.show_demo_section(len(demo.DemoSection)), 2)
        frames(lambda: demo.show_demo_section(-3), 2)
        self.assertEqual(ts.stats().DrawCalls, before.DrawCalls)

    def test_the_window_is_the_package_s_own(self):
        self.assertIs(demo.show_demo_window, ts.show_demo_window)
        results = []
        frames(lambda: results.append(demo.show_demo_window(True)), 2)
        self.assertEqual(results, [True, True])


if __name__ == "__main__":
    unittest.main(verbosity=2 if "-v" in sys.argv else 1)
