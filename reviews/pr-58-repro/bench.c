/* bench FILE REPEAT MODE: frame costs of tsql.c, a frame read as core_draw
   reads it (50 lines, 256 bytes a call), then the idle update as main.c
   runs it, with the longest stretch in which it does not ask stop.
   MODE end:  fresh open, first frame at the end, update, frame again
        top:  warm, an edit at byte 0, a frame at the end, update, frame
        open: warm, slash-star typed at byte 0 (an unclosed comment), then
              a key typed inside it: frame at the top, update, frame
        key:  warm at the end, a 1-byte edit in the window, frame, update
        tok:  tsql_token over the whole text */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "text.h"
#include "../../src/syntax/tsql.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static double last, gap;
static int ask(void) { double t = now(); if (t - last > gap) gap = t - last; last = t; return 0; }
static void frame(Tsql *q, const Text *t, size_t l0)
{
	unsigned char fc[256];
	size_t nl = text_lines(t);
	for (size_t l = l0; l < l0 + 50 && l < nl; l++) {
		size_t a = text_line_start(t, l), z = text_line_start(t, l + 1);
		if (z > a) z--;
		for (; a < z; a += 256) memset(fc, 0, sizeof fc), tsql_faces(q, a, a + 256 < z ? a + 256 : z, fc);
	}
}
static Tsql *q;
static Text *t;
/* frame, update, and the frame again if the update asks */
static void round_(const char *what, size_t l0)
{
	double t0 = now();
	frame(q, t, l0);
	double t1 = last = now();
	gap = 0;
	int r = tsql_update(q, ask);
	ask();
	double t2 = now();
	if (r == 1) frame(q, t, l0);
	double t3 = now();
	printf("%s: frame %.3f ms, update %.3f ms (longest stretch %.3f)%s", what, t1 - t0, t2 - t1, gap, r == 1 ? ", redrawn " : "");
	if (r == 1) printf("%.3f ms", t3 - t2);
	printf("; ");
}
static void edit(size_t p, const char *s) { tsql_edit(q, t, p, 0, 1); text_insert(t, p, s, 1); }
int main(int argc, char **argv)
{
	static char one[1 << 20];
	FILE *in = fopen(argv[1], "rb");
	size_t n1 = fread(one, 1, sizeof one, in), rep = strtoul(argv[2], 0, 10);
	const char *mode = argv[3];
	char *b = malloc(n1 * rep);
	for (size_t i = 0; i < rep; i++) memcpy(b + i * n1, one, n1);
	t = text_new(b, n1 * rep);
	q = tsql_new(t);
	size_t nl = text_lines(t), end = nl > 60 ? nl - 60 : 0;
	printf("%s %.0f KB: ", mode, n1 * rep / 1024.0);
	if (!strcmp(mode, "end")) round_("first frame at the end", end);
	else if (!strcmp(mode, "tok")) {
		static TsqlRd rd;
		rd.t = t;
		size_t len = text_len(t), k = 0;
		double t0 = now();
		for (size_t p = 0, e; p < len; p = e, k++) { int kd; e = tsql_token(&rd, p, &kd); }
		printf("%zu tokens %.3f ms", k, now() - t0);
	} else {
		frame(q, t, 0);
		while (tsql_update(q, NULL)) ;
		frame(q, t, end);
		if (!strcmp(mode, "top")) edit(0, "x"), round_("edit at 0, frame at the end", end);
		else if (!strcmp(mode, "key")) edit(text_line_start(t, end + 25), "x"), round_("key in the window", end);
		else {
			frame(q, t, 0);
			edit(0, "/"), round_("slash", 0);
			edit(1, "*"), round_("star", 0);
			edit(2, "a"), round_("key in the comment", 0);
			edit(3, "b"), round_("next key", 0);
		}
	}
	printf("\n");
	return 0;
}
