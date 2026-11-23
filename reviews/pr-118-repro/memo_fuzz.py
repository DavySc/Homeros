# the failed-search memory (Scan af, df) changes no answer: random texts dense
# in link openers and closers through the real driver and through one built
# without the two "p < *f" tests (both -O2); the outputs must be the same
import random, subprocess, sys, os
real, alt, seed, n, d = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]
T = ['[', '[[', ']', ']]', '][', '<', '>', 'http:', 'id:', 'a', ' ', '\n', '\n\n', '\\', '\\\\', '* ', '- ', '#+', ':', 'x',
     '<id:a', '[[a][', ' >', '\n >', '|', '=', '[fn:1]', '[fn::', '(', ')', '"', '<http:b', '[[b]', '\n \n', 'CLOCK: ', '\n#+a: b\n', '\n# \n']
# FN_TOKENS=1: footnote, macro, verbatim and quote shapes too (sexp's memory, holds' chain)
if os.environ.get('FN_TOKENS'):
    T += ['x [fn::a', ' [fn:x:', '\\cite{', '\\a*[', '}', '{', '=b', 'c=', '~', '$', '$$', ' [fn:1] ', '[fn:1] d\n', ' x',
          '⟨', '⟩', '\n: ', '\n\n\n', '\n* h\n', '[[fn:1]]', ' "', 'clock: ']
r = random.Random(seed)
os.makedirs(d, exist_ok=True)
names = []
for i in range(n):
    p = '%s/m%d.org' % (d, i)
    open(p, 'w').write(''.join(r.choice(T) for _ in range(r.randint(2, 60))))
    names.append(p)
bad = 0
for mode in (['-f', '7'], ['-f', '4096'], ['-s']):
    for k in range(0, n, 500):
        a = subprocess.run([real] + mode + names[k:k + 500], capture_output=True).stdout
        b = subprocess.run([alt] + mode + names[k:k + 500], capture_output=True).stdout
        if a != b:
            xa, xb = a.split(b'== '), b.split(b'== ')
            for u, v in zip(xa, xb):
                if u != v:
                    bad += 1
                    if bad <= 3: print('differ', ' '.join(mode), u.split(b'\n')[0].decode())
print('seed %d: %d differ of %d texts x 3 modes' % (seed, bad, n))
