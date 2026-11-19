#!/usr/bin/env bash
# kill.sh N [undo.c]: N kills of the writer under Wine, a read after each.
# Run from the repo root; needs zig and wine on PATH.
set -u
n=${1:-100}; src=${2:-src/undo/undo.c}; here=$(cd "$(dirname "$0")" && pwd)
mkdir -p build/kill && rm -f build/kill/kill.un*
zig cc -target x86_64-windows-gnu -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Iinclude -O1 \
  -o build/kill/killw.exe "$here/killw.c" "$src" src/text/*.c || exit 1
cd build/kill
export WINEDEBUG=-all WINEPREFIX=$PWD/prefix
wine killw.exe read >/dev/null 2>&1   # makes the prefix
declare -A c; seqs=
for i in $(seq "$n"); do
	wine killw.exe write >w.log 2>/dev/null &
	sleep 0.$(( RANDOM % 9 + 1 ))$(( RANDOM % 10 ))
	pkill -9 -f killw.exe; wait 2>/dev/null   # bash reports some of the kills on stderr
	[ -s w.log ] && { c["$(cat w.log)"]=$(( ${c["$(cat w.log)"]:-0} + 1 )); }
	out=$(wine killw.exe read 2>/dev/null | tr -d '\r'); r=${out%% *}
	[ "$r" = ok ] && seqs+=" ${out##* }"
	c[$r]=$(( ${c[$r]:-0} + 1 ))
done
echo "states written before each kill (seq at the read): $(echo $seqs | tr " " "\n" | sort -n | sed -n '1p;$p' | tr "\n" " ")(least, most)"
for k in "${!c[@]}"; do echo "$k: ${c[$k]}"; done
