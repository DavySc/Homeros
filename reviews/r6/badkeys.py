import sys; sys.path.insert(0, "reviews/r6"); import drv
cases = {'F4 90 80 80 (0x110000 = KEY_UP)': b'\xf4\x90\x80\x80', 'F4 90 80 84 (KEY_RESIZE)': b'\xf4\x90\x80\x84', 'ED A0 80 (surrogate D800)': b'\xed\xa0\x80', 'C0 80 (overlong NUL)': b'\xc0\x80', 'C1 A1 (overlong a)': b'\xc1\xa1', 'F7 BF BF BF (0x1FFFFF)': b'\xf7\xbf\xbf\xbf', 'E0 80 AF (overlong /)': b'\xe0\x80\xaf', 'C3 alone then Z': b'\xc3'}
for name, seq in cases.items():
    p = '/tmp/bad.txt'; open(p, 'w').write('one\ntwo\n')
    drv.run(p, [b'j', b'i', seq, b'Z', b'\x1b', 0.2, b':wq\r'])
    print(name, '->', open(p, 'rb').read())
