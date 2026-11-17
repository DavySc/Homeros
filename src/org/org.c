/* Org's element parser, a transcription of org-element.el (Org 9.8.7) at
   granularity element.  Each function names the org-element function it
   copies; the regexps are matched by hand, so each recogniser quotes the
   regexp it stands for.  Positions are bytes; lines end with '\n'.

   Org parses recursively; this keeps the open containers in a stack of
   frames instead, so nesting depth costs heap, not C stack.  Known
   differences, all outside ASCII or outside Davy's files (hypothesis):
   case folding and [:word:]/[:alnum:] are ASCII-exact only (other
   non-space characters count as word and alnum), and a block name inside a
   list is compared literally where Org uses it unquoted as a regexp. */
#include "org.h"
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
/* [-_[:word:]]: Org's syntax table makes $ % ' word characters */
static int namec(size_t p)
{
	if (p >= N) return 0;
	unsigned c = S[p];
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
	       c == '-' || c == '_' || c == '$' || c == '%' || c == '\'' || (c >= 0x80 && !sp(p));
}
static size_t names(size_t p) { while (namec(p)) p++; return p; }
static int digit(size_t p) { return p < N && S[p] >= '0' && S[p] <= '9'; }

/* ---- line recognisers ---- */

static int headline(size_t p)           /* ^\*+ */
{
	if (p != bol(p)) return 0;
	size_t q = p;
	while (q < N && S[q] == '*') q++;
	return q > p && q < N && S[q] == ' ';
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
static int fnl(size_t p) { size_t q = names(p + 4); return lit(p, "[fn:") && q > p + 4 && q < N && S[q] == ']'; }

/* org-element-paragraph-separate at line p */
static int separator(size_t p)
{
	size_t t = p;
	while (t < N && S[t] == '*') t++;
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
	} else if (lit(t, "#+CALL:")) {     /* org-element-babel-call-parser: its brackets may span lines */
		size_t p = blanks((size_t)((const unsigned char *)memchr(S + q, ':', N - q) - S) + 1), bb = nextl(q);
		while (p < bb && S[p] != '[' && S[p] != ']' && S[p] != '(' && S[p] != ')') p++;
		if (p < N && S[p] == '[' && (f = pair(p, '[', ']')) != ORG_NONE) p = f;
		if (p < N && S[p] == '(' && (f = pair(p, '(', ')')) != ORG_NONE) p = f;
		*e = (OrgNode){ b, q, ORG_NONE, ORG_NONE, after(nextl(p), lim), 0, ORG_BABEL_CALL };
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

/* org-element-parse-buffer and org-element--parse-elements */
OrgNode *org_parse(const char *s, size_t n, size_t *count)
{
	S = (const unsigned char *)s; N = n; nomem = 0;
	OrgNode *out = NULL; Frame *fr = NULL;
	size_t no = 0, co = 0, nf = 0, cf = 0;
	it = NULL; nit = cit = 0;
	if (grow((void **)&fr, &cf, 0, sizeof *fr) && grow((void **)&out, &co, 0, sizeof *out))
		fr[nf++] = (Frame){ ORG_NONE, bol(fwd(0, N)), N, 0, 0, ORG_NONE, M_FIRST };
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

/* org-element--headline-parse-title */
void org_head(const char *s, size_t n, size_t q, OrgHead *h)
{
	S = (const unsigned char *)s; N = n;
	size_t p = q, e = eol(q);
	while (p < N && S[p] == '*') p++;
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
	h->pri = -1;                          /* .*?(\[#([A-Z]|[0-9]|[1-5][0-9]|6[0-4])\] ?) */
	for (size_t x = p; x + 3 < e; x++) {
		if (S[x] != '[' || S[x + 1] != '#') continue;
		int c = up(S[x + 2]);
		if (c >= 'A' && c <= 'Z' && S[x + 3] == ']') h->pri = S[x + 2], p = x + 4;   /* case-fold-search t */
		else if (digit(x + 2) && S[x + 3] == ']') h->pri = S[x + 2] - '0', p = x + 4;
		else if (digit(x + 2) && digit(x + 3) && x + 4 < e && S[x + 4] == ']') {
			int v = (S[x + 2] - '0') * 10 + S[x + 3] - '0';
			if (v >= 10 && v <= 64) h->pri = v, p = x + 5;
		}
		if (h->pri >= 0) { if (p < N && S[p] == ' ') p++; break; }
	}
	h->commented = 0;
	if (p + 7 <= N && !memcmp(S + p, "COMMENT", 7) && (ateol(p + 7) || S[p + 7] == ' ')) {
		h->commented = 1;
		p = blanks(p + 7);
	}
	/* tags: [ \t]+(:([[:alnum:]_@#%:]+):)[ \t]*$ searched from before the
	   title's blanks */
	size_t ts = p, te = e, z = e, a;
	while (z > q && blank(z - 1)) z--;
	for (a = z; a > q && !blank(a - 1); a--) ;
	h->ga = h->gz = 0;
	size_t from = ts;
	while (from > q && blank(from - 1)) from--;
	if (from < a && a + 3 <= z && S[a] == ':' && S[z - 1] == ':') {
		size_t x = a, w = a;
		while (x < z && (S[x] == ':' || S[x] == '_' || S[x] == '@' || S[x] == '#' || S[x] == '%' ||
		                 (S[x] >= '0' && S[x] <= '9') || (up(S[x]) >= 'A' && up(S[x]) <= 'Z') || S[x] >= 0x80)) x++;
		while (w > from && blank(w - 1)) w--;
		if (x == z) { te = w; h->ga = a + 1; h->gz = z - 1; }
	}
	size_t lo = ts < te ? ts : te, hi = ts < te ? te : ts;
	while (lo < hi && (blank(lo) || S[lo] == '\r' || S[lo] == '\n')) lo++;
	while (hi > lo && (blank(hi - 1) || S[hi - 1] == '\r')) hi--;
	h->ta = lo; h->tz = hi;
}
