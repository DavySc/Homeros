# realistic T-SQL: procedures, blocks, IF/WHILE/TRY, DML with clauses, CTEs, CASE, comments
import random, sys
R = random.Random(int(sys.argv[1]))
def c(w):
    r = R.random()
    return w.upper() if r < .45 else w.lower() if r < .85 else w.capitalize()
def sp():
    r = R.random()
    return " " if r < .7 else "\n" + " " * R.randrange(0, 12) if r < .9 else "  " if r < .95 else "\n\n"
def kw(*ws): return sp().join(c(w) for w in ws)
def ident(): return R.choice(["t", "Stock", "dbo.Orders", "[Order Lines]", "o.Id", "x", "@id", "#tmp", "s.Qty", "Name", "Date"])
def expr(d=0):
    r = R.random()
    if d < 2 and r < .15: return kw("case", "when") + " " + cond(d+1) + " " + kw("then") + " " + expr(d+1) + " " + kw("else") + " " + expr(d+1) + " " + kw("end")
    if d < 2 and r < .25: return R.choice(["COUNT(*)", "getdate()", "ISNULL(" + expr(d+1) + "," + expr(d+1) + ")", "(" + sel(d+1) + ")"])
    if r < .5: return ident()
    return R.choice(["1", "N'x'", "'a''b'", "@qty", "NULL", "0x1F", "2.5"])
def cond(d=0):
    op = R.choice(["=", "<>", ">=", "<", "!=", " = ", " like "])
    s = expr(d) + op + expr(d)
    if R.random() < .3: s += sp() + c(R.choice(["and", "or"])) + sp() + cond(d+1) if d < 2 else ""
    if R.random() < .1: s = c("exists") + "(" + sel(d+1) + ")"
    return s
def sel(d=0):
    s = c("select") + sp() + (c("top") + " 10 " if R.random() < .1 else "") + (c("distinct") + " " if R.random() < .1 else "")
    s += ("," + sp() if R.random() < .5 else ", ").join(expr(d) for _ in range(R.randrange(1, 4)))
    if R.random() < .8:
        s += sp() + c("from") + sp() + ident()
        for _ in range(R.randrange(0, 3)):
            s += sp() + kw(*R.choice([["join"], ["inner", "join"], ["left", "join"], ["left", "outer", "join"], ["cross", "apply"], ["outer", "apply"]])) + sp() + ident()
            if R.random() < .7: s += sp() + c("on") + sp() + cond(d)
    if R.random() < .5: s += sp() + c("where") + sp() + cond(d)
    if R.random() < .2: s += sp() + kw("group", "by") + sp() + ident() + sp() + c("having") + sp() + cond(d)
    if R.random() < .2: s += sp() + kw("order", "by") + sp() + ident() + " " + c(R.choice(["asc", "desc"]))
    if d == 0 and R.random() < .1: s += sp() + c("union") + sp() + c("all") + sp() + sel(1)
    return s
def term(): return R.choice([";", "", "", ";\n"])
def stmt(d=0):
    r = R.random()
    if d < 4 and r < .08: return kw("begin") + sp() + "".join(stmt(d+1) + sp() for _ in range(R.randrange(0, 4))) + c("end") + term()
    if d < 4 and r < .16:
        s = c("if") + sp() + cond() + sp() + stmt(d+1)
        if R.random() < .5: s += sp() + c("else") + sp() + stmt(d+1)
        return s
    if d < 4 and r < .2: return c("while") + sp() + cond() + sp() + stmt(d+1)
    if d < 4 and r < .25: return kw("begin", "try") + sp() + stmt(d+1) + sp() + kw("end", "try") + sp() + kw("begin", "catch") + sp() + stmt(d+1) + sp() + kw("end", "catch") + term()
    if r < .4: return sel() + term()
    if r < .48: return kw("insert", "into") + " " + ident() + " (a, b)" + sp() + (c("values") + " (" + expr() + "," + expr() + ")" if R.random() < .5 else sel()) + term()
    if r < .56: return c("update") + " " + ident() + sp() + c("set") + " " + ident() + "=" + expr() + (sp() + c("from") + " " + ident() if R.random() < .3 else "") + (sp() + c("where") + " " + cond() if R.random() < .6 else "") + term()
    if r < .6: return c("delete") + " " + c("from") + " " + ident() + sp() + c("where") + " " + cond() + term()
    if r < .64: return kw("merge", "into") + " t " + c("using") + " s " + c("on") + " " + cond() + sp() + kw("when", "matched", "then") + " " + c("update") + " " + c("set") + " a = 1" + sp() + kw("when", "not", "matched", "then") + " " + c("insert") + " (a) " + c("values") + " (1);"
    if r < .7: return c("declare") + " @x " + c(R.choice(["int", "varchar(10)", "nvarchar(max)"])) + (" = " + expr() if R.random() < .5 else "") + term()
    if r < .76: return c("set") + " " + R.choice(["@x = " + expr(), c("nocount") + " " + c("on"), c("xact_abort") + " " + c("on")]) + term()
    if r < .8: return c("exec") + " " + R.choice(["dbo.usp_x @a = 1, @b = N'x'", "sp_executesql @q"]) + term()
    if r < .84: return R.choice([kw("return") + " 1", kw("commit"), kw("rollback"), kw("begin", "tran"), kw("throw"), c("print") + " 'x'", kw("break"), kw("continue")]) + term()
    if r < .88: return kw("with") + " cte " + c("as") + " (" + sel(1) + ")" + sp() + sel() + term()
    if r < .92: return R.choice(["-- note\n", "/* block */ ", "-- " + c("select") + " x\n"])
    if r < .95: return kw("create", "table") + " #t (a " + c("int") + " " + kw("not", "null") + ", b " + c("varchar") + "(5))" + term()
    return kw("truncate", "table") + " " + ident() + term()
def proc():
    s = kw(*R.choice([["create", "procedure"], ["create", "or", "alter", "procedure"], ["alter", "proc"]])) + " dbo.P @a " + c("int") + ", @b " + c("int") + " = 0" + sp() + c("as") + sp()
    if R.random() < .7: s += c("begin") + sp() + "".join(stmt(1) + sp() for _ in range(R.randrange(1, 8))) + c("end")
    else: s += "".join(stmt(1) + sp() for _ in range(R.randrange(1, 5)))
    return s
out = []
for _ in range(R.randrange(1, 6)):
    out.append(proc() if R.random() < .4 else "".join(stmt() + sp() for _ in range(R.randrange(1, 6))))
    out.append(R.choice(["\nGO\n", "\ngo\n", "\n", "\n\n"]))
sys.stdout.write("".join(out))
