#!/bin/sh
# What must be true before a release tag is created, checked; and then the
# two commands that create it, printed and not run.
#
#   tools/release.sh <tag>          e.g. tools/release.sh v0.3.0
#
# Checks, in this order, and stops at the first that fails:
#   1. the tag is "v" + the version of include/imtypeset.h, every copy of the
#      version agrees, and docs/CHANGELOG.md has a section for it
#      (tools/version.py --check --tag);
#   2. the working tree is clean and HEAD is what would be tagged;
#   3. the tag does not exist yet;
#   4. the drop-in files generate from this tree (tools/amalgamate.py);
#   5. the workflow files keep this repository's rules (tools/ci/check_workflows.py,
#      skipped with a note where PyYAML is not installed), and the
#      distribution's own tests pass (tools/ci/test_wheel.py).
#
# It does not run the presets or build a wheel: CI does, for the tag, and
# publishes the wheels on PyPI and attaches them to the GitHub Release only if
# every one of them passes (.github/workflows/wheels.yml).
# docs/versioning.md is the account of what a version promises, and
# docs/releasing.md of everything a person does around this script.
#
# It creates nothing. The tag is created and pushed by a person.
set -eu
tag="${1:?usage: tools/release.sh v<version>}"
repo="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo"
python="${PYTHON:-python3}"

"$python" -I -B tools/version.py --check --tag "$tag"

if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
    echo "release.sh: the working tree has uncommitted changes; a tag names a commit" >&2
    git status --short --untracked-files=no >&2
    exit 1
fi
if git rev-parse -q --verify "refs/tags/$tag" >/dev/null; then
    echo "release.sh: the tag $tag exists already ($(git rev-parse --short "refs/tags/$tag"))" >&2
    exit 1
fi

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
"$python" -I -B tools/amalgamate.py --out "$scratch/dist" > "$scratch/amalgamate.log" 2>&1 || {
    cat "$scratch/amalgamate.log" >&2
    echo "release.sh: the drop-in files do not generate" >&2
    exit 1
}
echo "the drop-in files generate ($(find "$scratch/dist" -type f | wc -l | tr -d ' ') files)"

status=0
"$python" -B tools/ci/check_workflows.py || status=$?
if [ "$status" -eq 77 ]; then
    echo "release.sh: NOTE: the workflow files were not checked (no PyYAML)"
elif [ "$status" -ne 0 ]; then
    exit "$status"
fi
"$python" -I -B tools/ci/test_wheel.py

head="$(git rev-parse HEAD)"
cat <<TEXT

$tag: everything this script checks holds at $head.
Not checked here: the presets, and the wheels (CI builds and checks them for the tag).

Before the tag (docs/releasing.md): $head is pushed, and the workflows "ci" and
"wheels" are green for it. A tag is never moved and PyPI never takes a file
name twice, so a tag whose wheels fail is a version that is spent.

To release, a person runs:
    git tag -a $tag -m "imtypeset ${tag#v}" $head
    git push origin $tag
and the workflow "wheels", once every wheel has passed: waits for the approval
the environment "pypi" asks for, publishes the wheels on PyPI as imtypeset
${tag#v}.<imgui-bundle line>, and attaches them to the GitHub Release of $tag.
TEXT
