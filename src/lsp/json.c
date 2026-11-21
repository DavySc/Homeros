/* JSON reader and writer helpers (json.h).  RFC 8259's grammar, checked in
   full, so a broken message is refused instead of half read. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "uni.h"
#include "json.h"
#include "lsp.h"

enum { DEPTH = 512 };

typedef struct { const char *s; size_t n, i; Jtok **t; int *cap, nt; } P;

static void ws(P *p)
{
	while (p->i < p->n && (p->s[p->i] == ' ' || p->s[p->i] == '\t' || p->s[p->i] == '\n' || p->s[p->i] == '\r')) p->i++;
}

static int hex(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static int digit(P *p) { return p->i < p->n && p->s[p->i] >= '0' && p->s[p->i] <= '9'; }

static int tok(P *p, char type)
{
	if (p->nt == *p->cap) {
		int c = *p->cap ? 2 * *p->cap : 64;
		Jtok *t = realloc(*p->t, (size_t)c * sizeof *t);
		if (!t) return -1;
		*p->t = t; *p->cap = c;
	}
	(*p->t)[p->nt] = (Jtok){ .type = type, .a = p->i };
	return p->nt++;
}

static int value(P *p, int depth)
{
	if (depth > DEPTH) return -1;
	ws(p);
	if (p->i >= p->n) return -1;
	const char *s = p->s;
	char c = s[p->i];
	int k;
	if (c == '{' || c == '[') {
		char close = c == '{' ? '}' : ']';
		if ((k = tok(p, c == '{' ? 'o' : 'a')) < 0) return -1;
		p->i++;
		ws(p);
		int n = 0;
		if (p->i < p->n && s[p->i] == close) p->i++;
		else for (;;) {
			if (c == '{') {
				ws(p);
				if (p->i >= p->n || s[p->i] != '"' || value(p, depth + 1) < 0) return -1;
				ws(p);
				if (p->i >= p->n || s[p->i++] != ':') return -1;
			}
			if (value(p, depth + 1) < 0) return -1;
			n++;
			ws(p);
			if (p->i >= p->n) return -1;
			if (s[p->i] == close) { p->i++; break; }
			if (s[p->i++] != ',') return -1;
		}
		(*p->t)[k].n = n;
	} else if (c == '"') {
		if ((k = tok(p, 's')) < 0) return -1;
		size_t a = ++p->i;
		for (;;) {
			if (p->i >= p->n) return -1;
			unsigned char d = (unsigned char)s[p->i];
			if (d == '"') break;
			if (d < 0x20) return -1;
			if (d == '\\') {
				if (++p->i >= p->n) return -1;
				d = (unsigned char)s[p->i];
				if (d == 'u') {
					for (int j = 1; j <= 4; j++) if (p->i + j >= p->n || hex(s[p->i + j]) < 0) return -1;
					p->i += 4;
				} else if (!strchr("\"\\/bfnrt", d) || !d) return -1;
			}
			p->i++;
		}
		(*p->t)[k].a = a;
		(*p->t)[k].z = p->i++;
		(*p->t)[k].end = p->nt;
		return k;
	} else {
		if ((k = tok(p, 'p')) < 0) return -1;
		static const char *const lit[] = { "true", "false", "null" };
		int done = 0;
		for (int j = 0; j < 3 && !done; j++) {
			size_t l = strlen(lit[j]);
			if (p->n - p->i >= l && !memcmp(s + p->i, lit[j], l)) { p->i += l; done = 1; }
		}
		if (!done) {
			if (s[p->i] == '-') p->i++;
			if (!digit(p)) return -1;
			if (s[p->i] == '0') p->i++;
			else while (digit(p)) p->i++;
			if (p->i < p->n && s[p->i] == '.') {
				p->i++;
				if (!digit(p)) return -1;
				while (digit(p)) p->i++;
			}
			if (p->i < p->n && (s[p->i] == 'e' || s[p->i] == 'E')) {
				p->i++;
				if (p->i < p->n && (s[p->i] == '+' || s[p->i] == '-')) p->i++;
				if (!digit(p)) return -1;
				while (digit(p)) p->i++;
			}
		}
	}
	(*p->t)[k].z = p->i;
	(*p->t)[k].end = p->nt;
	return k;
}

int json_parse(const char *s, size_t n, Jtok **t, int *cap)
{
	P p = { s, n, 0, t, cap, 0 };
	if (value(&p, 0) < 0) return -1;
	ws(&p);
	return p.i == n ? p.nt : -1;
}

int json_get(const char *s, const Jtok *t, int o, const char *key)
{
	if (o < 0 || t[o].type != 'o') return -1;
	size_t l = strlen(key);
	char b[64];
	for (int i = o + 1, k = 0; k < t[o].n; k++, i = t[i + 1].end)
		if (t[i].z - t[i].a < sizeof b && json_str(s, t, i, b) == l && !memcmp(b, key, l))
			return i + 1;
	return -1;
}

int json_at(const Jtok *t, int a, int k)
{
	if (a < 0 || t[a].type != 'a' || k < 0 || k >= t[a].n) return -1;
	int i = a + 1;
	while (k--) i = t[i].end;
	return i;
}

long json_int(const char *s, const Jtok *t, int i)
{
	if (i < 0 || t[i].type != 'p') return -1;
	long v = 0;
	for (size_t j = t[i].a; j < t[i].z; j++) {
		if (s[j] < '0' || s[j] > '9' || v > (0x7fffffffL - 9) / 10) return -1;
		v = v * 10 + (s[j] - '0');
	}
	return t[i].z > t[i].a ? v : -1;
}

int json_true(const char *s, const Jtok *t, int i)
{
	return i >= 0 && t[i].type == 'p' && t[i].z - t[i].a == 4 && !memcmp(s + t[i].a, "true", 4);
}

static unsigned u4(const char *s) { return (unsigned)(hex(s[0]) << 12 | hex(s[1]) << 8 | hex(s[2]) << 4 | hex(s[3])); }

size_t json_str(const char *s, const Jtok *t, int i, char *out)
{
	size_t o = 0;
	for (size_t j = t[i].a; j < t[i].z; j++) {
		if (s[j] != '\\') { out[o++] = s[j]; continue; }
		char c = s[++j];
		if (c != 'u') { out[o++] = c == 'b' ? '\b' : c == 'f' ? '\f' : c == 'n' ? '\n' : c == 'r' ? '\r' : c == 't' ? '\t' : c; continue; }
		unsigned u = u4(s + j + 1);
		j += 4;
		if (u >= 0xD800 && u < 0xDC00 && j + 6 < t[i].z && s[j + 1] == '\\' && s[j + 2] == 'u') {
			unsigned l = u4(s + j + 3);
			if (l >= 0xDC00 && l < 0xE000) { u = 0x10000 + ((u - 0xD800) << 10) + (l - 0xDC00); j += 6; }
		}
		o += (size_t)uni_put((unsigned char *)out + o, u >= 0xD800 && u < 0xE000 ? 0xFFFD : u);
	}
	out[o] = 0;
	return o;
}

/* A byte sequence the UTF-16 kernel counts as one character (lsp.h's
   u16len), and whether it is UTF-8 a JSON reader takes: the kernel only
   checks the shape, so overlong forms, surrogates and code points past
   U+10FFFF still need refusing here. */
static int valid(const unsigned char *b, size_t k)
{
	return k == 1 ? b[0] < 0x80
	     : k == 2 ? b[0] >= 0xC2
	     : k == 3 ? !(b[0] == 0xE0 && b[1] < 0xA0) && !(b[0] == 0xED && b[1] >= 0xA0)
	     : b[0] <= 0xF4 && !(b[0] == 0xF0 && b[1] < 0x90) && !(b[0] == 0xF4 && b[1] >= 0x90);
}

int str_addq(Str *s, const char *p, size_t n)
{
	const unsigned char *b = (const unsigned char *)p;
	size_t i = 0;
	int bad = 0;
	while (i < n) {
		size_t j = i;
		while (j < n && b[j] >= 0x20 && b[j] < 0x80 && b[j] != '"' && b[j] != '\\') j++;
		if (str_add(s, p + i, j - i)) return -1;
		if (j == n) break;
		if (b[j] >= 0x80) {
			/* the kernel's own step, so the server counts the columns the
			   kernel counts: a character it reads as 2 UTF-16 units (a 4-byte
			   form) has unit 1 inside it, so lsp_u16_byte answers 0 */
			size_t k = lsp_u16_byte(b + j, n - j, 1);
			if (!k) k = 4;
			if (valid(b + j, k) ? str_add(s, p + j, k) : (bad++, k == 4 ? str_lit(s, "\xEF\xBF\xBD\xEF\xBF\xBD") : str_lit(s, "\xEF\xBF\xBD"))) return -1;
			i = j + k;
			continue;
		}
		char e[8];
		unsigned char c = b[j];
		int l = c == '"' || c == '\\' ? snprintf(e, sizeof e, "\\%c", c)
		      : c == '\n' ? snprintf(e, sizeof e, "\\n") : c == '\t' ? snprintf(e, sizeof e, "\\t")
		      : snprintf(e, sizeof e, "\\u%04x", c);
		if (str_add(s, e, (size_t)l)) return -1;
		i = j + 1;
	}
	return bad;
}
