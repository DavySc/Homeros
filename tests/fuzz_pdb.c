/* libFuzzer: any bytes as a PDB; open it and read every module, contribution and section.
   Property: no read outside the streams. */
#include "../dbg/pdb.h"
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  uint8_t *buf = malloc(size ? size : 1);
  memcpy(buf, data, size);
  Pdb p;
  if (pdb_open(&p, buf, size)) {
    volatile uint64_t sum = 0;
    PdbModule m;
    for (uint32_t at = 0; pdb_module(&p, &at, &m);) {
      for (uint32_t i = 0; i < m.name.n; i += 1) sum += m.name.p[i];
      for (uint32_t i = 0; i < m.obj.n; i += 1) sum += m.obj.p[i];
      sum += m.stream + m.first.size;
    }
    for (uint32_t i = 0; i < pdb_contribs(&p); i += 1) {
      PdbContrib c = pdb_contrib(&p, i);
      sum += pdb_voff(&p, c.sec, c.off) + c.size + c.mod;
    }
    for (uint32_t i = 0; i <= p.sections + 1; i += 1) {
      PdbSection s = pdb_section(&p, i);
      for (uint32_t j = 0; j < s.name.n; j += 1) sum += s.name.p[j];
    }
    pdb_close(&p);
  }
  free(buf);
  return 0;
}
