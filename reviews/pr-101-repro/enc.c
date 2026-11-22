/* PR 101 review: the five encoders main 894d6ac had, against uni_put, on
   every code point below 0x110000 and (the keys' three, which ran on any
   int) on 0x110000..0x1FFFFF.  cc -Iinclude enc.c src/uni/uni.c */
#include <stdio.h>
#include <string.h>
#include "uni.h"

static long keys_ops(int c, char *b)   /* src/keys/ops.c utf8_put */
{
	unsigned k = (unsigned)c;
	if (k < 0x80) { b[0] = (char)k; return 1; }
	if (k < 0x800) { b[0] = (char)(0xC0 + k / 64); b[1] = (char)(0x80 + k % 64); return 2; }
	if (k < 0x10000) { b[0] = (char)(0xE0 + k / 4096); b[1] = (char)(0x80 + k / 64 % 64); b[2] = (char)(0x80 + k % 64); return 3; }
	b[0] = (char)(0xF0 + k / 262144); b[1] = (char)(0x80 + k / 4096 % 64); b[2] = (char)(0x80 + k / 64 % 64); b[3] = (char)(0x80 + k % 64);
	return 4;
}
static long keys_edit(int k, char *b)   /* src/keys/edit.c utf8 */
{
	unsigned c = (unsigned)k;
	if (c < 0x80) { b[0] = (char)c; return 1; }
	if (c < 0x800) { b[0] = (char)(0xC0 + c / 64); b[1] = (char)(0x80 + c % 64); return 2; }
	if (c < 0x10000) { b[0] = (char)(0xE0 + c / 4096); b[1] = (char)(0x80 + c / 64 % 64); b[2] = (char)(0x80 + c % 64); return 3; }
	b[0] = (char)(0xF0 + c / 262144); b[1] = (char)(0x80 + c / 4096 % 64);
	b[2] = (char)(0x80 + c / 64 % 64); b[3] = (char)(0x80 + c % 64);
	return 4;
}
static long keys_normal(int nchar, char *b)   /* src/keys/normal.c nv_replace */
{
	long nb = 0;
	unsigned k = (unsigned)nchar;
	if (k < 0x80) b[nb++] = (char)k;
	else if (k < 0x800) { b[nb++] = (char)(0xC0 + k / 64); b[nb++] = (char)(0x80 + k % 64); }
	else if (k < 0x10000) { b[nb++] = (char)(0xE0 + k / 4096); b[nb++] = (char)(0x80 + k / 64 % 64); b[nb++] = (char)(0x80 + k % 64); }
	else { b[nb++] = (char)(0xF0 + k / 262144); b[nb++] = (char)(0x80 + k / 4096 % 64); b[nb++] = (char)(0x80 + k / 64 % 64); b[nb++] = (char)(0x80 + k % 64); }
	return nb;
}
static long lsp_json(int ci, char *o)   /* src/lsp/json.c utf8 */
{
	unsigned c = (unsigned)ci;
	if (c < 0x80) { o[0] = (char)c; return 1; }
	if (c < 0x800) { o[0] = (char)(0xC0 | c >> 6); o[1] = (char)(0x80 | (c & 0x3F)); return 2; }
	if (c < 0x10000) { o[0] = (char)(0xE0 | c >> 12); o[1] = (char)(0x80 | (c >> 6 & 0x3F)); o[2] = (char)(0x80 | (c & 0x3F)); return 3; }
	o[0] = (char)(0xF0 | c >> 18); o[1] = (char)(0x80 | (c >> 12 & 0x3F));
	o[2] = (char)(0x80 | (c >> 6 & 0x3F)); o[3] = (char)(0x80 | (c & 0x3F));
	return 4;
}
static long tools_pick(int k, char *u)   /* src/tools/pick.c edit */
{
	size_t n = k < 0x80 ? 1 : k < 0x800 ? 2 : k < 0x10000 ? 3 : 4;
	if (n == 1) u[0] = (char)k;
	else {
		int lead = n == 2 ? 0xC0 : n == 3 ? 0xE0 : 0xF0;
		for (size_t i = n; i-- > 1; k /= 64) u[i] = (char)(0x80 + k % 64);
		u[0] = (char)(lead + k);
	}
	return (long)n;
}

int main(void)
{
	long (*old[])(int, char *) = { keys_ops, keys_edit, keys_normal, lsp_json, tools_pick };
	const char *name[] = { "keys/ops.c", "keys/edit.c", "keys/normal.c", "lsp/json.c", "tools/pick.c" };
	for (int f = 0; f < 5; f++) {
		unsigned top = f < 3 ? 0x200000 : 0x110000, bad = 0;
		for (unsigned c = 0; c < top; c++) {
			char a[4] = { 0 }; unsigned char b[4] = { 0 };
			long na = old[f]((int)c, a);
			int nb = uni_put(b, c);
			if (na != nb || memcmp(a, b, (size_t)nb)) bad++;
		}
		printf("%s: %u code points, %u differ\n", name[f], top, bad);
	}
	return 0;
}
