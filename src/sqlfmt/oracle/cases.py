# Records tests/test_sqlfmt.c's cases from SQLDiff itself: python3 cases.py path/to/sqldiff
import subprocess, sys, json
B = sys.argv[1]
cases = [
 # clause breaks, spaces after commas and around comparisons, keyword case
 "SELECT a,b FROM t WHERE a=1 AND b<>2 ORDER BY a",
 # joins and APPLY, GROUP BY / HAVING
 "select o.Id, COUNT(*) from dbo.Orders o inner join Lines l on l.OrderId=o.Id left outer join X x on 1=1 cross apply f(o.Id) group by o.Id having COUNT(*)>1",
 # statements without terminators, inferred boundaries; non-reserved words keep their case
 "SET NOCOUNT ON\nDECLARE @x int = 5\nUPDATE t SET a = @x WHERE b = 1\nDELETE FROM t WHERE a = 2\nINSERT INTO t (a, b) SELECT a, b FROM s\nEXEC dbo.P @a = 1",
 # blocks, IF / ELSE IF / ELSE, WHILE
 "IF @a = 1\nBEGIN\nSELECT 1\nEND\nELSE IF @a = 2\nSELECT 2\nELSE\nBEGIN\nWHILE @i < 10\nSET @i += 1\nEND",
 # TRY / CATCH, THROW, transactions
 "BEGIN TRY\nBEGIN TRAN\nUPDATE t SET a = 1\nCOMMIT\nEND TRY\nBEGIN CATCH\nROLLBACK;\nTHROW;\nEND CATCH",
 # a procedure whose body is one block, and one that is a list
 "create procedure dbo.P @a int, @b int = 0 as\nbegin\nset nocount on;\nselect @a\nend\nGO\nCREATE PROC dbo.Q AS\nSELECT 1\nSELECT 2\nGO",
 # comments: on their own line, after code, before END; GO with a count
 "BEGIN\n-- first\nSELECT 1 -- after\n/* block */ SELECT 2\n-- before end\nEND\nGO 2\n",
 # CASE is not broken; nested parentheses indent
 "SELECT CASE WHEN a = 1 THEN 'x' ELSE 'y' END AS c,\n(SELECT MAX(b)\nFROM s WHERE s.a = t.a) AS m FROM t",
 # *= stays glued; - sign after =; strings side by side keep their gap
 "UPDATE t SET a*=2, b=-1, c='a'\n'b' WHERE x=y",
 # sqlcmd lines and names next to dots or labels keep their case
 ":setvar  DB  Test\nSELECT t.Select, x.FROM FROM t\nlbl: SELECT 1\nGOTO lbl",
 # a parse problem keeps its statement as written
 "SELECT 1\nBEGIN\nSELECT 2\nSELECT 'unclosed",
 # CRLF line endings, extra blank lines, leading blank lines, trailing spaces
 "\r\n\r\nSELECT a FROM t   \r\n\r\n\r\n\r\nSELECT b FROM u\r\n",
 # WITH ... and MERGE clauses, OUTPUT, VALUES
 "WITH c AS (SELECT a FROM t) SELECT * FROM c\nMERGE INTO t USING s ON t.a = s.a WHEN MATCHED THEN UPDATE SET b = 1 WHEN NOT MATCHED THEN INSERT (a) VALUES (s.a) OUTPUT inserted.a;",
 # a statement after `;` on the same line that is GO: kept as written by the check
 "select 1; go\nselect 2",
 # a statement the check fails is kept as written, its keywords too; the rest is laid out
 "SELECT  a,b\nBEGIN go\nEND",
 # ALTER and CREATE VIEW, a function with RETURNS ... BEGIN
 "CREATE VIEW v AS SELECT a FROM t WHERE b = 1\nGO\nCREATE FUNCTION f(@a int) RETURNS int BEGIN RETURN @a END",
]
out = []
for c in cases:
    r = subprocess.run([B, "fmt", "-", "--stdin-filepath", "x.tsql.sql", "-d", "tsql", "--keyword-case", "lower"], input=c.encode(), capture_output=True)
    out.append((c, r.stdout.decode(), r.stderr.decode()))
def cstr(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\r', '\\r').replace('\n', '\\n"\n\t  "') + '"'
for c, o, e in out:
    print("\t{ " + cstr(c) + ",\n\t  " + cstr(o) + " },")
    if e: print("\t/* sqldiff: " + e.strip().replace("*/","* /") + " */")
