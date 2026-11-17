#!/bin/bash
# An outside writer before every file-system call of one file_save, for the
# swap path and the in-place path (a hard-linked file).  Run from the repo root.
set -e
D=$(mktemp -d); export XDG_STATE_HOME=$D/state
gcc -shared -fPIC -o $D/shim.so reviews/pr-16-repro/shim.c -ldl
gcc -Iinclude -o $D/steps reviews/pr-16-repro/steps.c src/file/*.c src/text/text.c
for mode in rename inplace same; do for kind in swap link; do
  echo "== outside writer: $mode; save path: $kind"
  for s in $(seq 1 18); do
    printf "%2d " $s
    SHIM_TRACE=1 VICTIM=$D/v MODE=$mode STEP=$s LD_PRELOAD=$D/shim.so $D/steps $D/v $kind 2>&1 | tr '\n' ' '; echo
  done | grep -v "finished before"
done; done
rm -rf $D
