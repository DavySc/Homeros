#!/usr/bin/env bash
# Starts headless sway (pixman, 1280x800) and runs "$@" under it with
# $SWAY_PID set; a wallpaper config folder at $XDG_RUNTIME_DIR/cfg when
# WALL is a file, an empty one at $XDG_RUNTIME_DIR/none.
set -eu
export XDG_RUNTIME_DIR=$(mktemp -d)
printf 'output HEADLESS-1 resolution 1280x800 scale 1\ndefault_border none\n%s\n' "${SWAYCFG:-}" > $XDG_RUNTIME_DIR/sway.cfg
WLR_BACKENDS=headless WLR_RENDERER=${RENDERER:-pixman} WLR_LIBINPUT_NO_DEVICES=1 sway -c $XDG_RUNTIME_DIR/sway.cfg >$XDG_RUNTIME_DIR/sway.log 2>&1 &
export SWAY_PID=$!
trap 'kill $SWAY_PID; rm -rf $XDG_RUNTIME_DIR' EXIT
until ls $XDG_RUNTIME_DIR/wayland-* >/dev/null 2>&1; do sleep 0.1; done
export WAYLAND_DISPLAY=$(basename $(ls $XDG_RUNTIME_DIR/wayland-* | grep -v lock | head -1))
export SWAYSOCK=$(ls $XDG_RUNTIME_DIR/sway-ipc.* | head -1)
mkdir -p $XDG_RUNTIME_DIR/cfg/homeros $XDG_RUNTIME_DIR/none
[ -z "${WALL:-}" ] || cp "$WALL" $XDG_RUNTIME_DIR/cfg/homeros/wallpaper
"$@"
