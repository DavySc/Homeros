/* libFuzzer: any bytes as an RDI file, then every lookup with keys taken from the input.
   The property is the header's promise: after rdi_open, no read outside the file. */
#include "../dbg/rdi.h"
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  /* an exact-size heap copy, so ASan sees one byte past the file */
  uint64_t *buf = malloc(size ? size : 1);
  memcpy(buf, data, size);
  Rdi r;
  if (rdi_open(&r, buf, size)) {
    uint64_t key[4] = {0};
    memcpy(key, data + (size > 64 ? size - 32 : 0), size < 32 ? size : 32);
    volatile uint64_t sum = 0;
    for (int k = 0; k < RDI_SECTIONS; k += 1) sum += *(const uint8_t *)rdi_elem(&r, k, key[0] % 97);
    for (int i = 0; i < 4; i += 1) {
      RdiLine l = rdi_line_from_voff(&r, key[i]);
      RdiSpan v = rdi_voffs_from_line(&r, l.file ? l.file : (uint32_t)key[i], (uint32_t)(key[i] >> 32));
      for (uint64_t j = 0; j < v.n; j += 1) sum += ((const uint64_t *)v.p)[j];
      RdiSpan s = rdi_str(&r, (uint32_t)key[i]);
      for (uint64_t j = 0; j < s.n; j += 1) sum += ((const uint8_t *)s.p)[j];
      sum += rdi_vmap(&r, RDI_SCOPE_VMAP, key[i]) + rdi_vmap(&r, RDI_GLOBAL_VMAP, key[i]);
      RdiSpan m = rdi_matches(&r, (uint32_t)(key[i] % 12), (const char *)data, size < 16 ? size : 16);
      for (uint64_t j = 0; j < m.n; j += 1) sum += ((const uint32_t *)m.p)[j];
    }
  }
  free(buf);
  return 0;
}
