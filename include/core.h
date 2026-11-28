/* The editor core: buffers, windows and splits, the cursor, modes,
   scrolling and wrapping, the status and command lines, the completion
   popup, and drawing the grid.  Package 4 (src/core/); ref/core.c is the
   thin editor's version (one buffer in one window: splits and other
   buffers refused, no wrapping, no popup).  The rules copy Neovim 0.12 with
   FEATURES.md's settings (number, relativenumber, scrolloff 6, splitright,
   splitbelow) and Neovim's defaults (hidden, laststatus 2, equalalways,
   display=lastline, sidescroll 1).
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

enum { SCROLLOFF = 6 };   /* FEATURES.md 1; keys' :s c flag scrolls by it too */

enum { MODE_NORMAL, MODE_INSERT, MODE_CMDLINE };
/* state flags kept with undo: the buffer has no lines (Neovim's ML_EMPTY: a
   new or empty file, or after deleting every line; shown as one empty line) */
enum { CORE_EMPTY = 1 };
#define WANT_EOL ((size_t)-1)      /* curswant after $ */

struct Win;
struct Frame;                      /* the layout tree, the core's own */

/* Where a window last had its cursor in a buffer, and its 'wrap' there
   (Neovim's WinInfo): entering the buffer again goes back there, using
   this window's entry or else the most recent.  win is NULL once the
   window is closed. */
typedef struct WinInfo {
	struct Win *win;
	size_t line, col;
	int wrap;
	struct WinInfo *next;      /* most recent first */
} WinInfo;

/* A position that follows the text: core_edit, core_delete_lines and
   core_undo move it as Neovim's mark_adjust() does, and core_mark_join as
   do_join()'s mark_col_adjust().  On deleted lines a-z . ^ " are unset; the
   change list, jump list, < and > go to the first deleted line.  [ ] never
   move.  Keys set and read them. */
typedef struct { size_t line, col; int set; } Mark;
/* a-z are mark[0..25], then the special marks */
enum { MARK_LBRACKET = 26, MARK_RBRACKET, MARK_LT, MARK_GT, MARK_DOT, MARK_CARET, MARK_QUOTE, MARK_N };
enum { JUMPLIST_SIZE = 100 };      /* Neovim's, for the jump and change lists */

typedef struct Buf {
	Text *text;
	Undo *undo;
	char *path;                /* NULL for a buffer without a name */
	const char *status;        /* shown in place of the file name for a buffer without a path; NULL:
	                              [No Name].  Owned by whoever sets it */
	char *full;                /* Neovim's b_ffname: path through file_full() (the path
	                              as typed when that fails); buffers are found by it
	                              (buflist_findname()); set and freed with path */
	FileId id;
	Journal *journal;          /* crash journal; NULL if it could not be opened (msg said why) */
	int watch;                 /* file_watch fd for the loop, -1 if none */
	int changed;               /* changed on disk since read or written: 1, then 2 once warned */
	int jerr;                  /* a journal write failed during this command */
	int noeol;                 /* file had no final newline (or was empty) */
	int dos;                   /* 'fileformat' dos: saved with CR LF (file_eol) */
	unsigned flags;            /* CORE_EMPTY */
	unsigned long saved_seq;   /* undo_seq at the last load or save */
	int nr;                    /* buffer number, from 1 (:b N) */
	long lastused;             /* seconds when a window last entered it (b_last_used), for the buffers picker */
	WinInfo *wi;
	struct Syntax *syn;        /* syntax.h; NULL: no grammar for this file */
	/* one FACE_* per byte of [a, z), drawn instead of syntax_faces (the
	   goals window, lsp_faces for a buffer with a server); NULL: syntax_faces */
	void (*faces)(const struct Buf *b, size_t a, size_t z, unsigned char *face);
	int nonumber;              /* setlocal nonumber norelativenumber (neogit's buffers) */
	int nomodifiable;          /* 'nomodifiable' (the quickfix buffer): the core's
	                              edits refuse with E21; text_insert still writes */
	struct LspDoc *lsp;        /* lsp.h; NULL: no language server for this file */
	Mark mark[MARK_N];
	Mark change[JUMPLIST_SIZE];   /* the change list (g; g,), oldest first */
	int nchange;
	struct Buf *next;          /* by number */
} Buf;

/* A window shows a buffer.  The cursor is per window; top, left and
   skipcol say what is in view (Neovim's w_topline, w_leftcol, w_skipcol).
   y, x, h and w are its text area on the screen, without the status line
   below it or the separator to its right. */
typedef struct Win {
	Buf *buf;
	Buf *alt;                  /* the alternate buffer (#, CTRL-^), or NULL */
	size_t line, col;          /* cursor: line, byte in line */
	size_t want;               /* wanted screen column for j and k */
	/* the cursor's cached screen column (w_virtcol with VALID_VIRTCOL):
	   kept while the cursor stays at line, col in the same mode and no
	   change invalidates it, even if the text under it changed (an undo,
	   see core_undo) */
	struct { size_t line, col, v; int mode, ok; } vc;
	size_t top, left, skipcol;
	size_t scroll;             /* lines for CTRL-D and CTRL-U ('scroll'); 0 = half the height */
	int wrap;                  /* 'wrap': off (FEATURES.md); set through core_set_wrap */
	int lbr;                   /* 'linebreak' and 'breakindent': core_float's window only */
	int conceal;               /* 'conceallevel': 2, or 0 (SPC u p, Davy's
	                              org-toggle-pretty-entities): the core asks
	                              syntax_conceal only when it is not 0 */
	int y, x, h, w;
	struct Frame *frame;       /* its leaf in the layout */
	/* CTRL-O CTRL-I, oldest first.  :bd forgets the buffer's entries in the
	   current window only (do_buffer_ext); other windows may keep a buffer
	   :bd deleted, which stays allocated (unlisted) */
	struct { Buf *buf; Mark m; } jump[JUMPLIST_SIZE];
	int njump, jumpidx;
	Mark pcmark;                /* the '' and `` mark (w_pcmark) */
	int changeidx;             /* where g; and g, are in buf's change list (w_changelistidx) */
	struct Win *next;          /* window order: top left to bottom right (CTRL-W w) */
} Win;

/* A tab page (Neovim's tabpage_T).  The window backends show each in its own
   OS window (Emacs's frames, SPC 5); the terminal shows the current one, with
   Neovim's tabline.  As Neovim keeps curwin and firstwin for the current tab
   page, the current one's windows are in Editor (win, prev, wins, layout); a
   tab page that is not current keeps them here (leave_tabpage(),
   enter_tabpage()). */
typedef struct Tab {
	Win *win, *prev, *wins;
	struct Frame *layout;
	int rows, cols;            /* its OS window's size in cells */
	int id;                    /* the backend's handle for its OS window, from 0, never reused */
	char key;                  /* SPC 5 {key} enters it: the first free letter but SPC 5's f, m, n
	                              and o when made (0 past 22 tab pages); SPC 5 n {letter} changes it */
	struct Tab *next;          /* tab page order (gt) */
} Tab;

/* One line of the completion popup (Neovim's complete-items); kind and
   menu may be NULL. */
typedef struct { const char *word, *kind, *menu; } PumItem;

typedef struct Editor {
	Buf *buf;                  /* the current window's buffer (curbuf) */
	Win *win;                  /* the current window (curwin) */
	Win *prev;                 /* the previous window (CTRL-W p), or NULL */
	Buf *bufs;                 /* every buffer, by number */
	Win *wins;                 /* every window, in window order */
	struct Frame *layout;
	Tab *tab;                  /* the current tab page (curtab) */
	Tab *tabs;                 /* every tab page, in order (first_tabpage) */
	/* the terminal: one screen for every tab page, which shows Neovim's
	   tabline ('showtabline' 1) and takes the screen's size on entering
	   (enter_tabpage()); 0 for the window backends, where each tab page is
	   its own OS window of its own size */
	int tabline;
	int rows, cols;            /* screen size: 24 x 80 (Neovim's default) until a draw sets it */
	int mode;
	KeyState ks;
	int ai;                    /* autoindent just inserted, nothing typed after it */
	int cmdtype;               /* ':', '/' or '?' */
	char cmd[256];
	size_t cmdlen, cmdpos;     /* command line bytes, the cursor in them */
	char msg[256];
	int quit;
	const PumItem *pum;        /* completion popup items (the caller's), or NULL */
	int npum, pumsel;          /* how many, which is selected (-1 none) */
	size_t pumcol;             /* byte in the cursor line where the completed word starts */
	Win *pop;                  /* the float core_float shows, or NULL */
	unsigned char *lb;         /* scratch copy of one line */
	size_t lbcap;
	/* the system clipboard, the backend's (backend.h); NULL: none, as
	   Neovim without a clipboard provider (tests, the conform driver) */
	int  (*clip_set)(const char *s, size_t n);
	long (*clip_get)(char *buf, size_t max);
} Editor;

/* Set by any package when what is shown changed between keys (a picker's
   list, a git refresh, a language server's reply): main.c redraws after the
   loop and clears it.  One flag for every package, defined in the core. */
extern int core_changed;

/* The first buffer, on path, in the first window.  0 ok, -1 error (msg set). */
int  core_open(Editor *e, const char *path);
void core_free(Editor *e);

/* The current buffer (e->buf) and the cursor of the current window (e->win). */
size_t core_lines(const Editor *e);
size_t core_line_start(const Editor *e, size_t l);
size_t core_line_len(const Editor *e, size_t l);    /* bytes, without '\n' */
/* Line l's bytes, valid until the next call. */
const unsigned char *core_line(Editor *e, size_t l, size_t *len);
/* Replace ndel bytes at pos by ins, recording it for undo.  Other windows
   on the buffer keep their cursor and top line on the same text (Neovim's
   mark_adjust()).  0 ok, -1 out of memory. */
int  core_edit(Editor *e, size_t pos, size_t ndel, const char *ins, size_t nins);
/* Delete lines [a, z); deleting all leaves the empty buffer.  0 ok, -1 out of memory. */
int  core_delete_lines(Editor *e, size_t a, size_t z);
/* Neovim's u_save(line - 1, line): an undo step starts at line (0-based)
   even if nothing changes, saving no text.  A new group saves a-z, < and >
   as they are now, for undo to bring back (u_savecommon()).  Keys call it
   for u_save_cursor() and u_save_gap(). */
void core_u_save(Editor *e, size_t line);
/* A join (J, gJ, Backspace or Delete over a line break, a charwise delete
   across lines) about to be made: marks at or after column mincol of line
   move by line_amount lines and col_amount columns, as Neovim's
   mark_col_adjust(), with spaces_removed as do_join() passes it.  Keys call
   it before the core_edit that joins, with the line numbers before it, as
   do_join() calls mark_col_adjust() before del_lines(), after core_u_save;
   line_amount < 0 also marks the step a join, so undo and redo count the
   joined line as changed (do_join's u_save) when placing the cursor;
   line_amount 1 after the core_edit that splits a line moves the marks past
   mincol to the new line, as open_line()'s OPENLINE_MARKFIX does (thread
   0's, agreed). */
void core_mark_join(Editor *e, size_t line, size_t mincol, long line_amount, long col_amount, int spaces_removed);
/* Neovim's setpcmark(): the cursor becomes the '' mark and the newest jump,
   the oldest falling off a full list; keys call it before a jump (G, gg,
   searches), the core before its buffer commands. */
void core_setpcmark(Editor *e);
/* One undo or redo step, placing the cursor as u_undoredo() does; the caller
   recomputes the wanted column once its count is done, even on failure.
   1 if the step invalidated Neovim's cached cursor column, 0 if not, -1 if
   there was nothing to undo or redo. */
int  core_undo(Editor *e, int redo);
int  core_modified(const Buf *b);
/* Writes buffer b (:w on e->buf, or each of :wa).  0 written, -1 not (msg set). */
int  core_write(Editor *e, Buf *b, int force);
/* A buffer's watch fd is readable: an outside change is noted, and warned
   at the buffer's next edit (W11). */
void core_watch(Editor *e, int fd);
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
/* Scroll the current window so the cursor is in view with scrolloff
   (update_topline() and curs_columns()), and outside Insert and
   Command-line mode look at its view again as win_update() does when it is
   drawn.  keys_feed calls it after every key, where Neovim's normal_check()
   and edit() call update_topline() (thread 0's, agreed). */
void core_update(Editor *e);
/* The cursor to line, byte col (0-based; past the end clamped to the last
   line and its end, as check_cursor()), the wanted column set from it, then
   core_update: the one way a picker, git, the infoview or :make jump to a
   position. */
void core_goto(Editor *e, size_t line, size_t col);

/* Buffers.  :e path, with force (:e!) dropping changes; path NULL is :e
   (load the file again) and "" is :enew.  The window's cursor goes back
   where it last was in that buffer.  0 ok, -1 not (msg set). */
int  core_edit_file(Editor *e, const char *path, int force);
int  core_buf_goto(Editor *e, int nr);        /* :b N */
/* b current without a jump (buflist_getfile() without GETF_SETMARK): CTRL-O
   and CTRL-I to another buffer's entry.  0 ok, -1 not (msg set). */
int  core_buf_enter(Editor *e, Buf *b);
Buf *core_buf_find(Editor *e, int nr);        /* listed buffer nr, NULL if none */
int  core_buf_next(Editor *e, long n);        /* :bn ]b (n > 0), :bp [b (n < 0) */
/* CTRL-^: the alternate buffer, E23 if none.  Not core_buf_goto: a buffer
   never shown in this window opens on the first non-blank (buflist_getfile,
   then do_ecmd's beginline(BL_WHITE)), where :b puts the cursor at 1,0. */
int  core_buf_alt(Editor *e);
int  core_buf_delete(Editor *e, int nr, int force);   /* :bd */
/* A buffer nobody edits (git status, Lean goals): its text becomes s[0, n)
   with no undo, and every window on it gets line (SIZE_MAX: its own line),
   clamped to the text, at column 0, with top no further down. */
void core_buf_fill(Editor *e, Buf *b, const char *s, size_t n, size_t line);
/* s[0, n) at the end of b, with no undo; a window on b whose cursor was
   on its last line follows to the new last line, column 0 (Emacs's
   compilation-scroll-output: :make's buffer, SQL's results). */
void core_buf_append(Editor *e, Buf *b, const char *s, size_t n);
/* The first window showing buffer nr, as its number in window order (the
   count of core_win_goto(e, 'w', count)), 0 if none: Neovim's bufwinnr(). */
long core_buf_shown(const Editor *e, int nr);
/* The Visual area's bytes [*a, *z), as Neovim's getregion(): with v from
   one end to the other, the last character whole; with V and CTRL-V the
   whole lines with the last one's break (none after the text's end). */
void core_visual(Editor *e, size_t *a, size_t *z);

/* Windows.  :sp and CTRL-W s (vertical 0), :vs and CTRL-W v (1); size 0
   leaves it to equalalways.  The new window shows the same buffer and
   becomes current.  0 ok, -1 not (E36, msg set). */
int  core_split(Editor *e, int vertical, int size);
/* :new (vertical 0) and :vnew (1): a split on a new empty buffer, in one
   step, so the new window records no position in the buffer it split from
   (do_ecmd runs with oldwin NULL). */
int  core_new(Editor *e, int vertical);
/* A new tab page after the current one, made current (win_new_tabpage()):
   path NULL copies the current window into it (SPC 5 2, Emacs's make-frame,
   Neovim's :tab split); "" is :tabnew's empty buffer; a name is SPC 5 f and
   :tabnew {file} (then do_ecmd()).  0 ok, -1 error (msg set). */
int  core_tabnew(Editor *e, const char *path);
/* SPC 5 0 and :tabclose (t NULL: the current one); the OS window's close
   button (KEY_CLOSE) names its t.  The last tab page is not closed: E784,
   as ex_tabclose() (the close button still quits, keys' doing).  0 ok, -1
   error (msg set). */
int  core_tabclose(Editor *e, Tab *t);
/* SPC 5 1 and :tabonly. */
int  core_tabonly(Editor *e);
/* Makes t current (goto_tabpage_tp()): SPC 5 o, gt, {n}gt, and the OS
   focusing another window (Emacs's handle-switch-frame). */
void core_tab_enter(Editor *e, Tab *t);
/* CTRL-W T (n 0): the current window into a new tab page (do_window() 'T').
   SPC 5 m {n}: into tab page n instead (DESIGN.md "New approaches" row, with
   the frames package).  0 ok, -1 error (msg set); a window alone in its
   tab page stays, with "Already only one window" and 0, as do_window()
   'T'. */
int  core_win_to_tab(Editor *e, int n);
/* :q, :close, CTRL-W c and q: close the current window.  Closing the last
   one quits (quit set), unless a buffer is modified and !force (E37 or
   E162).  0 ok, -1 not (msg set). */
int  core_close(Editor *e, int force);
int  core_only(Editor *e);                    /* :only, CTRL-W o */
/* CTRL-W h j k l (count windows away), w W (count: to window count),
   t b p.  0 ok, -1 no such window. */
int  core_win_goto(Editor *e, int how, long count);
/* CTRL-W x (count: with window count), r R (rotate count times),
   H J K L (to the far edge, full size).  0 ok, -1 not (msg set). */
int  core_win_move(Editor *e, int how, long count);
/* CTRL-W + - (vertical 0) and > < (1) by n; with set, CTRL-W _ and | to
   size n (0 = as large as possible). */
void core_resize(Editor *e, int vertical, long n, int set);
void core_equal(Editor *e);                   /* CTRL-W = */
/* :set wrap (1), :set nowrap (0): even an unchanged value redoes the view
   (changed_window_setting()) and curswant, unless the cursor is after $. */
void core_set_wrap(Editor *e, int on);

/* Scrolling the current window, as Neovim's move.c; the cursor stays in
   view.  CTRL-E (n > 0) and CTRL-Y (n < 0): n lines; CTRL-D (dir 1) and
   CTRL-U (-1): a count sets win->scroll; CTRL-F (1) and CTRL-B (-1): count
   pages; zt zz zb: how 't' 'z' 'b'.  The half and page scrolls return 0,
   or -1 when they could not move (Neovim beeps, which ends a macro or
   mapping); CTRL-E and CTRL-Y never beep (measured, PR 11's review). */
void core_scroll(Editor *e, long n);
int  core_scroll_half(Editor *e, int dir, long count);
int  core_scroll_page(Editor *e, int dir, long count);
void core_scroll_cursor(Editor *e, int how);
/* The line H, M or L goes to (how 'H' 'M' 'L'; count lines from the top or
   bottom).  so: apply scrolloff (cursor_correct()); nv_scroll() passes 0
   while an operator is pending, so d L reaches the last line shown. */
size_t core_screen_line(Editor *e, int how, long count, int so);
/* gj and gk: n screen lines down (n < 0 up), keeping the wanted column.
   0 ok, -1 at the first or last screen line. */
int  core_screen_move(Editor *e, long n);

/* The completion popup (pum_display()): n items, item sel selected (-1
   none), the completed word starting at byte col of the cursor line.  The
   items stay the caller's until the next call; n 0 hides the popup. */
void core_pum(Editor *e, const PumItem *items, int n, int sel, size_t col);

/* Neovim's LSP float (vim.lsp.util.open_floating_preview(), focusable
   false): the lines of s[0, n), Markdown when md (drawn as a Markdown
   buffer is) or plain text, in a window next to the cursor, replacing the
   last one.  It closes when the cursor moves or leaves its window
   (close_events CursorMoved, CursorMovedI, BufLeave) or s is NULL. */
void core_float(Editor *e, const char *s, size_t n, int md);

/* Draw tab page t's windows (NULL: the current one's) with their status
   lines, the command line and the popup into g; the layout takes g's size.
   Allocates nothing. */
void core_draw(Editor *e, Tab *t, Grid *g);
#endif
