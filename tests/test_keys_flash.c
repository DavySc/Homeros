/* Davy's vim.highlight.on_yank(): a yank sets ks.yank to '[ and '] (with
   the register's type) and a 150 ms timer clears it; a delete, the black
   hole register and an executing register do not.  The marks are Neovim's
   (measured once, 2026-11-19, nvim --headless on these lines; MAXCOL for
   a line's end); a block's are screen columns, the left one and the
   register's width (package 4's contract).  The core draws it. */
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "keys.h"
#include "loop.h"

static Editor e;

static void feed(const char *s)
{
	for (; *s; s++) keys_feed(&e, *s == 27 ? KEY_ESC : (unsigned char)*s, s[1] != 0);
}

static int flash(const char *keys, int type, size_t l1, size_t c1, size_t l2, size_t c2)
{
	e.ks.yank.type = 0;
	feed("\x1bgg0");
	feed(keys);
	int ok = e.ks.yank.type == type && (!type || (e.ks.yank.l1 == l1 && e.ks.yank.c1 == c1 && e.ks.yank.l2 == l2 && e.ks.yank.c2 == c2));
	if (!ok) fprintf(stderr, "%s: %d %zu %zu %zu %zu\n", keys, e.ks.yank.type, e.ks.yank.l1, e.ks.yank.c1, e.ks.yank.l2, e.ks.yank.c2);
	return ok;
}

int main(void)
{
	char path[] = "/tmp/keys_flashXXXXXX";
	int fd = mkstemp(path);
	CHECK(fd >= 0 && write(fd, "abc def\nghi jkl\n\nx\n\tab\n", 23) == 23 && !close(fd));
	CHECK(!core_open(&e, path));
	const size_t M = 0x7fffffff;
	CHECK(flash("yiw", 'v', 0, 0, 0, 2));
	CHECK(flash("wyy", 'V', 0, 0, 0, M));
	CHECK(flash("lvjy", 'v', 0, 1, 1, 1));
	CHECK(flash("l\x16jly", 22, 0, 1, 1, 2));
	CHECK(flash("yj", 'V', 0, 0, 1, M));
	CHECK(flash("wye", 'v', 0, 4, 0, 6));
	CHECK(flash("y}", 'V', 0, 0, 1, M));
	CHECK(flash("lll\x16j$y", 22, 0, 3, 1, 6));
	CHECK(flash("\"ayy\"Ayiw", 'V', 0, 0, 0, 2));   /* the register's type: an append to a linewise one */
	CHECK(flash("dd", 0, 0, 0, 0, 0) && flash("u\"_yy", 0, 0, 0, 0, 0));
	CHECK(flash("qayiwq", 'v', 0, 0, 0, 2) && flash("@a", 0, 0, 0, 0, 0));
	/* ks.executing is reg_executing(): set however the register runs, and
	   inside a nested one (TextYankPost logged reg_executing() as a, a, a a,
	   a, and b then a for these; measured once, 2026-11-20, nvim --headless) */
	CHECK(flash("@@", 0, 0, 0, 0, 0) && flash("2@a", 0, 0, 0, 0, 0) && flash("Q", 0, 0, 0, 0, 0));
	CHECK(flash("qbyy@aq", 'V', 0, 0, 0, M) && flash("@b", 0, 0, 0, 0, 0) && !e.ks.executing);
	/* a block's columns are screen columns: the Tab's last, where the
	   cursor stood ('virtualedit' block: "\x16" 2, '[ at coladd 7,
	   measured once in nvim), and the a */
	CHECK(flash("G\x16ly", 22, 4, 7, 4, 8) && flash("Gl\x16hy", 22, 4, 7, 4, 8));
	/* the area ends on the last character's first byte, where '] is on
	   its last (the 3-byte 漢 from byte 2) */
	feed("Go\x1bi");   /* no autoindent */
	keys_feed(&e, 0xe9, 1), keys_feed(&e, 0x6f22, 1);   /* keys are code points */
	CHECK(flash("Gvly", 'v', 5, 0, 5, 2));
	feed("u");

	/* the timer clears it after 150 ms and asks for a frame */
	CHECK(flash("yiw", 'v', 0, 0, 0, 2));
	long long t0 = loop_now();
	core_changed = 0;
	while (e.ks.yank.type && loop_now() - t0 < 2000000000LL) loop_once(100);
	long long ms = (loop_now() - t0) / 1000000;
	CHECK(!e.ks.yank.type && core_changed && ms >= 140 && ms < 1000);
	core_free(&e);
	unlink(path);
	printf("test_keys_flash: a yank flashes '[ to '] as Neovim's on_yank, cleared after %lld ms\n", ms);
	return 0;
}
