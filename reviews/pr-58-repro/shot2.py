# shot2.py PNG SQL: one letter per character cell of the SSMS screenshot (10 px cells from x=80), by the cell's ink colour:
# K blue keyword, o grey operator, . white text, F magenta function, S red string, C green comment, T bright green system object,
# N light green number, P maroon procedure.  Printed under each line of the SQL file.
import sys, colorsys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB'); W, H = im.size; px = im.load()
bg = (0x1e, 0x1e, 0x1e)
def d(a, b): return sum((x - y) ** 2 for x, y in zip(a, b))
ref = dict(K=(0x56,0x9c,0xd6), o=(0x7a,0x7d,0x80), F=(0xc9,0x75,0xd5), S=(0xcb,0x41,0x41), C=(0x57,0xa6,0x4a), T=(0xb0,0xe3,0x5d), N=(0xb5,0xce,0xa8), P=(0xbd,0x77,0x5c))
ref['.'] = (0xda,0xda,0xda)
lines = open(sys.argv[2]).read().replace('\r', '').split('\n')
rows = [any(d(px[x, y], bg) > 3000 for x in range(70, W)) for y in range(H)]
bands = []; y = 3
while y < H:
    if not rows[y]: y += 1; continue
    y1 = y
    while y1 < H and rows[y1]: y1 += 1
    bands.append((y, y1)); y = y1
for (y, y1), ln in zip(bands, [l for l in lines if l.strip()]):
    out = ''
    for c, ch in enumerate(ln):
        if ch == ' ': out += ' '; continue
        cell = [px[x, yy] for x in range(80 + 10 * c, min(W, 90 + 10 * c)) for yy in range(y - 2, min(H, y1 + 2))]
        # the cell's ink: mean of the 6 pixels farthest from the background
        cell.sort(key=lambda p: -d(p, bg)); top = cell[:6]
        m = tuple(sum(p[i] for p in top) // len(top) for i in range(3))
        h, s, v = colorsys.rgb_to_hsv(*[x / 255 for x in m])
        # normalise brightness away: compare hue/saturation against the references at their own value
        if s < 0.12: out += '.' if v > 0.68 else 'o'; continue
        best = min((k for k in ref if k not in '.o'), key=lambda k: d(tuple(int(x * 255) for x in colorsys.hsv_to_rgb(h, s, colorsys.rgb_to_hsv(*[q / 255 for q in ref[k]])[2])), ref[k]))
        out += best
    print(ln); print(out)
