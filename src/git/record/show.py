import json, sys
d = json.load(open(sys.argv[1]))
for n in sys.argv[2:]:
    sc = d[n]
    print('#####', n, '| setup:', sc['setup'])
    for st in sc['steps']:
        for w in st['windows']:
            print('== %r  [%s] cursor=%s sel=%s | echo: %s' % (st['keys'], w['buffer'], w['cursor'], w['selected'], st['echo']))
            for i, l in enumerate(w['lines']): print('%2d%s %s' % (i, '>' if w['cursor'] and w['cursor'][0] == i else ' ', l))
