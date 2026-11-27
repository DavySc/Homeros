#!/bin/bash
# esc.sh <tag> <rounds>: plants 103 and 133 against main's test (m/) and the head's (q/), in turn; a pass is an escape
d=$(dirname "$(readlink -f "$0")"); cd ${R:-/home/claude/Homeros}
for r in $(seq $2); do for n in 103 133; do for k in m q; do
  if $d/$k/$n >/dev/null 2>$d/err.esc; then echo "$n $k ESCAPED $(cut -d' ' -f1 /proc/loadavg)"; else echo "$n $k caught $(grep -m1 -o 'test_org.c:[0-9]*' $d/err.esc)"; fi >> $d/out/esc-$1.txt
done; done; done
