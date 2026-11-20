#!/bin/sh
# sh fuzz_call.sh SECONDS: builds and runs fuzz_call.c (from the repo's top).
set -eu
here=$(dirname "$0")
cmd=$(make -n fuzz FUZZERS=infoview_scan | sed 's/\\$//' | tr -d '\n' | sed 's/.*do *clang /clang /; s/ || exit 1.*//')
cmd=$(echo "$cmd" | sed "s| -o build/fuzz/fuzz_\$f tests/fuzz_\$f.c| -Wno-unused-function -o build/fuzz/fuzz_r43_call $here/fuzz_call.c|; s| src/infoview/infoview.c||")
eval "$cmd"
mkdir -p build/fuzz/r43_corpus
i=0
grep -o '"{\\"id\\":[^;]*' tests/test_infoview.c | sed 's/^"//; s/\\"/"/g; s/%s/\x01/g; s/"$//' | while read -r l; do
	i=$((i + 1)); printf 'pageA %s' "$l" > build/fuzz/r43_corpus/seed$i; done
printf '{"id" "m" "a" "uri" "range" "start" "end" "line" "character" "newText" "edits" "changes" "documentChanges" "textDocument" "selection" "position" "applyEdit" "insertText" "showDocument" "above" "\x01"' | tr ' ' '\n' | sed 's/\\/\\\\/g; s/"/\\"/g; s/^/"/; s/$/"/' > build/fuzz/r43.dict
sed -i 's/\x01/\\x01/' build/fuzz/r43.dict
ASAN_OPTIONS=detect_leaks=0 ./build/fuzz/fuzz_r43_call -max_total_time=$1 -max_len=512 -timeout=20 -dict=build/fuzz/r43.dict -artifact_prefix=build/fuzz/ build/fuzz/r43_corpus 2>build/fuzz/r43_call.log || { tail -30 build/fuzz/r43_call.log; echo "FAIL fuzz_r43_call"; exit 1; }
grep -m1 -o 'Done [0-9]* runs in [0-9]* second(s)' build/fuzz/r43_call.log
