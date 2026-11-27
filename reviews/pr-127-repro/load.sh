#!/bin/bash
# load.sh <kind> <tag>: the ratio check under load, 10 alternating rounds of 60 per side
# kind test: 6 copies of test_org's probe with plant 91 in a loop (what a plant shard runs beside it); spin: 8 busy loops
kind=$1; tag=$2; pids=()
for i in 1 2 3 4 5 6 7 8; do
  if [ $kind = test ]; then [ $i -le 6 ] && { (while :; do q/91 >/dev/null 2>&1; done) & pids+=($!); }
  else (while :; do :; done) & pids+=($!); fi
done
sleep 2
for i in 1 2 3 4 5 6 7 8 9 10; do ./r-head 60 0 >> out/ratio-$tag-head.txt; ./r-plant 60 0 >> out/ratio-$tag-plant.txt; done
for p in "${pids[@]}"; do pkill -P $p; kill $p; done 2>/dev/null
pkill -f q/91
