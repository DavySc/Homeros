/* Terminal input: random streams of keys (ASCII, UTF-8, arrows, other CSI
   sequences, Esc followed by a key) cut into random reads and random numbers
   of key slots must give the same keys as the stream itself, whether a cut
   falls inside a key in our reads or between the terminal's writes (a
   writer that pauses less than 'ttimeoutlen').  An Esc alone is Esc after
   'ttimeoutlen' (Neovim: 50 ms), and an Esc followed by a key at once is
   two keys without that wait. */
#include <poll.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "check.h"
#include "backend.h"
#include "keys.h"

static unsigned char in[1 << 16];
static int want[1 << 16], got[1 << 16];
static double first;    /* when the first key came */

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

/* appends one random key's bytes to in and its key (or none) to want */
static void token(size_t *n, int *nw, int last)
{
	static const char *const csi[] = { "\x1b[1;5A", "\x1b[3~", "\x1b[200~", "\x1b[?1u", "\x1b[H" };
	unsigned t = rnd(10);
	if (t < 4) {                       /* ASCII, not Esc */
		unsigned char c = (unsigned char)(rnd(4) ? 'a' + rnd(26) : rnd(27));
		in[(*n)++] = c; want[(*nw)++] = c;
	} else if (t < 6) {                /* UTF-8, 2 to 4 bytes */
		static const char *const u[] = { "\xc3\xa9", "\xe2\x88\x80", "\xf0\x9d\x93\x9d", "\xe6\xbc\xa2" };
		static const int cp[] = { 0xE9, 0x2200, 0x1D4DD, 0x6F22 };
		unsigned i = rnd(4);
		memcpy(in + *n, u[i], strlen(u[i])); *n += strlen(u[i]); want[(*nw)++] = cp[i];
	} else if (t < 8) {                /* arrows */
		unsigned i = rnd(4);
		memcpy(in + *n, "\x1b[", 2); in[*n + 2] = (unsigned char)("ABCD"[i]); *n += 3;
		want[(*nw)++] = (int[]){ KEY_UP, KEY_DOWN, KEY_RIGHT, KEY_LEFT }[i];
	} else if (t < 9) {                /* a sequence that is no key: dropped */
		const char *s = csi[rnd(5)];
		memcpy(in + *n, s, strlen(s)); *n += strlen(s);
	} else if (last) {                 /* Esc at the very end: Esc after the wait */
		in[(*n)++] = 27; want[(*nw)++] = 27;
	} else {                           /* Esc then a letter, as a fast typist */
		in[(*n)++] = 27; want[(*nw)++] = 27;
		in[*n] = (unsigned char)('a' + rnd(26)); want[(*nw)++] = in[(*n)++];
	}
}

/* Feeds in[0..n) through a pipe as stdin, reading with random slot counts.
   Fast: written at once, then closed.  Slow: written in random pieces with
   pauses up to 5 ms, then held open 200 ms, so an Esc at the end is ended
   by the wait, not by the end of the input. */
static int feed(size_t n, int slow)
{
	int p[2], saved = dup(0), ng = 0;
	pid_t w = -1;
	CHECK(!pipe(p));
	if (!slow) CHECK(write(p[1], in, n) == (ssize_t)n);
	else if (!(w = fork())) {
		close(p[0]);
		for (size_t i = 0; i < n;) {
			size_t k = 1 + rnd(8);
			if (k > n - i) k = n - i;
			if (write(p[1], in + i, k) != (ssize_t)k) _exit(1);
			i += k;
			usleep(rnd(5000));
		}
		usleep(200000);
		_exit(0);
	}
	CHECK(w != 0);
	close(p[1]);
	dup2(p[0], 0);
	close(p[0]);
	for (;;) {
		struct pollfd q = { .fd = 0, .events = POLLIN };
		CHECK(poll(&q, 1, -1) == 1);
		/* at the end, slots for everything: a tail kept for want of
		   slots is the caller's to collect */
		int max = q.revents & POLLIN ? 1 + (int)rnd(rnd(2) ? 8 : 300) : 300;
		int r = backend_input(got + ng, max);
		CHECK(r >= 0 && r <= max);
		if (!ng && r) first = now();
		ng += r;
		if (!r && !(q.revents & POLLIN)) break;
	}
	int st;
	if (slow) CHECK(waitpid(w, &st, 0) == w && WIFEXITED(st) && !WEXITSTATUS(st));
	dup2(saved, 0);
	close(saved);
	return ng;
}

static void same(int ng, int nw, int round)
{
	if (ng == nw && !memcmp(got, want, (size_t)nw * sizeof *got)) return;
	int i = 0;
	while (i < ng && i < nw && got[i] == want[i]) i++;
	fprintf(stderr, "round %d: %d keys, want %d; first difference at %d: %d, want %d\n",
	        round, ng, nw, i, i < ng ? got[i] : -1, i < nw ? want[i] : -1);
	CHECK(0);
}

int main(void)
{
	for (int round = 0; round < 400; round++) {
		size_t n = 0;
		int nw = 0, keys = 1 + (int)rnd(round < 20 ? 30 : 2000);
		for (int k = 0; k < keys; k++) token(&n, &nw, k == keys - 1);
		same(feed(n, round < 20), nw, round);
	}
	/* an Esc alone waits 'ttimeoutlen', and only then */
	for (int i = 0; i < 5; i++) {
		in[0] = 27; want[0] = 27;
		double t = now();
		same(feed(1, 1), 1, -1);
		double esc = first - t;
		CHECK(esc >= 45 && esc < 500);
		memcpy(in, "\x1bx", 2); want[1] = 'x';
		t = now();
		same(feed(2, 0), 2, -1);
		CHECK(first - t < 40);
	}
	puts("test_tty_input: 400 random key streams cut anywhere give the same keys; Esc alone waits 50 ms, Esc then a key does not");
	return 0;
}
