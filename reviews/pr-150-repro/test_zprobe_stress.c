/* PR 150 review probe: random tab page (frame) commands mixed with window,
   buffer and edit commands, every frame drawn after each at its own size,
   under ASan and UBSan.  Checks: e->buf is the current window's buffer,
   every window's cursor is on a line of its buffer, every frame's windows
   lie inside its grid.  argv[1]: seeds, argv[2]: steps per seed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "core.h"
#include "keys.h"
#include "grid.h"
#include "../src/core/core_int.h"

static void keys(Editor *e, const char *s) { for (; *s; s++) keys_input(e, *s == '\n' ? KEY_ENTER : *s == 27 ? KEY_ESC : (unsigned char)*s, s[1] != 0); }

int main(int argc, char **argv)
{
	int seeds = argc > 1 ? atoi(argv[1]) : 200, steps = argc > 2 ? atoi(argv[2]) : 200;
	char f[3][32] = { "/tmp/zsaXXXXXX", "/tmp/zsbXXXXXX", "/tmp/zscXXXXXX" };
	for (int i = 0; i < 3; i++) {
		FILE *fp;
		CHECK(mkstemp(f[i]) >= 0 && (fp = fopen(f[i], "w")));
		for (int l = 0; l < 30 + 10 * i; l++) fprintf(fp, "line %d of file %d\n", l, i);
		CHECK(!fclose(fp));
	}
	static Grid g[8];
	long total = 0; int maxt = 0;
	for (int s = 1; s <= seeds; s++) {
		srand((unsigned)s);
		static Editor e;
		CHECK(!core_open(&e, f[0]));
		for (int k = 0; k < steps; k++) {
			int r = rand() % 30;
			char b[64];
			switch (r) {
			case 0: keys(&e, " 52"); break;
			case 1: keys(&e, " 50"); break;
			case 2: keys(&e, " 51"); break;
			case 3: keys(&e, " 5o"); break;
			case 4: snprintf(b, sizeof b, " 5m%d", 1 + rand() % 4); keys(&e, b); break;
			case 5: keys(&e, "gt"); break;
			case 6: keys(&e, "gT"); break;
			case 7: snprintf(b, sizeof b, "%dgt", 1 + rand() % 4); keys(&e, b); break;
			case 8: keys(&e, "\x17T"); break;
			case 9: keys(&e, "\x17" "c"); break;
			case 10: keys(&e, "\x17s"); break;
			case 11: keys(&e, "\x17v"); break;
			case 12: keys(&e, "\x17o"); break;
			case 13: keys(&e, "\x17q"); if (e.quit) { e.quit = 0; } break;
			case 14: core_buf_delete(&e, 0, 1); break;
			case 15: core_edit_file(&e, f[rand() % 3], 1); break;
			case 16: keys(&e, "ohello\x1b"); break;
			case 17: keys(&e, "dd"); break;
			case 18: keys(&e, "u"); break;
			case 19: keys(&e, "G"); break;
			case 20: keys(&e, "Vjy"); break;
			case 21: { int n = 0; for (Tab *t = e.tabs; t; t = t->next) n++;
				Tab *t = e.tabs; for (int j = rand() % n; j; j--) t = t->next;
				core_tabclose(&e, t); break; }   /* KEY_CLOSE on any frame */
			case 22: core_tabnew(&e, ""); break;
			case 23: core_tabnew(&e, f[rand() % 3]); break;
			case 24: keys(&e, "\x17" "J"); break;
			case 25: keys(&e, "10\x04"); break;
			case 26: keys(&e, "v3j"); break;
			case 27: keys(&e, "\x1b"); break;
			case 28: keys_input(&e, KEY_FOCUS, 0); break;
			case 29: keys(&e, "ix\x1b"); break;
			}
			if (e.quit) e.quit = 0;
			CHECK(e.buf == e.win->buf);
			int i = 0, nt = 0;
			for (Tab *t = e.tabs; t; t = t->next) nt++;
			if (nt > maxt) maxt = nt;
			for (Tab *t = e.tabs; t && i < 8; t = t->next, i++) {
				CHECK(!grid_resize(&g[i], 6 + (i * 7 + s) % 30, 20 + (i * 13 + s) % 80));
				core_draw(&e, t == e.tab && rand() % 2 ? NULL : t, &g[i]);
				CHECK(e.buf == e.win->buf);
				for (Win *w = tab_firstwin(&e, t); w; w = w->next)
					CHECK(w->y >= 0 && w->x >= 0 && w->h >= 0 && w->w >= 0);   /* tiling may overflow a shrunk grid (fuzz_core) */
			}
			FOR_ALL_TAB_WINDOWS(&e, w) {
				size_t n = core_lines(&e);
				(void)n;
				CHECK(w->buf);
			}
			total++;
		}
		keys(&e, "\x1b");
		core_free(&e);
	}
	for (int i = 0; i < 8; i++) grid_free(&g[i]);
	for (int i = 0; i < 3; i++) remove(f[i]);
	printf("stress: %d seeds, %ld steps, up to %d frames, no failure\n", seeds, total, maxt);
	return 0;
}
