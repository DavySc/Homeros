/* Second pass of reviews/pr-67.md: a clipboard larger than get_clipboard()'s
   first 4,096 bytes, an empty one, one with a NUL, and 3p of lines.
   Build as a unit test: cp to tests/test_clipbig.c; make test TESTS=clipbig */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "core.h"
#include "keys.h"
static char clip[20000];
static long clipn = -1;
static int gets_;
static int fake_set(const char *s, size_t n) { CHECK(n < sizeof clip); memcpy(clip, s, n); clipn = (long)n; return 0; }
static long fake_get(char *buf, size_t max) { gets_++; if (clipn > 0) memcpy(buf, clip, (size_t)clipn < max ? (size_t)clipn : max); return clipn; }
static void keys(Editor *e, const char *s) { for (; *s; s++) keys_feed(e, *s == 27 ? KEY_ESC : (unsigned char)*s, s[1] != 0); }
static void show(Editor *e, const char *what)
{
	static char b[40000];
	size_t n = text_read(e->buf->text, 0, b, sizeof b - 1);
	b[n] = 0;
	printf("%s: %zu bytes, %zu lines, cursor %zu,%zu, starts [%.20s]\n", what, n, core_lines(e), e->win->line + 1, e->win->col, b);
}
int main(void)
{
	Editor e;
	FILE *f = fopen("/tmp/clipbig.txt", "w");
	fputs("ab\n", f); fclose(f);
	CHECK(!core_open(&e, "/tmp/clipbig.txt"));
	e.clip_set = fake_set; e.clip_get = fake_get;
	for (int i = 0; i < 10000; i++) clip[i] = i % 50 == 49 ? '\n' : 'x';
	clipn = 10000;
	keys(&e, "p"); show(&e, "10,000 bytes, p");
	CHECK(text_len(e.buf->text) == 3 + 10000);
	keys(&e, "u"); show(&e, "u");
	clipn = 0; keys(&e, "p"); show(&e, "empty clipboard, p");
	memcpy(clip, "a\0b", 3); clipn = 3; keys(&e, "p"); show(&e, "NUL inside, p");
	keys(&e, "u"); memcpy(clip, "L\n", 2); clipn = 2; gets_ = 0; keys(&e, "3p"); show(&e, "line, 3p"); printf("clip_get calls for 3p: %d\n", gets_);
	gets_ = 0; keys(&e, "yy"); printf("clip_get calls for yy: %d\n", gets_);
	gets_ = 0; keys(&e, "xxxx"); printf("clip_get calls for xxxx: %d, clipboard now %ld bytes\n", gets_, clipn);
	core_free(&e);
	puts("clipbig: ok");
	return 0;
}
