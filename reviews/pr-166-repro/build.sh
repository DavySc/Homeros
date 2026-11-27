#!/bin/sh
# build.sh <probe.c> <out>: builds a probe with the command `make test`
# builds a test with (ASan, UBSan).  Run from the repository root after
# `make test TESTS=keys_map` has built the objects.
set -e
cp "$1" tests/test_zprobe.c
trap 'rm -f tests/test_zprobe.c' EXIT
cmd=$(make -n test TESTS=zprobe 2>/dev/null | grep -A1 'for t in' | tail -1 | sed 's/ || exit 1; \\$//; s/\$t/zprobe/g')
sh -c "$cmd"
cp build/san/test_zprobe "$2"
