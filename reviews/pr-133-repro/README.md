# Repro files for the review of PR 133

Servers: the two docker lines of `src/sql/README.md` (Postgres image from
`mirror.gcr.io/library/postgres:17`), `SQL_SA_PASSWORD` and `SQL_PG_PASSWORD` set.

Build, from the repo root:

    gcc -std=c17 -D_DEFAULT_SOURCE -Iinclude -I. -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
      -o probe reviews/pr-133-repro/probe.c src/sql/*.c src/str/str.c src/uni/*.c src/date/date.c src/tools/match.c \
      $(pkg-config --libs openssl)

| Finding | Command | Seen on d3edad19 |
|---|---|---|
| 1 | `probe sql 0 127.0.0.1 1433 1 tempdb "create or alter procedure dbo.rvp as select 1 a" "exec dbo.rvp" "select 5 five"` | `exec dbo.rvp`: r=-1 state=6 "the reply ends inside a token or before its last DONE" |
| 1 | same with `exec sp_who 1`, `exec sp_executesql N'select 1 a'`, a procedure that raises an error | the same, 4 of 4 |
| 2 | `probe pgrace 20` | "16 of 20 next queries were cancelled by the previous run's cancel" |
| 3 | `probe leak 0`, `probe leak 1` | sessions named homeros: 1 before, 2 after the close while the child lives, 1 after it dies |
| 4 | `probe spurious` | `src/sql/tds.c:480:2: runtime error: null pointer passed as argument 1` |
| 6 | `probe sql 0 127.0.0.1 1433 1 master "select cast(N'Ελληνικά' collate Greek_CI_AS as varchar(20)) gr"` | `ÅëëçíéêÜ` |
| 9 | `probe sql 1 127.0.0.1 5432 1 postgres "create temp table rvt(a int)" "copy rvt from stdin"` | still busy after 30 s |
| 10 | `slow.c` against a role made with `set scram_iterations=5000000` | longest single pg_io call 1447 ms |
| 8 | `open.c 127.0.0.1` | net_open with the certificate check: 16.4 to 23.8 ms (3 runs); trusted: 0.2 to 1.9 ms |

Host-name check (works; no test holds it): a CA, a certificate for
`pgtest.homeros`, a second Postgres on 127.0.0.1:5433 with it, `/etc/hosts`
with `127.0.0.1 pgtest.homeros wrong.homeros`, `SSL_CERT_FILE=ca.pem`:
`probe sql 1 pgtest.homeros 5433 0 postgres "select 1 a"` logs in;
`wrong.homeros` gives "certificate verify failed (certificate: hostname
mismatch)"; `127.0.0.1` gives "IP address mismatch".  With `SSL_set1_host`
removed, `wrong.homeros` logs in.

Cancel of a running EXEC (works): `probe cancelexec 3 "exec dbo.rvlong"` with
`create or alter procedure dbo.rvlong as select top 3000000 a.object_id from
sys.all_objects a, sys.all_objects b, sys.all_objects c`.
