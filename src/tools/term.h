/* The terminal (term.c), and the float pieces it shares with the picker
   (pick.c). */
#ifndef TOOLS_TERM_H
#define TOOLS_TERM_H
#include "core.h"

typedef struct { int y, x, h, w; } Box;
void box_border(Grid *g, Box b, const char *title);   /* rounded, the title centred */

/* Ctrl+. opens and closes it; while open it takes every key but a resize.
   1 taken. */
int  term_key(Editor *e, int k);
void term_draw(Editor *e, Grid *g);
#endif
