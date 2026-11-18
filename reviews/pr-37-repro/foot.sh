#!/usr/bin/env bash
# The same as xterm.sh for foot, under a headless sway, keys from wtype.
# usage: foot.sh path/to/homeros
here=$(cd "$(dirname "$0")" && pwd); h=$(realpath "$1"); t=$(mktemp -d)
export XDG_RUNTIME_DIR=$t/run; mkdir -m 700 $XDG_RUNTIME_DIR
printf 'output HEADLESS-1 resolution 800x600\n' > $t/sway.conf
WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 WLR_RENDERER=pixman sway -c $t/sway.conf >$t/sway.log 2>&1 & sw=$!
sleep 2
export WAYLAND_DISPLAY=$(basename "$(ls $XDG_RUNTIME_DIR/wayland-* | grep -v lock | head -1)")
cap() { name=$1; shift
	foot python3 "$here/rawcap.py" "$t/cap" 4 >/dev/null 2>&1 & sleep 2.5
	wtype "$@"; wait $!
	printf 'foot %-18s %s\n' "$name" "$(tr '\n' ' ' < "$t/cap")"; }
foot --version
for i in 1 2 3 4 5; do cap Delete -k Delete; done
cap ctrl+Delete -M ctrl -k Delete -m ctrl
cap shift+Delete -M shift -k Delete -m shift
cap alt+Delete -M alt -k Delete -m alt
cap ctrl+shift+Delete -M ctrl -M shift -k Delete -m shift -m ctrl
cap KP_Delete -k KP_Delete
cap Insert -k Insert
cap Home -k Home
cap End -k End
cap Prior -k Prior
cap Next -k Next
cap F5 -k F5
for i in $(seq ${RUNS:-10}); do
	printf 'abc\nsecond\n' > "$t/f.txt"
	foot python3 "$here/relay.py" "$t/log" "$h" "$t/f.txt" >/dev/null 2>&1 & sleep 2.5
	wtype -k Delete -s 300 i -s 300 -k Delete -s 300 -k Escape -s 300 ':wq' -s 300 -k Return; wait $!
	printf 'foot: Homeros, Delete i Delete Esc :wq on "abc": %-3s foot sent: %s\n' "$(head -1 "$t/f.txt")" "$(tr '\n' ' ' < "$t/log")"
done
kill $sw; rm -rf "$t"
