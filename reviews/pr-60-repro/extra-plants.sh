#!/bin/bash
# Plants not in tests/plants/sqlfmt.txt, each run against test_sqlfmt and 20 s of
# fuzz_sqlfmt under ASan+UBSan.  usage: extra-plants.sh (from the repo root)
R=reviews/pr-60-repro
F="-std=c17 -D_DEFAULT_SOURCE -Iinclude -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all"
while IFS='~' read -r name old new; do
	T=$(mktemp -d); cp src/sqlfmt/sqlfmt.c $T/sqlfmt.c
	python3 - $T/sqlfmt.c "$old" "$new" <<'PY' || { echo "BROKEN        $name"; continue; }
import sys
p, old, new = sys.argv[1], sys.argv[2].replace('¶', '\n'), sys.argv[3].replace('¶', '\n')
s = open(p).read()
if s.count(old) != 1: sys.exit(1)
open(p, 'w').write(s.replace(old, new))
PY
	sed -i 's|#include "../syntax/tsql.h"|#include "../src/syntax/tsql.h"|' $T/sqlfmt.c
	by=
	gcc $F -o $T/t tests/test_sqlfmt.c $T/sqlfmt.c src/syntax/tsql.c src/text/text.c 2>$T/cc.log || { echo "NO BUILD      $name"; continue; }
	$T/t >/dev/null 2>&1 || by="test_sqlfmt"
	clang $F -fsanitize=fuzzer,address,undefined -o $T/f tests/fuzz_sqlfmt.c $T/sqlfmt.c src/syntax/tsql.c src/text/text.c 2>>$T/cc.log
	mkdir $T/c; (cd $T && ./f -max_total_time=20 c >/dev/null 2>&1) || by="$by fuzz_sqlfmt"
	[ -n "$by" ] && echo "caught        $name   ($by)" || echo "NOT CAUGHT    $name"
done < $R/extra-plants.txt
