/* Terminal input: random streams of keys (ASCII, UTF-8, arrows, Delete, other CSI
   sequences, Esc followed by a key, bytes that are not UTF-8) cut into
   random reads and random numbers of key slots must give the same keys as
   the stream itself, whether a cut falls inside a key in our reads or
   between the terminal's writes (a writer that pauses less than
   'ttimeoutlen').  The reader stops once poll says nothing is readable, so
   a key kept inside the backend while poll sleeps is a key lost.  Arbitrary
   bytes give the same keys whatever the slot counts; an unfinished sequence
   as long as the buffer does not stall.  An Esc alone is Esc after
   'ttimeoutlen' (Neovim: 50 ms), and an Esc followed by a key at once is
   two keys without that wait. */
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "check.h"
#include "backend.h"
#include "keys.h"

static unsigned char in[1 << 16];
static int want[1 << 16], got[1 << 16], got2[1 << 16];
static double first;    /* when the first key came */

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

static void put(size_t *n, const char *s) { memcpy(in + *n, s, strlen(s)); *n += strlen(s); }

/* appends one random key's bytes to in and its keys (or none) to want */
static void token(size_t *n, int *nw, int last)
{
	static const char *const csi[] = { "\x1b[1;5A", "\x1b[3;5~", "\x1b[200~", "\x1b[?1u", "\x1b[H" };
	/* not UTF-8: overlong, surrogate, past U+10FFFF (0x110000 would be
	   KEY_UP), a lead cut by another lead; each byte is U+FFFD */
	static const char *const bad[] = { "\xf4\x90\x80\x80", "\xf4\x90\x80\x84", "\xed\xa0\x80", "\xc0\x80",
	                                   "\xc1\xa1", "\xf7\xbf\xbf\xbf", "\xe0\x80\xaf", "\xc3", "\xff" };
	unsigned t = rnd(11);
	if (t < 4) {                       /* ASCII, not Esc */
		unsigned char c = (unsigned char)(rnd(4) ? 'a' + rnd(26) : rnd(27));
		in[(*n)++] = c; want[(*nw)++] = c;
	} else if (t < 6) {                /* UTF-8, 2 to 4 bytes */
		static const char *const u[] = { "\xc3\xa9", "\xe2\x88\x80", "\xf0\x9d\x93\x9d", "\xe6\xbc\xa2", "\xf4\x8f\xbf\xbf" };
		static const int cp[] = { 0xE9, 0x2200, 0x1D4DD, 0x6F22, 0x10FFFF };
		unsigned i = rnd(5);
		put(n, u[i]); want[(*nw)++] = cp[i];
	} else if (t < 8) {                /* arrows and Delete */
		unsigned i = rnd(5);
		put(n, (const char *[]){ "\x1b[A", "\x1b[B", "\x1b[C", "\x1b[D", "\x1b[3~" }[i]);
		want[(*nw)++] = (int[]){ KEY_UP, KEY_DOWN, KEY_RIGHT, KEY_LEFT, KEY_DEL }[i];
	} else if (t < 9) {                /* a sequence that is no key: dropped */
		put(n, csi[rnd(5)]);
	} else if (t < 10) {
		const char *s = bad[rnd(9)];
		put(n, s);
		for (size_t i = 0; s[i]; i++) want[(*nw)++] = 0xFFFD;
	} else if (last) {                 /* Esc at the very end: Esc after the wait */
		in[(*n)++] = 27; want[(*nw)++] = 27;
	} else {                           /* Esc then a letter, as a fast typist */
		in[(*n)++] = 27; want[(*nw)++] = 27;
		in[*n] = (unsigned char)('a' + rnd(26)); want[(*nw)++] = in[(*n)++];
	}
}

/* Feeds in[0..n) through a pipe as stdin, calling backend_input with slots
   from slots() only while poll says stdin is readable; stops when nothing
   is (fast: written whole first) or nothing was for 100 ms (slow: written
   in random pieces with pauses up to 5 ms).  The writer stays open
   meanwhile, so the end of the input never ends a wait.  drain: at the end, one call with 300 slots (for slot counts
   below a kept tail's length, which the editor never uses). */
static int feed(size_t n, int slow, int (*slots)(void), int drain, int *out)
{
	int p[2], saved = dup(0), ng = 0, idle = 0;
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
		usleep(300000);
		_exit(0);
	}
	CHECK(w != 0);
	fcntl(p[0], F_SETFL, O_NONBLOCK);  /* as a raw terminal (VMIN 0): a read never waits */
	dup2(p[0], 0);
	close(p[0]);
	if (slow) close(p[1]);
	for (;;) {
		struct pollfd q = { .fd = 0, .events = POLLIN };
		CHECK(poll(&q, 1, slow ? 100 : 0) >= 0);
		if (!(q.revents & POLLIN)) break;
		int max = slots(), r = backend_tty.input(out + ng, max);
		CHECK(r >= 0 && r <= max);
		if (!ng && r) first = now();
		ng += r;
		idle = r ? 0 : idle + 1;
		CHECK(idle < 10000);           /* readable, yet no key comes */
	}
	if (drain) ng += backend_tty.input(out + ng, 300);
	if (!slow) close(p[1]);
	int st;
	if (slow) CHECK(waitpid(w, &st, 0) == w && WIFEXITED(st) && !WEXITSTATUS(st));
	dup2(saved, 0);
	close(saved);
	return ng;
}

/* at least 2: an unfinished key at the very end is one byte */
static int few(void) { return 2 + (int)rnd(rnd(2) ? 7 : 299); }
static int all(void) { return 256; }
static int any(void) { return 1 + (int)rnd(300); }

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
		same(feed(n, round < 20, few, 0, got), nw, round);
	}
	/* arbitrary bytes: the keys do not depend on the slot counts */
	for (int round = 0; round < 150; round++) {
		size_t n = 1 + rnd(round % 2 ? 40 : 3000);
		for (size_t i = 0; i < n; i++) {
			unsigned t = rnd(12);
			in[i] = t < 2 ? 27 : t < 4 ? '[' : t < 5 ? (unsigned char)('0' + rnd(10)) : t < 6 ? ';'
			      : t < 8 ? (unsigned char)(0x80 + rnd(0x40)) : t < 10 ? (unsigned char)(0xC0 + rnd(0x40))
			      : (unsigned char)rnd(128);
		}
		int na = feed(n, 0, all, 1, got2), nb = feed(n, 0, any, 1, got);
		memcpy(want, got2, (size_t)na * sizeof *want);
		same(nb, na, round);
	}
	/* an unfinished sequence as long as the buffer: its bytes, no stall */
	size_t n = 0;
	int nw = 0;
	put(&n, "\x1b[");
	want[nw++] = 27; want[nw++] = '[';
	for (int i = 0; i < 300; i++) { in[n++] = '1'; want[nw++] = '1'; }
	in[n++] = 'x'; want[nw++] = 'x';
	same(feed(n, 0, all, 0, got), nw, -1);
	/* an Esc alone waits 'ttimeoutlen', and only then */
	for (int i = 0; i < 5; i++) {
		in[0] = 27; want[0] = 27;
		double t = now();
		same(feed(1, 1, few, 0, got), 1, -1);
		double esc = first - t;
		CHECK(esc >= 45 && esc < 500);
		memcpy(in, "\x1bx", 2); want[1] = 'x';
		t = now();
		same(feed(2, 0, few, 0, got), 2, -1);
		CHECK(first - t < 40);
	}
	puts("test_tty_input: 400 random key streams cut anywhere give the same keys, bytes that are not UTF-8 give U+FFFD, "
	     "150 byte soups do not depend on slot counts, a 302-byte sequence does not stall; Esc alone waits 50 ms, Esc then a key does not");
	return 0;
}
