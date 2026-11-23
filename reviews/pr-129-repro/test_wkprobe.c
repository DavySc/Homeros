/* Review probe for PR 129.  -DSIZES: keys_draw at every grid size from 1x1
   to 40x200, at three levels, under ASan.  -DTIME: ns per key of j/k through
   keys_input (compiles on main too), and with -DDRAW ns per keys_draw hidden
   and shown and per core_draw. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "keys.h"
#include "loop.h"

static int cmp(const void *a, const void *b) { long long x = *(const long long *)a, y = *(const long long *)b; return (x > y) - (x < y); }

#ifdef DRAW
static void wait_hint(Editor *e)
{
	long long t0 = loop_now();
	for (core_changed = 0; !core_changed && loop_now() - t0 < 1000000000LL;) loop_once(1000);
	(void)e;
}
#endif

int main(void)
{
	char path[] = "/tmp/wkprobeXXXXXX";
	int fd = mkstemp(path);
	CHECK(fd >= 0);
	for (int i = 0; i < 2000; i++) CHECK(write(fd, "some text on a line of the file\n", 32) == 32);
	close(fd);
	static Editor e;
	CHECK(!core_open(&e, path));
#ifdef SIZES
	const char *lv[] = { " ", " g", " i" };
	for (int l = 0; l < 3; l++) {
		for (const char *p = lv[l]; *p; p++) keys_input(&e, *p, 0);
		wait_hint(&e);
		for (int r = 1; r <= 40; r++)
			for (int c = 1; c <= 200; c++) {
				Grid g = { 0 };
				CHECK(!grid_resize(&g, r, c));
				for (int cr = 0; cr < r; cr += r > 8 ? r / 8 : 1) {   /* second review: the cursor's row moves the panel */
					g.crow = cr;
					grid_clear(&g);
					keys_draw(&e, &g);
				}
				g.crow = r - 1;
				keys_draw(&e, &g);
				grid_free(&g);
			}
		keys_input(&e, KEY_ESC, 0);
	}
	puts("sizes ok");
#endif
#ifdef TIME
	enum { N = 200000, R = 7 };
	long long t[R];
	for (int r = 0; r < R; r++) {
		long long t0 = loop_now();
		for (int i = 0; i < N; i++) { keys_input(&e, 'j', 0); keys_input(&e, 'k', 0); }
		t[r] = (loop_now() - t0) / (2 * N);
	}
	qsort(t, R, sizeof *t, cmp);
	printf("key j/k: median %lld ns (range %lld..%lld)\n", t[R / 2], t[0], t[R - 1]);
#ifdef DRAW
	Grid g = { 0 };
	CHECK(!grid_resize(&g, 60, 240));
	e.rows = 60; e.cols = 240;
	for (int r = 0; r < R; r++) {
		long long t0 = loop_now();
		for (int i = 0; i < 2000; i++) core_draw(&e, &g);
		t[r] = (loop_now() - t0) / 2000;
	}
	qsort(t, R, sizeof *t, cmp);
	printf("core_draw 60x240: median %lld ns (range %lld..%lld)\n", t[R / 2], t[0], t[R - 1]);
	for (int shown = 0; shown < 2; shown++) {
		if (shown) { keys_input(&e, ' ', 0); wait_hint(&e); }
		for (int r = 0; r < R; r++) {
			long long t0 = loop_now();
			for (int i = 0; i < 20000; i++) keys_draw(&e, &g);
			t[r] = (loop_now() - t0) / 20000;
		}
		qsort(t, R, sizeof *t, cmp);
		printf("keys_draw %s 60x240: median %lld ns (range %lld..%lld)\n", shown ? "shown" : "hidden", t[R / 2], t[0], t[R - 1]);
	}
#endif
#endif
	unlink(path);
	return 0;
}
