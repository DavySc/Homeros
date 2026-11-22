#!/usr/bin/env bash
# The Windows half of package 2's check, as src/file/check.sh: the Windows
# build's compiler (zig cc) on undo.c, then tests/test_undo_win.c under Wine.
# UBSan traps; ASan has no runtime for this target.  tools/planted.sh reads
# this output: the command line (-Werror) is not echoed, and a missing tool
# is said in words it does not count as a caught plant.
set -eu
cd "$(dirname "$0")/../.."
. tools/winpass.sh
for t in zig wine; do
	command -v $t >/dev/null || { echo "src/undo/win.sh: $t is not on PATH"; exit 2; }
done
mkdir -p build/win
# Wine wants a runtime directory; CI has one, a bare shell may not
[ -n "${XDG_RUNTIME_DIR:-}" ] || { export XDG_RUNTIME_DIR=$PWD/build/win/run; mkdir -p -m 700 "$XDG_RUNTIME_DIR"; }
# a prefix of its own, for the reason src/file/check.sh gives
export WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" WINEPREFIX=$PWD/build/win/prefix-undo
zig cc -target x86_64-windows-gnu -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Iinclude -O1 -g \
  -fsanitize=undefined -fsanitize-trap=undefined \
  -o build/win/test_undo_win.exe tests/test_undo_win.c src/undo/undo.c src/text/*.c
cd build/win
pass 300 test_undo_win.exe wine || { echo "FAIL test_undo_win (Windows, under Wine)"; exit 1; }
