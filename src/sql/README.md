# src/sql/: package 22, SQL in Homeros

The plan Davy approved (2026-11-21): https://claude.ai/artifact/ADFopM6MbLnECk7RMrb6Gn.
The evidence behind it: the knowledge base in the project files,
`sql-refactor-kb/` (chapters 10 to 13, lessons L1-L53), and the prototype
branch `claude/project-thread-osg6me`, which is never merged.

| File | What |
|---|---|
| `net.c` | one non-blocking connection: socket, TLS on OpenSSL memory buffers, certificate check; a value's text escaped for one cell |
| `tds.c` | SQL Server (TDS 7.4): prelogin, TLS, SQL login, batches, the token reader, cancel, every type as SSMS shows it |

## The test server

`tests/test_sql_tds.c` needs SQL Server on 127.0.0.1:1433 and its `sa`
password in `SQL_SA_PASSWORD` (CI has both); without them it fails and says
so.  For a local `make check`:

    export SQL_SA_PASSWORD=Homeros_Test1
    docker run -d --name mssql -e ACCEPT_EULA=Y -e MSSQL_SA_PASSWORD="$SQL_SA_PASSWORD" \
      -p 1433:1433 mcr.microsoft.com/mssql/server:2022-latest

The password is a test server's only; the client never reads one from a file.
