/* JSON as nvim-treesitter's highlights query colours tree-sitter-json
   (Neovim 0.12 bundles no JSON grammar): keys, strings and their escapes,
   numbers, true and false, null, comments and punctuation.  A lexer on
   tsql.c's index of token starts, not a parse, so a large file opens and
   edits as fast as T-SQL does: it keeps no state between tokens, and a
   string ends at its line's end, as tree-sitter-json's string does. */
#include "grid.h"
#include "tsql.h"

static int is_digit(int c) { return c >= '0' && c <= '9'; }

size_t json_token(TsqlRd *r, size_t p, int *k)
{
	int c = tsql_byte(r, p), d = tsql_byte(r, p + 1);
	r->open = 0;
	if (c < 0) return *k = TQ_SPACE, p;
	if (c == ' ' || (c >= '\t' && c <= '\r')) {
		while ((c = tsql_byte(r, ++p)) == ' ' || (c >= '\t' && c <= '\r')) ;
		return *k = TQ_SPACE, p;
	}
	if (c == '"') {
		/* a backslash escapes the byte after it, but not a line's end */
		while ((c = tsql_byte(r, ++p)) >= 0 && c != '"' && c != '\n')
			if (c == '\\' && (d = tsql_byte(r, p + 1)) >= 0 && d != '\n') p++;
		return *k = TQ_STRING, c == '"' ? p + 1 : p;
	}
	if (c == '/' && d == '/') {
		while ((c = tsql_byte(r, ++p)) >= 0 && c != '\n') ;
		return *k = TQ_COMMENT, p;
	}
	if (c == '/' && d == '*') {
		/* not nested; unclosed, to the end, as T-SQL's (tree-sitter-json
		   finds no comment there, but then one being typed would cost a
		   read to the end on every frame) */
		for (p += 2; (c = tsql_byte(r, p)) >= 0 && !(c == '*' && tsql_byte(r, p + 1) == '/'); p++) ;
		r->open = c < 0;
		return *k = TQ_COMMENT, c < 0 ? p : p + 2;
	}
	if (is_digit(c) || (c == '-' && is_digit(d))) {
		/* -?digits, a dot and digits, an exponent when a digit follows its
		   e and minus (tree-sitter-json's number) */
		while (is_digit(tsql_byte(r, ++p))) ;
		if (tsql_byte(r, p) == '.')
			while (is_digit(tsql_byte(r, ++p))) ;
		if ((c = tsql_byte(r, p)) == 'e' || c == 'E') {
			size_t e = p + 1 + (tsql_byte(r, p + 1) == '-');
			if (is_digit(tsql_byte(r, e)))
				for (p = e; is_digit(tsql_byte(r, p)); p++) ;
		}
		return *k = TQ_NUMBER, p;
	}
	if (c >= 'a' && c <= 'z') {
		while ((c = tsql_byte(r, ++p)) >= 'a' && c <= 'z') ;
		return *k = TQ_WORD, p;
	}
	*k = c == '{' || c == '}' || c == '[' || c == ']' || c == ',' || c == ':' ? TQ_PUNCT : TQ_OP;
	return p + 1;
}

/* the n bytes at p are s */
static int is(TsqlRd *r, size_t p, size_t n, const char *s)
{
	for (size_t i = 0; i < n; i++)
		if (!s[i] || tsql_byte(r, p + i) != s[i]) return 0;
	return !s[n];
}

void json_paint(TsqlRd *r, size_t p, size_t e, int k, size_t a, size_t z, unsigned char *f)
{
	unsigned char c = FACE_NONE;
	int d = tsql_byte(r, p);
	if (k == TQ_STRING) {
		/* a key is a string before a colon, past spaces and comments */
		size_t q = e, n;
		while ((n = json_token(r, q, &k)) > q && (k == TQ_SPACE || k == TQ_COMMENT)) q = n;
		c = tsql_byte(r, q) == ':' ? FACE_PROPERTY : FACE_STRING;
		k = TQ_STRING;
	} else if (k == TQ_NUMBER) c = FACE_NUMBER;
	else if (k == TQ_COMMENT) c = FACE_COMMENT;
	else if (k == TQ_PUNCT) c = d == ',' || d == ':' ? FACE_PUNCTUATION_DELIMITER : FACE_PUNCTUATION_BRACKET;
	else if (k == TQ_WORD) c = is(r, p, e - p, "true") || is(r, p, e - p, "false") ? FACE_BOOLEAN : is(r, p, e - p, "null") ? FACE_CONSTANT_BUILTIN : FACE_NONE;
	for (size_t i = p < a ? a : p; i < e && i < z; i++) f[i - a] = c;
	/* a backslash and the byte after it (tree-sitter-json's escape_sequence
	   takes one byte, also the u of A) */
	if (k == TQ_STRING)
		for (size_t i = p + 1; i + 1 < e; i++)
			if (tsql_byte(r, i) == '\\') {
				for (size_t j = i; j < i + 2; j++)
					if (j >= a && j < z) f[j - a] = FACE_STRING_ESCAPE;
				i++;
			}
}
