#!/usr/bin/env python3
"""mkprobe.py TEST_C OUT_C: the test with every try's time printed.
PROBE row i, k (0 short, 1 long), ms; then ROW i t0 t1 ratio."""
import sys
s = open(sys.argv[1]).read()
a = "\t\t\tif (ms < t[k % 2]) t[k % 2] = ms;\n"
assert s.count(a) == 1
s = s.replace(a, '\t\t\tprintf("TRY %zu %d %.3f\\n", i, k % 2, ms);\n' + a)
b = "\t\tif (t[1] / t[0] > worst) worst = t[1] / t[0];\n"
assert s.count(b) == 1
s = s.replace(b, '\t\tprintf("ROW %zu %.3f %.3f %.2f\\n", i, t[0], t[1], t[1] / t[0]);\n' + b)
open(sys.argv[2], 'w').write(s)
