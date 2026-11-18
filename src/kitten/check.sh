#!/usr/bin/env bash
# The save kitten on Windows: builds tests/test_kitten_win.c with zig cc and
# runs it under Wine, as src/loop/check.sh does.  UBSan traps; ASan has no
# runtime for this target.
set -eu
cd "$(dirname "$0")/../.."
for t in zig wine; do
	command -v $t >/dev/null || { echo "src/kitten/check.sh: $t is not on PATH"; exit 2; }
done
mkdir -p build/win
[ -n "${XDG_RUNTIME_DIR:-}" ] || { export XDG_RUNTIME_DIR=$PWD/build/win/run; mkdir -p -m 700 "$XDG_RUNTIME_DIR"; }
export WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" WINEPREFIX=$PWD/build/win/prefix-kitten
pick() { if ls src/$1/*.c >/dev/null 2>&1; then echo src/$1/*.c; else echo ref/$1.c; fi; }
exe=build/win/test_kitten_win.exe
zig cc -target x86_64-windows-gnu -std=c17 -Wall -Wextra -Werror -Iinclude -O1 -g \
  -fsanitize=undefined -fsanitize-trap=undefined -o "$exe" tests/test_kitten_win.c src/kitten/*.c \
  src/loop/*.c $(pick grid) $(pick uni) -lshell32
timeout 120 wine "$exe" || { echo "FAIL test_kitten_win"; exit 1; }
