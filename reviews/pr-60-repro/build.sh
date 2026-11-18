#!/bin/bash
# builds the review's probes into build/rv (run from the repo root)
set -e
mkdir -p build/rv
S="src/sqlfmt/sqlfmt.c src/syntax/tsql.c src/text/text.c"
C="gcc -std=c17 -D_DEFAULT_SOURCE -Iinclude"
$C -O2 -o build/rv/drv src/sqlfmt/oracle/drv.c $S
$C -O2 -o build/rv/time reviews/pr-60-repro/time.c $S
$C -O2 -o build/rv/mem reviews/pr-60-repro/mem.c $S
$C -O2 -o build/rv/range reviews/pr-60-repro/range.c $S
$C -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wl,--wrap=realloc,--wrap=malloc,--wrap=calloc -o build/rv/oom reviews/pr-60-repro/oom.c $S
