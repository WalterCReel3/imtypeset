#!/bin/sh
# Fills .deps/sdl2 of this repository from an SDL2 that is already built, so
# that examples/demo_sdl2 and python/ configure on a machine with no SDL2
# development package and without naming a directory outside the repository.
#
#   tools/vendor_sdl2.sh [--force] <SDL2 install prefix or build tree>
#
# Two layouts are read:
#
#   install prefix   <dir>/lib/libSDL2*.a (or .so) and <dir>/include/SDL2/*.h,
#                    SDL_config.h among them: what `cmake --install` of SDL2
#                    leaves.
#   build tree       a CMake build tree that fetched and built SDL2 as a
#                    dependency and never installed it: <dir>/lib/libSDL2*.a,
#                    headers in <dir>/_deps/sdl2-build/include/SDL2, the
#                    generated SDL_config.h in
#                    <dir>/_deps/sdl2-build/include-config-*/SDL2, and the
#                    licence in <dir>/_deps/sdl2-src/LICENSE.txt.
#
# Writes .deps/sdl2/{lib,include/SDL2,LICENSE.txt,PROVENANCE.txt} and nothing
# else. .deps/ is untracked and ignored: SDL2 is not part of the repository,
# and a fresh clone needs this script or a system SDL2. An existing .deps/sdl2
# is left alone unless --force is given, which replaces it.
#
# Nothing is compiled here. What is copied is whatever the source directory
# holds: its configuration (debug or release, static or shared, position-
# independent or not) and its platform come with it and PROVENANCE.txt records
# them. The Python host links SDL2 into a shared library, so a static SDL2
# must have been built position-independent (SDL_STATIC_PIC=ON).
set -eu

force=0
src=""
for arg in "$@"; do
    case "$arg" in
        --force) force=1 ;;
        -h|--help) sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "vendor_sdl2.sh: unknown option $arg" >&2; exit 2 ;;
        *)
            [ -z "$src" ] || { echo "vendor_sdl2.sh: one source directory, not two" >&2; exit 2; }
            src="$arg" ;;
    esac
done
[ -n "$src" ] || { echo "usage: tools/vendor_sdl2.sh [--force] <SDL2 install prefix or build tree>" >&2; exit 2; }
[ -d "$src" ] || { echo "vendor_sdl2.sh: $src is not a directory" >&2; exit 1; }

repo="$(cd "$(dirname "$0")/.." && pwd)"
src="$(cd "$src" && pwd)"
out="$repo/.deps/sdl2"

# The headers and the licence, by layout.
if [ -f "$src/include/SDL2/SDL.h" ]; then
    layout="install prefix"
    headers="$src/include/SDL2"
    config=""
    [ -f "$headers/SDL_config.h" ] || {
        echo "vendor_sdl2.sh: $headers has SDL.h and no SDL_config.h" >&2; exit 1; }
elif [ -f "$src/_deps/sdl2-build/include/SDL2/SDL.h" ]; then
    layout="build tree"
    headers="$src/_deps/sdl2-build/include/SDL2"
    config=""
    for candidate in "$src"/_deps/sdl2-build/include-config-*/SDL2; do
        if [ -f "$candidate/SDL_config.h" ]; then
            config="$candidate"
            break
        fi
    done
    [ -n "$config" ] || {
        echo "vendor_sdl2.sh: no SDL_config.h under $src/_deps/sdl2-build/include-config-*/SDL2" >&2
        exit 1; }
else
    echo "vendor_sdl2.sh: $src is neither an SDL2 install prefix (include/SDL2/SDL.h) nor a" >&2
    echo "build tree that holds one (_deps/sdl2-build/include/SDL2/SDL.h)" >&2
    exit 1
fi

libraries=""
for candidate in "$src"/lib/libSDL2*.a "$src"/lib/libSDL2*.so "$src"/lib/libSDL2*.so.* "$src"/lib/libSDL2*.dylib; do
    [ -e "$candidate" ] || continue
    # SDL2 itself and SDL2main only: SDL2_image, SDL2_mixer, SDL2_ttf and their
    # like are other libraries under other licences.
    case "$(basename "$candidate")" in
        libSDL2_*) continue ;;
    esac
    libraries="$libraries $candidate"
done
[ -n "$libraries" ] || { echo "vendor_sdl2.sh: no libSDL2* in $src/lib" >&2; exit 1; }

licence=""
for candidate in "$src/_deps/sdl2-src/LICENSE.txt" "$src/share/licenses/SDL2/LICENSE.txt" \
                 "$src/LICENSE.txt"; do
    if [ -f "$candidate" ]; then
        licence="$candidate"
        break
    fi
done

if [ -e "$out" ]; then
    if [ "$force" -ne 1 ]; then
        echo "vendor_sdl2.sh: $out exists; --force replaces it" >&2
        exit 1
    fi
    rm -rf "$out"
fi
mkdir -p "$out/lib" "$out/include/SDL2"
# -P: a shared library's version links stay links.
for library in $libraries; do
    cp -P "$library" "$out/lib/"
done
cp "$headers"/*.h "$out/include/SDL2/"
[ -z "$config" ] || cp "$config"/*.h "$out/include/SDL2/"
[ -z "$licence" ] || cp "$licence" "$out/LICENSE.txt"

version="$(awk '
    /^#define SDL_MAJOR_VERSION/ { major = $3 }
    /^#define SDL_MINOR_VERSION/ { minor = $3 }
    /^#define SDL_PATCHLEVEL/    { patch = $3 }
    END { print major "." minor "." patch }' "$out/include/SDL2/SDL_version.h")"
{
    echo "SDL $version, copied (not built here) by tools/vendor_sdl2.sh on $(date +%Y-%m-%d)"
    echo "from $src ($layout):"
    for library in $libraries; do
        echo "  lib/$(basename "$library")   <- ${library#"$src"/}"
    done
    echo "  include/SDL2/*.h   <- ${headers#"$src"/}"
    [ -z "$config" ] || echo "  include/SDL2/SDL_config.h and its neighbours   <- ${config#"$src"/}"
    if [ -n "$licence" ]; then
        echo "  LICENSE.txt   <- ${licence#"$src"/} (zlib licence)"
    else
        echo "  NO LICENSE.txt was found in the source directory: SDL2 is under the zlib licence,"
        echo "  and its text belongs beside any copy that is passed on."
    fi
    echo "Machine: $(uname -s) $(uname -m). The configuration (debug or release, static or shared,"
    echo "position-independent or not) is the source directory's."
    echo "Untracked (.deps/ is ignored): it exists so that the demo host and the Python host"
    echo "configure without a system SDL2 and without reaching outside the repository."
} > "$out/PROVENANCE.txt"

echo "vendor_sdl2.sh: SDL $version from $src ($layout) -> $out"
[ -n "$licence" ] || echo "vendor_sdl2.sh: warning: no LICENSE.txt found to copy" >&2
