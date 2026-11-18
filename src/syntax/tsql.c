/* T-SQL colours as SQL Server Management Studio's query editor gives them
   (Davy, 2026-11-18: SSMS, not Neovim, is the reference for T-SQL, whose
   tree-sitter grammar fails on DECLARE, [names], GO, BEGIN/END and
   procedures).  SSMS colours by word lists, not by a parse, so a statement
   it does not know never spoils the colours after it.  Its categories
   (learn.microsoft.com, "Color coding in query editors") map to Catppuccin's
   faces: keyword, string, comment, number, system function, system table,
   view or schema, stored procedure, operator (SSMS's grey for AND, EXISTS,
   ( and =, so Catppuccin's grey: its punctuation's overlay2, as Davy's
   SSMS screenshot of 2026-11-18 19:03 shows them muted); names and
   variables stay plain, as there.  Faces are read from the nearest known
   token start before the bytes asked; syntax_update keeps one every CK
   bytes to the text's end, and an edit keeps those after it until a read
   confirms them (struct Tsql). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "grid.h"
#include "tsql.h"
#include "tsql_words.h"

enum { CK = 4096, FAR = 4 * CK, NWORDS = sizeof words / sizeof *words };

int tsql_byte(TsqlRd *r, size_t i)
{
	if (i - r->a >= r->n) {
		r->a = i;
		r->n = text_read(r->t, i, r->b, sizeof r->b);
		if (!r->n) return -1;
	}
	return (unsigned char)r->b[i - r->a];
}

struct Tsql {
	TsqlRd r;
	/* Token starts, ascending, about CK apart, and the text's end once the
	   index reaches it.  ck[0..v) are known.  ck[v..n) were token starts
	   before the last edit, all after it, moved with the text: the lexer
	   keeps no state between tokens, so once a read from a known start
	   lands on one of them, the text after lexes as before and all are
	   known again (Emacs's syntax-ppss cache keeps positions this way, but
	   drops them; here they come back). */
	uint32_t *ck, n, v, cap;
	size_t at, end;            /* the token where the last faces ended; its
	                              end, 0 when not known */
	int kind;                  /* and its kind, so the frame's later reads
	                              inside a long comment do not lex it again */
	int skipped;               /* a window was left plain, far from the index */
	int json;                  /* JSON's lexer instead (json.c) */
};

static int is_space(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
static int is_digit(int c) { return c >= '0' && c <= '9'; }
/* SQL Server's identifier characters (and UTF-8's bytes) */
static int is_id(int c)
{
	return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '@' || c == '#' || c == '$' || c >= 0x80;
}

size_t tsql_token(TsqlRd *r, size_t p, int *k)
{
	int c = tsql_byte(r, p), d = tsql_byte(r, p + 1);
	r->open = 0;
	if (c < 0) return *k = TQ_SPACE, p;
	if (is_space(c)) {
		while (is_space(tsql_byte(r, ++p))) ;
		return *k = TQ_SPACE, p;
	}
	if (c == '-' && d == '-') {
		while ((c = tsql_byte(r, ++p)) >= 0 && c != '\n' && c != '\r') ;
		return *k = TQ_COMMENT, p;
	}
	if (c == '/' && d == '*') {
		/* SQL Server nests them */
		int depth = 1;
		for (p += 2; depth && (c = tsql_byte(r, p)) >= 0; ) {
			d = tsql_byte(r, p + 1);
			if (c == '/' && d == '*') depth++, p += 2;
			else if (c == '*' && d == '/') depth--, p += 2;
			else p++;
		}
		r->open = depth;
		return *k = TQ_COMMENT, p;
	}
	if ((c == 'N' || c == 'n') && d == '\'') c = '\'', p++;
	if (c == '\'' || c == '"' || c == '[') {
		/* the closer doubled stands for itself; unclosed runs to the end */
		int e = c == '[' ? ']' : c;
		*k = c == '\'' ? TQ_STRING : TQ_IDENT;
		while ((c = tsql_byte(r, ++p)) >= 0)
			if (c == e && tsql_byte(r, ++p) != e) break;
		r->open = c < 0;
		return p;
	}
	if (is_digit(c) || (c == '.' && is_digit(d))) {
		/* as SQL Server: letters after a number start a word (1as x is 1 as
		   x); 0x then hex digits; digits, a dot (not ..), digits; an
		   exponent only when a digit follows its e and sign */
		*k = TQ_NUMBER;
		if (c == '0' && (d == 'x' || d == 'X')) {
			for (p += 2; is_digit(c = tsql_byte(r, p)) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); p++) ;
			return p;
		}
		while (is_digit(tsql_byte(r, p))) p++;
		if (tsql_byte(r, p) == '.' && tsql_byte(r, p + 1) != '.')
			while (is_digit(tsql_byte(r, ++p))) ;
		if ((c = tsql_byte(r, p)) == 'e' || c == 'E') {
			size_t e = p + 1;
			if ((c = tsql_byte(r, e)) == '+' || c == '-') e++;
			if (is_digit(tsql_byte(r, e)))
				for (p = e; is_digit(tsql_byte(r, p)); p++) ;
		}
		return p;
	}
	if (c == '@' || c == '#') {
		while (is_id(tsql_byte(r, ++p))) ;
		return *k = TQ_VAR, p;
	}
	if (is_id(c)) {
		while (is_id(tsql_byte(r, ++p))) ;
		return *k = TQ_WORD, p;
	}
	if (c == ',' || c == '(' || c == ')' || c == ';' || c == '.') return *k = TQ_PUNCT, p + 1;
	*k = TQ_OP;
	/* <> != >= <= !< !> and the compound assignments, << >> :: || */
	if ((d == '=' && c && strchr("<>!+-*/%&|^", c)) || ((c == '<' || c == '>') && d == c) || (c == '<' && d == '>') || (c == '!' && (d == '<' || d == '>')) || (c == ':' && d == ':') || (c == '|' && d == '|'))
		return p + 2;
	return p + 1;
}

static int low(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

const size_t tsql_nwords = NWORDS;

/* a lower-case word's index in words[] plus 1, 0 when it is not there */
static size_t lookup(const char *l)
{
	size_t lo = 0, hi = NWORDS;
	while (lo < hi) {
		size_t m = lo + (hi - lo) / 2;
		int c = strcmp(words[m], l);
		if (!c) return m + 1;
		if (c < 0) lo = m + 1;
		else hi = m;
	}
	return 0;
}

int tsql_word(const char *w, size_t n, size_t *id)
{
	char l[40];
	size_t k = 0;
	if (n < sizeof l) {
		for (size_t i = 0; i < n; i++) l[i] = (char)low((unsigned char)w[i]);
		l[n] = 0;
		k = lookup(l);
	}
	if (id) *id = k;
	return k ? classes[k - 1] : 0;
}

/* the n bytes at p into l, lower case; 0 when they do not fit (no word in
   the lists is that long) */
static int copy(TsqlRd *r, size_t p, size_t n, char l[40])
{
	if (n >= 40) return 0;
	for (size_t i = 0; i < n; i++) l[i] = (char)low(tsql_byte(r, p + i));
	l[n] = 0;
	return 1;
}

static int is_schema(const char *l) { return !strcmp(l, "sys") || !strcmp(l, "information_schema"); }

/* the name before the dot at p is a system schema */
static int after_schema(TsqlRd *r, size_t p)
{
	char l[40];
	size_t b = p;
	while (b && p - b < 40 && is_id(tsql_byte(r, b - 1))) b--;
	return copy(r, b, p - b, l) && is_schema(l);
}

/* SSMS colours by word lists alone, so a word in two lists takes one
   colour wherever it is: LEFT JOIN and LEFT( are operators, and the day of
   DATEADD(day, ..) is a function, as DAY( is (Davy's screenshots) */
static unsigned char word_face(TsqlRd *r, size_t p, size_t e)
{
	char l[40];
	if (!copy(r, p, e - p, l)) return FACE_NONE;
	/* the system schemas and what is in them (SSMS's system table colour) */
	if (is_schema(l) || (p && tsql_byte(r, p - 1) == '.' && after_schema(r, p - 1))) return FACE_MODULE_BUILTIN;
	/* SQL Server's own stored procedures (hypothesis: by their sp_ and xp_) */
	if ((l[0] == 's' || l[0] == 'x') && l[1] == 'p' && l[2] == '_' && l[3]) return FACE_FUNCTION;
	size_t id = lookup(l);
	int c = id ? classes[id - 1] : 0;
	return c & TQ_OPERATOR ? FACE_PUNCTUATION_DELIMITER : c & TQ_FUNCTION ? FACE_FUNCTION_BUILTIN : c & TQ_KEYWORD ? FACE_KEYWORD : FACE_NONE;
}

static unsigned char face(TsqlRd *r, size_t p, size_t e, int k)
{
	switch (k) {
	case TQ_COMMENT: return FACE_COMMENT;
	case TQ_STRING: return FACE_STRING;
	/* a dot is plain, but in sys.objects it is the system table's (SSMS,
	   Davy's screenshot of 2026-11-18) */
	case TQ_PUNCT: return tsql_byte(r, p) != '.' ? FACE_PUNCTUATION_DELIMITER : after_schema(r, p) ? FACE_MODULE_BUILTIN : FACE_NONE;
	/* numbers are SSMS's light green (Davy's screenshot) */
	case TQ_NUMBER: return FACE_NUMBER;
	case TQ_OP: return FACE_PUNCTUATION_DELIMITER;
	case TQ_WORD: return word_face(r, p, e);
	/* @@ROWCOUNT and the other globals are system functions; @x is plain */
	case TQ_VAR: return tsql_byte(r, p + 1) == '@' ? word_face(r, p, e) : FACE_NONE;
	}
	return FACE_NONE;
}

static void paint(TsqlRd *r, size_t p, size_t e, int k, size_t a, size_t z, unsigned char *f)
{
	unsigned char c = face(r, p, e, k);
	for (size_t i = p < a ? a : p; i < e && i < z; i++) f[i - a] = c;
}

Tsql *tsql_new(const Text *t, int json)
{
	Tsql *q = calloc(1, sizeof *q);
	if (q) q->r.t = t, q->json = json;
	return q;
}

void tsql_free(Tsql *q)
{
	if (q) free(q->ck);
	free(q);
}

/* p is a token start, reached by a read from one */
static void step(Tsql *q, size_t p)
{
	/* a moved start p passed is no token start now */
	while (q->v < q->n && q->ck[q->v] < p) q->n--, memmove(q->ck + q->v, q->ck + q->v + 1, (q->n - q->v) * sizeof *q->ck);
	if (q->v < q->n && q->ck[q->v] == p) q->v = q->n;
	else if (p >= (q->v ? q->ck[q->v - 1] + (size_t)CK : CK) && q->n < q->cap) {
		memmove(q->ck + q->v + 1, q->ck + q->v, (q->n - q->v) * sizeof *q->ck);
		q->ck[q->v++] = (uint32_t)p, q->n++;
	}
}

static uint32_t first_at(const Tsql *q, uint32_t hi, size_t p)
{
	uint32_t lo = 0;
	while (lo < hi) {
		uint32_t m = lo + (hi - lo) / 2;
		if (q->ck[m] < p) lo = m + 1;
		else hi = m;
	}
	return lo;
}

void tsql_edit(Tsql *q, const Text *t, size_t pos, size_t ndel, size_t nins)
{
	/* a token start depends on the bytes before it and on the two from it,
	   which can end the token before (a word, a doubled quote, "<>", the
	   e and sign of 1e+3) */
	size_t lo = pos > 2 ? pos - 2 : 0;
	uint32_t i = first_at(q, q->n, lo);
	if (i > q->v) q->n = i;    /* among moved starts: those after it go */
	else {
		/* moved starts of an older edit would pass this one unchecked */
		uint32_t j = first_at(q, q->n = q->v, pos + ndel);
		for (uint32_t k = j; k < q->n; k++) q->ck[i + k - j] = (uint32_t)(q->ck[k] + nins - ndel);
		q->n -= j - i;
		q->v = i;
	}
	if (q->at >= lo) q->at = q->v ? q->ck[q->v - 1] : 0;
	/* the token's end, as any start, depends on the two bytes after it */
	if (q->end + 2 >= pos) q->end = 0;
	q->r.t = t;
	q->r.n = 0;
}

int tsql_update(Tsql *q, int (*stop)(void))
{
	size_t len = text_len(q->r.t), want = len / CK + 3;  /* and an edit's join, and the end */
	if (want > q->cap) {
		uint32_t *ck = realloc(q->ck, want * sizeof *ck);
		if (!ck) return 0;
		q->ck = ck;
		q->cap = (uint32_t)want;
	}
	/* the index, filled to the text's end here and not in the redraw */
	size_t p = q->v ? q->ck[q->v - 1] : 0;
	for (unsigned k = 1; p < len; k++) {
		if (stop && !(k % 1024) && stop()) return 2;
		int kind;
		/* the long comment the frame just read is not read again */
		size_t e = p == q->at && q->end > p ? q->end : (q->json ? json_token : tsql_token)(&q->r, p, &kind);
		step(q, p);
		/* landed on a moved start: the rest is known */
		p = q->n && q->v == q->n && q->ck[q->n - 1] > e ? q->ck[q->n - 1] : e;
	}
	step(q, len);
	if (q->v == q->n && (!q->n || q->ck[q->n - 1] < len) && q->n < q->cap) q->ck[q->n++] = (uint32_t)len, q->v = q->n;
	int r = q->skipped;
	q->skipped = 0;
	return r;
}

void tsql_faces(Tsql *q, size_t a, size_t z, unsigned char *f)
{
	size_t p = q->at <= a ? q->at : 0;
	uint32_t i = first_at(q, q->v, a + 1);
	if (i && q->ck[i - 1] > p) p = q->ck[i - 1];
	/* far from the index while syntax_update has not filled it: plain
	   now, coloured after it (as Neovim draws before a parse ends) */
	if (a - p > FAR && !(q->v == q->n && q->n && q->ck[q->n - 1] == text_len(q->r.t))) {
		q->skipped = 1;
		return;
	}
	while (p < z) {
		int k = q->kind;
		size_t e = q->end;
		if (p != q->at || e <= p) {
			e = (q->json ? json_token : tsql_token)(&q->r, p, &k);
			if (e == p) break;
		}
		step(q, p);
		/* a face is worked out on each read, as a JSON key's depends on
		   the text after it */
		if (e > a) (q->json ? json_paint : paint)(&q->r, p, e, k, a, z, f);
		q->at = p, q->end = e, q->kind = k;
		p = e;
	}
}
