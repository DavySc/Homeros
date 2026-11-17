#!/usr/bin/env bash
# The org parser against Org's own (org-element in Emacs 31.1, from PATH):
# ORG_N random org texts and ORG_N mutations of design/org/synthetic-agenda.org,
# compared element by element and by position.  Run from the repo root:
# bash src/org/oracle/check.sh [SEED]
set -e
n=${ORG_N:-500} seed=${1:-$(date +%s)} d=build/org-oracle
rm -rf $d; mkdir -p $d/in
gcc -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
	-Iinclude -Isrc/org -o $d/dump src/org/oracle/dump.c src/org/org.c $(ls src/text/*.c 2>/dev/null || echo ref/text.c) \
	$(ls src/uni/*.c 2>/dev/null || echo ref/uni.c)
python3 src/org/oracle/gen.py $seed $n $d/in
python3 src/org/oracle/gen.py $seed $n $d/in design/org/synthetic-agenda.org
emacs -Q --batch -l src/org/oracle/oracle.el $d/in/*.org > $d/emacs.txt
$d/dump $d/in/*.org > $d/ours.txt
k=$(diff $d/emacs.txt $d/ours.txt | grep -c '^<' || true)
echo "org-oracle: seed $seed, $(ls $d/in | wc -l) files, $(grep -vc '^==' $d/emacs.txt) elements: $k differ"
[ "$k" = 0 ] || { diff $d/emacs.txt $d/ours.txt | head -20; exit 1; }
