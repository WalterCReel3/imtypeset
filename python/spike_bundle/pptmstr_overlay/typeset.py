"""
imtypeset in pptmstr: whether it is there, its context, its faces.

Everything else in pptmstr asks ``module()`` and draws the way it always has when
the answer is ``None``, so an installation without the library, or with one the
wheel no longer accepts, is today's pptmstr and not a broken one.

The library is a sidecar built for ONE installed imgui_bundle wheel. It is looked
for only when ``IMTYPESET_LIBRARY`` names it, and ``import imtypeset`` checks it
against the wheel that is running (its gate: Dear ImGui's version and layout check,
then a probe of every field the library reads). After a wheel upgrade that import
raises; ``reason()`` then holds the sentence and the pane says it once.
"""

from __future__ import annotations

import ctypes
import importlib
import os
from pathlib import Path
from typing import Any

_ts: Any = None
_reason = "IMTYPESET_LIBRARY is not set"
_window_draw_list: Any = None


def load_fonts() -> None:
    """
    Make the imtypeset context and add its faces. Called at the end of
    ``theme.load_fonts``, which is hello_imgui's ``load_additional_fonts``: the
    runner empties the atlas before that callback (and again on a DPI change), so
    a face added anywhere earlier is gone by the first frame. pptmstr's own faces
    stay first; ``Fonts[0]`` is still Inconsolata.

    The faces are pptmstr's: Inconsolata Medium for prose and code, the vendored
    Bold, and the wheel's Latin Modern Math for formulas. There is no italic, as
    in ``theme.Face``: emphasis stays a colour.
    """
    global _ts, _reason, _window_draw_list
    _ts = None
    if not os.environ.get("IMTYPESET_LIBRARY"):
        return
    try:
        import imgui_bundle
        import imtypeset as ts

        from ..theme import _ASSETS

        if ts.current_context() is None:
            ts.create_context()
        wheel = Path(imgui_bundle.__file__).resolve().parent / "assets" / "fonts"
        medium = str(wheel / "Inconsolata-Medium.ttf")
        ts.add_font(ts.Face.Regular, medium)
        ts.add_font(ts.Face.Code, medium)
        ts.add_font(ts.Face.Bold, str(_ASSETS / "fonts" / "Inconsolata-Bold.ttf"))
        math = wheel / "latex" / "latinmodern-math.otf"
        if math.exists():
            ts.add_font(ts.Face.Math, str(math))
        # The current window's ImDrawList*, which is what the library draws into.
        # The binding has no way to hand over the address of its own object.
        binary = ctypes.CDLL(importlib.import_module("imgui_bundle._imgui_bundle").__file__)
        _window_draw_list = binary._ZN5ImGui17GetWindowDrawListEv
        _window_draw_list.restype = ctypes.c_void_p
        _window_draw_list.argtypes = []
    except Exception as exc:  # noqa: BLE001 -- any failure here means "draw as before"
        _reason = f"{type(exc).__name__}: {exc}"
        return
    _reason = ""
    _ts = ts


def module() -> Any:
    """The imtypeset module, or ``None`` when panes should draw as they did."""
    return _ts


def reason() -> str:
    """Why ``module()`` is ``None``; empty when it is not."""
    return _reason


def window_draw_list() -> int:
    """The address of the current window's draw list."""
    return int(_window_draw_list())


def disable(why: str) -> None:
    """Stop using the library for the rest of the run, and remember why."""
    global _ts, _reason
    _ts = None
    _reason = why
