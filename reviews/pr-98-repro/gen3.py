"""PR 98 review: texts aimed at the new LOGBOOK's indentation: drawers in
drawers, stray :LOGBOOK: and :END: lines, lists, blocks and text at mixed
indentation (spaces and tabs) under a headline that takes a note.
gen3.py DIR N SEED"""
import os, random, sys
d, n, seed = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
r = random.Random(seed)
os.makedirs(d, exist_ok=True)
IND = ['', '', '  ', '    ', ' ', '\t', ' \t', '\t  ', '         ', '   ']
LINES = [':NOTES:', ':END:', ':LOGBOOK:', ':LOGBOOK:', ':logbook:', ':END:', 'x', 'x y', '', '- item', '1. one', '- a :: b',
         '#+begin_quote', '#+end_quote', '#+begin_src c', '#+end_src', '| a |', ': fixed', '# c', '#+k: v', ':LOGBOOK: x',
         ':PROPERTIES:', ':a: b', 'CLOCK: [2026-11-10 Tue 09:00]', '[fn:1] f', '-----', '#+begin_center', '#+end_center',
         '- State "DONE"       from "TODO"       [2026-11-10 Tue 09:00]', ':END:x', ':DRAWER:', '\\begin{a}', '\\end{a}', '%%(x)']
for i in range(n):
    out = []
    for _ in range(r.randint(1, 3)):
        out.append('*' * r.choice([1, 1, 2]) + ' ' + r.choice(['TODO ', 'NEXT ', 'WAIT ', 'DONE ', '', 'CANCELLED ']) + r.choice(['a', 'b :t:', '']))
        if r.random() < .3:
            out.append(r.choice(IND) + r.choice(['SCHEDULED: <2026-11-12 Thu +1d>', 'DEADLINE: <2026-11-20 Fri>', 'CLOSED: [2026-11-10 Tue 09:00]']))
        if r.random() < .2:
            j = r.choice(IND)
            out += [j + ':PROPERTIES:', j + ':k: v', j + ':END:']
        for _ in range(r.randint(0, 7)):
            out.append(r.choice(IND) + r.choice(LINES))
    t = '\n'.join(out) + r.choice(['\n', '\n', ''])
    p = os.path.join(d, 't%05d.org' % i)
    open(p, 'w', encoding='utf-8', newline='').write(t)
    open(p + '.cmd', 'w').write('%d %s\n' % (len(t[:r.randint(0, len(t))].encode()), ''.join(r.choice('rrl') for _ in range(r.randint(1, 4)))))
