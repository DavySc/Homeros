#!/usr/bin/env bash
# Review of PR 59: builds probe.c against the tree (ASan, UBSan) and runs its cases.
set -eu
cd "$(dirname "$0")/../.."
[ -e build/syntax/queries.o ] || make -s test TESTS=kitten >/dev/null   # builds the tree-sitter objects
objs=$(ls build/syntax/tree-sitter.o build/syntax/queries.o; find build/syntax/g -name '*.o')
pick() { if ls src/$1/*.c >/dev/null 2>&1; then echo src/$1/*.c; else echo ref/$1.c; fi; }
srcs=$(for m in text undo uni core keys grid loop file syntax theme tools git lsp infoview kitten font org; do pick $m; done)
gcc -std=c17 -D_DEFAULT_SOURCE -Iinclude $(pkg-config --cflags freetype2 harfbuzz fontconfig) -Ibuild/syntax/src/tree-sitter/lib/include -Ibuild/syntax/src/tree-sitter/lib/src \
  -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -o build/probe59 reviews/pr-59-repro/probe.c $srcs $objs \
  $(pkg-config --libs freetype2 harfbuzz fontconfig) -lm
export ASAN_OPTIONS=detect_leaks=0
build/probe59 msg 'é-ü.txt'
build/probe59 msg '全角.md'
build/probe59 cmdheight
build/probe59 pick
build/probe59 tiny || true
# the cost per draw, without the sanitizers
gcc -std=c17 -D_DEFAULT_SOURCE -Iinclude $(pkg-config --cflags freetype2 harfbuzz fontconfig) -Ibuild/syntax/src/tree-sitter/lib/include -Ibuild/syntax/src/tree-sitter/lib/src \
  -O2 -o build/probe59o reviews/pr-59-repro/probe.c $srcs $objs $(pkg-config --libs freetype2 harfbuzz fontconfig) -lm
for i in 1 2 3 4 5; do build/probe59o bench; done
