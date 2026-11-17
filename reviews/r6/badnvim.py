import sys, os; sys.path.insert(0, "reviews/r6"); import drv, pty, time
NV='nvim'
cases = {'F4 90 80 80': b'\xf4\x90\x80\x80', 'ED A0 80': b'\xed\xa0\x80', 'C0 80': b'\xc0\x80', 'C1 A1': b'\xc1\xa1', 'F7 BF BF BF': b'\xf7\xbf\xbf\xbf', 'E0 80 AF': b'\xe0\x80\xaf', 'C3 then Z': b'\xc3'}
orig = os.execv
for name, seq in cases.items():
    p = '/tmp/badn.txt'; open(p, 'w').write('one\ntwo\n')
    os.execv = lambda exe, a: orig(NV, [NV, '-u', 'NONE', '-n', a[1]])
    drv.run(p, [0.5, b'j', b'i', seq, b'Z', b'\x1b', 0.3, b':wq\r', 0.5])
    print(name, '->', open(p, 'rb').read())
