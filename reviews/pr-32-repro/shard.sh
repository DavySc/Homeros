#!/usr/bin/env bash
# Finding 2: a SHARD value that selects nothing runs no plant and exits 0.
for s in 4/4 4 x; do SHARD=$s bash tools/planted.sh; echo "SHARD=$s: exit $?, no plant run"; done
