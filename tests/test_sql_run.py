"""Package 22's GO splitter against sqlcmd 18 (KB chapter 13): random
scripts of print markers, GO lines of every kind sqlcmd was measured on,
comments, strings across lines, errors and CRLF.  Two things are left
out: scripts where sqlcmd itself failed ("SqlState 24000, Invalid cursor
state", seen after an error in a repeated batch), and GO inside a block
comment that holds another block comment: sqlcmd ends a comment at the
first */, SQL Server nests them, and the splitter follows the server.

  python3 tests/test_sql_run.py DRIVER N SEED > tests/test_sql_run.txt
runs each script through sqlcmd in the local test container (docker,
container mssql: src/sql/README.md) and records the batches sqlcmd sent,
as the server saw them (an Extended Events session on sql_batch_starting),
and where sqlcmd refused a GO line.  DRIVER is test_sql's driver, which
reads the events back.  tests/test_sql_run.c replays the file through
sql_split."""
import os, random, re, subprocess, sys, time

GO = ['GO', 'go', '  Go  ', 'GO -- c', 'GO--c', 'GO 2', 'GO\t2', 'GO 0', 'GO;', 'GO/*x*/', 'GO x', 'GO 2 x',
      'GO 2;', 'GO -1', 'GO 2--c', 'GO 2 --c', 'GO 2 /*c*/', 'GO/', "GO'x'", 'GO(1)', 'GO 02', '[GO]', 'GOX',
      'G O', 'GO 1 2', 'GO2', 'GO,', 'GO.', 'GO@', 'GO#', 'GO$', 'GO!', 'GO=', 'GO[', 'GO"', 'GO-', 'GO 2-',
      'GO -', '/*x*/ GO', '/*x*/GO', 'GO :', 'GO\\', 'GO 2\t', 'GO\t--c', 'GO 2 -- c ;', 'GO_', 'GO*', 'GO+',
      'GO<', 'GO)', 'GO{', 'GO%', 'GO&', 'GO|', 'GO^', 'GO~', 'GO`', 'GO 1e2', 'GO 0x2', '-- GO', '  ', '',
      'GO 3', 'gO', 'GO  ']
OK = ['GO', 'go', '  Go  ', 'GO -- c', 'GO--c', 'GO 2', 'GO\t2', 'GO 0', 'GO 02', 'GO 3', '/*x*/ GO', 'GO 2--c', 'GO\t--c', 'GO  ', 'gO']
def go(rnd): return rnd.choice(GO if rnd.random() < .25 else OK)

def script(rnd):
    lines = []
    for _ in range(rnd.randint(2, 10)):
        r = rnd.random()
        n = len(lines) + 1
        if r < .35: lines.append("print 'L%d'" % n)
        elif r < .65: lines.append(go(rnd))
        elif r < .70: lines += ['/* a', rnd.choice([g for g in GO if '/*' not in g]), '*/ ' + rnd.choice(['', "print 'c%d'" % n, 'GO'])]
        elif r < .75: lines += ["print 'x", go(rnd), "y'"]
        elif r < .80: lines.append("raiserror('e%d', 16, 1)" % n)
        elif r < .84: lines.append('selct %d' % n)
        elif r < .88: lines.append('declare @v int = %d' % n)
        elif r < .92: lines.append('print @v')
        elif r < .96: lines.append("print 'a' print 'b' GO")
        else: lines.append("/* GO */ print 'm%d'" % n)
    eol = '\r\n' if rnd.random() < .2 else '\n'
    return eol.join(lines) + (eol if rnd.random() < .7 else '')

def drv(sql):
    p = subprocess.run([sys.argv[1], 'run', '-t'], input=sql.encode(), capture_output=True, timeout=60, check=True)
    return p.stdout.decode()

def unesc(s): return re.sub(r'\\(.)', lambda m: {'n': '\n', 't': '\t', 'r': '\r'}.get(m.group(1), m.group(1)), s)

XE = """select x.value('(data[@name="batch_text"]/value)[1]', 'nvarchar(max)') b from (select cast(target_data as xml) t
from sys.dm_xe_session_targets st join sys.dm_xe_sessions s on s.address = st.event_session_address where s.name = 'gocap') a
cross apply t.nodes('RingBufferTarget/event') n(x)"""

def sqlcmd(s, i):
    drv("if exists (select * from sys.server_event_sessions where name = 'gocap') drop event session gocap on server; "
        "create event session gocap on server add event sqlserver.sql_batch_starting(where sqlserver.client_app_name = N'SQLCMD') "
        "add target package0.ring_buffer with (max_dispatch_latency = 1 seconds); alter event session gocap on server state = start")
    path = '/tmp/claude-sqlrun-%d.sql' % i
    open(path, 'w', newline='').write(s)
    subprocess.run(['docker', 'cp', path, 'mssql:/tmp/x.sql'], check=True, capture_output=True)
    os.unlink(path)
    p = subprocess.run(['docker', 'exec', 'mssql', '/opt/mssql-tools18/bin/sqlcmd', '-C', '-S', 'localhost', '-U', 'sa',
                        '-P', os.environ['SQL_SA_PASSWORD'], '-i', '/tmp/x.sql'], capture_output=True, timeout=60)
    out = (p.stdout + p.stderr).decode()
    if 'SqlState' in out: return None, 0   # sqlcmd's own failure (24000 "Invalid cursor state" after some errors): not a split
    m = re.search(r"Syntax error at line (\d+) near command 'GO'", out)
    time.sleep(1.3)   # the session's dispatch latency
    rows = drv(XE).split('\n')[1:-2]   # the header, then "(n rows affected)"
    return [unesc(b) for b in rows if b not in ('SET QUOTED_IDENTIFIER OFF', 'SET TEXTSIZE 4096')], int(m.group(1)) if m else 0

rnd = random.Random(int(sys.argv[3]))
for i in range(int(sys.argv[2])):
    s = script(rnd)
    bs, bad = sqlcmd(s, i)
    if bs is None: continue
    out = '#script %d\n%s\n#batches %d %d\n' % (len(s.encode()), s, len(bs), bad)
    for b in bs: out += '#b %d\n%s\n' % (len(b.encode()), b)
    sys.stdout.write(out)
