/* PR 95 review probe: a branch name and a file name that together pass the
   status line's 4400-byte buffer; every width under ASan. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "git.h"
#include "grid.h"
#include "loop.h"

int main(void)
{
	static Editor e;
	static Grid g;
	static char cmd[8192], br[4200], name[300];
	char dir[] = "/tmp/pXXXXXX";
	CHECK(mkdtemp(dir) && !chdir(dir) && !setenv("HOME", dir, 1));
	for (int i = 0; i < 19; i++) { memset(br + strlen(br), 'b', 200); strcat(br, i < 18 ? "/" : ""); }
	for (int i = 0; i < 80; i++) strcat(name, "\xe6\xbc\xa2");
	strcat(name, ".sql");
	snprintf(cmd, sizeof cmd, "git init -q -b %s", br);
	CHECK(!system(cmd));
	FILE *f = fopen(name, "w");
	CHECK(f && fputs("select 1\n", f) >= 0 && !fclose(f) && !core_open(&e, name));
	for (int i = 0; i < 200 && !git_branch(e.buf); i++) CHECK(loop_once(20) == 0);
	printf("branch %zu bytes, name %zu bytes\n", git_branch(e.buf) ? strlen(git_branch(e.buf)) : 0, strlen(name));
	CHECK(!core_edit(&e, 0, 1, "", 0));
	for (int c = 1; c <= 400; c++) {
		CHECK(!grid_resize(&g, 6, c));
		core_draw(&e, &g);
	}
	CHECK(!grid_resize(&g, 6, 5000));
	core_draw(&e, &g);
	int r = e.win->y + e.win->h, last = 0;
	for (int c = 0; c < g.cols; c++) if (g.c[r * g.cols + c].len && g.c[r * g.cols + c].ch[0] != ' ') last = c;
	printf("5000 columns: last cell with text %d: ", last);
	for (int c = last - 30; c <= last; c++) printf("%.*s", g.c[r * g.cols + c].len, grid_text(&g.c[r * g.cols + c]));
	printf("\n");
	core_free(&e);
	grid_free(&g);
	return 0;
}
