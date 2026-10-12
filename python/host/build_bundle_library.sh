#!/bin/sh
# Builds imtypeset as a shared library for use INSIDE an installed, unmodified
# Dear ImGui Bundle wheel: the library holds no Dear ImGui; its ImGui symbols
# are left undefined and resolve against the wheel's own module, so it draws in
# the application's context, atlas and draw lists. python/imtypeset then reports
# host "imgui_bundle" (examples/python/bundle_host.py). docs/sidecar.md is the
# account of how that binding works on each platform and of what has run.
#
#   python/host/build_bundle_library.sh [bundle-checkout] [python]
#
#   bundle-checkout  optional; default ../imgui_bundle. A local clone of Dear
#                    ImGui Bundle, read only, used INSTEAD OF THE NETWORK for
#                    what it holds: the configuration header, and the fork's
#                    headers if its submodule is checked out. Need not exist.
#   python           default ../imgui-adv-text/.venv/bin/python, else python3:
#                    the interpreter whose installed imgui_bundle wheel the
#                    library is for. 3.11 or later.
#
#   IMTYPESET_BUNDLE_HEADERS  fork (default) | stand-in. Which Dear ImGui
#                    headers to compile against; see WHICH HEADERS.
#   IMTYPESET_BUNDLE_BIND     link | lookup. How the library finds the wheel's
#                    ImGui; see HOW IT BINDS. Default: link on Linux, lookup on
#                    macOS (the only choice there).
#   IMTYPESET_BUNDLE_STEPS    default "fetch compile link audit gate"; any
#                    leading part of it stops early, and a later part alone
#                    resumes in the same output directory.
#   IMTYPESET_BUNDLE_OUT      output directory, default build/py-host-bundle.
#   IMTYPESET_HEADER_CACHE    where fetched headers are kept, default
#                    build/bundle-headers.
#   IMTYPESET_IMGUI_FORK_SOURCE  a git URL or local repository to take the
#                    fork from, in place of https://github.com/pthom/imgui.git.
#   IMTYPESET_IMGUI_CLONE     a local clone of ocornut/imgui to take stand-in
#                    headers from, default ../imgui if it exists, else
#                    https://github.com/ocornut/imgui.git.
#   IMTYPESET_BUNDLE_RPATH    (link) the run path through which the library
#                    finds the wheel's module. Default: the absolute directory
#                    of the module it is built against, for a library used
#                    where it was built. tools/wheel/build_wheel.py passes
#                    $ORIGIN/../imgui_bundle, for a library installed beside
#                    the wheel; such a library is not loadable from the build
#                    directory, so the gate step is left to the caller.
#   IMTYPESET_BUNDLE_JOBS     compiler processes at once, default 4.
#   CXX                       the compiler; default g++ on Linux, c++ on macOS.
#   MACOSX_DEPLOYMENT_TARGET  macOS only; default 14.0, the tag of the wheels
#                    on PyPI for 1.92.801 and 1.92.900 (macosx_14_0_arm64).
#
# Writes <out>/libimtypeset_bundle.so (Linux) or .dylib (macOS), <out>/LIBRARY
# (one line: that path), <library>.attestation.json (the gate step: the record
# that the full probe accepted this library beside this wheel, which is what
# `import imtypeset` checks against), <out>/dist (the generated sources),
# <out>/obj, <out>/BUILD (the facts of this build, key=value) and five one-line
# files it reads back (WHEEL_VERSION, IMGUI_VERSION, MODULE, MODULE_SHA256,
# HEADERS); and the header cache. Nothing else.
#
# WHICH HEADERS. The wheel's ImGui is a fork (github.com/pthom/imgui) and the
# wheel ships no headers. python/host/fetch_headers.py fetches them at the
# commit python/host/bundle_pins.toml records for the wheel's version, and the
# bundle's own ImGui configuration header with them:
#
#   fork      the wheel's own headers. The library says "fork:<commit>" and is
#             expected to pass Dear ImGui's own layout check. A wheel version
#             the pins do not know, or a fetch that fails, ends the build: the
#             script never falls back to stand-in headers by itself.
#   stand-in  upstream Dear ImGui's docking tag of the same version, only when
#             IMTYPESET_BUNDLE_HEADERS=stand-in asks for it. The library says
#             "stand-in:..." (python/imtypeset's host_info() shows it), Dear
#             ImGui's own check refuses sizeof(ImGuiStyle) (the fork adds a
#             field), and python/imtypeset accepts the library only because its
#             field probe passes (python/imtypeset/_bundle.py).
#
# HOW IT BINDS.
#
#   link      (ELF) the wheel's module is named as a needed library, by file
#             name, with a run path to its directory; the linker refuses any
#             undefined symbol. One library per CPython ABI tag, because the
#             tag is in the module's file name.
#   lookup    the Dear ImGui symbols are left undefined and found when the
#             library is loaded, among what the process has already loaded:
#             the wheel's module must be loaded first, and visible. On macOS
#             this is `-undefined dynamic_lookup`, since Apple's linker cannot
#             link against an extension module; tools/ci/sidecar_symbols.py
#             then checks what the linker was not asked to. On Linux it is
#             available for comparison and is not the default: see
#             docs/sidecar.md for why.
#
# THE macOS BRANCH HAS NEVER RUN. It is written from Apple's documentation of
# ld and dyld as remembered; .github/workflows/sidecar-probe.yml is where it
# first runs.
set -eu

repo="$(cd "$(dirname "$0")/../.." && pwd)"
bundle="${1:-$repo/../imgui_bundle}"
python="${2:-}"
if [ -z "$python" ]; then
    python="$repo/../imgui-adv-text/.venv/bin/python"
    [ -x "$python" ] || python="python3"
fi
out="${IMTYPESET_BUNDLE_OUT:-$repo/build/py-host-bundle}"
headers="${IMTYPESET_BUNDLE_HEADERS:-fork}"
steps="${IMTYPESET_BUNDLE_STEPS:-fetch compile link audit gate}"
jobs="${IMTYPESET_BUNDLE_JOBS:-4}"
system="$(uname -s)"

case "$system" in
    Darwin) suffix="dylib"; default_bind="lookup"; default_cxx="c++" ;;
    Linux)  suffix="so";    default_bind="link";   default_cxx="g++" ;;
    *) echo "build_bundle_library.sh: no recipe for $system (Linux and macOS only)" >&2; exit 2 ;;
esac
bind="${IMTYPESET_BUNDLE_BIND:-$default_bind}"
cxx="${CXX:-$default_cxx}"
case "$headers" in fork|stand-in) ;; *)
    echo "build_bundle_library.sh: IMTYPESET_BUNDLE_HEADERS is 'fork' or 'stand-in', not '$headers'" >&2; exit 2 ;;
esac
case "$bind" in link|lookup) ;; *)
    echo "build_bundle_library.sh: IMTYPESET_BUNDLE_BIND is 'link' or 'lookup', not '$bind'" >&2; exit 2 ;;
esac
if [ "$system" = Darwin ] && [ "$bind" = link ]; then
    echo "build_bundle_library.sh: on macOS the only binding is 'lookup'" >&2; exit 2
fi
has_step() { case " $steps " in *" $1 "*) return 0 ;; *) return 1 ;; esac; }

mkdir -p "$out/obj"
out="$(cd "$out" && pwd)" # Absolute: the compiler is handed a header by name.
library="$out/libimtypeset_bundle.$suffix"

# The wheel, as the interpreter sees it. One line each, read back by name so
# that nothing depends on a sed or an awk dialect.
"$python" -P -B -c '
import hashlib
import sys
import imgui_bundle
from imgui_bundle import imgui
out = sys.argv[1]
digest = hashlib.sha256()
with open(imgui_bundle._imgui_bundle.__file__, "rb") as handle:
    for block in iter(lambda: handle.read(1 << 20), b""):
        digest.update(block)
facts = {
    "WHEEL_VERSION": imgui_bundle.__version__,
    "IMGUI_VERSION": imgui.get_version(),
    "MODULE": imgui_bundle._imgui_bundle.__file__,
    "MODULE_SHA256": digest.hexdigest(),
}
for name, value in facts.items():
    with open(f"{out}/{name}", "w", encoding="utf-8") as handle:
        handle.write(value + "\n")
' "$out"
wheel_version="$(cat "$out/WHEEL_VERSION")"
imgui_version="$(cat "$out/IMGUI_VERSION")"
bundle_so="$(cat "$out/MODULE")"
bundle_sha256="$(cat "$out/MODULE_SHA256")"
bundle_dir="$(dirname "$bundle_so")"
echo "wheel: imgui_bundle $wheel_version, Dear ImGui $imgui_version, $bundle_so"
echo "build: $system $(uname -m), $cxx, headers=$headers, bind=$bind, steps=$steps"

# --- fetch ---------------------------------------------------------------------------
if has_step fetch; then
    set -- --bundle-version "$wheel_version" --kind "$headers"
    [ -z "${IMTYPESET_HEADER_CACHE:-}" ] || set -- "$@" --cache "$IMTYPESET_HEADER_CACHE"
    [ ! -d "$bundle" ] || set -- "$@" --bundle-checkout "$bundle"
    if [ "$headers" = fork ]; then
        [ -z "${IMTYPESET_IMGUI_FORK_SOURCE:-}" ] || set -- "$@" --imgui-source "$IMTYPESET_IMGUI_FORK_SOURCE"
    else
        clone="${IMTYPESET_IMGUI_CLONE:-$repo/../imgui}"
        [ ! -d "$clone" ] || set -- "$@" --imgui-source "$clone"
        cat >&2 <<EOF
build_bundle_library.sh: NOTE: building against STAND-IN headers, as asked
  (IMTYPESET_BUNDLE_HEADERS=stand-in). They are upstream Dear ImGui's, not the
  wheel's: Dear ImGui's own layout check will refuse this library, and
  python/imtypeset accepts it on its field probe alone.
EOF
    fi
    status=0
    "$python" -I -B "$repo/python/host/fetch_headers.py" "$@" > "$out/HEADERS" || status=$?
    if [ "$status" -ne 0 ]; then
        cat >&2 <<EOF
build_bundle_library.sh: the $headers headers for imgui_bundle $wheel_version are not available
  (fetch_headers.py ended with status $status; its reason is above). Nothing was built.
  This script does not substitute other headers. If a library built against
  upstream Dear ImGui's headers as a stand-in is acceptable, ask for it by name:
      IMTYPESET_BUNDLE_HEADERS=stand-in $0 ...
EOF
        exit "$status"
    fi
fi
[ -f "$out/HEADERS" ] || { echo "build_bundle_library.sh: no fetched headers in $out; run the fetch step" >&2; exit 2; }
# HEADERS holds two lines, dir=... and label=...; a directory may contain '='.
header_dir=""; label=""
while IFS= read -r line; do
    case "$line" in
        dir=*)   header_dir="${line#dir=}" ;;
        label=*) label="${line#label=}" ;;
    esac
done < "$out/HEADERS"
inc="$header_dir/imgui"
config="$header_dir/imgui_bundle_config.h"
case "$label" in
    stand-in:*) stand_in_define="-DIMTYPESET_BUNDLE_STAND_IN" ;;
    fork:*)     stand_in_define="" ;;
    *) echo "build_bundle_library.sh: $out/HEADERS names no headers" >&2; exit 2 ;;
esac
echo "headers: $label, in $header_dir"

# --- compile -------------------------------------------------------------------------
# The sources are the drop-in files (tools/amalgamate.py; docs/distribution.md):
# the whole library is one translation unit, so the build is four compiler
# lines and a link line and needs nothing else of this repository.
#
# The bundle's ImGui configuration for a Python build (docs/bindings-notes.md,
# section 3), its throwing IM_ASSERT included: exceptions are on, as they must
# be. -ffp-contract=off: the results are defined without fused multiply-adds;
# the generated file also says so itself, and this says it for every file.
# The demo window is in it (imtypeset_demo.cpp). It calls
# ImGui::InputTextMultiline and InputTextWithHint, whose callback parameter the
# wheel's fork declares as a std::function: under stand-in headers the library
# calls upstream's names, which the wheel does not export, and
# imtypeset_bundle_shim.cpp defines those two and hands over to the wheel's
# (IMTYPESET_BUNDLE_STAND_IN).
#
# -Wno-terminate is GCC's name for a warning GCC alone gives (a throwing
# IM_ASSERT in a destructor of ImGui's); Clang is not handed an option it does
# not know.
if has_step compile; then
    "$python" -I -B "$repo/tools/amalgamate.py" --out "$out/dist"
    quiet=""
    if "$cxx" --version 2>/dev/null | grep -qi "free software foundation"; then
        quiet="-Wno-terminate"
    fi
    arch=""
    if [ "$system" = Darwin ]; then
        MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-14.0}"
        export MACOSX_DEPLOYMENT_TARGET
        [ -z "${IMTYPESET_BUNDLE_ARCH:-}" ] || arch="-arch $IMTYPESET_BUNDLE_ARCH"
    fi
    compile() {
        # shellcheck disable=SC2086  # the three unquoted words are option lists, possibly empty
        "$cxx" -std=c++17 -O2 -g -fPIC -fvisibility=hidden -fvisibility-inlines-hidden \
            -ffp-contract=off -fexceptions $quiet $arch \
            "-I$inc" "-I$out/dist" \
            "-DIMGUI_USER_CONFIG=\"$config\"" \
            -DIMGUI_BUNDLE_BUILD_PYTHON -DIMGUI_BUNDLE_PYTHON_API -DIMGUI_USE_WCHAR32 \
            -DImTextureID=int -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS -DIMGUI_ENABLE_FREETYPE \
            "-DIMTYPESET_BUNDLE_HEADERS=\"$label\"" \
            "-DIMTYPESET_BUNDLE_WHEEL_VERSION=\"$wheel_version\"" \
            "-DIMTYPESET_BUNDLE_MODULE_NAME=\"$(basename "$bundle_so")\"" \
            "-DIMTYPESET_BUNDLE_MODULE_SHA256=\"$bundle_sha256\"" \
            $stand_in_define \
            -c "$1" -o "$out/obj/$(basename "$1" .cpp).o" || touch "$out/obj/FAILED"
    }
    rm -f "$out/obj/FAILED" "$out"/obj/*.o "$library" "$library.attestation.json"
    running=0
    for src in "$out/dist/imtypeset.cpp" "$out/dist/imtypeset_c.cpp" "$out/dist/imtypeset_demo.cpp" \
        "$repo/python/host/imtypeset_bundle_shim.cpp"; do
        compile "$src" &
        running=$((running + 1))
        if [ "$running" -ge "$jobs" ]; then wait; running=0; fi
    done
    wait
    [ ! -f "$out/obj/FAILED" ] || { echo "build_bundle_library.sh: a source failed to compile" >&2; exit 1; }
    echo "compiled $(find "$out/obj" -name '*.o' | wc -l | tr -d ' ') objects into $out/obj"
fi

# --- link ----------------------------------------------------------------------------
# Either way the library exports the C ABI and nothing else: a GNU version
# script on ELF, an exported-symbols list on Mach-O, the same two patterns.
if has_step link; then
    rm -f "$library" "$library.attestation.json"
    if [ "$system" = Darwin ]; then
        # Apple's ld cannot name the wheel's module as a dependency (it is a
        # loadable bundle, not a dylib), so the ImGui symbols are marked for
        # lookup when the library is loaded. That permission covers every
        # undefined symbol, which is why the audit step exists.
        # shellcheck disable=SC2086
        "$cxx" -dynamiclib ${IMTYPESET_BUNDLE_ARCH:+-arch $IMTYPESET_BUNDLE_ARCH} \
            -o "$library" "$out"/obj/*.o \
            -Wl,-undefined,dynamic_lookup \
            "-Wl,-exported_symbols_list,$repo/python/host/exports_bundle.txt" \
            -Wl,-install_name,@rpath/libimtypeset_bundle.dylib
    elif [ "$bind" = lookup ]; then
        "$cxx" -shared -o "$library" "$out"/obj/*.o \
            "-Wl,--version-script=$repo/python/host/exports_bundle.map"
    else
        # By file name and not by path: the needed entry is then the module's
        # name alone, found through the run path.
        "$cxx" -shared -o "$library" "$out"/obj/*.o \
            "-L$bundle_dir" "-l:$(basename "$bundle_so")" \
            -Wl,-rpath,"${IMTYPESET_BUNDLE_RPATH:-$bundle_dir}" -Wl,--no-undefined \
            "-Wl,--version-script=$repo/python/host/exports_bundle.map"
    fi
    printf '%s\n' "$library" > "$out/LIBRARY"
    {
        echo "library=$library"
        echo "headers=$label"
        echo "bind=$bind"
        echo "system=$system"
        echo "machine=$(uname -m)"
        echo "compiler=$("$cxx" --version 2>&1 | head -n 1)"
        echo "wheel_version=$wheel_version"
        echo "imgui_version=$imgui_version"
        echo "module=$bundle_so"
        echo "module_sha256=$bundle_sha256"
        echo "rpath=${IMTYPESET_BUNDLE_RPATH:-$bundle_dir}"
    } > "$out/BUILD"
    echo "built $library ($label, bind=$bind)"
fi

# --- audit ---------------------------------------------------------------------------
if has_step audit; then
    "$python" -I -B "$repo/tools/ci/sidecar_symbols.py" audit "$library" "$bundle_so"
fi

# --- gate ----------------------------------------------------------------------------
# The full probe, in a child interpreter: first with its whole report printed,
# then once more to write the attestation, <library>.attestation.json, which
# is what `import imtypeset` holds the library and the wheel's module to from
# then on (python/imtypeset/_bundle.py). The package itself imports the wheel
# before the library, which a library bound by lookup needs; where that is not
# enough IMTYPESET_PRELOAD_BUNDLE=1 has the module reopened RTLD_GLOBAL.
if has_step gate; then
    if [ "$bind" = lookup ] && [ "$system" = Linux ]; then
        # The comparison build: on Linux the package never makes the module's
        # symbols global unasked (docs/sidecar.md, section 2), so this asks,
        # and whoever imports such a library must ask too.
        IMTYPESET_PRELOAD_BUNDLE=1
        export IMTYPESET_PRELOAD_BUNDLE
    fi
    PYTHONDONTWRITEBYTECODE=1 "$python" -P -B "$repo/tools/bundle_route/run_gate.py" "$library"
    PYTHONDONTWRITEBYTECODE=1 "$python" -P -B "$repo/tools/bundle_route/attest.py" "$library"
    echo "use it with:"
    echo "  IMTYPESET_LIBRARY=$library PYTHONPATH=$repo/python $python ..."
fi
