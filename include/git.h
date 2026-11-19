/* Integrated magit (DESIGN.md "Git (magit)", design/specs/Git.tla): the
   status, log and diff buffers with magit's sections and evil-collection
   keys, the commit message buffer, and git as child processes on the loop.
   Package 17 (src/git/).  A git
   buffer that changes between keys (a refresh ends) sets core_changed. */
#ifndef GIT_H
#define GIT_H
#include <stddef.h>
struct Editor;
struct Buf;

/* Every key goes here after tools_key, before keys_feed (main.c), except
   while a command is half typed (e->ks.pending, so "3x" stays x): 1 if git
   took it (a magit key that is not a Vim motion, ZZ or ZQ in the commit
   message buffer, a git prompt's input), 0 to pass it on. */
int  git_key(struct Editor *e, int key);
/* SPC g <key> (g and s status, l log, L file log, d diff, b branches,
   B open on GitHub) and SPC h <key> (hunks: s stage, r reset, S stage
   buffer, u unstage, p preview); prefix is 'g' or 'h'; keys call it.
   0 ok, -1 not (msg set). */
int  git_leader(struct Editor *e, int prefix, int key);
/* A file was read into b, or b got a new name (gitsigns' attach on
   BufRead): finds its repository and starts its status read.  Attaching b
   again replaces what it had. */
void git_attach(const struct Buf *b);
/* Gutter sign of line l (from 0) of buffer b, from the last refresh's
   diff: 0 none, '+' added, '~' changed, '_' lines deleted below it.  For
   the core's sign column.  No I/O, no allocation: 0 for a buffer never
   attached. */
int  git_sign(const struct Buf *b, size_t l);
/* lualine's branch for the status line: the branch of b's repository
   (.git/HEAD's ref name, a short hash when detached), NULL outside one, for
   a buffer never attached, or before git has looked.  Cached by git's
   refresh: no I/O and no allocation per call, since it is asked every
   frame. */
const char *git_branch(const struct Buf *b);
void git_free(void);                     /* kills git's children, frees all */
#endif
