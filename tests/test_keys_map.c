/* Davy's Normal-mode maps (FEATURES.md 4), fed through keys_input() as
   main.c feeds keys.  Neovim with nnoremap <Space>ff <Nop> and <Space>fg <Nop>
   on "abcb" (measured once, 2026-11-19, nvim --headless, feedkeys 'xt'):
   " x" gives "acb" at column 1, " fb" stays "abcb" at column 3, "  ffx"
   gives "acb" at column 1: a key that completes no map runs Space's own
   motion, then the keys, each checked for a map again.  (Here SPC ff opens
   a picker, which would take the x, so x comes after it closes.) */
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "keys.h"
#include "kitten.h"
#include "loop.h"
#include "tools.h"

static void feed(Editor *e, const char *s)
{
	for (; *s; s++) keys_input(e, *s == 27 ? KEY_ESC : (unsigned char)*s, s[1] != 0);
}

/* a picker opens in its Insert mode: Esc, then Esc closes it */
static int picked(Editor *e) { return tools_key(e, KEY_ESC) && tools_key(e, KEY_ESC) && !tools_key(e, KEY_ESC); }

static int at(Editor *e, const char *text, size_t col)
{
	char b[64];
	size_t n = text_read(e->buf->text, 0, b, sizeof b - 1);
	b[n] = 0;
	if (strcmp(b, text) || e->win->col != col) fprintf(stderr, "[%s] col %zu, want [%s] col %zu\n", b, e->win->col, text, col);
	return !strcmp(b, text) && e->win->col == col;
}

/* row r of g holds s */
static int row_has(const Grid *g, int r, const char *s)
{
	char b[512];
	size_t n = 0;
	for (int c = 0; c < g->cols; c++) {
		const Cell *x = &g->c[r * g->cols + c];
		memcpy(b + n, grid_text(x), x->len);
		n += x->len;
	}
	b[n] = 0;
	return strstr(b, s) != NULL;
}

static int face_at(const Grid *g, int r, int c) { return g->c[r * g->cols + c].face; }

/* draws the panel until it shows: the time it took, or -1 after 400 ms */
static long long wait_hint(Editor *e, Grid *g)
{
	long long t0 = loop_now();
	for (core_changed = 0; loop_now() - t0 < 400000000LL; loop_once(10)) {
		grid_clear(g);
		keys_draw(e, g);
		if (row_has(g, g->rows - 2, "<esc> close")) return core_changed ? loop_now() - t0 : -2;
	}
	return -1;
}

int main(void)
{
	char path[] = "/tmp/keys_mapXXXXXX";   /* outside any Git repository */
	int fd = mkstemp(path);
	CHECK(fd >= 0 && write(fd, "abcb\n", 5) == 5 && !close(fd));
	static Editor e;
	CHECK(!core_open(&e, path));

	/* no map: Space's motion, then the keys */
	feed(&e, " x");
	CHECK(at(&e, "acb\n", 1));
	feed(&e, "u0 fb");
	CHECK(at(&e, "abcb\n", 3));
	feed(&e, "0  ");
	CHECK(e.ks.pending && at(&e, "abcb\n", 1));
	feed(&e, "ff");                            /* SPC ff is a picker */
	CHECK(picked(&e) && at(&e, "abcb\n", 1));
	feed(&e, "x");
	CHECK(at(&e, "acb\n", 1));

	/* the maps; a lone SPC waits (no 'timeoutlen', as with which-key) */
	feed(&e, " fg");
	CHECK(picked(&e) && !e.ks.pending);
	feed(&e, " gl");
	CHECK(!strcmp(e.msg, "Not in a Git repository"));
	feed(&e, "-");
	CHECK(!strcmp(e.msg, "tools: file manager not built yet"));
	/* the save kitten's (my-nvim): <leader>uk turns it off and on, and
	   :KittenPaw plays it */
	feed(&e, " uk");
	CHECK(!strcmp(e.msg, "Kitten on save: off"));
	feed(&e, " uk");
	CHECK(!strcmp(e.msg, "Kitten on save: on"));
	/* SQL (package 22): SPC r opens the results split, :Connect [name]
	   names the buffer's connection, :Disconnect ends its session (here
	   no results, no file and no session) */
	feed(&e, " r");
	CHECK(!strcmp(e.msg, "sql: no results yet (F5 runs the query)") && !e.ks.pending);
	feed(&e, ":Connect\r");
	CHECK(!strcmp(e.msg, "sql: not connected"));
	feed(&e, ":Disconnect\r");
	CHECK(!strncmp(e.msg, "sql: ", 5));
	setenv("XDG_CONFIG_HOME", "/nonexistent", 1);
	feed(&e, ":Connect dev\r");
	CHECK(!strncmp(e.msg, "sql: no connections file", 24));
	/* SPC i s, the snippet picker, also in Visual mode (around the area);
	   no other map there: Space is its motion */
	feed(&e, " is");
	CHECK(picked(&e));
	feed(&e, "0v is");
	CHECK(picked(&e) && e.ks.visual);
	feed(&e, "\0330v ff");
	CHECK(!tools_key(&e, KEY_ESC));
	feed(&e, "\0330v x");
	CHECK(at(&e, "b\n", 0));
	feed(&e, "u");
	/* SPC u p: 'conceallevel' 2 or 0 (Davy's org-toggle-pretty-entities) */
	e.win->conceal = 2;
	feed(&e, " up");
	CHECK(!e.win->conceal);
	feed(&e, " up");
	CHECK(e.win->conceal == 2);
	static Grid g;
	CHECK(!grid_resize(&g, 24, 80));
	feed(&e, ":KittenPaw\r");
	kitten_draw(&e, &g);
	int drawn = 0;
	for (int i = 0; i < 24 * 80; i++) drawn |= *grid_text(&g.c[i]) != ' ';
	CHECK(drawn);

	/* which-key's panel: typed fast, none; after 200 ms the keys after
	   the prefix in which-key's order (maps, then groups with +), above
	   the command line; <BS> goes up a level, <Esc> closes and runs
	   nothing (which-key.nvim's view.lua and state.lua) */
	feed(&e, " ");
	keys_draw(&e, &g);
	CHECK(!row_has(&g, 22, "SPC"));
	CHECK(wait_hint(&e, &g) >= 190000000LL);
	CHECK(row_has(&g, 22, "<esc> close  <bs> back") && !row_has(&g, 23, "SPC"));
	CHECK(!row_has(&g, 16, "\xe2\x9e\x9c") && !row_has(&g, 21, "\xe2\x9e\x9c"));
	CHECK(row_has(&g, 17, "D \xe2\x9e\x9c Dashboard") && row_has(&g, 18, "e \xe2\x9e\x9c File Explorer") && row_has(&g, 19, "r \xe2\x9e\x9c SQL results") && row_has(&g, 20, ", \xe2\x9e\x9c Buffers"));
	CHECK(row_has(&g, 17, "c \xe2\x9e\x9c +compile") && row_has(&g, 18, "f \xe2\x9e\x9c +find") && row_has(&g, 19, "u \xe2\x9e\x9c +toggle"));
	CHECK(face_at(&g, 17, 5) == FACE_NORMAL_FLOAT && face_at(&g, 17, 7) == FACE_WK_SEPARATOR && face_at(&g, 17, 9) == FACE_WK_DESC && face_at(&g, 17, 35) == FACE_WK_GROUP);
	CHECK(g.c[22 * 80 + 79].bg == FACE_NORMAL_FLOAT && g.c[16 * 80].bg == FACE_NORMAL_FLOAT && g.c[15 * 80].bg != FACE_NORMAL_FLOAT && g.c[23 * 80].bg != FACE_NORMAL_FLOAT);
	/* the cursor under it (which-key's no_overlap): 2 rows below the cursor
	   and over the command line, cut; at the bottom, 4 high over it */
	g.crow = 15;
	grid_clear(&g);
	keys_draw(&e, &g);
	CHECK(row_has(&g, 23, "<esc> close") && row_has(&g, 18, "D \xe2\x9e\x9c") && row_has(&g, 20, "r \xe2\x9e\x9c") && row_has(&g, 21, ", \xe2\x9e\x9c") && !row_has(&g, 22, "\xe2\x9e\x9c") && g.c[16 * 80].bg != FACE_NORMAL_FLOAT);
	g.crow = 21;
	grid_clear(&g);
	keys_draw(&e, &g);
	CHECK(row_has(&g, 23, "<esc> close") && row_has(&g, 21, "D \xe2\x9e\x9c") && !row_has(&g, 22, "\xe2\x9e\x9c") && g.c[20 * 80].bg == FACE_NORMAL_FLOAT && g.c[19 * 80].bg != FACE_NORMAL_FLOAT);
	g.crow = 14;
	grid_clear(&g);
	keys_draw(&e, &g);
	CHECK(row_has(&g, 22, "<esc> close") && row_has(&g, 17, "D \xe2\x9e\x9c"));
	g.crow = 0;
	feed(&e, "g");                             /* shown: at once */
	grid_clear(&g);
	keys_draw(&e, &g);
	CHECK(row_has(&g, 22, " SPC \xc2\xbb +git ") && row_has(&g, 18, "b \xe2\x9e\x9c Git branches") && row_has(&g, 19, "B \xe2\x9e\x9c Open on GitHub") && row_has(&g, 18, "s \xe2\x9e\x9c Git status"));
	keys_input(&e, KEY_BS, 0);
	grid_clear(&g);
	keys_draw(&e, &g);
	CHECK(e.ks.pending && row_has(&g, 20, "Buffers") && !row_has(&g, 22, "git"));
	keys_input(&e, KEY_BS, 0);
	grid_clear(&g);
	keys_draw(&e, &g);
	CHECK(!e.ks.pending && !row_has(&g, 22, "SPC") && at(&e, "acb\n", 0));
	feed(&e, "03 \033x");                      /* Esc: nothing runs, the count goes */
	CHECK(!e.ks.pending && at(&e, "cb\n", 0));
	feed(&e, "u");
	feed(&e, " u");
	keys_draw(&e, &g);
	CHECK(!row_has(&g, 22, "SPC"));            /* the delay starts again */
	feed(&e, "k");
	CHECK(!strcmp(e.msg, "Kitten on save: off"));
	CHECK(wait_hint(&e, &g) < 0);              /* a map run stops the delay */
	feed(&e, " uk ");
	CHECK(wait_hint(&e, &g) > 0);
	feed(&e, "x");                             /* no map: Space, x; the panel goes */
	keys_draw(&e, &g);
	CHECK(at(&e, "ab\n", 1) && wait_hint(&e, &g) < 0);
	feed(&e, "u0v ");                          /* Visual: SPC i s only */
	CHECK(wait_hint(&e, &g) > 0 && row_has(&g, 19, "i \xe2\x9e\x9c +insert") && !row_has(&g, 18, "\xe2\x9e\x9c") && !row_has(&g, 20, "\xe2\x9e\x9c"));
	feed(&e, "\033\033");
	static Grid small;                         /* shorter than the list: cut, no write outside */
	CHECK(!grid_resize(&small, 6, 30));
	small.crow = 5;                            /* under the panel: it stays */
	feed(&e, " ");
	CHECK(wait_hint(&e, &small) > 0 && row_has(&small, 4, "SPC") && row_has(&small, 1, "D \xe2\x9e\x9c") && row_has(&small, 2, "e \xe2\x9e\x9c") && !row_has(&small, 5, "SPC"));
	small.crow = 0;                            /* over it: 2 rows below, 1 row of the list */
	grid_clear(&small);
	keys_draw(&e, &small);
	CHECK(row_has(&small, 5, "<esc> close") && row_has(&small, 3, "D \xe2\x9e\x9c") && !row_has(&small, 4, "\xe2\x9e\x9c"));
	for (int rows = 1; rows < 4; rows++) {     /* no room: nothing, no write outside */
		CHECK(!grid_resize(&small, rows, 30));
		small.crow = 0;
		keys_draw(&e, &small);
		CHECK(!row_has(&small, 0, "\xe2\x9e\x9c") && !row_has(&small, rows - 1, "<esc>"));
	}
	feed(&e, "\033");
	grid_free(&small);
	grid_free(&g);

	/* after a count or a register the map runs and takes them, as a <Cmd>
	   map does; an operator, Visual and Insert keep Space (Neovim, measured
	   once as above: "2 ff" stays at column 0, with <Cmd>echo<CR> "2 ffx"
	   gives "bcb", "v ffd" gives "cb", "i ff" Esc gives " ffabcb") */
	feed(&e, "u0");
	CHECK(at(&e, "abcb\n", 0));
	feed(&e, "2\"a ff");
	CHECK(picked(&e) && !e.ks.pending && at(&e, "abcb\n", 0));
	feed(&e, "x");
	CHECK(at(&e, "bcb\n", 0));
	feed(&e, "ud ");
	CHECK(!tools_key(&e, KEY_ESC) && at(&e, "bcb\n", 0));
	feed(&e, "u0v ffd");
	CHECK(!tools_key(&e, KEY_ESC) && at(&e, "cb\n", 0));
	feed(&e, "u0i ff\x1b");
	CHECK(!tools_key(&e, KEY_ESC) && at(&e, " ffabcb\n", 2));
	feed(&e, "2 x");                            /* no map: the count is Space's */
	CHECK(at(&e, " ffacb\n", 4));

	/* a register's name is read unmapped: "-p puts the small delete
	   register, while "a- runs the map (Neovim's normal_cmd_get_more_chars()
	   with no_mapping; reviews/pr-74-repro/nvim-regname.lua) */
	feed(&e, "uu0xyl\"-p");   /* yl: the unnamed register differs from "- */
	CHECK(at(&e, "bacb\n", 1));
	e.msg[0] = 0;
	feed(&e, "\"a-");
	CHECK(!strcmp(e.msg, "tools: file manager not built yet") && !e.ks.pending);

	/* keys_visual() for git's views (PR 119): Visual mode ends as Esc
	   ends it, so x deletes one character; it starts with its other end at
	   column 0, so d deletes to the cursor */
	feed(&e, "0lvl");
	keys_visual(&e, 0, 0);
	CHECK(!e.ks.visual && e.buf->mark[MARK_GT].set);
	feed(&e, "x");
	CHECK(at(&e, "bab\n", 2));
	keys_visual(&e, 'v', 0);
	CHECK(e.ks.visual == 'v' && !e.ks.vline && !e.ks.vcol);
	feed(&e, "d");
	CHECK(at(&e, "\n", 0));
	core_free(&e);

	/* the keys after a failed map go through the chain again: on the
	   dashboard SPC q is Space, then the dashboard's q, which quits (Neovim
	   checks them against buffer-local maps; nvim-replay.lua) */
	static Editor d;
	CHECK(!core_open(&d, NULL));
	tools_dash(&d, 0);
	feed(&d, " q");
	CHECK(d.quit);
	core_free(&d);
	unlink(path);

	/* nvim-orgmode's cit and ciT, in .org buffers only: each one undoable
	   change; elsewhere c is Vim's at once, and in Org c then w is cw */
	char org[] = "/tmp/keys_mapXXXXXX.org";
	fd = mkstemps(org, 4);
	CHECK(fd >= 0 && write(fd, "* a b\n", 6) == 6 && !close(fd));
	CHECK(!core_open(&d, org));
	feed(&d, "cit");
	CHECK(at(&d, "* TODO a b\n", 0));
	feed(&d, "$cit");
	CHECK(at(&d, "* NEXT a b\n", 9));
	feed(&d, "ciT");
	CHECK(at(&d, "* TODO a b\n", 9));
	feed(&d, "u");
	CHECK(at(&d, "* NEXT a b\n", 9));
	feed(&d, "xcitu");                         /* not the x before it */
	CHECK(at(&d, "* NEXT a \n", 8));
	feed(&d, "u");
	CHECK(at(&d, "* NEXT a b\n", 9));
	feed(&d, "0wwcwc\x1b");
	CHECK(at(&d, "* NEXT c b\n", 7));
	CHECK(!grid_resize(&g, 24, 80));
	feed(&d, "c");                             /* no panel after Org's c, and <BS> is its motion */
	CHECK(wait_hint(&d, &g) < 0);
	keys_input(&d, KEY_BS, 1);
	feed(&d, "X\033");
	CHECK(at(&d, "* NEXTXc b\n", 6));
	grid_free(&g);
	feed(&d, "u");
	CHECK(at(&d, "* NEXT c b\n", 6));
	/* DONE stamps CLOSED with the local clock (a zone 5:45 east, named as
	   POSIX TZ does, so no zone files are needed); a minute may turn */
	setenv("TZ", "XST-5:45", 1);
	tzset();
	char want[2][32], got[32] = "";
	time_t t = time(NULL);
	strftime(want[0], sizeof want[0], "CLOSED: [%Y-%m-%d %a %H:%M]", localtime(&t));
	feed(&d, "citcit");
	t = time(NULL);
	strftime(want[1], sizeof want[1], "CLOSED: [%Y-%m-%d %a %H:%M]", localtime(&t));
	text_read(d.buf->text, core_line_start(&d, 1), got, 31);
	CHECK(!strncmp(got, want[0], strlen(want[0])) || !strncmp(got, want[1], strlen(want[1])));
	core_free(&d);
	unlink(org);
	CHECK(!core_open(&d, NULL));
	feed(&d, "c");
	CHECK(!d.ks.pending);
	core_free(&d);
	puts("test_keys_map: ok");
	return 0;
}
