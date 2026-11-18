#!/bin/sh
# gone.sh /path/to/homeros : kill the compositor under the editor; does it quit or spin?
S=$(mktemp -d); chmod 700 $S; printf 'output HEADLESS-1 mode 1280x800\n' > $S/cfg
XDG_RUNTIME_DIR=$S WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 setsid sway -c $S/cfg >/dev/null 2>&1 &
sleep 1.5; sp=$(pgrep -n -x sway); echo text > $S/f.txt
XDG_RUNTIME_DIR=$S WAYLAND_DISPLAY=wayland-1 "$1" $S/f.txt & p=$!
sleep 1; kill -9 $sp; sleep 2
if kill -0 $p 2>/dev/null; then t1=$(awk '{print $14+$15}' /proc/$p/stat); sleep 2; t2=$(awk '{print $14+$15}' /proc/$p/stat)
  echo "still running; CPU ticks in 2 s: $((t2-t1)) of 200"; kill -9 $p
else wait $p; echo "exited with status $?"; fi
