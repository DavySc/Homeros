#!/bin/bash
# stage.sh <tag>: the summary of one battery
cd "$(dirname "$(readlink -f "$0")")"; t=$1
python3 sum.py out/$t-head.txt | grep -E 'OVER|max.bound 0.[6-9]'
cut -d' ' -f1,2 out/$t-alt.txt | sort | uniq -c; grep FAIL out/$t-alt.txt | cut -d' ' -f1,4- | sort | uniq -c
cut -d' ' -f1,2 out/$t-plants.txt | sort | uniq -c | tr '\n' ';'; echo
python3 plantsum.py $t 103 133 118 148 117 91 104; python3 altsum.py out/alt-$t-*.txt 2>/dev/null
