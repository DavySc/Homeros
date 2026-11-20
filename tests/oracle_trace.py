#!/usr/bin/env python3
"""The engine's stepping against RAD's recorded traces: oracle_trace.py <trace.exe> <traces dir> <programs dir> [wine]

Each trace is replayed by tests/trace.c on the same program and compared line by line. Of a stop's reason only the
kind is compared: the thread's name and an exception's details are the frontend's words.
"""
import re, subprocess, sys
from pathlib import Path

driver, traces, programs, wine = sys.argv[1], Path(sys.argv[2]), sys.argv[3], sys.argv[4:]
def kind(line): return re.sub(r'\[main_thread\s+(completed step|stopped at entry point|hit an exception).*\]$', r'[\1]', line)
steps = bad = 0
for t in sorted(traces.glob('*.txt')):
    name, script = t.name.split('.')[:2]
    want = [kind(l) for l in t.read_text().splitlines()]
    got = subprocess.run(wine + [driver, name + '.exe', script, '600'], cwd=programs, capture_output=True, text=True, timeout=600).stdout.splitlines()
    steps += len(want)
    differ = [i for i in range(max(len(want), len(got))) if i >= len(want) or i >= len(got) or want[i] != got[i]]
    if differ:
        i = differ[0]; bad += 1
        print(f'{t.name}: {len(differ)} of {len(want)} lines differ, first at line {i + 1}:\n  RAD:  {want[i] if i < len(want) else "(no line)"}\n  ours: {got[i] if i < len(got) else "(no line)"}')
print(f'traces: {steps} steps in {len(list(traces.glob("*.txt")))} traces, {bad} traces differ')
sys.exit(1 if bad or not steps else 0)
