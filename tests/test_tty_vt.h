/* A model terminal for the frame tests: it reads what the terminal backend
   writes and keeps the screen a real one would show, so a test can compare
   that screen with the grid.  It understands only what the backend sends
   (cursor moves, SGR 0/2/7, erase all, synchronized update, cursor
   visibility, the alternate screen, UTF-8 text) and calls anything else an error, so a new
   sequence in the backend needs a model change here first.  Wide characters
   follow xterm: printing on either half of one blanks the other half, and
   printing in the last column leaves the cursor there with the wrap pending.
   Erasing uses the current SGR's background, so an erase under reverse
   video marks the cells (VT_BADERASE) instead of blanking them.  Like a
   terminal that clusters graphemes (foot, kitty, WezTerm), a code point of
   width 0, and the one after a zero width joiner, joins the cell printed
   just before it. */
#include <string.h>

enum { VT_BADERASE = 99 };

/* The colours and styles after SGR's attribute, as strings, numbered in the
   order first seen (0 is none); a cell's face holds that number.  vt_face
   maps a grid face to the number of the SGR it must get, vt_layer[bg][face]
   a face under a cell's bg layer (grid.h). */
static char vt_style[1024][64];
static int vt_nstyle = 1, vt_face[FACE_COUNT], vt_layer[FACE_COUNT][FACE_COUNT];

static int vt_style_id(const char *s)
{
	for (int i = 0; i < vt_nstyle; i++) if (!strcmp(vt_style[i], s)) return i;
	CHECK(vt_nstyle < 1024 && strlen(s) < 64);
	strcpy(vt_style[vt_nstyle], s);
	return vt_nstyle++;
}

typedef struct { unsigned char ch[GRID_CLUSTER_MAX], len, attr; unsigned short face; } VtCell;

typedef struct {
	int rows, cols;
	VtCell c[64 * 256];
	int r, col, pending, attr, style, sync, hidden;
	VtCell *last;                /* the cell printed last, while the cursor stays after it */
	int zwj;                     /* the last code point was U+200D */
	int prints, frames, bad;     /* characters printed, frames ended, first error */
	char why[128];
	/* the terminal's own idea of a character's width */
	int (*width)(const unsigned char *s, int n);
	unsigned char u[4];          /* a UTF-8 character being collected */
	int un, uw;
	unsigned char esc[64];       /* an escape sequence being collected */
	int en;
} Vt;

static void vt_fail(Vt *t, const char *why)
{
	if (!t->bad) { t->bad = 1; snprintf(t->why, sizeof t->why, "%s", why); }
}

static void vt_init(Vt *t, int rows, int cols, int (*width)(const unsigned char *, int))
{
	memset(t, 0, sizeof *t);
	t->rows = rows; t->cols = cols; t->width = width;
	for (int i = 0; i < rows * cols; i++) t->c[i] = (VtCell){ " ", 1, ATTR_NONE, 0 };
}

/* A new size: a real terminal keeps or reflows its cells, so they are
   marked unknown; the SGR and modes stay. */
static void vt_resize(Vt *t, int rows, int cols)
{
	t->rows = rows; t->cols = cols;
	t->r = t->col = t->pending = 0;
	t->last = NULL;
	for (int i = 0; i < rows * cols; i++) t->c[i] = (VtCell){ "?", 1, VT_BADERASE, 0 };
}

static VtCell *vt_at(Vt *t, int r, int c) { return &t->c[r * t->cols + c]; }

static void vt_print(Vt *t, const unsigned char *s, int n)
{
	int w = t->width(s, n), zwj = t->zwj;
	if (!t->sync) vt_fail(t, "a character printed outside a synchronized update");
	if (!t->hidden) vt_fail(t, "a character printed with the cursor shown");
	t->zwj = n == 3 && !memcmp(s, "\xe2\x80\x8d", 3);
	if ((!w || zwj) && t->last) {
		if (t->last->len + n > GRID_CLUSTER_MAX) vt_fail(t, "a cluster longer than a cell holds");
		else { memcpy(t->last->ch + t->last->len, s, (size_t)n); t->last->len = (unsigned char)(t->last->len + n); }
		return;
	}
	if (!w) vt_fail(t, "a code point of width 0 with nothing to join");
	if (t->pending || t->col + w > t->cols) vt_fail(t, "a character printed past the right margin");
	if (t->bad) return;
	t->prints++;
	VtCell *x = t->last = vt_at(t, t->r, t->col);
	if (t->col > 0 && !x->len) x[-1] = (VtCell){ " ", 1, x[-1].attr, x[-1].face };
	if (t->col + w < t->cols && !x[w].len) x[w] = (VtCell){ " ", 1, x[w].attr, x[w].face };
	memcpy(x->ch, s, (size_t)n);
	x->len = (unsigned char)n;
	x->attr = (unsigned char)t->attr;
	x->face = (unsigned short)t->style;
	if (w == 2) x[1] = (VtCell){ "", 0, (unsigned char)t->attr, (unsigned short)t->style };
	t->col += w;
	if (t->col == t->cols) { t->col--; t->pending = 1; }
}

static void vt_csi(Vt *t)
{
	char s[65];
	memcpy(s, t->esc + 2, (size_t)t->en - 2);
	s[t->en - 2] = 0;
	int r, c;
	if (s[strlen(s) - 1] != 'm') t->last = NULL;   /* a move or erase: nothing to join */
	if (!strcmp(s, "?2026h")) { if (t->sync++) vt_fail(t, "nested synchronized update"); }
	else if (!strcmp(s, "?2026l")) {
		if (!t->sync--) vt_fail(t, "synchronized update ended twice");
		else if (t->hidden) vt_fail(t, "frame ended with the cursor hidden");
		t->frames++;
	}
	else if (!strcmp(s, "?1049h") || !strcmp(s, "?1049l")) {
		/* the alternate screen starts blank; what the main screen holds
		   is not known, so it is marked */
		for (int i = 0; i < t->rows * t->cols; i++)
			t->c[i] = (VtCell){ " ", 1, (unsigned char)(s[5] == 'h' ? ATTR_NONE : VT_BADERASE), 0 };
	}
	else if (!strcmp(s, "?25l")) t->hidden = 1;
	else if (!strcmp(s, "?25h")) t->hidden = 0;
	else if (s[0] == '0' && s[strlen(s) - 1] == 'm') {    /* 0, then 7 or 2, then the face's */
		const char *f = s + 1;
		t->attr = ATTR_NONE;
		if ((f[0] == ';' && (f[1] == '7' || f[1] == '2')) && (f[2] == ';' || f[2] == 'm')) {
			t->attr = f[1] == '7' ? ATTR_REV : ATTR_DIM;
			f += 2;
		}
		char st[64];
		snprintf(st, sizeof st, "%.*s", (int)strlen(f) - 1, f);
		t->style = vt_style_id(st);
	}
	else if (!strcmp(s, "H")) { t->r = t->col = t->pending = 0; }
	else if (!strcmp(s, "2J")) {
		for (int i = 0; i < t->rows * t->cols; i++)
			t->c[i] = (VtCell){ " ", 1, (unsigned char)(t->attr == ATTR_REV || t->style ? VT_BADERASE : ATTR_NONE), 0 };
	}
	else if (sscanf(s, "%d;%dH", &r, &c) == 2 && strchr(s, 'H')[1] == 0 && r >= 1 && r <= t->rows && c >= 1 && c <= t->cols) {
		t->r = r - 1; t->col = c - 1; t->pending = 0;
	}
	else vt_fail(t, "an escape sequence the model does not know");
}

/* Feed bytes; may be called with any split of the stream. */
static void vt_feed(Vt *t, const unsigned char *s, size_t n)
{
	for (size_t i = 0; i < n && !t->bad; i++) {
		unsigned char b = s[i];
		if (t->en) {
			if (t->en == sizeof t->esc) { vt_fail(t, "escape sequence too long"); return; }
			t->esc[t->en++] = b;
			if (t->en == 2 && b != '[') vt_fail(t, "escape without [");
			if (t->en > 2 && b >= 0x40 && b <= 0x7e) { vt_csi(t); t->en = 0; }
		} else if (t->un) {
			t->u[t->un++] = b;
			if ((b & 0xC0) != 0x80) vt_fail(t, "broken UTF-8");
			if (t->un == t->uw) { vt_print(t, t->u, t->un); t->un = 0; }
		} else if (b == 27) t->esc[t->en++] = b;
		else if (b >= 0x20 && b < 0x7f) vt_print(t, &b, 1);
		else if (b >= 0xC2 && b < 0xF5) { t->u[0] = b; t->un = 1; t->uw = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : 2; }
		else vt_fail(t, "a control byte or invalid UTF-8");
	}
}

/* Where the screen differs from g (cells, attributes, cursor), or NULL.  A
   grid's two-cell character may show as one cell and a blank, where the
   terminal thinks the character narrow. */
static inline const char *vt_differs(Vt *t, const Grid *g)
{
	static char why[64];
	if (t->bad) return t->why;
	if (t->rows != g->rows || t->cols != g->cols) return "size";
	for (int r = 0; r < g->rows; r++)
		for (int c = 0; c < g->cols; c++) {
			const Cell *x = &g->c[r * g->cols + c];
			const VtCell *y = vt_at(t, r, c);
			int ok = x->len ? x->len == y->len && x->attr == y->attr && !memcmp(grid_text(x), y->ch, x->len)
			       : (!y->len || (y->len == 1 && y->ch[0] == ' ')) && y->attr == x->attr;
			ok = ok && y->face == (x->bg ? vt_layer[x->bg][x->face] : vt_face[x->face]);
			if (!ok) { snprintf(why, sizeof why, "cell %d,%d", r, c); return why; }
		}
	if (t->r != g->crow || t->col != g->ccol || t->hidden || t->sync) {
		snprintf(why, sizeof why, "cursor at %d,%d, want %d,%d", t->r, t->col, g->crow, g->ccol);
		return why;
	}
	return NULL;
}
