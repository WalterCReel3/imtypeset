#!/bin/sh
# What a hosted runner needs beyond what it comes with.
#
#   tools/ci/install_deps.sh cpp          Ninja, FreeType (and the compiler CXX names, if absent)
#   tools/ci/install_deps.sh python-host  that, and SDL2
#   tools/ci/install_deps.sh sidecar      the system libraries the wheel's module is linked against
#
# Linux: apt, with sudo. macOS: Homebrew. Which packages a runner image already
# holds was not read anywhere; every name here is installed whether or not it
# is there, and the last lines print the versions the build will meet.
set -eu
what="${1:-cpp}"
system="$(uname -s)"
if [ "$system" = Linux ]; then
    packages=""
    case "$what" in
        cpp)         packages="ninja-build libfreetype-dev" ;;
        python-host) packages="ninja-build libfreetype-dev libsdl2-dev" ;;
        sidecar)     packages="libx11-6 libgl1" ;;
        *) echo "install_deps.sh: unknown set '$what'" >&2; exit 64 ;;
    esac
    case "${CXX:-}" in
        clang++*) command -v "$CXX" >/dev/null 2>&1 || packages="$packages clang" ;;
    esac
    sudo apt-get update -q
    # shellcheck disable=SC2086  # a list of package names
    sudo apt-get install -y -q --no-install-recommends $packages
elif [ "$system" = Darwin ]; then
    case "$what" in
        cpp)         brew install ninja freetype ;;
        python-host) brew install ninja freetype sdl2 ;;
        sidecar)     ;;
        *) echo "install_deps.sh: unknown set '$what'" >&2; exit 64 ;;
    esac
else
    echo "install_deps.sh: no recipe for $system" >&2
    exit 2
fi
if [ "$what" != sidecar ]; then
    cmake --version | head -n 1
    echo "ninja $(ninja --version)"
    if command -v pkg-config >/dev/null 2>&1; then
        echo "freetype2 (pkg-config's number is the libtool interface, not the release): $(pkg-config --modversion freetype2 2>/dev/null || echo 'not known to pkg-config')"
    fi
fi
"${CXX:-c++}" --version | head -n 1
