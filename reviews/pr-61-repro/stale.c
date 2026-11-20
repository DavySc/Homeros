/* PR 61 review: the start screen stays over a file opened from it.
   core_edit_file reuses the empty start buffer (buflist_new), so e->buf,
   its undo_seq and the mode are all as when the dashboard opened.
   Build as a test: cp stale.c tests/test_pr61_stale.c && make test TESTS=pr61_stale */
#include <stdio.h>
#include <string.h>
#include "check.h"
#include "core.h"
#include "tools.h"

int main(void)
{
	static Editor e;
	Grid g = { 0 };
	FILE *f = fopen("/var/tmp/pr61-stale.txt", "w");
	CHECK(f && fputs("hello\n", f) >= 0 && !fclose(f));
	CHECK(!core_open(&e, NULL));
	CHECK(!grid_resize(&g, 24, 80));
	tools_dash(&e, 0);
	core_draw(&e, &g); tools_draw(&e, &g);
	CHECK(!core_edit_file(&e, "/var/tmp/pr61-stale.txt", 0));   /* what the pickers' confirm calls */
	core_draw(&e, &g); tools_draw(&e, &g);
	int hello = 0, dash = 0;
	for (int i = 0; i < g.rows * g.cols; i++) {
		if (g.c[i].len != 1) continue;
		hello |= grid_text(&g.c[i])[0] == 'h' && i % g.cols < 10;
		dash |= grid_text(&g.c[i])[0] == 'Q';   /* "Quit" */
	}
	printf("file text shown: %d, dashboard shown: %d\n", hello, dash);
	CHECK(!tools_key(&e, 'q'));   /* q is the editor's key in a file */
	CHECK(!dash);
	return 0;
}
