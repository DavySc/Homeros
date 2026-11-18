/* libFuzzer: any bytes as a PDB through the converter, then the reader over what it wrote.
   Properties: the converter never reads outside the PDB, and what it writes always opens. */
#include "../dbg/conv.h"
#include "../dbg/rdi.h"
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  uint8_t *buf = malloc(size ? size : 1);
  memcpy(buf, data, size);
  uint64_t n;
  uint8_t *rdi = rdi_from_pdb(buf, size, &n);
  if (rdi) {
    Rdi r;
    char path[256];
    volatile uint64_t sum = 0;
    if (!rdi_open(&r, rdi, n)) abort();
    for (uint64_t voff = 0x1000; voff < 0x1400; voff += 7) {
      RdiLine l = rdi_line_from_voff(&r, voff);
      RdiSpan v = rdi_voffs_from_line(&r, l.file, l.line);
      for (uint64_t i = 0; i < v.n; i += 1) sum += ((const uint64_t *)v.p)[i];
      sum += rdi_path(&r, rdi_at(&r, RDI_SOURCE_FILES, RdiSourceFile, l.file)->path_node, path, sizeof path);
      const RdiSymbol *y = rdi_at(&r, RDI_PROCEDURES, RdiSymbol, rdi_at(&r, RDI_SCOPES, RdiScope, rdi_vmap(&r, RDI_SCOPE_VMAP, voff))->proc);
      sum += rdi_str(&r, y->name).n + rdi_str(&r, y->link_name).n;
    }
    free(rdi);
  }
  free(buf);
  return 0;
}
