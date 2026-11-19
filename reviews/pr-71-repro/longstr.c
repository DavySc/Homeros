/* longstr N MODE: a frame's cost (the core's reads: at most 50 lines and
   12,800 bytes from a position, 256 bytes a call, as draw_line asks) on
   MODE 0: T-SQL  SELECT '<N bytes>'
   MODE 1: JSON   {"data": "<N bytes>"}            one long string value
   MODE 2: JSON   {"a"<N spaces>: 1}               a key far from its colon
   MODE 3: JSON   ["a"<N bytes of block comment>]  a string before a long comment
   frames at the top and in the middle, after the index is filled; 5 runs each */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "text.h"
#include "tsql.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static unsigned char fc[256];
static unsigned long sum;
static void frame(Tsql *q, const Text *t, size_t p)
{
	size_t end = p + 12800, len = text_len(t);
	if (end > len) end = len;
	for (int l = 0; l < 50 && p < end; l++) {
		size_t z = text_line_start(t, text_line_of(t, p) + 1);
		if (z > end) z = end;
		for (; p < z; p += 256) { size_t e = p + 256 < z ? p + 256 : z; memset(fc, 0, e - p); tsql_faces(q, p, e, fc); for (size_t i = 0; i < e - p; i++) sum += fc[i]; }
		p = z;
	}
}
static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
int main(int argc, char **argv)
{
	size_t N = strtoul(argv[1], 0, 10);
	int mode = argc > 2 ? atoi(argv[2]) : 1;
	char *b = malloc(N + 64);
	static const char *const pre[] = { "SELECT '", "{\"data\": \"", "{\"a\"", "[\"a\"/*" }, *const post[] = { "'\n", "\"}\n", ": 1}\n", "*/]\n" };
	size_t n = strlen(pre[mode]);
	memcpy(b, pre[mode], n);
	for (size_t i = 0; i < N; i++) b[n++] = mode == 2 ? (i % 80 == 79 ? '\n' : ' ') : mode == 3 ? (i % 80 == 79 ? '\n' : 'x') : "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[i * 2654435761u >> 26 & 63];
	memcpy(b + n, post[mode], strlen(post[mode])), n += strlen(post[mode]);
	Text *t = text_new(b, n);
	Tsql *q = tsql_new(t, mode != 0);
	frame(q, t, 0);
	tsql_update(q, NULL);
	double top[5], mid[5];
	for (int i = 0; i < 5; i++) {
		double t0 = now(); frame(q, t, 0); double t1 = now(); frame(q, t, n / 2); double t2 = now();
		top[i] = t1 - t0, mid[i] = t2 - t1;
	}
	qsort(top, 5, sizeof *top, cmp), qsort(mid, 5, sizeof *mid, cmp);
	printf("mode %d, %zu bytes: frame at top median %.3f ms (%.3f-%.3f), in the middle %.3f ms (%.3f-%.3f)\n", mode, N, top[2], top[0], top[4], mid[2], mid[0], mid[4]);
	return sum == 1;
}
