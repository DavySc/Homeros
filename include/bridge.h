/* VS Code's Lean infoview in a browser (package 14, src/infoview/;
   DESIGN.md "Infoview"): the page is vscode-lean4's @leanprover/infoview,
   as kittymax's \w, served on 127.0.0.1 under a secret path by a child
   process (homeros-infoview) on a pipe, so the editor holds no socket.
   This side is the page's editor (vscode-lean4's EditorApi): it relays
   between the page and Lean's server through lsp.h, makes the page's edits
   through the core, and tells the page where the cursor is.  ref/infoview.c
   has none: bridge_open refuses with a message. */
#ifndef BRIDGE_H
#define BRIDGE_H
struct Editor;
/* \w: starts the child if it is not running and opens the page in the
   browser.  0 ok, -1 msg set. */
int  bridge_open(struct Editor *e);
/* After each command (main.c, after lsp_flush): the pages hear where the
   cursor is when it moved in a Lean file.  Nothing while the bridge is off. */
void bridge_update(struct Editor *e);
void bridge_free(void);                  /* stops the child */
#endif
