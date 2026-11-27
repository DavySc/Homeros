/* Re-review of PR 166 at d6fcec3d: SPC 5 in Visual mode, where SPC 5 is no
   map (usable(): only SPC i s is).  Before the fix commit Space moved and
   5{motion} ran, as Neovim does when no map matches. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "core.h"
#include "keys.h"

static void feed(Editor *e, const char *s)
{
	for (; *s; s++) keys_input(e, *s == 27 ? KEY_ESC : (unsigned char)*s, s[1] != 0);
}

int main(void)
{
	char path[] = "/tmp/zprobeXXXXXX";
	int fd = mkstemp(path), bad = 0;
	if (fd < 0 || write(fd, "abcdefghij\n", 11) != 11 || close(fd)) return 99;
	static Editor e;
	if (core_open(&e, path)) return 99;
	feed(&e, "v 5l");
	printf("v SPC 5 l: col %zu (Neovim: 6), visual %d, pending %d\n", e.win->col, e.ks.visual, e.ks.pending);
	bad += e.win->col != 6;
	feed(&e, "\x1b" "0v 3l");
	printf("v SPC 3 l: col %zu (Neovim: 4)\n", e.win->col);
	bad += e.win->col != 4;
	feed(&e, "\x1b" "0 5l");
	printf("Normal SPC 5 l (l is frame-less): col %zu (the PR: 0, a beep)\n", e.win->col);
	core_free(&e);
	unlink(path);
	return bad;
}
