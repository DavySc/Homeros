# a plant no check caught and no directed text shows: is its code visible at
# all?  Build the planted driver at -O2 and compare it with the real one on
# random dense texts (memo_fuzz.py).  python3 redundant.py COPY PLANTS PATTERN REAL
import re, subprocess, sys, os
copy, plants, pat, real = sys.argv[1:5]
here = os.path.dirname(os.path.abspath(__file__))
orig = open(copy + '/src/org/org.c').read()
for ln in open(plants).read().split('\n'):
    f = ln.split('~')
    if len(f) != 6 or not re.search(pat, f[0]): continue
    old, new = f[4].replace('¶', '\n'), f[5].replace('¶', '\n')
    open(copy + '/src/org/org.c', 'w').write(orig.replace(old, new))
    r = subprocess.run(['make', '-s', '-f', 'fdrv.mk', 'fdrv'], cwd=copy, capture_output=True, text=True)
    print('===', f[0], '' if r.returncode == 0 else 'BUILD FAILED', flush=True)
    for seed in ('11', '12', '13'):
        print('  ', subprocess.run(['python3', here + '/memo_fuzz.py', real, copy + '/build/gcc/org_drv', seed, '20000', copy + '/mf'], capture_output=True, text=True).stdout.strip().split('\n')[-1], flush=True)
open(copy + '/src/org/org.c', 'w').write(orig)
