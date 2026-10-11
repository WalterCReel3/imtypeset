#!/bin/sh
# Builds imtypeset as a shared library for use INSIDE an installed, unmodified
# Dear ImGui Bundle wheel: the library holds no Dear ImGui; its 46 ImGui symbols
# are left undefined and resolve against the wheel's own module, so it draws in
# the application's context, atlas and draw lists. python/imtypeset then reports
# host "imgui_bundle" (examples/python/bundle_host.py).
#
#   python/host/build_bundle_library.sh [bundle-checkout] [python]
#
#   bundle-checkout  default ../imgui_bundle: a clone of Dear ImGui Bundle. Read
#                    only (git show, and the fork's headers if they are there).
#   python           default ../imgui-adv-text/.venv/bin/python: the interpreter
#                    whose installed imgui_bundle wheel the library is for.
#   IMTYPESET_BUNDLE_OUT   output directory, default build/py-host-bundle.
#   IMTYPESET_IMGUI_CLONE  a clone of ocornut/imgui for the stand-in headers,
#                          default ../imgui.
#   CXX                    the compiler, default g++.
#
# Writes <out>/libimtypeset_bundle.so, <out>/dist (the generated sources),
# <out>/obj and <out>/headers; nothing else. One library per interpreter: it is
# linked against that interpreter's wheel and refuses any other at import.
#
# WHICH HEADERS. The wheel's ImGui is a fork (github.com/pthom/imgui) and the
# wheel ships no headers. Two cases:
#
#   fork      <bundle-checkout>/external/imgui/imgui/imgui.h exists AND that
#             submodule is at the commit the wheel's release tag pins. The
#             library is compiled against it and says "fork:<commit>".
#   stand-in  otherwise: upstream Dear ImGui's docking tag of the same version,
#             read from IMTYPESET_IMGUI_CLONE's git objects. The library says
#             "stand-in:...". Dear ImGui's own layout check then refuses
#             sizeof(ImGuiStyle) (the fork adds a field), and python/imtypeset
#             accepts the library only because its field probe passes
#             (python/imtypeset/_bundle.py).
#
# Either way the script ends by running the import gate in a child interpreter
# and fails if the gate refuses.
set -eu

repo="$(cd "$(dirname "$0")/../.." && pwd)"
bundle="${1:-$repo/../imgui_bundle}"
python="${2:-$repo/../imgui-adv-text/.venv/bin/python}"
out="${IMTYPESET_BUNDLE_OUT:-$repo/build/py-host-bundle}"
imgui_clone="${IMTYPESET_IMGUI_CLONE:-$repo/../imgui}"
fork="$bundle/external/imgui/imgui"

facts="$(PYTHONDONTWRITEBYTECODE=1 "$python" -P -B -c '
import imgui_bundle, os
from imgui_bundle import imgui
print(imgui_bundle.__version__)
print(imgui.get_version())
print(imgui_bundle._imgui_bundle.__file__)')"
wheel_version="$(echo "$facts" | sed -n 1p)"
imgui_version="$(echo "$facts" | sed -n 2p)"
bundle_so="$(echo "$facts" | sed -n 3p)"
bundle_dir="$(dirname "$bundle_so")"
echo "wheel: imgui_bundle $wheel_version, Dear ImGui $imgui_version, $bundle_so"

tag="v$wheel_version"
want="$(git -C "$bundle" ls-tree "$tag" external/imgui/imgui 2>/dev/null | awk '{print $3}')"
if [ -z "$want" ]; then
    echo "build_bundle_library.sh: $bundle has no tag $tag; cannot tell which fork commit the wheel pins" >&2
    exit 2
fi
mkdir -p "$out/obj" "$out/headers"
out="$(cd "$out" && pwd)" # Absolute: the compiler is handed a header inside it by name.
git -C "$bundle" show "$tag:imgui_bundle_cmake/imgui_bundle_config.h" > "$out/headers/imgui_bundle_config.h"

have=""
[ -f "$fork/imgui.h" ] && have="$(git -C "$fork" rev-parse HEAD 2>/dev/null || echo unknown)"
if [ "$have" = "$want" ]; then
    inc="$fork"
    label="fork:$want"
    stand_in_define=""
else
    stand_in="v$imgui_version-docking"
    if ! git --git-dir="$imgui_clone/.git" rev-parse -q --verify "$stand_in^{commit}" >/dev/null 2>&1; then
        echo "build_bundle_library.sh: neither the fork at $want nor $stand_in in $imgui_clone is available" >&2
        exit 77
    fi
    mkdir -p "$out/headers/stand-in"
    git --git-dir="$imgui_clone/.git" archive "$stand_in" imgui.h imconfig.h | tar -x -C "$out/headers/stand-in"
    inc="$out/headers/stand-in"
    label="stand-in:ocornut-$stand_in"
    stand_in_define="-DIMTYPESET_BUNDLE_STAND_IN"
    cat >&2 <<EOF
build_bundle_library.sh: NOTE: building against STAND-IN headers ($stand_in).
  The wheel's own headers are the fork at $want, and
  $fork is ${have:-not initialised}. For a library that passes Dear ImGui's own layout check,
  in $bundle run:
      git submodule update --init external/imgui/imgui
      git -C external/imgui/imgui checkout $want
  and run this script again.
EOF
fi

# The sources are the drop-in files (tools/amalgamate.py; docs/distribution.md):
# the whole library is one translation unit, so the build is the four compiler
# lines and the link line below and needs nothing else of this repository.
"$python" -I -B "$repo/tools/amalgamate.py" --out "$out/dist"

# The bundle's ImGui configuration for a Python build (docs/bindings-notes.md,
# section 3), its throwing IM_ASSERT included: exceptions are on, as they must be.
# -ffp-contract=off: the results are defined without fused multiply-adds; the
# generated file also says so itself, and this says it for every file.
# The demo window is in it (imtypeset_demo.cpp). It calls
# ImGui::InputTextMultiline and InputTextWithHint, whose callback parameter the
# wheel's fork declares as a std::function: under stand-in headers the library
# calls upstream's names, which the wheel does not export, and
# imtypeset_bundle_shim.cpp defines those two and hands over to the wheel's
# (IMTYPESET_BUNDLE_STAND_IN).
compile() {
    "${CXX:-g++}" -std=c++17 -O2 -g -fPIC -fvisibility=hidden -fvisibility-inlines-hidden \
        -ffp-contract=off -fexceptions -Wno-terminate \
        "-I$inc" "-I$out/dist" \
        "-DIMGUI_USER_CONFIG=\"$out/headers/imgui_bundle_config.h\"" \
        -DIMGUI_BUNDLE_BUILD_PYTHON -DIMGUI_BUNDLE_PYTHON_API -DIMGUI_USE_WCHAR32 \
        -DImTextureID=int -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS -DIMGUI_ENABLE_FREETYPE \
        "-DIMTYPESET_BUNDLE_HEADERS=\"$label\"" \
        $stand_in_define \
        -c "$1" -o "$out/obj/$(basename "$1" .cpp).o" || touch "$out/obj/FAILED"
}
rm -f "$out/obj/FAILED" "$out"/obj/*.o
for src in "$out/dist/imtypeset.cpp" "$out/dist/imtypeset_c.cpp" "$out/dist/imtypeset_demo.cpp" \
    "$repo/python/host/imtypeset_bundle_shim.cpp"; do
    compile "$src" &
done
wait
[ ! -f "$out/obj/FAILED" ] || { echo "build_bundle_library.sh: a source failed to compile" >&2; exit 1; }

"${CXX:-g++}" -shared -o "$out/libimtypeset_bundle.so" "$out"/obj/*.o "$bundle_so" \
    -Wl,-rpath,"$bundle_dir" -Wl,--no-undefined \
    "-Wl,--version-script=$repo/python/host/exports_bundle.map"
echo "built $out/libimtypeset_bundle.so ($label)"

PYTHONDONTWRITEBYTECODE=1 "$python" -P -B "$repo/tools/bundle_route/run_gate.py" "$out/libimtypeset_bundle.so"
echo "use it with:"
echo "  IMTYPESET_LIBRARY=$out/libimtypeset_bundle.so PYTHONPATH=$repo/python $python ..."
