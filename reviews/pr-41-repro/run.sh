#!/usr/bin/env bash
# Review of PR 41: builds probe.c against src/loop/win.c (or the file named
# by WINC) with zig cc and runs one probe under Wine, N times.
#   bash reviews/pr-41-repro/run.sh flood 5
#   bash reviews/pr-41-repro/run.sh early 5   (builds win.c with a 300 ms
#     pause between the child's start and the first read)
set -eu
cd "$(dirname "$0")/../.."
probe=$1 n=${2:-5} winc=${WINC:-src/loop/win.c}
mkdir -p build/win
[ -n "${XDG_RUNTIME_DIR:-}" ] || { export XDG_RUNTIME_DIR=$PWD/build/win/run; mkdir -p -m 700 "$XDG_RUNTIME_DIR"; }
export WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" WINEPREFIX=$PWD/build/win/prefix-review
src=$winc
if [ "$probe" = early ]; then
	src=build/win/win-early.c
	sed 's/if (ok) ResumeThread(pi.hThread);/if (ok) { ResumeThread(pi.hThread); Sleep(300); }/' "$winc" > "$src"
	cmp -s "$winc" "$src" && { echo "run.sh: the pause did not apply"; exit 2; }
fi
exe=build/win/probe-$probe.exe
zig cc -target x86_64-windows-gnu -std=c17 -Wall -Wextra -Werror -Iinclude -O1 -g \
  -fsanitize=undefined -fsanitize-trap=undefined -o "$exe" reviews/pr-41-repro/probe.c "$src"
for i in $(seq "$n"); do
	timeout 60 wine "$exe" "$probe" && echo "  run $i: exit 0" || echo "  run $i: exit $?"
done
