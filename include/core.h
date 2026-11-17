/* The editor core: one buffer in one window, the cursor, modes, the status
   and command lines, and drawing the grid.  Package 4 (src/core/) adds
   buffers, windows and splits, wrapping and the completion popup.
   The text always ends in '\n' (every line is terminated); a file without a
   final newline is shown the same and saved without it (noeol), so a file
   survives a save byte for byte. */
#ifndef CORE_H
#define CORE_H
#include <stddef.h>
#include "text.h"
#include "undo.h"
#include "file.h"
#include "grid.h"
#include "keys.h"

enum { MODE_NORMAL, MODE_INSERT, MODE_CMDLINE };
/* state flags kept with undo: the buffer has no lines (Neovim's ML_EMPTY: a
   new or empty file, or after deleting every line; shown as one empty line) */
enum { CORE_EMPTY = 1 };
#define WANT_EOL ((size_t)-1)      /* curswant after $ */

typedef struct Editor {
	Text *text;
	Undo *undo;
	char *path;
	FileId id;
	int noeol;                 /* file had no final newline (or was empty) */
	unsigned flags;            /* CORE_EMPTY */
	unsigned long saved_seq;   /* undo_seq at the last load or save */
	size_t line, col;          /* cursor: line, byte in line */
	size_t want;               /* wanted screen column for j and k */
	size_t top, left;          /* first line and screen column shown */
	int mode;
	KeyState ks;
	int ai;                    /* autoindent just inserted, nothing typed after it */
	char cmd[256];
	size_t cmdlen;
	char msg[256];
	int quit;
	unsigned char *lb;         /* scratch copy of one line */
	struct Syntax *syn;        /* syntax.h; NULL: no grammar for this file */
	size_t lbcap;
} Editor;

int  core_open(Editor *e, const char *path);  /* 0 ok, -1 error (msg set) */
void core_free(Editor *e);
size_t core_lines(const Editor *e);
size_t core_line_start(const Editor *e, size_t l);
size_t core_line_len(const Editor *e, size_t l);    /* bytes, without '\n' */
/* Line l's bytes, valid until the next call. */
const unsigned char *core_line(Editor *e, size_t l, size_t *len);
/* Replace ndel bytes at pos by ins, recording it for undo.  0 ok, -1 out of memory. */
int  core_edit(Editor *e, size_t pos, size_t ndel, const char *ins, size_t nins);
/* Delete lines [a, z); deleting all leaves the empty buffer.  0 ok, -1 out of memory. */
int  core_delete_lines(Editor *e, size_t a, size_t z);
/* One undo or redo step, placing the cursor as u_undoredo() does; the caller
   recomputes the wanted column once its count is done, even on failure.
   1 if the step invalidated Neovim's cached cursor column, 0 if not, -1 if
   there was nothing to undo or redo. */
int  core_undo(Editor *e, int redo);
int  core_modified(const Editor *e);
int  core_write(Editor *e, int force);        /* 0 written, -1 not (msg set) */
void core_msg(Editor *e, const char *fmt, ...);
/* Screen column where byte col of line l starts. */
size_t core_vcol(Editor *e, size_t l, size_t col);
/* Byte column in line l under screen column v (last char if past the end;
   the end of line if past_end). */
size_t core_col_at(Editor *e, size_t l, size_t v, int past_end);
size_t core_first_nonblank(Editor *e, size_t l);
void core_clamp(Editor *e);                   /* keep the cursor on a character in normal mode */
/* want = the cursor's screen column; on a tab in normal mode the tab's last
   cell, where Vim shows the cursor */
void core_set_want(Editor *e);
void core_draw(Editor *e, Grid *g);
#endif
