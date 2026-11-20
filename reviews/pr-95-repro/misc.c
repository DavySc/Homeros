/* PR 95 review probes: buffer identity through a symlink and a dotted
   path; the mode pill while an operator waits; core_draw's cost with the
   cursor at the end of one long line.
   usage: misc <dir> [bench <bytes> <frames>] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "grid.h"
#include "keys.h"

static void rowtext(const Grid *g, int r)
{
	for (int c = 0; c < g->cols; c++) { const Cell *z = &g->c[r * g->cols + c]; printf("%.*s", z->len, grid_text(z)); }
	printf("\n");
}

int main(int argc, char **argv)
{
	static Editor e;
	static Grid g;
	CHECK(argc > 1 && !chdir(argv[1]) && !setenv("HOME", argv[1], 1));
	if (argc > 4) {
		size_t n = strtoul(argv[3], NULL, 10);
		int frames = atoi(argv[4]);
		FILE *f = fopen("long", "w");
		for (size_t i = 0; i < n; i++) fputc('a' + (int)(i % 26), f);
		fputc('\n', f);
		CHECK(!fclose(f) && !core_open(&e, "long") && !grid_resize(&g, 40, 120));
		e.win->col = n - 1;
		core_set_want(&e);
		core_draw(&e, &g);
		struct timespec a, b;
		clock_gettime(CLOCK_MONOTONIC, &a);
		for (int i = 0; i < frames; i++) core_draw(&e, &g);
		clock_gettime(CLOCK_MONOTONIC, &b);
		printf("%.1f us/frame\n", ((double)(b.tv_sec - a.tv_sec) * 1e9 + (double)(b.tv_nsec - a.tv_nsec)) / 1e3 / frames);
		return 0;
	}
	FILE *f = fopen("a", "w");
	CHECK(f && fputs("one two\n", f) >= 0 && !fclose(f) && !symlink("a", "l") && !link("a", "h"));
	CHECK(!core_open(&e, "a") && !grid_resize(&g, 10, 80));
	static const char *const names[] = { "./a", "l", "h", "../x/a" };
	for (int i = 0; i < 4; i++) {
		if (core_edit_file(&e, names[i], 0)) printf(":e %s failed: %s\n", names[i], e.msg);
		int k = 0;
		for (Buf *b = e.bufs; b; b = b->next) k++;
		printf(":e %s -> %d buffers\n", names[i], k);
	}
	keys_feed(&e, 'd', 0);
	core_draw(&e, &g);
	printf("after d: ");
	rowtext(&g, e.win->y + e.win->h);
	return 0;
}
