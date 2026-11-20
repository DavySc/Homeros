/* bs N: a frame on {"data": "<N backslashes>"} (the step back over a run) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "text.h"
#include "tsql.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static unsigned char fc[256];
int main(int argc, char **argv)
{
	size_t N = strtoul(argv[1], 0, 10) & ~(size_t)1, n = 10;
	char *b = malloc(N + 64);
	memcpy(b, "{\"data\": \"", 10);
	memset(b + n, '\\', N), n += N;
	memcpy(b + n, "\"}\n", 3), n += 3;
	Text *t = text_new(b, n);
	Tsql *q = tsql_new(t, 1);
	tsql_update(q, NULL);
	for (int i = 0; i < 5; i++) {
		double t0 = now();
		for (size_t p = n / 2; p < n / 2 + 12800; p += 256) tsql_faces(q, p, p + 256, fc);
		printf("%.3f ms  ", now() - t0);
	}
	printf("(%zu backslashes, frame in the middle)\n", N);
	return 0;
}
