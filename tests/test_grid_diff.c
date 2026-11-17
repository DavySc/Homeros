/* The frame diff, against a model terminal (test_tty_vt.h): random frames of
   random characters (ASCII, two-byte, wide, four-byte wide), attributes,
   overlapping wide halves, clears and resizes.  After every present the
   model's screen must equal the grid, and the backend must have printed
   exactly the changed cells (plus the two blanks Neovim's
   print_cell_at_pos() puts before a non-ASCII wide one), each frame inside
   one synchronized update.  Then again with a terminal that thinks the
   four-byte characters narrow: the screen must still be right cell for cell.
   Also grid_put's own rules: a two-cell character stays whole (Neovim's
   grid_put_linebuf()). */
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "backend.h"
#include "test_tty_vt.h"

static unsigned char out[1 << 20];

static size_t present(const Grid *g, Grid *shown)
{
	int p[2], saved = dup(1);
	CHECK(!pipe(p));
	dup2(p[1], 1);
	close(p[1]);
	backend_present(g, shown);
	dup2(saved, 1);
	close(saved);
	size_t n = 0;
	ssize_t r;
	while ((r = read(p[0], out + n, sizeof out - n)) > 0) n += (size_t)r;
	close(p[0]);
	return n;
}

static const char *const chars[] = { "a", "b", "~", " ", "\xc3\xa9", "\xce\xbb", "\xe6\xbc\xa2", "\xe2\x88\x80", "\xf0\x9f\x98\x80", "\xf0\x9d\x93\x9d" };
static const int widths[] =         { 1,   1,   1,   1,   1,          1,          2,              1,              2,                  1 };

static int same_width(const unsigned char *s, int n)
{
	for (size_t i = 0; i < sizeof chars / sizeof *chars; i++)
		if ((int)strlen(chars[i]) == n && !memcmp(chars[i], s, (size_t)n)) return widths[i];
	return 1;
}

/* a terminal older than the grid's tables: four-byte characters are narrow */
static int narrow_width(const unsigned char *s, int n) { return n == 4 ? 1 : same_width(s, n); }

static void invariant(const Grid *g)
{
	for (int r = 0; r < g->rows; r++)
		for (int c = 0; c < g->cols; c++) {
			const Cell *x = &g->c[r * g->cols + c];
			if (x->len) continue;
			/* a right half follows a left half that starts a wide character */
			CHECK(c > 0 && x[-1].len > 0 && x[-1].attr == x->attr);
		}
}

static void run(int (*width)(const unsigned char *, int), int frames)
{
	static Vt t;
	static Grid prev;
	Grid g = { 0 }, shown = { 0 };
	int rows = 2 + (int)rnd(10), cols = 2 + (int)rnd(30);
	CHECK(!grid_resize(&g, rows, cols));
	vt_init(&t, rows, cols, width);
	for (int f = 0; f < frames; f++) {
		if (!rnd(40)) {          /* resize: the whole screen is sent again */
			rows = 1 + (int)rnd(12); cols = 1 + (int)rnd(40);
			CHECK(!grid_resize(&g, rows, cols));
			/* the terminal's old content does not matter: a new size is drawn whole */
			if (rows != t.rows || cols != t.cols) vt_init(&t, rows, cols, width);
		} else if (!rnd(20)) grid_clear(&g);
		for (int k = (int)rnd(3) ? (int)rnd(8) : (int)rnd(200); k > 0; k--) {
			unsigned i = rnd(sizeof chars / sizeof *chars);
			int r = (int)rnd((unsigned)rows + 1) - (rnd(9) == 0), c = (int)rnd((unsigned)cols + 1) - (rnd(9) == 0);
			int w = grid_put(&g, r, c, (const unsigned char *)chars[i], strlen(chars[i]), widths[i], (int)rnd(3));
			CHECK(w == (r >= 0 && r < rows && c >= 0 && c + widths[i] <= cols ? widths[i] : 0));
			invariant(&g);
		}
		g.crow = (int)rnd((unsigned)rows);
		g.ccol = (int)rnd((unsigned)cols);
		/* the cells that changed since the last frame, with Neovim's blanks */
		if (prev.rows != rows || prev.cols != cols) CHECK(!grid_resize(&prev, rows, cols));
		int want = 0;
		for (int i = 0; i < rows * cols; i++)
			if (g.c[i].len && !grid_cell_eq(&g.c[i], &prev.c[i]))
				want += 1 + 2 * (g.c[i].ch[0] >= 0x80 && i % cols + 1 < cols && !g.c[i + 1].len);
		int before = t.prints, fr = t.frames;
		size_t n = present(&g, &shown);
		vt_feed(&t, out, n);
		const char *why = vt_differs(&t, &g);
		if (why) {
			fprintf(stderr, "frame %d (%dx%d): %s\n", f, rows, cols, why);
			CHECK(0);
		}
		CHECK(t.frames == fr + 1);
		/* a cell that is part of an unchanged character is never sent */
		CHECK(t.prints - before == want);
		memcpy(prev.c, g.c, (size_t)(rows * cols) * sizeof *g.c);
	}
	grid_free(&g);
	grid_free(&shown);
	grid_free(&prev);
}

int main(void)
{
	/* grid_put's rules, one case each */
	Grid g = { 0 };
	CHECK(!grid_resize(&g, 1, 6));
	const unsigned char *wide = (const unsigned char *)"\xe6\xbc\xa2";
	CHECK(grid_put(&g, 0, 1, wide, 3, 2, ATTR_REV) == 2);
	CHECK(grid_put(&g, 0, 2, (const unsigned char *)"x", 1, 1, ATTR_NONE) == 1);   /* right half */
	CHECK(g.c[1].len == 1 && g.c[1].ch[0] == '>' && g.c[1].attr == ATTR_REV && g.c[2].ch[0] == 'x');
	CHECK(grid_put(&g, 0, 3, wide, 3, 2, ATTR_NONE) == 2);
	CHECK(grid_put(&g, 0, 3, (const unsigned char *)"y", 1, 1, ATTR_NONE) == 1);   /* left half */
	CHECK(g.c[4].len == 1 && g.c[4].ch[0] == ' ');
	CHECK(grid_put(&g, 0, 4, wide, 3, 2, ATTR_NONE) == 2);
	CHECK(grid_put(&g, 0, 3, wide, 3, 2, ATTR_DIM) == 2);                           /* overlapping wide */
	CHECK(g.c[3].len == 3 && !g.c[4].len && g.c[5].len == 1 && g.c[5].ch[0] == ' ');
	CHECK(!grid_put(&g, 0, 5, wide, 3, 2, ATTR_NONE) && !grid_put(&g, 0, 0, wide, 3, 0, ATTR_NONE));
	CHECK(grid_resize(&g, 1 << 16, 1 << 16) == -1 && g.rows == 1);                  /* overflow refused */
	grid_free(&g);

	for (int i = 0; i < 300; i++) run(same_width, 60);
	for (int i = 0; i < 100; i++) run(narrow_width, 60);
	puts("test_grid_diff: 24000 random frames shown exactly by a model terminal, only changed cells sent; wide pairs kept whole");
	return 0;
}
