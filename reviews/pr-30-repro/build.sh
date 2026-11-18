#!/bin/bash
# Builds the review's drivers into build/ (from the repo root, after make).
# stress runs under ASan, UBSan and LeakSanitizer; freetime is -O2, for timing.
SYN=build/syntax
common="-std=c17 -D_DEFAULT_SOURCE -Iinclude -I$SYN/src/tree-sitter/lib/include -I$SYN/src/tree-sitter/lib/src"
objs="src/text/*.c src/syntax/*.c $SYN/tree-sitter.o $SYN/queries.o $(find $SYN/g/ -name '*.o')"
gcc -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all $common -o build/stress reviews/pr-30-repro/stress.c $objs || exit 1
for x in gap freetime; do
  src=reviews/pr-30-repro/$x.c; [ -f $src ] || src=reviews/package-12-repro/$x.c
  [ -f $src ] && gcc -O2 $common -o build/$x $src $objs
done
