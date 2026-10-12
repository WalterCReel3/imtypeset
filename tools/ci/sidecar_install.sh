#!/bin/sh
# Installs one released imgui-bundle wheel into the current interpreter, from
# PyPI, as a wheel or not at all: a platform PyPI has no wheel for must end
# here in seconds and not in an hour of compiling the bundle from its sdist.
#
#   tools/ci/sidecar_install.sh <version> <report-directory> [python]
#
# Always exits 0. What pip said is in <report-directory>/pip-install.log, and
# tools/ci/sidecar_probe.py reports whether the wheel can be imported.
set -u
version="$1"
out="$2"
python="${3:-python}"
mkdir -p "$out"
{
    echo "\$ $python -m pip install --only-binary=:all: imgui-bundle==$version"
    "$python" -m pip install --disable-pip-version-check --only-binary=:all: "imgui-bundle==$version"
    echo "pip exit status: $?"
} > "$out/pip-install.log" 2>&1
tail -n 15 "$out/pip-install.log"
exit 0
