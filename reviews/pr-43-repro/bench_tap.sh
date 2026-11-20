#!/bin/sh
# sh bench_tap.sh N PAGES: from the repo's top, after make compilers.
set -eu
here=$(dirname "$0")
cmd=$(make -n compilers | grep -m1 '^gcc .*-o build/gcc/homeros ' | sed "s| -o build/gcc/homeros | -Wno-unused-function -o build/gcc/r43_bench_tap $here/bench_tap.c |; s| src/infoview/infoview.c||; s| src/main.c||")
eval "$cmd"
for i in 1 2 3 4 5; do ./build/gcc/r43_bench_tap "$@"; done
