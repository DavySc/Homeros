/* Render.tla's rules, on the real editor in a pseudo-terminal.  The editor
   (src/main.c with every module) runs under a small job-control shell; this
   test types 3,000 keys into it as fast as the terminal takes them, reads
   what it draws through a model terminal (test_tty_vt.h), and checks each
   frame against the screens of every whole-key prefix, computed here by the
   same core:
     NoTornFrame  every frame is the screen after a whole number of keys,
                  in order, each inside one synchronized update;
     Responsive   at most Batch (64) keys between frames while input waits,
                  and once input stops the last frame is the final screen;
   then the same after a resize (SIGWINCH) and after Ctrl-Z and fg (the
   terminal is redrawn whole). */
#define _XOPEN_SOURCE 700     /* posix_openpt */
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include "check.h"
#define main homeros_main
#include "../src/main.c"
#undef main
#include "uni.h"
#include "test_tty_vt.h"

enum { NKEYS = 3000, BATCH_RULE = 64 };

static unsigned char outb[1 << 24];
static size_t outn;
static int keys[NKEYS + 2];
static unsigned long long want[NKEYS + 2];

static unsigned long long hash(const Cell *c, int rows, int cols, int crow, int ccol)
{
	unsigned long long h = 1469598103934665603ULL;
	for (int i = 0; i < rows * cols; i++) {
		unsigned char b[6] = { c[i].len, c[i].attr, 0, 0, 0, 0 };
		memcpy(b + 2, c[i].ch, c[i].len);
		for (int j = 0; j < 2 + c[i].len; j++) h = (h ^ b[j]) * 1099511628211ULL;
	}
	return (h ^ (unsigned)(crow * 1000 + ccol)) * 1099511628211ULL;
}

static unsigned long long grid_hash(const Grid *g) { return hash(g->c, g->rows, g->cols, g->crow, g->ccol); }

static int has(size_t from, size_t to, const char *s)
{
	for (size_t n = strlen(s), i = from; i + n <= to; i++) if (!memcmp(outb + i, s, n)) return 1;
	return 0;
}

/* the terminal agrees with the core's widths */
static int width(const unsigned char *s, int n) { return uni_width(uni_cp(s, (size_t)n, 0)); }

/* Reads the editor's output until it has been quiet for ms (or exits). */
static int pump(int m, const unsigned char *in, size_t n, int ms)
{
	size_t sent = 0;
	for (;;) {
		struct pollfd p = { .fd = m, .events = POLLIN | (sent < n ? POLLOUT : 0) };
		int r = poll(&p, 1, sent < n ? 1000 : ms);
		CHECK(r >= 0);
		if (!r) { CHECK(sent == n); return 0; }
		if (p.revents & POLLOUT) {
			ssize_t w = write(m, in + sent, n - sent > 64 ? 64 : n - sent);
			if (w > 0) sent += (size_t)w;
		}
		if (p.revents & (POLLIN | POLLHUP | POLLERR)) {
			ssize_t k = read(m, outb + outn, sizeof outb - outn);
			if (k <= 0) return -1;            /* the editor exited */
			outn += (size_t)k;
			CHECK(outn < sizeof outb);
		}
	}
}

/* Feeds output [from, to) to the model; returns the hash after its last
   frame, and checks frames of phase A against the prefixes. */
static unsigned long long frames(Vt *t, size_t from, size_t to, int phase_a, int *nframes)
{
	unsigned long long last = 0;
	int at = 0;
	for (size_t i = from; i < to; i++) {
		int f = t->frames;
		vt_feed(t, outb + i, 1);
		if (t->bad) { fprintf(stderr, "byte %zu: %s\n", i, t->why); CHECK(0); }
		if (t->frames == f) continue;
		last = hash(t->c, t->rows, t->cols, t->r, t->col);
		++*nframes;
		if (!phase_a) continue;
		int j = at;
		while (j <= NKEYS + 1 && want[j] != last) j++;
		if (j > NKEYS + 1) { fprintf(stderr, "frame %d shows no whole-key state after key %d\n", *nframes, at); CHECK(0); }
		if (j - at > BATCH_RULE) { fprintf(stderr, "frame %d: %d keys since the last frame\n", *nframes, j - at); CHECK(0); }
		at = j;
	}
	return last;
}

int main(void)
{
	char path[] = "/tmp/homeros-render-XXXXXX";
	int fd = mkstemp(path);
	CHECK(fd >= 0);
	close(fd);

	/* insert 3,000 characters (lines of 25, some two-byte), then Esc */
	keys[0] = 'i';
	unsigned char in[4 * NKEYS], *p = in;
	*p++ = 'i';
	for (int i = 1; i <= NKEYS; i++) {
		int k = i == NKEYS ? KEY_ESC : i % 26 == 0 ? KEY_ENTER : i % 7 == 0 ? 0xE9 : 'a' + i % 26;
		keys[i] = k;
		if (k == 0xE9) { *p++ = 0xC3; *p++ = 0xA9; } else *p++ = (unsigned char)k;
	}
	/* the screen after each prefix, from the same core */
	static Editor e2;
	Grid g = { 0 };
	CHECK(!core_open(&e2, path) && !grid_resize(&g, 24, 80));
	core_draw(&e2, &g);
	want[0] = grid_hash(&g);
	for (int i = 0; i <= NKEYS; i++) {
		keys_feed(&e2, keys[i], 0);
		core_draw(&e2, &g);
		want[i + 1] = grid_hash(&g);
	}
	CHECK(!grid_resize(&g, 30, 100));
	core_draw(&e2, &g);
	unsigned long long resized_want = grid_hash(&g);
	CHECK(!grid_resize(&g, 20, 90));
	core_draw(&e2, &g);
	unsigned long long stopped_want = grid_hash(&g);

	int m = posix_openpt(O_RDWR | O_NOCTTY);
	CHECK(m >= 0 && !grantpt(m) && !unlockpt(m));
	struct winsize ws = { .ws_row = 24, .ws_col = 80 };
	CHECK(!ioctl(m, TIOCSWINSZ, &ws));
	char *slave = ptsname(m);
	fflush(stdout);
	pid_t sh = fork();
	CHECK(sh >= 0);
	if (!sh) {
		/* a job-control shell: a session on the terminal, the editor its
		   foreground job, continued (fg) whenever it stops; while it is
		   stopped the second time, the shell has the terminal and the
		   window is resized to 20x90 (the shell gets that SIGWINCH) */
		setsid();
		signal(SIGTTOU, SIG_IGN);
		int s = open(slave, O_RDWR);
		if (s < 0) _exit(10);
		pid_t ed = fork();
		if (!ed) {
			setpgid(0, 0);
			tcsetpgrp(s, getpid());
			signal(SIGTTOU, SIG_DFL);
			dup2(s, 0); dup2(s, 1); dup2(s, 2);
			close(s); close(m);
			char *argv[] = { "homeros", path, NULL };
			_exit(homeros_main(2, argv));   /* _exit: the parent's allocations are not leaks */
		}
		setpgid(ed, ed);
		int st, stops = 0;
		while (waitpid(ed, &st, WUNTRACED) == ed)
			if (WIFSTOPPED(st)) {
				if (++stops == 2) {
					struct winsize w2 = { .ws_row = 20, .ws_col = 90 };
					tcsetpgrp(s, getpgrp());
					ioctl(s, TIOCSWINSZ, &w2);
					tcsetpgrp(s, ed);
				}
				kill(ed, SIGCONT);
			} else _exit(!WIFEXITED(st) ? 11 : WEXITSTATUS(st) ? WEXITSTATUS(st) : stops == 2 ? 0 : 13);
		_exit(12);
	}
	fcntl(m, F_SETFL, O_NONBLOCK);

	static Vt t;
	vt_init(&t, 24, 80, width);
	int nf = 0;
	/* the first frame, then the keys (typed earlier, the terminal would
	   echo them before raw mode) */
	while (!has(0, outn, "\x1b[?2026l")) CHECK(!pump(m, NULL, 0, 20));
	CHECK(!pump(m, in, (size_t)(p - in), 300));
	unsigned long long last = frames(&t, 0, outn, 1, &nf);
	if (last != want[NKEYS + 1]) { fprintf(stderr, "the last of %d frames is not the final screen\n", nf); CHECK(0); }
	CHECK(nf >= NKEYS / BATCH_RULE);

	/* resize: a terminal keeps or reflows its cells; the model forgets them,
	   so the whole screen must come again */
	size_t from = outn;
	ws = (struct winsize){ .ws_row = 30, .ws_col = 100 };
	CHECK(!ioctl(m, TIOCSWINSZ, &ws));
	CHECK(!pump(m, NULL, 0, 300));
	vt_resize(&t, 30, 100);
	CHECK(frames(&t, from, outn, 0, &nf) == resized_want);

	/* Ctrl-Z, then fg (the shell's exit status says the editor stopped
	   twice): the main screen comes back meanwhile */
	from = outn;
	CHECK(!pump(m, (const unsigned char *)"\x1a", 1, 300));
	CHECK(frames(&t, from, outn, 0, &nf) == resized_want);
	CHECK(has(from, outn, "\x1b[?1049l"));   /* it did leave the screen */

	/* Ctrl-Z, resized while stopped, fg: the last frame is the new size,
	   drawn whole (frames before it may still be the old size, which this
	   model cannot show) */
	from = outn;
	CHECK(!pump(m, (const unsigned char *)"\x1a", 1, 300));
	size_t at = outn;
	while (at > from && !has(at, outn, "\x1b[?2026h")) at--;
	vt_init(&t, 20, 90, width);
	vt_resize(&t, 20, 90);
	CHECK(frames(&t, at, outn, 0, &nf) == stopped_want);

	/* :q! leaves the terminal as it found it: cooked, main screen */
	CHECK(pump(m, (const unsigned char *)":q!\r", 4, 5000) == -1);
	int st;
	CHECK(waitpid(sh, &st, 0) == sh && WIFEXITED(st) && WEXITSTATUS(st) == 0);
	struct termios tio;
	CHECK(!tcgetattr(m, &tio) && (tio.c_lflag & ICANON) && (tio.c_lflag & ECHO));
	at = outn;
	while (at > 0 && !has(at, outn, "\x1b[?1049")) at--;
	CHECK(has(at, outn, "\x1b[?1049l"));
	core_free(&e2);
	grid_free(&g);
	unlink(path);
	printf("test_tty_render: %d keys in %d frames, each a whole-key screen at most %d keys apart; resize, Ctrl-Z and a resize while stopped redraw whole; the terminal is cooked again after :q!\n",
	       NKEYS + 1, nf, BATCH_RULE);
	return 0;
}
