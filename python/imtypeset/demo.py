"""The library's demo window, and its sections one at a time.

    import imtypeset
    from imtypeset import demo

    demo.show_demo_window()                       # the whole window, every frame
    still_open = demo.show_demo_window(True)      # with a close button

    for section in demo.sections():               # a host with pages of its own
        if imgui.begin_tab_item(demo.section_name(section))[0]:
            demo.show_demo_section(section)
            imgui.end_tab_item()

Called between the host's new frame and its render, with an imtypeset context
current, as every drawing call of the package is. The demo adds no font: it
shows the faces the host added.

A section shown alone submits its contents into whatever window is current. It
sets its text at the size Dear ImGui's own text has there, so the host sizes it
by pushing a font size around the call. What it remembers between frames it
remembers per imtypeset context, apart from what the demo window remembers.
"""

from __future__ import annotations

import enum
from typing import Tuple

from . import Error, last_error, show_demo_window
from . import library as _library

__all__ = [
    "DemoSection",
    "section_name",
    "sections",
    "show_demo_section",
    "show_demo_window",
]


class DemoSection(enum.IntEnum):
    """ImTypesetDemoSection_. The values are stable; new ones are appended."""

    Paragraph = 0
    Faces = 1
    Specimen = 2
    Math = 3
    Formulas = 4
    Selection = 5
    Inspector = 6
    Stress = 7
    Fonts = 8
    KnownLimits = 9
    Document = 10


_lib = _library()


def sections() -> Tuple[DemoSection, ...]:
    """The sections the loaded library has, in the demo window's order: the
    members of `DemoSection` it gives a title to. (A library built with
    IMTYPESET_DISABLE_DEMO has none.)"""
    return tuple(member for member in DemoSection if section_name(member))


def section_name(section: int) -> str:
    """The section's title as the demo window's header shows it; "" for a value
    that is not a section."""
    return (_lib.ImTypeset_GetDemoSectionName(int(section)) or b"").decode("utf-8", "replace")


def show_demo_section(section: int) -> None:
    """ImTypeset::ShowDemoSection: the contents of one section of the demo
    window, into the current window. A value that is not a section draws
    nothing."""
    if not _lib.ImTypeset_ShowDemoSection(int(section)):
        # A C++ exception caught at the library's boundary: an assertion of
        # Dear ImGui's, in a build where those throw.
        raise Error(last_error(), "show_demo_section")
