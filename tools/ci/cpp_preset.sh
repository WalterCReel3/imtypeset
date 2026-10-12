#!/bin/sh
# Configures, builds and tests one CMake preset, as CMakePresets.json defines
# it, with no flag of this tree's weakened.
#
#   tools/ci/cpp_preset.sh <preset>
#
#   CC, CXX                  the compiler, as CMake reads them.
#   IMTYPESET_CI_BUILD_DIR   where to build; default build/<preset>, the
#                            preset's own directory.
#   IMTYPESET_CI_JOBS        default 4.
#   IMTYPESET_CI_CMAKE_ARGS  more arguments for the configure step, e.g.
#                            -DIMTYPESET_IMGUI_DIR=<checkout> where there is no
#                            network to fetch the pinned Dear ImGui with.
#
# The tests are the golden replays and everything else ctest knows; they read
# tests/golden and tests/data and nothing outside the repository. The scripts
# that WRITE goldens (tools/oracle) need the Python reference and are not run.
#
# The sanitiser options are the test presets' own, set here because ctest is
# given the directory and not the preset.
set -eu
preset="$1"
repo="$(cd "$(dirname "$0")/../.." && pwd)"
dir="${IMTYPESET_CI_BUILD_DIR:-$repo/build/$preset}"
jobs="${IMTYPESET_CI_JOBS:-4}"
cd "$repo"
if [ "$(uname -s)" = Darwin ] && command -v brew >/dev/null 2>&1; then
    # Homebrew's prefix, for find_package(Freetype) and SDL2.
    CMAKE_PREFIX_PATH="$(brew --prefix)${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
    export CMAKE_PREFIX_PATH
fi
echo "== $preset: $(uname -s) $(uname -m), CXX=${CXX:-unset}, in $dir"
# shellcheck disable=SC2086  # a list of arguments, possibly empty
cmake --preset "$preset" -B "$dir" ${IMTYPESET_CI_CMAKE_ARGS:-}
cmake --build "$dir" -j "$jobs"
case "$preset" in
    asan|exceptions)
        ASAN_OPTIONS="${ASAN_OPTIONS:-hard_rss_limit_mb=3000}"
        UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
        export ASAN_OPTIONS UBSAN_OPTIONS
        ;;
esac
ctest --test-dir "$dir" --output-on-failure -j "$jobs"
