/* review: the cost of painting a 50-line window (line by line, index filled) at 5 places in each file; prints ms, file, line */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "text.h"
#include "tsql.h"
#include "mk.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
int main(int argc, char **argv)
{
	static unsigned char face[1 << 20];
	int lang = atoi(argv[1]);
	char path[4096];
	while (fgets(path, sizeof path, stdin)) {
		path[strcspn(path, "\n")] = 0;
		FILE *f = fopen(path, "rb");
		if (!f) continue;
		fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
		char *b = malloc(n + 1); if (fread(b, 1, n, f) != (size_t)n) return 1;
		fclose(f);
		Text *t = text_new(b, n);
		Tsql *q = mk(t, lang);
		while (tsql_update(q, NULL) == 2) ;
		size_t L = text_lines(t);
		for (int w = 0; w < 5; w++) {
			size_t l0 = L > 50 ? (L - 50) * w / 4 : 0;
			double best = 1e18;
			for (int fr = 0; fr < 5; fr++) {
				double w0 = now();
				for (size_t l = l0; l < l0 + 50 && l <= L; l++) {
					size_t a = text_line_start(t, l), z = text_line_start(t, l + 1);
					if (z - a > sizeof face) z = a + sizeof face;
					memset(face, 0, z - a);
					tsql_faces(q, a, z, face);
				}
				double d = now() - w0;
				if (d < best) best = d;
			}
			printf("%.3f %s %zu\n", best, path, l0);
			if (L <= 50) break;
		}
		tsql_free(q); text_free(t); free(b);
	}
	return 0;
}
