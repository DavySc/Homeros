/* The save kitten: my-nvim's lua/config/core/kitten.lua, frame for frame
   (it ports kittymax's kitty-cat.el):

       /\_/\  <3
      ( ^.^ )__o
       > ^ <   saved notes.org
*/
#include <string.h>
#include "kitten.h"
#include "core.h"
#include "loop.h"
#include "uni.h"

/* M.frame_ms and M.hold_ms; the blink is math.floor(d * 0.7) */
enum { MS = 140, HOLD = 1400, NFRAMES = 6 };
/* the last frame, with the heart, also has the text */
typedef struct { const char *eyes, *paw, *heart; int ms; } Frame;
static const Frame frames[NFRAMES] = {
	{ "o.o", "", "", MS },
	{ "-.-", "", "", MS * 7 / 10 },
	{ "o.o", "_", "", MS },
	{ "^.^", "__", "", MS },
	{ "^.^", "__o", "", MS },
	{ "^.^", "__o", "<3", HOLD },
};

static int off;                  /* <leader>uk: vim.g.kitten_on_save == false */
static int frame = -1;           /* the frame on screen; -1: none */
static int armed = -1;           /* the frame whose timer runs; -1: none */
static const Buf *for_buf;       /* the buffer it plays for (state.for_buf) */
static Editor *ed;
static char name[1024];          /* "saved NAME"'s NAME: a Windows file name is up to 765 bytes */

static void step(void *ctx);

static void stop(void)
{
	if (armed >= 0) loop_timer(step, ed, -1);
	frame = armed = -1;
	for_buf = NULL;
}

/* The timer: the next frame, or after the last the message (step()). */
static void step(void *ctx)
{
	Editor *e = ctx;
	armed = -1;
	core_changed = 1;
	if (++frame < NFRAMES) return;
	stop();
	core_msg(e, "=^.^= saved %s", name);
}

void kitten_paw(Editor *e)
{
	stop();
	/* fnamemodify(file, ":t"), or "[No Name]" */
	const char *p = e->buf->path, *t = p;
	if (!p) t = "[No Name]";
	else for (; *p; p++) if (*p == '/'
#ifdef _WIN32
	                         || *p == '\\'
#endif
	                         ) t = p + 1;
	size_t n = strlen(t);
	if (n >= sizeof name) n = uni_prev((const unsigned char *)t, n, sizeof name);
	memcpy(name, t, n);
	name[n] = 0;
	ed = e;
	for_buf = e->buf;
	frame = 0;
}

void kitten_toggle(Editor *e)
{
	off = !off;
	core_msg(e, "Kitten on save: %s", off ? "off" : "on");
}

void kitten_written(Editor *e, Buf *b)
{
	/* M.should_play: the buffer in front of you, not from a macro
	   (reg_executing()); a formatter that saves again right after gets one
	   paw */
	if (!off && !e->ks.executing && b == e->buf && !(frame >= 0 && for_buf == b)) kitten_paw(e);
}

void kitten_key(void)
{
	if (frame >= 0) stop();
}

/* The command line's first row: under the lowest window's status line
   (laststatus 2), so a taller command line moves it up (cmdheight). */
static int cmdrow(const Editor *e)
{
	int r = 0;
	for (const Win *w = e->wins; w; w = w->next)
		if (w->y + w->h + 1 > r) r = w->y + w->h + 1;
	return r;
}

void kitten_draw(Editor *e, Grid *g)
{
	int cr = cmdrow(e);
	if (frame < 0) {
		/* the message keeps its colours (nvim_echo's chunks) while it is shown */
		if (e->mode != MODE_NORMAL || strncmp(e->msg, "=^.^= saved ", 12) || cr >= g->rows) return;
		int end = g->cols;
		int c = grid_puts(g, cr, 0, end, "=^.^=", 5, ATTR_NONE, FACE_KITTEN_CAT);
		c = grid_puts(g, cr, c, end, " saved ", 7, ATTR_NONE, FACE_KITTEN_DIM);
		grid_puts(g, cr, c, end, e->msg + 12, strlen(e->msg + 12), ATTR_NONE, FACE_KITTEN_TEXT);
		return;
	}
	const Frame *f = &frames[frame];
	/* Kitten.tla "ok": only the draw that first shows a frame arms the next;
	   arming on every draw lets other redraws push it back for ever.  A
	   screen too small for it still counts the frame as shown, so the
	   kitten ends on time. */
	if (armed != frame) {
		armed = frame;
		loop_timer(step, ed, f->ms);
	}
	/* the float: as wide as its widest line (10 cells, more with the
	   heart and text), two columns from the right, on the three rows above
	   the last status line and the command line (lines - cmdheight - 1 - 3) */
	int w = f->heart[0] ? 16 + grid_width(name, strlen(name)) : 10;
	int r = cr - 1 - 3, c = g->cols - w - 2, end;
	if (r < 0) r = 0;
	if (c < 0) c = 0;
	if (r + 3 > g->rows) return;
	end = c + w < g->cols ? c + w : g->cols;
	for (int i = 0; i < 3; i++)
		for (int x = c; x < end; x++) grid_puts(g, r + i, x, end, " ", 1, ATTR_NONE, FACE_NONE);
	int x = grid_puts(g, r, c, end, " /\\_/\\ ", 7, ATTR_NONE, FACE_KITTEN_CAT);
	grid_puts(g, r, x + 2, end, f->heart, strlen(f->heart), ATTR_NONE, FACE_KITTEN_HEART);
	x = grid_puts(g, r + 1, c, end, "( ", 2, ATTR_NONE, FACE_KITTEN_CAT);
	x = grid_puts(g, r + 1, x, end, f->eyes, 3, ATTR_NONE, FACE_KITTEN_FACE);
	x = grid_puts(g, r + 1, x, end, " )", 2, ATTR_NONE, FACE_KITTEN_CAT);
	grid_puts(g, r + 1, x, end, f->paw, strlen(f->paw), ATTR_NONE, FACE_KITTEN_PAW);
	x = grid_puts(g, r + 2, c, end, " > ^ < ", 7, ATTR_NONE, FACE_KITTEN_CAT);
	if (f->heart[0]) {
		x = grid_puts(g, r + 2, x + 3, end, "saved ", 6, ATTR_NONE, FACE_KITTEN_TEXT);
		grid_puts(g, r + 2, x, end, name, strlen(name), ATTR_NONE, FACE_KITTEN_TEXT);
		/* wider than the screen: the float is cut to it and its last line
		   wraps out of sight, so 'display' lastline ends it in "@@@"
		   (NonText: overlay0, as the dim face); a double-width character
		   cut by it is an "@" too (measured) */
		if (c + w > g->cols && end - c >= 4) {
			int a = end - 3 - !g->c[(r + 2) * g->cols + end - 3].len;
			grid_puts(g, r + 2, a, end, "@@@@", (size_t)(end - a), ATTR_NONE, FACE_KITTEN_DIM);
		}
	}
}
