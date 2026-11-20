import random, sys, os
TOK = ['<', '[', '>', ']', '<2026-11-22', '[2026-11-22', '2026-01-02', '<%%(', '%%(', '(', ')', 'diary-float t 4 2', ' ', ' ', '  ', '\t',
       ' Sun', ' Mon.', ' 9:00', ' 09:00', ' 19:30', ' 29:30', ' 39:30', '-40:00', '3:00-', '30:00', '9:00-10:30', '-11:00', ' 123:45', '12:3', '1:60', ':59',
       ' +1d', ' ++1w', ' .+1d', '.+1d/3d', ' +2D', ' -2d', ' --3w', ' -1M', ' +1y/x', '+5h', '/2W', '-', '--', '+', '++', '.+',
       'x', 'é', '\r', '\n', '0', '1', '2', '9', 'd', 'H', 'Y', '-0d', '<0000-00-00>', '[2026-11-22 Sun]--',
       '--<2026-11-23>', '--[2026-11-23 Mon 8:00]', '<2026-11-22 Sun 9:00-10:30 .+1d/3d -2d>', '<%%(x) 9:00>', '<%%(x) 39:30-10:00>', '<%%(a)b) 9:00-10:00>']
def gen(r):
    return ''.join(r.choice(TOK) for _ in range(r.randint(1, 40)))
n, seed, out = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
r = random.Random(seed)
os.makedirs(out, exist_ok=True)
for i in range(n):
    with open(os.path.join(out, 't%05d.org' % i), 'w', encoding='utf-8', newline='') as f:
        f.write(gen(r))
