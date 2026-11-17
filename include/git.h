/* Integrated magit (DESIGN.md "Git (magit)", design/specs/Git.tla): the
   status, log and diff buffers with magit's sections and evil-collection
   keys, the commit message buffer, and git as child processes on the loop.
   Package 17 (src/git/); ref/git.c takes no key and opens nothing.  A git
   buffer that changes between keys (a refresh ends) sets e->changed. */
#ifndef GIT_H
#define GIT_H
#include <stddef.h>
struct Editor;
struct Buf;

/* Every key goes here after tools_key, before keys_feed (main.c): 1 if git
   took it (a magit key that is not a Vim motion, ZZ or ZQ in the commit
   message buffer, a git prompt's input), 0 to pass it on. */
int  git_key(struct Editor *e, int key);
/* SPC g <key> (g and s status, l log, L file log, d diff, b branches,
   B open on GitHub) and SPC h <key> (hunks: s stage, r reset, S stage
   buffer, u unstage, p preview); prefix is 'g' or 'h'; keys call it.
   0 ok, -1 not (msg set). */
int  git_leader(struct Editor *e, int prefix, int key);
/* Gutter sign of line l (from 0) of buffer b, from the last refresh's
   diff: 0 none, '+' added, '~' changed, '_' lines deleted below it.  For
   the core's sign column. */
int  git_sign(const struct Buf *b, size_t l);
void git_free(void);                     /* kills git's children, frees all */
#endif
