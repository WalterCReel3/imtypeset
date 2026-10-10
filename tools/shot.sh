#!/bin/sh
# Run a command with an X display it can open a window on, and say which way
# the display was found.
#
#   tools/shot.sh [--route auto|display|xvfb|xvfb-netns|xvfb-tcp] [--screen WxHxD] [--probe]
#                 [--] command [args...]
#
# Whether a window can open is a fact about the SESSION, not about the machine:
# on this machine DISPLAY=:1 answers in an ordinary shell and is refused inside
# a sandboxed one. So nothing here is assumed; each route is tried and the one
# that worked is printed on stderr as one line,
#
#   shot.sh: route=<display|xvfb-netns|xvfb-tcp> DISPLAY=<...>
#
# followed, when a route failed first, by what it failed with. Routes, in order:
#
#   display     DISPLAY as the caller set it, if `xdpyinfo` can connect.
#   xvfb-netns  An Xvfb started for this command inside private user, mount and
#               network namespaces, reached over TCP on the namespace's own
#               loopback. Nothing outside the namespace can see the server and
#               two runs cannot collide on a port.
#   xvfb-tcp    The same without the network namespace (for a kernel or policy
#               that refuses one): the server listens on the host's TCP port
#               6000+N, N searched upward from $SHOT_DISPLAY_NUM (default 99).
#   none        Exit 3, with every route's error printed.
#
# Why each link of the Xvfb chain is there (the mechanism is the one worked out
# in the reference project's scripts; this file shares no code with them):
#
#   unshare -r     root inside a user namespace, so that `mount` is permitted.
#   unshare -m     the tmpfs below is private; the real /tmp is untouched.
#   mount tmpfs    Xvfb wants a lock file and a socket directory under /tmp,
#                  which a sandbox may make read-only.
#   -listen tcp -nolisten unix
#                  a sandbox can refuse socket(AF_UNIX) outright (EPERM), so
#                  the display is named with a host, 127.0.0.1:N, which Xlib
#                  reads as TCP port 6000+N.
#   llvmpipe       no GPU is reachable from in there. Mesa's software
#                  rasteriser provides OpenGL (4.5 core on this machine).
#
# The tmpfs shadows $TMPDIR along with the rest of /tmp, so TMPDIR is reset to
# /tmp inside and anything written there vanishes with the namespace. A command
# that must leave a file behind writes it to a path outside /tmp.
#
# The command sees SHOT_ROUTE and DISPLAY. Its exit status is this script's.

set -eu

ROUTE=auto
SCREEN=1400x1000x24
PROBE=0
while [ $# -gt 0 ]; do
    case "$1" in
    --route) ROUTE="$2"; shift 2 ;;
    --screen) SCREEN="$2"; shift 2 ;;
    --probe) PROBE=1; shift ;;
    --) shift; break ;;
    -h|--help) sed -n '2,45p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    -*) echo "shot.sh: unknown option $1" >&2; exit 2 ;;
    *) break ;;
    esac
done
MODES="netns tcp"
case "$ROUTE" in
auto|display|xvfb) ;;
xvfb-netns) MODES=netns; ROUTE=xvfb ;;
xvfb-tcp) MODES=tcp; ROUTE=xvfb ;;
*) echo "shot.sh: --route auto|display|xvfb|xvfb-netns|xvfb-tcp" >&2; exit 2 ;;
esac
if [ "$PROBE" = 1 ]; then
    # Something harmless that proves a client can connect.
    set -- sh -c 'xdpyinfo | sed -n "1,3p"'
elif [ $# -eq 0 ]; then
    echo "shot.sh: no command given (try --probe)" >&2
    exit 2
fi

command -v xdpyinfo >/dev/null || { echo "shot.sh: xdpyinfo is not installed (apt install x11-utils)" >&2; exit 2; }

# ---- route: display -------------------------------------------------------
DISPLAY_ERROR=""
if [ "$ROUTE" != xvfb ]; then
    if [ -z "${DISPLAY:-}" ]; then
        DISPLAY_ERROR="DISPLAY is not set"
    elif DISPLAY_ERROR=$(timeout 10 xdpyinfo 2>&1 >/dev/null); then
        echo "shot.sh: route=display DISPLAY=$DISPLAY" >&2
        SHOT_ROUTE=display exec "$@"
    else
        DISPLAY_ERROR="DISPLAY=$DISPLAY: ${DISPLAY_ERROR:-xdpyinfo failed without a message}"
    fi
    if [ "$ROUTE" = display ]; then
        echo "shot.sh: route=none" >&2
        echo "shot.sh:   display: $DISPLAY_ERROR" >&2
        exit 3
    fi
fi

# ---- route: xvfb ----------------------------------------------------------
XVFB_ERROR=""
for tool in Xvfb unshare; do
    command -v $tool >/dev/null || XVFB_ERROR="$tool is not installed"
done

# Runs inside the namespace. $1 = netns|tcp, $2 = screen, $3 = first display
# number, $4 = the display route's error (may be empty); the rest is the command.
INNER='
MODE=$1; SCREEN=$2; FIRST=$3; DISPLAY_ERROR=$4; shift 4
if [ "$MODE" = netns ]; then
    ip link set lo up || { echo "cannot bring up loopback in the network namespace" >&3; exit 97; }
fi
mount -t tmpfs tmpfs /tmp || { echo "cannot mount a tmpfs over /tmp in the namespace" >&3; exit 97; }
mkdir -p /tmp/.X11-unix /tmp/cache
export TMPDIR=/tmp XDG_CACHE_HOME=/tmp/cache XDG_CONFIG_HOME=/tmp/cache
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
unset WAYLAND_DISPLAY XAUTHORITY

XVFB=""
trap "[ -n \"\$XVFB\" ] && kill \$XVFB 2>/dev/null || true" EXIT
N=$FIRST
LAST=$((FIRST + 9))
[ "$MODE" = netns ] && LAST=$FIRST
UP=0
while [ $N -le $LAST ]; do
    Xvfb :$N -screen 0 "$SCREEN" -listen tcp -nolisten unix >/tmp/xvfb.log 2>&1 &
    XVFB=$!
    export DISPLAY=127.0.0.1:$N
    TRIES=0
    while [ $TRIES -lt 100 ] && kill -0 $XVFB 2>/dev/null; do
        if xdpyinfo >/dev/null 2>&1; then UP=1; break; fi
        TRIES=$((TRIES + 1))
        sleep 0.1
    done
    [ $UP = 1 ] && break
    kill $XVFB 2>/dev/null || true
    XVFB=""
    N=$((N + 1))
done
if [ $UP != 1 ]; then
    { echo "Xvfb did not come up (:$FIRST..:$LAST); its last log:"; cat /tmp/xvfb.log; } >&3
    exit 97
fi
echo "shot.sh: route=xvfb-$MODE DISPLAY=$DISPLAY screen=$SCREEN" >&2
[ -n "$DISPLAY_ERROR" ] && echo "shot.sh:   display was refused first: $DISPLAY_ERROR" >&2
export SHOT_ROUTE=xvfb-$MODE
echo up >&3
STATUS=0
"$@" 3>&- || STATUS=$?
exit $STATUS
'

FIRST="${SHOT_DISPLAY_NUM:-99}"
NETNS_ERROR=""
TCP_ERROR=""
if [ -z "$XVFB_ERROR" ]; then
    LOG=$(mktemp "${TMPDIR:-/tmp}/shot-sh.XXXXXX")
    trap 'rm -f "$LOG"' EXIT

    # The namespace writes "up" to fd 3 once a client has connected, and the
    # reason to fd 3 when it could not get that far. The command's own stdout
    # and stderr pass straight through; `unshare`'s own complaint, if the
    # kernel or a policy refuses the namespace, is on stderr as it was said.
    for MODE in $MODES; do
        FLAGS=-rm
        [ $MODE = netns ] && FLAGS=-rmn
        STATUS=0
        unshare $FLAGS sh -c "$INNER" shot-inner $MODE "$SCREEN" "$FIRST" "$DISPLAY_ERROR" "$@" 3>"$LOG" || STATUS=$?
        if grep -qx up "$LOG"; then
            exit $STATUS
        fi
        REASON="$(tr '\n' ' ' <"$LOG")(unshare $FLAGS exited $STATUS)"
        if [ $MODE = netns ]; then NETNS_ERROR=$REASON; else TCP_ERROR=$REASON; fi
    done
fi

echo "shot.sh: route=none" >&2
[ -n "$DISPLAY_ERROR" ] && echo "shot.sh:   display: $DISPLAY_ERROR" >&2
[ -n "$XVFB_ERROR" ] && echo "shot.sh:   xvfb: $XVFB_ERROR" >&2
[ -n "$NETNS_ERROR" ] && echo "shot.sh:   xvfb-netns: $NETNS_ERROR" >&2
[ -n "$TCP_ERROR" ] && echo "shot.sh:   xvfb-tcp: $TCP_ERROR" >&2
exit 3
