/* Language servers (package 13, src/lsp/).  Sync as Helix: incremental,
   one didChange per command, versions +1.  What the server says is shown
   by LspSync.tla's "ok" rules, as VS Code shows it: what it says about a
   version the text has since left is mapped through the edits made since,
   shown ranges move with every edit, and a reply is shown only if the text
   has not changed since the request (else dropped; the caller asks
   again).  A file holding bytes that are not UTF-8 is sent with U+FFFD for
   them, one per UTF-16 unit lsp_u16 counts, as the whole text on every
   change; nothing older is mapped across its edits.  One server per
   project root, shared by its files; Lean's is `lake serve` with Lean's
   extensions (file progress, plain goals, widgets, incremental
   diagnostics). */
#ifndef LSP_H
#define LSP_H
#include <stddef.h>
#include "text.h"

typedef struct LspDoc LspDoc;

/* A file type's server: lang is LSP's languageId; argv runs in the
   nearest directory above the file holding one of roots (else the file's
   own); lean adds Lean's options.  The table ends with ext NULL; tests
   point lsp_servers at a fake server. */
typedef struct { const char *ext, *lang; const char *const *argv, *const *roots; int lean; } LspServer;
extern const LspServer *lsp_servers;

/* What the server said, as bytes [a, z) of the current text.  Its ends
   follow their characters through every edit, as Neovim's extmarks: when
   the first character is deleted the start goes to the character after
   the deletion, when the last is, the end goes to the one before it; a
   range left with nothing is dropped (an empty one stays, as a point).
   Diagnostics are sorted by a; sev is 1 error .. 4 hint.  Lean's file
   progress is the ranges still being elaborated. */
typedef struct { size_t a, z; int sev; char *msg; } LspDiag;

/* NULL: no server for path's type, or it could not start. */
LspDoc *lsp_open(const char *path, const Text *t);
void    lsp_close(LspDoc *d);
/* Before every change to the text (the core's edits and undo's UndoHook),
   while t is still the old text, as syntax_edit: at pos, ndel bytes
   become ins. */
void    lsp_edit(LspDoc *d, const Text *t, size_t pos, size_t ndel, const char *ins, size_t nins);
/* After each command: the command's edits go as one didChange. */
void    lsp_flush(LspDoc *d);
void    lsp_saved(LspDoc *d);                     /* didSave */
/* Lean's \r: close and open the file again (as a new version), so it is
   elaborated anew.  After the server died, it starts the server again and
   opens every file it had (DESIGN.md: on restart clear and resend
   didOpen). */
void    lsp_restart(LspDoc *d);

const LspDiag *lsp_diags(const LspDoc *d, int *n);
const LspDiag *lsp_progress(const LspDoc *d, int *n);
/* lualine's lsp_status: the server's name (argv[0]'s basename, a client's
   default name), NULL for d NULL; *work is -1 before any $/progress, else
   the tokens begun and not yet ended (the core draws a spinner or a check
   mark).  No allocation: asked every frame. */
const char    *lsp_name(const LspDoc *d, int *work);

/* Requests about byte pos: hover (K).
   A new one cancels the last of its kind ($/cancelRequest).  lsp_answer is
   the reply's text (Markdown) while the text is unchanged, else NULL. */
enum { LSP_HOVER, LSP_NASK };
void        lsp_ask(LspDoc *d, int kind, size_t pos);
const char *lsp_answer(const LspDoc *d, int kind);

/* When something shown changes (a reply comes in, the server dies) it
   sets core_changed (core.h). */

/* lean.nvim's infoview as text: the goals at the cursor
   (Lean.Widget.getInteractiveGoals over Lean's RPC, with Lean's diff marks
   coloured as lean.nvim does), then the cursor line's messages, in a
   window of its own.  lean.nvim's defaults: it opens when a window shows a
   Lean buffer (autoopen), on the right at a third of the width, or at the
   bottom at a third of the height when the screen is less than 2.5 times
   wider than tall.  It shows what is at the cursor of the last current
   window whose buffer has a Lean LspDoc, so an Org note whose b->lsp is a
   Lean shadow document gets it too.  At most one question to Lean at a
   time.  main.c calls it after each command. */
struct Editor;
void lsp_goals(struct Editor *e);
void lsp_goals_toggle(struct Editor *e);   /* \i: close it, or open it again */

/* The infoview bridge (package 14) relays Lean's JSON as it passes,
   unparsed.  lsp_tap, when set, is given each message a Lean server
   (LspServer.lean) sends that is not the answer to a request (its
   notifications), its answer to initialize (the capabilities), and each
   didChange and didClose sent to it (out 1), whole. */
extern void (*lsp_tap)(int out, const char *msg, size_t n);
/* A message to the server that has uri open: method with params (JSON
   text, n bytes).  fn NULL sends a notification; else a request, whose
   whole answer goes to fn (msg NULL, n 0 if the server died first).  The
   document's edits not yet sent go first (lsp_flush), so the server
   answers about the text as it is.  0 ok, -1 no server has uri open. */
typedef void (*LspRawFn)(void *ctx, const char *msg, size_t n);
int  lsp_send(const char *uri, const char *method, const char *params, size_t n, LspRawFn fn, void *ctx);
const char *lsp_uri(const LspDoc *d);    /* d's file:// URI */
/* The answer to initialize of d's Lean server, whole (n bytes), NULL before
   it came: a bridge opened later has missed it on lsp_tap (PR 43 review). */
const char *lsp_init(const LspDoc *d, size_t *n);

/* The UTF-16 boundary (Lean 4.35 takes no UTF-8 positions): code units in
   the n bytes of s, and the byte where unit c starts (a unit inside a
   character or past the end gives that character's start, or n).  Bytes
   step as uni_len: a well-formed 4-byte sequence is 2 units, everything
   else 1 (an invalid byte is U+FFFD). */
/*@ // bytes in the character at s[i]
    logic integer u16len(unsigned char *s, integer n, integer i) =
      (0xF0 <= s[i] < 0xF8 && i + 4 <= n && 0x80 <= s[i + 1] < 0xC0 &&
       0x80 <= s[i + 2] < 0xC0 && 0x80 <= s[i + 3] < 0xC0) ? 4 :
      (0xE0 <= s[i] < 0xF0 && i + 3 <= n && 0x80 <= s[i + 1] < 0xC0 &&
       0x80 <= s[i + 2] < 0xC0) ? 3 :
      (0xC0 <= s[i] < 0xE0 && i + 2 <= n && 0x80 <= s[i + 1] < 0xC0) ? 2 : 1;
    logic integer u16w(unsigned char *s, integer n, integer i) = u16len(s, n, i) == 4 ? 2 : 1;
    // stepping by characters from byte 0 reaches byte i after u units
    inductive at{L}(unsigned char *s, integer n, integer i, integer u) {
      case start{L}: \forall unsigned char *s, integer n; at(s, n, 0, 0);
      case next{L}: \forall unsigned char *s, integer n, i, u;
        at(s, n, i, u) && i < n ==> at(s, n, i + u16len(s, n, i), u + u16w(s, n, i));
    }
*/
/*@ requires \valid_read(s + (0 .. n - 1));
    assigns \nothing;
    ensures at(s, n, n, \result);
*/
size_t lsp_u16(const unsigned char *s, size_t n);
/*@ requires \valid_read(s + (0 .. n - 1));
    assigns \nothing;
    ensures \result <= n;
    ensures \exists integer u; at(s, n, \result, u) && u <= c &&
      (\result < n ==> c < u + u16w(s, n, \result));
*/
size_t lsp_u16_byte(const unsigned char *s, size_t n, size_t c);
#endif
