#!/usr/bin/env python3
"""Median and range over the runs of bench.sh's output, per file and side."""
import re, sys, statistics as st
rows = {}
for l in sys.stdin:
    m = re.match(r"(old|new) seed (\d+) (\S+): .*syntax adds to a key: median ([\d.]+) ms, p99 ([\d.]+).*syntax_faces in a key's frame: median ([\d.]+), p99 ([\d.]+), max ([\d.]+); in the frame after a parse: median ([\d.]+), p99 ([\d.]+)", l)
    if m:
        rows.setdefault((m[3].split("/")[-1], m[1]), []).append([float(x) for x in m.groups()[3:]])
names = ["key adds: median", "key adds: p99", "faces in key frame: median", "p99", "max", "frame after parse: median", "p99"]
for (f, side), v in sorted(rows.items()):
    print(f"{f} {side} ({len(v)} runs)")
    for i, n in enumerate(names):
        c = [r[i] for r in v]
        print(f"   {n}: {st.median(c):.3f} [{min(c):.3f}-{max(c):.3f}]")
