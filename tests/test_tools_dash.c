/* The dashboard against snacks.nvim's (src/tools/conform/dash.py records it
   from Neovim set up as Davy's): every row's text and colours and the
   cursor, at sizes from 30x10 to 200x60, either side of where the picture
   fits; then its keys. */
#include <string.h>
#include <time.h>
#include "check.h"
#include "core.h"
#include "tools.h"
#include "../src/tools/dash.h"
#include "../src/tools/conform/dash-expect.inc"

static Editor e;
static Grid g;

/* a clock that stands still, so the footer always reads "loaded in 1.0ms"
   (the dashboard measures it at the first draw) */
enum { NOW = 1000 };
int timespec_get(struct timespec *t, int base)
{
	t->tv_sec = NOW;
	t->tv_nsec = 0;
	return base;
}

/* the legend's letter for a cell: its face's colour and boldness */
static char colour(const Cell *c)
{
	const unsigned char *s = grid_text(c);
	if (c->len == 1 && (s[0] == ' ' || s[0] == '~')) return ' ';
	const Face *f = &faces[c->face];
	char k[16];
	if (f->style & FACE_FG) snprintf(k, sizeof k, "%02x%02x%02x%s", f->fg[0], f->fg[1], f->fg[2], f->style & FACE_BOLD ? "b" : "");
	else snprintf(k, sizeof k, "default%s", f->style & FACE_BOLD ? "b" : "");
	for (size_t i = 0; i < sizeof legend / sizeof *legend; i++) if (!strcmp(k, legend[i])) return (char)('a' + i);
	return '?';
}

static void draw(void)
{
	core_draw(&e, &g);
	tools_draw(&e, &g);
}

static void open_dash(void)
{
	tools_dash(&e, NOW * 1000000000LL - 1000000);
	draw();
}

int main(void)
{
	CHECK(!core_open(&e, NULL));
	size_t ncase = sizeof cases / sizeof *cases;
	for (size_t i = 0; i < ncase; i++) {
		CHECK(!grid_resize(&g, cases[i].rows, cases[i].cols));
		open_dash();
		for (int r = 0; r < g.rows - 1; r++) {
			char text[4096], col[1024];
			size_t k = 0, n = 0;
			for (int c = 0; c < g.cols; c++) {
				const Cell *x = &g.c[r * g.cols + c];
				if (!x->len) continue;
				memcpy(text + k, grid_text(x), x->len);
				k += x->len;
				col[n++] = colour(x);
			}
			text[k] = col[n] = 0;
			if (strcmp(text, cases[i].row[r].text) || strcmp(col, cases[i].row[r].colour)) {
				fprintf(stderr, "case %zu (%dx%d) row %d\n got  |%s|\n want |%s|\n got  |%s|\n want |%s|\n", i,
				        cases[i].cols, cases[i].rows, r, text, cases[i].row[r].text, col, cases[i].row[r].colour);
				CHECK(0);
			}
		}
		CHECK(g.crow == cases[i].crow && g.ccol == cases[i].ccol);
	}

	/* keys: j and k go from item to item and stop at the ends */
	CHECK(!grid_resize(&g, 40, 100));
	open_dash();
	int first = g.crow;
	CHECK(tools_key(&e, 'k')); draw(); CHECK(g.crow == first);
	CHECK(tools_key(&e, 'j')); draw(); CHECK(g.crow == first + 2);
	for (int i = 0; i < 9; i++) CHECK(tools_key(&e, i % 2 ? 'j' : KEY_DOWN));
	draw(); CHECK(g.crow == first + 8);
	CHECK(tools_key(&e, KEY_UP)); draw(); CHECK(g.crow == first + 6);
	/* other keys are the editor's: : starts a command line under it */
	CHECK(!tools_key(&e, ':'));
	/* q quits, Enter on Quit too */
	CHECK(tools_key(&e, 'q') && e.quit);
	e.quit = 0;
	CHECK(tools_key(&e, 'j') && tools_key(&e, KEY_ENTER) && e.quit);
	e.quit = 0;
	/* n: insert mode in the empty buffer, the dashboard gone */
	CHECK(tools_key(&e, 'n') && e.mode == MODE_INSERT);
	draw();
	for (int i = 0; i < g.rows * g.cols; i++) CHECK(!(g.c[i].len == 1 && grid_text(&g.c[i])[0] == 'F'));   /* no "Find File" */
	CHECK(!tools_key(&e, 'x'));
	keys_feed(&e, KEY_ESC, 0);
	/* gone for good, also when the buffer is the start buffer again */
	open_dash();
	CHECK(tools_key(&e, 'j'));
	CHECK(!core_edit(&e, 0, 0, "x", 1));
	CHECK(!tools_key(&e, 'j'));
	draw();
	int seen = 0;   /* the buffer's text, not the dashboard */
	for (int c = 0; c < g.cols; c++) seen |= g.c[c].len == 1 && grid_text(&g.c[c])[0] == 'x';
	CHECK(seen);
	/* a file opened from it (r, Enter) reuses the empty start buffer: gone */
	core_free(&e);
	CHECK(!core_open(&e, NULL));
	open_dash();
	CHECK(!core_edit_file(&e, "tests/test_tools_dash.c", 0));
	draw();
	for (int i = 0; i < g.rows * g.cols; i++) CHECK(!(g.c[i].len == 1 && grid_text(&g.c[i])[0] == 'Q' && i % g.cols > 0));   /* no "Quit" */
	CHECK(!tools_key(&e, 'q') && !e.quit);
	grid_free(&g);
	core_free(&e);
	return 0;
}
