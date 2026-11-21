"""How established tools do on the statements the oracle judged: SQL
Server's verdict and referenced columns are the truth; the prototype's
resolver, SSDT's model (DacFx, compare/dacfx) and sqlglot's qualify
(with the snapshot as its schema) are each scored against it.
  python3 compare/three_way.py DB DUMP.jsonl
DUMP files come from DUMP=<file> python3 oracle/gen_oracle.py ..; DB is
the database the statements ran on, as it is now (gen changes with every
seed, so run this right after the oracle run that made the dump)."""
import collections, json, os, subprocess, sys
import pymssql, sqlglot
from sqlglot.optimizer.qualify import qualify

db, dumps = sys.argv[1], sys.argv[2:]
rows = [json.loads(l) for d in dumps for l in open(d)]
dacpac = '/tmp/claude-0/%s.dacpac' % db
subprocess.run(['dotnet', 'compare/dacfx/bin/dacq.dll', 'extract', db, dacpac], check=True)

# SSDT: one batch, every statement a procedure in the model
out = subprocess.run(['dotnet', 'compare/dacfx/bin/dacq.dll', 'refs', dacpac], input='\ngo\n'.join(r['sql'] for r in rows),
                     capture_output=True, text=True, check=True).stdout
dac, cur = {}, None
for l in out.splitlines():
    k, _, v = l.partition(' ')
    if k == 'S': cur = dac.setdefault(int(v), {'err': [], 'cols': set()})
    elif k == 'E': cur['err'].append(v.split(' ', 1)[0])
    elif k == 'C' and v.count('.') == 2: cur['cols'].add(v.lower())   # not CTE columns (dbo.dacq_n.CTE..)

# sqlglot: the snapshot's tables as its schema
schema = collections.defaultdict(dict)
objs = {}
for l in open('/tmp/claude-0/%s.snap' % db, encoding='utf-8'):
    f = l.rstrip('\n').split('\t')
    if f[0] == 'O': objs[f[1]] = (f[2], f[3])
    elif f[0] == 'C' and f[1] in objs: schema[objs[f[1]][0]].setdefault(objs[f[1]][1], {})[f[3]] = f[4]
def glot(sql):
    try:
        qualify(sqlglot.parse_one(sql, read='tsql'), schema=dict(schema), dialect='tsql', validate_qualify_columns=True)
        return 0
    except Exception as e:
        return type(e).__name__

# sqlglot's own expand * and qualify (one pass, as it offers them) on the
# server: does the result compile and describe the same as the original?
con = pymssql.connect('localhost', 'sa', 'Homeros_Test1', db, autocommit=True); cur = con.cursor()
def describe(sql):
    try:
        cur.execute("exec sp_describe_first_result_set N'%s', null, 2" % sql.replace("'", "''"))
        ix = {d[0]: i for i, d in enumerate(cur.description)}
        return [(r[ix['name']], r[ix['system_type_name']], r[ix['source_table']], r[ix['source_column']]) for r in cur.fetchall() if not r[ix['is_hidden']]]
    except pymssql.Error as e:
        return 'error %s' % e.args[0]
def glot_refactor(sql):
    try:
        new = qualify(sqlglot.parse_one(sql, read='tsql'), schema=dict(schema), dialect='tsql', expand_stars=True, qualify_columns=True).sql('tsql')
    except Exception as e:
        return 'refused', type(e).__name__
    a, b = describe(sql), describe(new)
    if a == b: return 'same', ''
    low = lambda d: d if isinstance(d, str) else [tuple((x or '').lower() for x in t) for t in d]
    return ('names recased' if low(a) == low(b) else 'broke'), new

score = collections.Counter()
examples = collections.defaultdict(list)
for i, r in enumerate(rows):
    srv_ok = not r['err']
    for tool, ok, cols in (('ours', not r['ours'], set(r['ocols'])), ('ssdt', not dac[i]['err'], dac[i]['cols']), ('sqlglot', not glot(r['sql']), None)):
        if ok != srv_ok:
            kind = 'false error' if srv_ok else 'missed error'
            score[tool, kind] += 1
            examples[tool, kind].append((r['sql'], r['err'], dac[i]['err'] if tool == 'ssdt' else ''))
        elif srv_ok and cols is not None and r['cols'] is not None and r['sql'].lower().startswith(('select', 'with')):
            if cols != set(r['cols']):
                score[tool, 'wrong columns'] += 1
                examples[tool, 'wrong columns'].append((r['sql'], sorted(set(r['cols']) ^ cols), ''))
            else: score[tool, 'same columns'] += 1
        else: score[tool, 'same verdict'] += 1
for r in rows:
    if not r['err'] and r['sql'].lower().startswith(('select', 'with')):
        res, ex = glot_refactor(r['sql'])
        score['sqlglot refactor', res] += 1
        if res == 'broke': examples['sqlglot refactor', res].append((r['sql'], ex, ''))
print('%d statements (%d valid on the server)' % (len(rows), sum(1 for r in rows if not r['err'])))
for tool in ('ours', 'ssdt', 'sqlglot'):
    print('%-8s %s' % (tool, ', '.join('%s %d' % (k, score[tool, k]) for k in ('same verdict', 'same columns', 'wrong columns', 'false error', 'missed error'))))
print('sqlglot expand * and qualify on valid queries: %s' % ', '.join('%s %d' % (k, score['sqlglot refactor', k]) for k in ('same', 'names recased', 'broke', 'refused')))
if os.environ.get('SHOW'):
    for (tool, kind), ex in sorted(examples.items()):
        for sql, a, b in ex[:int(os.environ['SHOW'])]: print('%s %s: %s | %s\n    %s' % (tool, kind, a, b, sql))
