#!/bin/sh
# Experiment B: build imtypeset as a shared library whose ImGui symbols are
# left UNDEFINED, to be resolved at load time against the ImGui inside the
# installed imgui_bundle wheel.
#
#   tools/experiments/build_expb.sh deps      # headers: .deps/imgui (v1.92.9, master)
#   tools/experiments/build_expb.sh docking   # headers: v1.92.9-docking, extracted from
#                                             # ../imgui's git objects into build/bundle-research/
#   tools/experiments/build_expb.sh fork      # headers: the bundle's own ImGui fork,
#                                             # ../imgui_bundle/external/imgui/imgui. NOT RUN
#                                             # HERE: that submodule is not initialised.
#   tools/experiments/build_expb.sh selfhost  # control: no bundle at all. This repository's
#                                             # ImGui (.deps/imgui) is compiled INTO the library.
#
# Run from the imtypeset root. Sources are the SNAPSHOT under
# build/bundle-research/snap (src/, include/, tests/support/), never the live
# tree. Writes only under build/bundle-research/<variant>/.
#
# The defines are the ones the bundle gives ImGui in a Python build:
#   IMGUI_USE_WCHAR32          hello_imgui, with FreeType (hello_imgui_build_lib.cmake);
#                              confirmed on the wheel by the mangled name of
#                              ImFontAtlas::AddFontFromMemoryTTF(..., unsigned int const*)
#   ImDrawIdx=unsigned int     imgui_bundle_cmake/imgui_bundle_config.h:82
#   ImTextureID=int            imgui_bundle_cmake/internal/add_imgui.cmake:56
#   IMGUI_DISABLE_OBSOLETE_FUNCTIONS   CMakeLists.txt:147, add_imgui.cmake:72
#   IMGUI_ENABLE_FREETYPE      hello_imgui; the wheel exports ImGuiFreeType::GetFontLoader
#   IMGUI_BUNDLE_PYTHON_API    CMakeLists.txt:379 (does nothing to stock headers: the
#                              bundle's ImGui FORK reads it, and that fork is not on this machine)
# NOT applied: the bundle's IM_ASSERT, which throws (imgui_bundle_config.h:40).
# The library is built -fno-exceptions, so a throwing macro does not compile in
# it; IM_ASSERT here is ImGui's default, assert().
set -eu

variant="${1:-docking}"
root="$(pwd)"
work="$root/build/bundle-research"
snap="$work/snap"
out="$work/$variant"
bundle_dir="$root/../imgui-adv-text/.venv/lib/python3.11/site-packages/imgui_bundle"
bundle_so="$bundle_dir/_imgui_bundle.cpython-311-x86_64-linux-gnu.so"

case "$variant" in
    deps) imgui_inc="$root/.deps/imgui" ;;
    docking) imgui_inc="$work/imgui-1.92.9-docking" ;;
    fork) imgui_inc="$root/../imgui_bundle/external/imgui/imgui" ;;
    selfhost) imgui_inc="$root/.deps/imgui" ;;
    *) echo "usage: $0 deps|docking|fork|selfhost" >&2; exit 2 ;;
esac
[ -f "$snap/src/imtypeset.cpp" ] || { echo "no snapshot at $snap" >&2; exit 2; }
[ -f "$imgui_inc/imgui.h" ] || { echo "no imgui.h at $imgui_inc" >&2; exit 2; }

mkdir -p "$out/obj"
defs="-DIMGUI_USE_WCHAR32 -DImDrawIdx=unsigned\ int -DImTextureID=int \
-DIMGUI_DISABLE_OBSOLETE_FUNCTIONS -DIMGUI_ENABLE_FREETYPE -DIMGUI_BUNDLE_PYTHON_API"
if [ "$variant" = selfhost ]; then
    # Our own ImGui, as this repository's default build configures it, minus
    # FreeType: stb_truetype, so that nothing but libstdc++ is linked.
    defs="-DIMGUI_USE_WCHAR32"
fi
common="-std=c++17 -O2 -g -fPIC -fvisibility=hidden -ffp-contract=off \
-I$imgui_inc -I$snap/include -I$snap/src -I$snap/tests"

: > "$out/build.log"
for src in "$snap"/src/*.cpp; do
    obj="$out/obj/$(basename "$src" .cpp).o"
    eval g++ $common $defs -fno-exceptions -fno-rtti -Wall -Wextra \
        -c "\"$src\"" -o "\"$obj\"" >> "$out/build.log" 2>&1
done
# The shim catches what the bundle's IM_ASSERT throws, so it has exceptions on.
eval g++ $common $defs -fexceptions -c "\"$root/tools/experiments/expb_shim.cpp\"" \
    -o "\"$out/obj/expb_shim.o\"" >> "$out/build.log" 2>&1

if [ "$variant" = selfhost ]; then
    : > "$out/build.log.imgui"
    for src in imgui imgui_draw imgui_tables imgui_widgets; do
        eval g++ $common $defs -c "\"$imgui_inc/$src.cpp\"" -o "\"$out/obj/$src.o\"" \
            >> "$out/build.log" 2>&1
    done
    eval g++ $common $defs -c "\"$root/tools/experiments/expb_selfhost.cpp\"" \
        -o "\"$out/obj/expb_selfhost.o\"" >> "$out/build.log" 2>&1
    g++ -shared -o "$out/libimtypeset_expb.so" "$out"/obj/*.o >> "$out/build.log" 2>&1
    echo "variant=selfhost headers=$imgui_inc (ImGui compiled in)"
    echo "warnings: $(grep -c 'warning:' "$out/build.log" || true)"
    ls -la "$out"/*.so
    exit 0
fi

# (1) ImGui symbols undefined, nothing recorded about where they come from.
g++ -shared -o "$out/libimtypeset_expb.so" "$out"/obj/*.o >> "$out/build.log" 2>&1
# (2) The same, with the bundle's module as a DT_NEEDED and an rpath to it.
g++ -shared -o "$out/libimtypeset_expb_needed.so" "$out"/obj/*.o \
    "$bundle_so" -Wl,-rpath,"$bundle_dir" >> "$out/build.log" 2>&1 ||
    echo "link with DT_NEEDED failed; see build.log" >&2

# Every symbol the library needs from outside, and whether the wheel has it.
nm -D --undefined-only "$out/libimtypeset_expb.so" | awk '{print $NF}' |
    grep -E '^_ZN?K?[0-9]*(5ImGui|6ImFont|10ImDrawList|11ImFontAtlas|11ImFontBaked|12ImFontConfig|15ImGuiTextBuffer|7ImGuiIO|10ImDrawData|8ImGuiStyle)|Im(Gui|Font|Draw)' |
    sort -u > "$out/undefined-imgui.txt"
nm -D --defined-only "$bundle_so" | awk '{print $NF}' | sort -u > "$out/bundle-defined.txt"
comm -23 "$out/undefined-imgui.txt" "$out/bundle-defined.txt" > "$out/missing-from-bundle.txt"

echo "variant=$variant headers=$imgui_inc"
echo "warnings: $(grep -c 'warning:' "$out/build.log" || true)"
echo "undefined ImGui symbols: $(wc -l < "$out/undefined-imgui.txt")"
echo "of those, not exported by the wheel: $(wc -l < "$out/missing-from-bundle.txt")"
ls -la "$out"/*.so
