#!/usr/bin/env python3
"""Review of PR 12: compare segs.c's output with editor.lua's.
Forward: cluster starts and widths must be equal (ASCII widths skipped: the
caller draws tabs and ^X).  One known disagreement is counted apart: Neovim's
virtcol() (getvcol) joins an invalid byte with the cluster after it, and
with a Prepend character before it, where its `l` and its screen do not; so
width differences at or right after an invalid byte are not failures.  Backward: count the lines where Neovim's `h`
visits a column that is not a cluster start (the PR's deviation 2)."""
import sys
lines = open(sys.argv[1], 'rb').read().split(b'\n')[:-1]
uni = open(sys.argv[2]).read().split('\n')
nv = open(sys.argv[3]).read().split('\n')
bad = hmid = n = vcol = 0
for ln, u, v in zip(lines, uni, nv):
    if not ln: continue
    n += 1
    uf = [x.split(':') for x in u.split('|')[0].split()]
    vf = [x.split(':') for x in v.split('|')[0].split()]
    def norm(f): return [(int(a), 0 if ln[int(a)] < 0x80 else int(w)) for a, w in f]
    a, b = norm(uf), norm(vf)
    inv = [ln[x[0]] >= 0x80 and x[1] == 4 for x in a]   # uni: an invalid byte
    if a != b and [x[0] for x in a] == [x[0] for x in b] and all(
            x == y or inv[k] or (k > 0 and inv[k - 1]) for k, (x, y) in enumerate(zip(a, b))):
        vcol += 1
    elif a != b:
        bad += 1
        if bad <= 5: print('DIFF', ln, norm(uf), norm(vf))
    starts = {int(a) for a, _ in uf}
    back = [int(x) for x in v.split('|')[1].split()]
    if any(b not in starts for b in back):
        hmid += 1
        if hmid <= 3: print('h inside a cluster:', ln, 'starts', sorted(starts), 'h visits', back)
    elif back != sorted(starts, reverse=True)[1:]:
        print('BACK DIFF', ln, sorted(starts), back); bad += 1
print('%d lines: %d forward differences, %d lines where only virtcol() next to an invalid byte differs, '
      '%d lines where Neovim h stops inside a cluster' % (n, bad, vcol, hmid))
sys.exit(1 if bad else 0)
