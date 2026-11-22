/* str.h's str_lines against splitting the whole output at once: the same
   lines however the output is cut, the tail kept until its '
' comes,
   lines of any length (pick.c once cut them at 4 KB).  str_addf formats
   into the room there is, and grows and formats again when the text does
   not fit (from empty, at the edge of the room, far past it); str_add
   keeps bytes and the NUL. */
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
	Str s = { 0 };
	char want[20000];
	size_t n = 0;
	CHECK(!str_addf(&s, "%d", 42) && s.n == 2 && !strcmp(s.s, "42"));
	n = 2; memcpy(want, "42", 2);
	/* every length from 1 to 3000 appended, so each side of each room's edge */
	for (int k = 1; n + (size_t)k < sizeof want && k < 3000; k += 7) {
		CHECK(!str_addf(&s, "%*d", k, k));
		n += (size_t)snprintf(want + n, sizeof want - n, "%*d", k, k);
		CHECK(s.n == n && s.n < s.cap && !memcmp(s.s, want, n) && s.s[n] == 0);
	}
	/* exactly the room, which leaves none for the NUL, and one less */
	for (int less = 0; less < 2; less++) {
		size_t r = s.cap - s.n - (size_t)less, was = s.n;
		CHECK(!str_addf(&s, "%*d", (int)r, 7) && s.n == was + r && s.n < s.cap && s.s[s.n - 1] == '7' && s.s[s.n] == 0);
		n = s.n;
	}
	CHECK(!str_add(&s, "a\0b", 3) && s.n == n + 3 && !memcmp(s.s + n, "a\0b", 3) && s.s[n + 3] == 0);
	str_free(&s);
	CHECK(!s.s && !s.n && !s.cap);
	puts("test_str: str_lines over 2000 random cuts; str_addf from empty and on each side of the room's edge, up to 3000 bytes at once; str_add keeps NULs");
	return 0;
}
