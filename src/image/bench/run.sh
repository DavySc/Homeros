#!/usr/bin/env bash
# Frame time with and without an image (package 21): builds frame.c with the
# editor's flags, starts headless sway (pixman, 1280x800, as tests/test_wl.c)
# and runs text and image alternately, RUNS times each.
# With a WALLPAPER, text scrolled and typing without and with it instead.
# Usage: src/image/bench/run.sh IMAGE.png [N frames] [RUNS] [WALLPAPER]
set -eu
cd "$(dirname "$0")/../../.."
img=$1 n=${2:-500} runs=${3:-5}
make -s homeros >/dev/null
srcs=$(make -s --eval 'p: ; @echo $(filter-out src/main.c,$(SRCS)) $(OBJS) $(CPPFLAGS) $(LIBS)' p)
cc -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Iinclude -O2 -o build/image/frame src/image/bench/frame.c $srcs
export XDG_RUNTIME_DIR=$(mktemp -d)
printf 'output HEADLESS-1 resolution 1280x800 scale 1\ndefault_border none\n' > $XDG_RUNTIME_DIR/sway.cfg
WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 sway -c $XDG_RUNTIME_DIR/sway.cfg >/dev/null 2>&1 &
sway=$!
trap 'kill $sway; rm -rf $XDG_RUNTIME_DIR' EXIT
until ls $XDG_RUNTIME_DIR/wayland-* >/dev/null 2>&1; do sleep 0.1; done
export WAYLAND_DISPLAY=$(basename $(ls $XDG_RUNTIME_DIR/wayland-* | grep -v lock | head -1))
mkdir -p $XDG_RUNTIME_DIR/cfg/homeros
[ -z "${4:-}" ] || cp "$4" $XDG_RUNTIME_DIR/cfg/homeros/wallpaper
for i in $(seq $runs); do
	if [ -z "${4:-}" ]; then build/image/frame "$img" $n 0; build/image/frame "$img" $n 1; continue; fi
	for m in 0 2; do
		echo -n "no wallpaper "; build/image/frame "$img" $n $m
		echo -n "wallpaper    "; XDG_CONFIG_HOME=$XDG_RUNTIME_DIR/cfg build/image/frame "$img" $n $m
	done
done
