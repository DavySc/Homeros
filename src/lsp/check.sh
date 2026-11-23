#!/usr/bin/env bash
# The Windows half of package 13: tests/test_lsp.c (the client against its
# fake servers, the goals window, the semantic tokens) built for Windows
# (winlink, tools/winpass.sh) and run under Wine, as src/tools/check.sh.
set -eu
cd "$(dirname "$0")/../.."
. tools/winpass.sh
for t in zig wine; do
	command -v $t >/dev/null || { echo "src/lsp/check.sh: $t is not on PATH"; exit 2; }
done
[ -n "${XDG_RUNTIME_DIR:-}" ] || { export XDG_RUNTIME_DIR=$PWD/build/win/run; mkdir -p -m 700 "$XDG_RUNTIME_DIR"; }
export WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" WINEPREFIX=$PWD/build/win/prefix-lsp
export LANG=C.UTF-8   # Wine names host files in the locale's charset: ü needs UTF-8
exe=build/win/test_lsp.exe   # pass wants the summary's name
winlink "$exe" tests/test_lsp.c
pass 600 "$exe" wine || { echo "FAIL test_lsp"; exit 1; }
