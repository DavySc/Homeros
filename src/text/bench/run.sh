#!/usr/bin/env bash
# Measures the rope against ropey on the same workload (bench.c, ropey.rs):
# instructions under callgrind, 5 alternating timed runs, peak RSS.
# Usage: src/text/bench/run.sh [runs]   (writes inputs and builds to build/bench)
set -e
cd "$(dirname "$0")/../../.."
b=build/bench; runs=${1:-5}
mkdir -p $b
[ -f $b/big.txt ] || (cd $b && python3 ../../design/proto/gen_input.py)
gcc -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Iinclude -O2 -g -o $b/c src/text/bench/bench.c src/text/*.c
cargo build -q --release --manifest-path src/text/bench/Cargo.toml --target-dir $b/rs
r=$b/rs/release/ropebench
echo "== callgrind, mid.txt (10 MB), 5,000 edits, instructions inside run()"
valgrind --tool=callgrind --collect-atstart=no '--toggle-collect=run*' --callgrind-out-file=$b/cg.c $b/c $b/mid.txt 5000 2>&1 | grep -E 'refs'
valgrind --tool=callgrind --collect-atstart=no '--toggle-collect=ropebench::run' --callgrind-out-file=$b/cg.r $r $b/mid.txt 5000 2>&1 | grep -E 'refs'
echo "== $runs alternating runs, big.txt (100 MB), 50,000 edits"
for i in $(seq $runs); do $b/c $b/big.txt 50000; $r $b/big.txt 50000; done
echo "== peak RSS (KB), same run"
for p in $b/c $r; do python3 -c 'import resource,subprocess,sys; subprocess.run(sys.argv[1:],stdout=subprocess.DEVNULL,check=True); print(sys.argv[1], resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss)' $p $b/big.txt 50000; done
