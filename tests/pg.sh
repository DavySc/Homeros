#!/bin/bash
# PostgreSQL on the bench (.github/workflows/pg-bench.yml, on the Windows runner's own PostgreSQL): the console debugger
# attaches to the backend of a session that sleeps, stops it in the executor at its next query, looks around, steps,
# and goes; the session must then get its answer and the server must live. usage: pg.sh <hdbg.exe>
set -eu
hdbg=$1; root=$(cygpath "$PGROOT"); work=$(mktemp -d); export PATH="$root/bin:$PATH" PGUSER=postgres PGPORT=5544 PGHOST=localhost PGDATABASE=postgres
ls "$root" "$root/symbols" | head -40
cp "$root"/symbols/postgres.pdb "$root/bin/"   # the debugger looks for a module's debug information beside it
ls -l "$root/bin/postgres.exe" "$root/bin/postgres.pdb"
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
[ "$(tail -n 1 "$work/psql.txt" | tr -d '\r')" -gt 1000000 ] || { echo "pg: the query's answer is missing"; exit 1; }
[ "$(psql -X -At -c 'select 41 + 1' | tr -d '\r')" = 42 ] || { echo "pg: the server is gone"; cat "$work/log.txt"; exit 1; }
pg_ctl -D "$work/data" -w stop
echo "pg ok"
