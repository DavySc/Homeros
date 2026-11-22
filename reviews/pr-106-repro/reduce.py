#!/usr/bin/env python3
"""Shrinks one recorded search case (a file with a single C..E case) while
Neovim and the core still differ in which cells are matches (S and C taken
as one).  usage: PYTHONPATH=tests python3 reduce.py CASEFILE"""
import sys, subprocess, tempfile, re
import test_core_search as t
L = open(sys.argv[1]).read().split('\n')
rows, cols, wrap, cl, cc = map(int, L[0].split()[1:])
pat = bytes.fromhex(L[1][2:]).decode()
lines = bytes.fromhex([x for x in L if x.startswith('F ')][0][2:]).decode().split('\n')
def differs(lines, cur):
    text = '\n'.join(lines)
    o = t.run_nvim({'rows': rows, 'cols': cols, 'wrap': bool(wrap), 'text': text, 'pat': pat, 'cur': cur})
    f = tempfile.mktemp()
    with open(f, 'w') as h:
        h.write('C %d %d %d %d %d\nP %s\nW %d %d %d\nF %s\n' % ((rows, cols, wrap) + tuple(o['pos']) + (pat.encode().hex(),) + tuple(o['view']) + (text.encode().hex(),)))
        for row in o['rows']: h.write('A %s\n' % row)
        h.write('E\n')
    err = subprocess.run(['build/san/test_core_search', f, '-v'], capture_output=True, text=True).stderr
    m = re.findall(r'nvim \[(.*)\]\n  core \[(.*)\]', err)
    return any(a.replace('C', 'S') != b.replace('C', 'S') for a, b in m), err, o
cur = [cl + 1, cc]
assert differs(lines, cur)[0]
changed = True
while changed:
    changed = False
    for i in range(len(lines)):
        for cand, c2 in ((lines[:i] + lines[i + 1:], [cur[0] - (i < cur[0] - 1), cur[1]]),
                         (lines[:i] + [lines[i][:len(lines[i]) // 2]] + lines[i + 1:], cur), (lines[:i] + [lines[i][len(lines[i]) // 2 + 1:]] + lines[i + 1:], cur)):
            if cand != lines and c2[0] >= 1 and cand and differs(cand, c2)[0]:
                lines, cur, changed = cand, c2, True
                break
        if changed: break
d = differs(lines, cur)
print('text', repr('\n'.join(lines)), 'pattern', repr(pat), 'cursor', cur, 'rows', rows, 'cols', cols, 'wrap', wrap)
print('nvim rows', d[2]['rows'], 'view', d[2]['view']); print(d[1])
