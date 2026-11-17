/* split-invariance of backend_input on arbitrary bytes: the keys must not depend on how many slots each call offers */
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include "check.h"
#include "backend.h"
#include "keys.h"
static unsigned char in[1<<15];
static int a[1<<16], b[1<<16];
static int feed(size_t n, int mode, int *got)
{
	int p[2], saved = dup(0), ng = 0, idle = 0;
	CHECK(!pipe(p));
	CHECK(write(p[1], in, n) == (ssize_t)n);
	close(p[1]); dup2(p[0], 0); close(p[0]);
	for (;;) {
		int max = mode == 0 ? 256 : mode == 1 ? 1 + (int)rnd(8) : 1 + (int)rnd(300);
		int r = backend_input(got + ng, max);
		CHECK(r >= 0 && r <= max);
		ng += r;
		if (!r) { if (++idle > 20) break; } else idle = 0;
	}
	for (int r; (r = backend_input(got + ng, 300)) > 0;) ng += r;   /* a tail kept for want of slots */
	CHECK(backend_input(got + ng, 300) == 0);
	/* drain anything left with big slots */
	dup2(saved, 0); close(saved);
	return ng;
}
int main(int argc, char **argv)
{
	int rounds = argc > 1 ? atoi(argv[1]) : 2000, bad = 0;
	for (int round = 0; round < rounds; round++) {
		size_t n = 1 + rnd(round % 2 ? 40 : 3000);
		for (size_t i = 0; i < n; i++) {
			unsigned t = rnd(12);
			in[i] = t < 2 ? 27 : t < 4 ? '[' : t < 5 ? (unsigned char)('0' + rnd(10)) : t < 6 ? ';'
			      : t < 8 ? (unsigned char)(0x80 + rnd(0x40)) : t < 10 ? (unsigned char)(0xC0 + rnd(0x40))
			      : (unsigned char)rnd(128);
		}
		int na = feed(n, 0, a), nb = feed(n, 1 + (int)rnd(2), b);
		if (na != nb || memcmp(a, b, (size_t)na * sizeof *a)) {
			int i = 0; while (i < na && i < nb && a[i] == b[i]) i++;
			if (bad++ < 5) { fprintf(stderr, "round %d (n=%zu): %d vs %d keys, first difference at %d: %x vs %x\n", round, n, na, nb, i, i<na?a[i]:-1, i<nb?b[i]:-1);
			  if (n < 60) { for (size_t j = 0; j < n; j++) fprintf(stderr, "%02x ", in[j]); fprintf(stderr, "\n"); } }
		}
	}
	printf("%d of %d rounds differ\n", bad, rounds);
	return bad != 0;
}
