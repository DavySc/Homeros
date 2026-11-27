#!/bin/bash
# load.sh <kind> [rounds]: battery.sh under a load. spin: 8 busy loops on 4 cores; self: 6 copies of test_org in a loop
# (what a plant shard runs beside it); idle: nothing
d=$(dirname "$(readlink -f "$0")"); kind=$1; pids=()
cd ${R:-/home/claude/Homeros}
case $kind in
  spin) for i in 1 2 3 4 5 6 7 8; do (while :; do :; done) & pids+=($!); done;;
  self) for i in 1 2 3 4 5 6; do (while :; do $d/t-head >/dev/null 2>&1; done) & pids+=($!); done;;
esac
sleep 2
$d/battery.sh $kind $2
for p in "${pids[@]}"; do pkill -P $p; kill $p; done 2>/dev/null
