/* T-SQL formatting (src/sqlfmt/).  Replays tests/test_sqlfmt.txt: texts
   with what SQLDiff's own `sqldiff fmt -d tsql --keyword-case lower`
   printed for them (src/sqlfmt/oracle/record.py; directed cases, one or
   more per rule sqlfmt.c ports, then generated T-SQL and token soup).  A
   statement of more than 65,536 tokens keeps its clause breaks.  A range
   edits only its tokens' gaps, and the edit is trimmed to bytes that
   differ.  Then random token soup, where the result keeps every token
   (keywords may change case only) and, when no statement was kept as
   written, formatting it again changes nothing. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "text.h"
#include "sqlfmt.h"

static int kept;

/* the whole text formatted, applied */
static char *fmt(const char *src, size_t n, size_t a, size_t z, size_t *len)
{
	Text *t = text_new(src, n);
	CHECK(t);
	const char *r;
	size_t m;
	CHECK((kept = sqlfmt(t, &a, &z, &r, &m)) >= 0);
	CHECK(a <= z && z <= n);
	static char out[1 << 23];
	CHECK(n - (z - a) + m < sizeof out);
	/* the edit is as small as it can be */
	CHECK(!m || a == z || (r[0] != src[a] && r[m - 1] != src[z - 1]));
	memcpy(out, src, a);
	memcpy(out + a, r, m);
	memcpy(out + a + m, src + z, n - z);
	*len = n - (z - a) + m;
	text_free(t);
	return out;
}

/* a failing case's text, with control bytes as \xNN (a NUL would make
   grep in tools/planted.sh read the log as binary) */
static void show(const char *s, size_t n)
{
	for (size_t i = 0; i < n; i++)
		fprintf(stderr, (unsigned char)s[i] < 32 && s[i] != '\n' ? "\\x%02x" : "%c", (unsigned char)s[i]);
	fprintf(stderr, "\n---\n");
}

static int eq(const char *got, size_t n, const char *want) { return n == strlen(want) && !memcmp(got, want, n); }

/* the bytes outside spaces, tabs and line breaks, lower case */
static size_t squeeze(const char *s, size_t n, char *o)
{
	size_t k = 0;
	for (size_t i = 0; i < n; i++)
		if (!strchr(" \t\r\n", s[i])) o[k++] = s[i] >= 'A' && s[i] <= 'Z' ? (char)(s[i] + 32) : s[i];
	return k;
}

static const char *const soup[] = {
	"select", "FROM", "where", "join", "Left", "on", "and", "or", "not", "group", "by", "order", "insert",
	"into", "values", "update", "set", "delete", "merge", "when", "then", "case", "else", "end", "begin",
	"if", "while", "return", "declare", "exec", "commit", "tran", "try", "catch", "create", "procedure",
	"as", "with", "union", "go", "x", "dbo.t", "NOCOUNT", "@x", "#t", "[a b]", "N'x'", "'a''b'", "1", ",",
	"(", ")", ";", ".", "=", "<>", "*=", ":", "-- c\n", "/* b */", "\n", "\n\n\n", "\r\n", "  ", "\t",
};

int main(void)
{
	FILE *f = fopen("tests/test_sqlfmt.txt", "rb");
	CHECK(f);
	static char rec[1 << 21];
	size_t rn = fread(rec, 1, sizeof rec, f), n, cases = 0;
	CHECK(rn < sizeof rec);
	fclose(f);
	for (char *p = strchr(rec, '\n') + 1, *e; p < rec + rn; cases++) {
		size_t in = strtoul(p, &e, 10), want = strtoul(e, &e, 10);
		const char *src = e + 1, *got = fmt(src, in, 0, in, &n);
		if (n != want || memcmp(got, src + in, n)) {
			fprintf(stderr, "case %zu:\n", cases);
			show(src, in), show(got, n), show(src + in, want);
			CHECK(0);
		}
		p = e + 1 + in + want;
	}
	CHECK(cases > 400);

	/* SQLDiff has no token limit: `from` still starts a line (as sqldiff
	   prints it, measured in reviews/pr-60.md) */
	static char big[1 << 19];
	size_t bn = 0, gn;
	memcpy(big, "SELECT a", 8), bn = 8;
	for (int k = 0; k < 40000; k++) memcpy(big + bn, ",a", 2), bn += 2;
	memcpy(big + bn, " FROM t", 7), bn += 7;
	const char *gb = fmt(big, bn, 0, bn, &gn);
	CHECK(gn == bn + 40000 + 1 && !memcmp(gb + gn - 8, "\nfrom t\n", 8));

	/* 999 nested BEGINs are laid out (1,000 is SQLDiff's limit, recorded) */
	bn = 0;
	for (int k = 0; k < 999; k++) memcpy(big + bn, "BEGIN\n", 6), bn += 6;
	memcpy(big + bn, "SELECT  1\n", 10), bn += 10;
	for (int k = 0; k < 999; k++) memcpy(big + bn, "END\n", 4), bn += 4;
	gb = fmt(big, bn, 0, bn, &gn);
	CHECK(!kept && !memcmp(gb, "begin\n    begin\n", 16));

	/* a range: only the second statement's gaps and words change */
	const char *two = "SELECT a,b FROM t\nSELECT c,d FROM u\n";
	const char *got = fmt(two, strlen(two), 18, strlen(two), &n);
	CHECK(eq(got, n, "SELECT a,b FROM t\nselect c, d\nfrom u\n"));
	got = fmt(two, strlen(two), 0, 18, &n);
	CHECK(eq(got, n, "select a, b\nfrom t\nSELECT c,d FROM u\n"));

	static char src[4096], again[1 << 16], a[1 << 16], b[1 << 16];
	for (int round = 0; round < 3000; round++) {
		size_t sn = 0;
		for (unsigned k = rnd(80); k--; ) {
			const char *w = soup[rnd(sizeof soup / sizeof *soup)];
			size_t l = strlen(w);
			if (sn + l + 1 >= sizeof src) break;
			memcpy(src + sn, w, l), sn += l;
			if (rnd(4)) src[sn++] = ' ';
		}
		got = fmt(src, sn, 0, sn, &n);
		CHECK(squeeze(src, sn, a) == squeeze(got, n, b) && !memcmp(a, b, squeeze(src, sn, a)));
		if (kept) continue;
		memcpy(again, got, n);
		size_t m;
		const char *twice = fmt(again, n, 0, n, &m);
		if (m != n || memcmp(twice, again, n)) {
			fprintf(stderr, "not stable:\n%.*s\n---\n%.*s\n", (int)n, again, (int)m, twice);
			CHECK(0);
		}
	}
	return 0;
}
