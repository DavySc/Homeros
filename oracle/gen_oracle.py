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
import json, os, random, re, subprocess, sys, pymssql

FILE = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].isdigit() else None   # statements to check, on WWI
seed = int(sys.argv[1]) if len(sys.argv) > 1 and not FILE else 1
N = int(sys.argv[2]) if len(sys.argv) > 2 else 300
rnd = random.Random(seed)
SA = ('localhost', 'sa', 'Homeros_Test1')

COLL = sys.argv[3] if len(sys.argv) > 3 else 'Latin1_General_CI_AS'
DB = os.environ.get('DB', 'WWI' if FILE else 'gen')   # another name lets two runs share the server
m = pymssql.connect(*SA, 'master', autocommit=True); mk = m.cursor()
if not FILE:
    mk.execute("if db_id('%s') is not null begin alter database %s set single_user with rollback immediate; drop database %s end" % (DB, DB, DB))
    mk.execute("create database %s collate %s" % (DB, COLL))
con = pymssql.connect(*SA, DB, autocommit=True); cur = con.cursor()
if not FILE: cur.execute("create schema s")
POOL = ['a', 'b', 'c', 'd', 'id', 'Name']
tables = {}
for i in range(1, 6 if not FILE else 1):
    cols = sorted(rnd.sample(POOL, rnd.randint(2, 4)), key=POOL.index)
    sch = 's' if i == 5 else 'dbo'
    tables['t%d' % i] = (sch, cols)
    cur.execute('create table %s.t%d (%s)' % (sch, i, ', '.join('[%s] int' % c for c in cols)))
    # rows, so O3 can compare results: few values, so joins match
    cur.execute('insert %s.t%d values %s' % (sch, i, ', '.join('(%s)' % ', '.join(str(rnd.randint(0, 3)) for _ in cols) for _ in range(6))))

snap = os.environ.get('SNAPS', '/tmp/claude-0') + '/%s.snap' % DB
subprocess.run(['oracle/snap.sh', DB, snap], check=True)

DML = float(os.environ.get('DML', '.35'))   # the share of DML statements and batches
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
    def expr(self, srcs, window=True):
        e = self.colref(srcs)
        t = rnd.choice(['%s'] * 6 + ['cast(%s as int)', 'convert(int, %s)', 'datediff(day, 0, %s)', 'coalesce(%s, 0)',
            'case when %s = 1 then 0 else 1 end'] + ['row_number() over (partition by %s order by (select null))'] * window)
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

def table():
    t = rnd.choice(sorted(tables)); sch, cols = tables[t]
    return (q(sch) + '.' if sch != 'dbo' or rnd.random() < .3 else '') + q(t), t, cols

def dml():
    """slice 4: UPDATE, DELETE, INSERT, MERGE, and batches over a temp
    table or table variable, with the same kinds of names as queries"""
    g = Gen(0, [])
    name, t, cols = table()
    kind = rnd.choice(['update', 'update', 'delete', 'insert', 'merge', 'temp'])
    wcol = lambda: q(rnd.choice(cols)) if rnd.random() > .03 else q('zz')
    if kind in ('update', 'delete'):
        srcs, frm = [], ''
        tgt = name
        if rnd.random() < .5:   # a FROM: the target by its alias, by its table name, or not in it
            fsql, fsrc = g.source(0, [], False, [])
            r = rnd.random()
            if r < .4: fsql, fsrc = '%s x0' % name, ('x0', cols); tgt = 'x0'
            elif r < .6: fsql, fsrc = name, (t, cols)
            else: srcs.append((t, cols))
            frm = ' from ' + fsql
            srcs.append(fsrc)
            if rnd.random() < .5:
                jsql, jsrc = g.source(1, srcs, False, [])
                frm += ' join %s on %s = %s' % (jsql, g.colref(srcs + [jsrc], .02), g.colref([jsrc], .02)); srcs.append(jsrc)
        else: srcs.append((t, cols))
        if kind == 'update':
            sets = ', '.join('%s = %s' % (wcol(), g.expr(srcs, window=False)[0]) for _ in range(rnd.randint(1, 2)))
            head = 'update %s set %s' % (tgt, sets)
        else: head = 'delete %s%s' % ('from ' if rnd.random() < .5 and not frm else '', tgt)
        out = ' output %s.%s' % ('inserted' if kind == 'update' and rnd.random() < .5 else 'deleted', wcol()) if rnd.random() < .2 else ''
        where = ' where %s = %s' % (g.colref(srcs), g.colref(srcs)) if rnd.random() < .6 else ''
        return head + out + frm + where
    if kind == 'insert':
        k = rnd.randint(1, len(cols))
        lst = ', '.join(wcol() for _ in range(k))
        if rnd.random() < .4: body = 'values (%s)' % ', '.join(rnd.choice(['1', g.colref([], .3)]) for _ in range(k))
        else: body = Gen(0, []).select([], star_ok=False, nitems=k)[0]
        return 'insert %s%s (%s) %s' % ('into ' if rnd.random() < .5 else '', name, lst, body)
    if kind == 'merge':
        sname, st, scols = table()
        T, S = ('t', cols), ('s', scols)
        ref = lambda srcs: g.colref(srcs, .03)
        whens = []
        if rnd.random() < .7: whens.append('when matched%s then update set %s = %s' % (' and %s = 1' % ref([T, S]) if rnd.random() < .3 else '', wcol(), ref([T, S])))
        cond = lambda side: ' and %s = 1' % ref(side if rnd.random() < .8 else [T, S]) if rnd.random() < .3 else ''   # sometimes the side it cannot see: 5334
        if rnd.random() < .5: whens.append('when not matched%s then insert (%s) values (%s)' % (cond([S]), wcol(), ref([S])))
        if rnd.random() < .4: whens.append('when not matched by source%s then %s' % (cond([T]), rnd.choice(['delete', 'update set %s = %s' % (wcol(), ref([T]))])))
        if not whens: whens.append('when matched then delete')
        return 'merge %s %st using %s s on %s = %s %s;' % (name, 'as ' if rnd.random() < .5 else '', sname, ref([T]), ref([S]), ' '.join(whens))
    # a temp table or table variable, made then used
    tn = rnd.choice(['#t', '@t'])
    k = rnd.randint(1, 3)
    tcols = ['k%d' % j for j in range(k)]
    if tn == '#t' and rnd.random() < .4:
        sql, outs = Gen(0, []).select([], star_ok=False, named=True, nitems=k)
        sql = sql.replace(' from ', ' into #t from ', 1); tcols = outs
    else:
        sql = '%s %s %s(%s)' % ('create table' if tn == '#t' else 'declare', tn, 'table ' if tn == '@t' else '', ', '.join('%s int' % c for c in tcols))
        sql += '; insert %s (%s) values (%s)' % (tn, ', '.join(tcols), ', '.join('1' for _ in tcols))
    al = rnd.choice(['', ' x9'])
    use = Gen(0, [((al.strip() or (tn if tn == '#t' else None)), tcols)])
    cols_used = ', '.join(use.colref([], .05) for _ in range(rnd.randint(1, 2)))
    return sql + '; select %s from %s%s' % (cols_used, tn, al)

def statement():
    if rnd.random() < DML: return dml()
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
        # name resolution); describing it binds it: its error is the answer.
        # Describe cannot bind a temp table (11525): running it can, in a
        # transaction rolled back
        if e.args[0] != 11525: return e.args[0], None, None
        try: cur.execute('begin tran; exec dbo.p; rollback')
        except pymssql.Error as e2:
            cur.execute('if @@trancount > 0 rollback')
            return e2.args[0], None, None
        desc = None
    try:
        # a written column (SET, INSERT's list) also as name!
        cur.execute("select concat(object_schema_name(referenced_id), '.', referenced_entity_name, '.', referenced_minor_name), is_updated from sys.dm_sql_referenced_entities('dbo.p', 'OBJECT') where referenced_minor_id <> 0")
        cols = sorted(set(x for r in cur.fetchall() for x in [r[0].lower()] + [r[0].lower() + '!'] * r[1]))
    except pymssql.Error as e:   # 2020: the server could not bind every column itself; no O2 for this one
        cols = None
    return 0, desc, cols

def rows(sql):
    """O3: every result set's rows, sorted, or the error number.  Run as a
    procedure in a transaction rolled back: its temp tables end with it
    and its writes are undone"""
    try:
        cur.execute('create or alter procedure dbo.r as ' + sql)
        cur.execute('begin tran; exec dbo.r')
        sets = []
        while True:
            try: sets.append(sorted(cur.fetchall(), key=repr))
            except pymssql.Error: pass
            if not cur.nextset(): break
        for t, (sch, cols) in sorted(tables.items()):   # and what it wrote
            cur.execute('select * from %s.%s' % (sch, t)); sets.append(sorted(cur.fetchall(), key=repr))
        cur.execute('rollback')
        return sets
    except pymssql.Error as e:
        cur.execute('if @@trancount > 0 rollback')
        return 'error %s' % e.args[0]

def refactor(op, sql, sdesc, scols, args=()):
    """a refactor of a valid statement: refused, or the server sees the same
    statement (O5 valid, O1 same columns, O2 same references, O3 same rows)"""
    p = subprocess.run(['build/refq', snap, op, *args], input=sql, capture_output=True, text=True)
    want = expect.get(op)
    if p.returncode == 3:
        if os.environ.get('SHOW'): print('REFUSED', op, *args, p.stdout.strip(), sql)
        if want and not p.stdout.startswith(want): return 'broke', 'refused (%s), expected %s' % (p.stdout.strip(), want)
        return 'n/a' if p.stdout.startswith('REFUSED: no ') else 'refused', None
    if p.returncode: return 'crash', p.stderr[-300:]
    new = p.stdout
    if want and want != new: return 'broke', 'expected %s, got %s' % (want, new)
    err, ndesc, ncols = server(new)
    if err: return 'broke', 'server %s on: %s' % (err, new)
    if ndesc != sdesc: return 'broke', 'O1 %s -> %s: %s' % (sdesc, ndesc, new)
    if scols is not None and ncols is not None and ncols != scols: return 'broke', 'O2 %s -> %s: %s' % (scols, ncols, new)
    if new == sql: return 'same', None
    # O3 only where rows are a function of the data (and running it changes
    # nothing), and on gen's small tables: WWI's take minutes to fetch
    if not FILE and not any(w in sql.lower() for w in ('next value for', 'tablesample', 'newid(', 'rand(')) and rows(sql) != rows(new):
        return 'broke', 'O3 rows differ: %s' % new
    return 'ok', None

def is_query(sql):
    """a SELECT, with or without WITH, not WITH .. UPDATE and the like"""
    return re.match(r'\s*(select|with)\b', sql, re.I) and not re.search(r'\)\s*(update|delete|insert|merge)\b', sql, re.I)

def ours(sql):
    out = subprocess.run(['build/resq', snap], input=sql, capture_output=True, text=True).stdout.splitlines()
    errs, cols, desc, top = set(), set(), [], None
    names.clear()
    for l in out:
        f = l.split('\t')
        if f[0] == 'R' and f[1] in ('alias', 'cte-def'): names.append((int(f[3]), f[6]))
        if f[0] == 'Q' and top is None: top = f[1]
        if f[0] == 'R':
            st = int(f[2])
            if st not in (0, 1): errs.add(st)
            if f[1] == 'column' and f[5] != '-': cols.add(f[5].lower()); f[7] == '1' and cols.add(f[5].lower() + '!')
            if f[1] == 'star' and st == 1: desc = None   # a * over unknown columns: no outputs to compare
        elif f[0] == 'O' and desc is not None:
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

names = []   # the statement's aliases and CTE names: (byte, name), for R1
stats = {}
bad = valid = 0
# DUMP=<file>: every statement with both verdicts, one JSON line each, for
# the comparison with other tools (compare/three_way.py)
dump = open(os.environ['DUMP'], 'w') if os.environ.get('DUMP') else None
stmts = [x.strip() for x in open(FILE).read().split('\ngo\n') if x.strip()] if FILE else None
# a case's comment lines: "-- star: <text>" is what star must give (or
# REFUSED), likewise qualify; other comment lines say why the case is there
cases = [([l for l in x.split('\n') if l.startswith('--')], '\n'.join(l for l in x.split('\n') if not l.startswith('--'))) for x in stmts] if FILE else None
if FILE: N = len(stmts)
for n in range(N):
    expect = dict(l[3:].split(': ', 1) for l in cases[n][0] if l[3:].split(':')[0] in ('star', 'qualify')) if FILE else {}
    sql = cases[n][1] if FILE else statement()
    err, sdesc, scols = server(sql)
    oerr, ocols, odesc = ours(sql)
    why = None
    if bool(err) != bool(oerr) or (err and err not in oerr): why = 'O5 server %s, resolver %s' % (err or 'ok', sorted(oerr) or 'ok')
    elif not err and not is_query(sql):
        valid += 1   # DML and batches: the verdict and O2 (columns read, columns written)
        # the server lists no columns of a statement on a temp table (it is
        # bound when it runs), and marks no column written through a CTE or a
        # derived table (KB 05)
        if scols is not None and not any(c.endswith('!') for c in scols): ocols = [c for c in ocols if not c.endswith('!')]
        # nor MERGE's INSERT column list (read or written)
        if scols is not None and sql.lower().startswith('merge'):
            ins = {n.strip(' []').lower() for m in re.findall(r'(?i)then\s+insert\s*\(([^)]*)\)', sql) for n in m.split(',')}
            ocols = [c for c in ocols if c in scols or c.rstrip('!').split('.')[-1] not in ins]
        if scols is not None and scols != ocols and '#' not in sql: why = 'O2 server %s resolver %s' % (scols, ocols)
    elif not err:
        valid += 1
        # the server keeps no column dependencies for FOR SYSTEM_TIME, and
        # FOR XML/JSON makes one column of the result (KB 05)
        if scols is not None and scols != ocols and 'for system_time' not in sql: why = 'O2 server %s resolver %s' % (scols, ocols)
        elif odesc is not None and sdesc is not None and not o1_same(sql, sdesc, odesc) and not any(w in sql for w in (' for json', ' for xml', ' for system_time')):
            why = 'O1 server %s resolver %s' % (sdesc, odesc)
    if dump: dump.write(json.dumps({'sql': sql, 'err': err, 'cols': scols, 'ours': sorted(oerr), 'ocols': ocols}) + '\n')
    if not why and not err:
        for op in ('star', 'qualify'):
            res, detail = refactor(op, sql, sdesc, scols)
            stats[op + ' ' + res] = stats.get(op + ' ' + res, 0) + 1
            if detail: why = '%s %s: %s' % (op, res, detail); break
        # R1: each alias or CTE name to a fresh name, and to another one the
        # statement has (a capture the meaning check must refuse or allow)
        for k, (at, nm) in enumerate(names[:4] if not why else ()):
            other = [m for _, m in names if m.lower() != nm.lower()]
            for to in ['zz%d' % k] + other[:1]:
                res, detail = refactor('alias', sql, sdesc, scols, (str(at), to))
                key = 'alias %s %s' % ('fresh' if to.startswith('zz') else 'taken', res)
                stats[key] = stats.get(key, 0) + 1
                if detail: why = 'alias %s at %d -> %s: %s' % (res, at, to, detail); break
            if why: break
        # R9: each CTE inlined, each derived table made a CTE
        for at, nm in names if not why else ():
            for op in ('inline', 'extract'):
                res, detail = refactor(op, sql, sdesc, scols, (str(at),))
                if res != 'n/a': stats[op + ' ' + res] = stats.get(op + ' ' + res, 0) + 1
                if detail: why = '%s %s at %d: %s' % (op, res, at, detail); break
            if why: break
    # "-- info <word>: <line>": info on the first <word> gives <line> first
    for l in cases[n][0] if FILE else ():
        if l.startswith('-- info ') and not why:
            word, want = l[8:].split(': ', 1)
            p = subprocess.run(['build/refq', snap, 'info', str(len(sql.encode()[:sql.encode().index(word.encode())]))], input=sql, capture_output=True, text=True)
            if p.stdout.split('\n')[0] != want: why = 'info %s: expected %s, got %s' % (word, want, p.stdout + p.stderr[-300:])
    if why:
        bad += 1
        if bad <= 8: print('DIFF', why, '\n    ', sql)
cur.execute('drop procedure if exists dbo.p; drop procedure if exists dbo.r')   # the sample database stays as restored
print('%s: %d statements (%d valid), %d differ%s' % (FILE or 'gen seed %d' % seed, N, valid, bad, ''.join('; %s %d' % kv for kv in sorted(stats.items()))))
sys.exit(1 if bad else 0)
