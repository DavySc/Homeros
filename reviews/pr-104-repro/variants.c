/* The audit's mechanisms written the other usual ways: which does the owned
   pass see?  Copied to src/tools/zz.c in a scratch tree by variants.sh. */
#include <stddef.h>
static int enc_or_right(unsigned c, unsigned char *o)   /* utf8: mask | 0x80 */
{
	if (c < 0x800) { o[0] = (unsigned char)(0xC0 | (c >> 6)); o[1] = (unsigned char)((c & 0x3F) | 0x80); return 2; }
	o[0] = (unsigned char)(0xE0 | (c >> 12)); o[1] = (unsigned char)(((c >> 6) & 0x3F) | 0x80); o[2] = (unsigned char)((c & 0x3F) | 0x80);
	return 3;
}
static int enc_dec(unsigned c, unsigned char *o)        /* utf8: 128 + */
{
	o[0] = (unsigned char)(192 + c / 64); o[1] = (unsigned char)(128 + c % 64);
	return 2;
}
static int enc_plus_right(unsigned c, unsigned char *o) /* utf8: c % 64 + 0x80 */
{
	o[0] = (unsigned char)(c / 64 + 0xC0); o[1] = (unsigned char)(c % 64 + 0x80);
	return 2;
}
static void split_eq(const char *b, size_t n, void (*fn)(const char *, size_t))  /* lines: == '\n' */
{
	size_t s = 0;
	for (size_t i = 0; i < n; i++) if (b[i] == '\n') { fn(b + s, i - s); s = i + 1; }
}
void *memchr(const void *, int, size_t);
static void split_memchr(const char *b, size_t n, void (*fn)(const char *, size_t))  /* lines: memchr */
{
	const char *e = b + n, *q;
	for (; (q = memchr(b, '\n', (size_t)(e - b))); b = q + 1) fn(b, (size_t)(q - b));
}
static void split_ptr(const char *b, const char *e, void (*fn)(const char *, size_t))  /* lines: *p != '\n' */
{
	for (const char *p = b; p < e; p++) { if (*p != '\n') continue; fn(b, (size_t)(p - b)); b = p + 1; }
}
static long num_rev(const char *s)                      /* digits: 10 * n + */
{
	long n = 0;
	while (*s >= '0' && *s <= '9') n = 10 * n + (*s++ - '0');
	return n;
}
static long num_two(const char *s)                      /* digits: n *= 10; n += */
{
	long n = 0;
	for (; *s >= '0' && *s <= '9'; s++) { n *= 10; n += *s - '0'; }
	return n;
}
static long num_48(const char *s)                       /* digits: - 48 */
{
	long n = 0;
	for (; *s >= 48 && *s <= 57; s++) n = n * 10 + *s - 48;
	return n;
}
long strtol(const char *, char **, int);
static long num_lib(const char *s) { return strtol(s, 0, 10); }   /* digits: strtol */
struct W { size_t line, col, want; }; struct E { struct W *win; };
static void go_field(struct E *e, size_t l, size_t c)   /* cursor: fields set, no core_set_want */
{
	e->win->line = l; e->win->col = c; e->win->want = c;
}
void (*use[])(void) = { (void (*)(void))enc_or_right, (void (*)(void))enc_dec, (void (*)(void))enc_plus_right, (void (*)(void))split_eq,
	(void (*)(void))split_memchr, (void (*)(void))split_ptr, (void (*)(void))num_rev, (void (*)(void))num_two, (void (*)(void))num_48,
	(void (*)(void))num_lib, (void (*)(void))go_field };
