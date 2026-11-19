"""Slice 2 stress test: random queries over a random schema, each checked
against SQL Server (KB 05-oracles.md):
  O5  valid or not: CREATE PROCEDURE binds every name (Msg 207, 209, 4104,
      208 ..); the resolver must find an error exactly when the server does
      (and the server's error number among its own);
  O1  for a valid one, sp_describe_first_result_set (browse mode 2) gives
      each output column's name and source column: the resolver's must match;
  O2  sys.dm_sql_referenced_entities on the procedure: the set of columns
      the resolver resolved must equal the server's.
Test server only (database gen, rebuilt each run).

python3 oracle/gen_oracle.py [SEED] [N] [COLLATION]"""
import random, subprocess, sys, pymssql

FILE = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].isdigit() else None   # statements to check, on WWI
seed = int(sys.argv[1]) if len(sys.argv) > 1 and not FILE else 1
N = int(sys.argv[2]) if len(sys.argv) > 2 else 300
rnd = random.Random(seed)
SA = ('localhost', 'sa', 'Homeros_Test1')

COLL = sys.argv[3] if len(sys.argv) > 3 else 'Latin1_General_CI_AS'
DB = 'WWI' if FILE else 'gen'
m = pymssql.connect(*SA, 'master', autocommit=True); mk = m.cursor()
if not FILE:
    mk.execute("if db_id('gen') is not null begin alter database gen set single_user with rollback immediate; drop database gen end")
    mk.execute("create database gen collate " + COLL)
con = pymssql.connect(*SA, DB, autocommit=True); cur = con.cursor()
if not FILE: cur.execute("create schema s")
POOL = ['a', 'b', 'c', 'd', 'id', 'Name']
tables = {}
for i in range(1, 6 if not FILE else 1):
    cols = sorted(rnd.sample(POOL, rnd.randint(2, 4)), key=POOL.index)
    sch = 's' if i == 5 else 'dbo'
    tables['t%d' % i] = (sch, cols)
    cur.execute('create table %s.t%d (%s)' % (sch, i, ', '.join('[%s] int' % c for c in cols)))

snap = '/tmp/claude-0/%s.snap' % DB
subprocess.run(['oracle/snap.sh', DB, snap], check=True)

UPPER = .3 if '_CI' in COLL else .01   # under a _CS_ collation most changes are errors
def case(s): return ''.join(ch.upper() if rnd.random() < UPPER else ch for ch in s)
def q(s): return '[%s]' % s if rnd.random() < .2 else case(s)

class Gen:
    """a query block: sources (exposed name, columns) and what it outputs"""
    def __init__(self, depth, outer):
        self.depth, self.outer = depth, outer
    def colref(self, srcs, wrong=.04):
        # a column of a visible source, sometimes unqualified, sometimes wrong
        vis = srcs + self.outer
        if not vis or rnd.random() < wrong: return q(rnd.choice(['zz', 'a', 'x.a', 'b']))
        name, cols = rnd.choice(vis)
        if not cols: return q(name) + '.' + q('a')
        c = rnd.choice(cols)
        return q(c) if rnd.random() < .45 or name is None else q(name) + '.' + q(c)
    def source(self, k, srcs, lateral, ctes):
        r = rnd.random()
        alias = 'x%d' % k
        if r < .15 and self.depth < 2:
            inner = Gen(self.depth + 1, (srcs if lateral else []) + self.outer)
            sql, outs = inner.select(ctes, star_ok=False, named=True)
            return '(%s) %s' % (sql, alias), (alias, outs)
        if r < .25 and ctes:
            n, outs = rnd.choice(ctes)
            if rnd.random() < .5: return '%s %s' % (q(n), alias), (alias, outs)
            return q(n), (n, outs)
        t = rnd.choice(sorted(tables)); sch, cols = tables[t]
        name = (q(sch) + '.' if sch != 'dbo' or rnd.random() < .3 else '') + q(t)
        if rnd.random() < .6: return '%s %s%s' % (name, 'as ' if rnd.random() < .3 else '', alias), (alias, cols)
        return name, (t, cols)
    def expr(self, srcs):
        e = self.colref(srcs)
        t = rnd.choice(['%s'] * 6 + ['cast(%s as int)', 'convert(int, %s)', 'datediff(day, 0, %s)', 'coalesce(%s, 0)',
            'case when %s = 1 then 0 else 1 end', 'row_number() over (partition by %s order by (select null))'])
        return t % e, t == '%s'
    def select(self, ctes, star_ok=True, named=False, nitems=None):
        srcs, parts = [], []
        for k in range(rnd.randint(1, 3)):
            lateral = k > 0 and rnd.random() < .2
            sql, s = self.source(k, srcs, lateral, ctes)
            if k == 0: parts.append(sql)
            elif lateral: parts.append(rnd.choice(['cross apply', 'outer apply']) + ' (select * from (values (1)) v(z) where 1 = 0 or %s = 1) ap%d' % (self.colref(srcs + [s], 0), k)); s = None
            elif rnd.random() < .3: parts.append(', ' + sql)
            else:
                on_srcs = srcs + [s] if rnd.random() < .97 else srcs + [s] + [('x9', ['a'])]
                parts.append('%s join %s on %s = %s' % (rnd.choice(['', 'left', 'inner']), sql, self.colref(on_srcs, .02), self.colref([s], .02)))
            if s: srcs.append(s)
        items, outs = [], []
        for j in range(nitems or rnd.randint(1, 4)):
            r = rnd.random() if not nitems else 1
            if r < .12 and star_ok:
                items.append('*'); outs += [c for _, cs in srcs for c in cs]
            elif r < .2 and star_ok and srcs:
                n, cs = rnd.choice(srcs); items.append(q(n) + '.*'); outs += cs
            elif r < .3 and self.depth < 2:
                inner = Gen(self.depth + 1, srcs + self.outer)
                sql, _ = inner.select(ctes, star_ok=False)
                al = 'sq%d' % j; items.append('%s = (select top 1 %s)' % (al, sql[len('select '):] if False else 'count(*) from (' + sql + ') z')); outs.append(al)
            else:
                e, plain = self.expr(srcs)
                if rnd.random() < .4 or named or not plain:
                    al = 'o%d' % j if rnd.random() < .7 or not plain else e.split('.')[-1].strip('[]')
                    items.append('%s%s%s' % (e, ' as ' if rnd.random() < .5 else ' ', al)); outs.append(al)
                else:
                    items.append(e); outs.append(e.split('.')[-1].strip('[]'))
        sql = 'select ' + ', '.join(items) + ' from ' + ' '.join(parts)
        if rnd.random() < .5: sql += ' where %s = %s' % (self.colref(srcs), self.colref(srcs))
        return sql, outs

def statement():
    ctes, pre = [], ''
    if rnd.random() < .3:
        defs = []
        for i in range(rnd.randint(1, 2)):
            g = Gen(1, [])
            sql, outs = g.select(ctes, star_ok=False, named=True)
            n = 'c%d' % i
            if rnd.random() < .3:
                cols = ['k%d' % j for j in range(len(outs))]
                defs.append('%s(%s) as (%s)' % (n, ', '.join(cols), sql)); outs = cols
            else: defs.append('%s as (%s)' % (n, sql))
            ctes.append((n, outs))
        pre = 'with ' + ', '.join(defs) + ' '
    g = Gen(0, [])
    if rnd.random() < .15:   # a set operator: same column count, ORDER BY sees the first part's names
        k = rnd.randint(1, 3)
        sql, outs = g.select(ctes, nitems=k)
        sql += ' %s ' % rnd.choice(['union', 'union all', 'except', 'intersect']) + Gen(0, []).select(ctes, nitems=k)[0]
    else:
        sql, outs = g.select(ctes)
    if rnd.random() < .25 and outs: sql += ' order by ' + q(rnd.choice(outs))
    return pre + sql

def server(sql):
    """(error number or 0, describe rows, referenced columns)"""
    try:
        cur.execute('create or alter procedure dbo.p as ' + sql)
    except pymssql.Error as e:
        return e.args[0], None, None
    try:
        cur.execute("exec sp_describe_first_result_set N'%s', null, 2" % sql.replace("'", "''"))
        ix = {d[0]: i for i, d in enumerate(cur.description)}
        desc = [(r[ix['name']], '%s.%s.%s' % (r[ix['source_schema']], r[ix['source_table']], r[ix['source_column']]) if r[ix['source_column']] else '-')
                for r in cur.fetchall() if not r[ix['is_hidden']]]
    except pymssql.Error as e:
        # CREATE PROCEDURE defers a statement naming a missing table (deferred
        # name resolution); describing it binds it: its error is the answer
        return e.args[0], None, None
    try:
        cur.execute("select concat(object_schema_name(referenced_id), '.', referenced_entity_name, '.', referenced_minor_name) from sys.dm_sql_referenced_entities('dbo.p', 'OBJECT') where referenced_minor_id <> 0")
        cols = sorted(set(r[0].lower() for r in cur.fetchall()))
    except pymssql.Error as e:   # 2020: the server could not bind every column itself; no O2 for this one
        cols = None
    return 0, desc, cols

def ours(sql):
    out = subprocess.run(['build/resq', snap], input=sql, capture_output=True, text=True).stdout.splitlines()
    errs, cols, desc, top = set(), set(), [], None
    for l in out:
        f = l.split('\t')
        if f[0] == 'Q' and top is None: top = f[1]
        if f[0] == 'R':
            st = int(f[2])
            if st not in (0, 1): errs.add(st)
            if f[1] == 'column' and f[5] != '-': cols.add(f[5].lower())
        elif f[0] == 'O':
            if f[3] != '-': cols.add(f[3].lower())
            if f[1] == top: desc.append((f[2] if f[2] != '-' else None, f[3]))
    return errs, sorted(cols), desc

def o1_same(sql, sdesc, odesc):
    """names equal; sources equal where both have one.  Two server habits
    are allowed (KB 05): CAST/CONVERT of a column keeps its source there,
    and a set operator's columns have none there"""
    if len(sdesc) != len(odesc): return False
    for (sn, ss), (on, os) in zip(sdesc, odesc):
        if (sn or '').lower() != (on or '').lower().strip('[]'): return False
        if ss != '-' and os != '-' and ss.lower() != os.lower(): return False
        if ss != '-' and os == '-' and 'convert(' not in sql and 'cast(' not in sql: return False
        if ss == '-' and os != '-' and not any(w in sql for w in (' union ', ' except ', ' intersect ')): return False
    return True

bad = valid = 0
stmts = [x.strip() for x in open(FILE).read().split('\ngo\n') if x.strip()] if FILE else None
stmts = [x.split('\n', 1)[1] if x.startswith('--') else x for x in stmts] if FILE else None
if FILE: N = len(stmts)
for n in range(N):
    sql = stmts[n] if FILE else statement()
    err, sdesc, scols = server(sql)
    oerr, ocols, odesc = ours(sql)
    why = None
    if bool(err) != bool(oerr) or (err and err not in oerr): why = 'O5 server %s, resolver %s' % (err or 'ok', sorted(oerr) or 'ok')
    elif not err and not sql.lower().startswith(('select', 'with')):
        valid += 1   # DML: the verdict only, until slice 4
    elif not err:
        valid += 1
        # the server keeps no column dependencies for FOR SYSTEM_TIME, and
        # FOR XML/JSON makes one column of the result (KB 05)
        if scols is not None and scols != ocols and 'for system_time' not in sql: why = 'O2 server %s resolver %s' % (scols, ocols)
        elif not o1_same(sql, sdesc, odesc) and not any(w in sql for w in (' for json', ' for xml', ' for system_time')):
            why = 'O1 server %s resolver %s' % (sdesc, odesc)
    if why:
        bad += 1
        if bad <= 8: print('DIFF', why, '\n    ', sql)
cur.execute('drop procedure if exists dbo.p')   # the sample database stays as restored
print('%s: %d statements (%d valid), %d differ' % (FILE or 'gen seed %d' % seed, N, valid, bad))
sys.exit(1 if bad else 0)
