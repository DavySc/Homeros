#!/usr/bin/env python3
"""The engine's stepping against RAD's recorded traces: oracle_trace.py <trace.exe> <traces dir> <programs dir> [wine]

Each trace is replayed by tests/trace.c on the same program and compared line by line. Of a stop's reason only the
kind is compared: the thread's name and an exception's details are the frontend's words. tests/trace_known.txt
lists the reviewed differences: such a trace is compared up to the line named there.
"""
import re, subprocess, sys
from pathlib import Path

driver, traces, programs, wine = sys.argv[1], Path(sys.argv[2]), sys.argv[3], sys.argv[4:]
if not wine: driver = str((Path(programs) / driver).resolve())   # Windows looks for a program from where the caller stands
def kind(line): return re.sub(r'\[main_thread\s+(completed step|stopped at entry point|hit an exception).*\]$', r'[\1]', line)
known = {}   # trace: (the line up to which it is compared here, the line where it must differ or 0)
for l in (Path(__file__).parent / 'trace_known.txt').read_text().splitlines():
    if l.strip() and l[0] != '#' and l.split()[1] in ('all', 'wine' if wine else 'windows'): known[l.split()[0]] = (int(l.split()[2]), int(l.split()[3]))
steps = bad = 0
files = [t for t in sorted(traces.glob('*.txt')) if not t.name.endswith('.eval.txt')]   # those are RAD's values, not steps (tests/zoo_check.c)
for t in files:
    name, script = t.name.split('.')[:2]
    want = [kind(l) for l in t.read_text().splitlines()]
    got = subprocess.run(wine + [driver, name + '.exe', script, '600'], cwd=programs, capture_output=True, text=True, timeout=120).stdout.splitlines()
    if t.name in known:   # past that line the two are in different places
        at, differs = known[t.name]
        if differs and got[differs - 1:differs] == want[differs - 1:differs]: bad += 1; print(f'{t.name}: the known difference at line {differs} is gone')
        want, got = want[:at - 1], got[:at - 1]
    steps += len(want)
    differ = [i for i in range(max(len(want), len(got))) if i >= len(want) or i >= len(got) or want[i] != got[i]]
    if differ:
        i = differ[0]; bad += 1
        print(f'{t.name}: {len(differ)} of {len(want)} lines differ, first at line {i + 1}:\n  RAD:  {want[i] if i < len(want) else "(no line)"}\n  ours: {got[i] if i < len(got) else "(no line)"}')
print(f'traces: {steps} steps in {len(files)} traces, {bad} traces differ')
sys.exit(1 if bad or not steps else 0)
