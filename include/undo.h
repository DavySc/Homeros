/* Undo: groups of edits.  ref/undo.c is a list (redo cleared by a new edit,
   Undo.tla "ok"); package 2 (src/undo/) makes it Neovim's tree (UndoTree.tla,
   proofs/Undo.lean, ref/undo/undo_diff.py) with the 1,000-change cap and the
   saved file, adding g-, g+, :undo N, :earlier and :later through thread 0.
   A position is a byte offset; a line/col pair is the cursor the core saved. */
#ifndef UNDO_H
#define UNDO_H
#include <stddef.h>
#include "text.h"

typedef struct Undo Undo;
/* The cursor and the core's state flags (CORE_EMPTY) when a group opened. */
typedef struct { size_t line, col; unsigned flags; } UndoCursor;
/* Where an undo or redo changed the text, for the core's cursor rule:
   bytes [lo, hi) of the resulting text differ from before; cur is the cursor
   saved when the group was opened; flags are the core's flags for the
   resulting state (Neovim's UH_EMPTYBUF). */
typedef struct { size_t lo, hi; UndoCursor cur; unsigned flags; } UndoWhere;

Undo *undo_new(void);
void  undo_free(Undo *u);
/* Records an edit already applied to the text: at pos, del (ndel bytes) was
   replaced by ins (nins bytes).  Opens a group, saving cur, if none is open;
   after is the core's flags once the edit is made.  0 ok, -1 out of memory. */
int   undo_record(Undo *u, size_t pos, const char *del, size_t ndel,
                  const char *ins, size_t nins, UndoCursor cur, unsigned after);
void  undo_close(Undo *u);               /* ends the open group, if any */
/* Called before each change undo or redo makes to the text, while the old
   text is still there (syntax_edit needs it; Neovim's one choke point is
   extmark_splice in u_undoredo): at pos, ndel bytes become ins. */
typedef void UndoHook(void *ctx, const Text *t, size_t pos, size_t ndel, const char *ins, size_t nins);
void  undo_hook(Undo *u, UndoHook *fn, void *ctx);
/* Close the open group, then undo (redo) one group.  1 done, 0 nothing to
   undo (redo), -1 out of memory. */
int   undo_undo(Undo *u, Text *t, UndoWhere *w);
int   undo_redo(Undo *u, Text *t, UndoWhere *w);
/* Number of the current state, as Neovim's seq_cur: 0 is the loaded text,
   every group gets the next number. */
unsigned long undo_seq(const Undo *u);

/* Moves through the tree in time, Neovim's undo_time(): closes the open
   group and plans the move.  A move is several groups and the cursor is
   placed after each (u_undoredo), so undo_step then applies it group by
   group.  SEQ counts changes (g- is -1), SEC seconds, FILE writes, ABS is
   :undo N.  1 planned (maybe no groups), 0 already at the oldest or newest,
   -1 no state N (E830). */
enum { UNDO_SEQ, UNDO_SEC, UNDO_FILE, UNDO_ABS };
int   undo_time(Undo *u, long step, int unit);
/* The next group of the planned move: 1 done (w as for undo_undo), 0 the
   move is over, -1 out of memory. */
int   undo_step(Undo *u, Text *t, UndoWhere *w);
void  undo_saved(Undo *u);               /* the text was written (u_update_save_nr) */
/* The undo file, keyed by the SHA-256 of the text (u_compute_hash).  write:
   0 ok, -1 errno set.  read: NULL if there is no file, it belongs to
   another text, or it is damaged. */
int   undo_write(const Undo *u, const Text *t, const char *path);
Undo *undo_read(const Text *t, const char *path);
#endif
