/* The status line against lualine with catppuccin's lualine theme, set up
   as UI proposal 5's mockup (tests/test_core_status.lua), as Neovim 0.12.6
   lays it out: more.txt holds nvim_eval_statusline()'s
   text and highlight groups for each state below (measured, 1 run each;
   lualine 221ce6b, catppuccin 05e8787).  Then the faces of the windows'
   other cells, as Neovim's highlight groups name them (drawline.c
   win_line(), win_redr_status()). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "check.h"
#include "core.h"
#include "git.h"
#include "grid.h"
#include "loop.h"
#include "uni.h"
#include "../src/core/core_int.h"

static char rec[128][2048];
static int nrec;

/* lualine's highlight group as the face of a cell and its background */
static void group(const char *s, size_t n, unsigned char *face, unsigned char *bg)
{
	static const char *const mode[] = { "normal", "insert", "visual", "replace", "command" };
	static const unsigned char a[] = { FACE_MODE_NORMAL, FACE_MODE_INSERT, FACE_MODE_VISUAL, FACE_MODE_REPLACE, FACE_MODE_COMMAND },
	                           b[] = { FACE_MODE_NORMAL_FG, FACE_MODE_INSERT_FG, FACE_MODE_VISUAL_FG, FACE_MODE_REPLACE_FG,
	                                   FACE_MODE_COMMAND_FG };
	char g[128];
	CHECK(n < sizeof g);
	memcpy(g, s, n);
	g[n] = 0;
	const char *t = strncmp(g, "lualine_transitional_", 21) ? g : g + 21;
	int m = 0;
	while (m < 4 && strncmp(t + 10, mode[m], strlen(mode[m]))) m++;
	*bg = 0;
	if (!strcmp(g, "lualine_c_normal")) *face = FACE_STATUS_LINE;
	else if (!strcmp(g, "lualine_c_inactive")) *face = FACE_STATUS_LINE_NC;
	else if (t == g) {
		CHECK(!strncmp(g, "lualine_a_", 10) || !strncmp(g, "lualine_b_", 10));
		*face = g[8] == 'a' ? a[m] : b[m];
		*bg = g[8] == 'a' ? 0 : FACE_STATUS_INFO;
	} else if (t[8] == 'a') {
		/* a pill's end: fg the pill's bg, bg the section's beside it */
		*face = b[m];
		*bg = strstr(t, "_to_lualine_b_") ? FACE_STATUS_INFO : 0;
	} else {
		CHECK(!strncmp(t, "lualine_b_", 10) && strstr(t, "_to_lualine_c_normal"));
		*face = FACE_STATUS_INFO;
		*bg = FACE_STATUS_LINE;
	}
}

/* the status line of window w against the recorded state tag: each cell's
   text, face and background */
static int fails;
static void status(const Grid *g, const Win *w, const char *tag)
{
	const char *line = NULL;
	for (int i = 0; i < nrec && !line; i++)
		if (!strncmp(rec[i], tag, strlen(tag)) && rec[i][strlen(tag)] == '\t') line = rec[i] + strlen(tag) + 1;
	CHECK(line);
	const char *hl = strchr(line, '\t');
	const unsigned char *s = (const unsigned char *)line;
	size_t n = (size_t)(hl - line);
	int r = w->y + w->h, c = 0;
	for (size_t i = 0; i < n && c < w->w; i += uni_glen(s, n, i)) {
		/* the group of byte i: the last that starts at or before it */
		unsigned char face = FACE_STATUS_LINE, bg = 0;
		for (const char *h = hl + 1; *h && *h != '\n';) {
			char *colon;
			size_t at = strtoul(h, &colon, 10), k = strcspn(colon + 1, " \n");
			if (at <= i) group(colon + 1, k, &face, &bg);
			h = colon + 1 + k + (colon[1 + k] == ' ');
		}
		const Cell *z = &g->c[r * g->cols + w->x + c];
		size_t k = uni_glen(s, n, i);
		if (z->len != k || memcmp(grid_text(z), s + i, k) || z->face != face || z->bg != bg) {
			fprintf(stderr, "%s, cell %d: nvim [%.*s] face %d bg %d, core [%.*s] face %d bg %d\n  nvim [%.*s]\n", tag, c,
			        (int)k, s + i, face, bg, z->len, grid_text(z), z->face, z->bg, (int)n, line);
			fails++; fprintf(stderr, "  core ["); for (int q = 0; q < w->w; q++) { const Cell *y = &g->c[r * g->cols + w->x + q]; fprintf(stderr, "%.*s", y->len, grid_text(y)); } fprintf(stderr, "]\n"); return;
		}
		c += uni_width(s, n, i);
	}
	if (c != w->w) { fprintf(stderr, "%s: nvim %d cells, window %d\n", tag, c, w->w); fails++; }
}

static void draw(Editor *e, Grid *g, int cols, const char *tag)
{
	CHECK(!grid_resize(g, 10, cols));
	core_draw(e, g);
	status(g, e->win, tag);
}

static void file(const char *name, const char *s)
{
	FILE *f = fopen(name, "w");
	CHECK(f && fputs(s, f) >= 0 && !fclose(f));
}


int main(int argc, char **argv)
{
	static Editor e;
	static Grid g;
	FILE *f = fopen(argv[argc - 1], "r");
	CHECK(f);
	while (nrec < 128 && fgets(rec[nrec], sizeof *rec, f)) nrec++;
	CHECK(!fclose(f));
	char dir[] = "/tmp/p95XXXXXX", tag[32];
	CHECK(mkdtemp(dir) && !chdir(dir) && !setenv("HOME", dir, 1));
	CHECK(!system("git init -q -b 'feature/\xe9\x95\xb7\xe3\x81\x84-branch'"));
	const char *name = "\xe6\xbc\xa2\xe5\xad\x97\xe3\x83\x95\xe3\x82\xa1\xe3\x82\xa4\xe3\x83\xab\xe5\x90\x8d.sql";
	file(name, "select 1\n");
	CHECK(!core_open(&e, name));
	for (int i = 0; i < 200 && !git_branch(e.buf); i++) CHECK(loop_once(20) == 0);
	CHECK(git_branch(e.buf));
	CHECK(!core_edit(&e, 0, 1, "", 0));
	CHECK(!grid_resize(&g, 10, 80) && !core_split(&e, 1, 0));
	core_draw(&e, &g);
	const Win *o = e.wins == e.win ? e.win->next : e.wins;
	for (int c = 1; c <= 70; c++) {
		core_resize(&e, 1, c, 1);
		core_draw(&e, &g);
		if (e.win->w != c) { fprintf(stderr, "w%d: window is %d\n", c, e.win->w); continue; }
		snprintf(tag, sizeof tag, "w%d", c);
		status(&g, e.win, tag);
	}
	for (int c = 1; c <= 25; c++) {
		core_resize(&e, 1, 79 - c, 1);
		core_draw(&e, &g);
		if (o->w != c) { fprintf(stderr, "o%d: window is %d\n", c, o->w); continue; }
		snprintf(tag, sizeof tag, "o%d", c);
		status(&g, o, tag);
	}
	CHECK(!core_only(&e));
	CHECK(!core_write(&e, e.buf, 0) && !core_edit_file(&e, "new2", 0));
	draw(&e, &g, 80, "new-git");
	CHECK(!core_edit(&e, 0, 0, "x", 1));
	CHECK(!core_write(&e, e.buf, 0));
	for (int i = 0; i < 200 && !git_branch(e.buf); i++) CHECK(loop_once(20) == 0);
	draw(&e, &g, 80, "saved-new");
	f = fopen("big", "w");
	for (int i = 0; i < 12345; i++) fputs("a\n", f);
	CHECK(!fclose(f) && !core_edit_file(&e, "big", 0));
	for (int i = 0; i < 200 && !git_branch(e.buf); i++) CHECK(loop_once(20) == 0);
	e.win->line = 1233;
	draw(&e, &g, 80, "big");
	core_free(&e);
	grid_free(&g);
	printf("more: %d states differ\n", fails);
	return 0;
}
