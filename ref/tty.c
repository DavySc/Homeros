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

int backend_open(void)
{
	if (!isatty(0) || !isatty(1) || tcgetattr(0, &saved) || pipe(sigpipe)) return -1;
	fcntl(sigpipe[0], F_SETFL, O_NONBLOCK);
	fcntl(sigpipe[1], F_SETFL, O_NONBLOCK);
	struct sigaction sa = { .sa_handler = on_winch };
	sigaction(SIGWINCH, &sa, NULL);
	return raw();
}

void backend_close(void) { cooked(); }

int backend_fds(int fds[2]) { fds[0] = 0; fds[1] = sigpipe[0]; return 2; }

void backend_size(int *rows, int *cols)
{
	struct winsize ws;
	if (ioctl(1, TIOCGWINSZ, &ws) || !ws.ws_row || !ws.ws_col) { *rows = 24; *cols = 80; return; }
	*rows = ws.ws_row;
	*cols = ws.ws_col;
}

int backend_ready(void) { return 1; }   /* a terminal has no frame callback */

/* Bytes to keys: UTF-8 to code points, CSI arrows to KEY_*, other escape
   sequences dropped.  A sequence split across reads is a hypothesis-level
   gap the terminal package closes. */
int backend_input(int *k, int max)
{
	unsigned char b[256], c;
	int n = 0;
	while (read(sigpipe[0], &c, 1) == 1) if (!n) k[n++] = KEY_RESIZE;
	ssize_t r = read(0, b, sizeof b);
	for (ssize_t i = 0; i < r && n < max;) {
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
		if (len > 1 && i + len <= r) {
			v &= len == 2 ? 0x1F : len == 3 ? 0x0F : 0x07;
			for (int j = 1; j < len; j++) v = v * 64 + (b[i + j] & 0x3Fu);
		} else len = 1;
		k[n++] = (int)v;
		i += len;
	}
	return n;
}

static void attr(int a)
{
	outs(a == ATTR_REV ? "\x1b[0;7m" : a == ATTR_DIM ? "\x1b[0;2m" : "\x1b[0m");
}

void backend_present(const Grid *g, Grid *shown)
{
	char b[64];
	outs("\x1b[?2026h\x1b[?25l");
	if (shown->rows != g->rows || shown->cols != g->cols) {
		if (grid_resize(shown, g->rows, g->cols)) return;
		outs("\x1b[0m\x1b[H\x1b[2J");
	}
	int a = -1, pr = -1, pc = -1;
	for (int r = 0; r < g->rows; r++)
		for (int c = 0; c < g->cols; c++) {
			const Cell *x = &g->c[r * g->cols + c];
			Cell *y = &shown->c[r * g->cols + c];
			if (grid_cell_eq(x, y)) continue;
			*y = *x;
			if (!x->len) continue;
			if (r != pr || c != pc) { snprintf(b, sizeof b, "\x1b[%d;%dH", r + 1, c + 1); outs(b); }
			if (x->attr != a) attr(a = x->attr);
			out(x->ch, x->len);
			pr = r;
			pc = c + (c + 1 < g->cols && !g->c[r * g->cols + c + 1].len ? 2 : 1);
		}
	snprintf(b, sizeof b, "\x1b[0m\x1b[%d;%dH\x1b[?25h\x1b[?2026l", g->crow + 1, g->ccol + 1);
	outs(b);
	flush();
}

void backend_suspend(void)
{
	cooked();
	kill(0, SIGTSTP);
	raw();
}
