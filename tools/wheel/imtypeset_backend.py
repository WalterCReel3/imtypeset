"""The build backend pyproject.toml names (PEP 517; tools/wheel/imtypeset_backend.py), so that

    pip wheel --no-deps .        and        pip install git+https://github.com/WalterCReel3/imtypeset@<tag>

work: a thin front for tools/wheel/build_wheel.py, which needs nothing
installed. It builds the wheel for the imgui-bundle of the environment pip is
installing into (IMTYPESET_WHEEL_PYTHON names another interpreter), so that
environment must already hold imgui-bundle, and the machine a C++ compiler
and, for the wheel's own headers, the network. That is the route for a
developer. An application's user gets a prebuilt wheel
(docs/sidecar.md, section 8).

    IMTYPESET_BUNDLE_HEADERS   fork (default) | stand-in, as for the build script
    IMTYPESET_WHEEL_PYTHON     the interpreter whose imgui_bundle to build for

There is no source distribution: `build_sdist` is not offered, because the
sources of a wheel are this repository at a tag.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_wheel  # noqa: E402


def get_requires_for_build_wheel(config_settings=None):
    return []


def build_wheel(wheel_directory, config_settings=None, metadata_directory=None):  # noqa: F811
    builder = sys.modules["build_wheel"]
    try:
        path = builder.build(
            os.environ.get("IMTYPESET_WHEEL_PYTHON") or sys.executable,
            wheel_directory,
            headers=os.environ.get("IMTYPESET_BUNDLE_HEADERS", "fork"),
            attested_where="built on install, through pip",
            log=lambda line: print(line, file=sys.stderr),
        )
    except builder.Failed as failure:
        raise SystemExit(f"imtypeset: no wheel was built: {failure}") from None
    return os.path.basename(path)
