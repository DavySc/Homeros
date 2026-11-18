#!/usr/bin/env bash
# Builds the review's probes into build/.  From the repo root, after make.
set -e
syn=build/syntax
L="src/text/*.c src/syntax/lmatch.c src/syntax/release.c $syn/tree-sitter.o $syn/queries.o $(find $syn/g -name '*.o')"
I="-Iinclude -I$syn/src/tree-sitter/lib/include -I$syn/src/tree-sitter/lib/src"
gcc -O2 -std=c17 -D_DEFAULT_SOURCE $SAN $I -o build/pr42-diff${SAN:+-san} reviews/pr-42-repro/diff.c $L
gcc -O2 -std=c17 -D_DEFAULT_SOURCE $I -o build/pr42-directed reviews/pr-42-repro/directed.c $L
