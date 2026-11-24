#!/usr/bin/env bash
# Run under sway.sh from the PR's tree: prints the screen's pixel inside the
# cursor's block without and with a plain red wallpaper.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
srcs=$(make -s --eval 'p: ; @echo $(filter-out src/main.c,$(SRCS)) $(OBJS) $(CPPFLAGS) $(LIBS)' p)
cc -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Iinclude -O2 -o build/image/cursor "$here/cursor.c" $srcs
python3 - $XDG_RUNTIME_DIR/cfg/homeros/wallpaper <<'PY'
import sys, zlib, struct
ch = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
open(sys.argv[1], 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', 8, 8, 8, 2, 0, 0, 0))
	+ ch(b'IDAT', zlib.compress(b''.join(b'\0' + b'\xff\0\0' * 8 for _ in range(8)))) + ch(b'IEND', b''))
PY
for c in none cfg; do
	XDG_CONFIG_HOME=$XDG_RUNTIME_DIR/$c build/image/cursor 3 & sleep 2
	echo "$c: cursor block $(grim -g '2,2 1x1' -t ppm - | tail -c 3 | od -An -tx1 | tr -d " "), a default cell $(grim -g '600,400 1x1' -t ppm - | tail -c 3 | od -An -tx1 | tr -d " ")"
	wait
done
