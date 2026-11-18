#!/usr/bin/env bash
# usage: fuzz_md.sh TREE SECONDS   (TREE built; clang with libFuzzer)
set -e
here=$(cd "$(dirname "$0")" && pwd)
printf 'reviewfuzz: $(OBJS)\n\tclang $(WARN) -O1 -g -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all $(CPPFLAGS) -o $(OUT) $(SRC) $(LIB_SRCS) $(OBJS) $(LIBS)\n' > /tmp/claude-0/pr56/r2/fuzz.mk
make -s -C "$1" -f Makefile -f /tmp/claude-0/pr56/r2/fuzz.mk reviewfuzz SRC="$here/fuzz_md.c" OUT="$1/build/fuzz_md"
mkdir -p /tmp/claude-0/pr56/r2/fuzz && cd /tmp/claude-0/pr56/r2/fuzz
XDG_STATE_HOME=/tmp/claude-0/pr56/r2/fuzz/state "$1/build/fuzz_md" -max_total_time="$2" -max_len=256 -timeout=20 -artifact_prefix=/tmp/claude-0/pr56/r2/fuzz/ 2>fuzz_md.log || { tail -40 fuzz_md.log; echo "FAIL fuzz_md"; exit 1; }
grep -m1 -o 'Done [0-9]* runs in [0-9]* second(s)' fuzz_md.log; grep -o 'cov: [0-9]*' fuzz_md.log | tail -1
