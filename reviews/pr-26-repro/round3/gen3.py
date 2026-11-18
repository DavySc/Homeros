#!/usr/bin/env python3
"""Review of PR 26, third pass: tools/conform.py's harness and its known list,
with other seeds and inputs its generator lacks.
usage: gen3.py REPO DRIVER N SEED MODE     MODE: base | wide | long | pct
Prints the count and writes the unlisted differing cases to MODE-SEED.json."""
import json, random, sys, os
from concurrent.futures import ThreadPoolExecutor
repo, drv, n, seed, mode = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]
src = open(os.path.join(repo, 'tools/conform.py')).read()
src = src[:src.rindex('main()')]
G = {'__file__': os.path.join(repo, 'tools/conform.py')}
exec(src, G)
known = json.load(open(os.path.join(repo, 'tests/conform-known.json')))
r = random.Random(seed)
if mode == 'wide':
    G['WORDS'][:] = ['漢字', '日本x', 'éa', 'x̀́', '👍', '🇳🇱', 'a\tb', '\t', 'ｆｕｌｌ', 'foo', 'x', '(', ')', '漢.', '.字', '', 'カ', 'é', '∀', '한글']
    G['CHARS'][:] = list('ox(). \t') + ['漢', '字', '👍', '́', 'é']
    G['INS'][:] = list('ab x.') + ['漢', '👍', '́', '<CR>', '<BS>', '<BS>', '<Tab>', '<Del>', '<C-w>', '<C-u>', '<Left>', '<Right>', '<Up>', '<Down>']
if mode == 'long':
    def gen_text(r):
        lines = []
        for _ in range(r.randint(0, 40)):
            ind = r.choice(['', '', '    ', '  ', '\t', '\t\t', '      '])
            lines.append(ind + ' '.join(r.choice(G['WORDS']) for _ in range(r.randint(0, 14))))
        return '\n'.join(lines) + ('\n' if lines and r.random() < .9 else '')
    def count(r, keys):
        x = r.random()
        if x < .3: keys.append(str(r.randint(1, 30)))
        elif x < .33: keys += [str(r.randint(1, 9)), '0']
    G['gen_text'], G['count'] = gen_text, count
    def gen_keys(r):
        keys = []
        for _ in range(r.randint(1, 30)): G['command'](r, keys)
        return keys
    G['gen_keys'] = gen_keys
if mode == 'pct':
    G['MOTIONS'] += ['%'] * 12
    G['WORDS'] += ['(', ')', '{', '}', '[', ']', '((', '))', '/*', '*/', '"("', "')'", '\\(']
cases = [{'text': G['gen_text'](r), 'keys': G['gen_keys'](r)} for _ in range(n)]
want = G['nvim'](cases)
with ThreadPoolExecutor(3) as x:
    def ours(c):
        try: return G['ours'](drv, c)
        except Exception as e: return {'error': type(e).__name__}
    got = list(x.map(ours, cases))
bad, nk, err = [], 0, 0
for c, w, g in zip(cases, want, got):
    if G['same'](g, w): continue
    if 'error' in g: err += 1
    elif G['known_rule'](known, c): nk += 1; continue
    bad.append({'text': c['text'], 'keys': c['keys'], 'nvim': w, 'ours': g})
json.dump(bad, open('%s-%d.json' % (mode, seed), 'w', errors='surrogateescape'), ensure_ascii=False, indent=0, default=str)
print('%s seed %d: %d sequences, %d unlisted differ (%d driver errors), %d listed' % (mode, seed, n, len(bad), err, nk))
