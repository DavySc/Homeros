#!/usr/bin/env bash
# Review of PR 53: the core's Windows side of 'fileformat' ("dos,unix"),
# which tests/test_file_win.c does not reach (it links src/file only).
# Builds eol_drv.c with the Windows editor's sources (the Makefile's
# WIN_SRCS without the window and main) and runs it under Wine on a few
# files.  Run from the repo root after `make wincc` (it needs build/syntax).
set -eu
R=$(dirname "$0"); SYN=build/syntax
G="c/src/parser lua/src/parser lua/src/scanner markdown/tree-sitter-markdown/src/parser markdown/tree-sitter-markdown/src/scanner markdown/tree-sitter-markdown-inline/src/parser markdown/tree-sitter-markdown-inline/src/scanner"
srcs=$(make -s -f Makefile --eval 'p: ; @echo $(filter-out src/main.c $(wildcard src/win/*.c),$(WIN_SRCS))' p)
d=$(mktemp -d)
o=""
for g in $G; do zig cc -target x86_64-windows-gnu -O1 -c -I$(dirname $SYN/src/$g) -o $d/$(echo $g | tr / _).o $SYN/src/$g.c; o="$o $d/$(echo $g | tr / _).o"; done
zig cc -target x86_64-windows-gnu -std=c17 -D_DEFAULT_SOURCE -Iinclude -I$SYN/src/tree-sitter/lib/include -I$SYN/src/tree-sitter/lib/src \
  -O1 -g -fsanitize=undefined -fsanitize-trap=undefined -o $d/eol_drv.exe $R/eol_drv.c $srcs $SYN/src/tree-sitter/lib/src/lib.c $SYN/queries.c $o
export WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" WINEPREFIX=$d/prefix XDG_RUNTIME_DIR=$d/run
mkdir -p -m 700 $d/run
t() {   # name, bytes (printf format), wanted bytes after :w
	printf "$2" > $d/f
	out=$(cd $d && timeout 300 wine eol_drv.exe f </dev/null 2>/dev/null | tr -d '\r')
	got=$(od -An -c $d/f | tr -s ' ' | tr -d '\n'); want=$(printf "$2" | od -An -c | tr -s ' ' | tr -d '\n')
	echo "$1: $(echo "$out" | sed -n '1p' | sed 's/^open //') | $(echo "$out" | sed -n 's/^write //p') | $([ "$got" = "$want" ] && echo "byte for byte" || echo "CHANGED to $got")"
}
t "CR LF file" 'a\r\nb\r\n'
t "LF file" 'a\nb\n'
t "mixed" 'a\r\nb\nc\r\n'
t "no LF at all" 'abc'
t "empty" ''
t "CR LF, no final line end" 'a\r\nb'
t "lone CR, no LF" 'a\rb'
t "LF, no final line end" 'a\nb'
rm -f $d/f; out=$(cd $d && timeout 300 wine eol_drv.exe f </dev/null 2>/dev/null | tr -d '\r'); echo "new file: $(echo "$out" | sed -n 1p)"
rm -f $d/f; out=$(cd $d && printf '105 97 13 98 27' | timeout 300 wine eol_drv.exe f 2>/dev/null | tr -d '\r'); echo "new file, ia<CR>b<Esc>, :w: $(echo "$out" | sed -n 's/^write //p') | on disk: $(od -An -c $d/f | tr -s ' ')"
printf 'a\r\nb\r\n' > $d/f; (cd $d && printf '105 120 27' | timeout 300 wine eol_drv.exe f crash >/dev/null 2>&1) || true
out=$(cd $d && timeout 300 wine eol_drv.exe f </dev/null 2>/dev/null | tr -d '\r'); echo "CR LF file, ix<Esc>, killed, reopened, :w: $(echo "$out" | sed -n '1p') | on disk: $(od -An -c $d/f | tr -s ' ')"
wineserver -k 2>/dev/null || true
rm -rf $d
