/* Terminal backend: raw mode, the alternate screen, changed cells only,
   inside a synchronized update (CSI ? 2026) so a frame is never shown half
   drawn.  A resize arrives through a self-pipe, so poll never misses it. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include "backend.h"
#include "keys.h"

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

/* Bytes to keys: UTF-8 to code points, CSI arrows to KEY_*, other escape
   sequences dropped.  A sequence split across reads is a hypothesis-level
   gap the terminal package closes. */
static int tty_input(int *k, int max)
{
	/* a character split by the end of a read waits here for its other bytes */
	static unsigned char b[256 + 3];
	static int held;
	unsigned char c;
	int n = 0;
	while (read(sigpipe[0], &c, 1) == 1) if (!n && max > 0) k[n++] = KEY_RESIZE;
	/* each byte gives at most one key, so never read more than there are
	   slots: what is not read stays in the terminal for the next call */
	int room = max - n - held;
	if (room > 256) room = 256;
	ssize_t r = room > 0 ? read(0, b + held, (size_t)room) : 0;
	r = (r > 0 ? r : 0) + held;
	held = 0;
	for (ssize_t i = 0; i < r;) {
		if (b[i] == 27 && i + 1 < r && b[i + 1] == '[') {
			ssize_t j = i + 2;
			while (j < r && (b[j] < 0x40 || b[j] > 0x7e)) j++;
			if (j < r && j == i + 2 && b[j] >= 'A' && b[j] <= 'D')
				k[n++] = (int)(b[j] == 'A' ? KEY_UP : b[j] == 'B' ? KEY_DOWN : b[j] == 'C' ? KEY_RIGHT : KEY_LEFT);
			i = j + 1;
			continue;
		}
		unsigned v = b[i];
		int len = v < 0x80 ? 1 : v >= 0xF0 ? 4 : v >= 0xE0 ? 3 : v >= 0xC0 ? 2 : 1;
		if (len > 1 && i + len > r) {
			ssize_t j = i + 1;
			while (j < r && (b[j] & 0xC0) == 0x80) j++;
			if (j == r) { held = (int)(r - i); memmove(b, b + i, (size_t)held); break; }
		}
		if (len > 1 && i + len <= r) {
			v &= len == 2 ? 0x1F : len == 3 ? 0x0F : 0x07;
			for (int j = 1; j < len; j++) v = v * 64 + (b[i + j] & 0x3Fu);
		} else len = 1;
		k[n++] = (int)v;
		i += len;
	}
	return n;
}


/* the SGR for a cell: its attribute, then its face from grid.h's theme */
static void style(const Cell *x)
{
	char s[64];
	const Face *f = &faces[x->face < FACE_COUNT ? x->face : FACE_NONE];
	int n = snprintf(s, sizeof s, "\x1b[0%s%s%s%s%s", x->attr == ATTR_REV ? ";7" : x->attr == ATTR_DIM ? ";2" : "",
	                 f->style & FACE_BOLD ? ";1" : "", f->style & FACE_ITALIC ? ";3" : "",
	                 f->style & FACE_UNDERLINE ? ";4" : "", f->style & FACE_STRIKE ? ";9" : "");
	if (f->style & FACE_FG) n += snprintf(s + n, sizeof s - n, ";38;2;%d;%d;%d", f->fg[0], f->fg[1], f->fg[2]);
	if (f->style & FACE_BG) n += snprintf(s + n, sizeof s - n, ";48;2;%d;%d;%d", f->bg[0], f->bg[1], f->bg[2]);
	s[n] = 'm'; s[n + 1] = 0;
	outs(s);
}

static void tty_present(const Grid *g, Grid *shown)
{
	char b[64];
	outs("\x1b[?2026h\x1b[?25l");
	if (shown->rows != g->rows || shown->cols != g->cols) {
		if (grid_resize(shown, g->rows, g->cols)) return;
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
			if (r != pr || c != pc) { snprintf(b, sizeof b, "\x1b[%d;%dH", r + 1, c + 1); outs(b); }
			if ((x->face << 8 | x->attr) != a) { a = x->face << 8 | x->attr; style(x); }
			out(x->ch, x->len);
			pr = r;
			pc = c + (c + 1 < g->cols && !g->c[r * g->cols + c + 1].len ? 2 : 1);
		}
	snprintf(b, sizeof b, "\x1b[0m\x1b[%d;%dH\x1b[?25h\x1b[?2026l", g->crow + 1, g->ccol + 1);
	outs(b);
	flush();
}

static void tty_suspend(void)
{
	cooked();
	kill(0, SIGTSTP);
	raw();
}

/* the terminal has no clipboard or mouse here; package 6 may add OSC 52 and
   SGR mouse reports */
static int  tty_clip_set(const char *s, size_t n) { (void)s; (void)n; return -1; }
static long tty_clip_get(char *buf, size_t max) { (void)buf; (void)max; return -1; }
static void tty_mouse(int *row, int *col) { *row = *col = 0; }

const Backend backend_tty = {
	tty_open, tty_close, tty_fds, tty_input, tty_size, tty_ready,
	tty_present, tty_suspend, tty_clip_set, tty_clip_get, tty_mouse
};
