"""Slice 1 oracle (args: DB [SEED] [ROUNDS] [crlf]): every object, column and module of a test database, as
INFORMATION_SCHEMA and OBJECT_DEFINITION give them (not the snapshot's
sys.* query), must be found by build/snapq under random spellings (case
changed where the collation ignores case, [..] or ".." quoting), and
mutated names must not be found.  Test servers only.

python3 oracle/snap_oracle.py DB [SEED] [ROUNDS]"""
import random, subprocess, sys, pymssql

db, seed, rounds = sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 1, int(sys.argv[3]) if len(sys.argv) > 3 else 3
rnd = random.Random(seed)
con = pymssql.connect('localhost', 'sa', 'Homeros_Test1', db)
cur = con.cursor()
cur.execute("select convert(nvarchar(128), databasepropertyex(db_name(), 'Collation')), schema_name()")
coll, dflt = cur.fetchone()
ci = '_CI' in coll
cur.execute("select TABLE_SCHEMA, TABLE_NAME from INFORMATION_SCHEMA.TABLES union select ROUTINE_SCHEMA, ROUTINE_NAME from INFORMATION_SCHEMA.ROUTINES")
objs = cur.fetchall()
cur.execute("select TABLE_SCHEMA, TABLE_NAME, COLUMN_NAME from INFORMATION_SCHEMA.COLUMNS c where exists (select * from INFORMATION_SCHEMA.TABLES t where t.TABLE_SCHEMA = c.TABLE_SCHEMA and t.TABLE_NAME = c.TABLE_NAME) or objectproperty(object_id(quotename(TABLE_SCHEMA) + '.' + quotename(TABLE_NAME)), 'IsTableFunction') = 1")
cols = cur.fetchall()
cur.execute("select schema_name(o.schema_id), o.name, object_definition(o.object_id) from sys.objects o where o.type in ('V','IF','TF','FN','P','TR') and o.is_ms_shipped = 0")
mods = cur.fetchall()

def key(s): return ''.join(c.lower() if c < '\x80' else c for c in s) if ci else s
def spell(n):
    if ci: n = ''.join(c.swapcase() if c < '\x80' and rnd.random() < .5 else c for c in n)
    r = rnd.random()
    if r < .4 and n and (n[0].isalpha() or n[0] == '_') and all(c.isalnum() or c == '_' for c in n): return n
    if r < .7: return '[' + n.replace(']', ']]') + ']'
    return '"' + n.replace('"', '""') + '"'
def fnv(b):
    h = 2166136261
    for x in b: h = ((h ^ x) * 16777619) & 0xffffffff
    return '%08x' % h

def expect_obj(schema, name):
    hits = [o for o in objs if key(o[1]) == key(name) and (schema is None or key(o[0]) == key(schema))]
    if schema is not None: return hits[0] if hits else None
    d = [o for o in hits if o[0] == dflt]
    return d[0] if d else hits[0] if len(hits) == 1 else ('many' if hits else None)

qs, want = [], []
for _ in range(rounds):
    for s, n in objs:
        qs.append('o\t%s\t%s' % (spell(s), spell(n))); want.append(('o', (s, n)))
        e = expect_obj(None, n)
        qs.append('o\t-\t%s' % spell(n)); want.append(('o', e))
        qs.append('o\t%s\t%s' % (spell(s), spell(n + 'x'))); want.append(('o', expect_obj(s, n + 'x')))
    for s, t, c in cols:
        qs.append('c\t%s\t%s\t%s' % (spell(s), spell(t), spell(c))); want.append(('c', c))
        qs.append('c\t%s\t%s\t%s' % (spell(s), spell(t), spell(c + 'q'))); want.append(('c', None))
for s, n, d in mods:
    qs.append('m\t%s\t%s' % (spell(s), spell(n))); want.append(('m', '%d %s' % (len(d.encode()), fnv(d.encode()))))

cur.execute("select concat(schema_name(a.schema_id), '.', a.name, '.', ca.name, char(9), schema_name(b.schema_id), '.', b.name, '.', cb.name) from sys.foreign_key_columns f join sys.objects a on a.object_id = f.parent_object_id join sys.columns ca on ca.object_id = a.object_id and ca.column_id = f.parent_column_id join sys.objects b on b.object_id = f.referenced_object_id join sys.columns cb on cb.object_id = b.object_id and cb.column_id = f.referenced_column_id")
fks = sorted(r[0] for r in cur.fetchall())

snap = '/tmp/claude-0/%s.snap' % db
subprocess.run(['oracle/snap.sh', db, snap], check=True)
if len(sys.argv) > 4 and sys.argv[4] == 'crlf':   # as sqlcmd writes on Windows
    b = open(snap, 'rb').read().replace(b'\n', b'\r\n'); snap += '.crlf'; open(snap, 'wb').write(b)
out = subprocess.run(['build/snapq', snap], input='k\n' + '\n'.join(qs) + '\n', capture_output=True, text=True, check=True).stdout.splitlines()
head, out = out[0], out[1:]
gotfk = sorted(out[:out.index('')]); out = out[out.index('') + 1:]
bad = 0 if gotfk == fks else 1
if bad: print('DIFF foreign keys', len(fks), 'want', len(gotfk), 'got')
for q, (kind, w), got in zip(qs, want, out):
    f = got.split('\t')
    if kind == 'o':
        ok = (w is None and got == '-1') or (w == 'many' and got == '-2') or (isinstance(w, tuple) and f[1:3] == list(w))
    elif kind == 'c':
        ok = (w is None and got == '-1') or (w is not None and f[1] == w)
    else:
        ok = got == w
    if not ok:
        bad += 1
        if bad <= 10: print('DIFF', repr(q), 'want', w, 'got', got)
print('%s: %s; %d lookups, %d differ (collation %s, seed %d)' % (db, head, len(qs), bad, coll, seed))
sys.exit(1 if bad or len(out) != len(qs) else 0)
