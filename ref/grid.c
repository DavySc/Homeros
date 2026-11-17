#include <stdlib.h>
#include <string.h>
#include "grid.h"

int grid_resize(Grid *g, int rows, int cols)
{
	Cell *c = calloc((size_t)rows * (size_t)cols, sizeof *c);
	if (!c && rows && cols) return -1;
	free(g->c);
	g->c = c; g->rows = rows; g->cols = cols; g->crow = g->ccol = 0;
	grid_clear(g);
	return 0;
}

void grid_free(Grid *g) { free(g->c); g->c = NULL; g->rows = g->cols = 0; }

void grid_clear(Grid *g)
{
	for (int i = 0; i < g->rows * g->cols; i++) g->c[i] = (Cell){ " ", 1, ATTR_NONE, FACE_NONE };
}

int grid_put(Grid *g, int r, int c, const unsigned char *s, size_t n, int width, int attr)
{
	if (r < 0 || r >= g->rows || c < 0 || c + width > g->cols || n == 0 || n > 4) return 0;
	Cell *x = &g->c[r * g->cols + c];
	memcpy(x->ch, s, n);
	x->len = (unsigned char)n;
	x->attr = (unsigned char)attr;
	x->face = FACE_NONE;
	if (width == 2) x[1] = (Cell){ "", 0, (unsigned char)attr, FACE_NONE };
	return width;
}

int grid_puts(Grid *g, int r, int c, const char *s, int attr)
{
	for (; *s; s++) c += grid_put(g, r, c, (const unsigned char *)s, 1, 1, attr) ? 1 : 0;
	return c;
}

int grid_cell_eq(const Cell *a, const Cell *b)
{
	return a->len == b->len && a->attr == b->attr && a->face == b->face && !memcmp(a->ch, b->ch, a->len);
}
