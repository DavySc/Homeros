/* paint.h's speed paths, seen through the pixels they leave: a mark drawn
   into the window's pixels outside any text moves with the rows a scroll
   copies (a redrawn row would lose it), and a typed row that now matches
   another row is drawn, not copied from it (a copy would bring the mark). */
#include <stdlib.h>
#include "check.h"
#include "test_font_env.h"
#include "font.h"
#include "paint.h"

enum { ROWS = 8, COLS = 20 };
static const uint32_t MARK = 0xff123456;
static int cw, ch;
static uint32_t *px;

static uint32_t *at(int r, int c) { return px + (size_t)(r * ch + 1) * (size_t)(COLS * cw) + (size_t)(c * cw + 1); }

static void row(Grid *g, int r, const char *s) { grid_blank(g, r, 0, 1, COLS); grid_str(g, r, 0, s, 0); }

int main(void)
{
	font_env();
	CHECK(font_open(FAMILY, 20) == 0);
	int base;
	font_cell(&cw, &ch, &base);
	px = calloc((size_t)ROWS * (size_t)ch * (size_t)COLS * (size_t)cw, sizeof *px);
	Grid g = { 0 }, b = { 0 };
	CHECK(px && !grid_resize(&g, ROWS, COLS) && !grid_resize(&b, ROWS, COLS));
	unsigned char dirty[ROWS];
	char s[24];
	g.crow = b.crow = ROWS - 1;

	/* a scroll by one row: row 5's pixels, mark and all, are row 4's now */
	for (int r = 0; r < ROWS - 1; r++) { snprintf(s, sizeof s, "line %d", r); row(&g, r, s); }
	paint(px, COLS * cw, NULL, 0, &g, &b, 1, dirty);
	*at(5, 15) = MARK;
	for (int r = 0; r < ROWS - 2; r++) { snprintf(s, sizeof s, "line %d", r + 1); row(&g, r, s); }
	row(&g, ROWS - 2, "");
	paint(px, COLS * cw, NULL, 0, &g, &b, 0, dirty);
	CHECK(*at(4, 15) == MARK);

	/* typing one row into a copy of the next: drawn, the mark not brought */
	row(&g, 2, "abc");
	row(&g, 3, "abd");
	paint(px, COLS * cw, NULL, 0, &g, &b, 1, dirty);
	*at(3, 15) = MARK;
	row(&g, 2, "abd");
	paint(px, COLS * cw, NULL, 0, &g, &b, 0, dirty);
	CHECK(*at(2, 15) != MARK && *at(3, 15) == MARK);

	grid_free(&g);
	grid_free(&b);
	free(px);
	puts("test_paint: a scroll copies its rows; a typed row is drawn, not copied");
	return 0;
}
