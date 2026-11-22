#!/usr/bin/env python3
"""Single 'hlsearch' cases in Neovim and in the core, side by side.
usage: PYTHONPATH=tests python3 one.py  (needs build/san/test_core_search)"""
import subprocess, tempfile, os
import test_core_search as t
CASES = [  # text, pattern, cursor (line 1-based, char 0-based)
    ("ab\ncd\n", "b\\n", [1, 1]), ("ab\ncd\n", "b\\n", [1, 0]),
    ("\nx\n", "\\n", [1, 0]), ("x\n\ny\n", "\\n", [2, 0]),
    ("ab\ncd\n", "b\\nc", [2, 0]), ("ab\ncd\n", "b\\nc", [1, 1]), ("ab\ncd\n", "b\\nc", [1, 0]), ("ab\ncd\n", "b\\nc", [2, 1]),
    ("ab\ncd\nef\n", "b\\_.*e", [2, 1]), ("ab\ncd\nef\n", "b\\_.*e", [3, 0]), ("ab\ncd\nef\n", "b\\_.*e", [3, 1]),
    ("ab\n\ncd\n", "b\\n\\nc", [2, 0]),
]
for text, pat, cur in CASES:
    c = {'rows': 6, 'cols': 20, 'wrap': False, 'text': text, 'pat': pat, 'cur': cur}
    o = t.run_nvim(c)
    f = tempfile.mktemp()
    with open(f, 'w') as h:
        h.write('C 6 20 0 %d %d\nP %s\nW %d %d %d\nF %s\n' % (tuple(o['pos']) + (pat.encode().hex(),) + tuple(o['view']) + (text.encode().hex(),)))
        for row in o['rows']: h.write('A %s\n' % row)
        h.write('E\n')
    r = subprocess.run(['build/san/test_core_search', f, '-v'], capture_output=True, text=True)
    print(repr(text), '/' + pat, 'cursor', o['pos'], 'nvim', o['rows'][:3], '|', (r.stderr.strip() or 'core same').replace('\n', ' '))
    os.unlink(f)
