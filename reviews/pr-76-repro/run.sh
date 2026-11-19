#!/bin/bash
# usage: run.sh SQLDIFF   (sqldiff built from SQLDiff PR 516, 2da1577)
# Prints Homeros's and sqldiff's output for each repro text, then runs the
# negative-indent probe: sqlfmt.c with an abort where newline()'s indent < 0.
set -e
D=$(cd "$(dirname "$0")" && pwd); R=$D/../..; T=$(mktemp -d); trap 'rm -rf $T' EXIT
src="$R/src/sqlfmt/oracle/drv.c $R/src/syntax/tsql.c $R/src/text/text.c"
gcc -std=c17 -D_DEFAULT_SOURCE -O1 -I$R/include -o $T/drv $src $R/src/sqlfmt/sqlfmt.c
{ echo '#include <stdio.h>'; echo '#include <stdlib.h>'; sed 's/for (int k = indent_of(p, i, c, allman(p, i)); k-- > 0; )/int K_ = indent_of(p, i, c, allman(p, i)); if (K_ < 0) { fprintf(stderr, "NEGATIVE %d\\n", K_); abort(); } for (int k = K_; k-- > 0; )/' $R/src/sqlfmt/sqlfmt.c; } > $R/src/sqlfmt/neg_probe.c
gcc -std=c17 -D_DEFAULT_SOURCE -O1 -I$R/include -I$R/src/sqlfmt -o $T/neg $src $R/src/sqlfmt/neg_probe.c; rm $R/src/sqlfmt/neg_probe.c
for f in nested cte stale stale-min; do
	echo "== $f.sql: Homeros"; $T/drv $D/$f.sql
	echo "== $f.sql: sqldiff"; "$1" fmt -d tsql --keyword-case lower $D/$f.sql 2>&1 | head -3 || true
	echo "== $f.sql: probe"; $T/neg $D/$f.sql > /dev/null 2>$T/e && echo "indent never negative" || head -1 $T/e
done
