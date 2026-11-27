#include "run.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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
	o->b[o->n++] = (SqlBatch){ .a = a, .z = z, .count = count };
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

static int has(const Str *s, const char *w)   /* "@x\0" lists, case ignored */
{
	for (size_t i = 0; i < s->n; i += strlen(s->s + i) + 1) if (!strcasecmp(s->s + i, w)) return 1;
	return 0;
}

static int use(Str *out, const Str *decl, const char *w)   /* w used: listed once, unless declared before; nonzero out of memory */
{
	return !has(decl, w) && !has(out, w) && str_add(out, w, strlen(w) + 1);
}

static int is(const char *w, const char *const *l)   /* one of l's words, NULL-ended */
{
	while (*l && strcasecmp(w, *l)) l++;
	return !!*l;
}

/* the DECLARE item of name dn ended: "@name\0" and its type and value,
   [ds, de) of the text, "\0" into decls */
static int item(TsqlRd *r, Str *decls, char *dn, size_t ds, size_t de)
{
	int bad = str_add(decls, dn, strlen(dn) + 1);
	for (size_t i = ds; !bad && i < de; i++) { char b = (char)tsql_byte(r, i); bad = str_add(decls, &b, 1); }
	*dn = 0;
	return bad || str_add(decls, "", 1);
}

int sql_vars(const Text *t, size_t a, size_t z, Str *out, Str *decls)
{
	/* words that start a statement, and so end a DECLARE or an EXEC */
	static const char *const stmt[] = { "select", "insert", "update", "delete", "merge", "set", "if", "else", "while",
		"begin", "end", "exec", "execute", "print", "return", "with", "declare", "create", "alter", "drop",
		"truncate", "open", "fetch", "close", "deallocate", "raiserror", "throw", "goto", "use", "waitfor", NULL };
	static const char *const routine[] = { "proc", "procedure", "function", "trigger", NULL };
	TsqlRd r = { .t = t };
	Str decl = { 0 };
	char w[130], c[130] = "", dn[130] = "";   /* the token; an EXEC's @name, a parameter's if = follows; the DECLARE item's name */
	size_t ds = 0, de = 0;   /* its type and value: the first token after the name, the end of the last */
	int k, depth = 0, nw = 0, create = 0, in_decl = 0, in_exec = 0, after = 0, bad = 0;   /* after: the token before was DECLARE or its comma (1), or EXEC (2) */
	out->n = 0;
	for (size_t p = a, e; p < z && !bad; p = e) {
		if ((e = tsql_token(&r, p, &k)) > z) e = z;
		if (k == TQ_SPACE || k == TQ_COMMENT) continue;
		size_t n = 0;
		for (size_t i = p; i < e && n < 129; i++) w[n++] = (char)tsql_byte(&r, i);   /* 128: a name's limit */
		w[n] = 0;
		int end = k == TQ_PUNCT ? *w == ';' || (*w == ',' && !depth) : k == TQ_WORD && !depth && is(w, stmt);
		if (*dn && end && decls) bad = item(&r, decls, dn, ds, de);
		else if (*dn) ds = ds ? ds : p, de = e;
		if (*c && !(k == TQ_OP && !strcmp(w, "="))) bad = use(out, &decl, c);
		*c = 0;
		int prev = after;
		after = 0;
		if (k == TQ_WORD) {
			if (++nw == 1) create = !strcasecmp(w, "create") || !strcasecmp(w, "alter");
			else if (create && nw <= 4 && is(w, routine)) break;   /* CREATE [OR ALTER] PROC: its variables are its parameters */
			if (!depth && is(w, stmt)) in_decl = !strcasecmp(w, "declare"), in_exec = !strncasecmp(w, "exec", 4), after = in_decl ? 1 : 2 * in_exec;
		} else if (k == TQ_PUNCT) {
			depth += *w == '(' ? 1 : *w == ')' && depth ? -1 : 0;
			if (*w == ';') in_decl = in_exec = 0;
			after = *w == ',' && !depth && in_decl;
		} else if (k == TQ_VAR && *w == '@' && w[1] != '@') {
			if (prev == 1) bad = !has(&decl, w) && str_add(&decl, w, n + 1), memcpy(dn, w, n + 1), ds = de = 0;
			else if (in_exec && prev != 2) memcpy(c, w, n + 1);   /* EXEC p @name = 1, but EXEC @rc = p */
			else bad = use(out, &decl, w);
		}
	}
	if (*c && !bad) bad = use(out, &decl, c);
	if (*dn && decls && !bad) bad = item(&r, decls, dn, ds, de);
	size_t m = 0;   /* declared after its use: the server's error to show (Msg 137), not a value to ask for */
	for (size_t i = 0, n; !bad && i < out->n; i += n) {
		n = strlen(out->s + i) + 1;
		if (!has(&decl, out->s + i)) memmove(out->s + m, out->s + i, n), m += n;
	}
	if (!bad) out->n = m;
	free(decl.s);
	return bad ? -1 : 0;
}
