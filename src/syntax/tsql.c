/* T-SQL colours as SQL Server Management Studio's query editor gives them
   (Davy, 2026-11-18: SSMS, not Neovim, is the reference for T-SQL, whose
   tree-sitter grammar fails on DECLARE, [names], GO, BEGIN/END and
   procedures).  SSMS colours by word lists, not by a parse, so a statement
   it does not know never spoils the colours after it.  Its categories
   (learn.microsoft.com, "Color coding in query editors") map to Catppuccin's
   faces: keyword, string, comment, system function, system table, view or
   schema, stored procedure, operator; numbers, names and variables stay
   plain, as there.  Faces are found from the nearest token start known
   before the bytes asked: one every CK bytes is kept, and those past an
   edit are dropped, since what follows a byte can change how it lexes. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "grid.h"
#include "tsql.h"
#include "tsql_words.h"

enum { CK = 4096, NWORDS = sizeof words / sizeof *words };

/* bytes of the text through a window */
typedef struct {
	const Text *t;
	size_t a, n;
	char b[4096];
} Rd;

static int rd(Rd *r, size_t i)
{
	if (i - r->a >= r->n) {
		r->a = i;
		r->n = text_read(r->t, i, r->b, sizeof r->b);
		if (!r->n) return -1;
	}
	return (unsigned char)r->b[i - r->a];
}

struct Tsql {
	Rd r;
	uint32_t *ck, n, cap;      /* token starts, ascending, CK apart */
	size_t at;                 /* a token start, where the last faces ended */
};

static int is_space(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
static int is_digit(int c) { return c >= '0' && c <= '9'; }
/* SQL Server's identifier characters (and UTF-8's bytes) */
static int is_id(int c)
{
	return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '@' || c == '#' || c == '$' || c >= 0x80;
}

static size_t token(Rd *r, size_t p, int *k)
{
	int c = rd(r, p), d = rd(r, p + 1);
	if (c < 0) return *k = TQ_SPACE, p;
	if (is_space(c)) {
		while (is_space(rd(r, ++p))) ;
		return *k = TQ_SPACE, p;
	}
	if (c == '-' && d == '-') {
		while ((c = rd(r, ++p)) >= 0 && c != '\n') ;
		return *k = TQ_COMMENT, p;
	}
	if (c == '/' && d == '*') {
		/* SQL Server nests them */
		int depth = 1;
		for (p += 2; depth && (c = rd(r, p)) >= 0; ) {
			d = rd(r, p + 1);
			if (c == '/' && d == '*') depth++, p += 2;
			else if (c == '*' && d == '/') depth--, p += 2;
			else p++;
		}
		return *k = TQ_COMMENT, p;
	}
	if ((c == 'N' || c == 'n') && d == '\'') c = '\'', p++;
	if (c == '\'' || c == '"' || c == '[') {
		/* the closer doubled stands for itself; unclosed runs to the end */
		int e = c == '[' ? ']' : c;
		*k = c == '\'' ? TQ_STRING : TQ_IDENT;
		while ((c = rd(r, ++p)) >= 0)
			if (c == e && rd(r, ++p) != e) break;
		return p;
	}
	if (is_digit(c) || (c == '.' && is_digit(d))) {
		while (is_id(c = rd(r, ++p)) || c == '.') ;
		return *k = TQ_NUMBER, p;
	}
	if (c == '@' || c == '#') {
		while (is_id(rd(r, ++p))) ;
		return *k = TQ_VAR, p;
	}
	if (is_id(c)) {
		while (is_id(rd(r, ++p))) ;
		return *k = TQ_WORD, p;
	}
	if (c == ',' || c == '(' || c == ')' || c == ';' || c == '.') return *k = TQ_PUNCT, p + 1;
	*k = TQ_OP;
	/* <> != >= <= !< !> and the compound assignments, :: */
	if ((d == '=' && strchr("<>!+-*/%&|^", c)) || (c == '<' && d == '>') || (c == '!' && (d == '<' || d == '>')) || (c == ':' && d == ':'))
		return p + 2;
	return p + 1;
}

size_t tsql_token(const Text *t, size_t pos, int *kind)
{
	Rd r = { t, 0, 0, { 0 } };
	return token(&r, pos, kind);
}

int tsql_word(const char *w, size_t n)
{
	char l[40];
	if (n >= sizeof l) return 0;
	for (size_t i = 0; i < n; i++) l[i] = w[i] >= 'A' && w[i] <= 'Z' ? (char)(w[i] + 32) : w[i];
	l[n] = 0;
	size_t lo = 0, hi = NWORDS;
	while (lo < hi) {
		size_t m = lo + (hi - lo) / 2;
		int c = strcmp(words[m], l);
		if (!c) return classes[m];
		if (c < 0) lo = m + 1;
		else hi = m;
	}
	return 0;
}

/* the n bytes at p, lower case, equal to lit */
static int is(Rd *r, size_t p, size_t n, const char *lit)
{
	if (n != strlen(lit)) return 0;
	for (size_t i = 0; i < n; i++) {
		int c = rd(r, p + i);
		if ((c >= 'A' && c <= 'Z' ? c + 32 : c) != lit[i]) return 0;
	}
	return 1;
}

static int schema(Rd *r, size_t p, size_t n) { return is(r, p, n, "sys") || is(r, p, n, "information_schema"); }

static unsigned char word_face(Rd *r, size_t p, size_t e)
{
	char w[40];
	size_t n = e - p;
	/* the system schemas and what is in them (SSMS's system table colour) */
	if (schema(r, p, n)) return FACE_MODULE_BUILTIN;
	if (p > 1 && rd(r, p - 1) == '.') {
		size_t b = p - 1;
		while (b && is_id(rd(r, b - 1))) b--;
		if (schema(r, b, p - 1 - b)) return FACE_MODULE_BUILTIN;
	}
	if (n >= sizeof w) return FACE_NONE;
	for (size_t i = 0; i < n; i++) w[i] = (char)rd(r, p + i);
	/* SQL Server's own stored procedures (hypothesis: by their sp_ and xp_) */
	if (n > 3 && (w[0] == 's' || w[0] == 'S' || w[0] == 'x' || w[0] == 'X') && (w[1] == 'p' || w[1] == 'P') && w[2] == '_')
		return FACE_FUNCTION;
	int c = tsql_word(w, n);
	size_t q = e;
	while (is_space(rd(r, q))) q++;
	int call = rd(r, q) == '(';
	/* LEFT and RIGHT are joins, or functions when called; a function that
	   is also a keyword (CHAR, CONVERT, YEAR) only when called */
	if ((c & TQ_OPERATOR) && !((c & TQ_FUNCTION) && call)) return FACE_OPERATOR;
	if ((c & TQ_FUNCTION) && (call || !(c & TQ_KEYWORD))) return FACE_FUNCTION_BUILTIN;
	return c & TQ_KEYWORD ? FACE_KEYWORD : FACE_NONE;
}

static unsigned char face(Rd *r, size_t p, size_t e, int k)
{
	switch (k) {
	case TQ_COMMENT: return FACE_COMMENT;
	case TQ_STRING: return FACE_STRING;
	case TQ_OP: case TQ_PUNCT: return FACE_OPERATOR;
	case TQ_WORD: return word_face(r, p, e);
	/* @@ROWCOUNT and the other globals are system functions; @x is plain */
	case TQ_VAR: return rd(r, p + 1) == '@' ? word_face(r, p, e) : FACE_NONE;
	}
	return FACE_NONE;
}

Tsql *tsql_new(const Text *t)
{
	Tsql *q = calloc(1, sizeof *q);
	if (q) q->r.t = t;
	return q;
}

void tsql_free(Tsql *q)
{
	if (q) free(q->ck);
	free(q);
}

void tsql_edit(Tsql *q, const Text *t, size_t pos)
{
	/* a token start depends on the bytes before it and on the one at it,
	   which ends the token before (a word, a doubled quote, "<>") */
	while (q->n && q->ck[q->n - 1] >= pos) q->n--;
	if (q->at >= pos) q->at = q->n ? q->ck[q->n - 1] : 0;
	q->r.t = t;
	q->r.n = 0;
}

void tsql_update(Tsql *q)
{
	/* room for the whole text's token starts, so no read starts far back */
	size_t want = text_len(q->r.t) / CK + 1;
	if (want <= q->cap) return;
	uint32_t *ck = realloc(q->ck, want * sizeof *ck);
	if (!ck) return;
	q->ck = ck;
	q->cap = (uint32_t)want;
}

void tsql_faces(Tsql *q, size_t a, size_t z, unsigned char *f)
{
	memset(f, 0, z - a);
	size_t p = q->at <= a ? q->at : 0;
	uint32_t lo = 0, hi = q->n;
	while (lo < hi) {
		uint32_t m = lo + (hi - lo) / 2;
		if (q->ck[m] <= a) lo = m + 1;
		else hi = m;
	}
	if (lo && q->ck[lo - 1] > p) p = q->ck[lo - 1];
	while (p < z) {
		int k;
		size_t e = token(&q->r, p, &k);
		if (e == p) break;
		if (p >= (q->n ? q->ck[q->n - 1] + (size_t)CK : CK) && q->n < q->cap) q->ck[q->n++] = (uint32_t)p;
		if (e > a) {
			unsigned char c = face(&q->r, p, e, k);
			for (size_t i = p < a ? a : p; i < e && i < z; i++) f[i - a] = c;
		}
		q->at = p;
		p = e;
	}
}
