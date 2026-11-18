/* magit's sections over lines, and git's output washed into them (magit
   4402e97: magit-section.el, magit-diff.el magit-diff-wash-diff, magit-git.el
   magit-format-ref-labels, magit-status.el).  Each function names the magit
   function whose output it copies. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sec.h"

int str_add(Str *s, const char *p, size_t n)
{
	if (s->n + n + 1 > s->cap) {
		size_t c = s->cap ? s->cap : 256;
		while (c < s->n + n + 1) c *= 2;
		char *q = realloc(s->s, c);
		if (!q) return -1;
		s->s = q; s->cap = c;
	}
	if (n) memcpy(s->s + s->n, p, n);
	s->n += n;
	s->s[s->n] = 0;
	return 0;
}

int str_addz(Str *s, const char *p) { return str_add(s, p, strlen(p)); }
void str_free(Str *s) { free(s->s); *s = (Str){ 0 }; }

void view_clear(View *v)
{
	v->pool.n = 0;
	v->nln = 0;
	v->nsec = 0;
	v->nopen = 0;
	str_add(&v->pool, "", 1);   /* offset 0 is "no value" */
}

void view_free(View *v)
{
	str_free(&v->pool);
	str_free(&v->text);
	free(v->ln); free(v->sec); free(v->vis);
	*v = (View){ .cache = v->cache };
}

static int grow(void **p, size_t *cap, size_t need, size_t size)
{
	if (need <= *cap) return 0;
	size_t c = *cap ? *cap * 2 : 64;
	while (c < need) c *= 2;
	void *q = realloc(*p, c * size);
	if (!q) return -1;
	*p = q; *cap = c;
	return 0;
}

const char *sec_value(const View *v, int i) { return v->sec[i].value ? v->pool.s + v->sec[i].value : ""; }

/* Types as text in idents, so a cache survives a rebuild */
int sec_ident(const View *v, int i, Str *out)
{
	out->n = 0;
	for (; i >= 0; i = v->sec[i].parent) {
		char t = (char)('A' + v->sec[i].type);
		if (str_add(out, &t, 1) || str_addz(out, sec_value(v, i)) || str_add(out, "\x1d", 1)) return -1;
	}
	return 0;
}

/* The cached entry for ident id: a pointer to its flag byte, or NULL */
static char *cache_find(Cache *c, const Str *id)
{
	if (!c) return NULL;
	for (size_t o = 0; o < c->s.n; ) {
		size_t len = strlen(c->s.s + o);
		if (len == id->n + 2 && !memcmp(c->s.s + o, id->s, id->n)) return c->s.s + o + id->n + 1;
		o += len + 1;
	}
	return NULL;
}

int sec_cache_set(View *v, int i)
{
	Str id = { 0 };
	if (!v->cache || sec_ident(v, i, &id)) { str_free(&id); return !v->cache ? 0 : -1; }
	char *f = cache_find(v->cache, &id), h = v->sec[i].hidden ? '1' : '0';
	int r = 0;
	if (f) *f = h;
	else r = str_add(&v->cache->s, id.s, id.n) || str_add(&v->cache->s, "\x1e", 1)
		|| str_add(&v->cache->s, &h, 1) || str_add(&v->cache->s, "", 1) ? -1 : 0;
	str_free(&id);
	return r;
}

int sec_open(View *v, int type, const char *value, size_t vlen, int hide)
{
	if (v->nopen == (int)(sizeof v->open / sizeof *v->open)) return -1;
	if (grow((void **)&v->sec, &v->csec, (size_t)v->nsec + 1, sizeof *v->sec)) return -1;
	size_t off = 0;
	if (value) {
		if (!v->pool.n && str_add(&v->pool, "", 1)) return -1;   /* offset 0 is "none" */
		off = v->pool.n;
		if (str_add(&v->pool, value, vlen) || str_add(&v->pool, "", 1)) return -1;
	}
	int i = v->nsec++;
	v->sec[i] = (Sec){ .type = type, .parent = v->nopen ? v->open[v->nopen - 1] : -1,
		.start = v->nln, .end = v->nln, .value = off, .hidden = hide, .after = i + 1 };
	v->open[v->nopen++] = i;
	/* magit-section-cached-visibility */
	if (v->cache && v->cache->s.n) {
		Str id = { 0 };
		if (sec_ident(v, i, &id)) { str_free(&id); return -1; }
		char *f = cache_find(v->cache, &id);
		if (f) v->sec[i].hidden = *f == '1';
		str_free(&id);
	}
	return i;
}

int sec_line(View *v, const char *s, size_t n)
{
	if (grow((void **)&v->ln, &v->cln, v->nln + 1, sizeof *v->ln)) return -1;
	size_t off = v->pool.n;
	if (str_add(&v->pool, s, n)) return -1;
	/* a newline in a file's name would end the line: shown as NUL (^@),
	   as Vim swaps the two in memory */
	for (char *p = v->pool.s + off; (p = memchr(p, '\n', (size_t)(v->pool.s + off + n - p))); ) *p = 0;
	v->ln[v->nln++] = (Line){ off, n, v->nopen ? v->open[v->nopen - 1] : -1 };
	return 0;
}

static int sec_linez(View *v, const char *s) { return sec_line(v, s, strlen(s)); }

void sec_close(View *v)
{
	int i = v->open[--v->nopen];
	v->sec[i].end = v->nln;
	v->sec[i].after = v->nsec;
}

int sec_has_body(const View *v, int i) { return v->sec[i].end > v->sec[i].start + 1; }
int sec_at(const View *v, size_t line) { return line < v->nln ? v->ln[line].sec : -1; }

size_t sec_vis_of(const View *v, size_t l)
{
	size_t a = 0, z = v->nvis;      /* the last visible line <= l */
	while (z - a > 1) {
		size_t m = a + (z - a) / 2;
		if (v->vis[m] <= l) a = m; else z = m;
	}
	return a;
}

/* Replace line l's text (a heading's count is known only after its body) */
static int line_set(View *v, size_t l, const char *s, size_t n)
{
	size_t off = v->pool.n;
	if (str_add(&v->pool, s, n)) return -1;
	v->ln[l].off = off; v->ln[l].len = n;
	return 0;
}

int sec_child_count(View *v, int i)
{
	Line *h = &v->ln[v->sec[i].start];
	int n = 0;
	for (int k = i + 1; k < v->sec[i].after; k = v->sec[k].after) n++;
	if (!n || !h->len || v->pool.s[h->off + h->len - 1] != ':') return 0;
	Str t = { 0 };
	char num[32];
	int r = str_add(&t, v->pool.s + h->off, h->len - 1) || str_addz(&t, " (")
		|| str_add(&t, num, (size_t)snprintf(num, sizeof num, "%d", n)) || str_addz(&t, ")")
		|| line_set(v, v->sec[i].start, t.s, t.n) ? -1 : 0;
	str_free(&t);
	return r;
}

/* A line is hidden when a hidden section holds it below its heading
   (magit-section-hide makes the content invisible, not the heading). */
int sec_render(View *v)
{
	v->text.n = 0;
	v->nvis = 0;
	for (size_t l = 0; l < v->nln; l++) {
		int shown = 1;
		for (int i = v->ln[l].sec; i >= 0 && shown; i = v->sec[i].parent)
			if (v->sec[i].hidden && l > v->sec[i].start) shown = 0;
		if (!shown) continue;
		if (grow((void **)&v->vis, &v->cvis, v->nvis + 1, sizeof *v->vis)) return -1;
		v->vis[v->nvis++] = l;
		if (str_add(&v->text, v->pool.s + v->ln[l].off, v->ln[l].len)) return -1;
		int i = v->ln[l].sec;
		/* magit-section-visibility-indicators in a terminal: "…" */
		if (i >= 0 && v->sec[i].start == l && v->sec[i].hidden && sec_has_body(v, i)
		    && str_addz(&v->text, "\xe2\x80\xa6")) return -1;
		if (str_add(&v->text, "\n", 1)) return -1;
	}
	return 0;
}

/* --- git status --porcelain=v2 -z --branch ------------------------------ */

static char *dupn(const char *s, size_t n)
{
	char *d = malloc(n + 1);
	if (d) { memcpy(d, s, n); d[n] = 0; }
	return d;
}

void branch_free(Branch *b)
{
	free(b->head); free(b->oid); free(b->upstream);
	str_free(&b->untracked); str_free(&b->conflicts);
	*b = (Branch){ 0 };
}

static int prefix(const char *s, size_t n, const char *p)
{
	size_t k = strlen(p);
	return n >= k && !memcmp(s, p, k);
}

int parse_status(const char *s, size_t n, Branch *b)
{
	branch_free(b);
	for (size_t i = 0; i < n; ) {
		const char *e = memchr(s + i, 0, n - i);
		size_t len = e ? (size_t)(e - (s + i)) : n - i;
		const char *p = s + i;
		i += len + 1;
		if (prefix(p, len, "# branch.oid ")) {
			free(b->oid);
			if (!(b->oid = dupn(p + 13, len - 13))) return -1;
		} else if (prefix(p, len, "# branch.head ")) {
			free(b->head);
			b->head = NULL;
			if (!prefix(p + 14, len - 14, "(detached)") && !(b->head = dupn(p + 14, len - 14))) return -1;
		} else if (prefix(p, len, "# branch.upstream ")) {
			free(b->upstream);
			if (!(b->upstream = dupn(p + 18, len - 18))) return -1;
		} else if (prefix(p, len, "# branch.ab ")) {
			char t[64], *q;
			snprintf(t, sizeof t, "%.*s", (int)(len - 12 < 63 ? len - 12 : 63), p + 12);
			b->ahead = strtol(t, &q, 10);
			b->behind = strtol(q, NULL, 10) != 0;	/* only whether: "-N" can be LONG_MIN */
			b->ab = 1;
		} else if (prefix(p, len, "? ")) {
			if (str_add(&b->untracked, p + 2, len - 2) || str_add(&b->untracked, "", 1)) return -1;
			b->nuntracked++;
		} else if (prefix(p, len, "u ") && len > 4) {
			/* u XY sub m1 m2 m3 mW h1 h2 h3 path: the path follows the 10th space */
			const char *q = p;
			for (int f = 0; f < 10 && q; f++) q = memchr(q + 1, ' ', len - (size_t)(q + 1 - p));
			if (q && (str_add(&b->conflicts, p + 2, 2) || str_add(&b->conflicts, " ", 1)
			          || str_add(&b->conflicts, q + 1, len - (size_t)(q + 1 - p)) || str_add(&b->conflicts, "", 1))) return -1;
		} else if (prefix(p, len, "2 ")) {
			/* a rename's original path is the next NUL-terminated field */
			const char *o = memchr(s + i, 0, i < n ? n - i : 0);
			i = o ? (size_t)(o - s) + 1 : n;
		}
	}
	return 0;
}

/* magit-insert-files 'untracked: hidden by default, at most 100 listed */
int build_untracked(View *v, const Branch *b)
{
	if (!b->nuntracked) return 0;
	char h[64];
	if (sec_open(v, T_UNTRACKED, NULL, 0, 1) < 0
	    || sec_line(v, h, (size_t)snprintf(h, sizeof h, "Untracked files (%d)", b->nuntracked))) return -1;
	const char *p = b->untracked.s;
	for (int k = 0; k < b->nuntracked && k < 100; k++, p += strlen(p) + 1)
		if (sec_open(v, T_FILE, p, strlen(p), 0) < 0 || sec_linez(v, p)) return -1;
		else sec_close(v);
	if (b->nuntracked > 100) {
		if (sec_open(v, T_INFO, NULL, 0, 0) < 0
		    || sec_line(v, h, (size_t)snprintf(h, sizeof h, "%d files not listed", b->nuntracked - 100))) return -1;
		sec_close(v);
	}
	if (sec_line(v, "", 0)) return -1;
	sec_close(v);
	return 0;
}

/* --- git diff --------------------------------------------------------------- */

/* magit-decode-git-path: git quotes a path with special bytes C-style */
static int unquote(const char *s, size_t n, Str *out)
{
	out->n = 0;
	if (n < 2 || s[0] != '"' || s[n - 1] != '"') return str_add(out, s, n);
	for (size_t i = 1; i + 1 < n; i++) {
		char c = s[i];
		if (c == '\\' && i + 2 < n) {
			c = s[++i];
			if (c >= '0' && c <= '7' && i + 2 < n) {
				int x = (c - '0') * 64 + (s[i + 1] - '0') * 8 + (s[i + 2] - '0');
				i += 2;
				c = (char)x;
			} else c = c == 'n' ? '\n' : c == 't' ? '\t' : c == 'a' ? '\a' : c == 'b' ? '\b'
				: c == 'f' ? '\f' : c == 'r' ? '\r' : c == 'v' ? '\v' : c;
		}
		if (str_add(out, &c, 1)) return -1;
	}
	return 0;
}

typedef struct { const char *p; size_t n; } Seg;

static Seg next_line(const char *s, size_t n, size_t *i)
{
	Seg l = { s + *i, 0 };
	const char *e = memchr(s + *i, '\n', n - *i);
	l.n = e ? (size_t)(e - l.p) : n - *i;
	*i += l.n + (e != NULL);
	return l;
}

static int is_headline(Seg l)
{
	return prefix(l.p, l.n, "diff ") || prefix(l.p, l.n, "@@") || prefix(l.p, l.n, "* Unmerged path ")
		|| prefix(l.p, l.n, "Submodule");
}

/* magit-diff-wash-hunk: a heading "@@ ... @@ about", then the lines that
   start with - + space or backslash */
static int wash_hunks(View *v, const char *s, size_t n, size_t *i)
{
	while (*i < n) {
		size_t j = *i;
		Seg h = next_line(s, n, &j);
		if (!prefix(h.p, h.n, "@@")) return 0;
		*i = j;
		if (sec_open(v, T_HUNK, h.p, h.n, 0) < 0 || sec_line(v, h.p, h.n)) return -1;
		while (*i < n && (s[*i] == '-' || s[*i] == '+' || s[*i] == ' ' || s[*i] == '\\')) {
			Seg l = next_line(s, n, i);
			if (sec_line(v, l.p, l.n)) return -1;
		}
		sec_close(v);
	}
	return 0;
}

static const char *conflict(const Branch *b, const char *path)
{
	if (b && b->conflicts.s)
		for (const char *p = b->conflicts.s; p < b->conflicts.s + b->conflicts.n; p += strlen(p) + 1)
			if (!strcmp(p + 3, path)) return p;
	return NULL;
}

/* magit-diff-insert-file-section: "%-11s" status, then the file (or
   "orig -> file"), then the binary and conflict notes */
static int file_heading(Str *t, const char *status, const Str *file, const Str *orig, int binary)
{
	char st[16];
	snprintf(st, sizeof st, "%-11s", status);
	if (str_addz(t, st)) return -1;
	if (orig->n && (str_add(t, orig->s, orig->n) || str_addz(t, " -> "))) return -1;
	if (str_add(t, file->s, file->n)) return -1;
	return binary ? str_addz(t, " (binary)") : 0;
}

int parse_diff(View *v, const char *s, size_t n, int file_hidden, int cached, const Branch *b)
{
	int files = 0;
	Str file = { 0 }, orig = { 0 }, hdr = { 0 }, t = { 0 }, modes = { 0 }, rename = { 0 };
	size_t i = 0;
	int r = -1;
	while (i < n) {
		Seg l = next_line(s, n, &i);
		if (prefix(l.p, l.n, "* Unmerged path ")) {
			/* magit lists an unmerged file under Staged changes, with who
			   did what (measured, the conflict trace), not under Unstaged */
			if (!cached) continue;
			if (unquote(l.p + 16, l.n - 16, &file)) goto out;
			const char *c = conflict(b, file.s ? file.s : "");
			const char *note = !c ? "" : !strncmp(c, "DD", 2) ? " (both deleted)" : !strncmp(c, "DU", 2) ? " (deleted by us)"
				: !strncmp(c, "UD", 2) ? " (deleted by them)" : !strncmp(c, "AA", 2) ? " (both added)"
				: !strncmp(c, "AU", 2) ? " (added by us)" : !strncmp(c, "UA", 2) ? " (added by them)" : "";
			t.n = 0;
			if (str_addz(&t, "unmerged   ") || str_add(&t, file.s, file.n) || str_addz(&t, note)
			    || sec_open(v, T_FILE, file.s, file.n, 0) < 0 || sec_line(v, t.s, t.n)) goto out;
			sec_close(v);
			files++;
			continue;
		}
		if (!prefix(l.p, l.n, "diff ")) continue;   /* what magit does not wash */
		const char *status = prefix(l.p, l.n, "diff --git ") ? "modified" : "unmerged";
		int binary = 0;
		orig.n = 0; modes.n = 0; rename.n = 0; hdr.n = 0;
		/* "diff --git A A" (no prefix): the name is the line's second half;
		   ---/+++ below say it exactly when present */
		int gitd = prefix(l.p, l.n, "diff --git ");
		size_t at = gitd ? 11 : prefix(l.p, l.n, "diff --cc ") ? 10 : prefix(l.p, l.n, "diff --combined ") ? 16 : 0;
		if (!at) continue;                          /* magit-diff-headline-re's three */
		const char *name = l.p + at;
		size_t nl = l.n - (size_t)(name - l.p);
		if (gitd && nl % 2 == 1 && !memcmp(name, name + nl / 2 + 1, nl / 2)) nl /= 2;
		if (unquote(name, nl, &file) || str_add(&hdr, l.p, l.n) || str_add(&hdr, "\n", 1)) goto out;
		while (i < n) {
			size_t j = i;
			Seg h = next_line(s, n, &j);
			if (is_headline(h)) break;
			i = j;
			int keep = 1;
			size_t i2 = i;
			Seg h2 = next_line(s, n, &i2);       /* the pair's second line */
			if (prefix(h.p, h.n, "old mode ") && prefix(h2.p, h2.n, "new mode ")) {
				i = i2;
				if (str_add(&modes, h.p, h.n) || str_add(&modes, "\n", 1) || str_add(&modes, h2.p, h2.n)) goto out;
				keep = 0;
			} else if (prefix(h.p, h.n, "deleted file ")) status = "deleted";
			else if (prefix(h.p, h.n, "new file ")) status = "new file";
			else if (prefix(h.p, h.n, "rename from ") && prefix(h2.p, h2.n, "rename to ")) {
				i = i2;
				if (unquote(h.p + 12, h.n - 12, &orig) || unquote(h2.p + 10, h2.n - 10, &file)
				    || str_add(&rename, h.p, h.n) || str_add(&rename, "\n", 1) || str_add(&rename, h2.p, h2.n)) goto out;
				status = "renamed";
				keep = 0;
			} else if (prefix(h.p, h.n, "copy from ") && prefix(h2.p, h2.n, "copy to ")) {
				i = i2;
				if (unquote(h.p + 10, h.n - 10, &orig) || unquote(h2.p + 8, h2.n - 8, &file)) goto out;
				status = "copied";
			} else if (prefix(h.p, h.n, "--- ") || prefix(h.p, h.n, "+++ ")) {
				size_t k = h.n - 4;
				if (k && h.p[4 + k - 1] == '\t') k--;   /* git adds a tab after a name with a space */
				if (!(k == 9 && !memcmp(h.p + 4, "/dev/null", 9))
				    && unquote(h.p + 4, k, h.p[0] == '-' ? &orig : &file)) goto out;
			} else if (prefix(h.p, h.n, "Binary files ")) binary = 1;
			if (keep && (str_add(&hdr, h.p, h.n) || str_add(&hdr, "\n", 1))) goto out;
		}
		if (orig.n == file.n && (!file.n || !memcmp(orig.s, file.s, file.n))) orig.n = 0;
		t.n = 0;
		if (file_heading(&t, status, &file, &orig, binary)) goto out;
		int f = sec_open(v, T_FILE, file.s, file.n, file_hidden || !strcmp(status, "deleted"));
		if (f < 0 || sec_line(v, t.s, t.n)) goto out;
		/* the file's diff header, for the patches that stage its hunks */
		v->sec[f].aux = v->pool.n;
		if (str_add(&v->pool, hdr.s, hdr.n) || str_add(&v->pool, "", 1)) goto out;
		/* magit's chmod and rename pseudo-hunks */
		Str *ps[2] = { &modes, &rename };
		for (int k = 0; k < 2; k++) {
			if (!ps[k]->n) continue;
			char *nlp = memchr(ps[k]->s, '\n', ps[k]->n);
			size_t h1 = nlp ? (size_t)(nlp - ps[k]->s) : ps[k]->n;
			if (sec_open(v, T_HUNK, k ? "rename" : "chmod", k ? 6 : 5, 0) < 0
			    || sec_line(v, ps[k]->s, h1)
			    || (nlp && sec_line(v, nlp + 1, ps[k]->n - h1 - 1))) goto out;
			sec_close(v);
		}
		if (wash_hunks(v, s, n, &i)) goto out;
		sec_close(v);
		files++;
	}
	r = files;
out:
	str_free(&file); str_free(&orig); str_free(&hdr); str_free(&t); str_free(&modes); str_free(&rename);
	return r;
}

/* --- git log --format=%m%x1f%h%x1f%D%x1f%s ------------------------------------ */

static int field(Seg l, int k, Seg *out)
{
	const char *p = l.p, *e = l.p + l.n;
	for (; k > 0; k--) {
		const char *q = memchr(p, 0x1f, (size_t)(e - p));
		if (!q) return -1;
		p = q + 1;
	}
	const char *q = memchr(p, 0x1f, (size_t)(e - p));
	*out = (Seg){ p, q ? (size_t)(q - p) : (size_t)(e - p) };
	return 0;
}

int log_subject(const char *s, size_t n, const char *ref, Str *out)
{
	out->n = 0;
	for (size_t i = 0; i < n; ) {
		Seg l = next_line(s, n, &i), d, sub;
		if (!l.n || field(l, 2, &d) || field(l, 3, &sub)) continue;
		if (ref) {
			/* ref is one of d's ", " separated names, maybe after "HEAD -> " or "tag: " */
			size_t k = strlen(ref);
			int hit = 0;
			for (const char *p = d.p; p + k <= d.p + d.n && !hit; p++)
				hit = !memcmp(p, ref, k) && (p == d.p || p[-1] == ' ')
				      && (p + k == d.p + d.n || p[k] == ',');
			if (!hit) continue;
		}
		return str_add(out, sub.p, sub.n) ? -1 : 1;
	}
	return 0;
}

/* magit-format-ref-labels, without push remotes (none configured) and
   without the tag-name clash check */
static int ref_labels(Seg d, const Branch *b, Str *out)
{
	enum { HEAD, UPSTREAM, TAGS, LOCAL, REMOTES, OTHER, N };
	Str g[N] = { 0 };
	int r = -1;
	for (size_t i = 0; i < d.n; ) {
		const char *e = d.p + d.n, *p = d.p + i;
		const char *q = p;
		while (q + 1 < e && !(q[0] == ',' && q[1] == ' ')) q++;
		if (q + 1 >= e) q = e;
		size_t len = (size_t)(q - p);
		i += len + 2;
		if (prefix(p, len, "HEAD -> ")) { p += 8; len -= 8; }
		if (prefix(p, len, "tag: ")) { p += 5; len -= 5; }
		int grp = OTHER;
		const char *name = p;
		if (len == 4 && !memcmp(p, "HEAD", 4)) { grp = HEAD; name = "@"; len = 1; }
		else if (prefix(p, len, "refs/tags/")) { grp = TAGS; name += 10; len -= 10; }
		else if (prefix(p, len, "refs/heads/")) {
			name += 11; len -= 11;
			grp = b && b->head && strlen(b->head) == len && !memcmp(b->head, name, len) ? HEAD : LOCAL;
		} else if (prefix(p, len, "refs/remotes/")) {
			name += 13; len -= 13;
			if (len >= 5 && !memcmp(name + len - 5, "/HEAD", 5)) continue;
			grp = b && b->upstream && strlen(b->upstream) == len && !memcmp(b->upstream, name, len) ? UPSTREAM : REMOTES;
		}
		Str *o = &g[grp];
		if ((o->n && str_add(o, " ", 1)) || str_add(o, name, len)) goto out;
	}
	out->n = 0;
	for (int k = 0; k < N; k++)
		if (g[k].n && ((out->n && str_add(out, " ", 1)) || str_add(out, g[k].s, g[k].n))) goto out;
	r = 0;
out:
	for (int k = 0; k < N; k++) str_free(&g[k]);
	return r;
}

/* magit-log-wash-rev in a status section: "hash refs subject" */
int parse_log(View *v, const char *s, size_t n, int m, const Branch *b)
{
	Str t = { 0 }, refs = { 0 };
	int r = -1;
	for (size_t i = 0; i < n; ) {
		Seg l = next_line(s, n, &i), h, d, sub;
		if (!l.n || (m && l.p[0] != m) || field(l, 1, &h) || field(l, 2, &d) || field(l, 3, &sub)) continue;
		t.n = 0;
		if (str_add(&t, h.p, h.n) || ref_labels(d, b, &refs)) goto out;
		if (refs.n && (str_add(&t, " ", 1) || str_add(&t, refs.s, refs.n))) goto out;
		if (str_add(&t, " ", 1) || str_add(&t, sub.p, sub.n)) goto out;
		if (sec_open(v, T_COMMIT, h.p, h.n, 0) < 0 || sec_line(v, t.s, t.n)) goto out;
		sec_close(v);
	}
	r = 0;
out:
	str_free(&t); str_free(&refs);
	return r;
}

/* --- the log buffer (magit-log-mode) ------------------------------------------ */

/* magit--age: the first unit the age reaches at least once, rounded */
static int age(long now, long at, Str *out)
{
	static const struct { const char *one, *many; unsigned long w; } u[] = {
		{ "year", "years", 31556952 }, { "month", "months", 2629746 }, { "week", "weeks", 604800 },
		{ "day", "days", 86400 }, { "hour", "hours", 3600 }, { "minute", "minutes", 60 },
		{ "second", "seconds", 1 } };
	/* magit--age takes the absolute difference; unsigned, as a time from
	   git can be any long and the difference of two longs fits there */
	unsigned long secs = at > now ? (unsigned long)at - (unsigned long)now : (unsigned long)now - (unsigned long)at;
	int k = 0;
	while (k < 6 && secs < u[k].w) k++;
	unsigned long c = secs / u[k].w + (secs % u[k].w >= u[k].w - u[k].w / 2);
	char t[64];
	return str_add(out, t, (size_t)snprintf(t, sizeof t, "%2lu %-7s", c, c == 1 ? u[k].one : u[k].many));
}

/* columns of UTF-8 text: code points (wide characters count one; hypothesis) */
static size_t cols(const char *s, size_t n)
{
	size_t c = 0;
	for (size_t i = 0; i < n; i++) c += ((unsigned char)s[i] & 0xc0) != 0x80;
	return c;
}

/* the first bytes of s that fill at most w columns */
static size_t fit(const char *s, size_t n, size_t w)
{
	size_t i = 0, c = 0;
	while (i < n) {
		size_t j = i + 1;
		while (j < n && ((unsigned char)s[j] & 0xc0) == 0x80) j++;
		if (c + 1 > w) break;
		c++;
		i = j;
	}
	return i;
}

int parse_logbuf(View *v, const char *s, size_t n, const Branch *b, int width, long now)
{
	Str t = { 0 }, refs = { 0 };
	int r = -1;
	/* magit-log-margin: author (18 columns), a space, the age; 30 columns */
	size_t text = width > 40 ? (size_t)width - 30 : 10;
	for (size_t i = 0; i < n; ) {
		Seg l = next_line(s, n, &i);
		t.n = 0;
		const char *x = memchr(l.p, 0x1f, l.n);
		if (!x) {
			/* a line of the graph alone: under the commits' graphs */
			if (str_addz(&t, "        ") || str_add(&t, l.p, l.n) || sec_line(v, t.s, t.n)) goto out;
			continue;
		}
		const char *h = x;
		while (h > l.p && ((h[-1] >= '0' && h[-1] <= '9') || (h[-1] >= 'a' && h[-1] <= 'f'))) h--;
		Seg rest = { h, l.n - (size_t)(h - l.p) }, hash, d, au, at, sub;
		if (field(rest, 0, &hash) || field(rest, 1, &d) || field(rest, 2, &au) || field(rest, 3, &at)
		    || field(rest, 4, &sub)) continue;
		if (str_add(&t, hash.p, hash.n) || str_add(&t, " ", 1) || str_add(&t, l.p, (size_t)(h - l.p))
		    || ref_labels(d, b, &refs)) goto out;
		if (refs.n && (str_add(&t, refs.s, refs.n) || str_add(&t, " ", 1))) goto out;
		if (str_add(&t, sub.p, sub.n)) goto out;
		size_t c = cols(t.s, t.n);
		if (c > text) t.n = fit(t.s, t.n, text);
		for (; c < text; c++) if (str_add(&t, " ", 1)) goto out;
		size_t an = fit(au.p, au.n, 18), ac = cols(au.p, an);
		if (str_add(&t, au.p, an)) goto out;
		for (; ac < 19; ac++) if (str_add(&t, " ", 1)) goto out;
		char num[32];
		snprintf(num, sizeof num, "%.*s", (int)(at.n < 31 ? at.n : 31), at.p);
		if (age(now, strtol(num, NULL, 10), &t)) goto out;
		if (sec_open(v, T_COMMIT, hash.p, hash.n, 0) < 0 || sec_line(v, t.s, t.n)) goto out;
		sec_close(v);
	}
	r = 0;
out:
	str_free(&t); str_free(&refs);
	return r;
}

/* --- the diff buffer (magit-diff-mode): --stat then the patch ------------------- */

int parse_diffbuf(View *v, const char *s, size_t n, const Branch *b)
{
	size_t i = 0, last = 0, nstat = 0;
	/* the stat: " file | N +-" lines, then " N files changed, ..." */
	while (i < n && !prefix(s + i, n - i, "diff ") && !prefix(s + i, n - i, "* Unmerged path ")) {
		size_t j = i;
		Seg l = next_line(s, n, &j);
		if (l.n && l.p[0] == ' ') { last = i; nstat++; }
		i = j;
	}
	if (nstat) {
		/* magit-diff-wash-diffstat: the summary heads the file lines */
		size_t j = last;
		Seg sum = next_line(s, n, &j);
		if (sec_open(v, T_INFO, "diffstat", 8, 0) < 0 || sec_line(v, sum.p + 1, sum.n - 1)) return -1;
		for (j = 0; j < last; ) {
			/* magit-diff-wash-diffstat: a file section per line */
			Seg l = next_line(s, n, &j);
			if (!l.n || l.p[0] != ' ') continue;
			const char *bar = memchr(l.p, '|', l.n);
			size_t fe = bar ? (size_t)(bar - l.p) : l.n;
			while (fe > 1 && l.p[fe - 1] == ' ') fe--;
			if (sec_open(v, T_FILE, l.p + 1, fe - 1, 0) < 0 || sec_line(v, l.p + 1, l.n - 1)) return -1;
			sec_close(v);
		}
		sec_close(v);
		if (sec_line(v, "", 0)) return -1;
	}
	return parse_diff(v, s + i, n - i, 0, 0, b) < 0 ? -1 : 0;
}

/* --- magit-revision-mode and magit-stash-mode ------------------------------------- */

/* the next NUL-ended part of s, or what is left */
static Seg part(const char *s, size_t n, size_t *i)
{
	Seg l = { s + *i, 0 };
	const char *e = memchr(s + *i, 0, n - *i);
	l.n = e ? (size_t)(e - l.p) : n - *i;
	*i += l.n + (e != NULL);
	return l;
}

static int lines_of(View *v, Seg t)
{
	for (size_t i = 0; i < t.n; ) {
		Seg l = next_line(t.p, t.n, &i);
		if (sec_line(v, l.p, l.n)) return -1;
	}
	return 0;
}

/* magit--insert-related-refs: "Merged:" and git branch's names after it */
static int related(View *v, Seg t, const char *title)
{
	Str o = { 0 };
	int r = str_addz(&o, title);
	while (!r && o.n < 11) r = str_add(&o, " ", 1);
	size_t n0 = o.n;
	for (size_t i = 0; !r && i < t.n; ) {
		Seg l = next_line(t.p, t.n, &i);
		if (l.n < 2 || prefix(l.p + 2, l.n - 2, "(HEAD") || prefix(l.p + 2, l.n - 2, "HEAD -> ")) continue;
		r = str_add(&o, " ", 1) || str_add(&o, l.p + 2, l.n - 2);
	}
	if (!r && o.n > n0) r = sec_line(v, o.s, o.n);
	str_free(&o);
	return r;
}

/* git show -s (%D, %H, the headers, %B), the parents' "%h %s", git branch
   --merged and --contains, each NUL-ended, then the diff with its stat
   (magit-revision-sections-hook without tags and notes) */
int parse_revbuf(View *v, const char *s, size_t n, const Branch *b)
{
	size_t i = 0;
	Seg d = part(s, n, &i), h = part(s, n, &i), hd = part(s, n, &i), msg = part(s, n, &i);
	Seg par = part(s, n, &i), mer = part(s, n, &i), con = part(s, n, &i);
	Str o = { 0 };
	int r = ref_labels(d, b, &o);
	if (!r && o.n) r = str_add(&o, " ", 1);
	r = r || str_add(&o, h.p, h.n) || sec_line(v, o.s, o.n) || lines_of(v, hd);
	str_free(&o);
	for (size_t k = 0; !r && k < par.n; ) {
		Seg l = next_line(par.p, par.n, &k);
		r = str_addz(&o, "Parent:     ") || str_add(&o, l.p, l.n) || sec_line(v, o.s, o.n);
		o.n = 0;
	}
	str_free(&o);
	if (r || related(v, mer, "Merged:") || related(v, con, "Contained:") || sec_line(v, "", 0)) return -1;
	while (msg.n && msg.p[msg.n - 1] == '\n') msg.n--;
	if (msg.n ? lines_of(v, msg) : sec_linez(v, "(no message)")) return -1;
	if (sec_line(v, "", 0)) return -1;
	return parse_diffbuf(v, s + i, n - i, b);
}

/* magit-stash-sections-hook: the work tree's, the index's and the untracked
   files' diffs, each NUL-ended, under "Unstaged", "Staged", "Untracked files" */
int parse_stashbuf(View *v, const char *s, size_t n, const Branch *b)
{
	static const char *const head[] = { "Unstaged", "Staged", "Untracked files" };
	size_t i = 0;
	for (int k = 0; k < 3 && i < n; k++) {
		Seg t = part(s, n, &i);
		if (k == 2 && !t.n) break;
		if ((k && sec_line(v, "", 0)) || sec_open(v, T_INFO, head[k], strlen(head[k]), 0) < 0
		    || sec_linez(v, head[k]) || parse_diff(v, t.p, t.n, 0, 0, b) < 0) return -1;
		sec_close(v);
	}
	return 0;
}
