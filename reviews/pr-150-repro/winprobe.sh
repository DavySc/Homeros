#!/usr/bin/env bash
# Review of PR 150 on real Windows (the windows job of a draft probe PR,
# never merged): the PR's test_win (frames: second window, focus, shared
# timer, raise order) and two probes, focusprobe (raise order 40 times)
# and qdcprobe (QueryDisplayConfig's rate next to DWM's).  Needs zig.
set -eu
cd "$(dirname "$0")/../.."
R=reviews/pr-150-repro
pick() { if ls src/$1/*.c >/dev/null 2>&1; then echo src/$1/*.c; else echo ref/$1.c; fi; }
mkdir -p build/win
Z="zig cc -target x86_64-windows-gnu"
printf '#include "stb_image.h"\n#include "stb_image_resize.h"\n' | $Z -O2 -x c -c -I$R \
  -DSTB_IMAGE_IMPLEMENTATION -DSTBI_ONLY_PNG -DSTBI_ONLY_JPEG -DSTBI_NO_STDIO -DSTB_IMAGE_RESIZE_IMPLEMENTATION -o build/win/stb.o -
rest="$(pick grid) $(pick uni) $(pick theme) -I$R src/image/image.c src/file/*.c src/loop/*.c $(pick text)"
libs="-luser32 -lgdi32 -ldwmapi -lshell32"
$Z -std=c17 -Wall -Wextra -Werror -Iinclude -O1 -g -fsanitize=undefined -fsanitize-trap=undefined \
  -o build/win/test_win.exe tests/test_win.c src/win/*.c $rest build/win/stb.o $libs
$Z -std=c17 -Iinclude -O1 -g -o build/win/focusprobe.exe $R/focusprobe.c src/win/*.c $rest build/win/stb.o $libs
$Z -std=c17 -O1 -o build/win/qdcprobe.exe $R/qdcprobe.c -luser32 -ldwmapi
cmd /c ver
build/win/qdcprobe.exe || true
# main's test_win on the same runner, to tell a failure of the PR from one of the runner
git fetch -q --depth 1 origin main && git show FETCH_HEAD:tests/test_win.c > build/win/test_win_main.c && git worktree add -f --detach build/base FETCH_HEAD >/dev/null && (cd build/base && $Z -std=c17 -Iinclude -O1 -g -o ../win/test_win_main.exe ../win/test_win_main.c src/win/*.c $(pick grid) $(pick uni) $(pick theme) -I../../$R src/image/image.c src/file/*.c src/loop/*.c $(pick text) ../win/stb.o $libs) && { build/win/test_win_main.exe && echo "main test_win: passed" || echo "main test_win: FAILED"; } || true
for i in 1 2 3 4 5; do echo "== test_win run $i"; build/win/test_win.exe && echo "test_win: passed" || echo "test_win: FAILED ($?)"; done
for i in 1 2 3; do build/win/focusprobe.exe 40 || echo "focusprobe: FAILED ($?)"; done
