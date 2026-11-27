import subprocess, os, sys
P = [
 ("login messages shown in the results", "if (was >= DB_READY) put(s, o->s, o->n);", "if (1) put(s, o->s, o->n);"),
 ("the last dir line wins, not the longest", "n > best && !strncmp", "n > 0 && !strncmp"),
 ("dir /a/ms also holds /a/msx", "&& (sep(path[n]) || sep(d[n - 1]))) best", "&& 1) best"),
 ("another query's results window is not closed", "if (k && (core_win_goto(E, 'w', k)", "if (0 && (core_win_goto(E, 'w', k)"),
 ("Alt+Break during login sends a cancel instead of dropping", "if (db_state(&s->db) != DB_BUSY || db_cancel(&s->db)) drop(s);", "if (db_cancel(&s->db)) drop(s);"),
 ("Postgres cancel connection not watched", "if (k && !loop_watch(k->fd, wake, s))", "if (0 && !loop_watch(k->fd, wake, s))"),
 ("a second F5 during a run is not refused", "\tif (s->q.n) { core_msg(E, s->cancel ?", "\tif (s->cancel) { core_msg(E, s->cancel ?"),
 ("F5 during a cancel is not refused", "\tif (s->q.n) { core_msg(E, s->cancel ?", "\tif (s->q.n && !s->cancel) { core_msg(E, s->cancel ?"),
 ("the run goes on after the cancel is answered", "if (s->cancel) s->cancel = 0, s->q.n = 0;", "if (s->cancel) s->cancel = 0, next(s);"),
 ("the cancel flag kept after the answer", "if (s->cancel) s->cancel = 0, s->q.n = 0;", "if (s->cancel) s->q.n = 0;"),
 ("a second Alt+Break sends a second cancel", "if (!s || !s->q.n || s->cancel) return s != NULL;", "if (!s || !s->q.n) return s != NULL;"),
 ("Postgres default port wrong", "o->pg ? 5432 : 1433", "o->pg ? 5433 : 1433"),
 ("SPC r from the query leaves the cursor in another window", "return in ? 0 : core_win_goto(e, 'p', 1);", "return 0;"),
 ("results not emptied before a run", '\tcore_buf_fill(E, core_buf_find(E, s->res), "", 0, 0);', "\t"),
 ("~ in a dir line not expanded", "int home = *fs == '~' && h;", "int home = 0;"),
 ("GO 0 runs once", "while (s->i < s->q.n && s->left <= 0)", "while (s->i < s->q.n && s->left < 0)"),
 ("a dead connection is not dropped", "\t\tputz(s, db_net(&s->db)->err), putz(s, \"\\n\");\n\t\tdrop(s);\n\t\tcore_changed = 1;", "\t\tputz(s, db_net(&s->db)->err), putz(s, \"\\n\");\n\t\tcore_changed = 1;"),
 ("results buffer left modifiable", "E->buf->nonumber = E->buf->nomodifiable = 1;", "E->buf->nonumber = 1;"),
 ("the run reads from the wrong offset of its copy", "s->text.s + (b.a - s->base)", "s->text.s + b.a"),
 ("no room for the split keeps the run", "if (oom || show(s)) { s->q.n = 0; return -1; }", "if (oom || show(s)) { return -1; }"),
 ("F5 in the results window is not taken", "Buf *qb = s ? core_buf_find(e, s->nr) : e->buf;", "Buf *qb = e->buf;"),
 ("a selection in another buffer used for the query", "if (e->ks.visual && qb == e->buf)", "if (e->ks.visual)"),
 ("a missing pass= sends an empty password", "if (!o->pg && !*o->pass && strcmp(o->user, \"-\")) {", "if (0) {"),
 ("a port with trailing letters accepted", "|| (p && *p)) {", ") {"),
 (":Disconnect when not connected says disconnected", "if (!s || s->fd < 0) {", "if (!s) {"),
 (":Connect keeps the old connection open", "\tdrop(s);\n\ts->spec = c;", "\ts->spec = c;"),
 ("Completion time printed after every batch", "\t\t\ts->q.n = 0;\n\t\t}\n\t\treturn;", "\t\t}\n\t\treturn;"),
 ("every key looks the session up", "if (key != KEY_F5 && key != KEY_BREAK) return 0;", "if (key < KEY_F5) return 0;"),
]
src = open("src/sql/sql.c").read()
env = dict(os.environ, SQL_SA_PASSWORD="Homeros_Test1", SQL_PG_PASSWORD="pgpw")
for name, old, new in P:
    if src.count(old) != 1: print("BROKEN", name, src.count(old)); continue
    open("src/sql/sql.c", "w").write(src.replace(old, new))
    b = subprocess.run(["/tmp/rv/mk.sh", ".", "tests/test_sql_editor.c", "/tmp/rv/t_plant2"], capture_output=True, text=True)
    if b.returncode: print("BUILD FAIL", name, b.stderr[-300:]); continue
    try:
        r = subprocess.run(["/tmp/rv/t_plant2"], capture_output=True, text=True, env=env, timeout=200)
        print("caught    " if r.returncode else "NOT CAUGHT", name)
    except subprocess.TimeoutExpired: print("caught (hang)", name)
open("src/sql/sql.c", "w").write(src)
