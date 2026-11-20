/* Review of PR 59: what the kitten draws outside its recorded cases.
   usage: probe msg NAME | cmdheight | pick | tiny
   Built by reviews/pr-59-repro/run.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "core.h"
#include "kitten.h"
#include "loop.h"
#include "tools.h"

static Editor e;
static Grid g;
static char dir[] = "/tmp/kprobeXXXXXX", path[2048];

static void show(const char *what)
{
	printf("-- %s (%dx%d)\n", what, g.rows, g.cols);
	for (int r = 0; r < g.rows; r++) {
		for (int c = 0; c < g.cols; c++) {
			const Cell *x = &g.c[r * g.cols + c];
			fwrite(grid_text(x), 1, x->len, stdout);
		}
		printf("|\n");
		for (int c = 0; c < g.cols; c++) {   /* faces: . none, k any kitten face */
			int f = g.c[r * g.cols + c].face;
			putchar(f == FACE_NONE ? '.' : f >= FACE_KITTEN_CAT && f <= FACE_KITTEN_DIM ? 'k' : '?');
		}
		printf("|\n");
	}
}
static void draw(void) { core_draw(&e, &g); tools_draw(&e, &g); kitten_draw(&e, &g); }
static void wait_frame(void) { core_changed = 0; while (!core_changed) loop_once(3000); }
static void play(int upto) { for (int i = 0; i < upto; i++) { draw(); wait_frame(); } }

int main(int argc, char **argv)
{
	if (!mkdtemp(dir)) return 1;
	const char *what = argc > 1 ? argv[1] : "";
	snprintf(path, sizeof path, "%s/%s", dir, argc > 2 ? argv[2] : "notes.org");
	if (!strcmp(what, "tiny")) {
		/* every frame on every small screen: ASan and UBSan say if a cell is out of the grid */
		core_open(&e, path);
		for (int rows = 1; rows <= 6; rows++) for (int cols = 1; cols <= 18; cols++) {
			grid_resize(&g, rows, cols);
			kitten_paw(&e);
			for (int i = 0; i < 6; i++) {
				grid_clear(&g); kitten_draw(&e, &g);
				if (i < 5) wait_frame();
			}
			kitten_key();
		}
		puts("tiny: no report from the sanitizers");
		return 0;
	}
	if (core_open(&e, path)) return 1;
	if (!strcmp(what, "bench")) {
		/* the cost of kitten_draw per frame: the last frame (the widest), then the tinted message */
		struct timespec a, b;
		grid_resize(&g, 50, 200);
		core_draw(&e, &g);
		core_write(&e, e.buf, 0);
		play(5);
		enum { N = 200000 };
		for (int k = 0; k < 2; k++) {
			clock_gettime(CLOCK_MONOTONIC, &a);
			for (int i = 0; i < N; i++) kitten_draw(&e, &g);
			clock_gettime(CLOCK_MONOTONIC, &b);
			printf("%s: %.0f ns per draw\n", k ? "message" : "last frame", ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / N);
			if (!k) { wait_frame(); core_draw(&e, &g); }
		}
		return 0;
	}
	grid_resize(&g, 10, 40);
	draw();
	if (!strcmp(what, "cmdheight")) { core_resize(&e, 0, -2, 0); draw(); }
	core_write(&e, e.buf, 0);
	if (!strcmp(what, "msg") || !strcmp(what, "cmdheight")) {
		play(5); draw(); show("last frame");
		wait_frame(); draw(); show("the message");
	} else if (!strcmp(what, "pick")) {
		play(6);
		tools_pick(&e, 0);   /* called directly: the key that opens it changes nothing else */
		draw(); show("picker open over the message");
	}
	return 0;
}
