/* The core's side of flash_search.lua: "ab ab ab ab", /ab, 0ly3l, then the
   faces of the first row's cells (I IncSearch, S Search, C CurSearch). */
#include <stdio.h>
#include "core.h"
#include "keys.h"
#include "../src/core/core_int.h"
int main(void)
{
	static Editor e;
	Grid g = { 0 };
	FILE *f = fopen("/tmp/pr106-fs.txt", "w");
	fputs("ab ab ab ab\n", f);
	fclose(f);
	if (core_open(&e, "/tmp/pr106-fs.txt") || grid_resize(&g, 6, 30)) return 1;
	layout_resize(&e, 6, 30);
	for (const char *p = "/ab\rgg0ly3l"; *p; p++) keys_feed(&e, *p == '\r' ? KEY_ENTER : *p, p[1] != 0);
	core_update(&e);
	core_draw(&e, &g);
	for (int c = win_numw(e.win); c < win_numw(e.win) + 11; c++)
		putchar(g.c[c].bg == FACE_INC_SEARCH ? 'I' : g.c[c].bg == FACE_SEARCH ? 'S' : g.c[c].bg == FACE_CUR_SEARCH ? 'C' : '.');
	putchar('\n');
	return 0;
}
