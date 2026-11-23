/* frame-gate probe 2: LINES copies of a line ('|' is a newline), with one
   other line put in place of line AT; the 50 lines from FROM shown, then
   coloured, as after a key typed on line AT (an update, then the frame) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "text.h"
#include "org.h"
#include "syntax.h"
int main(int argc, char **argv)
{
	if (argc < 6) return 2;
	const char *ln = argv[1], *sp = argv[3];
	size_t L = strtoul(argv[2], 0, 10), at = strtoul(argv[4], 0, 10), from = strtoul(argv[5], 0, 10), w = strlen(ln), m = 0;
	char *many = malloc(L * w + strlen(sp) + 1);
	for (size_t i = 0; i < L; i++) {
		const char *s = i == at ? sp : ln;
		memcpy(many + m, s, strlen(s)), m += strlen(s);
	}
	for (size_t i = 0; i < m; i++) if (many[i] == '|') many[i] = '\n';
	Text *t = text_new(many, m); OrgFx *x = orgfx_new(t);
	orgfx_update(x, NULL);
	static unsigned char fc[1 << 20]; Conceal c[64];
	struct timespec t0, t1;
	size_t a = text_line_start(t, from), z = text_line_start(t, from + 50);
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t0);
	for (size_t l = from; l < from + 50; l++) orgfx_conceal(x, l, c, 64);
	orgfx_faces(x, a, z, fc);
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t1);
	printf("%-22s x %zu, line %zu = %-18s shown from %zu: %.1f ms\n", ln, L, at, sp, from, ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e6);
	orgfx_free(x); text_free(t); free(many);
	return 0;
}
