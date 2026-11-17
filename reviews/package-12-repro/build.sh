#!/bin/bash
# Builds the review's drivers into build/ (from the repo root, after make).
# dump and window run under ASan and UBSan; gap is -O2, for timing.
SYN=build/syntax
T=$(ls src/text/*.c 2>/dev/null || echo ref/text.c)
common="-std=c17 -D_DEFAULT_SOURCE -Iinclude -I$SYN/src/tree-sitter/lib/include -DSYNTAX_QUERIES=\"$PWD/$SYN/src/queries\""
objs="$T src/syntax/*.c $SYN/tree-sitter.o $(find $SYN/g -name '*.o')"
for x in dump window; do
  gcc -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all $common -o build/$x reviews/package-12-repro/$x.c $objs || exit 1
done
gcc -O2 $common -o build/gap reviews/package-12-repro/gap.c $objs
