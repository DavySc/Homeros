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
   (e->ks.pending): 1 if an open picker took it (or :make's buffer, Enter),
   0 at once otherwise. */
int  tools_key(struct Editor *e, int key);
/* 1 while the terminal (Ctrl+.) is open: it takes every key, Ctrl+Z too,
   so main.c does not suspend the editor (Neovim's :terminal, Ghostty). */
int  tools_term(void);
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
/* :make cmd (Neovim's :make and quickfix list; Emacs's compile buffer):
   the shell ('shell' -c, cmd.exe /s /c on Windows) runs cmd in the current
   directory, its output shown as it comes in a buffer of its own in a
   window below, the errors it names (path:line[:col], path(line[,col]), a
   C:\ path whole) the quickfix list; Enter on one of its lines opens that
   error.  cmd NULL or "" runs the last command again (SPC cC); while its
   buffer is in a window every write does too (focus's runner).  0 ok, -1
   msg set. */
int  tools_make(struct Editor *e, const char *cmd);
/* the command :make last ran, NULL if none: SPC cc offers it for editing,
   as Emacs's compile offers compile-command */
const char *tools_make_last(void);
/* ]q (n > 0), [q (n < 0): the quickfix error n entries on, as :cnext and
   :cprevious; E42 without errors, E553 past the end.  0 ok, -1 msg set. */
int  tools_qf(struct Editor *e, long n);
/* BufWritePost: core_write calls it after each write. */
void tools_written(struct Editor *e, struct Buf *b);
#endif
