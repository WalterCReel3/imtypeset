#!/bin/sh
# Builds the sidecar library twice from the same sources and headers and
# requires the two files to be byte for byte the same: the build is
# reproducible (python/host/build_bundle_library.sh, "The same sources and
# headers give the same library bytes"). Everything a caller may change
# without changing the inputs differs between the two builds:
#
#                 first build                    second build
#   checkout      <scratch>/one/imtypeset        <scratch>/two/imtypeset-again
#   output        build/py-host-bundle inside    <scratch>/elsewhere/out-second
#                 the checkout (the default)
#   header cache  build/bundle-headers inside    <scratch>/cache-second
#                 the checkout (the default)
#   working dir   the checkout                   <scratch>
#   locale        LC_ALL=C                       LC_ALL=C.UTF-8 where it exists
#
# The two checkouts are copies of this one's src, include, python and tools
# and its top-level files, made here, so that both builds compile the same
# inputs even while this tree is being edited.
#
#   tools/ci/reproducible_bundle_library.sh <python> <scratch-directory>
#
#   python   the interpreter whose installed imgui_bundle the library is for
#            (build_bundle_library.sh's second argument).
#
# The headers are whatever build_bundle_library.sh is asked for
# (IMTYPESET_BUNDLE_HEADERS, IMTYPESET_IMGUI_FORK_SOURCE, IMTYPESET_IMGUI_CLONE
# pass through): the fork's by default, which needs the network. The steps are
# fetch, compile and link: what is compared is what the linker wrote.
#
# Exit 0 when the two libraries are identical; 1 when they differ, after
# printing where (the first differing bytes, the build-ids where readelf is
# there, and the strings found in one file and not the other); 2 for a usage
# error or a build that failed.
#
# RAN on a developer's machine (Linux x86-64, GCC 12) with stand-in headers.
# In .github/workflows/wheels.yml it runs in every wheel job, Linux and macOS,
# after the wheel is built; it has not run there yet.
set -eu
if [ "$#" -ne 2 ]; then
    echo "usage: tools/ci/reproducible_bundle_library.sh <python> <scratch-directory>" >&2
    exit 2
fi
python="$1"
# Absolute: each build runs from another directory. Not resolved through a
# symbolic link, which would lose the virtual environment it belongs to. A
# bare name is left to PATH.
case "$python" in
    /*) ;;
    */*) python="$(cd "$(dirname "$python")" && pwd)/$(basename "$python")" ;;
esac
repo="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$2"
scratch="$(cd "$2" && pwd)"
case "$scratch/" in "$repo/src/"*|"$repo/include/"*|"$repo/python/"*|"$repo/tools/"*)
    echo "reproducible_bundle_library.sh: the scratch directory must not be inside what is copied" >&2
    exit 2 ;;
esac
bundle_checkout="$repo/../imgui_bundle"
one="$scratch/one/imtypeset"
two="$scratch/two/imtypeset-again"
rm -rf "$scratch/one" "$scratch/two" "$scratch/elsewhere" "$scratch/cache-second"

"$python" -I -B -c '
import os, shutil, sys
repo, targets = sys.argv[1], sys.argv[2:]
skip = shutil.ignore_patterns("__pycache__", "*.pyc")
for target in targets:
    os.makedirs(target)
    for name in sorted(os.listdir(repo)):
        path = os.path.join(repo, name)
        if name in ("src", "include", "python", "tools"):
            shutil.copytree(path, os.path.join(target, name), ignore=skip, symlinks=True)
        elif os.path.isfile(path) and not os.path.islink(path) and not name.startswith("."):
            shutil.copyfile(path, os.path.join(target, name))
' "$repo" "$one" "$two"

utf8_locale="C"
if locale -a 2>/dev/null | grep -Eqi '^c\.utf-?8$'; then utf8_locale="C.UTF-8"; fi

build() { # build <checkout> <cwd> <locale> <log> [VAR=value ...]
    checkout="$1"; cwd="$2"; lc="$3"; log="$4"; shift 4
    status=0
    (
        cd "$cwd"
        env LC_ALL="$lc" IMTYPESET_BUNDLE_STEPS="fetch compile link" "$@" \
            sh "$checkout/python/host/build_bundle_library.sh" "$bundle_checkout" "$python"
    ) > "$log" 2>&1 || status=$?
    if [ "$status" -ne 0 ]; then
        tail -n 40 "$log" >&2
        echo "reproducible_bundle_library.sh: a build failed (status $status; log: $log)" >&2
        exit 2
    fi
}

echo "== first build: $one"
build "$one" "$one" C "$scratch/build-one.log" \
    IMTYPESET_BUNDLE_OUT="$one/build/py-host-bundle" IMTYPESET_HEADER_CACHE="$one/build/bundle-headers"
echo "== second build: $two"
build "$two" "$scratch" "$utf8_locale" "$scratch/build-two.log" \
    IMTYPESET_BUNDLE_OUT="$scratch/elsewhere/out-second" IMTYPESET_HEADER_CACHE="$scratch/cache-second"

first="$(cat "$one/build/py-host-bundle/LIBRARY")"
second="$(cat "$scratch/elsewhere/out-second/LIBRARY")"
digest() { "$python" -I -B -c 'import hashlib, sys; print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())' "$1"; }
echo "first:  $(wc -c < "$first" | tr -d ' ') bytes, sha256 $(digest "$first")"
echo "second: $(wc -c < "$second" | tr -d ' ') bytes, sha256 $(digest "$second")"
if cmp -s "$first" "$second"; then
    echo "identical: two builds of the same sources and headers gave the same library"
    exit 0
fi

echo "DIFFERENT: two builds of the same sources and headers gave two libraries" >&2
cmp -l "$first" "$second" 2>&1 | head -n 10 >&2 || true
if command -v readelf > /dev/null 2>&1; then
    for library in "$first" "$second"; do
        readelf -n "$library" 2>/dev/null | grep -i "build id" >&2 || true
    done
fi
if command -v strings > /dev/null 2>&1; then
    # Paths are what usually differs; strings shorter than that are noise.
    strings -a -n 8 "$first" | grep / | sort -u > "$scratch/strings-one.txt" || true
    strings -a -n 8 "$second" | grep / | sort -u > "$scratch/strings-two.txt" || true
    echo "strings with a '/' in one library and not the other (< first, > second), at most 40:" >&2
    diff "$scratch/strings-one.txt" "$scratch/strings-two.txt" | grep '^[<>]' | head -n 40 >&2 || true
fi
exit 1
