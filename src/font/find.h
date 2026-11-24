/* The font module's own interface between its files: font.c shapes, draws
   and caches; sprite.c draws the characters that fill the cell; the
   system's font finder is fc.c (fontconfig, as foot) or, on Windows,
   dwrite.c (DirectWrite's font fallback, as WezTerm and Windows Terminal),
   so both systems draw with the same FreeType and HarfBuzz code. */
#ifndef FIND_H
#define FIND_H

/* Whether c is drawn by sprite_draw rather than from a font */
int  sprite(unsigned c);
/* c's coverage, cw by ch bytes, its lines t pixels thick (light) */
void sprite_draw(unsigned c, int cw, int ch, int t, unsigned char *out);

/* Starts the finder on family (several, comma-separated, are tried in
   order) at px pixels per em; returns how many fonts it may ever give,
   which number them 0 up (0 is the main font), or -1 if none */
int  find_open(const char *family, int px);
void find_close(void);
/* Font i's file (UTF-8) and its index in the file; NULL if none */
const char *find_file(int i, int *index);
/* The first font after `after` that has all n characters of cp, colour
   fonts only if color; -1 if none */
int  find_font(const unsigned *cp, unsigned n, int after, int color);
#endif
