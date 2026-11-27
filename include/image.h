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
/* The window's wallpaper, as WezTerm's window_background_image: the PNG or
   JPEG "wallpaper" in Homeros' config folder (file_stdpath 2), a fifth of
   it laid over bg, at its own size (*iw x *ih pixels, rows *iw apart);
   decoded again only when the file or bg changes, which changes *ver
   (each window compares it with the picture it holds), looked for at
   each call (a window calls it when its size changes).  src: the part that covers a
   w x h window (aspect kept, centred), as x, y, width, height, which the
   window scales to its size.  NULL if there is none. */
const uint32_t *image_wallpaper(int w, int h, uint32_t bg, int *iw, int *ih, int src[4], unsigned *ver);
#endif
