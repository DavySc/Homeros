#!/usr/bin/env bash
# Review of PR 123: how often is the plant "loop: win: proc_wait waits on the
# other children" caught, with the old test (Sleep(100)) and the PR's (the
# mutex)?  Runs waits() alone (the other tests cut from main) N times per
# side, on Windows itself or under Wine (RUN=wine), LOAD busy processes
# beside it.  Usage: N=20 LOAD=0 [RUN=wine] bash reviews/pr-123-repro/win.sh [test.c ...]
set -u
cd "$(dirname "$0")/../.."
N=${N:-20} LOAD=${LOAD:-0} RUN=${RUN:-}
d=${OUT:-build/pr123}
py=python3; [ "${OS:-}" = Windows_NT ] && py=python
mkdir -p "$d/clean" "$d/plant"
cp src/loop/*.c "$d/clean/"; cp src/loop/*.c "$d/plant/"
$py - "$d/plant/win.c" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
old = "if (!p[i].ph || (only >= 0 && i != only)) continue;"
assert s.count(old) == 1
open(p, "w").write(s.replace(old, "if (!p[i].ph) continue;"))
PY
[ $# -gt 0 ] || set -- tests/test_loop_win.c reviews/pr-123-repro/old_test_loop_win.c
for ((i = 0; i < LOAD; i++)); do (while :; do :; done) & done
trap 'kill $(jobs -p) 2>/dev/null' EXIT
for t in "$@"; do
	b=$(basename "$t" .c)
	# waits() alone
	$py - "$t" "$d/$b.c" <<'PY'
import sys, re
s = open(sys.argv[1]).read()
a = s.index("if (argc > 1) return child(argv);"); z = s.index("\twaits();", a)
open(sys.argv[2], "w").write(s[:a] + "if (argc > 1) return child(argv);\n" + s[z:])
PY
	for side in clean plant; do
		exe=$d/$b-$side.exe
		zig cc -target x86_64-windows-gnu -std=c17 -Wno-unused-function -Iinclude -Itests -O1 -g -o "$exe" "$d/$b.c" "$d/$side"/*.c -lshell32 -lws2_32 || exit 2
	done
	pass=0 caught=0 why=
	for ((i = 0; i < N; i++)); do   # alternating
		timeout 60 $RUN "$d/$b-clean.exe" > "$d/out" 2>&1 && pass=$((pass + 1)) || why="$why $(grep -m1 CHECK "$d/out")"
		timeout 60 $RUN "$d/$b-plant.exe" > "$d/out" 2>&1 || { grep -q 'proc_wait(id, 3000)' "$d/out" && caught=$((caught + 1)); }
	done
	echo "$t: clean passed $pass of $N; plant caught $caught of $N (load $LOAD)$why"
done
