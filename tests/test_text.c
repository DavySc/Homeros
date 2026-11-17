/* text.h against a plain array model: 200,000 random edits. */
#include <string.h>
#include "check.h"
#include "text.h"

static char model[1 << 16];
static size_t mn;

static void same(const Text *t)
{
	static char b[sizeof model];
	CHECK(text_len(t) == mn);
	CHECK(text_read(t, 0, b, sizeof b) == mn && !memcmp(b, model, mn));
	size_t lines = 0;
	for (size_t i = 0; i < mn; i++) lines += model[i] == '\n';
	CHECK(text_lines(t) == lines);
	for (size_t l = 0, i = 0; l <= lines; l++) {
		CHECK(text_line_start(t, l) == i);
		while (i < mn && model[i] != '\n') { CHECK(text_line_of(t, i) == l); i++; }
		if (i < mn) { CHECK(text_line_of(t, i) == l); i++; }
	}
	CHECK(text_line_start(t, lines + 1) == mn);
}

int main(void)
{
	Text *t = text_new("ab\ncd", 5);
	CHECK(t);
	memcpy(model, "ab\ncd", 5); mn = 5;
	same(t);
	CHECK(text_insert(t, 6, "x", 1) == -1 && text_delete(t, 4, 2) == -1);   /* out of range */
	for (int i = 0; i < 200000; i++) {
		size_t pos = rnd((unsigned)mn + 1);
		if (rnd(2) && mn < sizeof model - 8) {
			char s[8];
			size_t n = rnd(8);
			for (size_t j = 0; j < n; j++) s[j] = "a\nb\xc3\xa9"[rnd(5)];
			CHECK(!text_insert(t, pos, s, n));
			memmove(model + pos + n, model + pos, mn - pos); memcpy(model + pos, s, n); mn += n;
		} else {
			size_t n = rnd((unsigned)(mn - pos) + 1);
			CHECK(!text_delete(t, pos, n));
			memmove(model + pos, model + pos + n, mn - pos - n); mn -= n;
		}
		if (i % 1000 == 0) same(t);
	}
	same(t);
	text_free(t);
	puts("test_text: 200000 random edits match the array model");
	return 0;
}
