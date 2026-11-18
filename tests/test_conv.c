/* The vmap builder against its definition on random ranges, nested or not: at every offset the
   answer is the covering range that sorts last, 0 where none covers, and keys strictly ascend. */
#define rdi_from_pdb rdi_from_pdb_under_test   /* conv.c is compiled into this file to reach vmap_build */
#include "../dbg/conv.c"
#include <stdio.h>

int main(void)
{
  uint32_t seed = 1;
  for (int round = 0; round < 200000; round += 1) {
    Buf ranges = {0}, out = {0};
    uint64_t r[12][4];
    int n = 0;
    for (int count = (seed = seed * 1103515245 + 12345) >> 16 & 7; n < count; n += 1) {
      uint64_t first = (seed = seed * 1103515245 + 12345) >> 16 & 31, size = 1 + ((seed = seed * 1103515245 + 12345) >> 16 & 15);
      r[n][0] = first, r[n][1] = ~(first + size), r[n][2] = (uint64_t)n + 1, r[n][3] = 0;
      rec(&ranges, r[n][0], r[n][1], r[n][2], 0);
    }
    vmap_build(&out, &ranges);
    const RdiVMap *v = (const RdiVMap *)out.p;
    uint64_t entries = out.n / sizeof *v;
    for (uint64_t i = 1; i < entries; i += 1)
      if (v[i - 1].voff >= v[i].voff) { printf("round %d: keys do not ascend\n", round); return 1; }
    for (uint64_t voff = 0; voff < 50; voff += 1) {
      uint64_t want = 0, got = 0;
      const uint64_t *best = 0;
      for (int i = 0; i < n; i += 1)
        if (r[i][0] <= voff && voff < ~r[i][1] && (!best || rec_cmp(r[i], best) > 0)) best = r[i], want = r[i][2];
      for (uint64_t i = 0; i + 1 < entries; i += 1) if (v[i].voff <= voff) got = v[i].idx;   /* the last entry only ends the map */
      if (entries && voff >= v[entries - 1].voff) got = 0;
      if (got != want) { printf("round %d offset %llu: want %llu, got %llu\n", round, (unsigned long long)voff, (unsigned long long)want, (unsigned long long)got); return 1; }
    }
    free(ranges.p);
    free(out.p);
  }
  printf("vmap: 200000 random range sets agree with the definition\n");
  return 0;
}
