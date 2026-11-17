/* Review of PR 20: does core_update + core_draw allocate after the first
   frames?  malloc, calloc and realloc are wrapped (--wrap) and counted
   during 5000 frames of scrolling, window switching, popup and wrap changes
   on a file with long lines, after one warm-up pass over the same frames. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "core.h"
void *__real_malloc(size_t); void *__real_calloc(size_t, size_t); void *__real_realloc(void *, size_t);
static long count, on;
void *__wrap_malloc(size_t n) { count += on; return __real_malloc(n); }
void *__wrap_calloc(size_t a, size_t b) { count += on; return __real_calloc(a, b); }
void *__wrap_realloc(void *p, size_t n) { count += on; return __real_realloc(p, n); }
static const PumItem pum[] = { { "foo", "v", NULL }, { "foobar", "f", "module.c" }, { "x", NULL, "menu" } };
static long pass(Editor *e, Grid *g, int measure)
{
	long total = 0;
	srand(1);
	for (int i = 0; i < 5000; i++) {
		switch (rand() % 8) {
		case 0: core_scroll(e, rand() % 9 - 4); break;
		case 1: core_scroll_half(e, rand() % 2 ? 1 : -1, 0); break;
		case 2: core_scroll_page(e, rand() % 2 ? 1 : -1, 1); break;
		case 3: core_win_goto(e, 'w', 0); break;
		case 4: e->win->line = (size_t)rand() % core_lines(e); e->win->col = 0; core_set_want(e); break;
		case 5: core_set_wrap(e, rand() % 2); break;
		case 6: core_pum(e, pum, 3, rand() % 3, 0); break;
		case 7: core_pum(e, NULL, 0, -1, 0); break;
		}
		count = 0; on = measure;
		core_update(e); core_draw(e, g);
		on = 0; total += count;
	}
	return total;
}
int main(void)
{
	char path[] = "/tmp/allocXXXXXX";
	if (mkstemp(path) < 0) return 1;
	FILE *f = fopen(path, "w");
	for (int i = 0; i < 3000; i++) { for (int k = 0; k < (i % 7 == 0 ? 900 : i % 40); k++) fputs("wörd\t", f); fputc('\n', f); }
	fclose(f);
	static Editor e; Grid g = { 0 };
	if (core_open(&e, path) || grid_resize(&g, 40, 120) || core_split(&e, 0, 0) || core_split(&e, 1, 0)) return 1;
	pass(&e, &g, 0);
	printf("allocations in 5000 frames after warm-up: %ld\n", pass(&e, &g, 1));
	core_free(&e); unlink(path);
	return 0;
}
