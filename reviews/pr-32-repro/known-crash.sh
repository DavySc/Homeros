#!/usr/bin/env bash
# Finding 1: a driver crash on a case that matches a known-difference rule
# counts as known, and the run exits 0.  Run from a scratch copy of the repo
# with tools-env/bin on PATH:  bash reviews/pr-32-repro/known-crash.sh
set -e
sed -i 's|	if (k == KEY_DEL && count) { count /= 10; return; }|	if (k == KEY_DEL \&\& count) { char *p = 0; *p = 1; }|' ref/keys.c
printf '%s' '[{"why": "Delete after a count", "keys": "[0-9]<Del>", "example": {"text": "abc\n", "keys": ["2", "<Del>", "x"]}}]' > tests/conform-known.json
make conform CONFORM_N=1500 SEED=5 | tail -1
echo "exit $?   (want: nonzero, the driver dies under ASan on every such case)"
