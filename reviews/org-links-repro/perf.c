/* frame-gate probe: the last 50 of LINES lines of one pattern, shown then coloured */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "text.h"
#include "org.h"
#include "syntax.h"
int main(int argc, char **argv)
{
	(void)argc;
	const char *ln = argv[1]; size_t L = strtoul(argv[2], 0, 10), w = strlen(ln);
	char *many = malloc(L * w); size_t m = 0;
	for (size_t i = 0; i < L; i++) memcpy(many + m, ln, w), m += w;
	for (size_t i = 0; i < m; i++) if (many[i] == '|') many[i] = '\n';
	Text *t = text_new(many, m); OrgFx *x = orgfx_new(t);
	orgfx_update(x, NULL);
	static unsigned char fc[1 << 20]; Conceal c[64];
	struct timespec t0, t1;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t0);
	size_t nl = text_lines(t);
	for (size_t l = nl - 50; l < nl; l++) orgfx_conceal(x, l, c, 64);
	orgfx_faces(x, m - 50 * w, m, fc);
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t1);
	printf("%-28s %zu lines: %.1f ms\n", ln, L, ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e6);
	return 0;
}
