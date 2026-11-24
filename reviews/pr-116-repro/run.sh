#!/bin/bash
# run.sh [TREE]: from a checkout of the PR (default .) with tools-env/bin on
# PATH.  Builds the probe (tests/test_keys_ex.c with the keys taken from a
# file) and prints each line of cases.txt as ours against Neovim, then
# the two random runs of the report (SEED and PN as given there).
# In a token file \r is Enter, \e Esc, \t Tab, \o CTRL-O, \v CTRL-V.
here=$(cd "$(dirname "$0")" && pwd)
cd "${1:-.}" && cp "$here/test_zprobe.c" tests/ && make -s test TESTS=zprobe >/dev/null 2>&1
RAW=1 PN=$(wc -l < "$here/cases.txt") TOKS="$here/cases.txt" ./build/san/test_zprobe 2>/dev/null | grep ours
[ -n "$RANDOM_RUNS" ] || exit 0
PN=1500 TOKS="$here/toksA.txt" ./build/san/test_zprobe
SEED=7 PN=3000 TOKS="$here/toksB.txt" ./build/san/test_zprobe
