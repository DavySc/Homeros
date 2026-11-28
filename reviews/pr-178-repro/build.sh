#!/bin/bash
# from the repository's root: links this directory as rv/ and builds the drivers the scripts use
ln -sfn reviews/pr-178-repro rv
S="src/org/org.c src/uni/uni.c src/date/date.c $(ls src/text/*.c | tr '\n' ' ')"
echo "$S" > rv/srcs
F="-std=c17 -D_DEFAULT_SOURCE -Iinclude"
gcc $F -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -o rv/drv_san tests/org_drv.c $S
gcc $F -O2 -o rv/drv_o2 tests/org_drv.c $S
gcc $F -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -o rv/exact rv/exact.c $S
gcc $F -o rv/cut rv/cut.c $S
