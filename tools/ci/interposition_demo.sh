#!/bin/sh
# Measures what opening Dear ImGui Bundle's module with RTLD_GLOBAL does to a
# library loaded afterwards: the reason python/host/build_bundle_library.sh
# does not bind by lookup on Linux (docs/sidecar.md).
#
#   tools/ci/interposition_demo.sh [python]     (Linux; needs cc and libfreetype.so.6)
#
# A four-line library that asks FreeType for its version, linked against the
# system's libfreetype, is loaded twice in fresh interpreters: once as is, once
# after the wheel's module (which exports its own FreeType's functions) has
# been made global. Prints the version each time. Different numbers mean the
# second library's FreeType calls went to the wheel's FreeType.
set -eu
repo="$(cd "$(dirname "$0")/../.." && pwd)"
python="${1:-python3}"
work="${IMTYPESET_DEMO_OUT:-$repo/build/interposition-demo}"
mkdir -p "$work"
cat > "$work/user.c" <<'SRC'
int FT_Init_FreeType(void** library);
void FT_Library_Version(void* library, int* major, int* minor, int* patch);
int freetype_version(int* major, int* minor, int* patch)
{
    void* library = 0;
    if (FT_Init_FreeType(&library) != 0)
        return 1;
    FT_Library_Version(library, major, minor, patch);
    return 0;
}
SRC
"${CC:-cc}" -shared -fPIC -o "$work/libuser.so" "$work/user.c" -l:libfreetype.so.6
for promote in 0 1; do
    PYTHONDONTWRITEBYTECODE=1 "$python" -P -B -c '
import ctypes, sys
import imgui_bundle
promote = sys.argv[2] == "1"
if promote:
    ctypes.CDLL(imgui_bundle._imgui_bundle.__file__, mode=ctypes.RTLD_GLOBAL)
user = ctypes.CDLL(sys.argv[1])
major, minor, patch = ctypes.c_int(), ctypes.c_int(), ctypes.c_int()
status = user.freetype_version(ctypes.byref(major), ctypes.byref(minor), ctypes.byref(patch))
print(("wheel module made global first: " if promote else "wheel module as Python loads it: ")
      + f"a library linked against the system FreeType gets {major.value}.{minor.value}.{patch.value}"
      + f" (status {status})")
' "$work/libuser.so" "$promote"
done
