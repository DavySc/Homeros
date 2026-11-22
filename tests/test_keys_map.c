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
