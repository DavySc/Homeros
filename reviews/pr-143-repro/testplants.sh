#!/bin/bash
# testplants.sh: every "test TESTS=org" plant of tests/plants/org.txt through the standalone build (build.sh), one at a
# time; caught = test_org fails a CHECK. The other plants (org-conform, fuzz) need the Makefile: CI's plant shards
d=$(dirname "$(readlink -f "$0")"); P=${R:-/home/claude/Homeros}/tests/plants/org.txt
grep -n "~test TESTS=org~" $P | cut -d: -f1 | while read n; do
  name=$($d/plant.sh $P $n $d/tp.bin 2>&1 | tail -1)
  cd ${R:-/home/claude/Homeros}
  if timeout 300 $d/tp.bin >/dev/null 2>$d/tp.err; then echo "NOT CAUGHT $n $name"; else echo "caught $n $(grep -m1 -o 'test_org.c:[0-9]*: CHECK' $d/tp.err || tail -1 $d/tp.err | cut -c1-60) $name"; fi
done
