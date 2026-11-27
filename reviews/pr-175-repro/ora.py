"""PR 175 review: a wider oracle than tests/test_sql_vars.py.
  python3 ora.py DRIVER VARS N SEED
DRIVER is build/san/test_sql (its "run -t" mode), VARS the vars probe.
Random batches from statement forms the PR's generator lacks; the server
(SET NOEXEC ON) names what each lacks; sql_vars must agree.  Batches with a
syntax error (level 15) are counted; type errors (level 16, from the
sql_variant stand-ins) come after every Msg 137 and are kept and shown with -v."""
import random, re, subprocess, sys

def stmt(rnd):
    v = lambda: rnd.choice(['@a', '@b', '@B', '@c', '@d'])
    t = lambda: rnd.choice(['@t', '@T', '@u'])
    return rnd.choice([
        lambda: 'select %s' % v(),
        lambda: 'declare %s int' % v(),
        lambda: 'declare %s int = 1, %s nvarchar(9)' % (v(), v()),
        lambda: 'declare %s int = case when 1 = 1 then 1 else 2 end, %s int' % (v(), v()),
        lambda: 'declare %s int = case when %s = 1 then 1 end, %s int' % (v(), v(), v()),
        lambda: 'declare %s int = (select case when 1 = 1 then 1 else 2 end), %s int' % (v(), v()),
        lambda: 'declare %s as decimal(10, 2) = 1.5, %s varchar(max) = %s' % (v(), v(), v()),
        lambda: 'DECLARE %s table (x int, y int)' % t(),
        lambda: 'declare %s int, %s table (x int primary key, y as x + 1)' % (v(), t()),
        lambda: 'declare %s cursor' % v(),
        lambda: 'declare cc cursor for select %s' % v(),
        lambda: 'declare cd cursor local fast_forward for select 1 where 1 = %s open cd fetch next from cd into %s, %s close cd deallocate cd' % (v(), v(), v()),
        lambda: 'select * from %s' % t(),
        lambda: 'insert %s (x) values (%s)' % (t(), v()),
        lambda: 'insert into %s select 1, %s' % (t(), v()),
        lambda: 'update %s set x = %s' % (t(), v()),
        lambda: 'delete %s output deleted.x into %s where x = %s' % (t(), t(), v()),
        lambda: "exec sp_executesql N'select @p', N'@p int', @p = %s" % v(),
        lambda: "exec sp_executesql @stmt = N'select @p', @params = N'@p int', @p = %s" % v(),
        lambda: "execute sp_executesql %s, N'@p int out', @p = %s output" % (v(), v()),
        lambda: 'exec %s = sp_who' % v(),
        lambda: 'exec %s = sp_who @loginame = %s' % (v(), v()),
        lambda: 'EXECUTE sys.sp_who %s' % v(),
        lambda: 'exec (%s)' % v(),
        lambda: "exec ('select ' + %s)" % v(),
        lambda: 'exec %s' % v(),
        lambda: 'exec %s @x = %s' % (v(), v()),
        lambda: "exec sp_who @loginame = 'sa' select %s = 1" % v(),
        lambda: "exec sp_who 'sa' print %s" % v(),
        lambda: 'insert into %s exec sp_who @loginame = %s' % (t(), v()),
        lambda: 'if 1 = 1 exec sp_who @loginame = %s else select %s' % (v(), v()),
        lambda: 'begin try exec sp_who @loginame = %s end try begin catch select %s end catch' % (v(), v()),
        lambda: 'set %s = 1' % v(),
        lambda: 'set %s += %s' % (v(), v()),
        lambda: 'select %s = 2' % v(),
        lambda: 'select %s = name from sys.objects' % v(),
        lambda: '-- %s' % v(),
        lambda: '/* %s /* %s */ %s */ select 1' % (v(), v(), v()),
        lambda: "select '%s'" % v(),
        lambda: "select 'it''s %s', N'%s'" % (v(), v()),
        lambda: 'select 1 [%s]' % v(),
        lambda: 'select 1 as "%s"' % v(),
        lambda: 'select 1 as [a]]%s]' % v(),
        lambda: 'select @@rowcount, @@error',
        lambda: 'select @@spid + %s' % v(),
        lambda: 'print %s;' % v(),
        lambda: 'if %s > 1 select 1' % v(),
        lambda: 'if %s > 1 begin select %s end else begin select %s end' % (v(), v(), v()),
        lambda: 'while %s < 3 begin set %s += 1 end' % (v(), v()),
        lambda: 'select (select %s), %s' % (v(), v()),
        lambda: 'select top (%s) name from sys.objects select name from sys.objects order by name offset %s rows' % (v(), v()),
        lambda: 'select case when %s = 1 then %s else %s end, %s' % (v(), v(), v(), v()),
        lambda: ';with c as (select %s x) select * from c where x = %s' % (v(), v()),
        lambda: "raiserror(%s, 16, 1)" % v(),
        lambda: ';throw 50000, %s, 1;' % v(),
        lambda: 'waitfor delay %s' % v(),
        lambda: 'begin tran %s commit tran %s' % (v(), v()),
        lambda: 'select %s.value(\'(/a)[1]\', \'int\')' % v(),
        lambda: 'select iif(%s = 1, %s, 0)' % (v(), v()),
        lambda: (lambda k: 'create table #x%d (x int default 0) insert #x%d values (%s)' % (k, k, v()))(rnd.randint(0, 999999)),
        lambda: 'select 1 where 1 in (%s,%s)' % (v(), v()),
        lambda: 'select(%s)' % v(),
        lambda: 'select 1+%s-%s' % (v(), v()),
        lambda: 'select name from sys.objects where name like %s + \'%%\'' % v(),
        lambda: 'merge %s as d using (select %s x) s on d.x = s.x when matched then update set x = %s;' % (t(), v(), v()),
        lambda: 'return',
        lambda: 'l%d: select %s' % (rnd.randint(0, 999999), v()),
        lambda: 'declare %s int set %s = 1' % (v(), v()),
        lambda: 'declare\n\t%s int\n\t, %s int' % (v(), v()),
        lambda: 'declare %s xml = \'<a/>\', %s sysname' % (v(), v()),
        lambda: 'exec sp_who; select %s = 1' % v(),
        lambda: 'execute as user = %s' % v(),
        lambda: 'exec sp_who @loginame = default',
        lambda: 'select 1 union all select %s' % v(),
        lambda: 'use master',
        # second pass: CASE nested, in blocks, and before a DECLARE
        lambda: 'declare %s int = case when 1 = 1 then case when 2 = 2 then 1 else 3 end else 2 end, %s int' % (v(), v()),
        lambda: 'begin declare %s int = case 1 when 1 then 2 end select %s end' % (v(), v()),
        lambda: 'if 1 = 1 begin select case when %s = 1 then 1 end end else select %s' % (v(), v()),
        lambda: 'select case when 1 = 1 then 1 end declare %s int, %s int' % (v(), v()),
        lambda: 'begin try select case when %s = 1 then 1 else 2 end end try begin catch declare %s int, %s int end catch' % (v(), v(), v()),
        lambda: 'declare %s int = (select case when 1 = 1 then 1 else 2 end) + case 1 when 1 then 1 end, %s int exec sp_who @loginame = %s' % (v(), v(), v()),
        lambda: 'kill %s' % v() if False else 'select %s' % v(),
    ])()

def drv(sql):
    p = subprocess.run([sys.argv[1], 'run', '-t'], input=sql.encode(), capture_output=True, timeout=60)
    return (p.stdout + p.stderr).decode()

def lacks(s):
    got = []
    while True:
        decl = ''.join('declare %s %s; ' % (v, 'table (x int, y int)' if t == 'table' else 'sql_variant') for v, t in got)
        o = drv('set noexec on; ' + decl + s)
        if 'has already been declared' in o: return None, o
        m = re.search(r'Must declare the (scalar|table) variable "(@\w+)"', o)
        if not m: return [v for v, t in got], o
        if len(got) > 12: return None, o
        got.append((m.group(2), m.group(1)))

def ours(s):
    b = s.encode()
    p = subprocess.run([sys.argv[2], '-m'], input=b'#batch %d\n%s\n' % (len(b), b), capture_output=True)
    return p.stdout.decode().split()[1:]

rnd = random.Random(int(sys.argv[4]))
kept = diff = other = 0
for i in range(int(sys.argv[3])):
    s = rnd.choice(['\n', ' ', ';\n']).join(stmt(rnd) for _ in range(rnd.randint(1, 4)))
    v, o = lacks(s)
    if v is None: continue
    if re.search(r'Msg \d+, Level 15', o):
        other += 1
        if '-v' in sys.argv: print('OTHER', repr(s), o[:300])
        continue
    kept += 1
    g = ours(s)
    if sorted(x.lower() for x in g) != sorted(x.lower() for x in v):
        diff += 1
        print('DIFF\n%s\n  server: %s\n  sql_vars: %s' % (s, ' '.join(v), ' '.join(g)))
print('kept %d, differ %d, refused for another reason %d' % (kept, diff, other))
