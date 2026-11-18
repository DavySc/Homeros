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
typedef struct { const char *eyes, *paw, *heart; int text, ms; } Frame;
static const Frame frames[NFRAMES] = {
	{ "o.o", "", "", 0, MS },
	{ "-.-", "", "", 0, MS * 7 / 10 },
	{ "o.o", "_", "", 0, MS },
	{ "^.^", "__", "", 0, MS },
	{ "^.^", "__o", "", 0, MS },
	{ "^.^", "__o", "<3", 1, HOLD },
};

static int off;                  /* <leader>uk: vim.g.kitten_on_save == false */
static int frame = -1;           /* the frame on screen; -1: none */
static int armed = -1;           /* the frame whose timer runs; -1: none */
static const Buf *for_buf;       /* the buffer it plays for (state.for_buf) */
static Editor *ed;
static char name[1024];          /* "saved NAME"'s NAME: a Windows file name is up to 765 bytes */
static char sum[sizeof ((Editor *)0)->msg];   /* the message it leaves; tinted while e->msg is it */

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
	memcpy(sum, e->msg, sizeof sum);
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
	sum[0] = 0;
}

void kitten_toggle(Editor *e)
{
	off = !off;
	core_msg(e, "Kitten on save: %s", off ? "off" : "on");
}

void kitten_written(Editor *e, Buf *b)
{
	/* M.should_play: the buffer in front of you; a formatter that saves
	   again right after gets one paw */
	if (!off && b == e->buf && !(frame >= 0 && for_buf == b)) kitten_paw(e);
}

void kitten_key(void)
{
	if (frame >= 0) stop();
}

/* Puts s (n bytes) from column c with face f, up to column end; returns the
   next column. */
static int put(Grid *g, int r, int c, int end, const char *s, size_t n, int f)
{
	const unsigned char *u = (const unsigned char *)s;
	for (size_t i = 0; i < n; i += uni_glen(u, n, i)) {
		int w = uni_width(u, n, i);
		if (c + w > end) break;
		w = grid_put(g, r, c, u + i, uni_glen(u, n, i), w, ATTR_NONE);
		if (w) g->c[r * g->cols + c].face = (unsigned char)f;
		c += w;
	}
	return c;
}

static int width(const char *s)
{
	const unsigned char *u = (const unsigned char *)s;
	size_t n = strlen(s);
	int w = 0;
	for (size_t i = 0; i < n; i += uni_glen(u, n, i)) w += uni_width(u, n, i);
	return w;
}

void kitten_draw(Editor *e, Grid *g)
{
	if (frame < 0) {
		/* the message keeps its colours (nvim_echo's chunks) while it is shown */
		if (!sum[0] || e->mode != MODE_NORMAL || strcmp(e->msg, sum)) return;
		int r = g->rows - 1, end = g->cols;
		int c = put(g, r, 0, end, "=^.^=", 5, FACE_KITTEN_CAT);
		c = put(g, r, c, end, " saved ", 7, FACE_KITTEN_DIM);
		put(g, r, c, end, sum + 12, strlen(sum + 12), FACE_KITTEN_TEXT);
		return;
	}
	const Frame *f = &frames[frame];
	/* the float: as wide as its widest line (10 cells, 11 with the heart,
	   more with the text), two columns from the right, on the three rows
	   above the last status line and the command line */
	int w = f->text ? 16 + width(name) : 10;
	int r = g->rows - 2 - 3, c = g->cols - w - 2, end;
	if (r < 0) r = 0;
	if (c < 0) c = 0;
	end = c + w < g->cols ? c + w : g->cols;
	for (int i = 0; i < 3; i++)
		for (int x = c; x < end; x++) put(g, r + i, x, end, " ", 1, FACE_NONE);
	int x = put(g, r, c, end, " /\\_/\\ ", 7, FACE_KITTEN_CAT);
	put(g, r, x + 2, end, f->heart, strlen(f->heart), FACE_KITTEN_HEART);
	x = put(g, r + 1, c, end, "( ", 2, FACE_KITTEN_CAT);
	x = put(g, r + 1, x, end, f->eyes, 3, FACE_KITTEN_FACE);
	x = put(g, r + 1, x, end, " )", 2, FACE_KITTEN_CAT);
	put(g, r + 1, x, end, f->paw, strlen(f->paw), FACE_KITTEN_PAW);
	x = put(g, r + 2, c, end, " > ^ < ", 7, FACE_KITTEN_CAT);
	if (f->text) {
		x = put(g, r + 2, x + 3, end, "saved ", 6, FACE_KITTEN_TEXT);
		put(g, r + 2, x, end, name, strlen(name), FACE_KITTEN_TEXT);
		/* wider than the screen: the float is cut to it and its last line
		   wraps out of sight, so 'display' lastline ends it in "@@@"
		   (NonText: overlay0, as the dim face) */
		if (c + w > g->cols) {
			/* a double-width character cut by it is an "@" too (measured) */
			int a = end - 3 - !g->c[(r + 2) * g->cols + end - 3].len;
			put(g, r + 2, a, end, "@@@@", (size_t)(end - a), FACE_KITTEN_DIM);
		}
	}
	/* Kitten.tla "ok": only the draw that first shows a frame arms the next;
	   arming on every draw lets other redraws push it back for ever */
	if (armed != frame) {
		armed = frame;
		loop_timer(step, ed, f->ms);
	}
}
