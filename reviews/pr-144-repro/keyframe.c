/* review, second pass: the frame after a key (an edit in the window's middle, then its 50 lines read) and the first
   frame after a jump (a fresh, filled index), at 5 places in each file; prints worst-key ms, jump ms, file, line */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "text.h"
#include "tsql.h"
#include "mk.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static unsigned char face[1 << 20];
static void frame(Tsql *q, const Text *t, size_t l0)
{
	size_t L = text_lines(t);
	for (size_t l = l0; l < l0 + 50 && l <= L; l++) {
		size_t a = text_line_start(t, l), z = text_line_start(t, l + 1);
		if (z - a > sizeof face) z = a + sizeof face;
		memset(face, 0, z - a);
		tsql_faces(q, a, z, face);
	}
}
int main(int argc, char **argv)
{
	int lang = atoi(argv[1]);
	const char *ins = argc > 2 ? argv[2] : "a";
	char path[4096];
	while (fgets(path, sizeof path, stdin)) {
		path[strcspn(path, "\n")] = 0;
		FILE *f = fopen(path, "rb");
		if (!f) continue;
		fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
		char *b = malloc(n + 1); if (fread(b, 1, n, f) != (size_t)n) return 1;
		fclose(f);
		Text *t = text_new(b, n);
		size_t L = text_lines(t), ni = strlen(ins);
		for (int w = 0; w < 5; w++) {
			size_t l0 = L > 50 ? (L - 50) * w / 4 : 0;
			Tsql *q = mk(t, lang);
			while (tsql_update(q, NULL) == 2) ;
			double j0 = now();
			frame(q, t, l0);
			double jump = now() - j0, worst = 0;
			for (int k = 0; k < 6; k++) {
				size_t p = text_line_start(t, l0 + 25 < L ? l0 + 25 : l0);
				double k0 = now();
				if (k % 2) { tsql_edit(q, t, p, ni, 0); if (text_delete(t, p, ni)) return 2; }
				else { tsql_edit(q, t, p, 0, ni); if (text_insert(t, p, ins, ni)) return 2; }
				frame(q, t, l0);
				double d = now() - k0;
				if (d > worst) worst = d;
				while (tsql_update(q, NULL) == 2) ;
			}
			printf("%.3f %.3f %s %zu\n", worst, jump, path, l0);
			tsql_free(q);
			if (L <= 50) break;
		}
		text_free(t); free(b);
	}
	return 0;
}
