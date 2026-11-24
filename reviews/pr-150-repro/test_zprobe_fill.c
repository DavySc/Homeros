/* PR 150 review probe: core_buf_fill (git status, the compile buffer, the
   Lean goals, SQL results) clamps the cursors of the windows on the buffer
   in the current tab page only.  Frame 1 shows a 40-line file with its
   cursor on line 35; from frame 2 the buffer is refilled with 2 lines;
   frame 1 is then drawn and entered. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "check.h"
#include "core.h"
#include "keys.h"
#include "grid.h"
#include "../src/core/core_int.h"

int main(void)
{
	static Editor e;
	static Grid g;
	char a[] = "/tmp/zfaXXXXXX";
	FILE *f;
	CHECK(mkstemp(a) >= 0 && (f = fopen(a, "w")));
	for (int i = 0; i < 40; i++) fprintf(f, "line %d\n", i);
	CHECK(!fclose(f) && !core_open(&e, a) && !grid_resize(&g, 20, 60));
	core_goto(&e, 35, 0);
	core_draw(&e, NULL, &g);
	Tab *t1 = e.tab;
	Buf *b = e.buf;
	CHECK(!core_tabnew(&e, ""));
	core_buf_fill(&e, b, "x\ny\n", 4, SIZE_MAX);
	printf("fill: frame 1's window on the refilled buffer: cursor line %zu, top %zu, buffer has %zu lines\n",
	       t1->win->line, t1->win->top, (size_t)2);
	core_draw(&e, t1, &g);   /* frame 1 drawn as its OS window shows it */
	int text = 0;
	for (int r = 0; r < g.rows - 2; r++) {
		char row[64] = { 0 };
		for (int c = 0; c < 10 && c < g.cols; c++) row[c] = (char)(g.c[r * g.cols + c].len ? *grid_text(&g.c[r * g.cols + c]) : ' ');
		if (r < 3) printf("fill: frame 1 row %d: '%s'\n", r, row);
		text += row[4] == 'x' || row[4] == 'y';
	}
	printf("fill: frame 1 shows %d of the buffer's 2 lines\n", text);
	core_tab_enter(&e, t1);
	core_draw(&e, NULL, &g);
	keys_feed(&e, 'x', 0);
	core_draw(&e, NULL, &g);
	printf("fill: after entering frame 1 and x: cursor line %zu\n", e.win->line);
	core_free(&e);
	grid_free(&g);
	remove(a);
	return 0;
}
