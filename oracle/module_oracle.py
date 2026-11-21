"""Slice 2 on real code: every module of a sample database (written by
Microsoft, valid by construction).
  O5  the resolver must report no error SQL Server would give, in any
      module (unsure is allowed: temp tables, DML until slice 4);
  O2  for views and inline functions (pure queries), the columns it
      resolves must equal sys.dm_sql_referenced_entities'.
Slice 3: star and qualify on every module; each result is ALTERed in a
transaction that is rolled back, and must compile (O5), reference the
same columns (O2) and, for a view, give the same columns (O1) and rows
(O3, as a checksum).
Test server only.  python3 oracle/module_oracle.py DB"""
import os, re, subprocess, sys, pymssql

db = sys.argv[1]
con = pymssql.connect('localhost', 'sa', 'Homeros_Test1', db)
cur = con.cursor()
snap = os.environ.get('SNAPS', '/tmp/claude-0') + '/%s.snap' % db
subprocess.run(['oracle/snap.sh', db, snap], check=True)
cur.execute("select o.object_id, schema_name(o.schema_id) + '.' + o.name, rtrim(o.type), m.definition from sys.sql_modules m join sys.objects o on o.object_id = m.object_id where o.type in ('V','IF','TF','FN','P','TR') order by 2")
mods = cur.fetchall()
cur.execute("select lower(concat(schema_name(schema_id), '.', name)) from sys.table_types")
types = set(r[0] for r in cur.fetchall())
def seen(name, typ):
    """what the server says about a module: its referenced columns, and
    for a view its result columns and a checksum of its rows"""
    cur.execute("select concat(object_schema_name(referenced_id), '.', referenced_entity_name, '.', referenced_minor_name) from sys.dm_sql_referenced_entities(%s, 'OBJECT') where referenced_minor_id <> 0", name)
    got = [sorted(r[0].lower() for r in cur.fetchall())]
    if typ == 'V':
        cur.execute('select name, system_type_name, source_schema, source_table, source_column from sys.dm_exec_describe_first_result_set(%s, null, 1)', 'select * from ' + name)
        got.append(cur.fetchall())
        cur.execute('select count_big(*), checksum_agg(binary_checksum(*)) from ' + name)
        got.append(cur.fetchall())
    return got

stats = {}
def refactored(name, typ, text):
    """None, or why a refactor of the module broke it"""
    # slice 6: every alias and CTE name renamed, every CTE inlined, every
    # derived table made a CTE (positions from the resolver)
    out = subprocess.run(['build/resq', snap], input=text.encode(), capture_output=True).stdout.decode('utf-8', 'replace').splitlines()
    at = [f.split('\t')[3] for f in out if f.split('\t')[:2] in (['R', 'alias'], ['R', 'cte-def'])]
    for op, *args in [('star',), ('qualify',)] + [(o, a, *n) for a in at for o, *n in (('alias', 'zz_r'), ('inline',), ('extract',))]:
        p = subprocess.run(['build/refq', snap, op, *args], input=text.encode(), capture_output=True)
        res = {0: 'ok', 3: 'refused'}.get(p.returncode, 'crash')
        if res == 'refused' and p.stdout.startswith(b'REFUSED: no '): continue
        if res == 'ok' and p.stdout == text.encode(): res = 'same'
        stats[op + ' ' + res] = stats.get(op + ' ' + res, 0) + 1
        if res == 'crash': return '%s crash %s' % (op, p.stderr[-300:].decode())
        if res != 'ok': continue
        before = seen(name, typ)
        try:
            cur.execute(re.sub(r'(?i)\bcreate\b', 'alter', p.stdout.decode(), count=1))
            after = seen(name, typ)
        except pymssql.Error as e:
            after = 'error %s' % (e.args,)
        con.rollback()
        if after != before and ('natively compiled' in str(after) or '(3729,' in str(after)):
            # no ALTER of a natively compiled procedure in a transaction
            # (12331), nor of a function a security policy uses (3729):
            # compile a copy under another name, then drop it
            copy = name + '_refq'
            con.autocommit(True)
            try:
                cur.execute(re.sub(r'(?i)\bcreate(\s+(?:procedure|function)\s+)[\w.\[\]]+', lambda m: 'create' + m.group(1) + copy, p.stdout.decode(), count=1))
                after = seen(copy, typ)
            except pymssql.Error as e:
                after = 'error %s' % (e.args,)
            cur.execute('drop %s if exists %s' % ('procedure' if typ == 'P' else 'function', copy))
            con.autocommit(False)
        if after != before: return '%s broke it: %s -> %s' % (op, before, after)
    return None

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
    else: why = refactored(name, typ, text)
    if why: pass
    else:
        checked += 1
        # a trigger's inserted.c and deleted.c the server lists as columns of
        # tables named inserted and deleted: they are its table's (KB 05)
        cur.execute("select concat(coalesce(object_schema_name(referenced_id), object_schema_name(parent_object_id), referenced_schema_name), '.', case when referenced_entity_name in ('inserted', 'deleted') and parent_object_id is not null then object_name(parent_object_id) else referenced_entity_name end, '.', referenced_minor_name) from sys.dm_sql_referenced_entities(%s, 'OBJECT') r cross join (select parent_object_id from sys.objects where object_id = object_id(%s)) t where referenced_minor_id <> 0 or referenced_entity_name in ('inserted', 'deleted') and referenced_minor_name is not null", (name, name))
        srv = set(r[0].lower() for r in cur.fetchall())
        # allowances (KB 05): the server lists nothing a temp table's statement
        # uses (it binds them when they run), and CONTAINSTABLE(t, *) as the
        # full-text columns, which the snapshot does not hold; it lists no
        # column of a table-type parameter (measured: 3 WWI modules)
        if '#' in text: cols &= srv
        cols = set(c for c in cols if c.rsplit('.', 1)[0] not in types)   # nor a table-type parameter's columns
        if re.search(r'(?i)containstable\s*\([^,]*,\s*\*', text): srv &= cols
        if srv != cols: why = 'O2 server only %s; resolver only %s' % (sorted(srv - cols), sorted(cols - srv))
    if why:
        bad += 1
        print('DIFF', name, typ, why)
print('%s: %d modules (%d compared with the server references), %d differ%s' % (db, len(mods), checked, bad, ''.join('; %s %d' % kv for kv in sorted(stats.items()))))
sys.exit(1 if bad else 0)
