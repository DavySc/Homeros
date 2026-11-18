/* Review of PR 56: drawing headings must not change the buffer or the
   cursor.  For each case of a recorded file (C/L/K.../E lines, as
   tests/test_core_markdown.txt) opens the Markdown file, feeds the case's
   keys and then 40 random keys (moves, inserts, deletes, undo), drawing
   and parsing after every key, and prints after every key the cursor
   (line, byte column), the window's top and leftcol, the grid's cursor cell
   and a hash of the text.  Built against main and against the PR
   (build.sh), the two outputs must be the same.   usage: state FILE */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "syntax.h"
#include "../src/core/core_int.h"

static char line[8192];
static unsigned long long rs;
static unsigned pick(unsigned n) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs % n); }

static void show(Editor *e, Grid *g)
{
	do core_draw(e, g); while (syntax_update(e->buf->syn, e->buf->text, NULL));
	unsigned long h = 2166136261u;
	for (size_t l = 0; l < core_lines(e); l++) {
		size_t n;
		const unsigned char *s = core_line(e, l, &n);
		for (size_t i = 0; i < n; i++) h = (h ^ s[i]) * 16777619u;
		h = (h ^ '\n') * 16777619u;
	}
	printf("%zu %zu %zu %zu %d %d %lx\n", e->win->line, e->win->col, e->win->top, e->win->left, g->crow, g->ccol, h);
}

int main(int argc, char **argv)
{
	FILE *in = fopen(argc > 1 ? argv[1] : "", "r"), *md = NULL;
	char dir[] = "/tmp/state.XXXXXX", cwd[4096], keys[256] = "";
	CHECK(in && mkdtemp(dir) && getcwd(cwd, sizeof cwd));
	int rows = 0, cols = 0, ncase = 0;
	static const char *const K[] = { "j", "k", "h", "l", "0", "$", "w", "b", "x", "dd", "u", "G", "gg", "^",
		"i# \x1b", "A #\x1b", "ox\x1b", "O## y\x1b", "i\x1b", "3j", "2k", "5l", "I#\x1b", "a \x1b" };
	while (fgets(line, sizeof line, in)) {
		line[strcspn(line, "\n")] = 0;
		if (line[0] == 'C') {
			CHECK(sscanf(line + 2, "%d %d", &rows, &cols) == 2);
			CHECK(!chdir(dir) && (md = fopen("f.md", "w")) && !chdir(cwd));
			keys[0] = 0;
		} else if (line[0] == 'L') fprintf(md, "%s\n", line + 2);
		else if (line[0] == 'K') { CHECK(strlen(line + 2) < sizeof keys); strcpy(keys, line + 2); }
		else if (line[0] == 'E') {
			static Editor e;
			Grid g = { 0 };
			fclose(md);
			CHECK(!chdir(dir) && !core_open(&e, "f.md") && !grid_resize(&g, rows, cols));
			printf("case %d\n", ++ncase);
			rs = 2 * (unsigned long long)ncase + 1;
			if (ncase % 3 == 0) core_set_wrap(&e, 1);
			show(&e, &g);
			for (const char *s = keys; *s; s++) if (*s != ':') { keys_feed(&e, (unsigned char)*s, 0); show(&e, &g); }
			if (e.mode != MODE_NORMAL) { keys_feed(&e, KEY_ESC, 0); show(&e, &g); }
			for (int i = 0; i < 40; i++)
				for (const char *s = K[pick(sizeof K / sizeof *K)]; *s; s++) {
					keys_feed(&e, *s == 27 ? KEY_ESC : (unsigned char)*s, 0);
					show(&e, &g);
				}
			core_free(&e);
			grid_free(&g);
			CHECK(!unlink("f.md") && !chdir(cwd));
		}
	}
	rmdir(dir);
	fprintf(stderr, "state: %d cases\n", ncase);
	return 0;
}
