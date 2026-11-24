#!/usr/bin/env bash
# Builds tests/test_sql_wake.c for Windows three times: as it is, and with
# each of the two fixes of PR 130's second round removed (run by the probe
# workflow's Linux job; the windows job runs the three programs).
set -eu
. src/sql/openssl-win.sh
mkdir -p build/win
build() {   # NAME: the tree in the current directory
	zig cc -target x86_64-windows-gnu -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Iinclude -O1 -g \
	  -fsanitize=undefined -fsanitize-trap=undefined -o "$OLDPWD/build/win/wake_$1.exe" tests/test_sql_wake.c src/sql/*.c src/str/*.c src/uni/*.c src/date/*.c \
	  src/tools/match.c $(echo "$SQL_WIN" | sed "s|build/openssl-win|$OLDPWD/build/openssl-win|g")
}
plant() {   # NAME OLD NEW
	local t; t=$(mktemp -d); cp -r include src tests "$t"/
	python3 - "$t/src/sql/net.c" "$2" "$3" <<'PY'
import sys
p, old, new = sys.argv[1:4]
s = open(p).read()
assert s.count(old) == 1, old
open(p, 'w').write(s.replace(old, new))
PY
	(cd "$t" && build "$1")
}
(cd . && build ok)
plant noclose '	if (c->up == 2) WSASetEvent(H(c->fd));
' ''
plant flushfirst 'if (woke(c) || net_flush(c)) return -1;' 'if (woke(c)) return -1;'
ls -l build/win/wake_*.exe
