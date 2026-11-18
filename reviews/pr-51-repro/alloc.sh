#!/usr/bin/env bash
# Builds alloc.c with every grammar's scanner compiled with malloc, calloc
# and realloc renamed (tree-sitter's runtime and the parsers as the Makefile
# builds them) and runs it.  From the repo root after `make compilers`.
# Usage: alloc.sh FILE LINE [FILE LINE]...
S=build/syntax; I="-I$S/src/tree-sitter/lib/include -I$S/src/tree-sitter/lib/src"; mkdir -p build/rv/sc
objs=
for g in lua/src markdown/tree-sitter-markdown/src markdown/tree-sitter-markdown-inline/src sql; do
	o=build/rv/sc/$(echo $g | tr / _).o
	gcc -O2 -c -I$S/src/$g -Dmalloc=rv_scanner_malloc -Dcalloc=rv_scanner_calloc -Drealloc=rv_scanner_realloc \
	  -include stdlib.h $S/src/$g/scanner.c -o $o || exit 1
	objs="$objs $o"
done
gcc -std=c17 -D_DEFAULT_SOURCE -O2 -Iinclude $I -o build/rv/alloc reviews/pr-51-repro/alloc.c src/text/*.c src/syntax/*.c \
  $S/tree-sitter.o $(find $S/g -name 'parser.o') $objs $S/queries.o || exit 1
while [ $# -ge 2 ]; do ./build/rv/alloc "$1" "$2"; shift 2; done
