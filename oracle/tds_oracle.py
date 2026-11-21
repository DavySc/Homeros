"""Bake-off oracle for client/tds.c (args: TDSQ [SEED] [N]): random values of
every type SQL Server 2022 sends, each next to the server's own text for it
(CONVERT with the style SSMS shows), through tdsq; the two columns must be
equal.  Also NULLs (in wide rows, so the server sends NBCROW), sql_variant,
values larger than a packet, and rows and messages in the server's order.
Test servers only."""
import random, re, struct, subprocess, sys

tdsq = sys.argv[1]
seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
N = int(sys.argv[3]) if len(sys.argv) > 3 else 40
rnd = random.Random(seed)

def q(s): return "N'" + s.replace("'", "''") + "'"
def chars(n, wide):
    out = []
    for _ in range(n):
        r = rnd.random()
        if r < .6: out.append(chr(rnd.randint(32, 126)))
        elif r < .7: out.append(rnd.choice('\t\n\r\\'))
        elif r < .85: out.append(chr(rnd.choice([rnd.randint(0xA0, 0xFF), 0x20AC, 0x201C, 0x2122, 0x0160, 0x0178])))
        elif wide: out.append(chr(rnd.choice([rnd.randint(0x100, 0xD7FF), rnd.randint(0xE000, 0xFFFD), rnd.randint(0x10000, 0x10FFFF)])))
        else: out.append('e')
    return ''.join(out)
def digits(n): return ''.join(rnd.choice('0123456789') for _ in range(n)) or '0'
def ymd(lo=1, hi=9999): return '%04d-%02d-%02d' % (rnd.randint(lo, hi), rnd.randint(1, 12), rnd.randint(1, 28))
def hms(): return '%02d:%02d:%02d' % (rnd.randint(0, 23), rnd.randint(0, 59), rnd.randint(0, 59))
def frac(s): return '.' + digits(s) if s else ''
def off(): m = rnd.randint(-840, 840); return '%s%02d:%02d' % ('-' if m < 0 else '+', abs(m) // 60, abs(m) % 60)

# type: (literal maker, server text of x)
def dec():
    p = rnd.randint(1, 38); s = rnd.randint(0, p)
    return 'decimal(%d,%d)' % (p, s), "cast('%s%s.%s' as decimal(%d,%d))" % (rnd.choice(['', '-']), digits(p - s) if p > s else '0', digits(s), p, s)
def flt():
    while True:
        v = struct.unpack('<d', rnd.getrandbits(64).to_bytes(8, 'little'))[0] if rnd.random() < .5 else rnd.uniform(-1e6, 1e6)
        if v == v and abs(v) != float('inf') and (v == 0 or 1e-300 < abs(v) < 1e300): return "cast('%r' as float)" % v
def real():
    while True:
        v = struct.unpack('<f', rnd.getrandbits(32).to_bytes(4, 'little'))[0] if rnd.random() < .5 else rnd.uniform(-1e4, 1e4)
        if v == v and abs(v) != float('inf') and (v == 0 or 1e-37 < abs(v) < 1e37): return "cast('%r' as real)" % struct.unpack('<f', struct.pack('<f', v))[0]
def ints(t, lo, hi):
    return lambda: "cast(%d as %s)" % (rnd.choice([lo, hi, 0, -1 if lo < 0 else 1, rnd.randint(lo, hi)]), t)
def binlit(n): return '0x' + ''.join('%02X' % rnd.randint(0, 255) for _ in range(n))

S = "convert(nvarchar(max), x)"
TYPES = [
    ('tinyint', ints('tinyint', 0, 255), S), ('smallint', ints('smallint', -32768, 32767), S),
    ('int', ints('int', -2**31, 2**31 - 1), S), ('bigint', ints('bigint', -2**63, 2**63 - 1), S),
    ('bit', lambda: 'cast(%d as bit)' % rnd.randint(0, 1), S),
    ('decimal', lambda: dec()[1], S),
    ('money', lambda: "cast('%s%s.%s' as money)" % (rnd.choice(['', '-']), digits(rnd.randint(1, 14)), digits(4)), "convert(nvarchar(max), x, 2)"),
    ('smallmoney', lambda: "cast('%s%s.%s' as smallmoney)" % (rnd.choice(['', '-']), digits(rnd.randint(1, 5)), digits(4)), "convert(nvarchar(max), x, 2)"),
    ('float', flt, "convert(nvarchar(max), convert(binary(8), x), 1)"), ('real', real, "convert(nvarchar(max), convert(binary(4), x), 1)"),
    ('datetime', lambda: "cast('%s %s%s' as datetime)" % (ymd(1753), hms(), frac(3)), "convert(nvarchar(max), x, 121)"),
    ('smalldatetime', lambda: "cast('%s %s' as smalldatetime)" % (ymd(1900, 2078), hms()[:5]), "convert(nvarchar(max), x, 120)"),
    ('date', lambda: "cast('%s' as date)" % ymd(), S),
]
for s in range(8):
    TYPES += [
        ('time(%d)' % s, (lambda s: lambda: "cast('%s%s' as time(%d))" % (hms(), frac(s), s))(s), S),
        ('datetime2(%d)' % s, (lambda s: lambda: "cast('%s %s%s' as datetime2(%d))" % (ymd(), hms(), frac(s), s))(s), S),
        ('datetimeoffset(%d)' % s, (lambda s: lambda: "cast('%s %s%s %s' as datetimeoffset(%d))" % (ymd(2, 9998), hms(), frac(s), off(), s))(s), S),
    ]
TYPES += [
    ('uniqueidentifier', lambda: "cast('%08X-%04X-%04X-%04X-%012X' as uniqueidentifier)" % tuple(rnd.getrandbits(b) for b in (32, 16, 16, 16, 48)), S),
    ('varchar', lambda: "cast(%s as varchar(200))" % q(chars(rnd.randint(0, 60), False)), S),
    ('char', lambda: "cast(%s as char(30))" % q(chars(rnd.randint(0, 30), False)), S),
    ('nvarchar', lambda: "cast(%s as nvarchar(200))" % q(chars(rnd.randint(0, 60), True)), S),
    ('nchar', lambda: "cast(%s as nchar(30))" % q(chars(rnd.randint(0, 15), True)), S),
    ('varchar(max)', lambda: "cast(replicate(cast(%s as varchar(max)), %d) as varchar(max))" % (q(chars(rnd.randint(1, 40), False)), rnd.choice([1, 300, 3000])), S),
    ('nvarchar(max)', lambda: "replicate(cast(%s as nvarchar(max)), %d)" % (q(chars(rnd.randint(1, 40), True)), rnd.choice([1, 300, 3000])), S),
    ('varchar utf8', lambda: "cast(%s collate Latin1_General_100_CI_AS_SC_UTF8 as varchar(400))" % q(chars(rnd.randint(0, 60), True)), S),
    ('varbinary', lambda: "cast(%s as varbinary(100))" % binlit(rnd.randint(0, 50)), "convert(nvarchar(max), x, 1)"),
    ('binary', lambda: "cast(%s as binary(16))" % binlit(rnd.randint(1, 16)), "convert(nvarchar(max), x, 1)"),
    ('varbinary(max)', lambda: "cast(replicate(cast(%s as varchar(max)), %d) as varbinary(max))" % (q(chars(rnd.randint(1, 9), False).replace('\\', 'x')), rnd.choice([1, 2000])), "convert(nvarchar(max), x, 1)"),
    ('xml', lambda: "cast(%s as xml)" % q('<a b="%s">%s</a>' % (digits(3), chars(rnd.randint(0, 20), True).replace('&', '').replace('<', '').replace('\r', ''))), S),
    ('text', lambda: "cast(%s as text)" % q(chars(rnd.randint(0, 40), False)), "convert(nvarchar(max), convert(varchar(max), x))"),
    ('ntext', lambda: "cast(%s as ntext)" % q(chars(rnd.randint(0, 40), True)), "convert(nvarchar(max), x)"),
    ('image', lambda: "cast(%s as image)" % binlit(rnd.randint(1, 40)), "convert(nvarchar(max), convert(varbinary(max), x), 1)"),
    ('hierarchyid', lambda: "hierarchyid::Parse('/%d/%d/')" % (rnd.randint(0, 99), rnd.randint(0, 99)), "convert(nvarchar(max), convert(varbinary(max), x), 1)"),
    ('geography', lambda: "geography::Point(%f, %f, 4326)" % (rnd.uniform(-89, 89), rnd.uniform(-179, 179)), "convert(nvarchar(max), convert(varbinary(max), x), 1)"),
]
VARIANT = {'tinyint', 'smallint', 'int', 'bigint', 'bit', 'decimal', 'money', 'float', 'real', 'datetime', 'date', 'time(3)',
           'datetime2(7)', 'datetimeoffset(2)', 'uniqueidentifier', 'varchar', 'nvarchar', 'varbinary'}

# strings: the server's bytes, decoded here, so the client's decoders are checked by another one
HEX = "convert(nvarchar(max), convert(varbinary(max), x), 1)"
DECODE = {'varchar': 'cp1252', 'char': 'cp1252', 'varchar(max)': 'cp1252', 'text': 'cp1252', 'varchar utf8': 'utf-8',
          'nvarchar': 'utf-16-le', 'nchar': 'utf-16-le', 'nvarchar(max)': 'utf-16-le', 'ntext': 'utf-16-le'}
TYPES = [(n, m, HEX.replace('(max), x)', '(max), convert(%svarchar(max), x))' % ('n' if DECODE[n] == 'utf-16-le' else '')) if n in ('text', 'ntext') else HEX) if n in DECODE else (n, m, w) for n, m, w in TYPES]
def esc(t): return t.replace('\\', '\\\\').replace('\t', '\\t').replace('\n', '\\n').replace('\r', '\\r')

bad = total = 0
def run(sql):
    p = subprocess.run([tdsq, '-t', '127.0.0.1', '1433', 'sa', 'Homeros_Test1', 'master'], input=sql.encode(), capture_output=True, timeout=120)   # a client that hangs fails the run (a null-bitmap plant hung it for 3 h)
    if p.returncode: print('tdsq failed:', p.stderr.decode()[:300], sql[:200]); sys.exit(1)   # one failed run fails the check (a hang would cost 120 s per type)
    return p.stdout.decode()

for name, make, want in TYPES:
    for variant in ([0, 1] if name in VARIANT else [0]):
        vals = [make() for _ in range(N)] + ['null']
        x = 'cast(x as sql_variant)' if variant else 'x'
        sql = 'select %s as got, %s as want from (%s) u(x)' % (x, want, ' union all '.join('select %s' % v for v in vals))
        out = run(sql)
        if out is None: bad += 1; continue
        lines = out.split('\n')
        end = max(i for i, l in enumerate(lines) if re.fullmatch(r'\(\d+ rows? affected\)', l))
        rows = [l.split('\t') for l in lines[1:end]]
        if len(rows) != len(vals): print('DIFF %s: %d rows, want %d' % (name, len(rows), len(vals))); bad += 1
        for r in rows:
            total += 1
            ok = len(r) == 2 and r[0] == r[1]
            if name in DECODE and len(r) == 2 and r[1] != 'NULL':
                ok = r[0] == esc(bytes.fromhex(r[1][2:]).decode(DECODE[name], errors='replace'))
            if name in ('float', 'real') and len(r) == 2 and r[1] != 'NULL':   # the server's bits (style 3 overflows on e-300)
                try: ok = (float(r[0]) if name == 'float' else struct.unpack('<f', struct.pack('<f', float(r[0])))[0]) == struct.unpack('>d' if name == 'float' else '>f', bytes.fromhex(r[1][2:]))[0]
                except ValueError: ok = False
            if not ok:
                bad += 1
                if bad <= 15: print('DIFF %s%s: got %r want %r' % (name, ' as sql_variant' if variant else '', r[0][:120], r[1][:120] if len(r) > 1 else None))

# NBCROW: a wide row, mostly NULL; and the order of rows and messages
cols = ', '.join('cast(%s as int) c%d' % ('null' if rnd.random() < .7 else str(i), i) for i in range(40))
out = run('select %s; raiserror(%s, 16, 1); select 3 three; print %s' % (cols, q('mid'), q('end')))
want_tail = ['Msg 50000, Level 16, State 1, Line 1', 'mid', 'three', '3', '(1 row affected)', 'end', '']
total += 1
if out is None or out.split('\n')[-7:] != want_tail: bad += 1; print('DIFF order/NBCROW:', repr(out[-200:] if out else out))
print('tds oracle seed %d: %d values, %d differ' % (seed, total, bad))
sys.exit(1 if bad else 0)
