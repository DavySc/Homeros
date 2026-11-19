import subprocess, sys, os
os.environ['HDD'] = 'r'
def h(b, t): return subprocess.run(['../%s/build/san/test_zzdiff' % t, 'x', b.hex()], capture_output=True, text=True).stdout
def bad(b): return h(b, 'wt-main') != h(b, 'wt-fold')
b = bytes.fromhex(sys.argv[1]); assert bad(b)
ch = True
while ch:
    ch = False
    i = 0
    while i < len(b):
        c = b[:i] + b[i+2:]
        if bad(c): b = c; ch = True
        else: i += 2
print(b.hex())
