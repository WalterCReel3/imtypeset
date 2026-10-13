#!/bin/sh
# Attaches the wheels of a tag build to a GitHub Release, after refusing
# anything that is not a release.
#
#   tools/ci/wheel_release.sh <tag> <directory holding the wheels, at any depth>
#
# Refuses, and creates nothing, unless tools/ci/wheel_publish.py accepts the
# wheels: the tag is "v" + the version every copy in the tree agrees on,
# docs/CHANGELOG.md has a section for it, every wheel passes
# tools/wheel/check_wheel.py --release for that tag (no stand-in headers;
# accepted by Dear ImGui's own layout check), and the wheels are exactly the
# matrix's. It is the check the upload to PyPI is behind, so a GitHub Release
# and the index hold the same set or neither holds any.
#
# Then `gh release create` with the wheels and the changelog's section as the
# notes. GH_TOKEN must allow writing releases: in the workflow it is the run's
# own token, in the one job that has `contents: write`. IMTYPESET_RELEASE_DRY=1
# stops before `gh`.
#
# Nothing is uploaded to a package index here. RAN with IMTYPESET_RELEASE_DRY=1
# (docs/sidecar.md, section 14); never against GitHub.
set -eu
tag="$1"
dir="$2"
repo="$(cd "$(dirname "$0")/../.." && pwd)"
python="${PYTHON:-python3}"

gathered="$(mktemp -d)"
trap 'rm -rf "$gathered"' EXIT
"$python" -I -B "$repo/tools/ci/wheel_publish.py" "$tag" "$dir" "$gathered/wheels"
set --
for wheel in "$gathered"/wheels/*.whl; do set -- "$@" "$wheel"; done
count="$#"

notes="$(mktemp)"
"$python" -I -B "$repo/tools/version.py" --notes > "$notes"
echo "wheel_release.sh: $count wheels pass for $tag"
if [ "${IMTYPESET_RELEASE_DRY:-}" = 1 ]; then
    echo "wheel_release.sh: dry run; not creating the release. Notes:"; cat "$notes"; rm -f "$notes"
    exit 0
fi
gh release create "$tag" "$@" --verify-tag --title "imtypeset ${tag#v}" --notes-file "$notes"
rm -f "$notes"
