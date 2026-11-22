#!/usr/bin/env python3
"""Runs the dupes plants of a plants file and prints what each failure says:
repro/plants.py <repo> <plants.txt> [old-repo]
With old-repo, its tools/dupes.py and tests/dupes-known.txt replace the
copy's after planting (plants in those two files are skipped): what the old
tool says about the same planted source."""
import sys, os, shutil, subprocess, tempfile
repo, plants = sys.argv[1:3]
old_repo = sys.argv[3] if len(sys.argv) > 3 else None
for l in open(plants, encoding='utf-8'):
    p = l.rstrip('\n').split('~')
    if len(p) < 6 or not p[1].startswith('dupes'): continue
    name, _, _, f, old, new = p[0], p[1], p[2], p[3], p[4], '~'.join(p[5:])
    t = tempfile.mkdtemp()
    for d in ('src', 'ref', 'include', 'tests', 'tools'): shutil.copytree(os.path.join(repo, d), os.path.join(t, d))
    s = open(os.path.join(t, f), encoding='utf-8').read()
    old, new = old.replace('¶', '\n'), new.replace('¶', '\n')
    if s.count(old) != 1: print('BROKEN', name); continue
    open(os.path.join(t, f), 'w', encoding='utf-8').write(s.replace(old, new))
    if old_repo:
        if f in ('tools/dupes.py', 'tests/dupes-known.txt'): shutil.rmtree(t); continue
        for x in ('tools/dupes.py', 'tests/dupes-known.txt'): shutil.copy(os.path.join(old_repo, x), os.path.join(t, x))
    r = subprocess.run([sys.executable, 'tools/dupes.py'], cwd=t, capture_output=True, text=True)
    print('caught' if r.returncode else 'NOT CAUGHT', '|', name[:60], '|', (r.stdout.strip().split('\n')[0])[:110])
    shutil.rmtree(t)
