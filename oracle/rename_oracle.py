"""Slice 5, O4: renaming a column across code.  For each column some
module of DB references (dm_sql_referenced_entities), refq writes the
rename script (sp_rename, then an ALTER of each module whose text
changes); it runs but for its COMMIT, then every module that referenced
the column must
  compile again (sp_refreshsqlmodule),
  reference the same columns, with the new name for the renamed one,
  for a view: give the same column names and the same rows.
A refusal is counted with its reason, and a blocker the snapshot names
must really stop sp_rename.
Test server only; nothing is committed.  python3 oracle/rename_oracle.py DB [N]"""
import os, re, subprocess, sys, pymssql
from collections import Counter

db, limit = sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 10**9
con = pymssql.connect('localhost', 'sa', 'Homeros_Test1', db)
cur = con.cursor()
snap = '/tmp/claude-0/%s.snap' % db
subprocess.run(['oracle/snap.sh', db, snap], check=True)
cur.execute("select o.object_id, schema_name(o.schema_id) + '.' + o.name, rtrim(o.type), m.definition, m.uses_native_compilation, m.is_schema_bound from sys.sql_modules m join sys.objects o on o.object_id = m.object_id where o.type in ('V','IF','TF','FN','P','TR') order by 2")
mods = cur.fetchall()

def refs(name):
    cur.execute("select lower(concat(object_schema_name(referenced_id), '.', referenced_entity_name, '.', referenced_minor_name)) from sys.dm_sql_referenced_entities(%s, 'OBJECT') where referenced_minor_id <> 0", name)
    return sorted(r[0] for r in cur.fetchall())
def seen(name, typ):
    got = [refs(name)]
    if typ == 'V':
        cur.execute('select name from sys.dm_exec_describe_first_result_set(%s, null, 0)', 'select * from ' + name)
        got.append([r[0] for r in cur.fetchall()])
        cur.execute('select count_big(*), checksum_agg(binary_checksum(*)) from ' + name)
        got.append(cur.fetchall())
    return got

before = {}
users = {}   # column -> modules the server says reference it
for oid, name, typ, text, native, bound in mods:
    try:
        before[name] = seen(name, typ)
    except pymssql.Error:
        con.rollback(); continue
    for c in before[name][0]: users.setdefault(c, set()).add(name)
cur.execute("select lower(concat(schema_name(o.schema_id), '.', o.name, '.', c.name)), schema_name(o.schema_id), o.name, c.name from sys.columns c join sys.objects o on o.object_id = c.object_id where o.type = 'U' order by 1")
cols = [r for r in cur.fetchall() if r[0] in users and os.environ.get('ONLY', r[0]) == r[0]]
cols = cols[::max(1, len(cols) // limit)]   # N spread over the columns, all by default

# a system-versioned table's rename carries its history table's column
cur.execute("select lower(concat(schema_name(t.schema_id), '.', t.name)), lower(concat(schema_name(h.schema_id), '.', h.name)) from sys.tables t join sys.tables h on h.object_id = t.history_table_id")
hist = dict(cur.fetchall())
cur.execute("select lower(concat(schema_name(schema_id), '.', name)) from sys.tables where is_memory_optimized = 1")
memopt = set(r[0] for r in cur.fetchall())
cur.execute("select schema_name(o.schema_id) + '.' + o.name, lower(concat(object_schema_name(o.parent_object_id), '.', object_name(o.parent_object_id))) from sys.objects o where o.type = 'TR'")
parent = dict(cur.fetchall())
stats = Counter()
bad = 0
for i, (key, sch, tab, col) in enumerate(cols):
    new = col + '_r' if i % 4 else col + ' r]'   # every fourth needs brackets
    why = None
    p = subprocess.run(['build/refq', snap, 'script', '%s.%s.%s' % (sch, tab, col), new], input=b'', capture_output=True)
    script = p.stdout.decode()
    if p.returncode == 3:
        why = 'refused: ' + script.strip()[9:]
        if os.environ.get('WHY'): print(key, why)
    elif p.returncode: why = 'CRASH %s' % p.stderr[-300:].decode()
    altered = set(re.findall(r"(?im)^EXEC \(N'\s*(?:--[^\n]*\n\s*)*ALTER\s+\w+\s+(\S+)", script))
    blocker = re.search(r'\((4928|15336|11408|5074)\)$|history table', why or '')
    if blocker:   # the snapshot's reason: sp_rename must really refuse
        try:
            cur.execute('exec sp_rename %s, %s, %s', ('%s.%s.%s' % (sch, tab, col), new, 'COLUMN'))
            why = 'refused (%s) but sp_rename works' % why
        except pymssql.Error as e:
            if blocker.group(1) and blocker.group(1) != str(e.args[0]): why = 'refused (%s) but sp_rename says %s' % (why, e.args)
        con.rollback()
    if why and why.startswith('refused'):
        stats[why] += 1; continue
    if not why and ('%s.%s' % (sch, tab)).lower() in memopt: stats['memory-optimized table: no sp_rename in a transaction'] += 1; continue
    if not why and any(m[4] and m[1] in [x.replace('[', '').replace(']', '') for x in altered] for m in mods): stats['natively compiled module: no ALTER in a transaction'] += 1; continue
    if not why:
        step = 'the script'
        try:
            cur.execute(script[:script.rindex('COMMIT;')])   # all of it but the COMMIT
            ren = {key: ('%s.%s.%s' % (sch, tab, new)).lower()}
            h = hist.get(('%s.%s' % (sch, tab)).lower())
            if h: ren[h + '.' + col.lower()] = h + '.' + new.lower()
            for name in sorted(users[key]):
                typ = next(m[2] for m in mods if m[1] == name)
                step = 'refresh ' + name
                cur.execute('exec sp_refreshsqlmodule %s', name)
                # a trigger on the table: the server names inserted.c and
                # deleted.c as columns of tables in the trigger's schema
                m = {**ren, **{'%s.%s.%s' % (sch.lower(), x, col.lower()): '%s.%s.%s' % (sch.lower(), x, new.lower()) for x in ('inserted', 'deleted')}} if parent.get(name) == ('%s.%s' % (sch, tab)).lower() else ren
                want = [sorted(m.get(c, c) for c in before[name][0])] + before[name][1:]
                got = seen(name, typ)
                if got != want: why = '%s: %s -> %s' % (name, want, got); break
        except pymssql.Error as e:
            why = 'server error at %s: %s %s' % (step, e.args[0], str(e.args[1] if len(e.args) > 1 else e.args)[:300])
        con.rollback()
    if why:
        bad += 1; print('DIFF', key, '->', new, why)
    else: stats['renamed'] += 1
print('%s: %d columns used by code, %d differ' % (db, len(cols), bad))
for k, v in sorted(stats.items()): print('  %4d %s' % (v, k))
sys.exit(1 if bad else 0)
