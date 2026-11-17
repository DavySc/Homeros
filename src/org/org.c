/* Org's element parser, copied from org-element.el (Org 9.8.7 in Emacs 31.1):
   each element parser below is the elisp function of the same name, and the
   regexps are Org's own, printed by Emacs, run by a small backtracking
   matcher with Emacs's semantics (leftmost alternative first, lazy and
   greedy repeats, case folding, $ at a search bound).

   Elements never cross a headline line (every section's limit is the next
   headline), so the text is parsed in chunks, one per headline plus the
   part before the first: an edit reparses only the chunks it touched, and
   the headline nesting, ends and depths are then fixed up over the list. */
#include <stdlib.h>
#include <string.h>
#include "org.h"
#include "uni.h"
#include "class.h"

/* The chunk being parsed: L bytes; EOB when it ends the text, so that $ and
   eobp hold at its end (a chunk that does not end the text ends where a
   headline starts). */
static const unsigned char *S;
static size_t L;
static int EOB;

static int inr(const unsigned (*r)[2], size_t n, unsigned c)
{
	size_t a = 0, b = n;
	while (a < b) {
		size_t m = a + (b - a) / 2;
		if (c < r[m][0]) b = m; else if (c > r[m][1]) a = m + 1; else return 1;
	}
	return 0;
}
#define IN(T, c) inr(T, sizeof T / sizeof *T, c)

/* ---- Emacs regexps ---- */

enum { O_SET, O_BYTE, O_SPLIT, O_JMP, O_SAVE, O_BOL, O_EOL, O_MATCH };
enum { C_WORD = 1, C_ALNUM = 2, C_SPACE = 4 };
#define INF 0xffffu
typedef struct {
	unsigned char op, lazy, cls, neg;
	unsigned short min, max;  /* O_SET repeats */
	int x, y;                 /* jumps, relative; O_SAVE slot; O_BYTE byte */
	unsigned char set[16];    /* ASCII members, negation and folding applied */
} Ins;

typedef struct { const char *p; Ins *v; int n, fold, ncap; } Comp;

static void add(Ins *i, int c, int fold)
{
	i->set[c >> 3] |= (unsigned char)(1u << (c & 7));
	if (fold && c >= 'A' && c <= 'Z') add(i, c + 32, 0);
	if (fold && c >= 'a' && c <= 'z') add(i, c - 32, 0);
}

static Ins *emit(Comp *c, int op)
{
	Ins *i = &c->v[c->n++];
	memset(i, 0, sizeof *i);
	i->op = (unsigned char)op; i->min = i->max = 1;
	return i;
}

static void insert(Comp *c, int at)
{
	memmove(&c->v[at + 1], &c->v[at], (size_t)(c->n - at) * sizeof *c->v);
	c->n++;
	memset(&c->v[at], 0, sizeof *c->v);
}

static void calt(Comp *c);

static void cclass(Comp *c, Ins *i)
{
	const char *p = c->p;
	if (*p == '^') { i->neg = 1; p++; }
	for (int first = 1; first || *p != ']'; first = 0) {
		if (p[0] == '[' && p[1] == ':') {
			const char *e = strchr(p + 2, ':');
			size_t k = (size_t)(e - p - 2);
			unsigned char b = !strncmp(p + 2, "word", k) ? C_WORD : !strncmp(p + 2, "space", k) ? C_SPACE
				: !strncmp(p + 2, "alnum", k) ? C_ALNUM : 0;  /* digit: ASCII only */
			i->cls |= b;
			for (int ch = 0; ch < 128; ch++)
				if (b ? (b == C_WORD ? IN(WORD, (unsigned)ch) : b == C_ALNUM ? IN(ALNUM, (unsigned)ch) : IN(SPACE, (unsigned)ch))
				      : ch >= '0' && ch <= '9') add(i, ch, 0);
			p = e + 2;
		} else if (p[1] == '-' && p[2] != ']') {
			for (int ch = (unsigned char)p[0]; ch <= (unsigned char)p[2]; ch++) add(i, ch, c->fold);
			p += 3;
		} else add(i, (unsigned char)*p++, c->fold);
	}
	c->p = p + 1;
	if (i->neg) for (int k = 0; k < 16; k++) i->set[k] = (unsigned char)~i->set[k];
}

static void catom(Comp *c)
{
	const char *p = c->p;
	if (p[0] == '\\' && p[1] == '(') {
		int n = -1;
		p += 2;
		if (p[0] == '?' && p[1] == ':') p += 2;
		else if (p[0] == '?') { n = (int)strtol(p + 1, (char **)&p, 10); p++; if (n >= c->ncap) c->ncap = n; }
		else n = ++c->ncap;
		c->p = p;
		if (n >= 0) emit(c, O_SAVE)->x = 2 * n;
		calt(c);
		c->p += 2;  /* \) */
		if (n >= 0) emit(c, O_SAVE)->x = 2 * n + 1;
		return;
	}
	if (*p == '[') { c->p = p + 1; cclass(c, emit(c, O_SET)); return; }
	c->p = p + 1;
	if (*p == '^') { emit(c, O_BOL); return; }
	if (*p == '$') { emit(c, O_EOL); return; }
	Ins *i = emit(c, O_SET);
	if (*p == '.') { add(i, '\n', 0); i->neg = 1; for (int k = 0; k < 16; k++) i->set[k] = (unsigned char)~i->set[k]; return; }
	if (*p == '\\') {
		c->p = p + 2;
		if (p[1] == 's' || p[1] == 'S') {
			c->p = p + 3;
			i->cls = C_SPACE; i->neg = p[1] == 'S';
			for (int ch = 0; ch < 128; ch++) if (IN(SPACE, (unsigned)ch) != i->neg) add(i, ch, 0);
			return;
		}
		p++;
	}
	if ((unsigned char)*p >= 0x80) { i->op = O_BYTE; i->x = (unsigned char)*p; return; }
	add(i, (unsigned char)*p, c->fold);
}

static void cpiece(Comp *c)
{
	int s = c->n;
	const char *a = c->p;
	catom(c);
	for (;;) {
		const char *p = c->p;
		unsigned lo, hi;
		if (*p == '*' || *p == '+' || *p == '?') { lo = *p == '+'; hi = *p == '?' ? 1 : INF; p++; }
		else if (p[0] == '\\' && p[1] == '{') {
			lo = (unsigned)strtoul(p + 2, (char **)&p, 10); hi = lo;
			if (*p == ',') { p++; hi = *p == '\\' ? INF : (unsigned)strtoul(p, (char **)&p, 10); }
			p += 2;  /* \} */
		} else return;
		int lazy = *p == '?' && p[-1] != '}';
		c->p = p + lazy;
		if (c->n == s + 1 && c->v[s].op == O_SET && c->v[s].min == 1 && c->v[s].max == 1) {
			c->v[s].min = (unsigned short)lo; c->v[s].max = (unsigned short)hi; c->v[s].lazy = (unsigned char)lazy;
			continue;
		}
		/* a group: X{m,n} is m copies, then X* or n-m X?; compiled again from its text */
		const char *after = c->p;
		for (unsigned k = 1; k < lo; k++) { s = c->n; c->p = a; catom(c); }
		if (lo == 0 || hi > lo) {
			if (lo) { s = c->n; c->p = a; catom(c); }
			int len = c->n - s;
			insert(c, s);
			if (hi == INF) {
				c->v[s].op = O_SPLIT;
				c->v[s].x = lazy ? len + 2 : 1; c->v[s].y = lazy ? 1 : len + 2;
				Ins *j = emit(c, O_JMP);
				j->x = s - (c->n - 1);
			} else {
				c->v[s].op = O_SPLIT;
				c->v[s].x = lazy ? len + 1 : 1; c->v[s].y = lazy ? 1 : len + 1;
			}
		}
		c->p = after;
		return;
	}
}

static void calt(Comp *c)
{
	int s = c->n;
	while (*c->p && !(c->p[0] == '\\' && (c->p[1] == '|' || c->p[1] == ')'))) cpiece(c);
	if (c->p[0] == '\\' && c->p[1] == '|') {
		c->p += 2;
		insert(c, s);
		int j = c->n;
		emit(c, O_JMP);
		c->v[s].op = O_SPLIT; c->v[s].x = 1; c->v[s].y = j + 1 - s;
		calt(c);
		c->v[j].x = c->n - j;
	}
}

static Ins *compile(const char *re, int fold, Ins *v)
{
	Comp c = { re, v, 0, fold, 0 };
	calt(&c);
	emit(&c, O_MATCH);
	return v;
}

/* Compiled once per pattern: the patterns are static strings, so their
   address is their name. */
static Ins *prog(const char *re, int fold)
{
	static struct { const char *re; int fold; Ins *v; } cache[128];
	static Ins pool[4096];
	static int used;
	size_t h = ((size_t)re >> 3 ^ (size_t)fold) % 128;
	while (cache[h].re && (cache[h].re != re || cache[h].fold != fold)) h = (h + 1) % 128;
	if (!cache[h].re) {
		Ins *v = compile(re, fold, pool + used);
		int k = 0;
		while (v[k].op != O_MATCH) k++;
		used += k + 1;
		cache[h].re = re; cache[h].fold = fold; cache[h].v = v;
	}
	return cache[h].v;
}

static size_t LIM, CAP[24];
static int DOLLAR;

static int one(const Ins *i, size_t q, size_t *len)
{
	if (q >= LIM) return 0;
	unsigned b = S[q];
	*len = 1;
	if (i->op == O_BYTE) return (int)b == i->x;
	if (b < 0x80) return i->set[b >> 3] >> (b & 7) & 1;
	*len = uni_len(S, LIM, q);
	unsigned cp = uni_cp(S, LIM, q);
	int in = ((i->cls & C_WORD) && IN(WORD, cp)) || ((i->cls & C_ALNUM) && IN(ALNUM, cp))
		|| ((i->cls & C_SPACE) && IN(SPACE, cp));
	return in != i->neg;
}

static int run(const Ins *i, size_t sp)
{
	for (;;) {
		size_t len, q, k;
		switch (i->op) {
		case O_SET: case O_BYTE:
			if (i->min == 1 && i->max == 1) {
				if (!one(i, sp, &len)) return 0;
				sp += len; i++; continue;
			}
			for (q = sp, k = 0; k < i->min; k++) { if (!one(i, q, &len)) return 0; q += len; }
			if (i->lazy) {
				for (;;) {
					if (run(i + 1, q)) return 1;
					if (k == i->max || !one(i, q, &len)) return 0;
					q += len; k++;
				}
			}
			while (k < i->max && one(i, q, &len)) { q += len; k++; }
			for (;;) {
				if (run(i + 1, q)) return 1;
				if (k == i->min) return 0;
				q = uni_prev(S, LIM, q); k--;
			}
		case O_SPLIT:
			if (run(i + i->x, sp)) return 1;
			i += i->y; continue;
		case O_JMP: i += i->x; continue;
		case O_SAVE: {
			size_t old = CAP[i->x];
			CAP[i->x] = sp;
			if (run(i + 1, sp)) return 1;
			CAP[i->x] = old;
			return 0;
		}
		case O_BOL: if (sp > 0 && S[sp - 1] != '\n') return 0; i++; continue;
		case O_EOL: if (sp < LIM ? S[sp] != '\n' : !DOLLAR) return 0; i++; continue;
		default: CAP[1] = sp; return 1;
		}
	}
}

#define NONE ((size_t)-1)
static int match(const Ins *p, size_t x, size_t lim, int dollar)
{
	LIM = lim; DOLLAR = dollar;
	for (int k = 0; k < 24; k++) CAP[k] = NONE;
	CAP[0] = x;
	if (!run(p, x)) return 0;
	CAP[0] = x;
	return 1;
}
/* looking-at, case folded (org-element binds case-fold-search to t) */
static int la(const char *re, size_t x) { return match(prog(re, 1), x, L, EOB); }
static int lacs(const char *re, size_t x) { return match(prog(re, 0), x, L, EOB); }
/* re-search-forward with a bound: the match starts at or after x and ends by lim */
static int rsearch(const Ins *p, size_t x, size_t lim)
{
	for (; x <= lim; x++) {
		if (p->op == O_BOL && x && S[x - 1] != '\n') {
			const unsigned char *e = memchr(S + x, '\n', lim - x);
			if (!e) return 0;
			x = (size_t)(e - S);
			continue;
		}
		if (match(p, x, lim, 1)) return 1;
	}
	return 0;
}
static int rs(const char *re, size_t x, size_t lim) { return x <= lim && rsearch(prog(re, 1), x, lim); }
static int rscs(const char *re, size_t x, size_t lim) { return x <= lim && rsearch(prog(re, 0), x, lim); }
/* A pattern built from the text (a block's name): compiled every time. */
static int rsdyn(const char *a, const char *name, size_t n, const char *b, int quote, size_t x, size_t lim)
{
	char re[512];
	Ins v[512];
	size_t k = strlen(a);
	if (k + 2 * n + strlen(b) + 1 > sizeof re) return 0;  /* no Org name is that long */
	memcpy(re, a, k);
	for (size_t i = 0; i < n; i++) {
		if (quote && strchr("[*.\\?+^$", name[i])) re[k++] = '\\';
		re[k++] = name[i];
	}
	strcpy(re + k, b);
	return x <= lim && rsearch(compile(re, 1, v), x, lim);
}

/* ---- line helpers (Emacs's motion functions) ---- */

static size_t eol(size_t x) { const unsigned char *e = memchr(S + x, '\n', L - x); return e ? (size_t)(e - S) : L; }
static size_t nextl(size_t x) { size_t e = eol(x); return e < L ? e + 1 : L; }  /* forward-line */
static size_t bol(size_t x) { while (x && S[x - 1] != '\n') x--; return x; }
static int ws(unsigned c) { return c == ' ' || c == '\r' || c == '\t' || c == '\n'; }
static size_t skipf(size_t x, size_t lim) { while (x < lim && ws(S[x])) x++; return x; }
static size_t skipb(size_t x, size_t lo) { while (x > lo && ws(S[x - 1])) x--; return x; }
/* (skip-chars-forward " \r\t\n" limit) (if (eobp) (point) (line-beginning-position)) */
static size_t after(size_t x, size_t lim)
{
	x = skipf(x, lim);
	return x == L && EOB ? x : bol(x);
}
static int blank(size_t x) { return la("[ \t]*$", x); }

/* ---- the node list ---- */

struct Org { OrgNode *v; size_t n, cap; char *buf; size_t bufcap; };
static Org *O;      /* the list being written */
static size_t B;    /* offset of S in the text */
static int oom;

static OrgNode *node(void)
{
	static OrgNode dummy;
	if (O->n == O->cap) {
		size_t c = O->cap ? 2 * O->cap : 256;
		OrgNode *v = realloc(O->v, c * sizeof *v);
		if (!v) { oom = 1; return &dummy; }
		O->v = v; O->cap = c;
	}
	return &O->v[O->n++];
}

static const char ITEM_RE[] = "\\([ \t]*\\([-+]\\|\\(\\([0-9]+\\)[.)]\\)\\)\\|[ \t]+\\*\\)\\([ \t]+\\|$\\)";
static const char FULL_ITEM_RE[] = "^[ \t]*\\(\\(?:[-+*]\\|\\(?:[0-9]+\\|[A-Za-z]\\)[.)]\\)\\(?:[ \t]+\\|$\\)\\)\\(?:\\[@\\(?:start:\\)?\\([0-9]+\\|[A-Za-z]\\)\\][ \t]*\\)?\\(?:\\(\\[[ X-]\\]\\)\\(?:[ \t]+\\|$\\)\\)?\\(?:\\(.*\\)[ \t]+::\\(?:[ \t]+\\|$\\)\\)?";
static const char DRAWER_RE[] = "^[\t ]*:\\([_[:word:]-]+\\):[\t ]*$";
static const char END_RE[] = "^[ \t]*:END:[ \t]*$";
static const char AFFILIATED_RE[] = "[ \t]*#\\+\\(?:\\(?1:\\(?:CAPTION\\|RESULTS\\)\\)\\(?:\\[\\(.*\\)\\]\\)?\\|\\(?1:\\(?:DATA\\|HEADERS?\\|LABEL\\|NAME\\|PLOT\\|RES\\(?:NAME\\|ULT\\)\\|\\(?:S\\(?:OURC\\|RCNAM\\)\\|TBLNAM\\)E\\)\\)\\|\\(?1:ATTR_[-_A-Za-z0-9]+\\)\\):[ \t]*";
static const char COMMENT_RE[] = "^[\t ]*#\\(?: \\|$\\)";
static const char CLOCK_RE[] = "\\(?:^[\t ]*CLOCK:\\(?:[\t ]+\\(?:\\[\\([[:digit:]]\\{4\\}-[[:digit:]]\\{2\\}-[[:digit:]]\\{2\\}\\(?: .*?\\)?\\)\\]\\)\\(?:--\\(?:\\[\\([[:digit:]]\\{4\\}-[[:digit:]]\\{2\\}-[[:digit:]]\\{2\\}\\(?: .*?\\)?\\)\\]\\)[\t ]+=>[\t ]+[[:digit:]]+:[[:digit:]][[:digit:]]\\)?\\|[\t ]+=>[\t ]+[[:digit:]]+:[[:digit:]][[:digit:]]\\)[\t ]*$\\)";
static const char LATEX_RE[] = "^[ \t]*\\\\begin{\\([A-Za-z0-9*]+\\)}";

static const char SEPARATE_RE[] = "^\\(?:\\*+ \\|\\[fn:[-_[:word:]]+\\]\\|%%(\\|[ \t]*\\(?:$\\||\\|\\+\\(?:-+\\+\\)+[ \t]*$\\|#\\(?: \\|$\\|\\+\\(?:BEGIN_\\S-+\\|\\S-+\\(?:\\[.*\\]\\)?:[ \t]*\\)\\)\\|:\\(?: \\|$\\|[-_[:word:]]+:[ \t]*$\\)\\|-\\{5,\\}[ \t]*$\\|\\\\begin{\\([A-Za-z0-9*]+\\)}\\|\\(?:^[\t ]*CLOCK:\\(?:[\t ]+\\(?:\\[\\([[:digit:]]\\{4\\}-[[:digit:]]\\{2\\}-[[:digit:]]\\{2\\}\\(?: .*?\\)?\\)\\]\\)\\(?:--\\(?:\\[\\([[:digit:]]\\{4\\}-[[:digit:]]\\{2\\}-[[:digit:]]\\{2\\}\\(?: .*?\\)?\\)\\]\\)[\t ]+=>[\t ]+[[:digit:]]+:[[:digit:]][[:digit:]]\\)?\\|[\t ]+=>[\t ]+[[:digit:]]+:[[:digit:]][[:digit:]]\\)[\t ]*$\\)\\|\\(?:[-+*]\\|\\(?:[0-9]+\\)[.)]\\)\\(?:[ \t]\\|$\\)\\)\\)";
static const char CURRENT_RE[] = "\\(?:\\(?1:^[ \t]*\\\\begin{[A-Za-z0-9*]+}\\)\\|\\(?2:^[\t ]*:[_[:word:]-]+:[\t ]*$\\)\\|\\(?3:[ \t]*:\\( \\|$\\)\\)\\|\\(?7:^[\t ]*#\\+BEGIN:[\t ]*[^[:space:]]\\)\\|\\(?4:[ \t]*#\\+\\)\\(?:BEGIN_\\(?5:[^[:space:]]+\\)\\|\\(?6:CALL:\\)\\|\\(?8:[^[:space:]]+:\\)\\)\\|\\(?9:^\\[fn:\\([-_[:word:]]+\\)\\]\\)\\|\\(?10:[ \t]*-----+[ \t]*$\\)\\|\\(?11:%%(\\)\\)";
static const char PROPDRAWER_RE[] = "^[ \t]*:PROPERTIES:[ \t]*\n\\(?:[ \t]*:\\S-+:\\(?:[ \t].*\\)?[ \t]*\n\\)*?[ \t]*:END:[ \t]*$";
static const char PROPERTY_RE[] = "^\\(?4:[ \t]*\\)\\(?1::\\(?2:\\S-+\\):\\)\\(?:\\(?3:$\\)\\|[ \t]+\\(?3:.*?\\)\\)\\(?5:[ \t]*\\)$";
static const char PLANNING_LINE_RE[] = "\\(?:^[\t ]*\\(\\(?:\\(?:CLOSED\\|DEADLINE\\|SCHEDULED\\):\\)\\)\\)";

static const char FOOTNOTE_SEP_RE[] = "^\\*+ \\|^\\[fn:\\([-_[:word:]]+\\)\\]\\|^\\([ \t]*\n\\)\\{2,\\}";
static const char TABLEEL_RULE_RE[] = "[ \t]*\\+\\(?:-+\\+\\)+[ \t]*$";

#define G(n) (CAP[2 * (n)] != NONE)

/* The list structure (org-element--list-struct): every item of the list,
   nested ones too, in order, with where each ends.  An item is found in
   order, so LS[s..LN) stays sorted. */
typedef struct { size_t pos, end; int ind, desc; } Item;
static Item *LS;
static size_t LN, LC, *OPEN, OC;

/* org-current-text-column: string-width of the indentation, a tab counting 8
   wherever it is (not current-column's tab stops) */
static int indent(size_t x)
{
	int c = 0;
	for (; x < L && (S[x] == ' ' || S[x] == '\t'); x++) c += S[x] == '\t' ? 8 : 1;
	return c;
}

static size_t list_struct(size_t x, size_t lim)
{
	size_t s = LN, on = 0;
	for (;;) {
		if (x >= lim) {
			size_t end = nextl(skipb(x, 0));
			while (on) LS[OPEN[--on]].end = end;
			break;
		}
		if (la("^[ \t]*\n[ \t]*\n", x)) { while (on) LS[OPEN[--on]].end = x; break; }
		if (la(ITEM_RE, x)) {
			int ind = indent(x);
			while (on && ind <= LS[OPEN[on - 1]].ind) LS[OPEN[--on]].end = x;
			la(FULL_ITEM_RE, x);
			int desc = G(4) && strchr("-+*", S[CAP[2]]);
			if (LN == LC || on == OC) {
				size_t c = 2 * LC + 64;
				Item *v = realloc(LS, c * sizeof *v);
				size_t *w = v ? realloc(OPEN, c * sizeof *w) : NULL;
				if (v) LS = v;
				if (!w) { oom = 1; break; }
				OPEN = w; LC = OC = c;
			}
			LS[LN] = (Item){ x, x, ind, desc };
			OPEN[on++] = LN++;
			x = nextl(x);
		} else if (blank(x)) x = nextl(x);
		else {
			int ind = indent(x);
			size_t end = nextl(skipb(x, 0));
			while (ind <= LS[OPEN[on - 1]].ind) { LS[OPEN[--on]].end = end; if (!on) return s; }
			if (la("[ \t]*#\\+BEGIN\\(:\\|_\\S-+\\)", x)
			    && rsdyn("^[ \t]*#\\+END", (const char *)S + CAP[2], CAP[3] - CAP[2], "[ \t]*$", 0, x, lim)) x = CAP[1];
			else if (la(DRAWER_RE, x) && rs(END_RE, x, lim)) x = CAP[1];
			x = nextl(x);
		}
	}
	return s;
}

static const Item *assq(size_t pos, size_t s, size_t e)
{
	while (s < e) {
		size_t m = s + (e - s) / 2;
		if (LS[m].pos < pos) s = m + 1; else if (LS[m].pos > pos) e = m; else return &LS[m];
	}
	return NULL;
}

/* ---- elements ---- */

enum { M_NONE, M_FIRST, M_SECTION, M_PLANNING, M_PROPS, M_TOPCOMMENT, M_ITEM, M_NODEPROP, M_ROW };

typedef struct { size_t begin, post, cbeg, cend, end, ss, se; int type, has; } E;

static void set(E *e, int type, size_t begin, size_t post, size_t end)
{
	e->type = type; e->begin = begin; e->post = post; e->end = end;
}
static void contents(E *e, size_t cbeg, size_t cend) { e->has = 1; e->cbeg = cbeg; e->cend = cend; }

static void paragraph(size_t begin, size_t x, size_t lim, E *e)
{
	size_t p = eol(x);
	for (;;) {
		if (!rs(SEPARATE_RE, p, lim)) { p = lim; break; }
		p = bol(CAP[1]);
		int stop = 1;
		if (la(DRAWER_RE, p)) stop = rs(END_RE, nextl(p), lim);
		else if (la("[ \t]*#\\+BEGIN_\\(\\S-+\\)", p))
			stop = rsdyn("^[ \t]*#\\+END_", (const char *)S + CAP[2], CAP[3] - CAP[2], "[ \t]*$", 1, p, lim);
		else if (la(LATEX_RE, p))
			stop = rsdyn("\\\\end{", (const char *)S + CAP[2], CAP[3] - CAP[2], "}[ \t]*$", 1, p, lim);
		else if (la("[ \t]*#\\+\\(\\S-+\\)\\[.*\\]:", p)) {
			size_t k = CAP[3] - CAP[2];
			stop = (k == 7 && !strncasecmp((const char *)S + CAP[2], "CAPTION", 7))
			    || (k == 7 && !strncasecmp((const char *)S + CAP[2], "RESULTS", 7));
		}
		if (stop) break;
		p = eol(p);
	}
	size_t before = p == lim ? lim : bol(p);
	set(e, ORG_PARAGRAPH, begin, x, after(before, lim));
	contents(e, x, nextl(skipb(before, x)));
}

/* center, quote, special, dynamic, verse and the blocks holding a value */
static int block(int type, size_t begin, size_t y, size_t lim, E *e)
{
	switch (type) {
	case ORG_CENTER_BLOCK: if (!rs("^[ \t]*#\\+END_CENTER[ \t]*$", y, lim)) return 0; break;
	case ORG_QUOTE_BLOCK: if (!rs("^[ \t]*#\\+END_QUOTE[ \t]*$", y, lim)) return 0; break;
	case ORG_VERSE_BLOCK: if (!rs("^[ \t]*#\\+END_VERSE[ \t]*$", y, lim)) return 0; break;
	case ORG_COMMENT_BLOCK: if (!rs("^[ \t]*#\\+END_COMMENT[ \t]*$", y, lim)) return 0; break;
	case ORG_EXAMPLE_BLOCK: if (!rs("^[ \t]*#\\+END_EXAMPLE[ \t]*$", y, lim)) return 0; break;
	case ORG_EXPORT_BLOCK: if (!rs("^[ \t]*#\\+END_EXPORT[ \t]*$", y, lim)) return 0; break;
	case ORG_SRC_BLOCK: if (!rs("^[ \t]*#\\+END_SRC[ \t]*$", y, lim)) return 0; break;
	case ORG_DYNAMIC_BLOCK: if (!rs("^[ \t]*#\\+END:?[ \t]*$", y, lim)) return 0; break;
	default:
		la("[ \t]*#\\+BEGIN_\\(\\S-+\\)[ \t]*\\(.*\\)[ \t]*$", y);
		if (!rsdyn("^[ \t]*#\\+END_", (const char *)S + CAP[2], CAP[3] - CAP[2], "[ \t]*$", 1, y, lim)) return 0;
	}
	size_t endl = CAP[0], c = nextl(y);
	set(e, type, begin, y, after(nextl(endl), lim));
	if (type == ORG_VERSE_BLOCK) contents(e, c, endl);
	else if ((type == ORG_CENTER_BLOCK || type == ORG_QUOTE_BLOCK || type == ORG_SPECIAL_BLOCK
	          || type == ORG_DYNAMIC_BLOCK) && c < endl) contents(e, c, endl);
	return 1;
}

/* clock and planning: blank lines after them count, the indentation of the line after does not */
static size_t clockend(size_t x, size_t lim)
{
	size_t y = skipf(nextl(x), lim), z = y;
	while (z && (S[z - 1] == ' ' || S[z - 1] == '\t')) z--;
	return z && S[z - 1] != '\n' ? y : z;
}

static int tableel(size_t y, size_t lim)
{
	size_t next = nextl(y);
	if (!la(TABLEEL_RULE_RE, y) || next >= lim) return 0;
	if (!rs("^[ \t]*\\(?:$\\|[^+| \t]\\)", eol(y), lim)) {
		size_t p = lim;
		p = p == bol(p) ? bol(p ? p - 1 : 0) : bol(p);
		return la(TABLEEL_RULE_RE, p);
	}
	size_t p = CAP[1];
	if (bol(p) == next) return 0;
	return la(TABLEEL_RULE_RE, bol(p) ? bol(bol(p) - 1) : 0);
}

static void item(size_t x, size_t lim, E *e)
{
	size_t begin = bol(x);
	la(FULL_ITEM_RE, begin);
	size_t m0 = CAP[1], g4 = CAP[8];
	int ordered = memchr(S + CAP[2], '.', CAP[3] - CAP[2]) || memchr(S + CAP[2], ')', CAP[3] - CAP[2]);
	const Item *it = assq(begin, e->ss, e->se);
	size_t end = it ? it->end : nextl(begin);
	end = end == bol(end) ? end : nextl(end);
	if (end > lim) end = lim;
	size_t c = skipf(g4 != NONE && ordered ? g4 : m0, end);
	set(e, ORG_ITEM, begin, begin, end);
	if (c != end) contents(e, bol(c) == begin ? c : bol(c), nextl(skipb(end, 0)));
}

static void plain_list(size_t begin, size_t y, size_t lim, E *e)
{
	if (!e->se || !assq(y, e->ss, e->se)) { e->ss = list_struct(y, lim); e->se = LN; }
	const Item *it = assq(y, e->ss, e->se);
	size_t pos = it ? it->end : nextl(y);
	for (int ind = it ? it->ind : 0; it && (it = assq(pos, e->ss, e->se)) && it->ind == ind;) pos = it->end;
	pos = skipb(pos, 0);
	size_t cend = pos == bol(pos) ? pos : nextl(pos), z = skipf(cend, lim);
	set(e, ORG_PLAIN_LIST, begin, y, z == lim ? lim : bol(z));
	contents(e, y, cend);
}

static void footnote(size_t begin, size_t y, size_t lim, E *e)
{
	size_t end;
	if (!rs(FOOTNOTE_SEP_RE, eol(y), lim)) end = lim;
	else if (S[CAP[0]] == '[') {
		size_t p = bol(CAP[1]);
		p = p ? bol(p - 1) : 0;
		while (p > y && la(AFFILIATED_RE, p)) p = p ? bol(p - 1) : 0;
		end = nextl(p);
	} else if (S[CAP[0]] == '*') end = CAP[0];
	else { size_t p = skipf(CAP[1], lim); end = p == lim ? lim : bol(p); }
	const unsigned char *r = memchr(S + y, ']', L - y);
	size_t c = skipf(r ? (size_t)(r - S) + 1 : L, end);
	set(e, ORG_FOOTNOTE_DEFINITION, begin, y, end);
	if (c != end) contents(e, bol(c) == y ? c : bol(c), nextl(skipb(end, 0)));
}

static void element(size_t x, size_t lim, int mode, E *e)
{
	if (mode == M_ITEM) { item(x, lim, e); return; }
	if (mode == M_ROW) {
		set(e, ORG_TABLE_ROW, x, x, nextl(x));
		if (!la("^[ \t]*|-", x)) {
			size_t c = (size_t)((const unsigned char *)memchr(S + x, '|', L - x) - S) + 1, z = eol(x);
			while (z > c && (S[z - 1] == ' ' || S[z - 1] == '\t')) z--;
			contents(e, c, z);
		}
		return;
	}
	if (mode == M_NODEPROP) {
		la(PROPERTY_RE, x);
		set(e, ORG_NODE_PROPERTY, x, x, CAP[1] < L ? CAP[1] + 1 : L);
		return;
	}
	if (la(COMMENT_RE, x)) {
		size_t z = nextl(x);
		while (z < lim && la(COMMENT_RE, z)) z = nextl(z);
		set(e, ORG_COMMENT, x, x, after(z, lim));
		return;
	}
	int star = x && S[bol(x - 1)] == '*';
	if (mode == M_PLANNING && star && la(PLANNING_LINE_RE, x)) { set(e, ORG_PLANNING, x, x, clockend(x, lim)); return; }
	if (((mode == M_PLANNING && star) || mode == M_PROPS || mode == M_TOPCOMMENT) && la(PROPDRAWER_RE, x)) {
		size_t c = nextl(x);
		rs(END_RE, x, lim);
		set(e, ORG_PROPERTY_DRAWER, x, x, after(nextl(CAP[1]), lim));
		if (CAP[0] > c) contents(e, c, CAP[0]);
		return;
	}
	if (x != bol(x)) { paragraph(x, x, lim, e); return; }
	if (la(CLOCK_RE, x)) { set(e, ORG_CLOCK, x, x, clockend(x, lim)); return; }
	/* affiliated keywords (org-element--collect-affiliated-keywords) */
	size_t y = x;
	while (y < lim && la(AFFILIATED_RE, y)) y = nextl(y);
	if (y != x && (blank(y) || la(COMMENT_RE, y) || la("^[ \t]*CLOCK:", y) || la("^\\*+ ", y))) y = x;
	if (y != x && y >= lim) { y = x; goto keyword; }
	if (la(CURRENT_RE, y)) {
		if (G(1)) {
			la(LATEX_RE, y);
			if (!rsdyn("\\\\end{", (const char *)S + CAP[2], CAP[3] - CAP[2], "}[ \t]*$", 1, y, lim)) goto par;
			set(e, ORG_LATEX_ENVIRONMENT, x, y, after(nextl(CAP[1]), lim));
		} else if (G(2)) {
			if (!rs(END_RE, eol(y) < lim ? eol(y) : lim, lim)) goto par;
			size_t endl = CAP[0], c = bol(skipf(nextl(y), L));
			set(e, ORG_DRAWER, x, y, after(nextl(endl), lim));
			if (c < endl) contents(e, c, endl);
		} else if (G(3)) {
			size_t z = y;
			while (z < lim && la("[ \t]*:\\( \\|$\\)", z)) z = nextl(z);
			set(e, ORG_FIXED_WIDTH, x, y, after(z, lim));
		} else if (G(5)) {
			static const struct { const char *name; int type; } B[] = {
				{ "CENTER", ORG_CENTER_BLOCK }, { "COMMENT", ORG_COMMENT_BLOCK }, { "EXAMPLE", ORG_EXAMPLE_BLOCK },
				{ "EXPORT", ORG_EXPORT_BLOCK }, { "QUOTE", ORG_QUOTE_BLOCK }, { "SRC", ORG_SRC_BLOCK },
				{ "VERSE", ORG_VERSE_BLOCK } };
			int type = ORG_SPECIAL_BLOCK;
			for (size_t i = 0; i < sizeof B / sizeof *B; i++)
				if (CAP[11] - CAP[10] == strlen(B[i].name) && !strncasecmp((const char *)S + CAP[10], B[i].name, strlen(B[i].name)))
					type = B[i].type;
			if (!block(type, x, y, lim, e)) goto par;
		} else if (G(6)) set(e, ORG_BABEL_CALL, x, y, after(nextl(y), lim));
		else if (G(7)) { if (!block(ORG_DYNAMIC_BLOCK, x, y, lim, e)) goto par; }
		else if (G(8)) {
		keyword:
			set(e, ORG_KEYWORD, x, y, after(nextl(y), lim));
		} else if (G(9)) footnote(x, y, lim, e);
		else if (G(10)) set(e, ORG_HORIZONTAL_RULE, x, y, after(nextl(y), lim));
		else if (G(11)) set(e, ORG_DIARY_SEXP, x, y, after(nextl(y), lim));
		else goto par;
		return;
	}
	if (la("[ \t]*|", y) || tableel(y, lim)) {
		int org = la("[ \t]*|", y);
		size_t te = rs(org ? "^[ \t]*\\($\\|[^| \t]\\)" : "^[ \t]*\\($\\|[^| \t+]\\)", y, lim) ? CAP[0] : lim, z = te;
		while (la("[ \t]*#\\+TBLFM: +\\(.*\\)[ \t]*$", z) && z < L) z = nextl(z);
		set(e, ORG_TABLE, x, y, after(z, lim));
		if (org) contents(e, y, te);
		return;
	}
	if (la(ITEM_RE, y)) { plain_list(x, y, lim, e); return; }
par:
	paragraph(x, y, lim, e);
}

static int greater(int t)
{
	switch (t) {
	case ORG_CENTER_BLOCK: case ORG_DRAWER: case ORG_DYNAMIC_BLOCK: case ORG_FOOTNOTE_DEFINITION:
	case ORG_ITEM: case ORG_PLAIN_LIST: case ORG_PROPERTY_DRAWER: case ORG_QUOTE_BLOCK:
	case ORG_SPECIAL_BLOCK: case ORG_TABLE: return 1;
	}
	return 0;
}

/* org-element--next-mode */
static int inside(int mode, int t)
{
	switch (t) {
	case ORG_SECTION: return mode == M_FIRST ? M_TOPCOMMENT : M_PLANNING;
	case ORG_PLAIN_LIST: return M_ITEM;
	case ORG_PROPERTY_DRAWER: return M_NODEPROP;
	case ORG_TABLE: return M_ROW;
	}
	return M_NONE;
}
static int next(int mode, int t)
{
	if (mode == M_ITEM || mode == M_NODEPROP || mode == M_ROW) return mode;
	if ((mode == M_PLANNING && t == ORG_PLANNING) || (mode == M_TOPCOMMENT && t == ORG_COMMENT)) return M_PROPS;
	return M_NONE;
}

static OrgNode *emitn(int type, size_t begin, size_t post, size_t end, unsigned level)
{
	OrgNode *n = node();
	*n = (OrgNode){ B + begin, B + end, B + post, 0, 0, (unsigned char)type, 0, level };
	return n;
}

/* org-element--parse-elements */
static void parse(size_t x, size_t end, int mode, size_t ss, size_t se, unsigned level)
{
	while (x < end && !oom) {
		E e = { 0 };
		size_t keep = LN;  /* a list's structure lives while its items are parsed */
		e.ss = ss; e.se = se;
		element(x, end, mode, &e);
		if (e.end <= x) e.end = nextl(x);  /* never seen; Emacs would loop for ever */
		OrgNode *n = emitn(e.type, e.begin, e.post, e.end, level);
		if (e.has) { n->cbeg = B + e.cbeg; n->cend = B + e.cend; }
		x = e.end;
		if (e.has && greater(e.type)) parse(e.cbeg, e.cend, inside(mode, e.type), e.ss, e.se, level + 1);
		if (!se) LN = keep;
		mode = next(mode, e.type);
	}
}

static int heading(size_t x) { size_t y = x; while (y < L && S[y] == '*') y++; return y > x && y < L && S[y] == ' '; }

/* S[0..L) holds whole chunks: from a headline line, or from the text's start
   when first. */
static void chunks(int first)
{
	size_t x = 0;
	LN = 0;
	if (first) {
		x = bol(skipf(0, L));
		if (x < L && !heading(x)) {
			size_t h = x;
			while (h < L && !heading(h)) h = nextl(h);
			emitn(ORG_SECTION, x, x, h, 0);
			O->v[O->n - 1].cbeg = B + x; O->v[O->n - 1].cend = B + h;
			parse(x, h, M_TOPCOMMENT, 0, 0, 1);
			x = h;
		}
	}
	while (x < L && !oom) {
		size_t h = nextl(x), stars = 0;
		while (S[x + stars] == '*') stars++;
		emitn(ORG_HEADLINE, x, x, x, (unsigned)stars);
		while (h < L && !heading(h)) h = nextl(h);
		size_t c = skipf(nextl(x), L);
		if (c < h) {
			c = bol(c);
			OrgNode *n = emitn(ORG_SECTION, c, c, h, 1);
			n->cbeg = B + c; n->cend = B + h;
			parse(c, h, M_PLANNING, 0, 0, 2);
		}
		x = h;
	}
}

/* ---- the list: headline ends, contents and depths (org-element-headline-parser) ---- */

static void fixup(Org *o, size_t len)
{
	size_t *open = (size_t *)o->buf, on = 0;  /* headline indices; o->buf is free between parses */
	unsigned base = 0;
	for (size_t i = 0; i <= o->n; i++) {
		OrgNode *n = i < o->n ? &o->v[i] : NULL;
		if (n && n->type != ORG_HEADLINE) { n->depth = base + n->level; continue; }
		while (on && (!n || o->v[open[on - 1]].level >= n->level)) {
			OrgNode *h = &o->v[open[--on]];
			h->end = n ? n->begin : len;
		}
		if (!n) break;
		n->depth = (unsigned)on;
		base = (unsigned)on;
		open[on++] = i;
	}
	for (size_t i = 0; i < o->n; i++) {
		OrgNode *h = &o->v[i];
		if (h->type != ORG_HEADLINE) continue;
		OrgNode *k = i + 1 < o->n ? &o->v[i + 1] : NULL;
		h->cbeg = h->cend = 0;
		if (k && k->type == ORG_SECTION && k->depth > h->depth) h->cbeg = k->begin;
		else {
			for (k = h + 1; k < o->v + o->n && k->type != ORG_HEADLINE; k++) ;
			if (k < o->v + o->n && k->begin < h->end) h->cbeg = k->begin;
		}
		if (h->cbeg) h->cend = h->end;
	}
}

static int grow(Org *o, size_t n)
{
	if (n <= o->bufcap) return 0;
	char *b = realloc(o->buf, n);
	if (!b) return -1;
	o->buf = b; o->bufcap = n;
	return 0;
}

/* Parses t[a, z), whole chunks, into p's list. */
static int reparse(Org *p, const Text *t, size_t a, size_t z)
{
	p->n = 0;
	if (grow(p, z - a + 1)) return -1;
	text_read(t, a, p->buf, z - a);
	S = (const unsigned char *)p->buf; L = z - a; EOB = z == text_len(t); B = a; O = p; oom = 0;
	chunks(a == 0);
	return oom ? -1 : 0;
}

Org *org_new(const Text *t)
{
	Org *o = calloc(1, sizeof *o);
	if (o && org_edit(o, t, 0, 0, text_len(t))) { org_free(o); return NULL; }
	return o;
}

void org_free(Org *o)
{
	if (!o) return;
	free(o->v); free(o->buf); free(o);
}

static int starts_headline(const Text *t, size_t a)
{
	char c = 0;
	size_t k = 0;
	while (text_read(t, a + k, &c, 1) == 1 && c == '*') k++;
	return k && c == ' ';
}

int org_edit(Org *o, const Text *t, size_t pos, size_t ndel, size_t nins)
{
	static Org part;
	size_t len = text_len(t), i0 = 0, i1 = o->n, a = 0, z = len, pa = 0, pi = 0;
	/* the chunks holding pos - 1 through pos + ndel; the one before too if
	   the first one's headline line lost its stars */
	for (size_t i = 0; i < o->n; i++) {
		const OrgNode *n = &o->v[i];
		if (n->type != ORG_HEADLINE) continue;
		if (n->begin < (pos ? pos : 1)) { pa = a; pi = i0; i0 = i; a = n->begin; }
		else if (n->begin > pos + ndel) { i1 = i; z = n->begin - ndel + nins; break; }
	}
	if (a && !starts_headline(t, a)) { a = pa; i0 = pi; }
	if (!a) i0 = 0;
	size_t k, need;
	if (reparse(&part, t, a, z)) { o->n = 0; return -1; }
	k = part.n;
	need = o->n - (i1 - i0) + k;
	if (need > o->cap) {
		OrgNode *v = realloc(o->v, need * sizeof *v);
		if (!v) { o->n = 0; return -1; }
		o->v = v; o->cap = need;
	}
	for (size_t i = i1; i < o->n; i++) {
		OrgNode *n = &o->v[i];
		n->begin += nins - ndel; n->end += nins - ndel; n->post += nins - ndel;
		if (n->cend) { n->cbeg += nins - ndel; n->cend += nins - ndel; }
	}
	if (o->n > i1) memmove(o->v + i0 + k, o->v + i1, (o->n - i1) * sizeof *o->v);
	if (k) memcpy(o->v + i0, part.v, k * sizeof *o->v);
	o->n = need;
	if (grow(o, (need + 1) * sizeof(size_t))) { o->n = 0; return -1; }
	fixup(o, len);
	return 0;
}

size_t org_count(const Org *o) { return o->n; }
const OrgNode *org_nodes(const Org *o) { return o->v; }

/* ---- properties, read when asked (org-element defers them too) ---- */

/* Puts t[a, z) in the parse buffer; *o is a scratch list for its buffer. */
static Org scratch;
static void load(const Text *t, size_t a, size_t z)
{
	if (grow(&scratch, z - a + 1)) z = a;
	L = text_read(t, a, scratch.buf, z - a);
	S = (const unsigned char *)scratch.buf; EOB = a + L == text_len(t); B = a;
}
static size_t line_end(const Text *t, size_t x)
{
	size_t l = text_line_of(t, x);
	return l < text_lines(t) ? text_line_start(t, l + 1) : text_len(t);
}

void org_headline(const Text *t, const OrgNode *h, OrgHeadline *out)
{
	load(t, h->begin, line_end(t, h->begin));
	size_t x = 0, e = eol(0);
	memset(out, 0, sizeof *out);
	while (x < L && S[x] == '*') x++;
	while (x < L && (S[x] == ' ' || S[x] == '\t')) x++;
	out->todo = out->todo_end = B + x;
	out->priority = -1;
	if (lacs("\\(CANCELLED\\|DONE\\|NEXT\\|TODO\\|WAIT\\)\\(?: \\|$\\)", x)) {
		out->todo_end = B + CAP[3];
		out->done = S[x] == 'D' || S[x] == 'C';
		x = CAP[1];
		while (x < L && (S[x] == ' ' || S[x] == '\t')) x++;
	}
	if (la(".*?\\(\\[#\\([A-Z]\\|[0-9]\\|[1-5][0-9]\\|6[0-4]\\)\\] ?\\)", x)) {
		out->priority = S[CAP[4]] >= '0' && S[CAP[4]] <= '9' ? atoi((const char *)S + CAP[4]) : S[CAP[4]];
		x = CAP[1];
	}
	if (lacs("COMMENT\\(?: \\|$\\)", x)) {
		out->commented = 1;
		x = CAP[1];
		while (x < L && (S[x] == ' ' || S[x] == '\t')) x++;
	}
	size_t y = x;
	while (y && (S[y - 1] == ' ' || S[y - 1] == '\t')) y--;
	size_t te = e;
	out->tags = out->tags_end = B + e;
	if (rs("[ \t]+\\(:\\([[:alnum:]_@#%:]+\\):\\)[ \t]*$", y, e)) {
		te = CAP[0];
		out->tags = B + CAP[2]; out->tags_end = B + CAP[3];
	}
	out->title = B + (x < te ? x : te); out->title_end = B + (x < te ? te : x);
}

static int num(size_t a, size_t b) { int v = 0; for (; a < b; a++) v = v * 10 + S[a] - '0'; return v; }
static char unit(size_t a) { return strchr("hdwm", S[a] | 32) && S[a] >= 'a' ? (char)S[a] : 'y'; }

/* org-parse-time-string on S[a, z), with NODEFAULT */
static void date(size_t a, size_t z, int *y, int *m, int *d, int *h, int *mi)
{
	static const char TS0[] = "\\(\\([0-9]\\{4\\}\\)-\\([0-9]\\{2\\}\\)-\\([0-9]\\{2\\}\\)\\( +[^]+0-9>\r\n -]+\\)?\\( +\\([0-9]\\{1,2\\}\\):\\([0-9]\\{2\\}\\)\\)?\\)";
	if (!rs(TS0, a, z)) return;
	*y = num(CAP[4], CAP[5]); *m = num(CAP[6], CAP[7]); *d = num(CAP[8], CAP[9]);
	if (G(7)) { *h = num(CAP[14], CAP[15]); *mi = num(CAP[16], CAP[17]); }
}

/* org-element-timestamp-parser at S[x] */
/* fold: case-fold-search, which org-element-planning-parser binds to nil
   around it and the clock and object parsers leave at t */
static int stamp(size_t x, OrgTimestamp *o, int fold)
{
	static const char TS[] = "[[<]\\([[:digit:]]\\{4\\}-[[:digit:]]\\{2\\}-[[:digit:]]\\{2\\}\\(?: .*?\\)?\\)[]>]\\|\\(?:<%%\\(?:([^>\n]+)\\)\\([^\n>]*\\)>\\)";
	static const char RAW[] = "\\([<[]\\(%%\\)?.*?\\)[]>]\\(?:--\\([[<]\\([[:digit:]]\\{4\\}-[[:digit:]]\\{2\\}-[[:digit:]]\\{2\\}\\(?: .*?\\)?\\)[]>]\\)\\)?";
	memset(o, 0, sizeof *o);
	o->y = o->m = o->d = o->h = o->min = o->y2 = o->m2 = o->d2 = o->h2 = o->min2 = -1;
	if (!la(TS, x)) return 0;
	int active = S[x] == '<';
	la(RAW, x);
	size_t raw = CAP[1], ds = CAP[2], de = CAP[3], diary = G(2), e2 = CAP[6], e2end = CAP[7];
	int hasend = G(3);
	if (diary) { la(TS, x); ds = CAP[4]; de = CAP[5]; }
	size_t end = raw;
	while (end < L && (S[end] == ' ' || S[end] == '\t')) end++;
	o->begin = B + x; o->end = B + end;
	int tr = 0, th = -1, tm = -1;
	if (rs("[012]?[0-9]:[0-5][0-9]\\(-\\([012]?[0-9]\\):\\([0-5][0-9]\\)\\)", ds, de)) {
		tr = 1; th = num(CAP[4], CAP[5]); tm = num(CAP[6], CAP[7]);
	}
	o->type = diary ? 'd' : active ? (hasend || tr ? 'A' : 'a') : (hasend || tr ? 'I' : 'i');
	o->range = hasend ? 'd' : tr ? 't' : 0;
	if (!diary) {
		static const char REP[] = "\\(?1:\\(?:[+.]?\\+\\)\\)\\(?2:[[:digit:]]+\\)\\(?3:[dhmwy]\\)\\(?:/\\(?4:[[:digit:]]+\\)\\(?5:[dhmwy]\\)\\)?";
		static const char WARN[] = "\\(-\\)?-\\([0-9]+\\)\\([hdwmy]\\)";
		if (rsearch(prog(REP, fold), x, raw)) {
			o->rep = S[CAP[2]] == '.' ? '.' : CAP[3] - CAP[2] == 2 ? '#' : '+';
			o->rep_n = num(CAP[4], CAP[5]); o->rep_unit = unit(CAP[6]);
			if (G(4)) { o->rep_dl_n = num(CAP[8], CAP[9]); o->rep_dl_unit = unit(CAP[10]); }
		}
		if (rsearch(prog(WARN, fold), x, raw)) {
			o->warn = G(1) ? 'f' : 'a'; o->warn_n = num(CAP[4], CAP[5]); o->warn_unit = unit(CAP[6]);
		}
		date(ds, de, &o->y, &o->m, &o->d, &o->h, &o->min);
		int y = -1, m = -1, d = -1, h = -1, mi = -1;
		if (hasend) date(e2, e2end, &y, &m, &d, &h, &mi);
		o->y2 = y >= 0 ? y : o->y; o->m2 = m >= 0 ? m : o->m; o->d2 = d >= 0 ? d : o->d;
		o->h2 = h >= 0 ? h : tr ? th : o->h;
		o->min2 = mi >= 0 ? mi : tr ? tm : o->min;
	} else if (rs("\\([012]?[0-9]\\):\\([0-5][0-9]\\)\\(-\\([012]?[0-9]\\):\\([0-5][0-9]\\)\\)?", ds, de)) {
		o->h = num(CAP[2], CAP[3]); o->min = num(CAP[4], CAP[5]);
		if (G(3)) { o->h2 = num(CAP[8], CAP[9]); o->min2 = num(CAP[10], CAP[11]); }
	}
	return 1;
}

int org_timestamp(const Text *t, size_t pos, OrgTimestamp *out)
{
	load(t, pos, line_end(t, pos));
	return stamp(0, out, 1);
}

int org_planning(const Text *t, const OrgNode *p, OrgTimestamp out[3])
{
	load(t, p->begin, p->end);
	int found = 0;
	for (size_t x = 0; rscs("\\(?:\\(?:CLOSED\\|DEADLINE\\|SCHEDULED\\):\\)", x, L);) {
		int k = S[CAP[0]] == 'C' ? 0 : S[CAP[0]] == 'D' ? 1 : 2;
		x = CAP[1];
		while (x < L && (S[x] == ' ' || S[x] == '\t')) x++;
		if (stamp(x, &out[k], 0)) found |= 1 << k; else found &= ~(1 << k);
	}
	return found;
}

int org_clock(const Text *t, const OrgNode *c, OrgTimestamp *out, size_t dur[2])
{
	load(t, c->begin, line_end(t, c->begin));
	size_t e = eol(0), x;
	dur[0] = dur[1] = B + e;
	if (!rs("CLOCK:", 0, e)) return 0;
	for (x = CAP[1]; x < e && (S[x] == ' ' || S[x] == '\t'); x++) ;
	int has = stamp(x, out, 1);
	if (rs("=> ", x, e)) {
		for (x = CAP[1]; x < e && (S[x] == ' ' || S[x] == '\t'); x++) ;
		if (la("\\(\\S-+\\)[ \t]*$", x)) { dur[0] = B + CAP[2]; dur[1] = B + CAP[3]; }
	}
	return has;
}
