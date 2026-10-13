#!/bin/sh
# Builds the imtypeset wheel for one released imgui-bundle and the interpreter
# given, checks the archive, then installs it into a NEW environment beside
# that imgui-bundle and checks the installation: the import with no variable
# set, the frozen shape, the headless checks and the partial-update cases
# (tools/wheel/check_installed.py).
#
#   tools/ci/wheel_build.sh <imgui-bundle version> <out-directory> [python]
#
#   IMTYPESET_BUNDLE_HEADERS  fork (default) | stand-in. A stand-in wheel is a
#                             development build: it says so in its version,
#                             and wheel_release.sh refuses it.
#   IMTYPESET_RELEASE_TAG     when set (a tag build): the wheel is built with
#                             --release and held to that tag.
#   IMTYPESET_REHEARSAL       "true" (a rehearsal of a release, from any ref):
#                             the same, for the tag the tree's own version
#                             would have, "v" + tools/version.py.
#
# Writes <out>/wheels/*.whl, <out>/work (the build's own scratch and log) and
# two environments, <out>/build-env and <out>/test-env. Needs the network: pip
# installs imgui-bundle from PyPI, as a wheel or not at all.
#
# RAN: on GitHub's runners (Linux x86-64 and macOS arm64, 2026-10-12), and on
# a developer's machine with IMTYPESET_RELEASE_TAG set (docs/sidecar.md,
# section 14).
set -eu
version="$1"
out="$2"
python="${3:-python}"
repo="$(cd "$(dirname "$0")/../.." && pwd)"
headers="${IMTYPESET_BUNDLE_HEADERS:-fork}"
tag="${IMTYPESET_RELEASE_TAG:-}"
if [ -z "$tag" ] && [ "${IMTYPESET_REHEARSAL:-}" = true ]; then
    tag="v$("$python" -I -B "$repo/tools/version.py")"
fi
mkdir -p "$out"
out="$(cd "$out" && pwd)"

# Where a person looks: the job's summary page; the log where there is none.
summary() {
    if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then cat >> "$GITHUB_STEP_SUMMARY"; else cat; fi
}

echo "== the environment the wheel is built for"
"$python" -m venv "$out/build-env"
build_python="$out/build-env/bin/python"
"$build_python" -m pip install --disable-pip-version-check --only-binary=:all: "imgui-bundle==$version"

echo "== the wheel"
set -- --python "$build_python" --out "$out/wheels" --work "$out/work" --headers "$headers" \
    --attested-where "GitHub Actions run ${GITHUB_RUN_ID:-unknown} of ${GITHUB_REPOSITORY:-unknown} at ${GITHUB_SHA:-unknown}"
[ -z "$tag" ] || set -- "$@" --release
"$python" -P -B "$repo/tools/wheel/build_wheel.py" "$@"
if [ -n "$tag" ]; then
    "$python" -I -B "$repo/tools/wheel/check_wheel.py" --release --tag "$tag" "$out"/wheels/*.whl
else
    "$python" -I -B "$repo/tools/wheel/check_wheel.py" "$out"/wheels/*.whl
    # Not a release build; say all the same whether a release would take this
    # wheel, where a person looks before creating a tag (the job's summary).
    if "$python" -I -B "$repo/tools/wheel/check_wheel.py" --release "$out"/wheels/*.whl > "$out/release-check.txt" 2>&1; then
        echo "- a release would accept this wheel (check_wheel.py --release)" | summary
    else
        { echo "- NOT what a release accepts (check_wheel.py --release):"; sed 's/^/      /' "$out/release-check.txt"; } | summary
    fi
fi

# The platform tag is measured from the library, never asserted: say what it
# came to where a person looks (the job's summary page).
# shellcheck disable=SC2016
"$python" -I -B -c 'import json, sys
a = json.load(open(sys.argv[1], encoding="utf-8"))
print("- `%s`: platform tag `%s` (%s)" % (a["distribution_version"], a["platform_tag"], a["platform_tag_basis"]))' \
    "$out/work/stage/imtypeset/_attestation.json" | summary

echo "== a new environment: imgui-bundle from PyPI, imtypeset from the wheel just built"
"$python" -m venv "$out/test-env"
test_python="$out/test-env/bin/python"
"$test_python" -m pip install --disable-pip-version-check --only-binary=:all: "imgui-bundle==$version"
# From the directory only: the name is not looked up on an index.
# --pre: a stand-in build is a development release (.dev0), which pip takes
# unasked only when nothing else fits; here it is the one wheel there is.
"$test_python" -m pip install --disable-pip-version-check --no-index --pre --find-links "$out/wheels" imtypeset
"$test_python" -m pip check

echo "== the installation"
"$test_python" -P -B "$repo/tools/wheel/check_installed.py" --scratch "$out/scratch"
