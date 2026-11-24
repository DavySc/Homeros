#!/usr/bin/env bash
# As offset.sh under headless weston 13 (pixman, kiosk shell: the window
# fills 1280x800), read from weston-screenshooter's PNG.  Run from the PR's
# tree after offset.sh built build/image/cursor.
set -eu
export XDG_RUNTIME_DIR=$(mktemp -d)
chmod 700 $XDG_RUNTIME_DIR
weston --backend=headless --renderer=pixman --shell=kiosk-shell.so --no-config --debug --width=1280 --height=800 --socket=wl-off >$XDG_RUNTIME_DIR/weston.log 2>&1 &
pid=$!
trap 'kill $pid; rm -rf $XDG_RUNTIME_DIR' EXIT
until [ -e $XDG_RUNTIME_DIR/wl-off ]; do sleep 0.1; done
export WAYLAND_DISPLAY=wl-off
mkdir -p $XDG_RUNTIME_DIR/cfg/homeros
cp tests/test_wallpaper.png $XDG_RUNTIME_DIR/cfg/homeros/wallpaper
XDG_CONFIG_HOME=$XDG_RUNTIME_DIR/cfg build/image/cursor 5 & cur=$!; sleep 3
(cd $XDG_RUNTIME_DIR && weston-screenshooter && python3 - <<'PY'
import glob, zlib, struct
d = open(glob.glob('wayland-screenshot-*.png')[0], 'rb').read()
w, h, depth, ct = struct.unpack('>IIBB', d[16:26]); n = {2: 3, 6: 4}[ct]
raw = b''; i = 8
while i < len(d):
    L, t = struct.unpack('>I4s', d[i:i + 8])
    if t == b'IDAT': raw += d[i + 8:i + 8 + L]
    i += 12 + L
raw = zlib.decompress(raw); stride = w * n + 1; prev = bytearray(w * n); rows = []
for y in range(h):
    f = raw[y * stride]; cur = bytearray(raw[y * stride + 1:(y + 1) * stride])
    for x in range(w * n):
        a = cur[x - n] if x >= n else 0; b = prev[x]; c = prev[x - n] if x >= n else 0
        if f == 1: cur[x] = (cur[x] + a) & 255
        elif f == 2: cur[x] = (cur[x] + b) & 255
        elif f == 3: cur[x] = (cur[x] + (a + b) // 2) & 255
        elif f == 4:
            p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
            cur[x] = (cur[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
    rows.append(cur); prev = cur
print('%dx%d' % (w, h))
for y in (100, 240, 400): print('y=%d: %s' % (y, bytes(rows[y][620 * n:620 * n + 3]).hex()))
PY
)
wait $cur
