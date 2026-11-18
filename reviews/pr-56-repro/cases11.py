# second pass: the six screens of fresh seeds 21 and 22 (PR's recorder, 200 cases each) where a
# heading differed, again as recorded (twice) and, in insert mode, after CTRL-O :redraw!
import os
def load(path, n):
    c, lines, toks = 0, [], []
    for l in open(path, encoding='utf-8'):
        l = l.rstrip('\n')
        if l.startswith('C '): c += 1
        if c == n:
            if l.startswith('L '): lines.append(l[2:])
            if l.startswith('T '): toks.append(l[2:])
    return lines, toks
D = os.environ['FRESH']   # directory with fresh21.txt and fresh22.txt
CASES = []
for s, n in ((21, 27), (21, 38), (21, 48), (21, 85), (21, 139), (22, 60)):
    lines, toks = load('%s/fresh%d.txt' % (D, s), n)
    CASES.append(('seed %d case %d as recorded' % (s, n), 20, 50, lines, toks))
    CASES.append(('seed %d case %d as recorded, again' % (s, n), 20, 50, lines, toks))
    if toks[-1] == 'k:i': CASES.append(('seed %d case %d + CTRL-O :redraw!' % (s, n), 20, 50, lines, toks + ['k:\x0f:redraw!\r']))
# seed 21 case 38 (command-line mode): without the ':' and with CTRL-L; with ':' sent a second later
lines, toks = load('%s/fresh21.txt' % D, 38)
if os.environ.get('ONLY38'):
    CASES = [('seed 21 case 38 without the colon, CTRL-L', 20, 50, lines, toks[:-1] + ['redraw']),
             ('seed 21 case 38 without the colon', 20, 50, lines, toks[:-1]),
             ('seed 21 case 38 as recorded, third time', 20, 50, lines, toks)]
