#!/bin/sh
# sh run-late.sh LAKE_PROJECT FILE.lean ROOT: finding 1 in the real infoview
# (from the repo's top; needs what oracle/README.md lists).  Builds the
# oracle's editor with a wait before bridge_open (R43_LATE_MS) and prints
# what the page shows with no wait and with 8 s.
set -eu
top=$PWD
mkdir -p build/oracle
sed 's|	if (bridge_open(&e))|	for (int t = getenv("R43_LATE_MS") ? atoi(getenv("R43_LATE_MS")) : 0; t > 0; t -= 10) loop_once(10);\n	if (bridge_open(\&e))|' src/infoview/oracle/drive.c > build/oracle/drive_late.c
cmd=$(make -n test TESTS=infoview | grep -m1 'tests/test_\$t.c' | sed 's/ || exit 1.*//; s/^ *//' | sed "s| -o build/san/test_\$t tests/test_\$t.c| -o build/oracle/drive_late build/oracle/drive_late.c|; s/-fsanitize=address,undefined//")
eval "$cmd"
gcc -O2 -o build/oracle/homeros-infoview src/infoview/serve/serve.c
cd "$1"
for ms in 0 8000; do
	R43_LATE_MS=$ms NODE_PATH=$(npm root -g) node "$top/reviews/pr-43-repro/late.mjs" "$top/build/oracle/drive_late" "$2" "$3"
	pkill -f "lake serve" || true
done
