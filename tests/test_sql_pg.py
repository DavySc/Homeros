"""Package 22's PostgreSQL value oracle (args: DRIVER [SEED] [N]; DRIVER is
test_sql, run with -p): random values of PostgreSQL's common types, each
next to the hex of the server's own text for it (format %s: the type's
output function), decoded here (another decoder than the client's); the two
must be equal.  Also NULLs, values larger than a TLS record, and rows and
messages in the server's order.  Test servers only."""
import random, struct, subprocess, sys

drv = sys.argv[1]
seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
N = int(sys.argv[3]) if len(sys.argv) > 3 else 40
rnd = random.Random(seed)

def q(s): return "'" + s.replace("'", "''") + "'"
def chars(n):
    out = []
    for _ in range(n):
        r = rnd.random()
        if r < .6: out.append(chr(rnd.randint(32, 126)))
        elif r < .7: out.append(rnd.choice('\t\n\r\\'))
        else: out.append(chr(rnd.choice([rnd.randint(0xA0, 0xD7FF), rnd.randint(0xE000, 0xFFFD), rnd.randint(0x10000, 0x10FFFF)])))
    return ''.join(out)
def digits(n): return ''.join(rnd.choice('0123456789') for _ in range(n)) or '0'
def ymd(): return '%04d-%02d-%02d' % (rnd.randint(1, 9999), rnd.randint(1, 12), rnd.randint(1, 28))
def hms(): return '%02d:%02d:%02d.%s' % (rnd.randint(0, 23), rnd.randint(0, 59), rnd.randint(0, 59), digits(rnd.randint(0, 6)))
def pick(*special): return lambda f: (lambda: rnd.choice(special) if rnd.random() < .2 else f())
def flt(bits):
    v = struct.unpack('<d' if bits == 64 else '<f', rnd.getrandbits(bits).to_bytes(bits // 8, 'little'))[0]
    return q(repr(v) if v == v and abs(v) != float('inf') else rnd.choice(['NaN', 'Infinity', '-Infinity']))
def text():
    return q(chars(rnd.randint(0, 80))) if rnd.random() < .95 else 'repeat(%s, %d)' % (q(chars(5)), rnd.randint(4000, 40000))

TYPES = [
    ('int2', lambda: str(rnd.choice([-32768, 32767, 0, rnd.randint(-32768, 32767)]))),
    ('int4', lambda: str(rnd.choice([-2**31, 2**31 - 1, rnd.randint(-2**31, 2**31 - 1)]))),
    ('int8', lambda: str(rnd.choice([-2**63, 2**63 - 1, rnd.randint(-2**63, 2**63 - 1)]))),
    ('numeric', pick("'NaN'", "'Infinity'", "'-Infinity'")(lambda: q(rnd.choice(['', '-']) + digits(rnd.randint(0, 40)) + '.' + digits(rnd.randint(0, 30))))),
    ('float4', lambda: flt(32)), ('float8', lambda: flt(64)),
    ('bool', lambda: rnd.choice(['true', 'false'])),
    ('text', text), ('varchar', text),
    ('bytea', lambda: "'\\x" + ''.join('%02x' % rnd.randint(0, 255) for _ in range(rnd.randint(0, 60))) + "'"),
    ('date', pick("'infinity'", "'-infinity'", "'4713-01-01 BC'")(lambda: q(ymd()))),
    ('timestamp', pick("'infinity'", "'-infinity'")(lambda: q(ymd() + ' ' + hms()))),
    ('timestamptz', lambda: q('%s %s%+03d' % (ymd(), hms(), rnd.randint(-12, 14)))),
    ('time', lambda: q(hms())), ('timetz', lambda: q('%s%+03d' % (hms(), rnd.randint(-12, 14)))),
    ('interval', lambda: q('%d years %d mons %d days %d:%02d:%02d' % (rnd.randint(-100, 100), rnd.randint(-11, 11), rnd.randint(-30, 30), rnd.randint(0, 99), rnd.randint(0, 59), rnd.randint(0, 59)))),
    ('uuid', lambda: q('%032x' % rnd.getrandbits(128))),
    ('jsonb', lambda: q('{"a": %s, "b": [1, "x\\ty", null]}' % rnd.randint(-9, 9))),
    ('json', lambda: q('{"k":\t"%s"}' % ''.join(c for c in chars(5) if c not in '"\\\t\n\r'))),
    ('int4[]', lambda: q('{%s}' % ','.join(str(rnd.randint(-99, 99)) for _ in range(rnd.randint(0, 6))))),
    ('text[]', lambda: 'array[%s]::text[]' % ','.join(q(chars(rnd.randint(0, 6))) for _ in range(rnd.randint(1, 4)))),
    ('inet', lambda: q('%d.%d.%d.%d/%d' % tuple([rnd.randint(0, 255) for _ in range(4)] + [32]))),
]
def esc(t): return t.replace('\\', '\\\\').replace('\t', '\\t').replace('\n', '\\n').replace('\r', '\\r')

def run(sql):
    p = subprocess.run([drv, 'run', '-p', '-t'], input=sql.encode(), capture_output=True, timeout=120)
    if p.returncode: print('test_sql failed:', p.stderr.decode()[:300], sql[:200]); sys.exit(1)
    return p.stdout.decode()

bad = total = 0
for name, make in TYPES:
    vals = ['((%s)::%s)' % (make(), name) for _ in range(N)] + ['(null::%s)' % name]
    out = run("select x as got, case when x is not null then encode(convert_to(format('%%s', x), 'UTF8'), 'hex') end as want from (values %s) u(x)" % ', '.join(vals))
    if out is None: bad += 1; continue
    lines = out.split('\n')
    rows = [l.split('\t') for l in lines[1:-2]]
    if lines[-2:] != ['(SELECT %d)' % len(vals), ''] or len(rows) != len(vals): print('DIFF %s: %d rows, want %d' % (name, len(rows), len(vals))); bad += 1
    if rows and rows[-1] != ['NULL', 'NULL']: print('DIFF %s: NULL shown as %r' % (name, rows[-1])); bad += 1   # VALUES keeps its order
    for r in rows:
        total += 1
        ok = len(r) == 2 and (r == ['NULL', 'NULL'] or (r[1] != 'NULL' and r[0] == esc(bytes.fromhex(r[1]).decode('utf-8'))))
        if not ok:
            bad += 1
            if bad <= 15: print('DIFF %s: got %r want %r' % (name, r[0][:120], r[1][:120] if len(r) > 1 else None))

# the order of rows and messages; a simple query stops at its first error
out = run("select 1 a; do $$ begin raise notice 'n1'; end $$; select 2 b; select 1/0; select 3")
want = 'a\n1\n(SELECT 1)\nNOTICE 00000: n1\n(DO)\nb\n2\n(SELECT 1)\nERROR 22012: division by zero\n'
total += 1
if out != want: bad += 1; print('DIFF order:', repr(out))
print('pg oracle seed %d: %d values, %d differ' % (seed, total, bad))
sys.exit(1 if bad else 0)
