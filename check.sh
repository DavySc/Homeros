#!/bin/sh
# The prototype's whole check: build with gcc and clang (-Werror, ASan,
# UBSan), then every slice's oracle against the local test server.
# Needs: dockerd with container mssql holding WWI, AW and tricky (the
# gen database is made by the check), container pg (PostgreSQL 17,
# ssl on)
# (README.md, "Test servers"); python3 with pymssql.
set -e
cd "$(dirname "$0")"
# snapshots in a directory of this run's: two runs at once (plants, an
# oracle by hand) must not read each other's (KB lesson L27)
SNAPS=$(mktemp -d); export SNAPS; trap 'rm -rf "$SNAPS"' EXIT
make -s clean
make -s CC=clang
make -s clean
make -s CC=gcc
for db in tricky WWI AW; do python3 oracle/snap_oracle.py $db ${SEED:-1} 2; done
python3 oracle/snap_oracle.py tricky ${SEED:-1} 1 crlf
# slice 2: the resolver against the server
python3 oracle/gen_oracle.py tests/queries.sql
python3 oracle/gen_oracle.py ${SEED:-1} ${GEN_N:-250}
python3 oracle/gen_oracle.py $((${SEED:-1} + 1)) ${GEN_N:-250} Latin1_General_CS_AS
for db in WWI AW; do python3 oracle/module_oracle.py $db; done
# slice 5: rename scripts run on the server (all but their COMMIT)
for db in WWI AW; do python3 oracle/rename_oracle.py $db ${RENAME_N:-}; done
# slices 3 and 5: hand cases (the meaning check alone; renames) on WWI.snap, taken above
oracle/cases.sh
# the SQL client bake-off: every type through the own TDS client against the server's own text
python3 oracle/tds_oracle.py build/tdsq ${SEED:-1} ${TDS_N:-20}
python3 oracle/pg_oracle.py build/pgq ${SEED:-1} ${PG_N:-20}
# without -t the test servers' own certificates are refused
echo 'select 1' | build/tdsq 127.0.0.1 1433 sa Homeros_Test1 master 2>&1 | grep -q 'certificate verify failed'
echo 'select 1' | build/pgq 127.0.0.1 5432 postgres Homeros_Test1 postgres 2>&1 | grep -q 'certificate verify failed'
# any reply bytes, both clients: parsed whole or split, the same; a whole TDS reply never waits
# a cancelled result: the client reads on to the server's end and shows no error of its own
c=$(echo 'select top 2000000 a.object_id from sys.all_objects a, sys.all_objects b' | build/tdsq -t -c 1000 127.0.0.1 1433 sa Homeros_Test1 master 2>&1 >"$SNAPS/tdsc")
{ echo "$c" | grep -q '^cancel ' && ! grep -q '^Msg' "$SNAPS/tdsc"; } || { echo "tdsq cancel: $c"; exit 1; }
c=$(echo 'select generate_series(1, 2000000)' | build/pgq -t -c 1000 127.0.0.1 5432 postgres Homeros_Test1 postgres 2>&1 >"$SNAPS/pgc")
{ echo "$c" | grep -q '^cancel ' && ! grep -q ERROR "$SNAPS/pgc"; } || { echo "pgq cancel: $c"; exit 1; }
for f in tds pg; do mkdir "$SNAPS/$f" && build/fuzz_$f -max_total_time=${FUZZ_SECONDS:-60} -dict=fuzz/$f.dict -artifact_prefix="$SNAPS/" "$SNAPS/$f" 2>/dev/null; done
echo "check: ok"
