/* Review of PR 136: the library's share of an update on one file (ms, best of 20) */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
typedef struct ZigH ZigH;
ZigH *zig17_parse(const char *, size_t, unsigned char *); void zig17_free(ZigH *);
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec * 1e-6; }
int main(int c, char **v)
{
	(void)c; FILE *f = fopen(v[1], "rb"); fseek(f, 0, 2); long l = ftell(f); rewind(f);
	char *b = malloc(l + 1); unsigned char *fc = malloc(l + 1); if (fread(b, 1, l, f) != (size_t)l) return 1; b[l] = 0;
	double p = 1e9, pf = 1e9;
	for (int i = 0; i < 20; i++) {
		double t = now(); ZigH *h = zig17_parse(b, l, NULL); double d = now() - t; if (d < p) p = d; zig17_free(h);
		t = now(); h = zig17_parse(b, l, fc); d = now() - t; if (d < pf) pf = d; zig17_free(h);
	}
	printf("%ld bytes: parse alone %.2f, with colours and fold spans %.2f\n", l, p, pf);
	return 0;
}
