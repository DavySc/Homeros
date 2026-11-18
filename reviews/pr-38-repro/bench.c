/* PR 38 review: time sha256() of undo.c over a 10 MB Lean-like text (same generator as
   tuning/bench/kb.c) and over a 50 KB one.  Build with -DUNDO='"undo_old.c"' or undo_new.c. */
#include UNDO
#include <time.h>
static double now(void) { struct timespec s; clock_gettime(CLOCK_MONOTONIC, &s); return s.tv_sec * 1e3 + s.tv_nsec / 1e6; }
static unsigned long long r = 88172645463325252ull;
static unsigned long long nx(void) { r ^= r << 13; r ^= r >> 7; r ^= r << 17; return r; }
int main(int argc, char **argv)
{
	size_t N = argc > 1 ? (size_t)atol(argv[1]) : 10u << 20; int reps = argc > 2 ? atoi(argv[2]) : 1;
	unsigned char *b = malloc(N);
	const char *sym[] = { "∀", "→", "ℕ", "𝓝", "≤", "α", "⟨", "⟩" };
	size_t i = 0;
	while (i < N - 8) { unsigned k = nx() % 100; if (k < 5) { const char *s = sym[nx() % 8]; size_t l = strlen(s); memcpy(b + i, s, l); i += l; } else if (k < 7) b[i++] = '\n'; else b[i++] = 'a' + nx() % 26; }
	while (i < N) b[i++] = 'x';
	Text *t = text_new((char *)b, N); unsigned char o[32];
	double t0 = now();
	for (int k = 0; k < reps; k++) sha256(t, o);
	printf("%.3f %02x%02x%02x%02x\n", (now() - t0) / reps, o[0], o[1], o[2], o[3]);
	return 0;
}
