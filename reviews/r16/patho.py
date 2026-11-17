"""Worst-case shapes for the org parsers: patho.py DIR"""
import sys, os
P = {
 'fn-blank-lines': '[fn:1] x\n' + '\n' * 300000 + 'y\n',
 'table-rule-long': '+' + '-+' * 300000 + '\n| a |\n+-+\n',
 'kw-brackets': '#+' + '[' * 20000 + ']:\nx\n',
 'caption-brackets': '#+CAPTION' + '[]' * 20000 + '\nx\n',
 'headline-spaces': '* a' + ' ' * 40000 + 'b\n',
 'headline-colons': '* a ' + ':a' * 40000 + ' \n',
 'item-colons': '- ' + ' ::' * 30000 + 'x\n',
 'clock-long': 'CLOCK: [2026-11-22 Sun' + ' x]' * 20000 + '--[2026-11-22 Sun] => 1:00\n',
 'unclosed-blocks': '#+begin_quote\n' * 20000,
 'unclosed-drawers': ':D:\n' * 20000,
 'unclosed-latex': '\\begin{x}\n' * 20000,
 'props-many': '* h\n:PROPERTIES:\n' + ':K: v\n' * 300000 + ':END:\n',
 'props-unclosed': '* h\n:PROPERTIES:\n' + ':K: v\n' * 100000,
 'list-flat': '- a\n' * 200000,
 'list-deep': ''.join(' ' * j + '- a\n' for j in range(3000)),
 'headlines-deep': ''.join('*' * j + ' h\n' for j in range(1, 3000)),
 'headlines-flat': '* h\n' * 300000,
 'one-long-line': 'x' * 3000000 + '\n',
 'paragraph-many-lines': 'word word\n' * 300000,
 'call-parens': '#+CALL: f' + '(' * 100000 + '\n' + 'x\n' * 1000,
 'quote-nest': '#+begin_quote\n' * 3000 + '#+end_quote\n' * 3000,
 'footnotes-many': '[fn:1] x\n' * 100000,
 'affiliated-many': '#+NAME: x\n' * 100000 + 'p\n',
 'tblfm-many': '| a |\n' + '#+TBLFM: x\n' * 100000,
 'tabs-indent': '\t' * 100000 + '- a\n' + '\t' * 100000 + ' - b\n',
}
os.makedirs(sys.argv[1], exist_ok=True)
for k, v in P.items(): open(os.path.join(sys.argv[1], k + '.org'), 'w', newline='').write(v)
