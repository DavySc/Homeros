#!/usr/bin/env bash
# Runs only tests/plants/syntax.txt through tools/planted.sh, in 4 shards.
# From the repo root, with tools-env/bin on PATH.
sed 's#cat tests/plants/\*.txt#cat tests/plants/syntax.txt#' tools/planted.sh > tools/planted-syntax.sh
for k in 0 1 2 3; do SHARD=$k/4 bash tools/planted-syntax.sh > build/planted-syntax-$k.log 2>&1 & done
wait; cat build/planted-syntax-?.log; rm tools/planted-syntax.sh
