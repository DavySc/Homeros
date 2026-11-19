import sys
exec(open('split.py').read().split("a, b = load")[0])
a, b = load(sys.argv[1]), load(sys.argv[2]); i = int(sys.argv[3])
(ha, sa), (hb, sb) = a[i], b[i]
toks = ['<start>'] + [l[2:] for l in ha.split('\n') if l.startswith('T ')]
print(ha.split('\n')[0], 'file1 line1:', repr(bytes.fromhex(ha.split('\n')[1][2:]).split(b'\n')[0][:30]))
print(toks)
n = 0
for k, (x, y) in enumerate(zip(sa, sb)):
    if x != y:
        print('--- screen', k, 'after', toks[k])
        for r, (p, q) in enumerate(zip(x, y)):
            if p != q: print('  row', r, '\n   A', p[:100], '\n   B', q[:100])
        n += 1
        if n >= int(sys.argv[4]) : break
