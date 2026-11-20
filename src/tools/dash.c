/* The dashboard: snacks.dashboard (folke/snacks.nvim at 882c996) as my-nvim
   sets it up (lua/config/core/ui.lua), with the app icon's cat and frog in
   place of splash.lua's cat (still shown only with 18 lines below it, and
   animated as it is: blink, ear twitch, whiskers, shimmer), the
   keys, and the "loaded in" footer.  Its keys are the preset's that make sense
   here: f n g r q (c opens Neovim's config, which homeros does not have; s
   and L are hidden in my-nvim too, without lazy.nvim).  It covers the
   window, and the status line as snacks' laststatus=0 does, until the
   window shows a file, the buffer changes or insert mode starts.
   The comments name the snacks or splash function each rule comes from. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "tools.h"
#include "core.h"
#include "dash.h"
#include "loop.h"
#include "uni.h"

enum { WIDTH = 60, BELOW = 18, NKEY = 5 };

static struct {
	int open;
	const Buf *buf;            /* the start buffer under it */
	unsigned long seq;         /* its undo_seq when the dashboard opened */
	long long start;           /* main()'s start, then -1 once measured */
	double ms;                 /* the "loaded in" time */
	int sel;                   /* the item the cursor is on */
	int n, ticking;            /* the animation's frame; its timer runs */
	long long t0;              /* when it opened (loop_now) */
} D;

static const struct { const char *icon, *desc; char key; } keys[NKEY] = {
	{ "\xef\x80\x82 ", "Find File", 'f' },     /* U+F002 */
	{ "\xef\x85\x9b ", "New File", 'n' },      /* U+F15B */
	{ "\xef\x80\xa2 ", "Find Text", 'g' },     /* U+F022 */
	{ "\xef\x83\x85 ", "Recent Files", 'r' },  /* U+F0C5 */
	{ "\xef\x90\xa6 ", "Quit", 'q' },          /* U+F426 */
};

/* The cat and the frog over a terminal (the app icon), with each cell's
   role: c cat, p its patches, f frog, k its cheeks, m its mouth, s the
   sparkle, t the terminal, b its prompt and bookmark */
enum { ART_ROWS = 12, ART_COLS = 41 };
/* in UTF-8, one column per character, every row ART_COLS wide (snacks
   centres each row on its own) */
static const struct { const char *text; char role[ART_COLS + 1]; } art[ART_ROWS] = {
	{ "                                |  /     ",
	  "                                s  s     " },
	{ "    /\\         /\\                   -    ",
	  "    pp         pp                   s    " },
	{ "   /  \\_______/  \\      .-.   .-.        ",
	  "   c  ccccccccc  c      fff   fff        " },
	{ "  /               \\    ( ^ )-( ^ )       ",
	  "  c               c    f f fff f f       " },
	{ " |      ^       ^  | .-'         '-.     ",
	  " c      c       c  c fff         fff     " },
	{ " | =         w     |(  o   \\___/  o )    ",
	  " c c         c     cf  k   mmmmm  k f    " },
	{ "  \\       _       / '-.___________.-'    ",
	  "  c       c       c fffffffffffffffff    " },
	{ "╭──\\_____( ' )___/──────────────┬──┬────╮",
	  "tttcccccccccccccccttttttttttttttbbbbttttt" },
	{ "│                               │  │    │",
	  "t                               bbbb    t" },
	{ "│   >_                          │/\\│    │",
	  "t   bb                          bbbb    t" },
	{ "│                                       │",
	  "t                                       t" },
	{ "╰───────────────────────────────────────╯",
	  "ttttttttttttttttttttttttttttttttttttttttt" },
};
/* splash.lua's shimmer (hl_for): the face at position i of frame n is
   SplashCat((i + n / 2) % 12 + 1), its 12-step gradient */
static int shimmer(int i)
{
	static const unsigned char gradient[12] = {
		FACE_KITTEN_TEXT, FACE_DASHBOARD_PROMPT, FACE_DASHBOARD_TITLE, FACE_DASHBOARD_FOOTER,
		FACE_SHIMMER_SAPPHIRE, FACE_SHIMMER_TEAL, FACE_DASHBOARD_FROG, FACE_SHIMMER_TEAL,
		FACE_SHIMMER_SAPPHIRE, FACE_DASHBOARD_FOOTER, FACE_DASHBOARD_TITLE, FACE_DASHBOARD_PROMPT,
	};
	return gradient[(i + D.n / 2) % 12];
}

/* row y's role r; the terminal shimmers down its rows, as splash's outline */
static int face_of(char r, int y)
{
	switch (r) {
	case 'c': return FACE_DASHBOARD_CAT;
	case 'p': return FACE_DASHBOARD_CAT_PATCH;
	case 'f': return FACE_DASHBOARD_FROG;
	case 'k': return FACE_DASHBOARD_FROG_CHEEK;
	case 'm': return FACE_DASHBOARD_FROG_MOUTH;
	case 's': return FACE_DASHBOARD_SPARKLE;
	case 'b': return FACE_DASHBOARD_PROMPT;
	}
	return shimmer(y);
}

/* ---- the layout (D:render, D:format) ---- */

static void cell(Grid *g, int r, int c, const char *s, size_t n, int w, int face)
{
	if (r < 0 || r >= g->rows || c < 0 || c + w > g->cols) return;
	grid_put(g, r, c, (const unsigned char *)s, n, w, ATTR_NONE);
	g->c[r * g->cols + c].face = (unsigned char)face;
}

/* UTF-8 s from column c in one face; returns the next column */
static int str(Grid *g, int r, int c, const char *s, int face)
{
	return grid_puts(g, r, c, g->cols, s, strlen(s), ATTR_NONE, face);
}

/* the window's rows: the screen less the command line (laststatus=0) */
static int height(Editor *e) { return e->rows - 1; }

/* the item rows, from the top of the screen, for the cursor and drawing:
   the cat block (its rows, the name, one padding line), then each key and
   the gap after it, then the footer */
static void geometry(Editor *e, int *top, int *art_on, int *first)
{
	/* M.pick's rule: the picture only with room around and below it */
	*art_on = e->cols >= ART_COLS + 6 && height(e) >= ART_ROWS + BELOW;
	int lines = (*art_on ? ART_ROWS : 0) + 2 + NKEY * 2 + 1;
	*top = (height(e) - lines) / 2;
	if (*top < 0) *top = 0;
	*first = *top + (*art_on ? ART_ROWS : 0) + 2;
}

/* splash.lua's animation: a frame every 350 ms (M.interval) for 300 s
   (M.seconds) from each opening.  As the save kitten (Kitten.tla "ok"),
   only the draw that first shows a frame arms the next: an idle or hidden
   dashboard is woken at most once, and other redraws do not hold it back. */
enum { INTERVAL = 350 };
static const long long SECONDS = 300;

static void tick(void *ctx)
{
	(void)ctx;
	D.ticking = 0;
	D.n++;
	core_changed = 1;
}

void tools_dash(Editor *e, long long start_ns)
{
	double ms = D.ms;   /* :Dashboard (start_ns 0) shows the startup time again */
	memset(&D, 0, sizeof D);
	D.ms = ms;
	D.open = 1;
	D.buf = e->buf;
	D.seq = undo_seq(e->buf->undo);
	D.start = start_ns;
	D.t0 = loop_now();
}

/* still shown?  Gone once the window shows a file or another buffer
   (bufhidden=wipe; after :Dashboard the file's buffer is still there, for
   CTRL-^ or SPC ,), the buffer changed or another window opened. */
static int shown(Editor *e)
{
	if (D.open && (e->buf != D.buf || D.buf->path || e->wins->next || e->mode == MODE_INSERT || undo_seq(D.buf->undo) != D.seq)) {
		D.open = 0;
		loop_timer(tick, NULL, -1);   /* M.stop on SnacksDashboardClosed */
	}
	return D.open;
}

/* D:action */
static void action(Editor *e, int i)
{
	switch (keys[i].key) {
	case 'f': tools_pick(e, PICK_FILES); break;
	case 'g': tools_pick(e, PICK_GREP); break;
	case 'r': tools_pick(e, PICK_RECENT); break;
	/* :ene | startinsert: the empty buffer is already here */
	case 'n': keys_feed(e, 'i', 0); shown(e); break;
	/* :qa; while it shows there is one window, so :q is :qa (E162 for a
	   file changed before :Dashboard) */
	case 'q': core_close(e, 0); break;
	}
}

int dash_key(Editor *e, int k)
{
	if (!shown(e) || e->mode != MODE_NORMAL) return 0;
	for (int i = 0; i < NKEY; i++) if (k == keys[i].key) { action(e, i); return 1; }
	switch (k) {
	/* update_cursor: a move down or up lands on the next or previous item */
	case 'j': case KEY_DOWN: if (D.sel < NKEY - 1) D.sel++; return 1;
	case 'k': case KEY_UP: if (D.sel > 0) D.sel--; return 1;
	case KEY_ENTER: action(e, D.sel); return 1;
	}
	return 0;
}

void dash_draw(Editor *e, Grid *g)
{
	if (D.start) {
		/* the footer's number is measured once (ui.lua), at the first
		   draw, also when a file is shown then, for a later :Dashboard */
		struct timespec t;
		timespec_get(&t, TIME_UTC);
		D.ms = ((long long)t.tv_sec * 1000000000 + t.tv_nsec - D.start) / 1e6;
		D.start = 0;
	}
	if (!shown(e)) return;
	if (!D.ticking && loop_now() - D.t0 < SECONDS * 1000000000) {
		D.ticking = 1;
		loop_timer(tick, NULL, INTERVAL);
	}
	int top, art_on, first, h = height(e);
	geometry(e, &top, &art_on, &first);
	/* the window, blank, with "~" past the buffer's last line */
	int end = first + NKEY * 2 + 1;
	for (int r = 0; r < h; r++)
		for (int c = 0; c < g->cols; c++) cell(g, r, c, c || r < end ? " " : "~", 1, 1, 0);
	for (int r = end; r < h; r++) g->c[r * g->cols].attr = ATTR_DIM;
	/* D:render: the pane starts at (cols - WIDTH) / 2; a text item is
	   centred in WIDTH */
	int col = (e->cols - WIDTH) / 2, r = top;
	if (col < 0) col = 0;
	if (art_on) {
		int x0 = col + (WIDTH - ART_COLS) / 2;
		for (int y = 0; y < ART_ROWS; y++, r++) {
			const unsigned char *t = (const unsigned char *)art[y].text;
			size_t n = strlen(art[y].text);
			for (size_t i = 0, x = 0; i < n; x++) {
				size_t k = uni_glen(t, n, i);
				/* M.texts: the right ear a column in (n % 31 == 7; row 1
				   is ASCII), a blink (n % 12 == 11, the frog's half a
				   cycle later) and wiggling whiskers (n % 9 == 4) */
				size_t j = y == 1 && x >= 14 && x <= 16 && D.n % 31 == 7 ? i + 1 : i;
				const char *s = art[y].text + j;
				if (*s == '^' && ((y == 4 && D.n % 12 == 11) || (y == 3 && D.n % 12 == 5))) s = "-";
				else if (*s == '=' && D.n % 9 == 4) s = "~";
				if (*s != ' ') cell(g, r, x0 + (int)x, s, k, 1, face_of(art[y].role[x + j - i], y));
				i += k;
			}
		}
	}
	static const char name[] = "h o m e r o s";
	for (int i = 0, x0 = col + (WIDTH - (int)sizeof name + 1) / 2; name[i]; i++)
		if (name[i] != ' ') cell(g, r, x0 + i, name + i, 1, 1, shimmer(i));
	r += 2;
	/* the keys: icon, description, key right-aligned (Icon and Desc link
	   Special, Key links Number) */
	for (int i = 0; i < NKEY; i++, r += 2) {
		str(g, r, col, keys[i].icon, FACE_STRING_SPECIAL);
		str(g, r, col + 3, keys[i].desc, FACE_STRING_SPECIAL);
		cell(g, r, col + WIDTH - 1, &keys[i].key, 1, 1, FACE_NUMBER);
	}
	/* the footer: footer links Title, special Special */
	char ms[32], foot[64];
	snprintf(ms, sizeof ms, "%.1fms", D.ms);
	snprintf(foot, sizeof foot, "\xe2\x9a\xa1 Homeros loaded in %s", ms);
	int x = col + (WIDTH - grid_width(foot, strlen(foot))) / 2;
	x = str(g, r, x, "\xe2\x9a\xa1 Homeros loaded in ", FACE_DASHBOARD_FOOTER);
	str(g, r, x, ms, FACE_STRING_SPECIAL);
	/* the cursor on the item's description */
	g->crow = first + 2 * D.sel;
	g->ccol = col + 3;
}
