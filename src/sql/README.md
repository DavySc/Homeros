# src/sql/: package 22, SQL in Homeros

The plan Davy approved (2026-11-21): https://claude.ai/artifact/ADFopM6MbLnECk7RMrb6Gn.
The evidence behind it: the knowledge base in the project files,
`sql-refactor-kb/` (chapters 10 to 13, lessons L1-L53), and the prototype
branch `claude/project-thread-osg6me`, which is never merged.

| File | What |
|---|---|
| `net.c` | one non-blocking connection: socket, TLS on OpenSSL memory buffers, certificate check; a value's text escaped for one cell |
| `tds.c` | SQL Server (TDS 7.4): prelogin, TLS, SQL login, batches, the token reader, cancel, every type as the server's CONVERT shows it (varchar read as code page 1252, the code page of Davy's servers: another code page shows wrong) |
| `pg.c` | PostgreSQL (protocol 3.0): SSLRequest, TLS, SCRAM-SHA-256, simple queries, cancel on a second connection, values in the server's own text |
| `db.c` | one interface over both clients, for the editor and the tests |
| `run.c` | a script's batches, split on GO as sqlcmd 18 splits them (`GO n` too); the @variables a batch uses undeclared |
| `sql.c` | the editor's side (`include/sql.h`): F5, Alt+Break and Ctrl-C, the results split, the connections file, `:Connect`, `:Declare`, the catalog for completion |

## Running queries

In a `.sql` buffer, F5 runs the selection, or the whole buffer when nothing
is selected (SSMS). Alt+Break cancels the run, and so does Ctrl-C while a run is going (a terminal sends no Break); otherwise Ctrl-C is Vim's. Results and messages come
in the server's order in a split below the query, which SPC r opens and
closes. SQL Server scripts run batch by batch, split on GO. PostgreSQL runs
the text as one query.

A SQL Server run that uses an @variable it does not declare (a selected
SELECT whose DECLARE is above it) asks for it, as DataGrip asks for a
query's parameters: F5 types `:Declare @a ` on the command line, followed by
the file's own `DECLARE @a` type and value if it has one, and Enter runs. The value is
kept for that buffer and goes as `DECLARE @a int = 3;` in front of each
batch that needs it, on the batch's first line, so the server's line
numbers stay right. `:Declare @a int = 5` changes it, `:Declare @a` forgets
it, and `:Declare` lists them. The kept value wins over an edited
`DECLARE` in the file, so each batch's results start with the `DECLARE` line
that ran (Davy, 2026-11-27). `tests/test_sql_vars.py` checked which
variables a batch lacks against the server's own answer.

Completion (package 19) asks `sql_catalog` for the tables of the buffer's
connection, or one table's columns (its name in any case, in every schema),
as SSMS's IntelliSense does: an index of each table's first row makes a
call cost about 8 ns per table, not a copy (PR 173's review: 0.0105 ms at
1,000 tables, 0.85 ms at 100,000). When a session on a
connection logs in (its first F5: IntelliSense too needs a connected
window), one `INFORMATION_SCHEMA.COLUMNS` query reads them on a connection
of its own, in the background; every buffer on that connection shares them.
They are read again at a later login if that failed, when completion asks
(fn NULL), and after `:Connect`. `design/specs/SqlCat.tla` models it.

A buffer's connection comes from `$XDG_CONFIG_HOME/homeros/sql`
(`~/.config/homeros/sql`). The longest `dir` line whose folder holds the file
names it, and `:Connect name` sets another one (`:Connect` alone shows it):

```
# name  server  host[:port]  user  database  [trust]  [pass=...]
work    mssql   sql01.corp   davy  Sales     trust    pass=secret
local   pg      localhost    davy  postgres
dir ~/work/sql work
dir ~/notes/db local
```

`trust` accepts the server's own (self-signed) certificate. `pass=` comes
last and takes the rest of the line, so a password may hold spaces. A file that
holds a password must be readable by its owner only (`chmod 600`), libpq's
rule for `~/.pgpass`. Otherwise the run is refused.

## The test servers

`tests/test_sql.c` needs SQL Server on 127.0.0.1:1433 with its `sa`
password in `SQL_SA_PASSWORD`, and PostgreSQL on 127.0.0.1:5432 with its
`postgres` password in `SQL_PG_PASSWORD` (CI has all four); without them it
fails and says so.  For a local `make check`:

    export SQL_SA_PASSWORD=Homeros_Test1 SQL_PG_PASSWORD=Homeros_Test1
    docker run -d --name mssql -e ACCEPT_EULA=Y -e MSSQL_SA_PASSWORD="$SQL_SA_PASSWORD" \
      -p 1433:1433 mcr.microsoft.com/mssql/server:2022-latest
    docker run -d --name pg -e POSTGRES_PASSWORD="$SQL_PG_PASSWORD" -p 5432:5432 postgres:17 \
      -c ssl=on -c ssl_cert_file=/etc/ssl/certs/ssl-cert-snakeoil.pem \
      -c ssl_key_file=/etc/ssl/private/ssl-cert-snakeoil.key

The passwords are test servers' only; the clients never read one from a file.
