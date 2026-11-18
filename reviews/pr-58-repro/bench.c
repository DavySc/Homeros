/* bench FILE REPEAT MODE: frame costs of tsql.c, a frame read as core_draw
   reads it (50 lines, 256 bytes a call).
   MODE end: fresh open (after tsql_update), first frame at the end
        top: warm, an edit at byte 0, then a frame at the end
        open: warm, "/" "*" inserted at byte 0 (an unclosed comment), a frame at the top, then a second
        tok: tsql_token over the whole text
        key: warm frame at the end, a 1-byte edit in the window, a frame */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "text.h"
#include "../../src/syntax/tsql.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static void frame(Tsql *q, const Text *t, size_t l0)
{
	unsigned char fc[256];
	size_t nl = text_lines(t);
	for (size_t l = l0; l < l0 + 50 && l < nl; l++) {
		size_t a = text_line_start(t, l), z = text_line_start(t, l + 1);
		if (z > a) z--;
		for (; a < z; a += 256) tsql_faces(q, a, a + 256 < z ? a + 256 : z, fc);
	}
}
int main(int argc, char **argv)
{
	static char one[1 << 20];
	FILE *in = fopen(argv[1], "rb");
	size_t n1 = fread(one, 1, sizeof one, in), rep = strtoul(argv[2], 0, 10);
	const char *mode = argv[3];
	char *b = malloc(n1 * rep);
	for (size_t i = 0; i < rep; i++) memcpy(b + i * n1, one, n1);
	Text *t = text_new(b, n1 * rep);
	Tsql *q = tsql_new(t);
	tsql_update(q);
	size_t nl = text_lines(t), last = nl > 60 ? nl - 60 : 0;
	double t0, t1;
	if (!strcmp(mode, "end")) {
		t0 = now(); frame(q, t, last); t1 = now();
	} else if (!strcmp(mode, "top")) {
		frame(q, t, last);
		tsql_edit(q, t, 0); text_insert(t, 0, "x", 1); tsql_update(q);
		t0 = now(); frame(q, t, last); t1 = now();
	} else if (!strcmp(mode, "open")) {
		frame(q, t, last); frame(q, t, 0);
		tsql_edit(q, t, 0); text_insert(t, 0, "/", 1); tsql_update(q);
		tsql_edit(q, t, 1); text_insert(t, 1, "*", 1); tsql_update(q);
		frame(q, t, 0);
		t0 = now(); frame(q, t, 0); t1 = now();
	} else if (!strcmp(mode, "key")) {
		frame(q, t, last);
		size_t p = text_line_start(t, last + 25);
		tsql_edit(q, t, p); text_insert(t, p, "x", 1); tsql_update(q);
		t0 = now(); frame(q, t, last); t1 = now();
	} else {
		size_t len = text_len(t), k = 0;
		static TsqlRd rd;
		rd.t = t;
		t0 = now();
		for (size_t p = 0, e; p < len; p = e, k++) { int kd; e = tsql_token(&rd, p, &kd); }
		t1 = now();
		printf("%zu tokens ", k);
	}
	printf("%s %.1f KB: %.3f ms\n", mode, n1 * rep / 1024.0, t1 - t0);
	return 0;
}
