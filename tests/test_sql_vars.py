"""Package 22's undeclared @variables (sql_vars) against SQL Server:
random batches of uses, DECLAREs (lists, tables, values, CASE and
parentheses in them), EXEC with parameter names and a return status, SET
and SELECT assignments, @@x, and @names in strings, comments and [names].

  python3 tests/test_sql_vars.py DRIVER N SEED > tests/test_sql_vars.txt
asks the server which variables each batch lacks: it runs the batch
under SET NOEXEC (compiled, not run), and while the server answers
"Must declare the scalar (or table) variable", declares that one before
it and asks again.  DRIVER is test_sql's driver.  tests/test_sql_vars.c
replays the file through sql_vars.  Left out: CREATE PROCEDURE and the
like, where sql_vars asks for nothing (the parameters are declared, and
an undeclared one is the CREATE's error to show), and batches that use a
variable before its DECLARE or declare one twice ("already been
declared"), where it asks for nothing either: the batch is wrong as
typed, and the server's error says so."""
import random, re, subprocess, sys

def stmt(rnd):
    v = lambda: rnd.choice(['@a', '@b', '@B', '@c'])
    return rnd.choice([
        lambda: 'select %s' % v(),
        lambda: 'select name from sys.objects where name = %s and type = %s' % (v(), v()),
        lambda: 'declare %s int' % v(),
        lambda: 'declare %s int = 1, %s nvarchar(9)' % (v(), v()),
        lambda: 'DECLARE %s table (x int, y int)' % rnd.choice(['@t', '@T']),
        lambda: 'declare %s int = case when 1 = 1 then 1 else 2 end, %s int' % (v(), v()),
        lambda: 'declare %s decimal(10, 2) = 1.5, %s int' % (v(), v()),
        lambda: 'declare %s int = (select 1), %s int' % (v(), v()),
        lambda: 'select * from @t',
        lambda: "exec sp_executesql N'select @p', N'@p int', @p = %s" % v(),
        lambda: 'exec %s = sp_who' % v(),
        lambda: 'EXECUTE sys.sp_who %s' % v(),
        lambda: 'set %s = 1' % v(),
        lambda: 'select %s = 2' % v(),
        lambda: '-- %s' % v(),
        lambda: '/* %s */ select 1' % v(),
        lambda: "select '%s'" % v(),
        lambda: 'select 1 [%s]' % v(),
        lambda: 'select @@rowcount',
        lambda: 'print %s;' % v(),
        lambda: 'if %s > 1 select 1' % v(),
        lambda: 'select (select %s), %s' % (v(), v()),
    ])()

def drv(sql):
    p = subprocess.run([sys.argv[1], 'run', '-t'], input=sql.encode(), capture_output=True, timeout=60)
    return (p.stdout + p.stderr).decode()

def lacks(s):
    got = []
    while True:
        decl = ''.join('declare %s %s; ' % (v, 'table (x int)' if t == 'table' else 'sql_variant') for v, t in got)
        o = drv('set noexec on; ' + decl + s)
        if 'has already been declared' in o: return None
        m = re.search(r'Must declare the (scalar|table) variable "(@\w+)"', o)
        if not m: return [v for v, t in got]
        got.append((m.group(2), m.group(1)))

rnd = random.Random(int(sys.argv[3]))
for i in range(int(sys.argv[2])):
    s = rnd.choice(['\n', ' ', ';\n']).join(stmt(rnd) for _ in range(rnd.randint(1, 5)))
    v = lacks(s)
    if v is not None: sys.stdout.write('#batch %d\n%s\n#vars %s\n' % (len(s.encode()), s, ' '.join(v)))
