/* Snippets (package 19, src/snip/): Davy's catalogue, snippets/sql.md in
   Homeros' config folder (file_stdpath 2), offered in the completion popup
   while typing in a .sql buffer and expanded as Neovim 0.12's vim.snippet
   does: Tab and S-Tab jump between the tab stops, a placeholder is selected
   and typing replaces it, stops with the same number mirror each other
   live.  The catalogue is Markdown: each ```sql block whose first line is
   "-- name : description" is a snippet in VS Code's syntax. */
#ifndef SNIP_H
#define SNIP_H
#include <stddef.h>
struct Editor;
/* main.c offers each key here after tools_key and git_key: 1 taken.  While a
   snippet is active or the popup can open (Insert mode in a .sql buffer),
   snip_key runs the key through keys_feed itself, since the mirrors and the
   popup follow the text the key made.  pending is keys_feed's: passing it on
   keeps insertchar()'s batching of typeahead for a paste. */
int snip_key(struct Editor *e, int key, int pending);
struct Buf;
struct Text;
/* Every change to buffer b, from core.c's undo_seen() (Neovim's
   extmark_splice): bytes [pos, pos + ndel) of t became the nins bytes ins,
   and the active snippet's stops move with it. */
void snip_edit(const struct Buf *b, const struct Text *t, size_t pos, size_t ndel, const char *ins, size_t nins);
#endif
