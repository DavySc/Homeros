# second pass: lines just under VS Code's limit, one comment or many on the line: gen2.py DIR
import sys, os, random
d = sys.argv[1]; os.makedirs(d, exist_ok=True)
w = lambda n, s: open(os.path.join(d, n), 'w').write(s)
N = 19900
units = {'stars': '*a ', 'brackets': '[a', 'bangs': '![a](', 'tickrun': '`', 'parens': '[a]((((', 'ticks2': '`a',
         'under': '_a ', 'lt': '<a', 'bb': '**a ', 'tick3': '``` ', 'img': '![', 'ref': '[a][', 'esc': '\\*'}
for k, u in units.items():
    w('one_%s.lean' % k, "/-- " + u * ((N - 4) // len(u)) + "\n-/\ndef x := 1\n")
    w('dash_%s.lean' % k, "-- " + u * ((N - 3) // len(u)) + "\ndef x := 1\n")
    # many comments on one line, each opening what the rest of the line is searched for
    c = "/-" + u + "-/"
    w('many_%s.lean' % k, c * (N // len(c)) + "\ndef x := 1\n")
    # 50 such lines: a frame of them
    w('frame_%s.lean' % k, ("/-- " + u * ((N - 7) // len(u)) + "-/\n") * 50)
# runs of backticks of every length, each in its own comment
s = ''
k = 1
while len(s) + k + 4 < N: s += "/-" + "`" * k + "-/"; k += 1
w('many_runs.lean', s + "\ndef x := 1\n")
random.seed(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
toks = ['/-', '-/', '`', '``', '[', ']', '(', ')', '*', '**', '_', '!', '<', '>', ' ', 'a', ':', '"', '\\', '#', '~~~', 'http://a']
for i in range(40):
    sub = random.sample(toks, random.randint(2, 6))
    s = ''
    while len(s) < N - 10: s += random.choice(sub)
    w('rnd_%02d.lean' % i, "/-- " + s[:N - 10] + "\n-/\ndef x := 1\n")
