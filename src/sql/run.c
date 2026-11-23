#include "run.h"
#include <stdlib.h>
#include "match.h"
#include "tsql.h"

static int blank(int c) { return c == ' ' || c == '\t' || c == '\r'; }

static int add(SqlBatches *o, TsqlRd *r, size_t a, size_t z, long count)   /* GO 0's too: run 0 times */
{
	size_t i = a;
	while (i < z && (tsql_byte(r, i) == '\n' || tsql_byte(r, i) == '\r')) i++;   /* sqlcmd sends a batch of blanks, not one of empty lines */
	if (i == z) return 0;
	if (o->n == o->cap) {
		size_t cap = o->cap ? 2 * o->cap : 16;
		SqlBatch *b = realloc(o->b, cap * sizeof *b);
		if (!b) return -1;
		o->b = b, o->cap = cap;
	}
	o->b[o->n++] = (SqlBatch){ a, z, count };
	return 0;
}

/* The rest of a line after "GO" at p (measured against sqlcmd 18, 65 cases):
   its count and the line's end into *e when GO is a separator; 0 when the
   line is SQL (GOX, GO;); -1 when sqlcmd refuses it (GO x, GO 2;, GO/). */
static long go_line(TsqlRd *r, size_t p, size_t z, size_t *e)
{
	int c = p < z ? tsql_byte(r, p) : '\n', d = p + 1 < z ? tsql_byte(r, p + 1) : '\n';
	int dash = c == '-' && d == '-';
	if (!dash && c != '\n' && c != '-' && !blank(c)) {
		/* sqlcmd's own syntax ($(var), !!, :cmd, quotes) is refused, the rest is SQL */
		const char *bad = "$![\"'(:/";
		while (*bad && *bad != c) bad++;
		return *bad ? -1 : 0;
	}
	while (p < z && blank(tsql_byte(r, p))) p++;
	char digits[24];
	size_t n = 0;
	for (; p < z && (c = tsql_byte(r, p)) >= '0' && c <= '9'; p++) if (n < sizeof digits) digits[n++] = (char)c;
	const char *s = digits;
	long count = n ? match_num(&s, digits + n) : 1;
	while (p < z && blank(tsql_byte(r, p))) p++;
	if (p + 1 < z && tsql_byte(r, p) == '-' && tsql_byte(r, p + 1) == '-')
		while (p < z && tsql_byte(r, p) != '\n') p++;
	if (p < z && tsql_byte(r, p) != '\n') return -1;
	*e = p < z ? p + 1 : z;
	return count;
}

long sql_split(const Text *t, size_t a, size_t z, SqlBatches *out)
{
	TsqlRd r = { .t = t };
	size_t start = a, line = 1;
	int lead = 1;   /* only blanks and block comments so far on this line */
	out->n = 0;
	for (size_t p = a, e; p < z; p = e) {
		int k;
		e = tsql_token(&r, p, &k);
		if (e > z) e = z;
		size_t go = 0;
		long count = 0;
		int g = e - p >= 2 ? tsql_byte(&r, p) : 0, o = g ? tsql_byte(&r, p + 1) : 0;
		if (lead && k == TQ_WORD && (g == 'g' || g == 'G') && (o == 'o' || o == 'O')
			&& (e - p == 2 || tsql_byte(&r, p + 2) == '$'))
			count = go_line(&r, p + 2, z, &go);
		if (count < 0) return (long)line;
		int comment = k == TQ_COMMENT && tsql_byte(&r, p) == '/';
		for (size_t i = p; i < e; i++) if (tsql_byte(&r, i) == '\n') line++, lead = 1;
		if (go) {
			size_t end = p;   /* sqlcmd sends a comment before GO on its line, not the blanks */
			while (end > start && blank(tsql_byte(&r, end - 1))) end--;
			if (add(out, &r, start, end, count)) return -1;
			line++, lead = 1, start = e = go;
		} else if (k != TQ_SPACE && !comment) lead = 0;
	}
	return add(out, &r, start, z, 1);
}
