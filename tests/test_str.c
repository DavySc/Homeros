/* str.h's str_lines against splitting the whole output at once: the same
   lines however the output is cut, the tail kept until its '\n' comes,
   lines of any length (pick.c once cut them at 4 KB). */
#include <string.h>
#include "check.h"
#include "str.h"

static Str got;

static void line(void *ctx, char *s, size_t n)
{
	CHECK(ctx == &got && s[n] == 0 && !memchr(s, '\n', n));
	str_add(&got, s, n);
	str_add(&got, "|", 1);
}

int main(void)
{
	static char out[20000];
	for (int run = 0; run < 2000; run++) {
		size_t n = rnd(run % 100 ? 64 : (unsigned)sizeof out);
		for (size_t i = 0; i < n; i++) out[i] = "ab\n\n"[rnd(i % 7 ? 4 : 2)];
		Str part = { 0 }, want = { 0 };
		got.n = 0;
		for (size_t a = 0, z; a < n; a = z) {
			z = a + rnd((unsigned)(n - a) + 1);
			CHECK(!str_lines(&part, out + a, z - a, line, &got));
		}
		for (char *s = out, *nl; (nl = memchr(s, '\n', (size_t)(out + n - s))); s = nl + 1) {
			str_add(&want, s, (size_t)(nl - s));
			str_add(&want, "|", 1);
		}
		size_t tail = 0;
		while (tail < n && out[n - 1 - tail] != '\n') tail++;
		CHECK(got.n == want.n && (!want.n || !memcmp(got.s, want.s, want.n)));
		CHECK(part.n == tail && (!tail || !memcmp(part.s, out + n - tail, tail)));
		str_free(&part);
		str_free(&want);
	}
	str_free(&got);
	printf("test_str: ok\n");
	return 0;
}
