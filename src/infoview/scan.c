/* Finding values in JSON text without parsing it (infoview.c): Lean's
   messages pass through whole, so only the few values the page's calls
   need are looked up, in place.  Private to src/infoview/; fuzzed by
   tests/fuzz_infoview_scan.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scan.h"

int j_put(Str *s, const void *p, size_t n)
{
	if (s->n + n + 1 > s->cap) {
		size_t cap = s->cap ? s->cap : 256;
		while (cap < s->n + n + 1) cap *= 2;
		char *b = realloc(s->b, cap);
		if (!b) return -1;
		s->b = b; s->cap = cap;
	}
	if (n) memcpy(s->b + s->n, p, n);
	s->n += n;
	s->b[s->n] = 0;
	return 0;
}

char *j_copy(const char *s, size_t n)
{
	char *d = malloc(n + 1);
	if (d) { memcpy(d, s, n); d[n] = 0; }
	return d;
}

size_t j_ws(const char *s, size_t n, size_t i)
{
	while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
	return i;
}

/* The end of the value at i; NONE if it does not end inside s. */
size_t j_skip(const char *s, size_t n, size_t i)
{
	i = j_ws(s, n, i);
	if (i >= n) return NONE;
	if (s[i] == '"') {
		for (i++; i < n; i++)
			if (s[i] == '\\') i++;
			else if (s[i] == '"') return i + 1;
		return NONE;
	}
	if (s[i] == '{' || s[i] == '[') {
		for (int depth = 0; i < n; i++) {
			if (s[i] == '"') {
				size_t e = j_skip(s, n, i);
				if (e == NONE) return NONE;
				i = e - 1;
			} else if (s[i] == '{' || s[i] == '[') depth++;
			else if ((s[i] == '}' || s[i] == ']') && --depth == 0) return i + 1;
		}
		return NONE;
	}
	size_t a = i;
	while (i < n && !strchr(",:}] \t\r\n[{\"", s[i])) i++;
	return i > a ? i : NONE;
}

/* Is the value at v the string lit (written without escapes)? */
int j_is(const char *s, size_t n, size_t v, const char *lit)
{
	size_t l = strlen(lit);
	return v != NONE && v + l + 2 <= n && s[v] == '"' && !memcmp(s + v + 1, lit, l) && s[v + l + 1] == '"';
}

/* The value of key in the object at o, or NONE. */
size_t j_member(const char *s, size_t n, size_t o, const char *key)
{
	if (o == NONE || (o = j_ws(s, n, o)) >= n || s[o] != '{') return NONE;
	for (size_t i = j_ws(s, n, o + 1); i < n && s[i] == '"'; ) {
		size_t k = i, e = j_skip(s, n, i);
		if (e == NONE || (i = j_ws(s, n, e)) >= n || s[i] != ':') return NONE;
		size_t v = j_ws(s, n, i + 1);
		if (j_is(s, e, k, key)) return j_skip(s, n, v) != NONE ? v : NONE;
		if ((i = j_skip(s, n, v)) == NONE || (i = j_ws(s, n, i)) >= n || s[i] != ',') return NONE;
		i = j_ws(s, n, i + 1);
	}
	return NONE;
}

/* Element k of the array at a, or NONE. */
size_t j_elem(const char *s, size_t n, size_t a, int k)
{
	if (a == NONE || (a = j_ws(s, n, a)) >= n || s[a] != '[') return NONE;
	size_t i = j_ws(s, n, a + 1);
	if (i >= n || s[i] == ']') return NONE;
	for (; k > 0; k--) {
		if ((i = j_skip(s, n, i)) == NONE || (i = j_ws(s, n, i)) >= n || s[i] != ',') return NONE;
		i = j_ws(s, n, i + 1);
	}
	return j_skip(s, n, i) != NONE ? i : NONE;
}

size_t j_len(const char *s, size_t n, size_t v) { size_t e = j_skip(s, n, v); return e == NONE ? 0 : e - v; }

static void utf8(Str *d, unsigned long c)
{
	char b[4];
	size_t k = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
	if (k == 1) b[0] = (char)c;
	else {
		for (size_t i = k - 1; i > 0; i--) { b[i] = (char)(0x80 | (c & 0x3F)); c >>= 6; }
		b[0] = (char)((k == 2 ? 0xC0 : k == 3 ? 0xE0 : 0xF0) | c);
	}
	j_put(d, b, k);
}

long j_hex4(const char *s, size_t n, size_t i)
{
	long c = 0;
	if (i + 4 > n) return -1;
	for (size_t k = i; k < i + 4; k++) {
		int h = s[k] >= '0' && s[k] <= '9' ? s[k] - '0' : s[k] >= 'a' && s[k] <= 'f' ? s[k] - 'a' + 10 :
		        s[k] >= 'A' && s[k] <= 'F' ? s[k] - 'A' + 10 : -1;
		if (h < 0) return -1;
		c = c * 16 + h;
	}
	return c;
}

/* The string at v, decoded (malloc'd; lone surrogates become U+FFFD); NULL
   if v is no string. */
char *j_str(const char *s, size_t n, size_t v, size_t *out)
{
	Str d = { 0 };
	size_t e = j_skip(s, n, v);
	if (e == NONE || s[v] != '"') return NULL;
	j_put(&d, "", 0);
	for (size_t i = v + 1; i < e - 1; i++) {
		if (s[i] != '\\') { j_put(&d, s + i, 1); continue; }
		char ch = s[++i];
		if (ch != 'u') {
			ch = ch == 'b' ? '\b' : ch == 'f' ? '\f' : ch == 'n' ? '\n' : ch == 'r' ? '\r' : ch == 't' ? '\t' : ch;
			j_put(&d, &ch, 1);
			continue;
		}
		long c = j_hex4(s, e, i + 1), lo;
		i += 4;
		if (c >= 0xD800 && c < 0xDC00 && i + 2 < e && s[i + 1] == '\\' && s[i + 2] == 'u' &&
		    (lo = j_hex4(s, e, i + 3)) >= 0xDC00 && lo < 0xE000) {
			c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
			i += 6;
		} else if (c < 0 || (c >= 0xD800 && c < 0xE000)) c = 0xFFFD;
		utf8(&d, (unsigned long)c);
	}
	if (out) *out = d.n;
	return d.b;
}

/* s[0, n) as the inside of a JSON string. */
void j_quote(Str *d, const char *s, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		unsigned char ch = (unsigned char)s[i];
		char b[8];
		if (ch == '"' || ch == '\\') { j_put(d, "\\", 1); j_put(d, s + i, 1); }
		else if (ch < 0x20) { snprintf(b, sizeof b, "\\u%04x", ch); j_put(d, b, 6); }
		else j_put(d, s + i, 1);
	}
}

