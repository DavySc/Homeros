#!/bin/sh
# snap.sh DB OUT: the snapshot of DB in the local test server (container
# mssql, never a work server) into OUT, with sqlcmd as the editor would run it.
# Files in the container are per run: two runs at once (a plant run and an
# oracle) once wrote into one file (KB lesson L27).
set -e
d=$(dirname "$0") t=/tmp/snap.$$
docker cp -q "$d/snap-tsql.sql" mssql:$t.sql
docker exec mssql /opt/mssql-tools18/bin/sqlcmd -S localhost -U sa -P "${SA_PASSWORD:-Homeros_Test1}" -C -b -f 65001 -d "$1" -y 0 -i $t.sql -o $t.snap
docker cp -q mssql:$t.snap "$2"
docker exec -u root mssql rm -f $t.sql $t.snap
