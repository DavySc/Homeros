#!/bin/sh
# cmp.sh: from the repo's root, with nvim on PATH and dumpj built (build-dumpj.sh)
D=${D:-/tmp/pr71-cmp}; H=$(dirname "$0"); mkdir -p $D
python3 $H/gen.py $D
N=$(($(wc -l < $D/names.txt) + 1))
nvim --headless --clean -l tests/test_json.lua build/syntax/src/json.so build/syntax/src/json.scm $D $N
${DUMPJ:-./dumpj} $(for i in $(seq 0 $((N-1))); do echo $D/$i.json; done)
python3 $H/cmp.py $D
