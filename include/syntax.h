/* Syntax colours (package 12, src/syntax/): tree-sitter, in Neovim's and
   Helix's shape.  The tree is edited on every change, reparsed after a
   frame is drawn, and only the visible bytes are queried. */
#ifndef SYNTAX_H
#define SYNTAX_H
#include <stddef.h>
#include "text.h"

typedef struct Syntax Syntax;

/* The grammar for path's file type, or NULL if there is none. */
Syntax *syntax_open(const char *path, const Text *t);
void    syntax_free(Syntax *s);
/* Called before every change to the text (the core's edits and undo's
   UndoHook), while t is still the old text: ts_tree_edit needs the old end
   row and column.  At pos, ndel bytes become ins. */
void    syntax_edit(Syntax *s, const Text *t, size_t pos, size_t ndel, const char *ins, size_t nins);
/* Deferred work, after a frame when no key is waiting: the incremental
   reparse, in slices as Neovim 0.11 parses.  stop is asked now and then
   (between tree-sitter's progress checks, every 100 parse steps, and between
   regions; measured on the slowest file, Emacs 31.1's coding.c, 5 runs of
   460 keys, reviews/pr-30.md: longest stretch per key median 0.29 to 0.41
   ms, p99 1.9 to 3.1, max 3.2 to 11.3 inside one parse step;
   the first update of a language also compiles its queries, 9 to 23 ms
   without asking) and returns nonzero when input is waiting.  0 nothing changed, 1 colours changed (draw again), 2 stopped
   early (call again once the input is handled). */
int     syntax_update(Syntax *s, const Text *t, int (*stop)(void));
/* One face (grid.h FACE_*) per byte of [a, z), z - a at most 8,192 (core_draw
   asks for at most 1,024), into face; no allocation. */
void    syntax_faces(Syntax *s, size_t a, size_t z, unsigned char *face);
#endif
