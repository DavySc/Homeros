#!/bin/sh
# sh build.sh NAME: builds NAME.c against the tree (run from the repo's
# top, after `make test TESTS=infoview` built the objects) as
# build/san/r43_NAME, with the unit test's helpers (everything in
# tests/test_infoview.c before its main) as build/iv_gen.h.
set -eu
here=$(dirname "$0")
sed -n '1,/^int main/p' tests/test_infoview.c | sed '$d' > build/iv_gen.h
cmd=$(make -n test TESTS=infoview | grep -m1 'tests/test_$$t.c\|tests/test_\$t.c')
cmd=$(echo "$cmd" | sed 's/ || exit 1.*//; s/^ *//')
cmd=$(echo "$cmd" | sed "s| -o build/san/test_\$\$*t tests/test_\$\$*t.c| -Ibuild -Itests -Wno-unused-function -o build/san/r43_$1 $here/$1.c|")
eval "$cmd"
