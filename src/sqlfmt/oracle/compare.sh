#!/bin/bash
# Differential test of src/sqlfmt against SQLDiff's own formatter.
# usage: compare.sh SQLDIFF GEN FROM TO      (GEN: soup.py or tsql.py)
#        compare.sh SQLDIFF FILES...          (.sql files, e.g. SQLDiff's own)
# SQLDIFF is `sqldiff` built from DavySc/SQLDiff main (cargo build --release
# -p sqldiff-cli).  Homeros breaks lines with LF (sqlfmt.c newline()), so CRs
# are dropped from both outputs before they are compared.
set -e
O=$(cd "$(dirname "$0")" && pwd); R=$O/../../..; B=$1; shift
T=$(mktemp -d); trap 'rm -rf $T' EXIT
gcc -std=c17 -D_DEFAULT_SOURCE -O2 -I$R/include -o $T/drv $O/drv.c $R/src/sqlfmt/sqlfmt.c $R/src/syntax/tsql.c $R/src/text/text.c
one() {
	$T/drv "$1" > $T/a 2>/dev/null || { echo "CRASH $2"; return 1; }
	$B fmt -d tsql --keyword-case lower "$1" > $T/b 2>/dev/null || true
	cmp -s <(tr -d '\r' < $T/a) <(tr -d '\r' < $T/b) || { echo "DIFF $2"; return 1; }
}
same=0 differ=0
if [[ $1 == *.py ]]; then
	for i in $(seq $2 $3); do python3 -I $O/$1 $i > $T/in.sql; if one $T/in.sql "$1 $i"; then same=$((same+1)); else differ=$((differ+1)); fi; done
else
	for f; do if one "$f" "$f"; then same=$((same+1)); else differ=$((differ+1)); fi; done
fi
echo "same=$same differ=$differ"
