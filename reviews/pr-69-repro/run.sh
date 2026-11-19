#!/bin/bash
# Before/after comparison for PR 69.  usage: run.sh BASE_TREE PR_TREE [N]
# Formats N tsql.py and N soup.py texts (whole, and over the middle third)
# with sqlfmt.c from both trees under ASan+UBSan and compares every byte.
set -e
H=$(cd "$(dirname "$0")" && pwd); B=$1; P=$2; N=${3:-3000}; T=$(mktemp -d); trap 'rm -rf $T' EXIT
for t in $B $P; do
	gcc -std=c17 -D_DEFAULT_SOURCE -O2 -fsanitize=address,undefined -I$t/include -o $T/drv$((++k)) $H/drv.c $t/src/sqlfmt/sqlfmt.c $t/src/syntax/tsql.c $t/src/text/text.c
done
mkdir $T/in
for i in $(seq 1 $N); do python3 -I $P/src/sqlfmt/oracle/tsql.py $i > $T/in/t$i.sql; python3 -I $P/src/sqlfmt/oracle/soup.py $i > $T/in/s$i.sql; done
ls $T/in/* | xargs $T/drv1 > $T/a; ls $T/in/* | xargs $T/drv2 > $T/b
cmp $T/a $T/b && echo "$((2 * N)) texts, $(wc -c < $T/a) bytes of output: identical"
