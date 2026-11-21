#!/bin/bash
# delete each line of fold.c in turn; list those whose removal compiles and passes the recorded replay
R=${1:-.}; M=$(mktemp -d)
n=$(wc -l < $R/src/fold/fold.c)
for i in $(seq 30 $n); do
  line=$(sed -n "${i}p" $R/src/fold/fold.c)
  case "$line" in ""|*"/*"*"*/"|"{"|"}"|*"	}"|*"*/") [[ "$line" =~ \; ]] || continue;; esac
  sed "${i}d" $R/src/fold/fold.c > $M/fold_m.c
  gcc -std=c17 -O1 -w -I$R/include -I$R/tests -o $M/tm $R/tests/test_fold.c $M/fold_m.c 2>/dev/null || continue
  if (cd $R && timeout 20 $M/tm >/dev/null 2>&1); then echo "SURVIVES $i: $line"; fi
done
