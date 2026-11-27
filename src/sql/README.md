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
| `run.c` | a script's batches, split on GO as sqlcmd 18 splits them (`GO n` too) |
| `sql.c` | the editor's side (`include/sql.h`): F5, Alt+Break and Ctrl-C, the results split, the connections file, `:Connect`, the catalog for completion |

## Running queries

In a `.sql` buffer, F5 runs the selection, or the whole buffer when nothing
is selected (SSMS). Alt+Break cancels the run, and so does Ctrl-C while a run is going (a terminal sends no Break); otherwise Ctrl-C is Vim's. Results and messages come
in the server's order in a split below the query, which SPC r opens and
closes. SQL Server scripts run batch by batch, split on GO. PostgreSQL runs
the text as one query.

Completion (package 19) asks `sql_catalog` for the tables' columns of the
buffer's connection, as SSMS's IntelliSense does. They are read once per
connection, with one `information_schema.columns` query on a connection of
its own, in the background, and kept until asked again or `:Connect`.

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

`trust` accepts the server's own (self-signed) certificate. A file that
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
