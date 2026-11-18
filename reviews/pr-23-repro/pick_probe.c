/* Review of PR 23: probes of the picker through tools.h, with the real loop.
     pick_probe time DIR      fill time of the smart picker in DIR, then the time
                              inside tools_key for each key of "core" and 4 x BS
     pick_probe g DIR         g, CTRL-C, reopen, Esc, j: does j move the cursor?
     pick_probe tiny DIR      draw at every grid size from 1x1 to 40x130
     pick_probe show DIR KEYS draw after typing KEYS (smart)
     pick_probe buffers DIR KEYS FILE...  the buffers picker after typing KEYS,
                              with FILEs as buffers 2, 3, ... (ref/core.c has
                              one buffer, so they are linked in by hand, the
                              last one current, as after :e of each in turn) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "core.h"
#include "loop.h"
#include "tools.h"
static Editor e;
static Grid g;
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static void settle(void)
{
	double t0 = now();
	while (now() - t0 < 300) { core_changed = 0; loop_once(300); if (core_changed) t0 = now(); }
}
static void show(void)
{
	core_draw(&e, &g); tools_draw(&e, &g);
	for (int r = 0; r < g.rows; r++) {
		int rev = 0;      /* '*' marks the selected (reversed) row */
		for (int c = 0; c < g.cols; c++) rev |= g.c[r * g.cols + c].attr == ATTR_REV && r < g.rows - 2;
		putchar(rev ? '*' : ' ');
		for (int c = 0; c < g.cols; c++) { Cell *x = &g.c[r * g.cols + c]; fwrite(x->ch, 1, x->len, stdout); } putchar('\n'); }
}
int main(int argc, char **argv)
{
	if (argc < 3 || chdir(argv[2])) return 2;
	setenv("XDG_DATA_HOME", "/tmp/pick-probe-data", 1);
	if (core_open(&e, "/tmp/pick-probe-buf")) return 2;   /* a new, empty buffer */
	int rows = getenv("ROWS") ? atoi(getenv("ROWS")) : 40, cols = getenv("COLS") ? atoi(getenv("COLS")) : 140;
	grid_resize(&g, rows, cols); e.rows = rows; e.cols = cols;
	if (!strcmp(argv[1], "time")) {
		double t0 = now(), worst = 0, in = 0;
		tools_pick(&e, PICK_SMART);
		/* the fill: how long the loop is inside one loop_once (the editor
		   answers no key meanwhile) */
		for (double q = now(); now() - q < 500;) {
			core_changed = 0;
			double a = now(); loop_once(300); double d = now() - a;
			if (core_changed) { q = now(); in += d; if (d > worst) worst = d; }
		}
		core_draw(&e, &g); tools_draw(&e, &g);
		int r = 1; while (r < g.rows) { int c = 0; for (; c < g.cols; c++) if (g.c[r*g.cols+c].len == 1 && g.c[r*g.cols+c].ch[0] == '/') break; if (c < g.cols) break; r++; }
		printf("fill: %.0f ms wall, %.0f ms inside callbacks, worst single loop_once %.1f ms\n", now() - t0 - 500, in, worst);
		const int keys[] = { 'c', 'o', 'r', 'e', KEY_BS, KEY_BS, KEY_BS, KEY_BS };
		for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) {
			double a = now(); tools_key(&e, keys[i]); double d = now() - a;
			double b = now(); core_draw(&e, &g); tools_draw(&e, &g); double dd = now() - b;
			printf("key %-3s: %.1f ms in tools_key, %.2f ms draw\n", keys[i] == KEY_BS ? "BS" : (char[]){ (char)keys[i], 0 }, d, dd);
		}
	} else if (!strcmp(argv[1], "g")) {
		tools_pick(&e, PICK_SMART); settle();
		tools_key(&e, KEY_ESC); tools_key(&e, 'g'); tools_key(&e, KEY_CTRL_C);
		tools_pick(&e, PICK_SMART); settle();
		tools_key(&e, KEY_ESC);
		show();
		tools_key(&e, 'j');
		show();
	} else if (!strcmp(argv[1], "tiny")) {
		tools_pick(&e, PICK_SMART); settle();
		for (rows = 1; rows <= 40; rows++) for (cols = 1; cols <= 130; cols += (cols < 12 ? 1 : 7)) {
			grid_resize(&g, rows, cols); e.rows = rows; e.cols = cols;
			tools_key(&e, 'j');
			core_draw(&e, &g); tools_draw(&e, &g);
			if (g.crow < 0 || g.crow >= rows || g.ccol < 0 || g.ccol >= cols) printf("%dx%d: cursor at %d,%d outside the grid\n", rows, cols, g.crow, g.ccol);
		}
		printf("tiny: done\n");
	} else if (!strcmp(argv[1], "buffers")) {
		static Buf extra[16];
		Buf *last = e.bufs;
		for (int i = 4; i < argc && i < 20; i++) {
			extra[i - 4] = *e.bufs;          /* shares the text: only read here */
			extra[i - 4].nr = i - 2; extra[i - 4].path = argv[i]; extra[i - 4].next = NULL;
			last->next = &extra[i - 4]; last = last->next;
		}
		e.win->buf = last;
		tools_pick(&e, PICK_BUFFERS); settle();
		for (const char *s = argv[3]; *s; s++) tools_key(&e, (unsigned char)*s);
		show();
	} else if (!strcmp(argv[1], "show")) {
		tools_pick(&e, argc > 4 && !strcmp(argv[4], "grep") ? PICK_GREP : PICK_SMART); settle();
		for (const char *s = argv[3]; *s; s++) tools_key(&e, *s == '~' ? KEY_ESC : (unsigned char)*s);
		settle();
		show();
	}
	return 0;
}
