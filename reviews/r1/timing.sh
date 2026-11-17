#!/usr/bin/env bash
# 5 alternating runs per side on 100 MB: A's rope, B's rope, ropey.
cd /home/claude/rev
R=/home/claude/Homeros/design/proto/rss.py
echo "== author A's workload (50000 edits, 1000000 lookups each way)"
for i in 1 2 3 4 5; do for s in benchA_A benchA_B ropeyA; do echo -n "$s "; python3 $R bin/$s data/big.txt 50000 1000000 | tr '\n' ' '; echo; done; done
echo "== author B's workload (50000 edits and lookups)"
for i in 1 2 3 4 5; do for s in benchB_A benchB_B ropeyB; do echo -n "$s "; python3 $R bin/$s data/big.txt 50000 | tr '\n' ' '; echo; done; done
echo "== scenarios"
for w in open type backspace paste bigdel screen lines readall randedit; do for i in 1 2 3 4 5; do for s in scen_A scen_B ropescen; do echo -n "$s "; python3 $R bin/$s data/big.txt $w | tr '\n' ' '; echo; done; done; done
