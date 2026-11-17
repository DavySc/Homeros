#!/bin/bash
# usage: idle.sh CMD...  : start, wait 3 s, then count context switches of every thread over 20 s; print threads, RSS
"$@" 2>/dev/null & p=$!; sleep 3
sw(){ for t in /proc/$p/task/*; do awk -v t=$(basename $t) '/ctxt_switches/ {s+=$2} END {print t, s}' $t/status; done; }
a=$(sw); sleep 20; b=$(sw)
echo "threads $(ls /proc/$p/task | wc -l), RSS $(awk '/VmRSS/ {print $2/1024 " MB"}' /proc/$p/status)"
join <(echo "$a") <(echo "$b") | awk -v m=$p '{d=$3-$2; tot+=d; if ($1==m) main=d} END {print "context switches in 20 s: main thread " main ", all threads " tot}'
kill $p
