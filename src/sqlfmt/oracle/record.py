# Records tests/test_sqlfmt.txt from SQLDiff itself, which test_sqlfmt
# replays: python3 record.py path/to/sqldiff > tests/test_sqlfmt.txt
# (sqldiff built from DavySc/SQLDiff main; its commit goes on the first line).
# Each record is "len(in) len(out)\n", the text, then what
# `sqldiff fmt -d tsql --keyword-case lower` prints for it with the text's
# CR LFs read as LF: Homeros breaks lines with LF (sqlfmt.c newline()).
import os, subprocess, sys
B = sys.argv[1]
O = os.path.dirname(os.path.abspath(__file__))
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
 # a byte the lexer does not know is a problem: its statement is kept
 "SELECT  a \x01  b,c FROM  t",
 # CREATE OR ALTER VIEW: the view's query is laid out
 "CREATE OR ALTER VIEW v AS SELECT a,b FROM t WHERE c=1",
 # an unclosed block comment, string or name is a parse problem: kept as written
 "SELECT  a,b\n/* open",
 "SELECT  a,b\nSELECT 'it''s",
 "SELECT  a,b\nSELECT [a]]",
 # doubled quotes inside strings and names close nothing
 "SELECT  'it''s',[a]]b] ,\"c\"\"d\",c FROM  t",
 # GO before a -- comment; blank lines after GO
 "SELECT 1\nGO -- c\nSELECT  2",
 "GO\n\n\n\nSELECT  1",
 # ELSE ends an IF branch on one line
 "IF 1=1 SELECT 1 ELSE SELECT 2",
 # a sqlcmd line ends at its line break
 ":setvar a b\nSELECT  1,2",
 # a NUL byte is no operator: a problem, its statement kept
 "SELECT  1\0SELECT  2\n",
 # a label named like a keyword keeps its case
 "SELECT  1\nBEGIN: SELECT 2\nGOTO BEGIN",
 # Davy's layout (2026-11-19): a ( alone on its line hangs at the clause;
 # AND, OR and ON end where WHERE, HAVING or the join's first word ends
 "DECLARE @rdbms_type rdbms_type = 0 --SQL Server\n\nINSERT INTO #prog_object\n(\nprog_object_id,\nprog_object_type_id,\nreport_id\n)\nSELECT\n'rpt_ds' + rds.report_id,\ncoalesce(r.generation_order_no, 100),\n0\nFROM report r\nJOIN report_excel_data_source rds\nON rds.model_id = r.model_id\nAND rds.branch_id = r.branch_id\nWHERE r.model_id = @model_id\nAND r.branch_id = @branch_id\nOR r.report_type = 'EXCEL_TEMPLATE'",
 "insert into #t\n(\na,\nb\n)\nselect x,\ncoalesce(\ny, 1)\nfrom t\nleft join u\non u.a = t.a\nand u.b = t.b\nwhere t.c = 1\nand t.e in\n(\n1, 2\n)\ngroup by x\nhaving count(*) > 1\nand max(y) < 2",
 "WITH L AS\n(\nSELECT a FROM t\nWHERE b = 1\nAND c = 2\n)\nSELECT * FROM L\nWHERE (a = 1\nOR a = 2)\n-- before\nAND a > 0",
 # nesting: 20 levels laid out; 1,000 is SQLDiff's limit, kept as written
 "BEGIN\n" * 20 + "SELECT  1\n" + "END\n" * 20,
 "BEGIN\n" * 1000 + "SELECT  1\n" + "END\n" * 1000,
]
# generated texts as Homeros holds them: a CR LF file's CRs are not in the text
cases += [subprocess.run(["python3", "-I", os.path.join(O, g), str(i)], capture_output=True, check=True).stdout.decode("utf-8", "surrogateescape").replace("\r\n", "\n")
          for g, n in (("tsql.py", 150), ("soup.py", 300)) for i in range(1, n + 1)]
commit = subprocess.run(["git", "-C", os.path.dirname(os.path.realpath(B)), "rev-parse", "--short", "HEAD"], capture_output=True, text=True).stdout.strip()
w = sys.stdout.buffer
w.write(b"sqldiff %s, %d cases\n" % (commit.encode(), len(cases)))
for c in cases:
    i = c.encode("utf-8", "surrogateescape")
    o = subprocess.run([B, "fmt", "-", "--stdin-filepath", "x.sql", "-d", "tsql", "--keyword-case", "lower"],
                       input=i.replace(b"\r\n", b"\n"), capture_output=True).stdout
    w.write(b"%d %d\n" % (len(i), len(o)) + i + o)
