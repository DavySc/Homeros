/* one long line of COUNT copies of a unit: shown, then coloured in pieces of
   200 bytes, as the core asks a long line */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "text.h"
#include "org.h"
#include "syntax.h"
int main(int argc, char **argv)
{
	if (argc < 3) return 2;
	const char *u = argv[1]; size_t k = strtoul(argv[2], 0, 10), w = strlen(u), m = 0;
	char *b = malloc(k * w + 16);
	memcpy(b, "[fn:1] d\n", 9); m = 9;
	for (size_t i = 0; i < k; i++) memcpy(b + m, u, w), m += w;
	b[m++] = '\n';
	Text *t = text_new(b, m); OrgFx *x = orgfx_new(t);
	orgfx_update(x, NULL);
	static unsigned char fc[256]; static Conceal c[1 << 16];
	struct timespec t0, t1;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t0);
	orgfx_conceal(x, 1, c, 1 << 16);
	for (size_t a = 9; a < m; a += 200) orgfx_faces(x, a, a + 200 < m ? a + 200 : m, fc);
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t1);
	printf("%-24s x %zu (%zu bytes): %.1f ms\n", u, k, m, ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e6);
	orgfx_free(x); text_free(t); free(b);
	return 0;
}
