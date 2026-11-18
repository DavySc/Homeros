#!/usr/bin/env bash
# The Windows half of package 17: builds tests/test_git_win.c for Windows
# with zig cc and runs it under Wine, as src/file/check.sh does for package
# 11.  UBSan traps; ASan has no runtime for this target, so it runs without.
set -eu
cd "$(dirname "$0")/../.."
for t in zig wine; do
	command -v $t >/dev/null || { echo "src/git/check.sh: $t is not on PATH"; exit 2; }
done
pick() { if ls src/$1/*.c >/dev/null 2>&1; then echo src/$1/*.c; else echo ref/$1.c; fi; }
mkdir -p build/win
[ -n "${XDG_RUNTIME_DIR:-}" ] || { export XDG_RUNTIME_DIR=$PWD/build/win/run; mkdir -p -m 700 "$XDG_RUNTIME_DIR"; }
# a prefix of its own, as package 11's; Wine's Recycle Bin is the
# freedesktop trash under XDG_DATA_HOME, which the test looks in
export WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" WINEPREFIX=$PWD/build/win/prefix-git
export XDG_DATA_HOME=$PWD/build/win/git-data TEST_TRASH=$PWD/build/win/git-data/Trash/files
rm -rf "$XDG_DATA_HOME"
exe=build/win/test_git_win.exe
zig cc -target x86_64-windows-gnu -std=c17 -Wall -Wextra -Werror -D_DEFAULT_SOURCE -Iinclude -O1 -g \
  -fsanitize=undefined -fsanitize-trap=undefined -o "$exe" tests/test_git_win.c src/git/*.c \
  $(pick core) $(pick text) $(pick undo) $(pick uni) $(pick loop) $(pick file) $(pick grid) $(pick theme) $(pick lsp) -lshell32
timeout 300 wine "$exe" || { echo "FAIL test_git_win"; exit 1; }
