#!/bin/sh
# Photograph demo_sdl2 twice in one run: what its renderer says it drew
# (--screenshot, read back from the renderer) and what the X server shows (the
# window itself, by xwd and by ImageMagick's import). Needs an X display, so it
# is run under tools/shot.sh:
#
#   tools/shot.sh -- examples/demo_sdl2/grab.sh build/shot-rig/demo_sdl2 OUT-PREFIX \
#       [demo_sdl2 options: --backend, --size, --page, ...]
#
# Writes OUT-PREFIX.app.png (the host's own read-back), OUT-PREFIX.xwd.png and
# OUT-PREFIX.import.png, and prints how many pixels each X grab differs from the
# read-back by. OUT-PREFIX must be outside /tmp: under tools/shot.sh's Xvfb
# route /tmp is a tmpfs that goes away with the namespace.
#
# Exit status: 0 when all three images exist; the comparison is reported, not
# judged.

set -eu
[ $# -ge 2 ] || { sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//' >&2; exit 2; }
DEMO=$1; OUT=$2; shift 2
FRAMES=30

[ -n "${DISPLAY:-}" ] || { echo "grab.sh: DISPLAY is not set; run under tools/shot.sh" >&2; exit 2; }
for tool in xwd xwininfo convert import compare; do
    command -v $tool >/dev/null || { echo "grab.sh: $tool is not installed" >&2; exit 2; }
done

WORK=$(mktemp -d "${TMPDIR:-/tmp}/grab.XXXXXX")
BMP="$WORK/app.bmp"
"$DEMO" "$@" --frames $FRAMES --screenshot "$BMP" --stay &
PID=$!
trap 'kill $PID 2>/dev/null || true; rm -rf "$WORK"' EXIT

# The BMP appears under its name only after the frame it shows was presented.
TRIES=0
while [ ! -f "$BMP" ]; do
    kill -0 $PID 2>/dev/null || { echo "grab.sh: demo_sdl2 exited before frame $FRAMES" >&2; exit 1; }
    TRIES=$((TRIES + 1))
    [ $TRIES -lt 600 ] || { echo "grab.sh: no screenshot after 60 s" >&2; exit 1; }
    sleep 0.1
done
sleep 0.3

ID=$(xwininfo -root -tree | sed -n 's/^ *\(0x[0-9a-f]*\) "imtypeset demo.*/\1/p' | head -1)
[ -n "$ID" ] || { echo "grab.sh: no window titled 'imtypeset demo...' on $DISPLAY" >&2; xwininfo -root -tree >&2; exit 1; }
echo "grab.sh: window $ID on $DISPLAY"

xwd -id "$ID" -silent >"$WORK/win.xwd"
convert "$WORK/win.xwd" "$OUT.xwd.png"
import -window "$ID" "$OUT.import.png"
convert "$BMP" "$OUT.app.png"
kill $PID 2>/dev/null || true
wait $PID 2>/dev/null || true

for grab in xwd import; do
    # `compare -metric AE` prints the count of differing pixels on stderr and
    # exits 1 when there are any, which is a result here and not a failure.
    DIFF=$(compare -metric AE "$OUT.app.png" "$OUT.$grab.png" null: 2>&1 || true)
    echo "grab.sh: $grab vs app read-back: $DIFF differing pixel(s)"
done
identify -format "grab.sh: %f %wx%h\n" "$OUT.app.png" "$OUT.xwd.png" "$OUT.import.png"
