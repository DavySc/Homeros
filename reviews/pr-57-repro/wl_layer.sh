#!/bin/bash
# usage: wl_layer.sh <worktree where make compilers has run (build/syntax, build/wl exist)>
D=$(cd "$(dirname "$0")" && pwd); cd "$1" || exit 2
L=$(make -n test TESTS=wl 2>/dev/null | grep -m1 -o 'tests/test_$t.c .*' | sed 's/^tests\/test_$t.c //; s/src\/wl\/wl.c//; s/ || exit 1.*//')
mkdir -p build/san
gcc -std=c17 -D_DEFAULT_SOURCE -Iinclude -Isrc/wl $(pkg-config --cflags freetype2 harfbuzz) -DHOMEROS_WL -Ibuild/wl -Ibuild/syntax/src/tree-sitter/lib/include -Ibuild/syntax/src/tree-sitter/lib/src -w -O1 -g -fsanitize=address,undefined \
  -o build/san/wl_layer "$D/wl_layer.c" $L || exit 2
# leaks off: fontconfig's at exit; the repro frees nothing
ASAN_OPTIONS=detect_leaks=0 ./build/san/wl_layer
