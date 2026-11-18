#!/usr/bin/env bash
# The differential probe over files, seeds and window heights.
# usage: sweep.sh FIRST LAST STEPS FILE...   (ROWS="20 50 120" by default)
first=$1 last=$2 steps=$3; shift 3
for f in "$@"; do for rows in ${ROWS:-20 50 120}; do for s in $(seq $first $last); do
  ./build/pr42-diff "$f" $s $steps $rows | tail -1 | sed "s/^/rows $rows /"
done; done; done
