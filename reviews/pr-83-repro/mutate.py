#!/usr/bin/env python3
"""Review of PR 83: tools/org_conform.py's colour texts with a few random
characters inserted, deleted or doubled, so that lines the generator always
writes whole are also seen broken.  mutate.py DIR N SEED writes N files."""
import os, random, sys
import types
# org_conform.py runs main() when imported: take its generator only
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools', 'org_conform.py')).read()
oc = types.SimpleNamespace(__file__=__file__)
exec(src[:src.rindex('\nmain()')], oc.__dict__)

CH = list('*#+:|[]<>%@-_=/ \t\r\f.)0123456789xXaé') + [' ', '　', 'DONE', 'TODO', '::', '@@', '#+end_src', '#+begin_src', '\r\n', '\n']
out, n, seed = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
r = random.Random(seed)
os.makedirs(out, exist_ok=True)
for i in range(n):
    s = oc.gen_faces(r)
    for _ in range(r.randint(1, 4)):
        p = r.randint(0, len(s))
        k = r.random()
        if k < .5:
            s = s[:p] + r.choice(CH) + s[p:]
        elif k < .8:
            s = s[:p] + s[p + 1:]
        else:
            s = s[:p] + s[p:p + r.randint(1, 12)] + s[p:]
    with open(os.path.join(out, 'm%05d.org' % i), 'w', encoding='utf-8', newline='') as f:
        f.write(s)
