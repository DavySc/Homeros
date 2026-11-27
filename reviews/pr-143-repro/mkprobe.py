# test_org.c with every timed check's two times printed (stderr):
# L unit try org_ms plain_ms / G gate try org_ms plain_ms / R walk_ms up_ms
# NOFAIL=1 drops the three timed CHECKs so every check is measured in one run
import sys, os
src, out = sys.argv[1], sys.argv[2]
s = open(src).read()
def rep(old, new):
    global s
    assert s.count(old) == 1, old
    s = s.replace(old, new)
nf = os.environ.get('NOFAIL')
rep('ok = colour_line(line[i], m, 256, c) < 16 * colour_line(line[0], lm[0], lm[0], 0);',
    '{ double A = colour_line(line[i], m, 256, c), B = colour_line(line[0], lm[0], lm[0], 0); ok = A < 16 * B; fprintf(stderr, "L %d %d %.4f %.4f\\n", (int)i, r, A, B); }')
rep('ok = frame(many, m, l0, w, gate[k].shown, *gate[k].top, fc) < gate[k].bound * frame(plain, m, l0, w, 0, 0, fp);',
    '{ double A = frame(many, m, l0, w, gate[k].shown, *gate[k].top, fc), B = frame(plain, m, l0, w, 0, 0, fp); ok = A < gate[k].bound * B; fprintf(stderr, "G %d %d %.4f %.4f\\n", (int)k, r, A, B); }')
rep('CHECK(2 * (cpu_ms() - u1) < u1 - u0);',
    '{ double u2 = cpu_ms(); fprintf(stderr, "R 0 0 %.4f %.4f\\n", u2 - u1, u1 - u0); ' + ('' if nf else 'CHECK(2 * (u2 - u1) < u1 - u0);') + ' }')
if nf:
    s = s.replace('ok = A < 16 * B;', 'ok = 1;').replace('ok = A < gate[k].bound * B;', 'ok = 1;')
open(out, 'w').write(s)
