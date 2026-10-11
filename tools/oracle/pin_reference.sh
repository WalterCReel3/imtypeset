#!/usr/bin/env bash
#
# Materialise the frozen Python reference inside this repository, read-only.
#
#     tools/oracle/pin_reference.sh            extract it, or confirm what is there
#     tools/oracle/pin_reference.sh --lock     also (re)write tools/oracle/REFERENCE.lock
#
# The oracle scripts (tools/oracle/dump_*.py, manifest.py) import `advtext`
# from build/advtext-reference and from nowhere else; pinned_reference.py is
# what holds them to it. This script is what puts the tree there.
#
# What is pinned is one commit of ../imgui-adv-text, named below. It is taken
# with `git archive` from that repository's object store, so the state of its
# working tree and of whichever branch it has checked out does not matter. Only
# read-only git commands are run against it and nothing is written there: no
# worktree, no tag, no index refresh.
#
# The destination holds a stamp, `.pinned-commit`. If the directory exists
# with a stamp naming another commit, or with no stamp, this script refuses
# and changes nothing: remove the directory yourself if that is what you mean.
# If the stamp matches, the tree is checked against REFERENCE.lock and the
# script exits without extracting.
#
# REFERENCE.lock is tracked. It holds the commit, the tree, and one SHA-256
# over the SHA-256 of every file of the archive, so a pinned tree can be
# verified with no access to ../imgui-adv-text at all. Once it exists, an
# extraction that does not reproduce it is refused.
#
# What is NOT pinned: the interpreter and its libraries. They stay
# ../imgui-adv-text/.venv (a virtual environment is not history: no commit
# holds it). tests/golden/PROVENANCE.md records the versions.

set -euo pipefail

TAG="advtext-reference"
COMMIT="32e395346551cac82c7a6dd6e9b6d8e47feeac8e"
TREE="7a591a485818c85fd6c6c43df75271a477c9d612"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SOURCE_GIT="${IMTYPESET_REFERENCE_GIT:-$ROOT/../imgui-adv-text/.git}"
DEST="$ROOT/build/advtext-reference"
STAMP="$DEST/.pinned-commit"
LOCK="$ROOT/tools/oracle/REFERENCE.lock"

write_lock=0
case "${1:-}" in
    "") ;;
    --lock) write_lock=1 ;;
    *) echo "usage: tools/oracle/pin_reference.sh [--lock]" >&2; exit 2 ;;
esac

die() { echo "pin_reference: $*" >&2; exit 1; }

# `<sha256>  <path>` for every file under $1 but the stamp, sorted by path in
# the C locale; and the SHA-256 of that listing. pinned_reference.py computes
# the same two things.
listing() {
    (cd "$1" && find . -type f ! -path ./.pinned-commit -printf '%P\0' \
        | LC_ALL=C sort -z | xargs -0 sha256sum)
}
digest_of() { listing "$1" | sha256sum | cut -d' ' -f1; }
count_of() { listing "$1" | wc -l; }

lock_field() { sed -n "s/^$1=//p" "$LOCK"; }

check_against_lock() {
    local dir="$1"
    [ -f "$LOCK" ] || return 0
    [ "$(lock_field commit)" = "$COMMIT" ] \
        || die "REFERENCE.lock names commit $(lock_field commit), this script pins $COMMIT"
    local have want
    have="$(digest_of "$dir")"
    want="$(lock_field sha256)"
    [ "$have" = "$want" ] \
        || die "$dir does not match REFERENCE.lock (digest $have, locked $want): the tree was edited, or is not the pinned commit"
}

if [ -e "$DEST" ]; then
    [ -f "$STAMP" ] || die "$DEST exists and has no .pinned-commit stamp; refusing to touch it"
    have="$(sed -n 's/^commit=//p' "$STAMP")"
    [ "$have" = "$COMMIT" ] \
        || die "$DEST holds commit ${have:-(none)}, not $COMMIT; refusing to replace it"
    check_against_lock "$DEST"
    if [ "$write_lock" = 1 ] && [ ! -f "$LOCK" ]; then
        die "no REFERENCE.lock and the tree is already extracted: remove $DEST and run --lock again, so the lock is written from a fresh archive"
    fi
    echo "pin_reference: $DEST is $TAG ($COMMIT), $(count_of "$DEST") files, matches REFERENCE.lock"
    exit 0
fi

[ -d "$SOURCE_GIT" ] || die "no git directory at $SOURCE_GIT (set IMTYPESET_REFERENCE_GIT)"
git_ref() { git --no-optional-locks --git-dir="$SOURCE_GIT" "$@"; }

# The tag is a convenience and may be moved or deleted by whoever owns that
# repository; the commit is what is pinned. Both are checked, the commit rules.
resolved="$(git_ref rev-parse --verify --quiet "$COMMIT^{commit}")" \
    || die "commit $COMMIT is not in $SOURCE_GIT"
[ "$resolved" = "$COMMIT" ] || die "$COMMIT resolved to $resolved"
tree="$(git_ref rev-parse "$COMMIT^{tree}")"
[ "$tree" = "$TREE" ] || die "commit $COMMIT has tree $tree, expected $TREE"
if tagged="$(git_ref rev-parse --verify --quiet "$TAG^{commit}")"; then
    [ "$tagged" = "$COMMIT" ] \
        || die "tag $TAG in $SOURCE_GIT is $tagged, not the pinned $COMMIT: the tag moved; decide which is the reference before going on"
else
    echo "pin_reference: note: tag $TAG is not in $SOURCE_GIT; pinning by commit" >&2
fi

mkdir -p "$ROOT/build"
staging="$(mktemp -d "$ROOT/build/.advtext-reference.XXXXXX")"
trap 'chmod -R u+w "$staging" 2>/dev/null; rm -rf "$staging"' EXIT
git_ref archive --format=tar "$COMMIT" | tar -x -C "$staging"

if [ "$write_lock" = 1 ]; then
    {
        echo "# Written by tools/oracle/pin_reference.sh --lock. Do not edit."
        echo "# sha256 is the SHA-256 of the listing '<sha256>  <path>' of every file"
        echo "# of the archive, sorted by path (LC_ALL=C)."
        echo "tag=$TAG"
        echo "commit=$COMMIT"
        echo "tree=$TREE"
        echo "files=$(count_of "$staging")"
        echo "sha256=$(digest_of "$staging")"
    } > "$LOCK"
    echo "pin_reference: wrote $LOCK"
elif [ ! -f "$LOCK" ]; then
    die "no REFERENCE.lock; run with --lock once to write it"
fi
check_against_lock "$staging"

{
    echo "tag=$TAG"
    echo "commit=$COMMIT"
    echo "tree=$TREE"
} > "$staging/.pinned-commit"
# Files are made read-only so that nothing edits the oracle in place; the
# directories stay writable so that `rm -rf build/advtext-reference` works.
find "$staging" -type f -exec chmod a-w {} +
mv "$staging" "$DEST"
trap - EXIT
echo "pin_reference: extracted $TAG ($COMMIT) into $DEST, $(count_of "$DEST") files"
