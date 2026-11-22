/* Folds (package 20, src/fold/): Neovim 0.12's fold.c with
   foldmethod=expr, the way my-nvim folds every filetype, one table per
   window.  The levels come from a FoldExpr (the core passes
   syntax_foldexpr); each fold keeps its open or closed state across edits
   as foldMarkAdjust() and foldUpdateIEMS() keep it.  'foldminlines' 1,
   'foldenable' always on.  Lines are from 0. */
#ifndef FOLD_H
#define FOLD_H
#include <stddef.h>

typedef struct Fold Fold;
/* foldexpr's value for line: the level, *start 1 for ">n" */
typedef int FoldExpr(void *ctx, size_t line, int *start);
/* the line takes more than one screen row ('wrap'): a one-line fold shows
   closed only then (checkSmall()) */
typedef int FoldTall(void *ctx, size_t line);

/* A window's folds with 'foldlevel' level (99; 1 for .org, nvim-orgmode's
   org_startup_folded 'content'); tall NULL: no line is ('nowrap').  No
   folds until the first fold_update.  NULL when out of memory. */
Fold *fold_new(int level, FoldExpr *expr, FoldTall *tall, void *ctx);
/* the folds of a window that is split (copyFoldingState()), with ctx */
Fold *fold_dup(const Fold *f, void *ctx);
void  fold_free(Fold *f);
/* foldMarkAdjust(), beside the core's mark_adjust(): lines [l1, l2) move
   by amount (FOLD_DEL: deleted), lines from l2 on by after; l2 (size_t)-1
   is the end of the buffer.  insert: in Insert mode (a line appended there
   joins the fold above it) */
enum { FOLD_DEL = -1 };
void  fold_adjust(Fold *f, size_t l1, size_t l2, long amount, long after, int insert);
/* foldUpdate() after changed_lines(): the lines [top, end) changed, the
   buffer has n lines.  The first call does every line.  It calls expr, so
   never from the draw path.  Neovim skips it in Insert mode and
   vim.treesitter.foldexpr() catches up on InsertLeave. */
void  fold_update(Fold *f, size_t top, size_t end, size_t n);
/* hasFolding(): 1 and the closed fold [*l1, *l2] holding line, else 0;
   no allocation */
int   fold_closed(const Fold *f, size_t line, size_t *l1, size_t *l2);
/* foldlevel(): how many folds hold line */
int   fold_level(const Fold *f, size_t line);
/* za zc zo zO (count times, at line), zR zM, zv (open folds at line), zX
   (after a fold_update of every line: 'foldlevel' again), and setting
   'foldlevel' to count (nvim-orgmode's S-Tab).  0, or -1 for "E490: No
   fold found" */
enum { FOLD_TOGGLE, FOLD_CLOSE, FOLD_OPEN, FOLD_OPEN_REC, FOLD_OPEN_ALL,
       FOLD_CLOSE_ALL, FOLD_VIEW, FOLD_RESET, FOLD_LEVEL };
int   fold_cmd(Fold *f, int op, size_t line, long count);
#endif
