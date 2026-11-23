#!/usr/bin/env bash
# The Windows half of the pickers' path rules: builds tests/test_tools_win.c
# for Windows (winlink, tools/winpass.sh) and runs it under Wine, as
# src/shada/check.sh does.
set -eu
cd "$(dirname "$0")/../.."
. tools/winpass.sh
for t in zig wine; do
	command -v $t >/dev/null || { echo "src/tools/check.sh: $t is not on PATH"; exit 2; }
done
[ -n "${XDG_RUNTIME_DIR:-}" ] || { export XDG_RUNTIME_DIR=$PWD/build/win/run; mkdir -p -m 700 "$XDG_RUNTIME_DIR"; }
export WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" WINEPREFIX=$PWD/build/win/prefix-tools
exe=build/win/test_tools_win.exe
winlink "$exe" tests/test_tools_win.c
pass 600 "$exe" wine || { echo "FAIL test_tools_win"; exit 1; }
