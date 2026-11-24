/* PR 156 review probe: a file handed over (main.c's steps: KEY_FOCUS, then
   core_tabnew) arriving in every half-done state, mixed with random frame
   and edit commands, every frame drawn after each, under ASan and UBSan.
   After each hand-over: Normal mode, no Visual, nothing pending, a new tab
   page current (unless the open failed), and the keys "ix<Esc>" then edit
   the new buffer.  argv[1]: seeds, argv[2]: steps per seed. */
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
	char f[3][32] = { "/tmp/zhaXXXXXX", "/tmp/zhbXXXXXX", "/tmp/zhcXXXXXX" };
	for (int i = 0; i < 3; i++) {
		FILE *fp;
		CHECK(mkstemp(f[i]) >= 0 && (fp = fopen(f[i], "w")));
		for (int l = 0; l < 30 + 10 * i; l++) fprintf(fp, "line %d of file %d\n", l, i);
		CHECK(!fclose(fp));
	}
	static const char *half[] = { "i", "ihello", "v", "V", "\x16", "vj", "d", "2d", "c", "\"a", ":", ":s/li", "/fi", "r", "f", " ", " 5", "g", "z",
	                              "\x17", "q", "qa", "y", "3", "ihel\x0e", "o", "R", "gU", "m", "'", "@", "ciw", "A", ":g/x/", "?", " 5m", "Vj:", ">" , "!" };
	static Grid g[8];
	long total = 0, hands = 0, bad = 0; int maxt = 0;
	for (int s = 1; s <= seeds; s++) {
		srand((unsigned)s);
		static Editor e;
		CHECK(!core_open(&e, f[0]));
		for (int k = 0; k < steps; k++) {
			int r = rand() % 16;
			switch (r) {
			case 0: keys(&e, " 52"); break;
			case 1: keys(&e, " 50"); break;
			case 2: keys(&e, " 5o"); break;
			case 3: keys(&e, "gt"); break;
			case 4: keys(&e, "\x17s"); break;
			case 5: keys(&e, "\x17" "c"); break;
			case 6: keys(&e, "ohello\x1b"); break;
			case 7: keys(&e, "dd"); break;
			case 8: keys(&e, "u"); break;
			case 9: core_edit_file(&e, f[rand() % 3], 1); break;
			case 10: { int n = 0; for (Tab *t = e.tabs; t; t = t->next) n++;
				Tab *t = e.tabs; for (int j = rand() % n; j; j--) t = t->next;
				core_tabclose(&e, t); break; }
			default: {
				/* a half-done state, then the hand-over as main.c does it */
				int h = rand() % (int)(sizeof half / sizeof *half);
				keys(&e, half[h]);
				if (e.quit) { e.quit = 0; break; }
				int nt = 0; for (Tab *t = e.tabs; t; t = t->next) nt++;
				if (nt >= 8) { keys(&e, "\x1b\x1b"); break; }
				int was = e.tab->id, w = rand() % 5;
				const char *p = w < 3 ? f[w] : w == 3 ? "" : "/tmp/zh-new-file";
				e.quit = 0;
				keys_input(&e, KEY_FOCUS, 0);
				int rc = core_tabnew(&e, p);
				hands++;
				int ok = e.mode == MODE_NORMAL && !e.ks.visual && !e.ks.pending && (rc || e.tab->id != was);
				if (ok && !rc) {
					/* the next keys act on the new buffer as Normal-mode keys */
					size_t n0 = core_lines(&e);
					Buf *bb = e.buf;
					keys(&e, "oZ\x1b");
					ok = e.buf == bb && core_lines(&e) == n0 + 1 && e.mode == MODE_NORMAL;
					if (ok) keys(&e, "u");
				}
				if (!ok) { bad++; if (bad <= 20) printf("seed %d step %d: after \"%s\" + hand-over of \"%s\": rc %d mode %d visual %d pending %d tab %d was %d lines %zu msg \"%s\"\n", s, k, half[h], p, rc, e.mode, e.ks.visual, e.ks.pending, e.tab->id, was, core_lines(&e), e.msg); keys(&e, "\x1b\x1b"); }
				break; }
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
			}
			total++;
		}
		keys(&e, "\x1b\x1b");
		core_free(&e);
	}
	for (int i = 0; i < 8; i++) grid_free(&g[i]);
	for (int i = 0; i < 3; i++) remove(f[i]);
	printf("hand: %d seeds, %ld steps, %ld hand-overs, up to %d frames, %ld left a state half done\n", seeds, total, hands, maxt, bad);
	return bad != 0;
}
