#include "pt.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
char nondet_char(void);
int main(void) {
  char orig[4]; size_t n = 3;
  for (int i = 0; i < 4; i++) orig[i] = nondet_char();
  char s[3]; size_t k = 2;
  for (int i = 0; i < 3; i++) s[i] = nondet_char();
  size_t pos = POS, p0 = P0;
  PT t; if (pt_init(&t, orig, n)) return 0;
  t.add = malloc(8); if (!t.add) return 0; t.add_cap = 8;
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
