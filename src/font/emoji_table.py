# Prints the Emoji_Presentation ranges of emoji-data.txt (argv[1]) as C,
# adjacent ranges merged: the table in font.c.  Run on Unicode 16.0.0, the
# version DESIGN.md names for the Unicode tables.
import sys
r = []
for l in open(sys.argv[1]):
    l = l.split('#')[0].strip()
    if not l: continue
    cp, prop = [x.strip() for x in l.split(';')]
    if prop != 'Emoji_Presentation': continue
    a, b = (cp.split('..') + [cp])[:2]
    r.append([int(a, 16), int(b, 16)])
m = []
for a, b in sorted(r):
    if m and a == m[-1][1] + 1: m[-1][1] = b
    else: m.append([a, b])
for i in range(0, len(m), 4):
    print('\t' + ' '.join('{ 0x%04X, 0x%04X },' % (a, b) for a, b in m[i:i + 4]))
