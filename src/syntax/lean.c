/* Lean 4 as VS Code colours it (Davy, 2026-11-23: VS Code is the reference
   for Lean): vscode-lean4's TextMate grammar, syntaxes/lean4.json at
   9ac77ca, under Catppuccin Mocha.  Lean's semantic tokens (package 13)
   colour only keywords and variables over these.  The grammar's rules are
   copied in its order with TextMate's semantics: a match starts at the
   first position where any rule matches, the first rule listed wins there,
   a match never crosses a line's end (VS Code gives the regex one line and
   its '\n'), and inside a region its end is tried before its rules.  A
   region (a comment, a string, a definition's head) is one token, so a
   token start depends on the code point before it and on the line from it
   (tsql_edit), as on the T-SQL index.  test_syntax_lean.c holds every byte
   to VS Code's own colour (tests/test_syntax_lean.js records them). */
#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include "grid.h"
#include "tsql.h"
#include "uni.h"
#include "lean.h"
#include "lean_words.h"

#define N(a) (sizeof a / sizeof *a)

typedef struct {
	TsqlRd *r;
	size_t a, z;           /* faces for [a, z) into f; none when f is NULL */
	unsigned char *f;
	int probe;             /* only whether a rule matches: a region stops
	                          at its start */
} Lx;

static void put(Lx *x, size_t p, size_t e, unsigned char c)
{
	if (x->f)
		for (size_t i = p < x->a ? x->a : p; i < e && i < x->z; i++) x->f[i - x->a] = c;
}

/* the code point at p, its length into *n; past the end the line's '\n'
   VS Code adds, and a byte that starts none U+FFFD, as VS Code decodes */
static unsigned cpn(TsqlRd *r, size_t p, size_t *n)
{
	unsigned char b[4];
	size_t k = 0;
	for (int c; k < 4 && (c = tsql_byte(r, p + k)) >= 0; k++) b[k] = (unsigned char)c;
	if (!k) return *n = 0, '\n';
	*n = uni_len(b, k, 0);
	return *n == 1 && b[0] >= 0x80 ? 0xfffd : uni_cp(b, k, 0);
}

static unsigned cp(TsqlRd *r, size_t p)
{
	size_t n;
	return cpn(r, p, &n);
}

static size_t adv(TsqlRd *r, size_t p)
{
	size_t n;
	cpn(r, p, &n);
	return p + (n ? n : 1);
}

/* where the code point before p starts */
static size_t back(TsqlRd *r, size_t p)
{
	size_t s = p - 1;
	int c;
	while (s && p - s < 4 && (c = tsql_byte(r, s)) >= 0x80 && c < 0xc0) s--;
	return s;
}

/* the code point before p; a line's start has none, which no rule tells
   from a '\n' */
static unsigned before(TsqlRd *r, size_t p)
{
	size_t s = p, n;
	int c;
	if (!p) return '\n';
	while (--s && p - s < 4 && (c = tsql_byte(r, s)) >= 0x80 && c < 0xc0) ;
	unsigned u = cpn(r, s, &n);
	return s + n == p ? u : 0xfffd;
}

/* Oniguruma's \s and [[:space:]] */
static int is_sp(unsigned c)
{
	return (c >= 9 && c <= 13) || c == ' ' || c == 0x85 || c == 0xa0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a) ||
	       c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}

/* Oniguruma's \w */
static int is_w(unsigned c)
{
	size_t lo = 0, hi = N(lean_word);
	while (lo < hi) {
		size_t m = lo + (hi - lo) / 2;
		if (c > lean_word[m][1]) lo = m + 1;
		else if (c < lean_word[m][0]) hi = m;
		else return 1;
	}
	return 0;
}

/* \b */
static int wb(TsqlRd *r, size_t p) { return is_w(before(r, p)) != is_w(cp(r, p)); }

/* n hex digits at p */
static int hexes(TsqlRd *r, size_t p, size_t n)
{
	while (n && isxdigit(tsql_byte(r, p))) n--, p++;
	return !n;
}

/* c is one of the ASCII s */
static int in(unsigned c, const char *s) { return c && c < 128 && strchr(s, (int)c); }

/* the end of the bytes s at p, 0 when they are not there */
static size_t lit(TsqlRd *r, size_t p, const char *s)
{
	for (; *s; s++, p++)
		if (tsql_byte(r, p) != (unsigned char)*s) return 0;
	return p;
}

/* \s* not past the line: what follows must be on it */
static size_t blank(TsqlRd *r, size_t p)
{
	size_t n;
	unsigned c;
	while ((c = cpn(r, p, &n)) != '\n' && is_sp(c)) p += n;
	return p;
}

/* [^\]\s]* from p */
static size_t unbracket(TsqlRd *r, size_t p)
{
	size_t n;
	unsigned c;
	while ((c = cpn(r, p, &n)) != ']' && !is_sp(c) && n) p += n;
	return p;
}

/* after a word ending in [, the rest of word\[[^\]\s]*\]; 0 without its ] */
static size_t bracket(TsqlRd *r, size_t e)
{
	if (!e || tsql_byte(r, e - 1) != '[') return e;
	e = unbracket(r, e);
	return tsql_byte(r, e) == ']' ? e + 1 : 0;
}

/* (w0|w1|..) then \b, and with dot (?!\.) before it: the first word that
   fits, as Oniguruma tries them; a word ending in [ is the grammar's
   word\[[^\]\s]*\] */
static size_t alt(TsqlRd *r, size_t p, const char *const *w, size_t n, int dot)
{
	for (size_t i = 0; i < n; i++) {
		size_t e = bracket(r, lit(r, p, w[i]));
		if (e && (!dot || tsql_byte(r, e) != '.') && wb(r, e)) return e;
	}
	return 0;
}

/* \b(?<!\.)(..) */
static int word_start(TsqlRd *r, size_t p) { return wb(r, p) && before(r, p) != '.'; }

/* a string's escape: \\[\\"ntr'], \\x[0-9A-Fa-f]{2} or \\u[0-9A-Fa-f]{4} */
static size_t esc(TsqlRd *r, size_t p)
{
	int c = tsql_byte(r, p + 1);
	if (tsql_byte(r, p) != '\\') return 0;
	if (in((unsigned)c, "\\\"ntr'")) return p + 2;
	return c == 'x' && hexes(r, p + 2, 2) ? p + 4 : c == 'u' && hexes(r, p + 2, 4) ? p + 6 : 0;
}

/* Markdown in comments, as the grammar includes lean4-markdown.json in
   each (its rules that exist: frontMatter needs \A, paragraphb and
   raw_block are missing, list_paragraph begins at ^ only where a list's
   indentation already stands): colours, and where a comment ends, since a
   code span or a link can take a -/, and a fence or a quote holds it.
   Catppuccin Mocha sets a colour and a style apart (a code span is green
   in the comment's italic, green bold in **bold**): md_face maps them. */
enum { OV, GR, TE, RE, PE, YE, SA, LA, BL, SK, PI };    /* fg */
enum { NS, IT, BO };                                    /* style */
#define COL(fg, st) ((fg) * 3 + (st))
static const unsigned char md_face[][3] = {
	[OV] = { FACE_DOC_PUNCT, FACE_COMMENT, 0 },
	[GR] = { FACE_STRING, FACE_DOC_CODE, FACE_DOC_CODE_BOLD },
	[TE] = { FACE_MARKUP_LIST, FACE_DOC_RULE, 0 },
	[RE] = { FACE_MARKUP_HEADING_1, FACE_MARKUP_ITALIC, FACE_MARKUP_STRONG },
	[PE] = { FACE_MARKUP_HEADING_2, FACE_DOC_HEADING_2, 0 },
	[YE] = { FACE_MARKUP_HEADING_3, FACE_MODULE, 0 },
	[SA] = { FACE_MARKUP_HEADING_5, FACE_DOC_HEADING_5, 0 },
	[LA] = { FACE_MARKUP_HEADING_6, FACE_DOC_LINK, FACE_DOC_LINK_BOLD },
	[BL] = { FACE_FUNCTION, FACE_FUNCTION_DEFINITION, FACE_DOC_URL_BOLD },
	[SK] = { 0, FACE_DOC_LANG, 0 },
	[PI] = { FACE_STRING_ESCAPE, FACE_DOC_QUOTE, FACE_DOC_QUOTE_BOLD },
};
/* the six headings' colours */
static const unsigned char md_h[] = { RE, PE, YE, GR, SA, LA };

typedef struct {
	Lx *x;
	size_t ls, eol;        /* the line: its start and its '\n' */
	size_t g;              /* \G: where the last begin or while match ended */
	int blk;               /* what holds the line: 0 the comment, Q a quote,
	                          L a list, F a fence */
	size_t fi, fn, fk;     /* a fence's indentation (\2) and length (\3) */
	int fc;                /* and its ` or ~ */
	int nin;               /* **bold** and *italic* open on the line */
	unsigned char in[32];  /* each: its delimiter, + 128 for bold */
} Md;
enum { Q = 1, L, F };

static void pc(Md *m, size_t p, size_t e, int c) { put(m->x, p, e, md_face[c / 3][c % 3]); }
static int at(Md *m, size_t p) { return tsql_byte(m->x->r, p); }

/* what plain text has: the innermost bold or italic's, a quote's or the
   comment's colour */
static int ctx(Md *m)
{
	if (m->nin) return m->in[m->nin - 1] >= 128 ? COL(RE, BO) : COL(RE, IT);
	return m->blk == Q ? COL(PI, IT) : COL(OV, IT);
}

/* a colour with the context's style */
static int inh(Md *m, int fg) { return COL(fg, ctx(m) % 3); }

/* delimiters [p, s) and [t, e) in out, what they hold in fg with the
   context's style */
static void wrap(Md *m, size_t p, size_t s, size_t t, size_t e, int out, int fg)
{
	pc(m, p, s, out), pc(m, s, t, inh(m, fg)), pc(m, t, e, out);
}

/* a code span: (`+)([^`]|(?!(?<!`)\1(?!`))`)*+(\1), the run shortened
   while no closer of its length follows on the line */
static size_t raw(Md *m, size_t p, int paint)
{
	size_t n = 0, k, q;
	while (at(m, p + n) == '`') n++;
	for (k = n; k; k--)
		for (q = p + k; q < m->eol; q++) {
			size_t j = 0;
			if (at(m, q) != '`' || at(m, q - 1) == '`') continue;
			while (j < k && at(m, q + j) == '`') j++;
			if (j < k || at(m, q + k) == '`') continue;
			if (paint) wrap(m, p, p + k, q, q + k, COL(GR, NS), GR);
			return q + k;
		}
	return 0;
}

/* a link's [text]: the position of its ], brackets in it balanced and \\.
   escaping; 0 when it does not close on the line */
static size_t square(Md *m, size_t p)
{
	size_t d = 0;
	for (int c; p < m->eol; p++) {
		if ((c = at(m, p)) == '\\') {
			if (++p >= m->eol) return 0;
			p = adv(m->x->r, p) - 1;
		} else if (c == '[') d++;
		else if (c == ']' && !d--) return p;
	}
	return 0;
}

static size_t sp_tab(Md *m, size_t p)
{
	while (at(m, p) == ' ' || at(m, p) == '\t') p++;
	return p;
}

/* \s* on the line */
static size_t ws(Md *m, size_t p)
{
	size_t n;
	while (p < m->eol && is_sp(cpn(m->x->r, p, &n))) p += n;
	return p;
}

/* a title, ((\().+?(\))) or ((").+?(")), from its opener at p, then
   \s*(\)) when close, else \s*$: the first that fits; its end, 0 for
   none */
static size_t title(Md *m, size_t p, int close, size_t *te)
{
	int o = at(m, p), c = o == '(' ? ')' : o;
	if (o != '(' && o != '"') return 0;
	for (size_t j = adv(m->x->r, p + 1); j < m->eol; j++)
		if (at(m, j) == c) {
			size_t e = ws(m, j + 1);
			if (close ? at(m, e) == ')' : e >= m->eol) return *te = j + 1, close ? e + 1 : e;
		}
	return 0;
}

/* a URL then >?[ \t]*(title)?\s*(\)) for links and images: which URL
   ends fit is the caller's; the match's end, 0 for none */
static size_t url_tail(Md *m, size_t u, size_t *ue, size_t *te)
{
	for (int gt = at(m, u) == '>'; gt >= 0; gt--) {
		size_t e = sp_tab(m, u + (size_t)gt), t;
		*ue = u + (size_t)gt;
		if ((t = title(m, e, 1, te))) return t;
		*te = 0;
		if (at(m, e = ws(m, e)) == ')') return e + 1;
	}
	return 0;
}

/* a link or an image from p: [text] to t, ( then the URL from us to u,
   its end to ue, the title to te, ) at e - 1 */
static void paint_link(Md *m, size_t p, size_t t, size_t us, size_t u, size_t ue, size_t te, size_t e)
{
	int img = at(m, p) == '!';
	size_t s = sp_tab(m, ue);
	wrap(m, p, p + 1 + (size_t)img, t, t + 1, COL(GR, NS), img ? GR : LA);
	pc(m, t + 1, t + 2, COL(OV, NS));
	pc(m, t + 2, us, COL(BL, NS));
	pc(m, us, u, inh(m, BL));
	pc(m, u, ue, COL(BL, NS));
	pc(m, ue, s, ctx(m));
	if (te) wrap(m, s, s + 1, te - 1, te, COL(GR, NS), GR);
	pc(m, te ? te : s, e - 1, ctx(m));
	pc(m, e - 1, e, COL(OV, NS));
}

/* [text](url "title"), its URL runs of [^\s()] and balanced parentheses,
   given back whole from the last while the rest fails */
static size_t link_inline(Md *m, size_t p, size_t t)
{
	TsqlRd *r = m->x->r;
	if (at(m, t + 1) != '(') return 0;
	for (int lt = at(m, t + 2) == '<'; lt >= 0; lt--) {
		size_t us = t + 2 + (size_t)lt, b[64], n = 0, q = us, e, ue, te;
		/* the item ends; past 64 items the URL is taken whole */
		for (int c; q < m->eol && !is_sp(cp(r, q)); ) {
			if ((c = at(m, q)) == ')') break;
			if (c == '(') {
				size_t d = 0, j = q;
				for (; j < m->eol && !is_sp(cp(r, j)); j++)
					if (at(m, j) == '(') d++;
					else if (at(m, j) == ')' && !--d) break;
				if (j >= m->eol || at(m, j) != ')') break;
				q = j + 1;
			} else
				while (q < m->eol && !is_sp(cp(r, q)) && at(m, q) != '(' && at(m, q) != ')') q = adv(r, q);
			if (n < 64) b[n++] = q;
			else b[63] = q;
		}
		for (size_t i = n + 1; i--; ) {
			size_t u = i ? b[i - 1] : us;
			if (!(e = url_tail(m, u, &ue, &te))) continue;
			paint_link(m, p, t, us, u, ue, te, e);
			return e;
		}
	}
	return 0;
}

/* ![text](url "title"): the URL \S+?, the first end that fits */
static size_t image_inline(Md *m, size_t p, size_t t)
{
	size_t e, ue, te, us = t + 2 + (at(m, t + 2) == '<');
	if (at(m, t + 1) != '(') return 0;
	for (size_t u = us; u < m->eol && !is_sp(cp(m->x->r, u)); ) {
		u = adv(m->x->r, u);
		if (!(e = url_tail(m, u, &ue, &te))) continue;
		paint_link(m, p, t, us, u, ue, te, e);
		return e;
	}
	return 0;
}

/* <mailto:a@b.c> and <http://x>: (<)((?:mailto:)?[-.\w]+@[-a-z0-9]+
   (\.[-a-z0-9]+)*\.[a-z]+)(>) and (<)((?:https?|ftp)://.*?)(>) */
static size_t angle(Md *m, size_t p)
{
	TsqlRd *r = m->x->r;
	size_t q = p + 1, e = 0, n;
	if (at(m, p) != '<') return 0;
	if (lit(r, q, "http://") || lit(r, q, "https://") || lit(r, q, "ftp://"))
		for (e = q; e < m->eol && at(m, e) != '>'; e++) ;
	if (!e || e >= m->eol)
		for (int pre = !!lit(r, q, "mailto:"); pre >= 0; pre--) {
			size_t a = q + 7 * (size_t)pre, s = a, dots = 0, last = 0;
			unsigned c;
			while (s < m->eol && ((c = cpn(r, s, &n)) == '-' || c == '.' || is_w(c))) s += n;
			if (s == a || at(m, s) != '@') continue;
			for (e = ++s; (c = (unsigned)at(m, e)) == '-' || c == '.' || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); e++)
				if (c == '.') {
					if (e == s || at(m, e - 1) == '.') break;
					dots++, last = e;
				}
			/* labels, the last of letters */
			int ok = at(m, e) == '>' && dots && at(m, e - 1) != '.';
			for (size_t i = last + 1; ok && i < e; i++) ok = at(m, i) >= 'a' && at(m, i) <= 'z';
			if (ok) break;
			e = 0;
		}
	if (!e || at(m, e) != '>') return 0;
	wrap(m, p, p + 1, e, e + 1, COL(BL, NS), BL);
	return e + 1;
}

static size_t bold_italic(Md *m, size_t p, int bold);

/* after the ] at t, a space or none, then [ref]: its ], 0 for none; *b
   its [ */
static size_t ref(Md *m, size_t t, size_t *b)
{
	size_t e = *b = t + 1 + (at(m, t + 1) == ' ');
	if (at(m, e) != '[') return 0;
	while (++e < m->eol && at(m, e) != ']') ;
	return e < m->eol ? e : 0;
}

/* the inline rules at p (VS Code's #inline, and what bold and italic
   take: the same but italic in italic); its end, 0 for none */
static size_t inl(Md *m, size_t p, int italic)
{
	TsqlRd *r = m->x->r;
	size_t e, t;
	int c = at(m, p), d = at(m, p + 1);
	if ((c == '*' || c == '_') && ((e = bold_italic(m, p, 1)) || (italic && (e = bold_italic(m, p, 0))))) return e;
	if (c == '`') return raw(m, p, 1);
	if (c == '\\' && d > 0 && in((unsigned)d, "-`*_#+.!(){}[]\\>")) return pc(m, p, p + 2, inh(m, PI)), p + 2;
	if (c == '!' && d == '[' && (t = square(m, p + 2))) {
		if ((e = image_inline(m, p, t))) return e;
		/* ![text] [ref] */
		size_t b;
		if ((e = ref(m, t, &b))) {
			wrap(m, p, p + 2, t, t + 1, COL(GR, NS), GR);
			pc(m, t + 1, b, ctx(m)), wrap(m, b, b + 1, e, e + 1, COL(LA, NS), LA);
			return e + 1;
		}
	}
	if ((e = angle(m, p))) return e;
	if (c != '[') return 0;
	if ((t = square(m, p + 1))) {
		if ((e = link_inline(m, p, t))) return e;
		/* [text][ref], [text] [] */
		size_t b;
		if ((e = ref(m, t, &b)) && (b == t + 1 || e == b + 1)) {
			wrap(m, p, p + 1, t, t + 1, COL(GR, NS), LA);
			pc(m, t + 1, b, ctx(m)), wrap(m, b, b + 1, e, e + 1, COL(OV, NS), LA);
			return e + 1;
		}
	}
	/* [x]: (\[)(\S+?)(\]) */
	for (e = p + 1; e < m->eol && !is_sp(cp(r, e)); e = adv(r, e))
		if (e > p + 1 && at(m, e) == ']') {
			wrap(m, p, p + 1, e, e + 1, COL(GR, NS), LA);
			return e + 1;
		}
	return 0;
}

/* bold's and italic's lookahead: from s, items (an HTML tag, a code span,
   an escape, a link, for italic its delimiter doubled, any other code
   point but a closer) to a closer, as Oniguruma's possessive ++ takes
   them */
static int closes(Md *m, size_t s, int dc, int bold)
{
	TsqlRd *r = m->x->r;
	size_t q = s, items = 0, e, t;
	for (int c; q < m->eol; items++) {
		c = at(m, q);
		if (c == '<') {
			for (e = q + 1; e < m->eol && at(m, e) != '>'; e++) ;
			if (e < m->eol) {
				q = e + 1;
				continue;
			}
		}
		if (c == '`' && (e = raw(m, q, 0))) {
			q = e;
			continue;
		}
		if (c == '\\') {
			q += 1 + (in((unsigned)at(m, q + 1), "\\`*_{}[]()#.!+->") != 0);
			continue;
		}
		if (c == '[' && (t = square(m, q + 1))) {
			size_t b;
			if ((e = ref(m, t, &b))) {
				q = e + 1;
				continue;
			}
			if (at(m, t + 1) == '(') {
				/* \( [ \t]*+ <?(.*?)>? [ \t]*+ ((['"])(.*?)\3)? \) */
				size_t s2 = sp_tab(m, t + 2), k, done = 0;
				for (k = s2; !done && k < m->eol; k++)
					for (int gt = at(m, k) == '>'; !done && gt >= 0; gt--) {
						size_t e2 = sp_tab(m, k + (size_t)gt);
						int qu = at(m, e2);
						if (qu == '\'' || qu == '"')
							for (size_t j = e2 + 1; !done && j < m->eol; j++)
								if (at(m, j) == qu && at(m, j + 1) == ')') done = j + 2;
						if (!done && at(m, e2) == ')') done = e2 + 1;
					}
				if (done) {
					q = done;
					continue;
				}
			}
		}
		if (!bold && c == dc && at(m, q + 1) == dc) {
			q += 2;
			continue;
		}
		if (!is_sp(before(r, q)) && c == dc && (!bold || at(m, q + 1) == dc)) break;
		q = adv(r, q);
	}
	if (!items || q >= m->eol) return 0;
	/* (?<=\S)(?=_\b|\*)\1, for bold (?=__\b|\*\*)\1 */
	return dc == '*' || !is_w(cp(r, q + 1 + (size_t)bold));
}

/* **bold** or *italic* beginning at p: (\*\*(?=\w)|(?<!\w)\*\*|(?<!\w)\b__)
   (?=\S) and its closer ahead; its delimiter's end */
static size_t bold_italic(Md *m, size_t p, int bold)
{
	TsqlRd *r = m->x->r;
	int dc = at(m, p);
	size_t e = p + 1 + (size_t)bold;
	if (bold && at(m, p + 1) != dc) return 0;
	if (dc == '_' ? is_w(before(r, p)) : is_w(before(r, p)) && !is_w(cp(r, e))) return 0;
	if (is_sp(cp(r, e)) || m->nin == (int)sizeof m->in || !closes(m, e, dc, bold)) return 0;
	pc(m, p, e, COL(OV, NS));
	m->in[m->nin++] = (unsigned char)(dc + 128 * bold);
	m->g = e;
	return e;
}

/* an ATX heading at p (^ or \G): [ ]{0,3}(#{1,6})\s+ and its text to a -/
   or the line's end; then heading.N on it: the #s in the heading's
   colour, the text in it and the comment's italic, and a run of # that
   ends the text after a blank, (\s+#+)$, in the colour */
static size_t heading(Md *m, size_t p)
{
	TsqlRd *r = m->x->r;
	size_t h = p, n = 0, s, e, t;
	while (h < p + 3 && at(m, h) == ' ') h++;
	while (at(m, h + n) == '#') n++;
	if (!n || n > 6 || !is_sp(cp(r, h + n)) || h + n >= m->eol) return 0;
	for (e = h + n; e < m->eol && !(at(m, e) == '-' && at(m, e + 1) == '/'); e++) ;
	int col = md_h[n - 1];
	pc(m, p, e, ctx(m));
	/* heading.N's \s+(?=[\S[^#]]) needs a character after it, before e,
	   which at the line's end is the newline the capture took */
	if ((s = ws(m, h + n)) >= e && (e < m->eol ? (s = back(r, e)) <= h + n : (s = e, 0))) return e;
	pc(m, h, h + n, COL(col, NS));
	pc(m, h + n, e, COL(col, IT));
	for (t = e; t > s && at(m, t - 1) == '#'; t--) ;
	if (t < e && t > s && is_sp(before(r, t)) && back(r, t) > s && !is_sp(before(r, back(r, t))))
		pc(m, back(r, t), e, COL(col, NS));
	else if (t < e && t > s && is_sp(before(r, t)) && back(r, t) > s) {
		/* blanks before the run: \s* keeps all but the last */
		size_t b = back(r, t);
		while (b > s && is_sp(before(r, b))) b = back(r, b);
		if (b > s) pc(m, back(r, t), e, COL(col, NS));
	}
	return e;
}

/* (^|\G)[ ]{0,3}([\*\-\_])([ ]{0,2}\2){2,}[ \t]*$ */
static size_t separator(Md *m, size_t p)
{
	size_t q = p, k = 0;
	while (q < p + 3 && at(m, q) == ' ') q++;
	int c = at(m, q);
	if (c != '*' && c != '-' && c != '_') return 0;
	for (size_t j = q + 1; ; k++, q = j, j++) {
		for (j = q + 1; j < q + 3 && at(m, j) == ' '; j++) ;
		if (at(m, j) != c) break;
	}
	if (k < 2 || sp_tab(m, q + 1) < m->eol) return 0;
	return pc(m, p, m->eol, inh(m, TE)), m->eol;
}

/* [name]: url "title", anywhere on a line it ends:
   \s*(\[)([^]]+?)(\])(:)[ \t]*(<?)(\S+?)(>?)[ \t]*(title)?\s*$ */
static size_t link_def(Md *m, size_t p)
{
	TsqlRd *r = m->x->r;
	size_t b = ws(m, p), t, u0, te = 0;
	if (at(m, b) != '[') return 0;
	for (t = b + 1; t < m->eol && at(m, t) != ']'; t++) ;
	if (t == b + 1 || t >= m->eol || at(m, t + 1) != ':') return 0;
	u0 = sp_tab(m, t + 2);
	size_t us = u0 + (at(m, u0) == '<');
	for (size_t u = us; u < m->eol && !is_sp(cp(r, u)); ) {
		u = adv(r, u);
		for (int gt = at(m, u) == '>'; gt >= 0; gt--) {
			size_t ue = u + (size_t)gt, s = sp_tab(m, ue);
			if (!title(m, s, 0, &te)) te = 0;
			if (te || ws(m, s) >= m->eol) {
				pc(m, p, b, ctx(m)), wrap(m, b, b + 1, t, t + 1, COL(LA, NS), LA);
				pc(m, t + 1, t + 2, COL(TE, NS)), pc(m, t + 2, u0, ctx(m)), pc(m, u0, us, COL(BL, NS));
				pc(m, us, u, inh(m, BL)), pc(m, u, ue, COL(BL, NS)), pc(m, ue, m->eol, ctx(m));
				if (te) wrap(m, s, s + 1, te - 1, te, COL(GR, NS), GR);
				return m->eol;
			}
		}
	}
	return 0;
}

/* a setext heading's ===: ^(={3,})(?=[ \t]*$) */
static size_t setext(Md *m, size_t q)
{
	size_t e = q;
	while (at(m, e) == '=') e++;
	if (e < q + 3 || sp_tab(m, e) < m->eol) return 0;
	return pc(m, q, e, inh(m, RE)), e;
}

static size_t line_end(TsqlRd *r, size_t p)
{
	int c;
	while ((c = tsql_byte(r, p)) >= 0 && c != '\n') p++;
	return p;
}

/* a while rule at a line's start: a quote's (^|\G)\s*(>) ?, a list's
   ((^|\G)(?!.*-/)([ ]{2,4}|\t))|(^[ \t]*$); a fence ends on
   (^|\G)(\2|\s{0,3})(\3)\s*(?=-/|$) and holds every other line */
static size_t md_line(Md *m)
{
	TsqlRd *r = m->x->r;
	size_t q = m->ls, e, i;
	if (m->blk == Q && at(m, e = ws(m, q)) == '>') {
		size_t k = e + 1 + (at(m, e + 1) == ' ');
		pc(m, q, e, COL(PI, IT)), pc(m, e, e + 1, COL(PI, NS)), pc(m, e + 1, k, COL(PI, IT));
		return m->g = k;
	}
	if (m->blk == L) {
		for (e = q; e + 1 < m->eol && !(at(m, e) == '-' && at(m, e + 1) == '/'); e++) ;
		for (i = q; i < q + 4 && at(m, i) == ' '; i++) ;
		if (e + 1 >= m->eol && (i >= q + 2 || at(m, q) == '\t')) i = i < q + 2 ? q + 1 : i;
		else if ((i = sp_tab(m, q)) < m->eol) i = 0;
		if (i) return pc(m, q, i, COL(OV, IT)), m->g = i;
	}
	if (m->blk == F) {
		for (int alt = 0; alt < 2; alt++) {
			size_t n = 0, k = 0;
			if (!alt) {
				while (n < m->fn && at(m, q + n) == at(m, m->fi + n)) n++;
				if (n < m->fn) continue;
				i = q + n;
			} else
				for (i = q; k < 3 && i < m->eol && is_sp(cp(r, i)); k++) i = adv(r, i);
			for (n = 0; n < m->fk && at(m, i + n) == m->fc; n++) ;
			e = ws(m, i + n);
			if (n < m->fk || !(e >= m->eol || (at(m, e) == '-' && at(m, e + 1) == '/'))) continue;
			pc(m, q, i, COL(OV, IT)), pc(m, i, i + n, COL(OV, NS)), pc(m, i + n, e, COL(OV, IT));
			m->blk = 0;
			return e;
		}
		return pc(m, q, m->eol, COL(OV, IT)), m->eol;
	}
	m->blk = 0;
	return q;
}

/* at a line's start in the comment: a quote, a list or a fence begins */
static size_t md_begin(Md *m)
{
	size_t q = m->ls, i = q, j, k;
	int c;
	while (i < q + 3 && at(m, i) == ' ') i++;
	if (at(m, i) == '>') {
		k = i + 1 + (at(m, i + 1) == ' ');
		pc(m, q, i, COL(PI, IT)), pc(m, i, i + 1, COL(PI, NS)), pc(m, i + 1, k, COL(PI, IT));
		m->blk = Q;
		return m->g = k;
	}
	/* ([ ]{0,3})([*+-]|[0-9]+\.)([ \t])(?!.*-/) */
	if (in((unsigned)at(m, i), "*+-")) j = i + 1;
	else {
		for (j = i; at(m, j) >= '0' && at(m, j) <= '9'; j++) ;
		j = j > i && at(m, j) == '.' ? j + 1 : 0;
	}
	if (j && (at(m, j) == ' ' || at(m, j) == '\t')) {
		for (k = j + 1; k + 1 < m->eol && !(at(m, k) == '-' && at(m, k + 1) == '/'); k++) ;
		if (k + 1 >= m->eol) {
			pc(m, q, i, COL(OV, IT)), pc(m, i, j, COL(TE, NS)), pc(m, j, j + 1, COL(OV, IT));
			m->blk = L;
			return m->g = j + 1;
		}
	}
	/* (\s*)(`{3,}|~{3,})\s*(?=([^`~]*)?$) */
	i = ws(m, q);
	c = at(m, i);
	for (j = i; (c == '`' || c == '~') && at(m, j) == c; j++) ;
	if (j < i + 3) return 0;
	for (k = ws(m, j); k < m->eol && at(m, k) != '`' && at(m, k) != '~'; k++) ;
	if (k < m->eol) return 0;
	k = ws(m, j);
	pc(m, q, i, COL(OV, IT)), pc(m, i, j, COL(OV, NS)), pc(m, j, k, COL(OV, IT)), pc(m, k, m->eol, COL(SK, IT));
	m->blk = F, m->fi = q, m->fn = i - q, m->fc = c, m->fk = j - i;
	return m->eol;
}

/* a comment from p (-- to the line's end, or /- to its -/, nested; /--
   and /-! the same), Markdown in it; 0 when none starts at p */
static size_t comment(Lx *x, size_t p)
{
	TsqlRd *r = x->r;
	int c = tsql_byte(r, p), d = tsql_byte(r, p + 1), dash = c == '-' && d == '-';
	size_t q = p + 2, depth = 1, e;
	if (!dash && (c != '/' || d != '-')) return 0;
	q += !dash && (tsql_byte(r, q) == '-' || tsql_byte(r, q) == '!');
	if (x->probe) return q;
	/* the first line has no ^: the comment's start stands there */
	Md m = { .x = x, .ls = p, .eol = line_end(r, q), .g = q };
	pc(&m, p, q, COL(OV, IT));
	while (!(x->f && q >= x->z)) {
		if (q >= m.eol) {
			m.nin = 0;
			if (dash || tsql_byte(r, q) < 0) return q;
			m.ls = ++q, m.eol = line_end(r, q), m.g = SIZE_MAX;
			if (m.blk) q = md_line(&m);
			continue;
		}
		int dc = m.nin ? m.in[m.nin - 1] % 128 : 0, bold = m.nin && m.in[m.nin - 1] >= 128;
		if (m.nin) {
			/* bold's end (?<=\S)(\1)|(?=$|-/), italic's (?<=\S)(\1)((?!\1)|(?=\1\1))|(?=$|-/) */
			if (at(&m, q) == '-' && at(&m, q + 1) == '/') {
				m.nin--, m.g = SIZE_MAX;
				continue;
			}
			if (!is_sp(before(r, q)) && at(&m, q) == dc && (bold ? at(&m, q + 1) == dc : at(&m, q + 1) != dc || at(&m, q + 2) == dc)) {
				pc(&m, q, q + 1 + (size_t)bold, COL(OV, NS));
				q += 1 + (size_t)bold, m.nin--, m.g = SIZE_MAX;
				continue;
			}
			if ((e = inl(&m, q, bold))) {
				q = e;
				continue;
			}
		} else if (m.blk == F)
			q = md_line(&m);
		else if (!dash && !m.blk && at(&m, q) == '-' && at(&m, q + 1) == '/') {
			pc(&m, q, q + 2, COL(OV, IT));
			q += 2, m.g = SIZE_MAX;
			if (!--depth) return q;
			continue;
		} else if (((q == m.ls || q == m.g) && ((e = separator(&m, q)) || (e = heading(&m, q)))) ||
		           (q == m.ls && !m.blk && (e = md_begin(&m))) || (e = link_def(&m, q)) || (e = inl(&m, q, 1))) {
			q = e;
			continue;
		} else if (q == m.ls && (e = setext(&m, q))) {
			q = e;
			continue;
		} else if (!dash && !m.blk && at(&m, q) == '/' && at(&m, q + 1) == '-') {
			pc(&m, q, q + 2, COL(OV, IT));
			q += 2, depth++, m.g = q;
			continue;
		}
		pc(&m, q, e = adv(r, q), ctx(&m));
		q = e;
	}
	return q;
}

/* «..» from p, its content in face c; 0 when none starts at p */
static size_t guillemets(Lx *x, size_t p, unsigned char b, unsigned char c)
{
	size_t q = p, n;
	if (cpn(x->r, q, &n) != 0xab) return 0;
	put(x, p, q += n, b);
	if (x->probe) return q;
	while (n && cpn(x->r, q, &n) != 0xbb) put(x, q, q + n, c), q += n;
	put(x, q, q + n, b);
	return q + n;
}

/* a definition's head, after its keyword: the names it defines, up to
   with, extends, where or one of : | ( [ { ⦃ < > */
static size_t head(Lx *x, size_t q, unsigned char b)
{
	TsqlRd *r = x->r;
	size_t e, n;
	for (unsigned c; (c = cpn(r, q, &n)), n; ) {
		if (in(c, ":|([{<>") || c == 0x2983) break;
		if (wb(r, q) && (((e = lit(r, q, "with")) || (e = lit(r, q, "extends")) || (e = lit(r, q, "where"))) && wb(r, e))) break;
		if ((e = comment(x, q)) || (e = guillemets(x, q, b, FACE_FUNCTION_DEFINITION))) {
			q = e;
			continue;
		}
		if (wb(r, q) && !in(c, ":(){}=?") && c != 0xab && c != 0xbb && c != 0x2192 && c != 0x3bb && c != 0x2200 && !is_sp(c)) {
			/* \b[^:«»(){}\s=→λ∀?][^:«»(){}\s]* */
			for (e = q + n; (c = cpn(r, e, &n)), n && !in(c, ":(){}") && c != 0xab && c != 0xbb && !is_sp(c); ) e += n;
			put(x, q, e, FACE_FUNCTION_DEFINITION);
			q = e;
			continue;
		}
		put(x, q, q + n, b);
		q += n;
	}
	return q;
}

/* a number: \b([0-9]+|0([xX][0-9a-fA-F]+)|[-]?(0|[1-9][0-9]*)(\.[0-9]+)?
   ([eE][+-]?[0-9]+)?)\b, its alternatives and optional parts tried in
   Oniguruma's order */
static size_t digits(TsqlRd *r, size_t p)
{
	while (tsql_byte(r, p) >= '0' && tsql_byte(r, p) <= '9') p++;
	return p;
}

static size_t exponent(TsqlRd *r, size_t p)
{
	int c = tsql_byte(r, p);
	size_t e = p + 1;
	if (c != 'e' && c != 'E') return p;
	if (tsql_byte(r, e) == '+' || tsql_byte(r, e) == '-') e++;
	return digits(r, e) > e ? digits(r, e) : p;
}

static size_t number(TsqlRd *r, size_t p)
{
	size_t e = digits(r, p), i, f;
	int c = tsql_byte(r, p), d = tsql_byte(r, p + 1);
	if (e > p && wb(r, e)) return e;
	if (c == '0' && (d == 'x' || d == 'X')) {
		for (e = p + 2; isxdigit(tsql_byte(r, e)); e++) ;
		if (e > p + 2 && wb(r, e)) return e;
	}
	i = p + (c == '-');
	c = tsql_byte(r, i);
	if (c == '0') i++;
	else if (c >= '1' && c <= '9') i = digits(r, i);
	else return 0;
	f = tsql_byte(r, i) == '.' && digits(r, i + 1) > i + 1 ? digits(r, i + 1) : i;
	if (wb(r, e = exponent(r, f))) return e;
	if (wb(r, f)) return f;
	return f > i && wb(r, e = exponent(r, i)) ? e : 0;
}

/* a character: (?<!\]|\w)'[^\\']' or (?<!\]|\w)'(\\(x..|u....|.))' */
static size_t character(Lx *x, size_t p)
{
	TsqlRd *r = x->r;
	unsigned b = before(r, p), c;
	size_t n, e = p + 1, k;
	if (tsql_byte(r, p) != '\'' || b == ']' || is_w(b)) return 0;
	if (tsql_byte(r, e) != '\\') {
		c = cpn(r, e, &n);
		if (!n || c == '\'' || tsql_byte(r, e + n) != '\'') return 0;
		return put(x, p, e + n + 1, FACE_STRING), e + n + 1;
	}
	c = (unsigned)tsql_byte(r, e + 1);
	if (c == 'x' && hexes(r, e + 2, 2) && tsql_byte(r, e + 4) == '\'') k = e + 4;
	else if (c == 'u' && hexes(r, e + 2, 4) && tsql_byte(r, e + 6) == '\'') k = e + 6;
	else if ((c = cpn(r, e + 1, &n)) == '\n' || tsql_byte(r, k = e + 1 + n) != '\'') return 0;
	put(x, p, k + 1, FACE_STRING);
	put(x, e, k, FACE_STRING_ESCAPE);
	return k + 1;
}

/* attribute [..] and @[..]: with ] all of it, else [ is plain */
static size_t attribute(Lx *x, size_t p, size_t e, unsigned char b)
{
	size_t k = unbracket(x->r, e + 1);
	if (tsql_byte(x->r, e) != '[') return 0;
	if (tsql_byte(x->r, k) == ']') return put(x, p, k + 1, FACE_KEYWORD), k + 1;
	put(x, p, e, FACE_KEYWORD);
	put(x, e, e + 1, b);
	put(x, e + 1, k, FACE_KEYWORD);
	return k;
}

/* the string after its ", to its ", escapes in it */
static size_t string(Lx *x, size_t q)
{
	size_t e;
	int c;
	while ((c = tsql_byte(x->r, q)) >= 0 && c != '"')
		if ((e = esc(x->r, q))) put(x, q, e, FACE_STRING_ESCAPE), q = e;
		else put(x, q, q + 1, FACE_STRING), q++;
	return put(x, q, q + 1, FACE_STRING), q + (c >= 0);
}

/* the grammar's rules at p, base the face of what they leave plain (green
   in an interpolated string's braces): the end of the match and, through
   *istr, 1 when it opens an interpolated string; 0 when none matches */
static size_t rule(Lx *x, size_t p, unsigned char b, int *istr)
{
	TsqlRd *r = x->r;
	size_t e, k;
	int c = tsql_byte(r, p);
	*istr = 0;
	if ((e = comment(x, p))) return e;
	if (wb(r, p) && (e = alt(r, p, lean_types, N(lean_types), 0))) return put(x, p, e, FACE_KEYWORD), e;
	if (wb(r, p) && (e = lit(r, p, "attribute")) && wb(r, e) && (k = attribute(x, p, blank(r, e), b))) return k;
	if (c == '@' && (k = attribute(x, p, p + 1, b))) return k;
	if (word_start(r, p) && (e = alt(r, p, lean_mods, N(lean_mods), 1))) return put(x, p, e, FACE_KEYWORD), e;
	/* sorry, admit and #exit (invalid.illegal) take no colour, and no rule
	   after them could match inside them */
	if (c == '#' && (e = alt(r, p + 1, lean_cmds, N(lean_cmds), 0))) return put(x, p, e, FACE_KEYWORD), e;
	if (wb(r, p) && (e = lit(r, p, "deriving")) && is_sp(cp(r, e)) && cp(r, e) != '\n' && (e = lit(r, blank(r, e), "instance")) && wb(r, e))
		return put(x, p, e, FACE_KEYWORD), e;
	if (word_start(r, p) && (e = alt(r, p, lean_defs, N(lean_defs), 0)) && is_sp(cp(r, e))) {
		/* \s+(\{[^}]*\})?, its \s+ to the line's '\n' at most: the head
		   goes on on the next line */
		size_t n, j;
		put(x, p, e, FACE_KEYWORD);
		for (k = e; is_sp(c = (int)cpn(r, k, &n)) && n; k += n)
			if (c == '\n') {
				k += n;
				break;
			}
		if (tsql_byte(r, k) == '{') {
			for (j = k + 1; (c = tsql_byte(r, j)) >= 0 && c != '}' && c != '\n'; j++) ;
			if (c == '}') k = j + 1;
		}
		put(x, e, k, b);
		return x->probe ? k : head(x, k, b);
	}
	if (word_start(r, p) && (e = alt(r, p, lean_kws, N(lean_kws), 1))) return put(x, p, e, FACE_KEYWORD), e;
	if ((e = guillemets(x, p, b, b))) return e;
	for (size_t i = 0; i < N(lean_istr); i++)
		if ((e = bracket(r, lit(r, p, lean_istr[i]))) && tsql_byte(r, k = blank(r, e)) == '"') {
			put(x, p, e, FACE_KEYWORD);
			put(x, e, k + 1, FACE_STRING);
			*istr = 1;
			return k + 1;
		}
	if (c == '"') return put(x, p, p + 1, FACE_STRING), x->probe ? p + 1 : string(x, p + 1);
	if (wb(r, p) && (e = alt(r, p, lean_bools, N(lean_bools), 0))) return put(x, p, e, FACE_VARIABLE_BUILTIN), e;
	if ((e = character(x, p))) return e;
	if (wb(r, p) && (e = number(r, p))) return put(x, p, e, FACE_NUMBER), e;
	return 0;
}

size_t lean_lex(TsqlRd *r, size_t p, size_t a, size_t z, unsigned char *f)
{
	Lx x = { r, a, z, f, 0 }, t = { r, 0, 0, NULL, 1 };
	int open;
	size_t q = rule(&x, p, FACE_NONE, &open), e, ib = open;
	if (!q) {
		/* plain up to where a rule matches */
		for (q = adv(r, p); tsql_byte(r, q) >= 0 && !rule(&t, q, FACE_NONE, &open); q = adv(r, q)) ;
		return put(&x, p, q, FACE_NONE), q;
	}
	/* in an interpolated string (ib odd) or its braces (ib even), nested
	   ib / 2 deep; counted, as a pile of them must not take the stack */
	for (int c; ib && (c = tsql_byte(r, q)) >= 0 && !(f && q >= z); ) {
		if (ib % 2) {
			if (c == '"') put(&x, q, q + 1, FACE_STRING), q++, ib--;
			else if (c == '{') put(&x, q, q + 1, FACE_KEYWORD), q++, ib++;
			else if ((e = esc(r, q))) put(&x, q, e, FACE_STRING_ESCAPE), q = e;
			else put(&x, q, q + 1, FACE_STRING), q++;
		} else if (c == '}') put(&x, q, q + 1, FACE_KEYWORD), q++, ib--;
		else if ((e = rule(&x, q, FACE_STRING, &open))) q = e, ib += (size_t)open;
		else put(&x, q, e = adv(r, q), FACE_STRING), q = e;
	}
	return q;
}
