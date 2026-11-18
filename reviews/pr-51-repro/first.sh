#!/usr/bin/env bash
# Builds first.c as the editor is built (-O2, the Makefile's objects) and runs
# it 5 times per language, alternating.  From the repo root after `make compilers`.
S=build/syntax; mkdir -p build/rv
gcc -std=c17 -D_DEFAULT_SOURCE -O2 -Iinclude -I$S/src/tree-sitter/lib/include -I$S/src/tree-sitter/lib/src -o build/rv/first \
  reviews/pr-51-repro/first.c src/text/*.c src/syntax/*.c $S/tree-sitter.o $(find $S/g -name '*.o') $S/queries.o || exit 1
for i in 1 2 3 4 5; do for l in c lua md sql; do ./build/rv/first $l; done; done | paste - - | sort -s -k1,1
