"""Makes the REAL pptmstr import three modules from this directory instead of
from its own tree, so that a change which cannot be written into ../pptmstr can
still be run in the application itself.

Put this directory on PYTHONPATH and Python imports this file at startup (it is
what `site` looks for under that name). Nothing else is needed:

    PYTHONPATH=<imtypeset>/python/spike_bundle/pptmstr_overlay:<imtypeset>/python \
    IMTYPESET_LIBRARY=<imtypeset>/build/.../libimtypeset_bundle.so \
        ../pptmstr/.venv/bin/python -P -B -m pptmstr --fake

    pptmstr.theme           theme.py      pptmstr's, plus two lines at the end of load_fonts
    pptmstr.ui.rich_pane    rich_pane.py  pptmstr's, plus the imtypeset path
    pptmstr.ui.typeset      typeset.py    new

Every other pptmstr module is the application's own. PPTMSTR_OVERLAY=0 turns the
redirection off (the application then runs exactly as it is on disk).
"""

import importlib.abc
import importlib.util
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_REDIRECT = {
    "pptmstr.theme": os.path.join(_HERE, "theme.py"),
    "pptmstr.ui.rich_pane": os.path.join(_HERE, "rich_pane.py"),
    "pptmstr.ui.typeset": os.path.join(_HERE, "typeset.py"),
}


class _Overlay(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        where = _REDIRECT.get(fullname)
        if where is None:
            return None
        return importlib.util.spec_from_file_location(fullname, where)


def install() -> None:
    if not any(isinstance(finder, _Overlay) for finder in sys.meta_path):
        sys.meta_path.insert(0, _Overlay())


if os.environ.get("PPTMSTR_OVERLAY", "1") != "0":
    install()
