"""Every text of K lines over a fixed set of line kinds, one file each:
enumgen.py DIR K SET   (SET 'wide' = 33 kinds, 'core' = 16 kinds)"""
import sys, os, itertools
WIDE = ['* h', '** TODO g :t:', '- a', '  - b', '  c', '', '#+NAME: x', ':D:', ':END:', '#+begin_quote', '#+end_quote', '[fn:1] f',
        '| t |', '#+TBLFM: x', 'CLOCK: [2026-11-22 Sun]', 'SCHEDULED: <2026-11-22 Sun>', ':PROPERTIES:', ':K: v', '#+begin_src c',
        '#+end_src', '# c', ': f', '\\begin{x}', '\\end{x}', '#+CALL: f(', ')', '-----', 'x', '#+BEGIN: d', '#+END:', '+--+', '1. n', '    d']
CORE = ['* h', '- a', '  - b', '  c', '', '#+NAME: x', ':D:', ':END:', '#+begin_quote', '#+end_quote', '[fn:1] f', '| t |',
        ':PROPERTIES:', ':K: v', 'SCHEDULED: <2026-11-22 Sun>', 'x']
d, k, s = sys.argv[1], int(sys.argv[2]), WIDE if sys.argv[3] == 'wide' else CORE
os.makedirs(d, exist_ok=True)
for i, t in enumerate(itertools.product(range(len(s)), repeat=k)):
    sub = os.path.join(d, '%03d' % (i // 5000)); os.makedirs(sub, exist_ok=True)
    open(os.path.join(sub, 'e%s%d-%07d.org' % (sys.argv[3][0], k, i)), 'w', newline='').write('\n'.join(s[j] for j in t) + '\n')
