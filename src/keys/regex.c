/* Vim's patterns, as Neovim 0.12 reads them (regexp.c peekchr(), the magic
   levels \v \m \M \V) and matches them, over the buffer's lines so \n and
   \_x cross line ends (vim_regexec_multi()).  A pattern is compiled to
   nodes, each naming the node after it.  A node that can match in more than
   one way recurses for the rest, so a failure backtracks, as Neovim's
   backtracking engine (regexp_bt.c) does; a multi on one character loops
   instead (regrepeat()), so ".*" on a long line does not recurse per
   character.  Neovim picks its NFA engine for most patterns; both give the
   same matches, which tests/test_keys_regex.c checks against nvim. */
#include <limits.h>
#include <stdio.h>
#include "k.h"
#include "uni.h"

enum { END, NOTHING, CHAR, ANY, SET, CLASS, BOL, EOL, BOW, EOW, BOF, EOFILE, NEWLINE,
	OPEN, CLOSE, BRANCH, REP, REPEND, BACKREF, ZS, ZE, LOOK, LOOKEND };
/* CHAR: a = code point.  ANY, SET, CLASS: nl = a line break matches too
   (\_); SET: rng[a .. b), neg; CLASS: a = its letter.  OPEN, CLOSE,
   BACKREF: a = group.  BRANCH: a = this alternative, b = the next BRANCH
   or -1.  REP: a = body, b = min, c = max (-1: none), lazy, simple (a body
   of one character node).  REPEND: a = its REP.  LOOK: a = body ending in
   LOOKEND, b = '=', '!', '>' or '<', neg. */
typedef struct { int op, next, a, b, c, nl, neg, lazy, simple; } Node;
static Node *prog;
static int np, cap;
static int (*rng)[2], nrng, caprng;   /* ranges; a < 0: the class [:...:] number -1 - a */
static int ic;
static const char *err;

/* the messages are the NFA engine's, which Neovim shows */
static const char *fmt(const char *f, int c)
{
	static char b[64];
	snprintf(b, sizeof b, f, c);
	return b;
}

static int node(int op)
{
	if (np == cap) {
		Node *p = realloc(prog, sizeof *prog * (size_t)(cap ? cap * 2 : 64));
		if (!p) { err = "E342: Out of memory!"; return 0; }
		prog = p, cap = cap ? cap * 2 : 64;
	}
	prog[np] = (Node){ op, -1, 0, 0, 0, 0, 0, 0, 0 };
	return np++;
}

static void range(int a, int b)
{
	if (nrng == caprng) {
		void *p = realloc(rng, sizeof *rng * (size_t)(caprng ? caprng * 2 : 32));
		if (!p) { err = "E342: Out of memory!"; return; }
		rng = p, caprng = caprng ? caprng * 2 : 32;
	}
	rng[nrng][0] = a, rng[nrng++][1] = b;
}

/* ---- reading the pattern (peekchr(), skipchr()) ---- */

enum { MAGIC = 0x200000, MAGIC_NONE = 1, MAGIC_OFF, MAGIC_ON, MAGIC_ALL };
static const char *rp;
static int magic, curchr, prev, prevprev, at_start, prev_at_start, after_slash, nparens, closed[10];

static int cplen(const char *s) { return *s ? (int)uni_len((const unsigned char *)s, strlen(s), 0) : 0; }
static int cpat(const char *s) { return *s ? (int)uni_cp((const unsigned char *)s, strlen(s), 0) : 0; }

/* is the plain character c magic here */
static int plain_magic(int c)
{
	if (c && strchr(".[~", c)) return magic >= MAGIC_ON;
	if (c && strchr("(){%+=?@!&|<>#\"',-:;`/", c)) return magic == MAGIC_ALL;
	if (c == '*')
		return magic >= MAGIC_ON && !at_start && !(prev_at_start && prev == MAGIC + '^')
		       && (after_slash || (prev != MAGIC + '(' && prev != MAGIC + '&' && prev != MAGIC + '|'));
	if (c == '^')
		return magic >= MAGIC_OFF && (at_start || magic == MAGIC_ALL || prev == MAGIC + '(' || prev == MAGIC + '|'
		       || prev == MAGIC + '&' || prev == MAGIC + 'n' || (prev == '(' && prevprev == MAGIC + '%'));
	if (c == '$' && magic >= MAGIC_OFF) {
		const char *p = rp + 1;
		int all = magic == MAGIC_ALL;
		for (; p[0] == '\\' && p[1] && strchr("cCmMvVZ", p[1]); p += 2) all = p[1] == 'v' || (all && strchr("cCZ", p[1]));
		return !*p || (p[0] == '\\' && p[1] && strchr("|&)n", p[1])) || (all && strchr("|&)", *p)) || magic == MAGIC_ALL;
	}
	return 0;
}

static int peek(void)
{
	if (curchr != -1) return curchr;
	int c = (unsigned char)rp[0], d = (unsigned char)rp[1];
	if (c == '\\' && d) {
		if (d < 0x80 && strchr("%&()*+.123456789<=>?@ACDFHIKLMOPSUVWXZ[_acdfhiklmnopsuvwxz{|~", d)) {
			/* a backslash toggles what the character would be (the
			   position rules of ^ and * as for it, but not at the start:
			   "/\*ptr") */
			prev_at_start = at_start, at_start = 0;
			rp++, after_slash++;
			curchr = plain_magic(d) ? d : MAGIC + d;
			rp--, after_slash--;
		} else if (d == 'r' || d == 't' || d == 'e' || d == 'b') curchr = d == 'r' ? '\r' : d == 't' ? '\t' : d == 'e' ? 27 : 8;
		else if (magic == MAGIC_NONE && (d == '$' || d == '^')) curchr = MAGIC + d;
		else curchr = cpat(rp + 1);
	} else curchr = c < 0x80 ? (plain_magic(c) ? MAGIC + c : c) : cpat(rp);
	if (curchr == MAGIC + '^') at_start = 1, prev_at_start = 0;
	return curchr;
}

static void skip(void)
{
	rp += *rp == '\\' && rp[1] ? 1 + cplen(rp + 1) : cplen(rp);
	prev_at_start = at_start, at_start = 0;
	prevprev = prev, prev = curchr, curchr = -1;
}

static int get(void) { int c = peek(); skip(); return c; }

/* \%d123 \%x2a \%u20AC \o40 in []: at most max digits of base; -1: none */
static long digits(const char **s, int base, int max)
{
	long v = 0;
	int i = 0;
	for (; i < max; i++) {
		int c = (unsigned char)(*s)[i];
		int d = c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : 99;
		if (d >= base) break;
		v = v * base + d;
	}
	*s += i;
	return i ? v : -1;
}

/* ---- compiling (reg(), regbranch(), regpiece(), regatom()) ---- */

typedef struct { int first, last; } Frag;
static Frag alts(int paren);
static const Frag NONE_ = { -1, -1 };

static Frag one_node(int op, int a)
{
	int n = node(op);
	prog[n].a = a;
	return (Frag){ n, n };
}

static int isclassletter(int c) { return c > 0 && c < 0x80 && strchr("iIkKfFpPsSdDxXoOwWhHaAlLuU", c); }

static const char *const CLASSES[] = { "alnum", "alpha", "blank", "cntrl", "digit", "graph", "lower", "print",
	"punct", "space", "upper", "xdigit", "return", "tab", "escape", "backspace", "ident", "keyword", "fname", 0 };

/* get_char_class(): [:name:] at p, its number, or -1 */
static int charclass(const char **p)
{
	if ((*p)[0] != '[' || (*p)[1] != ':') return -1;
	for (int i = 0; CLASSES[i]; i++) {
		size_t n = strlen(CLASSES[i]);
		if (!strncmp(*p + 2, CLASSES[i], n) && !strncmp(*p + 2 + n, ":]", 2)) { *p += n + 4; return i; }
	}
	return -1;
}

/* skip_anyof(): the "]" that ends the collection whose "[" is before p,
   or the NUL */
static const char *skip_anyof(const char *p)
{
	p += *p == '^';
	if (*p == ']' || *p == '-') p++;
	while (*p && *p != ']') {
		if (cplen(p) > 1) p += cplen(p);
		else if (*p == '-') { p++; if (*p && *p != ']') p += cplen(p); }
		else if (*p == '\\' && p[1] && strchr("]^-n\\rtebdoxuU", p[1])) p += 2;
		else if (*p == '[') { if (charclass(&p) < 0) p++; }
		else p++;
	}
	return p;
}

/* the collection [...] at rp (on its "["); 0 when it has no "]", so "[" is
   literal */
static int collection(int *neg, int *nl)
{
	const char *end = skip_anyof(rp + 1), *p;
	if (*end != ']') return 0;
	p = rp + 1;
	*neg = *p == '^';
	p += *neg;
	int startc = -1;
	if (*p == ']' || *p == '-') range(startc = *p, *p), p++;
	while (p < end) {
		if (*p == '-') {
			p++;
			if (p == end || startc == -1 || (p[0] == '\\' && p[1] == 'n')) { range('-', '-'); startc = '-'; continue; }
			int endc = cpat(p);
			p += cplen(p);
			if (endc == '\\' && strchr("doxuU", *p)) {
				int b = *p == 'd' ? 10 : *p == 'o' ? 8 : 16;
				const char *q = p + 1;
				long v = digits(&q, b, *p == 'x' ? 2 : *p == 'u' ? 4 : *p == 'U' ? 8 : *p == 'o' ? 4 : 10);
				if (v >= 0) endc = (int)v, p = q;
			}
			if (startc > endc) { err = "E944: Reverse range in character class"; return 0; }
			rng[nrng - 1][1] = endc;
			startc = -1;
		} else if (*p == '\\' && strchr("]^-n\\rtebdoxuU", p[1])) {
			int d = p[1];
			p += 2;
			if (d == 'n') { *nl = 1; startc = -1; continue; }
			if (strchr("doxuU", d)) {
				const char *q = p;
				long v = digits(&q, d == 'd' ? 10 : d == 'o' ? 8 : 16, d == 'x' ? 2 : d == 'u' ? 4 : d == 'U' ? 8 : d == 'o' ? 4 : 10);
				startc = v < 0 ? d : (int)v;   /* coll_get_char(): no digits is the letter */
				if (v >= 0) p = q;
				if (v < 0) { range('\\', '\\'); }
			} else startc = d == 'r' ? '\r' : d == 't' ? '\t' : d == 'e' ? 27 : d == 'b' ? 8 : d;
			range(startc, startc);
		} else if (*p == '[' && charclass(&(const char *){ p }) >= 0) {
			range(-1 - charclass(&p), 0);
			startc = -1;
		} else {
			startc = cpat(p);
			range(startc, startc);
			p += cplen(p);
		}
	}
	rp = end;
	return 1;
}

static Frag atom(int *simple)
{
	const char *at = rp;
	int c = get(), neg, nl = 0, a;
	*simple = 0;
	switch (c) {
	case MAGIC + '^': return one_node(BOL, 0);
	case MAGIC + '$': return one_node(EOL, 0);
	case MAGIC + '<': return one_node(BOW, 0);
	case MAGIC + '>': return one_node(EOW, 0);
	case MAGIC + 'n': return one_node(NEWLINE, 0);
	case MAGIC + '.': *simple = 1; return one_node(ANY, 0);
	case MAGIC + '_': {
		at = rp;
		c = get();
		if (c > MAGIC) c -= MAGIC;   /* no_Magic() */
		Frag f;
		if (c == '^') return one_node(BOL, 0);
		if (c == '$') return one_node(EOL, 0);
		if (c == '.') f = one_node(ANY, 0);
		else if (c == '[') {
			rp = at + (*at == '\\');
			a = nrng;
			if (!collection(&neg, &nl)) {   /* no "]": "[" is a character */
				if (err) return NONE_;
				nrng = a;
				skip();
				c = '[';
				break;
			}
			skip();
			f = one_node(SET, a);
			prog[f.first].b = nrng, prog[f.first].neg = neg;
		} else if (isclassletter(c)) f = one_node(CLASS, c);
		else { err = fmt("E877: (NFA regexp) Invalid character class: %d", c); return NONE_; }
		prog[f.first].nl = 1;
		return f;
	}
	case MAGIC + '(': {
		if (nparens >= 9) { err = "E872: (NFA regexp) Too many '('"; return NONE_; }
		int g = ++nparens, o = node(OPEN);
		prog[o].a = g;
		Frag f = alts(1);
		if (err) return f;
		int cl = node(CLOSE);
		prog[cl].a = g;
		closed[g] = 1;
		prog[o].next = f.first, prog[f.last].next = cl;
		return (Frag){ o, cl };
	}
	case MAGIC + '%':
		c = get();
		if (c == '(' || c == MAGIC + '(') return alts(1);
		if (c == '^' || c == MAGIC + '^') return one_node(BOF, 0);
		if (c == '$' || c == MAGIC + '$') return one_node(EOFILE, 0);
		if (c && strchr("doxuU", c)) {
			long v = digits(&rp, c == 'd' ? 10 : c == 'o' ? 8 : 16, c == 'x' ? 2 : c == 'u' ? 4 : c == 'U' ? 8 : 10);
			curchr = -1;
			if (v < 0) { err = "E678: Invalid character after \\%[dxouU]"; return NONE_; }
			*simple = 1;
			return one_node(CHAR, (int)v);
		}
		err = fmt("E867: (NFA) Unknown operator '\\%%%c'", c > MAGIC ? c - MAGIC : c);
		return NONE_;
	case MAGIC + 'z':
		c = get();
		if (c == 's' || c == 'e') {
			c = peek();
			if (c == MAGIC + '*' || c == MAGIC + '+' || c == MAGIC + '{') { err = fmt("E888: (NFA regexp) cannot repeat \\z%c", rp[-1]); return NONE_; }
			return one_node(rp[-1] == 's' ? ZS : ZE, 0);
		}
		err = fmt("E867: (NFA) Unknown operator '\\z%c'", c > MAGIC ? c - MAGIC : c);
		return NONE_;
	case MAGIC + '[':
		rp = at + (*at == '\\');
		a = nrng;
		if (collection(&neg, &nl)) {
			skip();
			Frag f = one_node(SET, a);
			prog[f.first].b = nrng, prog[f.first].neg = neg, prog[f.first].nl = nl;
			*simple = !nl;
			return f;
		}
		if (err) return NONE_;
		nrng = a;
		skip();
		c = '[';
		break;
	case MAGIC + '~':
		err = "E33: No previous substitute regular expression";
		return NONE_;
	case MAGIC + '=': case MAGIC + '?': case MAGIC + '+': case MAGIC + '@': case MAGIC + '{': case MAGIC + '*':
		err = fmt("E866: (NFA regexp) Misplaced %c", c - MAGIC);
		return NONE_;
	default:
		if (c >= MAGIC + '1' && c <= MAGIC + '9') {
			/* seen_endbrace(): the group's \) comes first, unless a
			   look-behind follows */
			const char *p = rp;
			while (*p && !(p[0] == '@' && p[1] == '<' && (p[2] == '!' || p[2] == '='))) p++;
			if (!closed[c - MAGIC - '0'] && !*p) { err = "E65: Illegal back reference"; return NONE_; }
			return one_node(BACKREF, c - MAGIC - '0');
		}
		if (c > MAGIC && isclassletter(c - MAGIC)) { *simple = 1; return one_node(CLASS, c - MAGIC); }
		if (c > MAGIC) c -= MAGIC;   /* magic with no meaning here: itself */
	}
	*simple = 1;
	return one_node(CHAR, c);
}

static int ismulti(int c)
{
	return c == MAGIC + '*' || c == MAGIC + '+' || c == MAGIC + '=' || c == MAGIC + '?' || c == MAGIC + '{' || c == MAGIC + '@';
}

static Frag piece(void)
{
	int simple, lazy = 0, min = 0, max = -1;
	Frag a = atom(&simple);
	if (err) return a;
	int c = peek();
	if (!ismulti(c)) return a;
	skip();
	if (c == MAGIC + '+') min = 1;
	else if (c == MAGIC + '=' || c == MAGIC + '?') max = 1;
	else if (c == MAGIC + '{') {
		/* read_limits(): \{n,m} \{-n,m} \{n} \{,m} \{} */
		const char *p = rp;
		lazy = *p == '-';
		p += lazy;
		long lo = digits(&p, 10, 9), hi = lo;
		if (*p == ',') p++, hi = digits(&p, 10, 9);
		p += *p == '\\';
		if (*p != '}') { err = "E870: (NFA regexp) Error reading repetition limits"; return a; }
		rp = p + 1, curchr = -1;
		min = lo < 0 ? 0 : (int)lo, max = hi < 0 ? -1 : (int)hi;
		if (max >= 0 && min > max) { int t = min; min = max, max = t; }
	} else if (c == MAGIC + '@') {
		const char *p = rp;
		while (*p >= '0' && *p <= '9') p++;   /* \@123<= : no limit kept */
		int k = *p == '=' || *p == '!' || *p == '>' ? *p : 0, neg = *p == '!';
		if (*p == '<' && (p[1] == '=' || p[1] == '!')) k = '<', neg = p[1] == '!', p++;
		if (!k) { err = fmt("E869: (NFA) Unknown operator '\\@%c'", *p); return a; }
		rp = p + 1, curchr = -1;
		int n = node(LOOK), e = node(LOOKEND);
		prog[a.last].next = e;
		prog[n].a = a.first, prog[n].b = k, prog[n].neg = neg;
		return (Frag){ n, n };
	}
	if (ismulti(peek())) { err = "E871: (NFA regexp) Can't have a multi follow a multi"; return a; }
	int r = node(REP);
	prog[r].a = a.first, prog[r].b = min, prog[r].c = max, prog[r].lazy = lazy, prog[r].simple = simple;
	if (!simple) { int e = node(REPEND); prog[e].a = r; prog[a.last].next = e; }
	return (Frag){ r, r };
}

/* regbranch(): pieces up to \| \) or the end; \c \C \m \M \v \V \Z take
   effect where they stand */
static Frag branch(void)
{
	Frag f = NONE_;
	for (int c; (c = peek()) && c != MAGIC + '|' && c != MAGIC + '&' && c != MAGIC + ')';) {
		if (c > MAGIC && strchr("cCvmMVZ", c - MAGIC)) {
			if (c == MAGIC + 'c') ic = 1;
			else if (c == MAGIC + 'C') ic = ic == 1;
			else if (c != MAGIC + 'Z') magic = c == MAGIC + 'v' ? MAGIC_ALL : c == MAGIC + 'm' ? MAGIC_ON : c == MAGIC + 'M' ? MAGIC_OFF : MAGIC_NONE;
			int s = prev_at_start, pr = prev, pp = prevprev;
			skip();
			at_start = s, prev = pr, prevprev = pp;   /* skipchr_keepstart() */
			continue;
		}
		Frag p = piece();
		if (err) return f;
		if (f.first < 0) f = p;
		else prog[f.last].next = p.first, f.last = p.last;
	}
	if (f.first < 0) f = one_node(NOTHING, 0);
	if (peek() != MAGIC + '&') return f;
	/* regbranch(): what comes before \& must match here too, as \@= does */
	skip();
	int n = node(LOOK), e = node(LOOKEND);
	prog[f.last].next = e, prog[n].a = f.first, prog[n].b = '=';
	Frag r = branch();
	prog[n].next = r.first;
	return (Frag){ n, r.last };
}

/* reg(): alternatives, up to \) when in a group */
static Frag alts(int paren)
{
	int join = -1, br = -1, first = -1;
	for (;;) {
		Frag b = branch();
		if (err) return b;
		int c = peek();
		if (c == MAGIC + '|') {
			if (join < 0) join = node(NOTHING);
			int n = node(BRANCH);
			prog[n].a = b.first, prog[n].b = -1, prog[b.last].next = join;
			if (br >= 0) prog[br].b = n;
			else first = n;
			br = n;
			skip();
			at_start = 1;
			continue;
		}
		if (paren ? c != MAGIC + ')' : c != 0) { err = paren ? "E54: Unmatched \\(" : "E55: Unmatched \\)"; return b; }
		if (paren) skip();
		if (first < 0) return b;
		int n = node(BRANCH);
		prog[n].a = b.first, prog[n].b = -1, prog[b.last].next = join;
		prog[br].b = n;
		return (Frag){ first, join };
	}
}

static int start;

/* skip_regexp_ex(): where the pattern at p ends, at delim or the NUL; with
   delim '?' each "\\?" in it becomes "?".  *all: \\v was the last of \\v \\V */
char *re_skip(char *p, int delim, int *all)
{
	int my = MAGIC_ON;
	for (; *p && *p != delim; p += cplen(p))
		if ((*p == '[' && my >= MAGIC_ON) || (*p == '\\' && p[1] == '[' && my <= MAGIC_OFF)) {
			p = (char *)skip_anyof(p + 1);
			if (!*p) break;
		} else if (*p == '\\' && p[1]) {
			if (delim == '?' && p[1] == '?') memmove(p, p + 1, strlen(p));
			else p++;
			if (*p == 'v') my = MAGIC_ALL;
			else if (*p == 'V') my = MAGIC_NONE;
		}
	if (all) *all = my == MAGIC_ALL;
	return p;
}

/* Compile pat.  icase: 'ignorecase' ('smartcase' applied by the caller);
   \c and \C in the pattern win.  0, or -1 with Neovim's message in *msg. */
int re_compile(const char *pat, int icase, const char **msg)
{
	/* the program compiled last, kept: 'hlsearch' compiles for each line
	   drawn (keys_hl()) */
	static char last[sizeof ((Editor *)0)->cmd + 1];
	static int last_ic = -1;
	*msg = NULL;
	if (last_ic == !!icase && !strcmp(pat, last)) return 0;
	last_ic = -1;
	np = nrng = nparens = 0;
	memset(closed, 0, sizeof closed);
	err = NULL;
	rp = pat;
	magic = MAGIC_ON;
	curchr = prev = prevprev = -1;
	at_start = 1, prev_at_start = after_slash = 0;
	ic = icase ? 2 : 0;   /* 2: from the option, which \C turns off */
	int e = node(END);
	Frag f = alts(0);
	if (!err) prog[f.last].next = e, start = f.first;
	ic = ic != 0;
	*msg = err;
	if (!err && strlen(pat) < sizeof last) strcpy(last, pat), last_ic = !!icase;
	return err ? -1 : 0;
}

int re_hasnl(void)
{
	for (int i = 0; i < np; i++) if (prog[i].op == NEWLINE || prog[i].nl) return 1;
	return 0;
}

/* ---- matching (regmatch(), regrepeat()) ---- */

static Pos gs[10], ge[10], zs, ze, mend, lookend;
static int look;          /* the innermost look-around's kind: \zs in one, \ze but in \@=, do nothing */
static Pos behind;        /* where a \@<= body must end (BHPOS), coladd 1 while in one */
static int *cnt;          /* passes made of each REP */
static Pos *ist;          /* where each REP's current pass started */
static long *stack, nstack, capstack;   /* positions of the simple REPs */
static long steps;
enum { MAXSTEPS = 20000000 };

static int fold(int c) { return ic ? map_case(c, 0) : c; }
static int isdigit_(int c) { return c >= '0' && c <= '9'; }
static int isalpha_(int c) { return (c | 32) >= 'a' && (c | 32) <= 'z'; }
static int islower_(int c) { return map_case(c, 1) != c || c == 0xdf; }
static int isupper_(int c) { return map_case(c, 0) != c; }

/* vim_isIDc(), vim_iswordc(), vim_isfilec(), vim_isprintc() with the
   default 'isident' 'iskeyword' 'isfname' 'isprint' */
static int wordc(int c) { return c < 0x100 ? isdigit_(c) || isalpha_(c) || c == '_' || c >= 0xc0 : cls_of(c) >= 2; }
static int identc(int c) { return c < 0x100 && wordc(c); }
static int filec(int c) { return c >= 0x100 || wordc(c) || (c < 0x80 && c && strchr("/.-_+,#$%~=", c)); }
static int printc(int c) { return (c >= 0x20 && c < 0x7f) || c >= 0xa1; }

static int isclass(int k, int c)
{
	switch (k) {
	case 'i': return identc(c);
	case 'I': return identc(c) && !isdigit_(c);
	case 'k': return wordc(c);
	case 'K': return wordc(c) && !isdigit_(c);
	case 'f': return filec(c);
	case 'F': return filec(c) && !isdigit_(c);
	case 'p': return printc(c);
	case 'P': return printc(c) && !isdigit_(c);
	}
	int m;
	switch (k | 32) {
	case 's': m = c == ' ' || c == '\t'; break;
	case 'd': m = isdigit_(c); break;
	case 'x': m = isdigit_(c) || ((c | 32) >= 'a' && (c | 32) <= 'f'); break;
	case 'o': m = c >= '0' && c <= '7'; break;
	case 'w': m = isdigit_(c) || isalpha_(c) || c == '_'; break;
	case 'h': m = isalpha_(c) || c == '_'; break;
	case 'a': m = isalpha_(c); break;
	case 'l': m = c >= 'a' && c <= 'z'; break;
	default: m = c >= 'A' && c <= 'Z';
	}
	return m == !(k & 32) ? 0 : 1;
}

/* check_char_class() of the NFA engine, for [:name:] */
static int incharclass(int k, int c)
{
	switch (k) {
	case 0: return c < 0x80 && (isdigit_(c) || isalpha_(c));
	case 1: return c < 0x80 && isalpha_(c);
	case 2: return c == ' ' || c == '\t';
	case 3: return c < 0x20 || c == 0x7f;
	case 4: return isdigit_(c);
	case 5: return c > 0x20 && c < 0x7f;
	case 6: return islower_(c) && c != 170 && c != 186;
	case 7: return printc(c);
	case 8: return c > 0x20 && c < 0x7f && !isdigit_(c) && !isalpha_(c);
	case 9: return c == ' ' || (c >= 9 && c <= 13);
	case 10: return isupper_(c);
	case 11: return isdigit_(c) || ((c | 32) >= 'a' && (c | 32) <= 'f');
	case 12: return c == '\r';
	case 13: return c == '\t';
	case 14: return c == 27;
	case 15: return c == 8;
	case 16: return identc(c);
	case 17: return wordc(c);
	default: return filec(c);
	}
}

static int inset(const Node *n, int c)
{
	for (int i = n->a; i < n->b; i++) {
		int a = rng[i][0], b = rng[i][1];
		if (a < 0 ? incharclass(-1 - a, c)
		          : (c >= a && c <= b) || (ic && ((map_case(c, 0) >= a && map_case(c, 0) <= b) || (map_case(c, 1) >= a && map_case(c, 1) <= b))))
			return 1;
	}
	return 0;
}

/* the character at (l, c) and its bytes with any composing ones after it
   (ADVANCE_REGINPUT()); 0 bytes at the end of the line */
static int chr(long l, long c, long *len)
{
	const unsigned char *s = (const unsigned char *)ml_get(l);
	size_t n = (size_t)ml_len(l);
	if (c >= (long)n) { *len = 0; return 0; }
	*len = (long)uni_glen(s, n, (size_t)c);
	return (int)uni_cp(s, n, (size_t)c);
}

/* a one-character node at (l, c): its length, -1 if it does not match */
static long one(const Node *n, long l, long c, int rep)
{
	long len;
	int ch = chr(l, c, &len);
	if (!len) return -1;
	switch (n->op) {
	case ANY: return len;
	/* a composing character after it in the text does not match;
	   both engines' repeats take the character and leave it */
	case CHAR: {
		long cp = (long)uni_len((const unsigned char *)ml_get(l), (size_t)ml_len(l), (size_t)c);
		return fold(ch) == fold(n->a) && (rep || cp == len) ? cp : -1;
	}
	case SET: return inset(n, ch) != n->neg ? len : -1;
	default: return isclass(n->a, ch) ? len : -1;
	}
}

static int cls_at(long l, long c)
{
	long len;
	int ch = chr(l, c, &len);
	return len ? cls_of(ch) : 0;
}

static int cls_before(long l, long c)
{
	const unsigned char *s = (const unsigned char *)ml_get(l);
	size_t n = (size_t)ml_len(l);
	return cls_of((int)uni_cp(s, n, uni_gprev(s, n, (size_t)c)));
}

static int push(long v)
{
	if (nstack == capstack) {
		long *p = realloc(stack, sizeof *stack * (size_t)(capstack ? capstack * 2 : 256));
		if (!p) return 0;
		stack = p, capstack = capstack ? capstack * 2 : 256;
	}
	stack[nstack++] = v;
	return 1;
}

static int m(int n, long l, long c);

/* the REP r at (l, c) with cnt[r] passes made */
static int rep(int r, long l, long c)
{
	Node *p = &prog[r];
	long max = p->c < 0 ? LONG_MAX : p->c;
	if (p->simple) {
		const Node *b = &prog[p->a];
		long k = 0, len, base = nstack;
		int ok = 0;
		if (p->lazy) {
			for (;; c += len, k++) {
				if (k >= p->b && m(p->next, l, c)) return 1;
				if (k >= max || (len = one(b, l, c, 1)) < 0 || ++steps > MAXSTEPS) return 0;
			}
		}
		/* regrepeat(): as many as match, then back off one at a time */
		for (; k < max && (len = one(b, l, c, 1)) >= 0; k++, c += len)
			if (!push(c)) return 0;
		for (; k >= p->b && !ok; k--) {
			ok = m(p->next, l, c);
			if (k) c = stack[base + k - 1];
		}
		nstack = base;
		return ok;
	}
	int k = cnt[r], ok = 0;
	Pos o = ist[r];
	int more = k < max;
	if (k < p->b) ist[r] = (Pos){ l, c, 0 }, ok = m(p->a, l, c);
	else if (!p->lazy) {
		if (more) ist[r] = (Pos){ l, c, 0 }, ok = m(p->a, l, c);
		ist[r] = o;
		if (!ok) ok = m(p->next, l, c);
	} else if (!(ok = m(p->next, l, c)) && more) ist[r] = (Pos){ l, c, 0 }, ok = m(p->a, l, c);
	ist[r] = o;
	return ok;
}

static int m(int n, long l, long c)
{
	for (; n >= 0; n = prog[n].next) {
		if (++steps > MAXSTEPS) return 0;
		Node *p = &prog[n];
		long len;
		switch (p->op) {
		case END: {
			/* nfa_regmatch(): a match that ends before a composing
			   character is none */
			if (c && c < ml_len(l)) {
				const unsigned char *t = (const unsigned char *)ml_get(l);
				size_t h = uni_gprev(t, (size_t)ml_len(l), (size_t)c);
				if (h + uni_glen(t, (size_t)ml_len(l), h) != (size_t)c) return 0;
			}
			mend = (Pos){ l, c, 0 };
			return 1;
		}
		case LOOKEND:
			if (behind.coladd && (behind.lnum != l || behind.col != c)) return 0;
			lookend = (Pos){ l, c, 0 };
			return 1;
		case NOTHING: break;
		case ANY: case CHAR: case SET: case CLASS:
			if ((len = one(p, l, c, 0)) >= 0) c += len;
			else if (p->nl && c >= ml_len(l) && l < nlines()) l++, c = 0;
			else return 0;
			break;
		case BOL: if (c) return 0; break;
		case EOL: if (c < ml_len(l)) return 0; break;
		case BOF: if (l || c) return 0; break;
		case EOFILE: if (l + 1 != nlines() || c < ml_len(l)) return 0; break;
		case BOW: {
			int k = cls_at(l, c);
			if (k <= 1 || (c && cls_before(l, c) == k)) return 0;
			break;
		}
		case EOW: {
			if (!c) return 0;
			int k = cls_before(l, c);
			if (k <= 1 || cls_at(l, c) == k) return 0;
			break;
		}
		case NEWLINE:
			if (c < ml_len(l) || l >= nlines()) return 0;
			l++, c = 0;
			break;
		case OPEN: case CLOSE: case ZS: case ZE: {
			if (look && (p->op == ZS || (p->op == ZE && look != '='))) break;
			Pos *s = p->op == OPEN ? &gs[p->a] : p->op == CLOSE ? &ge[p->a] : p->op == ZS ? &zs : &ze, o = *s;
			*s = (Pos){ l, c, 1 };
			if (m(p->next, l, c)) return 1;
			*s = o;
			return 0;
		}
		case BRANCH:
			for (int b = n; b >= 0; b = prog[b].b)
				if (m(prog[b].a, l, c)) return 1;
			return 0;
		case BACKREF: {
			/* an unset group matches empty */
			Pos a = gs[p->a], z = ge[p->a];
			if (!a.coladd || !z.coladd) break;
			while (lt(a, z)) {
				if (a.col >= ml_len(a.lnum)) {
					if (c < ml_len(l) || l >= nlines()) return 0;
					a.lnum++, a.col = 0, l++, c = 0;
					continue;
				}
				long la, lb;
				int x = chr(a.lnum, a.col, &la), y = chr(l, c, &lb);
				if (!lb || fold(x) != fold(y)) return 0;
				a.col += la, c += lb;
			}
			break;
		}
		case REP: {
			int k = cnt[n], ok = (cnt[n] = 0, rep(n, l, c));
			cnt[n] = k;
			return ok;
		}
		case REPEND: {
			int r = p->a;
			/* an empty pass ends the loop, not the match (BACK) */
			if (cnt[r] >= prog[r].b && ist[r].lnum == l && ist[r].col == c) { n = r; continue; }
			cnt[r]++;
			int ok = rep(r, l, c);
			cnt[r]--;
			return ok;
		}
		case LOOK: {
			Pos save = mend, ob = behind, oz = ze;
			int ol = look;
			int ok = 0;
			behind = (Pos){ l, c, p->b == '<' };
			look = p->b;
			if (p->b != '<') ok = m(p->a, l, c);
			else   /* a match of the body that ends here, from here back */
				for (long s = c;; s = (long)uni_gprev((const unsigned char *)ml_get(l), (size_t)ml_len(l), (size_t)s)) {
					if (m(p->a, l, s)) { ok = 1; break; }
					if (!s) break;
				}
			mend = save, behind = ob, look = ol;
			if (ok == p->neg) return ze = oz, 0;
			if (p->b == '>') l = lookend.lnum, c = lookend.col;
			if (memcmp(&ze, &oz, sizeof ze)) {   /* \ze in it: undone if what follows fails */
				if (m(p->next, l, c)) return 1;
				ze = oz;
				return 0;
			}
			break;
		}
		}
	}
	return 1;
}

static int growth(void)
{
	static int capn;
	if (np <= capn) return 1;
	int *a = realloc(cnt, sizeof *cnt * (size_t)np);
	if (!a) return 0;
	cnt = a;
	Pos *b = realloc(ist, sizeof *ist * (size_t)np);
	if (!b) return 0;
	ist = b, capn = np;
	return 1;
}

/* vim_regexec_multi(): the first match that starts in line l at or after
   column col; its start and end (\zs \ze applied) and the groups.  1, 0 for
   none, -1 when the match took too long (Neovim's E363). */
int re_exec(long l, long col, Pos *s, Pos *e, Pos *sub_s, Pos *sub_e)
{
	if (!growth()) return -1;
	steps = 0;
	for (long c = col; c <= ml_len(l); c += c < ml_len(l) ? (long)uni_glen((const unsigned char *)ml_get(l), (size_t)ml_len(l), (size_t)c) : 1) {
		memset(gs, 0, sizeof gs), memset(ge, 0, sizeof ge);
		zs.coladd = ze.coladd = 0;
		nstack = 0;
		if (m(start, l, c)) {
			*s = zs.coladd ? zs : (Pos){ l, c, 0 };
			*e = ze.coladd ? ze : mend;
			if (lt(*e, *s)) *e = *s;
			s->coladd = e->coladd = 0;
			if (sub_s) for (int i = 0; i < 10; i++) sub_s[i] = gs[i], sub_e[i] = ge[i];
			return 1;
		}
		if (steps > MAXSTEPS) return -1;
	}
	return 0;
}
