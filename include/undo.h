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
/* Close the open group, then undo (redo) one group.  1 done, 0 nothing to
   undo (redo), -1 out of memory. */
int   undo_undo(Undo *u, Text *t, UndoWhere *w);
int   undo_redo(Undo *u, Text *t, UndoWhere *w);
/* Number of the current state, as Neovim's seq_cur: 0 is the loaded text,
   every group gets the next number. */
unsigned long undo_seq(const Undo *u);
#endif
