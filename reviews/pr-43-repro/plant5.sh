#!/bin/sh
# sh plant5.sh RUNS [SECONDS]: how often the server fuzzer catches the
# package's own plant "a page id longer than its buffer" within
# FUZZ_SECONDS (60, as tests/plants/infoview.txt runs it), with make's
# flags.  From the repo's top.
set -eu
runs=$1; secs=${2:-60}
d=build/p5
mkdir -p $d/tests $d/src/infoview/serve
cp tests/fuzz_infoview_serve.c $d/tests/
sed 's/	while (n < 64 \&\&/	while (n < 80 \&\&/' src/infoview/serve/serve.c > $d/src/infoview/serve/serve.c
clang -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O1 -g -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -o $d/fuzz $d/tests/fuzz_infoview_serve.c
caught=0
for i in $(seq $runs); do
	if $d/fuzz -max_total_time=$secs -max_len=256 -timeout=20 -artifact_prefix=$d/ 2>$d/log; then echo "run $i: not caught ($(grep -o 'Done [0-9]* runs' $d/log))"
	else caught=$((caught + 1)); echo "run $i: caught after $(grep -c '' $d/log) log lines, $(grep -o '^#[0-9]*' $d/log | tail -1) runs"; fi
done
echo "caught in $caught of $runs runs of $secs s"
