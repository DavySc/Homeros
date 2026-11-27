/* SQL buffers (sql.h): SSMS's query window over the editor's buffers.  One
   connection per query buffer, opened at its first F5 and kept (so temp
   tables and an open transaction last, as in SSMS); its batches run one
   after another, everything the server says appended to the results
   buffer as it arrives, as make.c's buffer grows. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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
	SqlBatches q;           /* the run: its batches (offsets in the buffer at F5), the one running, its runs left */
	size_t i;
	long left;
	int cancel;             /* Alt+Break sent a cancel the server has not answered yet */
	Str text, sql;          /* the run's text as it was at F5, from offset base; the running batch's */
	size_t base;
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

/* n bytes at the end of the results (core_buf_append: a window on the
   last line follows) */
static void put(Sess *s, const char *p, size_t n)
{
	Buf *b = s->res ? core_buf_find(E, s->res) : NULL;
	if (b && n) core_buf_append(E, b, p, n);
}

static void putz(Sess *s, const char *z) { put(s, z, strlen(z)); }

/* The connections file's line for name, or with name "" the connection of
   the longest "dir" line holding path.  A line is
     name mssql|pg host[:port] user database [trust] [pass=...]
   (user - : the Windows login, SQL Server's from Windows)
     dir folder name
   A password in the file makes it refused unless only its owner can read
   it (libpq's rule for ~/.pgpass).  0 found, -1 msg set. */
#ifdef _WIN32
#define HOME "USERPROFILE"
static int sep(char c) { return c == '\\' || c == '/'; }
#else
#define HOME "HOME"
static int sep(char c) { return c == '/'; }
#endif

static int lookup(const char *name, const char *path, Spec *o)
{
	char f[4096], l[1024], want[256] = "";
	file_stdpath(f, sizeof f, 2, "/sql");
	FILE *fp = fopen(f, "r");
	if (!fp) { core_msg(E, "sql: no connections file %s (src/sql/README.md)", f); return -1; }
#ifdef _WIN32   /* libpq checks pgpass.conf's mode off Windows only: the profile is the user's */
	int open = 1;
#else
	struct stat st;
	int open = !fstat(fileno(fp), &st) && !(st.st_mode & 077);
#endif
	size_t best = 0;
	snprintf(want, sizeof want, "%s", name);
	for (int round = 0; round < 2; round++, rewind(fp)) {   /* the dir lines, then the connection */
		while (fgets(l, sizeof l, fp)) {
			char a[5][256] = {{0}}, x[256] = "", pass[256] = "", *pw = l;
			/* pass= comes last and takes the rest of the line, spaces too
			   (Davy's password has them), but not a Windows editor's CR */
			while ((pw = strstr(pw + 1, "pass=")) && !match_space(pw[-1]));
			if (pw) pw[strcspn(pw, "\r\n")] = 0, snprintf(pass, sizeof pass, "%s", pw + 5), *pw = 0;
			int k = sscanf(l, "%255s %255s %255s %255s %255s %255s", a[0], a[1], a[2], a[3], a[4], x);
			if (k < 3 || a[0][0] == '#') continue;
			if (round == 0 && !*name && !strcmp(a[0], "dir")) {   /* "dir folder name": the folder may hold spaces */
				char t[4096], d[PATH_MAX], *fs = l + strspn(l, " 	") + 3, *fe = fs + strlen(fs), *nm;
				while (fe > fs && match_space(fe[-1])) fe--;
				for (nm = fe; nm > fs && !match_space(nm[-1]);) nm--;   /* the name: the last word */
				snprintf(a[2], sizeof a[2], "%.*s", (int)(fe - nm), nm);
				for (fe = nm; fe > fs && match_space(fe[-1]);) fe--;
				fs += strspn(fs, " 	");
				const char *h = getenv(HOME);
				int home = *fs == '~' && h;
				snprintf(t, sizeof t, "%s%.*s", home ? h : "", (int)(fe - fs - home), fs + home);
				if (file_full(t, d)) snprintf(d, sizeof d, "%s", t);   /* the buffer's name's form (C:\ on Windows) */
				size_t n = strlen(d);
				if (path && n > best && !strncmp(path, d, n) && (sep(path[n]) || sep(d[n - 1]))) best = n, snprintf(want, sizeof want, "%s", a[2]);
			} else if (round == 1 && k >= 5 && !strcmp(a[0], want) && (!strcmp(a[1], "mssql") || !strcmp(a[1], "pg"))) {
				*o = (Spec){ .pg = a[1][0] == 'p' };
				snprintf(o->name, sizeof o->name, "%s", a[0]);
				/* host, host:port, [IPv6] or [IPv6]:port */
				char *h = a[2] + (a[2][0] == '['), *c = strrchr(h, a[2][0] == '[' ? ']' : ':');
				if (a[2][0] == '[' && c) *c++ = 0, c = *c == ':' ? c : NULL;
				const char *p = c ? c + 1 : NULL;
				long port = p ? match_num(&p, p + strlen(p)) : o->pg ? 5432 : 1433;
				fclose(fp);
				if (port <= 0 || port > 65535 || (p && *p)) { core_msg(E, "sql: %s: the port in %s is not a number from 1 to 65535", a[0], a[2]); return -1; }
				if (c && a[2][0] != '[') *c = 0;
				o->port = (int)port;
				snprintf(o->host, sizeof o->host, "%s", h);
				snprintf(o->user, sizeof o->user, "%s", a[3]);
				snprintf(o->db, sizeof o->db, "%s", a[4]);
				o->trust = !strcmp(x, "trust");
				memcpy(o->pass, pass, sizeof pass);
				if (!o->pg && !*o->pass && strcmp(o->user, "-")) { core_msg(E, "sql: %s has no pass= (an empty password is a failed login on the server)", o->name); return -1; }   /* user "-": the Windows login */
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

/* Opens c with the loop watching it, fn waking: its fd, or -1 with d's
   error set (the caller closes d once it has read it: db_close clears it) */
static int dial(Db *d, const Spec *c, void (*fn)(void *, int), void *ctx)
{
	int r = db_open(d, c->pg, c->host, c->port, c->pg || strcmp(c->user, "-") ? c->user : NULL, c->pass, c->db, c->trust);
	Net *n = db_net(d);
	if (r || loop_watch(n->fd, fn, ctx)) {
		if (!*n->err) snprintf(n->err, sizeof n->err, "sql: too many watches");
		return -1;
	}
	loop_write(n->fd, 1);   /* the connect and the first bytes */
	return n->fd;
}

/* A connection's catalog (sql_catalog), read on a connection of its own
   when a buffer's session on it logs in, as SSMS's IntelliSense needs a
   connected window (SqlCat.tla) */
typedef struct Cat {
	struct Cat *next;
	char name[256];
	Db db;
	int fd, st;             /* its connection while it reads (-1 none); 0 not read, 1 logging in, 3 reading, 2 read, -1 failed */
	Str cols, tabs;         /* the rows as schema\0table\0column\0 (the server sorted them); each table's first row's offset, as size_t */
} Cat;

/* A catalog row at p: its table and column; the next row */
static const char *row(const char *p, const char **t, const char **col)
{
	*t = p + strlen(p) + 1, *col = *t + strlen(*t) + 1;
	return *col + strlen(*col) + 1;
}

static Cat *C;

static Cat *cat(const char *name, int make)
{
	Cat *c = C;
	while (c && strcmp(c->name, name)) c = c->next;
	if (!c && make && (c = calloc(1, sizeof *c))) *c = (Cat){ .next = C, .fd = -1 }, snprintf(c->name, sizeof c->name, "%s", name), C = c;
	return c;
}

static void uncat(Cat *c, int st)   /* its connection closed, its state st */
{
	if (c->fd >= 0) loop_unwatch(c->fd);
	if (c->st == 1 || c->st == 3) db_close(&c->db);
	c->fd = -1, c->st = st;
	if (st != 2) c->cols.n = c->tabs.n = 0;
}

static void catwake(void *ctx, int fd)
{
	Cat *c = ctx;
	(void)fd;
	int was = db_state(&c->db), r = db_io(&c->db, 1), now = db_state(&c->db);
	Str *o = db_out(&c->db);
	if (was < DB_READY) o->n = 0;   /* the login's messages */
	/* the first idle after the login (which may run a batch of its own
	   first: TDS's) sends the query, the next one has its rows; capitals,
	   for a case-sensitive SQL Server database (Postgres folds them) */
	if (!r && was != DB_READY && now == DB_READY && c->st == 1) c->st = 3, r = db_query(&c->db,
		"SELECT TABLE_SCHEMA, TABLE_NAME, COLUMN_NAME FROM INFORMATION_SCHEMA.COLUMNS"
		" WHERE TABLE_SCHEMA NOT IN ('pg_catalog', 'information_schema') ORDER BY 1, 2, ORDINAL_POSITION");
	else if (!r && was != DB_READY && now == DB_READY) {   /* the rows: the lines after the header's, three fields each */
		if (o->n) for (char *p = o->s, *z = p + o->n, *n, *a, *b, *h = NULL; !r && (n = memchr(p, '\n', (size_t)(z - p))); p = n + 1)
			if ((a = memchr(p, '\t', (size_t)(n - p))) && (b = memchr(a + 1, '\t', (size_t)(n - a - 1))) && (h || !(h = p)))
				*a = *b = *n = 0, r = str_add(&c->cols, p, net_unesc(p, (size_t)(n + 1 - p)));   /* a tab in a name came escaped */
		for (const char *p = c->cols.s, *z = p + c->cols.n, *q = NULL, *t, *col, *n; !r && p < z; q = p, p = n)
			if (n = row(p, &t, &col), !q || strcmp(p, q) || strcmp(t, q + strlen(q) + 1)) {   /* a table's first row */
				size_t at = (size_t)(p - c->cols.s);
				r = str_add(&c->tabs, (const char *)&at, sizeof at);
			}
		uncat(c, r ? -1 : 2);
		return;
	}
	if (r || now == DB_DEAD) { uncat(c, -1); return; }
	loop_write(c->fd, db_pending(&c->db));
}

static void catread(const Spec *p)   /* a session on p logged in: its catalog read, unless read or reading */
{
	Cat *c = cat(p->name, 1);
	if (!c || c->st > 0) return;
	c->st = 1;
	if ((c->fd = dial(&c->db, p, catwake, c)) < 0) uncat(c, -1);
}

static void drop(Sess *s)   /* the connection, gone or replaced */
{
	if (s->fd >= 0) loop_unwatch(s->fd);
	if (s->kill >= 0) loop_unwatch(s->kill);
	if (s->fd >= 0 || s->kill >= 0) db_close(&s->db);
	s->fd = s->kill = -1;
	s->q.n = s->i = 0, s->cancel = 0;
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
	SqlBatch b = s->q.b[s->i];   /* from the text at F5: edits since do not change the run (SqlSess.tla's Snapshot) */
	s->sql.n = 0;
	if (str_add(&s->sql, s->text.s + (b.a - s->base), b.z - b.a) || str_add(&s->sql, "", 1)) { putz(s, "sql: out of memory\n"); s->q.n = 0; return; }
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
	Net *k = db_kill(&s->db);
	if (s->kill >= 0 && !k) loop_unwatch(s->kill), s->kill = -1;   /* the server closed it: the cancel is done */
	if (k) loop_write(s->kill, k->snd.n > 0);
	if (r || db_state(&s->db) == DB_DEAD) {
		putz(s, db_net(&s->db)->err), putz(s, "\n");
		drop(s);
		core_changed = 1;
		return;
	}
	loop_write(s->fd, db_pending(&s->db));
	if (was < DB_READY && db_state(&s->db) >= DB_READY) catread(&s->spec);
	if (db_state(&s->db) == DB_READY && was != DB_READY) {
		if (s->cancel) s->cancel = 0, s->q.n = 0;   /* the server answered the cancel: the run ends here (SqlSess.tla) */
		else next(s);
	}
}

/* The results in a split below the query (make.c's window); another
   query's results shown give their window up first (one results split, as
   SSMS has one per query window).  The cursor stays.  0 ok, -1 msg set. */
static int show(Sess *s)
{
	if (s->res && core_buf_shown(E, s->res)) return 0;
	long k = 0;
	for (size_t i = 0; i < nS && !k; i++) if (S[i]->res) k = core_buf_shown(E, S[i]->res);
	if (k && (core_win_goto(E, 'w', k) || core_close(E, 0) || core_win_goto(E, 'w', core_buf_shown(E, s->nr)))) return -1;
	Buf *r = s->res ? core_buf_find(E, s->res) : NULL;
	if (r ? core_split(E, 0, 0) || core_buf_goto(E, s->res) : core_new(E, 0)) return -1;
	s->res = E->buf->nr;
	E->buf->nonumber = E->buf->nomodifiable = 1;
	return core_win_goto(E, 'p', 1);
}

static int run(Sess *s, size_t a, size_t z)
{
	Buf *b = core_buf_find(E, s->nr);
	if (s->q.n) { core_msg(E, s->cancel ? "sql: the server has not answered the cancel yet" : "sql: a query is running (Ctrl-C or Alt+Break cancels)"); return -1; }
	if (!s->spec.name[0] && lookup("", b->full, &s->spec)) return -1;
	long bad = 0;
	s->q.n = 0;
	if (s->spec.pg) { SqlBatch one = { a, z, 1 }; if (!(s->q.b = realloc(s->q.b, sizeof one))) return -1; s->q.b[0] = one, s->q.n = 1; }
	else bad = sql_split(b->text, a, z, &s->q);
	if (bad < 0) return -1;
	if (bad) core_msg(E, "sql: line %ld: a GO line sqlcmd refuses (Syntax error near command 'GO')", bad);
	if (!s->q.n) return bad ? -1 : 0;
	char t[4096];
	int oom = 0;
	s->text.n = 0, s->base = a;   /* the run's own copy (SqlSess.tla's Snapshot) */
	for (size_t p = a, k; !oom && p < z; p += k)
		k = text_read(b->text, p, t, z - p < sizeof t ? z - p : sizeof t), oom = !k || str_add(&s->text, t, k);
	if (oom) core_msg(E, "sql: out of memory");
	if (oom || show(s)) { s->q.n = 0; return -1; }
	core_buf_fill(E, core_buf_find(E, s->res), "", 0, 0);
	s->i = 0, s->left = s->q.b[0].count, s->t0 = loop_now();
	if (s->fd >= 0) { next(s); return 0; }
	if ((s->fd = dial(&s->db, &s->spec, wake, s)) < 0) { putz(s, db_net(&s->db)->err), putz(s, "\n"); db_close(&s->db), s->q.n = 0; return -1; }
	return 0;
}

int sql_key(Editor *e, int key)
{
	if (key != KEY_F5 && key != KEY_BREAK && key != KEY_CTRL_C) return 0;
	E = e;
	Sess *s = sess(e->buf->nr, 0);
	if (key != KEY_F5) {
		/* Ctrl-C cancels as Alt+Break, which a terminal cannot send (Davy,
		   2026-11-24); with no run to cancel it stays Vim's */
		if (!s || !s->q.n || s->cancel) return s && key == KEY_BREAK;
		if (db_state(&s->db) != DB_BUSY || db_cancel(&s->db)) drop(s);   /* logging in: the connection goes */
		else {   /* the run stays busy until the server answers (SqlSess.tla's Break and Ack) */
			s->cancel = 1;
			Net *k = db_kill(&s->db);
			if (k && !loop_watch(k->fd, wake, s)) s->kill = k->fd, loop_write(s->kill, 1);
			loop_write(s->fd, db_pending(&s->db));
		}
		putz(s, "Query was cancelled by user.\n");   /* SSMS's line, at once */
		return 1;
	}
	Buf *qb = s ? core_buf_find(e, s->nr) : e->buf;   /* F5 in the results runs their query, as SSMS */
	if (!qb || !is_sql(qb)) return 0;
	if (!s && !(s = sess(e->buf->nr, 1))) return core_msg(e, "sql: out of memory"), 1;
	size_t a = 0, z = text_len(qb->text);
	if (e->ks.visual && qb == e->buf) core_visual(e, &a, &z);   /* the selection */
	run(s, a, z);
	return 1;
}

int sql_results(Editor *e)
{
	E = e;
	Sess *s = sess(e->buf->nr, 0);
	if (!s || !s->res || !core_buf_find(e, s->res)) { core_msg(e, "sql: no results yet (F5 runs the query)"); return -1; }
	long k = core_buf_shown(E, s->res);
	if (k) {   /* close it, the cursor back where it was */
		int in = e->buf->nr == s->res;
		if (core_win_goto(e, 'w', k) || core_close(e, 0)) return -1;
		return in ? 0 : core_win_goto(e, 'p', 1);   /* from the results: the window above, the query's, took their room */
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
	Cat *k = cat(name, 0);
	if (k) uncat(k, 0);   /* read again at its next login: the file may name another server now */
	drop(s);
	s->spec = c;
	core_msg(e, "sql: %s (%s)", c.name, c.host);
	return 0;
}

int sql_disconnect(Editor *e)
{
	E = e;
	Sess *s = sess(e->buf->nr, 0);
	if (!s || s->fd < 0) { core_msg(e, "sql: not connected"); return -1; }
	drop(s);   /* the server ends the session: its temp tables, an open transaction rolled back */
	core_msg(e, "sql: disconnected from %s (F5 connects again)", s->spec.name);
	return 0;
}

int sql_catalog(Editor *e, const Buf *b, const char *table, void (*fn)(void *ctx, const char *schema, const char *table, const char *column), void *ctx)
{
	E = e;
	Sess *s = is_sql(b) ? sess(b->nr, 1) : NULL;
	char m[sizeof e->msg];
	Spec c;
	memcpy(m, e->msg, sizeof m);   /* completion asked, not Davy: no message */
	if (s && !s->spec.name[0] && !lookup("", b->full, &c)) s->spec = c;
	memcpy(e->msg, m, sizeof m);
	if (!s || !s->spec.name[0]) return 0;
	Cat *k = cat(s->spec.name, 0);
	if (!k) return 0;
	if (!fn) {   /* read again: now if this buffer is logged in, else at its login */
		uncat(k, 0);
		if (s->fd >= 0 && db_state(&s->db) >= DB_READY) catread(&s->spec);
		return 0;
	}
	if (k->st != 2 || !k->cols.n) return 0;
	int n = 0;
	const size_t *o = (const void *)k->tabs.s, nt = k->tabs.n / sizeof *o;
	for (size_t i = 0; i < nt; i++) {   /* the table count plus the columns asked for, not the whole catalog */
		const char *p = k->cols.s + o[i], *z = k->cols.s + (i + 1 < nt ? o[i + 1] : k->cols.n), *t, *col;
		row(p, &t, &col);
		if (!table) fn(ctx, p, t, NULL), n++;
		else if (!strcasecmp(t, table)) for (const char *q; p < z; p = q, n++) q = row(p, &t, &col), fn(ctx, p, t, col);
	}
	return n;
}
