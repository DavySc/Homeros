#!/usr/bin/env python3
"""perf.py: org_open's time on large notes (the -O2 driver, one position, less the time at position 0 of an empty paragraph text of the same size is not subtracted: whole process time, best of 5)"""
import subprocess, time, os
def run(name, text, pos):
    p = '/tmp/claude-0/perf.org'; b = text.encode()
    open(p, 'wb').write(b); open(p + '.cmd', 'w').write(str(pos if pos >= 0 else len(b) + pos))
    ts = []
    for _ in range(5):
        t = time.perf_counter(); o = subprocess.run(['rv/drv_o2', '-o', p], capture_output=True, timeout=600); ts.append(time.perf_counter() - t)
    print('%-58s %7d KB  %8.1f ms (range %.1f..%.1f)  %s' % (name, len(b) // 1024, sorted(ts)[2] * 1e3, min(ts) * 1e3, max(ts) * 1e3, o.stdout.split(b'\n')[1][:40].decode()))
para = 'The meet of x and y is below both, see [[*Lattice]] and [fn:1] or https://x.org/a here.\n'
run('20,000 one-line paragraphs, link at the end', '* Lattice\n' + (para + '\n') * 20000 + 'see [[*Lattice]] x\n', -5)
run('same, with one radio target', '* Lattice\n<<<meet>>>\n\n' + (para + '\n') * 20000 + 'see [[*Lattice]] x\n', -5)
run('same, on the radio link in the last paragraph', '* Lattice\n<<<meet>>>\n\n' + (para + '\n') * 20000 + 'see meet x\n', -5)
for n in (200, 1000, 4000):
    run('one paragraph of %d lines, no radio target, link at its end' % n, '* Lattice\n' + para * n + 'see [[*Lattice]] x\n', -5)
    run('one paragraph of %d lines, a radio target elsewhere' % n, '* Lattice\n<<<join>>>\n\n' + para * n + 'see [[*Lattice]] x\n', -5)
run('2,000 radio targets, 20,000 paragraphs', '* Lattice\n' + ''.join('<<<t%d>>>\n' % i for i in range(2000)) + '\n' + (para + '\n') * 20000 + 'see [[*Lattice]] x\n', -5)
run('20,000 references to a footnote defined at the end, on the first', '* Lattice\n' + ('a [fn:1] b\n\n') * 20000 + '[fn:1] def\n', 13)
run('same, on the definition (back to a reference)', '* Lattice\n' + ('a [fn:1] b\n\n') * 20000 + '[fn:1] def\n', -9)
run('fuzzy link with no match, 20,000 headings', ''.join('* Head %d\nx\n' % i for i in range(20000)) + 'see [[nope]] x\n', -6)
run('20,000 <<targets>>, fuzzy link to the last', ''.join('<<t%d>> x\n\n' % i for i in range(20000)) + 'see [[t19999]] x\n', -6)
run('20,000 lines "<<a b>> x" in verbatim, link [[a b]]', ''.join('=<<a b>>= x\n\n' for i in range(20000)) + 'see [[a b]] x\n', -6)
