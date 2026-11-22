/* Daily tools (package 15, src/tools/): the pickers (snacks.picker in
   Davy's Neovim config: SPC ff smart, fg grep, "," buffers, fr registers,
   e explorer; the dashboard's files and recent), the dashboard and the
   file manager (oil.nvim: -, SPC pv, :e of a directory); later the
   terminal, SQL, debugger and Claude.  A picker's
   list that changes between keys (rg output arriving) sets core_changed. */
#ifndef TOOLS_H
#define TOOLS_H
#include "grid.h"
struct Editor;
struct Buf;

enum { PICK_SMART, PICK_GREP, PICK_BUFFERS, PICK_REGISTERS, PICK_EXPLORER, PICK_FILES, PICK_RECENT, PICK_SNIPPETS };
int  tools_pick(struct Editor *e, int what);        /* 0 ok, -1 msg set */
/* Every key goes here first (main.c), except while a command is half typed
   (e->ks.pending): 1 if an open picker took it, 0 at once when none is open. */
int  tools_key(struct Editor *e, int key);
/* The open picker over the windows, right after core_draw.  No allocation. */
void tools_draw(struct Editor *e, Grid *g);
/* Opens buffer "oil:///abs/dir/" (oil's name); dir NULL is the current
   file's directory.  0 ok, -1 msg set. */
int  tools_oil(struct Editor *e, const char *dir);
/* oil's buffer-local normal-mode keys (Enter - _ g. gd ga q C-l C-p): keys
   call it in an oil:// buffer with nothing pending.  1 taken. */
int  tools_oil_key(struct Editor *e, int key);
/* :w of an oil:// buffer applies its edits (oil's BufWriteCmd); core_write
   calls it.  0 ok, -1 msg set. */
int  tools_write(struct Editor *e, struct Buf *b);
/* The dashboard (snacks.dashboard as my-nvim sets it up): over the empty
   first window when homeros starts without a file, until that window shows
   another buffer or the buffer changes.  It takes keys through tools_key
   and draws in tools_draw.  start_ns: when main() began (TIME_UTC), for its
   "loaded in" line; 0 (:Dashboard, SPC D) keeps the time measured at
   startup (thread 0's, agreed). */
void tools_dash(struct Editor *e, long long start_ns);
#endif
