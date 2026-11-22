/* Review of PR 111: drives lsp_goals against fake.py.  Built as a test
   (see README): ./drive <scenario> <dir of fake.py>.  Scenarios: sev, flood,
   cursor, small. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "core.h"
#include "grid.h"
#include "keys.h"
#include "loop.h"
#include "lsp.h"
#include "text.h"

static long long ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

static Editor e;
static Buf *gb(void) { for (Win *w = e.wins; w; w = w->next) if (!w->buf->lsp) return w->buf; return NULL; }
static void spin(int t) { for (long long t0 = ms(); ms() - t0 < t; loop_once(10)) ; }
static void show(const char *what)
{
	static char got[1 << 15];
	Buf *b = gb();
	size_t n = b ? text_read(b->text, 0, got, sizeof got - 1) : 0;
	got[n] = 0;
	printf("--- %s: goals window (%zu bytes, %zu lines):\n%.300s\n---\n", what, n, b ? text_lines(b->text) : 0, got);
}

int main(int argc, char **argv)
{
	if (argc < 3) return 2;
	setvbuf(stdout, NULL, _IONBF, 0);
	const char *scen = argv[1];
	static char fake[4096], log[256], cmd[8192];
	snprintf(fake, sizeof fake, "%s/fake.py", argv[2]);
	snprintf(log, sizeof log, "/tmp/rv111-%s.log", scen);
	remove(log);
	static const char *sargv[] = { "python3", fake, NULL, log, NULL };
	static const char *const roots[] = { "lakefile.toml", NULL };
	static LspServer table[2];
	sargv[2] = scen;
	table[0] = (LspServer){ ".lean", "lean4", sargv, roots, 1 };
	lsp_servers = table;
	if (system("rm -rf /tmp/rv111p && mkdir -p /tmp/rv111p && touch /tmp/rv111p/lakefile.toml")) return 2;
	if (core_open(&e, "/tmp/rv111p/x.lean") || !e.buf->lsp) { puts("no doc"); return 2; }
	if (!strcmp(scen, "small")) { e.rows = 3; e.cols = 20; } else { e.rows = 40; e.cols = 160; }
	const char *init = "theorem a : True := by\n  trivial\n\n";
	core_edit(&e, 0, 0, init, strlen(init));
	lsp_flush(e.buf->lsp);
	e.win->line = e.win->col = 0;
	Win *lean = e.win;
	lsp_goals(&e);
	if (!strcmp(scen, "small")) {
		int n = 0;
		for (Win *w = e.wins; w; w = w->next) n++;
		printf("windows=%d msg after lsp_goals: \"%s\"\n", n, e.msg);
		e.msg[0] = 0;
		keys_input(&e, 'j', 0); lsp_goals(&e);
		printf("msg after the next key (j): \"%s\"\n", e.msg);
		return 0;
	}
	spin(1500);
	show("settled on line 1");
	if (!strcmp(scen, "flood") || !strcmp(scen, "err")) {
		snprintf(cmd, sizeof cmd, "echo -n 'connect requests in 1.5 s without a key: '; grep -c connect %s", log);
		return system(cmd) != 0;
	}
	if (!strcmp(scen, "enter")) {                  /* the user goes to the goals window and presses G */
		core_win_goto(&e, 'l', 1);
		printf("in goals window: line %zu, core_lines %zu, text_len %zu\n", e.win->line, core_lines(&e), text_len(e.buf->text));
		keys_input(&e, 'G', 0);
		printf("after G: line %zu\n", e.win->line);
		keys_input(&e, 'A', 0); keys_input(&e, 'z', 0); keys_input(&e, 27, 0);
		printf("after Az<Esc>: text_len %zu, msg \"%s\"\n", text_len(e.buf->text), e.msg);
		keys_input(&e, 'u', 0);
		printf("after u: text_len %zu\n", text_len(e.buf->text));
	}
	if (!strcmp(scen, "stale")) {                  /* second review: line 2 always answers -32900 */
		keys_input(&e, 'j', 0); lsp_goals(&e);
		spin(1500);
		printf("cursor now on line %zu\n", e.win->line + 1);
		show("after j");
		snprintf(cmd, sizeof cmd, "echo -n 'connects: '; grep -c connect %s; echo -n 'calls: '; grep -c rpc/call %s", log, log);
		if (system(cmd)) return 1;
	}
	if (!strncmp(scen, "draw", 4)) {   /* what reaches the screen */
		Grid g = { 0 };
		grid_resize(&g, e.rows, e.cols);
		for (int k = scen[4] ? scen[4] - '0' : 0; k > 0; k--) { keys_input(&e, 'j', 0); lsp_goals(&e); }
		if (scen[4]) { spin(1500); show("after j"); }
		core_draw(&e, &g);
		Win *iv = lean->next ? lean->next : e.wins;
		printf("goals window drawn (x %d, w %d, h %d), non-blank rows:\n", iv->x, iv->w, iv->h);
		for (int r = iv->y; r < iv->y + iv->h; r++) {
			char row[1024]; size_t k = 0; int any = 0;
			for (int c = iv->x; c < iv->x + iv->w; c++) {
				const Cell *x = &g.c[r * g.cols + c];
				if (!x->len) continue;
				const unsigned char *t = grid_text(x);
				if (x->len != 1 || t[0] != ' ') any = 1;
				memcpy(row + k, t, x->len); k += x->len;
			}
			while (k && row[k - 1] == ' ') k--;
			if (any) printf("%2d|%.*s\n", r, (int)k, row);
		}
	}
	if (!strcmp(scen, "cursor")) {
		Grid g = { 0 };
		grid_resize(&g, e.rows, e.cols);
		core_win_goto(&e, 'l', 1);                 /* the user looks at the last goal */
		keys_input(&e, 'G', 0);
		Win *iv = e.win;
		printf("in goals window: line %zu of %zu\n", iv->line, core_lines(&e));
		core_win_goto(&e, 'p', 1);
		if (e.win != lean) { puts("not back"); return 2; }
		keys_input(&e, 'j', 0); lsp_goals(&e);     /* line 2: no goals */
		spin(1500);
		show("after j");
		printf("goals window's cursor: line %zu, top %zu; its buffer has %zu lines\n", iv->line, iv->top, text_lines(iv->buf->text));
		core_draw(&e, &g);
		puts("drawn");
		core_win_goto(&e, 'l', 1);
		printf("current line %zu of core_lines %zu\n", e.win->line, core_lines(&e));
		size_t n;
		const unsigned char *l = core_line(&e, e.win->line, &n);
		printf("core_line(cursor) -> %p, n=%zu\n", (void *)l, n);
		keys_input(&e, 'y', 0); keys_input(&e, 'y', 0);
		puts("yy done");
		keys_input(&e, 'x', 0);
		puts("x done");
	}
	core_free(&e);
	spin(200);
	return 0;
}
