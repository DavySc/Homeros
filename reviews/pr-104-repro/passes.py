"""time of each pass of tools/dupes.py, and of the owners pass done on text
read once (run in the repo root)"""
import sys, time
import glob, re, fnmatch, types
dupes = types.ModuleType("dupes"); exec(open("tools/dupes.py").read().rsplit("\nsys.exit", 1)[0], dupes.__dict__)
files = sorted(f for d in ('src','ref','include') for f in glob.glob(d+'/**/*.[ch]', recursive=True))
mods = [f for f in files if f.count('/') <= 2 - f.startswith(('ref/', 'include/'))]
print(len(files), 'files', len(mods), 'module files')
t=time.time(); toks={f:dupes.tokens(f) for f in files}; print('tokens', round(time.time()-t,3))
t=time.time(); n=sum(1 for f in mods for _ in dupes.shapes(toks[f])); print('shapes', n, round(time.time()-t,3))
rules=[l.rstrip('\n').split(' | ') for l in open('tests/owners.txt') if l.strip() and l[0]!='#']
t=time.time(); a=[]
for name,rx,own in rules:
    for f in mods:
        for k,line in enumerate(dupes.clean(f).split('\n'),1):
            if re.search(rx,line): a.append((name,f,k))
print('owners as written', len(a), round(time.time()-t,3))
t=time.time(); c={f:dupes.clean(f) for f in mods}; b=[]
for name,rx,own in rules:
    for f in mods:
        for m in re.finditer(rx,c[f]): b.append((name,f,c[f].count('\n',0,m.start())+1))
print('owners on text cleaned once', len(b), round(time.time()-t,3), 'same lines' if sorted(set(a))==sorted(set(b)) else 'DIFFERENT')
