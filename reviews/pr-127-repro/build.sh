#!/bin/bash
# build.sh <tree> <out>: the sanitizer build of the probe (the Makefile's SAN flags)
set -e
python3 "$(dirname "$0")/mkprobe.py" "$1/tests/test_org.c" "$1/tests/probe_org.c"
cd "$1" && gcc -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Iinclude -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all -o "$2" tests/probe_org.c src/org/*.c src/text/*.c src/uni/*.c
rm tests/probe_org.c
