#!/usr/bin/env bash
# Review of PR 156 on real Windows (the windows job of a draft probe PR,
# never merged): the PR's test_win_remote 10 times, then two probes:
# inflight (a message sent but unread when the window is destroyed) and
# raise (the new frame comes forward only with AllowSetForegroundWindow?).
set -eu
cd "$(dirname "$0")/../.."
R=reviews/pr-156-repro
mkdir -p build/win
Z="zig cc -target x86_64-windows-gnu"
$Z -std=c17 -Wall -Wextra -Werror -Iinclude -O1 -g -fsanitize=undefined -fsanitize-trap=undefined \
  -o build/win/test_win_remote.exe tests/test_win_remote.c src/win/remote.c src/file/*.c src/loop/*.c src/text/*.c
$Z -std=c17 -Wall -Wextra -O1 -o build/win/inflight.exe $R/inflight.c -luser32
$Z -std=c17 -Wall -Wextra -O1 -o build/win/raise.exe $R/raise.c -luser32
cmd //c ver
ok=0
for i in 1 2 3 4 5 6 7 8 9 10; do build/win/test_win_remote.exe >/dev/null && ok=$((ok + 1)) || echo "test_win_remote run $i: FAILED ($?)"; done
echo "test_win_remote: $ok of 10 runs passed"
build/win/test_win_remote.exe || true
build/win/inflight.exe 50 || echo "inflight: exit $?"
build/win/raise.exe 5 || echo "raise: exit $?"
