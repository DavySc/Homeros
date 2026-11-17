import re, sys, statistics as st, collections
d = collections.defaultdict(lambda: collections.defaultdict(list)); sec = None
for line in open('timing.log'):
    if line.startswith('=='): sec = line.strip('= \n'); continue
    p = line.split()
    if not p: continue
    name = p[0]; scen = p[1] if sec == 'scenarios' else ''
    for k, v in re.findall(r'(\w+)=([0-9.]+)', line):
        if k in ('ok', 'sink', 'sum', 'len'): continue
        d[(sec, scen, k)][name].append(float(v.rstrip('.')))
for (sec, scen, k), by in d.items():
    print(sec[:10], scen, k, ' | '.join(f"{n} {st.median(v):.3f} ({min(v):.3f}-{max(v):.3f}) n={len(v)}" for n, v in by.items()))
