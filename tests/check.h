/* Shared by the tests: a failed CHECK prints where and exits 1. */
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
static unsigned long long rng = 88172645463325252ULL;
static inline unsigned rnd(unsigned n) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (unsigned)(rng % n); }
