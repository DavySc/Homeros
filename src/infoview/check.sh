#!/usr/bin/env bash
# The Windows half of package 14: builds homeros-infoview.exe and
# tests/test_infoview_win.c beside it with zig cc and runs the test under
# Wine (on Windows itself, CI's windows job, there), as src/loop/check.sh.
# UBSan traps; ASan has no runtime for this target.
set -eu
cd "$(dirname "$0")/../.."
. tools/winpass.sh
run=wine; [ "${OS:-}" = Windows_NT ] && run=
for t in zig $run; do
	command -v $t >/dev/null || { echo "src/infoview/check.sh: $t is not on PATH"; exit 2; }
done
mkdir -p build/win
[ -z "$run" ] || [ -n "${XDG_RUNTIME_DIR:-}" ] || { export XDG_RUNTIME_DIR=$PWD/build/win/run; mkdir -p -m 700 "$XDG_RUNTIME_DIR"; }
export WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" WINEPREFIX=$PWD/build/win/prefix-infoview
cc="zig cc -target x86_64-windows-gnu -std=c17 -Wall -Wextra -Werror -Iinclude -O1 -g -fsanitize=undefined -fsanitize-trap=undefined"
$cc -o build/win/homeros-infoview.exe src/infoview/serve/serve.c src/str/str.c -lws2_32 -lbcrypt
exe=build/win/test_infoview_win.exe
$cc -o "$exe" tests/test_infoview_win.c src/loop/*.c src/str/str.c -lws2_32
pass 120 "$exe" $run || { echo "FAIL test_infoview_win"; exit 1; }
