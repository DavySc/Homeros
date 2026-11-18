/* Known answers on the fixture written by seed_rdi.py (argv[1]). */
#include "../dbg/rdi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failed;
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); failed = 1; } } while (0)

int main(int argc, char **argv)
{
  FILE *f = argc > 1 ? fopen(argv[1], "rb") : 0;
  if (!f) return 2;
  uint64_t *buf = malloc(1 << 16);
  uint64_t n = fread(buf, 1, 1 << 16, f);
  Rdi r;
  CHECK(rdi_open(&r, buf, n));
  CHECK(!rdi_open(&r, buf, n - 8));              /* the last section no longer fits (4 bytes of it are padding) */
  CHECK(rdi_open(&r, buf, n));
  RdiSpan s = rdi_str(&r, 1);
  CHECK(s.n == 3 && memcmp(s.p, "a.c", 3) == 0);
  CHECK(rdi_str(&r, 2).n == 4 && rdi_str(&r, 3).n == 5 && rdi_str(&r, 4).n == 0 && rdi_str(&r, 0).n == 0);
  /* a string table that runs backwards yields the empty string (the section table starts at byte 24,
     32 bytes a section, the offset 8 bytes in) */
  uint32_t *table = (uint32_t *)((uint8_t *)buf + ((uint64_t *)((uint8_t *)buf + 24 + 3 * 32 + 8))[0]);
  table[1] = 5;
  CHECK(rdi_str(&r, 1).n == 0);
  table[1] = 0;
  /* lines: 0x1000 -> 10, 0x1010 -> 11 (two entries, 11 then 13: the last is taken), 0x1020 -> 12, 0x1030 ends */
  CHECK(rdi_line_from_voff(&r, 0x0fff).line == 0);
  CHECK(rdi_line_from_voff(&r, 0x1000).line == 10 && rdi_line_from_voff(&r, 0x1000).file == 1);
  CHECK(rdi_line_from_voff(&r, 0x100f).line == 10);
  CHECK(rdi_line_from_voff(&r, 0x1010).line == 13);
  CHECK(rdi_line_from_voff(&r, 0x1011).line == 13);
  CHECK(rdi_line_from_voff(&r, 0x102f).line == 12);
  CHECK(rdi_line_from_voff(&r, 0x1030).line == 0);
  CHECK(rdi_line_from_voff(&r, UINT64_MAX).line == 0);
  /* line -> voffs: exact, the next line when there is none, the last line past the end */
  RdiSpan v = rdi_voffs_from_line(&r, 1, 11);
  CHECK(v.n == 1 && *(const uint64_t *)v.p == 0x1010);
  v = rdi_voffs_from_line(&r, 1, 3);
  CHECK(v.n == 1 && *(const uint64_t *)v.p == 0x1000);
  v = rdi_voffs_from_line(&r, 1, 99);
  CHECK(v.n == 1 && *(const uint64_t *)v.p == 0x1020);
  CHECK(rdi_voffs_from_line(&r, 0, 11).n == 0 && rdi_voffs_from_line(&r, 7, 11).n == 0);
  /* paths: built from the leaf back to the root, cut to the buffer; a ring of nodes ends */
  char path[8];
  CHECK(rdi_path(&r, 2, path, 8) == 8 && memcmp(path, "main/a.c", 8) == 0);
  CHECK(rdi_path(&r, 2, path, 4) == 8 && memcmp(path, "main", 4) == 0 && rdi_path(&r, 0, path, 8) == 0);
  CHECK(rdi_path(&r, 3, path, 8) > 8);
  CHECK(rdi_vmap(&r, RDI_GLOBAL_VMAP, 0x2000) == 1 && rdi_vmap(&r, RDI_GLOBAL_VMAP, 0x2010) == 0 && rdi_vmap(&r, RDI_GLOBAL_VMAP, 0x1fff) == 0);
  /* names */
  RdiSpan m = rdi_matches(&r, RDI_NAMES_PROCEDURES, "main", 4);
  CHECK(m.n == 1 && *(const uint32_t *)m.p == 1);
  CHECK(rdi_matches(&r, RDI_NAMES_PROCEDURES, "mai", 3).n == 0);
  CHECK(rdi_matches(&r, RDI_NAMES_TYPES, "main", 4).n == 0);
  CHECK(rdi_at(&r, RDI_PROCEDURES, RdiSymbol, 1)->name == 2 && rdi_at(&r, RDI_PROCEDURES, RdiSymbol, 9)->name == 0);
  free(buf);
  fclose(f);
  puts(failed ? "test_rdi FAILED" : "test_rdi ok");
  return failed;
}
