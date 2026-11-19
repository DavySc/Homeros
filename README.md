# Database-aware SQL refactors: the prototype

**Never merged to main.** An orphan branch (no history shared with main) so it cannot be merged by accident, like the debugger prototype. It exists to learn; what it teaches goes into the knowledge base at `/mnt/project-files/sql-refactor-kb/` (start at `INDEX.md`; slice status in `07-plan.md`, lessons in `08-lessons.md`), which the real build in Homeros starts from.

Davy, 2026-11-19: SQL refactors that use a database connection, ready when the text editing is done; T-SQL first, then PostgreSQL and Db2.

## Layout
| Path | What |
|---|---|
| `sqlref/` | the C code that would move into Homeros: `snap.[ch]` (catalog snapshot), `resolve.[ch]` (what every name refers to), `refactor.[ch]` (expand `*`, qualify, object info, and the one meaning check every refactor passes) |
| `cmd/` | small drivers the oracles talk to (`snapq`, `resq`, `refq`) |
| `oracle/` | the snapshot query per dialect (`snap-tsql.sql`), `snap.sh` (takes a snapshot from the test server), the oracles that check each slice against the server, test databases (`tricky.sql`) |
| `tests/queries.sql` | hand-written cases, with the exact refactor results some must give |
| `tests/plants.txt` | planted bugs: each must make `./check.sh` fail |
| `check.sh`, `plants.sh` | the whole check; every plant caught |

Main's lexer (`src/syntax/tsql.c`) and text (`src/text/text.c`) are compiled from a checkout of main, never copied: `make HOMEROS=<path to main>` (default `../main`).

## Test servers (local only; never Davy's work laptop or work databases)
```
nohup dockerd >/tmp/dockerd.log 2>&1 &
docker run -d --name mssql -e ACCEPT_EULA=Y -e MSSQL_SA_PASSWORD=Homeros_Test1 -p 1433:1433 mcr.microsoft.com/mssql/server:2022-latest
# Microsoft's samples, restored as AW and WWI:
#   github.com/Microsoft/sql-server-samples/releases/download/adventureworks/AdventureWorks2022.bak
#   github.com/Microsoft/sql-server-samples/releases/download/wide-world-importers-v1.0/WideWorldImporters-Full.bak
# then: docker cp oracle/tricky.sql mssql:/tmp/ && docker exec mssql /opt/mssql-tools18/bin/sqlcmd -S localhost -U sa -P Homeros_Test1 -C -b -f 65001 -i /tmp/tricky.sql
pip install pymssql
```
Restore: `RESTORE DATABASE AW FROM DISK='/tmp/aw.bak' WITH MOVE 'AdventureWorks2022' TO '/var/opt/mssql/data/aw.mdf', MOVE 'AdventureWorks2022_log' TO '/var/opt/mssql/data/aw.ldf'` (WWI: `WWI_Primary`, `WWI_UserData`, `WWI_Log`, `WWI_InMemory_Data_1`). clang's sanitizers need `libclang-rt-18-dev`.

`./check.sh` (about 1.5 min), then `./plants.sh` (each plant reruns the check until it fails; about 20 min).
