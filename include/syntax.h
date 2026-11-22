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
/* Neovim's 'filetype' for path ("sql", "cpp" for a .h), NULL when unknown
   or path is NULL (an unnamed buffer); the status line shows it (thread
   0's, agreed) */
const char *syntax_filetype(const char *path);
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
   without asking) and returns nonzero when input is waiting.  Once a parse is done it recomputes
   the fold levels of the rows the edits and the parse changed, as _fold.lua's
   on_bytes and on_changedtree do.  [*r1, *r2) are the rows whose
   syntax_foldexpr answers may have changed, for fold_update (r1 == r2: none;
   set on every call).  0 nothing changed, 1 colours or folds changed (draw
   again), 2 stopped early (call again once the input is handled). */
int     syntax_update(Syntax *s, const Text *t, int (*stop)(void), size_t *r1, size_t *r2);
/* One face (grid.h FACE_*) per byte of [a, z), z - a at most 8,192 (core_draw
   asks for at most 1,024), into face; no allocation. */
void    syntax_faces(Syntax *s, size_t a, size_t z, unsigned char *face);
/* Neovim's conceal (conceallevel=2, concealcursor="": the core draws the
   cursor's line as typed, as org-appear and render-markdown's
   anti_conceal do): what line l shows in place of some of its bytes, up
   to cap spans in byte order, bytes [a, z) of the line (columns, not
   buffer offsets, as SyntaxMd's) drawn as s, one character of one
   cell or "" (hidden): Neovim's conceal char (matchadd's 'conceal'), so
   x^{2} is three spans, "^{" hidden, "2" as "²", "}" hidden.  Org: org-pretty-entities (\leq as ≤), sub- and superscripts in
   {} where every character has a Unicode one, org-hide-emphasis-markers
   (orgfx_conceal).  The one conceal mechanism for every file type.  No
   allocation; returns the count. */
typedef struct { size_t a, z; const char *s; } Conceal;
size_t  syntax_conceal(Syntax *s, size_t line, Conceal *out, size_t cap);
/* foldexpr's value for line (from 0), as Neovim reads it: the fold level,
   and *start 1 for ">n" (a fold starts here).  Tree-sitter's
   vim.treesitter.foldexpr() (folds.scm), Markdown's ftplugin
   (g:markdown_folding: heading levels), Org's subtrees (orgfx_foldexpr);
   0 for a buffer without folds.  It answers from the levels the last
   finished parse left, never parsing, as vim.treesitter.foldexpr() reads
   foldinfo.levels.  A row an edit added answers -1 (Neovim's "-1":
   undefined, take a neighbour's level) until the next parse. */
int     syntax_foldexpr(Syntax *s, size_t line, int *start);
/* What render-markdown draws on line l, from the tree, without allocating:
   0 for a non-Markdown buffer or a line with none of these, else 1 */
typedef struct {
	unsigned char head;    /* ATX heading level 1-6, else 0 */
	unsigned char list;    /* list_item nesting from 1, else 0 */
	unsigned char ordered; /* the list marker is n. or n) */
	unsigned char task;    /* ' ', 'x' or '-' for [ ], [x], [-]; else 0 */
	unsigned char code;    /* fenced code: 1 open fence, 2 a background row
	                          (start_row + 1 to end_row - 2 of the block's
	                          range, as code.lua), 3 close; else 0 (none for
	                          a range spanning 1 row or fewer, which
	                          render-markdown skips) */
	size_t mark, mend;     /* byte columns of the heading's '#'s or the fence;
	                          on a code 2 row mark is the block's start column */
	size_t item, iend;     /* byte columns of the list marker (a line can hold
	                          a list marker and a heading: "- # x") */
	size_t index;          /* ordered: this list_item and the list_items
	                          right before it, from 1 (sibling_count) */
	size_t task_col;       /* column of a task marker's '[' */
	size_t info, ninfo;    /* the open fence's info string */
	size_t lang, nlang;    /* its language node, column and length */
} SyntaxMd;
int     syntax_md(Syntax *s, const Text *t, size_t l, SyntaxMd *out);
#endif
