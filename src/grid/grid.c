/* The cell grid.  A two-cell character is its left cell (the bytes) and a
   right cell of len 0; grid_put keeps that pair whole when another character
   lands on half of it, as Neovim's grid_put_linebuf() does, so the frame diff
   and every backend can trust it.  A cell holds a whole grapheme cluster:
   up to 4 bytes in the cell, a longer one interned in a static store whose
   slot number the cell holds instead (Neovim's schar_T and glyph_cache), so
   comparing two cells stays 4 bytes. */
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "grid.h"
#include "uni.h"

/* The cluster store: an open-addressed hash table (linear probing) whose
   slot is the cluster itself, so a cluster's number is its slot.  Emptied by
   grid_frame once over half full, so a frame can always add SLOTS / 2 - 1
   new clusters; past that (hypothesis: no real screen gets there) a cluster
   shows as U+FFFD, Neovim's sign for a cell it cannot show. */
enum { SLOTS = 1 << 17 };
static struct { unsigned char len, b[GRID_CLUSTER_MAX]; } store[SLOTS];
static unsigned used;

static int is_cont(unsigned char b) { return b >= 0x80 && b < 0xC0; }

/* the cluster's slot, -1 if the store is full */
static long intern(const unsigned char *s, size_t n)
{
	unsigned h = 2166136261u;                       /* FNV-1a */
	for (size_t i = 0; i < n; i++) h = (h ^ s[i]) * 16777619u;
	for (h %= SLOTS; store[h].len; h = (h + 1) % SLOTS)
		if (store[h].len == n && !memcmp(store[h].b, s, n)) return h;
	if (used == SLOTS - 1) return -1;               /* one slot stays empty: probes end */
	used++;
	store[h].len = (unsigned char)n;
	memcpy(store[h].b, s, n);
	return h;
}

const unsigned char *grid_text(const Cell *c)
{
	if (c->len <= 4) return c->ch;
	unsigned h;
	memcpy(&h, c->ch, sizeof h);
	return store[h].b;
}

int grid_frame(void)
{
	if (used <= SLOTS / 2) return 0;
	memset(store, 0, sizeof store);
	used = 0;
	return 1;
}

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
	for (int i = 0; i < g->rows * g->cols; i++) g->c[i] = (Cell){ " ", 1, ATTR_NONE, FACE_NONE, 0 };
}

int grid_put(Grid *g, int r, int c, const unsigned char *s, size_t n, int width, int attr)
{
	if (r < 0 || r >= g->rows || c < 0 || (width != 1 && width != 2) || c + width > g->cols
	    || n == 0) return 0;
	/* too long: its longest prefix of whole code points that fits, as
	   Neovim's utfc_ptrlen2schar() cuts it */
	if (n > GRID_CLUSTER_MAX)
		for (n = GRID_CLUSTER_MAX; n > 1 && is_cont(s[n]); n--) {}
	long h = n > 4 ? intern(s, n) : 0;
	if (h < 0) { s = (const unsigned char *)"\xef\xbf\xbd"; n = 3; }
	Cell *x = &g->c[r * g->cols + c];
	/* landing on the right half of a two-cell character: Neovim truncates
	   its left half into '>' (grid_put_linebuf) */
	if (c > 0 && !x->len) x[-1] = (Cell){ ">", 1, x[-1].attr, x[-1].face, x[-1].bg };
	/* covering only the left half of one: its right half becomes a space */
	if (c + width < g->cols && !x[width].len) x[width] = (Cell){ " ", 1, x[width].attr, x[width].face, x[width].bg };
	if (n > 4) { unsigned u = (unsigned)h; memcpy(x->ch, &u, sizeof u); }
	else memcpy(x->ch, s, n);
	x->len = (unsigned char)n;
	x->attr = (unsigned char)attr;
	x->face = FACE_NONE;
	x->bg = 0;
	if (width == 2) x[1] = (Cell){ "", 0, (unsigned char)attr, FACE_NONE, 0 };
	return width;
}

int grid_puts(Grid *g, int r, int c, int end, const char *s, size_t n, int attr, int face)
{
	const unsigned char *u = (const unsigned char *)s;
	for (size_t i = 0, len; i < n; i += len) {
		int w = uni_width(u, n, i), k = c;
		len = uni_glen(u, n, i);
		if (c + w > end) break;
		if (w > 2 || u[i] < 0x20 || u[i] == 0x7f)
			for (int j = 0; j < w; j++) c += grid_put(g, r, c, (const unsigned char *)" ", 1, 1, attr);
		else c += grid_put(g, r, c, u + i, len, w, attr);
		for (; k < c; k++) g->c[r * g->cols + k].face = (unsigned char)face;
	}
	return c;
}

int grid_width(const char *s, size_t n)
{
	int c = 0;
	for (size_t i = 0; i < n; i += uni_glen((const unsigned char *)s, n, i))
		c += uni_width((const unsigned char *)s, n, i);
	return c;
}

const Face *grid_face(const Cell *x, const Face **bg)
{
	const Face *f = &faces[x->face < FACE_COUNT ? x->face : FACE_NONE];
	*bg = x->bg && x->bg < FACE_COUNT ? &faces[x->bg] : f;
	return f;
}

int grid_cell_eq(const Cell *a, const Cell *b)
{
	return a->len == b->len && a->attr == b->attr && a->face == b->face && a->bg == b->bg && !memcmp(a->ch, b->ch, a->len < 4 ? a->len : 4);
}
