#!/bin/sh
# Score the prototype, SSDT (DacFx) and sqlglot against SQL Server on
# generated statements (seeds FIRST..LAST, N each) and on tests/queries.sql
# (WWI).  Not part of ./check.sh: it measures, it does not gate.
#   compare/run.sh [FIRST] [LAST] [N] [COLLATION]
cd "$(dirname "$0")/.." || exit 1
d=$(mktemp -d)
for s in $(seq "${1:-1}" "${2:-5}"); do
	DUMP=$d/gen$s.jsonl python3 oracle/gen_oracle.py "$s" "${3:-500}" ${4:+"$4"} >/dev/null
	echo "gen seed $s ${4:-}"; python3 compare/three_way.py gen "$d/gen$s.jsonl" | tail -4
done
DUMP=$d/q.jsonl python3 oracle/gen_oracle.py tests/queries.sql >/dev/null
echo "tests/queries.sql (WWI)"; python3 compare/three_way.py WWI "$d/q.jsonl" | tail -4
rm -rf "$d"
