#!/usr/bin/env bash
# Does the compositor honour the viewport source's offset?  Run under
# sway.sh (RENDERER=pixman or gles2) from the PR's tree.  tests/test_wallpaper.png
# is 8x8, 2 rows red above 6 blue; a 1280x800 window is covered by rows 1 to
# 5, so red ends at 160 of 800 with the offset and at 320 without it.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
srcs=$(make -s --eval 'p: ; @echo $(filter-out src/main.c,$(SRCS)) $(OBJS) $(CPPFLAGS) $(LIBS)' p)
cc -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Iinclude -O2 -o build/image/cursor "$here/cursor.c" $srcs
cp tests/test_wallpaper.png $XDG_RUNTIME_DIR/cfg/homeros/wallpaper
XDG_CONFIG_HOME=$XDG_RUNTIME_DIR/cfg build/image/cursor 4 & sleep 3
for y in 100 240 400; do echo "y=$y: $(grim -g "620,$y 1x1" -t ppm - | tail -c 3 | od -An -tx1 | tr -d ' ')"; done
wait
