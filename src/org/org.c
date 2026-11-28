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
#include "date.h"
#include "org.h"
#include "uni.h"
#include "class.h"
#include "grid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

const char *const org_todo[] = { "TODO", "NEXT", "WAIT", "DONE", "CANCELLED", NULL };

static const unsigned char *S;
static size_t N;

enum { M_NIL, M_FIRST, M_SECTION, M_TOP, M_PLANNING, M_PROPS, M_NODEPROP, M_ITEM, M_ROW };

/* ---- lines and characters ---- */

static size_t upto(size_t p, size_t e, int c)   /* the first c in [p, e), else e */
{
	const unsigned char *q = p < e ? memchr(S + p, c, e - p) : NULL;
	return q ? (size_t)(q - S) : e;
}
static size_t eol(size_t p) { return upto(p, N, '\n'); }
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
	if (c < 0x80) { while (lo < k && c > t[lo][1]) lo++; hi = lo; }   /* ASCII: the first few ranges */
	while (lo < hi) { size_t m = lo + (hi - lo) / 2; if (c > t[m][1]) lo = m + 1; else hi = m; }
	return lo < k && c >= t[lo][0];
}
#define IN(t, c) in(t, sizeof t / sizeof *t, c)
/* bytes of the [-_[:word:]] character at p, else 0 */
static size_t namec(size_t p)
{
	if (p >= N) return 0;
	unsigned c = S[p] < 0x80 ? S[p] : uni_cp(S, N, p);
	return c == '-' || c == '_' || IN(WORD, c) ? (c < 0x80 ? 1 : uni_len(S, N, p)) : 0;
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
static int clockl(size_t p, size_t e)   /* org-element-clock-line-re on the line up to e */
{
	p = blanks(p);
	if (!lit(p, "CLOCK:")) return 0;
	p += 6;
	if (!blank(p)) return 0;
	size_t a = blanks(p), z = e;
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
	if (latexl(p, &name) || clockl(p, eol(p))) return 1;
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
	size_t c = *cap ? *cap : 64;
	while (c <= n) c *= 2;   /* past n, which may be more than one step on */
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
/* a planning or clock line: it ends where the blank lines after it end */
static void oneline(OrgNode *e, size_t q, size_t lim, int type)
{
	size_t p = fwd(nextl(q), lim);
	while (p > 0 && blank(p - 1)) p--;
	if (p != bol(p)) p = blanks(p);
	*e = (OrgNode){ q, q, ORG_NONE, ORG_NONE, p, 0, type };
}
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
		oneline(e, q, lim, ORG_PLANNING);
		return;
	}
	if ((star || mode == M_PROPS || mode == M_TOP) && propdrawer(q)) {
		size_t cb = nextl(q), end = search(q, lim, f_drawer_end, 0, 0);   /* org-element-property-drawer-parser */
		if (end == ORG_NONE) end = search(q, N, f_drawer_end, 0, 0);
		*e = (OrgNode){ q, q, end > cb ? cb : ORG_NONE, end > cb ? end : ORG_NONE, after(nextl(end), lim), 0, ORG_PROPERTY_DRAWER };
		return;
	}
	if (!bl) { paragraph(e, q, q, lim); return; }
	if (clockl(q, eol(q))) {                    /* org-element-clock-parser */
		oneline(e, q, lim, ORG_CLOCK);
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
static int is(size_t p, const char *k)   /* a loop, not strlen and memcmp: most calls fail at once */
{
	for (; *k; k++, p++) if (p >= N || S[p] != (unsigned char)*k) return 0;
	return 1;
}
static int one(size_t p, const char *k) { return p < N && S[p] && strchr(k, S[p]); }

/* org-fontify-meta-lines-and-blocks-1's state at the line at: st 0
   outside, 1 in a block whose end line (or line of stars) begins at end
   (quoting: its content is org-block), 2 stopped (a block without an end:
   the keyword's matcher returned nil) */
typedef struct {
	size_t at, end; int st, quoting;
	size_t lk, la, lz, lv, lw;   /* links: where the search goes on; the last link and its shown part */
	size_t af, df;               /* where a failed angle or description search stopped */
	size_t fl, fz, fd, fq; int fm;   /* footnotes: the line taken, where a reference's span goes on to,
	                                    the last definition, the line the look ahead stopped at and what it found */
} Scan;
struct OrgFx {
	const Text *t;
	char *s;               /* the text at the last update, and its parse */
	size_t n, cap, k;
	OrgNode *e;
	int dirty;             /* edited since: [lo, ho) of s is [lo, hc) of t */
	size_t lo, ho, hc;
	Scan c, cn;            /* the scan's state at the last line it coloured, and after it */
	unsigned char *lf;     /* that line's faces (the core asks a long line in pieces) */
	size_t lcap;           /* as long as the longest line, unless out of memory */
	Scan cs;               /* conceal's: the scan's state at the last line it showed */
	size_t *em, emcap;     /* conceal's emphasis matches, begin and end (they grow, then stay) */
	size_t *bl, bn, bcap;  /* blines(), bn ORG_NONE until built */
	Scan *ck;              /* the scan's state every 4096 lines it walked, in order */
	size_t kn, kcap;
	size_t sp, sf, sg;     /* sexp() fails at sp and for an opener in (sf, sg) (0: a reference is past a line's start) */
};
static OrgFx *X;

/* org-element-paragraph-separate at q, in a string ending at z ($ there) */
static int separate(size_t q, size_t z)
{
	size_t b = blanks(q), x;
	if ((x = stars(q)) > q && x < z && S[x] == ' ') return 1;
	if (is(q, "[fn:") && (x = names(q + 4)) > q + 4 && x < z && S[x] == ']') return 1;
	if (is(q, "%%(") || b >= z || ateol(b) || S[b] == '|' || is(b, "\\begin{") || (is(b, "CLOCK:") && clockl(q, upto(q, z, '\n')))) return 1;   /* font-lock folds no case */
	x = b + 1;
	if (S[b] == '#') {
		if (x >= z || ateol(x) || S[x] == ' ') return 1;
		if (S[x] != '+') return 0;
		if (is(x + 1, "BEGIN_") && x + 7 < z && !sp(x + 7)) return 1;
		/* \S-+\(?:\[.*\]\)?: with Emacs's backtracking: a : after one
		   character of the run, or ]: past a [ after one */
		size_t o = 0, r;
		for (r = x + 1; r < z && !sp(r); r++) {
			if (S[r] == ':' && r > x + 1) return 1;
			if (S[r] == '[' && r > x + 1) o = 1;
		}
		for (; o && r < z; r++) if (S[r] == ':' && S[r - 1] == ']') return 1;
		return 0;
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
/* org-emphasis-regexp-components' post */
static int post(size_t p) { return ateol(p) || sp(p) || one(p, "-.,:!?;'\")}\\["); }
/* and its pre, before the marker at p */
static int pre(size_t p) { return !p || sp(p - 1) || one(p - 1, "-('\"{"); }
/* org-emph-re's and org-verbatim-re's closer for the marker at p, from c
   on: the first that fits, else e */
static size_t fits(size_t p, size_t c, size_t e)
{
	while (c < e && !(S[c] == S[p] && !sp(c - 1) && post(c + 1))) c++;
	return c;
}

/* ---- links: org-activate-links, for org-highlight-links' bracket, angle
   and plain links (Org's default, all of them), as whole-buffer font-lock
   finds them.  Known differences: a bracket link past 32 choices of a
   backslash run is no link (Emacs backtracks on); the newline a
   description hides stays (a cell grid keeps its lines) ---- */

/* org-link-types: Org 9.8.7's with the modules it loads by default */
static const char *const ltypes[] = { "bbdb", "bibtex", "docview", "doi", "elisp", "eww", "file", "file+emacs",
	"file+sys", "ftp", "gnus", "help", "http", "https", "id", "info", "irc", "mailto", "mhe", "news", "rmail",
	"shell", "shortdoc", "w3m" };
static int FOLD;   /* the element parser folds case; font-lock does not */
static int tc(size_t p) { return p < N && ((S[p] >= 'a' && S[p] <= 'z') || (FOLD && alpha(p)) || S[p] == '+' || S[p] == '3'); }   /* a type's letters */
static size_t ltype(size_t p)   /* TYPE: at p: its end, or 0 */
{
	size_t x = p;
	while (tc(x)) x++;
	if (x >= N || S[x] != ':') return 0;
	for (size_t i = 0; i < sizeof ltypes / sizeof *ltypes; i++)
		if (strlen(ltypes[i]) == x - p && (FOLD ? lit(p, ltypes[i]) : is(p, ltypes[i]))) return x + 1;
	return 0;
}
static int nsb(size_t p) { return p < N && !one(p, "[] \t\n()<>"); }   /* [^][ \t\n()<>] */
/* [<([]\([^][ \t\n()<>]\|[<([][^][ \t\n()<>]*[])>]\)*[])>] at p: its end, or 0 */
static size_t paren(size_t p)
{
	if (!one(p, "<([")) return 0;
	for (p++; ; p++) {
		if (one(p, "<([")) {   /* a group inside: no group in it */
			for (p++; nsb(p); p++) ;
			if (!one(p, "])>")) return 0;
		} else if (!nsb(p)) break;
	}
	return one(p, "])>") ? p + 1 : 0;
}
/* org-link-plain-re's last character: [^[:punct:][:space:]\n], -, /
   ([:punct:] is any non-word character above ASCII) */
static int fin(size_t p)
{
	unsigned c = S[p];
	if (c >= 0x80) return word(p);
	return c == '-' || c == '/' || c == 127 || digit(p) || alpha(p) || (c < 32 && c != '\t' && c != '\n' && c != '\f' && c != '\r');
}
/* org-link-plain-re at p, where a word starts: the longest path whose last
   item may end it, or 0 */
static size_t plain(size_t p)
{
	size_t q = ltype(p), z = 0, x;
	for (int n = 0; q && (x = nsb(q) ? q + uni_len(S, N, q) : paren(q)); n++, q = x)
		if (n && (!nsb(q) || fin(q))) z = x;
	return z;
}
/* org-link-angle-re at p (a '<'): its end, or 0.  A search that fails
   stops at *f (one from before there would stop there too, so it fails at
   once: no rescan of a paragraph for each opener) */
static size_t angle(size_t p, size_t *f)
{
	size_t q = ltype(p + 1);
	if (!q || p < *f) return 0;
	for (; q < N; q++) {
		if (S[q] == '>') return q + 1;
		if (S[q] == '\n' && ((q = blanks(q + 1)) >= N || one(q, ">\n"))) break;
	}
	*f = q;
	return 0;
}
/* no link holds the start of the line q, whatever the regexp makes of it:
   paragraph-separate takes the line up to any end a link can have there
   (after a ] or a >, or past the line) */
static int inside(size_t q)
{
	size_t e = eol(q);
	for (size_t y = q; y < e; y++) if ((S[y] == ']' || S[y] == '>') && !separate(q, y + 1)) return 0;
	return separate(q, e);
}
/* org-link-bracket-re after its [[ (at u), from i, in Emacs's backtracking
   order: only a run of backslashes gives a choice, to go on at the run's
   end or one past it (\ and pairs before a bracket, or backslashes before
   another character), tried in the order Emacs reaches them.  The match's
   end and its description (else the URI) in [*va, *vz), or 0; *f as
   angle's, for the description */
static size_t uri(size_t i, size_t u, size_t *va, size_t *vz, int d, size_t *f)
{
	for (; i < N && S[i] != '[' && S[i] != ']'; i++) {
		if (S[i] != '\\') continue;
		size_t k = i, a, b = 0, z;
		while (k < N && S[k] == '\\') k++;
		if (k >= N) return 0;
		if (one(k, "[]") && (k - i) % 2 == 0) a = k, b = k - i >= 4 ? k + 1 : 0;
		else a = k + 1, b = k - i >= 2 ? k : 0;
		if (b && d >= 32) return 0;   /* known difference: past 32 choices, no link */
		if (b && (z = uri(a, u, va, vz, d + 1, f))) return z;
		i = (b ? b : a) - 1;
	}
	if (i == u || i >= N || S[i] != ']') return 0;
	*va = u; *vz = i;
	if (is(i + 1, "[") && i + 3 < *f) return 0;   /* the description: \[\(.+?\)\], as angle's *f */
	if (is(i + 1, "[")) {
		size_t q;
		for (q = i + 3; q + 1 < N && !(X && S[q] == '\n' && inside(q + 1)); q++)   /* X: font-lock's search; the parser's stops at its container's end */
			if (S[q] == ']' && S[q + 1] == ']') { *va = i + 2; *vz = q; return q + 2; }
		*f = q + 1;
		return 0;
	}
	return is(i + 1, "]") ? i + 2 : 0;
}
/* org-activate-links' next link from c->lk starting before lim: [la, lz),
   shown [lv, lw) (lv = la unless a bracket link hides its brackets); 0
   when none */
static int link_next(Scan *c, size_t lim)
{
	/* a match starts at a [ or <, or at a plain link's type, found from
	   the : after it (a type: at most 10 bytes of [a-z3+], no : [ <) */
	for (size_t p = c->lk, a, z, v, w, q; ; p++) {   /* lim is a line's start: a type holds no newline */
		for (const unsigned char *s = S; p < lim && s[p] != '[' && s[p] != '<' && s[p] != ':'; p++) ;
		if (p >= lim) break;
		for (a = p; S[p] == ':' && a > c->lk && p - a < 10 && tc(a - 1); a--) ;
		for (; a < p && !((!a || !word(uni_prev(S, N, a))) && ltype(a) == p + 1); a++) ;
		if (a >= lim) break;
		v = a; w = 0;
		z = a < p ? plain(a) : S[p] == '<' ? angle(p, &c->af) : S[p] == '[' && is(p + 1, "[") ? uri(p + 2, p + 2, &v, &w, 0, &c->df) : 0;
		if (!z) continue;
		/* "Do not span over paragraph boundaries": ^ at the start and after each newline */
		for (q = a; q < z && !separate(q, upto(q, z, '\n')); q = nextl(q)) ;
		if (q < z) continue;
		/* no test for a plain link after a tag: org-activate-tags runs
		   later, so whole-buffer font-lock never sees one */
		c->la = a; c->lz = c->lk = z; c->lv = v; c->lw = w ? w : z;
		return 1;
	}
	if (c->lk < lim) c->lk = lim;
	return 0;
}

/* ---- footnotes: org-activate-footnote-links, as whole-buffer font-lock
   finds them.  Known differences: after a reference inside an earlier
   one's span the search goes on (Org's matcher returns nil there, and
   font-lock stops looking for footnotes; a live Emacs only in its chunk);
   a reference's span stops at a headline (a scan may start there).
   Known cost: sexp() remembers one failed scan, so a paragraph whose lines
   each open an inline footnote that never closes, with a pair or a quote
   after it, scans to the paragraph's end from every line shown (PR 118's
   review, -O2, 20,000 lines: x [fn:: ( [fn:1] 3.6 s, x [fn::a (b) 2.8 s,
   x [fn:: " 1.3 s, measured); scan-sexps in Emacs does the same scan for
   each (hypothesis), and bounding it needs the depth after every opener
   kept, which a text in normal use never asks for ---- */

/* scan-sexps over the [ at p in Org's syntax table: parentheses of any
   pair count, " quotes; the end, or 0 for its scan-error */
static size_t sexp(size_t p)
{
	static unsigned char sx[128];   /* ASCII in Org's syntax table: 1 ", 2 an opener, 3 a closer */
	if (!sx['"']) for (unsigned c = 0; c < 128; c++) sx[c] = c == '"' ? 1 : IN(OPENS, c) ? 2 : IN(CLOSES, c) ? 3 : 0;
	if (p == X->sp || (p > X->sf && p < X->sg)) return 0;
	size_t q = p, w = p, d = 0, k;   /* w: the last closer or quote seen */
	for (; q < N; q += k) {
		unsigned c = S[q], t;
		k = 1;
		if (c < 0x80) t = sx[c];
		else c = uni_cp(S, N, q), k = uni_len(S, N, q), t = IN(OPENS, c) ? 2 : IN(CLOSES, c) ? 3 : 0;
		if (t == 1) {
			const unsigned char *r = memchr(S + q + 1, '"', N - q - 1);
			if (!r) break;
			w = (size_t)(r - S); k = w + 1 - q;
		} else if (t == 2) d++;
		else if (t == 3 && (w = q, !--d)) return q + k;
	}
	/* from an opener after w, only openers come before q (the end, or a
	   quote with no pair): it fails too.  Typing [fn:: asks it each frame */
	X->sp = p; X->sf = w; X->sg = q;
	return 0;
}
/* org-footnote-re at p: \[fn:\(?:\(LABEL\)?\(:\)\|\(LABEL\)\]\): its end, or 0 */
static size_t fnre(size_t p)
{
	if (!is(p, "[fn:")) return 0;
	size_t x = names(p + 4);
	return x < N && (S[x] == ':' || (S[x] == ']' && x > p + 4)) ? x + 1 : 0;
}
static int defl(size_t l) { size_t x = fnre(l); return x && S[x - 1] == ']'; }   /* org-footnote-definition-re */
/* org-in-regexp's search, of the lines [h's start, b), for a match of re
   (its start, its end in *z, from p) that holds pos: its end, or ORG_NONE.
   h keeps the matches' chain, so a line's references, asked in order,
   search it once: x the match found from z, x == z before the first */
typedef struct { size_t x, z; } Hit;
static size_t holds(Hit *h, size_t b, size_t pos, size_t (*re)(size_t p, size_t *z))
{
	size_t n = N;
	N = b;   /* a bounded search sees nothing past b */
	while (h->x != ORG_NONE && h->x <= pos && (h->z < pos || h->x == h->z)) h->x = re(h->z, &h->z);
	N = n;
	return h->x != ORG_NONE && h->x <= pos && h->z >= pos ? h->z : ORG_NONE;
}
static size_t link_re(size_t q, size_t *z)   /* org-link-bracket-re's next match */
{
	size_t v, w, f = 0;
	for (; q < N; q++) if (S[q] == '[' && is(q + 1, "[") && (*z = uri(q + 2, q + 2, &v, &w, 0, &f))) return q;
	return ORG_NONE;
}
static size_t macro_re(size_t q, size_t *z)   /* \\[a-zA-Z]+\*?\(\(\[[^][\n{}]*\]\)\|\({[^{}\n]*}\)\)* */
{
	for (; q < N; q++) {
		if (S[q] != '\\' || !alpha(q + 1)) continue;
		size_t x = letters(q + 1), y;
		x += is(x, "*");
		for (;; x = y + 1) {
			char c = x < N && S[x] == '[' ? ']' : x < N && S[x] == '{' ? '}' : 0;
			for (y = x + 1; c && y < N && !one(y, c == '}' ? "{}\n" : "[]{}\n"); y++) ;
			if (!c || y >= N || S[y] != c) break;
		}
		*z = x;
		return q;
	}
	return ORG_NONE;
}
/* org-verbatim-re's next match from q, its marker and closer in VM and VC
   (lazily over up to one newline: a closer before the second) */
static size_t VM, VC;
static size_t verb_re(size_t q, size_t *z)
{
	for (size_t m = q + (q > 0), c; m < N; m++)   /* a match starts one before its marker */
		if (one(m, "=~") && pre(m) && !sp(m + 1) && !ateol(m + 1) && (c = fits(m, m + 2, eol(nextl(m)))) < eol(nextl(m))) {
			VM = m; VC = c; *z = c + 1 + (c + 1 < N);
			return m ? m - 1 : 0;
		}
	return ORG_NONE;
}
/* the headlines and #+begin_ and #+end_ lines, in order (they grow, then
   stay; built when first asked after an update): *n of them, or NULL out of
   memory */
static const size_t *blines(size_t *n);
/* org-in-block-p of org-footnote-forbidden-blocks at the line l
   (org-between-regexps-p between the headlines around it).  Only the
   comment entry shows in colours: in example, src and export blocks
   org-block is the top face anyway (no text found where they differ, 9
   directed texts, measured); they stay as Org's list, for the footnote
   jumps that will ask the same question */
static int forbidden(size_t l)
{
	static const char *const k[] = { "#+begin_comment", "#+begin_example", "#+begin_export", "#+begin_src" };
	size_t n, j, lo = 0, hi;
	const size_t *L = blines(&n);
	if (!L) return 0;
	for (hi = n; lo < hi; ) { size_t m = (lo + hi) / 2; if (L[m] <= l) lo = m + 1; else hi = m; }
	for (int i = 0; i < 4; i++) {
		for (j = lo; j-- > 0 && !(headline(L[j]) && L[j] < l) && !lit(blanks(L[j]), k[i]); ) ;
		if (j == ORG_NONE || headline(L[j])) continue;   /* no begin since the headline above */
		while (++j < n && !lit(blanks(L[j]), k[i]) && !(headline(L[j]) && L[j] > l) &&
		       !(lit(blanks(L[j]), "#+end_") && lit(blanks(L[j]) + 6, k[i] + 8))) ;
		if (j < n && L[j] >= l && !headline(L[j]) && !lit(blanks(L[j]), k[i])) return 1;
	}
	return 0;
}
/* the text the scan reads: its parse, and its OrgFx */
static const OrgNode *PE;
static size_t PK;
static size_t ent(size_t p, size_t *x);
static const char *entity(size_t a, size_t z);
static int within(size_t p, size_t z, const char *k) { for (; p < z; p++) if (is(p, k)) return p + strlen(k) <= z; return 0; }
/* a LaTeX fragment starts at the line l, in a paragraph ending at z: what
   org-element-context finds there (org-element-latex-fragment-parser,
   after the entity parser, in the paragraph) */
static int frag(size_t l, size_t z)
{
	size_t x, m = l + 1;
	const unsigned char *q;
	if (S[l] == '\\' && one(m, "([")) return within(l + 2, z, S[m] == '(' ? "\\)" : "\\]");
	if (S[l] == '\\') return alpha(m) && !(ent(l, &x) && entity(m, x));   /* \\[a-zA-Z]+ matches */
	if (S[l] != '$' || m >= z) return 0;
	if (S[m] == '$') return within(l + 2, z, "$$");
	if (one(m, " \t\n,.;") || !(q = memchr(S + m, '$', z - m))) return 0;
	m = (size_t)(q - S) + 1;   /* after the closing $ */
	return !one(m - 2, " \t\n,.") && (m >= z || ateol(m) || IN(DPOST, uni_cp(S, N, m)));
}
/* org-footnote-in-valid-context-p at the line l; known difference: an
   object other than a LaTeX fragment starting there that holds l (a link,
   a fragment from a line above) is not looked for */
static int valid(size_t l)
{
	size_t b = blanks(l), up = prevl(prevl(l)), m, i = PK ? first(PE, PK, l + 1) : 0;
	while (i-- > 0 && PE[i].end <= l) i = PE[i].parent + 1;   /* the element at l (org-element-context) */
	if (i != ORG_NONE && (PE[i].type == ORG_LATEX_ENVIRONMENT || (PE[i].type == ORG_PARAGRAPH && frag(l, PE[i].cend)))) return 0;
	if (comment(l) || (b < N && S[b] == ':' && blank(b + 1)) || forbidden(l)) return 0;
	m = holds(&(Hit){ up, up }, eol(nextl(nextl(l))), l, verb_re);
	return m == ORG_NONE || l < VM || l > VC;
}
/* org-footnote-next-reference-or-definition's span for the match at p,
   ending at x, on the line l: [*a, *z), or 0 when it throws nothing; v:
   NULL when no definition's span is asked for, else valid(l) once known
   (-1 before), as it depends on the line only */
static int fn_span(Scan *c, size_t p, size_t x, size_t l, Hit *h, size_t *a, size_t *z, int *v)
{
	size_t e, d = c->fd, y, k;   /* a link holding p, ending there, does not count */
	if (p > l && (y = sexp(p)) && ((k = holds(h, e = eol(l), p, link_re)) == ORG_NONE || k == p) &&
	    holds(h + 1, e, x - 1, macro_re) == ORG_NONE)
		return *a = p, *z = y, 1;
	if (!v || d == ORG_NONE || !(*v < 0 ? (*v = valid(l)) : *v)) return 0;
	*a = d; *z = S[x - 1] == ']' ? x : x + 1;
	return 1;
}
/* the separators org-footnote-next-reference-or-definition looks back to:
   ^\*+ and ^\([ \t]*\n\)\{2,\} */
static int fn_sep(size_t l) { return l < N && (S[l] == '*' ? headline(l) : emptyl(l) && eol(l) < N && emptyl(nextl(l))); }
/* put [a, z) less what r, the span put last, holds: references below a
   definition each throw its span again, and putting it whole each time is
   quadratic on a long line */
static void cover(size_t r[2], size_t a, size_t z, void (*put)(size_t, size_t))
{
	if (a > r[1]) r[0] = r[1] = a;   /* every z is past the reference, so past r[0] */
	if (a < r[0]) put(a, r[0]), r[0] = a;
	if (z > r[1]) put(r[1], z), r[1] = z;
}
/* the footnotes of the line l, ending at e: put each span (a reference's, from the
   line above, a later one's from the definition above); its state on */
static void fn_line(Scan *c, size_t l, size_t e, void (*put)(size_t, size_t))
{
	size_t a, z, x, r[2] = { l, l };
	if (c->fl == l + 1) return;
	c->fl = l + 1;
	if (fn_sep(l)) c->fd = ORG_NONE, c->fz = S[l] == '*' ? 0 : c->fz;   /* a headline: as a scan starting there */
	if (defl(l)) c->fd = l;
	if (put && c->fz > l) cover(r, l, c->fz, put);
	if (put && c->fd != ORG_NONE) {   /* a reference below that throws its definition's span */
		if (c->fq <= l) {
			for (c->fq = nextl(l), c->fm = 0; c->fq < N && !fn_sep(c->fq) && !defl(c->fq); c->fq = nextl(c->fq)) {
				Hit h[2] = { { c->fq, c->fq }, { c->fq, c->fq } };
				int v = -1;
				for (size_t p = c->fq, q = eol(p); (p = upto(p, q, '[')) < q && !c->fm; p++)
					if ((x = fnre(p)) && fn_span(c, p, x, c->fq, h, &a, &z, &v) && a < c->fq) c->fm = 1;
				if (c->fm) break;
			}
		}
		if (c->fm) cover(r, l, nextl(l), put);
	}
	Hit h[2] = { { l, l }, { l, l } };   /* link_re's, macro_re's */
	int v = -1;
	for (size_t p = l; (p = upto(p, e, '[')) < e; p++)
		if ((put || p > l) && (x = fnre(p)) && fn_span(c, p, x, l, h, &a, &z, put ? &v : NULL)) {
			if (z > c->fz) c->fz = z;
			if (put) cover(r, a, z, put);   /* the search goes on at the match's end less one: no [ before it */
		}
}


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
	size_t l = c->at, e = eol(l), nl = e < N ? e + 1 : e, q = blanks(l);
	fn_line(c, l, e, NULL);   /* the footnotes the line's caller did not take */
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
/* org-activate-dates on the line [l, e): put(a, z) for each date it
   gives org-date and mouse-face */
static void dates(size_t l, size_t e, void (*put)(size_t, size_t))
{
	size_t lc = e, x, z;   /* after the line's last ] or >: a date needs one at p + 11 or later */
	while (lc > l && S[lc - 1] != ']' && S[lc - 1] != '>') lc--;
	for (size_t p = l; p + 12 <= lc; p++) {   /* org-tsr-regexp-both */
		if ((S[p] != '<' && S[p] != '[') || (x = ts_both(p)) == ORG_NONE) continue;
		for (z = x + 1; z < e && z < x + 4 && S[z] == '-'; z++) ;
		if (z > x + 1 && (z = ts_both(z)) != ORG_NONE) x = z;
		/* Org stops dating the whole buffer at one preceded by "[", a
		   live Emacs only the chunk it fontifies: this goes on */
		if (!p || S[p - 1] != '[') put(p, x + 1);
		p = x;
	}
}
static void date_face(size_t a, size_t z) { set(a, z, FACE_ORG_DATE); }
static void fn_face(size_t a, size_t z) { set(a, z, FACE_MARKUP_LINK_LABEL); }
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
	for (int k = c->lz > l; k || link_next(c, nl); k = 0) set(c->la, c->lz, FACE_MARKUP_LINK_URL);   /* org-activate-links */
	if (hl && (x = tags(l, e, &z)) != ORG_NONE) set(x, z, FACE_ORG_TAG);   /* org-activate-tags */
	dates(l, e, date_face);
	fn_line(c, l, e, fn_face);   /* org-activate-footnote-links */
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


OrgFx *orgfx_new(const Text *t)
{
	OrgFx *f = calloc(1, sizeof *f);
	if (f) f->t = t, f->dirty = 1, f->hc = text_len(t), f->c.at = f->cs.at = f->bn = ORG_NONE;
	return f;
}

void orgfx_free(OrgFx *f)
{
	if (f) free(f->s), free(f->e), free(f->lf), free(f->em), free(f->bl), free(f->ck);
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
	f->c.at = f->cs.at = f->bn = ORG_NONE, f->kn = f->sp = f->sg = 0;
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
/* the links of c up to L: from a line no link holds, when one is near */
static void links_to(Scan *c, size_t L)
{
	size_t P = L;
	for (int n = 0; n < 64 && P > c->lk && !inside(P); n++) P = prevl(P);
	if (P > c->lk && inside(P)) c->lk = P;
	while (link_next(c, L)) ;
}
/* the scan's state at the line L (meta's set must write nowhere), from the
   nearest state on the way: the last coloured or shown line's, or a
   checkpoint; a walk leaves one every 4096 lines, so a text is walked
   once after an update */
static Scan scan_at(OrgFx *f, size_t L)
{
	size_t R = head_at(f, L), i = f->kn;   /* a headline's line is outside any block */
	if (R == ORG_NONE) R = 0;
	while (i > 0 && f->ck[i - 1].at > L) i--;
	const Scan *k[] = { &f->c, &f->cs, i ? &f->ck[i - 1] : &f->c };
	Scan c = { .at = R, .lk = R, .fd = ORG_NONE };
	for (int j = 0; j < 3; j++) if (k[j]->at <= L && k[j]->at > c.at) c = *k[j];
	for (size_t n = 1; c.at < L; n++) {
		meta(&c);
		if (n % 4096 == 0 && (!f->kn || c.at > f->ck[f->kn - 1].at) && grow((void **)&f->ck, &f->kcap, f->kn, sizeof *f->ck)) {
			links_to(&c, c.at);
			f->ck[f->kn++] = c;
		}
	}
	links_to(&c, L);
	return c;
}
static void paint(OrgFx *f, size_t a, size_t z, unsigned char *out)
{
	S = (const unsigned char *)f->s; N = f->n; F = out; FA = a; FZ = z; PE = f->e; PK = f->k; X = f;
	if (a >= z || a >= N) return;
	size_t L = bol(a);
	Scan c = scan_at(f, L);
	while (c.at < z && c.at < N) {
		size_t l = c.at, nl = nextl(l), u = a > l ? a : l, v = z < nl ? z : nl;
		if (f->c.at == l) c = f->cn;   /* coloured: the scan steps over it */
		else if (nl - l > f->lcap) { F = out; FA = a; FZ = z; f->c.at = ORG_NONE; fontify(&c); continue; }   /* out of memory */
		else { F = f->lf; FA = l; FZ = nl; memset(F, 0, nl - l); f->c = c; fontify(&c); f->cn = c; }
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
   newline (Org's [^{}]*? does not); emphasis never ends on a CLOCK line.
   A link (org-link-descriptive) hides its brackets and target ---- */

#include "entities.h"
#include "scripts.h"

static Conceal *C;
static size_t CN, CCAP, CL, CE;   /* the spans so far, their room, the line [CL, CE) */
static size_t Prot;   /* Org takes display and invisible off the line before it: only compositions show */
static void hide(size_t a, size_t z, const char *s)
{
	if (a >= CL && z <= CE && CN < CCAP && (*s || a >= Prot)) C[CN++] = (Conceal){ a, z, s };
}
/* remove-text-properties invisible on [a, z): what is hidden there shows */
static void unhide(size_t a, size_t z)
{
	for (size_t i = CN; i-- > 0; ) {
		Conceal *x = &C[i];
		if (*x->s || x->z <= a || x->a >= z) continue;
		if (x->a < a && x->z > z && CN < CCAP) C[CN++] = (Conceal){ z, x->z, "" };
		if (x->a < a) x->z = a;
		else if (x->z > z) x->a = z;
		else *x = C[--CN];
	}
}
/* the ranges where a script shows as typed (org-emphasis or mouse-face at
   its start), XK of them in X's em: they grow, then stay; out of memory,
   later ones are left out */
static size_t XK;
static void rec(size_t a, size_t z);
/* [ \t]*\(|[-+]+|?\|\+[-+]+\+\)[ \t]*$ at the line l: a table rule */
static int rule(size_t l)
{
	size_t b = blanks(l), x;
	for (x = b + 1; one(x, "-+"); x++) ;
	if (S[b] == '|') return x > b + 1 && ateol(blanks(x + is(x, "|")));
	return S[b] == '+' && x >= b + 3 && S[x - 1] == '+' && ateol(blanks(x));
}
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
	if (c < p + 2) c = fits(p, p + 2, E2);
	return (Cx[k] = c) < lim ? c : 0;
}
/* from the line before CL: emphasis there may end on CL */
static void emphasis(size_t from)
{
	size_t bar = 0, rm = ORG_NONE;   /* the next | after p; the line rule() saw */
	int rv = 0;
	E2 = CE < N ? eol(CE + 1) : N;
	memset(Cx, 0, sizeof Cx);
	for (size_t p = from; p < CE; p++) {
		if (!one(p, "*/_+=~") || !pre(p)) continue;
		/* the match starts at the newline before a line's first marker,
		   so the line above is the one org-match-line looks at */
		size_t l = p < CL ? from : CL, m = p == l && p ? bol(p - 1) : l, b = blanks(m), nl = p < CL ? CL - 1 : CE, c;
		if (S[p] == '+' && m != rm) rm = m, rv = rule(m);
		if ((S[p] == '+' && rv) || (S[p] == '*' && p == l && headline(l))) continue;
		if (!(c = emph_close(p, p < CL ? CE : E2)) || separate(p, c + 1)) continue;
		if (nl < c && separate(nl + 1, c + 1)) continue;   /* string-match-p: ^ after the newline too */
		if (bar <= p) for (bar = p + 1; bar < E2 && S[bar] != '|'; bar++) ;
		if (S[b] == '|' && bar < c) continue;
		if (S[p] == '=' || S[p] == '~') unhide(p, c + 1);   /* verbatim: no earlier markup inside shows hidden */
		rec(p, c + 1);
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
/* org-element-entity-parser's regexp at the \\ at p: the match's end, the
   name's at *x; 0 when none */
static size_t ent(size_t p, size_t *x)
{
	*x = 0;
	if (is(p + 1, "there4")) *x = p + 7;
	else if (is(p + 1, "sup") && one(p + 4, "123")) *x = p + 5;
	else if (is(p + 1, "frac") && one(p + 5, "13") && one(p + 6, "24")) *x = p + 7;
	if (*x && ent_alpha(*x) && S[*x] != '\n') *x = 0;   /* the next alternative */
	if (!*x && (*x = letters(p + 1)) == p + 1) return 0;
	if (ateol(*x)) return *x;
	if (is(*x, "{}")) return *x + 2;
	return ent_alpha(*x) ? 0 : *x + uni_len(S, N, *x);
}
static void entities_at(size_t l)
{
	for (size_t p = l; p < CE; p++) {
		size_t x, e;
		if (S[p] != '\\' || !(e = ent(p, &x))) continue;
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
/* org-create-multibrace-regexp o c 3 at q: the closing c, 0 when none:
   groups inside are all flat, or all hold flat groups only.  Its [^{}]*?
   crosses lines; here at most nl newlines */
static size_t braces(size_t q, int o, int c, size_t nl)
{
	int d = 0, kind = 0, g = 0;   /* kind: 1 flat groups, 2 nested */
	for (size_t p = q; p < N; p++) {
		if ((S[p] == o && ++d > 3) || (S[p] == '\n' && !nl--)) return 0;
		if (S[p] == o && d == 3) g = 1;
		if (S[p] != c) continue;
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
		size_t z = braces(p + 2, '{', '}', 1), m = p + 2;
		if (!z) continue;
		/* ^[ \t]*#[ +] as comment-p; a script over lines shows as typed */
		int ok = p >= l && p >= Prot && z < CE && !(S[blanks(l)] == '#' && one(blanks(l) + 1, " +")), up = S[p + 1] == '^', n;
		for (; i < ek && em[2 * i] <= m; i++) if (em[2 * i + 1] > ez) ez = em[2 * i + 1];
		if (ok && m >= ez && m >= kz && (n = raised(m + 1, z, up, 0)) >= 0 && CN + (size_t)n + 2 <= CCAP)
			hide(p + 1, m + 1, ""), raised(m + 1, z, up, 1), hide(z, z + 1, "");
		p = z + 1 < CE && one(z + 1, "_^") ? z - 1 : z;
	}
}
static void rec(size_t a, size_t z)
{
	if (grow((void **)&X->em, &X->emcap, 2 * XK + 1, sizeof *X->em)) X->em[2 * XK] = a, X->em[2 * XK + 1] = z, ++XK;
}
static const size_t *blines(size_t *n)
{
	if (X->bn == ORG_NONE) {
		X->bn = 0;
		for (size_t l = 0; l < N; l = nextl(l)) {
			if (!headline(l) && !lit(blanks(l), "#+begin_") && !lit(blanks(l), "#+end_")) continue;
			if (!grow((void **)&X->bl, &X->bcap, X->bn, sizeof *X->bl)) { X->bn = ORG_NONE; return NULL; }
			X->bl[X->bn++] = l;
		}
	}
	*n = X->bn;
	return X->bl;
}
static int by_first(const void *a, const void *b)
{
	size_t x = *(const size_t *)a, y = *(const size_t *)b;
	return x < y ? -1 : x > y;
}
/* a hidden part of a link, on the line: the newline stays (a cell grid
   keeps its lines) */
static void hidel(size_t a, size_t z)
{
	if (a < CL) a = CL;
	if (z > CE) z = CE;
	if (a < z) hide(a, z, "");
}
size_t orgfx_conceal(OrgFx *f, size_t line, Conceal *out, size_t cap)
{
	orgfx_update(f, NULL);
	S = (const unsigned char *)f->s; N = f->n; PE = f->e; PK = f->k; X = f;
	CL = text_line_start(f->t, line); CE = eol(CL);
	C = out; CN = 0; CCAP = cap;
	if (CL >= N && N) return 0;
	size_t pl = CL ? prevl(CL) : 0, b = blanks(CL);
	Scan c = (FZ = 0, scan_at(f, pl));   /* a headline may end a block: from the line above */
	if (CL) while (link_next(&c, CL)) ;
	if (CL) meta(&c);
	f->cs = c;
	Scan d = c;   /* org-activate-code: ^[ \t]*:\(?: .*\|$\) */
	Prot = b < N && S[b] == ':' && (ateol(b + 1) || S[b + 1] == ' ') ? N : meta(&d);
	XK = 0;
	for (int k = c.lz > CL; k || link_next(&c, nextl(CL)); k = 0) {   /* org-activate-links: org-link-descriptive */
		if (c.lv > c.la) hidel(c.la, c.lv), hidel(c.lw, c.lz);
		rec(c.la, c.lz);
	}
	dates(CL, CE, rec);
	fn_line(&c, CL, CE, rec);
	emphasis(pl);
	entities_at(CL);
	if (XK) qsort(f->em, XK, 2 * sizeof *f->em, by_first);
	scripts(pl, CL, f->em, XK);
	qsort(C, CN, sizeof *C, by_first);   /* a Conceal starts with its a */
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

/* "<2026-11-19 Thu 17:30>" for minute t (since the epoch), with the time
   when hm, extra x[0, k) before the closing bracket */
static size_t stamp(char *o, long t, int hm, int inactive, const char *x, size_t k)
{
	static const char *const wd[] = { "Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed" };
	long z = date_fdiv(t, 1440), mi = t - z * 1440;
	int y, m, d;
	date_civil(z, &y, &m, &d);
	size_t n = (size_t)sprintf(o, "%c%04d-%02d-%02d %s", inactive ? '[' : '<', y, m, d, wd[z - 7 * date_fdiv(z, 7)]);
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
	*t = date_days(num(p, p + 4), num(p + 5, p + 7), num(p + 8, p + 10)) * 1440 + h * 60 + mi;
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
		h -= 24 * date_fdiv(h, 24);
		x[1] = (char)('0' + h / 10), x[2] = (char)('0' + h % 10), x[4] = (char)('0' + mi / 10), x[5] = (char)('0' + mi % 10);
	}
	long day = date_fdiv(t, 1440), mi = t - day * 1440;
	if (what == 'M' || what == 'y')   /* from the digits, as Org: 2026-02-30 +1m is 03-30 */
		day = date_days(num(a + 1, a + 5) + (what == 'y' ? n : 0), num(a + 6, a + 8) + (what == 'M' ? n : 0), num(a + 9, a + 11));
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
		if (type == '.') tschange(ts, what == 'h' ? now - t : date_fdiv(now, 1440) - date_fdiv(t, 1440), what == 'h' ? 'm' : 'd', 0);
		if (type == '+') {
			do {   /* Org asks "Continue?" at the tenth shift; yes is the coordinator's call while Davy's card is open */
				tschange(ts, n, what, 0);
				tsbody(ts + 1, &t);
			} while (what == 'h' ? !(now < t) : date_fdiv(now, 1440) >= date_fdiv(t, 1440));
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

/* ---- org-open-at-point (org.el, ol.el, org-footnote.el): where a link, a
   footnote or a headline leads.  The object under the cursor is found as
   org-element-context finds it: Org's object lexer (org-element--object-lex)
   over the element's contents, moving into the object holding the cursor.
   Known differences: link types and the TODO keywords match case and all,
   where Org folds case (it finds http in HTTP:x); a coderef's label is
   always (ref:%s) (a block's -l switch is not read); a [[/regexp/]] search
   (an occur tree in Org) searches its words as a heading's ---- */

static size_t LO, PT, NN;   /* the narrowed text's start (N its end), the lexer's point, the text's length */
static size_t (*RT)[2], RN, RCAP;   /* the radio targets' values (org-target-link-regexp), longest first */
static int bolp(size_t p) { return p <= LO || S[p - 1] == '\n'; }
static int anum(size_t p) { return p < N && IN(ALNUM, uni_cp(S, N, p)); }   /* [[:alnum:]] */
static size_t nextc(size_t p) { return p < N ? p + uni_len(S, N, p) : p + 1; }
static int wstart(size_t p) { return word(p) && (p <= LO || !word(uni_prev(S, N, p))); }   /* \< */
static size_t after_str(size_t p, const char *k)   /* search-forward k from p: the match's end, or 0 */
{
	size_t n = strlen(k);
	for (; p + n <= N; p++) if (!memcmp(S + p, k, n)) return p + n;
	return 0;
}

enum { O_BOLD, O_CODE, O_ITALIC, O_STRIKE, O_UNDER, O_VERB, O_SUB, O_SUP, O_ENTITY, O_LATEX, O_BREAK,
       O_SNIP, O_MACRO, O_TARGET, O_RADIO, O_COOKIE, O_FN, O_CALL, O_ISRC, O_TS, O_CITE, O_LINK, O_CELL, O_REF };
#define BIT(t) (1u << (t))
enum {   /* org-element-object-restrictions */
	R_MIN = BIT(O_BOLD) | BIT(O_CODE) | BIT(O_ENTITY) | BIT(O_ITALIC) | BIT(O_LATEX) | BIT(O_STRIKE) |
	        BIT(O_SUB) | BIT(O_SUP) | BIT(O_UNDER) | BIT(O_VERB),
	R_STD = BIT(O_CELL) - 1,   /* all but table-cell and citation-reference */
	R_LINK = R_MIN | BIT(O_SNIP) | BIT(O_CALL) | BIT(O_ISRC) | BIT(O_MACRO) | BIT(O_COOKIE),
	R_CELL = R_MIN | BIT(O_CITE) | BIT(O_SNIP) | BIT(O_FN) | BIT(O_LINK) | BIT(O_MACRO) | BIT(O_RADIO) |
	         BIT(O_TARGET) | BIT(O_TS),
};
static unsigned restr(int t)
{
	return t == O_LINK ? R_LINK : t == O_RADIO ? R_MIN : t == O_CELL ? R_CELL : t == O_CITE ? BIT(O_REF) : t == O_FN || t == O_BOLD ||
	       t == O_ITALIC || t == O_STRIKE || t == O_UNDER || t == O_SUB || t == O_SUP ? R_STD : 0;
}
/* an object: type, [b, e) with its blanks after, contents [cb, ce) (cb ORG_NONE when none) */
typedef struct { int t, f; size_t b, e, cb, ce, la, lz; } Obj;   /* f, [la, lz): a link's, as LK */
static int obj(Obj *o, int t, size_t b, size_t z, size_t cb, size_t ce)
{
	*o = (Obj){ t, 0, b, blanks(z), cb, ce, 0, 0 };
	return 1;
}

/* the radio target i at y, case folded, its blank runs any whitespace
   (\s-+): the end, or 0 */
static size_t rmatch(size_t y, size_t i)
{
	for (size_t a = RT[i][0], z = RT[i][1]; a < z; )
		if (S[a] == ' ') {
			while (a < z && S[a] == ' ') a++;
			if (!sp(y)) return 0;
			while (sp(y)) y = nextc(y);
		} else if (y < N && up(S[y]) == up(S[a])) y++, a++;
		else return 0;
	return y;
}
/* org-target-link-regexp at q (looking-at): the target's span in [*a, *z) */
static int radio_at(size_t q, size_t *a, size_t *z)
{
	for (int alt = 0; alt < 2; alt++) {   /* ^ or [^[:alnum:]] before, $ or [^[:alnum:]] after */
		size_t y = alt ? nextc(q) : q, e;
		if (alt ? q >= N || anum(q) : !bolp(q)) continue;
		for (size_t i = 0; i < RN; i++)
			if ((e = rmatch(y, i)) && (e >= N || !anum(e))) return *a = y, *z = e, 1;
	}
	return 0;
}
static size_t radio_search(size_t p, size_t *a, size_t *z)   /* its next match from p: where it starts, or ORG_NONE */
{
	for (; p <= N; p = nextc(p)) if (radio_at(p, a, z)) return p;
	return ORG_NONE;
}

/* the object parsers, each at the p org-element--object-lex calls it at */
static int emph(size_t p, int t, Obj *o)   /* org-element--parse-generic-emphasis */
{
	if (!(bolp(p) || sp(uni_prev(S, N, p)) || one(p - 1, "-('\"{") || (bolp(p - 1) && S[p - 1] == S[p])) || p + 1 >= N || sp(p + 1))
		return 0;
	size_t c = fits(p, p + 2, N);
	if (c >= N) return 0;
	return t == O_CODE || t == O_VERB ? obj(o, t, p, c + 1, ORG_NONE, 0) : obj(o, t, p, c + 1, p + 1, c);
}
static int subsup(size_t p, int t, Obj *o)   /* org-match-substring-regexp, from the character before p */
{
	size_t q = p + 1, z, y, e = 0;
	if (bolp(p) || sp(uni_prev(S, N, p))) return 0;
	if (one(q, "{(") && (z = braces(q, S[q], S[q] == '{' ? '}' : ')', ORG_NONE)))
		return S[q] == '{' ? obj(o, t, p, z + 1, q + 1, z) : obj(o, t, p, z + 1, q, z + 1);
	if (one(q, "*")) return obj(o, t, p, q + 1, q, q + 1);
	for (y = q + one(q, "+-"); anum(y) || one(y, ".,\\"); ) if (y = nextc(y), anum(uni_prev(S, N, y))) e = y;
	return e && obj(o, t, p, e, q, e);
}
static int entity_o(size_t p, Obj *o)   /* org-element-entity-parser */
{
	size_t x, k;
	if (is(p + 1, "_ ")) return (k = spaces(p + 2) - p - 2) <= 20 && obj(o, O_ENTITY, p, p + 2 + k, ORG_NONE, 0);
	return ent(p, &x) && entity(p + 1, x) && obj(o, O_ENTITY, p, x + 2 * is(x, "{}"), ORG_NONE, 0);
}
static int latex(size_t p, Obj *o)   /* org-element-latex-fragment-parser */
{
	size_t z = 0, m;
	if (S[p] != '$') z = is(p + 1, "(") ? after_str(p, "\\)") : is(p + 1, "[") ? after_str(p, "\\]") :
	                     alpha(p + 1) && macro_re(p, &m) == p ? m : 0;
	else if (is(p + 1, "$")) z = after_str(p + 2, "$$");
	else if (!(p > LO && S[p - 1] == '$') && !one(p + 1, " \t\n,.;") && (m = after_str(p + 1, "$")) &&
	         !one(m - 2, " \t\n,.") && (ateol(m) || IN(DPOST, uni_cp(S, N, m))))
		z = m;
	return z && obj(o, O_LATEX, p, z, ORG_NONE, 0);
}
static int lbreak(size_t p, Obj *o)   /* org-element-line-break-parser */
{
	if (!is(p, "\\\\") || !ateol(blanks(p + 2)) || (p > LO && S[p - 1] == '\\')) return 0;
	*o = (Obj){ O_BREAK, 0, p, nextl(p), ORG_NONE, 0, 0, 0 };
	return 1;
}
static int ascii_alnum(size_t p) { return alpha(p) || digit(p); }
static int snippet(size_t p, Obj *o)   /* @@\([-A-Za-z0-9]+\):, then @@ */
{
	size_t x = p + 2, y;
	while (ascii_alnum(x) || one(x, "-")) x++;
	return x > p + 2 && is(x, ":") && (y = after_str(x + 1, "@@")) && obj(o, O_SNIP, p, y, ORG_NONE, 0);
}
static int macro_o(size_t p, Obj *o)   /* {{{\([a-zA-Z][-a-zA-Z0-9_]*\)\((\(\(?:.\|\n\)*?\))\)?}}} */
{
	size_t x = p + 4, y;
	if (!is(p, "{{{") || !alpha(p + 3)) return 0;
	while (ascii_alnum(x) || one(x, "-_")) x++;
	if (!is(x, "(")) return is(x, "}}}") && obj(o, O_MACRO, p, x + 3, ORG_NONE, 0);
	return (y = after_str(x + 1, ")}}}")) && obj(o, O_MACRO, p, y, ORG_NONE, 0);
}
/* org-target-regexp's value from q: <<\(B\|B[^<>\n\r]*B\)>>, B [^<>\n\r \t]:
   where its >> starts, or 0 */
static size_t tgt(size_t q)
{
	size_t r = q + 1;
	if (q >= N || one(q, "<>\n\r \t")) return 0;
	while (r < N && !one(r, "<>\n\r")) r++;
	return is(r, ">>") && !one(r - 1, " \t") ? r : 0;
}
static int target(size_t p, Obj *o) { size_t r = tgt(p + 2); return is(p, "<<") && r && obj(o, O_TARGET, p, r + 2, ORG_NONE, 0); }
static int radio_t(size_t p, Obj *o) { size_t r = tgt(p + 3); return is(p, "<<<") && r && is(r, ">>>") && obj(o, O_RADIO, p, r + 3, p + 3, r); }
static int cookie(size_t p, Obj *o)   /* \[[0-9]*\(%\|/[0-9]*\)\] */
{
	size_t x = digits(p + 1, N);
	if (is(x, "%]")) return obj(o, O_COOKIE, p, x + 2, ORG_NONE, 0);
	return is(x, "/") && is(x = digits(x + 1, N), "]") && obj(o, O_COOKIE, p, x + 1, ORG_NONE, 0);
}
static int fnref(size_t p, Obj *o)   /* org-element-footnote-reference-parser */
{
	size_t x = fnre(p), c;
	if (!x || (c = pair(p, '[', ']')) == ORG_NONE) return 0;
	return S[x - 1] == ':' ? obj(o, O_FN, p, c, x, c - 1) : obj(o, O_FN, p, c, ORG_NONE, 0);
}
/* \<call_\([^ \t\n[(]+\)[([] and its brackets, or \<src_\([^ \t\n[{]+\)[{[] */
static int inline_o(size_t p, int t, Obj *o)
{
	int c = t == O_CALL ? '(' : '{';
	size_t x = p + (t == O_CALL ? 5 : 4), y;
	if (!is(p, t == O_CALL ? "call_" : "src_") || !wstart(p)) return 0;
	while (x < N && !one(x, " \t\n[") && S[x] != c) x++;
	if (x == p + (t == O_CALL ? 5 : 4) || !(one(x, "[") || (x < N && S[x] == c))) return 0;
	if (is(x, "[") && (y = pair(x, '[', ']')) != ORG_NONE) x = y;
	if (x >= N || S[x] != c || (x = pair(x, c, c == '(' ? ')' : '}')) == ORG_NONE) return 0;
	if (t == O_CALL && is(x, "[") && (y = pair(x, '[', ']')) != ORG_NONE) x = y;
	return obj(o, t, p, x, ORG_NONE, 0);
}
static int ts_o(size_t p, Obj *o) { OrgTs t; return ts(p, 1, &t) && obj(o, O_TS, p, t.end, ORG_NONE, 0); }
static int keyc(size_t p) { return p < N && (word(p) || one(p, "-.:?!`'/*@+|(){}<>&_^$#%~")); }
static size_t citekey(size_t x, size_t z)   /* org-element-citation-key-re's next match in [x, z): its end, or 0 */
{
	while (x + 1 < z && !(S[x] == '@' && keyc(x + 1))) x++;
	if (x + 1 >= z) return 0;
	for (x++; x < z && keyc(x); x++) ;
	return x;
}
static int cite(size_t p, Obj *o)   /* org-element-citation-parser */
{
	size_t x = p + 5, y = x + 1, c, k, b, e;
	if (!lit(p, "[cite")) return 0;
	if (is(x, "/")) { while (anum(y) || one(y, "/_-")) y = nextc(y); if (y > x + 1) x = y; }
	if (!is(x, ":") || (c = pair(p, '[', ']')) == ORG_NONE || !(k = citekey(x, c))) return 0;
	for (x++; one(x, "\t\n "); x++) ;
	for (b = k; b > x && S[b - 1] != ';'; b--) ;   /* after a common prefix */
	for (e = c - 1; e > k && one(e - 1, " \r\t\n"); e--) ;
	for (y = e; y > k && S[y - 1] != ';'; y--) ;   /* before a common suffix */
	return obj(o, O_CITE, p, c, b, y > k && !citekey(y, e) ? y : e);
}
static int ref(size_t p, Obj *o)   /* org-element-citation-reference-parser */
{
	size_t k = citekey(p, N), e;
	if (!k) return 0;
	return e = after_str(k, ";"), *o = (Obj){ O_REF, 0, p, e ? e : N, ORG_NONE, 0, 0, 0 }, 1;
}
static int cell(size_t p, Obj *o)   /* [ \t]*\(.*?\)[ \t]*\(?:|\|$\) */
{
	size_t x = blanks(p), y = x, z;
	while ((z = blanks(y)) < N && S[z] != '|' && S[z] != '\n') y = nextc(y);
	*o = (Obj){ O_CELL, 0, p, z < N && S[z] == '|' ? z + 1 : z, x, y, 0, 0 };
	return 1;
}
/* org-element-link-parser: o->f the format ('r' radio, 'b' bracket, 'p'
   plain, 'a' angle), [la, lz) its path (a radio's text, a bracket's URI,
   a plain or angle link's type, colon and path) */
static int link(size_t p, Obj *o)
{
	size_t a, z, va, vz, f = 0;
	int r = 0;
	if (RN && radio_at(bolp(p) ? p : uni_prev(S, N, p), &a, &z)) r = obj(o, O_LINK, p, z, a, z), o->f = 'r';
	else if (is(p, "[[") && (z = uri(p + 2, p + 2, &va, &vz, 0, &f))) {
		r = va == p + 2 ? obj(o, O_LINK, p, z, ORG_NONE, 0) : obj(o, O_LINK, p, z, va, vz);
		o->f = 'b'; a = p + 2; z = va == p + 2 ? vz : va - 2;
	} else if ((wstart(p) && (z = plain(p))) || (S[p] == '<' && (z = angle(p, &f)))) {
		r = obj(o, O_LINK, p, z, ORG_NONE, 0);
		o->f = S[p] == '<' ? 'a' : 'p'; a = p + (S[p] == '<'); z -= S[p] == '<';
	}
	if (r) o->la = a, o->lz = z;
	return r;
}

/* org-element--object-regexp at p, case folded: a candidate */
static int cand(size_t p)
{
	if (p >= N) return 0;
	if (one(p, "_^") && (one(p + 1, "-{(*+.,") || anum(p + 1))) return 1;
	if (one(p, "*~=+_/") && p + 1 < N && !sp(p + 1)) return 1;
	switch (S[p]) {
	case '[': return lit(p + 1, "cite:") || lit(p + 1, "cite/") || lit(p + 1, "fn:") || digit(p + 1) || is(p + 1, "%]") ||
	                 (is(p + 1, "/") && is(digits(p + 2, N), "]")) || is(p + 1, "[");
	case '@': return is(p + 1, "@");
	case '{': return is(p + 1, "{{");
	case '<':
		if (is(p + 1, "%%") || is(p + 1, "<") || digit(p + 1)) return 1;
		for (size_t i = 0; i < sizeof ltypes / sizeof *ltypes; i++) if (lit(p + 1, ltypes[i])) return 1;
		return 0;
	case '$': return 1;
	case '\\': return alpha(p + 1) || one(p + 1, "[(") || (is(p + 1, "\\") && ateol(blanks(p + 2))) || is(p + 1, "_ ");
	}
	return lit(p, "call_") || lit(p, "src_") || (wstart(p) && ltype(p));
}
/* the parser org-element--object-lex calls for the candidate at p */
static int parse_at(size_t p, unsigned r, Obj *o)
{
	int d = p + 1 < N ? S[p + 1] : 0;
#define OK(t) (r & BIT(t))
	if (lit(p, "call_")) return OK(O_CALL) && inline_o(p, O_CALL, o);
	if (lit(p, "src_")) return OK(O_ISRC) && inline_o(p, O_ISRC, o);
	switch (S[p]) {
	case '^': return OK(O_SUP) && subsup(p, O_SUP, o);
	case '_': return (OK(O_UNDER) && emph(p, O_UNDER, o)) || (OK(O_SUB) && subsup(p, O_SUB, o));
	case '*': return OK(O_BOLD) && emph(p, O_BOLD, o);
	case '/': return OK(O_ITALIC) && emph(p, O_ITALIC, o);
	case '~': return OK(O_CODE) && emph(p, O_CODE, o);
	case '=': return OK(O_VERB) && emph(p, O_VERB, o);
	case '+': return OK(O_STRIKE) && emph(p, O_STRIKE, o);
	case '@': return OK(O_SNIP) && snippet(p, o);
	case '{': return OK(O_MACRO) && macro_o(p, o);
	case '$': return OK(O_LATEX) && latex(p, o);
	case '<': return d == '<' ? (OK(O_RADIO) && radio_t(p, o)) || (OK(O_TARGET) && target(p, o))
	                          : (OK(O_TS) && ts_o(p, o)) || (OK(O_LINK) && link(p, o));
	case '\\': return d == '\\' ? OK(O_BREAK) && lbreak(p, o) : (OK(O_ENTITY) && entity_o(p, o)) || (OK(O_LATEX) && latex(p, o));
	case '[':
		if (d == '[' && OK(O_LINK)) return link(p, o);
		if (d == 'f' && OK(O_FN)) return fnref(p, o);
		if (d == 'c' && OK(O_CITE)) return cite(p, o);
		if ((d == '%' || d == '/') && OK(O_COOKIE)) return cookie(p, o);
		return (OK(O_TS) && ts_o(p, o)) || (OK(O_COOKIE) && cookie(p, o));
	}
	return OK(O_LINK) && link(p, o);
#undef OK
}
/* org-element--object-lex from PT for the objects r allows: the next one */
static int lex(unsigned r, Obj *o)
{
	if (r & BIT(O_CELL)) return cell(PT, o);
	if (r & BIT(O_REF)) return ref(PT, o);
	size_t lim = N, n = N, a = 0, z, x = ORG_NONE;
	if (RN && (r & BIT(O_LINK))) {   /* stop before a radio link, then parse it */
		size_t from = bolp(PT) ? PT : uni_prev(S, N, PT);
		if ((x = radio_search(from, &a, &z)) != ORG_NONE && PT == nextc(bol(from)) && PT == z)
			x = radio_search(ateol(z) ? z : nextc(z), &a, &z);   /* not the one-character radio link before PT again */
		if (x != ORG_NONE) lim = nextc(a);
	}
	for (size_t p = PT; ; p = nextc(p)) {
		N = lim;
		while (p < N && !cand(p)) p = nextc(p);
		N = n;
		if (p >= lim) break;
		if (parse_at(p, r, o)) return 1;
	}
	return x != ORG_NONE && link(a, o);
}

/* org-element-at-point: the innermost element holding pos (org-element--parse-to) */
static size_t at_point(size_t pos)
{
	size_t par = ORG_NONE;
	for (size_t j = 0; j < PK; j++) {
		const OrgNode *e = &PE[j];
		if (e->parent != par || (e->end <= pos && e->end != NN)) continue;
		if (!greater(e->type)) return j;
		if (e->cbeg == ORG_NONE || !(e->cbeg < pos || (e->cbeg == pos && e->type != ORG_PLAIN_LIST && e->type != ORG_TABLE)) ||
		    !(e->cend > pos || (pos == e->cend && pos == NN)))
			return j;
		par = j;
	}
	return par;
}
/* [ \t]+\(:[[:alnum:]_@#%:]+:\) then [ \t]*$, or [ \t]*$, at q: the tags'
   [*ga, *gz) (ORG_NONE when none) */
static int tail(size_t q, size_t *ga, size_t *gz)
{
	size_t b = blanks(q), x = b + 1;
	*ga = *gz = ORG_NONE;
	if (ateol(b)) return 1;
	if (b == q || S[b] != ':') return 0;
	while (x < N && (one(x, ":_@#%") || anum(x))) x = nextc(x);
	if (x < b + 3 || S[x - 1] != ':' || !ateol(blanks(x))) return 0;
	return *ga = b, *gz = x, 1;
}
/* org-complex-heading-regexp on the headline h, case matters: the title
   (match 4) in [*a, *z) and the tags (match 5) in [*ga, *gz), ORG_NONE
   when none */
static void title(size_t h, size_t *a, size_t *z, size_t *ga, size_t *gz)
{
	size_t q = stars(h), k, r, s;
	*a = *z = ORG_NONE;
	for (int step = 0; step < 3; step++) {   /* with the keyword and the priority, without one, without both */
		r = q;
		if (step == 0 && is(r, " ")) {
			s = spaces(r);
			for (int i = 0; org_todo[i]; i++) if (k = strlen(org_todo[i]), s + k <= N && !memcmp(S + s, org_todo[i], k)) { r = s + k; break; }
			if (r == q) continue;
		}
		if (step < 2 && is(r, " ") && is(s = spaces(r), "[#") && s + 3 < N && S[s + 2] != '\n' && S[s + 3] == ']') r = s + 4;
		else if (step == 1) continue;
		if (tail(r, ga, gz)) return;   /* (?: +\(.*?\))?? takes nothing when it can */
		if (!is(r, " ")) continue;
		for (s = spaces(r), r = s; !tail(r, ga, gz); r = nextc(r)) ;
		*a = s; *z = r;
		return;
	}
}

static Obj OB[64];   /* org-element-context's objects, outermost first */
static size_t EL;    /* and its element (org-element-at-point) */
/* an unordered item's tag on its line l (org-list-full-item-re's group 4):
   [*a, *z), or 0 */
static int itag(size_t l, size_t *a, size_t *z)
{
	size_t x = blanks(l), y, r;
	if (!one(x, "-+*") || !(blank(x + 1) || ateol(x + 1))) return 0;
	x = blanks(x + 1);
	if (is(x, "[@")) {   /* \[@\(?:start:\)?\([0-9]+\|[a-zA-Z]\)\][ \t]* */
		y = x + 2 + 6 * lit(x + 2, "start:");
		y = digit(y) ? digits(y, N) : y + alpha(y);
		if (y > x + 2 && is(y, "]")) x = blanks(y + 1);
	}
	if (is(x, "[") && one(x + 1, " X-") && is(x + 2, "]") && (blank(x + 3) || ateol(x + 3))) x = blanks(x + 3);
	for (r = eol(x); r > x + 1; r--)   /* \(.*\)[ \t]+::\(?:[ \t]+\|$\) */
		if (is(r, "::") && blank(r - 1) && (blank(r + 2) || ateol(r + 2))) return *a = x, *z = r - 1, 1;
	return 0;
}
/* org-element-context at pos, in OB: the objects it moved into and, last,
   the one it returns; how many (0: it returns the element EL, ORG_NONE
   when there is none).  Known difference: past 64 nested objects it
   returns the 64th */
static int context_in(size_t pos)
{
	size_t x, a, z, k;
	unsigned r;
	int n = 0, ln = -1;
	Obj o, last;
	if ((EL = at_point(pos)) == ORG_NONE) return 0;
	const OrgNode *e = &PE[EL];
	if (pos < e->aff) {   /* an affiliated keyword: only CAPTION's values hold objects */
		size_t b = blanks(bol(pos)), y = b + 9;
		if (!lit(b, "#+CAPTION")) return 0;
		a = z = ORG_NONE;
		if (is(y, "[")) for (x = eol(y); x >= y + 3; x--) if (S[x - 2] == ']' && S[x - 1] == ':') { a = y + 1; z = x - 2; y = x - 1; break; }
		if (!is(y, ":")) return 0;
		if ((k = blanks(y + 1)) <= pos) LO = k, N = eol(k);
		else if (a != ORG_NONE && pos >= a && pos < z) LO = a, N = z;
		else return 0;
		r = R_STD & ~BIT(O_FN);
	} else if (e->type == ORG_ITEM) {   /* in its tag (Org finds it again from the line's start: earlier, only in the bullet, checkbox and counter, holding no object) */
		if (bol(pos) != e->aff || !itag(e->aff, &a, &z) || pos < a || pos >= z) return 0;
		LO = a; N = z; r = R_STD & ~BIT(O_BREAK);
	} else if (e->type == ORG_HEADLINE) {   /* in its title */
		title(e->begin, &a, &z, &x, &k);
		if (a == ORG_NONE || pos > z) return 0;
		a += 7 * is(a, "COMMENT");
		if (a >= z) return 0;
		LO = a; N = z; r = R_STD & ~BIT(O_BREAK);
	} else if (e->type == ORG_PARAGRAPH || e->type == ORG_TABLE_ROW || e->type == ORG_VERSE_BLOCK) {
		if (e->cbeg == ORG_NONE || pos < e->cbeg || !(pos < e->cend || (pos == e->cend && pos == NN))) return 0;
		LO = e->cbeg; N = e->cend; r = e->type == ORG_TABLE_ROW ? BIT(O_CELL) : R_STD;
	} else return 0;
	for (PT = LO; lex(r, &o) && o.b <= pos; ) {
		if (o.e <= pos && o.e != N) {   /* past it; one ending at pos, no blank before, is returned if nothing starts there */
			if (o.e <= PT) break;
			PT = o.e;
			if (o.e == pos && !one(pos - 1, " \t")) last = o, ln = n;
		} else if (o.cb != ORG_NONE && pos >= o.cb && (pos < o.ce || (pos == o.ce && (pos == N || !one(pos - 1, " \t")))) && n < 63) {
			OB[n++] = o;   /* into it */
			PT = LO = o.cb; N = o.ce; r = restr(o.t);
		} else return OB[n++] = o, n;
	}
	if (ln >= 0) OB[ln] = last, n = ln + 1;
	return n;
}
static int context(size_t pos)
{
	int n = context_in(pos);
	LO = 0; N = NN;
	return n;
}
/* org-element-end of the context n returned, less its blanks before (skip-chars-backward " \t") */
static size_t ctx_end(int n)
{
	size_t b = n ? OB[n - 1].b : PE[EL].begin, e = n ? OB[n - 1].e : PE[EL].end;
	while (e > b && one(e - 1, " \t")) e--;
	return e;
}

/* ---- the searches: org-link-search, org-link--search-radio-target ---- */
typedef struct { const char *s; size_t n; } Word;
static Word *QW;     /* the search string's words (split-string) */
static size_t QK, QWCAP;
static char *Q;      /* the search string, or a link's path */
static size_t QCAP;
static int split(const char *q)
{
	QK = 0;
	for (const char *p = q; *p; ) {
		p += strspn(p, " \f\t\n\r\v");
		size_t n = strcspn(p, " \f\t\n\r\v");
		if (!n) break;
		if (!grow((void **)&QW, &QWCAP, QK, sizeof *QW)) return 0;
		QW[QK++] = (Word){ p, n };
		p += n;
	}
	return 1;
}
/* the words, case folded, at x with blanks between (sep 0 [ \t\n]+,
   1 [ \t]+, 2 [ \t]+\(?:\n[ \t]*\)?): the end, or 0 */
static size_t words_at(size_t x, int sep)
{
	for (size_t i = 0; i < QK; i++) {
		size_t y = x;
		if (i && sep) { y = blanks(x); if (sep == 2 && y > x && is(y, "\n")) y = blanks(y + 1); }
		else if (i) while (one(y, " \t\n")) y++;
		if (i && y == x) return 0;
		for (size_t j = 0; j < QW[i].n; j++, y++) if (y >= N || up(S[y]) != up((unsigned char)QW[i].s[j])) return 0;
		x = y;
	}
	return x;
}
/* the words of [a, z) (statistics cookies separate words, when cookies)
   are the search's, case folded */
static int words_are(size_t a, size_t z, int cookies)
{
	size_t i = 0, j = 0, n = N, x = a, k;
	int in = 0, ok = 0;
	Obj c;
	N = z;
	for (;;) {
		k = x < z && cookies && is(x, "[") && cookie(x, &c) ? c.e - x : 0;
		if (x >= z || k || one(x, " \f\t\n\r\v")) {
			if (in && j != QW[i++].n) break;
			in = 0;
			if (x >= z) { ok = i == QK; break; }
			x += k ? k : 1;
		} else {
			if (!in && i >= QK) break;
			if (!in) in = 1, j = 0;
			if (j >= QW[i].n || up(S[x]) != up((unsigned char)QW[i].s[j])) break;
			j++, x++;
		}
	}
	N = n;
	return ok;
}
static void say(OrgOpen *o, int kind, size_t pos, const char *fmt, const char *a)
{
	o->kind = kind; o->pos = pos;
	snprintf(o->s, sizeof o->s, fmt, a);
}
/* org-link-search for q, which holds no newline (a link's path) */
static void lsearch(char *q, OrgOpen *o)
{
	size_t n = strlen(q), l, x, y, z, a, k;
	if (!q[strspn(q, " \r\t\n")]) return say(o, ORG_OPEN_ERROR, 0, "Invalid search string \"%s\"", q);
	if (!split(q + (*q == '*'))) return say(o, -1, 0, "%s", "");
	if (*q == '#') {   /* org-find-property "CUSTOM_ID" */
		for (l = 0; l < N; l = nextl(l))
			if (x = blanks(l), lit(x, ":CUSTOM_ID:") && blank(x + 11) && lit(y = blanks(x + 11), q + 1) &&
			    ateol(blanks(y + n - 1)) && (k = at_point(l)) != ORG_NONE && PE[k].type == ORG_NODE_PROPERTY)
				return say(o, ORG_OPEN_GOTO, (k = heading(l)) == ORG_NONE ? 0 : k, "%s", "");
		return say(o, ORG_OPEN_ERROR, 0, "No match for custom ID: %s", q + 1);
	}
	if (*q == '(' && n > 1 && q[n - 1] == ')') {   /* a coderef: its words less the parentheses */
		QW[0].s++, QW[0].n--, QW[QK - 1].n--;
		q[n - 1] = 0;
		for (x = 0; x < N; x++) {
			if (!(y = words_at(x, 1))) continue;
			if ((k = at_point(y)) != ORG_NONE && (PE[k].type == ORG_SRC_BLOCK || PE[k].type == ORG_EXAMPLE_BLOCK)) {
				for (z = eol(y); z > bol(y) && blank(z - 1); z--) ;   /* .*?\([ \t]*\((ref:LABEL)\)[ \t]*\)$ */
				if (z >= bol(y) + n + 4 && lit(z - n - 4, "(ref:") && lit(z - n + 1, q + 1) && is(z - 1, ")"))
					return say(o, ORG_OPEN_GOTO, z - n - 4, "%s", "");
			}
			if (y > x) x = y - 1;   /* on from the match's end (at an empty one Org searches forever) */
		}
		return say(o, ORG_OPEN_ERROR, 0, "No match for coderef: %s", q + 1);
	}
	if (*q != '*') {
		for (x = 0; x + 2 <= N; x++)   /* <<words>> holding a target */
			if (is(x, "<<") && (y = words_at(x + 2, 0)) && is(y, ">>")) {
				int c = context(y + 1);
				if (c && OB[c - 1].t == O_TARGET) return say(o, ORG_OPEN_GOTO, OB[c - 1].b, "%s", "");
				x = y;
			}
		for (l = 0; l < N; l = nextl(l)) {   /* ^[ \t]*#\+NAME: +words[ \t]*$ naming an element */
			x = blanks(l);
			if (!lit(x, "#+NAME:") || !is(x + 7, " ") || !(y = words_at(spaces(x + 7), 1)) || !ateol(blanks(y)) ||
			    (k = at_point(eol(l))) == ORG_NONE || PE[k].begin > l || PE[k].aff <= l)
				continue;
			for (a = l, z = l; z < PE[k].aff; z = nextl(z)) if (lit(blanks(z), "#+NAME:")) a = z;   /* the last NAME is its name */
			a = blanks(blanks(a) + 7);
			for (z = eol(a); z > a && blank(z - 1); z--) ;
			if (words_are(a, z, 0)) return say(o, ORG_OPEN_GOTO, l, "%s", "");
		}
	}
	for (l = 0; l < N; l = nextl(l)) {   /* a headline's title, less COMMENT, TODO keyword, priority and tags */
		if (!headline(l)) continue;
		title(l, &a, &z, &x, &y);
		if (a == ORG_NONE) a = z = l;
		if (is(a, "COMMENT") && one(a + 7, " \t")) a = blanks(a + 7);
		if (words_are(a, z, 1)) return say(o, ORG_OPEN_GOTO, l, "%s", "");
	}
	say(o, ORG_OPEN_ERROR, 0, "No match for fuzzy expression: %s", q);
}
/* org-link--search-radio-target for the words of q */
static void rsearch(OrgOpen *o, const char *q)
{
	for (size_t x = 0, y; x + 3 <= N; x++)   /* <<<words>>> holding a radio target */
		if (is(x, "<<<") && (y = words_at(x + 3, 2)) && is(y, ">>>")) {
			int c = context(y + 2);
			if (c && OB[c - 1].t == O_RADIO) return say(o, ORG_OPEN_GOTO, OB[c - 1].b, "%s", "");
			x = y + 1;
		}
	say(o, ORG_OPEN_ERROR, 0, "No match for radio target: %s", q);
}

/* ---- footnotes: org-footnote-action ---- */
/* \[fn:L[]:] at x, L the label [la, lz), case folded: its end, or 0 */
static size_t fn_at(size_t x, size_t la, size_t lz, const char *after)
{
	size_t k = lz - la;
	return lit(x, "[fn:") && x + 5 + k <= N && same(x + 4, la, k) && one(x + 4 + k, after) ? x + 5 + k : 0;
}
/* org-footnote-at-reference-p at x: the reference's begin, or ORG_NONE */
static size_t ref_at(size_t x)
{
	int c = context(x);
	return c && OB[c - 1].t == O_FN && x < ctx_end(c) ? OB[c - 1].b : ORG_NONE;
}
/* org-footnote-goto-definition of the label [la, lz) (org-footnote-get-definition):
   where it leads, or ORG_NONE */
static size_t fn_def(size_t la, size_t lz)
{
	for (size_t x = 0, e, d; x < N; x = nextc(x)) {   /* ^\[fn:L\]\|.\[fn:L: */
		if (!(e = bolp(x) ? fn_at(x, la, lz, "]") : 0) && !(S[x] != '\n' && (e = fn_at(nextc(x), la, lz, ":")))) continue;
		int c = context(e - 1);
		if (c ? OB[c - 1].t == O_FN : EL != ORG_NONE && PE[EL].type == ORG_FOOTNOTE_DEFINITION) {
			d = c ? OB[c - 1].b : PE[EL].aff;   /* Org looks at its begin, before any affiliated keyword, and reads stale match data */
			return (x = fn_at(d, la, lz, "]:")) ? x : d;
		}
		x = e - 2;
	}
	return ORG_NONE;
}
static void footnote(size_t pos, OrgOpen *o)
{
	int c = context(pos);
	size_t la, lz, x;
	if ((c || EL != ORG_NONE) && pos > ctx_end(c)) return say(o, ORG_OPEN_NEW, 0, "%s", "");
	if (c && OB[c - 1].t == O_FN) {
		la = OB[c - 1].b + 4; lz = names(la);
		if (la == lz) return say(o, ORG_OPEN_GOTO, OB[c - 1].cb, "%s", "");   /* anonymous: to its text */
		return say(o, ORG_OPEN_GOTO, (x = fn_def(la, lz)) == ORG_NONE ? pos : x, "%s", "");   /* "Create one?" no */
	}
	if (c) {   /* inside a footnote: org-footnote--allow-reference-p, a new one, or else the menu */
		const Obj *b = &OB[c - 1];
		int ok = !bolp(pos) && (pos == ctx_end(c) || pos == b->b || (b->t != O_LINK && b->cb != ORG_NONE && pos >= b->cb && pos <= b->ce));
		return say(o, ok ? ORG_OPEN_NEW : ORG_OPEN_MENU, 0, "%s", "");
	}
	if (EL == ORG_NONE || PE[EL].type != ORG_FOOTNOTE_DEFINITION) return say(o, ORG_OPEN_NEW, 0, "%s", "");
	la = PE[EL].aff + 4; lz = names(la);   /* org-footnote-goto-previous-reference */
	for (size_t b = pos; b >= lz - la + 5; b--)
		if (fn_at(x = b - (lz - la + 5), la, lz, "]:") && (x = ref_at(x)) != ORG_NONE) return say(o, ORG_OPEN_GOTO, x, "%s", "");
	for (x = pos; x < N; x++) {
		size_t e = fn_at(x, la, lz, "]:"), r;
		if (e && (r = ref_at(e - 1)) != ORG_NONE) return say(o, ORG_OPEN_GOTO, r, "%s", "");
		if (e) x = e - 2;
	}
	char l[256];
	snprintf(l, sizeof l, "%.*s", (int)(lz - la), (const char *)S + la);
	say(o, ORG_OPEN_ERROR, 0, "Cannot find reference of footnote \"%s\"", l);
}

/* ---- following a link: org-link-open ---- */
/* [a, z) into Q, "[ \t]*\n[ \t]*" made nl (a space, or nothing when 0) */
static int qcopy(size_t a, size_t z, int nl)
{
	size_t k = 0, m = 0;   /* m: where the last replacement ends, which blanks before a newline do not reach */
	if (!grow((void **)&Q, &QCAP, z - a + 1, 1)) return 0;
	for (size_t x = a; x < z; x++)
		if (S[x] == '\n') {
			while (k > m && (Q[k - 1] == ' ' || Q[k - 1] == '\t')) k--;
			if (nl) Q[k++] = (char)nl;
			m = k;
			while (x + 1 < z && blank(x + 1)) x++;
		} else Q[k++] = (char)S[x];
	Q[k] = 0;
	return 1;
}
/* org-link-unescape: a backslash run before a bracket or the end is halved */
static void unescape(char *q)
{
	char *w = q;
	for (char *r = q; *r; ) {
		size_t k = strspn(r, "\\");
		if (k && (!r[k] || r[k] == '[' || r[k] == ']')) k /= 2, memset(w, '\\', k), w += k, r += strspn(r, "\\");
		else if (k) memmove(w, r, k), w += k, r += k;
		else *w++ = *r++;
	}
	*w = 0;
}
/* org-link-open of the link k (org-element-link-parser's type and path) */
static void open_link(const Obj *k, OrgOpen *o)
{
	char *path;
	size_t i, n, t = 0;   /* the type's length, which the parser takes in any case */
	if (!qcopy(k->la, k->lz, k->f == 'b' ? ' ' : 0)) return say(o, -1, 0, "%s", "");
	if (k->f == 'r') {
		if (!split(Q)) return say(o, -1, 0, "%s", "");
		return rsearch(o, Q);
	}
	if (k->f == 'b') unescape(Q);
	for (i = 0; i < sizeof ltypes / sizeof *ltypes && !t; i++)   /* org-link-types-re */
		if (n = strlen(ltypes[i]), !strncasecmp(Q, ltypes[i], n) && Q[n] == ':') t = n;
	if (k->f == 'b' && (*Q == '/' || (*Q == '~' && (!Q[1] || Q[1] == '/')) || !strncmp(Q, "./", 2) || !strncmp(Q, "../", 3))) path = Q;
	else if (t) path = Q + t + 1;
	else return lsearch(Q, o);   /* coderef (ref), custom-id #id, fuzzy: searched */
	int ln = path != Q && (strncasecmp(Q, "file", 4) || (t > 4 && (Q[4] != '+' || t == 5)));
	if (!ln) {
		char *opt = strstr(path, "::");   /* in the path before its ::option, two or more slashes and an optional X: then a slash are X: and the slash */
		size_t sl = strspn(path, "/"), pl = opt ? (size_t)(opt - path) : strlen(path);
		if (sl >= 2 && sl + 2 < pl && path[sl + 1] == ':' && path[sl + 2] == '/') path += sl;
		else if (sl >= 3) path += sl - 1;
	}
	if ((size_t)snprintf(o->s, sizeof o->s, "%.*s%s%s", ln ? (int)t : 0, Q, ln ? " " : "", path) >= sizeof o->s)
		return say(o, ORG_OPEN_ERROR, 0, "%s", "Link too long to open");   /* never cut: a cut one opens another */
	o->kind = ln ? ORG_OPEN_LINK : ORG_OPEN_FILE;
}
static const char *const schemes[] = { "aaa://", "about:", "acap://", "apt:", "bzr://", "bzr+ssh://", "attachment:/",
	"chrome://", "cid:", "content://", "crid://", "cvs://", "data:", "dav:", "dict://", "doi:", "dns:", "dtn:", "feed:",
	"file:/", "finger://", "fish://", "ftp://", "geo:", "git://", "go:", "gopher://", "h323:", "http://", "https://",
	"im:", "imap://", "info:", "ipp:", "irc://", "irc6://", "ircs://", "iris.beep:", "jar:", "ldap://", "ldaps://",
	"magnet:", "mailto:", "mid:", "mtqp://", "mupdate://", "news:", "nfs://", "nntp://", "opaquelocktoken:", "pop://",
	"pres:", "resource://", "rmi://", "rsync://", "rtsp://", "rtspu://", "service:", "sftp://", "sip:", "sips:",
	"smb://", "sms:", "snmp://", "soap.beep://", "soap.beeps://", "ssh://", "svn://", "svn+ssh://", "tag:", "tel:",
	"telnet://", "tftp://", "tip://", "tn3270://", "udp://", "urn:", "uuid:", "vemmi://", "webcal://", "xri://",
	"xmlrpc.beep://", "xmlrpc.beeps://", "z39.50r://", "z39.50s://", "xmpp:", "gemini://", "fax:", "man:", "mms://",
	"mmsh://", "modem:", "prospero:", "snews:", "wais://" };   /* thing-at-point-uri-schemes */
static int urlc(size_t p)   /* thing-at-point-bounds-of-url-at-point's allowed-chars; [:alpha:] is [[:alnum:]] less its digits */
{
	unsigned c = p < N ? uni_cp(S, N, p) : 0;
	return c >= 0x80 ? IN(ALNUM, c) && !IN(DIGITS, c) : one(p, "-./0123456789:=&?$+@_~#,%;*()!'[]") || alpha(p);
}
/* thing-at-point-url-at-point's scheme for <URL:[a, z)>: "" when it has
   one, NULL when it is no URL (never news: Org loads Gnus, whose bound
   tables turn thing-at-point-newsgroup-p's guess off) */
static const char *bare(size_t a, size_t z)
{
	size_t n = N, x = a;
	const char *r = NULL;
	N = z;
	while (x < N && (x == a ? alpha(x) : ascii_alnum(x) || one(x, "-+."))) x++;
	if (x > a && is(x, ":")) r = "";
	for (x = a; x < N && !one(x, ":</>@"); x++) ;
	if (!r && x > a && is(x, "@") && (ascii_alnum(x + 1) || one(x + 1, "-.=&?$+_~#,%;*"))) r = "mailto:";
	if (!r) r = lit(a, "ftp.") ? "ftp://" : lit(a, "www.") ? "https://" : NULL;
	N = n;
	return r;
}
/* thing-at-point 'url (not lax) at pos: [*a, *z) and the scheme it lacks, or NULL */
static const char *tap_url(size_t pos, size_t *a, size_t *z)
{
	size_t x, y = 0, b = pos, e = pos, m, i, l = eol(pos);
	for (x = bol(pos); x <= pos && x < l; x = y + 1) {   /* <URL:...>, case folded: the first ending after pos */
		for (; x < l; x++) {
			if (!lit(x, "<URL:")) continue;
			for (y = x + 5; !one(y, "<>\n") && y < l; y++) ;
			if (y > x + 5 && is(y, ">")) break;
		}
		if (x >= l) break;
		if (y >= pos) return *a = x + 5, *z = y, bare(x + 5, y);
	}
	while (b > 0 && urlc(uni_prev(S, N, b))) b = uni_prev(S, N, b);
	while (b < pos && !ascii_alnum(b)) b = nextc(b);
	while (urlc(e)) e = nextc(e);
	while (e > pos && one(e - 1, ":;.,!?'")) e--;
	for (x = b; x < e; x = nextc(x))   /* a scheme at b or after a non-alphanumeric */
		for (i = 0; (x == b || !ascii_alnum(x - 1)) && i < sizeof schemes / sizeof *schemes; i++)
			if ((m = x + strlen(schemes[i])) <= e && lit(x, schemes[i])) {
				if (m == e) return NULL;
				if (x > 0 && one(x - 1, "([")) {   /* after an opening parenthesis: up to its closing one */
					int d = 1;
					for (m = x; m < e && (d += one(m, "([") - one(m, ")]")); m++) ;
					if (m < e && S[m] == (S[x - 1] == '(' ? ')' : ']')) e = m;
				}
				return x <= pos && pos <= e ? (*a = x, *z = e, "") : NULL;
			}
	return NULL;
}
static size_t email(size_t s)   /* thing-at-point-email-regexp at s: its end, or 0 */
{
	size_t p = s + is(s, "<"), q;
	if (!ascii_alnum(p) && !one(p, "-+_~/")) return 0;
	while (ascii_alnum(++p) || one(p, "-+_.~:/")) ;
	if (!is(p, "@")) return 0;
	for (q = ++p; ascii_alnum(q) || is(q, "-") || (q > p && is(q, ".")); q++) ;
	return q > p ? q + is(q, ">") : 0;
}
static size_t prin1(char *d, size_t k, const char *s)   /* s quoted as %S writes it, at d + k */
{
	size_t n = sizeof ((OrgOpen *)0)->s - 1;
	if (k < n) d[k++] = '"';
	for (; *s; s++) {
		if ((*s == '"' || *s == '\\') && k < n) d[k++] = '\\';
		if (k < n) d[k++] = *s;
	}
	if (k < n) d[k++] = '"';
	d[k] = 0;
	return k;
}
/* org-link-open-from-string on pre then [a, z): parsed in a buffer of its
   own, where the link must fill it (these are never searched in this one) */
static void from_string(const char *pre, size_t a, size_t z, OrgOpen *o)
{
	size_t n = strlen(pre), m = n + z - a, nn = N, rn = RN, k;
	const unsigned char *s = S;
	char *t = malloc(m + 1);
	Obj l;
	if (!t) return;
	memcpy(t, pre, n), memcpy(t + n, S + a, z - a), t[m] = 0;
	S = (const unsigned char *)t, N = m, RN = 0;
	int ok = link(0, &l);
	if (ok && l.e == m) open_link(&l, o);
	else {
		o->kind = ORG_OPEN_ERROR, o->pos = 0;
		k = prin1(o->s, (size_t)snprintf(o->s, sizeof o->s, ok ? "Garbage after link in " : "No valid link in "), t);
		if (ok) k = prin1(o->s, k + (size_t)snprintf(o->s + k, sizeof o->s - k, " ("), t + l.e), snprintf(o->s + k, sizeof o->s - k, ")");
	}
	S = s, N = nn, RN = rn;
	free(t);
}
/* org-open-at-point-global: a link (org-link-any-re) or a date around pos */
static void global(size_t pos, OrgOpen *o)
{
	size_t x = bol(pos), lo, hi, k = 0, z, fa = 0, fd = 0, v, w;
	while (x < N && separate(x, N)) x = nextl(x);   /* forward-paragraph's lines */
	for (x = nextl(x); x < N && !separate(x, N); x = nextl(x)) ;
	for (lo = pos; lo < x; lo++) k += S[lo] == '\n';
	k += x == N && N && S[N - 1] != '\n';
	for (lo = bol(pos), hi = eol(pos), z = k; z-- > 0; ) lo = prevl(lo), hi = eol(nextl(hi));
	N = hi;
	for (x = lo; x <= pos; x = z > x ? z : nextc(x)) {
		z = is(x, "[[") ? uri(x + 2, x + 2, &v, &w, 0, &fd) : S[x] == '<' ? angle(x, &fa) : wstart(x) ? plain(x) : 0;
		if (!z || z < pos) continue;
		Obj l;   /* org-link-open-from-string: the link parsed alone (it parses as one), opened here */
		size_t rn = RN;
		RN = 0; LO = x; N = z;
		link(x, &l);
		RN = rn; LO = 0; N = NN;
		return open_link(&l, o);
	}
	N = NN;
	for (x = bol(pos); x < eol(pos); x++)   /* org-ts-regexp-both or org-tsr-regexp-both (a range), the cursor on it */
		if ((z = ts_both(x)) != ORG_NONE && x <= pos) {
			for (k = 1; k < 4 && is(z + k, "-"); k++) ;
			if (pos <= z || (k > 1 && (z = ts_both(z + k)) != ORG_NONE && pos <= z)) return say(o, ORG_OPEN_AGENDA, 0, "%s", "");
		}
	const char *pre = tap_url(pos, &v, &w);
	if (pre) return from_string(pre, v, w, o);
	for (lo = pos, k = 500; k-- && lo > 0; ) lo = uni_prev(S, N, lo);   /* thing-at-point 'email: 500 characters around */
	for (hi = pos, k = 500; k-- && hi < N; ) hi = nextc(hi);
	for (N = hi, x = lo; x < hi && !((z = email(x)) && z >= pos); x = nextc(x)) ;
	N = NN;
	if (x < hi && x <= pos) return from_string("mailto:", x, z, o);
	say(o, ORG_OPEN_ERROR, 0, "%s", "No link found");
}

int org_open(const char *s, size_t n, size_t pos, OrgOpen *o)
{
	S = (const unsigned char *)s; N = NN = n; LO = 0; X = NULL; RN = 0;
	OrgNode *e = org_parse(s, n, &PK);
	if (!(PE = e)) return -1;
	FOLD = 1;
	size_t rn = 0, x, r, i, j, k, el, a, z;
	int c;
	o->kind = -1;
	for (x = 0; x + 3 <= N; x++)   /* org-update-radio-target-regexp */
		if (is(x, "<<<") && (r = tgt(x + 3)) && is(r, ">>>")) {
			if ((c = context(r + 2)) && OB[c - 1].t == O_RADIO) {
				for (i = 0; i < rn && !(RT[i][1] - RT[i][0] == r - x - 3 && !memcmp(S + RT[i][0], S + x + 3, r - x - 3)); i++) ;
				if (i == rn && !grow((void **)&RT, &RCAP, rn, sizeof *RT)) goto out;
				if (i == rn) RT[rn][0] = x + 3, RT[rn++][1] = r;
			}
			x = r + 1;
		}
	for (i = 0; i < rn / 2; i++) a = RT[i][0], z = RT[i][1], RT[i][0] = RT[rn - 1 - i][0], RT[i][1] = RT[rn - 1 - i][1], RT[rn - 1 - i][0] = a, RT[rn - 1 - i][1] = z;
	for (i = 1; i < rn; i++)   /* longest first, a stable sort */
		for (j = i; j > 0 && RT[j][1] - RT[j][0] > RT[j - 1][1] - RT[j - 1][0]; j--)
			a = RT[j][0], z = RT[j][1], RT[j][0] = RT[j - 1][0], RT[j][1] = RT[j - 1][1], RT[j - 1][0] = a, RT[j - 1][1] = z;
	RN = rn;
	c = context(pos);
	for (k = c; k > 0; k--) {   /* org-element-lineage of the kinds Org follows */
		int t = OB[k - 1].t;
		if (t == O_FN || t == O_LINK || t == O_TS || t == O_ISRC || t == O_CITE || t == O_REF) break;
	}
	for (el = EL; !k && el != ORG_NONE; el = PE[el].parent) {
		int t = PE[el].type;
		if (t == ORG_CLOCK || t == ORG_COMMENT || t == ORG_COMMENT_BLOCK || t == ORG_FOOTNOTE_DEFINITION || t == ORG_HEADLINE ||
		    t == ORG_KEYWORD || t == ORG_NODE_PROPERTY || t == ORG_PLANNING || t == ORG_SRC_BLOCK) break;
	}
	int t = k ? OB[k - 1].t : el == ORG_NONE ? -1 : PE[el].type, on = 0;
	OrgTs v;
	if (!k) EL = el;
	if (!k && t == ORG_HEADLINE) title(PE[el].begin, &a, &z, &x, &r);
	if (!k && t == ORG_PLANNING)   /* org-in-regexp org-ts-regexp-both, the cursor on it */
		for (x = bol(pos); x < eol(pos); x++) on |= (z = ts_both(x)) != ORG_NONE && x <= pos && pos <= z;
	if (!k && t == ORG_CLOCK) on = org_clock(s, n, &PE[el], &v, (size_t[2]){ 0 }) && pos >= v.begin && pos <= v.end;
	if (t < 0) say(o, ORG_OPEN_ERROR, 0, "%s", "No link found");
	else if (!k && (t == ORG_COMMENT || t == ORG_COMMENT_BLOCK || t == ORG_NODE_PROPERTY || t == ORG_KEYWORD)) global(pos, o);
	else if (!k && t == ORG_HEADLINE)   /* on its tags, or not: a menu of the entry's links */
		say(o, bol(pos) == PE[el].begin && x != ORG_NONE && pos >= x && pos < r ? ORG_OPEN_TAGS : ORG_OPEN_OFFER, 0, "%s", "");
	else if (k ? t == O_FN : t == ORG_FOOTNOTE_DEFINITION &&   /* on a reference, or a definition's label */
	         (PE[el].cbeg != ORG_NONE ? blanks(pos) < PE[el].cbeg : PE[el].aff == bol(blanks(pos))))
		footnote(pos, o);
	else if (on) say(o, ORG_OPEN_AGENDA, 0, "%s", "");
	else if (!k && t == ORG_SRC_BLOCK) say(o, ORG_OPEN_RESULT, 0, "%s", "");
	else if (!k || pos >= ctx_end((int)k)) say(o, ORG_OPEN_ERROR, 0, "%s", "No link found");   /* the blanks after an object */
	else if (t != O_LINK) say(o, t == O_ISRC ? ORG_OPEN_RESULT : t == O_TS ? ORG_OPEN_AGENDA : ORG_OPEN_CITE, 0, "%s", "");
	else open_link(&OB[k - 1], o);
out:
	free(e);
	PE = NULL; PK = 0; N = NN; FOLD = 0;
	return o->kind < 0 ? -1 : 0;
}
