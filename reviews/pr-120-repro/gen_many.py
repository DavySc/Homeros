#!/usr/bin/env python3
"""A case file in test_core_diag.txt's format: LINES lines of "int x = 1;",
one diagnostic on the first PER bytes of every line (or, with a 4th
argument "one", N diagnostics side by side on line 0).
usage: gen_many.py LINES PER OUT [one]"""
import sys
n, per, out = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
one = len(sys.argv) > 4
line = (b"x" * 200 if one else b"int x = 1;") + b"\n"
text = line * n
with open(out, "w") as f:
    f.write("C 40 120 0 0 0 0\n")
    if one:
        for k in range(per): f.write("D %d %d 1 %s\n" % (k, k + 1, b"m".hex()))
    else:
        for k in range(n): f.write("D %d %d %d %s\n" % (k * len(line), k * len(line) + per, 1 + k % 4, b"expected ;".hex()))
    f.write("W 0 0 0 0 0\nF %s\nE\n" % text.hex())
