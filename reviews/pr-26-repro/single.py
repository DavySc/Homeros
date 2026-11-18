#!/usr/bin/env python3
"""Each key sequence of a list on one fixed text, in Neovim and the driver:
which of FEATURES.md section 2's keys differ.  usage: single.py DRIVER"""
import json, sys, os
from concurrent.futures import ThreadPoolExecutor
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../tools/conform.py')).read()
G = {}
exec(src[:src.rindex('main()')], G)
G['CODES'].update({'<C-i>': 9, '<C-e>': 5, '<C-y>': 25, '<C-f>': 6, '<C-b>': 2, '<C-l>': 12, '<C-g>': 7, '<C-k>': 11,
                   '<C-n>': 14, '<C-h>': 8, '<C-j>': 10, '<C-m>': 13, '<C-c>': 3})
TEXT = 'if (foo) {\n\tbar(x, "a b");\n        baz = foo + 0x1f;\n}\n\n<a><b>foo bar</b></a>\nfoo bar foo\nlong line one two three four five six seven eight nine ten\n'
SEQS = '''j = =|= G|j w g q q|g q G|j ! ! t r <Space> a <Space> b <CR>|/ f o o <CR>|? f o o <CR>|/ f o o <CR> n|/ f o o <CR> N|w w *|G k #|w *  N|/ b a / e <CR>|d / b a z <CR>
5 G f f d i t|5 G f f d a t|5 G f f v a t a t d|y y " + p|y y " * p|: s / f o o / X / <CR>|: % s / f o o / X / g <CR>|: g / f o o / d <CR>|: 2 , 3 d <CR>|: 3 <CR>|: n o r m a l <Space> x <CR>|: 2 , 3 n o r m <Space> A ; <CR>
j m a G ' a|j l m a G ` a|j m a G d ' a|G ' '|G ` `|G <C-o>|G <C-o> <C-i>|x j x g ;|x j x g ; g ; g ,|j x G ' .|j x G ` [|j i x <Esc> G g i y <Esc>|j v l <Esc> G ' <|j v l <Esc> G g v d
% |l l l %|j $ %|2 5 %|H|L|M|3 H|d L|<C-d>|<C-d> <C-u>|<C-e>|<C-e> <C-y>|<C-f>|<C-f> <C-b>|z t|z z|z b|z <CR>|z .|z -
j U|j x x U|w g ?  w|g ? ?|j &|g &|w g i <Esc>|w R x <BS> <BS> <Esc>|g J|3 J|w g u i w|w " a y w " A y w " a p|q a x q u @ a|q a x q 2 @ a|w y w : <C-r> "|" . p|i a b <Esc> " . p|: w q <Esc> " : p|/ b a r <CR> " / p
j z f j|z a|z o|z c|z R|z M|] s|z =|i <C-k> a : <Esc>|i <C-v> u 2 2 0 0 <Esc>|i <C-v> 0 6 5 <Esc>|i a <C-o> 0 b <Esc>|i <C-a> <Esc>|i x <Esc> o <C-a> <Esc>|i x <Esc> j i <C-@>|i <C-e> <Esc>|j i <C-y> <Esc>|j A <C-h> <Esc>|i a <C-j> b <Esc>|i <C-n> <Esc>
[ <Space>|] <Space>|3 ] <Space>|g x|g d|g D|w g d|g O|K|Z Z|g a|g 8|g <C-g>|<C-l>|<C-g>|g m|g M|g 0|g $|g ^|g I x <Esc>|g i|g &|g -|x x g - |x x u g +|w g * |w g #
w y s i w )|w d s (|j c s " '|v g s|g c c|g c j|v j g c|g a i p =|" _ d d p|" a d d " a P|" A y y|: r e g <CR>|: e <Space> x <CR>|: s p <CR>|: v s <CR>|: b n <CR>|: 5 <CR>|: $ <CR>|: . , $ d <CR>|: u <CR>|: r e d o <CR>|: 2 y <CR> P
<C-w> s|<C-w> v|<C-w> s <C-w> j x|<C-w> s <C-w> c|<C-w> o|<Tab>|<C-i>|<C-n>|<C-p>|<C-j>|<C-m>|<C-h>|<BS>|<Space>|<Del>|<C-c>|& |\\|Y p|<Home>|<End>'''
G['CODES'].update({'<Home>': 0, '<End>': 0, '<C-@>': 0})
seqs = [s.split(' ') for line in SEQS.split('\n') for s in line.split('|') if s.strip()]
seqs = [[' ' if k == '<Space>' else k for k in s if k] for s in seqs]
seqs = [s for s in seqs if not any(k in ('<Home>', '<End>', '<C-@>', 'K', 'g x', 'gx') for k in s) and s[:2] != ['Z', 'Z'] and s != ['g', 'x'] and s != ['z', '=']]
cases = [{'text': TEXT, 'keys': s} for s in seqs]
def nv(c):
    try: return G['nvim1']([c])[0]
    except Exception as e: return {'lines': ['<nvim failed: %s>' % type(e).__name__], 'cursor': None}
with ThreadPoolExecutor(4) as x:
    want = list(x.map(nv, cases)); got = list(x.map(lambda c: G['ours'](sys.argv[1], c), cases))
nd = 0
for c, w, g in zip(cases, want, got):
    same = g.get('lines') == w['lines'] and g.get('cursor') == w['cursor']
    nd += not same
    print('%-5s %s' % ('same' if same else 'DIFF', ' '.join('<Space>' if k == ' ' else k for k in c['keys'])))
    if not same:
        dl = [(i + 1, a, b) for i, (a, b) in enumerate(zip(w['lines'], g.get('lines') or [])) if a != b][:2]
        print('      nvim cursor %s lines %d; ours cursor %s lines %s; first differing lines %s' % (w['cursor'], len(w['lines']), g.get('cursor') or g, len(g.get('lines') or []), dl))
print('%d sequences, %d differ' % (len(cases), nd))
