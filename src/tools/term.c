/* The terminal: a shell on a pseudo-terminal (loop.h's proc_pty) whose
   screen libghostty-vt keeps, in a float over the windows as my-nvim's
   snacks.terminal opens it (C-/ there; Ctrl+. here, Davy's choice,
   2026-11-20).  Ctrl+. hides it and the shell lives on (snacks' toggle);
   the shell's exit closes it, as Neovim's :terminal closes with its job.
   The cells are read from libghostty's render state at each frame; their
   colours become faces in the slots grid.h leaves free past FACE_COUNT. */
#include <ghostty/vt.h>
#include <stdlib.h>
#include <string.h>
#include "keys.h"
#include "loop.h"
#include "term.h"
#include "uni.h"

enum { SLOT0 = FACE_COUNT, SLOTS = FACES };

static struct {
	GhosttyTerminal t;
	GhosttyRenderState rs;
	GhosttyRenderStateRowIterator it;
	GhosttyRenderStateRowCells cells;
	int id;                   /* the shell's process; -1 none */
	int open;
	int rows, cols;           /* the terminal's size */
	unsigned frame;           /* term_draw's count */
	unsigned used[SLOTS];     /* the frame a slot was last drawn in */
	int nslots;               /* slots from SLOT0 taken so far */
} T = { .id = -1 };

int tools_term(void) { return T.open; }

/* Catppuccin Mocha's terminal colours (catppuccin/ghostty), so a shell
   looks as in Davy's Ghostty; colours 16 to 255 stay xterm's */
static const GhosttyColorRgb ansi[16] = {
	{ 69, 71, 90 }, { 243, 139, 168 }, { 166, 227, 161 }, { 249, 226, 175 },
	{ 137, 180, 250 }, { 245, 194, 231 }, { 148, 226, 213 }, { 186, 194, 222 },
	{ 88, 91, 112 }, { 243, 139, 168 }, { 166, 227, 161 }, { 249, 226, 175 },
	{ 137, 180, 250 }, { 245, 194, 231 }, { 148, 226, 213 }, { 166, 173, 200 },
};

/* snacks' float: 90% of the screen above the status line, centred */
static Box box(const Editor *e)
{
	int h = e->rows * 9 / 10, w = e->cols * 9 / 10;
	return (Box){ (e->rows - 1 - h) / 2, (e->cols - w) / 2, h, w };
}

static void on_write_pty(GhosttyTerminal t, void *ud, const uint8_t *b, size_t n)
{
	(void)t; (void)ud;
	proc_write(T.id, b, n);
}

static void on_output(void *ctx, int stream, const char *b, size_t n)
{
	(void)ctx;
	if (stream == 1 && n) ghostty_terminal_vt_write(T.t, (const uint8_t *)b, n);
	if (stream == 0) {
		ghostty_terminal_free(T.t);
		T.t = NULL;
		T.id = -1;
		T.open = 0;
	}
	if (T.open || !stream) core_changed = 1;
}

/* Starts the shell: 'shell' as Neovim sets it, $SHELL or sh; cmd.exe on
   Windows.  0 ok. */
static int start(Box b)
{
#ifdef _WIN32
	const char *sh = getenv("COMSPEC");
	char *argv[] = { (char *)(sh ? sh : "cmd.exe"), NULL };
#else
	const char *sh = getenv("SHELL");
	char *argv[] = { (char *)(sh && *sh ? sh : "sh"), NULL };
#endif
	GhosttyColorRgb pal[256];
	T.rows = b.h - 2; T.cols = b.w - 2;
	if (T.rows < 1 || T.cols < 1) return -1;
	if ((!T.rs && (ghostty_render_state_new(NULL, &T.rs) || ghostty_render_state_row_iterator_new(NULL, &T.it)
	               || ghostty_render_state_row_cells_new(NULL, &T.cells)))
	    || ghostty_terminal_new(NULL, &T.t, (uint16_t)T.cols, (uint16_t)T.rows)) return -1;
	ghostty_terminal_get(T.t, GHOSTTY_TERMINAL_DATA_COLOR_PALETTE_DEFAULT, pal);
	memcpy(pal, ansi, sizeof ansi);
	ghostty_terminal_set(T.t, GHOSTTY_TERMINAL_OPT_COLOR_PALETTE, pal);
	ghostty_terminal_set(T.t, GHOSTTY_TERMINAL_OPT_COLOR_FOREGROUND, &(GhosttyColorRgb){ 205, 214, 244 });
	ghostty_terminal_set(T.t, GHOSTTY_TERMINAL_OPT_COLOR_BACKGROUND, &(GhosttyColorRgb){ 30, 30, 46 });
	ghostty_terminal_set(T.t, GHOSTTY_TERMINAL_OPT_WRITE_PTY, (const void *)on_write_pty);
	if ((T.id = proc_pty(argv, NULL, T.rows, T.cols, on_output, NULL)) >= 0) return 0;
	ghostty_terminal_free(T.t);
	T.t = NULL;
	return -1;
}

/* What a key sends: xterm's, cursor keys as DECCKM says (Neovim's
   terminal_send_key through libvterm). */
static void send(int k)
{
	char u[8];
	int n;
	if (k >= KEY_UP && k <= KEY_RIGHT) {
		GhosttyTerminalModeConfig m = { .mode = GHOSTTY_MODE_CURSOR_KEYS };
		ghostty_terminal_get(T.t, GHOSTTY_TERMINAL_DATA_MODE, &m);
		u[0] = 27; u[1] = m.value ? 'O' : '['; u[2] = k == KEY_UP ? 'A' : k == KEY_DOWN ? 'B' : k == KEY_RIGHT ? 'C' : 'D';
		n = 3;
	} else if (k == KEY_DEL) { memcpy(u, "\x1b[3~", 4); n = 4; }
	else if (k < 0x110000) n = uni_put((unsigned char *)u, (unsigned)k);
	else return;
	proc_write(T.id, u, (size_t)n);
	ghostty_terminal_scroll_viewport(T.t, (GhosttyTerminalScrollViewport){ .tag = GHOSTTY_TERMINAL_SCROLL_VIEWPORT_BOTTOM });
}

int term_key(Editor *e, int k)
{
	if (k == KEY_CTRL_DOT) {
		T.open = !T.open && (T.t || !start(box(e)));
		return 1;
	}
	if (!T.open || k == KEY_RESIZE) return 0;
	if (k == KEY_WHEEL_UP || k == KEY_WHEEL_DOWN)
		ghostty_terminal_scroll_viewport(T.t, (GhosttyTerminalScrollViewport){ .tag = GHOSTTY_TERMINAL_SCROLL_VIEWPORT_DELTA,
		                                                                      .value = { .delta = k == KEY_WHEEL_UP ? -3 : 3 } });
	else if (k != KEY_MOUSE_LEFT && k != KEY_CLOSE) send(k);
	return k != KEY_CLOSE;
}

/* The face of a cell's colours and style: a slot already holding it, else
   a free one, else one no cell used in this frame or the last, which the
   shown grid therefore does not hold, so reusing it cannot leave a cell
   in the old colours (Neovim's attr table gets cleared and the screen
   redrawn instead).  FACE_NONE when every slot is in use. */
static int face_of(const Face *f)
{
	int k = SLOT0, old = 0;
	for (; k < SLOT0 + T.nslots && memcmp(&faces[k], f, sizeof *f); k++)
		if (!old && T.used[k] + 1 < T.frame) old = k;
	if (k == SLOT0 + T.nslots) {
		if (k < SLOTS) T.nslots++;
		else if (old) k = old;
		else return FACE_NONE;
		faces[k] = *f;
	}
	T.used[k] = T.frame;
	return k;
}

static void rgb(unsigned char out[3], GhosttyColorRgb c) { out[0] = c.r; out[1] = c.g; out[2] = c.b; }

/* One cell of the render state's current row at (r, c) of the grid; the
   cells it took. */
static int cell(Grid *g, int r, int c, const GhosttyRenderStateColors *col)
{
	unsigned char s[GRID_CLUSTER_MAX];
	GhosttyBuffer u = { .ptr = s, .cap = sizeof s };
	GhosttyCell raw;
	GhosttyCellWide wide = GHOSTTY_CELL_WIDE_NARROW;
	GhosttyStyle st;
	GhosttyColorRgb fg, bg;
	ghostty_render_state_row_cells_get(T.cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_RAW, &raw);
	ghostty_cell_get(raw, GHOSTTY_CELL_DATA_WIDE, &wide);
	if (wide == GHOSTTY_CELL_WIDE_SPACER_TAIL) return 0;   /* drawn with its head */
	ghostty_render_state_row_cells_get(T.cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &st);
	int hasfg = !ghostty_render_state_row_cells_get(T.cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR, &fg);
	int hasbg = !ghostty_render_state_row_cells_get(T.cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR, &bg);
	if (ghostty_render_state_row_cells_get(T.cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_UTF8, &u) || !u.len) s[0] = ' ', u.len = 1;
	int w = wide == GHOSTTY_CELL_WIDE_WIDE ? 2 : 1, n = grid_put(g, r, c, s, u.len, w, st.flags & GHOSTTY_STYLE_FLAG_FAINT ? ATTR_DIM : ATTR_NONE);
	if (st.flags & GHOSTTY_STYLE_FLAG_INVERSE) {
		GhosttyColorRgb t = hasfg ? fg : col->foreground;
		fg = hasbg ? bg : col->background;
		bg = t;
		hasfg = hasbg = 1;
	}
	if (st.flags & GHOSTTY_STYLE_FLAG_INVISIBLE) { fg = hasbg ? bg : col->background; hasfg = 1; }
	int ul = GHOSTTY_STYLE_UNDERLINE(st);
	Face f = { .style = (unsigned char)((st.flags & GHOSTTY_STYLE_FLAG_BOLD ? FACE_BOLD : 0) | (st.flags & GHOSTTY_STYLE_FLAG_ITALIC ? FACE_ITALIC : 0)
	                                    | (st.flags & GHOSTTY_STYLE_FLAG_STRIKETHROUGH ? FACE_STRIKE : 0)
	                                    | (ul == GHOSTTY_SGR_UNDERLINE_CURLY ? FACE_UNDERCURL : ul ? FACE_UNDERLINE : 0)
	                                    | (hasfg ? FACE_FG : 0) | (hasbg ? FACE_BG : 0)) };
	if (hasfg) rgb(f.fg, fg);
	if (hasbg) rgb(f.bg, bg);
	int face = f.style ? face_of(&f) : FACE_NONE;
	for (int i = 0; i < n; i++) g->c[r * g->cols + c + i].face = (unsigned short)face;
	return n;
}

void term_draw(Editor *e, Grid *g)
{
	if (!T.open) return;
	Box b = box(e);
	if (b.h - 2 < 1 || b.w - 2 < 1) return;
	if (b.h - 2 != T.rows || b.w - 2 != T.cols) {
		T.rows = b.h - 2; T.cols = b.w - 2;
		ghostty_terminal_resize(T.t, (uint16_t)T.cols, (uint16_t)T.rows, 1, 1);
		proc_resize(T.id, T.rows, T.cols);
	}
	T.frame++;
	box_fill(g, b);
	GhosttyString title = { 0 };
	char tl[64] = " Terminal ";
	if (!ghostty_terminal_get(T.t, GHOSTTY_TERMINAL_DATA_TITLE, &title) && title.len && title.len < sizeof tl - 2) {
		tl[0] = ' ';
		memcpy(tl + 1, title.ptr, title.len);
		memcpy(tl + 1 + title.len, " ", 2);
	}
	box_border(g, b, tl);
	GhosttyRenderStateColors col = GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
	GhosttyRenderStateCursor cur = GHOSTTY_INIT_SIZED(GhosttyRenderStateCursor);
	ghostty_render_state_update(T.rs, T.t);
	ghostty_render_state_get(T.rs, GHOSTTY_RENDER_STATE_DATA_COLORS, &col);
	ghostty_render_state_get(T.rs, GHOSTTY_RENDER_STATE_DATA_CURSOR, &cur);
	ghostty_render_state_get(T.rs, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &T.it);
	for (int r = 0; r < T.rows && ghostty_render_state_row_iterator_next(T.it); r++) {
		ghostty_render_state_row_get(T.it, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &T.cells);
		for (int c = 0; c < T.cols && ghostty_render_state_row_cells_next(T.cells);)
			c += cell(g, b.y + 1 + r, b.x + 1 + c, &col);
	}
	ghostty_render_state_clean(T.rs);
	if (cur.visible && cur.viewport_has_value) {
		g->crow = b.y + 1 + cur.viewport_y;
		g->ccol = b.x + 1 + cur.viewport_x;
		g->cshape = cur.visual_style == GHOSTTY_CURSOR_STYLE_BAR ? CURSOR_BAR
		          : cur.visual_style == GHOSTTY_CURSOR_STYLE_UNDERLINE ? CURSOR_UNDER : CURSOR_BLOCK;
	}
}
