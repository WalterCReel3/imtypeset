#!/bin/sh
# Attaches the wheels of a tag build to a GitHub Release, after refusing
# anything that is not a release.
#
#   tools/ci/wheel_release.sh <tag> <directory holding the wheels, at any depth>
#
# Refuses, and creates nothing, unless: the tag is "v" + the version every copy
# in the tree agrees on, docs/CHANGELOG.md has a section for it
# (tools/version.py), there is at least one wheel, and every wheel passes
# tools/wheel/check_wheel.py --release for that tag (no stand-in headers;
# accepted by Dear ImGui's own layout check).
#
# Then `gh release create` with the wheels and the changelog's section as the
# notes. GH_TOKEN must allow writing releases: in the workflow it is the run's
# own token, in the one job that has `contents: write`. IMTYPESET_RELEASE_DRY=1
# stops before `gh`.
#
# Nothing is uploaded to a package index. NEVER RUN against GitHub.
set -eu
tag="$1"
dir="$2"
repo="$(cd "$(dirname "$0")/../.." && pwd)"
python="${PYTHON:-python3}"

"$python" -I -B "$repo/tools/version.py" --check --tag "$tag"
list="$(mktemp)"
find "$dir" -name 'imtypeset-*.whl' | sort > "$list"
count="$(wc -l < "$list" | tr -d ' ')"
[ "$count" -gt 0 ] || { echo "wheel_release.sh: no wheel under $dir" >&2; exit 1; }
set --
while IFS= read -r wheel; do set -- "$@" "$wheel"; done < "$list"
rm -f "$list"
"$python" -I -B "$repo/tools/wheel/check_wheel.py" --release --tag "$tag" "$@"

notes="$(mktemp)"
"$python" -I -B "$repo/tools/version.py" --notes > "$notes"
echo "wheel_release.sh: $count wheels pass for $tag"
if [ "${IMTYPESET_RELEASE_DRY:-}" = 1 ]; then
    echo "wheel_release.sh: dry run; not creating the release. Notes:"; cat "$notes"; rm -f "$notes"
    exit 0
fi
gh release create "$tag" "$@" --verify-tag --title "imtypeset ${tag#v}" --notes-file "$notes"
rm -f "$notes"
