#!/bin/bash
# usage: plant.sh N  : plants line N of review-plants.txt in a copy, builds test_keys_map against the shared objects, runs it
P=$(dirname "$(readlink -f "$0")"); R=$(cd "$P/../.." && pwd)
IFS='~' read -r name old new < <(sed -n "${1}p" $P/review-plants.txt)
t=$(mktemp -d); cd $R && cp -r Makefile assets include ref src tests tools design $t/ && ln -s $R/build $t/build
python3 - "$t/src/keys/keys.c" "$old" "$new" <<'PY' || { echo "BROKEN $name"; exit; }
import sys
p, old, new = sys.argv[1], sys.argv[2].replace('¶', '\n'), sys.argv[3].replace('¶', '\n')
s = open(p).read()
if s.count(old) != 1: sys.exit('plant does not apply: %r' % old)
open(p, 'w').write(s.replace(old, new))
PY
cd $t && make -s -f $P/probe.mk probe OUT=$t/tk SRC=tests/test_keys_map.c > $t/log 2>&1 || { echo "BUILD FAILED $name: $(grep -m1 error $t/log)"; exit; }
if ./tk > $t/run 2>&1; then echo "NOT CAUGHT  $name"; else echo "caught      $name: $(grep -m1 'CHECK(\|ERROR' $t/run | cut -c1-150)"; fi
rm -rf $t
