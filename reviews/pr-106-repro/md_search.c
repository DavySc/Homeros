/* 'hlsearch' on rendered Markdown lines (the merge with PR 84): "/ab" in a
   .md file, cursor on the last line; prints each row's text and the cells
   drawn Search (S) or CurSearch (C). */
#include <stdio.h>
#include "core.h"
#include "keys.h"
#include "../src/core/core_int.h"
int main(void)
{
	static Editor e;
	Grid g = { 0 };
	FILE *f = fopen("/tmp/pr106-s.md", "w");
	fputs("# ab ab\n\n- ab item\n\n| ab | cd |\n|----|----|\n| x  | ab |\n\n`ab` **ab** ab\nab\n", f);
	fclose(f);
	if (core_open(&e, "/tmp/pr106-s.md") || grid_resize(&g, 14, 30)) return 1;
	layout_resize(&e, 14, 30);
	for (const char *p = "/ab\rG"; *p; p++) keys_feed(&e, *p == '\r' ? KEY_ENTER : *p, p[1] != 0);
	core_update(&e);
	core_draw(&e, &g);
	for (int r = 0; r < 10; r++) {
		for (int c = 0; c < 24; c++) { const Cell *z = &g.c[r * g.cols + c]; if (z->len) fwrite(z->ch, 1, z->len, stdout); }
		printf("\n");
		for (int c = 0; c < 24; c++) putchar(g.c[r * g.cols + c].bg == FACE_SEARCH ? 'S' : g.c[r * g.cols + c].bg == FACE_CUR_SEARCH ? 'C' : '.');
		printf("\n");
	}
	return 0;
}
