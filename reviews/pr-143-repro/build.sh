#!/bin/bash
# build.sh <tree> <out> [probe|alt]: the sanitizer build of test_org (the Makefile's SAN flags), of the probe (mkprobe.py) or of the other references (mkalt.py)
# OPT=-O0 or -O2 stands in for a slower or a faster runner
set -e
d=$(dirname "$(readlink -f "$0")")
f=tests/test_org.c
[ "$3" = probe ] && { python3 "$d/mkprobe.py" "$1/tests/test_org.c" "$1/tests/probe_org.c"; f=tests/probe_org.c; }
[ "$3" = alt ] && { python3 "$d/mkalt.py" "$1/tests/test_org.c" "$1/tests/alt_org.c"; f=tests/alt_org.c; }
cd "$1" && gcc -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Iinclude ${OPT:--O1} -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all -o "$2" $f src/org/*.c src/text/*.c src/uni/*.c src/date/*.c
