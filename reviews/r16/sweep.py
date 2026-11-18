"""One file per code point and construct: a character X inside a tag, a drawer
name, a footnote label, a block name, a keyword, before a bullet.
sweep.py DIR"""
import sys, os
T = {'tag': '* h :aXb:\n', 'drawer': ':aXb:\nx\n:END:\n', 'fn': '[fn:aXb] y\n', 'block': '#+begin_aXb\nq\n#+end_aXb\n',
     'blockcase': '#+begin_X\nq\n#+end_Y\n', 'kw': '#+aX: v\np\n', 'prop': '* h\n:PROPERTIES:\n:aXb: v\n:END:\n', 'bullet': 'X- x\n', 'dyn': '#+BEGIN: Xa\n#+END:\n',
     'latex': '\\begin{aXb}\n\\end{aXb}\n', 'hl': '*X h\n', 'item': '-Xa\n', 'ts': 'CLOCK: [2026-11-22X]\n', 'attr': '#+ATTR_X: v\np\n'}
cps = [c for c in range(0x80, 0x3100)] + list(range(0x3100, 0x11000, 97)) + [0x1F600, 0x1D4DD, 0x10400, 0x2F800]
os.makedirs(sys.argv[1], exist_ok=True)
for k, t in T.items():
    for c in cps:
        if 0xD800 <= c <= 0xDFFF: continue
        ch = chr(c)
        if k == 'blockcase':
            if ch.upper() == ch.lower() or len(ch.upper()) != 1 or len(ch.lower()) != 1: continue
            s = t.replace('X', ch.lower()).replace('Y', ch.upper())
        else: s = t.replace('X', ch)
        open(os.path.join(sys.argv[1], '%s-%05x.org' % (k, c)), 'w', encoding='utf-8', newline='').write(s)
