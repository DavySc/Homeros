/* The cell grid.  A two-cell character is its left cell (the bytes) and a
   right cell of len 0; grid_put keeps that pair whole when another character
   lands on half of it, as Neovim's grid_put_linebuf() does, so the frame diff
   and every backend can trust it. */
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "grid.h"

int grid_resize(Grid *g, int rows, int cols)
{
	if (rows < 0 || cols < 0 || (cols && rows > INT_MAX / cols)) return -1;
	Cell *c = malloc((size_t)rows * (size_t)cols * sizeof *c);
	if (!c && rows && cols) return -1;
	free(g->c);
	g->c = c; g->rows = rows; g->cols = cols; g->crow = g->ccol = 0;
	grid_clear(g);
	return 0;
}

void grid_free(Grid *g) { free(g->c); g->c = NULL; g->rows = g->cols = 0; }

void grid_clear(Grid *g)
{
	for (int i = 0; i < g->rows * g->cols; i++) g->c[i] = (Cell){ " ", 1, ATTR_NONE };
}

int grid_put(Grid *g, int r, int c, const unsigned char *s, size_t n, int width, int attr)
{
	if (r < 0 || r >= g->rows || c < 0 || (width != 1 && width != 2) || c + width > g->cols
	    || n == 0 || n > 4) return 0;
	Cell *x = &g->c[r * g->cols + c];
	/* landing on the right half of a two-cell character: Neovim truncates
	   its left half into '>' (grid_put_linebuf) */
	if (c > 0 && !x->len) x[-1] = (Cell){ ">", 1, x[-1].attr };
	/* covering only the left half of one: its right half becomes a space */
	if (c + width < g->cols && !x[width].len) x[width] = (Cell){ " ", 1, x[width].attr };
	memcpy(x->ch, s, n);
	x->len = (unsigned char)n;
	x->attr = (unsigned char)attr;
	if (width == 2) x[1] = (Cell){ "", 0, (unsigned char)attr };
	return width;
}

int grid_puts(Grid *g, int r, int c, const char *s, int attr)
{
	for (; *s; s++) c += grid_put(g, r, c, (const unsigned char *)s, 1, 1, attr);
	return c;
}

int grid_cell_eq(const Cell *a, const Cell *b)
{
	return a->len == b->len && a->attr == b->attr && !memcmp(a->ch, b->ch, a->len);
}
