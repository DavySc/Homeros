# shrink a text line by line while Org's block fontifier still errors on it
import sys, subprocess, os
S = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
def bad(lines):
    p = os.path.join(S, 't', 'shrink.org'); open(p, 'wb').write(b''.join(lines))
    o = subprocess.run(['emacs', '-Q', '--batch', '-l', os.path.join(S, 'new', 'org_faces.el'), p], capture_output=True).stdout
    return b'!error' in o
ls = open(sys.argv[1], 'rb').read().splitlines(keepends=True)
assert bad(ls)
i = 0
while i < len(ls):
    t = ls[:i] + ls[i+1:]
    if bad(t): ls = t
    else: i += 1
sys.stdout.buffer.write(b''.join(ls))
open(os.path.join(S, 't', 'min.org'), 'wb').write(b''.join(ls))
