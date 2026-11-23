#!/usr/bin/env bash
# The Windows half of package 13: tests/test_lsp.c (the client against its
# fake server, the goals window) built for Windows and run under Wine.
set -eu
cd "$(dirname "$0")/../.."
. tools/winpass.sh
for t in zig wine; do
	command -v $t >/dev/null || { echo "src/lsp/check.sh: $t is not on PATH"; exit 2; }
done
. src/tools/ghostty-win.sh
SYN=build/syntax
srcs=$(make -s -n wincc | head -1 | tr ' ' '\n' | grep '\.c$' | grep -v -e '^src/main\.c$' -e '^src/win/')
mkdir -p build/win/tools
[ -n "${XDG_RUNTIME_DIR:-}" ] || { export XDG_RUNTIME_DIR=$PWD/build/win/run; mkdir -p -m 700 "$XDG_RUNTIME_DIR"; }
export WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" WINEPREFIX=$PWD/build/win/prefix-lsp
export LANG=C.UTF-8   # Wine names host files in the locale's charset: ü needs UTF-8
# tree-sitter and the grammars outside our warnings, as src/syntax/build.mk
objs=
for g in $(sed -n '/^SYN_GRAMMARS/,/[^\\]$/p' src/syntax/build.mk | tr -d '\\' | cut -d= -f2); do
	o=build/win/tools/$(echo $g | tr / _).o
	[ -f $o ] || zig cc -target x86_64-windows-gnu -O2 -c -I$(dirname $SYN/src/$g) $SYN/src/$g.c -o $o
	objs="$objs $o"
done
inc="-I$SYN/src/tree-sitter/lib/include -I$SYN/src/tree-sitter/lib/src"
make -s $SYN/queries.c build/win/stb.o build/ghostty/windows.lib   # stb: src/image/build.mk; thread 0's, agreed
zig cc -target x86_64-windows-gnu -O2 -c $SYN/queries.c -o build/win/tools/queries.o
[ -f build/win/tools/ts.o ] || zig cc -target x86_64-windows-gnu -O2 -c $inc $SYN/src/tree-sitter/lib/src/lib.c -o build/win/tools/ts.o
exe=build/win/test_lsp.exe
zig cc -target x86_64-windows-gnu -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Iinclude -I/usr/include/stb $inc -O1 -g \
  -fsanitize=undefined -fsanitize-trap=undefined -o "$exe" tests/test_lsp.c $srcs build/win/tools/queries.o build/win/tools/ts.o $objs build/win/stb.o $GHOSTTY_WIN
pass 600 "$exe" wine || { echo "FAIL test_lsp"; exit 1; }
