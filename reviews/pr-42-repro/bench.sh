#!/usr/bin/env bash
# PR 42's own bench (src/syntax/bench/frame.c at the PR head), with the base's
# syntax.c ("old") and the PR's ("new"), alternating, seeds 1..RUNS.
# From the repo root, after make.  usage: bench.sh BASE RUNS KEYS FILE...
set -e
base=$1 runs=$2 keys=$3; shift 3
syn=build/syntax
mkdir -p build/pr42-old
git show $base:src/syntax/syntax.c > build/pr42-old/syntax.c
for side in old new; do
  [ $side = old ] && sx=build/pr42-old/syntax.c || sx=src/syntax/syntax.c
  gcc -O2 -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Iinclude -Isrc/syntax -I$syn/src/tree-sitter/lib/include -I$syn/src/tree-sitter/lib/src \
    -Wl,--wrap=syntax_edit,--wrap=syntax_faces,--wrap=text_line_of,--wrap=text_line_start \
    -o build/pr42-frame-$side src/syntax/bench/frame.c \
    $(for m in text undo uni core keys grid loop file theme tools git lsp; do ls src/$m/*.c 2>/dev/null || echo ref/$m.c; done) \
    src/syntax/lmatch.c src/syntax/release.c $sx $syn/tree-sitter.o $syn/queries.o $(find $syn/g -name '*.o') -lm
done
for f in "$@"; do for r in $(seq 1 "$runs"); do for side in old new; do
  echo "$side seed $r $(./build/pr42-frame-$side "$f" "$keys" "$r")"
done; done; done
