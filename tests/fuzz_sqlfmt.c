/* libFuzzer: T-SQL formatting (src/sqlfmt/) on texts decoded from the
   input, raw bytes included.  The result differs from the text only in
   spaces, tabs, line breaks and letter case, and when no statement was
   kept as written, formatting it again changes nothing (a kept statement's
   neighbours may lay out differently once it is kept: SQLDiff's too). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "text.h"
#include "sqlfmt.h"

static const char *const tok[32] = {
	"select ", "from ", "where ", "begin ", "end ", "if ", "else ", "while ", "try ", "catch ", "case ",
	"create proc p as ", "insert ", "values ", "set ", "go", "x", "'", "N'", "\"", "[", "]", "/*", "*/",
	"--", "\n", "\r\n", "=", ",", "(", ")", ";",
};

/* a token can add an indent level, so the output can grow as the square */
enum { MAX = 1 << 12, OUT = 1 << 21 };

static int kept;

static size_t fmt(const char *s, size_t n, char *o)
{
	Text *t = text_new(s, n);
	if (!t) abort();
	size_t a = 0, z = n, m;
	const char *r;
	if ((kept = sqlfmt(t, &a, &z, &r, &m)) < 0 || a > z || z > n || n - (z - a) + m > OUT) abort();
	memcpy(o, s, a);
	memcpy(o + a, r, m);
	memcpy(o + a + m, s + z, n - z);
	text_free(t);
	return n - (z - a) + m;
}

static size_t squeeze(const char *s, size_t n, char *o)
{
	size_t k = 0;
	for (size_t i = 0; i < n; i++)
		/* strchr finds a NUL byte too: keep those */
		if (!s[i] || !strchr(" \t\r\n", s[i])) o[k++] = s[i] >= 'A' && s[i] <= 'Z' ? (char)(s[i] + 32) : s[i];
	return k;
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t size)
{
	static char b[MAX], o1[OUT], o2[OUT], q1[OUT], q2[OUT];
	size_t bn = 0;
	for (size_t i = 0; i < size; i++) {
		const char *s = d[i] < 128 ? tok[d[i] % 32] : (const char *)&d[i];
		size_t l = d[i] < 128 ? strlen(s) : 1;
		if (bn + l > sizeof b) break;
		memcpy(b + bn, s, l), bn += l;
		if (d[i] % 3 && bn < sizeof b) b[bn++] = ' ';
	}
	size_t n1 = fmt(b, bn, o1), k = squeeze(b, bn, q1);
	if (k != squeeze(o1, n1, q2) || memcmp(q1, q2, k)) abort();
	if (kept) return 0;
	size_t n2 = fmt(o1, n1, o2);
	if (n1 != n2 || memcmp(o1, o2, n1)) abort();
	return 0;
}
