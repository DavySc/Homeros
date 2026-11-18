#!/usr/bin/env bash
# What a real xterm sends for Delete and its modifier forms, and whether
# Delete deletes in Homeros running inside it.  usage: xterm.sh path/to/homeros
here=$(cd "$(dirname "$0")" && pwd); h=$(realpath "$1"); t=$(mktemp -d)
Xvfb :97 -screen 0 800x600x24 >/dev/null 2>&1 & xv=$!
export DISPLAY=:97; sleep 1
press() { xdotool mousemove 100 100; sleep 0.3; for k in "$@"; do xdotool key "$k"; sleep 0.3; done; }
for k in Delete ctrl+Delete shift+Delete alt+Delete ctrl+shift+Delete KP_Delete Insert Home End Prior Next F5; do
	xterm -geometry 80x24+0+0 -e python3 "$here/rawcap.py" "$t/cap" 2.5 & sleep 1.2
	press $k; wait $!
	printf 'xterm %-18s %s\n' "$k" "$(tr '\n' ' ' < "$t/cap")"
done
for i in 1 2 3; do
	printf 'abc\nsecond\n' > "$t/f.txt"
	xterm -geometry 80x24+0+0 -e "$h" "$t/f.txt" & sleep 1.2
	press Delete i Delete Escape colon w q Return; wait $!
	printf 'xterm: Homeros, Delete i Delete Esc :wq on "abc": %s\n' "$(head -1 "$t/f.txt")"
done
kill $xv; rm -rf "$t"
