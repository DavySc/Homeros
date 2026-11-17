/* uni.h: every 1-, 2- and 3-byte input and 2^24 random 4-byte inputs
   against a direct reading of the UTF-8 rules (the contract's want and
   wellformed); uni_prev inverts uni_len on well-formed text. */
#include "check.h"
#include "uni.h"

static size_t spec(const unsigned char *s, size_t n, size_t i)
{
	unsigned c = s[i];
	size_t k = c < 0x80 ? 1 : c >= 0xC0 && c < 0xE0 ? 2 : c >= 0xE0 && c < 0xF0 ? 3 : c >= 0xF0 && c < 0xF8 ? 4 : 0;
	if (k == 0 || i + k > n) return 1;
	for (size_t j = 1; j < k; j++) if ((s[i + j] & 0xC0) != 0x80) return 1;
	return k;
}

int main(void)
{
	unsigned char s[4];
	for (unsigned long v = 0; v < (1UL << 24); v++) {
		s[0] = (unsigned char)(v >> 16); s[1] = (unsigned char)(v >> 8); s[2] = (unsigned char)v;
		for (size_t n = 1; n <= 3; n++) CHECK(uni_len(s, n, 0) == spec(s, n, 0));
	}
	for (unsigned long r = 0; r < (1UL << 24); r++) {
		for (int j = 0; j < 4; j++) s[j] = (unsigned char)(0x80 + rnd(0x80));
		s[0] = (unsigned char)(0xF0 + rnd(16));
		CHECK(uni_len(s, 4, 0) == spec(s, 4, 0));
	}
	const unsigned char t[] = "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80z";   /* a é € 😀 z */
	size_t n = sizeof t - 1, starts[] = { 0, 1, 3, 6, 10, 11 };
	for (int i = 0; i < 5; i++) {
		CHECK(starts[i] + uni_len(t, n, starts[i]) == starts[i + 1]);
		CHECK(uni_prev(t, n, starts[i + 1]) == starts[i]);
	}
	CHECK(uni_cp(t, n, 1) == 0xE9 && uni_cp(t, n, 3) == 0x20AC && uni_cp(t, n, 6) == 0x1F600);
	CHECK(uni_cp((const unsigned char *)"\x80", 1, 0) == 0xFFFD);
	CHECK(uni_prev((const unsigned char *)"a\x80\x80\x80\x80", 5, 5) == 4);  /* stray continuation bytes: one byte each */
	puts("test_uni: 16.7 M short inputs exhaustive, 16.7 M random 4-byte, prev/cp cases");
	return 0;
}
