/* The rope's benchmark, the same workload as ropey.rs: load FILE, N random
   edits of 1 to 10 bytes (logged), N line lookups each way, undo all edits,
   check the text came back.  Build: see run.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "text.h"

typedef struct { size_t pos, ins, del, at; } Edit;
static unsigned long long rng = 88172645463325252ull;
static unsigned long long nx(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
static double now(void) { struct timespec s; clock_gettime(CLOCK_MONOTONIC, &s); return s.tv_sec + s.tv_nsec / 1e9; }
static unsigned long long fnv(const char *s, size_t n)
{
	unsigned long long h = 1469598103934665603ull;
	for (size_t i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 1099511628211ull; }
	return h;
}

static double tm[5];

/* The workload alone, outside main so callgrind can count it by itself
   (--toggle-collect=run): file reading and the hash are left out. */
__attribute__((noinline)) static Text *run(const char *orig, size_t n, long N, size_t *sum)
{
	tm[0] = now();
	Text *t = text_new(orig, n);
	tm[1] = now();
	Edit *log = malloc((size_t)N * sizeof *log);
	char *dl = malloc((size_t)N * 10);
	size_t dlen = 0;
	for (long i = 0; i < N; i++) {
		size_t len = text_len(t), pos = nx() % (len + 1);
		if (nx() & 1) {
			size_t k = 1 + nx() % 10; char s[10];
			for (size_t j = 0; j < k; j++) s[j] = (char)('a' + nx() % 26);
			text_insert(t, pos, s, k); log[i] = (Edit){pos, k, 0, 0};
		} else {
			size_t k = 1 + nx() % 10; if (k > len - pos) k = len - pos;
			text_read(t, pos, dl + dlen, k); text_delete(t, pos, k);
			log[i] = (Edit){pos, 0, k, dlen}; dlen += k;
		}
	}
	tm[2] = now();
	for (long i = 0; i < N; i++) {
		*sum += text_line_start(t, nx() % (text_lines(t) + 1));
		*sum += text_line_of(t, nx() % (text_len(t) + 1));
	}
	tm[3] = now();
	for (long i = N - 1; i >= 0; i--) {
		if (log[i].ins) text_delete(t, log[i].pos, log[i].ins);
		else text_insert(t, log[i].pos, dl + log[i].at, log[i].del);
	}
	tm[4] = now();
	free(log); free(dl);
	return t;
}

int main(int argc, char **argv)
{
	if (argc < 3) return 2;
	FILE *f = fopen(argv[1], "rb");
	if (!f) return 2;
	fseek(f, 0, SEEK_END); size_t n = (size_t)ftell(f); rewind(f);
	char *orig = malloc(n);
	if (fread(orig, 1, n, f) != n) return 1;
	fclose(f);
	long N = atol(argv[2]);
	unsigned long long h0 = fnv(orig, n);
	size_t sum = 0;
	Text *t = run(orig, n, N, &sum);
	char *out = malloc(n);
	int ok = text_len(t) == n && text_read(t, 0, out, n) == n && fnv(out, n) == h0;
	printf("C    load_s=%.3f edit_s=%.3f lines_s=%.3f undo_s=%.3f sum=%zu ok=%d\n",
	       tm[1] - tm[0], tm[2] - tm[1], tm[3] - tm[2], tm[4] - tm[3], sum, ok);
	text_free(t);
	return !ok;
}
