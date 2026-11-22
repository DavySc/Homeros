#!/usr/bin/env bash
# The Windows half of package 17: builds tests/test_git_win.c and
# tests/test_git.c (the magit traces) for Windows with zig cc and runs them
# under Wine, as src/file/check.sh does for package 11, with MinGit, Git for
# Windows' own minimal build, as the git.  UBSan traps; ASan has no runtime
# for this target, so it runs without.  On Windows itself (CI's windows
# job) it builds and runs test_git_win alone: the traces need the bridge
# below, which is Wine's.
set -euo pipefail
cd "$(dirname "$0")/../.."
. tools/winpass.sh
run=wine; [ "${OS:-}" = Windows_NT ] && run=
for t in zig $run; do
	command -v $t >/dev/null || { echo "src/git/check.sh: $t is not on PATH"; exit 2; }
done
pick() { if ls src/$1/*.c >/dev/null 2>&1; then echo src/$1/*.c; else echo ref/$1.c; fi; }
mkdir -p build/win
if [ -n "$run" ]; then
[ -n "${XDG_RUNTIME_DIR:-}" ] || { export XDG_RUNTIME_DIR=$PWD/build/win/run; mkdir -p -m 700 "$XDG_RUNTIME_DIR"; }
# a prefix of its own, as package 11's; Wine's Recycle Bin is the
# freedesktop trash under XDG_DATA_HOME, kept here (test_git looks in it)
export WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" WINEPREFIX=$PWD/build/win/prefix-git
# file names in UTF-8, as the bridge's shell writes them
export LANG=C.UTF-8
# a server left from an earlier run keeps that run's environment
wineserver -k 2>/dev/null || true
export XDG_DATA_HOME=$PWD/build/win/git-data TEST_TRASH=$PWD/build/win/git-data/Trash/files
rm -rf "$XDG_DATA_HOME"
# a prefix made now is made by a server without this environment: the
# trash would go elsewhere on that first run (measured, 4 of 4 runs); make
# it, and let that server end, first
wine wineboot -i; wineserver -w
fi
for t in test_git_win ${run:+test_git}; do
	zig cc -target x86_64-windows-gnu -std=c17 -Wall -Wextra -Werror -D_DEFAULT_SOURCE -Iinclude -O1 -g \
	  -fsanitize=undefined -fsanitize-trap=undefined -o build/win/$t.exe tests/$t.c src/git/*.c \
	  $(pick core) $(pick text) $(pick undo) $(pick uni) $(pick keys) $(pick loop) $(pick file) $(pick grid) \
	  $(pick theme) $(pick fold) $(pick kitten) $(pick lsp) $(pick sqlfmt) $(pick snip) $(pick tools) $(pick shada) $(pick str) $(pick org) src/syntax/tsql.c -lshell32   # str: package 17's, agreed by thread 0; org: keys calls org_todo_step
done
pass 300 build/win/test_git_win.exe $run || { echo "FAIL test_git_win"; exit 1; }
[ -n "$run" ] || exit 0

# MinGit, pinned by its hash
mg=build/win/mingit-2.47.1
if [ ! -x $mg/cmd/git.exe ]; then
	curl -sSfL -o build/win/mingit.zip \
	  https://github.com/git-for-windows/git/releases/download/v2.47.1.windows.1/MinGit-2.47.1-64-bit.zip
	echo "50b04b55425b5c465d076cdb184f63a0cd0f86f6ec8bb4d5860114a713d2c29a  build/win/mingit.zip" | sha256sum -c --quiet
	rm -rf $mg && mkdir -p $mg && unzip -q build/win/mingit.zip -d $mg && rm build/win/mingit.zip
fi
export WINEPATH="Z:$(pwd -P)/$mg/cmd"
# The bridge: MinGit's shell cannot start a second program under Wine
# (measured, 3 runs), so test_git's scripts run in this shell instead
# (its run()): it writes req.sh, this runs it and writes out
# The scratch files live outside any repository, as on Linux: the core
# attaches each file it opens to its repository
export TEST_BRIDGE=$PWD/build/win/git-bridge TEST_TMP=$(mktemp -d /tmp/homeros-git-XXXXXX)
rm -rf "$TEST_BRIDGE" && mkdir -p "$TEST_BRIDGE"
( while [ ! -e "$TEST_BRIDGE/stop" ]; do
	if [ -e "$TEST_BRIDGE/req.sh" ]; then
		sh "$TEST_BRIDGE/req.sh" > "$TEST_BRIDGE/out.part" 2>&1 || true
		rm "$TEST_BRIDGE/req.sh"
		mv "$TEST_BRIDGE/out.part" "$TEST_BRIDGE/out"
	else sleep 0.002; fi
done ) &
trap 'touch "$TEST_BRIDGE/stop"; rm -rf "$TEST_TMP"' EXIT
pass 1800 build/win/test_git.exe wine || { echo "FAIL test_git (Windows)"; exit 1; }
