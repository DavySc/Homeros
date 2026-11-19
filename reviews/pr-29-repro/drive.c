/* Review driver for package 17 (headless views, as tests/test_git.c):
   lines on stdin: "o" open status, "k keys", "s" settle, "$ cmd" shell in
   the repository, "p" print the view, "l ms" one loop turn, "c n" cursor,
   "m text" commit message then ZZ */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "core.h"
#include "git.h"
#include "keys.h"
#include "loop.h"
#include "../src/git/gitint.h"
static Editor e;
static void nop(void *c, int st, const char *b, size_t n) { (void)c; (void)st; (void)b; (void)n; }
static Repo *R(void) { return git_repos; }
static void settle(void)
{
	for (int i = 0; i < 400; i++) {
		loop_once(i < 2 ? 5 : 50);
		Repo *r = R();
		int busy = r && r->editing != 2 && (r->busy || r->dirty || r->qn || r->editing == 1 || r->editing == 3);
		for (Aux *a = r ? r->aux : NULL; a; a = a->next) busy |= a->busy;
		if (!busy && i >= 2) return;
	}
	Repo *r = R();
	printf("NEVER SETTLED busy %d dirty %d qn %d editing %d left %d\n", r->busy, r->dirty, r->qn, r->editing, r->left);
}
static int keyof(const char **p)
{
	static const struct { const char *n; int k; } names[] = {
		{ "<Tab>", KEY_TAB }, { "<CR>", KEY_ENTER }, { "<Esc>", KEY_ESC }, { "<BS>", KEY_BS },
		{ "<C-j>", 10 }, { "<C-k>", 11 }, { "<Space>", ' ' } };
	if (**p == '<')
		for (size_t i = 0; i < sizeof names / sizeof *names; i++)
			if (!strncmp(*p, names[i].n, strlen(names[i].n))) { *p += strlen(names[i].n); return names[i].k; }
	return *(*p)++;
}
int main(int argc, char **argv)
{
	char line[8192], scratch[] = "/tmp/drive-scratch-XXXXXX";
	close(mkstemp(scratch));
	const char *repo = argc > 1 ? argv[1] : ".";
	core_open(&e, scratch);
	e.rows = 60; e.cols = 100; e.msg[0] = 0;
	git_editor(&e);
	git_headless = 1;
	while (fgets(line, sizeof line, stdin)) {
		line[strcspn(line, "\n")] = 0;
		Repo *r = R();
		if (line[0] == 'o') printf("open: %d\n", git_status(&e, line[1] ? line + 2 : repo));
		else if (line[0] == 's') settle();
		else if (line[0] == 'f') { git_free(); }
		else if (line[0] == 'z') { for (int i = 0; i < atoi(line + 2); i++) { char *av[] = { "sleep", "30", NULL }; printf("spawn %d\n", proc_spawn(av, NULL, nop, NULL)); } }
		else if (line[0] == 'l') loop_once(atoi(line + 2));
		else if (line[0] == '$') { char c[9000]; snprintf(c, sizeof c, "cd '%s' && %s", repo, line + 2); if (system(c)) printf("shell failed\n"); }
		else if (line[0] == 'c') { if (git_ha) git_ha->cur = (size_t)atoi(line + 2); else r->cur = (size_t)atoi(line + 2); }
		else if (line[0] == 'k') {
			for (const char *p = line + 2; *p; ) {
				int k = keyof(&p);
				View *v = git_ha ? &git_ha->v : &r->st;
				size_t *cur = git_ha ? &git_ha->cur : &r->cur;
				if (!git_key(&e, k)) { if (k == 'j' && *cur + 1 < v->nvis) ++*cur; else if (k == 'k' && *cur) --*cur; }
			}
		} else if (line[0] == 'm') {
			FILE *f = fopen(r->editmsg, "w"); fputs(line + 2, f); fputc('\n', f); fclose(f);
			git_editor_done(r, 1);
		} else if (line[0] == 'p') {
			View *v = git_ha ? &git_ha->v : &r->st;
			size_t cur = git_ha ? git_ha->cur : r->cur, i = 0;
			const char *s = v->text.s ? v->text.s : "";
			while (*s) {
				const char *nl = strchr(s, '\n');
				size_t len = nl ? (size_t)(nl - s) : strlen(s);
				printf("%c %.*s\n", i == cur ? '>' : ' ', (int)len, s);
				s += len + (nl != NULL); i++;
			}
			printf("[msg] %s  [ver %lu shown %lu nproc %d]\n", e.msg, r->ver, r->shown, r->nproc);
		}
	}
	git_free();
	core_free(&e);
	unlink(scratch);
	return 0;
}
