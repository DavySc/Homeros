/* Package 13: the JSON reader against a simple model.  The model is a
   second recogniser of RFC 8259, written straight from its grammar; both
   must accept the same inputs.  What is accepted must survive a round
   trip: written back out (strings decoded and escaped again) and read, it
   gives the same text again. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "../src/lsp/json.h"

static const char *s, *e;

static void sp(void) { while (s < e && strchr(" \t\n\r", *s) && *s) s++; }
static int lit(const char *w) { size_t n = strlen(w); if ((size_t)(e - s) < n || memcmp(s, w, n)) return 0; s += n; return 1; }
static int dig(void) { return s < e && *s >= '0' && *s <= '9'; }

static int val(void);
static int str(void)
{
	if (s >= e || *s++ != '"') return 0;
	for (;;) {
		if (s >= e) return 0;
		unsigned char c = (unsigned char)*s++;
		if (c == '"') return 1;
		if (c < 0x20) return 0;
		if (c != '\\') continue;
		if (s >= e) return 0;
		c = (unsigned char)*s++;
		if (c == 'u') {
			for (int i = 0; i < 4; i++, s++) if (s >= e || !strchr("0123456789abcdefABCDEF", *s) || !*s) return 0;
		} else if (!c || !strchr("\"\\/bfnrt", c)) return 0;
	}
}
static int num(void)
{
	if (s < e && *s == '-') s++;
	if (!dig()) return 0;
	if (*s++ != '0') while (dig()) s++;
	if (s < e && *s == '.') { s++; if (!dig()) return 0; while (dig()) s++; }
	if (s < e && (*s == 'e' || *s == 'E')) { s++; if (s < e && (*s == '+' || *s == '-')) s++; if (!dig()) return 0; while (dig()) s++; }
	return 1;
}
static int val(void)
{
	sp();
	if (s >= e) return 0;
	if (*s == '"') return str();
	if (*s == '{' || *s == '[') {
		char close = *s == '{' ? '}' : ']', obj = *s == '{';
		s++; sp();
		if (s < e && *s == close) { s++; return 1; }
		for (;;) {
			if (obj) { sp(); if (!str()) return 0; sp(); if (s >= e || *s++ != ':') return 0; }
			if (!val()) return 0;
			sp();
			if (s >= e) return 0;
			if (*s == close) { s++; return 1; }
			if (*s++ != ',') return 0;
		}
	}
	return lit("true") || lit("false") || lit("null") || num();
}

static Jtok *t;
static int cap;

/* Token i and what is inside it, written out again. */
static int out(Str *o, const char *src, int i)
{
	if (t[i].type == 's') {
		char *b = malloc(t[i].z - t[i].a + 1);
		CHECK(b);
		size_t n = json_str(src, t, i, b);
		str_lit(o, "\"");
		str_addq(o, b, n);
		str_lit(o, "\"");
		free(b);
		return i + 1;
	}
	if (t[i].type == 'p') { str_add(o, src + t[i].a, t[i].z - t[i].a); return i + 1; }
	int obj = t[i].type == 'o', k = i + 1;
	str_add(o, obj ? "{" : "[", 1);
	for (int m = 0; m < t[i].n; m++) {
		if (m) str_lit(o, ",");
		if (obj) { k = out(o, src, k); str_lit(o, ":"); }
		k = out(o, src, k);
	}
	str_add(o, obj ? "}" : "]", 1);
	CHECK(k == t[i].end);
	return k;
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	const char *in = (const char *)d;
	s = in; e = in + n;
	int model = val();
	sp();
	model = model && s == e;
	int nt = json_parse(in, n, &t, &cap);
	CHECK((nt >= 0) == model);
	if (nt < 0) return 0;
	CHECK(t[0].end == nt);
	Str a = { 0 }, b = { 0 };
	out(&a, in, 0);
	char *copy = malloc(a.n + 1);
	CHECK(copy);
	memcpy(copy, a.s, a.n);
	CHECK(json_parse(copy, a.n, &t, &cap) >= 0);
	out(&b, copy, 0);
	CHECK(a.n == b.n && !memcmp(a.s, b.s, a.n));
	free(copy); free(a.s); free(b.s);
	return 0;
}
