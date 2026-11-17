/* Stand-in for package 7 (font.h, not cut yet): one font found by
   fontconfig, FreeType coverage bitmaps cached by code point, no shaping
   and no fallback.  Goes when font.h lands. */
#ifndef GLYPH_H
#define GLYPH_H
typedef struct { int w, h, left, top; const unsigned char *a; } Glyph;
extern int glyph_cw, glyph_ch, glyph_asc;   /* cell width, height, baseline */
int  glyph_open(int px);    /* (re)opens at px pixels per em; 0 ok, -1 error */
const Glyph *glyph_get(unsigned cp);        /* valid until the next call */
void glyph_close(void);
#endif
