/* Review of PR 29: git_sign and SPC h on the one buffer ref/core.c has.
   usage: signs FILE [line key]...   prints the signs, then runs SPC h key at each line */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core.h"
#include "git.h"
#include "loop.h"
#include "../src/git/gitint.h"
static Editor e;
static void settle(void)
{
	for (int i = 0; i < 100; i++) {
		loop_once(i < 2 ? 5 : 50);
		Repo *r = git_repos;
		if (i >= 2 && r && !r->busy && !r->dirty && !r->qn) return;
	}
}
static void signs(void)
{
	for (size_t l = 0; l < core_lines(&e); l++) { int s = git_sign(e.buf, l); putchar(s ? s : '.'); }
	printf("  [%s]\n", e.msg);
}
int main(int argc, char **argv)
{
	core_open(&e, argv[1]);
	e.rows = 40; e.cols = 100; e.msg[0] = 0;
	git_editor(&e);
	git_sign(e.buf, 0);
	settle();
	signs();
	for (int i = 2; i + 1 < argc; i += 2) {
		e.win->line = (size_t)atoi(argv[i]);
		e.msg[0] = 0;
		printf("SPC h %s at line %s: %d\n", argv[i + 1], argv[i], git_leader(&e, 'h', argv[i + 1][0]));
		settle();
		signs();
	}
	git_free();
	core_free(&e);
	return 0;
}
