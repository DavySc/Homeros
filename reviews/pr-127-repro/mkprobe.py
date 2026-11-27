# test_org.c with every timed gate's time printed (stderr): L unit try us / G gate try us / R walk_ns up_ns
import sys
src, out = sys.argv[1], sys.argv[2]
o, n = [], 0
import os
text = open(src).read()
if os.environ.get('NOFAIL'):
    for c in ('CHECK(ms < 100);', 'CHECK(ms < gate[k].ms);', 'CHECK(d[1] < d[0]);'):
        assert text.count(c) == 1; text = text.replace(c, '')
for ln in text.split('\n'):
    o.append(ln)
    if 'ms = (t1.tv_sec - t0.tv_sec) * 1000 +' in ln:
        tag, var = (('L', 'i'), ('G', 'k'))[n]; n += 1
        o.append('fprintf(stderr, "%s %%d %%d %%ld\\n", (int)%s, r, (long)((t1.tv_sec - t0.tv_sec) * 1000000 + (t1.tv_nsec - t0.tv_nsec) / 1000));' % (tag, var))
    if 'for (int i = 0; i < 2; i++) d[i] =' in ln:
        o.append('fprintf(stderr, "R %ld %ld\\n", d[0], d[1]);'); n += 1
assert n == 3, n
open(out, 'w').write('\n'.join(o))
