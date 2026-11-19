#!/usr/bin/env python3
"""Review of PR 67: gq on buffers that are not SQL, against nvim --headless
through tools/conform.py's harness, with a generator of its own.
usage: gq.py REPO DRIVER N SEED MODE    MODE: plain | wide | mix | lead
plain: paragraphs of words, indents, blank and blank-only lines, trailing
blanks, Tabs and runs of blanks inside lines, nroff macros, long words.
wide: the same with wide and combining characters.  mix: conform.py's own
commands between the gq ones.  lead: lines that start with a comment leader
(the gap src/keys/README lists), to count it.
Writes the differing cases to MODE-SEED.json."""
import json, random, sys, os
from concurrent.futures import ThreadPoolExecutor
repo, drv, n, seed, mode = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]
src = open(os.path.join(repo, 'tools/conform.py')).read()
G = {'__file__': os.path.join(repo, 'tools/conform.py')}
exec(src[:src.rindex('main()')], G)
r = random.Random(seed)
W = ['a', 'of', 'the', 'lorem', 'ipsum', 'consectetur', 'x', 'I', 'sed.', 'do,', 'eiusmod', '(tempor)', 'incididunt-ut', '42', 'e.g.', 'A', 'supercalifragilistic']
if mode == 'wide':
    W += ['漢字', '日本語のテキスト', 'éa', 'x̀́', '👍', '🇳🇱', 'ｆｕｌｌ', 'λ', '∀x', '한글', '́', 'ẞß']
SEP = [' '] * 12 + ['  ', '   ', '\t', ' \t', '\t ', ' ́', '\t̀ ', ' ', '\x0c']
LEAD = ['# ', '// ', '> ', '- ', '-- ', '* ', '/* ', '% ', '#', '//', '>', '-', '+ ', '1. ', '• ']
def line(r):
    x = r.random()
    if x < .12: return r.choice(['', '', '', ' ', '\t', '   ', '\x0c'])
    if x < .16: return r.choice(['.PP', '.SH NAME', '.x', '.', '. PP', '{', '}', '.LP words here', '..', '.bp', ' .PP'])
    ind = r.choice(['', '', '', '', '  ', '    ', '\t', '\t  ', ' \t', '        ', ' ' * 30, ' ' * 85])
    s = ''
    for i in range(r.choice([1, 2, 3, 5, 8, 12, 20, 40])):
        w = r.choice(W)
        if r.random() < .02: w = w * r.randint(5, 30)
        s += (r.choice(SEP) if i else '') + w
    if mode == 'lead' and r.random() < .5: s = r.choice(LEAD) + s
    return ind + s + r.choice([''] * 6 + [' ', '  ', '\t', ' ́'])
def text(r):
    L = [line(r) for _ in range(r.choice([0, 1, 1, 2, 3, 5, 8, 15, 30]))]
    return '\n'.join(L) + ('\n' if L and r.random() < .9 else '')
MOT = ['j', 'k', 'G', 'gg', '}', '{', 'ap', 'ip', 'q', 'gq', 'w', '$', '0', 'e', 'l', 'h', ')', '(', 'as', 'is', 'aw', '_', '+', '-', ']]', '[[', 'fa', 'tx', '%', '|', 'iw', 'a(']
MOVE = ['j', 'k', 'w', 'b', '$', '0', 'G', 'gg', '}', '{', 'l', 'h', '^', 'e', '+', '-']
def cnt(r, k):
    if r.random() < .25: k.append(str(r.randint(1, 5)))
def keys(r):
    k = []
    for _ in range(r.randint(1, 6)):
        x = r.random()
        if mode == 'mix' and x < .35: G['command'](r, k)
        elif x < .5: cnt(r, k); k.append('gq'); cnt(r, k); k.append(r.choice(MOT))
        elif x < .62:
            k.append(r.choice(['v', 'V', '<C-v>']))
            for _ in range(r.randint(0, 3)): cnt(r, k); k.append(r.choice(MOVE))
            k.append('gq')
        elif x < .75: cnt(r, k); k.append(r.choice(MOVE))
        elif x < .85: k.append(r.choice(['u', 'u', '<C-r>', '.', '.', 'uu', 'u<C-r>']))
        elif x < .9: k += ['<C-w>', r.choice(['v', 'v', 's', 'w', 'c', 'o'])]
        elif x < .95: k += r.choice([['i', '^', '<C-d>', '<Esc>'], ['A', ' ', 'x', '<Esc>'], ['o', 'a', ' ', 'b', '<Esc>'], ['J'], ['x'], ['d', 'd'], ['p'], ['>', '>'], ['i', '<Tab>', '<Esc>']])
        else: k += ['q', 'q', 'g', 'q', r.choice(['j', 'q', 'ip']), 'q', r.choice(['', 'j', '}']), '@', 'q']
    return k
cases = [{'text': text(r), 'keys': keys(r)} for _ in range(n)]
want = G['nvim'](cases)
def ours(c):
    try: return G['ours'](drv, c)
    except Exception as e: return {'error': type(e).__name__}
with ThreadPoolExecutor(3) as x: got = list(x.map(ours, cases))
bad = [{'text': c['text'], 'keys': c['keys'], 'nvim': w, 'ours': g} for c, w, g in zip(cases, want, got) if not G['same'](g, w)]
err = sum('error' in b['ours'] for b in bad)
json.dump(bad, open('%s-%d.json' % (mode, seed), 'w', errors='surrogateescape'), ensure_ascii=False, indent=0, default=str)
print('%s seed %d: %d sequences, %d differ (%d driver errors), %d with gq changing text in nvim' % (mode, seed, n, len(bad), err,
      sum(1 for c, w in zip(cases, want) if 'gq' in ''.join(c['keys']) and '\n'.join(w['lines']) != c['text'].rstrip('\n'))))
