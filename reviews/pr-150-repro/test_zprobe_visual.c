/* PR 150 review probe: a frame that is not current is drawn with the
   current frame's Visual area, yank flash and :s match (core_draw(e, t, g)).
   Frame 1 shows a.txt, frame 2 shows b.txt (another buffer).  In frame 1:
   V j (Visual over lines 1-2).  Drawing frame 2 must show no Visual cell:
   its buffer has no Visual area. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "core.h"
#include "keys.h"
#include "grid.h"
#include "../src/core/core_int.h"

static int count(const Grid *g, int face)
{
	int n = 0;
	for (int i = 0; i < g->rows * g->cols; i++) n += g->c[i].bg == face;
	return n;
}

int main(void)
{
	static Editor e;
	static Grid g1, g2;
	char a[] = "/tmp/zpaXXXXXX", b[] = "/tmp/zpbXXXXXX";
	FILE *f;
	CHECK(mkstemp(a) >= 0 && mkstemp(b) >= 0);
	CHECK((f = fopen(a, "w")) && fputs("aaaa\nbbbb\ncccc\n", f) >= 0 && !fclose(f));
	CHECK((f = fopen(b, "w")) && fputs("1111\n2222\n3333\n4444\n", f) >= 0 && !fclose(f));
	CHECK(!core_open(&e, a) && !grid_resize(&g1, 10, 40) && !grid_resize(&g2, 12, 50));
	core_draw(&e, NULL, &g1);
	Tab *t1 = e.tab;
	CHECK(!core_tabnew(&e, b));
	Tab *t2 = e.tab;
	core_draw(&e, NULL, &g2);
	e.win->line = 3;                      /* frame 2's cursor on its last line */
	core_draw(&e, NULL, &g2);
	core_tab_enter(&e, t1);
	core_draw(&e, NULL, &g1);
	int none = count(&g2, FACE_VISUAL);
	keys_feed(&e, 'V', 0);
	keys_feed(&e, 'j', 0);
	core_draw(&e, NULL, &g1);
	core_draw(&e, t2, &g2);
	printf("visual: frame 1 has %d Visual cells; frame 2 (other buffer) has %d (was %d)\n",
	       count(&g1, FACE_VISUAL), count(&g2, FACE_VISUAL), none);
	keys_feed(&e, KEY_ESC, 0);
	/* yank flash: yy in frame 1, then frame 2 drawn */
	keys_feed(&e, 'y', 0);
	keys_feed(&e, 'y', 0);
	core_draw(&e, NULL, &g1);
	core_draw(&e, t2, &g2);
	printf("yank: frame 1 has %d flash cells; frame 2 has %d\n", count(&g1, FACE_INC_SEARCH), count(&g2, FACE_INC_SEARCH));
	int bad = count(&g2, FACE_VISUAL) != none || count(&g2, FACE_INC_SEARCH);
	keys_feed(&e, KEY_ESC, 0);
	core_free(&e);
	grid_free(&g1);
	grid_free(&g2);
	remove(a);
	remove(b);
	return bad;
}
