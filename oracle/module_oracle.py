"""Slice 2 on real code: every module of a sample database (written by
Microsoft, valid by construction).
  O5  the resolver must report no error SQL Server would give, in any
      module (unsure is allowed: temp tables, DML until slice 4);
  O2  for views and inline functions (pure queries), the columns it
      resolves must equal sys.dm_sql_referenced_entities'.
Test server only.  python3 oracle/module_oracle.py DB"""
import subprocess, sys, pymssql

db = sys.argv[1]
con = pymssql.connect('localhost', 'sa', 'Homeros_Test1', db)
cur = con.cursor()
snap = '/tmp/claude-0/%s.snap' % db
subprocess.run(['oracle/snap.sh', db, snap], check=True)
cur.execute("select o.object_id, schema_name(o.schema_id) + '.' + o.name, rtrim(o.type), m.definition from sys.sql_modules m join sys.objects o on o.object_id = m.object_id where o.type in ('V','IF','TF','FN','P','TR') order by 2")
mods = cur.fetchall()
bad = checked = 0
for oid, name, typ, text in mods:
    out = subprocess.run(['build/resq', snap], input=text, capture_output=True, text=True)
    if out.returncode:
        bad += 1; print('CRASH', name, out.stderr[-500:]); continue
    errs, cols = [], set()
    for l in out.stdout.splitlines():
        f = l.split('\t')
        if f[0] == 'R':
            if int(f[2]) > 1: errs.append('%s %s %s' % (f[2], f[1], f[6]))
            if f[1] == 'column' and f[5] != '-': cols.add(f[5].lower())
        elif f[0] == 'O' and f[3] != '-': cols.add(f[3].lower())
    why = None
    if errs: why = 'O5 resolver errors in valid code: %s' % errs[:5]
    elif typ in ('V', 'IF'):
        checked += 1
        cur.execute("select concat(object_schema_name(referenced_id), '.', referenced_entity_name, '.', referenced_minor_name) from sys.dm_sql_referenced_entities(%s, 'OBJECT') where referenced_minor_id <> 0", name)
        srv = set(r[0].lower() for r in cur.fetchall())
        if srv != cols: why = 'O2 server only %s; resolver only %s' % (sorted(srv - cols), sorted(cols - srv))
    if why:
        bad += 1
        print('DIFF', name, typ, why)
print('%s: %d modules (%d views and inline functions compared), %d differ' % (db, len(mods), checked, bad))
sys.exit(1 if bad else 0)
