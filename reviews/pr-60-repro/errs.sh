#!/bin/bash
# Is the `errs` rule of render() (keep the gap next to a byte the lexer does
# not know) ever seen?  Builds sqlfmt without it and compares both on pieces
# of real T-SQL with unknown bytes put in.  usage: errs.sh FILES...
T=$(mktemp -d)
sed -e 's/|| strings || errs) put(g, gn);/|| strings) put(g, gn);/' -e 's|"../syntax/tsql.h"|"../src/syntax/tsql.h"|' src/sqlfmt/sqlfmt.c > $T/sqlfmt.c
gcc -std=c17 -D_DEFAULT_SOURCE -O2 -Iinclude -Wno-unused-variable -o $T/drv src/sqlfmt/oracle/drv.c $T/sqlfmt.c src/syntax/tsql.c src/text/text.c
python3 - $T "$@" <<'PY'
import random, subprocess, re, sys
T, files = sys.argv[1], sys.argv[2:]
R = random.Random(7); d = 0
data = [open(f, 'rb').read().replace(b'\r', b'') for f in files]
bad = [b'\x01', b'\\', b'$', b'\x7f', b'\x00', b'}', b'`', b'?', b'\x1b']
for i in range(6000):
    L = R.choice(data).split(b'\n'); a = R.randrange(len(L)); s = b'\n'.join(L[a:a + R.randrange(1, 25)]) + b'\n'
    t = re.findall(rb'\s+|\w+|.', s, re.S)
    for _ in range(R.randrange(1, 5)): t.insert(R.randrange(len(t) + 1), R.choice(bad) + R.choice([b'', b' ', b'  ', b'\n']))
    if R.random() < .3: t = [R.choice([b'begin ' * 1001, b'GO\n', b''])] + t
    open(T + '/in.sql', 'wb').write(b''.join(t))
    d += subprocess.run(['build/rv/drv', T + '/in.sql'], capture_output=True).stdout != subprocess.run([T + '/drv', T + '/in.sql'], capture_output=True).stdout
print('6000 inputs with unknown bytes,', d, 'differ without the errs rule')
PY
