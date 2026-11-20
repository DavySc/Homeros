/* The save kitten: Davy's my-nvim kitten.lua (from kittymax's
   kitty-cat.el).  After a save of the buffer in front of you, a kitten in
   the bottom right blinks, puts out its paw and gets a heart; then
   "=^.^= saved NAME" stays as the message.  Any key sends it away.
   src/kitten/; its timing is design/specs/Kitten.tla "ok". */
#ifndef KITTEN_H
#define KITTEN_H
#include "grid.h"
struct Editor;
struct Buf;

/* BufWritePost: core_write calls it after each write that succeeded.  Plays
   only for the current buffer, and once per save (a second write of the
   same buffer while it plays starts nothing). */
void kitten_written(struct Editor *e, struct Buf *b);
/* main.c, for every key before its command (vim.on_key): the kitten goes. */
void kitten_key(void);
/* main.c, after tools_draw.  Draws the frame and, the first time a frame
   is drawn, arms the timer for the next one: a hidden window draws
   nothing, so it is not woken again.  No allocation. */
void kitten_draw(struct Editor *e, Grid *g);
/* :KittenPaw plays it now; <leader>uk turns it on or off for saves. */
void kitten_paw(struct Editor *e);
void kitten_toggle(struct Editor *e);
#endif
