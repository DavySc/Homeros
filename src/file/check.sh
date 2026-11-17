#!/usr/bin/env bash
# The Windows half of package 11: builds tests/test_file_win.c for Windows
# with zig cc and runs it under Wine, as src/win/check.sh does for package 9.
# UBSan traps; ASan has no runtime for this target, so it runs without.
set -eu
cd "$(dirname "$0")/../.."
for t in zig wine; do
	command -v $t >/dev/null || { echo "src/file/check.sh: $t is not on PATH"; exit 2; }
done
pick() { if ls src/$1/*.c >/dev/null 2>&1; then echo src/$1/*.c; else echo ref/$1.c; fi; }
mkdir -p build/win
# Wine wants a runtime directory; CI has one, a bare shell may not
[ -n "${XDG_RUNTIME_DIR:-}" ] || { export XDG_RUNTIME_DIR=$PWD/build/win/run; mkdir -p -m 700 "$XDG_RUNTIME_DIR"; }
export WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" WINEPREFIX=${WINEPREFIX:-$PWD/build/win/prefix}
exe=build/win/test_file_win.exe
zig cc -target x86_64-windows-gnu -std=c17 -Wall -Wextra -Werror -D_FILE_OFFSET_BITS=64 -Iinclude -O1 -g \
  -fsanitize=undefined -fsanitize-trap=undefined -o "$exe" tests/test_file_win.c src/file/*.c $(pick text)
timeout 600 wine "$exe" || { echo "FAIL test_file_win"; exit 1; }
