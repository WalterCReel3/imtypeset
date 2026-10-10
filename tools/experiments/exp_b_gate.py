"""Experiment B, first half: load the imtypeset experiment library into a
Python process next to the installed imgui_bundle, and ask ImGui itself whether
the two were compiled against the same structs. Nothing else is called.

    PYTHONDONTWRITEBYTECODE=1 ../imgui-adv-text/.venv/bin/python -B \
        tools/experiments/exp_b_gate.py deps|docking

Runs each way of loading in its own subprocess and prints one JSON object per
attempt. Writes nothing. Build the library first: tools/experiments/build_expb.sh.
"""

from __future__ import annotations

import ctypes
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

# How the library is brought into the process.
LOADS = [
    "plain_before_import",  # CDLL(lib) with imgui_bundle never imported
    "plain_after_import",  # import imgui_bundle; CDLL(lib). Python loads extensions RTLD_LOCAL.
    "global_after_import",  # import imgui_bundle; CDLL(bundle .so, RTLD_GLOBAL); CDLL(lib)
    "needed_before_import",  # CDLL(lib linked with the bundle .so as DT_NEEDED), no import
    "needed_after_import",  # import imgui_bundle; CDLL(that lib)
]


def lib_path(variant: str, needed: bool) -> str:
    name = "libimtypeset_expb_needed.so" if needed else "libimtypeset_expb.so"
    return os.path.join(ROOT, "build", "bundle-research", variant, name)


def load(variant: str, how: str):
    """The ctypes library, loaded the way `how` says. Raises OSError if it cannot be."""
    if how.endswith("after_import"):
        import imgui_bundle._imgui_bundle as native

        if how == "global_after_import":
            ctypes.CDLL(native.__file__, mode=ctypes.RTLD_GLOBAL)
    return ctypes.CDLL(lib_path(variant, how.startswith("needed")), mode=os.RTLD_NOW)


def text_call(function, *args) -> tuple[int, str]:
    buffer = ctypes.create_string_buffer(1 << 16)
    function.restype = ctypes.c_int
    code = function(*args, buffer, len(buffer))
    return code, buffer.value.decode("utf-8", "replace")


def gate(lib) -> dict:
    """ImGui's DebugCheckVersionAndDataLayout through the boundary, then the sizes it does not cover."""
    out: dict = {}
    buffer = ctypes.create_string_buffer(1 << 16)
    lib.expb_abi.restype = None
    lib.expb_abi(buffer, len(buffer))
    out["compiled_against"] = json.loads(buffer.value)
    code, message = text_call(lib.expb_check_layout)
    out["DebugCheckVersionAndDataLayout"] = {"result": code, "message": message}
    out["layout_ok"] = code == 1
    if code != 1:
        return out
    code, message = text_call(lib.expb_probe_sizes)
    out["probe_sizes_code"] = code
    try:
        probe = json.loads(message)
    except ValueError:
        out["probe_sizes"] = message
        out["layout_ok"] = False
        return out
    out["probe_sizes"] = probe
    # [bytes the bundle's constructor wrote, sizeof in this library's headers].
    # ImDrawData's constructor does not clear the whole object, so only "not
    # more than ours" can be asked of it.
    mismatched = [
        name
        for name, pair in probe.items()
        if isinstance(pair, list) and (pair[0] > pair[1] or (name != "ImDrawData" and pair[0] != pair[1]))
    ]
    out["probe_size_mismatches"] = mismatched
    if mismatched:
        out["layout_ok"] = False
    return out


def child(variant: str, how: str) -> dict:
    out: dict = {"variant": variant, "load": how}
    try:
        lib = load(variant, how)
    except OSError as exc:
        out["loaded"] = False
        out["error"] = str(exc).replace(ROOT, ".")[:300]
        return out
    out["loaded"] = True
    if "imgui_bundle" in sys.modules:
        from imgui_bundle import imgui

        out["bundle_imgui_version"] = imgui.get_version()
    out.update(gate(lib))
    return out


def main(argv: list[str]) -> int:
    if len(argv) == 3 and argv[0] == "--child":
        print(json.dumps(child(argv[1], argv[2])))
        return 0
    variant = argv[0] if argv else "docking"
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
    for how in LOADS:
        proc = subprocess.run(
            [sys.executable, "-B", os.path.abspath(__file__), "--child", variant, how],
            capture_output=True,
            text=True,
            env=env,
            cwd=os.environ.get("TMPDIR", "/tmp"),
            timeout=120,
        )
        lines = proc.stdout.strip().splitlines()
        try:
            result = json.loads(lines[-1]) if lines else {"variant": variant, "load": how}
        except ValueError:
            result = {"variant": variant, "load": how, "stdout": proc.stdout[-300:]}
        result["exit"] = proc.returncode
        if proc.returncode != 0:
            result["stderr_tail"] = proc.stderr.strip().splitlines()[-4:]
        print(json.dumps(result))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
