import subprocess, os, sys
P = [
 ("results window does not follow the tail", "if (w->buf == b && w->line >= last) w->line", "if (0 && w->buf == b && w->line >= last) w->line"),
 ("login messages shown in the results", "if (was >= DB_READY) put(s, o->s, o->n);", "if (1) put(s, o->s, o->n);"),
 ("the last dir line wins, not the longest", "n > best && !strncmp", "n > 0 && !strncmp"),
 ("dir /a/ms also holds /a/msx", "&& (path[n] == '/' || d[n - 1] == '/')) best", "&& 1) best"),
 ("another query's results window is not closed", "if (k && (core_win_goto(E, 'w', k)", "if (0 && (core_win_goto(E, 'w', k)"),
 ("Alt+Break during login sends a cancel instead of dropping", "if (db_state(&s->db) != DB_BUSY || db_cancel(&s->db)) {", "if (db_cancel(&s->db)) {"),
 ("Postgres cancel connection not watched", "if (k && !loop_watch(k->fd, wake, s))", "if (0 && !loop_watch(k->fd, wake, s))"),
 ("v selection starts at its line's start", "(k->visual == 'v' ? c1 : 0)", "0"),
 ("a second F5 during a run is not refused", 'if (s->q.n) { core_msg(E, "sql: a query is running', 'if (0) { core_msg(E, "sql: a query is running'),
 ("batches after a cancelled one still run", "\t\ts->q.n = 0;\n\t\tif (db_state", "\t\tif (db_state"),
 ("Postgres default port wrong", "o->pg ? 5432 : 1433", "o->pg ? 5433 : 1433"),
 ("SPC r from the results leaves the cursor elsewhere", "long q = in ? shown(s->nr) : 0;", "long q = 0;"),
 ("results not emptied before a run", "\ttext_delete(r->text, 0, text_len(r->text));", "\t"),
 ("~ in a dir line not expanded", "a[1][0] == '~' && h ? h : \"\"", "\"\""),
 ("~ kept after HOME", "a[1] + (a[1][0] == '~' && h)", "a[1]"),
 ("GO 0 runs once", "while (s->i < s->q.n && s->left <= 0)", "while (s->i < s->q.n && s->left < 0)"),
 ("a dead connection keeps its watch (no drop)", "\t\tputz(s, db_net(&s->db)->err), putz(s, \"\\n\");\n\t\tdrop(s);\n\t\tcore_changed = 1;", "\t\tputz(s, db_net(&s->db)->err), putz(s, \"\\n\");\n\t\tcore_changed = 1;"),
 ("results buffer left modifiable with numbers", "E->buf->nonumber = E->buf->nomodifiable = 1;", "E->buf->nonumber = 1;"),
 ("F5 taken in insert mode", "|| e->mode != MODE_NORMAL) return 0;", ") return 0;"),
 ("V selection runs to the character only", "z = k->visual == 'v' ?", "z = 1 ?"),
]
src = open("src/sql/sql.c").read()
env = dict(os.environ, SQL_SA_PASSWORD="Homeros_Test1", SQL_PG_PASSWORD="pgpw")
for name, old, new in P:
    if src.count(old) != 1: print("BROKEN", name, src.count(old)); continue
    open("src/sql/sql.c", "w").write(src.replace(old, new))
    b = subprocess.run(["/tmp/rv/mk.sh", ".", "tests/test_sql_editor.c", "/tmp/rv/t_plant"], capture_output=True, text=True)
    if b.returncode: print("BUILD FAIL", name, b.stderr[-300:]); continue
    try:
        r = subprocess.run(["/tmp/rv/t_plant"], capture_output=True, text=True, env=env, timeout=200)
        print("caught    " if r.returncode else "NOT CAUGHT", name)
    except subprocess.TimeoutExpired: print("caught (hang)", name)
open("src/sql/sql.c", "w").write(src)
