#!/usr/bin/env python3
"""Reviewer's comparison: canon.el (Org) against one or more drivers on the
same files.  cmp.py OUTDIR NAME=DRIVER... -- FILE...
Prints per driver: files differing, elements compared, and keeps the first
differences in OUTDIR/NAME.diff."""
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
def split(out):
    d, cur = {}, None
    for l in out.split(b'\n'):
        if l.startswith(b'== '):
            cur = l[3:]; d[cur] = []
        elif l and cur is not None:
            d[cur].append(l)
    return d
def main():
    out = sys.argv[1]
    k = sys.argv.index('--')
    drivers = [a.split('=', 1) for a in sys.argv[2:k]]
    files = sys.argv[k + 1:]
    if files and files[0].startswith('@'): files = open(files[0][1:]).read().split()
    os.makedirs(out, exist_ok=True)
    bad = {n: [] for n, _ in drivers}
    crash = {n: 0 for n, _ in drivers}
    elements = errors = 0
    logs = {n: open(os.path.join(out, n + '.diff'), 'wb') for n, _ in drivers}
    for i in range(0, len(files), 300):
        part = files[i:i + 300]
        el = split(subprocess.run(['emacs', '-Q', '--batch', '-l', os.path.join(HERE, 'canon.el')] + part,
                                  capture_output=True).stdout)
        res = {}
        for n, drv in drivers:
            r = subprocess.run([drv] + part, capture_output=True)
            if r.returncode:   # find the file that kills it
                res[n] = {}
                for p in part:
                    q = subprocess.run([drv, p], capture_output=True)
                    if q.returncode:
                        crash[n] += 1
                        res[n][os.path.basename(p).encode()] = [b'!crash ' + q.stderr[-300:].replace(b'\n', b' ')]
                    else:
                        res[n].update(split(q.stdout))
            else:
                res[n] = split(r.stdout)
        for p in part:
            name = os.path.basename(p).encode()
            a = el.get(name)
            if a is None or (a and a[-1].startswith(b'!error')):
                errors += 1
                continue
            elements += sum(1 for l in a if not l.startswith(b'+'))
            for n, _ in drivers:
                b = res[n].get(name)
                if a != b:
                    bad[n].append(p)
                    b = b or [b'(no output)']
                    j = next((j for j in range(max(len(a), len(b))) if j >= len(a) or j >= len(b) or a[j] != b[j]), 0)
                    logs[n].write(b'%s line %d\n  org:  %s\n  ours: %s\n' % (
                        p.encode(), j + 1, a[j] if j < len(a) else b'(end)', b[j] if j < len(b) else b'(end)'))
    print('%d files, %d elements, Org failed on %d' % (len(files), elements, errors))
    for n, _ in drivers:
        print('  %s: %d files differ (%d crashed the driver)' % (n, len(bad[n]), crash[n]))
        open(os.path.join(out, n + '.bad'), 'w').write('\n'.join(bad[n]) + ('\n' if bad[n] else ''))
main()
