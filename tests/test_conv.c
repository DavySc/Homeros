/* A scope covers nothing when it has no address or no bytes. The vmap builder against its definition on random ranges, nested or not: at every offset the
   answer is the covering range that sorts last, 0 where none covers, and keys strictly ascend. */
#define rdi_from_pdb rdi_from_pdb_under_test   /* conv.c is compiled into this file to reach vmap_build */
#include "../dbg/conv.c"
#include <stdio.h>

int main(void)
{
  setvbuf(stdout, 0, _IONBF, 0);   /* a failure returns without freeing, and the leak report must not swallow its message */
  Out o = {0};
  pair(&o.s[RDI_SCOPE_VOFFS], 0, 16), pair(&o.s[RDI_SCOPE_VOFFS], 16, 16), pair(&o.s[RDI_SCOPE_VOFFS], 16, 32), pair(&o.s[RDI_SCOPE_VOFFS], 40, 48);
  scope(&o, 1, 0, 0, 0);
  const RdiScope *made = (const RdiScope *)o.s[RDI_SCOPES].p;
  const uint64_t *kept = (const uint64_t *)o.s[RDI_SCOPE_VOFFS].p;
  if (o.scopes.n != 64 || o.s[RDI_SCOPE_VOFFS].n != 32 || made->voff_first != 0 || made->voff_opl != 4 || kept[0] != 16 || kept[1] != 32 || kept[2] != 40 || kept[3] != 48) { printf("scope: pairs without address or bytes are not dropped\n"); return 1; }
  /* What an inlined call covers, from annotations written by hand (each line: the annotations, the base, the pairs):
     which lengths move the offset, where a piece starts, a piece that continues the one before, the end closing
     what is left, 2-byte and 4-byte operands, and the malformed ones, which add nothing. */
  static const struct { uint8_t n, a[12]; uint64_t base; uint8_t pairs; uint64_t want[6]; } sites[] = {
    {3, {11, 0x24, 0}, 0x1000, 1, {0x1004, 0x1004}},                           /* offset +4 and a line, then the end: an empty piece */
    {6, {3, 8, 4, 16, 3, 0}, 0x1000, 1, {0x1008, 0x1018}},                     /* +8, length 16; an offset at its end adds nothing */
    {8, {3, 8, 4, 4, 3, 4, 4, 2}, 0x1000, 2, {0x1008, 0x100c, 0x1010, 0x1012}},/* after a length the next offset counts from the end of the piece */
    {6, {12, 4, 8, 12, 2, 6}, 0x1000, 2, {0x1008, 0x100c, 0x100e, 0x1010}},    /* after a length with an offset, from the start of the piece (RAD: from its end) */
    {7, {11, 0x24, 11, 0x23, 12, 5, 2}, 0x1000, 1, {0x1004, 0x100e}},          /* a piece starts at the first offset since the last piece */
    {7, {3, 8, 4, 16, 12, 4, 0, 0}, 0x1000, 1, {0x1008, 0x101c}},              /* a second piece that starts where the first ended is one piece */
    {7, {3, 8, 4, 16, 12, 4, 8}, 0x1000, 2, {0x1008, 0x1018, 0x1020, 0x1024}}, /* a gap of 8 makes two */
    {5, {3, 0x81, 0x00, 4, 2}, 0x1000, 1, {0x1100, 0x1102}},                   /* a 2-byte operand: offset 0x100 */
    {8, {3, 0xc0, 0x01, 0x00, 0x00, 4, 2, 0}, 0x1000, 1, {0x11000, 0x11002}},  /* a 4-byte operand: offset 0x10000 */
    {4, {3, 8, 3, 8}, 0x1000, 1, {0x1008, 0x1010}},                            /* offsets with no length: up to the last of them, at the end of the record */
    {5, {3, 8, 3, 8, 0}, 0x1000, 1, {0x1008, 0x1010}},                         /* or at the end annotation */
    {6, {3, 8, 5, 7, 3, 4}, 0x1000, 1, {0x1008, 0x100c}},                      /* a new file moves nothing */
    {2, {3, 0x81}, 0x1000, 0, {0}},                                            /* an operand cut short */
    {4, {3, 8, 4, 0xe0}, 0x1000, 0, {0}},                                      /* a first byte no operand starts with */
    {6, {3, 8, 4, 16, 7, 1}, 0x1000, 0, {0}},                                  /* an annotation not decoded: the earlier piece goes too */
    {3, {2, 0, 0}, 0x1000, 0, {0}},                                            /* code moved out of the procedure */
    {5, {1, 8, 4, 4, 0}, 0x1000, 0, {0}},                                      /* an offset that is not a step */
    {10, {3, 0xdf, 0xff, 0xff, 0xff, 4, 0xdf, 0xff, 0xff, 0xff}, 0x1000, 1, {0x1000 + 0x1fffffffull, 0x1000 + 0x3ffffffeull}},   /* large, and still inside 32 bits */
  };
  for (unsigned i = 0; i < sizeof sites / sizeof *sites; i += 1) {
    Buf out = {0};
    pair(&out, 1, 0x1008);                                                     /* what was there stays, though a piece may start at its end */
    uint8_t *a = memcpy(malloc(sites[i].n), sites[i].a, sites[i].n);           /* exactly as long, so that reading past it is seen */
    inline_pieces(&out, (PdbSpan){a, sites[i].n}, sites[i].base);
    const uint64_t *v = (const uint64_t *)out.p;
    if (out.n != 16 + 16 * (uint64_t)sites[i].pairs || memcmp(v + 2, sites[i].want, out.n - 16) != 0 || v[0] != 1 || v[1] != 0x1008) { printf("inline site %u: wrong pieces\n", i); return 1; }
    free(a);
    free(out.p);
  }
  free(o.s[RDI_SCOPES].p), free(o.s[RDI_SCOPE_VOFFS].p), free(o.scopes.p);
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
