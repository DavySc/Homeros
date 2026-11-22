/* Inline images in window mode (package 21, src/image/): Markdown and Org
   image links, org-download and Org's LaTeX previews (FEATURES.md, "images
   and LaTeX inline (window mode)").  Decoded once by stb_image (Linux and
   Windows alike) into a cache; drawing copies pixel rows, allocating
   nothing. */
#ifndef IMAGE_H
#define IMAGE_H
#include <stddef.h>
#include <stdint.h>
#include "grid.h"
#include "text.h"
struct Syntax;

/* The image at path as a number 1..255, 0 if unreadable; cached by path,
   mtime, size and bg.  Scaled down (never up) to fit cols x rows cells of
   cw x ch pixels, laid over bg (0xRRGGBB), padded to whole cells. */
int  image_load(const char *path, int cols, int rows, int cw, int ch, uint32_t bg);
/* image n's size in cells */
void image_size(int n, int *cols, int *rows);
/* image n at row r, column c of g (ATTR_IMAGE cells), its first skip rows
   left out (scrolled above r), clipped to g */
void image_put(Grid *g, int r, int c, int n, int skip);
/* the tile cell x shows, into an XRGB8888 buffer (Wayland shm, 32-bit
   DIB) whose pixel rows are stride pixels apart: one memcpy per pixel row */
void image_draw(uint32_t *px, int stride, const Cell *x);
/* Org's dvipng preview of tex: the PNG's path once rendered; else starts
   latex then dvipng (proc_spawn, loop.h) into dir, returns NULL and sets
   core_changed when they are done. */
const char *image_latex(const char *tex, size_t n, uint32_t fg, int dpi, const char *dir);
/* The image line l of t shows below it, as virtual lines (package 4): a
   Markdown image link, an Org link to a .png, .jpg or .jpeg file, or an
   Org LaTeX fragment (image_latex into ltximg/ beside the file); relative
   paths from path's directory, the file type from path (syntax_filetype),
   code blocks skipped through s.  Its number for image_put (image_size
   gives its rows), fit to cols x rows cells of cw x ch pixels; 0 when the
   line has none, cw is 0 (the terminal: the link text stays), the file
   does not decode, or the fragment is still rendering (core_changed is set
   when it is done).  Cached by the line's bytes and the box, so an edit
   needs no hook and a frame does no stat() after the first. */
int image_line(const Text *t, struct Syntax *s, const char *path, size_t l, int cols, int rows, int cw, int ch, uint32_t fg, uint32_t bg);
#endif
