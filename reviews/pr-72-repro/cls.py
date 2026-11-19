import sys, json, os, random
from concurrent.futures import ThreadPoolExecutor
n, seed = int(sys.argv[1]), int(sys.argv[2])
sys.argv = ['x', './drv', '0', '0']
exec(open('vis.py').read().replace('\nmain()', ''))
r = random.Random(seed)
cases = [{'text': text(r), 'keys': keys(r), 'rows': r.choice([8, 12, 20]), 'cols': r.choice([20, 40, 80]), 'wrap': r.random() < .5} for _ in range(n)]
cnt = {'same': 0, 'noise': 0, 'pos': 0, 'draw': 0, 'mode': 0, 'exit': 0}
with ThreadPoolExecutor(4) as ex:
    for i, (c, a, b) in enumerate(ex.map(lambda c: run('./drv', c), cases)):
        if a == b: cnt['same'] += 1; continue
        if b[0] is None: cnt['exit'] += 1; print('EXIT', json.dumps(c, ensure_ascii=False), b[1]); continue
        if not a[1]:
            # nvim not in Visual: ours must show no area and no visual label
            if any('V' in x for x in b[0]) or b[1].startswith('-- VIS'): cnt['noise'] += 1
            else: cnt['same'] += 1
            continue
        if a[2] != b[2]: cnt['pos'] += 1; continue
        k = 'mode' if a[1] != b[1] else 'draw'
        cnt[k] += 1
        print('==', k, i, json.dumps(c, ensure_ascii=False)); print(' nvim', a[1], a[2], 'ours', b[1], b[2])
        for x, y in zip(a[0], b[0]):
            if x != y: print('  ', x, '|', y)
print(seed, cnt)
