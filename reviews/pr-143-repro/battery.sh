#!/bin/bash
# battery.sh <tag> [rounds]: the measurements of one load state, interleaved round by round so each sees the same load
# per round: 4 probe runs of the clean head, main's and the head's test_org in turn (4 each), each thin-margin plant's
# real test_org twice (a pass is an escape) and its probe once, 2 rounds of each alternative reference
d=$(dirname "$(readlink -f "$0")"); tag=$1; cd ${R:-/home/claude/Homeros}
thin="103 133 118 148 117 91 104"
for r in $(seq ${2:-15}); do
  for i in 1 2 3 4; do $d/p-head 2>> $d/out/$tag-head.txt >/dev/null; done
  for i in 1 2 3 4; do for t in main head; do
    if $d/t-$t >/dev/null 2>$d/err.$tag; then echo "$t pass $(cut -d' ' -f1 /proc/loadavg)"; else echo "$t FAIL $(cut -d' ' -f1 /proc/loadavg) $(grep -m1 -o 'test_org.c:[0-9]*: CHECK([^)]*)' $d/err.$tag)"; fi >> $d/out/$tag-alt.txt
  done; done
  for n in $thin; do
    for i in 1 2; do if $d/q/$n >/dev/null 2>$d/err.$tag; then echo "$n ESCAPED"; else echo "$n caught $(grep -m1 -o 'test_org.c:[0-9]*' $d/err.$tag)"; fi >> $d/out/$tag-plants.txt; done
    timeout 120 $d/p/$n 2>> $d/out/$tag-$n.txt >/dev/null
  done
  for b in a-head x/alt1 x/alt2 x/alt4; do $d/$b 2 2>> $d/out/alt-$tag-$(basename $b).txt; done
done
echo "battery $tag done"
