#!/usr/bin/env bash
# Rerun the PR 38 review from the repo root: sh reviews/pr-38-repro/run.sh
# Needs gcc, clang (with compiler-rt) and libssl-dev.  Old code is undo.c at the PR's base.
set -e
R=reviews/pr-38-repro; O=${OUT:-build/pr38}; mkdir -p $O
git show ${BASE:-644a907}:src/undo/undo.c > $O/undo_old.c; cp src/undo/undo.c $O/undo_new.c
F="-std=c17 -D_DEFAULT_SOURCE -w -Iinclude -I$O"
for c in gcc clang; do for u in old new; do
  $c $F -O2 -DUNDO="\"undo_$u.c\"" -o $O/vec_${c}_$u $R/vec.c src/text/text.c -lcrypto
  $c $F -O2 -DUNDO="\"undo_$u.c\"" -o $O/b_${c}_$u $R/bench.c src/text/text.c
  $O/vec_${c}_$u 3000
done
  $c $F -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DUNDO='"undo_new.c"' -o $O/vec_${c}_san $R/vec.c src/text/text.c -lcrypto
  $O/vec_${c}_san 300
done
for a in "10MB 10485760 1" "50KB 51200 200"; do set -- $a
  for r in 1 2 3 4 5 6 7; do for b in gcc_old gcc_new clang_old clang_new; do echo "$1 $b $($O/b_$b $2 $3)"; done; done; done
