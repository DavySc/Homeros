/* SQL buffers (sql.h): SSMS's query window over the editor's buffers.  One
   connection per query buffer, opened at its first F5 and kept (so temp
   tables and an open transaction last, as in SSMS); its batches run one
   after another, everything the server says appended to the results
   buffer as it arrives, as make.c's buffer grows. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "sql.h"
#include "core.h"
#include "file.h"
#include "loop.h"
#include "match.h"
#include "db.h"
#include "run.h"

/* A connection as the connections file names it */
typedef struct { char name[256], host[256], user[256], pass[256], db[256]; int pg, port, trust; } Spec;

typedef struct {
	int nr, res;            /* the query buffer, its results buffer (0 none yet) */
	Spec spec;              /* the connection, spec.name[0] 0 until one is found */
	Db db;
	int fd, kill;           /* what the loop watches: db_net's and Postgres's cancel, -1 none */
	SqlBatches q;           /* the run: its batches, the one running, its runs left */
	size_t i;
	long left;
	Str sql;                /* the running batch's text */
	long long t0;
} Sess;

static Editor *E;
static Sess **S;
static size_t nS;

static Sess *sess(int nr, int make)
{
	for (size_t i = 0; i < nS; i++) if (S[i]->nr == nr || S[i]->res == nr) return S[i];
	Sess **s = make ? realloc(S, (nS + 1) * sizeof *S) : NULL;
	if (!s || !(s[nS] = calloc(1, sizeof **s))) { S = s ? s : S; return NULL; }
	S = s;
	*S[nS] = (Sess){ .nr = nr, .fd = -1, .kill = -1 };
	return S[nS++];
}

static int is_sql(const Buf *b)
{
	size_t n = b->path ? strlen(b->path) : 0;
	return n > 4 && !strcmp(b->path + n - 4, ".sql");
}

/* The window showing buffer nr, as its number in window order, 0 none */
static long shown(int nr)
{
	long k = 1;
	for (Win *w = E->wins; w; w = w->next, k++) if (w->buf->nr == nr) return k;
	return 0;
}

/* n bytes at the end of the results; a window whose cursor was on the last
   line follows (make.c's put) */
static void put(Sess *s, const char *p, size_t n)
{
	Buf *b = s->res ? core_buf_find(E, s->res) : NULL;
	if (!b || !n) return;
	size_t last = text_lines(b->text) ? text_lines(b->text) - 1 : 0;
	text_insert(b->text, text_len(b->text), p, n);
	b->flags &= ~(unsigned)CORE_EMPTY;
	for (Win *w = E->wins; w; w = w->next)
		if (w->buf == b && w->line >= last) w->line = text_lines(b->text) - 1, w->col = 0;
	core_changed = 1;
}

static void putz(Sess *s, const char *z) { put(s, z, strlen(z)); }

/* The connections file's line for name, or with name "" the connection of
   the longest "dir" line holding path.  A line is
     name mssql|pg host[:port] user database [trust] [pass=...]
     dir folder name
   A password in the file makes it refused unless only its owner can read
   it (libpq's rule for ~/.pgpass).  0 found, -1 msg set. */
static int lookup(const char *name, const char *path, Spec *o)
{
	char f[4096], l[1024], want[256] = "";
	file_stdpath(f, sizeof f, 2, "/sql");
	FILE *fp = fopen(f, "r");
	if (!fp) { core_msg(E, "sql: no connections file %s (src/sql/README.md)", f); return -1; }
	struct stat st;
	int open = !fstat(fileno(fp), &st) && !(st.st_mode & 077);
	size_t best = 0;
	snprintf(want, sizeof want, "%s", name);
	for (int round = 0; round < 2; round++, rewind(fp)) {   /* the dir lines, then the connection */
		while (fgets(l, sizeof l, fp)) {
			char a[5][256] = {{0}}, x[2][256] = {{0}};
			int k = sscanf(l, "%255s %255s %255s %255s %255s %255s %255s", a[0], a[1], a[2], a[3], a[4], x[0], x[1]);
			if (k < 3 || a[0][0] == '#') continue;
			if (round == 0 && !*name && !strcmp(a[0], "dir")) {
				char d[4096];
				const char *h = getenv("HOME");
				snprintf(d, sizeof d, "%s%s", a[1][0] == '~' && h ? h : "", a[1] + (a[1][0] == '~' && h));
				size_t n = strlen(d);
				if (path && n > best && !strncmp(path, d, n) && (path[n] == '/' || d[n - 1] == '/')) best = n, snprintf(want, sizeof want, "%s", a[2]);
			} else if (round == 1 && k >= 5 && !strcmp(a[0], want) && (!strcmp(a[1], "mssql") || !strcmp(a[1], "pg"))) {
				*o = (Spec){ .pg = a[1][0] == 'p' };
				snprintf(o->name, sizeof o->name, "%s", a[0]);
				char *c = strrchr(a[2], ':');
				const char *p = c ? c + 1 : NULL;
				long port = p ? match_num(&p, p + strlen(p)) : o->pg ? 5432 : 1433;
				o->port = port > 0 && port < 65536 ? (int)port : 0;
				if (c) *c = 0;
				snprintf(o->host, sizeof o->host, "%s", a[2]);
				snprintf(o->user, sizeof o->user, "%s", a[3]);
				snprintf(o->db, sizeof o->db, "%s", a[4]);
				for (int j = 0; j < 2; j++)
					if (!strcmp(x[j], "trust")) o->trust = 1;
					else if (!strncmp(x[j], "pass=", 5)) snprintf(o->pass, sizeof o->pass, "%s", x[j] + 5);
				fclose(fp);
				if (*o->pass && !open) { core_msg(E, "sql: %s holds a password but others can read it (chmod 600)", f); return -1; }
				return 0;
			}
		}
		if (round == 0 && !*want) break;
	}
	fclose(fp);
	if (*want) core_msg(E, "sql: no connection %s in %s", want, f);
	else core_msg(E, "sql: no connection for this folder: :Connect name, or a dir line in %s", f);
	return -1;
}

static void wake(void *ctx, int fd);

static void drop(Sess *s)   /* the connection, gone or replaced */
{
	if (s->fd >= 0) loop_unwatch(s->fd);
	if (s->kill >= 0) loop_unwatch(s->kill);
	if (s->fd >= 0 || s->kill >= 0) db_close(&s->db);
	s->fd = s->kill = -1;
	s->q.n = s->i = 0;
}

/* Sends the next batch that runs, or ends the run */
static void next(Sess *s)
{
	while (s->i < s->q.n && s->left <= 0) if (++s->i < s->q.n) s->left = s->q.b[s->i].count;
	if (s->i >= s->q.n) {
		if (s->q.n) {
			char t[64];
			putz(s, (snprintf(t, sizeof t, "\nCompletion time: %.0f ms\n", (double)(loop_now() - s->t0) / 1e6), t));
			s->q.n = 0;
		}
		return;
	}
	SqlBatch b = s->q.b[s->i];
	Buf *qb = core_buf_find(E, s->nr);
	char c[4096];
	int bad = !qb;
	s->sql.n = 0;
	for (size_t p = b.a, k; !bad && p < b.z; p += k)
		k = text_read(qb->text, p, c, b.z - p < sizeof c ? b.z - p : sizeof c), bad = !k || str_add(&s->sql, c, k);
	if (bad || str_add(&s->sql, "", 1)) { putz(s, "sql: out of memory\n"); s->q.n = 0; return; }
	s->left--;
	if (db_query(&s->db, s->sql.s)) { putz(s, db_net(&s->db)->err), putz(s, "\n"); drop(s); return; }
	loop_write(s->fd, db_pending(&s->db));
}

static void wake(void *ctx, int fd)
{
	Sess *s = ctx;
	(void)fd;
	int was = db_state(&s->db), r = db_io(&s->db, 1);
	Str *o = db_out(&s->db);
	if (was >= DB_READY) put(s, o->s, o->n);   /* the login's messages are not the query's (SSMS shows none) */
	o->n = 0;
	if (s->kill >= 0 && db_kill_fd(&s->db) != s->kill) loop_unwatch(s->kill), s->kill = -1;   /* the cancel was sent */
	if (r || db_state(&s->db) == DB_DEAD) {
		putz(s, db_net(&s->db)->err), putz(s, "\n");
		drop(s);
		core_changed = 1;
		return;
	}
	loop_write(s->fd, db_pending(&s->db));
	if (db_state(&s->db) == DB_READY && was != DB_READY) next(s);
}

/* The results in a split below the query (make.c's window); another
   query's results shown give their window up first (one results split, as
   SSMS has one per query window).  The cursor stays.  0 ok, -1 msg set. */
static int show(Sess *s)
{
	if (s->res && shown(s->res)) return 0;
	long k = 0;
	for (size_t i = 0; i < nS && !k; i++) if (S[i]->res) k = shown(S[i]->res);
	if (k && (core_win_goto(E, 'w', k) || core_close(E, 0) || core_win_goto(E, 'w', shown(s->nr)))) return -1;
	Buf *r = s->res ? core_buf_find(E, s->res) : NULL;
	if (r ? core_split(E, 0, 0) || core_buf_goto(E, s->res) : core_new(E, 0)) return -1;
	s->res = E->buf->nr;
	E->buf->nonumber = E->buf->nomodifiable = 1;
	return core_win_goto(E, 'p', 1);
}

static int run(Sess *s, size_t a, size_t z)
{
	Buf *b = core_buf_find(E, s->nr);
	if (s->q.n) { core_msg(E, "sql: a query is running (Alt+Break cancels)"); return -1; }
	if (!s->spec.name[0] && lookup("", b->full, &s->spec)) return -1;
	long bad = 0;
	s->q.n = 0;
	if (s->spec.pg) { SqlBatch one = { a, z, 1 }; if (!(s->q.b = realloc(s->q.b, sizeof one))) return -1; s->q.b[0] = one, s->q.n = 1; }
	else bad = sql_split(b->text, a, z, &s->q);
	if (bad < 0) return -1;
	if (bad) core_msg(E, "sql: line %ld: a GO line sqlcmd refuses (Syntax error near command 'GO')", bad);
	if (!s->q.n) return bad ? -1 : 0;
	if (show(s)) return -1;
	Buf *r = core_buf_find(E, s->res);
	text_delete(r->text, 0, text_len(r->text));
	for (Win *w = E->wins; w; w = w->next) if (w->buf == r) w->line = w->col = w->top = 0;
	s->i = 0, s->left = s->q.b[0].count, s->t0 = loop_now();
	if (s->fd >= 0) { next(s); return 0; }
	Spec *c = &s->spec;
	if (db_open(&s->db, c->pg, c->host, c->port, c->user, c->pass, c->db, c->trust) || loop_watch(db_net(&s->db)->fd, wake, s)) {
		putz(s, *db_net(&s->db)->err ? db_net(&s->db)->err : "sql: too many watches"), putz(s, "\n");
		db_close(&s->db), s->q.n = 0;
		return -1;
	}
	s->fd = db_net(&s->db)->fd;
	loop_write(s->fd, 1);   /* the connect and the first bytes */
	return 0;
}

int sql_key(Editor *e, int key)
{
	E = e;
	Sess *s = sess(e->buf->nr, 0);
	if (key == KEY_BREAK) {
		if (!s || !s->q.n) return s != NULL;
		s->q.n = 0;
		if (db_state(&s->db) != DB_BUSY || db_cancel(&s->db)) {   /* still logging in: the connection goes */
			drop(s);
			putz(s, "Query was cancelled by user.\n");
			return 1;
		}
		if ((s->kill = db_kill_fd(&s->db)) >= 0 && loop_watch(s->kill, wake, s)) s->kill = -1;
		if (s->kill >= 0) loop_write(s->kill, 1);
		loop_write(s->fd, db_pending(&s->db));
		putz(s, "Query was cancelled by user.\n");   /* SSMS's line */
		return 1;
	}
	if (key != KEY_F5 || !is_sql(e->buf) || e->mode != MODE_NORMAL) return 0;
	if (!s && !(s = sess(e->buf->nr, 1))) return core_msg(e, "sql: out of memory"), 1;
	Text *t = e->buf->text;
	size_t a = 0, z = text_len(t);
	KeyState *k = &e->ks;
	if (k->visual) {   /* the selection: v from character to character, V and CTRL-V whole lines */
		size_t l1 = k->vline, c1 = k->vcol, l2 = e->win->line, c2 = e->win->col;
		if (l1 > l2 || (l1 == l2 && c1 > c2)) { size_t x = l1; l1 = l2, l2 = x; x = c1; c1 = c2, c2 = x; }
		a = text_line_start(t, l1) + (k->visual == 'v' ? c1 : 0);
		z = k->visual == 'v' ? text_line_start(t, l2) + c2 + 1 : text_line_start(t, l2 + 1);
		if (z > text_line_start(t, l2 + 1)) z = text_line_start(t, l2 + 1);
	}
	run(s, a, z);
	return 1;
}

int sql_results(Editor *e)
{
	E = e;
	Sess *s = sess(e->buf->nr, 0);
	if (!s || !s->res || !core_buf_find(e, s->res)) { core_msg(e, "sql: no results yet (F5 runs the query)"); return -1; }
	long k = shown(s->res);
	if (k) {   /* close it, the cursor back where it was */
		int in = e->buf->nr == s->res;
		if (core_win_goto(e, 'w', k) || core_close(e, 0)) return -1;
		long q = in ? shown(s->nr) : 0;
		return q ? core_win_goto(e, 'w', q) : in ? 0 : core_win_goto(e, 'p', 1);
	}
	return show(s);
}

int sql_connect(Editor *e, const char *name)
{
	E = e;
	Sess *s = sess(e->buf->nr, name && *name);
	if (!name || !*name) {
		core_msg(e, s && s->spec.name[0] ? "sql: %s (%s)" : "sql: not connected", s ? s->spec.name : "", s ? s->spec.host : "");
		return 0;
	}
	if (!s) { core_msg(e, "sql: out of memory"); return -1; }
	Spec c;
	if (lookup(name, NULL, &c)) return -1;
	drop(s);
	s->spec = c;
	core_msg(e, "sql: %s (%s)", c.name, c.host);
	return 0;
}
