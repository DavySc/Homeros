#!/usr/bin/env python3
"""perf2.py: org_open on a link at the end of one paragraph of L lines, in a note with R radio targets none of which the paragraph mentions (-O2 driver, whole process, median of 5 and range)"""
import subprocess, time
def run(R, L):
    para = 'The meet of x and y is below both, see [[*Lattice]] and [fn:1] or https://x.org/a here.\n'
    text = '* Lattice\n' + ''.join('<<<term%d>>> ' % i for i in range(R)) + '\n\n' + para * L + 'see [[*Lattice]] x\n'
    p = '/tmp/claude-0/perf.org'; b = text.encode()
    open(p, 'wb').write(b); open(p + '.cmd', 'w').write(str(len(b) - 5))
    ts = []
    for _ in range(5):
        t = time.perf_counter(); o = subprocess.run(['rv/drv_o2', '-o', p], capture_output=True, timeout=600); ts.append(time.perf_counter() - t)
    print('%3d radio targets, paragraph of %4d lines: %8.1f ms (range %.1f..%.1f)  %s' % (R, L, sorted(ts)[2] * 1e3, min(ts) * 1e3, max(ts) * 1e3, o.stdout.split(b'\n')[1][:30].decode()))
for R in (0, 1, 10, 50):
    for L in (10, 30, 100):
        run(R, L)
