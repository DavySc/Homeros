/* Org's element parser, a transcription of org-element.el (Org 9.8.7) at
   granularity element.  Each function names the org-element function it
   copies; the regexps are matched by hand, so each recogniser quotes the
   regexp it stands for.  Positions are bytes; lines end with '\n'.

   Org parses recursively; this keeps the open containers in a stack of
   frames instead, so nesting depth costs heap, not C stack.  Known
   differences, all outside ASCII or outside Davy's files (hypothesis):
   case folding is ASCII-only, a block name inside a list is compared
   literally where Org uses it unquoted as a regexp, and the TODO keywords
   are Davy's (org_todo) whatever a file's #+TODO: lines say. */
#include "org.h"
#include "uni.h"
#include "class.h"
#include "grid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const org_todo[] = { "TODO", "NEXT", "WAIT", "DONE", "CANCELLED", NULL };

static const unsigned char *S;
static size_t N;

enum { M_NIL, M_FIRST, M_SECTION, M_TOP, M_PLANNING, M_PROPS, M_NODEPROP, M_ITEM, M_ROW };

/* ---- lines and characters ---- */

static size_t eol(size_t p)
{
	const unsigned char *e = p < N ? memchr(S + p, '\n', N - p) : NULL;
	return e ? (size_t)(e - S) : N;
}
static size_t nextl(size_t p) { p = eol(p); return p < N ? p + 1 : N; }
static size_t bol(size_t p) { while (p > 0 && S[p - 1] != '\n') p--; return p; }
static size_t prevl(size_t p) { p = bol(p); return p > 0 ? bol(p - 1) : 0; }   /* forward-line -1 */
static int ateol(size_t p) { return p >= N || S[p] == '\n'; }
static int blank(size_t p) { return p < N && (S[p] == ' ' || S[p] == '\t'); }
static size_t blanks(size_t p) { while (blank(p)) p++; return p; }
static int ws(size_t p) { return blank(p) || (p < N && (S[p] == '\n' || S[p] == '\r')); }
/* skip-chars-forward " \r\t\n" up to lim; never moves back */
static size_t fwd(size_t p, size_t lim) { while (p < lim && ws(p)) p++; return p; }
static size_t back(size_t p) { while (p > 0 && ws(p - 1)) p--; return p; }
/* the end most parsers take: skip blank lines, then (if (eobp) (point)
   (line-beginning-position)) */
static size_t after(size_t p, size_t lim) { p = fwd(p, lim); return p >= N ? N : bol(p); }

static int up(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
/* case-insensitive literal (case-fold-search t) */
static int lit(size_t p, const char *k)
{
	for (; *k; k++, p++) if (p >= N || up(S[p]) != up((unsigned char)*k)) return 0;
	return 1;
}
static int same(size_t a, size_t b, size_t n)
{
	for (size_t i = 0; i < n; i++) if (up(S[a + i]) != up(S[b + i])) return 0;
	return 1;
}
/* whitespace syntax in Org's syntax table, measured: \t \n \f \r space,
   U+00A0, U+2000..U+200B, U+202F, U+205F, U+3000 */
static int sp(size_t p)
{
	if (p >= N) return 0;
	unsigned c = S[p], d = p + 1 < N ? S[p + 1] : 0, e = p + 2 < N ? S[p + 2] : 0;
	if (c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == ' ') return 1;
	if (c == 0xC2) return d == 0xA0;
	if (c == 0xE2 && d == 0x80) return e <= 0x8B || e == 0xAF;
	if (c == 0xE2 && d == 0x81) return e == 0x9F;
	return c == 0xE3 && d == 0x80 && e == 0x80;
}
static size_t run(size_t p) { while (p < N && !sp(p)) p++; return p; }   /* \S-* */
/* org-property-re's :KEY: at b, on a line ending at e: its end, or 0 */
static size_t key(size_t b, size_t e)
{
	size_t z;
	return b < e && S[b] == ':' && (z = run(b + 1)) >= b + 3 && S[z - 1] == ':' && (z == e || blank(z)) ? z : 0;
}
/* is c in a class.h table (sorted, disjoint ranges)? */
static int in(const unsigned (*t)[2], size_t k, unsigned c)
{
	size_t lo = 0, hi = k;
	while (lo < hi) { size_t m = lo + (hi - lo) / 2; if (c > t[m][1]) lo = m + 1; else hi = m; }
	return lo < k && c >= t[lo][0];
}
#define IN(t, c) in(t, sizeof t / sizeof *t, c)
/* bytes of the [-_[:word:]] character at p, else 0 */
static size_t namec(size_t p)
{
	if (p >= N) return 0;
	unsigned c = uni_cp(S, N, p);
	return c == '-' || c == '_' || IN(WORD, c) ? uni_len(S, N, p) : 0;
}
static size_t names(size_t p) { size_t k; while ((k = namec(p))) p += k; return p; }
static int digit(size_t p) { return p < N && S[p] >= '0' && S[p] <= '9'; }

/* ---- line recognisers ---- */

static size_t stars(size_t p) { while (p < N && S[p] == '*') p++; return p; }
static size_t spaces(size_t p) { while (p < N && S[p] == ' ') p++; return p; }

static int headline(size_t p)           /* ^\*+ */
{
	size_t q = stars(p);
	return p == bol(p) && q > p && q < N && S[q] == ' ';
}
static int comment(size_t p)            /* org-comment-regexp ^[ \t]*#( |$) */
{
	p = blanks(p);
	return p < N && S[p] == '#' && (ateol(p + 1) || S[p + 1] == ' ');
}
static int emptyl(size_t p) { return ateol(blanks(p)); }   /* ^[ \t]*$ */
static int drawerl(size_t p)            /* org-element-drawer-re */
{
	p = blanks(p);
	if (p >= N || S[p] != ':') return 0;
	size_t q = names(p + 1);
	return q > p + 1 && q < N && S[q] == ':' && ateol(blanks(q + 1));
}
static int endl(size_t p, const char *k, size_t name, size_t len)   /* ^[ \t]*#\+K(NAME)?[ \t]*$ */
{
	p = blanks(p);
	if (!lit(p, k)) return 0;
	p += strlen(k);
	if (len) { if (p + len > N || !same(p, name, len)) return 0; p += len; }
	return ateol(blanks(p));
}
static int drawer_end(size_t p) { return endl(p, ":END:", 0, 0); }
static int fixedl(size_t p)             /* [ \t]*:( |$) */
{
	p = blanks(p);
	return p < N && S[p] == ':' && (ateol(p + 1) || S[p + 1] == ' ');
}
static int planningl(size_t p)          /* org-element-planning-line-re */
{
	p = blanks(p);
	return lit(p, "CLOSED:") || lit(p, "DEADLINE:") || lit(p, "SCHEDULED:");
}
/* [[:digit:]]{4}-[[:digit:]]{2}-[[:digit:]]{2} */
static int date(size_t p)
{
	for (int i = 0; i < 10; i++)
		if (i == 4 || i == 7 ? p + i >= N || S[p + i] != '-' : !digit(p + i)) return 0;
	return 1;
}
/* the end of an inactive timestamp opened at p whose ']' is at r, as
   org-ts-regexp-inactive \[(DATE(?: .*?)?)\] can match it */
static int tsto(size_t p, size_t r)
{
	return S[p] == '[' && date(p + 1) && r >= p + 11 && (r == p + 11 || S[p + 11] == ' ');
}
/* [\t ]+=>[\t ]+[[:digit:]]+:[[:digit:]][[:digit:]][\t ]*$ ending the line
   at e: the start of its first blank, or ORG_NONE */
static size_t duration(size_t a, size_t e)
{
	while (e > a && blank(e - 1)) e--;
	if (e < a + 3 || !digit(e - 1) || !digit(e - 2) || S[e - 3] != ':') return ORG_NONE;
	size_t d = e - 3;
	while (d > a && digit(d - 1)) d--;
	if (d == e - 3 || d == a || !blank(d - 1)) return ORG_NONE;
	while (d > a && blank(d - 1)) d--;
	if (d < a + 2 || S[d - 1] != '>' || S[d - 2] != '=') return ORG_NONE;
	d -= 2;
	if (d == a || !blank(d - 1)) return ORG_NONE;
	while (d > a && blank(d - 1)) d--;
	return d;
}
static int clockl(size_t p)             /* org-element-clock-line-re */
{
	p = blanks(p);
	if (!lit(p, "CLOCK:")) return 0;
	p += 6;
	if (!blank(p)) return 0;
	size_t a = blanks(p), e = eol(p), z = e;
	if (duration(p, e) == p) return 1;   /* CLOCK: => 1:00 */
	if (a >= N || S[a] != '[') return 0;
	while (z > a && blank(z - 1)) z--;
	if (z > a && S[z - 1] == ']' && tsto(a, z - 1)) return 1;
	size_t d = duration(a, e);           /* [ts]--[ts] => 1:00 */
	if (d == ORG_NONE || d == a || S[d - 1] != ']') return 0;
	for (size_t x = a + 11; x + 15 <= d; x++)
		if (S[x] == ']' && S[x + 1] == '-' && S[x + 2] == '-' && tsto(a, x) && tsto(x + 3, d - 1)) return 1;
	return 0;
}
/* org-element--affiliated-re */
static int affl(size_t p)
{
	static const char *const k[] = { "DATA", "HEADER", "HEADERS", "LABEL", "NAME", "PLOT",
		"RESNAME", "RESULT", "SOURCE", "SRCNAME", "TBLNAME", "CAPTION", "RESULTS", NULL };
	p = blanks(p);
	if (!lit(p, "#+")) return 0;
	p += 2;
	for (int i = 0; k[i]; i++) {
		size_t q = p + strlen(k[i]);
		if (!lit(p, k[i])) continue;
		if (q < N && S[q] == ':') return 1;
		if (i >= 11 && q < N && S[q] == '[')   /* dual: \[(.*)\] */
			for (size_t e = eol(q), x = q + 1; x + 1 < e + 1 && x + 1 < N; x++)
				if (S[x] == ']' && S[x + 1] == ':') return 1;
	}
	if (!lit(p, "ATTR_")) return 0;
	size_t q = p + 5;
	while (q < N && (S[q] == '-' || S[q] == '_' || (S[q] >= '0' && S[q] <= '9') ||
	                 (up(S[q]) >= 'A' && up(S[q]) <= 'Z'))) q++;
	return q > p + 5 && q < N && S[q] == ':';
}
/* org-property-drawer-re at p */
static int propdrawer(size_t p)
{
	size_t q = blanks(p);
	if (!lit(q, ":PROPERTIES:") || (q = blanks(q + 12)) >= N || S[q] != '\n') return 0;
	for (p = q + 1; p < N; p = q + 1) {
		if (drawer_end(p)) return 1;
		q = blanks(p);
		if (q >= N || S[q] != ':') return 0;
		size_t r = run(q + 1);
		if (r < q + 3 || S[r - 1] != ':' || r >= N || (S[r] != ' ' && S[r] != '\t' && S[r] != '\n')) return 0;
		if ((q = eol(r)) >= N) return 0;
	}
	return 0;
}
/* org-item-re: ([ \t]*([-+]|[0-9]+[.)])|[ \t]+\*)([ \t]+|$) */
static int iteml(size_t p)
{
	size_t t = blanks(p), u = t + 1;
	if (t >= N) return 0;
	if (digit(t)) {
		while (digit(u)) u++;
		if (u >= N || (S[u] != '.' && S[u] != ')')) return 0;
		u++;
	} else if (!(S[t] == '-' || S[t] == '+' || (S[t] == '*' && t > p))) return 0;
	return ateol(u) || blank(u);
}
static size_t ind(size_t p)             /* org-current-text-column: a tab is 8 */
{
	size_t c = 0;
	for (; blank(p); p++) c += S[p] == '\t' ? 8 : 1;
	return c;
}
/* \\begin{([A-Za-z0-9*]+)} after the blanks; the name's end, or 0 */
static size_t latexl(size_t p, size_t *name)
{
	p = blanks(p);
	if (!lit(p, "\\begin{")) return 0;
	size_t q = *name = p + 7;
	while (q < N && (S[q] == '*' || (S[q] >= '0' && S[q] <= '9') ||
	                 (up(S[q]) >= 'A' && up(S[q]) <= 'Z'))) q++;
	return q > *name && q < N && S[q] == '}' ? q : 0;
}
/* the first line from p (p itself if it starts one) before lim satisfying
   f, as re-search-forward "^..." bounded by lim; ORG_NONE if none */
static size_t search(size_t p, size_t lim, int (*f)(size_t, size_t, size_t), size_t a, size_t b)
{
	for (p = p == bol(p) ? p : nextl(p); p < lim; p = nextl(p))
		if (eol(p) <= lim && f(p, a, b)) return p;
	return ORG_NONE;
}
static int f_drawer_end(size_t p, size_t a, size_t b) { (void)a; (void)b; return drawer_end(p); }
static int f_end(size_t p, size_t a, size_t b) { return endl(p, "#+END_", a, b); }
static int f_endc(size_t p, size_t a, size_t b) { (void)a; (void)b; return endl(p, "#+END:", 0, 0); }
/* scan-lists over one kind of bracket (org-element--parse-paired-brackets):
   after the bracket closing the one at p, or ORG_NONE */
static size_t pair(size_t p, int o, int c)
{
	size_t d = 0;
	for (; p < N; p++)
		if (S[p] == o) d++;
		else if (S[p] == c && --d == 0) return p + 1;
	return ORG_NONE;
}
/* org-element-babel-call-parser's end for the #+CALL: line at q: its
   brackets may span lines, to the end of the text */
static size_t call_end(size_t q, size_t lim)
{
	size_t p = blanks((size_t)((const unsigned char *)memchr(S + q, ':', N - q) - S) + 1), bb = nextl(q), f;
	while (p < bb && S[p] != '[' && S[p] != ']' && S[p] != '(' && S[p] != ')') p++;
	if (p < N && S[p] == '[' && (f = pair(p, '[', ']')) != ORG_NONE) p = f;
	if (p < N && S[p] == '(' && (f = pair(p, '(', ')')) != ORG_NONE) p = f;
	return after(nextl(p), lim);
}
static int f_dynend(size_t p, size_t a, size_t b)
{
	(void)a; (void)b;
	p = blanks(p);
	if (!lit(p, "#+END")) return 0;
	p += 5;
	return ateol(blanks(p + (p < N && S[p] == ':')));
}
/* \\end{NAME}[ \t]*$ anywhere from p, bounded by lim: the match end */
static size_t latex_end(size_t p, size_t lim, size_t name, size_t len)
{
	for (; p + 6 + len <= lim; p++)
		if (lit(p, "\\end{") && same(p + 5, name, len) && S[p + 5 + len] == '}') {
			size_t e = blanks(p + 6 + len);
			if (ateol(e) && e <= lim) return e;
		}
	return ORG_NONE;
}
/* \S-+: after "#+" (the keyword alternative of org-element--current-element-re) */
static int kwcolon(size_t p)
{
	size_t r = run(p);
	for (size_t i = p + 1; i < r; i++) if (S[i] == ':') return 1;
	return 0;
}
/* [ \t]*#\+(\S-+)\[.*\]: : the end of group 1, or 0 */
static size_t kwbracket(size_t p)
{
	size_t r = run(p), e = eol(p);
	for (size_t i = r; i > p + 1; i--)
		if (S[i - 1] == '[')
			for (size_t j = i; j + 1 < e; j++)
				if (S[j] == ']' && S[j + 1] == ':') return i - 1;
	return 0;
}
/* \+(-+\+)+[ \t]*$ at p */
static int tablerule(size_t p)
{
	if (p >= N || S[p] != '+') return 0;
	int g = 0;
	for (p++; p < N && S[p] == '-'; p++, g++) {
		while (p < N && S[p] == '-') p++;
		if (p >= N || S[p] != '+') return 0;
	}
	return g && ateol(blanks(p));
}
/* \[fn:[-_[:word:]]+\] */
static int fnl(size_t p)
{
	if (!lit(p, "[fn:")) return 0;
	size_t q = names(p + 4);
	return q > p + 4 && q < N && S[q] == ']';
}

/* org-element-paragraph-separate at line p */
static int separator(size_t p)
{
	size_t t = stars(p);
	if (t > p && t < N && S[t] == ' ') return 1;
	if (fnl(p) || lit(p, "%%(")) return 1;
	t = blanks(p);
	if (ateol(t) || S[t] == '|' || tablerule(t)) return 1;
	if (S[t] == '#') {
		if (ateol(t + 1) || S[t + 1] == ' ') return 1;
		if (S[t + 1] != '+') return 0;
		if (lit(t + 2, "BEGIN_") && run(t + 8) > t + 8) return 1;
		return kwcolon(t + 2) || kwbracket(t + 2);
	}
	if (S[t] == ':') return ateol(t + 1) || S[t + 1] == ' ' || drawerl(p);
	if (S[t] == '-') {
		size_t q = t;
		while (q < N && S[q] == '-') q++;
		if (q - t >= 5 && ateol(blanks(q))) return 1;
	}
	size_t name;
	if (latexl(p, &name) || clockl(p)) return 1;
	size_t u = t + 1;
	if (digit(t)) {
		while (digit(u)) u++;
		if (u >= N || (S[u] != '.' && S[u] != ')')) return 0;
		u++;
	} else if (S[t] != '-' && S[t] != '+' && S[t] != '*') return 0;
	return ateol(u) || blank(u);
}

/* ---- the parse ---- */

typedef struct { size_t pos, ind, end; } Item;   /* org-element--list-struct's (pos ind ... end) */
typedef struct { size_t node, pos, lim, st, stn, mark; int mode; } Frame;

static Item *it;
static size_t nit, cit;
static int nomem;

static int grow(void **a, size_t *cap, size_t n, size_t z)
{
	if (n < *cap) return 1;
	size_t c = *cap ? *cap * 2 : 64;
	void *b = realloc(*a, c * z);
	if (!b) { nomem = 1; return 0; }
	*a = b; *cap = c;
	return 1;
}

/* org-list-end-re ^[ \t]*\n[ \t]*\n */
static int listend(size_t p)
{
	p = blanks(p);
	return p < N && S[p] == '\n' && (p = blanks(p + 1)) < N && S[p] == '\n';
}
/* \[@(?:start:)?([0-9]+|[A-Za-z])\] at p - 2: after the ], or 0 */
static size_t counter(size_t p)
{
	size_t c = p;
	if (digit(c)) while (digit(c)) c++;
	else if (c < N && up(S[c]) >= 'A' && up(S[c]) <= 'Z') c++;
	return c > p && c < N && S[c] == ']' ? c + 1 : 0;
}

/* org-element--list-struct from the item at p: appends the items to it[] */
static void list_struct(size_t p, size_t lim)
{
	size_t *stk = NULL, open = 0, cs = 0;   /* the items not ended yet */
	for (;;) {
		if (p >= lim || listend(p)) {
			size_t e = p >= lim ? nextl(back(p)) : p;
			while (open) it[stk[--open]].end = e;
			break;
		}
		if (iteml(p)) {
			size_t i = ind(p);
			while (open && i <= it[stk[open - 1]].ind) it[stk[--open]].end = p;
			if (!grow((void **)&it, &cit, nit, sizeof *it) || !grow((void **)&stk, &cs, open, sizeof *stk)) break;
			it[nit] = (Item){ p, i, ORG_NONE };
			stk[open++] = nit++;
		} else if (!emptyl(p)) {
			size_t i = ind(p), e = nextl(back(p));
			while (open && i <= it[stk[open - 1]].ind) it[stk[--open]].end = e;
			if (!open) break;
			size_t t = blanks(p), r, f = ORG_NONE;
			if (lit(t, "#+BEGIN:"))   /* #+END: with its colon, unlike the dynamic block's */
				f = search(p, lim, f_endc, 0, 0);
			else if (lit(t, "#+BEGIN_") && (r = run(t + 8)) > t + 8)
				f = search(p, lim, f_end, t + 8, r - t - 8);
			else if (drawerl(p))
				f = search(p, lim, f_drawer_end, 0, 0);
			if (f != ORG_NONE) p = f;
		}
		p = nextl(p);
	}
	free(stk);
}

static Item *find_item(size_t st, size_t stn, size_t pos)
{
	size_t lo = st, hi = st + stn;
	while (lo < hi) {
		size_t m = lo + (hi - lo) / 2;
		if (it[m].pos < pos) lo = m + 1; else hi = m;
	}
	return lo < st + stn && it[lo].pos == pos ? &it[lo] : NULL;
}

/* org-element-paragraph-parser */
static void paragraph(OrgNode *e, size_t begin, size_t cb, size_t lim)
{
	/* the search starts at the end of the first line, so an empty first
	   line is its own separator */
	size_t q = eol(cb), stop = lim, name, r, f;
	if (q != bol(q)) q = nextl(q);
	for (;; q = nextl(q)) {
		while (q < lim && !(eol(q) <= lim && separator(q))) q = nextl(q);
		if (q >= lim) break;
		size_t t = blanks(q);
		if (drawerl(q)) f = search(nextl(q), lim, f_drawer_end, 0, 0) != ORG_NONE;
		else if (lit(t, "#+BEGIN_") && (r = run(t + 8)) > t + 8) f = search(q, lim, f_end, t + 8, r - t - 8) != ORG_NONE;
		else if ((r = latexl(q, &name))) f = latex_end(q, lim, name, r - name) != ORG_NONE;
		else if (lit(t, "#+") && (r = kwbracket(t + 2))) {
			size_t k = r - t - 2;
			f = k == 7 && (lit(t + 2, "CAPTION") || lit(t + 2, "RESULTS"));
		} else f = 1;
		if (f) { stop = q; break; }
	}
	size_t c = back(stop);
	*e = (OrgNode){ begin, cb, cb, nextl(c > cb ? c : cb), after(stop, lim), 0, ORG_PARAGRAPH };
}

/* the element at q (org-element--current-element); st/stn the list
   structure in force; a plain list may append a new one to it[] */
static void element(OrgNode *e, size_t q, size_t lim, int mode, size_t *st, size_t *stn)
{
	size_t f, r, name;
	if (mode == M_ITEM) {               /* org-element-item-parser */
		Item *x = find_item(*st, *stn, q);
		size_t end = x ? x->end : lim, t = blanks(q), u = t + 1, tag = ORG_NONE, me, c;
		end = end == bol(end) ? end : nextl(end);
		if (end > lim) end = lim;
		int ordered = digit(t);             /* org-list-full-item-re; org-item-re allows no letters */
		if (ordered) { while (digit(u)) u++; u++; }
		u = blanks(u);
		if (lit(u, "[@") && ((c = lit(u + 2, "start:") ? counter(u + 8) : 0) || (c = counter(u + 2))))
			u = blanks(c);
		if (u + 2 < N && S[u] == '[' && S[u + 2] == ']' &&     /* (\[[ X-]\])([ \t]+|$) */
		    (S[u + 1] == ' ' || up(S[u + 1]) == 'X' || S[u + 1] == '-') && (ateol(u + 3) || blank(u + 3)))
			u = blanks(u + 3);
		me = u;
		for (size_t j = eol(u); j >= u + 3; j--)   /* (.*)[ \t]+::([ \t]+|$) */
			if (S[j - 2] == ':' && S[j - 1] == ':' && blank(j - 3) && (ateol(j) || blank(j))) {
				tag = u; me = blanks(j);
				break;
			}
		size_t cb = ORG_NONE, ce = ORG_NONE;
		c = fwd(tag != ORG_NONE && ordered ? tag : me, end);
		if (c != end) { cb = bol(c) == q ? c : bol(c); ce = nextl(back(end)); }
		*e = (OrgNode){ q, q, cb, ce, end, 0, ORG_ITEM };
		return;
	}
	if (mode == M_ROW) {                /* org-element-table-row-parser */
		size_t t = blanks(q), cb = ORG_NONE, ce = ORG_NONE;
		if (!(t + 1 < N && S[t] == '|' && S[t + 1] == '-')) {
			cb = q;
			while (cb < N && S[cb++] != '|') ;
			ce = eol(q);
			while (ce > cb && blank(ce - 1)) ce--;
		}
		*e = (OrgNode){ q, q, cb, ce, nextl(q), 0, ORG_TABLE_ROW };
		return;
	}
	if (mode == M_NODEPROP) { *e = (OrgNode){ q, q, ORG_NONE, ORG_NONE, nextl(q), 0, ORG_NODE_PROPERTY }; return; }
	if (headline(q)) {                  /* org-element-headline-parser */
		size_t k = 0, end = N, c;
		while (S[q + k] == '*') k++;
		for (size_t p = nextl(q); p < N; p = nextl(p)) {
			size_t j = 0;
			while (p + j < N && S[p + j] == '*') j++;
			if (j && j <= k && p + j < N && S[p + j] == ' ') { end = p; break; }
		}
		c = fwd(nextl(q), end);
		*e = (OrgNode){ q, q, c != end ? bol(c) : ORG_NONE, c != end ? end : ORG_NONE, end, 0, ORG_HEADLINE };
		return;
	}
	if (mode == M_FIRST || mode == M_SECTION) {   /* org-element-section-parser */
		size_t end = nextl(q);
		while (end < N && !headline(end)) end = nextl(end);
		*e = (OrgNode){ q, q, q, end, end, 0, ORG_SECTION };
		return;
	}
	int bl = q == bol(q);
	if (bl && comment(q)) {             /* org-element-comment-parser */
		size_t p = nextl(q);
		while (p < lim && comment(p)) p = nextl(p);
		*e = (OrgNode){ q, q, ORG_NONE, ORG_NONE, after(p, lim), 0, ORG_COMMENT };
		return;
	}
	int star = mode == M_PLANNING && S[prevl(q)] == '*';   /* right under the headline */
	if (star && planningl(q)) {   /* org-element-planning-parser */
		size_t p = fwd(nextl(q), lim);
		while (p > 0 && blank(p - 1)) p--;
		if (p != bol(p)) p = blanks(p);
		*e = (OrgNode){ q, q, ORG_NONE, ORG_NONE, p, 0, ORG_PLANNING };
		return;
	}
	if ((star || mode == M_PROPS || mode == M_TOP) && propdrawer(q)) {
		size_t cb = nextl(q), end = search(q, lim, f_drawer_end, 0, 0);   /* org-element-property-drawer-parser */
		if (end == ORG_NONE) end = search(q, N, f_drawer_end, 0, 0);
		*e = (OrgNode){ q, q, end > cb ? cb : ORG_NONE, end > cb ? end : ORG_NONE, after(nextl(end), lim), 0, ORG_PROPERTY_DRAWER };
		return;
	}
	if (!bl) { paragraph(e, q, q, lim); return; }
	if (clockl(q)) {                    /* org-element-clock-parser */
		size_t p = fwd(nextl(q), lim);
		while (p > 0 && blank(p - 1)) p--;
		if (p != bol(p)) p = blanks(p);
		*e = (OrgNode){ q, q, ORG_NONE, ORG_NONE, p, 0, ORG_CLOCK };
		return;
	}
	/* org-element--collect-affiliated-keywords */
	size_t b = q, k = 0;
	while (q < lim && affl(q)) q = nextl(q), k++;
	if (k && (emptyl(q) || comment(q) || (lit(blanks(q), "CLOCK:")) || headline(q))) q = b, k = 0;
	size_t t = blanks(q);
	int type = -1;
	if (k && q >= lim) { q = b; type = ORG_KEYWORD; }
	else if ((r = latexl(q, &name))) {  /* org-element-latex-environment-parser */
		if ((f = latex_end(q, lim, name, r - name)) != ORG_NONE) {
			*e = (OrgNode){ b, q, ORG_NONE, ORG_NONE, after(nextl(f), lim), 0, ORG_LATEX_ENVIRONMENT };
			return;
		}
		paragraph(e, b, q, lim); return;
	} else if (drawerl(q)) {            /* org-element-drawer-parser */
		if ((f = search(eol(q) < lim ? eol(q) : lim, lim, f_drawer_end, 0, 0)) != ORG_NONE) {
			size_t c = bol(fwd(nextl(q), N));
			*e = (OrgNode){ b, q, c < f ? c : ORG_NONE, c < f ? f : ORG_NONE, after(nextl(f), lim), 0, ORG_DRAWER };
			return;
		}
		paragraph(e, b, q, lim); return;
	} else if (fixedl(q)) {             /* org-element-fixed-width-parser */
		size_t p = q;
		while (p < lim && fixedl(p)) p = nextl(p);
		*e = (OrgNode){ b, q, ORG_NONE, ORG_NONE, after(p, lim), 0, ORG_FIXED_WIDTH };
		return;
	} else if (lit(t, "#+BEGIN:") && (r = blanks(t + 8)) < N && !sp(r)) {   /* org-element-dynamic-block-parser */
		if ((f = search(q, lim, f_dynend, 0, 0)) == ORG_NONE) { paragraph(e, b, q, lim); return; }
		size_t c = nextl(q);
		*e = (OrgNode){ b, q, c < f ? c : ORG_NONE, c < f ? f : ORG_NONE, after(nextl(f), lim), 0, ORG_DYNAMIC_BLOCK };
		return;
	} else if (lit(t, "#+BEGIN_") && (r = run(t + 8)) > t + 8) {
		static const char *const bn[] = { "CENTER", "COMMENT", "EXAMPLE", "EXPORT", "QUOTE", "SRC", "VERSE" };
		static const int bt[] = { ORG_CENTER_BLOCK, ORG_COMMENT_BLOCK, ORG_EXAMPLE_BLOCK, ORG_EXPORT_BLOCK,
		                          ORG_QUOTE_BLOCK, ORG_SRC_BLOCK, ORG_VERSE_BLOCK };
		size_t len = r - t - 8;
		type = ORG_SPECIAL_BLOCK;
		for (int i = 0; i < 7; i++) if (strlen(bn[i]) == len && lit(t + 8, bn[i])) type = bt[i];
		if ((f = search(q, lim, f_end, t + 8, len)) == ORG_NONE) { paragraph(e, b, q, lim); return; }
		size_t c = nextl(q), end = after(nextl(f), lim);
		if (type == ORG_VERSE_BLOCK) *e = (OrgNode){ b, q, c, f, end, 0, type };
		else if (type == ORG_CENTER_BLOCK || type == ORG_QUOTE_BLOCK || type == ORG_SPECIAL_BLOCK)
			*e = (OrgNode){ b, q, c < f ? c : ORG_NONE, c < f ? f : ORG_NONE, end, 0, type };
		else *e = (OrgNode){ b, q, ORG_NONE, ORG_NONE, end, 0, type };
		return;
	} else if (lit(t, "#+CALL:")) {
		*e = (OrgNode){ b, q, ORG_NONE, ORG_NONE, call_end(q, lim), 0, ORG_BABEL_CALL };
		return;
	} else if (lit(t, "#+") && kwcolon(t + 2)) type = ORG_KEYWORD;
	else if (fnl(q)) {                  /* org-element-footnote-definition-parser */
		size_t end = lim, c, p;
		for (p = nextl(q); p < lim; p = nextl(p)) {
			if (headline(p)) { end = p; break; }
			if (fnl(p) && eol(p) <= lim) {
				for (p = prevl(p); p > q && affl(p); p = prevl(p)) ;
				end = nextl(p);
				break;
			}
			size_t a = blanks(p);
			if (a < lim && a < N && S[a] == '\n' && (c = blanks(a + 1)) < lim && c < N && S[c] == '\n') {
				for (p = c + 1; (c = blanks(p)) < lim && c < N && S[c] == '\n'; p = c + 1) ;
				end = fwd(p, lim);
				end = end == lim ? lim : bol(end);
				break;
			}
		}
		c = fwd((size_t)((const unsigned char *)memchr(S + q, ']', N - q) - S) + 1, end);
		size_t cb = c == end ? ORG_NONE : bol(c) == q ? c : bol(c);
		*e = (OrgNode){ b, q, cb, cb == ORG_NONE ? ORG_NONE : nextl(back(end)), end, 0, ORG_FOOTNOTE_DEFINITION };
		return;
	}
	if (type < 0) {
		size_t d = t;
		while (d < N && S[d] == '-') d++;
		if (d - t >= 5 && ateol(blanks(d))) type = ORG_HORIZONTAL_RULE;
		else if (lit(q, "%%(")) type = ORG_DIARY_SEXP;
	}
	if (type >= 0) { *e = (OrgNode){ b, q, ORG_NONE, ORG_NONE, after(nextl(q), lim), 0, type }; return; }
	int org = t < N && S[t] == '|';
	if (!org && tablerule(t) && nextl(q) < lim) {   /* a table.el table: ends with a full rule */
		size_t x;
		for (x = nextl(q); x < lim; x = nextl(x)) {
			size_t a = blanks(x);
			if (ateol(a) || (S[a] != '+' && S[a] != '|')) break;
		}
		if (x >= lim) x = lim == bol(lim) ? prevl(lim) : bol(lim);
		else x = x == nextl(q) ? ORG_NONE : prevl(x);
		if (x != ORG_NONE && tablerule(blanks(x))) org = 2;
	}
	if (org) {                          /* org-element-table-parser */
		size_t p;
		for (p = q; p < lim; p = nextl(p)) {
			size_t a = blanks(p);
			if (ateol(a) || (S[a] != '|' && (org == 1 || S[a] != '+'))) break;
		}
		size_t tend = p < lim ? p : lim;
		for (p = tend; p < N && lit(blanks(p), "#+TBLFM: "); p = nextl(p)) ;
		*e = (OrgNode){ b, q, org == 1 ? q : ORG_NONE, org == 1 ? tend : ORG_NONE, after(p, lim), 0, ORG_TABLE };
		return;
	}
	if (iteml(q)) {                     /* org-element-plain-list-parser */
		Item *x = find_item(*st, *stn, q);
		if (!x) {
			size_t m = nit;
			list_struct(q, lim);
			*st = m; *stn = nit - m;
			if (nomem || !(x = find_item(*st, *stn, q))) { *e = (OrgNode){ b, q, q, lim, lim, 0, ORG_PLAIN_LIST }; return; }
		}
		size_t i = x->ind, p = x->end;
		for (Item *y; (y = find_item(*st, *stn, p)) && y->ind == i; ) p = y->end;
		p = back(p);
		size_t ce = p == bol(p) ? p : nextl(p), end = fwd(ce, lim);
		*e = (OrgNode){ b, q, q, ce, end == lim ? lim : bol(end), 0, ORG_PLAIN_LIST };
		return;
	}
	paragraph(e, b, q, lim);
}

static int greater(int t)
{
	return t == ORG_HEADLINE || t == ORG_SECTION || t == ORG_PLAIN_LIST || t == ORG_ITEM ||
	       t == ORG_PROPERTY_DRAWER || t == ORG_DRAWER || t == ORG_CENTER_BLOCK ||
	       t == ORG_QUOTE_BLOCK || t == ORG_SPECIAL_BLOCK || t == ORG_DYNAMIC_BLOCK ||
	       t == ORG_FOOTNOTE_DEFINITION || t == ORG_TABLE;
}

/* org-element--next-mode */
static int child_mode(int mode, int t)
{
	switch (t) {
	case ORG_HEADLINE: return M_SECTION;
	case ORG_SECTION: return mode == M_FIRST ? M_TOP : M_PLANNING;
	case ORG_PLAIN_LIST: return M_ITEM;
	case ORG_PROPERTY_DRAWER: return M_NODEPROP;
	case ORG_TABLE: return M_ROW;
	default: return M_NIL;
	}
}
static int next_mode(int mode, int t)
{
	if (mode == M_ITEM || mode == M_NODEPROP || mode == M_ROW) return mode;
	if ((mode == M_PLANNING && t == ORG_PLANNING) || (mode == M_TOP && t == ORG_COMMENT)) return M_PROPS;
	return M_NIL;
}

/* org-element--parse-elements over the top level from pos to lim */
static OrgNode *parse(size_t pos, size_t lim, int mode, size_t *count)
{
	nomem = 0;
	OrgNode *out = NULL; Frame *fr = NULL;
	size_t no = 0, co = 0, nf = 0, cf = 0;
	it = NULL; nit = cit = 0;
	if (grow((void **)&fr, &cf, 0, sizeof *fr) && grow((void **)&out, &co, 0, sizeof *out))
		fr[nf++] = (Frame){ ORG_NONE, pos, lim, 0, 0, ORG_NONE, mode };
	while (nf && !nomem) {
		Frame *F = &fr[nf - 1];
		if (F->pos >= F->lim) {
			if (F->mark != ORG_NONE) nit = F->mark;
			nf--;
			continue;
		}
		size_t st = F->st, stn = F->stn, m = nit;
		int mode = F->mode;
		OrgNode e;
		element(&e, F->pos, F->lim, mode, &st, &stn);
		if (nomem || !grow((void **)&out, &co, no, sizeof *out)) break;
		e.parent = F->node;
		out[no] = e;
		F->mode = next_mode(mode, e.type);
		F->pos = e.end > F->pos ? e.end : F->lim;   /* Org would loop here */
		if (greater(e.type) && e.cbeg != ORG_NONE) {
			if (!grow((void **)&fr, &cf, nf, sizeof *fr)) break;
			int keep = e.type == ORG_PLAIN_LIST || e.type == ORG_ITEM;
			fr[nf++] = (Frame){ no, e.cbeg, e.cend, keep ? st : 0, keep ? stn : 0,
			                    nit > m ? m : ORG_NONE, child_mode(mode, e.type) };
		} else nit = m;
		no++;
	}
	free(fr); free(it);
	if (nomem) { free(out); return NULL; }
	*count = no;
	return out;
}

/* org-element-parse-buffer */
OrgNode *org_parse(const char *s, size_t n, size_t *count)
{
	S = (const unsigned char *)s; N = n;
	return parse(bol(fwd(0, N)), N, M_FIRST, count);
}

/* the first node that begins at p or later */
static size_t first(const OrgNode *e, size_t k, size_t p)
{
	size_t lo = 0, hi = k;
	while (lo < hi) { size_t m = lo + (hi - lo) / 2; if (e[m].begin < p) lo = m + 1; else hi = m; }
	return lo;
}
static size_t top(const OrgNode *e, size_t i) { while (e[i].parent != ORG_NONE) i = e[i].parent; return i; }
static size_t moved(size_t x, size_t del, size_t ins) { return x == ORG_NONE ? x : x - del + ins; }

/* Reparses from the top-level node before the edit to the one after it.
   Org's top level is a section, then headlines each ending at the next one
   with as many stars or fewer, so the text before that span parses as
   before when the headline line at its start is untouched; the text after
   it, when its first headline stays at the top (no new shallower headline
   in the span: the span's last node would then end past it).  Only a babel
   call reads past its subtree (its brackets are matched to the end of the
   text); one before the span is measured again, and a whole reparse
   follows when its end moved. */
int org_edit(OrgNode **pe, size_t *pk, const char *s, size_t n, size_t pos, size_t del, size_t ins)
{
	OrgNode *e = *pe, *f, *g;
	size_t k = *pk, lo = 0, c, z, hi, m, t, L, R;
	S = (const unsigned char *)s; N = n;
	int mode = M_FIRST;
	if (!k) goto whole;
	if ((c = first(e, k, pos + 1)) && (c = top(e, c - 1)) && !memchr(S + e[c].begin, '\n', pos - e[c].begin))
		c = top(e, c - 1);   /* the edit touches its headline line */
	if (c) { lo = c; L = e[c].begin; mode = M_NIL; } else L = bol(fwd(0, N));
	for (size_t j = 0; j < lo; j++)   /* if its end moved, so did what its parent holds */
		if (e[j].type == ORG_BABEL_CALL) {
			size_t x = call_end(e[j].aff, e[e[j].parent].cend);
			if (x != (e[j].end <= pos ? e[j].end : e[j].end - del + ins)) goto whole;
			e[j].end = x;
		}
	z = first(e, k, pos + del + 1);
	R = e[z ? top(e, z - 1) : 0].end;
	if (!(f = parse(L, R - del + ins, mode, &m))) goto fail;
	for (t = m; t && f[t - 1].parent != ORG_NONE; t--) ;
	/* a new shallower headline holds what followed: parse() went on to its
	   end, where a top-level node began before */
	if (t && f[t - 1].end > R - del + ins) R = f[t - 1].end + del - ins;
	hi = first(e, k, R);
	if (m > hi - lo) {
		if (!(g = realloc(e, (k - (hi - lo) + m) * sizeof *e))) { free(f); goto fail; }
		e = g;
	}
	memmove(e + lo + m, e + hi, (k - hi) * sizeof *e);
	k = k - (hi - lo) + m;
	for (size_t j = lo + m; j < k; j++) {
		OrgNode *x = &e[j];
		x->begin = moved(x->begin, del, ins); x->aff = moved(x->aff, del, ins); x->end = moved(x->end, del, ins);
		x->cbeg = moved(x->cbeg, del, ins); x->cend = moved(x->cend, del, ins);
		if (x->parent != ORG_NONE) x->parent = x->parent - hi + lo + m;
	}
	for (size_t j = 0; j < m; j++) {
		e[lo + j] = f[j];
		if (f[j].parent != ORG_NONE) e[lo + j].parent += lo;
	}
	free(f);
	*pe = e; *pk = k;
	return 0;
whole:
	free(e);
	if (!(*pe = org_parse(s, n, pk))) *pk = 0;
	return *pe ? 0 : -1;
fail:
	free(e); *pe = NULL; *pk = 0;
	return -1;
}

/* ---- timestamps ---- */

/* string-to-number on digits, held at a bound instead of growing */
static int num(size_t a, size_t z)
{
	int v = 0;
	for (; a < z; a++) v = v > 99999999 ? v : v * 10 + (S[a] - '0');
	return v;
}
static size_t digits(size_t p, size_t lim) { while (p < lim && digit(p)) p++; return p; }
/* org-ts-regexp-both at p: its closing bracket, or ORG_NONE */
static size_t ts_both(size_t p)
{
	static const char form[] = "<dddd-dd-dd";
	for (size_t i = 0; form[i]; i++) {
		if (p + i >= N) return ORG_NONE;
		int c = S[p + i];
		if (!i ? c != '<' && c != '[' : form[i] == 'd' ? !digit(p + i) : c != '-') return ORG_NONE;
	}
	size_t q = p + 11;
	if (q < N && (S[q] == ']' || S[q] == '>')) return q;
	if (q >= N || S[q] != ' ') return ORG_NONE;
	while (++q < N && S[q] != '\n' && S[q] != ']' && S[q] != '>') ;
	return q < N && S[q] != '\n' ? q : ORG_NONE;
}
/* [012]?[0-9]:[0-5][0-9] at p, below lim: its end, else 0; the hour's
   digits [p, *hz).  The two readings of [012]? never both fit. */
static size_t hm(size_t p, size_t lim, size_t *hz)
{
	size_t h = p + 1;
	if (h < lim && digit(h)) { if (S[p] > '2') return 0; h++; }
	if (!(p < lim && digit(p) && h + 2 < lim && S[h] == ':' && S[h + 1] >= '0' && S[h + 1] <= '5' && digit(h + 2)))
		return 0;
	*hz = h;
	return h + 3;
}
/* org-parse-time-string with nodefault on [p, lim): org-ts-regexp0 is
   found at p + 1, where the timestamp's date is */
static void tstime(size_t p, size_t lim, int *y, int *m, int *d, int *h, int *mi)
{
	p++;
	*y = num(p, p + 4); *m = num(p + 5, p + 7); *d = num(p + 8, p + 10);
	*h = *mi = -1;
	size_t q = p + 10, r = q;
	while (r < lim && S[r] == ' ') r++;
	if (r > q) {   /* \( +[^]+0-9>\r\n -]+\)? */
		size_t w = r;
		while (w < lim && !strchr("]+0123456789>\r\n -", S[w])) w++;
		if (w > r) q = w;
	}
	for (r = q; r < lim && S[r] == ' '; r++) ;
	if (r == q || !digit(r)) return;   /* \( +\([0-9]\{1,2\}\):\([0-9]\{2\}\)\)? */
	size_t c = r + 1 < lim && digit(r + 1) ? r + 2 : r + 1;
	if (c + 2 < lim && S[c] == ':' && digit(c + 1) && digit(c + 2)) { *h = num(r, c); *mi = num(c + 1, c + 3); }
}
static char unit(int c) { return c == 'h' || c == 'd' || c == 'w' || c == 'm' ? (char)c : 'y'; }
static int unitc(size_t p, size_t lim, int fold)
{
	return p < lim && (strchr("hdwmy", S[p]) || (fold && strchr("HDWMY", S[p])));
}

static int ts(size_t p, int fold, OrgTs *t)
{
	size_t c, ds, de, r = ORG_NONE, z, e2 = ORG_NONE;
	int diary = 0;
	if ((c = ts_both(p)) == ORG_NONE) {   /* <%%([^>\n]+)\([^\n>]*\)> */
		if (!lit(p, "<%%(")) return 0;
		for (z = p + 4; z < N && S[z] != '>' && S[z] != '\n'; z++) if (S[z] == ')' && z > p + 4) r = z;
		if (z >= N || S[z] != '>' || r == ORG_NONE) return 0;
		diary = 1;
		ds = r + 1; de = z;
		for (c = p + 3; S[c] != ']' && S[c] != '>'; c++) ;   /* the raw value's .*?[]>] */
	} else ds = p, de = c;
	z = c + 1;   /* \(?:--\(org-ts-regexp-both\)\)? */
	if (z + 1 < N && S[z] == '-' && S[z + 1] == '-' && (e2 = ts_both(z + 2)) != ORG_NONE) z = e2 + 1;
	memset(t, 0, sizeof *t);
	t->begin = p;
	while (z < N && (S[z] == ' ' || S[z] == '\t')) z++;
	t->end = z;
	z = e2 != ORG_NONE ? e2 + 1 : c + 1;   /* the raw value ends here */
	t->y = t->m = t->d = t->h = t->min = t->y2 = t->m2 = t->d2 = t->h2 = t->min2 = -1;
	int th = -1, tm = -1;
	size_t i, a, hz, hz2, q;
	for (i = ds; i < de; i++)   /* [012]?[0-9]:[0-5][0-9]\(-\([012]?[0-9]\):\([0-5][0-9]\)\) */
		if ((a = hm(i, de, &hz)) && a < de && S[a] == '-' && hm(a + 1, de, &hz2)) {
			th = num(a + 1, hz2); tm = num(hz2 + 1, hz2 + 3);
			break;
		}
	if (diary)   /* \([012]?[0-9]\):\([0-5][0-9]\)\(-\([012]?[0-9]\):\([0-5][0-9]\)\)? */
		for (i = ds; i < de; i++)
			if ((a = hm(i, de, &hz))) {
				t->h = num(i, hz); t->min = num(hz + 1, hz + 3);
				if (a < de && S[a] == '-' && hm(a + 1, de, &hz2)) { t->h2 = num(a + 1, hz2); t->min2 = num(hz2 + 1, hz2 + 3); }
				break;
			}
	int act = S[p] == '<';
	t->type = diary ? 'd' : (e2 != ORG_NONE || th >= 0) ? (act ? 'A' : 'I') : act ? 'a' : 'i';
	t->range = e2 != ORG_NONE ? 'd' : th >= 0 ? 't' : 0;
	if (diary) return 1;
	tstime(ds, de, &t->y, &t->m, &t->d, &t->h, &t->min);
	int y2 = -1, m2 = -1, d2 = -1, h2 = -1, mi2 = -1;
	if (e2 != ORG_NONE) tstime(c + 3, e2 + 1, &y2, &m2, &d2, &h2, &mi2);
	t->y2 = y2 >= 0 ? y2 : t->y; t->m2 = m2 >= 0 ? m2 : t->m; t->d2 = d2 >= 0 ? d2 : t->d;
	t->h2 = h2 >= 0 ? h2 : th >= 0 ? th : t->h;
	t->min2 = mi2 >= 0 ? mi2 : tm >= 0 ? tm : t->min;
	for (i = p; i < z; i++) {   /* \(\+\|\+\+\|\.\+\)\([0-9]+\)\([hdwmy]\)\(?:/\([0-9]+\)\([hdwmy]\)\)? */
		static const char *const k[] = { "+", "++", ".+" };
		int j;
		for (j = 0; j < 3; j++) {
			size_t l = strlen(k[j]);
			if (i + l > z || memcmp(S + i, k[j], l)) continue;
			a = digits(i + l, z);
			if (a > i + l && unitc(a, z, fold)) break;
		}
		if (j == 3) continue;
		size_t l = strlen(k[j]);
		t->rep = "+#."[j]; t->rep_n = num(i + l, a); t->rep_unit = unit(S[a]);
		if (a + 1 < z && S[a + 1] == '/' && (q = digits(a + 2, z)) > a + 2 && unitc(q, z, fold)) {
			t->rep_dl_n = num(a + 2, q); t->rep_dl_unit = unit(S[q]);
		}
		break;
	}
	for (i = p; i < z; i++) {   /* \(-\)?-\([0-9]+\)\([hdwmy]\) */
		if (S[i] != '-') continue;
		int first = i + 1 < z && S[i + 1] == '-' && (a = digits(i + 2, z)) > i + 2 && unitc(a, z, fold);
		q = i + 1 + first;
		if (!first && !((a = digits(q, z)) > q && unitc(a, z, fold))) continue;
		t->warn = first ? 'f' : 'a'; t->warn_n = num(q, a); t->warn_unit = unit(S[a]);
		break;
	}
	return 1;
}

int org_ts(const char *s, size_t n, size_t p, OrgTs *t)
{
	S = (const unsigned char *)s; N = n;
	return ts(p, 1, t);
}

/* org-element-planning-parser: case-fold-search nil */
int org_planning(const char *s, size_t n, const OrgNode *e, OrgTs t[3])
{
	S = (const unsigned char *)s; N = n;
	static const char *const k[] = { "CLOSED:", "DEADLINE:", "SCHEDULED:" };
	int got = 0;
	for (size_t p = e->begin; p < e->end; p++)
		for (int j = 0; j < 3; j++) {
			size_t l = strlen(k[j]);
			if (p + l > e->end || memcmp(S + p, k[j], l)) continue;
			p += l;
			while (p < e->end && blank(p)) p++;
			if (ts(p, 0, &t[j])) got |= 1 << j;
			else got &= ~(1 << j);
			p--;
			break;
		}
	return got;
}

/* org-element-clock-parser, case-fold-search on */
int org_clock(const char *s, size_t n, const OrgNode *e, OrgTs *t, size_t dur[2])
{
	S = (const unsigned char *)s; N = n;
	size_t p = e->begin, z = eol(p);
	while (!lit(p, "CLOCK:")) p++;
	p = blanks(p + 6);
	int got = ts(p, 1, t);
	dur[0] = dur[1] = p;
	for (; p + 3 <= z; p++)
		if (!memcmp(S + p, "=> ", 3)) {
			size_t a = blanks(p + 3), b = run(a), c = blanks(b);
			if (b > a && b <= z && c == z) { dur[0] = a; dur[1] = b; }
			break;
		}
	return got;
}

/* \[#\(org-priority-value-regexp\)\] at x, before e: the value (-1 when
   none) and in *end the position after it; fold: case-fold-search */
static int prio(size_t x, size_t e, int fold, size_t *end)
{
	if (x + 3 >= e || S[x] != '[' || S[x + 1] != '#') return -1;
	int c = fold ? up(S[x + 2]) : S[x + 2];
	if (c >= 'A' && c <= 'Z' && S[x + 3] == ']') return *end = x + 4, S[x + 2];
	if (digit(x + 2) && S[x + 3] == ']') return *end = x + 4, S[x + 2] - '0';
	if (digit(x + 2) && digit(x + 3) && x + 4 < e && S[x + 4] == ']') {
		int v = (S[x + 2] - '0') * 10 + S[x + 3] - '0';
		if (v >= 10 && v <= 64) return *end = x + 5, v;
	}
	return -1;
}
/* [ \t]+\(:\([[:alnum:]_@#%:]+\):\)[ \t]*$ ending the line [q, e): the
   group's first colon (ORG_NONE when none) and in *z the end of its last */
static size_t tags(size_t q, size_t e, size_t *z)
{
	size_t a, x;
	for (*z = e; *z > q && blank(*z - 1); --*z) ;
	for (a = *z; a > q && !blank(a - 1); a--) ;
	if (a == q || a + 3 > *z || S[a] != ':' || S[*z - 1] != ':') return ORG_NONE;
	for (x = a; x < *z && (S[x] == ':' || S[x] == '_' || S[x] == '@' || S[x] == '#' || S[x] == '%' ||
	                       IN(ALNUM, uni_cp(S, N, x))); x += uni_len(S, N, x)) ;
	return x == *z ? a : ORG_NONE;
}

/* org-element--headline-parse-title */
void org_head(const char *s, size_t n, size_t q, OrgHead *h)
{
	S = (const unsigned char *)s; N = n;
	size_t p = stars(q), e = eol(q);
	h->level = (int)(p - q);
	p = blanks(p);
	h->todo = -1;
	for (int i = 0; org_todo[i]; i++) {   /* case-sensitive, then " " or $ */
		size_t k = strlen(org_todo[i]);
		if (p + k <= N && !memcmp(S + p, org_todo[i], k) && (ateol(p + k) || S[p + k] == ' ')) {
			h->todo = i;
			p = blanks(p + k);
			break;
		}
	}
	h->pri = -1;                          /* .*?(\[#(org-priority-value-regexp)\] ?) */
	for (size_t x = p; x + 3 < e; x++)
		if ((h->pri = prio(x, e, 1, &p)) >= 0) { if (p < N && S[p] == ' ') p++; break; }
	h->commented = 0;
	if (p + 7 <= N && !memcmp(S + p, "COMMENT", 7) && (ateol(p + 7) || S[p + 7] == ' ')) {
		h->commented = 1;
		p = blanks(p + 7);
	}
	/* tags: [ \t]+(:([[:alnum:]_@#%:]+):)[ \t]*$ searched from before the
	   title's blanks */
	size_t ts = p, te = e, z, a = tags(q, e, &z), from = ts;
	h->ga = h->gz = 0;
	while (from > q && blank(from - 1)) from--;
	if (a != ORG_NONE && from < a) {
		for (te = a; te > from && blank(te - 1); te--) ;
		h->ga = a + 1; h->gz = z - 1;
	}
	size_t lo = ts < te ? ts : te, hi = ts < te ? te : ts;
	while (lo < hi && (blank(lo) || S[lo] == '\r' || S[lo] == '\n')) lo++;
	while (hi > lo && (blank(hi - 1) || S[hi - 1] == '\r')) hi--;
	h->ta = lo; h->tz = hi;
}

/* ---- colours: Org's font-lock (org-set-font-lock-defaults) ----

   Each line gets the faces of Org's font-lock keywords that a whole-buffer
   fontification gives it, in the keywords' order, so the last one to set a
   character's face is the face it shows (Emacs's top face).  Only the meta
   lines and blocks keyword reads across lines: a block runs from its begin
   line to its end line or to the next line of stars, so its state at a
   headline is always outside, and a scan starts at the headline before the
   window (from the parse) or where the last one stopped. */

static unsigned char *F;   /* the faces of [FA, FZ) */
static size_t FA, FZ;
static void set(size_t a, size_t b, int f)
{
	for (a = a < FA ? FA : a; a < b && a < FZ; a++) F[a - FA] = (unsigned char)f;
}
static int alpha(size_t p) { return p < N && up(S[p]) >= 'A' && up(S[p]) <= 'Z'; }
static size_t letters(size_t p) { while (alpha(p)) p++; return p; }
static int word(size_t p) { return p < N && IN(WORD, uni_cp(S, N, p)); }
static int is(size_t p, const char *k) { return p + strlen(k) <= N && !memcmp(S + p, k, strlen(k)); }
static int one(size_t p, const char *k) { return p < N && S[p] && strchr(k, S[p]); }

/* org-fontify-meta-lines-and-blocks-1's state at the line at: st 0
   outside, 1 in a block whose end line (or line of stars) begins at end
   (quoting: its content is org-block), 2 stopped (a block without an end:
   the keyword's matcher returned nil) */
typedef struct { size_t at, end; int st, quoting; } Scan;

/* ^[ \t]*#\+end_NAME\> or ^\*+[[:space:]] at m, NAME at [g, g + len) */
static int block_end(size_t m, size_t g, size_t len)
{
	size_t b = blanks(m);
	return (stars(m) > m && sp(stars(m))) ||
	       (lit(b, "#+END") && b + 5 + len <= N && same(b + 5, g, len) && !word(b + 5 + len));
}

/* org-fontify-meta-lines-and-blocks-1 at the line c->at; on to the next.
   Returns where Org's taking display and invisible off the line ends, 0
   when it does not (org-fontify-meta-lines-and-blocks-1's
   remove-text-properties: a quoting block's lines up to its end line's
   match, a #+ line other than a block's begin, a comment and the document
   info) */
static size_t meta(Scan *c)
{
	size_t l = c->at, e = eol(l), nl = nextl(l), q = blanks(l);
	c->at = nl;
	if (c->st == 2) return 0;
	if (c->st == 1) {
		if (l < c->end) { if (c->quoting) set(l, nl, FACE_ORG_BLOCK); }
		else {
			if (S[l] != '*') set(l, e < N ? e + 1 : e, FACE_ORG_BLOCK_LINE);
			c->st = 0;
		}
		return !c->quoting ? 0 : l < c->end || S[l] != '*' ? nl : stars(l) + 1;
	}
	/* ^[ \t]*#\(\(+[a-zA-Z]+:?\|[ \t]\|$\)\(_[a-zA-Z]+\)?\)[ \t]*... */
	if (q >= e || S[q] != '#') return 0;
	size_t g3 = q + 1, z3 = g3, z4;
	if (g3 < e && S[g3] == '+' && alpha(g3 + 1)) z3 = letters(g3 + 1), z3 += z3 < e && S[z3] == ':';
	else if (blank(g3)) z3 = g3 + 1;
	else if (g3 < e) return 0;
	z4 = z3 < e && S[z3] == '_' && alpha(z3 + 1) ? letters(z3 + 1) : z3;
	if (z4 > z3 && z3 - g3 == 6 && lit(g3, "+begin")) {
		size_t m;
		for (m = nl; m < N && !block_end(m, z3, z4 - z3); m = nextl(m)) ;
		if (m >= N) { c->st = 2; return 0; }
		static const char *const quoting[] = { "src", "example", "export" };
		c->quoting = 0;
		for (int i = 0; i < 3; i++)
			c->quoting |= z4 - z3 - 1 == strlen(quoting[i]) && lit(z3 + 1, quoting[i]);
		set(l, nl, FACE_ORG_BLOCK_LINE);
		c->st = 1; c->end = m;
		return c->quoting ? nl : 0;
	}
	static const char *const info[] = { "+title:", "+subtitle:", "+author:", "+email:", "+date:" };
	for (int i = 0; i < 5; i++)
		if (z4 == z3 && z3 - g3 == strlen(info[i]) && lit(g3, info[i])) {
			set(l, z3, FACE_COMMENT);
			set(blanks(z3), e < N ? e + 1 : e, i ? FACE_ORG_INFO : FACE_MARKUP_HEADING);
			return 0;
		}
	if (z3 - g3 >= 8 && lit(g3, "+caption")) {
		/* [ \t]*#\+caption\(\[.*\]\)?:[ \t]* : when it fails, the match
		   data are still the whole line's */
		size_t k = g3 + 8, x = ORG_NONE;
		if (k < e && S[k] == '[')
			for (size_t j = e; j > k + 1 && x == ORG_NONE; j--) if (S[j - 1] == ']' && j < e && S[j] == ':') x = j + 1;
		if (x == ORG_NONE && k < e && S[k] == ':') x = k + 1;
		if (x == ORG_NONE) { set(l, e, FACE_COMMENT); return nl; }
		set(l, x, FACE_COMMENT);
		set(blanks(x), e, FACE_ORG_BLOCK);
		return nl;
	}
	set(l, e, FACE_COMMENT);   /* a comment, or any other #+ line */
	return S[g3] == '+' ? nl : 0;
}

static int level(size_t n)   /* org-get-level-face, org-cycle-level-faces */
{
	static const unsigned char f[8] = { FACE_MARKUP_HEADING_1, FACE_MARKUP_HEADING_2, FACE_MARKUP_HEADING_3,
		FACE_MARKUP_HEADING_4, FACE_MARKUP_HEADING_5, FACE_MARKUP_HEADING_6, FACE_ORG_LEVEL_7, FACE_ORG_LEVEL_8 };
	return f[(n - 1) % 8];
}
/* \(:?=[^|\n]*\) after " *" at p: the group's start, or ORG_NONE */
static size_t formula(size_t p, size_t e)
{
	p = spaces(p);
	return p < e && (S[p] == '=' || (S[p] == ':' && p + 1 < e && S[p + 1] == '=')) ? p : ORG_NONE;
}
/* the todo keyword (index in org_todo) at p followed by " ", or blanks to
   the end of the line e; -1 when none */
static int todo(size_t p, size_t e, size_t *k)
{
	for (int i = 0; org_todo[i]; i++) {
		*k = p + strlen(org_todo[i]);
		if (*k <= e && is(p, org_todo[i]) && (blanks(*k) == e || S[*k] == ' ')) return i;
	}
	return -1;
}
static int dcolon(size_t x, size_t e) { return x + 2 <= e && S[x] == ':' && S[x + 1] == ':' && (x + 2 == e || blank(x + 2)); }

/* The line l's faces, keyword by keyword in org-font-lock-keywords' order */
static void fontify(Scan *c)
{
	size_t l = c->at, e = eol(l), nl = e < N ? e + 1 : e, b = blanks(l), q = stars(l), p, x, z;
	int hl = q > l && q < N && S[q] == ' ';
	if (hl) set(l, e, level(q - l));   /* ^\(\**\)\(\* \)\(.*\) */
	/* ^[ \t]*\(\(|\|\+-[-+]\).*\S-\)\n? as org-table-row, its group as
	   org-table: the same face here */
	if (b < e && (S[b] == '|' || (is(b, "+-") && one(b + 2, "-+")))) {
		for (z = 0, p = b + (S[b] == '|' ? 1 : 3); p < e; p += uni_len(S, N, p)) if (!sp(p)) z = p + uni_len(S, N, p);
		if (z) set(l, z == e ? nl : z, FACE_ORG_TABLE);
	}
	if (b < e && S[b] == '|') {
		/* ^[ \t]*|\(?:.*?|\)? *\(:?=[^|\n]*\) */
		for (x = ORG_NONE, p = b + 1; p < e && x == ORG_NONE; p++) if (S[p] == '|') x = formula(p + 1, e);
		if (x == ORG_NONE) x = formula(b + 1, e);
		for (z = x + 1; x != ORG_NONE && z < e && S[z] != '|'; z++) ;
		if (x != ORG_NONE) set(x, z, FACE_ORG_FORMULA);
		p = spaces(b + 1);   /* ^[ \t]*| *\([#*]\) *| */
		if (one(p, "#*") && (x = spaces(p + 1)) < e && S[x] == '|') set(p, p + 1, FACE_ORG_FORMULA);
		/* ^[ \t]*|\( *\([$!_^/]\) *|.*\)| */
		if (one(p, "$!_^/") && (x = spaces(p + 1)) < e && S[x] == '|') {
			for (z = e; z > x + 1 && S[z - 1] != '|'; z--) ;
			if (z > x + 1) set(b + 1, z - 1, FACE_ORG_FORMULA);
		}
	}
	for (p = l; p < e; p++)   /* | *\(<[lrc]?[0-9]*>\) */
		if (S[p] == '|' && (x = spaces(p + 1)) < e && S[x] == '<') {
			z = digits(x + 1 + one(x + 1, "lrc"), e);
			if (z < e && S[z] == '>') set(x, z + 1, FACE_ORG_FORMULA), p = z;
		}
	/* org-property-re: :KEY: as org-special-keyword, the value as
	   org-property-value (no colour) */
	if ((z = key(b, e))) {
		set(b, z, FACE_KEYWORD);
		for (x = e, p = blanks(z); x > p && blank(x - 1); x--) ;
		set(p, x, FACE_NONE);
	}
	if (drawerl(l)) set(b, names(b + 1) + 1, FACE_ORG_DRAWER);
	p = l;   /* ^&?%%(.*\|<%%([^>\n]*?> */
	if (lit(l + (S[l] == '&'), "%%(")) set(l, e, FACE_ORG_DATE), p = e;
	for (; p + 4 <= e; p++)
		if (is(p, "<%%(")) {
			for (x = p + 4; x < e && S[x] != '>'; x++) ;
			if (x == e) break;   /* none closes later ones either */
			set(p, x + 1, FACE_ORG_DATE), p = x;
		}
	if (hl && (x = tags(l, e, &z)) != ORG_NONE) set(x, z, FACE_ORG_TAG);   /* org-activate-tags */
	size_t lc = e;   /* after the line's last ] or >: a date needs one at p + 11 or later */
	while (lc > l && S[lc - 1] != ']' && S[lc - 1] != '>') lc--;
	for (p = l; p + 12 <= lc; p++) {   /* org-activate-dates: org-tsr-regexp-both */
		if ((S[p] != '<' && S[p] != '[') || (x = ts_both(p)) == ORG_NONE) continue;
		for (z = x + 1; z < e && z < x + 4 && S[z] == '-'; z++) ;
		if (z > x + 1 && (z = ts_both(z)) != ORG_NONE) x = z;
		/* Org stops dating the whole buffer at one preceded by "[", a
		   live Emacs only the chunk it fontifies: this goes on */
		if (!p || S[p - 1] != '[') set(p, x + 1, FACE_ORG_DATE);
		p = x;
	}
	if (q > l && (p = spaces(q)) > q) {   /* org-heading-keyword-regexp-format */
		int i = todo(p, e, &x);
		if (i >= 0) set(p, x, i < org_done ? FACE_ORG_TODO : FACE_COMMENT);
		if (i >= org_done) {   /* org-headline-done */
			for (z = e, x = spaces(x); z > x && blank(z - 1); z--) ;
			set(x, z, FACE_COMMENT);
		}
	}
	if (q > l)   /* ^\*+.*?\(\[#\(V\)\] ?\) */
		for (p = q; p + 3 < e; p++)
			if (prio(p, e, 0, &x) >= 0) { set(p, x, FACE_ORG_PRIORITY); break; }
	for (p = l; p < e; p++) {   /* \<DEADLINE: \<SCHEDULED: \<CLOSED: \<CLOCK: */
		static const char *const k[] = { "DEADLINE:", "SCHEDULED:", "CLOSED:", "CLOCK:" };
		if (p > l && word(uni_prev(S, N, p))) continue;
		for (int i = 0; i < 4; i++)
			if (is(p, k[i])) set(p, p + strlen(k[i]), FACE_KEYWORD);
	}
	/* org-list-full-item-re's checkbox: ^[ \t]*\(\(?:[-+*]\|\(?:[0-9]+\|[A-Za-z]\)[.)]\)\(?:[ \t]+\|$\)\)
	   \(?:\[@\(?:start:\)?\([0-9]+\|[A-Za-z]\)\][ \t]*\)?\(\[[ X-]\]\)\(?:[ \t]+\|$\) */
	x = one(b, "-+*") ? b + 1 : ORG_NONE;
	if (digit(b) || alpha(b)) x = digit(b) ? digits(b, e) : b + 1, x = one(x, ".)") ? x + 1 : ORG_NONE;
	if (x != ORG_NONE && (x == e || blank(x))) {
		x = blanks(x);
		if (is(x, "[@")) {
			size_t v = x + 2 + 6 * is(x + 2, "start:");
			p = digit(v) ? digits(v, e) : alpha(v) ? v + 1 : v;
			if (p > v && p < e && S[p] == ']') x = blanks(p + 1);
		}
		if (is(x, "[") && one(x + 1, " X-") && is(x + 2, "]") && (x + 3 == e || blank(x + 3)))
			set(x, x + 3, FACE_ORG_CHECKBOX);
	}
	for (p = l; p < e; p++) {   /* \[\([0-9]*%\)\]\|\[\([0-9]*\)/\([0-9]*\)\] */
		if (S[p] != '[') continue;
		x = digits(p + 1, e);
		if (x + 1 < e && S[x] == '%' && S[x + 1] == ']') {
			set(p, x + 2, x == p + 4 && !memcmp(S + p + 1, "100", 3) ? FACE_COMMENT : FACE_ORG_TODO);
			p = x + 1;
		} else if (x < e && S[x] == '/' && (z = digits(x + 1, e)) < e && S[z] == ']') {
			set(p, z + 1, x > p + 1 && x - p - 1 == z - x - 1 && !memcmp(S + p + 1, S + x + 1, z - x - 1) ? FACE_COMMENT : FACE_ORG_TODO);
			p = z;
		}
	}
	/* \(?:^[ \t]*[-+]\|^[ \t]+[*]\)[ \t]+\(.*?[ \t]+::\)\([ \t]+\|$\) as org-list-dt */
	if (b < e && (S[b] == '-' || S[b] == '+' || (S[b] == '*' && b > l)) && blank(b + 1)) {
		size_t v = blanks(b + 1);
		for (z = ORG_NONE, p = v; p < e && z == ORG_NONE; p++) if (blank(p) && dcolon(blanks(p), e)) z = blanks(p) + 2;
		if (z != ORG_NONE) set(v, z, FACE_ORG_CHECKBOX);
		else if (v >= b + 3 && dcolon(v, e)) set(v - 1, v + 2, FACE_ORG_CHECKBOX);
	}
	for (p = l; p + 1 < e; p++)   /* \(@@\)\([a-z-]+:\).*?\(@@\): an export snippet */
		if (is(p, "@@")) {
			for (x = p + 2; x < e && (S[x] == '-' || (S[x] >= 'a' && S[x] <= 'z')); x++) ;
			if (x == p + 2 || !is(x, ":")) continue;
			for (z = x + 1; z + 1 < e && !is(z, "@@"); z++) ;
			if (z + 1 >= e) continue;
			set(p, p + 2, FACE_COMMENT); set(p + 2, x + 1, FACE_ORG_TAG); set(z, z + 2, FACE_COMMENT);
			p = z + 1;
		}
	if (hl)   /* ^\*+ \(.*:ARCHIVE:.*\) as org-archived */
		for (p = q + 1; p + 9 <= e; p++) if (is(p, ":ARCHIVE:")) { set(q + 1, e, FACE_COMMENT); break; }
	if (fixedl(l)) set(b, nl, FACE_MARKUP_RAW);   /* org-activate-code */
	meta(c);
	/* ^\*+\(?: +TODO\)?\(?: +\[#V\]\)? +\(COMMENT\)\(?: \|$\) */
	for (int k = 0; q > l && k < 4; k++) {   /* k: bit 0 no keyword, bit 1 no priority */
		p = q;
		if (!(k & 1)) { if ((x = spaces(p)) == p || todo(x, e, &z) < 0 || !one(z, " ")) continue; p = z; }
		if (!(k & 2)) { if ((x = spaces(p)) == p || prio(x, e, 0, &z) < 0) continue; p = z; }
		if ((x = spaces(p)) > p && x + 7 <= e && is(x, "COMMENT") && (x + 7 == e || S[x + 7] == ' ')) {
			set(x, x + 7, FACE_KEYWORD);
			break;
		}
	}
}

struct OrgFx {
	const Text *t;
	char *s;               /* the text at the last update, and its parse */
	size_t n, cap, k;
	OrgNode *e;
	int dirty;             /* edited since: [lo, ho) of s is [lo, hc) of t */
	size_t lo, ho, hc;
	Scan c;                /* the scan's state at the last line it coloured */
	unsigned char *lf;     /* that line's faces (the core asks a long line in pieces) */
	size_t lcap;           /* as long as the longest line, unless out of memory */
	Scan cs;               /* conceal's: the scan's state at the last line it showed */
	size_t *em, emcap;     /* conceal's emphasis matches, begin and end (they grow, then stay) */
};

OrgFx *orgfx_new(const Text *t)
{
	OrgFx *f = calloc(1, sizeof *f);
	if (f) f->t = t, f->dirty = 1, f->hc = text_len(t), f->c.at = f->cs.at = ORG_NONE;
	return f;
}

void orgfx_free(OrgFx *f)
{
	if (f) free(f->s), free(f->e), free(f->lf), free(f->em);
	free(f);
}

void orgfx_edit(OrgFx *f, const Text *t, size_t pos, size_t ndel, size_t nins)
{
	f->t = t;
	if (!f->dirty) f->lo = f->ho = f->hc = pos, f->dirty = 1;
	if (pos < f->lo) f->lo = pos;
	if (pos + ndel > f->hc) f->ho += pos + ndel - f->hc, f->hc = pos + ndel;
	f->hc = f->hc - ndel + nins;
}

int orgfx_update(OrgFx *f, int (*stop)(void))
{
	(void)stop;   /* org_edit reparses a subtree in one go (1.7 ms at most on the corpus, measured) */
	size_t n = text_len(f->t);
	if (!f->dirty) return 0;
	if (n >= f->cap) {   /* one more: never a null s */
		char *s = realloc(f->s, n + 1);
		if (!s) return 0;
		f->s = s; f->cap = n + 1;
	}
	memmove(f->s + f->hc, f->s + f->ho, f->n - f->ho);
	text_read(f->t, f->lo, f->s + f->lo, f->hc - f->lo);
	f->n = n;
	org_edit(&f->e, &f->k, f->s, n, f->lo, f->ho - f->lo, f->hc - f->lo);   /* out of memory: no parse, scans from the start */
	f->dirty = 0;
	f->c.at = f->cs.at = ORG_NONE;
	S = (const unsigned char *)f->s; N = n;
	for (size_t l = bol(f->lo), m; l < n && l <= f->hc; l = nextl(l))   /* the lines the edits touched */
		if ((m = nextl(l) - l) > f->lcap) {
			unsigned char *g = realloc(f->lf, m);
			if (g) f->lf = g, f->lcap = m;
		}
	return 1;
}

/* the headline whose line holds p or comes before it, ORG_NONE before the first */
static size_t head_at(const OrgFx *f, size_t p)
{
	size_t i = f->k ? first(f->e, f->k, p + 1) : 0;
	if (!i) return ORG_NONE;
	for (i--; f->e[i].type != ORG_HEADLINE && f->e[i].parent != ORG_NONE; i = f->e[i].parent) ;
	return f->e[i].type == ORG_HEADLINE ? f->e[i].begin : ORG_NONE;
}

/* the faces of [a, z) of the text at the last update into out */
/* the scan's state at the line L, from k when it is on the way (meta's
   set must write nowhere) */
static Scan scan_at(OrgFx *f, const Scan *k, size_t L)
{
	size_t R = head_at(f, L);   /* a headline's line is outside any block */
	if (R == ORG_NONE) R = 0;
	Scan c = k->at <= L && k->at >= R ? *k : (Scan){ R, 0, 0, 0 };
	while (c.at < L) meta(&c);
	return c;
}
static void paint(OrgFx *f, size_t a, size_t z, unsigned char *out)
{
	S = (const unsigned char *)f->s; N = f->n; F = out; FA = a; FZ = z;
	if (a >= z || a >= N) return;
	size_t L = bol(a);
	Scan c = scan_at(f, &f->c, L);
	while (c.at < z && c.at < N) {
		size_t l = c.at, nl = nextl(l), u = a > l ? a : l, v = z < nl ? z : nl;
		if (f->c.at == l) { FZ = 0; c = f->c; meta(&c); }   /* coloured: the scan steps over it */
		else if (nl - l > f->lcap) { F = out; FA = a; FZ = z; f->c.at = ORG_NONE; fontify(&c); continue; }   /* out of memory */
		else { F = f->lf; FA = l; FZ = nl; memset(F, 0, nl - l); f->c = c; fontify(&c); }
		memcpy(out + (u - a), f->lf + (u - l), v - u);
	}
}

void orgfx_faces(OrgFx *f, size_t a, size_t z, unsigned char *face)
{
	if (!f->dirty) { paint(f, a, z, face); return; }
	/* before the update: the old text's colours where it is unchanged */
	paint(f, a, z < f->lo ? z : f->lo, face);
	if (z > f->hc) {
		size_t b = a > f->hc ? a : f->hc;
		paint(f, b - f->hc + f->ho, z - f->hc + f->ho, face + (b - a));
	}
}

/* nvim-orgmode's foldexpr: a line's level is its subtree's headline's
   stars (org-current-level), 0 before the first headline */
int orgfx_foldexpr(OrgFx *f, size_t line, int *start)
{
	orgfx_update(f, NULL);
	size_t p = text_line_start(f->t, line), h = head_at(f, p);
	S = (const unsigned char *)f->s; N = f->n;
	*start = h == p;
	return h == ORG_NONE ? 0 : (int)(stars(h) - h);
}

/* ---- conceal: what Org shows in place of bytes (org-pretty-entities,
   org-hide-emphasis-markers, org-use-sub-superscripts '{}, Davy's
   kittymax), from org-do-emphasis-faces, org-fontify-entities and
   org-raise-scripts in font-lock's order, then what
   org-fontify-meta-lines-and-blocks and org-activate-code take off again.
   Known differences: a script shows as typed unless every character it
   shows has a Unicode superscript or subscript (a cell grid cannot raise
   text), or when its braces span lines, and its braces stop at the second
   newline (Org's [^{}]*? does not); emphasis never ends on a CLOCK line;
   a script in a link is raised (no links yet) ---- */

#include "entities.h"
#include "scripts.h"

static Conceal *C;
static size_t CN, CCAP, CL, CE;   /* the spans so far, their room, the line [CL, CE) */
static size_t Prot;   /* Org takes display and invisible off the line before it: only compositions show */
static void hide(size_t a, size_t z, const char *s)
{
	if (a >= CL && z <= CE && CN < CCAP && (*s || a >= Prot)) C[CN++] = (Conceal){ a, z, s };
}
/* org-element-paragraph-separate at q, in a string ending at z ($ there);
   its CLOCK line is left out */
static int separate(size_t q, size_t z)
{
	size_t b = blanks(q), x;
	if ((x = stars(q)) > q && x < z && S[x] == ' ') return 1;
	if (is(q, "[fn:") && (x = names(q + 4)) > q + 4 && x < z && S[x] == ']') return 1;
	if (is(q, "%%(") || b >= z || ateol(b) || S[b] == '|' || is(b, "\\begin{")) return 1;
	x = b + 1;
	if (S[b] == '#') {
		if (x >= z || ateol(x) || S[x] == ' ') return 1;
		if (S[x] != '+') return 0;
		if (is(x + 1, "BEGIN_") && x + 7 < z && !sp(x + 7)) return 1;
		for (x++; x < z && !sp(x) && S[x] != ':' && S[x] != '['; x++) ;
		if (x < z && S[x] == '[') while (x < z && !ateol(x) && S[x] != ']') x++;
		return x < z && S[x] == ']' ? x + 1 < z && S[x + 1] == ':' : x < z && S[x] == ':' && x > b + 2;
	}
	if (S[b] == ':') return x >= z || ateol(x) || S[x] == ' ' || ((x = names(x)) > b + 1 && x < z && S[x] == ':' && ((x = blanks(x + 1)) >= z || ateol(x)));
	if (S[b] == '+' && is(x, "-")) {   /* \+\(?:-+\+\)+[ \t]*$ */
		while (x < z && S[x] == '-') { while (x < z && S[x] == '-') x++; if (x >= z || S[x] != '+') return 0; x++; }
		return (x = blanks(x)) >= z || ateol(x);
	}
	for (x = b; x < z && S[x] == '-'; x++) ;
	if (x - b >= 5 && ((x = blanks(x)) >= z || ateol(x))) return 1;
	x = one(b, "-+*") ? b + 1 : digit(b) && one(digits(b, z), ".)") ? digits(b, z) + 1 : 0;
	return x && (x >= z || blank(x) || ateol(x));
}
/* [ \t]*\(|[-+]+|?\|\+[-+]+\+\)[ \t]*$ at the line l: a table rule */
static int rule(size_t l)
{
	size_t b = blanks(l), x;
	for (x = b + 1; one(x, "-+"); x++) ;
	if (S[b] == '|') return x > b + 1 && ateol(blanks(x + is(x, "|")));
	return S[b] == '+' && x >= b + 3 && S[x - 1] == '+' && ateol(blanks(x));
}
static int post(size_t p) { return ateol(p) || sp(p) || one(p, "-.,:!?;'\")}\\["); }
/* org-emph-re or org-verbatim-re's match with its marker at p, before
   lim (the second newline after p): the closing marker, or 0.  The first
   closer that fits does not depend on p, so each marker's last one (up
   to E2, the end of the line after CL) is kept for the openers after it:
   one pass over the lines for all of them */
static size_t Cx[6], E2;
static size_t emph_close(size_t p, size_t lim)
{
	size_t k = (size_t)(strchr("*/_+=~", S[p]) - "*/_+=~"), c = Cx[k];
	if (ateol(p + 1) || sp(p + 1)) return 0;
	if (c < p + 2)
		for (c = p + 2; c < E2 && !(S[c] == S[p] && !sp(c - 1) && post(c + 1)); c++) ;
	return (Cx[k] = c) < lim ? c : 0;
}
/* from the line before CL: emphasis there may end on CL */
static void emphasis(OrgFx *f, size_t from, size_t *ek)
{
	size_t bar = 0, rm = ORG_NONE;   /* the next | after p; the line rule() saw */
	int rv = 0;
	E2 = CE < N ? eol(CE + 1) : N;
	memset(Cx, 0, sizeof Cx);
	for (size_t p = from; p < CE; p++) {
		if (!one(p, "*/_+=~") || (p > 0 && S[p - 1] != '\n' && !sp(p - 1) && !one(p - 1, "-('\"{"))) continue;
		/* the match starts at the newline before a line's first marker,
		   so the line above is the one org-match-line looks at */
		size_t l = p < CL ? from : CL, m = p == l && p ? bol(p - 1) : l, b = blanks(m), nl = p < CL ? CL - 1 : CE, c;
		if (S[p] == '+' && m != rm) rm = m, rv = rule(m);
		if ((S[p] == '+' && rv) || (S[p] == '*' && p == l && headline(l))) continue;
		if (!(c = emph_close(p, p < CL ? CE : E2)) || separate(p, c + 1)) continue;
		if (nl < c && separate(nl + 1, c + 1)) continue;   /* string-match-p: ^ after the newline too */
		if (bar <= p) for (bar = p + 1; bar < E2 && S[bar] != '|'; bar++) ;
		if (S[b] == '|' && bar < c) continue;
		if (S[p] == '=' || S[p] == '~')   /* verbatim: no earlier markup inside shows hidden */
			for (size_t i = CN; i-- > 0; ) if (C[i].a >= p && C[i].z <= c + 1) C[i] = C[--CN];
		if (2 * *ek == f->emcap) {   /* out of memory: later scripts inside emphasis are raised */
			size_t *g = realloc(f->em, (2 * f->emcap + 64) * sizeof *g);
			if (g) f->em = g, f->emcap = 2 * f->emcap + 64;
		}
		if (2 * *ek < f->emcap) f->em[2 * *ek] = p, f->em[2 * *ek + 1] = c + 1, ++*ek;
		if (!comment(l)) hide(p, p + 1, ""), hide(c, c + 1, "");
	}
}
static const char *entity(size_t a, size_t z)
{
	size_t lo = 0, hi = sizeof entities / sizeof *entities;
	while (lo < hi) {
		size_t m = (lo + hi) / 2;
		int d = strncmp(entities[m][0], (const char *)S + a, z - a);
		if (!d && entities[m][0][z - a]) d = 1;
		if (!d) return entities[m][1];
		if (d < 0) lo = m + 1; else hi = m;
	}
	return NULL;
}
static int ent_alpha(size_t p) { return p < N && (alpha(p) || (S[p] >= 0x80 && IN(ALNUM, uni_cp(S, N, p)))); }
static void entities_at(size_t l)
{
	for (size_t p = l; p < CE; p++) {
		if (S[p] != '\\') continue;
		size_t x = 0, e;
		if (is(p + 1, "there4")) x = p + 7;
		else if (is(p + 1, "sup") && one(p + 4, "123")) x = p + 5;
		else if (is(p + 1, "frac") && one(p + 5, "13") && one(p + 6, "24")) x = p + 7;
		if (x && ent_alpha(x) && S[x] != '\n') x = 0;   /* the next alternative */
		if (!x && (x = letters(p + 1)) == p + 1) continue;
		if (ateol(x)) e = x;
		else if (is(x, "{}")) e = x + 2;
		else if (!ent_alpha(x)) e = x + uni_len(S, N, x);
		else continue;
		const char *u = comment(l) ? NULL : entity(p + 1, x);
		/* one character of one cell, or as typed (\zwj takes none) */
		if (u && uni_width((const unsigned char *)u, strlen(u), 0) == 1) hide(p, is(x, "{}") ? x + 2 : x, u);
		if (u) p = e - 2;
		else p = e - 1;
	}
}
static int by_cp(const void *k, const void *e)
{
	unsigned a = *(const unsigned *)k, b = ((const Script *)e)->c;
	return a < b ? -1 : a > b;
}
static const char *script(unsigned c, int up)
{
	const Script *x = up ? bsearch(&c, sup, sizeof sup / sizeof *sup, sizeof *sup, by_cp)
	                     : bsearch(&c, sub, sizeof sub / sizeof *sub, sizeof *sub, by_cp);
	return x ? x->s : NULL;
}
/* org-create-multibrace-regexp "{" "}" 3 at q: the closing brace, 0 when
   none: groups inside are all flat, or all hold flat groups only.  Its
   [^{}]*? crosses lines; here at most one newline */
static size_t braces(size_t q)
{
	int d = 0, kind = 0, g = 0, nl = 0;   /* kind: 1 flat groups, 2 nested */
	for (size_t p = q; p < N; p++) {
		if ((S[p] == '{' && ++d > 3) || (S[p] == '\n' && nl++)) return 0;
		if (S[p] == '{' && d == 3) g = 1;
		if (S[p] != '}') continue;
		if (--d == 0) return p;
		if (d == 1) { if (kind && kind != 1 + g) return 0; kind = 1 + g; g = 0; }
	}
	return 0;
}
/* [a, z) raised (up) or lowered: each character it shows as its Unicode
   form, one span each (put), or the spans it adds; -1 when a character
   has none, as a cell grid cannot raise text */
static int raised(size_t a, size_t z, int up, int put)
{
	int n = 0;
	for (size_t q = a, k, j; q < z; q = k) {
		for (j = CN; j-- > 0 && !(C[j].a <= q && q < C[j].z); ) ;
		k = j < CN ? C[j].z : q + uni_len(S, N, q);
		if (j < CN && !*C[j].s) continue;   /* a hidden emphasis marker */
		const char *s = script(j < CN ? uni_cp((const unsigned char *)C[j].s, strlen(C[j].s), 0) : uni_cp(S, N, q), up);
		if (!s) return -1;
		if (!put) n += j >= CN;
		else if (j < CN) C[j].s = s;   /* an entity: its script form */
		else hide(q, k, s);
	}
	return n;
}
/* from the line before: a script there may end on this line and cover a
   start here */
static void scripts(size_t from, size_t l, const size_t *em, size_t ek)
{
	size_t kz = key(blanks(l), CE), i = 0, ez = 0;   /* a property's key is org-special-keyword; the emphasis seen, its furthest end */
	for (size_t p = from; p + 2 < CE; p++) {
		if (sp(p) || !one(p + 1, "_^") || S[p + 2] != '{') continue;
		size_t z = braces(p + 2), m = p + 2;
		if (!z) continue;
		/* ^[ \t]*#[ +] as comment-p; a script over lines shows as typed */
		int ok = p >= l && p >= Prot && z < CE && !(S[blanks(l)] == '#' && one(blanks(l) + 1, " +")), up = S[p + 1] == '^', n;
		for (; i < ek && em[2 * i] <= m; i++) if (em[2 * i + 1] > ez) ez = em[2 * i + 1];
		if (ok && m >= ez && m >= kz && (n = raised(m + 1, z, up, 0)) >= 0 && CN + (size_t)n + 2 <= CCAP)
			hide(p + 1, m + 1, ""), raised(m + 1, z, up, 1), hide(z, z + 1, "");
		p = z + 1 < CE && one(z + 1, "_^") ? z - 1 : z;
	}
}
static int by_start(const void *a, const void *b)
{
	size_t x = ((const Conceal *)a)->a, y = ((const Conceal *)b)->a;
	return x < y ? -1 : x > y;
}
size_t orgfx_conceal(OrgFx *f, size_t line, Conceal *out, size_t cap)
{
	size_t ek = 0;
	orgfx_update(f, NULL);
	S = (const unsigned char *)f->s; N = f->n;
	CL = text_line_start(f->t, line); CE = eol(CL);
	C = out; CN = 0; CCAP = cap;
	if (CL >= N && N) return 0;
	size_t pl = CL ? prevl(CL) : 0, b = blanks(CL);
	Scan c = (FZ = 0, scan_at(f, &f->cs, pl));   /* a headline may end a block: from the line above */
	if (CL) meta(&c);
	f->cs = c;
	/* org-activate-code: ^[ \t]*:\(?: .*\|$\) */
	Prot = b < N && S[b] == ':' && (ateol(b + 1) || S[b + 1] == ' ') ? N : meta(&c);
	emphasis(f, pl, &ek);
	entities_at(CL);
	scripts(pl, CL, f->em, ek);
	qsort(C, CN, sizeof *C, by_start);
	size_t k = 0;   /* a marker two matches hide is hidden once; columns of the line */
	for (size_t i = 0; i < CN; i++) if (!k || C[i].a >= C[k - 1].z + CL) C[k++] = (Conceal){ C[i].a - CL, C[i].z - CL, C[i].s };
	return k;
}

/* ---- TODO states: org-todo as Davy's Emacs runs it (kittymax's
   kitty-org.el: org-log-done time, org-log-into-drawer t,
   org-enforce-todo-dependencies t; a note a state asks for is taken empty,
   as C-c C-c on the note buffer) ---- */

static char *W;        /* the text as org-todo changes it, step by step; S and N follow it */
static size_t WC;
static int oom;

static void wrep(size_t a, size_t b, const char *x, size_t k)   /* [a, b) becomes x[0, k) */
{
	size_t m = N - (b - a) + k;
	if (oom) return;
	if (m > WC) {
		char *w = realloc(W, m + m / 2 + 64);
		if (!w) { oom = 1; return; }
		W = w, WC = m + m / 2 + 64;
	}
	memmove(W + a + k, W + b, N - b);
	memcpy(W + a, x, k);
	N = m, S = (const unsigned char *)W;
}
static void wins(size_t a, const char *x) { wrep(a, a, x, strlen(x)); }

static size_t heading(size_t p)   /* org-back-to-heading */
{
	for (p = bol(p); !headline(p); p = prevl(p)) if (!p) return ORG_NONE;
	return p;
}
static size_t next_head(size_t p) { do p = nextl(p); while (p < N && !headline(p)); return p; }
static size_t hlevel(size_t h) { return stars(h) - h; }

/* org-todo's match at the space after a headline's stars: " +KW( +|[ \t]*$)",
   else " *"; its end, *k the keyword (-1 for none) */
static size_t kwmatch(size_t p, int *k)
{
	size_t q = spaces(p), r;
	if (q > p && (*k = todo(q, eol(q), &r)) >= 0) return S[r] == ' ' ? spaces(r) : blanks(r);
	*k = -1;
	return q;
}
static int kw_at(size_t h) { int k; kwmatch(stars(h), &k); return k; }   /* org-get-todo-state */
static int notdone(size_t h) { int k = kw_at(h); return k >= 0 && k < org_done; }

/* the line after the headline at h and its planning line (case-sensitive,
   as org-todo binds case-fold-search to nil, unless fold) */
static size_t after_planning(size_t h, int fold)
{
	size_t p = nextl(h), q = blanks(p);
	return (fold ? planningl(p) : is(q, "CLOSED:") || is(q, "DEADLINE:") || is(q, "SCHEDULED:")) ? nextl(p) : p;
}
static int propre(size_t p, int fold)   /* org-property-drawer-re at p */
{
	return fold ? propdrawer(p) : is(blanks(p), ":PROPERTIES:") && propdrawer(p);
}

/* org-entry-get h NAME without inheritance, read from h's property drawer
   (the last line naming it, any case, measured): the value's [*a, *z), 0
   when it has none */
static int prop(size_t h, const char *name, size_t *a, size_t *z)
{
	size_t p = after_planning(h, 0), k = strlen(name);
	int got = 0;
	if (!propre(p, 0)) return 0;
	for (p = nextl(p); !drawer_end(p); p = nextl(p)) {
		size_t q = blanks(p);
		if (S[q] != ':' || !lit(q + 1, name) || q + 1 + k >= N || S[q + 1 + k] != ':') continue;
		*a = blanks(q + k + 2), *z = eol(p), got = 1;
		while (*z > *a && blank(*z - 1)) --*z;
	}
	return got;
}
static int ordered(size_t h)   /* org-not-nil of the ORDERED property */
{
	size_t a, z;
	return prop(h, "ORDERED", &a, &z) && !(z - a == 3 && is(a, "nil"));
}
static size_t up_heading(size_t h)   /* org-up-heading-safe */
{
	size_t l = hlevel(h);
	while (h) if (headline(h = prevl(h)) && hlevel(h) < l) return h;
	return ORG_NONE;
}
static int notdone_between(size_t a, size_t b)   /* re-search-forward org-not-done-heading-regexp a..b */
{
	for (; a < b; a = nextl(a)) if (headline(a) && notdone(a)) return 1;
	return 0;
}

/* org-block-todo-from-children-or-siblings-or-parent */
static int blocked(size_t h, int from, int to)
{
	if (from >= org_done || to < org_done) return 0;
	size_t l = hlevel(h), p, q;
	for (q = next_head(h); q < N && hlevel(q) > l; q = next_head(q)) if (notdone(q)) return 1;
	for (p = h; (q = up_heading(p)) != ORG_NONE; p = q) {
		if (ordered(q) && notdone_between(nextl(q), p)) return 1;
		if (!notdone(q)) return 0;
	}
	return 0;
}

/* ---- dates, as Emacs's encode-time normalises them ---- */

static long fdiv(long a, long b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
static long days(long y, long m, long d)   /* since 1970-01-01; months past 12 roll over */
{
	y += fdiv(m - 1, 12), m -= 12 * fdiv(m - 1, 12);
	y -= m <= 2;
	long era = fdiv(y, 400), yoe = y - era * 400, doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
	return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}
static void civil(long z, int *y, int *m, int *d)
{
	z += 719468;
	long era = fdiv(z, 146097), doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	long doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
	*d = (int)(doy - (153 * mp + 2) / 5 + 1), *m = (int)(mp < 10 ? mp + 3 : mp - 9), *y = (int)(yoe + era * 400 + (*m <= 2));
}
/* "<2026-11-19 Thu 17:30>" for minute t (since the epoch), with the time
   when hm, extra x[0, k) before the closing bracket */
static size_t stamp(char *o, long t, int hm, int inactive, const char *x, size_t k)
{
	static const char *const wd[] = { "Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed" };
	long z = fdiv(t, 1440), mi = t - z * 1440;
	int y, m, d;
	civil(z, &y, &m, &d);
	size_t n = (size_t)sprintf(o, "%c%04d-%02d-%02d %s", inactive ? '[' : '<', y, m, d, wd[z - 7 * fdiv(z, 7)]);
	if (hm) n += (size_t)sprintf(o + n, " %02ld:%02ld", mi / 60, mi % 60);
	memcpy(o + n, x, k);
	o[n + k] = inactive ? ']' : '>';
	return n + k + 1;
}

/* org-ts-regexp1's date and optional day name and " H:MM" at p (after the
   bracket); its end, the minute it names in *t (0:00 without a time) */
static size_t tsbody(size_t p, long *t)
{
	if (!date(p)) return 0;
	size_t q = p + 10, r = spaces(q), u = r;
	while (u < N && !strchr("]+0123456789>\r\n -", S[u])) u++;
	if (u > r) q = u;
	long h = 0, mi = 0;
	if (q + 5 <= N && S[q] == ' ' && digit(q + 1)) {
		size_t c = digit(q + 2) ? q + 3 : q + 2;
		if (c + 3 <= N && S[c] == ':' && digit(c + 1) && digit(c + 2))
			h = num(q + 1, c), mi = num(c + 1, c + 3), q = c + 3;
	}
	*t = days(num(p, p + 4), num(p + 5, p + 7), num(p + 8, p + 10)) * 1440 + h * 60 + mi;
	return q;
}
/* the stamp org-ts-regexp3 matches at a ([<...]>, at most 16 bytes after
   the time), its end; 0 when none */
static size_t ts3(size_t a)
{
	long t;
	size_t q, c;
	if (a >= N || (S[a] != '<' && S[a] != '[') || !(q = tsbody(a + 1, &t))) return 0;
	for (c = q; c < N && c - q <= 16 && S[c] != ']' && S[c] != '>' && S[c] != '\n'; c++) ;
	return c < N && c - q <= 16 && (S[c] == ']' || S[c] == '>') ? c + 1 : 0;
}
/* org-in-regexp org-ts-regexp3 around q: the stamp's start, ORG_NONE */
static size_t ts_around(size_t q)
{
	for (size_t a = bol(q), z; a <= q; a = z ? z : a + 1)
		if ((z = ts3(a)) && z >= q) return a;
	return ORG_NONE;
}

/* (-HH:MM)?( +[.+]?-?[-+]N[hdwmy](/N[hdwmy])?)*[]>] at i, in a stamp
   ending at z: the group's end, 0 when no bracket follows it */
static size_t extra_at(size_t i, size_t z)
{
	size_t p = i, q, r;
	if (p + 6 < z && S[p] == '-' && S[p + 1] >= '0' && S[p + 1] <= '2' && digit(p + 2) && S[p + 3] == ':' &&
	    S[p + 4] >= '0' && S[p + 4] <= '5' && digit(p + 5)) p += 6;
	for (;;) {
		for (q = p; S[q] == ' '; q++) ;
		if (q == p) break;
		size_t c = 0, x, y;   /* [.+]?-?[-+]N[hdwmy], backtracking as the regexp does */
		for (x = S[q] == '.' || S[q] == '+'; !c && x != (size_t)-1; x--)
			for (y = S[q + x] == '-'; !c && y != (size_t)-1; y--)
				if (one(q + x + y, "-+") && (r = digits(q + x + y + 1, z)) > q + x + y + 1 && one(r, "hdwmy")) c = r;
		if (!c) break;
		p = c + 1;
		if (S[p] == '/' && (r = digits(p + 1, z)) > p + 1 && one(r, "hdwmy")) p = r + 1;
	}
	return S[p] == ']' || S[p] == '>' ? p : 0;
}

/* org-timestamp-change: the stamp at a moves by n minutes ('m'), hours
   ('h'), days ('d'), months ('M') or years ('y'); nodelay drops its " --Nu"
   delays.  Returns the end of the new stamp. */
static size_t tschange(size_t a, long n, char what, int nodelay)
{
	char o[96], x[32];
	size_t z = ts3(a), i, j, k = 0, e;
	long t;
	tsbody(a + 1, &t);
	for (i = a; !(e = extra_at(i, z)); i++) ;
	for (j = i; j < e; j++) {   /* without the delays " --Nu" */
		if (nodelay && S[j] == ' ' && j + 3 < e && S[j + 1] == '-' && S[j + 2] == '-' && digit(j + 3)) {
			size_t r = digits(j + 3, e);
			if (one(r, "hdwmy")) { j = r; continue; }
		}
		x[k++] = (char)S[j];
	}
	int hm = 0;   /* "^.{10}.*?[0-9]+:[0-9][0-9]" */
	for (j = a + 11; j + 2 < z && !hm; j++) hm = S[j] == ':' && digit(j - 1) && digit(j + 1) && digit(j + 2);
	if ((what == 'h' || what == 'm') && k >= 6 && x[0] == '-' && x[3] == ':') {   /* org-modify-ts-extra */
		long h = (x[1] - '0') * 10 + x[2] - '0' + (what == 'h' ? n : 0), mi = (x[4] - '0') * 10 + x[5] - '0' + (what == 'm' ? n : 0);
		if (mi < 0) mi += 60, h--;
		if (mi > 59) mi -= 60, h++;
		h -= 24 * fdiv(h, 24);
		x[1] = (char)('0' + h / 10), x[2] = (char)('0' + h % 10), x[4] = (char)('0' + mi / 10), x[5] = (char)('0' + mi % 10);
	}
	long day = fdiv(t, 1440), mi = t - day * 1440;
	if (what == 'M' || what == 'y')   /* from the digits, as Org: 2026-02-30 +1m is 03-30 */
		day = days(num(a + 1, a + 5) + (what == 'y' ? n : 0), num(a + 6, a + 8) + (what == 'M' ? n : 0), num(a + 9, a + 11));
	t = day * 1440 + mi + (what == 'd' ? n * 1440 : what == 'h' ? n * 60 : what == 'm' ? n : 0);
	size_t len = stamp(o, t, hm, S[a] == '[', x, k);
	wrep(a, z, o, len);
	return a + len;
}

/* the entry at h parsed alone (org_parse leaves S and N on its text) */
static OrgNode *entry(size_t h, size_t *k)
{
	size_t n = N;
	OrgNode *e = org_parse((const char *)S + h, next_head(h) - h, k);
	S = (const unsigned char *)W, N = n;
	if (!e) oom = 1;
	return e;
}

/* the innermost element of the entry parse e (k nodes, the entry at h)
   holding q; its type, -1 when none */
static int at_type(const OrgNode *e, size_t k, size_t h, size_t q)
{
	int t = -1;
	for (size_t i = 0; i < k && e[i].begin + h <= q; i++) if (q < e[i].end + h) t = e[i].type;
	return t;
}
/* org-at-timestamp-p 'agenda at q, in the stamp starting at a: a planning
   or property line, or a timestamp object */
static int agenda_ts(size_t h, size_t a, size_t q)
{
	size_t k;
	OrgTs x;
	if (!org_ts((const char *)S, N, a, &x) || x.end < q) return 0;   /* a whole stamp first, then the line's kind */
	OrgNode *e = entry(h, &k);
	if (!e) return 0;
	int t = at_type(e, k, h, q);
	free(e);
	return t == ORG_PLANNING || t == ORG_NODE_PROPERTY || t == ORG_PARAGRAPH || t == ORG_HEADLINE ||
	       t == ORG_TABLE_ROW || t == ORG_VERSE_BLOCK;
}
/* org-repeat-re from p on, bounded by lim: the stamp's start, the repeater
   [*r, *re) ([.+]?\+N[hdwmy], without a habit's /N, which nothing after
   reads); ORG_NONE */
static size_t repeat_re(size_t p, size_t lim, size_t *r, size_t *re)
{
	for (; p + 12 < lim; p++) {
		if (S[p] != '<' || !date(p + 1) || S[p + 11] != ' ') continue;
		for (size_t q = p + 12; q < lim && S[q] != '>' && S[q] != '\n'; q++) {
			size_t u = q + (S[q] == '.' || S[q] == '+'), v;
			if (S[u] != '+') u = q;   /* [.+]? backtracks to nothing */
			if (S[u] != '+' || (v = digits(u + 1, lim)) == u + 1 || v >= lim || !one(v, "hdwmy")) continue;
			*r = q, *re = v + 1;
			return p;
		}
	}
	return ORG_NONE;
}

static int alnum(size_t p) { return p < N && (S[p] >= 0x80 || digit(p) || alpha(p)); }   /* a word's byte, for \< */
/* \<KW *[[<]([^]>]+)[]>] at q (KW one of CLOSED: DEADLINE: SCHEDULED:, or
   only CLOSED: and [ when closed): its end, 0 when none */
static size_t kwtime(size_t q, size_t lim, int closed)
{
	static const char *const k[] = { "CLOSED:", "DEADLINE:", "SCHEDULED:" };
	for (int i = 0; i < (closed ? 1 : 3); i++) {
		if (!is(q, k[i]) || (q && alnum(q - 1))) continue;
		size_t p = q + strlen(k[i]), r;
		while (p < lim && S[p] == ' ') p++;
		if (p >= lim || (S[p] != '[' && (closed || S[p] != '<'))) continue;
		for (r = p + 1; r < lim && S[r] != ']' && (closed || S[r] != '>'); r++) ;
		if (r > p + 1 && r < lim) return r + 1;
	}
	return 0;
}
/* org-add-planning-info: CLOSED at now added (add) or removed */
static void closed(size_t h, int add, const char *now)
{
	size_t l = nextl(h), p = blanks(l), q, z;
	if (l < N && after_planning(h, 0) != l) {
		for (q = p; q < eol(l) && !(z = kwtime(q, eol(l), 1)); q++) ;
		if (q < eol(l)) {
			while (z < eol(l) && !kwtime(z, eol(l), 0)) z++;
			wrep(q, z, "", 0);
		}
		if (!add && ateol(blanks(p))) { wrep(l - 1, eol(l), "", 0); return; }
		for (z = q = eol(l); q > p && blank(q - 1); q--) ;
		wrep(q, z, "", 0);
	} else if (add) wins(p = eol(h), "\n"), p++;
	if (!add) return;
	wins(p, "CLOSED: ");
	wins(p + 8, now);
	if (!ateol(p + 8 + strlen(now))) wins(p + 8 + strlen(now), " ");
}

/* current-indentation, which indent-line-to and org-indent-line use: tab
   stops every 8 (ind is org-element's, where a tab adds 8) */
static size_t col(size_t p)
{
	size_t c = 0;
	for (; blank(p); p++) c = S[p] == '\t' ? c / 8 * 8 + 8 : c + 1;
	return c;
}
/* indent-line-to c at p (indent-tabs-mode: tabs, then spaces); after it */
static size_t indent(size_t p, size_t c)
{
	char b[64];
	size_t k = 0;
	for (; c >= 8 && k < 48; c -= 8) b[k++] = '\t';
	while (c-- && k < 63) b[k++] = ' ';
	wrep(p, p, b, k);
	return p + k;
}

/* org-indent-line's column for line l of the entry at h: on an element's
   first line, its sibling before's first line, else its parent drawer's
   (a stray :LOGBOOK: line above opens the drawer), else 0; on a drawer's
   :END: (the only line of a drawer outside its contents), the drawer's;
   inside an element (a stray :LOGBOOK: in a drawer is a paragraph), the
   line above's */
static size_t sibling_ind(size_t h, size_t l)
{
	size_t k, i, j, c = 0;
	OrgNode *e = entry(h, &k);
	if (!e) return 0;
	for (i = k; i-- > 0 && e[i].aff + h != l; ) ;
	if (i < k) {
		for (j = i; j-- > 0 && e[j].parent != e[i].parent; ) ;
		if (j < k) c = col(e[j].begin + h);
		else if (e[i].parent != ORG_NONE && e[e[i].parent].type == ORG_DRAWER) c = col(e[e[i].parent].begin + h);
	} else {
		for (i = k; i-- > 0 && !(e[i].aff + h < l && l < e[i].end + h); ) ;
		if (i < k) c = col(e[i].type == ORG_DRAWER || e[i].type == ORG_PROPERTY_DRAWER ? e[i].begin + h : prevl(l));
	}
	free(e);
	return c;
}

/* org-end-of-meta-data, as the note is stored (case-fold-search on) */
static size_t meta_end(size_t h)
{
	size_t m = after_planning(h, 1);
	if (!propre(m, 1)) return m;
	for (m = nextl(m); !drawer_end(m); m = nextl(m)) ;
	return nextl(m);
}
/* org-store-log-note at org-log-beginning: the state line of a change from
   to, at the top of the LOGBOOK drawer, made when there is none */
static void note(size_t h, int to, int from, const char *now)
{
	size_t m = meta_end(h), ee = m < N && headline(m) ? m : next_head(m), at = ORG_NONE, k;
	OrgNode *e = entry(h, &k);
	if (!e) return;
	size_t p = m;   /* where a drawer is made: after the last :LOGBOOK: line that is none */
	for (size_t q = m; q < ee && at == ORG_NONE; q = nextl(q))
		if (lit(blanks(q), ":LOGBOOK:") && ateol(blanks(blanks(q) + 9))) {
			p = eol(q);
			for (size_t i = 0; i < k; i++) if (e[i].type == ORG_DRAWER && e[i].aff + h == q) at = nextl(q);
		}
	free(e);
	size_t c = at != ORG_NONE ? col(prevl(at)) : 0;
	if (at == ORG_NONE) {
		int blank_line = p == bol(p) && ateol(blanks(p)), text_line = p == bol(p) && !ateol(p);
		if (p != bol(p)) wins(p++, "\n");
		wins(p, ":LOGBOOK:\n:END:");
		if (blank_line || text_line) wins(p + 15, "\n");
		c = sibling_ind(h, p);
		at = indent(p, c) + 10;
		indent(at, sibling_ind(h, at));   /* org-indent-region, a line at a time */
	}
	if (at != bol(at)) wins(at++, "\n");
	else if (!ateol(blanks(at))) wins(at, "\n");
	at = indent(at, c);
	char t[96], a[16], b[16] = "";
	snprintf(a, sizeof a, "\"%s\"", org_todo[to]);
	if (from >= 0) snprintf(b, sizeof b, "\"%s\"", org_todo[from]);
	snprintf(t, sizeof t, "- State %-12s from %-12s %s", a, b, now);
	wins(at, t);
}
/* org-entry-put h "LAST_REPEAT" v */
static void last_repeat(size_t h, const char *v)
{
	size_t p = after_planning(h, 0), q, z;
	if (!propre(p, 0)) {   /* org-insert-property-drawer, indented as the planning line */
		size_t c = p != nextl(h) ? col(nextl(h)) : 0;
		q = p == bol(p) && p > 0 ? p - 1 : p;
		int bob = q == 0;
		if (!bob) wins(q++, "\n");
		wins(q = indent(q, c), ":PROPERTIES:\n");
		wins(q = indent(q + 13, c), ":END:");
		if (q + 5 == N || bob) wins(q + 5, "\n");
		p = after_planning(h, 0);
	}
	for (z = nextl(p); !drawer_end(z); z = nextl(z)) ;
	for (q = nextl(p); q < z; q = nextl(q)) {
		size_t c = blanks(q);
		if (lit(c, ":LAST_REPEAT:") && (ateol(c + 13) || blank(c + 13))) { wrep(q, eol(q), "", 0); break; }
	}
	if (q == z) wins(z, "\n");
	q = indent(q, col(prevl(q)));   /* as the property before it, or the drawer */
	wins(q, ":LAST_REPEAT: ");
	wins(q + 14, v);
}
/* org-remove-timestamp-with-keyword "SCHEDULED:" in the entry at h */
static void unschedule(size_t h)
{
	for (size_t q = next_head(h); q-- > h; ) {
		size_t p = spaces(q + 10), z;
		if (!is(q, "SCHEDULED:") || (q && alnum(q - 1)) || p == q + 10 || p >= N || S[p] != '<') continue;
		for (z = p + 1; z < N && S[z] != '>' && S[z] != '\n'; z++) ;
		if (z == p + 1 || z >= N || S[z] != '>') continue;
		wrep(q, blanks(z + 1), "", 0);
		size_t l = bol(q);
		if (blanks(l) < q && S[q - 1] == ' ') wrep(q - 1, q, "", 0);
		else if (ateol(blanks(l))) wrep(l, eol(l) < N ? eol(l) + 1 : N, "", 0);
	}
}

static int err;   /* Org stops with an error part way, as Emacs leaves the text */

/* org-toggle-comment on the headline at h: COMMENT after its keyword and
   priority (org-complex-heading-regexp's groups), added or removed */
static void toggle_comment(size_t h, int on)
{
	size_t p = stars(h), q, r;
	int k;
	if (kwmatch(p, &k), k >= 0) p = spaces(p) + strlen(org_todo[k]);
	q = spaces(p);
	if (q > p && is(q, "[#") && q + 3 < N && S[q + 3] == ']' && (S[q + 4] == ' ' || ateol(blanks(q + 4)))) p = q + 4;
	p = blanks(p);
	if (!blank(p - 1)) wins(p++, " ");
	if (!on) {
		for (r = p; r < eol(p) && S[r] != ' '; r++) ;
		wrep(p, r < eol(p) ? blanks(r + 1) : r, "", 0);
	} else {
		wins(p, "COMMENT");
		if (!ateol(p + 7)) wins(p + 7, " ");
	}
}

/* org-auto-repeat-maybe: back to TODO, the repeating stamps moved on */
static void repeat(size_t h, int from, const char *nows, long now)
{
	size_t a, r, re, p;
	for (p = h; (a = repeat_re(p, next_head(h), &r, &re)) != ORG_NONE && !agenda_ts(h, a, re); p = a + 1) ;
	size_t k = stars(h), x, z, ts, u = r;
	if (a != ORG_NONE) while (S[u] == '.' || S[u] == '+') u++;
	if (a == ORG_NONE || !num(u, digits(u, N))) return;   /* (string-to-number (substring repeat 1)) */
	int kw;
	wrep(k, kwmatch(k, &kw), from < 0 ? " " : " TODO ", from < 0 ? 1 : 6);   /* the sequence's head, none from none */
	closed(h, 0, nows);
	last_repeat(h, nows);
	size_t pk;
	OrgNode *e = entry(h, &pk);
	if (!e) return;
	for (size_t i = 0; i < pk; i++) {
		OrgTs t[3];
		if (e[i].type != ORG_PLANNING) continue;
		e[i].begin += h, e[i].aff += h, e[i].end += h;
		if (org_planning((const char *)S, N, &e[i], t) & 4) {
			for (z = t[2].end; z > t[2].begin && (S[z - 1] == ' ' || S[z - 1] == '\t'); z--) ;
			if (repeat_re(t[2].begin, z, &x, &x) == ORG_NONE) unschedule(h);
		}
		break;
	}
	free(e);
	for (p = h; !err && (a = repeat_re(p, next_head(h), &r, &re)) != ORG_NONE; ) {
		if (!agenda_ts(h, a, re)) { p = re; continue; }
		char type = S[r] == '.' || (S[r] == '+' && S[r + 1] == '+') ? (char)S[r] : 0, what;
		u = r + (type != 0) + 1;
		long n = num(u, x = digits(u, N));
		what = S[x] == 'w' ? (n *= 7, 'd') : S[x] == 'm' ? 'M' : (char)S[x];
		int hm = 0;
		for (z = a; z + 3 < re && !hm; z++) hm = digit(z) && S[z + 1] == ':' && digit(z + 2) && digit(z + 3);
		if (what == 'h' && !hm) { err = 1; break; }
		if ((ts = ts_around(re)) == ORG_NONE) { err = 1; break; }   /* "Not at a timestamp" */
		long t;
		tsbody(ts + 1, &t);
		if (type == '.') tschange(ts, what == 'h' ? now - t : fdiv(now, 1440) - fdiv(t, 1440), what == 'h' ? 'm' : 'd', 0);
		if (type == '+') {
			do {   /* Org asks "Continue?" at the tenth shift; yes is the coordinator's call while Davy's card is open */
				tschange(ts, n, what, 0);
				tsbody(ts + 1, &t);
			} while (what == 'h' ? !(now < t) : fdiv(now, 1440) >= fdiv(t, 1440));
			tschange(ts, -n, what, 0);
		}
		p = tschange(ts, n, what, 1);
	}
}

const char *org_todo_step(const char *s, size_t n, size_t pos, int dir, long now, size_t *a, size_t *b, size_t *len)
{
	char nows[32], kw[16];
	size_t h, i, j;
	if (n + 64 > WC) {
		char *w = realloc(W, n + 64);
		if (!w) return NULL;
		W = w, WC = n + 64;
	}
	memcpy(W, s, n);
	S = (const unsigned char *)W, N = n, oom = err = 0;
	if (pos > n || (h = heading(pos)) == ORG_NONE) return *a = *b = *len = 0, W;
	OrgHead hd;
	org_head(W, N, h, &hd);
	if (hd.commented) toggle_comment(h, 0);   /* back after the change; a blocked one leaves it out, as Org */
	int from = kw_at(h), to = dir > 0 ? (from + 1 < 5 ? from + 1 : -1) : from == 0 ? -1 : from < 0 ? 4 : from - 1;
	if (!blocked(h, from, to)) {
		snprintf(kw, sizeof kw, to < 0 ? " " : " %s ", to < 0 ? "" : org_todo[to]);
		wrep(stars(h), kwmatch(stars(h), &(int){ 0 }), kw, strlen(kw));
		nows[stamp(nows, now, 1, 1, "", 0)] = 0;
		if (to < 0 || (to < org_done && !(from >= 0 && from < org_done))) closed(h, 0, nows);
		if (to >= org_done && from < org_done) {
			closed(h, 1, nows);
			repeat(h, from, nows, now);
		}
		if (hd.commented && !err) toggle_comment(h, 1);
		if (to >= 2 && !err) note(h, to, from, nows);
	}
	if (oom) return NULL;
	for (i = 0; i < n && i < N && s[i] == W[i]; i++) ;
	for (j = 0; j < n - i && j < N - i && s[n - 1 - j] == W[N - 1 - j]; j++) ;
	*a = i, *b = n - j, *len = N - j - i;
	return W + i;
}
