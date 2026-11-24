#!/usr/bin/env bash
# The review's frame-time bench: main against the PR without a wallpaper
# against the PR with one, alternating, RUNS times, for scrolling (0), typing
# (2) and a whole redraw with colour emoji (3).  Also sway's own CPU time per
# run (pixman: the compositor's blending is CPU here).
# Usage: bench.sh MAIN_TREE PR_TREE WALLPAPER [N frames] [RUNS]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
main=$1 pr=$2 wall=$3 n=${4:-500} runs=${5:-5}
for t in "$main" "$pr"; do (
	cd "$t"; make -s homeros >/dev/null
	srcs=$(make -s --eval 'p: ; @echo $(filter-out src/main.c,$(SRCS)) $(OBJS) $(CPPFLAGS) $(LIBS)' p)
	cc -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Iinclude -O2 -o build/image/frame2 "$here/frame2.c" $srcs
) done
export XDG_RUNTIME_DIR=$(mktemp -d)
printf 'output HEADLESS-1 resolution 1280x800 scale 1\ndefault_border none\n' > $XDG_RUNTIME_DIR/sway.cfg
WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 sway -c $XDG_RUNTIME_DIR/sway.cfg >/dev/null 2>&1 &
sway=$!
trap 'kill $sway; rm -rf $XDG_RUNTIME_DIR' EXIT
until ls $XDG_RUNTIME_DIR/wayland-* >/dev/null 2>&1; do sleep 0.1; done
export WAYLAND_DISPLAY=$(basename $(ls $XDG_RUNTIME_DIR/wayland-* | grep -v lock | head -1))
mkdir -p $XDG_RUNTIME_DIR/cfg/homeros $XDG_RUNTIME_DIR/none
cp "$wall" $XDG_RUNTIME_DIR/cfg/homeros/wallpaper
cpu() { awk '{print $14 + $15}' /proc/$sway/stat; }
one() { # label tree config mode
	local c0=$(cpu)
	echo -n "$1 "; XDG_CONFIG_HOME=$XDG_RUNTIME_DIR/$3 "$2/build/image/frame2" "$here/../../tests/test_image.png" $n $4 | tr -d '\n'
	echo " sway-ticks $(( $(cpu) - c0 ))"
}
for i in $(seq $runs); do for m in 0 2 3; do
	one "main        " "$main" none $m
	one "pr-unset    " "$pr" none $m
	one "pr-wallpaper" "$pr" cfg $m
done; done
