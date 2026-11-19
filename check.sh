#!/bin/sh
# The prototype's whole check: build with gcc and clang (-Werror, ASan,
# UBSan), then every slice's oracle against the local test server.
# Needs: dockerd with container mssql holding WWI, AW and tricky
# (README.md, "Test servers"); python3 with pymssql.
set -e
cd "$(dirname "$0")"
make -s clean
make -s CC=clang
make -s clean
make -s CC=gcc
for db in tricky WWI AW; do python3 oracle/snap_oracle.py $db ${SEED:-1} 2; done
python3 oracle/snap_oracle.py tricky ${SEED:-1} 1 crlf
echo "check: ok"
