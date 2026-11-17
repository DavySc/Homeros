#!/usr/bin/env python3
"""Review of PR 26: tools/conform.py's harness with inputs its generator lacks.
usage: gen2.py DRIVER N SEED MODE
MODE: base   conform.py's own generator (other seeds)
      wide   double-width, combining and emoji text, tabs inside lines
      long   up to 40 lines, counts up to 30, up to 30 commands
      pct    % typed often; run with NOMATCHIT=1 for Neovim's built-in %
      extra  keys FEATURES.md section 2 lists that conform.py never types
Writes the differing cases, with both results, to MODE-SEED.json."""
import json, random, sys, os
from concurrent.futures import ThreadPoolExecutor
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../tools/conform.py')).read()
src = src[:src.rindex('main()')]
# NOMATCHIT=1: Neovim's built-in % (matchit never loads)
if os.environ.get('NOMATCHIT'): src = src.replace("'--clean', '--headless',", "'--clean', '--headless', '--cmd', 'let g:loaded_matchit=1',")
G = {}
exec(src, G)
drv, n, seed, mode = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
r = random.Random(seed)

if mode == 'wide':
    G['WORDS'][:] = ['漢字', '日本x', 'éa', 'x̀́', '👍', '🇳🇱', 'a\tb', '\t', 'ｆｕｌｌ', 'foo', 'x', '(', ')', '漢.', '.字', '', 'カ', 'é', '∀', '한글']
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
if mode == 'extra':
    # typed by nobody in conform.py; FEATURES.md section 2 or Neovim's own table
    G['MOTIONS'] += ['H', 'M', 'L', '*', '#', 'n', 'N', 'gi<Esc>', 'g;', 'g,', '<C-o>', '<C-i>', "''", '``', 'ma', "'a", '`a',
                     'gM', 'gm', 'g0', 'g$', 'g^', '<C-e>', '<C-y>', '<C-d>', '<C-u>', '<C-f>', '<C-b>', 'zz', 'zt', 'zb',
                     '&', 'g&', 'U', '%', '/x<CR>', '?o<CR>', '/\\<foo<CR>']
    G['OPS'] += ['=', 'gq', 'gw', 'g?', '!', 'zf', 'gc']
    G['OBJECTS'] += ['it', 'at', 'i>', 'a]']
    G['SIMPLE'] += ['==', 'gqq', 'g??', '"+p', '".p', '":p', '"/p', '"%p', 'g8', 'ga', 'gcc', '<C-l>', '<C-g>', 'g<C-g>',
                    ':s/o/0/<CR>', ':2<CR>', ':d<CR>', ':normal x<CR>', ':%s/x/y/g<CR>', ':g/foo/d<CR>', ':noh<CR>', 'g-', 'g+']
    G['INS'] += ['<C-v>', '<C-k>a:', '<C-a>', '<C-e>', '<C-y>', '<C-n>', '<C-h>', '<C-j>', '<C-m>', '<C-c>']
    G['CODES'].update({'<C-i>': 9, '<C-e>': 5, '<C-y>': 25, '<C-f>': 6, '<C-b>': 2, '<C-l>': 12, '<C-g>': 7, '<C-k>': 11,
                       '<C-n>': 14, '<C-h>': 8, '<C-j>': 10, '<C-m>': 13, '<C-c>': 3})

cases = [{'text': G['gen_text'](r), 'keys': G['gen_keys'](r)} for _ in range(n)]
want = G['nvim'](cases)
with ThreadPoolExecutor(os.cpu_count()) as x:
    def ours(c):
        try: return G['ours'](drv, c)
        except Exception as e: return {'error': type(e).__name__}   # the driver ran over 20 s
    got = list(x.map(ours, cases))
bad = []
for c, w, g in zip(cases, want, got):
    if g.get('lines') != w['lines'] or g.get('cursor') != w['cursor']:
        bad.append({'text': c['text'], 'keys': c['keys'], 'nvim': w, 'ours': g})
json.dump(bad, open('%s-%d.json' % (mode, seed), 'w'), ensure_ascii=False, indent=0, default=str)
print('%s seed %d: %d sequences, %d differ' % (mode, seed, n, len(bad)))
