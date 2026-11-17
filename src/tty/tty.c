/* Terminal backend: raw mode, the alternate screen, changed cells only,
   inside a synchronized update (CSI ? 2026) so a frame is never shown half
   drawn.  A resize arrives through a self-pipe, so poll never misses it. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include "backend.h"
#include "keys.h"

enum { TTIMEOUTLEN = 50 };          /* ms, Neovim's 'ttimeoutlen' default */

static struct termios saved;
static int raw_on, sigpipe[2] = { -1, -1 };
static unsigned char ob[1 << 16];   /* output, flushed when full: no allocation per frame */
static size_t on;

static void flush(void)
{
	for (size_t i = 0; i < on;) {
		ssize_t w = write(1, ob + i, on - i);
		if (w < 0 && errno != EINTR) break;
		if (w > 0) i += (size_t)w;
	}
	on = 0;
}

static void out(const void *s, size_t n)
{
	if (on + n > sizeof ob) flush();
	memcpy(ob + on, s, n);
	on += n;
}

static void outs(const char *s) { out(s, strlen(s)); }

static void on_winch(int sig)
{
	int e = errno;
	(void)sig;
	if (write(sigpipe[1], "w", 1) < 0) {}   /* full pipe: a resize is pending anyway */
	errno = e;
}

static int raw(void)
{
	struct termios t = saved;
	t.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	t.c_oflag &= ~(tcflag_t)OPOST;
	t.c_cflag |= CS8;
	t.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
	t.c_cc[VMIN] = 0;
	t.c_cc[VTIME] = 0;
	if (tcsetattr(0, TCSAFLUSH, &t)) return -1;
	raw_on = 1;
	outs("\x1b[?1049h\x1b[H\x1b[2J");
	flush();
	return 0;
}

static void cooked(void)
{
	if (!raw_on) return;
	outs("\x1b[0m\x1b[?25h\x1b[?1049l");
	flush();
	tcsetattr(0, TCSAFLUSH, &saved);
	raw_on = 0;
}

static int tty_open(void)
{
	if (!isatty(0) || !isatty(1) || tcgetattr(0, &saved) || pipe(sigpipe)) return -1;
	fcntl(sigpipe[0], F_SETFL, O_NONBLOCK);
	fcntl(sigpipe[1], F_SETFL, O_NONBLOCK);
	struct sigaction sa = { .sa_handler = on_winch };
	sigaction(SIGWINCH, &sa, NULL);
	return raw();
}

static void tty_close(void) { cooked(); }

static int tty_fds(int fds[2]) { fds[0] = 0; fds[1] = sigpipe[0]; return 2; }

static void tty_size(int *rows, int *cols)
{
	struct winsize ws;
	if (ioctl(1, TIOCGWINSZ, &ws) || !ws.ws_row || !ws.ws_col) { *rows = 24; *cols = 80; return; }
	*rows = ws.ws_row;
	*cols = ws.ws_col;
}

static int tty_ready(void) { return 1; }   /* a terminal has no frame callback */

/* The key at s[0..n): its length in bytes, 0 if s may be the start of a
   longer key (Neovim's TERMKEY_RES_AGAIN), or with force the first byte
   alone.  *k is the key, or -1 for a sequence that gives none.  A byte that
   is not part of well-formed UTF-8 is U+FFFD: keys.h has no raw bytes, and
   text must never decode to a KEY_* code. */
static int key(const unsigned char *s, int n, int force, int *k)
{
	*k = s[0] < 0x80 ? s[0] : 0xFFFD;
	if (s[0] == 27) {
		if (n == 1) return force;                    /* Esc, or the start of a sequence */
		if (s[1] != '[') return 1;
		int j = 2;                                   /* CSI: parameters, then a final byte */
		while (j < n && (s[j] < 0x40 || s[j] > 0x7e)) j++;
		if (j == n) return force;
		*k = s[2] == 'A' ? KEY_UP : s[2] == 'B' ? KEY_DOWN : s[2] == 'C' ? KEY_RIGHT : s[2] == 'D' ? KEY_LEFT : -1;
		return j + 1;
	}
	if (s[0] < 0xC2 || s[0] > 0xF4) return 1;      /* ASCII, or no UTF-8 lead */
	int len = s[0] >= 0xF0 ? 4 : s[0] >= 0xE0 ? 3 : 2, j = 1;
	while (j < len && j < n && (s[j] & 0xC0) == 0x80) j++;
	if (j < len) return j == n ? force : 1;          /* cut short, or broken: the byte alone */
	unsigned v = s[0] & (len == 2 ? 0x1Fu : 0x0Fu);  /* a 4-byte lead's bit 3 is 0 */
	for (j = 1; j < len; j++) v = v * 64 + (s[j] & 0x3Fu);
	/* overlong (2-byte ones are refused by the lead), surrogate, past U+10FFFF */
	if ((len == 3 && (v < 0x800 || (v >= 0xD800 && v <= 0xDFFF))) || (len == 4 && (v < 0x10000 || v > 0x10FFFF)))
		return 1;
	*k = (int)v;
	return len;
}

/* Bytes to keys: UTF-8 to code points, CSI arrows to KEY_*, other CSI
   sequences dropped.  A read that ends inside a key keeps that tail and, if
   no more input comes within TTIMEOUTLEN ms, takes it byte by byte: an Esc
   typed alone is Esc, as in Neovim (tinput_timer_cb). */
static int tty_input(int *k, int max)
{
	static unsigned char b[256];
	static int held;
	unsigned char c;
	int n = 0;
	while (read(sigpipe[0], &c, 1) == 1) if (!n && max > 0) k[n++] = KEY_RESIZE;
	/* each byte gives at most one key, so never read more than there are
	   slots: what is not read stays in the terminal and keeps poll waking */
	int room = max - n - held;
	if (room > (int)sizeof b - held) room = (int)sizeof b - held;
	ssize_t r = room > 0 ? read(0, b + held, (size_t)room) : 0;
	r = (r > 0 ? r : 0) + held;
	int i = 0, force = 0, x, len;
	for (;;) {
		while (i < r && n < max && (len = key(b + i, (int)(r - i), force, &x)) > 0) {
			if (x >= 0) k[n++] = x;
			i += len;
		}
		/* The tail of a key: wait for the rest.  With no slots to spare
		   for it, keep it for a call with more; a "key" as long as the
		   buffer cannot grow; a signal (a resize) ends the wait early. */
		if (force || i == r || n == max || room <= 0) break;
		struct pollfd p = { .fd = 0, .events = POLLIN };
		if (r - i < (ssize_t)sizeof b && (poll(&p, 1, TTIMEOUTLEN) < 0 || p.revents & POLLIN)) break;
		force = 1;
	}
	held = (int)(r - i);
	memmove(b, b + i, (size_t)held);
	return n;
}

/* Catppuccin Mocha (FEATURES.md: Catppuccin, flavour toggle later): per
   face the SGR that catppuccin/nvim's treesitter.lua gives the capture, with
   its default styles (comments, conditionals and "miscs" italic), links
   followed and unlisted captures falling back to their parent.  Its
   per-language overrides (@markup.heading.1.markdown and the like) have no
   face of their own. */
static const char *const face_fg[FACE_COUNT] = {
	"",
	";38;2;205;214;244",                  /* variable: text */
	";38;2;243;139;168",                  /* variable.builtin: red */
	";38;2;235;160;172",                  /* variable.parameter: maroon */
	";38;2;235;160;172",                  /* variable.parameter.builtin: maroon */
	";38;2;180;190;254",                  /* variable.member: lavender */
	";38;2;250;179;135",                  /* constant: peach */
	";38;2;250;179;135",                  /* constant.builtin: peach */
	";38;2;203;166;247",                  /* constant.macro: mauve */
	";3;38;2;249;226;175",                /* module: yellow, italic */
	";3;38;2;249;226;175",                /* module.builtin: yellow, italic */
	";38;2;116;199;236",                  /* label: sapphire */
	";38;2;166;227;161",                  /* string: green */
	";38;2;148;226;213",                  /* string.documentation: teal */
	";38;2;245;194;231",                  /* string.regexp: pink */
	";38;2;245;194;231",                  /* string.escape: pink */
	";38;2;245;194;231",                  /* string.special: pink */
	";38;2;242;205;205",                  /* string.special.symbol: flamingo */
	";38;2;245;194;231",                  /* string.special.path: pink */
	";3;4;38;2;137;180;250",              /* string.special.url: blue, italic underline */
	";38;2;148;226;213",                  /* character: teal */
	";38;2;245;194;231",                  /* character.special: pink */
	";38;2;250;179;135",                  /* boolean: peach */
	";38;2;250;179;135",                  /* number: peach */
	";38;2;250;179;135",                  /* number.float: peach */
	";38;2;249;226;175",                  /* type: yellow */
	";38;2;203;166;247",                  /* type.builtin: mauve */
	";38;2;249;226;175",                  /* type.definition: yellow */
	";38;2;250;179;135",                  /* attribute: peach */
	";38;2;250;179;135",                  /* attribute.builtin: peach */
	";38;2;180;190;254",                  /* property: lavender */
	";38;2;137;180;250",                  /* function: blue */
	";38;2;250;179;135",                  /* function.builtin: peach */
	";38;2;137;180;250",                  /* function.call: blue */
	";38;2;245;194;231",                  /* function.macro: pink */
	";38;2;137;180;250",                  /* function.method: blue */
	";38;2;137;180;250",                  /* function.method.call: blue */
	";38;2;249;226;175",                  /* constructor: yellow */
	";38;2;137;220;235",                  /* operator: sky */
	";38;2;203;166;247",                  /* keyword: mauve */
	";38;2;203;166;247",                  /* keyword.coroutine: mauve */
	";38;2;203;166;247",                  /* keyword.function: mauve */
	";38;2;203;166;247",                  /* keyword.operator: mauve */
	";38;2;203;166;247",                  /* keyword.import: mauve */
	";38;2;203;166;247",                  /* keyword.type: mauve */
	";38;2;203;166;247",                  /* keyword.modifier: mauve */
	";38;2;203;166;247",                  /* keyword.repeat: mauve */
	";38;2;203;166;247",                  /* keyword.return: mauve */
	";38;2;203;166;247",                  /* keyword.debug: mauve */
	";38;2;203;166;247",                  /* keyword.exception: mauve */
	";3;38;2;203;166;247",                /* keyword.conditional: mauve, italic */
	";38;2;137;220;235",                  /* keyword.conditional.ternary: sky */
	";38;2;245;194;231",                  /* keyword.directive: pink */
	";38;2;245;194;231",                  /* keyword.directive.define: pink */
	";38;2;147;153;178",                  /* punctuation.delimiter: overlay2 */
	";38;2;147;153;178",                  /* punctuation.bracket: overlay2 */
	";38;2;245;194;231",                  /* punctuation.special: pink */
	";3;38;2;147;153;178",                /* comment: overlay2, italic */
	";3;38;2;147;153;178",                /* comment.documentation: overlay2, italic */
	";38;2;30;30;46;48;2;243;139;168",    /* comment.error: base on red */
	";38;2;30;30;46;48;2;249;226;175",    /* comment.warning: base on yellow */
	";38;2;30;30;46;48;2;242;205;205",    /* comment.todo: base on flamingo */
	";38;2;30;30;46;48;2;245;224;220",    /* comment.note: base on rosewater */
	";1;38;2;243;139;168",                /* markup.strong: red, bold */
	";3;38;2;243;139;168",                /* markup.italic: red, italic */
	";9;38;2;205;214;244",                /* markup.strikethrough: text, strikethrough */
	";4",                                 /* markup.underline: underline */
	";38;2;137;180;250",                  /* markup.heading: blue */
	";38;2;137;180;250",                  /* markup.heading.1: blue */
	";38;2;137;180;250",                  /* markup.heading.2: blue */
	";38;2;137;180;250",                  /* markup.heading.3: blue */
	";38;2;137;180;250",                  /* markup.heading.4: blue */
	";38;2;137;180;250",                  /* markup.heading.5: blue */
	";38;2;137;180;250",                  /* markup.heading.6: blue */
	";38;2;245;194;231",                  /* markup.quote: pink */
	";38;2;137;180;250",                  /* markup.math: blue */
	";38;2;180;190;254",                  /* markup.link: lavender */
	";38;2;180;190;254",                  /* markup.link.label: lavender */
	";3;4;38;2;137;180;250",              /* markup.link.url: blue, italic underline */
	";38;2;166;227;161",                  /* markup.raw: green */
	";38;2;166;227;161",                  /* markup.raw.block: green */
	";38;2;148;226;213",                  /* markup.list: teal */
	";38;2;166;227;161",                  /* markup.list.checked: green */
	";38;2;127;132;156",                  /* markup.list.unchecked: overlay1 */
	";38;2;166;227;161",                  /* diff.plus: green */
	";38;2;243;139;168",                  /* diff.minus: red */
	";38;2;137;180;250",                  /* diff.delta: blue */
	";38;2;137;180;250",                  /* tag: blue */
	";38;2;137;180;250",                  /* tag.builtin: blue */
	";3;38;2;249;226;175",                /* tag.attribute: yellow, italic */
	";38;2;148;226;213",                  /* tag.delimiter: teal */
};

static void style(const Cell *x)
{
	outs(x->attr == ATTR_REV ? "\x1b[0;7" : x->attr == ATTR_DIM ? "\x1b[0;2" : "\x1b[0");
	if (x->face < FACE_COUNT) outs(face_fg[x->face]);
	outs("m");
}

static void tty_present(const Grid *g, Grid *shown)
{
	char b[64];
	outs("\x1b[?2026h\x1b[?25l");
	if (shown->rows != g->rows || shown->cols != g->cols) {
		if (grid_resize(shown, g->rows, g->cols)) { outs("\x1b[?25h\x1b[?2026l"); flush(); return; }
		outs("\x1b[0m\x1b[H\x1b[2J");
	}
	int a = -1, pr = -1, pc = -1;   /* a: the style sent last, attr and face */
	for (int r = 0; r < g->rows; r++)
		for (int c = 0; c < g->cols; c++) {
			const Cell *x = &g->c[r * g->cols + c];
			Cell *y = &shown->c[r * g->cols + c];
			if (grid_cell_eq(x, y)) continue;
			*y = *x;
			if (!x->len) continue;
			int wide = c + 1 < g->cols && !x[1].len;
			if ((x->face << 8 | x->attr) != a) { a = x->face << 8 | x->attr; style(x); }
			/* The terminal may give a non-ASCII character another width
			   than the grid does (East Asian ambiguous, newer emoji), so
			   Neovim's print_cell_at_pos() clears both cells of a wide one
			   first and moves the cursor again after it.  Its test is
			   "ambiguous width"; uni.h has none, so this takes every
			   non-ASCII character, which costs bytes, never cells. */
			if (wide && x->ch[0] >= 0x80) {
				if (r != pr || c != pc) { snprintf(b, sizeof b, "\x1b[%d;%dH", r + 1, c + 1); outs(b); }
				outs("  ");
				pr = -1;
			}
			if (r != pr || c != pc) { snprintf(b, sizeof b, "\x1b[%d;%dH", r + 1, c + 1); outs(b); }
			out(x->ch, x->len);
			pr = x->ch[0] < 0x80 ? r : -1;
			pc = c + 1 + wide;
		}
	snprintf(b, sizeof b, "\x1b[%d;%dH\x1b[?25h\x1b[?2026l", g->crow + 1, g->ccol + 1);
	outs(b);
	flush();
}

static void tty_suspend(void)
{
	cooked();
	kill(0, SIGTSTP);
	raw();
	/* a resize while stopped went to the shell, the foreground job then */
	if (write(sigpipe[1], "w", 1) < 0) {}
}

/* the terminal has no clipboard or mouse here; OSC 52 and SGR mouse reports
   are for later */
static int  tty_clip_set(const char *s, size_t n) { (void)s; (void)n; return -1; }
static long tty_clip_get(char *buf, size_t max) { (void)buf; (void)max; return -1; }
static void tty_mouse(int *row, int *col) { *row = *col = 0; }

const Backend backend_tty = {
	tty_open, tty_close, tty_fds, tty_input, tty_size, tty_ready,
	tty_present, tty_suspend, tty_clip_set, tty_clip_get, tty_mouse
};
