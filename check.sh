#!/bin/sh
# The prototype's whole check: build with gcc and clang (-Werror, ASan,
# UBSan), then every slice's oracle against the local test server.
# Needs: dockerd with container mssql holding WWI, AW and tricky (the
# gen database is made by the check)
# (README.md, "Test servers"); python3 with pymssql.
set -e
cd "$(dirname "$0")"
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
echo "check: ok"
