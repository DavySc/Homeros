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
#include "tools.h"
#include "uni.h"
#include "db.h"
#include "run.h"

/* A connection as the connections file names it */
typedef struct { char name[256], host[256], user[256], pass[256], db[256]; int pg, port, trust, alone; } Spec;   /* alone: the server, database - */

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
	Str vars, decl;         /* :Declare's "@name\0type = value\0" pairs; the run's DECLAREs (SqlBatch's pre) */
	size_t pa, pz;          /* the F5 that asked :Declare */
	/* the results' layout (put): a line the read cut, the rows held until
	   the widths are known, the messages that came inside the open set,
	   the bytes for the buffer and where they go, the cut values whole; the
	   grids and the spans the faces come from */
	Str part, held, msgs, o, whole;
	size_t at, line, lo, nheld, ng, nsp, csp, ncut;   /* at, line: o's first byte and line in the buffer; lo: o's bytes counted in line */
	long long t1;           /* the open set's start */
	int in;                 /* a result set is open: 1 its rows held, 2 laid out */
	int said, err;          /* a message block is open (its rule out); an error's text is */
	int looked;             /* the folder's connection looked up (conn) */
	int bad;                /* the run's worst: 1 an error, 2 cancelled */
	char status[600];       /* the results' status line (Buf.status), SSMS's bar under the query */
	struct Grid { size_t line, rows; int rn, n, *w; } *g;
	struct Span { size_t a; unsigned n; unsigned char f; } *sp;
	struct Cut { size_t at, p, n; } *cut;   /* a cut cell's first byte in the results, its whole value in whole */
} Sess;

static Editor *E;
/* the session whose F5 waits for the Enter that answers its :Declare
   prompt; any key that reaches sql_key is typed outside that prompt and
   forgets it (Esc, Ctrl-C, BS on the empty line), so a :Declare typed
   later never runs other text */
static void *W;
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
   cells, a longer value cut with a ..., which Enter shows whole), so a
   long result shows its rows as they come; a set is laid out early when
   its rows trickle (WAIT).  SSMS's grid also sizes its columns from the
   first rows (hypothesis, not measured).  Messages show as they come, under
   a rule; one that comes inside a set waits for the set's end. */
enum { CAP = 40, HOLD = 500, WAIT = 200 /* ms */ };
#define NONAME "(No column name)"   /* SSMS's name for a column without one */

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
static int pad(Sess *s, size_t n)
{
	static const char b[] = "                                         ";   /* a cell's most: CAP + 1 */
	_Static_assert(sizeof b - 1 == CAP + 1, "pad's blanks");
	return add(s, b, n);
}
static int gap(Sess *s) { return (s->at || s->o.n) && add(s, "\n", 1); }   /* a blank line, unless first */
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

/* a cell of w cells and its space (last: 1 the names' last cell, no
   space; 2 a row's, nothing after it): a NULL in its face; a value wider
   than w but within CAP whole, pushing the row's later cells right (an id
   that gains a digit after the first rows stays readable); a longer one cut
   at w with the ... in another face, kept whole for Enter */
static int cell(Sess *s, const char *p, size_t n, int w, int last)
{
	int null = n && (unsigned char)*p == 0xFD;
	p += null, n -= (size_t)null;
	size_t c, k = fit(p, n, CAP, &c);
	int cut = k < n;
	if (cut) {
		struct Cut *q = s->ncut % 64 ? s->cut : realloc(s->cut, (s->ncut + 64) * sizeof *q);
		if (!q) return -1;
		s->cut = q, s->cut[s->ncut++] = (struct Cut){ s->at + s->o.n, s->whole.n, n };
		if (str_add(&s->whole, p, n)) return -1;
		k = fit(p, n, (size_t)w - 1, &c), c++;
	}
	return (null && span(s, s->o.n, n, FACE_DIAG_WARN)) || add(s, p, k)
		|| (cut && (span(s, s->o.n, 3, FACE_NUMBER) || add(s, "\xe2\x80\xa6", 3)))
		|| (last < 2 && pad(s, (c < (size_t)w ? (size_t)w - c : 0) + !last));
}

/* a line of cells (the names or a row, without its mark): the row number
   (none for the names), then each cell after a |; the names fill their
   last cell, so their bar is as wide as the rule */
static int line(Sess *s, struct Grid *g, const char *p, size_t n, size_t nr)
{
	char t[32];
	int r = add(s, t, (size_t)(nr ? snprintf(t, sizeof t, "%*zu ", g->rn, nr) : snprintf(t, sizeof t, "%*s ", g->rn, "")));
	for (int i = 0; i < g->n && !r; i++) {
		const char *e = memchr(p, '\t', n);
		size_t k = e ? (size_t)(e - p) : n;
		int last = i < g->n - 1 ? 0 : nr ? 2 : 1;
		r = add(s, "\xe2\x94\x82 ", 4) || cell(s, nr || k ? p : NONAME, nr || k ? k : sizeof NONAME - 1, g->w[i], last);
		p += k + !!e, n -= k + !!e;
	}
	return r || add(s, "\n", 1);
}

/* The held set laid out (open: more rows may come): the widths, the names
   on their rule, the rows */
static int lay(Sess *s, int open)
{
	struct Grid *g = &s->g[s->ng - 1];
	char *p = s->held.s, *e = p + s->held.n, *z;
	for (char *q = p; q < e; q = z + 1) {   /* each line: its mark, then cells */
		z = memchr(q, '\n', (size_t)(e - q));
		int head = q == p;
		for (int i = 0; i < g->n && q < z; i++) {
			char *t = memchr(q + 1, '\t', (size_t)(z - q - 1));
			size_t c, k = (size_t)((t ? t : z) - q - 1), nul = k && (unsigned char)q[1] == 0xFD;
			fit(q + 1 + nul, k - nul, CAP, &c);
			if (head && !k) c = sizeof NONAME - 1;
			if ((int)c > g->w[i]) g->w[i] = (int)c;
			q = t ? t : z;
		}
	}
	g->rn = 1;   /* the row numbers' width: a set still coming may pass its first rows' */
	for (size_t k = open ? 1000000 : s->nheld; k >= 10; k /= 10) g->rn++;
	int r = gap(s);
	for (char *q = s->o.s + s->lo; q < s->o.s + s->o.n; q++) s->line += *q == '\n';   /* the lines before it */
	s->lo = s->o.n, g->line = s->line, s->said = 0;
	z = memchr(p, '\n', (size_t)(e - p));
	r = r || line(s, g, p + 1, (size_t)(z - p - 1), 0);
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

/* a message line: under the Messages rule when one starts after a grid,
   an error (between NET_ERRs, which may span lines) in its face, no mark
   shown (a message holds no other: the clients' text is UTF-8) */
static int say(Sess *s, const char *p, size_t n)
{
	static const char h[] = "\xe2\x94\x80\xe2\x94\x80 Messages \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\n";
	int r = !s->said && (gap(s) || span(s, s->o.n, sizeof h - 2, FACE_FUNCTION) || add(s, h, sizeof h - 1));
	size_t a = s->o.n;
	s->said = 1;
	for (size_t i = 0, j; i < n && !r; i = j + 1) {
		for (j = i; j < n && (unsigned char)p[j] != 0xFC; j++) ;
		r = add(s, p + i, j - i);
		if (!r && j < n) {
			if (s->err) r = span(s, a, s->o.n - a, FACE_DIAG_ERROR);
			s->err = !s->err, s->bad |= 1, a = s->o.n;
		}
	}
	return r || (s->err && span(s, a, s->o.n - a, FACE_DIAG_ERROR)) || add(s, "\n", 1);
}

/* the open set's end: laid out if still held, its row count under it, the
   messages that came inside it */
static int endset(Sess *s)
{
	if (!s->in || (s->in == 1 && lay(s, 0))) return s->in ? -1 : 0;
	char t[32];
	size_t rows = s->g[s->ng - 1].rows, n = (size_t)snprintf(t, sizeof t, "(%zu row%s)\n", rows, rows == 1 ? "" : "s");
	int r = span(s, s->o.n, n - 1, FACE_ORG_TABLE) || add(s, t, n);
	s->in = 0;
	for (char *q = s->msgs.s, *e = q + s->msgs.n, *z; q < e && !r; q = z + 1) z = memchr(q, '\n', (size_t)(e - q)), r = say(s, q, (size_t)(z - q));
	s->msgs.n = 0;
	return r;
}

static void put(Sess *s, const char *p, size_t n);
static void tick(void *s) { put(s, "", 0); }   /* a trickling set's rows shown */

/* one line of the clients' text (str_lines): a set's names start a grid,
   its rows go to it, its end (NET_END) ends it; anything else is a message,
   held while a set is open */
static void take(void *ctx, char *p, size_t n)
{
	Sess *s = ctx;
	unsigned char m = n ? (unsigned char)*p : 0;
	int r = 0;
	if (m == 0xFF && s->in) {
		if (s->in == 2) r = line(s, &s->g[s->ng - 1], p + 1, n - 1, ++s->g[s->ng - 1].rows);
		else r = str_add(&s->held, p, n) || str_add(&s->held, "\n", 1) || (++s->nheld == HOLD && lay(s, 1));
	} else if (m == 0xFE) {
		int k = 1, *w;
		for (size_t i = 1; i < n; i++) k += p[i] == '\t';
		struct Grid *g = (r = endset(s)) ? NULL : realloc(s->g, (s->ng + 1) * sizeof *g);
		if (g) s->g = g;
		if (!g || !(w = calloc((size_t)k, sizeof *w))) r = 1;
		else g[s->ng++] = (struct Grid){ .n = k, .w = w }, s->in = 1, s->nheld = 0, s->t1 = loop_now(), loop_timer(tick, s, WAIT), r = str_add(&s->held, p, n) || str_add(&s->held, "\n", 1);
	} else if (m == 0xFB) r = endset(s);
	else r = s->in ? str_add(&s->msgs, p, n) || str_add(&s->msgs, "\n", 1) : say(s, p, n);
	if (r) core_msg(E, "sql: out of memory (the results are cut)");
}

/* n bytes of the clients' text at the end of the results
   (core_buf_append: a window on the last line follows); with no run
   going, what is held goes out */
static void put(Sess *s, const char *p, size_t n)
{
	Buf *b = s->res ? core_buf_find(E, s->res) : NULL;
	if (!b) return;
	s->o.n = s->lo = 0, s->at = text_len(b->text), s->line = text_lines(b->text);
	int r = str_lines(&s->part, p, n, take, s);
	if (!r && s->in && !s->q.n) r = endset(s);   /* the run is over */
	else if (!r && s->in == 1 && loop_now() - s->t1 >= WAIT * 1000000LL) r = lay(s, 1);
	if (r) core_msg(E, "sql: out of memory (the results are cut)");
	if (s->o.n) core_buf_append(E, b, s->o.s, s->o.n);
}

static void putz(Sess *s, const char *z) { put(s, z, strlen(z)); }

/* a line of the client's own (net.err, out of memory) as an error */
static void fail(Sess *s, const char *m) { putz(s, NET_ERR), putz(s, m), putz(s, NET_ERR "\n"); }

/* the run is over: what it left held goes out, its status line */
static void end(Sess *s)
{
	size_t rows = 0;
	long long ms = (loop_now() - s->t0) / 1000000;
	int ran = s->q.n != 0;
	s->q.n = 0, put(s, "", 0);
	if (!ran) return;   /* a connection dropped between runs: the last run's line stays */
	for (size_t i = 0; i < s->ng; i++) rows += s->g[i].rows;
	snprintf(s->status, sizeof s->status, "%s \xc2\xb7 %zu row%s \xc2\xb7 %02lld:%02lld:%02lld.%03lld \xc2\xb7 %s / %s",
		s->bad > 1 ? "Query was canceled by user" : s->bad ? "\xe2\x9c\x97 Query completed with errors" : "\xe2\x9c\x93 Query executed successfully",
		rows, rows == 1 ? "" : "s", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60, ms % 1000, s->spec.name, s->spec.db);
}

/* a new run's empty results */
static void clear(Sess *s)
{
	core_buf_fill(E, core_buf_find(E, s->res), "", 0, 0);
	for (size_t i = 0; i < s->ng; i++) free(s->g[i].w);
	snprintf(s->status, sizeof s->status, "Executing query... \xc2\xb7 %s / %s", s->spec.name, s->spec.db);
	s->ng = s->nsp = s->ncut = 0, s->in = s->bad = s->said = s->err = 0, s->part.n = s->held.n = s->msgs.n = s->whole.n = 0;
}

/* the grid line l is in (its names' line or after), or NULL */
static struct Grid *grid(Sess *s, size_t l)
{
	size_t lo = 0, hi = s->ng;
	while (lo < hi) { size_t m = (lo + hi) / 2; if (s->g[m].line <= l) lo = m + 1; else hi = m; }
	return lo && l - s->g[lo - 1].line < s->g[lo - 1].rows + 2 ? &s->g[lo - 1] : NULL;
}

/* line l of b, in memory kept for the next (no allocation once it is long
   enough); its length in *n, NULL when out of memory */
static char *text(const Buf *b, size_t l, size_t *n)
{
	static char *t;
	static size_t tn;
	size_t ls = text_line_start(b->text, l);
	*n = text_line_start(b->text, l + 1) - ls;
	char *q = *n > tn ? realloc(t, *n) : t;
	if (q) t = q, tn = *n > tn ? *n : tn, *n = text_read(b->text, ls, t, *n);
	return q;
}

/* the end of cell i of a row line t[0, n) whose cell starts at k (after
   its | ): its next |, past a value wider than the column (a | inside such
   a value, past the column's width, is taken for the cell's end) */
static size_t over(const struct Grid *g, int i, const char *t, size_t n, size_t k)
{
	size_t c, e = k + fit(t + k, k < n ? n - k : 0, (size_t)g->w[i], &c);
	while (i < g->n - 1 && e + 3 <= n && memcmp(t + e, "\xe2\x94\x82", 3)) e++;
	return i < g->n - 1 ? e : n;
}

/* The results' faces (Buf.faces): the grids' names, rule, row numbers and
   |, then the spans: NULLs, cuts, counts, the messages' rule and errors */
static void rfaces(const Buf *b, size_t a, size_t z, unsigned char *f)
{
	Sess *s = sess(b->nr, 0);
	memset(f, FACE_NONE, z - a);
	if (!s) return;
	size_t l = text_line_of(b->text, a), ls = text_line_start(b->text, l), lo = 0, hi = s->nsp, n;
	struct Grid *g = grid(s, l);
	char *t;
	if (g && l - g->line < 2) memset(f, l == g->line ? FACE_PMENU_SEL : FACE_ORG_TABLE, z - a);
	else if (g && (t = text(b, l, &n))) {   /* a row: its number, then a | and a cell each */
		size_t k = (size_t)g->rn + 1;
		for (size_t j = a; j < z && j < ls + k; j++) f[j - a] = FACE_LINE_NR;
		for (int i = 0; i < g->n && k < n; i++) {
			for (size_t j = ls + k > a ? ls + k : a; j < z && j < ls + k + 3; j++) f[j - a] = FACE_ORG_TABLE;
			k = over(g, i, t, n, k + 4);
		}
	}
	while (lo < hi) { size_t m = (lo + hi) / 2; if (s->sp[m].a + s->sp[m].n <= a) lo = m + 1; else hi = m; }
	for (; lo < s->nsp && s->sp[lo].a < z; lo++)
		for (size_t j = s->sp[lo].a > a ? s->sp[lo].a : a, e = s->sp[lo].a + s->sp[lo].n; j < e && j < z; j++) f[j - a] = s->sp[lo].f;
}

/* Buffer *nr (made when there is none), read-only, current: in its window
   when shown, else in a split.  0 ok, -1 not (msg set). */
static int below(int *nr)
{
	Buf *b = *nr ? core_buf_find(E, *nr) : NULL;
	long w = b ? core_buf_shown(E, *nr) : 0;
	if (w ? core_win_goto(E, 'w', w) : b ? core_split(E, 0, 0) || core_buf_goto(E, *nr) : core_new(E, 0)) return -1;
	*nr = E->buf->nr, E->buf->nomodifiable = 1;
	return 0;
}

/* Enter on a row's cell: its whole value, unescaped, in the one Cell value
   buffer, shown in a split below as the results are (SSMS opens a long
   value in a window of its own).  1 taken, 0 not a cell. */
static int whole(Sess *s)
{
	static const char *const name = "Cell value";
	static int nr;
	size_t l = E->win->line, ls = core_line_start(E, l), n, k, lo = 0, hi = s->ncut;
	struct Grid *g = grid(s, l);
	char *t = g && l - g->line >= 2 ? text(E->buf, l, &n) : NULL;
	if (!t) return 0;
	k = (size_t)g->rn + 5;
	for (int i = 0; i < g->n - 1 && over(g, i, t, n, k) <= E->win->col; i++) k = over(g, i, t, n, k) + 4;
	if (k > n || E->win->col + 4 < k) return 0;   /* on the row number */
	while (lo < hi) { size_t m = (lo + hi) / 2; if (s->cut[m].at < ls + k) lo = m + 1; else hi = m; }
	Str v = {0};
	int i = 0;
	for (size_t x = (size_t)g->rn + 5; over(g, i, t, n, x) < k; i++) x = over(g, i, t, n, x) + 4;
	size_t e = over(g, i, t, n, k);
	if (lo < s->ncut && s->cut[lo].at < ls + e) i = str_add(&v, s->whole.s + s->cut[lo].p, s->cut[lo].n);
	else {
		while (e > k && (t[e - 1] == ' ' || t[e - 1] == '\n')) e--;
		i = str_add(&v, t + k, e - k);
	}
	if (i) core_msg(E, "sql: out of memory");
	if (i || below(&nr)) return str_free(&v), 1;
	v.n = net_unesc(v.s ? v.s : "", v.n);
	core_buf_fill(E, E->buf, v.s ? v.s : "", v.n, 0);
	E->buf->status = name;
	str_free(&v);
	return 1;
}

/* The connections file's line for name, or with name "" the connection of
   the longest "dir" line holding path.  A line is
     name mssql|pg host[:port] user database [trust] [pass=...]
   (user - : the Windows login, SQL Server's from Windows; database - :
   the server alone, its default database), and name/db is name's with
   database db
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
	char f[4096], l[1024], want[256] = "", *db = NULL;
	size_t wn = 0;
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
			} else if (round == 1 && k >= 5 && !strncmp(a[0], want, wn) && !a[0][wn] && (!strcmp(a[1], "mssql") || !strcmp(a[1], "pg"))) {
				*o = (Spec){ .pg = a[1][0] == 'p', .alone = !db && !strcmp(a[4], "-") };
				memcpy(o->name, want, sizeof want);
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
				snprintf(o->db, sizeof o->db, "%s", db ? db : strcmp(a[4], "-") ? a[4] : o->pg ? "postgres" : "");   /* Postgres needs one: libpq's default */
				o->trust = !strcmp(x, "trust");
				memcpy(o->pass, pass, sizeof pass);
				if (!o->pg && !*o->pass && strcmp(o->user, "-")) { core_msg(E, "sql: %s has no pass= (an empty password is a failed login on the server)", o->name); return -1; }   /* user "-": the Windows login */
				if (*o->pass && !open) { core_msg(E, "sql: %s holds a password but others can read it (chmod 600)", f); return -1; }
				return 0;
			}
		}
		if (round == 0 && !*want) break;
		if (round == 0) db = strchr(want, '/'), wn = db ? (size_t)(db++ - want) : strlen(want);
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
	int fd, st, pg;         /* its connection while it reads (-1 none); 0 not read, 1 logging in, 3 reading, 2 read, -1 failed */
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

static char pickfor[256];   /* a server alone just picked: its databases, when read, open the picker again */

static void uncat(Cat *c, int st)   /* its connection closed, its state st */
{
	int mine = st && !strcmp(pickfor, c->name);
	if (mine && st < 0) core_msg(E, "sql: %s: %s", c->name, db_net(&c->db)->err);   /* before the close clears it */
	if (c->fd >= 0) loop_unwatch(c->fd);
	if (c->st == 1 || c->st == 3) db_close(&c->db);
	c->fd = -1, c->st = st;
	if (st != 2) c->cols.n = c->tabs.n = 0;
	if (mine) *pickfor = 0;
	if (mine && st > 0) tools_pick(E, PICK_SQL), core_changed = 1;
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
	/* the server's databases come first, as rows with schema '' (none has
	   that name), for the connections picker (SSMS's Object Explorer) */
	if (!r && was != DB_READY && now == DB_READY && c->st == 1) c->st = 3, r = db_query(&c->db, c->pg
		? "SELECT s, t, c FROM (SELECT TABLE_SCHEMA::text s, TABLE_NAME::text t, COLUMN_NAME::text c, ORDINAL_POSITION::int o FROM INFORMATION_SCHEMA.COLUMNS"
		  " UNION ALL SELECT '', datname, '', 0 FROM pg_database WHERE datallowconn AND NOT datistemplate) x"
		  " WHERE s NOT IN ('pg_catalog', 'information_schema') ORDER BY 1, 2, o"
		: "SELECT s, t, c FROM (SELECT TABLE_SCHEMA s, TABLE_NAME t, COLUMN_NAME c, ORDINAL_POSITION o FROM INFORMATION_SCHEMA.COLUMNS"
		  " UNION ALL SELECT '', name COLLATE DATABASE_DEFAULT, '', 0 FROM sys.databases) x ORDER BY 1, 2, o");   /* hcs's collation is not the server's */
	else if (!r && was != DB_READY && now == DB_READY) {   /* the rows: the lines after the header's, three fields each */
		if (o->n) o->n = net_plain(o->s, o->n);   /* the layout's marks (net.h) */
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
	c->st = 1, c->pg = p->pg;
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
	if (str_add(&s->sql, s->decl.s + b.pre, b.npre) || str_add(&s->sql, s->text.s + (b.a - s->base), b.z - b.a) || str_add(&s->sql, "", 1)) { fail(s, "sql: out of memory"); end(s); return; }
	s->left--;
	if (b.npre) put(s, s->decl.s + b.pre, b.npre - 1), putz(s, "\n");   /* which value ran (Davy, 2026-11-27: a kept value wins over an edited DECLARE) */
	if (db_query(&s->db, s->sql.s)) { fail(s, db_net(&s->db)->err); drop(s); return; }
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
		fail(s, db_net(&s->db)->err);
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
	if (below(&s->res)) return -1;
	E->buf->nonumber = 1;
	E->buf->faces = rfaces, E->buf->status = s->status;
	return core_win_goto(E, 'p', 1);
}

/* name's "type = value" in a "@name\0type = value\0" list, case ignored */
static const char *val(const Str *l, const char *name)
{
	for (size_t i = 0; i < l->n; i += strlen(l->s + i) + 1, i += strlen(l->s + i) + 1)
		if (!strcasecmp(l->s + i, name)) return l->s + i + strlen(l->s + i) + 1;
	return NULL;
}

/* The file's DECLAREs, batch by batch (GO ends one), into d */
static int defaults(const Buf *b, Str *out, Str *d)
{
	SqlBatches f = {0};
	int r = sql_split(b->text, 0, text_len(b->text), &f) < 0;
	for (size_t i = 0; !r && i < f.n; i++) r = sql_vars(b->text, f.b[i].a, f.b[i].z, out, d);
	free(f.b);
	return r;
}

/* Each batch's undeclared @variables DECLAREd before it, on its first
   line (the server's line numbers stay the buffer's), from :Declare; the
   first without one is asked for as Davy would type it (DataGrip's
   parameter prompt), with the file's own DECLARE of it as the default.
   0 ready, 1 asked, -1 out of memory. */
static int params(Sess *s, const Buf *b, size_t a, size_t z)
{
	Str need = {0}, file = {0}, d = {0};
	int r = 0;
	s->decl.n = 0;
	for (size_t i = 0; !r && i < s->q.n; i++) {
		SqlBatch *q = &s->q.b[i];
		r = sql_vars(b->text, q->a, q->z, &need, NULL), q->pre = s->decl.n;
		for (size_t j = 0; !r && j < need.n; j += strlen(need.s + j) + 1) {
			const char *v = val(&s->vars, need.s + j), *f;
			if (v) { r = str_addf(&s->decl, "DECLARE %s %s; ", need.s + j, v); continue; }
			if ((r = defaults(b, &file, &d))) break;
			/* the default only if the command line holds all of it, and no
			   line break (a string's), which would be Enter */
			char k[sizeof E->cmd];
			f = val(&d, need.s + j);
			if (snprintf(k, sizeof k, "Declare %s %s", need.s + j, f && !strpbrk(f, "\r\n") ? f : "") > (int)sizeof k - 4) snprintf(k, sizeof k, "Declare %s ", need.s + j);   /* keys takes a key while 4 bytes are left */
			keys_feed(E, KEY_ESC, 1), keys_feed(E, ':', 1);
			for (size_t i = 0, m = strlen(k), l; i < m; i += l)   /* code points, as typed */
				l = uni_len((const unsigned char *)k, m, i), keys_feed(E, (int)uni_cp((const unsigned char *)k, m, i), i + l < m);
			s->pa = a, s->pz = z, W = s, r = 1;
		}
		q->npre = s->decl.n - q->pre;
	}
	free(need.s), free(file.s), free(d.s);
	return r;
}

static int run(Sess *s, size_t a, size_t z)
{
	Buf *b = core_buf_find(E, s->nr);
	if (s->q.n) { core_msg(E, s->cancel ? "sql: the server has not answered the cancel yet" : "sql: a query is running (Ctrl-C or Alt+Break cancels)"); return -1; }
	if (!s->spec.name[0] && lookup("", b->full, &s->spec)) return -1;
	long bad = 0;
	s->q.n = 0;
	if (s->spec.pg) { SqlBatch one = { .a = a, .z = z, .count = 1 }; if (!(s->q.b = realloc(s->q.b, sizeof one))) return -1; s->q.b[0] = one, s->q.n = 1; }
	else bad = sql_split(b->text, a, z, &s->q);
	if (bad < 0) return -1;
	if (bad) core_msg(E, "sql: line %ld: a GO line sqlcmd refuses (Syntax error near command 'GO')", bad);
	if (!s->q.n) return bad ? -1 : 0;
	int p = s->spec.pg ? 0 : params(s, b, a, z);   /* Postgres has no @variables */
	if (p) { if (p < 0) core_msg(E, "sql: out of memory"); s->q.n = 0; return p < 0 ? -1 : 0; }
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
	if ((s->fd = dial(&s->db, &s->spec, wake, s)) < 0) { fail(s, db_net(&s->db)->err); db_close(&s->db), end(s); return -1; }
	return 0;
}

int sql_key(Editor *e, int key)
{
	W = NULL, *pickfor = 0, E = e;
	if (key != KEY_F5 && key != KEY_BREAK && key != KEY_CTRL_C && key != KEY_ENTER) return 0;
	Sess *s = sess(e->buf->nr, 0);
	if (key == KEY_ENTER) return s && s->res == e->buf->nr && e->mode == MODE_NORMAL && !e->ks.pending && whole(s);   /* as make_key */
	if (key != KEY_F5) {
		/* Ctrl-C cancels as Alt+Break, which a terminal cannot send (Davy,
		   2026-11-24); with no run to cancel it stays Vim's */
		if (!s || !s->q.n || s->cancel) return s && key == KEY_BREAK;
		s->bad = 2;
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
	if (!name || !*name) return tools_pick(e, PICK_SQL);
	char nm[300];
	snprintf(nm, sizeof nm, "%.*s", (int)strcspn(name, " "), name);   /* a picker item's mark cut */
	name = nm;
	if (!s) { core_msg(e, "sql: out of memory"); return -1; }
	Spec c;
	if (lookup(name, NULL, &c)) return -1;
	Cat *k = cat(name, 0);
	if (k) uncat(k, 0);   /* read again at its next login: the file may name another server now */
	drop(s);
	s->spec = c;
	core_msg(e, "sql: %s (%s)%s", c.name, c.host, c.alone ? ": its databases come in the picker" : "");
	if (c.alone) snprintf(pickfor, sizeof pickfor, "%s", c.name), catread(&c);
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

/* b's session with its connection, or NULL; the folder's looked up once,
   with no message (completion or the status line asked, not Davy) */
static Sess *conn(const Buf *b)
{
	Sess *s = E && is_sql(b) ? sess(b->nr, 1) : NULL;   /* E: lookup's messages go there; the first key sets it (sql_key) */
	char m[sizeof E->msg];
	Spec c;
	if (s && !s->spec.name[0] && !s->looked) {
		memcpy(m, E->msg, sizeof m);
		if (!lookup("", b->full, &c)) s->spec = c;
		memcpy(E->msg, m, sizeof m);
		s->looked = 1;
	}
	return s && s->spec.name[0] ? s : NULL;
}

static int up(const Sess *s) { return s->fd >= 0 && db_state(&s->db) >= DB_READY; }

/* name, and " ✓" while live: the picker's and the status line's one mark */
static const char *marked(const char *name, int live)
{
	static char m[300];
	return live ? (snprintf(m, sizeof m, "%s \xe2\x9c\x93", name), m) : name;
}

const char *sql_status(const Buf *b, int *live)
{
	Sess *s = conn(b);
	return s ? marked(s->spec.name, *live = up(s)) : NULL;
}

/* The picker's items as name\0, rebuilt at item 0: each connection in the
   file, each followed by its server's databases once a catalog read them */
static Str items;

const char *sql_pick(Editor *e, int i, int *live)
{
	static size_t at, n;   /* item n is at items.s + at: the picker asks in order */
	E = e;
	if (!i) {
		char f[4096], l[1024], a[2][256], d[600];
		file_stdpath(f, sizeof f, 2, "/sql");
		FILE *fp = fopen(f, "r");
		items.n = at = n = 0;
		while (fp && fgets(l, sizeof l, fp))
			if (sscanf(l, "%255s %255s", a[0], a[1]) == 2 && (!strcmp(a[1], "mssql") || !strcmp(a[1], "pg"))) {
				size_t an = strlen(a[0]);
				Cat *k = C;
				while (k && (k->st != 2 || strncmp(k->name, a[0], an) || (k->name[an] && k->name[an] != '/'))) k = k->next;
				if (str_add(&items, a[0], an + 1)) break;
				const size_t *o = k ? (const void *)k->tabs.s : NULL;
				for (size_t j = 0, nt = k ? k->tabs.n / sizeof *o : 0; j < nt && !k->cols.s[o[j]]; j++)
					if (str_add(&items, d, (size_t)snprintf(d, sizeof d, "%s/%s", a[0], k->cols.s + o[j] + 1) + 1)) break;
			}
		if (fp) fclose(fp);
	}
	for (; n < (size_t)i && at < items.n; n++) at += strlen(items.s + at) + 1;
	if (n != (size_t)i || at >= items.n) return NULL;
	*live = 0;
	for (size_t j = 0; j < nS && !*live; j++) *live = up(S[j]) && !strcmp(S[j]->spec.name, items.s + at);
	return marked(items.s + at, *live);
}

int sql_catalog(Editor *e, const Buf *b, const char *table, void (*fn)(void *ctx, const char *schema, const char *table, const char *column), void *ctx)
{
	E = e;
	Sess *s = conn(b);
	if (!s) return 0;
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
		if (!*p) continue;   /* a database */
		if (!table) fn(ctx, p, t, NULL), n++;
		else if (!strcasecmp(t, table)) for (const char *q; p < z; p = q, n++) q = row(p, &t, &col), fn(ctx, p, t, col);
	}
	return n;
}

int sql_declare(Editor *e, const char *arg)
{
	E = e;
	Sess *s = sess(e->buf->nr, is_sql(e->buf));
	if (!s) return core_msg(e, "sql: :Declare is for a .sql buffer"), -1;
	if (!*arg) {   /* the list */
		char m[sizeof e->msg] = "sql:";
		for (size_t i = 0, n; i < s->vars.n; i += n + strlen(s->vars.s + i + n) + 1)
			n = strlen(s->vars.s + i) + 1, snprintf(m + strlen(m), sizeof m - strlen(m), " %s %s;", s->vars.s + i, s->vars.s + i + n);
		return core_msg(e, "%s", s->vars.n ? m : "sql: nothing declared"), 0;
	}
	size_t n = strcspn(arg, " \t");
	if (*arg != '@' || n < 2 || n > 128) return core_msg(e, "sql: :Declare @name type = value"), -1;
	char name[130];
	memcpy(name, arg, n), name[n] = 0;
	const char *v = val(&s->vars, name);
	if (v) {   /* forgotten, or replaced */
		char *p = (char *)v - n - 1;
		size_t len = n + 1 + strlen(v) + 1;
		memmove(p, p + len, s->vars.n - (size_t)(p - s->vars.s) - len), s->vars.n -= len;
	}
	const char *rest = arg + n + strspn(arg + n, " \t");
	if (!*rest) return 0;
	if (str_add(&s->vars, name, n + 1) || str_add(&s->vars, rest, strlen(rest) + 1)) return core_msg(e, "sql: out of memory"), -1;
	if (W != s) return 0;
	W = NULL;
	size_t len = text_len(core_buf_find(e, s->nr)->text);   /* no key came between, but a reload from disk may have */
	return run(s, s->pa < len ? s->pa : len, s->pz < len ? s->pz : len);   /* the F5 whose prompt this answers */
}
