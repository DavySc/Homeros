#!/usr/bin/env bash
# The Windows half of src/shada: builds tests/test_shada_win.c for Windows
# with zig cc and runs it under Wine, as src/file/check.sh does.
set -eu
cd "$(dirname "$0")/../.."
. tools/winpass.sh
for t in zig wine; do
	command -v $t >/dev/null || { echo "src/shada/check.sh: $t is not on PATH"; exit 2; }
done
pick() { if ls src/$1/*.c >/dev/null 2>&1; then echo src/$1/*.c; else echo ref/$1.c; fi; }
mkdir -p build/win
[ -n "${XDG_RUNTIME_DIR:-}" ] || { export XDG_RUNTIME_DIR=$PWD/build/win/run; mkdir -p -m 700 "$XDG_RUNTIME_DIR"; }
export WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" WINEPREFIX=$PWD/build/win/prefix-shada
exe=build/win/test_shada_win.exe
zig cc -target x86_64-windows-gnu -std=c17 -Wall -Wextra -Werror -D_FILE_OFFSET_BITS=64 -Iinclude -O1 -g \
  -fsanitize=undefined -fsanitize-trap=undefined -o "$exe" tests/test_shada_win.c src/shada/*.c src/file/*.c src/loop/*.c $(pick text) -lshell32
pass 600 "$exe" wine || { echo "FAIL test_shada_win"; exit 1; }
