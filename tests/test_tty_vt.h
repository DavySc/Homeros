/* A model terminal for the frame tests: it reads what the terminal backend
   writes and keeps the screen a real one would show, so a test can compare
   that screen with the grid.  It understands only what the backend sends
   (cursor moves, SGR 0/2/7, erase all, synchronized update, cursor
   visibility, the alternate screen, UTF-8 text) and calls anything else an error, so a new
   sequence in the backend needs a model change here first.  Wide characters
   follow xterm: printing on either half of one blanks the other half, and
   printing in the last column leaves the cursor there with the wrap pending.
   Erasing uses the current SGR's background, so an erase under reverse
   video marks the cells (VT_BADERASE) instead of blanking them. */
#include <string.h>

enum { VT_BADERASE = 99 };

typedef struct {
	int rows, cols;
	Cell c[64 * 256];
	int r, col, pending, attr, sync, hidden;
	int prints, frames, bad;     /* characters printed, frames ended, first error */
	char why[128];
	/* the terminal's own idea of a character's width */
	int (*width)(const unsigned char *s, int n);
	unsigned char u[4];          /* a UTF-8 character being collected */
	int un, uw;
	unsigned char esc[32];       /* an escape sequence being collected */
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
	for (int i = 0; i < rows * cols; i++) t->c[i] = (Cell){ " ", 1, ATTR_NONE };
}

static Cell *vt_at(Vt *t, int r, int c) { return &t->c[r * t->cols + c]; }

static void vt_print(Vt *t, const unsigned char *s, int n)
{
	int w = t->width(s, n);
	if (!t->sync) vt_fail(t, "a character printed outside a synchronized update");
	if (t->pending || t->col + w > t->cols) vt_fail(t, "a character printed past the right margin");
	if (t->bad) return;
	t->prints++;
	Cell *x = vt_at(t, t->r, t->col);
	if (t->col > 0 && !x->len) x[-1] = (Cell){ " ", 1, x[-1].attr };
	if (t->col + w < t->cols && !x[w].len) x[w] = (Cell){ " ", 1, x[w].attr };
	memcpy(x->ch, s, (size_t)n);
	x->len = (unsigned char)n;
	x->attr = (unsigned char)t->attr;
	if (w == 2) x[1] = (Cell){ "", 0, (unsigned char)t->attr };
	t->col += w;
	if (t->col == t->cols) { t->col--; t->pending = 1; }
}

static void vt_csi(Vt *t)
{
	char s[33];
	memcpy(s, t->esc + 2, (size_t)t->en - 2);
	s[t->en - 2] = 0;
	int r, c;
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
			t->c[i] = (Cell){ " ", 1, (unsigned char)(s[5] == 'h' ? ATTR_NONE : VT_BADERASE) };
	}
	else if (!strcmp(s, "?25l")) t->hidden = 1;
	else if (!strcmp(s, "?25h")) t->hidden = 0;
	else if (!strcmp(s, "0m")) t->attr = ATTR_NONE;
	else if (!strcmp(s, "0;7m")) t->attr = ATTR_REV;
	else if (!strcmp(s, "0;2m")) t->attr = ATTR_DIM;
	else if (!strcmp(s, "H")) { t->r = t->col = t->pending = 0; }
	else if (!strcmp(s, "2J")) {
		for (int i = 0; i < t->rows * t->cols; i++)
			t->c[i] = (Cell){ " ", 1, (unsigned char)(t->attr == ATTR_REV ? VT_BADERASE : ATTR_NONE) };
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
			const Cell *x = &g->c[r * g->cols + c], *y = vt_at(t, r, c);
			int ok = x->len ? grid_cell_eq(x, y)
			       : (!y->len || (y->len == 1 && y->ch[0] == ' ')) && y->attr == x->attr;
			if (!ok) { snprintf(why, sizeof why, "cell %d,%d", r, c); return why; }
		}
	if (t->r != g->crow || t->col != g->ccol || t->hidden || t->sync) {
		snprintf(why, sizeof why, "cursor at %d,%d, want %d,%d", t->r, t->col, g->crow, g->ccol);
		return why;
	}
	return NULL;
}
