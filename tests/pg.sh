#!/bin/bash
# PostgreSQL on the bench (.github/workflows/pg-bench.yml, on the Windows runner): the console debugger
# attaches to the backend of a session that sleeps, stops it in the executor at its next query, looks around, steps,
# and goes; the session must then get its answer and the server must live. Then Visual Studio does the same, and the stops must be the same. usage: pg.sh <hdbg.exe> <where PostgreSQL is installed, built with debug information>
set -eu
hdbg=$1; root=$2; work=$(mktemp -d); export PATH="$root/bin:$root/lib:$PATH" PGUSER=postgres PGPORT=5544 PGHOST=localhost PGDATABASE=postgres
ls -l "$root/bin/postgres.exe" "$root/bin/postgres.pdb"   # the debugger looks for a module's debug information beside it
initdb -D "$work/data" -U postgres -A trust > /dev/null
pg_ctl -D "$work/data" -l "$work/log.txt" -w start
psql -X -At -c "select pg_sleep(30)" -c "select count(*) + 1000000 from pg_class" > "$work/psql.txt" &
sleep 2
pid=$(psql -X -At -c "select pid from pg_stat_activity where query like '%pg_sleep%' and pid <> pg_backend_pid()")
echo "backend $pid"
printf 'c\nb ExecutorRun\nc\nbt\nl\np queryDesc->operation\np *queryDesc\nn\nn\nn\ns\nbt\no\np $ReturnValue\nt\nq\n' | timeout 300 "$hdbg" -p "$pid" | tr -d '\r' | tee "$work/hdbg.txt" | cut -c1-300
wait %1 || { echo "pg: the session did not end well"; cat "$work/log.txt"; exit 1; }
cat "$work/psql.txt"
grep -q -x 'breakpoint' "$work/hdbg.txt" || { echo "pg: no stop at the breakpoint"; exit 1; }
grep -q '^#0 .* ExecutorRun  postgres.exe' "$work/hdbg.txt" || { echo "pg: the stop is not in ExecutorRun"; exit 1; }
grep -q '^#[0-9]* .* exec_simple_query  postgres.exe' "$work/hdbg.txt" || { echo "pg: the stack does not reach exec_simple_query"; exit 1; }
# what it shows there is what the session asked: the statement's kind and text in the executor's own structure, and one row processed
grep -q -x 'queryDesc->operation = CMD_SELECT (1)  (CmdType)' "$work/hdbg.txt" || { echo "pg: the statement's kind is not shown"; exit 1; }
grep -q '^\*queryDesc = {CMD_SELECT (1), 0x[0-9a-f]*, 0x[0-9a-f]* "select count(\*) + 1000000 from pg_class"' "$work/hdbg.txt" || { echo "pg: the statement's text is not shown"; exit 1; }
grep -q -x '\$ReturnValue = 1  (unsigned long long)' "$work/hdbg.txt" || { echo "pg: PortalRunSelect's count of rows is not shown"; exit 1; }
[ "$(tail -n 1 "$work/psql.txt" | tr -d '\r')" -gt 1000000 ] || { echo "pg: the query's answer is missing"; exit 1; }
[ "$(psql -X -At -c 'select 41 + 1' | tr -d '\r')" = 42 ] || { echo "pg: the server is gone"; cat "$work/log.txt"; exit 1; }
# the same session in Visual Studio (vs/pg.ps1), the reference: ours must have stopped where it stops, at the breakpoint and after every step
psql -X -At -c "select pg_sleep(90)" -c "select count(*) + 1000000 from pg_class" > "$work/psql2.txt" &
session=$!
sleep 2
pid=$(psql -X -At -c "select pid from pg_stat_activity where query like '%pg_sleep%' and pid <> pg_backend_pid()")
powershell -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$(dirname "$0")/../vs/pg.ps1")" "$pid" "$(cygpath -w "$work/vs.txt")" | cut -c1-600
wait $session || { echo "pg: the session Visual Studio was in did not end well"; exit 1; }
ours=$(awk '/^(breakpoint|stepped)$/ { getline; n = split($NF, p, "/"); print $3, p[n] }' "$work/hdbg.txt")
theirs=$(tr -d '\r' < "$work/vs.txt" | sed -n 's/^STEP [a-z]* \(.*\) [0-9]*$/\1/p')
[ "$ours" = "$theirs" ] || { echo "pg: ours stops at:"; echo "$ours"; echo "Visual Studio at:"; echo "$theirs"; exit 1; }
pg_ctl -D "$work/data" -w stop
echo "pg ok"
