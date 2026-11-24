#!/usr/bin/env bash
# builds reviews/pr-100-repro/<name>.c as `make test` builds a test, into
# build/san/pr100_<name>; run from the repo root after `make test TESTS=keys_map`
set -e
n=${1:-probe}
line=$(make -n test TESTS=keys_map | grep -m1 -- '-o build/san/test_\$t')
line=${line%%||*}
line=${line//tests\/test_\$t.c/reviews/pr-100-repro/$n.c -Itests}
eval "${line//test_\$t/pr100_$n}"
