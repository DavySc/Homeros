/* Fonts and shaping (package 7, src/font/).  FreeType draws glyphs, HarfBuzz
   shapes runs (ligatures on, so -> is drawn as an arrow), and fontconfig finds
   a fallback font for a cluster the first font lacks, as foot does with fcft.
   Glyph bitmaps are cached per font and glyph in memory taken at font_open,
   so a cache hit allocates nothing. */
#ifndef FONT_H
#define FONT_H
#include <stddef.h>

typedef struct {
	const unsigned char *px;  /* h rows of w coverage bytes, or of w premultiplied BGRA pixels if color */
	int w, h;
	int left, top;            /* from the pen on the baseline to the bitmap's left edge, and up to its top */
	int color;
} Glyph;

typedef struct {
	const Glyph *g;           /* valid until the next font_shape or font_open */
	size_t cluster;           /* byte offset in the run of the cluster this glyph draws */
	int x, y;                 /* pixels from that cluster's cell origin on the baseline; y up */
} Placed;

/* Opens family (a fontconfig name, "JetBrains Mono") at px pixels per em.
   0 ok; -1 if fontconfig has no font at all, or out of memory; a family it
   lacks gets its best match, as in foot.  Called again (a new size or output
   scale) it replaces the font and empties the cache. */
int  font_open(const char *family, int px);
void font_close(void);
/* The cell, from the first font: width, height, baseline from the top. */
void font_cell(int *w, int *h, int *baseline);
/* The style the next font_shape calls use: FONT_BOLD and FONT_ITALIC are
   FACE_BOLD and FACE_ITALIC's bits, so a face's style & 3 is the index.  A
   style the family lacks draws in regular, as foot does. */
enum { FONT_REGULAR, FONT_BOLD, FONT_ITALIC, FONT_BOLD_ITALIC };
void font_style(int style);
/* Shapes one run of UTF-8 text (one row, one attribute, one style); returns how many
   glyphs it wrote to out (at most max), in cluster order. */
int  font_shape(const unsigned char *s, size_t n, Placed *out, int max);
/* Glyph cache lookups since font_open. */
void font_stats(unsigned long *hit, unsigned long *miss);
#endif
