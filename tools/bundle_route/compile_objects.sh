#!/bin/sh
# Compiles every src/*.cpp into build/bundle-route/<variant>/obj with Dear ImGui
# Bundle's Python-build configuration (its throwing IM_ASSERT included), leaving
# every Dear ImGui symbol undefined.
#
#   tools/bundle_route/compile_objects.sh <variant> <imgui-include-dir>
#
# The bundle's config header is read from the v1.92.900 tag of ../imgui_bundle
# with `git show` (nothing is written there).
set -eu
variant="$1"; inc="$2"
repo="$(cd "$(dirname "$0")/../.." && pwd)"
out="$repo/build/bundle-route/$variant"
mkdir -p "$out/obj" "$repo/build/bundle-route/cfg"
cfg="$repo/build/bundle-route/cfg/imgui_bundle_config.h"
[ -f "$cfg" ] || git -C "$repo/../imgui_bundle" show v1.92.900:imgui_bundle_cmake/imgui_bundle_config.h > "$cfg"
one() {
    g++ -std=c++17 -O2 -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -ffp-contract=off \
        -fexceptions "-I$inc" "-I$repo/include" "-I$repo/src" \
        "-DIMGUI_USER_CONFIG=\"$cfg\"" -DIMGUI_BUNDLE_BUILD_PYTHON -DIMGUI_BUNDLE_PYTHON_API \
        -DIMGUI_USE_WCHAR32 -DImTextureID=int -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS \
        -DIMGUI_ENABLE_FREETYPE -DIMTYPESET_DISABLE_DEMO ${EXTRA_CXXFLAGS:-} -c "$1" -o "$out/obj/$(basename "$1" .cpp).o"
}
n=0
for src in "$repo"/src/*.cpp "$@"; do
    case "$src" in "$variant"|"$inc") continue ;; esac
    one "$src" &
    n=$((n + 1))
    if [ $((n % 4)) -eq 0 ]; then wait; fi
done
wait
ls "$out/obj" | wc -l
