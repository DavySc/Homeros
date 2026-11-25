#!/usr/bin/env bash
# As sway.sh, under headless weston (pixman, 1280x800): $COMP_PID is weston.
set -eu
export XDG_RUNTIME_DIR=$(mktemp -d)
chmod 700 $XDG_RUNTIME_DIR
weston --backend=headless --renderer=pixman --width=1280 --height=800 --socket=wl-rev --idle-time=0 >$XDG_RUNTIME_DIR/weston.log 2>&1 &
export COMP_PID=$!
trap 'kill $COMP_PID; rm -rf $XDG_RUNTIME_DIR' EXIT
until [ -e $XDG_RUNTIME_DIR/wl-rev ]; do sleep 0.1; done
export WAYLAND_DISPLAY=wl-rev
mkdir -p $XDG_RUNTIME_DIR/cfg/homeros $XDG_RUNTIME_DIR/none
[ -z "${WALL:-}" ] || cp "$WALL" $XDG_RUNTIME_DIR/cfg/homeros/wallpaper
"$@"
