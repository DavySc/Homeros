#!/bin/bash
# battery.sh <tag> <rounds>: per round, the clean head's probe twice and its test_org twice, each thin plant's test_org
# twice (a pass is an escape) and its probe once
d=$(dirname "$(readlink -f "$0")"); tag=$1; cd ${R:-/home/claude/Homeros}
for r in $(seq $2); do
  for i in 1 2; do $d/p-head 2>> $d/out/$tag-head.txt >/dev/null
    if $d/t-head >/dev/null 2>$d/err.$tag; then echo "head pass"; else echo "head FAIL $(grep -m1 -o 'test_org.c:[0-9]*: CHECK([^)]*)' $d/err.$tag)"; fi >> $d/out/$tag-alt.txt; done
  for n in 103 133 118 117 148 91 104 150; do
    for i in 1 2; do if $d/q/$n >/dev/null 2>$d/err.$tag; then echo "$n ESCAPED"; else echo "$n caught $(grep -m1 -o 'test_org.c:[0-9]*' $d/err.$tag)"; fi >> $d/out/$tag-plants.txt; done
    timeout 300 $d/p/$n 2>> $d/out/$tag-$n.txt >/dev/null
  done
done
echo "battery $tag done"
