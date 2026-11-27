#!/bin/bash
# runs.sh: the whole test_sql, alternating variants, against both live servers;
# one line per run: variant, exit status, the first CHECK that failed
cd /home/claude/wt159
export SQL_SA_PASSWORD=Homeros_Test1 SQL_PG_PASSWORD=Homeros_Test1
R=/tmp/claude-0/rv
for i in $(seq ${1:-20}); do
  for b in h0 h1 h2 m1 m2; do
    timeout 300 $R/$b > $R/run.out 2>&1; rc=$?
    echo "$b $rc $(grep -m1 -E 'CHECK\(|ERROR|differ' $R/run.out | grep -v ' 0 differ' | cut -c1-160)" >> $R/runs.txt
  done
done
echo finished >> $R/runs.txt
