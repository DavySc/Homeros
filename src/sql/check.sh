#!/usr/bin/env bash
# The Windows half of package 22: tests/test_sql.c built for Windows with
# zig cc and OpenSSL (openssl.nix), run under Wine against the test servers
# (README.md), then the value oracles (tests/test_sql_tds.py, test_sql_pg.py)
# through it.
# UBSan traps; ASan has no runtime for this target.  The test takes about
# a second: 120 s is for a planted hang.
set -eu
cd "$(dirname "$0")/../.."
. tools/winpass.sh
for t in zig wine; do
	command -v $t >/dev/null || { echo "src/sql/check.sh: $t is not on PATH"; exit 2; }
done
. src/sql/openssl-win.sh
mkdir -p build/win
[ -n "${XDG_RUNTIME_DIR:-}" ] || { export XDG_RUNTIME_DIR=$PWD/build/win/run; mkdir -p -m 700 "$XDG_RUNTIME_DIR"; }
export WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" WINEPREFIX=$PWD/build/win/prefix-sql
# test_X.exe from tests/test_X.c and the clients; the windows job runs test_sql_wake.exe on Windows itself
for t in sql_wake sql; do
	zig cc -target x86_64-windows-gnu -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Iinclude -O1 -g \
	  -fsanitize=undefined -fsanitize-trap=undefined -o build/win/test_$t.exe tests/test_$t.c src/sql/net.c src/sql/tds.c src/sql/pg.c src/sql/db.c src/str/*.c src/uni/*.c src/date/*.c \
	  src/tools/match.c $SQL_WIN
	pass 120 build/win/test_$t.exe wine || { echo "FAIL test_$t (Windows)"; exit 1; }
done
exe=build/win/test_sql.exe
printf '#!/bin/sh\nexec wine %s "$@"\n' "$PWD/$exe" > build/win/test_sql.sh
chmod +x build/win/test_sql.sh
for o in tds pg; do
	python3 tests/test_sql_$o.py build/win/test_sql.sh "${SEED:-1}" || { echo "FAIL test_sql_$o oracle (Windows)"; exit 1; }
done
# F5 in the editor (tests/test_sql_editor.c), linked with every module as
# src/git/check.sh links its tests
exe=build/win/test_sql_editor.exe
winlink "$exe" tests/test_sql_editor.c
pass 300 "$exe" wine || { echo "FAIL test_sql_editor (Windows)"; exit 1; }
