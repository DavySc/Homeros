/* Loading the snapshot (snap.h).  One allocation holds the unescaped
   strings; records point into it. */
#include <stdlib.h>
#include <string.h>
#include "snap.h"

static int low(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int up(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

/* How names compare (snap.h): d is the dialect, or D_CI for T-SQL under a
   case-insensitive collation.  The byte of an identifier as it compares: */
enum { D_CI = -1 };
static int fold(int d, int c, int quoted)
{
	if (d == D_CI) return low(c);
	if (quoted || d == D_TSQL) return c;
	return d == D_PG ? low(c) : up(c);
}
/* catalog names are stored as the server spells them: exact */
static int same(int d, const char *cat, Name n)
{
	size_t i = 0;
	for (; i < n.n; i++)
		if (!cat[i] || fold(d, (unsigned char)cat[i], 1) != fold(d, (unsigned char)n.s[i], n.quoted)) return 0;
	return !cat[i];
}
static int mode(const Snap *s) { return s->ci ? D_CI : s->dialect; }
static uint32_t hash(int d, uint32_t h, const char *s, size_t n, int quoted)
{
	for (size_t i = 0; i < n; i++) h = (h ^ (uint32_t)fold(d, (unsigned char)s[i], quoted)) * 16777619u;
	return h;
}
/* objects hash by name only, columns by their object and name: quoted
   (exact) catalog spelling hashes as the folded identifier does */
static uint32_t key_o(int d, Name n) { return hash(d, 2166136261u, n.s, n.n, n.quoted); }
static uint32_t key_c(int d, size_t o, Name n) { return hash(d, 2166136261u ^ (uint32_t)(o + 1) * 2654435761u, n.s, n.n, n.quoted); }
static Name cat(const char *s) { return (Name){s, strlen(s), 1}; }

static void put(Snap *s, uint32_t k, uint32_t v)
{
	size_t i = k & (s->nh - 1);
	while (s->h[i]) i = (i + 1) & (s->nh - 1);
	s->h[i] = v;
}

/* the fields of one line, unescaped in place; returns their number */
static int fields(char *p, char *end, char **f, int max)
{
	int n = 0;
	char *w = p;
	f[n++] = w;
	while (p < end) {
		if (*p == '\t') { *w++ = 0; p++; if (n == max) return n; f[n++] = w; continue; }
		if (*p == '\\' && p + 1 < end) {
			p++;
			*w++ = *p == 't' ? '\t' : *p == 'n' ? '\n' : *p == 'r' ? '\r' : *p;
			p++;
			continue;
		}
		*w++ = *p++;
	}
	*w = 0;
	return n;
}

static long by_id(const Snap *s, int64_t id)
{
	size_t a = 0, z = s->no;
	while (a < z) {
		size_t m = a + (z - a) / 2;
		if (s->o[m].id < id) a = m + 1; else z = m;
	}
	return a < s->no && s->o[a].id == id ? (long)a : -1;
}
static int cmp_o(const void *a, const void *b) { int64_t x = ((const SnapObj *)a)->id, y = ((const SnapObj *)b)->id; return (x > y) - (x < y); }
static int cmp_c(const void *a, const void *b)
{
	const SnapCol *x = a, *y = b;
	if (x->obj != y->obj) return x->obj < y->obj ? -1 : 1;
	return (x->id > y->id) - (x->id < y->id);
}

static long col_id(const Snap *s, long o, int64_t id)
{
	for (uint32_t j = 0; o >= 0 && j < s->o[o].ncol; j++)
		if (s->c[s->o[o].col + j].id == id) return (long)(s->o[o].col + j);
	return -1;
}

Snap *snap_load(const char *src, size_t n, int dialect, size_t *bad)
{
	Snap *s = calloc(1, sizeof *s);
	size_t lines = 1, nbad = 0, nm = 0;
	if (!s) return NULL;
	s->dialect = dialect;
	for (size_t i = 0; i < n; i++) lines += src[i] == '\n';
	s->mem = malloc(n + 1);
	s->o = malloc(lines * sizeof *s->o);
	s->c = malloc(lines * sizeof *s->c);
	s->k = malloc(lines * sizeof *s->k);
	/* ids as the server gave them, until the objects are sorted */
	int64_t *cobj = malloc(lines * sizeof *cobj), (*kid)[6] = malloc(lines * sizeof *kid), *mid = malloc(lines * sizeof *mid);
	char **mtext = malloc(lines * sizeof *mtext);
	if (!s->mem || !s->o || !s->c || !s->k || !cobj || !kid || !mid || !mtext) goto fail;
	memcpy(s->mem, src, n);
	s->mem[n] = 0;
	for (char *p = s->mem, *end = s->mem + n; p < end;) {
		char *e = memchr(p, '\n', (size_t)(end - p)), *f[8];
		if (!e) e = end;
		char *le = e > p && e[-1] == '\r' ? e - 1 : e;   /* sqlcmd on Windows */
		int nf = le - p >= 2 && p[1] == '\t' ? fields(p, le, f, 8) : 0;
		if (nf == 5 && !strcmp(f[0], "O"))
			s->o[s->no++] = (SnapObj){f[2], f[3], f[4], NULL, strtoll(f[1], 0, 10), 0, 0};
		else if (nf == 7 && !strcmp(f[0], "C")) {
			cobj[s->nc] = strtoll(f[1], 0, 10);
			s->c[s->nc++] = (SnapCol){f[3], f[4], 0, (int32_t)strtol(f[2], 0, 10), (int32_t)strtol(f[5], 0, 10), (uint8_t)(*f[6] == '1')};
		} else if (nf == 7 && !strcmp(f[0], "K")) {
			for (int j = 0; j < 6; j++) kid[s->nk][j] = strtoll(f[1 + j], 0, 10);
			s->nk++;
		} else if (nf == 3 && !strcmp(f[0], "D"))
			s->ci = dialect == D_TSQL && strstr(f[1], "_CI") != NULL, s->dflt = f[2];
		else if (nf == 3 && !strcmp(f[0], "M"))
			mid[nm] = strtoll(f[1], 0, 10), mtext[nm++] = f[2];
		else if (nf) nbad++;
		p = e + 1;
	}
	qsort(s->o, s->no, sizeof *s->o, cmp_o);
	size_t w = 0;
	for (size_t i = 0; i < s->nc; i++) {
		long o = by_id(s, cobj[i]);
		if (o < 0) { nbad++; continue; }
		s->c[w] = s->c[i], s->c[w++].obj = (uint32_t)o;
	}
	s->nc = w;
	qsort(s->c, s->nc, sizeof *s->c, cmp_c);
	for (size_t i = s->nc; i-- > 0;) s->o[s->c[i].obj].col = (uint32_t)i, s->o[s->c[i].obj].ncol++;
	for (size_t i = 0; i < nm; i++) {
		long o = by_id(s, mid[i]);
		if (o < 0) nbad++; else s->o[o].text = mtext[i];
	}
	w = 0;
	for (size_t i = 0; i < s->nk; i++) {
		long a = by_id(s, kid[i][2]), b = by_id(s, kid[i][4]), ca = col_id(s, a, kid[i][3]), cb = col_id(s, b, kid[i][5]);
		if (ca < 0 || cb < 0) { nbad++; continue; }
		s->k[w++] = (SnapFk){(uint32_t)a, (uint32_t)ca, (uint32_t)b, (uint32_t)cb};
	}
	s->nk = w;
	for (s->nh = 16; s->nh < 2 * (s->no + s->nc); s->nh *= 2);
	s->h = calloc(s->nh, sizeof *s->h);
	if (!s->h) goto fail;
	for (size_t i = 0; i < s->no; i++) put(s, key_o(mode(s), cat(s->o[i].name)), (uint32_t)(i + 1));
	for (size_t i = 0; i < s->nc; i++) put(s, key_c(mode(s), s->c[i].obj, cat(s->c[i].name)), (uint32_t)(s->no + i + 1));
	free(cobj), free(kid), free(mid), free(mtext);
	if (bad) *bad = nbad;
	return s;
fail:
	free(cobj), free(kid), free(mid), free(mtext);
	snap_free(s);
	return NULL;
}

void snap_free(Snap *s)
{
	if (!s) return;
	free(s->mem), free(s->o), free(s->c), free(s->k), free(s->h), free(s);
}

long snap_object(const Snap *s, Name schema, Name name)
{
	int d = mode(s);
	const char *dflt = s->dflt;
	long hit = -1, other = -1;
	int many = 0;
	for (size_t i = key_o(d, name) & (s->nh - 1); s->h[i]; i = (i + 1) & (s->nh - 1)) {
		size_t v = s->h[i] - 1;
		if (v >= s->no || !same(d, s->o[v].name, name)) continue;
		if (schema.s) { if (same(d, s->o[v].schema, schema)) return (long)v; continue; }
		if (dflt && !strcmp(s->o[v].schema, dflt)) hit = (long)v;
		else many += other >= 0, other = (long)v;
	}
	return hit >= 0 ? hit : many ? -2 : other;
}

long snap_column(const Snap *s, size_t o, Name col)
{
	int d = mode(s);
	for (size_t i = key_c(d, o, col) & (s->nh - 1); s->h[i]; i = (i + 1) & (s->nh - 1)) {
		size_t v = s->h[i] - 1;
		if (v >= s->no && s->c[v - s->no].obj == o && same(d, s->c[v - s->no].name, col)) return (long)(v - s->no);
	}
	return -1;
}

Name snap_name(const char *s, size_t n, char *buf)
{
	if (n < 2 || (s[0] != '[' && s[0] != '"')) return (Name){s, n, 0};
	char close = s[0] == '[' ? ']' : '"';
	size_t w = 0;
	for (size_t i = 1; i + 1 < n; i++) {
		buf[w++] = s[i];
		if (s[i] == close) i++;
	}
	return (Name){buf, w, 1};
}
