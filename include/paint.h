/* The window backends' painter (src/paint/): grids drawn on the CPU into
   32-bit pixels, 0xAARRGGBB, opaque but where a wallpaper shows (Wayland's
   ARGB8888 or XRGB8888 and the Windows DIB are the same bytes), with
   font.h's glyphs, as foot draws.  The backends open the font (font_open)
   and own the pixels; the painter keeps no state between calls. */
#ifndef PAINT_H
#define PAINT_H
#include <stdint.h>
#include "grid.h"

/* A backend's own attr after grid.h's: its input method's preedit, drawn
   underlined (Wayland's text-input-v3) */
enum { PAINT_PRE = ATTR_IMAGE + 1 };
/* The window's base and text colours (Catppuccin Mocha's), 0xRRGGBB */
enum { PAINT_BASE = 0x1e1e2e, PAINT_TEXT = 0xcdd6f4 };

/* back: where the base colour shows, what shows instead (a wallpaper):
   the pixels at the same place in back, row y at back + y * bstride
   (bstride 0: one row for every row, as Wayland's clear one); NULL: the
   base colour.

   Fills the first w by h pixels of px, stride pixels a row, with the
   window's background, as a grid of spaces shows */
void paint_clear(uint32_t *px, int stride, const uint32_t *back, int bstride, int w, int h);
/* Draws g into px, stride pixels a row, which shows grid b: rows a scroll
   moved are copied (unless back differs by row), of each other row only
   the cells that differ from b are drawn (all: every cell).  Makes b's cells and cursor g's; dirty[r]
   (g->rows bytes) says whether row r changed. */
void paint(uint32_t *px, int stride, const uint32_t *back, int bstride, const Grid *g, Grid *b, int all, unsigned char *dirty);
/* The cells of row r of g that differ from row q of b, as *lo to *hi,
   widened to word starts; how many, 0 if none differ */
int  paint_span(const Grid *g, int r, const Grid *b, int q, int *lo, int *hi);
#endif
