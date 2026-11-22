"""CPU seconds (user+sys) of each tool, 5 alternating runs, median and range:
cpu.py name=dupes.py[,known-list] ...   (run in a repo root; a known list is put in place for that tool)"""
import sys, subprocess, resource, statistics, shutil
tools = [a.split('=') for a in sys.argv[1:]]; r = {n: [] for n, _ in tools}
keep = open('tests/dupes-known.txt').read()
for i in range(5):
    for n, t in tools:
        t, _, k = t.partition(',')
        open('tests/dupes-known.txt', 'w').write(open(k).read() if k else keep)
        a = resource.getrusage(resource.RUSAGE_CHILDREN)
        subprocess.run([sys.executable, t], capture_output=True)
        b = resource.getrusage(resource.RUSAGE_CHILDREN)
        r[n].append(b.ru_utime + b.ru_stime - a.ru_utime - a.ru_stime)
open('tests/dupes-known.txt', 'w').write(keep)
for n, v in r.items(): print(f'{n}: median {statistics.median(v):.2f} s ({min(v):.2f} to {max(v):.2f})')
