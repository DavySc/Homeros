/* pdbq <file.pdb>: prints what dbg/pdb.c reads, one record per line, for tests/oracle_pdb.py to
   compare with llvm-pdbutil. */
#include "../dbg/pdb.h"
#include "../dbg/le.h"
#include "../dbg/load.h"

int main(int argc, char **argv)
{
  uint64_t size;
  void *buf = argc == 2 ? load(argv[1], &size) : 0;
  Pdb p;
  if (!buf || !pdb_open(&p, buf, size)) return 2;
  PdbModule m;
  for (uint32_t at = 0, i = 0; pdb_module(&p, &at, &m); i += 1)
    printf("module %u stream %u first %u:%u:%u:%u name %.*s obj %.*s\n", i, m.stream, m.first.mod, m.first.sec,
           m.first.off, m.first.size, (int)m.name.n, (const char *)m.name.p, (int)m.obj.n, (const char *)m.obj.p);
  for (uint32_t at = 0, i = 0; pdb_module(&p, &at, &m); i += 1) {
    PdbSpan syms, c13;
    uint8_t *stream = pdb_module_stream(&p, &m, &syms, &c13);
    PdbLineBlock b;
    for (PdbLineCursor c = {0}; pdb_line_block(&p, c13, &c, &b);) {
      printf("lines %u %.*s %u:", i, (int)b.file.n, (const char *)b.file.p, b.sum_kind);
      for (uint32_t j = 0; j < b.sum.n; j += 1) printf("%02X", b.sum.p[j]);
      printf(" %llx-%llx", (unsigned long long)b.base, (unsigned long long)b.opl);
      for (uint32_t j = 0; j < b.count; j += 1) printf(" %u@%llx", le32(b.lines + 8 * j + 4) & 0xffffff, (unsigned long long)b.base + le32(b.lines + 8 * j));
      putchar('\n');
    }
    free(stream);
  }
  for (uint32_t i = 0; i < pdb_contribs(&p); i += 1) {
    PdbContrib c = pdb_contrib(&p, i);
    printf("contrib %u:%u:%u:%u voff %llx\n", c.mod, c.sec, c.off, c.size, (unsigned long long)pdb_voff(&p, c.sec, c.off));
  }
  for (uint32_t i = 1; i <= p.sections; i += 1) {
    PdbSection s = pdb_section(&p, i);
    printf("section %u %.*s %x %x %x %x %x\n", i, (int)s.name.n, (const char *)s.name.p, s.vsize, s.voff, s.fsize, s.foff, s.flags);
  }
  pdb_close(&p);
  free(buf);
  return 0;
}
