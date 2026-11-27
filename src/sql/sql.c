/* SQL buffers (sql.h): SSMS's query window over the editor's buffers.  One
   connection per query buffer, opened at its first F5 and kept (so temp
   tables and an open transaction last, as in SSMS); its batches run one
   after another, everything the server says appended to the results
   buffer as it arrives, as make.c's buffer grows. */
#include <limits.h>
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
#include "uni.h"

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
	/* the results' layout (put): a line the read cut, the rows held until
	   the widths are known, the messages (shown after the grids, as SSMS's
	   Messages tab), the bytes for the buffer and where they go; the grids
	   and the spans the faces come from */
	Str part, held, msgs, o;
	size_t at, line, nheld, ng, nsp, csp;   /* at, line: o's first byte and line in the buffer */
	int in;                 /* a result set is open: 1 its rows held, 2 laid out */
	int ruled;              /* the messages' rule is out */
	struct Grid { size_t line, rows; int rn, n, *w; } *g;
	struct Span { size_t a; unsigned n; unsigned char f; } *sp;
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

/* The results as SSMS's grid: the clients' text (net.h's marks) laid out
   in columns as wide as the names and the first HOLD rows (capped at CAP
   cells, a longer value cut with a ...), so a long result shows its rows
   as they come.  SSMS's grid also sizes its columns from the first rows
   (hypothesis, not measured). */
enum { CAP = 40, HOLD = 500 };

/* The bytes of p[0, n) that fit in w cells (whole graphemes), their cells
   in *c; a control byte takes 2 (^X, as the core draws it) */
static size_t fit(const char *p, size_t n, size_t w, size_t *c)
{
	const unsigned char *u = (const unsigned char *)p;
	size_t i = 0, k = 0;
	for (size_t x; i < n && k + (x = u[i] < 32 || u[i] == 127 ? 2 : (size_t)uni_width(u, n, i)) <= w; k += x) i += uni_glen(u, n, i);
	*c = k;
	return i;
}

static int add(Sess *s, const char *p, size_t n) { return str_add(&s->o, p, n); }
static int pad(Sess *s, size_t n) { while (n--) if (add(s, " ", 1)) return -1; return 0; }
static int span(Sess *s, size_t a, size_t n, int f)   /* face f on the bytes [a, a + n) of o */
{
	if (s->nsp == s->csp) {
		struct Span *q = realloc(s->sp, (s->csp = s->csp ? 2 * s->csp : 64) * sizeof *q);
		if (!q) return s->csp = s->nsp, -1;
		s->sp = q;
	}
	s->sp[s->nsp++] = (struct Span){ s->at + a, (unsigned)n, (unsigned char)f };
	return 0;
}

/* a cell of w cells and its space: a NULL in its face, a cut value with
   the ... in another */
static int cell(Sess *s, const char *p, size_t n, int w, int last)
{
	int null = n && (unsigned char)*p == 0xFD;
	p += null, n -= (size_t)null;
	size_t c, k = fit(p, n, (size_t)w, &c);
	int cut = k < n;
	if (cut) k = fit(p, n, (size_t)w - 1, &c), c++;
	return (null && span(s, s->o.n, n, FACE_DIAG_WARN)) || add(s, p, k)
		|| (cut && (span(s, s->o.n, 3, FACE_NUMBER) || add(s, "\xe2\x80\xa6", 3)))
		|| (!last && pad(s, (size_t)w - c + 1));
}

/* a line of cells (the names or a row, without its mark): the row number
   (none for the names), then each cell after a | */
static int line(Sess *s, struct Grid *g, const char *p, size_t n, size_t nr)
{
	char t[32];
	int r = add(s, t, (size_t)(nr ? snprintf(t, sizeof t, "%*zu ", g->rn, nr) : snprintf(t, sizeof t, "%*s ", g->rn, "")));
	for (int i = 0; i < g->n && !r; i++) {
		const char *e = memchr(p, '\t', n);
		size_t k = e ? (size_t)(e - p) : n;
		r = add(s, "\xe2\x94\x82 ", 4) || cell(s, p, k, g->w[i], i == g->n - 1);
		p += k + !!e, n -= k + !!e;
	}
	return r || add(s, "\n", 1);
}

/* The held set laid out: the widths, the names on their rule, the rows */
static int lay(Sess *s)
{
	struct Grid *g = &s->g[s->ng - 1];
	char *p = s->held.s, *e = p + s->held.n, *z;
	for (char *q = p; q < e; q = z + 1) {   /* each line: its mark, then cells */
		z = memchr(q, '\n', (size_t)(e - q));
		for (int i = 0; i < g->n && q < z; i++) {
			char *t = memchr(q + 1, '\t', (size_t)(z - q - 1));
			size_t c, k = (size_t)((t ? t : z) - q - 1), nul = k && (unsigned char)q[1] == 0xFD;
			fit(q + 1 + nul, k - nul, CAP, &c);
			if ((int)c > g->w[i]) g->w[i] = (int)c;
			q = t ? t : z;
		}
	}
	g->rn = 1;   /* the row numbers' width: a set still coming may pass HOLD's */
	for (size_t k = s->nheld == HOLD ? 1000000 : s->nheld; k >= 10; k /= 10) g->rn++;
	g->line = s->line;
	for (char *q = s->o.s; q && q < s->o.s + s->o.n; q++) g->line += *q == '\n';
	z = memchr(p, '\n', (size_t)(e - p));
	int r = line(s, g, p + 1, (size_t)(z - p - 1), 0);
	for (int k = 0; k <= g->rn && !r; k++) r = add(s, "\xe2\x94\x80", 3);   /* the rule, a + under each | */
	for (int i = 0; i < g->n && !r; i++) {
		r = add(s, "\xe2\x94\xbc", 3);
		for (int k = 0; k < g->w[i] + 2 - (i == g->n - 1) && !r; k++) r = add(s, "\xe2\x94\x80", 3);
	}
	r = r || add(s, "\n", 1);
	for (char *q = z + 1; q < e && !r; q = z + 1) z = memchr(q, '\n', (size_t)(e - q)), r = line(s, g, q + 1, (size_t)(z - q - 1), ++g->rows);
	s->held.n = 0, s->in = 2;
	return r;
}

/* the open set's end: laid out if still held, its row count under it */
static int endset(Sess *s)
{
	if (!s->in || (s->in == 1 && lay(s))) return s->in ? -1 : 0;
	char t[32];
	size_t rows = s->g[s->ng - 1].rows, n = (size_t)snprintf(t, sizeof t, "(%zu row%s)\n\n", rows, rows == 1 ? "" : "s");
	s->in = 0;
	return span(s, s->o.n, n - 2, FACE_ORG_TABLE) || add(s, t, n);
}

/* one line of the clients' text (str_lines): a set's names start a grid,
   its rows go to it, anything else ends it and is a message */
static void take(void *ctx, char *p, size_t n)
{
	Sess *s = ctx;
	unsigned char m = n ? (unsigned char)*p : 0;
	int r = 0;
	if (m == 0xFF && s->in) {
		if (s->in == 2) r = line(s, &s->g[s->ng - 1], p + 1, n - 1, ++s->g[s->ng - 1].rows);
		else r = str_add(&s->held, p, n) || str_add(&s->held, "\n", 1) || (++s->nheld == HOLD && lay(s));
	} else if (m == 0xFE) {
		int k = 1, *w;
		for (size_t i = 1; i < n; i++) k += p[i] == '\t';
		struct Grid *g = (r = endset(s)) ? NULL : realloc(s->g, (s->ng + 1) * sizeof *g);
		if (g) s->g = g;
		if (!g || !(w = calloc((size_t)k, sizeof *w))) r = 1;
		else g[s->ng++] = (struct Grid){ .n = k, .w = w }, s->in = 1, s->nheld = 0, r = str_add(&s->held, p, n) || str_add(&s->held, "\n", 1);
	} else r = endset(s) || str_add(&s->msgs, p, n) || str_add(&s->msgs, "\n", 1);
	if (r) core_msg(E, "sql: out of memory (the results are cut)");
}

/* The messages under a rule after the grids, the errors in their face */
static int messages(Sess *s)
{
	static const char h[] = "\xe2\x94\x80\xe2\x94\x80 Messages \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\n";
	int r = endset(s);
	if (r || !s->msgs.n) return r;
	r = !s->ruled && (span(s, s->o.n, sizeof h - 2, FACE_FUNCTION) || add(s, h, sizeof h - 1));
	s->ruled = 1;
	for (size_t i = 0, a = 0; i < s->msgs.n && !r; i++)
		if ((unsigned char)s->msgs.s[i] == 0xFC) {
			if (a) r = span(s, a, s->o.n - a, FACE_DIAG_ERROR), a = 0;
			else a = s->o.n;
		} else r = add(s, s->msgs.s + i, 1);
	s->msgs.n = 0;
	return r;
}

/* n bytes of the clients' text at the end of the results
   (core_buf_append: a window on the last line follows); with no run
   going, what is held goes out, the messages last */
static void put(Sess *s, const char *p, size_t n)
{
	Buf *b = s->res ? core_buf_find(E, s->res) : NULL;
	if (!b) return;
	s->o.n = 0, s->at = text_len(b->text), s->line = text_lines(b->text);
	int r = str_lines(&s->part, p, n, take, s);
	if (!s->q.n && s->part.n) take(s, s->part.s, s->part.n), s->part.n = 0;   /* a last line without its \n */
	if (r || (!s->q.n && messages(s))) core_msg(E, "sql: out of memory (the results are cut)");
	if (s->o.n) core_buf_append(E, b, s->o.s, s->o.n);
}

static void putz(Sess *s, const char *z) { put(s, z, strlen(z)); }

/* the run is over: what it left held goes out */
static void end(Sess *s) { s->q.n = 0, put(s, "", 0); }

/* a new run's empty results */
static void clear(Sess *s)
{
	core_buf_fill(E, core_buf_find(E, s->res), "", 0, 0);
	for (size_t i = 0; i < s->ng; i++) free(s->g[i].w);
	s->ng = s->nsp = 0, s->in = s->ruled = 0, s->part.n = s->held.n = s->msgs.n = 0;
}

/* The results' faces (Buf.faces): the grids' names, rule, row numbers and
   |, then the spans: NULLs, cuts, counts, the messages' rule and errors */
static void rfaces(const Buf *b, size_t a, size_t z, unsigned char *f)
{
	static char *t;   /* the line, kept: no allocation once it is long enough */
	static size_t tn;
	Sess *s = sess(b->nr, 0);
	memset(f, FACE_NONE, z - a);
	if (!s) return;
	size_t l = text_line_of(b->text, a), ls = text_line_start(b->text, l), lo = 0, hi = s->ng;
	while (lo < hi) { size_t m = (lo + hi) / 2; if (s->g[m].line <= l) lo = m + 1; else hi = m; }
	struct Grid *g = lo ? &s->g[lo - 1] : NULL;
	if (g && l - g->line < 2) memset(f, l == g->line ? FACE_PMENU_SEL : FACE_ORG_TABLE, z - a);
	else if (g && l - g->line < g->rows + 2) {   /* a row: its number, then a | and a cell each */
		size_t n = text_line_start(b->text, l + 1) - ls, c, k = (size_t)g->rn + 1;
		char *q = n > tn ? realloc(t, n) : t;
		if (!q) return;
		t = q, tn = n > tn ? n : tn, n = text_read(b->text, ls, t, n);
		for (size_t j = a; j < z && j < ls + k; j++) f[j - a] = FACE_LINE_NR;
		for (int i = 0; i < g->n && k < n; i++) {
			for (size_t j = ls + k > a ? ls + k : a; j < z && j < ls + k + 3; j++) f[j - a] = FACE_ORG_TABLE;
			k += 4;
			k += fit(t + k, k < n ? n - k : 0, (size_t)g->w[i] + (i < g->n - 1), &c);
		}
	}
	lo = 0, hi = s->nsp;
	while (lo < hi) { size_t m = (lo + hi) / 2; if (s->sp[m].a + s->sp[m].n <= a) lo = m + 1; else hi = m; }
	for (; lo < s->nsp && s->sp[lo].a < z; lo++)
		for (size_t j = s->sp[lo].a > a ? s->sp[lo].a : a, e = s->sp[lo].a + s->sp[lo].n; j < e && j < z; j++) f[j - a] = s->sp[lo].f;
}

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
			char a[5][256] = {{0}}, x[2][256] = {{0}};
			int k = sscanf(l, "%255s %255s %255s %255s %255s %255s %255s", a[0], a[1], a[2], a[3], a[4], x[0], x[1]);
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
				for (int j = 0; j < 2; j++)
					if (!strcmp(x[j], "trust")) o->trust = 1;
					else if (!strncmp(x[j], "pass=", 5)) snprintf(o->pass, sizeof o->pass, "%s", x[j] + 5);
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
	Str cols;               /* the rows as schema\0table\0column\0 */
} Cat;

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
	if (st != 2) c->cols.n = 0;
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
		if (o->n) o->n = net_plain(o->s, o->n);   /* the layout's marks (net.h) */
		if (o->n) for (char *p = o->s, *z = p + o->n, *n, *a, *b, *h = NULL; !r && (n = memchr(p, '\n', (size_t)(z - p))); p = n + 1)
			if ((a = memchr(p, '\t', (size_t)(n - p))) && (b = memchr(a + 1, '\t', (size_t)(n - a - 1))) && (h || !(h = p)))
				*a = *b = *n = 0, r = str_add(&c->cols, p, net_unesc(p, (size_t)(n + 1 - p)));   /* a tab in a name came escaped */
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
	s->i = 0, s->cancel = 0;
	end(s);
}

/* Sends the next batch that runs, or ends the run */
static void next(Sess *s)
{
	while (s->i < s->q.n && s->left <= 0) if (++s->i < s->q.n) s->left = s->q.b[s->i].count;
	if (s->i >= s->q.n) {
		if (s->q.n) {
			char t[64];
			putz(s, (snprintf(t, sizeof t, "\nCompletion time: %.0f ms\n", (double)(loop_now() - s->t0) / 1e6), t));
			end(s);
		}
		return;
	}
	SqlBatch b = s->q.b[s->i];   /* from the text at F5: edits since do not change the run (SqlSess.tla's Snapshot) */
	s->sql.n = 0;
	if (str_add(&s->sql, s->text.s + (b.a - s->base), b.z - b.a) || str_add(&s->sql, "", 1)) { putz(s, "sql: out of memory\n"); end(s); return; }
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
		if (s->cancel) s->cancel = 0, end(s);   /* the server answered the cancel: the run ends here (SqlSess.tla) */
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
	E->buf->faces = rfaces;
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
	clear(s);
	s->i = 0, s->left = s->q.b[0].count, s->t0 = loop_now();
	if (s->fd >= 0) { next(s); return 0; }
	if ((s->fd = dial(&s->db, &s->spec, wake, s)) < 0) { putz(s, db_net(&s->db)->err), putz(s, "\n"); db_close(&s->db), end(s); return -1; }
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

int sql_catalog(Editor *e, const Buf *b, void (*fn)(void *ctx, const char *schema, const char *table, const char *column), void *ctx)
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
	for (const char *p = k->cols.s, *z = p + k->cols.n, *t, *col; p < z; n++, p = col + strlen(col) + 1)
		t = p + strlen(p) + 1, col = t + strlen(t) + 1, fn(ctx, p, t, col);
	return n;
}
