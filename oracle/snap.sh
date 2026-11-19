#!/bin/sh
# snap.sh DB OUT: the snapshot of DB in the local test server (container
# mssql, never a work server) into OUT, with sqlcmd as the editor would run it.
set -e
d=$(dirname "$0")
docker cp -q "$d/snap-tsql.sql" mssql:/tmp/snap-tsql.sql
docker exec mssql /opt/mssql-tools18/bin/sqlcmd -S localhost -U sa -P "${SA_PASSWORD:-Homeros_Test1}" -C -b -f 65001 -d "$1" -y 0 -i /tmp/snap-tsql.sql -o /tmp/out.snap
docker cp -q mssql:/tmp/out.snap "$2"
