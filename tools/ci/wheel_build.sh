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
#
# Writes <out>/wheels/*.whl, <out>/work (the build's own scratch and log) and
# two environments, <out>/build-env and <out>/test-env. Needs the network: pip
# installs imgui-bundle from PyPI, as a wheel or not at all.
#
# NEVER RUN where it was written: there was no index to install from. What ran
# there is each tool it calls, against environments that already held
# imgui-bundle (docs/sidecar.md, section 12).
set -eu
version="$1"
out="$2"
python="${3:-python}"
repo="$(cd "$(dirname "$0")/../.." && pwd)"
headers="${IMTYPESET_BUNDLE_HEADERS:-fork}"
tag="${IMTYPESET_RELEASE_TAG:-}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"

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
fi

echo "== a new environment: imgui-bundle from PyPI, imtypeset from the wheel just built"
"$python" -m venv "$out/test-env"
test_python="$out/test-env/bin/python"
"$test_python" -m pip install --disable-pip-version-check --only-binary=:all: "imgui-bundle==$version"
# From the directory only: the name is not looked up on an index.
"$test_python" -m pip install --disable-pip-version-check --no-index --find-links "$out/wheels" imtypeset
"$test_python" -m pip check

echo "== the installation"
"$test_python" -P -B "$repo/tools/wheel/check_installed.py" --scratch "$out/scratch"
