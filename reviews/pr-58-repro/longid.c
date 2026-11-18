#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "text.h"
#include "../../src/syntax/tsql.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
int main(int c, char **v)
{
	static char b[1 << 22]; unsigned char f[256];
	size_t n = fread(b, 1, sizeof b, fopen(v[1], "rb"));
	Text *t = text_new(b, n); Tsql *q = tsql_new(t);
	tsql_update(q);
	tsql_faces(q, 0, 256, f);
	double t0 = now(); tsql_faces(q, n - 3, n - 1, f); double t1 = now();
	printf("%zu bytes: faces of the last word %.2f ms\n", n, t1 - t0);
}
