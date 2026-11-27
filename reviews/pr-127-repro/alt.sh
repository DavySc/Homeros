#!/bin/bash
# alt.sh <n> <tag>: main's and the head's test_org, unchanged, run in turn; one line per run
d=$(dirname "$(readlink -f "$0")")
for i in $(seq $1); do for t in main head; do
  cd $d/$t
  if ../t-$t >../o.$$ 2>&1; then echo "$2 $t pass $(cut -d' ' -f1 /proc/loadavg)"; else echo "$2 $t FAIL $(cut -d' ' -f1 /proc/loadavg) $(grep -m1 -o 'test_org.c:[0-9]*.*' ../o.$$ | cut -c1-90)"; fi
done; done; rm -f $d/o.$$
