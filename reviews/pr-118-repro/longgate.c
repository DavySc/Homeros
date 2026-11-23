/* tests/test_org.c's long-line gate (one 64 KB line, coloured in 256-byte
   pieces, then shown; limit 100 ms under the sanitizers) with link and
   footnote units */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "text.h"
#include "org.h"
#include "syntax.h"
int main(void)
{
	static const char *const units[] = { "word ", "a[fn:1] ", "a [fn::b c] ", "a [fn:: (", "[[a][b]] ", "http://b.c ", "<http:b c> ", "[[a][b ", "<http:b " };
	static char line[65536];
	static unsigned char lf[sizeof line];
	for (size_t i = 0; i < sizeof units / sizeof *units; i++) {
		size_t m = 0, ul = strlen(units[i]);
		while (m + ul < sizeof line) memcpy(line + m, units[i], ul), m += ul;
		line[m++] = '\n';
		struct timespec t0, t1;
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t0);
		Text *t = text_new(line, m);
		OrgFx *x = orgfx_new(t);
		orgfx_update(x, NULL);
		for (size_t a = 0; a < m; a += 256) orgfx_faces(x, a, a + 256 < m ? a + 256 : m, lf + a);
		Conceal c[64];
		orgfx_conceal(x, 0, c, 64);
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t1);
		printf("%-14s %ld ms\n", units[i], (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000);
		orgfx_free(x); text_free(t);
	}
	return 0;
}
