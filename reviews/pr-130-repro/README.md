# Repro files for the review of PR 130

Tools: `tools.nix` on PATH (zig, wine), `make build/openssl-win/lib/libssl.a`,
the test servers of `src/sql/README.md`.  Windows programs are built as
`src/sql/check.sh` builds its test:

    . src/sql/openssl-win.sh
    zig cc -target x86_64-windows-gnu -std=c17 -D_DEFAULT_SOURCE -Iinclude -Isrc/sql -O1 -g -o X.exe reviews/pr-130-repro/X.c \
      src/sql/*.c src/str/*.c src/uni/*.c src/date/*.c src/tools/match.c $SQL_WIN

| File | What | Finding |
|---|---|---|
| `wwake.c`, `wsrv.py` | Winsock alone (no OpenSSL: `zig cc ... wwake.c -lws2_32`): a send that would block, the socket turns writable, then the event is reset as `net_read` resets it. `python3 wsrv.py 7811 &`, `wine wwake.exe 7811 3000` (reset after the socket turned writable: stalls) and `... 0` (reset before: wakes). | 1 |
| `close.c`, `srv.py` | Winsock alone: a close that arrives with unread data. `python3 srv.py 7802 100000 &`, `wine close.exe 7802 300`. | what holds |
| `big.c` | the client itself: batches larger than any socket buffer, `SQL_SA_PASSWORD=... wine big.exe 100 3`. | 1 |
| `block.c` | how long `tds_open` and the longest `tds_io` hold the thread: `wine block.exe HOST` (Windows login), `wine block.exe HOST PASSWORD` (sa). | 3 |
| `review-plants.txt` | my plants, in `tools/planted.sh`'s format. | 2 |

The Windows login runs need the test domain: project files
`package-22/sspi-lab/lab.sh`, a Wine with krb5 (`wineWow64Packages.full`),
`KRB5_CONFIG=krb5.conf`, `kinit davy`, and the two `/etc/hosts` lines lab.sh
names.  The Windows runner's numbers came from a draft PR (157, closed) whose
workflow was cut to the `windows` job running `close.exe` and `wwake.exe`.
