# test_org.c (PR 143 second head: least of 5 on each side) with every timed check's two least times printed (stderr):
# L unit 0 org_ms plain_ms / S 0 0 line_ms sixteenth_ms / G gate 0 org_ms plain_ms / R 0 0 up_ms walk_ms
# NOFAIL=1 drops the timed CHECKs so every check is measured in one run
import sys, os
src, out = sys.argv[1], sys.argv[2]
s = open(src).read()
nf = os.environ.get('NOFAIL')
def rep(old, tag, idx):
    global s
    assert s.count(old) == 1, old
    s = s.replace(old, '{ fprintf(stderr, "%s %%d 0 %%.4f %%.4f\\n", (int)(%s), a, b); %s }' % (tag, idx, '' if nf else old))
rep('CHECK(a < 16 * b);', 'L', 'i')
rep('CHECK(a < 32 * b);', 'S', '0')
rep('CHECK(a < gate[k].bound * b);', 'G', 'k')
old = 'CHECK(2 * (cpu_ms() - u1) < u1 - u0);'
assert s.count(old) == 1
s = s.replace(old, '{ double u2 = cpu_ms(); fprintf(stderr, "R 0 0 %.4f %.4f\\n", u2 - u1, u1 - u0); ' + ('' if nf else 'CHECK(2 * (u2 - u1) < u1 - u0);') + ' }')
open(out, 'w').write(s)
