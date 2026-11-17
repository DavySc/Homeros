#!/usr/bin/env bash
# The rope against ropey 1.6.1: instructions (callgrind, exact) per step on
# 10 MB, then wall time and peak memory on 100 MB, RUNS alternating runs per
# side.  Needs cargo (fetches ropey), valgrind and python3.
#   bash src/text/bench/run.sh [RUNS]
set -e
cd "$(dirname "$0")"
RUNS=${1:-5}
out=${OUT:-/tmp/rope-bench}
mkdir -p "$out"
[ -f "$out/big.txt" ] || (cd "$out" && python3 "$OLDPWD/../../../design/proto/gen_input.py")
gcc -std=c17 -D_DEFAULT_SOURCE -O2 -I../../../include -o "$out/c" bench.c ../text.c
(cd rope && cargo build --release -q)
cp rope/target/release/rope "$out/rope"

ir() { valgrind --tool=callgrind --callgrind-out-file=/dev/null "$@" 2>&1 >/dev/null | grep -o 'Collected : [0-9]*' | grep -o '[0-9]*$'; }
echo "instructions on 10 MB (callgrind; each step is the difference to the run without it)"
for s in c rope; do
	base=$(ir "$out/$s" "$out/mid.txt" 0 0)
	ed=$(ir "$out/$s" "$out/mid.txt" 5000 0)
	lk=$(ir "$out/$s" "$out/mid.txt" 0 100000)
	echo "$s: open+read+check $base, 5000 edits+undo $((ed - base)), 100000 lookups each way $((lk - base))"
done

echo "100 MB, 50000 edits, 1000000 lookups each way, $RUNS alternating runs (seconds; peak RSS includes the 100 MB file both read first)"
for i in $(seq "$RUNS"); do
	for s in c rope; do
		python3 ../../../design/proto/rss.py "$out/$s" "$out/big.txt" 50000 1000000
	done
done
