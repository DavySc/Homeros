#!/bin/sh
# Starts a headless sway and runs "$@" in it: sway.sh CMD ARGS...
d=$(mktemp -d /tmp/pr68-XXXXXX)
export XDG_RUNTIME_DIR=$d
printf 'output HEADLESS-1 mode 1280x800\ndefault_border none\n' > $d/sway.cfg
WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 sway -c $d/sway.cfg >/dev/null 2>&1 &
sp=$!
for i in $(seq 100); do s=$(ls $d | grep '^wayland-[0-9]*$' | head -1); [ -n "$s" ] && break; sleep 0.1; done
export WAYLAND_DISPLAY=$s PR68_DIR=$d
"$@"; rc=$?
kill $sp; wait $sp 2>/dev/null
[ -n "$KEEP" ] || rm -rf $d
exit $rc
