# for each plant whose name matches the pattern: build a planted driver in the
# scratch copy and run the directed texts against Org (-f and -s)
import re, subprocess, sys, glob, os
copy, plants, pat, texts = sys.argv[1:5]
orig = open(copy + '/src/org/org.c').read()
for ln in open(plants).read().split('\n'):
    f = ln.split('~')
    if len(f) != 6 or not re.search(pat, f[0]): continue
    old, new = f[4].replace('¶', '\n'), f[5].replace('¶', '\n')
    assert orig.count(old) == 1, f[0]
    open(copy + '/src/org/org.c', 'w').write(orig.replace(old, new))
    r = subprocess.run(['make', '-s', '-f', 'rev.mk', 'drv'], cwd=copy, capture_output=True, text=True)
    print('===', f[0], '' if r.returncode == 0 else 'BUILD FAILED ' + r.stderr[-300:])
    for m in ('-f', '-s'):
        o = subprocess.run(['python3', 'tools/org_conform.py', m, 'build/san/org_drv_t'] + sorted(glob.glob(texts + '/*.org')), cwd=copy, capture_output=True, text=True).stdout
        names = sorted(set(re.findall(r'org-fail-\d+-(\w+\.org)|^(\w+\.org)', o, re.M)))
        print('  ', m, [l for l in o.split('\n') if 'differ' in l][-1:], ' '.join(sorted(set(re.findall(r'\b\w+\.org\b', o)))))
open(copy + '/src/org/org.c', 'w').write(orig)
