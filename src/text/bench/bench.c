/* The rope against ropey (rope.rs runs the same steps on ropey):
   open FILE, N random 1 to 10 byte edits logged for undo, undo them all and
   check the text is back, read it all out, then Q random line lookups each
   way.  Prints the seconds of each step.  Not built by make: run.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "text.h"

typedef struct { size_t pos, ins, del, at; } Edit;
static unsigned long long rng = 88172645463325252ull;
static unsigned long long nx(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
static double now(void) { struct timespec s; clock_gettime(CLOCK_MONOTONIC, &s); return s.tv_sec + s.tv_nsec / 1e9; }

int main(int argc, char **argv)
{
	if (argc != 4) { fprintf(stderr, "usage: bench FILE EDITS LOOKUPS\n"); return 2; }
	FILE *f = fopen(argv[1], "rb");
	if (!f) return 2;
	fseek(f, 0, SEEK_END); size_t n = ftell(f); rewind(f);
	char *orig = malloc(n), *out = malloc(n);
	if (!orig || !out || fread(orig, 1, n, f) != n) return 2;
	fclose(f);
	long N = atol(argv[2]), Q = atol(argv[3]);
	Edit *log = malloc(N * sizeof *log);
	char *dl = malloc(N * 10 + 1);
	size_t dn = 0, sink = 0;
	double t0 = now();
	Text *t = text_new(orig, n);
	if (!t) return 2;
	double t1 = now();
	for (long i = 0; i < N; i++) {
		size_t len = text_len(t), pos = nx() % (len + 1), k = 1 + nx() % 10;
		if (nx() & 1) {
			char s[10];
			for (size_t j = 0; j < k; j++) s[j] = 'a' + nx() % 26;
			text_insert(t, pos, s, k);
			log[i] = (Edit){ pos, k, 0, 0 };
		} else {
			if (k > len - pos) k = len - pos;
			text_read(t, pos, dl + dn, k);
			text_delete(t, pos, k);
			log[i] = (Edit){ pos, 0, k, dn };
			dn += k;
		}
	}
	double t2 = now();
	for (long i = N - 1; i >= 0; i--)
		if (log[i].ins) text_delete(t, log[i].pos, log[i].ins);
		else text_insert(t, log[i].pos, dl + log[i].at, log[i].del);
	double t3 = now();
	int ok = text_read(t, 0, out, n) == n && text_len(t) == n && !memcmp(out, orig, n);
	double t4 = now();
	size_t lines = text_lines(t);
	for (long i = 0; i < Q; i++) {
		sink += text_line_start(t, nx() % (lines + 1));
		sink += text_line_of(t, nx() % (n + 1));
	}
	double t5 = now();
	printf("C    open_s=%.3f edit_s=%.3f undo_s=%.3f read_s=%.3f lookup_s=%.3f ok=%d sink=%zu\n",
	       t1 - t0, t2 - t1, t3 - t2, t4 - t3, t5 - t4, ok, sink % 10);
	text_free(t);
	return !ok;
}
