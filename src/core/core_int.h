/* What the core's files share.  Rules copy Neovim 0.12; each comment names
   the function it comes from. */
#ifndef CORE_INT_H
#define CORE_INT_H
#include "core.h"

enum { TABSTOP = 8, MAXH = 1 << 30 };   /* Vim's tabstop */
enum { FR_LEAF, FR_ROW, FR_COL };
enum { ROWS, COLS };                  /* an axis: Frame.size[ROWS] is the height */

/* What Neovim keeps in win_T beyond what core.h shows. */
typedef struct {
	int fraction;                 /* w_fraction: the cursor row in 1/16384 of the height */
	int prev_fraction_row;        /* w_prev_fraction_row */
	int wrow, wcol;               /* the cursor on the window (w_wrow, w_wcol) */
	int crow, cheight;            /* the cursor line's first row and rows (w_cline_row, w_cline_height) */
	/* w_valid's VALID_TOPLINE and w_valid_cursor.lnum (check_cursor_moved()):
	   update_topline() keeps the view while the cursor stays on its line,
	   so a scroll command's view lasts; wrap as last seen, whose change
	   invalidates as changed_window_setting() does */
	int topvalid, wrap;
	size_t vline, vskip;          /* w_valid_cursor.lnum, w_valid_skipcol */
	/* w_botline, w_empty_rows and w_valid's VALID_BOTLINE (bv 1: exact)
	   and VALID_BOTLINE_AP (bv 2: approximate but used): Neovim's
	   update_topline() reads an approximate w_botline, and win_update()
	   looks at the view again when the drawn one differs */
	size_t bot;
	int empty, bv;
	int allwrap;                  /* w_allbuf_opt's 'wrap': what :set gave the window */
	/* w_valid's VALID_WCOL for the sideways scroll: the cursor, leftcol
	   and width it was done for (check_cursor_moved(), win_new_width()) */
	int sideok, sw;
	size_t sline, scol, sleft;
} WinState;

enum { BOT_EXACT = 1, BOT_AP = 2 };   /* WinState.bv */

/* Neovim's frame_T.  A leaf holds one window and that window's WinState. */
struct Frame {
	int layout;
	int size[2];                  /* with the status line and the separator */
	struct Frame *parent, *child, *next, *prev;
	Win *win;
	int vsep;                     /* leaf: w_vsep_width */
	int top;                      /* the top frame: its first row (firstwin's w_winrow), below the tabline */
	WinState s;
};

/* Cells the cluster at s[i] takes at screen column v of a line in window
   w, as win_charsize(): a tab to the next stop, a control character ^X,
   else uni_width(), one more for a wide one at a wrapping row's end. */
int  core_cellw(const Win *w, const unsigned char *s, size_t n, size_t i, size_t v);

/* A line of the buffer read in pieces, so drawing needs no allocation. */
typedef struct {
	const Text *t;
	size_t pos, end;              /* next byte to read, end of line */
	unsigned char b[512];
	size_t i, n;                  /* position and fill of b */
} LineIt;
void lit_init(LineIt *it, const Buf *b, size_t l);
/* The next grapheme cluster, the unit Neovim 0.12 draws and moves by: its bytes in *s (valid until the next call), their
   count returned; 0 at the end of the line. */
size_t lit_next(LineIt *it, const unsigned char **s);

/* core_cellw() of the cluster s[0, k) at screen column v of the line it
   reads (just past s), with w->lbr's 'linebreak' and 'breakindent'
   (charsize_regular()): *head of the cells are breakindent's, before the
   character; a blank where the next word does not fit fills the row.
   b keeps what it needs of the line so far, { 0, 1, 0 } at its start */
typedef struct { int ind, white, seen; } Lbr;
int  lbr_cellw(const Win *w, Lbr *b, const LineIt *it, const unsigned char *s, size_t k, size_t v, int *head);

size_t buf_lines(const Buf *b);
/* where the current window's cursor was when core_float opened e->pop:
   it closes once that changes (draw.c) */
extern struct PopAt { const Win *w; size_t line, col; } core_popat;
size_t win_vcol_end(const Win *w, size_t l);  /* screen width of line l in w (linetabsize) */
/* The cursor's screen column in window w (getvvcol(): on a tab in normal
   mode, its last cell) and the start and end of the character. */
size_t win_vcol(Editor *e, Win *w, size_t *start, size_t *end);
/* getvvcol() of byte col of line l in w: the character's first and last
   cell (one past the end of the line: one cell); 1 on a tab */
int win_vcols(const Win *w, size_t l, size_t col, size_t *start, size_t *end);

/* window geometry (move.c) */
int  win_numw(const Win *w);          /* sign and number columns, with its space: win_col_off() */
int  win_signw(const Win *w);         /* the sign column: 0 or 2 */
int  win_plines(const Win *w, size_t l);   /* plines_win_nofill(.., true) */
size_t win_botline(Editor *e, Win *w, int *empty_rows);
void win_update(Editor *e, Win *w);   /* update_topline(), curs_columns() */
void win_drawn(Editor *e);            /* after drawing: w_botline as drawn */
void win_ecmd_topline(Editor *e, Win *w);
void win_check_cursor(Win *w);        /* check_cursor(): lnum and col in the buffer */
void win_set_fraction(Win *w);
void win_new_height(Editor *e, Win *w, int h);
void win_new_width(Editor *e, Win *w, int wd);
void win_cursor_pos(Editor *e, Win *w, int *wrow, int *wcol);
void scroll_halfway(Editor *e, Win *w, int atend, int prefer_above);
void win_invalidate(Win *w);          /* changed_line_abv_curs_win(): update_topline() looks again */

/* layout (win.c) */
void layout_init(Editor *e, Win *w, int ch);
int  tabline_height(const Editor *e);
void layout_resize(Editor *e, int rows, int cols);
void layout_free(struct Frame *f);
/* make w current (win_enter()) */
void win_enter(Editor *e, Win *w);
void win_close(Editor *e, Win *w);    /* w is not the last window */
/* Tab pages (win.c).  tab_use() switches without entering (leave_tabpage(),
   use_tabpage()); tab_first() and tab_free_all() for core_open and core_free. */
Win *tab_firstwin(const Editor *e, const Tab *t);
void tab_use(Editor *e, Tab *t);
int  tab_first(Editor *e);
void tab_free_all(Editor *e);
void close_othertabs(Editor *e, Buf *b);
/* FOR_ALL_TAB_WINDOWS() */
#define FOR_ALL_TAB_WINDOWS(e, w) \
	for (Tab *tp_ = (e)->tabs; tp_; tp_ = tp_->next) \
		for (Win *w = tab_firstwin(e, tp_); w; w = w->next)
/* do_ecmd() after a split, with no oldwin (:new, :tabnew) */
int  edit_split(Editor *e, const char *path);
Buf *buf_unlisted(void);                 /* the buffers :bd left, by number */
void core_wininfo_set(Buf *b, Win *w, int close);   /* buflist_setfpos() */
#endif
