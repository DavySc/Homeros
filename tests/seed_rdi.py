#!/usr/bin/env python3
"""Write a small well-formed RDI file: a fuzz seed, and the fixture for test_rdi.c.
One unit, one source file "a.c", lines 10,11,13,12 at voffs 0x1000,0x1010,0x1010 (a second entry at the same voff),0x1020, end 0x1030."""
import struct, sys
S = {k: b"" for k in range(43)}
strings = [b"", b"a.c", b"main"]
S[2] = b"".join(strings)
offs, o = [], 0
for s in strings: offs.append(o); o += len(s)
S[3] = struct.pack("<%dI" % len(offs), *offs)
S[4] = struct.pack("<4I", 0, 1, 1, 1)
S[7] = struct.pack("<5I", 0, 0, 0, 0, 0) + struct.pack("<5I", 0, 1, 1, 0, 0)              # source files
S[8] = struct.pack("<5I", 0, 0, 0, 0, 0) + struct.pack("<5I", 0, 0, 0, 5, 0)              # line tables
S[9] = struct.pack("<5Q", 0x1000, 0x1010, 0x1010, 0x1020, 0x1030)
S[10] = struct.pack("<10I", 1, 10, 1, 11, 1, 13, 1, 12, 0, 0)
S[12] = struct.pack("<5I", 0, 0, 0, 0, 0) + struct.pack("<5I", 3, 3, 0, 0, 0)             # source line maps
S[13] = struct.pack("<3I", 10, 11, 12)
S[14] = struct.pack("<4I", 0, 1, 2, 3)
S[15] = struct.pack("<3Q", 0x1000, 0x1010, 0x1020)
S[16] = b"\0" * 64 + struct.pack("<16I", 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0)  # units
S[17] = struct.pack("<4Q", 0x1000, 1, 0x1030, 0)
S[31] = b"\0" * 32 + struct.pack("<IBBHQ4I", 2, 0, 0, 0, 0, 0, 0, 0, 0)                   # procedures
S[40] = b"".join(struct.pack("<4I", 0, 0, 3 * (k == 4), k == 4) for k in range(9))     # name maps
h = 5381
for c in b"main": h = (h * 33 + c) % 2**64
S[41] = b"".join(struct.pack("<2I", 0, h % 3 == b) for b in range(3))                   # three buckets: a wrong multiplier misses
S[42] = struct.pack("<3I", 2, 1, 1)
body, table, off = b"", b"", 24 + 43 * 32
for k in range(43):
    table += struct.pack("<IIQQQ", 0, 0, off, len(S[k]), len(S[k]))
    pad = -len(S[k]) % 8
    body += S[k] + b"\0" * pad; off += len(S[k]) + pad
open(sys.argv[1], "wb").write(struct.pack("<QIII", 0x0000676264646172, 23, 24, 43) + b"\0" * 4 + table + body)
