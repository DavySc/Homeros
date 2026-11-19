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
      const RdiInlineSite *site = rdi_at(&r, RDI_INLINE_SITES, RdiInlineSite, rdi_at(&r, RDI_SCOPES, RdiScope, rdi_vmap(&r, RDI_SCOPE_VMAP, voff))->inline_site);
      sum += rdi_str(&r, site->name).n + rdi_line_in(&r, site->line_table, voff).line;
    }
    /* a type is made only of types before it, so reading one through always ends */
    for (uint64_t i = 1; i < r.s[RDI_TYPE_NODES].n; i += 1) {
      const RdiType *t = rdi_at(&r, RDI_TYPE_NODES, RdiType, i);
      RdiSpan params = rdi_run(&r, RDI_INDEX_RUNS, t->c, t->kind == 0x1005 || t->kind == 0x1006 ? t->b : 0);
      if ((t->kind < 0x1000 ? 0 : t->kind < 0x2000 || t->kind == 0xf000 ? t->a : t->b) >= i) abort();
      for (uint64_t k = 0; k < params.n; k += 1) if (((const uint32_t *)params.p)[k] >= i) abort();
    }
    /* each record of members is the one its type names, and the records share out the two member tables in order */
    uint64_t next[2] = {1, 1};
    for (uint64_t i = 1; i < r.s[RDI_UDTS].n; i += 1) {
      const RdiUdt *u = rdi_at(&r, RDI_UDTS, RdiUdt, i);
      if (rdi_at(&r, RDI_TYPE_NODES, RdiType, u->self_type)->c != i || (u->member_count && u->member_first != next[u->flags & 1])) abort();
      next[u->flags & 1] += u->member_count;
    }
    if (next[0] != r.s[RDI_MEMBERS].n || next[1] != r.s[RDI_ENUM_MEMBERS].n) abort();
    free(rdi);
  }
  free(buf);
  return 0;
}
