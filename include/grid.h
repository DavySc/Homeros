/* The cell grid: what the core draws and a backend shows.  The core fills a
   grid each frame; a backend compares it with the last frame it showed and
   sends only changed cells (Neovim's grid, foot).  No allocation after
   grid_resize, so the redraw path allocates nothing. */
#ifndef GRID_H
#define GRID_H
#include <stddef.h>

enum { ATTR_NONE, ATTR_DIM, ATTR_REV };   /* line numbers, status line */
/* Syntax faces (package 12): Neovim's standard tree-sitter captures; a
   backend maps each to a colour of the theme. */
enum { FACE_NONE, FACE_COMMENT, FACE_STRING, FACE_KEYWORD, FACE_TYPE, FACE_FUNCTION,
       FACE_CONSTANT, FACE_NUMBER, FACE_OPERATOR, FACE_PUNCT, FACE_VARIABLE, FACE_COUNT };

typedef struct {
	unsigned char ch[4], len;  /* one character, UTF-8; len 0 = right half of a wide one */
	unsigned char attr, face;  /* face: FACE_*, set by the core after grid_put */
} Cell;

typedef struct {
	int rows, cols;
	Cell *c;              /* rows * cols */
	int crow, ccol;       /* the cursor */
} Grid;

int  grid_resize(Grid *g, int rows, int cols);    /* 0 ok, -1 out of memory; cells cleared */
void grid_free(Grid *g);
void grid_clear(Grid *g);
/* Puts one character at (r, c) if it fits; returns the cells it took. */
int  grid_put(Grid *g, int r, int c, const unsigned char *s, size_t n, int width, int attr);
/* Puts an ASCII string; returns the next column. */
int  grid_puts(Grid *g, int r, int c, const char *s, int attr);
int  grid_cell_eq(const Cell *a, const Cell *b);
#endif
