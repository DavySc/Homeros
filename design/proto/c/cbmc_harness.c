/* CBMC: for every original text up to 4 bytes, every insert (pos, 1..3 bytes)
   followed by deleting what was inserted gives back the original text, and
   no operation reads or writes out of bounds. */
#include "pt.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
size_t nondet_size(void); char nondet_char(void);
int main(void) {
  char orig[4]; size_t n = N_ORIG;
  for (int i = 0; i < 4; i++) orig[i] = nondet_char();
  char s[3]; size_t k = N_INS;
  for (int i = 0; i < 3; i++) s[i] = nondet_char();
  size_t pos = nondet_size(); __CPROVER_assume(pos <= n);
  PT t; if (pt_init(&t, orig, n)) return 0;
  /* preallocate so the run never reallocs (sizes stay concrete) */
  t.add = malloc(8); if (!t.add) return 0; t.add_cap = 8;
  if (!t.p) { t.p = malloc(16 * sizeof(Piece)); if (!t.p) return 0; t.cap = 16; }
  /* a second, earlier edit makes the table non-trivial */
  size_t p0 = nondet_size(); __CPROVER_assume(p0 <= n);
  if (pt_insert(&t, p0, "Z", 1)) return 0;
  if (pt_delete(&t, p0, 1, 0)) return 0;
  if (pt_insert(&t, pos, s, k)) return 0;
  assert(t.len == n + k);
  char mid[7]; assert(pt_read(&t, 0, mid, t.len) == n + k);
  assert(memcmp(mid + pos, s, k) == 0);
  if (pt_delete(&t, pos, k, 0)) return 0;
  char out[4]; assert(pt_read(&t, 0, out, t.len) == n);
  assert(memcmp(out, orig, n) == 0);
  pt_free(&t);
  return 0;
}
