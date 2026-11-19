/* libFuzzer: any sequence of window, buffer, scrolling, editing, undo and
   mark commands, drawn after each at a size the input picks, leaves the
   editor consistent: the windows tiling the screen until it shrinks,
   every cursor on a line of its buffer, the current one on the first
   byte of a character, the current window showing its cursor line, every
   set mark a-z on a line of its buffer until a :e or :bd, and nothing ASan or UBSan objects to. */
#include <stdint.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core.h"
#include "grid.h"
#include "uni.h"

static char dir[64], files[3][80];   /* absolute: libFuzzer writes its findings to the working directory */
static const char *texts[] = {
	"ab \xce\xbbx \xe1\x83\x90\xf0\x90\x90\x80\n\tef\n\n  gh\n",
	"",
	"x\ny\nz\n\tw\nlong line long line long line long line long line long line\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n",
};
static const char *ins[] = { "a", "\n", "\xce\xbb", "\t", "word ", "\xe1\x83\x90\n\n", "\xf0\x90\x90\x80", "  ", "0123456789012345678901234567890123456789" };
static const PumItem pum[] = { { "foo", "v", NULL }, { "foobar", "f", "module.c" }, { "\xce\xbbx", NULL, "menu" } };

/* the byte at or before p that starts a character of line l */
static size_t boundary(Editor *e, size_t l, size_t col)
{
	size_t len, p = 0;
	const unsigned char *s = core_line(e, l, &len);
	if (col > len) col = len;
	for (size_t k; p < col && p + (k = uni_len(s, len, p)) <= col; p += k) {}
	return p;
}

static void put_cursor(Editor *e, size_t l, size_t col)
{
	size_t n = core_lines(e);
	e->win->line = l % n;
	e->win->col = boundary(e, e->win->line, col);
	core_set_want(e);
}

/* a buffer read again keeps its marks past a shorter text (measured: 'a
   on line 7 of 3 after :e!, on line 6 of 3 after :bd! and :b; Neovim's E19
   when used), so marks are checked only until a :e or :bd */
static int reloaded;

static int rows, cols, shrunk;

static void check(Editor *e)
{
	static unsigned char cover[256][256];
	memset(cover, 0, sizeof cover);
	if (e->buf != e->win->buf) abort();
	int found = 0;
	for (Win *w = e->wins; w; w = w->next) {
		found |= w == e->win;
		if (!w->buf || !w->buf->text) abort();
		/* a terminal that shrinks below what the windows need leaves a
		   layout Neovim itself bends (measured: windows 0 columns wide
		   after :set columns=12 and :split), so the windows are checked
		   to tile only until a shrink */
		if (!shrunk) {
			if (w->y < 0 || w->x < 0 || w->h < 0 || w->w < 0 || w->y + w->h >= rows || w->x + w->w > cols) abort();
			for (int r = w->y; r < w->y + w->h; r++)
				for (int c = w->x; c < w->x + w->w; c++)
					if (cover[r][c]++) abort();
		}
		if (w->line >= text_lines(w->buf->text)) abort();
	}
	if (!found) abort();
	/* another window's column is left alone by a change before it, as in
	   Neovim, until it is entered: only the current one's is checked */
	if (e->win->col > core_line_len(e, e->win->line) || boundary(e, e->win->line, e->win->col) != e->win->col) abort();
	if (e->win->h > 0 && e->win->top > e->win->line) abort();
	for (Buf *b = e->bufs; b && !reloaded; b = b->next) {
		if (!b->text) continue;
		size_t n = text_lines(b->text);
		for (int k = 0; k < 26; k++)
			if (b->mark[k].set && b->mark[k].line >= n) abort();
	}
}


static unsigned long hsh; static int verbose;
static void mix(unsigned long v) { hsh = (hsh ^ v) * 1099511628211ul; }
static void dump(Editor *e, Grid *g)
{
	for (Win *w = e->wins; w; w = w->next) {
		mix((unsigned long)w->x); mix((unsigned long)w->y); mix((unsigned long)w->w); mix((unsigned long)w->h);
		mix(w->line); mix(w->col); mix(w->top); mix(w == e->win);
	}
	for (int i = 0; i < g->rows * g->cols; i++) {
		const Cell *c = &g->c[i];
		const unsigned char *t = grid_text(c);
		mix(c->len); mix(c->attr);
		for (int k = 0; k < c->len; k++) mix(t[k]);
	}
	if (verbose) {
		printf("-- rows %d cols %d cursor %d,%d msg '%s'\n", g->rows, g->cols, g->crow, g->ccol, e->msg);
		for (Win *w = e->wins; w; w = w->next) printf("  win%s x%d y%d w%d h%d line%zu col%zu top%zu\n", w == e->win ? "*" : "", w->x, w->y, w->w, w->h, w->line, w->col, w->top);
		for (int r = 0; r < g->rows; r++) { printf("  |"); for (int c = 0; c < g->cols; c++) { const Cell *q = &g->c[r * g->cols + c]; if (q->len) fwrite(grid_text(q), 1, q->len, stdout); } printf("|\n"); }
	}
	mix((unsigned long)g->crow); mix((unsigned long)g->ccol);
	for (const char *m = e->msg; *m; m++) mix((unsigned char)*m);
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	static Editor e;
	static Grid g;
	if (!dir[0]) {
		snprintf(dir, sizeof dir, "/tmp/hdd-%s", getenv("HDD"));
		mkdir(dir, 0700);
		for (int i = 0; i < 3; i++) {
			snprintf(files[i], sizeof files[i], "%s/f%d", dir, i);
			FILE *f = fopen(files[i], "w");
			if (!f) abort();
			fputs(texts[i], f);
			fclose(f);
		}
	}
	if (core_open(&e, files[0])) abort();
	reloaded = 0;
	rows = 12, cols = 40, shrunk = 0;
	if (grid_resize(&g, rows, cols)) abort();
	for (size_t i = 0; i + 1 < n; i += 2) {
		unsigned op = d[i] % 26, a = d[i + 1];
		long cnt = a % 5;
		if (e.npum) core_pum(&e, NULL, 0, -1, 0);
		switch (op) {
		case 0: core_split(&e, a & 1, (int)(a >> 4)); break;
		case 1: core_close(&e, a & 1); break;
		case 2: core_only(&e); break;
		case 3: core_win_goto(&e, "wWhjkltbp"[a % 9], cnt); break;
		case 4: core_win_move(&e, "HJKLrRx"[a % 7], cnt); break;
		case 5: core_resize(&e, a & 1, (long)(a >> 2) - 32, (a >> 1) & 1); break;
		case 6: core_equal(&e); break;
		case 7: reloaded = 1; core_edit_file(&e, a % 4 == 3 ? "" : files[a % 4], a >> 7); break;
		case 8: core_buf_goto(&e, (int)(a % 6)); break;
		case 9: core_buf_next(&e, a & 1 ? 1 + cnt : -1 - cnt); break;
		case 10: core_buf_alt(&e); break;
		case 11: reloaded = 1; core_buf_delete(&e, a & 1 ? (int)(a % 6) : 0, 1); break;
		case 12: core_new(&e, a & 1); break;
		case 13: core_scroll(&e, (long)(a % 9) - 4); break;
		case 14: core_scroll_half(&e, a & 1 ? 1 : -1, cnt); break;
		case 15: core_scroll_page(&e, a & 1 ? 1 : -1, cnt); break;
		case 16: core_scroll_cursor(&e, "tzb"[a % 3]); break;
		case 17: core_set_wrap(&e, a & 1); break;
		case 18: {                    /* an insert at the cursor, which moves past it */
			const char *s = ins[a % (sizeof ins / sizeof *ins)];
			size_t at = core_line_start(&e, e.win->line) + e.win->col;
			if (!core_edit(&e, at, 0, s, strlen(s))) {
				size_t l = text_line_of(e.buf->text, at + strlen(s));
				put_cursor(&e, l, at + strlen(s) - core_line_start(&e, l));
			}
			break;
		}
		case 19: {                    /* lines from the cursor, as dd does */
			size_t l = e.win->line, z = l + 1 + cnt;
			if (!core_delete_lines(&e, l, z > core_lines(&e) ? core_lines(&e) : z)) put_cursor(&e, l < core_lines(&e) ? l : core_lines(&e) - 1, 0);
			break;
		}
		case 20: core_undo(&e, a & 1); break;
		case 21: e.buf->mark[a % 26] = (Mark){ e.win->line, e.win->col, 1 }; break;
		case 22: put_cursor(&e, a * 7u, a % 13); break;
		case 23:
			shrunk |= 3 + (int)a % 30 < rows || 12 + (int)(a * 3) % 80 < cols;
			rows = 3 + a % 30; cols = 12 + (a * 3) % 80; if (grid_resize(&g, rows, cols)) abort(); break;
		case 24: {                    /* the word starts at or before the cursor */
			int np = 1 + a % 3, sel = (int)(a >> 4) % 4 - 1;
			core_pum(&e, pum, np, sel < np ? sel : -1, boundary(&e, e.win->line, a % 8 < e.win->col ? a % 8 : e.win->col));
			break;
		}
		case 25:
			if (a & 1) core_screen_move(&e, (long)(a % 7) - 3);
			else {
				e.win->line = core_screen_line(&e, "HML"[a % 3], 1 + cnt, a / 3 % 2);
				e.win->col = core_col_at(&e, e.win->line, e.win->want, 0);
			}
			break;
		}
		core_update(&e);
		core_draw(&e, &g);
		check(&e);
		dump(&e, &g);
	}
	core_free(&e);
	return 0;
}

/* N random inputs from SEED, biased to window commands; one hash per input */
int main(int argc, char **argv)
{
	if (argc < 2) return 0;
	if (argv[1][0] == 'x') {   /* one input as hex, every step shown */
		uint8_t b[256]; size_t m = strlen(argv[2]) / 2;
		for (size_t k = 0; k < m; k++) { unsigned v; sscanf(argv[2] + 2 * k, "%2x", &v); b[k] = (uint8_t)v; }
		verbose = argc > 3; hsh = 1469598103934665603ul;
		LLVMFuzzerTestOneInput(b, m);
		printf("%lx\n", hsh);
		return 0;
	}
	const char *want = getenv("SHOW");
	unsigned long n = strtoul(argv[1], 0, 10), x = strtoul(argc > 2 ? argv[2] : "1", 0, 10) * 2654435761ul + 1;
	uint8_t d[256];
	for (unsigned long i = 0; i < n; i++) {
		size_t len = 2 + 2 * (size_t)((x = x * 6364136223846793005ul + 1442695040888963407ul) >> 58);
		for (size_t k = 0; k < len; k++) {
			x = x * 6364136223846793005ul + 1442695040888963407ul;
			d[k] = (uint8_t)(x >> 33);
			/* half the commands are window commands (ops 0 to 6) or a resize (23) */
			if (!(k & 1) && (x >> 60) & 1) d[k] = (uint8_t)("\0\1\2\3\4\5\6\27\0\4\5\5\0\27\4\6"[(x >> 50) & 15]);
		}
		if (want && strtoul(want, 0, 10) == i + 1) { for (size_t k = 0; k < len; k++) printf("%02x", d[k]); printf("\n"); return 0; }
		hsh = 1469598103934665603ul;
		LLVMFuzzerTestOneInput(d, len);
		printf("%lx\n", hsh);
	}
	return 0;
}
