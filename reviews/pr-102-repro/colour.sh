#!/usr/bin/env bash
# Frame time with and without an image (package 21): builds frame.c with the
# editor's flags, starts headless sway (pixman, 1280x800, as tests/test_wl.c)
# and runs text and image alternately, RUNS times each.
# Usage: src/image/bench/run.sh IMAGE.png [N frames] [RUNS]
set -eu
cd "$(dirname "$0")/../.."
img=$1 out=$2
make -s homeros >/dev/null
srcs=$(make -s --eval 'p: ; @echo $(filter-out src/main.c,$(SRCS)) $(OBJS) $(CPPFLAGS) $(LIBS)' p)
cc -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Iinclude -O2 -o build/image/colour reviews/pr-102-repro/colour.c $srcs
export XDG_RUNTIME_DIR=$(mktemp -d)
printf 'output HEADLESS-1 resolution 1280x800 scale 1\ndefault_border none\n' > $XDG_RUNTIME_DIR/sway.cfg
WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 sway -c $XDG_RUNTIME_DIR/sway.cfg >/dev/null 2>&1 &
sway=$!
trap 'kill $sway; rm -rf $XDG_RUNTIME_DIR' EXIT
until ls $XDG_RUNTIME_DIR/wayland-* >/dev/null 2>&1; do sleep 0.1; done
export WAYLAND_DISPLAY=$(basename $(ls $XDG_RUNTIME_DIR/wayland-* | grep -v lock | head -1))
build/image/colour "$img" "$out"
