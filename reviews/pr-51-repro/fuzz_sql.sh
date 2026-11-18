#!/usr/bin/env bash
# The package's fuzzer on SQL only, with $$ and $a$ tokens.  From the repo
# root after `make compilers`, tools-env/bin on PATH.  FUZZ_SECONDS, 60 by default.
cp reviews/pr-51-repro/fuzz_sql.c tests/fuzz_sqlrv.c
make fuzz FUZZERS=sqlrv FUZZ_SECONDS=${FUZZ_SECONDS:-60}; rc=$?
rm tests/fuzz_sqlrv.c; exit $rc
