/* Davy's Normal-mode maps (keys_map(), FEATURES.md 4), fed as main.c
   feeds keys.  Neovim with nnoremap <Space>ff <Nop> and <Space>fg <Nop>
   on "abcb" (measured once, 2026-11-19, nvim --headless, feedkeys 'xt'):
   " x" gives "acb" at column 1, " fb" stays "abcb" at column 3, "  ffx"
   gives "acb" at column 1: a key that completes no map runs Space's own
   motion, then the keys, each checked for a map again.  (Here SPC ff opens
   a picker, which would take the x, so x comes after it closes.) */
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "git.h"
#include "keys.h"
#include "tools.h"

static void feed(Editor *e, const char *s)
{
	for (; *s; s++) {
		int k = *s == 27 ? KEY_ESC : (unsigned char)*s;
		if (!(e->ks.pending ? keys_map(e, k) : tools_key(e, k) || git_key(e, k) || keys_map(e, k))) keys_feed(e, k, s[1] != 0);
	}
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

	core_free(&e);
	unlink(path);
	puts("test_keys_map: ok");
	return 0;
}
