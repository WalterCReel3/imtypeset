#!/bin/sh
# Writes pptmstr.patch again from rich_pane.py and typeset.py in this directory
# and a pptmstr checkout (read only; default ../pptmstr beside this repository).
#
#   sh python/spike_bundle/pptmstr_overlay/make_patch.sh [pptmstr-checkout]
#
# rich_pane.py and typeset.py here ARE the patched files, so their part of the
# patch is a diff. theme.py here is not: it also holds one line that exists only
# so the copy can find pptmstr's assets from outside the package. Its part of
# the patch (two lines at the end of load_fonts) is kept as it stands in
# pptmstr.patch, which is why this file edits the patch and does not replace it.
# Ends with `git apply --check` in the checkout, which writes nothing.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
pptmstr="$(cd "${1:-$here/../../../../pptmstr}" && pwd)"
old="$here/pptmstr.patch"
new="$(mktemp)"
trap 'rm -f "$new"' EXIT
sed -n '/^--- a\/pptmstr\/ui\/rich_pane.py$/q;p' "$old" > "$new"
[ -s "$new" ] || { echo "make_patch.sh: $old has no theme.py part to keep" >&2; exit 1; }
diff -u --label a/pptmstr/ui/rich_pane.py --label b/pptmstr/ui/rich_pane.py \
    "$pptmstr/pptmstr/ui/rich_pane.py" "$here/rich_pane.py" >> "$new" || [ $? -eq 1 ]
diff -u --label a/pptmstr/ui/typeset.py --label b/pptmstr/ui/typeset.py \
    /dev/null "$here/typeset.py" >> "$new" || [ $? -eq 1 ]
cat "$new" > "$old"
echo "$(grep -c '^+[^+]' "$old") lines added, $(grep -c '^-[^-]' "$old") removed"
git -C "$pptmstr" apply --check "$old"
echo "git apply --check passes in $pptmstr at $(git -C "$pptmstr" rev-parse --short HEAD)"
