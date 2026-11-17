/* Workload: load FILE, do N random edits (logged for undo), undo them all, check the text is back. */
#include "pt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
typedef struct { size_t pos, ins, del; size_t delat; } Edit;   /* deleted bytes stored in a log buffer */
static unsigned long long rng = 88172645463325252ull;
static unsigned long long nx(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
static double now(void) { struct timespec s; clock_gettime(CLOCK_MONOTONIC, &s); return s.tv_sec + s.tv_nsec / 1e9; }
static unsigned long long fnv(const char *s, size_t n) { unsigned long long h = 1469598103934665603ull; for (size_t i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 1099511628211ull; } return h; }
int main(int argc, char **argv) {
  FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); size_t n = ftell(f); rewind(f);
  char *orig = malloc(n); if (fread(orig, 1, n, f) != n) return 1; fclose(f);
  long N = atol(argv[2]);
  unsigned long long h0 = fnv(orig, n);
  double t0 = now();
  PT t; pt_init(&t, orig, n);
  Edit *log = malloc(N * sizeof *log); char *dl = malloc(N * 16); size_t dlen = 0;
  for (long i = 0; i < N; i++) {
    size_t pos = nx() % (t.len + 1);
    if (nx() & 1) { size_t k = 1 + nx() % 10; char s[10]; for (size_t j = 0; j < k; j++) s[j] = 'a' + nx() % 26;
      pt_insert(&t, pos, s, k); log[i] = (Edit){pos, k, 0, 0}; }
    else { size_t k = 1 + nx() % 10; if (k > t.len - pos) k = t.len - pos;
      pt_delete(&t, pos, k, dl + dlen); log[i] = (Edit){pos, 0, k, dlen}; dlen += k; }
  }
  double t1 = now();
  for (long i = N - 1; i >= 0; i--) {
    if (log[i].ins) pt_delete(&t, log[i].pos, log[i].ins, NULL);
    else pt_insert(&t, log[i].pos, dl + log[i].delat, log[i].del);
  }
  double t2 = now();
  char *out = malloc(t.len); pt_read(&t, 0, out, t.len);
  printf("C   edits=%ld edit_s=%.3f undo_s=%.3f pieces_after_undo=%zu ok=%d\n", N, t1 - t0, t2 - t1, t.n, t.len == n && fnv(out, n) == h0);
  return 0;
}
