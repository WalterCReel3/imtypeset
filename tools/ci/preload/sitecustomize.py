"""Loads Dear ImGui Bundle's extension module before anything else in the
interpreter, for a sidecar library that finds Dear ImGui by lookup at load
(python/host/build_bundle_library.sh, IMTYPESET_BUNDLE_BIND=lookup).

Python imports a module named sitecustomize at startup when one is on its
path; this directory is put on PYTHONPATH for that and holds nothing else.
It acts only when IMTYPESET_PRELOAD_BUNDLE is set:

    import    `import imgui_bundle`, and no more: the module is loaded the way
              Python loads any extension module (sys.getdlopenflags()).
    1         that, and then the module is opened again with RTLD_GLOBAL, which
              makes the symbols of an already loaded object visible to objects
              loaded later.

This is a harness: python/imtypeset does not do either itself, and a library
bound by lookup cannot be imported without one of them (docs/sidecar.md says
which, per platform, once a run has shown it). An interpreter started with -I
or -S does not read it.
"""

import os as _os

_mode = _os.environ.get("IMTYPESET_PRELOAD_BUNDLE", "")
if _mode in ("1", "import"):
    try:
        import imgui_bundle as _imgui_bundle

        if _mode == "1":
            import ctypes as _ctypes

            _ctypes.CDLL(_imgui_bundle._imgui_bundle.__file__, mode=_ctypes.RTLD_GLOBAL)
    except Exception as _exc:  # the interpreter must still start, and say why
        import sys as _sys

        print(f"tools/ci/preload: {type(_exc).__name__}: {_exc}", file=_sys.stderr)
