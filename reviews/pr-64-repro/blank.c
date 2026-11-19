/* PR 64 review: the cost of after_name's walk back over spaces.  A text of
   n blank bytes (newlines), then "char(10)"; times tsql_faces on the last
   200 bytes, as one frame's draw asks.
   cc -O2 -Iinclude -Isrc/syntax reviews/pr-64-repro/blank.c src/syntax/tsql.c src/text/text.c src/uni/uni.c -o build/blank */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "text.h"
#include "tsql.h"
int main(int argc, char **argv)
{
	size_t n = argc > 1 ? strtoul(argv[1], 0, 0) : 1 << 20;
	const char *w = argc > 2 ? argv[2] : "char(10)";
	char *s = malloc(n + 64);
	memcpy(s, "x", 1), memset(s + 1, '\n', n - 1), strcpy(s + n, w);
	size_t len = n + strlen(w);
	Text *t = text_new(s, len);
	Tsql *q = tsql_new(t);
	tsql_update(q, 0);
	unsigned char face[200];
	double best = 1e9;
	for (int i = 0; i < 20; i++) {
		struct timespec a, b;
		memset(face, 0, sizeof face);
		clock_gettime(CLOCK_MONOTONIC, &a);
		tsql_faces(q, len - 200, len, face);
		clock_gettime(CLOCK_MONOTONIC, &b);
		double ms = (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
		if (ms < best) best = ms;
	}
	printf("%zu blank bytes, then %s: %.3f ms per tsql_faces (best of 20)\n", n, w, best);
	return 0;
}
