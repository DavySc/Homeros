#!/usr/bin/env bash
# Builds reviews/package-12-repro/frame2.c against package 12 and the editor (each
# package's src/ where it has one, else the thin ref/),
# then runs it RUNS times (seeds 1..RUNS) per file.  From the repo root,
# after make.  usage: src/syntax/bench/run.sh RUNS KEYS FILE...
set -e
runs=$1 keys=$2; shift 2
syn=build/syntax
gcc -O2 -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Iinclude -I$syn/src/tree-sitter/lib/include \
  -DSYNTAX_QUERIES="\"$PWD/$syn/src/queries\"" -Wl,--wrap=syntax_edit,--wrap=syntax_faces,--wrap=text_line_of,--wrap=text_line_start \
  -o build/frame2 reviews/package-12-repro/frame2.c \
  $(for m in text undo uni core keys grid file; do ls src/$m/*.c 2>/dev/null || echo ref/$m.c; done) \
  src/syntax/*.c $syn/tree-sitter.o $(find $syn/g -name '*.o')
for f in "$@"; do
  for r in $(seq 1 "$runs"); do ./build/frame2 "$f" "$keys" "$r"; done
done
