#!/bin/bash
# usage: mko2.sh <tree> <probe.c> <out>: the probe against the tree's sources at -O2, no sanitizers (for timings)
set -e
tree=$1; src=$2; out=$3
cd $tree
line=$(make -n test TESTS=ZZZ 2>/dev/null | grep -m1 'tests/test_\$t.c')
flags=$(echo "$line" | sed 's/ -o build.*//; s/^ *gcc//; s/-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all/-O2/')
srcs=$(echo "$line" | sed 's/.*tests\/test_\$t.c //; s/ || exit 1.*//')
gcc $flags -Wno-error -Wno-unused-result -Itests -Isrc/sql -I$(dirname $src) -o $out $src $srcs
