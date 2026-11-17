#!/usr/bin/env python3
"""Review of PR 12: random lines, drawn differently from tests/test_uni_nvim.c:
uniform code points from scripts where clusters matter, whole short words of
them, and invalid bytes.  usage: gen_lines.py SEED COUNT > lines.bin"""
import random, sys
r = random.Random(int(sys.argv[1]))
BLOCKS = [(0x300, 0x36F), (0x600, 0x6FF), (0x900, 0x97F), (0x980, 0x9FF), (0xB80, 0xBFF), (0xD00, 0xD7F),
          (0xE00, 0xE7F), (0x1000, 0x109F), (0x1100, 0x11FF), (0xA960, 0xA97F), (0xAC00, 0xD7FF),
          (0x1F1E6, 0x1F1FF), (0x1F300, 0x1F6FF), (0x1F900, 0x1F9FF), (0x2600, 0x27BF), (0xFE00, 0xFE0F),
          (0x200B, 0x200F), (0x1F3FB, 0x1F3FF), (0xE0020, 0xE007F), (0xE0100, 0xE01EF), (0x11000, 0x1107F),
          (0x1780, 0x17FF), (0x1B00, 0x1B7F), (0x4E00, 0x4E20), (0xFF00, 0xFFEF), (0x80, 0xFF), (0x2000, 0x206F)]
def enc(c):
    if c < 0x80: return bytes([c])
    if c < 0x800: return bytes([0xC0 + c // 64, 0x80 + c % 64])
    if c < 0x10000: return bytes([0xE0 + c // 4096, 0x80 + c // 64 % 64, 0x80 + c % 64])
    return bytes([0xF0 + c // 262144, 0x80 + c // 4096 % 64, 0x80 + c // 64 % 64, 0x80 + c % 64])
for _ in range(int(sys.argv[2])):
    out = b''
    blocks = r.sample(BLOCKS, r.randint(1, 3)) + [(0x200D, 0x200D), (0x94D, 0x94D)]
    for _ in range(r.randint(1, 16)):
        x = r.random()
        if x < 0.80:
            a, b = r.choice(blocks); out += enc(r.randint(a, b))
        elif x < 0.88: out += bytes([r.choice(b'ax .\t')])
        elif x < 0.94: out += bytes([r.randint(0x80, 0xFF)])
        elif x < 0.97: out += enc(r.randint(0x80, 0x10FFFF))[:-1]
        else: out += enc(r.randint(0x80, 0x1FFFFF))
    sys.stdout.buffer.write(out + b'\n')
