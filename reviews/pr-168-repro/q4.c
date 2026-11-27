/* Q4 (head 16d2538c): ks.pending after whole and half commands (package 5's 0bf0e57e) */
#include "h.h"
int main(void)
{
	setup();
	put("a.txt", "one two\nthree four\nfive six\nseven\n", 0600);
	core_open(&e, "a.txt");
	const char *k[] = { "d", "\033", "dd", "\"a", "\033", "\"ayy", "\"ap", "yiw", "v\"ay", "vd", "3", "\033", "ciwx\033", "u", ".", "g", "g", "qa", "q", "@a", "\"", "\033", ">", ">", "d2", "j", "zf", "j", ":", "\033", "r", "x" };
	for (size_t i = 0; i < sizeof k / sizeof *k; i++) {
		keys(k[i]);
		char s[32]; snprintf(s, sizeof s, "%s", k[i]); for (char *p = s; *p; p++) if (*p == 27) *p = '$';
		printf("%s:%d ", s, e.ks.pending);
	}
	printf("\n");
	return 0;
}
