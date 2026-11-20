#!/usr/bin/env bash
# The Windows half of package 10: builds tests/test_loop_win.c for Windows
# with zig cc and runs it under Wine, as src/file/check.sh does for package
# 11.  UBSan traps; ASan has no runtime for this target, so it runs without.
# On Windows itself (CI's windows job) it runs there, for what Wine does not
# do (ResizePseudoConsole), as src/git/check.sh.
set -eu
cd "$(dirname "$0")/../.."
run=wine; [ "${OS:-}" = Windows_NT ] && run=
for t in zig $run; do
	command -v $t >/dev/null || { echo "src/loop/check.sh: $t is not on PATH"; exit 2; }
done
mkdir -p build/win
[ -n "${XDG_RUNTIME_DIR:-}" ] || { export XDG_RUNTIME_DIR=$PWD/build/win/run; mkdir -p -m 700 "$XDG_RUNTIME_DIR"; }
# a prefix of its own, as package 11's: a Wine server left running without
# a display would be reused by package 9's window test
export WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" WINEPREFIX=$PWD/build/win/prefix-loop
exe=build/win/test_loop_win.exe
zig cc -target x86_64-windows-gnu -std=c17 -Wall -Wextra -Werror -Iinclude -O1 -g \
  -fsanitize=undefined -fsanitize-trap=undefined -o "$exe" tests/test_loop_win.c src/loop/*.c -lshell32
# about 12 s; a planted spin must not cost CI 5 minutes
timeout 120 $run "$exe" || { echo "FAIL test_loop_win"; exit 1; }
